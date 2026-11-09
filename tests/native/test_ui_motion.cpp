#include "ui_motion.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
void check(bool condition, const char* why) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
bool near(float actual, float expected) { return std::fabs(actual - expected) < 0.001F; }
}

int main() {
    using namespace ecu;
    using namespace ecu::ui;
    check(adaptiveMotionDuration(0, 10000, 100, 40) == 100 &&
              adaptiveMotionDuration(100, 10000, 100, 40) == 100,
          "zero and one-percent changes retain the slow duration");
    check(adaptiveMotionDuration(550, 10000, 100, 40) == 70 &&
              adaptiveMotionDuration(-550, 10000, 100, 40) == 70,
          "midrange change interpolates duration independently of direction");
    check(adaptiveMotionDuration(1000, 10000, 100, 40) == 40 &&
              adaptiveMotionDuration(20000, 10000, 100, 40) == 40,
          "ten-percent and larger changes use the bounded fast duration");
    check(adaptiveMotionDuration(1, 100, 120, 60) == 120 &&
              adaptiveMotionDuration(10, 100, 120, 60) == 60,
          "the speed profile retains its explicit 120-to-60ms limits");
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto inf = std::numeric_limits<float>::infinity();
    check(adaptiveMotionDuration(nan, 100, 100, 40) == 100 &&
              adaptiveMotionDuration(inf, 100, 100, 40) == 100 &&
              adaptiveMotionDuration(10, nan, 100, 40) == 100 &&
              adaptiveMotionDuration(10, inf, 100, 40) == 100,
          "nonfinite input falls back to the bounded slow duration");
    check(adaptiveMotionDuration(10, 0, 100, 40) == 100 &&
              adaptiveMotionDuration(10, -100, 100, 40) == 100,
          "a nonpositive scale cannot create an invalid normalized delta");
    check(adaptiveMotionDuration(550, 10000, 40, 100) == 70 &&
              adaptiveMotionDuration(0, 100, 40, 100) == 100 &&
              adaptiveMotionDuration(10, 100, 40, 100) == 40,
          "reversed limits are ordered to keep small changes slower");
    check(adaptiveMotionDuration(5, 100, 0, 0) == 0 &&
              adaptiveMotionDuration(5, 100, 60, 60) == 60 &&
              adaptiveMotionDuration(0, 100, 0xffffffffU, 0) == 0xffffffffU &&
              adaptiveMotionDuration(550, 10000, 0xffffffffU, 0) == 0x80000000U &&
              adaptiveMotionDuration(10, 100, 0xffffffffU, 0) == 0,
          "equal, zero and full uint32 limits remain representable");
    check(adaptiveMotionDuration(std::numeric_limits<float>::max(),
                                 std::numeric_limits<float>::min(), 100, 40) == 40,
          "finite extreme changes cannot overflow normalization");
    check(adaptiveMotionDuration(0, std::numeric_limits<float>::denorm_min(), 100, 40) == 100 &&
              adaptiveMotionDuration(std::numeric_limits<float>::denorm_min(),
                                     std::numeric_limits<float>::denorm_min(), 100, 40) == 40,
          "subnormal scales preserve zero as slow and a full-scale change as fast");
    check(adaptiveMotionDuration(std::numeric_limits<float>::max() * 0.055F,
                                 std::numeric_limits<float>::max(), 100, 40) == 70 &&
              adaptiveMotionDuration(std::numeric_limits<float>::min() * 0.055F,
                                     std::numeric_limits<float>::min(), 100, 40) == 70,
          "midrange ratios remain usable at huge and subnormal delta magnitudes");
    check(adaptiveMotionDuration(550, 10000, 0x00ffffffU, 0) == 0x00800000U &&
              adaptiveMotionDuration(550, 10000, 0x01000000U, 0) == 0x00800000U,
          "float-to-wide duration transition preserves safe rounding and conversion");
    ScalarMotion state;
    const ScalarMotion& stateQuery = state;
    check(!stateQuery.animating(0), "an uninitialized presentation is resting");
    state.snap(0);
    check(!stateQuery.animating(0), "a snapped presentation is resting");
    state.retarget(100, 10, 100);
    check(stateQuery.animating(10) && stateQuery.animating(109) &&
              !stateQuery.animating(110) && !stateQuery.animating(10000),
          "motion is active until the exact end timestamp, even before sampling value");
    check(stateQuery.animating(60) && near(state.value(60), 87.5F),
          "a const future-state query cannot settle or mutate the trajectory");
    state.retarget(100, 60, 40);
    check(stateQuery.animating(109) && !stateQuery.animating(110),
          "repeated targets with a different duration do not postpone or shorten motion");
    check(state.value(110) == 100 && !stateQuery.animating(111),
          "the final value remains available after animating becomes false");
    state.retarget(200, 120, 100);
    state.reset();
    check(!stateQuery.animating(121), "reset discards active motion");
    state.snap(0);
    state.retarget(100, 0xfffffff0U, 100);
    check(stateQuery.animating(0xfffffff0U) && stateQuery.animating(0x53U) &&
              !stateQuery.animating(0x54U) && state.value(0x54U) == 100,
          "active-state query survives rollover and settles at the exact wrapped timestamp");
    for (unsigned mode = 0; mode < 5; ++mode) {
        state.snap(0);
        state.retarget(100, 0, 100);
        MotionOptions stop;
        if (mode == 0) stop.valid = false;
        if (mode == 1) stop.generation = 1;
        if (mode == 2) stop.reducedMotion = true;
        if (mode == 3) stop.urgent = true;
        state.retarget(200, 20, mode == 4 ? 0 : 100, stop);
        check(!stateQuery.animating(20),
              "stale, new generation, reduced motion, urgency and zero duration stop motion");
    }
    state.snap(0);
    state.retarget(nan, 0);
    check(!stateQuery.animating(0), "nonfinite targets cannot leave active motion");
    ScalarMotion motion;
    check(!motion.valid(), "uninitialized presentation has no valid numeric value");
    motion.retarget(800, 0);
    check(motion.valid() && motion.value(0) == 800,
          "the first valid sample appears immediately without rising from zero");
    motion.snap(0);
    motion.retarget(100, 10, 100);
    check(motion.value(10) == 0 && near(motion.value(35), 57.8125F) && near(motion.value(60), 87.5F),
          "cubic ease-out follows independent quarter and halfway fixtures");
    check(motion.value(110) == 100 && motion.value(10000) == 100,
          "completed motion settles exactly and stays settled");
    motion.snap(0);
    motion.retarget(100, 0, 100);
    motion.retarget(200, 50, 100);
    check(near(motion.value(50), 87.5F) && near(motion.value(100), 185.9375F),
          "a 20Hz retarget continues from the current display without a position jump");
    check(motion.value(150) == 200, "retarget reaches the new target exactly");
    motion.snap(0);
    motion.retarget(100, 0, 100);
    motion.retarget(100, 50, 100);
    check(motion.value(100) == 100, "repeated identical samples cannot restart an unfinished curve");
    motion.snap(100);
    motion.retarget(-100, 0, 120);
    float previous = 100;
    for (unsigned now = 0; now <= 150; ++now) {
        const auto current = motion.value(now);
        check(current <= previous && current >= -100 && current <= 100,
              "decreasing motion stays monotonic without overshoot");
        previous = current;
    }
    MotionOptions options{7, true, false, false};
    motion.retarget(1000, 200, 100, options);
    check(motion.value(200) == 1000, "a new source generation cannot blend from an old source");
    motion.retarget(2000, 210, 100, options);
    options.valid = false;
    motion.retarget(2000, 220, 100, options);
    check(!motion.valid(), "stale or invalid input discards the presentation state immediately");
    options.valid = true;
    motion.retarget(3000, 230, 100, options);
    check(motion.value(230) == 3000, "valid input after stale snaps without replaying old motion");
    motion.retarget(4000, 240, 100, options);
    options.reducedMotion = true;
    motion.retarget(4000, 250, 100, options);
    check(motion.value(250) == 4000, "reduced motion snaps even if the target itself did not change");
    options.reducedMotion = false;
    motion.retarget(5000, 260, 100, options);
    options.urgent = true;
    motion.retarget(9000, 270, 100, options);
    check(motion.value(270) == 9000, "an urgent raw warning bypasses visual interpolation");
    options.urgent = false;
    motion.retarget(10000, 280, 100, options);
    options.generation = 8;
    motion.retarget(850, 290, 100, options);
    check(motion.value(290) == 850, "new session resets an in-progress trajectory");
    motion.retarget(1200, 300, 0, options);
    check(motion.value(300) == 1200, "zero-duration motion is an immediate presentation");
    motion.retarget(std::numeric_limits<float>::quiet_NaN(), 301);
    check(!motion.valid(), "nonfinite targets cannot contaminate later display values");
    motion.snap(0);
    motion.retarget(100, 0xfffffff0U, 100);
    check(near(motion.value(0x22U), 87.5F) && motion.value(0x54U) == 100,
          "animation timing survives the uint32 millisecond boundary");
    motion.reset();
    motion.retarget(77, 123);
    check(motion.value(123) == 77, "explicit reset removes every previous trajectory");

    FrameClock clock;
    check(clock.due(0) && !clock.due(16) && clock.due(17) && !clock.due(33) &&
              clock.due(34) && clock.due(50) && !clock.due(50),
          "rational 60Hz uses 16/17ms intervals and never emits twice in one timestamp");
    clock.reset();
    unsigned presentations = 0;
    for (unsigned now = 0; now < 60000; ++now) if (clock.due(now)) ++presentations;
    check(presentations == 3600, "rational cadence does not accumulate fixed-17ms drift over a minute");
    clock.reset();
    check(clock.due(100) && clock.due(1000) && !clock.due(1000) && !clock.due(1016) && clock.due(1017),
          "late callbacks drop missed frames instead of replaying a backlog");
    clock.reset();
    check(clock.due(5000) && !clock.due(5000) && !clock.due(5016) && clock.due(5017),
          "pause/reset restarts from now with no deferred presentations");
    check(clock.due(6000, RefreshMode::Balanced) && !clock.due(6032, RefreshMode::Balanced) &&
              clock.due(6033, RefreshMode::Balanced), "Balanced preserves its selected33ms cadence");
    check(clock.due(7000, RefreshMode::Economy) && !clock.due(7049, RefreshMode::Economy) &&
              clock.due(7050, RefreshMode::Economy), "Economy preserves its selected50ms cadence");
    clock.reset();
    check(clock.due(0xfffffff0U) && !clock.due(0) && clock.due(1) && clock.due(18),
          "frame cadence handles uint32 wrap without catching up or stalling");
    std::puts("UI motion: adaptive duration, pure active-state query, bounded cubic interpolation, "
              "validity resets and rational cadence PASS");
}
