#pragma once
#include "settings.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>

namespace ecu::ui {
enum class SettingField : std::uint8_t { Brightness, Refresh, Motion, Speed, Temperature, RpmScale, Yellow, Red };
class SettingsDraft {
public:
    UiSettings values{};
    void load(const UiSettings& settings) { values = settings; saved_ = settings; }
    void acknowledge(const UiSettings& settings) { saved_ = settings; }
    bool dirty() const { return !(values == saved_); }
    bool adjust(SettingField field, int direction) {
        const auto before = values;
        const int step = direction > 0 ? 1 : -1;
        switch (field) {
            case SettingField::Brightness: values.brightness = std::clamp(int(values.brightness) + step * 10, 10, 100); break;
            case SettingField::Refresh: values.refreshMode = RefreshMode(std::clamp(int(values.refreshMode) + step, 0, 2)); break;
            case SettingField::Motion: values.reducedMotion = direction > 0; break;
            case SettingField::Speed: values.speedUnit = SpeedUnit(direction > 0 ? 1 : 0); break;
            case SettingField::Temperature: values.tempUnit = TempUnit(direction > 0 ? 1 : 0); break;
            case SettingField::RpmScale: values.rpmScale = std::clamp(int(values.rpmScale) + step * 500, 1000, 12000); break;
            case SettingField::Yellow: values.rpmYellowPercent = std::clamp(int(values.rpmYellowPercent) + step * 5, 1, int(values.rpmRedPercent) - 1); break;
            case SettingField::Red: values.rpmRedPercent = std::clamp(int(values.rpmRedPercent) + step * 5, int(values.rpmYellowPercent) + 1, 100); break;
        }
        return !(before == values);
    }
private:
    UiSettings saved_{};
};
class LocalTextEntry {
public:
    void begin(bool secret, std::size_t maximum) { clear(); secret_ = secret; maximum_ = std::min(maximum, std::size_t(64)); }
    bool append(char character) {
        if (character < 32 || character > 126 || used_ == maximum_) return false;
        bytes_[used_++] = character; bytes_[used_] = 0; return true;
    }
    void backspace() { if (used_) bytes_[--used_] = 0; }
    void clear() {
        auto* cursor = static_cast<volatile char*>(bytes_.data());
        for (std::size_t index = 0; index < bytes_.size(); ++index) cursor[index] = 0;
        used_ = 0;
    }
    const char* text() const { return bytes_.data(); }
    std::size_t size() const { return used_; }
    void preview(char* output, std::size_t capacity) const {
        if (!capacity) return;
        const auto count = std::min(used_, capacity - 1);
        for (std::size_t index = 0; index < count; ++index) output[index] = secret_ ? '*' : bytes_[used_ - count + index];
        output[count] = 0;
    }
    static char key(unsigned bank, unsigned index) {
        const auto code = 32 + bank * 18 + index;
        return bank < 6 && index < 18 && code <= 126 ? static_cast<char>(code) : 0;
    }
    bool validPassword() const {
        if (used_ >= 8 && used_ <= 63) return true;
        if (used_ != 64) return false;
        for (std::size_t index = 0; index < used_; ++index) {
            const auto byte = bytes_[index];
            if (!((byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f') || (byte >= 'A' && byte <= 'F'))) return false;
        }
        return true;
    }
private:
    std::array<char, 65> bytes_{};
    std::size_t used_ = 0, maximum_ = 64;
    bool secret_ = true;
};
struct Point { float x = 0; float y = 0; };
struct Rect {
    int x, y, w, h;
    constexpr bool contains(int px, int py) const {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
    constexpr bool overlaps(Rect other) const {
        return x < other.x + other.w && other.x < x + w &&
               y < other.y + other.h && other.y < y + h;
    }
};
inline constexpr std::array<Rect, 4> navigation{{
    {8, 40, 148, 84}, {164, 40, 148, 84}, {8, 132, 148, 84}, {164, 132, 148, 84}}};
inline constexpr Rect content{8, 36, 304, 144};
inline constexpr Rect menuReturn{8, 188, 304, 44};
inline constexpr Rect rpmCard{8, 2, 120, 34};
inline constexpr Rect rpmBar{8, 44, 304, 56};
inline constexpr Rect speedCard{8, 104, 184, 72};
inline constexpr Rect gearCard{204, 104, 108, 72};
inline constexpr Rect temperatureCard{8, 180, 148, 36};
inline constexpr Rect oilStrip{164, 180, 148, 36};
inline constexpr Rect dashboardHint{8, 224, 304, 16};
inline constexpr Rect alertStrip{0, 218, 320, 22};
inline constexpr std::array<Rect, 3> settingsRows{{{8, 40, 304, 44}, {8, 88, 304, 44}, {8, 136, 304, 44}}};
inline constexpr std::array<Rect, 6> settingsCategories{{
    {8, 40, 148, 44}, {164, 40, 148, 44}, {8, 88, 148, 44},
    {164, 88, 148, 44}, {8, 136, 148, 44}, {164, 136, 148, 44}}};
inline constexpr Rect keyboardKey(unsigned index) { return {8 + int(index % 6) * 52, 46 + int(index / 6) * 48, 44, 44}; }
inline constexpr std::array<Point, 5> calibrationTargets{{
    {26, 54}, {294, 54}, {294, 166}, {26, 166}, {160, 110}}};

inline void formatStorageStatus(char* output, std::size_t size, bool mounted,
                               std::uint32_t capacityMiB) {
    const char* status = mounted ? "SD montado" : "SD nao montado";
    if (capacityMiB) {
        std::snprintf(output, size, "%s\nCapacidade: %lu MiB", status,
                      static_cast<unsigned long>(capacityMiB));
    } else {
        std::snprintf(output, size, "%s\nCapacidade: --", status);
    }
}

// Hysteresis keeps brief missing samples from turning one tap into two clicks.
class TouchDebouncer {
public:
    bool update(bool down, Point point, std::uint32_t now) {
        if (down) {
            releasing_ = false;
            if (!contact_ || (!pressed_ &&
                (std::fabs(point.x - anchor_.x) > 14 ||
                 std::fabs(point.y - anchor_.y) > 14))) {
                contact_ = true;
                anchor_ = point;
                pressedAt_ = now;
            }
            if (!pressed_ && std::uint32_t(now - pressedAt_) >= 25) {
                pressed_ = true;
                position_ = point;
            }
        } else {
            contact_ = false;
            if (!releasing_) {
                releasing_ = true;
                releasedAt_ = now;
            }
            if (std::uint32_t(now - releasedAt_) >= 25) pressed_ = false;
        }
        return pressed_;
    }
    Point position() const { return position_; }
    void reset() { *this = TouchDebouncer{}; }
private:
    Point anchor_{}, position_{};
    bool contact_ = false, pressed_ = false, releasing_ = false;
    std::uint32_t pressedAt_ = 0, releasedAt_ = 0;
};

class DashboardDoubleTap {
public:
    // `pressed` is the debounced state; `point` must keep the real mapped
    // trajectory, rather than the frozen coordinate supplied to LVGL.
    bool update(bool pressed, Point point, std::uint32_t now) {
        if (firstTap_ && std::uint32_t(now - releasedAt_) > 400) firstTap_ = false;
        if (pressed) {
            if (!pressed_) {
                pressed_ = true;
                startedAt_ = now;
                origin_ = point;
                validTap_ = inside(point);
            }
            if (!inside(point) || !close(point, origin_, 18) ||
                std::uint32_t(now - startedAt_) > 350) validTap_ = false;
            return false;
        }
        if (!pressed_) return false;
        pressed_ = false;
        if (!validTap_ || !inside(point) || !close(point, origin_, 18) ||
            std::uint32_t(now - startedAt_) > 350) {
            firstTap_ = false;
            return false;
        }
        if (firstTap_ && close(point, firstPoint_, 48)) {
            firstTap_ = false;
            return true;
        }
        firstTap_ = true;
        releasedAt_ = now;
        firstPoint_ = point;
        return false;
    }
    void reset() { *this = DashboardDoubleTap{}; }
private:
    static bool inside(Point point) {
        return std::isfinite(point.x) && std::isfinite(point.y) &&
            point.x >= 0 && point.x < 320 && point.y >= 0 && point.y < 240;
    }
    static bool close(Point a, Point b, float distance) {
        const float x = a.x - b.x, y = a.y - b.y;
        return x * x + y * y <= distance * distance;
    }
    bool pressed_ = false, validTap_ = false, firstTap_ = false;
    std::uint32_t startedAt_ = 0, releasedAt_ = 0;
    Point origin_{}, firstPoint_{};
};

struct Calibration {
    std::array<float, 6> coefficients{};
    bool valid() const {
        for (auto value : coefficients) if (!std::isfinite(value)) return false;
        const auto& c = coefficients;
        const float determinant = c[0] * c[4] - c[1] * c[3];
        return std::fabs(determinant) > 0.0001F &&
            std::fabs(c[0]) < 1 && std::fabs(c[1]) < 1 &&
            std::fabs(c[3]) < 1 && std::fabs(c[4]) < 1 &&
            std::fabs(c[2]) < 8192 && std::fabs(c[5]) < 8192;
    }
    Point map(Point point) const {
        const auto& c = coefficients;
        return {c[0] * point.x + c[1] * point.y + c[2],
                c[3] * point.x + c[4] * point.y + c[5]};
    }
    bool fit(const std::array<Point, 5>& raw, const std::array<Point, 5>& target) {
        coefficients = {};
        Point rawMean{}, targetMean{};
        for (std::size_t i = 0; i < raw.size(); ++i) {
            if (!std::isfinite(raw[i].x) || !std::isfinite(raw[i].y) ||
                raw[i].x < 100 || raw[i].x > 4000 ||
                raw[i].y < 100 || raw[i].y > 4000 ||
                !std::isfinite(target[i].x) || !std::isfinite(target[i].y)) return false;
            rawMean.x += raw[i].x / 5;
            rawMean.y += raw[i].y / 5;
            targetMean.x += target[i].x / 5;
            targetMean.y += target[i].y / 5;
        }
        double xx = 0, xy = 0, yy = 0, xt = 0, yt = 0, xu = 0, yu = 0;
        for (std::size_t i = 0; i < raw.size(); ++i) {
            const double x = raw[i].x - rawMean.x, y = raw[i].y - rawMean.y;
            const double tx = target[i].x - targetMean.x, ty = target[i].y - targetMean.y;
            xx += x * x; xy += x * y; yy += y * y;
            xt += x * tx; yt += y * tx; xu += x * ty; yu += y * ty;
        }
        const double determinant = xx * yy - xy * xy;
        if (!std::isfinite(determinant) || determinant < 1.0e10) return false;
        coefficients[0] = float((xt * yy - yt * xy) / determinant);
        coefficients[1] = float((yt * xx - xt * xy) / determinant);
        coefficients[3] = float((xu * yy - yu * xy) / determinant);
        coefficients[4] = float((yu * xx - xu * xy) / determinant);
        coefficients[2] = targetMean.x - coefficients[0] * rawMean.x - coefficients[1] * rawMean.y;
        coefficients[5] = targetMean.y - coefficients[3] * rawMean.x - coefficients[4] * rawMean.y;
        if (!valid()) { coefficients = {}; return false; }
        for (std::size_t i = 0; i < raw.size(); ++i) {
            const auto mapped = map(raw[i]);
            if (std::hypot(mapped.x - target[i].x, mapped.y - target[i].y) > 18) {
                coefficients = {};
                return false;
            }
        }
        return true;
    }
};
}
