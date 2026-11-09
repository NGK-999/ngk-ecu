#include "simulation_view.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
}

int main() {
    using namespace ecu;
    using namespace ecu::simulation;
    struct RpmFixture {
        std::int32_t rpm;
        bool valid;
        RpmWarning previous, expected;
    };
    const RpmFixture rpmCases[]{
        {0, true, RpmWarning::Normal, RpmWarning::Normal},
        {3999, true, RpmWarning::Normal, RpmWarning::Normal},
        {4000, true, RpmWarning::Normal, RpmWarning::High},
        {4499, true, RpmWarning::Normal, RpmWarning::High},
        {4500, true, RpmWarning::Normal, RpmWarning::Critical},
        {12000, true, RpmWarning::Normal, RpmWarning::Critical},
        {4500, true, RpmWarning::High, RpmWarning::Critical},
        {4350, true, RpmWarning::Critical, RpmWarning::Critical},
        {4349, true, RpmWarning::Critical, RpmWarning::High},
        {3850, true, RpmWarning::High, RpmWarning::High},
        {3849, true, RpmWarning::High, RpmWarning::Normal},
        {3850, true, RpmWarning::Critical, RpmWarning::High},
        {3849, true, RpmWarning::Critical, RpmWarning::Normal},
        {5000, false, RpmWarning::Critical, RpmWarning::Normal},
        {-1, true, RpmWarning::Critical, RpmWarning::Normal}
    };
    for (const auto fixture : rpmCases) {
        check(rpmWarning(fixture.rpm, fixture.valid, fixture.previous) == fixture.expected,
              "absolute RPM thresholds and 150 RPM descending hysteresis are respected");
    }
    Snapshot source{true, true, Frame{99, 1, 1250, 123, 855, 355, -1, 0, None}};
    auto view = present(source, SpeedUnit::Kmh, TempUnit::Celsius);
    check(view.live && view.rpmValid && view.speedValid && view.temperatureValid && view.tpsValid &&
              view.gearValid && view.oilValid, "live frame exposes valid synthetic channels");
    check(view.rpm == 1250 && view.speed == 12 && view.temperature == 86 && view.tps == 36 &&
              view.gear == -1 && view.oil == 0 && view.alert == Alert::None,
          "display values use rounded units without changing source frame");
    view = present(source, SpeedUnit::Mph, TempUnit::Fahrenheit);
    check(view.speed == 8 && view.temperature == 186, "mph and Fahrenheit are converted before rounding");
    source.frame.temp10 = -400;
    check(present(source, SpeedUnit::Kmh, TempUnit::Celsius).temperature == -40 &&
              present(source, SpeedUnit::Kmh, TempUnit::Fahrenheit).temperature == -40,
          "negative Celsius is preserved through both unit systems");
    source.frame.speed10 = 2500;
    check(present(source, SpeedUnit::Kmh, TempUnit::Celsius).speed == 250 &&
              present(source, SpeedUnit::Mph, TempUnit::Celsius).speed == 155,
          "maximum speed remains within the display's supported three digits");
    source.frame = {99, 2, 0, 0, 850, 0, 0, 0, None};
    view = present(source, SpeedUnit::Kmh, TempUnit::Celsius);
    check(view.rpmValid && view.rpm == 0 && view.speedValid && view.speed == 0 && view.gearValid,
          "valid stopped synthetic engine is distinct from missing data");
    source.frame.faults = NoRpm | NoSpeed | TempOpen | TpsInvalid | GearInvalid;
    source.frame.oil = 2;
    view = present(source, SpeedUnit::Kmh, TempUnit::Celsius);
    check(!view.rpmValid && !view.speedValid && !view.temperatureValid && !view.tpsValid &&
              !view.gearValid && !view.oilValid && view.alert == Alert::TempOpen,
          "fault bits invalidate each corresponding channel with a meaningful priority");
    struct Fixture { std::uint8_t flags; Alert expected; };
    const Fixture priority[]{
        {LowOil | Overheat | TempShort | NoRpm | NoSpeed | TpsInvalid | GearInvalid, Alert::LowOil},
        {Overheat | TempShort | NoRpm | NoSpeed | TpsInvalid | GearInvalid, Alert::Overheat},
        {TempShort | NoRpm | NoSpeed | TpsInvalid | GearInvalid, Alert::TempShort},
        {TempOpen | NoRpm | NoSpeed | TpsInvalid | GearInvalid, Alert::TempOpen},
        {NoRpm | NoSpeed | TpsInvalid | GearInvalid, Alert::NoRpm},
        {NoSpeed | TpsInvalid | GearInvalid, Alert::NoSpeed},
        {TpsInvalid | GearInvalid, Alert::TpsInvalid}, {GearInvalid, Alert::GearInvalid}
    };
    source.frame.oil = 0;
    for (const auto fixture : priority) {
        source.frame.faults = fixture.flags;
        view = present(source, SpeedUnit::Kmh, TempUnit::Celsius);
        check(view.alert == fixture.expected && std::strncmp(alertText(view.alert), "SIM:", 4) == 0,
              "fault priority is stable and every warning identifies simulation");
    }
    source.frame.faults = None;
    source.frame.oil = 1;
    view = present(source, SpeedUnit::Kmh, TempUnit::Celsius);
    check(view.oilValid && view.oil == 1 && view.alert == Alert::LowOil,
          "explicit low oil state is visible even without a duplicate fault bit");
    source.frame.faults = LowOil | Overheat | NoRpm;
    source.live = false;
    view = present(source, SpeedUnit::Kmh, TempUnit::Celsius);
    check(!view.live && !view.rpmValid && !view.speedValid && !view.temperatureValid &&
              !view.tpsValid && !view.gearValid && !view.oilValid && view.alert == Alert::None &&
              view.rpm == 0 && view.temperature == 0 && view.speed == 0,
          "stale clears presentation values and ignores old faults");
    source.enabled = false;
    source.live = true;
    check(!present(source, SpeedUnit::Kmh, TempUnit::Celsius).live,
          "disabled session cannot expose data from a previous source");

    source = {true, true, Frame{99, 3, 4500, 123, 855, 355, 2, 0, None}};
    view = present(source, SpeedUnit::Kmh, TempUnit::Celsius);
    check(view.rpmWarning == RpmWarning::Critical && view.alert == Alert::CriticalRpm,
          "valid 4500 RPM raises a visible critical simulation alert");
    check(std::strcmp(alertText(view.alert), "SIM: GIRO CRITICO") == 0,
          "critical rotation message explicitly identifies simulation");
    source.frame.rpm = 4400;
    view = present(source, SpeedUnit::Kmh, TempUnit::Celsius, view.rpmWarning);
    check(view.rpmWarning == RpmWarning::Critical && view.alert == Alert::CriticalRpm,
          "view carries critical hysteresis while RPM descends through 4400");
    view = present(source, SpeedUnit::Kmh, TempUnit::Celsius);
    check(view.rpmWarning == RpmWarning::High && view.alert == Alert::HighRpm,
          "existing callers without previous state classify current absolute RPM");
    check(std::strcmp(alertText(view.alert), "SIM: ROTACAO ALTA") == 0,
          "high rotation message explicitly identifies simulation");

    UiSettings settings;
    for (const auto scale : {1000, 4500, 8000, 12000}) {
        settings.rpmScale = static_cast<std::uint16_t>(scale);
        source.frame.rpm = 4000;
        view = present(source, settings.speedUnit, settings.tempUnit);
        check(view.rpmWarning == RpmWarning::High && view.alert == Alert::HighRpm,
              "4000 RPM warning is independent of chosen display scale");
        source.frame.rpm = 4500;
        view = present(source, settings.speedUnit, settings.tempUnit);
        check(view.rpmWarning == RpmWarning::Critical,
              "4500 RPM critical state is independent of chosen display scale");
    }
    const Fixture rpmPriority[]{
        {LowOil | Overheat, Alert::LowOil}, {Overheat | TempShort, Alert::Overheat},
        {TempShort, Alert::TempShort}, {TempOpen, Alert::TempOpen},
        {NoRpm | NoSpeed, Alert::NoRpm}, {NoSpeed | TpsInvalid, Alert::CriticalRpm},
        {TpsInvalid | GearInvalid, Alert::CriticalRpm}
    };
    for (const auto fixture : rpmPriority) {
        source.frame.faults = fixture.flags;
        view = present(source, SpeedUnit::Kmh, TempUnit::Celsius, RpmWarning::Critical);
        check(view.alert == fixture.expected,
              "oil and temperature faults outrank RPM while missing RPM suppresses its alarm");
        if (fixture.flags & NoRpm) {
            check(view.rpmWarning == RpmWarning::Normal && !view.rpmValid,
                  "missing RPM clears previous critical state rather than alarming on old data");
        }
    }
    source.frame.faults = None;
    source.frame.oil = 1;
    view = present(source, SpeedUnit::Kmh, TempUnit::Celsius, RpmWarning::Critical);
    check(view.alert == Alert::LowOil && view.rpmWarning == RpmWarning::Critical,
          "explicit oil state retains footer priority over critical rotation");
    source.live = false;
    view = present(source, SpeedUnit::Kmh, TempUnit::Celsius, RpmWarning::Critical);
    check(view.rpmWarning == RpmWarning::Normal && view.alert == Alert::None,
          "stale source clears critical color state and every old alert");
    source.live = true;
    source.enabled = false;
    view = present(source, SpeedUnit::Kmh, TempUnit::Celsius, RpmWarning::Critical);
    check(view.rpmWarning == RpmWarning::Normal && view.alert == Alert::None,
          "disabled simulation clears critical color state and every old alert");

    State timed;
    timed.start(99, 100);
    source.frame.oil = 0;
    timed.accept(source.frame, 100);
    view = present(timed.snapshot(849), SpeedUnit::Kmh, TempUnit::Celsius);
    check(view.rpmWarning == RpmWarning::Critical && view.alert == Alert::CriticalRpm,
          "valid critical frame remains visible before the 750 ms TTL");
    view = present(timed.snapshot(850), SpeedUnit::Kmh, TempUnit::Celsius, view.rpmWarning);
    check(view.rpmWarning == RpmWarning::Normal && view.alert == Alert::None && !view.rpmValid,
          "TTL expiration clears critical warning exactly at 750 ms");
    source.frame.rpm = 4400;
    source.frame.sequence = 4;
    timed.accept(source.frame, 851);
    view = present(timed.snapshot(851), SpeedUnit::Kmh, TempUnit::Celsius, view.rpmWarning);
    check(view.rpmWarning == RpmWarning::High && view.alert == Alert::HighRpm,
          "fresh frame after expiry does not revive the old critical hysteresis");
    if (failures) return EXIT_FAILURE;
    std::puts("simulation view: units, RPM alerts/hysteresis, fault priority and stale suppression PASS");
    return EXIT_SUCCESS;
}
