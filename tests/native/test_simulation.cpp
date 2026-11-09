#include "simulation.hpp"

#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}
using namespace ecu::simulation;
bool same(const Frame& left, const Frame& right) {
    return left.session == right.session && left.sequence == right.sequence &&
        left.rpm == right.rpm && left.speed10 == right.speed10 && left.temp10 == right.temp10 &&
        left.tps10 == right.tps10 && left.gear == right.gear && left.oil == right.oil &&
        left.faults == right.faults;
}
void rejected(const char* line, const char* message) {
    Frame output{99, 88, 77, 66, 55, 44, 3, 2, 16};
    const auto before = output;
    check(!parseFrame(line, output), message);
    check(same(before, output), "invalid input preserves every output field");
}
}

int main() {
    std::uint32_t token = 123;
    check(parseSessionToken("1", token) && token == 1, "minimum stop token accepted");
    check(parseSessionToken("4294967295", token) && token == 4294967295U,
          "maximum stop token accepted");
    for (const char* value : {static_cast<const char*>(nullptr), "", "0", "4294967296", "-1", "+1", " 1", "1 ",
                              "1\n", "1\t", "1 2", "1garbage", "1.0", "1e3", "0x1"}) {
        token = 123;
        check(!parseSessionToken(value, token) && token == 123,
              "invalid stop token rejected without modifying output");
    }
    Frame frame;
    check(parseFrame("SIM1 305419896 1 850 123 -400 1000 -1 2 16", frame),
          "Python golden frame parses negative temperature and reverse gear");
    check(same(frame, Frame{305419896, 1, 850, 123, -400, 1000, -1, 2, 16}),
          "golden frame preserves integer units and every field");
    check(parseFrame("SIM1 4294967295 4294967295 12000 2500 2000 1000 -1 2 251", frame),
          "maximum identifiers and payload bounds parse without signed overflow");
    check(same(frame, Frame{4294967295U, 4294967295U, 12000, 2500, 2000, 1000, -1, 2, 251}),
          "maximum golden frame matches the independent Python fixture");
    check(parseFrame("SIM1 1 1 0 0 -400 0 0 0 0", frame), "lower payload boundaries accepted");
    check(parseFrame("SIM1 1 1 12000 2500 2000 1000 4 2 255", frame) == false,
          "all flags cannot combine open and short temperature faults");
    for (const char* line : {
             "SIM1 0 1 0 0 0 0 0 0 0", "SIM1 1 0 0 0 0 0 0 0 0",
             "SIM1 4294967296 1 0 0 0 0 0 0 0", "SIM1 1 4294967296 0 0 0 0 0 0 0",
             "SIM1 -1 1 0 0 0 0 0 0 0", "SIM1 1 -1 0 0 0 0 0 0 0",
             "SIM1 1 1 -1 0 0 0 0 0 0", "SIM1 1 1 12001 0 0 0 0 0 0",
             "SIM1 1 1 0 -1 0 0 0 0 0", "SIM1 1 1 0 2501 0 0 0 0 0",
             "SIM1 1 1 0 0 -401 0 0 0 0", "SIM1 1 1 0 0 2001 0 0 0 0",
             "SIM1 1 1 0 0 -2147483648 0 0 0 0", "SIM1 1 1 0 0 -4294967296 0 0 0 0",
             "SIM1 1 1 0 0 0 -1 0 0 0", "SIM1 1 1 0 0 0 1001 0 0 0",
             "SIM1 1 1 0 0 0 0 -2 0 0", "SIM1 1 1 0 0 0 0 5 0 0",
             "SIM1 1 1 0 0 0 0 0 -1 0", "SIM1 1 1 0 0 0 0 0 3 0",
             "SIM1 1 1 0 0 0 0 0 0 -1", "SIM1 1 1 0 0 0 0 0 0 256",
             "SIM1 1 1 0 0 0 0 0 0 12", "SIM1 1 1 0 0 0 0 0 0 99999999999999999999999"}) {
        rejected(line, "out-of-range integer or conflicting fault rejected");
    }
    rejected(nullptr, "null line rejected");
    rejected("", "empty line rejected");
    rejected("SIM2 1 1 0 0 0 0 0 0 0", "unknown protocol version rejected");
    rejected("sim1 1 1 0 0 0 0 0 0 0", "prefix is case-sensitive");
    rejected(" SIM1 1 1 0 0 0 0 0 0 0", "leading space rejected");
    rejected("SIM1 1 1 0 0 0 0 0 0 0 ", "trailing space rejected");
    rejected("SIM1  1 1 0 0 0 0 0 0 0", "empty field from repeated space rejected");
    rejected("SIM1\t1 1 0 0 0 0 0 0 0", "tab cannot delimit fields");
    rejected("SIM1 +1 1 0 0 0 0 0 0 0", "noncanonical plus sign rejected");
    rejected("SIM1 1 1 1.0 0 0 0 0 0 0", "decimal value rejected");
    rejected("SIM1 1 1 1e3 0 0 0 0 0 0", "exponent rejected");
    rejected("SIM1 1 1 0x1 0 0 0 0 0 0", "hexadecimal notation rejected");
    rejected("SIM1 1 1 0 0 0 0 0 0", "missing field rejected");
    rejected("SIM1 1 1 0 0 0 0 0 0 0 1", "extra field rejected");
    rejected("SIM1 1 1 0 0 0 0 0 0 0garbage", "trailing garbage rejected");
    rejected("SIM1 1 1 0 0 0 0 0 0 0\n", "framer must remove LF before parsing");
    rejected("SIM1 1 1 0 0 0 0 0 0 0\r", "CR cannot become payload data");
    rejected("SIM1 1 1 0 0 0 0 0 0 \xFF", "non-ASCII byte rejected");
    const std::string shortLine = "SIM1 9 1 0 0 0 0 0 0 0";
    const auto maximumLine = std::string("SIM1 ") + std::string(95 - shortLine.size(), '0') + shortLine.substr(5);
    check(maximumLine.size() == 95 && parseFrame(maximumLine.c_str(), frame),
          "95-byte line fits without truncating its fields");
    rejected((std::string("SIM1 0") + maximumLine.substr(5)).c_str(), "96-byte line rejected");

    State state;
    auto status = state.snapshot(0);
    check(!status.enabled && !status.live && same(status.frame, Frame{}), "boot is off with cleared payload");
    const Frame first{123, 1, 850, 123, 850, 500, 1, 0, None};
    check(!state.accept(first, 0), "frames cannot start a session implicitly");
    check(!state.start(0, 10), "zero session token rejected");
    check(state.start(123, 10), "explicit nonzero token starts session");
    status = state.snapshot(10);
    check(status.enabled && !status.live && status.frame.session == 123 && status.frame.sequence == 0,
          "session waits for a frame before becoming live");
    check(!state.stop(0) && !state.stop(999) && state.snapshot(10).enabled,
          "wrong-token stop cannot terminate an active session");
    check(state.accept(first, 20) && state.snapshot(20).live, "first valid frame becomes live");
    check(state.snapshot(769).live && !state.snapshot(770).live,
          "TTL is live through749ms and stale at750ms");
    status = state.snapshot(900);
    check(status.enabled && !status.live && same(status.frame, first),
          "stale preserves session/sequence metadata but never reports live");
    check(!state.accept(first, 900) && !state.snapshot(900).live,
          "replaying a frame cannot refresh the stale TTL");
    auto next = first;
    next.sequence = 2;
    check(state.accept(next, 900) && state.snapshot(900).live, "new frame resumes the same stale session");
    auto invalid = next;
    invalid.sequence = 3;
    invalid.temp10 = -401;
    check(!state.accept(invalid, 1000) && same(state.snapshot(1000).frame, next),
          "direct typed submit is range-checked and preserves last accepted frame");
    invalid = next;
    invalid.sequence = 3;
    invalid.faults = TempOpen | TempShort;
    check(!state.accept(invalid, 1000), "direct typed submit rejects conflicting faults");
    invalid = next;
    invalid.sequence = 3;
    invalid.session = 456;
    check(!state.accept(invalid, 1000), "another session token cannot update data or timestamp");
    check(!state.accept(first, 1000), "older sequence rejected while live");
    check(!state.start(123, 1000) && state.snapshot(1000).frame.sequence == 2,
          "repeated start cannot reset the current session replay guard");
    check(!state.start(0, 1000) && state.snapshot(1000).frame.sequence == 2,
          "invalid start preserves an existing session");
    check(!state.snapshot(1650).live, "invalid input cannot extend a valid frame TTL");
    check(state.start(456, 2000) && !state.snapshot(2000).live,
          "new token clears previous measurements and sequence watermark");
    next.session = 456;
    next.sequence = 1;
    check(state.accept(next, 2000), "sequence restarts only after an explicit new session");
    check(!state.accept(first, 2000), "old session remains rejected after replacement");
    check(state.stop(456) && !state.stop(456), "stop requires the currently active token");
    status = state.snapshot(2000);
    check(!status.enabled && !status.live && same(status.frame, Frame{}), "stop clears token and all payload data");

    check(state.start(789, 0xfffffff0U), "session starts before millis rollover");
    next.session = 789;
    next.sequence = 1;
    check(state.accept(next, 0xfffffff0U), "frame accepted before millis rollover");
    check(state.snapshot(0x2ddU).live && !state.snapshot(0x2deU).live,
          "TTL subtraction survives the uint32 millis rollover");
    next.sequence = 4294967295U;
    check(state.accept(next, 0x300U), "last uint32 sequence is accepted");
    next.sequence = 1;
    check(!state.accept(next, 0x301U), "sequence wrap cannot reopen old-frame replay within a session");
    next.sequence = 0;
    check(!state.accept(next, 0x302U), "zero sequence is always invalid");

    State expired;
    Frame retained{42, 1, 850, 0, 850, 0, 0, 0, None};
    check(expired.start(42, 100) && expired.accept(retained, 100) && expired.snapshot(849).live,
          "rollover regression starts from a live frame");
    check(!expired.snapshot(850).live, "first observed TTL expiration marks the frame stale");
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        check(!expired.snapshot(0xfffffff0U).live && !expired.snapshot(100).live &&
                  !expired.snapshot(849).live && !expired.snapshot(850).live,
              "an observed stale frame cannot revive across any subsequent millis cycle");
    }
    check(!expired.accept(retained, 100) && !expired.start(42, 100) && !expired.snapshot(100).live,
          "replay and repeated session start cannot clear the expiration latch");
    retained.sequence = 2;
    retained.temp10 = -401;
    check(!expired.accept(retained, 100) && !expired.snapshot(100).live,
          "invalid replacement cannot clear the expiration latch");
    retained.temp10 = 850;
    check(expired.accept(retained, 100) && expired.snapshot(100).live && expired.snapshot(849).live,
          "a valid newer frame explicitly clears the expiration latch");
    check(!expired.snapshot(850).live, "resumed frame expires again at its own TTL boundary");
    retained.session = 43;
    retained.sequence = 1;
    check(expired.start(43, 100) && !expired.snapshot(100).live && expired.accept(retained, 100) &&
              expired.snapshot(100).live,
          "a new session clears old expiration state but still requires its own valid frame");
    if (failures) return EXIT_FAILURE;
    std::puts("simulation: protocol golden, integer bounds, sessions, replay and TTL rollover PASS");
    return EXIT_SUCCESS;
}
