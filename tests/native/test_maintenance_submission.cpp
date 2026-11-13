#include "maintenance_policy.hpp"
#include "maintenance_submission.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>

namespace {
using Result = ecu::MaintenanceSubmission::Result;
constexpr uint32_t sectors = 4194304, token = 123456;

// In-memory transport and real authorization policy; never invokes a formatter.
struct Transport {
    ecu::MaintenanceSubmission submission;
    ecu::MaintenancePolicy policy;
    bool ready = true, full = false, pending = false;
    uint32_t queued = 0, completed = 0;
    unsigned physicalCalls = 0;
    unsigned replies = 0;

    Result send() {
        return submission.submit(ready, completed, [&](uint32_t id) {
            if (full || pending) return false;
            queued = id;
            pending = true;
            return true;
        });
    }

    void finish(const char* line, bool sameCard = true) {
        assert(pending);
        if (!sameCard) policy.invalidateCard();
        if (policy.request(line, sectors, token, sameCard)) ++physicalCalls;
        completed = queued;
        pending = false;
        ready = sameCard && !policy.consumed();
    }

    bool report(bool terminal = true, bool outboxAvailable = true) {
        return submission.report(completed, terminal, [&] {
            if (!outboxAvailable) return false;
            ++replies;
            return true;
        });
    }
};

void invalid_command_allows_corrected_retry() {
    for (const auto* invalid : {"FORMAT_SD 4194304 123455", "FORMAT_SD 4194305 123456",
                               "FORMAT_SD invalid"}) {
        Transport t;
        assert(t.send() == Result::Queued);
        assert(t.send() == Result::Refused); // Queue draining is not completion.
        t.finish(invalid);
        assert(t.ready && t.physicalCalls == 0);
        assert(t.send() == Result::Refused); // Result must precede the next request.
        assert(t.report());
        assert(t.send() == Result::Queued);
        assert(t.queued != t.completed);
        t.finish("FORMAT_SD 4194304 123456");
        assert(t.physicalCalls == 1 && t.policy.consumed());
        assert(t.report());
        assert(t.send() == Result::Refused); // Even if the real formatter fails.
    }
}

void only_matching_completion_releases_the_pending_request() {
    ecu::MaintenanceSubmission submission;
    uint32_t first = 0, second = 0;
    assert(submission.submit(true, 0, [&](uint32_t id) { first = id; return true; }) == Result::Queued);
    assert(submission.submit(true, first + 1, [](uint32_t) { assert(false); return true; }) == Result::Refused);
    assert(!submission.report(first + 1, true, [] { assert(false); return true; }));
    assert(submission.report(first, true, [] { return true; }));
    assert(submission.submit(true, first, [&](uint32_t id) { second = id; return true; }) == Result::Queued);
    assert(first != second && second != 0);
    assert(submission.submit(true, first, [](uint32_t) { assert(false); return true; }) == Result::Refused);
}

void queue_failure_does_not_latch_and_revocation_never_rearms() {
    Transport t;
    t.full = true;
    assert(t.send() == Result::QueueFull);
    t.full = false;
    assert(t.send() == Result::Queued);
    t.finish("FORMAT_SD 4194304 123456", false);
    assert(t.report());
    assert(!t.ready && t.physicalCalls == 0 && !t.policy.consumed());
    assert(t.send() == Result::Refused);
    assert(!t.policy.request("FORMAT_SD 4194304 123456", sectors, token, true));
}

void result_backpressure_never_loses_or_reorders_completion() {
    Transport t;
    assert(t.send() == Result::Queued);
    t.finish("FORMAT_SD invalid");
    assert(!t.report(true, false)); // Full text outbox, keep the result pending.
    assert(t.send() == Result::Refused);
    assert(!t.report(false)); // An active snapshot cannot report a result.
    assert(t.replies == 0);
    assert(t.report());
    assert(t.report()); // Repeated snapshot emits only once.
    assert(t.replies == 1);
    assert(t.send() == Result::Queued);
    // A stale completed ID from the first request cannot unlock the second.
    assert(t.report());
    assert(t.send() == Result::Refused);
    t.finish("FORMAT_SD invalid"); // Same failure text, distinct completion.
    assert(t.report());
    assert(t.replies == 2);
    assert(t.send() == Result::Queued);
}
}  // namespace

int main() {
    invalid_command_allows_corrected_retry();
    only_matching_completion_releases_the_pending_request();
    queue_failure_does_not_latch_and_revocation_never_rearms();
    result_backpressure_never_loses_or_reorders_completion();
    std::cout << "maintenance submission: 4 scenarios passed\n";
}
