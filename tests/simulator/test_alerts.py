"""RPM warnings are display references, independent of gauge scale or engine control."""
import unittest

from ngk_simulator.alerts import RpmWarning, rpm_warning


class RpmWarningTests(unittest.TestCase):
    def test_rising_rpm_crosses_warning_and_critical_thresholds(self):
        for rpm, expected in ((0, RpmWarning.NORMAL), (3999, RpmWarning.NORMAL),
                              (4000, RpmWarning.HIGH), (4499, RpmWarning.HIGH),
                              (4500, RpmWarning.CRITICAL), (9000, RpmWarning.CRITICAL)):
            with self.subTest(rpm=rpm):
                self.assertEqual(rpm_warning(rpm, True), expected)

    def test_hysteresis_prevents_chatter_and_releases_each_level(self):
        state = RpmWarning.NORMAL
        for rpm, expected in ((4500, RpmWarning.CRITICAL), (4400, RpmWarning.CRITICAL),
                              (4350, RpmWarning.CRITICAL), (4349, RpmWarning.HIGH),
                              (3900, RpmWarning.HIGH), (3850, RpmWarning.HIGH),
                              (3849, RpmWarning.NORMAL), (3900, RpmWarning.NORMAL)):
            state = rpm_warning(rpm, True, state)
            self.assertEqual(state, expected, rpm)

    def test_missing_rpm_clears_previous_warning_even_with_old_high_value(self):
        self.assertEqual(rpm_warning(9000, False, RpmWarning.CRITICAL), RpmWarning.NORMAL)
        self.assertEqual(rpm_warning(3900, True, RpmWarning.NORMAL), RpmWarning.NORMAL)


if __name__ == '__main__':
    unittest.main()
