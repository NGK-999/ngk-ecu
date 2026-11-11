"""Pure synthetic 60-second drive cycle; no I/O or vehicle calibration."""

from bisect import bisect_right
from dataclasses import dataclass, replace
import math

from .model import Controls, Fault


@dataclass(frozen=True)
class CycleFrame:
    """One timeline position with an independent, mutable controls snapshot.

    ``index`` is zero-based; ``cycle_number`` starts at one. Elapsed values
    are seconds, with ``total_elapsed`` measured since the original start.
    """

    phase_name: str
    index: int
    cycle_number: int
    phase_elapsed: float
    total_elapsed: float
    controls: Controls


# Boundaries are cumulative seconds. Templates stay private and are never
# returned directly, so callers may freely edit the controls in a frame.
_STARTS = (0, 4, 12, 18, 24, 30, 36, 44, 50, 56)
_DURATION = 60
_BASE = Controls(block_c=25, thermal_auto=True)
_PHASES = (
    ("Parado", replace(_BASE)),
    ("Partida / lenta", replace(_BASE, engine_on=True)),
    ("Primeira", replace(_BASE, engine_on=True, gear=1, throttle_pct=45)),
    ("Segunda", replace(_BASE, engine_on=True, gear=2, throttle_pct=60)),
    ("Terceira", replace(_BASE, engine_on=True, gear=3, throttle_pct=65)),
    ("Quarta", replace(_BASE, engine_on=True, gear=4, throttle_pct=35)),
    ("Frenagem", replace(_BASE, engine_on=True, brake_pct=70)),
    ("Falhas", replace(_BASE, engine_on=True, faults=Fault.LOW_OIL | Fault.OVERHEAT)),
    ("Recuperação", replace(_BASE, engine_on=True)),
    ("Desligamento", replace(_BASE)),
)


def _clock(now: float) -> float:
    if isinstance(now, bool) or not isinstance(now, (int, float)):
        raise ValueError("now exige número finito não negativo")
    try:
        value = float(now)
    except OverflowError as error:
        raise ValueError("now exige número finito não negativo") from error
    if not math.isfinite(value) or value < 0:
        raise ValueError("now exige número finito não negativo")
    return value


class DriveCycle:
    """Select a phase from a caller-owned monotonic clock without stepping.

    Call ``stop`` to retrieve the saved original controls, including after a
    single cycle ends automatically. A new start requires that restoration
    snapshot to have been consumed first. No model state is reset here.
    """

    def __init__(self) -> None:
        self._running = False
        self._original: Controls | None = None
        self._started = 0.0
        self._last_now: float | None = None
        self._repeat = True

    @property
    def running(self) -> bool:
        return self._running

    def start(self, now: float, controls: Controls, repeat: bool = True) -> CycleFrame:
        timestamp = _clock(now)
        if self._original is not None:
            raise RuntimeError("Pare o ciclo e restaure os controles antes de reiniciar")
        if not isinstance(repeat, bool):
            raise ValueError("repeat exige booleano")
        self._original = replace(controls)
        self._started = timestamp
        self._last_now = timestamp
        self._repeat = repeat
        self._running = True
        return self._frame(0.0)

    def update(self, now: float) -> CycleFrame | None:
        timestamp = _clock(now)
        if self._last_now is not None and timestamp < self._last_now:
            raise ValueError("now não pode retroceder")
        if not self._running:
            return None
        self._last_now = timestamp
        elapsed = timestamp - self._started
        if not self._repeat and elapsed >= _DURATION:
            self._running = False
            return None
        return self._frame(elapsed)

    def stop(self) -> Controls | None:
        original = self._original
        self._original = None
        self._running = False
        self._last_now = None
        return replace(original) if original is not None else None

    def _frame(self, elapsed: float) -> CycleFrame:
        completed, position = divmod(elapsed, _DURATION)
        index = bisect_right(_STARTS, position) - 1
        name, controls = _PHASES[index]
        return CycleFrame(name, index, int(completed) + 1,
                          position - _STARTS[index], elapsed, replace(controls))
