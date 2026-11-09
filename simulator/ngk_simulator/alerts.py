"""Illustrative bench warnings; these never set an engine speed or cut output."""
from enum import Enum


class RpmWarning(Enum):
    NORMAL = 0
    HIGH = 1
    CRITICAL = 2


def rpm_warning(rpm: float, valid: bool,
                previous: RpmWarning = RpmWarning.NORMAL) -> RpmWarning:
    """4000/4500 RPM display references, with 150 RPM release hysteresis.

    Neither the gauge scale nor these defaults certify an installed engine's
    operating limit. Invalid samples clear state instead of holding an alarm.
    """
    if not valid:
        return RpmWarning.NORMAL
    if rpm >= 4500 or (previous is RpmWarning.CRITICAL and rpm >= 4350):
        return RpmWarning.CRITICAL
    if rpm >= 4000 or (previous is not RpmWarning.NORMAL and rpm >= 3850):
        return RpmWarning.HIGH
    return RpmWarning.NORMAL
