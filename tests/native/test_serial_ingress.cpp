#include "serial_ingress.hpp"
#include "serial_tx.hpp"
#include "simulation.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace {
void check(bool okay, const char* message) {
    if (!okay) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

template<std::size_t Capacity = 256> struct Input {
    std::array<unsigned char, Capacity> bytes{};
    std::size_t begin = 0, used = 0, dropped = 0, peak = 0;
    void receive(unsigned char byte) {
        if (used == Capacity) { ++dropped; return; }
        bytes[(begin + used++) % Capacity] = byte;
        peak = std::max(peak, used);
    }
    void receive(const char* value) {
        while (*value) receive(static_cast<unsigned char>(*value++));
    }
    std::size_t available() const { return used; }
    int read() {
        if (!used) return -1;
        const auto result = bytes[begin];
        begin = (begin + 1) % Capacity;
        --used;
        return result;
    }
};

// Historical main.cpp policy is retained only as the counterexample. The
// candidate below exercises the same SerialIngress used by production.
class PreviousIngress {
public:
    template<class Dispatch, class Reject>
    void poll(Input<>& input, const ecu::SerialTxBuffer<2048>& tx,
              Dispatch dispatch, Reject reject) {
        if (tx.pending()) return;
        for (unsigned count = 0; count < 128 && input.available(); ++count) {
            const auto byte = char(input.read());
            const auto result = parser.push(byte);
            if (result == ecu::CommandLine::Ready) dispatch(parser.value());
            else if (result == ecu::CommandLine::Rejected) reject();
            if (byte == '\n') return;
        }
    }
private:
    ecu::CommandLine parser;
};

struct ModelResult {
    std::size_t dropped = 0, peak = 0;
    unsigned frames = 0, loadCalls = 0, statusCalls = 0;
    bool repliedInTime = false;
};

template<class Ingress> ModelResult model(unsigned rate) {
    struct Byte { std::uint32_t at; unsigned char value; };
    struct Write { std::uint32_t at; std::string value; };
    constexpr std::uint32_t durationUs = 2500000, requestUs = 175000;
    std::vector<Write> writes;
    for (unsigned seq = 1; seq <= rate * 5 / 2; ++seq) {
        writes.push_back({(seq - 1) * 1000000 / rate,
            "SIM1 2147483647 " + std::to_string(seq) + " 2450 325 850 400 2 0 0\n"});
    }
    writes.push_back({100000, "load-status\n"});
    writes.push_back({requestUs, "sim-status\n"});
    std::stable_sort(writes.begin(), writes.end(), [](const auto& a, const auto& b) { return a.at < b.at; });
    std::vector<Byte> wire;
    std::uint32_t wireEnd = 0;
    for (const auto& write : writes) {
        wireEnd = std::max(wireEnd, write.at);
        for (const auto byte : write.value) {
            // 115200 baud, 8N1: 86.81 us/byte, rounded up conservatively.
            wireEnd += 87;
            wire.push_back({wireEnd, static_cast<unsigned char>(byte)});
        }
    }

    Input<> input;
    ecu::SerialTxBuffer<2048> tx;
    Ingress ingress;
    ecu::simulation::State simulation;
    check(simulation.start(2147483647, 0), "model starts a synthetic session");
    std::array<char, 900> longReply{};
    longReply.fill('x');
    longReply.back() = '\n';
    std::string received;
    ModelResult result;
    std::size_t next = 0;
    for (std::uint32_t now = 0; now < durationUs; now += 6000) {
        while (next < wire.size() && wire[next].at <= now) input.receive(wire[next++].value);
        const auto dispatch = [&](const char* line) {
            if (!std::strncmp(line, "SIM1 ", 5)) {
                ecu::simulation::Frame frame;
                if (ecu::simulation::parseFrame(line, frame) && simulation.accept(frame, now / 1000)) {
                    ++result.frames;
                } else check(tx.append("{\"kind\":\"sim\",\"state\":\"rejected\",\"protocol\":1}\r\n"), "SIM rejection fits");
            } else if (!std::strcmp(line, "load-status")) {
                ++result.loadCalls;
                check(tx.append("%.*s", int(longReply.size()), longReply.data()), "long control reply fits");
            } else if (!std::strcmp(line, "sim-status")) {
                ++result.statusCalls;
                check(tx.append("STATUS\n"), "status reply fits");
            } else check(tx.append("ERROR command rejected\r\n"), "unknown command reply fits");
        };
        ingress.poll(input, tx, dispatch, [&] { check(tx.append("ERROR invalid command line\r\n"), "invalid line reply fits"); });
        const auto count = tx.chunkSize(128);
        received.append(tx.data(), count);
        tx.consume(count);
        if (received.find("STATUS\n") != std::string::npos && now <= requestUs + 2000000) result.repliedInTime = true;
    }
    result.dropped = input.dropped;
    result.peak = input.peak;
    return result;
}

void reproduces_rx_overflow_and_preserves_control_at_100_hz() {
    for (const auto rate : {20U, 50U, 100U}) {
        const auto previous = model<PreviousIngress>(rate);
        if (rate == 100) {
            check(previous.dropped > 0 && previous.peak == 256 && !previous.repliedInTime,
                  "historical TX gate reproduces RX256 overflow and a two-second control timeout at 100 Hz");
        } else check(previous.dropped == 0 && previous.repliedInTime, "historical model completes at 20/50 Hz");
        const auto current = model<ecu::SerialIngress>(rate);
        check(current.dropped == 0 && current.frames == rate * 5 / 2 && current.repliedInTime,
              "production ingress drains SIM through a long TX reply without losing the control request");
        check(current.loadCalls == 1 && current.statusCalls == 1, "control requests execute exactly once");
        std::printf("RX256 model %u Hz: old dropped=%zu, control=%s; ingress dropped=%zu, peak=%zu, frames=%u\n",
            rate, previous.dropped, previous.repliedInTime ? "reply" : "timeout", current.dropped, current.peak, current.frames);
    }
}

void defers_side_effects_until_the_complete_ack_fits() {
    Input<> input;
    input.receive("start\nSIM1 7 1 2450 325 850 400 2 0 0\n");
    ecu::SerialTxBuffer<2048> tx;
    std::array<char, 1500> pending{};
    pending.fill('p');
    check(tx.append("%.*s", int(pending.size()), pending.data()), "seed pending transmission");
    ecu::SerialIngress ingress;
    unsigned effects = 0, frames = 0;
    auto dispatch = [&](const char* line) {
        if (!std::strcmp(line, "start")) { ++effects; check(tx.append("ACK\n"), "effect has space for ACK"); }
        else { check(effects == 1, "SIM does not overtake deferred control"); ++frames; }
    };
    auto reject = [] { check(false, "valid input is not rejected"); };
    check(ingress.poll(input, tx, dispatch, reject) == 6 && effects == 0 && frames == 0,
          "one complete command is retained before effects or later SIM are processed");
    check(ingress.poll(input, tx, dispatch, reject) == 0 && effects == 0, "blocked retry cannot repeat effects");
    tx.consume(600);
    ingress.poll(input, tx, dispatch, reject);
    ingress.poll(input, tx, dispatch, reject);
    check(effects == 1 && frames == 1 && std::strstr(tx.data(), "ACK\n"), "one resumed effect keeps its complete ACK");
}

void preserves_invalid_line_rejection_under_backpressure() {
    Input<> input;
    for (const auto byte : std::array<unsigned char, 14>{'b','a','d',0,'s','t','a','r','t','\n','o','k','\r','\n'}) input.receive(byte);
    ecu::SerialTxBuffer<2048> tx;
    std::array<char, 1984> pending{};
    pending.fill('p');
    check(tx.append("%.*s", int(pending.size()), pending.data()), "leave only 63 bytes free");
    ecu::SerialIngress ingress;
    unsigned effects = 0, rejections = 0;
    auto dispatch = [&](const char* line) {
        check(!std::strcmp(line, "ok"), "invalid suffix never becomes a command");
        ++effects;
        check(tx.append("ACK\n"), "valid following command keeps ACK");
    };
    auto reject = [&] { ++rejections; check(tx.append("ERROR invalid command line\r\n"), "parser rejection is visible"); };
    ingress.poll(input, tx, dispatch, reject);
    check(rejections == 0 && effects == 0, "no invalid input is forgotten while its error cannot fit");
    tx.consume(1);
    ingress.poll(input, tx, dispatch, reject);
    check(rejections == 1 && effects == 0, "64-byte reservation admits one complete rejection");
    tx.consume(1024);
    ingress.poll(input, tx, dispatch, reject);
    check(rejections == 1 && effects == 1 && std::strstr(tx.data(), "ERROR invalid command line\r\nACK\n"),
          "CRLF resynchronizes after NUL and both replies retain order");
}

void reserves_full_control_reply_and_retains_a_sim_rejection() {
    Input<> input;
    input.receive("info\nSIM1 invalid\n");
    ecu::SerialTxBuffer<2048> tx;
    std::array<char, 1023> preceding{};
    preceding.fill('p');
    std::array<char, 996> reply{};
    reply.fill('r');
    check(tx.append("%.*s", int(preceding.size()), preceding.data()), "leave exactly 1024 bytes for a control reply");
    ecu::SerialIngress ingress;
    unsigned queries = 0, rejectedFrames = 0;
    auto dispatch = [&](const char* line) {
        if (!std::strcmp(line, "info")) {
            ++queries;
            check(tx.append("%.*s", int(reply.size()), reply.data()), "996-byte conservative INFO maximum is queued atomically");
        } else {
            ecu::simulation::Frame frame;
            check(!ecu::simulation::parseFrame(line, frame), "fixture is a rejected SIM frame");
            ++rejectedFrames;
            check(tx.append("{\"kind\":\"sim\",\"state\":\"rejected\",\"protocol\":1}\r\n"), "SIM rejection fits its reservation");
        }
    };
    auto reject = [] { check(false, "SIM syntax is rejected by its command handler"); };
    ingress.poll(input, tx, dispatch, reject);
    check(queries == 1 && rejectedFrames == 0 && tx.pending() == 2019,
          "a nearly full outbox retains the following SIM frame until its rejection can fit");
    tx.consume(64);
    ingress.poll(input, tx, dispatch, reject);
    check(queries == 1 && rejectedFrames == 1 && std::strstr(tx.data(), "{\"kind\":\"sim\",\"state\":\"rejected\",\"protocol\":1}\r\n"),
          "a rejected SIM frame remains visible exactly once after one TX chunk");
}

void preserves_session_order_and_does_not_refresh_ttl_without_a_frame() {
    Input<> input;
    input.receive("sim-start 9\nSIM1 9 1 2450 325 850 400 2 0 0\nsim-stop 9\nSIM1 9 2 2450 325 850 400 2 0 0\n");
    ecu::SerialTxBuffer<2048> tx;
    ecu::SerialIngress ingress;
    ecu::simulation::State simulation;
    std::uint32_t nowMs = 1000;
    unsigned accepted = 0, refused = 0;
    auto dispatch = [&](const char* line) {
        if (!std::strcmp(line, "sim-start 9")) {
            check(simulation.start(9, nowMs) && tx.append("ready\n"), "session starts before following SIM");
        } else if (!std::strcmp(line, "sim-stop 9")) {
            check(simulation.stop(9) && tx.append("off\n"), "session stops before following SIM");
        } else {
            ecu::simulation::Frame frame;
            if (ecu::simulation::parseFrame(line, frame) && simulation.accept(frame, nowMs)) ++accepted;
            else { ++refused; check(tx.append("rejected\n"), "old-session frame is visibly rejected"); }
        }
    };
    auto reject = [] { check(false, "session fixture contains valid ASCII lines"); };
    ingress.poll(input, tx, dispatch, reject);
    check(accepted == 1 && refused == 1 && !simulation.snapshot(nowMs).enabled &&
          !std::strcmp(tx.data(), "ready\noff\nrejected\n"), "batched commands preserve session boundaries and reply order");
    tx.consume(tx.pending());
    input.receive("sim-start 9\nSIM1 9 1 2450 325 850 400 2 0 0\n");
    ingress.poll(input, tx, dispatch, reject);
    nowMs = 1749;
    ingress.poll(input, tx, dispatch, reject);
    check(simulation.snapshot(nowMs).live, "accepted synthetic frame stays valid until TTL");
    nowMs = 1750;
    ingress.poll(input, tx, dispatch, reject);
    check(!simulation.snapshot(nowMs).live && simulation.snapshot(nowMs).acceptedAtMs == 1000,
          "empty ingress polls do not extend the frame TTL");
}

void drains_text_before_dispatching_binary_capture() {
    Input<> input;
    input.receive("info\nscreen-capture\nSIM1 9 1 2450 325 850 400 2 0 0\n");
    ecu::SerialTxBuffer<2048> tx;
    ecu::SerialIngress ingress;
    unsigned queries = 0, captures = 0, frames = 0;
    bool captureSawPendingText = false;
    std::string wire;
    auto dispatch = [&](const char* line) {
        if (!std::strcmp(line, "info")) {
            ++queries;
            check(tx.append("TEXT\n"), "query queues a complete text response");
        } else if (!std::strcmp(line, "screen-capture")) {
            ++captures;
            captureSawPendingText = tx.pending() != 0;
            wire.append("BINARY"); // Synchronous capture's direct UART output, in RAM only.
        } else {
            check(captures == 1, "later SIM cannot overtake pending binary capture");
            ++frames;
        }
    };
    auto reject = [] { check(false, "capture fixture uses valid command lines"); };
    ingress.poll(input, tx, dispatch, reject);
    check(queries == 1 && captures == 0 && frames == 0,
          "exact screen-capture waits for preceding text even when its reply reservation fits");
    wire.append(tx.data(), 2);
    tx.consume(2);
    ingress.poll(input, tx, dispatch, reject);
    check(captures == 0 && frames == 0, "partial text transmission cannot start capture");
    wire.append(tx.data(), tx.pending());
    tx.consume(tx.pending());
    ingress.poll(input, tx, dispatch, reject);
    ingress.poll(input, tx, dispatch, reject);
    check(captures == 1 && !captureSawPendingText && frames == 1 && wire == "TEXT\nBINARY",
          "complete text precedes exactly one capture and following SIM retains order");

    input.receive("screen-capture extra\n");
    check(tx.append("still pending\n"), "seed text before a different control command");
    unsigned ordinaryControls = 0;
    ingress.poll(input, tx, [&](const char* line) {
        check(!std::strcmp(line, "screen-capture extra"), "capture barrier matches only the exact command");
        ++ordinaryControls;
    }, reject);
    check(ordinaryControls == 1 && tx.pending() != 0, "other controls retain normal TX-space admission");
}

void bounds_work_and_keeps_the_95_character_parser() {
    Input<1024> input;
    for (unsigned i = 0; i < 300; ++i) input.receive('\n');
    input.receive("ok\n");
    ecu::SerialTxBuffer<2048> tx;
    ecu::SerialIngress ingress;
    unsigned accepted = 0, rejected = 0;
    auto dispatch = [&](const char*) { ++accepted; };
    auto reject = [&] { ++rejected; };
    check(ingress.poll(input, tx, dispatch, reject) == 256 && input.available() == 47 && accepted == 0,
          "a newline flood cannot exceed the 256-byte iteration budget");
    check(ingress.poll(input, tx, dispatch, reject) == 47 && accepted == 1, "next iteration processes the remaining terminal line");
    for (unsigned i = 0; i < 95; ++i) input.receive('x');
    input.receive('\n');
    for (unsigned i = 0; i < 96; ++i) input.receive('x');
    input.receive('\n');
    ingress.poll(input, tx, dispatch, reject);
    check(accepted == 2 && rejected == 1, "95 characters remain valid; 96 are rejected through LF");
    static_assert(std::is_trivially_copyable_v<ecu::SerialIngress>);
    static_assert(sizeof(ecu::SerialIngress) <= sizeof(ecu::CommandLine) + 8,
                  "ingress stores only the existing fixed parser and pending state");
}
}

int main() {
    reproduces_rx_overflow_and_preserves_control_at_100_hz();
    defers_side_effects_until_the_complete_ack_fits();
    preserves_invalid_line_rejection_under_backpressure();
    reserves_full_control_reply_and_retains_a_sim_rejection();
    preserves_session_order_and_does_not_refresh_ttl_without_a_frame();
    drains_text_before_dispatching_binary_capture();
    bounds_work_and_keeps_the_95_character_parser();
    std::puts("Bounded serial ingress: PASS");
}
