#pragma once

#include <cstdint>

namespace ecu {
enum class RefreshMode : std::uint8_t { Smooth = 0, Balanced = 1, Economy = 2 };
enum class SpeedUnit : std::uint8_t { Kmh = 0, Mph = 1 };
enum class TempUnit : std::uint8_t { Celsius = 0, Fahrenheit = 1 };

struct UiSettings {
    std::uint8_t brightness = 70;
    RefreshMode refreshMode = RefreshMode::Smooth;
    SpeedUnit speedUnit = SpeedUnit::Kmh;
    TempUnit tempUnit = TempUnit::Celsius;
    std::uint16_t rpmScale = 8000;
    bool reducedMotion = false;
    // Display references only: these thresholds do not control the engine.
    std::uint8_t rpmYellowPercent = 75;
    std::uint8_t rpmRedPercent = 90;
};

inline bool validSettings(const UiSettings& settings) {
    return settings.brightness >= 10 && settings.brightness <= 100 &&
        static_cast<std::uint8_t>(settings.refreshMode) <= 2 &&
        static_cast<std::uint8_t>(settings.speedUnit) <= 1 &&
        static_cast<std::uint8_t>(settings.tempUnit) <= 1 &&
        settings.rpmScale >= 1000 && settings.rpmScale <= 12000 &&
        settings.rpmScale % 500 == 0 && settings.rpmYellowPercent >= 1 &&
        settings.rpmRedPercent <= 100 && settings.rpmYellowPercent < settings.rpmRedPercent;
}

inline bool operator==(const UiSettings& left, const UiSettings& right) {
    return left.brightness == right.brightness && left.refreshMode == right.refreshMode &&
        left.speedUnit == right.speedUnit && left.tempUnit == right.tempUnit &&
        left.rpmScale == right.rpmScale && left.reducedMotion == right.reducedMotion &&
        left.rpmYellowPercent == right.rpmYellowPercent &&
        left.rpmRedPercent == right.rpmRedPercent;
}

inline std::uint32_t framePeriodMs(RefreshMode mode) {
    switch (mode) {
        case RefreshMode::Smooth: return 17;
        case RefreshMode::Balanced: return 33;
        case RefreshMode::Economy: return 50;
    }
    return 33;
}

inline float speedDisplay(float kmh, SpeedUnit unit) {
    return unit == SpeedUnit::Mph ? kmh / 1.609344F : kmh;
}

inline float temperatureDisplay(float celsius, TempUnit unit) {
    return unit == TempUnit::Fahrenheit ? celsius * 1.8F + 32.0F : celsius;
}
}
