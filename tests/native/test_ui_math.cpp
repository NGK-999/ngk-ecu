#include "ui_motion.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
void check(bool condition, const char* why) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}

// Frozen original formula: independent of the optimized phase reduction.
class OriginalFrameClock {
public:
    bool due(std::uint32_t now, ecu::RefreshMode mode) {
        if (!started_ || mode != mode_) {
            started_ = true; mode_ = mode; previous_ = now; phase_ = 0;
            return true;
        }
        const auto elapsed = std::uint32_t(now - previous_);
        previous_ = now;
        const std::uint32_t units = mode == ecu::RefreshMode::Smooth ? 60 : 1;
        const std::uint32_t threshold = mode == ecu::RefreshMode::Smooth ? 1000 :
            (mode == ecu::RefreshMode::Economy ? 50 : 33);
        const auto accumulated = std::uint64_t(phase_) + std::uint64_t(elapsed) * units;
        phase_ = static_cast<std::uint32_t>(accumulated % threshold);
        return accumulated >= threshold;
    }
    void reset() { *this = OriginalFrameClock{}; }
private:
    std::uint32_t previous_ = 0, phase_ = 0;
    ecu::RefreshMode mode_ = ecu::RefreshMode::Smooth;
    bool started_ = false;
};

void clockOracle() {
    constexpr std::array modes{ecu::RefreshMode::Smooth, ecu::RefreshMode::Balanced, ecu::RefreshMode::Economy};
    constexpr std::array<std::uint32_t, 12> deltas{
        0, 1, 16, 17, 32, 33, 49, 50, 1000, 0x7fffffffU, 0xfffffffeU, 0xffffffffU};
    ecu::ui::FrameClock actual;
    OriginalFrameClock expected;
    const auto compare = [&](std::uint32_t now, ecu::RefreshMode mode) {
        check(actual.due(now, mode) == expected.due(now, mode), "FrameClock differs from the original uint64 oracle");
    };
    for (auto mode : modes) {
        for (auto start : {0U, 0xfffffff0U}) {
            actual.reset(); expected.reset();
            std::uint32_t now = start;
            compare(now, mode);
            for (unsigned step = 0; step < 120000; ++step) {
                now += step & 1U;  // Repeated timestamp and1ms progress.
                compare(now, mode);
            }
            for (auto delta : deltas) {
                now += delta;
                compare(now, mode);
                // A wrong remainder can hide on the large jump itself.
                for (unsigned step = 0; step < 100; ++step) compare(++now, mode);
            }
        }
    }
    std::uint32_t seed = 0x48e3fc71U, now = 0xffffff80U;
    auto mode = ecu::RefreshMode::Smooth;
    for (unsigned step = 0; step < 200000; ++step) {
        seed = seed * 1664525U + 1013904223U;
        if (step % 997U == 0) { actual.reset(); expected.reset(); }
        if (step % 131U == 0) mode = modes[(seed >> 24U) % modes.size()];
        now += deltas[(seed >> 16U) % deltas.size()];
        compare(now, mode);
        compare(now, mode);  // No catch-up at the same timestamp.
    }
}

void singleRpmSample() {
    // The UI reuses one post-retarget sample for its ramp and numeric slot.
    // Verify the old second sample cannot change value or settlement semantics.
    ecu::ui::ScalarMotion original, reused;
    original.snap(800, 7); reused.snap(800, 7);
    for (unsigned now = 0; now < 220; ++now) {
        const auto retarget = [&](float value, std::uint32_t duration, ecu::ui::MotionOptions options) {
            original.retarget(value, now, duration, options);
            reused.retarget(value, now, duration, options);
        };
        if (now == 0) retarget(4000, 100, {7, true, false, false});
        if (now == 40) retarget(2000, 40, {7, true, false, false});
        if (now == 50) retarget(9000, 100, {7, true, false, true});
        if (now == 60) retarget(9000, 100, {7, false, false, true});
        if (now == 70) retarget(3000, 100, {8, true, false, false});
        if (now == 80) retarget(4000, 100, {8, true, true, false});
        if (now == 90 || now == 100) retarget(6000, 100, {8, true, false, false});
        const auto oldRamp = original.value(now);
        const auto oldNumber = original.value(now);
        const auto shared = reused.value(now);
        check(shared == oldRamp && shared == oldNumber &&
                  std::lround(shared) == std::lround(oldNumber) &&
                  reused.animating(now) == original.animating(now),
              "one RPM sample differs from repeated sampling for urgency, validity or terminal value");
    }
}

#ifndef NGK_CLOCK_ORACLE_ONLY
unsigned originalRamp(float rpm, std::uint16_t scale) {
    return static_cast<unsigned>(std::lround(std::clamp(rpm, 0.0F, float(scale)) * (77824.0F / scale)));
}

void rampOracle() {
    ecu::ui::RpmRampScale ramp;
    check(ramp.update(8000), "the first scale configures its factor");
    check(!ramp.update(8000), "an unchanged scale reuses its existing factor");
    for (std::uint16_t scale = 1000; scale <= 12000; scale += 500) {
        check(ramp.update(scale), "a changed scale replaces the cached factor");
        check(!ramp.update(scale), "repeated scale must not recompute the factor");
        const auto compare = [&](float rpm) {
            check(ramp.progress(rpm) == originalRamp(rpm, scale), "cached RPM factor changes rounded Q8 progress");
        };
        for (unsigned rpm = 0; rpm <= 12000; ++rpm) compare(float(rpm));
        for (float rpm : {-std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                          -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::denorm_min(), std::numeric_limits<float>::denorm_min(),
                          -0.0F, 0.0F, float(scale)}) compare(rpm);
        const float factor = 77824.0F / scale;
        for (unsigned q8 = 0; q8 < 77824; q8 += 127) {
            const float boundary = (float(q8) + 0.5F) / factor;
            compare(boundary);
            compare(std::nextafter(boundary, -std::numeric_limits<float>::infinity()));
            compare(std::nextafter(boundary, std::numeric_limits<float>::infinity()));
        }
    }
    ramp.reset();
    check(ramp.update(12000) && ramp.progress(12000) == 77824U && ramp.progress(-1) == 0U,
          "page reset rebuilds the scale and keeps exact clamp endpoints");
}
#endif
}

int main() {
    clockOracle();
    singleRpmSample();
#ifndef NGK_CLOCK_ORACLE_ONLY
    rampOracle();
    std::puts("UI math: original-clock oracle, repeated-sample equivalence and exact cached Q8 progress PASS");
#else
    std::puts("UI math: original-clock oracle and repeated-sample equivalence PASS");
#endif
}
