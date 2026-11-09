#include "perf_stats.hpp"
#include <cstdio>
#include <cstdlib>

namespace {
void check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
}

int main() {
    ecu::ui::PerfWindow<4> empty;
    check(empty.size() == 0 && empty.count() == 0, "empty window has no observations");
    check(empty.min() == 0 && empty.max() == 0 && empty.p95() == 0,
          "empty statistics are defined as zero");
    empty.add(12);
    check(empty.count() == 1 && empty.min() == 12 && empty.max() == 12 && empty.p95() == 12,
          "one observation determines all statistics");
    ecu::ui::PerfWindow<20> ranks;
    for (unsigned value = 20; value; --value) ranks.add(value);
    check(ranks.p95() == 19, "p95 uses nearest rank independently of insertion order");
    ecu::ui::PerfWindow<4> ring;
    for (unsigned value : {1000U, 1U, 2U, 3U, 4U}) ring.add(value);
    check(ring.size() == 4 && ring.count() == 5, "ring bounds retained samples but counts all");
    check(ring.p95() == 4, "overwritten samples no longer affect rolling p95");
    check(ring.min() == 1 && ring.max() == 1000, "extrema cover the complete measured interval");
    ring.add(0);
    check(ring.min() == 0 && ring.p95() == 4, "zero duration is a valid retained observation");
    check(ecu::ui::elapsedUs(0x20U, 0xFFFFFFF0U) == 48,
          "microsecond intervals survive uint32 wraparound");
    std::puts("Bounded performance statistics: PASS");
}
