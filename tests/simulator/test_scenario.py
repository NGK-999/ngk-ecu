"""Pure synthetic cycle checks; no Qt, serial port, or hardware."""

from dataclasses import replace
import math
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "simulator"))

from ngk_simulator.model import Controls, Fault, VehicleModel
from ngk_simulator.scenario import DriveCycle


class DriveCycleTests(unittest.TestCase):
    def test_start_returns_cold_stopped_controls_and_does_not_mutate_original(self):
        cycle = DriveCycle()
        original = Controls(engine_on=True, manual=True, manual_rpm=9000,
                            manual_speed_kph=120, block_c=150, gear=4,
                            throttle_pct=100, faults=Fault.NO_RPM)
        saved = replace(original)
        self.assertFalse(cycle.running)
        frame = cycle.start(100, original)
        self.assertTrue(cycle.running)
        self.assertEqual((frame.phase_name, frame.index, frame.cycle_number),
                         ("Parado", 0, 1))
        self.assertEqual((frame.phase_elapsed, frame.total_elapsed), (0, 0))
        self.assertEqual(frame.controls, Controls(block_c=25, thermal_auto=True))
        self.assertEqual(original, saved)
        self.assertIsNot(frame.controls, original)

    def test_every_exact_phase_boundary_and_duration(self):
        cycle = DriveCycle()
        cycle.start(10, Controls())
        phases = [
            (0, 4, "Parado", False, 0, 0, 0, Fault.NONE),
            (4, 8, "Partida / lenta", True, 0, 0, 0, Fault.NONE),
            (12, 6, "Primeira", True, 1, 45, 0, Fault.NONE),
            (18, 6, "Segunda", True, 2, 60, 0, Fault.NONE),
            (24, 6, "Terceira", True, 3, 65, 0, Fault.NONE),
            (30, 6, "Quarta", True, 4, 35, 0, Fault.NONE),
            (36, 8, "Frenagem", True, 0, 0, 70, Fault.NONE),
            (44, 6, "Falhas", True, 0, 0, 0, Fault.LOW_OIL | Fault.OVERHEAT),
            (50, 6, "Recuperação", True, 0, 0, 0, Fault.NONE),
            (56, 4, "Desligamento", False, 0, 0, 0, Fault.NONE),
        ]
        for index, (start, duration, name, engine, gear, throttle, brake, faults) in enumerate(phases):
            with self.subTest(phase=name):
                frame = cycle.update(10 + start)
                self.assertEqual((frame.index, frame.phase_name, frame.cycle_number), (index, name, 1))
                self.assertEqual((frame.phase_elapsed, frame.total_elapsed), (0, start))
                controls = frame.controls
                self.assertEqual((controls.engine_on, controls.gear, controls.throttle_pct,
                                  controls.brake_pct, controls.faults),
                                 (engine, gear, throttle, brake, faults))
                self.assertFalse(controls.manual)
                self.assertTrue(controls.thermal_auto)
                self.assertEqual((controls.block_c, controls.manual_rpm, controls.manual_speed_kph),
                                 (25, 0, 0))
                VehicleModel().step(controls, 0.05)
                end = cycle.update(10 + start + duration - 0.001)
                self.assertEqual(end.index, index)
                self.assertAlmostEqual(end.phase_elapsed, duration - 0.001)
        repeated = cycle.update(70)
        self.assertEqual((repeated.index, repeated.cycle_number, repeated.phase_elapsed), (0, 2, 0))

    def test_large_time_jump_skips_cycles_and_reports_absolute_elapsed(self):
        cycle = DriveCycle()
        cycle.start(123, Controls())
        elapsed = 60 * 10**12 + 25.5
        frame = cycle.update(123 + elapsed)
        self.assertEqual((frame.index, frame.cycle_number), (4, 10**12 + 1))
        self.assertEqual((frame.phase_elapsed, frame.total_elapsed), (1.5, elapsed))
        self.assertEqual(cycle.update(123 + elapsed), frame)

    def test_single_cycle_finishes_at_sixty_and_stop_still_restores_snapshot(self):
        for end in (60, 61, 10**12):
            with self.subTest(end=end):
                original = Controls(manual=True, manual_rpm=3210, gear=-1)
                saved = replace(original)
                cycle = DriveCycle()
                cycle.start(10, original, repeat=False)
                original.gear = 4
                self.assertIsNone(cycle.update(10 + end))
                self.assertFalse(cycle.running)
                self.assertIsNone(cycle.update(10 + end + 1))
                restored = cycle.stop()
                self.assertEqual(restored, saved)
                self.assertIsNot(restored, original)
                self.assertIsNone(cycle.stop())

    def test_mutating_frames_does_not_corrupt_templates_or_original(self):
        original = Controls(manual=True, throttle_pct=99, block_c=120)
        saved = replace(original)
        cycle = DriveCycle()
        first = cycle.start(0, original)
        first.controls.block_c = -30
        first.controls.manual = True
        self.assertEqual(cycle.update(0).controls, Controls(block_c=25, thermal_auto=True))
        moving = cycle.update(12)
        moving.controls.throttle_pct = 999
        self.assertEqual(cycle.update(12).controls.throttle_pct, 45)
        self.assertEqual(cycle.update(72).controls.throttle_pct, 45)
        self.assertEqual(original, saved)
        self.assertEqual(cycle.stop(), saved)
        self.assertFalse(cycle.running)
        self.assertIsNone(cycle.update(73))

    def test_invalid_start_does_not_activate_cycle(self):
        for invalid in (-1, math.nan, math.inf, -math.inf, True, None, "1", 10**1000):
            with self.subTest(now=invalid):
                cycle = DriveCycle()
                with self.assertRaises(ValueError):
                    cycle.start(invalid, Controls())
                self.assertFalse(cycle.running)
                self.assertIsNone(cycle.stop())

    def test_invalid_or_reversed_update_preserves_last_valid_clock(self):
        cycle = DriveCycle()
        cycle.start(10, Controls())
        expected = cycle.update(22)
        for invalid in (21.999, -1, math.nan, math.inf, -math.inf, True, None, "22", 10**1000):
            with self.subTest(now=invalid), self.assertRaises(ValueError):
                cycle.update(invalid)
            self.assertEqual(cycle.update(22), expected)

    def test_stop_before_start_is_noop_and_restart_uses_new_snapshot(self):
        cycle = DriveCycle()
        self.assertIsNone(cycle.stop())
        self.assertIsNone(cycle.update(0))
        cycle.start(10, Controls(gear=2))
        with self.assertRaises(RuntimeError):
            cycle.start(11, Controls(gear=3))
        self.assertEqual(cycle.stop().gear, 2)
        cycle.start(0, Controls(gear=4), repeat=False)
        self.assertEqual(cycle.update(0).cycle_number, 1)
        self.assertEqual(cycle.stop().gear, 4)


if __name__ == "__main__":
    unittest.main()
