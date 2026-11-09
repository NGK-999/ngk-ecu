#pragma once
#include <cstdint>

namespace ecu {
enum class RpmWarning : std::uint8_t { Normal, High, Critical };
// Illustrative bench warnings, independent of the display scale. These are
// neither qualified engine limits nor a physical rev limiter or actuator.
inline constexpr std::int32_t rpmHighThreshold = 4000;
inline constexpr std::int32_t rpmCriticalThreshold = 4500;
inline constexpr std::int32_t rpmWarningHysteresis = 150;

inline constexpr RpmWarning rpmWarning(std::int32_t rpm, bool valid,
                                       RpmWarning previous = RpmWarning::Normal) {
    if (!valid || rpm < 0) return RpmWarning::Normal;
    if (rpm >= rpmCriticalThreshold ||
        (previous == RpmWarning::Critical && rpm >= rpmCriticalThreshold - rpmWarningHysteresis)) {
        return RpmWarning::Critical;
    }
    if (rpm >= rpmHighThreshold ||
        (previous != RpmWarning::Normal && rpm >= rpmHighThreshold - rpmWarningHysteresis)) {
        return RpmWarning::High;
    }
    return RpmWarning::Normal;
}
}
