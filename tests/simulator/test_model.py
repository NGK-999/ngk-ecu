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

    def test_warm_idle_varies_smoothly_around_700_without_drift(self):
        controls = Controls(engine_on=True, block_c=85)
        model = VehicleModel()
        run_for(model, controls, 15)
        rpm = [model.step(controls, 0.05).rpm for _ in range(1200)]
        self.assertGreater(min(rpm), 670)
        self.assertLess(max(rpm), 730)
        self.assertGreater(max(rpm) - min(rpm), 12)
        self.assertLess(max(abs(right - left) for left, right in zip(rpm, rpm[1:])), 3)
        self.assertLess(abs(sum(rpm[:200]) / 200 - sum(rpm[-200:]) / 200), 25)

    def test_cold_idle_is_higher_than_warm_idle(self):
        cold = run_for(VehicleModel(), Controls(engine_on=True, block_c=25), 20)
        warm = run_for(VehicleModel(), Controls(engine_on=True, block_c=85), 20)
        self.assertGreater(cold.rpm, 950)
        self.assertLess(cold.rpm, 1050)
        self.assertGreater(cold.rpm - warm.rpm, 200)

    def test_rpm_has_inertia_and_a_model_ceiling_with_exact_manual_override(self):
        model = VehicleModel()
        controls = Controls(engine_on=True)
        idle = run_for(model, controls, 10)
        controls.throttle_pct = 100
        rising = model.step(controls, 0.05)
        self.assertGreater(rising.rpm, idle.rpm)
        self.assertLess(rising.rpm, 2000)
        revved = run_for(model, controls, 30)
        self.assertGreater(revved.rpm, 4500)
        self.assertLessEqual(revved.rpm, 5000)
        controls.throttle_pct = 0
        falling = model.step(controls, 0.05)
        self.assertLess(falling.rpm, revved.rpm)
        self.assertGreater(falling.rpm, 3000)
        controls.manual = True
        controls.manual_rpm = 9000
        self.assertEqual(model.step(controls, 0.05).rpm, 9000)
        controls.manual_rpm = 12000
        self.assertEqual(model.step(controls, 0.05).rpm, 12000)

    def test_engaged_gears_do_not_accelerate_indefinitely(self):
        limits = {-1: 50, 1: 50, 2: 80, 3: 115, 4: 155}
        speeds = {}
        for gear, ceiling in limits.items():
            model = VehicleModel()
            controls = Controls(engine_on=True, throttle_pct=100, gear=gear)
            sample = run_for(model, controls, 180, 0.1)
            with self.subTest(gear=gear):
                self.assertGreater(sample.speed_kph, 0)
                self.assertLess(sample.speed_kph, ceiling)
                self.assertLessEqual(sample.rpm, 5000)
            speeds[gear] = sample.speed_kph
        self.assertGreater(speeds[4], speeds[1])

    def test_stopped_engine_reaches_exact_zero_and_does_not_idle_oscillate(self):
        model = VehicleModel()
        controls = Controls(engine_on=True, throttle_pct=100)
        run_for(model, controls, 10)
        controls.engine_on = False
        run_for(model, controls, 10)
        self.assertEqual([model.step(controls, 0.05).rpm for _ in range(100)], [0] * 100)

    def test_reset_repeats_the_entire_idle_oscillation(self):
        controls = Controls(engine_on=True, block_c=25, thermal_auto=True)
        model = VehicleModel()
        first = [model.step(controls, 0.05) for _ in range(500)]
        model.reset()
        self.assertEqual([model.step(controls, 0.05) for _ in range(500)], first)

    def test_automatic_surface_temperature_warms_and_cools_gradually(self):
        controls = Controls(engine_on=True, block_c=25, thermal_auto=True)
        model = VehicleModel()
        self.assertEqual(model.step(controls, 0).block_c, 25)
        first = model.step(controls, 0.05)
        self.assertGreater(first.block_c, 25)
        self.assertLess(first.block_c, 25.2)
        warm = run_for(model, controls, 30)
        self.assertGreater(warm.block_c, first.block_c + 1)
        self.assertLess(warm.block_c, 85)
        controls.engine_on = False
        cooling = model.step(controls, 0.05)
        self.assertLess(cooling.block_c, warm.block_c)
        self.assertGreater(cooling.block_c, warm.block_c - 0.2)
        cooled = run_for(model, controls, 30)
        self.assertGreater(cooled.block_c, 25)
        self.assertLess(cooled.block_c, cooling.block_c)

    def test_automatic_temperature_has_synthetic_load_and_airflow_effects(self):
        idle = Controls(engine_on=True, block_c=85, thermal_auto=True)
        loaded = replace(idle, throttle_pct=100, gear=4)
        self.assertGreater(run_for(VehicleModel(), loaded, 30).block_c,
                           run_for(VehicleModel(), idle, 30).block_c)
        slow, fast = VehicleModel(), VehicleModel()
        seed = replace(loaded, manual=True, manual_speed_kph=100, manual_rpm=3000)
        fast.step(seed, 0.05)
        self.assertLess(run_for(fast, loaded, 5).block_c,
                        run_for(slow, loaded, 5).block_c)
        sample = run_for(VehicleModel(), loaded, 600, 0.1)
        self.assertGreaterEqual(sample.block_c, 85)
        self.assertLessEqual(sample.block_c, 110)

    def test_thermal_state_is_held_at_zero_dt_and_reseeded_by_explicit_input(self):
        controls = Controls(engine_on=True, block_c=25, thermal_auto=True)
        model = VehicleModel()
        warm = run_for(model, controls, 30)
        self.assertEqual(model.step(controls, 0), warm)
        controls.block_c = 80
        self.assertEqual(model.step(controls, 0).block_c, 80)
        after = model.step(controls, 0.1)
        self.assertGreater(after.block_c, 80)
        self.assertLess(after.block_c, 80.2)
        controls.thermal_auto = False
        self.assertEqual(model.step(controls, 0.1).block_c, 80)

    def test_manual_temperature_stays_exact_and_can_reseed_automatic_mode(self):
        model = VehicleModel()
        controls = Controls(engine_on=True, block_c=200, thermal_auto=True,
                            manual=True, manual_rpm=9000, manual_speed_kph=100)
        self.assertEqual(run_for(model, controls, 5).block_c, 200)
        controls.block_c = 30
        self.assertEqual(model.step(controls, 0.1).block_c, 30)
        controls.manual = False
        self.assertEqual(model.step(controls, 0).block_c, 30)
        self.assertGreater(model.step(controls, 0.1).block_c, 30)

    def test_overheat_output_does_not_leave_the_automatic_thermal_state_hot(self):
        controls = Controls(engine_on=True, block_c=85, thermal_auto=True, faults=Fault.OVERHEAT)
        model = VehicleModel()
        self.assertGreaterEqual(model.step(controls, 0.05).block_c, 150)
        controls.faults = Fault.NONE
        self.assertLess(model.step(controls, 0.05).block_c, 110)

    def test_thermal_auto_is_boolean_and_invalid_input_preserves_dynamics(self):
        self.assertFalse(Controls().thermal_auto)
        controls = Controls(engine_on=True, block_c=25, thermal_auto=True)
        model, reference = VehicleModel(), VehicleModel()
        model.step(controls, 0.1)
        reference.step(controls, 0.1)
        for invalid in (None, 1, "sim"):
            controls.thermal_auto = invalid
            with self.subTest(value=invalid), self.assertRaises(ValueError):
                model.step(controls, 0.1)
        controls.thermal_auto = True
        self.assertEqual(model.step(controls, 0.1), reference.step(controls, 0.1))

    def test_a_long_pause_cannot_jump_the_automatic_surface_temperature(self):
        controls = Controls(engine_on=True, throttle_pct=100, gear=1,
                            block_c=25, thermal_auto=True)
        self.assertEqual(VehicleModel().step(controls, 900), VehicleModel().step(controls, 0.1))

    def test_leaving_high_manual_rpm_decays_with_inertia_instead_of_a_cut(self):
        controls = Controls(engine_on=True, manual=True, manual_rpm=9000)
        model = VehicleModel()
        model.step(controls, 0.05)
        controls.manual = False
        first = model.step(controls, 0.05)
        self.assertGreater(first.rpm, 5000)
        self.assertLess(first.rpm, 9000)
        self.assertLess(run_for(model, controls, 20).rpm, 5000)


if __name__ == "__main__":
    unittest.main()
