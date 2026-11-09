"""Golden ASCII frames shared with the pure C++ parser; no transport is opened."""

from dataclasses import replace
import math
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "simulator"))

from ngk_simulator.model import Fault, Sample
from ngk_simulator.protocol import encode_frame


class ProtocolTests(unittest.TestCase):
    def test_default_frame_is_complete_ascii_with_one_lf(self):
        self.assertEqual(encode_frame(1, 1, Sample()), b"SIM1 1 1 0 0 850 0 0 0 0\n")

    def test_cpp_shared_golden_and_largest_fields(self):
        sample = Sample(rpm=850, speed_kph=12.3, block_c=-40, throttle_pct=100,
                        gear=-1, oil=2, faults=Fault.LOW_OIL)
        self.assertEqual(encode_frame(305419896, 1, sample),
                         b"SIM1 305419896 1 850 123 -400 1000 -1 2 16\n")
        maximum = Sample(rpm=12000, speed_kph=250, block_c=200, throttle_pct=100,
                         gear=-1, oil=2, faults=Fault(251))
        encoded = encode_frame(4294967295, 4294967295, maximum)
        self.assertEqual(encoded,
                         b"SIM1 4294967295 4294967295 12000 2500 2000 1000 -1 2 251\n")
        self.assertLessEqual(len(encoded), 95)

    def test_half_units_round_away_from_zero_without_locale_or_exponents(self):
        sample = Sample(rpm=3141.5, speed_kph=42.25, block_c=-3.25, throttle_pct=12.25,
                        gear=-1, oil=1, faults=Fault.LOW_OIL)
        self.assertEqual(encode_frame(1, 2, sample), b"SIM1 1 2 3142 423 -33 123 -1 1 16\n")
        self.assertEqual(encode_frame(1, 3, replace(sample, block_c=-0.05)).split()[5], b"-1")

    def test_session_and_sequence_require_nonzero_uint32_integers(self):
        for invalid in (0, -1, 4294967296, 1.0, True, None, "1"):
            for position in (0, 1):
                arguments = [1, 1, Sample()]
                arguments[position] = invalid
                with self.subTest(value=invalid, position=position), self.assertRaises(ValueError):
                    encode_frame(*arguments)

    def test_sample_fields_reject_nonfinite_or_out_of_range_before_rounding(self):
        cases = {"rpm": (-0.01, 12000.01), "speed_kph": (-0.01, 250.01),
                 "block_c": (-40.01, 200.01), "throttle_pct": (-0.01, 100.01)}
        for field, outside in cases.items():
            for value in (*outside, math.nan, math.inf, -math.inf, True, None, "1"):
                with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                    encode_frame(1, 1, replace(Sample(), **{field: value}))

    def test_integer_discrete_fields_and_exclusive_temperature_faults_are_checked(self):
        for field, values in {"gear": (-2, 5, True, 1.0), "oil": (-1, 3, True, 0.0),
                              "faults": (-1, 256, True, 1.0, Fault.TEMP_OPEN | Fault.TEMP_SHORT)}.items():
            for value in values:
                with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                    encode_frame(1, 1, replace(Sample(), **{field: value}))


if __name__ == "__main__":
    unittest.main()
