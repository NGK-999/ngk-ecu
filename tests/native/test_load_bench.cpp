#include "load_bench_policy.hpp"
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace {
unsigned failures = 0;
#define CHECK(expression) do { if (!(expression)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); ++failures; } } while (false)
using namespace ecu::loadbench;

void parserRejectsInvalidWithoutChangingOutput() {
    Request request{23, 9000, 1};
    for (const auto* line : {"", "load-start", "load-start 0 0 1000", "load-start -1 0 1000",
            "load-start +1 0 1000", "load-start 1 3 1000", "load-start 1 1 999",
            "load-start 1 1 120001", "load-start 4294967296 0 1000", "load-start 1 256 1000",
            "load-start 1 0 4294967296", "load-start 1 0 1000 extra", "load-start  1 0 1000",
            "load-start 1 0 1000 ", "load-start 1 0 1000\n", "load-start 1\t0 1000"}) {
        CHECK(!parseRequest(line, request));
        CHECK(request.id == 23 && request.durationMs == 9000 && request.mode == 1);
    }
    CHECK(!parseRequest(nullptr, request));
    CHECK(parseRequest("load-start 4294967295 0 1000", request));
    CHECK(request.id == 4294967295U && request.mode == 0 && request.durationMs == 1000);
    CHECK(parseRequest("load-start 1 2 120000", request));
    CHECK(request.id == 1 && request.mode == 2 && request.durationMs == 120000);
    char oversized[130]; std::memset(oversized, '1', sizeof(oversized)); oversized[129] = 0;
    CHECK(!parseRequest(oversized, request));
}

void busyRequestCannotReplaceActiveJobAndStopIsIdempotent() {
    Control control;
    CHECK(control.stop() && control.state == State::Idle);
    CHECK(!control.start(Request{0, 1000, 1}));
    CHECK(control.start(Request{5, 1000, 1}));
    CHECK(control.state == State::Queued);
    CHECK(!control.start(Request{6, 2000, 2}));
    CHECK(control.request.id == 5 && control.request.mode == 1);
    control.state = State::Running;
    CHECK(!control.start(Request{6, 2000, 2}));
    CHECK(control.stop() && control.state == State::Stopping);
    CHECK(control.stop() && control.state == State::Stopping);
    CHECK(!control.start(Request{6, 2000, 2}));
    control.state = State::Stopped;
    CHECK(control.stop() && control.state == State::Stopped);
    CHECK(control.start(Request{6, 2000, 2}));
}

void schedulerSkipsExpiredPeriodsAndStopsAt64BitDeadline() {
    Schedule schedule;
    constexpr std::uint64_t start = 0x100000000ULL;
    schedule.begin(start, Request{1, 1000, 1});
    CHECK(schedule.dispatch(start));
    CHECK(!schedule.dispatch(start + 9999));
    CHECK(schedule.dispatch(start + 10000));
    CHECK(schedule.dispatch(start + 45000));
    CHECK(schedule.missed == 2 && schedule.maxLateUs == 25000);
    CHECK(schedule.nextUs == start + 50000);
    CHECK(!schedule.dispatch(start + 45000));
    CHECK(!schedule.dispatch(start + 1000000));
    schedule.finish(start + 1000000);
    CHECK(schedule.missed == 97); // [start, end): three produced, 97 missed.
    schedule.finish(start + 1000000);
    CHECK(schedule.missed == 97);
    schedule.begin(start, Request{1, 1000, 0});
    CHECK(!schedule.dispatch(start + 500000));
    schedule.finish(start + 1000000);
    CHECK(schedule.missed == 0 && schedule.periodUs == 0);
    schedule.begin(start, Request{1, 1000, 2});
    CHECK(schedule.dispatch(start + 1000));
    CHECK(schedule.nextUs == start + 2000);
    for (const std::uint8_t mode : {1, 2}) {
        schedule.begin(start, Request{1, 1000, mode});
        const auto period = mode == 1 ? 10000U : 1000U;
        for (std::uint32_t offset = 0; offset < 1000000; offset += period) {
            CHECK(schedule.dispatch(start + offset));
            CHECK(!schedule.dispatch(start + offset));
        }
        schedule.finish(start + 1000000);
        CHECK(schedule.missed == 0 && schedule.maxLateUs == 0);
    }
}

void mailboxTracksReplacementAndFinalDrainExactly() {
    Mailbox mailbox;
    Work output;
    CHECK(!mailbox.take(100, output));
    mailbox.publish(100, 10);
    mailbox.publish(200, 20);
    mailbox.publish(300, 30);
    CHECK(mailbox.produced == 3 && mailbox.consumed == 0 && mailbox.overwritten == 2 && mailbox.pending);
    CHECK(mailbox.sequenceGaps == 0); // Gaps are observed only on consumption.
    CHECK(mailbox.take(450, output));
    CHECK(output.sequence == 3 && output.checksum == 30 && output.readyUs == 300);
    CHECK(mailbox.consumed == 1 && mailbox.sequenceGaps == 2 && mailbox.consumeMaxUs == 150);
    CHECK(!mailbox.pending && !mailbox.take(1000, output));
    mailbox.publish(500, 40);
    CHECK(mailbox.produced == mailbox.consumed + mailbox.overwritten + unsigned(mailbox.pending));
    CHECK(mailbox.take(520, output)); // Main may drain the final pending item after stop.
    CHECK(mailbox.produced == 4 && mailbox.consumed == 2 && mailbox.overwritten == 2 && !mailbox.pending);
    CHECK(mailbox.consumeMaxUs == 150 && mailbox.sequenceGaps == mailbox.overwritten);
}

void kernelIsDeterministicAndControlDoesNoWork() {
    CHECK(kernel(0x12345678, config(0)) == 0x12345678);
    CHECK(config(1).channels == 16 && config(1).rateHz == 100 && config(1).iterations == 32);
    CHECK(config(2).channels == 32 && config(2).rateHz == 1000 && config(2).iterations == 128);
    // One channel, one integer iteration: rotate(1 xor 0x9e3779b9, 5) + 0x7f4a7c15.
    CHECK(kernel(1, Config{1, 1, 1}) == 0x4639b328U);
    CHECK(kernel(1, config(1)) != kernel(2, config(1)));
    CHECK(kernel(1, config(1)) != kernel(1, config(2)));
}
}

int main() {
    parserRejectsInvalidWithoutChangingOutput();
    busyRequestCannotReplaceActiveJobAndStopIsIdempotent();
    schedulerSkipsExpiredPeriodsAndStopsAt64BitDeadline();
    mailboxTracksReplacementAndFinalDrainExactly();
    kernelIsDeterministicAndControlDoesNoWork();
    if (!failures) std::puts("load benchmark policy: all checks passed");
    return failures ? 1 : 0;
}
