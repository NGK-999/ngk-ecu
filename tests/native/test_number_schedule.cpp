#include "ui_motion.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {
constexpr std::uint8_t none = 0, rpm = 1, speed = 2, both = rpm | speed;
void check(bool condition, const char* why) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}

#ifdef NGK_TEST_COMBINED_BASELINE
// Real previous policy, used to observe RED before implementing staggered slots.
class Schedule {
public:
    void request(std::uint8_t fields) { pending_ |= fields; }
    std::uint8_t pending() const { return pending_; }
    void reset() { pending_ = 0; clock_.reset(); }
    void presented(std::uint8_t selected, std::uint8_t moving) {
        pending_ = (pending_ & ~selected) | (selected & moving);
    }
    std::uint8_t select(std::uint32_t now, bool frameDue, ecu::RefreshMode mode,
                        bool, bool urgent, bool = true) {
        if (urgent) { request(both); return both; }
        const auto numericMode = mode == ecu::RefreshMode::Smooth ? ecu::RefreshMode::Balanced : mode;
        return pending_ && frameDue && clock_.due(now, numericMode) ? both : none;
    }
private:
    std::uint8_t pending_ = 0;
    ecu::ui::FrameClock clock_;
};
#else
using Schedule = ecu::ui::NumberSchedule;
#endif

void balancedSlots() {
    using ecu::RefreshMode;
    Schedule schedule;
    ecu::ui::FrameClock frames;
    schedule.request(both);
    unsigned rpmFrames = 0, speedFrames = 0, numericFrames = 0;
    for (unsigned now = 0; now < 60000; ++now) {
        const auto selected = schedule.select(now, frames.due(now), RefreshMode::Smooth, false, false);
        check(selected != both, "Smooth moving frames must not batch the two numbers");
        if (selected) ++numericFrames;
        if (selected & rpm) ++rpmFrames;
        if (selected & speed) ++speedFrames;
        schedule.presented(selected, selected);  // Both trajectories remain active.
    }
    check(rpmFrames == 1800 && speedFrames == 1800 && numericFrames == 3600,
          "Smooth 60Hz allocates exactly 30Hz to each number over a minute");

    schedule.reset(); frames.reset(); schedule.request(rpm);
    rpmFrames = speedFrames = 0;
    for (unsigned now = 0; now < 1000; ++now) {
        const auto selected = schedule.select(now, frames.due(now), RefreshMode::Smooth, false, false);
        rpmFrames += bool(selected & rpm); speedFrames += bool(selected & speed);
        schedule.presented(selected, selected);
    }
    check(rpmFrames == 30 && speedFrames == 0, "a lone dirty RPM keeps 30Hz without borrowing speed slots");
    schedule.reset(); schedule.request(speed);
    check(schedule.select(0, true, RefreshMode::Smooth, false, false) == none &&
              schedule.select(1, false, RefreshMode::Smooth, false, false) == none &&
              schedule.select(17, true, RefreshMode::Smooth, false, false) == speed &&
              schedule.select(34, true, RefreshMode::Smooth, false, false) == none &&
              schedule.select(50, true, RefreshMode::Smooth, false, false) == speed,
          "a lone dirty speed waits for its own alternating slot");
}

void pendingAndUrgency() {
    using ecu::RefreshMode;
    Schedule schedule;
    check(schedule.pending() == none &&
              schedule.select(0, true, RefreshMode::Smooth, false, false) == none,
          "idle scheduling emits no numeric work");
    schedule.request(both);
    check(schedule.select(1, false, RefreshMode::Smooth, false, false) == none &&
              schedule.select(17, true, RefreshMode::Smooth, false, false) == rpm,
          "only a due instrument frame advances the alternator");
    check(schedule.pending() == both, "choosing a slot cannot consume a pending terminal presentation");
    schedule.presented(rpm, none);
    check(schedule.pending() == speed, "settled RPM cannot erase speed's pending presentation");
    schedule.presented(speed, speed);
    check(schedule.pending() == speed, "a sampled active trajectory remains pending");
    check(schedule.select(18, false, RefreshMode::Smooth, false, true) == both,
          "urgency paints both immediately between instrument frames");
    schedule.presented(both, none);
    check(schedule.pending() == none, "both urgent terminal values settle independently");
    schedule.request(both);
    check(schedule.select(34, true, RefreshMode::Smooth, false, false) == rpm,
          "urgent painting resets the next normal slot to RPM");
}

void independentTerminalValues() {
    using ecu::RefreshMode;
    using ecu::ui::ScalarMotion;
    Schedule schedule;
    ecu::ui::FrameClock frames;
    ScalarMotion rpmMotion, speedMotion;
    rpmMotion.snap(0); speedMotion.snap(0);
    rpmMotion.retarget(100, 0, 40); speedMotion.retarget(200, 0, 90);
    schedule.request(both);
    unsigned rpmTerminal = 0, speedTerminal = 0;
    float lastRpm = -1, lastSpeed = -1;
    for (unsigned now = 0; now < 200; ++now) {
        const auto selected = schedule.select(now, frames.due(now), RefreshMode::Smooth, false, false);
        if (selected & rpm) {
            lastRpm = rpmMotion.value(now);
            if (lastRpm == 100) ++rpmTerminal;
        }
        if (selected & speed) {
            lastSpeed = speedMotion.value(now);
            if (lastSpeed == 200) ++speedTerminal;
        }
        const auto moving = (rpmMotion.animating(now) ? rpm : none) |
                            (speedMotion.animating(now) ? speed : none);
        schedule.presented(selected, moving);
        if (now == 67) check(schedule.pending() == speed, "speed remains pending after RPM's terminal slot");
    }
    check(lastRpm == 100 && lastSpeed == 200 && rpmTerminal == 1 && speedTerminal == 1 &&
              schedule.pending() == none,
          "different motion durations present each exact terminal value once then become idle");
}

void compatibilityAndPolicyChanges() {
    using ecu::RefreshMode;
    Schedule schedule;
    schedule.request(rpm);
    check(schedule.select(0, true, RefreshMode::Balanced, false, false) == both &&
              schedule.select(32, true, RefreshMode::Balanced, false, false) == none &&
              schedule.select(33, true, RefreshMode::Balanced, false, false) == both,
          "Balanced preserves combined numbers and its original 33ms clock");
    check(schedule.select(50, true, RefreshMode::Economy, false, false) == both &&
              schedule.select(99, true, RefreshMode::Economy, false, false) == none &&
              schedule.select(100, true, RefreshMode::Economy, false, false) == both,
          "Economy resets cadence safely and preserves combined numbers every 50ms");
    check(schedule.select(200, true, RefreshMode::Smooth, true, false) == both &&
              schedule.select(232, true, RefreshMode::Smooth, true, false) == none &&
              schedule.select(233, true, RefreshMode::Smooth, true, false) == both,
          "reduced motion preserves the previous combined Smooth numeric policy");
    check(schedule.select(300, true, RefreshMode::Smooth, false, false, false) == both &&
              schedule.select(332, true, RefreshMode::Smooth, false, false, false) == none &&
              schedule.select(333, true, RefreshMode::Smooth, false, false, false) == both,
          "the combined reference switch preserves the old numeric clock");
    schedule.request(speed);
    check(schedule.select(400, false, RefreshMode::Smooth, false, false) == none &&
              schedule.select(401, true, RefreshMode::Smooth, false, false) == rpm &&
              schedule.select(417, true, RefreshMode::Smooth, false, false) == speed,
          "returning to staggered mode preserves pending fields and restarts slot order");
}

void resetsAndDiscontinuousTime() {
    using ecu::RefreshMode;
    Schedule schedule;
    ecu::ui::FrameClock frames;
    schedule.request(both);
    check(schedule.select(0, frames.due(0), RefreshMode::Smooth, false, false) == rpm &&
              schedule.select(1000, frames.due(1000), RefreshMode::Smooth, false, false) == speed &&
              schedule.select(1000, frames.due(1000), RefreshMode::Smooth, false, false) == none &&
              schedule.select(1016, frames.due(1016), RefreshMode::Smooth, false, false) == none &&
              schedule.select(1017, frames.due(1017), RefreshMode::Smooth, false, false) == rpm,
          "late callbacks dispatch one slot and never catch up with a burst");
    schedule.reset(); frames.reset(); schedule.request(both);
    check(schedule.select(0xfffffff0U, frames.due(0xfffffff0U), RefreshMode::Smooth, false, false) == rpm &&
              schedule.select(0, frames.due(0), RefreshMode::Smooth, false, false) == none &&
              schedule.select(1, frames.due(1), RefreshMode::Smooth, false, false) == speed,
          "slot cadence survives uint32 rollover");

    ecu::ui::ScalarMotion motion;
    motion.snap(800, 7); motion.retarget(2000, 0, 100, {7, true, false, false});
    motion.retarget(900, 20, 100, {8, true, false, true});
    check(schedule.select(20, false, RefreshMode::Smooth, false, true) == both && motion.value(20) == 900,
          "new generation urgency presents both without blending an old source");
    motion.retarget(900, 21, 100, {8, false, false, true});
    check(schedule.select(21, false, RefreshMode::Smooth, false, true) == both && !motion.valid(),
          "TTL or invalidity bypasses the frame clock and cannot retain a numeric trajectory");
    schedule.presented(both, none);
    schedule.reset(); frames.reset(); schedule.request(both);
    check(schedule.select(100, frames.due(100), RefreshMode::Smooth, false, true) == both,
          "page or overlay reset repaints both immediately without replaying pending slots");
}
}

int main() {
    balancedSlots();
    pendingAndUrgency();
    independentTerminalValues();
    compatibilityAndPolicyChanges();
    resetsAndDiscontinuousTime();
    std::puts("Number schedule: staggered30/30Hz, independent terminal values, immediate urgency, "
              "legacy cadence, resets and no catch-up PASS");
}
