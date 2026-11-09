#include "ui_layout.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}
bool near(float actual, float expected) {
    return std::fabs(actual - expected) < 0.5F;
}
}

int main() {
    using namespace ecu::ui;
    const Rect screen{0, 0, 320, 240};
    for (std::size_t i = 0; i < navigation.size(); ++i) {
        const auto& current = navigation[i];
        check(current.w >= 44 && current.h >= 44, "navigation is finger sized");
        check(screen.contains(current.x, current.y) &&
                  screen.contains(current.x + current.w - 1, current.y + current.h - 1),
              "navigation remains inside the screen");
        for (std::size_t j = i + 1; j < navigation.size(); ++j) {
            check(!current.overlaps(navigation[j]), "navigation targets do not overlap");
        }
    }
    check(navigation.size() == 4, "all four pages remain accessible");
    check(navigation[0].y == navigation[1].y && navigation[2].y == navigation[3].y &&
              navigation[2].y >= navigation[0].y + navigation[0].h + 8 &&
              navigation[0].w == navigation[1].w && navigation[2].w == navigation[3].w,
          "menu exposes four balanced targets on two spaced rows");
    check(menuReturn.w >= 44 && menuReturn.h >= 44 && !menuReturn.overlaps(content),
          "secondary pages have a separate finger-sized Menu action");
    check(screen.contains(menuReturn.x + menuReturn.w - 1, menuReturn.y + menuReturn.h - 1),
          "Menu return action fits the screen");
    check(speedCard.h >= 68 && speedCard.w >= 160 && rpmCard.w >= 200 && rpmCard.h >= 48,
          "cockpit leaves room for large speed and five-digit RPM glyphs");
    check(gearCard.w >= 72 && gearCard.h >= 48 && gearCard.y == speedCard.y &&
              !gearCard.overlaps(speedCard),
          "SIM gear gets its own large area beside speed");
    for (const auto instrument : {speedCard, gearCard, rpmCard, temperatureCard, oilStrip}) {
        check(screen.contains(instrument.x, instrument.y) &&
                  screen.contains(instrument.x + instrument.w - 1, instrument.y + instrument.h - 1),
              "dashboard instrument remains inside the screen");
        check(!instrument.overlaps(dashboardHint), "menu gesture hint does not cover instruments");
    }
    check(!rpmCard.overlaps(temperatureCard) && !rpmCard.overlaps(speedCard) &&
              !temperatureCard.overlaps(speedCard) && !oilStrip.overlaps(temperatureCard) &&
              !oilStrip.overlaps(speedCard) && !oilStrip.overlaps(rpmCard),
          "all instruments remain separate");
    check(!gearCard.overlaps(rpmCard) && !gearCard.overlaps(temperatureCard) &&
              !gearCard.overlaps(oilStrip) && !gearCard.overlaps(rpmBar),
          "SIM gear cannot cover another instrument");
    check(temperatureCard.y == oilStrip.y &&
              temperatureCard.y >= speedCard.y + speedCard.h + 8 &&
              temperatureCard.y + temperatureCard.h + 8 <= dashboardHint.y &&
              screen.contains(dashboardHint.x + dashboardHint.w - 1,
                              dashboardHint.y + dashboardHint.h - 1),
          "temperature and oil share a base row separated from values and footer");
    check(!rpmBar.overlaps(speedCard) && !rpmBar.overlaps(rpmCard) &&
              rpmBar.y >= rpmCard.y + rpmCard.h &&
              screen.contains(rpmBar.x + rpmBar.w - 1, rpmBar.y + rpmBar.h - 1),
          "RPM band separates top rotation from lower driving instruments");
    for (std::size_t index = 0; index < settingsCategories.size(); ++index) {
        const auto target = settingsCategories[index];
        check(target.w >= 44 && target.h >= 44 &&
                  screen.contains(target.x, target.y) &&
                  screen.contains(target.x + target.w - 1, target.y + target.h - 1) &&
                  !target.overlaps(menuReturn),
              "each settings category is reachable by a distinct finger-sized target");
        for (std::size_t other = index + 1; other < settingsCategories.size(); ++other) {
            check(!target.overlaps(settingsCategories[other]), "category targets never overlap");
        }
    }
    for (unsigned index = 0; index < 18; ++index) {
        const auto current = keyboardKey(index);
        check(current.w >= 44 && current.h >= 44 &&
                  screen.contains(current.x + current.w - 1, current.y + current.h - 1),
              "every keyboard character is finger sized and inside the screen");
        for (unsigned other = index + 1; other < 18; ++other) {
            check(!current.overlaps(keyboardKey(other)), "keyboard targets do not overlap");
        }
    }
    for (const auto row : settingsRows) {
        check(row.h >= 44 && !row.overlaps(menuReturn), "settings rows leave the footer accessible");
    }

    TouchDebouncer touch;
    check(!touch.update(true, {20, 205}, 100), "a new contact is not immediately pressed");
    check(!touch.update(true, {21, 205}, 119), "short noise does not make a click");
    check(touch.update(true, {21, 205}, 126), "stable contact becomes pressed");
    check(touch.update(false, {}, 130), "one missing sample does not release");
    check(touch.update(true, {22, 205}, 140), "returning contact cancels release");
    check(touch.update(false, {}, 150), "release starts debounce");
    check(!touch.update(false, {}, 176), "stable release completes exactly one click");
    check(!touch.update(false, {}, 200), "released contact stays released");
    check(!touch.update(true, {20, 205}, 300), "second click needs another debounce");
    check(!touch.update(true, {90, 205}, 320), "moving contact restarts debounce");
    check(touch.update(true, {90, 205}, 346), "new stable position can be pressed");
    check(near(touch.position().x, 90), "press uses the stable position");
    touch.reset();
    check(!touch.update(true, {20, 205}, 0xFFFFFFF0U), "wraparound starts a fresh contact");
    check(touch.update(true, {20, 205}, 20), "debounce handles millisecond wraparound");

    Calibration calibration;
    check(!calibration.valid(), "empty NVS calibration cannot enable touch");
    const std::array<Point, 5> raw{{{600, 800}, {3500, 800}, {3500, 3400},
                                     {600, 3400}, {2050, 2100}}};
    check(calibration.fit(raw, calibrationTargets), "five well spread points calibrate");
    auto mapped = calibration.map({2050, 2100});
    check(near(mapped.x, 160) && near(mapped.y, 110), "calibration maps the measured center");
    mapped = calibration.map({600, 3400});
    check(near(mapped.x, 26) && near(mapped.y, 166), "calibration maps a measured corner");
    const std::array<Point, 5> rotated{{{800, 3500}, {800, 600}, {3400, 600},
                                         {3400, 3500}, {2100, 2050}}};
    check(calibration.fit(rotated, calibrationTargets), "axis swap and inversion calibrate");
    mapped = calibration.map({3400, 3500});
    check(near(mapped.x, 26) && near(mapped.y, 166), "swapped coordinates stay correct");
    auto bad = raw;
    bad.fill({1000, 1000});
    check(!calibration.fit(bad, calibrationTargets) && !calibration.valid(),
          "degenerate calibration invalidates old coefficients");
    bad = raw;
    bad[4] = {3700, 3600};
    check(!calibration.fit(bad, calibrationTargets), "inconsistent fifth point is rejected");
    bad = raw;
    bad[0].x = std::numeric_limits<float>::quiet_NaN();
    check(!calibration.fit(bad, calibrationTargets), "invalid raw data is rejected");
    calibration.coefficients = {0.09F, 0, std::numeric_limits<float>::infinity(),
                                0, 0.05F, 0};
    check(!calibration.valid(), "nonfinite NVS coefficients are rejected");

    char storage[96];
    formatStorageStatus(storage, sizeof(storage), false, 1898);
    check(std::strstr(storage, "1898 MiB") != nullptr,
          "a known card capacity remains visible when mounting fails");
    check(std::strstr(storage, "nao montado") != nullptr,
          "unmounted card does not appear mounted");
    formatStorageStatus(storage, sizeof(storage), true, 1898);
    check(std::strstr(storage, "SD montado") != nullptr &&
              std::strstr(storage, "1898 MiB") != nullptr,
          "mounted storage shows its identified capacity");
    formatStorageStatus(storage, sizeof(storage), false, 0);
    check(std::strstr(storage, "Capacidade: --") != nullptr,
          "missing card does not invent a zero capacity reading");

    if (failures) return EXIT_FAILURE;
    std::puts("UI layout, touch debounce and five-point calibration: PASS");
    return EXIT_SUCCESS;
}
