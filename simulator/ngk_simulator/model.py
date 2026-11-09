"""Pure, illustrative driving dynamics for synthetic dashboard demonstrations.

This demonstration loosely evokes a carbureted 1300; year and carburetor
variants differ. The coefficients, RPM envelope and surface-temperature
targets are presentation choices, not vehicle/sensor calibration, operating
limits, an ignition cut or a guarantee about a real engine.
"""

from dataclasses import dataclass
from enum import IntFlag
import math


class Fault(IntFlag):
    NONE = 0
    NO_RPM = 1
    NO_SPEED = 2
    TEMP_OPEN = 4
    TEMP_SHORT = 8
    LOW_OIL = 16
    OVERHEAT = 32
    TPS_INVALID = 64
    GEAR_INVALID = 128


@dataclass
class Controls:
    engine_on: bool = False
    throttle_pct: float = 0.0
    brake_pct: float = 0.0
    clutch: bool = False
    gear: int = 0
    block_c: float = 85.0
    manual: bool = False
    manual_rpm: float = 0.0
    manual_speed_kph: float = 0.0
    faults: Fault = Fault.NONE
    thermal_auto: bool = False


@dataclass(frozen=True)
class Sample:
    rpm: float = 0.0
    speed_kph: float = 0.0
    block_c: float = 85.0
    throttle_pct: float = 0.0
    gear: int = 0
    oil: int = 0
    faults: Fault = Fault.NONE


def _finite_number(value: object, name: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{name}: numero finito necessario")
    try:
        result = float(value)
    except (ValueError, OverflowError) as error:
        raise ValueError(f"{name}: numero finito necessario") from error
    if not math.isfinite(result):
        raise ValueError(f"{name}: numero finito necessario")
    return result


def _integer_range(value: object, name: str, low: int, high: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not low <= value <= high:
        raise ValueError(f"{name}: inteiro entre {low} e {high} necessario")
    return value


def _validated_faults(value: object) -> Fault:
    bits = _integer_range(value, "faults", 0, 255)
    faults = Fault(bits)
    if faults & Fault.TEMP_OPEN and faults & Fault.TEMP_SHORT:
        raise ValueError("Bloco aberto e em curto sao falhas mutuamente exclusivas")
    return faults


def set_fault(controls: Controls, fault: Fault, enabled: bool) -> None:
    """Change one fault without changing any driving control or saved sample."""
    selected = _integer_range(fault, "fault", 1, 128)
    if selected & (selected - 1) or not isinstance(enabled, bool):
        raise ValueError("Selecione uma falha e um estado booleano")
    current = _validated_faults(controls.faults)
    if enabled:
        if fault == Fault.TEMP_OPEN:
            current &= ~Fault.TEMP_SHORT
        elif fault == Fault.TEMP_SHORT:
            current &= ~Fault.TEMP_OPEN
        current |= fault
    else:
        current &= ~Fault(fault)
    controls.faults = Fault(current)


def _clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


class VehicleModel:
    """Stateful synthetic RPM and nonnegative speed magnitude, including reverse.

    Neutral or a pressed clutch decouples the accelerator from road speed.
    Engine-off vehicles can coast; the brake always slows them. Manual mode
    sets both values directly, independently of engine_on, and seeds the next
    driving step. Each call advances at most 100 ms after a paused GUI resumes.
    The automatic model approaches at most 5000 RPM under power. A manual
    value above that envelope decays with inertia when returning to automatic.
    Thermal auto treats block_c as a new initial surface temperature whenever
    enabled or changed. Manual mode always presents the exact selected value.
    """

    def __init__(self) -> None:
        self.reset()

    def reset(self) -> None:
        self._rpm = 0.0
        self._speed_kph = 0.0
        self._elapsed = 0.0
        self._block_c = 85.0
        self._block_input = None
        self._thermal_active = False

    def _surface_temperature(self, controls: Controls, selected: float,
                             throttle: float, gear: int, elapsed: float) -> float:
        active = controls.thermal_auto and not controls.manual
        if not active or not self._thermal_active or selected != self._block_input:
            self._block_c = selected
        self._block_input = selected
        self._thermal_active = active
        if active and elapsed:
            airflow = _clamp(self._speed_kph / 120.0, 0.0, 1.0)
            if controls.engine_on:
                coupled = gear != 0 and not controls.clutch
                load = throttle / 100.0 * (1.0 if coupled else 0.45)
                target = 85.0 + 25.0 * load * (1.0 - 0.25 * airflow)
                time_constant = 45.0
            else:
                target = 25.0
                time_constant = 90.0 / (1.0 + airflow)
            self._block_c += (target - self._block_c) * (1.0 - math.exp(-elapsed / time_constant))
        return self._block_c

    def step(self, controls: Controls, dt: float) -> Sample:
        elapsed = _finite_number(dt, "dt")
        if elapsed < 0:
            raise ValueError("dt nao pode ser negativo")
        elapsed = min(elapsed, 0.1)
        throttle = _clamp(_finite_number(controls.throttle_pct, "throttle_pct"), 0.0, 100.0)
        brake = _clamp(_finite_number(controls.brake_pct, "brake_pct"), 0.0, 100.0)
        block = _clamp(_finite_number(controls.block_c, "block_c"), -40.0, 200.0)
        manual_rpm = _clamp(_finite_number(controls.manual_rpm, "manual_rpm"), 0.0, 12000.0)
        manual_speed = _clamp(_finite_number(controls.manual_speed_kph, "manual_speed_kph"), 0.0, 250.0)
        gear = _integer_range(controls.gear, "gear", -1, 4)
        faults = _validated_faults(controls.faults)
        if not all(isinstance(value, bool) for value in
                   (controls.engine_on, controls.clutch, controls.manual, controls.thermal_auto)):
            raise ValueError("Motor, embreagem, modo manual e termico exigem valores booleanos")

        block = self._surface_temperature(controls, block, throttle, gear, elapsed)

        if controls.manual:
            self._rpm = manual_rpm
            self._speed_kph = manual_speed
        elif elapsed:
            self._elapsed += elapsed
            engaged = controls.engine_on and not controls.clutch and gear != 0
            ratios = {-1: 105.0, 1: 110.0, 2: 70.0, 3: 48.0, 4: 34.0}
            speed_ms = self._speed_kph / 3.6
            drive = 0.0
            if engaged:
                drive = {-1: 3.2, 1: 4.2, 2: 3.0, 3: 2.1, 4: 1.5}[gear] * throttle / 100.0
                # Available drive smoothly falls away near this gear's model
                # envelope. A downshift or manual seed never teleports speed.
                wheel_rpm = self._speed_kph * ratios[gear]
                drive *= max(0.0, 1.0 - (wheel_rpm / 5000.0) ** 2)
            drag = 0.12 + speed_ms * 0.018
            if engaged:
                drag += 0.4 * (1.0 - throttle / 100.0)
            speed_ms = _clamp(speed_ms + (drive - drag - brake * 0.08) * elapsed, 0.0, 250.0 / 3.6)
            self._speed_kph = speed_ms * 3.6

            if not controls.engine_on:
                target_rpm = 0.0
            else:
                cold = _clamp((85.0 - block) / 60.0, 0.0, 1.0)
                variation = (18.0 * math.sin(self._elapsed * 1.7) +
                             5.0 * math.sin(self._elapsed * 0.63)) * (1.0 - throttle / 100.0) ** 2
                idle = 700.0 + cold * 300.0 + variation
                target_rpm = max(idle, self._speed_kph * ratios[gear]) if engaged else (
                    idle + (5000.0 - idle) * throttle / 100.0)
                target_rpm = min(target_rpm, 5000.0)
            time_constant = 0.4 if not controls.engine_on else (0.55 if target_rpm > self._rpm else 0.85)
            self._rpm += (target_rpm - self._rpm) * (1.0 - math.exp(-elapsed / time_constant))
            if not controls.engine_on and self._rpm < 1.0:
                self._rpm = 0.0

        if faults & Fault.OVERHEAT:
            block = max(block, 150.0)
        # Sensor faults affect validity, not vehicle motion. The receiver uses
        # their bits instead of interpreting an artificial zero as a reading.
        return Sample(self._rpm, self._speed_kph, block, throttle, gear,
                      1 if faults & Fault.LOW_OIL else 0, faults)
