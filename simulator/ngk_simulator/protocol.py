"""Pure encoder for the bounded, line-oriented NGK ECU simulation protocol 1."""

from decimal import Decimal, ROUND_HALF_UP

from .model import Sample, _finite_number, _integer_range, _validated_faults

UINT32_MAX = 4294967295


def _scaled(value: object, name: str, low: float, high: float, scale: int) -> int:
    number = _finite_number(value, name)
    if not low <= number <= high:
        raise ValueError(f"{name}: valor fora de {low}..{high}")
    # Decimal text preserves human slider half-units, including negative C,
    # without binary-float ties or Python's round-to-even convention.
    return int((Decimal(str(number)) * scale).quantize(Decimal("1"), rounding=ROUND_HALF_UP))


def encode_frame(session: int, sequence: int, sample: Sample) -> bytes:
    """Validate and encode one ASCII LF frame; never opens or writes a transport."""
    token = _integer_range(session, "session", 1, UINT32_MAX)
    seq = _integer_range(sequence, "sequence", 1, UINT32_MAX)
    if not isinstance(sample, Sample):
        raise ValueError("sample precisa ser uma Sample")
    fields = (
        token,
        seq,
        _scaled(sample.rpm, "rpm", 0, 12000, 1),
        _scaled(sample.speed_kph, "speed_kph", 0, 250, 10),
        _scaled(sample.block_c, "block_c", -40, 200, 10),
        _scaled(sample.throttle_pct, "throttle_pct", 0, 100, 10),
        _integer_range(sample.gear, "gear", -1, 4),
        _integer_range(sample.oil, "oil", 0, 2),
        int(_validated_faults(sample.faults)),
    )
    encoded = ("SIM1 " + " ".join(str(value) for value in fields) + "\n").encode("ascii")
    if len(encoded) > 95:
        raise ValueError("Frame SIM1 excede 95 bytes")
    return encoded
