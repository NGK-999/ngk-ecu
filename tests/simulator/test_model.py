"""Behavioral checks for the illustrative vehicle; no calibrated vehicle claims."""

from dataclasses import FrozenInstanceError, replace
import math
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "simulator"))

from ngk_simulator.model import Controls, Fault, Sample, VehicleModel, set_fault


def run_for(model, controls, seconds, dt=0.05):
    sample = Sample()
    for _ in range(round(seconds / dt)):
        sample = model.step(controls, dt)
    return sample


class ModelTests(unittest.TestCase):
    def test_initial_and_reset_state_matches_stopped_default_controls(self):
        model = VehicleModel()
        self.assertEqual(model.step(Controls(), 0.033), Sample())
        run_for(model, Controls(engine_on=True, throttle_pct=80, gear=1), 2)
        model.reset()
        self.assertEqual(model.step(Controls(), 0.033), Sample())
        with self.assertRaises(FrozenInstanceError):
            Sample().rpm = 1

    def test_invalid_time_steps_are_rejected_without_advancing_state(self):
        driving = Controls(engine_on=True, throttle_pct=80, gear=1)
        for invalid in (-1, math.inf, -math.inf, math.nan, None, "0.1", True):
            model, reference = VehicleModel(), VehicleModel()
            model.step(driving, 0.1)
            reference.step(driving, 0.1)
            with self.subTest(dt=invalid), self.assertRaises(ValueError):
                model.step(driving, invalid)
            self.assertEqual(model.step(driving, 0.1), reference.step(driving, 0.1))

    def test_zero_time_holds_dynamics_and_long_steps_are_bounded(self):
        controls = Controls(engine_on=True, throttle_pct=100, gear=1)
        model, reference = VehicleModel(), VehicleModel()
        previous = model.step(controls, 0.1)
        self.assertEqual(model.step(controls, 0), previous)
        self.assertEqual(reference.step(controls, 999), previous)

    def test_throttle_in_neutral_revs_without_driving(self):
        sample = run_for(VehicleModel(), Controls(engine_on=True, throttle_pct=80), 3)
        self.assertGreater(sample.rpm, 3000)
        self.assertEqual(sample.speed_kph, 0)

    def test_reverse_and_each_forward_gear_drive_within_the_protocol_limits(self):
        speeds = []
        for gear in (-1, 1, 2, 3, 4):
            sample = run_for(VehicleModel(), Controls(engine_on=True, throttle_pct=80, gear=gear), 4)
            with self.subTest(gear=gear):
                self.assertEqual(sample.gear, gear)
                self.assertGreater(sample.speed_kph, 0)
                self.assertGreater(sample.rpm, 0)
                self.assertLessEqual(sample.speed_kph, 250)
                self.assertLessEqual(sample.rpm, 12000)
            speeds.append(sample.speed_kph)
        self.assertGreater(speeds[1], speeds[-1])

    def test_clutch_decouples_engine_and_vehicle_and_braking_stops_it(self):
        model = VehicleModel()
        controls = Controls(engine_on=True, throttle_pct=100, gear=1)
        moving = run_for(model, controls, 4)
        controls.clutch = True
        coasting = run_for(model, controls, 0.5)
        self.assertLess(coasting.speed_kph, moving.speed_kph)
        self.assertGreater(coasting.rpm, moving.rpm)
        controls.throttle_pct = 0
        controls.brake_pct = 100
        stopped = run_for(model, controls, 6)
        self.assertEqual(stopped.speed_kph, 0)
        self.assertGreater(stopped.rpm, 500)

    def test_engine_off_and_neutral_both_allow_decelerating_coast(self):
        for changed in ({"engine_on": False}, {"gear": 0}):
            model = VehicleModel()
            controls = Controls(engine_on=True, throttle_pct=100, gear=2)
            moving = run_for(model, controls, 4)
            controls = replace(controls, **changed)
            after = run_for(model, controls, 0.5)
            with self.subTest(changed=changed):
                self.assertGreater(after.speed_kph, 0)
                self.assertLess(after.speed_kph, moving.speed_kph)

    def test_manual_mode_overrides_dynamics_and_resumes_from_that_speed(self):
        model = VehicleModel()
        controls = Controls(manual=True, manual_rpm=5000, manual_speed_kph=123.4, block_c=-20)
        sample = model.step(controls, 0.05)
        self.assertEqual((sample.rpm, sample.speed_kph, sample.block_c), (5000, 123.4, -20))
        controls.manual = False
        controls.brake_pct = 100
        controls.clutch = True
        after = model.step(controls, 0.1)
        self.assertGreater(after.speed_kph, 0)
        self.assertLess(after.speed_kph, sample.speed_kph)
        self.assertLess(after.rpm, sample.rpm)

    def test_finite_slider_values_are_clamped_without_mutating_controls(self):
        controls = Controls(manual=True, throttle_pct=200, brake_pct=-20,
                            manual_rpm=13000, manual_speed_kph=999, block_c=300)
        previous = replace(controls)
        sample = VehicleModel().step(controls, 0.05)
        self.assertEqual((sample.rpm, sample.speed_kph, sample.block_c, sample.throttle_pct),
                         (12000, 250, 200, 100))
        self.assertEqual(controls, previous)
        controls.manual_rpm = -1
        controls.manual_speed_kph = -1
        controls.block_c = -100
        sample = VehicleModel().step(controls, 0.05)
        self.assertEqual((sample.rpm, sample.speed_kph, sample.block_c), (0, 0, -40))

    def test_all_numeric_controls_must_be_finite_even_when_not_active(self):
        for name in ("throttle_pct", "brake_pct", "block_c", "manual_rpm", "manual_speed_kph"):
            for value in (math.nan, math.inf, -math.inf, None, "20", True):
                controls = Controls()
                setattr(controls, name, value)
                with self.subTest(name=name, value=value), self.assertRaises(ValueError):
                    VehicleModel().step(controls, 0.05)

    def test_gear_and_fault_fields_reject_invalid_values(self):
        for gear in (-2, 5, 1.0, math.nan, True, "N"):
            with self.subTest(gear=gear), self.assertRaises(ValueError):
                VehicleModel().step(Controls(gear=gear), 0.05)
        for faults in (256, -1, True, Fault.TEMP_OPEN | Fault.TEMP_SHORT):
            with self.subTest(faults=faults), self.assertRaises(ValueError):
                VehicleModel().step(Controls(faults=faults), 0.05)

    def test_faults_preserve_sensor_values_and_mark_validity_without_sentinels(self):
        mask = Fault.NO_RPM | Fault.NO_SPEED | Fault.TPS_INVALID | Fault.GEAR_INVALID | Fault.TEMP_OPEN
        controls = Controls(manual=True, manual_rpm=2000, manual_speed_kph=40,
                            throttle_pct=50, gear=2, faults=mask)
        sample = VehicleModel().step(controls, 0.05)
        self.assertEqual((sample.rpm, sample.speed_kph, sample.throttle_pct, sample.gear, sample.block_c),
                         (2000, 40, 50, 2, 85))
        self.assertEqual(sample.faults, mask)

    def test_low_oil_and_overheat_have_visible_illustrative_values(self):
        controls = Controls(faults=Fault.LOW_OIL | Fault.OVERHEAT)
        sample = VehicleModel().step(controls, 0.05)
        self.assertEqual(sample.oil, 1)
        self.assertGreaterEqual(sample.block_c, 150)
        controls.faults = Fault.NONE
        clean = VehicleModel().step(controls, 0.05)
        self.assertEqual((clean.oil, clean.block_c), (0, 85))

    def test_fault_helper_excludes_open_short_and_clearing_does_not_change_controls(self):
        controls = Controls(engine_on=True, throttle_pct=57, gear=3)
        set_fault(controls, Fault.TEMP_OPEN, True)
        set_fault(controls, Fault.TEMP_SHORT, True)
        self.assertEqual(controls.faults, Fault.TEMP_SHORT)
        set_fault(controls, Fault.LOW_OIL, True)
        set_fault(controls, Fault.TEMP_SHORT, False)
        self.assertEqual(controls.faults, Fault.LOW_OIL)
        previous = replace(controls, faults=Fault.NONE)
        controls.faults = Fault.NONE
        self.assertEqual(controls, previous)

    def test_sustained_acceleration_remains_finite_and_bounded(self):
        sample = run_for(VehicleModel(), Controls(engine_on=True, throttle_pct=100, gear=1), 100, 0.1)
        self.assertTrue(all(math.isfinite(value) for value in
                            (sample.rpm, sample.speed_kph, sample.block_c, sample.throttle_pct)))
        self.assertGreaterEqual(sample.rpm, 0)
        self.assertLessEqual(sample.rpm, 12000)
        self.assertGreaterEqual(sample.speed_kph, 0)
        self.assertLessEqual(sample.speed_kph, 250)


if __name__ == "__main__":
    unittest.main()
