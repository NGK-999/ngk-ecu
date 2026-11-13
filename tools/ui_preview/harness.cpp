#include "ui.hpp"
#include "simulation_service.hpp"
#include "board.hpp"
#include "profile_service.hpp"
#include "radio.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <lvgl.h>

extern "C" { int64_t headless_time_us = 1000000; }
namespace ecu { std::uint16_t framebuffer[320 * 240]{}; }
namespace ecu::ui {
void previewRequire(bool, const char*);
void previewShow(unsigned);
void previewBounds();
void previewUnits(bool);
void previewScreenSettings();
void previewReadings(const char*, const char*, const char*, const char*);
void previewBoot();
void previewPanelUnits(const char*, const char*);
void previewManualCalibration();
void previewStartImport();
void previewWatchDashboard();
void previewDashboardPreserved();
void previewRpmPixelsBefore();
void previewRpmPixelsAfter();
void previewRampIdle();
void previewWatchOverlay(bool);
void previewOverlayPreserved(bool);
void previewStale();
}
namespace ecu::profiles {
Snapshot previewSnapshot{};
bool snapshot(Snapshot& out) { out = previewSnapshot; return true; }
bool apply(const UiSettings&) { return false; }
bool exportSd() { return false; }
bool importSd() { return true; } // Queue accepted; completion is published separately below.
bool refreshSd() { return false; }
const char* statusText(Status) { return "PREVIA: sem persistencia"; }
}
namespace ecu::radio {
Snapshot previewSnapshot{};
bool snapshot(Snapshot& out) { out = previewSnapshot; out.running = true; return true; }
bool enableWifi(bool) { return false; }
bool scanWifi() { return false; }
bool connectWifi(const char*, const char*) { return false; }
bool disconnectWifi(bool) { return false; }
bool enableBle(bool) { return false; }
bool discoverBle(uint16_t) { return false; }
bool confirmPairing(bool) { return false; }
bool clearBonds() { return false; }
const char* statusText(Status) { return "PREVIA: radios indisponiveis"; }
}
extern "C" void headless_assert(const char* file, int line) {
    std::fprintf(stderr, "LVGL_ASSERT %s:%d\n", file, line); std::abort();
}

void save(const char* directory, const char* name, bool panel) {
    // Settle existing animation with a deterministic clock, below the 750ms SIM TTL.
    for (int frame = 0; frame < 20; ++frame) {
        headless_time_us += 10000; ecu::ui::tick();
    }
    if (panel) ecu::ui::previewBounds();
    const auto path = std::string(directory) + "/" + name + ".ppm";
    auto* file = std::fopen(path.c_str(), "wb");
    ecu::ui::previewRequire(file != nullptr, "open host screenshot");
    std::fprintf(file, "P6\n320 240\n255\n");
    for (auto pixel : ecu::framebuffer) {
        pixel = std::uint16_t((pixel >> 8) | (pixel << 8));
        const unsigned char rgb[3] = {
            static_cast<unsigned char>(((pixel >> 11) & 31) * 255 / 31),
            static_cast<unsigned char>(((pixel >> 5) & 63) * 255 / 63),
            static_cast<unsigned char>((pixel & 31) * 255 / 31)};
        ecu::ui::previewRequire(std::fwrite(rgb, 1, 3, file) == 3, "write host screenshot");
    }
    ecu::ui::previewRequire(std::fclose(file) == 0, "close host screenshot");
    std::printf("PASS %s%s\n", name, panel ? " (glyph/parent/screen bounds)" : "");
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    using namespace ecu;
    ui::previewRequire(ui::begin(), "initialize real UI with host display");
    ui::previewBoot();
    ui::previewWatchDashboard();
    // Like the SD worker, publish the saved profile after UI initialization.
    profiles::previewSnapshot.ready = profiles::previewSnapshot.persisted = true;
    profiles::previewSnapshot.revision = 1;
    profiles::previewSnapshot.settings.speedUnit = SpeedUnit::Mph;
    profiles::previewSnapshot.settings.tempUnit = TempUnit::Fahrenheit;
    headless_time_us += 10000; ui::tick();
    ui::previewPanelUnits("mph", "°F");
    ui::previewDashboardPreserved();
    std::puts("PASS late saved profile updates boot dashboard units");
    // Restore metric fixtures before the existing scene suite.
    profiles::previewSnapshot.settings = UiSettings{};
    ++profiles::previewSnapshot.revision;
    headless_time_us += 500000; ui::tick();
    ui::previewRequire(ui::startUsbSimulation(), "select in-memory source for async import regression");
    ui::previewRequire(simulation::beginSession(98), "start import regression session");
    ui::previewStartImport();
    ui::previewWatchDashboard();
    ui::previewRequire(simulation::submit({98, 1, 2400, 1000, 1000, 350, 3, 0, simulation::None}),
                       "submit 100 km/h and 100 Celsius before profile completion");
    profiles::previewSnapshot.settings.speedUnit = SpeedUnit::Mph;
    profiles::previewSnapshot.settings.tempUnit = TempUnit::Fahrenheit;
    profiles::previewSnapshot.status = profiles::Status::Imported;
    ++profiles::previewSnapshot.revision;
    headless_time_us += 500000; ui::tick();
    for (int frame = 0; frame < 20; ++frame) {
        headless_time_us += 10000; ui::tick();
    }
    ui::previewReadings("2400", "62", "3", "212");
    ui::previewPanelUnits("mph", "°F");
    ui::previewDashboardPreserved();
    std::puts("PASS asynchronous import after menu/dashboard navigation keeps numbers and units consistent");
    simulation::endAnySession();
    profiles::previewSnapshot.settings = UiSettings{};
    ++profiles::previewSnapshot.revision;
    headless_time_us += 500000; ui::tick();
    for (int frame = 0; frame < 20; ++frame) {
        headless_time_us += 10000; ui::tick();
    }
    ui::previewRpmPixelsBefore();
    profiles::previewSnapshot.settings.rpmScale = 10000;
    ++profiles::previewSnapshot.revision;
    headless_time_us += 500000; ui::tick();
    for (int frame = 0; frame < 20; ++frame) {
        headless_time_us += 10000; ui::tick();
    }
    ui::previewRpmPixelsAfter();
    // A completion revision with identical settings must cause no ramp draw.
    ++profiles::previewSnapshot.revision;
    for (int frame = 0; frame < 120; ++frame) {
        headless_time_us += 10000; ui::tick();
    }
    ui::previewRampIdle();
    std::puts("PASS late RPM scale repaints graduations once and stays idle for unchanged settings");
    profiles::previewSnapshot.settings = UiSettings{};
    ++profiles::previewSnapshot.revision;
    headless_time_us += 500000; ui::tick();
    ui::previewWatchOverlay(true);
    profiles::previewSnapshot.settings.speedUnit = SpeedUnit::Mph;
    profiles::previewSnapshot.settings.tempUnit = TempUnit::Fahrenheit;
    ++profiles::previewSnapshot.revision;
    headless_time_us += 500000; ui::tick();
    ui::previewPanelUnits("mph", "°F");
    ui::previewOverlayPreserved(true);
    radio::previewSnapshot.blePairingPending = true;
    radio::previewSnapshot.blePairingId = 1;
    radio::previewSnapshot.blePasskey = 123456;
    headless_time_us += 500000; ui::tick();
    ui::previewWatchOverlay(false);
    profiles::previewSnapshot.settings = UiSettings{};
    ++profiles::previewSnapshot.revision;
    headless_time_us += 500000; ui::tick();
    ui::previewPanelUnits("km/h", "°C");
    ui::previewOverlayPreserved(false);
    radio::previewSnapshot = {};
    headless_time_us += 500000; ui::tick();
    std::puts("PASS profile unit changes preserve calibration and pairing overlays");
    ui::previewManualCalibration();
    ui::previewShow(0);
    save(argv[1], "menu", false);
    ui::previewShow(1); save(argv[1], "painel-sem-sinal", true);
    ui::previewRequire(ui::startUsbSimulation(), "select synthetic source in memory");
    ui::previewRequire(simulation::beginSession(99), "start in-memory session");
    simulation::Frame frame{99, 1, 2400, 456, 855, 350, 3, 0, simulation::None};
    auto submit = [&] {
        ui::previewRequire(simulation::submit(frame), "accept synthetic frame");
    };
    submit(); save(argv[1], "painel-sim", true);
    ui::previewReadings("2400", "46", "3", "86");
    ++frame.sequence; frame.rpm = 12000; frame.speed10 = 2500; frame.temp10 = 2000; frame.gear = 4;
    submit(); save(argv[1], "painel-maximos", true);
    ui::previewReadings("12000", "250", "4", "200");
    ++frame.sequence; frame.rpm = 1250; frame.speed10 = 45; frame.temp10 = -400; frame.gear = -1;
    submit(); save(argv[1], "painel-re", true);
    ui::previewReadings("1250", "5", "R", "-40");
    ++frame.sequence; frame.rpm = 850; frame.speed10 = 0; frame.temp10 = 855; frame.gear = 0;
    submit(); save(argv[1], "painel-neutro", true);
    ui::previewReadings("850", "0", "N", "86");
    ui::previewUnits(true);
    ++frame.sequence; frame.rpm = 2400; frame.speed10 = 2500; frame.temp10 = 2000; frame.gear = 3;
    submit(); save(argv[1], "painel-imperial", true);
    ui::previewReadings("2400", "155", "3", "392");
    ui::previewPanelUnits("mph", "°F");
    ui::previewUnits(false);
    ++frame.sequence; frame.rpm = 12000; frame.faults = simulation::LowOil | simulation::Overheat; frame.oil = 1;
    submit(); save(argv[1], "painel-alertas", true);
    headless_time_us += 800000; ui::tick();
    save(argv[1], "painel-sem-link", true); ui::previewStale();
    simulation::endAnySession();
    ui::previewShow(8); save(argv[1], "ajustes", false);
    ui::previewScreenSettings(); save(argv[1], "ajustes-tela", false);
    std::puts("PASS real LVGL 8.4 UI; synthetic services only; no hardware validation");
}
