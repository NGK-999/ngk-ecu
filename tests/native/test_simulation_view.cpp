#include "simulation_view.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
}

int main() {
    using namespace ecu;
    using namespace ecu::simulation;
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
    if (failures) return EXIT_FAILURE;
    std::puts("simulation view: units, per-channel validity, fault priority and stale suppression PASS");
    return EXIT_SUCCESS;
}
