#pragma once
#include "settings.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ecu::ui {
// Presentation only: <=1% of full scale retains slowMs, >=10% uses fastMs.
// Interpolate linearly between those thresholds, rounding to the nearest ms.
// Reversed limits are ordered; invalid delta/scale falls back to the slow limit.
// Zero durations are allowed. No allocation or state changes are performed.
inline std::uint32_t adaptiveMotionDuration(float delta, float fullScale,
                                          std::uint32_t slowMs, std::uint32_t fastMs) {
    const auto slow = std::max(slowMs, fastMs);
    const auto fast = std::min(slowMs, fastMs);
    if (!std::isfinite(delta) || !std::isfinite(fullScale) || fullScale <= 0) return slow;
    const float magnitude = std::fabs(delta);
    // Classify before division: even float extremes cannot overflow the ratio.
    // Check zero/small first because subnormal scales can round both bounds to zero.
    if (magnitude <= fullScale * 0.01F) return slow;
    if (magnitude >= fullScale * 0.10F) return fast;
    if (slow <= 0x00ffffffU) {
        // Normal UI durations stay in float; double is emulated on classic ESP32.
        const float progress = (magnitude / fullScale - 0.01F) / 0.09F;
        const float duration = float(slow) - progress * float(slow - fast);
        return static_cast<std::uint32_t>(std::clamp(std::round(duration),
                                                   float(fast), float(slow)));
    }
    // Rare oversized durations need exact uint32 bounds before converting back.
    const double progress = (double(magnitude) / double(fullScale) - 0.01) / 0.09;
    const double duration = double(slow) - progress * double(slow - fast);
    return static_cast<std::uint32_t>(std::clamp(std::floor(duration + 0.5),
                                               double(fast), double(slow)));
}
struct MotionOptions {
    std::uint32_t generation = 0;
    bool valid = true, reducedMotion = false, urgent = false;
};
class ScalarMotion {
public:
    // Presentation only. Callers retain raw values for validity, TTL and alerts.
    // Use a source/session generation; reset or valid=false on loss of signal.
    void retarget(float target, std::uint32_t now, std::uint32_t duration = 100,
                  MotionOptions options = {}) {
        if (!options.valid || !std::isfinite(target)) { reset(); return; }
        if (!valid_ || generation_ != options.generation || options.reducedMotion ||
            options.urgent || !duration) {
            snap(target, options.generation);
            return;
        }
        if (target == target_) return;  // A repeated sample must not postpone settling.
        start_ = value(now);
        target_ = target;
        startedAt_ = now;
        duration_ = start_ == target_ ? 0 : duration;
    }
    float value(std::uint32_t now) {
        if (!valid_) return 0;  // Not a display sentinel: check valid() before formatting.
        if (!duration_) return target_;
        const auto elapsed = std::uint32_t(now - startedAt_);
        if (elapsed >= duration_) {
            start_ = target_;
            duration_ = 0;
            return target_;
        }
        const float remaining = 1.0F - float(elapsed) / float(duration_);
        const float eased = 1.0F - remaining * remaining * remaining;
        const float blended = (1.0F - eased) * start_ + eased * target_;
        if (!std::isfinite(blended)) return target_;
        return std::clamp(blended, std::min(start_, target_), std::max(start_, target_));
    }
    void snap(float target, std::uint32_t generation = 0) {
        if (!std::isfinite(target)) { reset(); return; }
        start_ = target_ = target;
        generation_ = generation;
        duration_ = 0;
        valid_ = true;
    }
    void reset() { *this = ScalarMotion{}; }
    bool valid() const { return valid_; }
    // Query only: callers must still present value(now) once when motion ends.
    // Unsigned subtraction supports the same millisecond rollover as value().
    bool animating(std::uint32_t now) const {
        return valid_ && duration_ && std::uint32_t(now - startedAt_) < duration_;
    }
private:
    float start_ = 0, target_ = 0;
    std::uint32_t generation_ = 0, startedAt_ = 0, duration_ = 0;
    bool valid_ = false;
};
class FrameClock {
public:
    // Smooth is rational 60Hz, not fixed17ms. Other modes retain33/50ms.
    // Keep phase but drop missed frames; each call returns at most one frame.
    bool due(std::uint32_t now, RefreshMode mode = RefreshMode::Smooth) {
        if (!started_ || mode != mode_) {
            mode_ = mode;
            started_ = true;
            previousAt_ = now;
            phase_ = 0;
            return true;
        }
        const auto elapsed = std::uint32_t(now - previousAt_);
        previousAt_ = now;
        const std::uint32_t units = mode == RefreshMode::Smooth ? 60 : 1;
        const std::uint32_t threshold = mode == RefreshMode::Smooth ? 1000 :
            (mode == RefreshMode::Economy ? 50 : 33);
        const auto accumulated = std::uint64_t(phase_) + std::uint64_t(elapsed) * units;
        phase_ = static_cast<std::uint32_t>(accumulated % threshold);
        return accumulated >= threshold;
    }
    void reset() { *this = FrameClock{}; }
private:
    std::uint32_t previousAt_ = 0, phase_ = 0;
    RefreshMode mode_ = RefreshMode::Smooth;
    bool started_ = false;
};

// UI scheduling only. A field stays pending until its own terminal value has
// actually been sampled/presented. Smooth slots never borrow the other field's
// allocation, so one moving number also remains at30Hz instead of rising to60Hz.
class NumberSchedule {
public:
    enum : std::uint8_t { None = 0, Rpm = 1, Speed = 2, Both = Rpm | Speed };

    void request(std::uint8_t fields) { pending_ |= fields & Both; }
    std::uint8_t pending() const { return pending_; }
    void reset() { *this = NumberSchedule{}; }

    // Call only after reading value(now)/attempting the selected labels. Never
    // clear an unselected field just because its easing finished meanwhile.
    void presented(std::uint8_t selected, std::uint8_t moving) {
        selected &= Both;
        pending_ = static_cast<std::uint8_t>((pending_ & ~selected) | (selected & moving));
    }

    std::uint8_t select(std::uint32_t now, bool instrumentFrameDue, RefreshMode mode,
                        bool reducedMotion, bool urgent, bool enableStaggered = true) {
        if (!configured_ || mode != mode_ || reducedMotion != reducedMotion_ ||
            enableStaggered != enableStaggered_) {
            mode_ = mode;
            reducedMotion_ = reducedMotion;
            enableStaggered_ = enableStaggered;
            configured_ = true;
            next_ = Rpm;
            combinedClock_.reset();
        }
        if (urgent) {
            request(Both);
            next_ = Rpm;
            return Both;
        }
        if (!pending_ || !instrumentFrameDue) return None;
        if (enableStaggered && mode == RefreshMode::Smooth && !reducedMotion) {
            const auto slot = next_;
            next_ = slot == Rpm ? Speed : Rpm;
            return slot & pending_;
        }
        const auto numericMode = mode == RefreshMode::Smooth ? RefreshMode::Balanced : mode;
        return combinedClock_.due(now, numericMode) ? Both : None;
    }

private:
    FrameClock combinedClock_;
    RefreshMode mode_ = RefreshMode::Smooth;
    std::uint8_t pending_ = None, next_ = Rpm;
    bool configured_ = false, reducedMotion_ = false, enableStaggered_ = true;
};
}
