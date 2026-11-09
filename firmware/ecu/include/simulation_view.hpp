#pragma once
#include "simulation.hpp"
#include "settings.hpp"
#include "engine_alerts.hpp"
#include <cmath>

namespace ecu::simulation {
enum class Alert : std::uint8_t { None, LowOil, Overheat, TempShort, TempOpen, NoRpm, NoSpeed, TpsInvalid, GearInvalid, HighRpm, CriticalRpm };
struct View {
    bool live = false, rpmValid = false, speedValid = false, temperatureValid = false;
    bool tpsValid = false, gearValid = false, oilValid = false;
    std::int32_t rpm = 0, speed = 0, temperature = 0, tps = 0, gear = 0, oil = 0;
    Alert alert = Alert::None;
    RpmWarning rpmWarning = RpmWarning::Normal;
};
inline View present(const Snapshot& source, SpeedUnit speedUnit, TempUnit tempUnit,
                    RpmWarning previous = RpmWarning::Normal) {
    View result;
    if (!source.enabled || !source.live) return result;
    const auto& frame = source.frame;
    result.live = true;
    result.rpmValid = !(frame.faults & NoRpm);
    result.rpmWarning = ecu::rpmWarning(frame.rpm, result.rpmValid, previous);
    result.speedValid = !(frame.faults & NoSpeed);
    result.temperatureValid = !(frame.faults & (TempOpen | TempShort));
    result.tpsValid = !(frame.faults & TpsInvalid);
    result.gearValid = !(frame.faults & GearInvalid);
    result.oilValid = frame.oil != 2;
    if (result.rpmValid) result.rpm = frame.rpm;
    if (result.speedValid) result.speed = static_cast<std::int32_t>(std::lround(speedDisplay(frame.speed10 / 10.0F, speedUnit)));
    if (result.temperatureValid) result.temperature = static_cast<std::int32_t>(std::lround(temperatureDisplay(frame.temp10 / 10.0F, tempUnit)));
    if (result.tpsValid) result.tps = (frame.tps10 + 5) / 10;
    if (result.gearValid) result.gear = frame.gear;
    if (result.oilValid) result.oil = frame.faults & LowOil ? 1 : frame.oil;
    if ((frame.faults & LowOil) || frame.oil == 1) result.alert = Alert::LowOil;
    else if (frame.faults & Overheat) result.alert = Alert::Overheat;
    else if (frame.faults & TempShort) result.alert = Alert::TempShort;
    else if (frame.faults & TempOpen) result.alert = Alert::TempOpen;
    else if (frame.faults & NoRpm) result.alert = Alert::NoRpm;
    else if (result.rpmWarning == RpmWarning::Critical) result.alert = Alert::CriticalRpm;
    else if (result.rpmWarning == RpmWarning::High) result.alert = Alert::HighRpm;
    else if (frame.faults & NoSpeed) result.alert = Alert::NoSpeed;
    else if (frame.faults & TpsInvalid) result.alert = Alert::TpsInvalid;
    else if (frame.faults & GearInvalid) result.alert = Alert::GearInvalid;
    return result;
}
inline const char* alertText(Alert alert) {
    switch (alert) {
    case Alert::None: return "";
    case Alert::LowOil: return "SIM: OLEO BAIXO";
    case Alert::Overheat: return "SIM: SUPERAQUECIMENTO";
    case Alert::TempShort: return "SIM: BLOCO EM CURTO";
    case Alert::TempOpen: return "SIM: BLOCO ABERTO";
    case Alert::NoRpm: return "SIM: SEM RPM";
    case Alert::NoSpeed: return "SIM: SEM VELOCIDADE";
    case Alert::TpsInvalid: return "SIM: TPS INVALIDO";
    case Alert::GearInvalid: return "SIM: MARCHA INVALIDA";
    case Alert::HighRpm: return "SIM: ROTACAO ALTA";
    case Alert::CriticalRpm: return "SIM: GIRO CRITICO";
    }
    return "SIM: FALHA";
}
}
