"""Bounded synthetic transport used by the real-time Qt soak; no hardware."""
import importlib.util
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("simulator_soak", ROOT / "scripts/soak-simulator.py")
soak = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = soak
SPEC.loader.exec_module(soak)


class SoakTransportTests(unittest.TestCase):
    def setUp(self):
        self.now = 10.0
        self.bus = soak.MemoryBus()
        self.device = soak.MemorySerial(self.bus, clock=lambda: self.now)
        self.device.open()

    def reply(self, command):
        self.device.write(command)
        return json.loads(self.device.read(512))

    def test_protocol_ttl_and_session_validation_without_retaining_frames(self):
        self.assertEqual(self.reply(b"info\n")["simulation_protocol"], 1)
        self.assertEqual(self.reply(b"sim-start 123\n")["state"], "ready")
        self.device.write(b"SIM1 123 1 2450 325 850 400 2 0 0\n")
        self.assertTrue(self.reply(b"sim-status\n")["live"])
        self.now += .751
        self.assertFalse(self.reply(b"sim-status\n")["live"])
        self.device.write(b"SIM1 999 2 2450 325 850 400 2 0 0\n")
        self.device.write(b"SIM1 123 1 2450 325 850 400 2 0 0\n")
        self.device.write(b"SIM1 123 2 2450 325 850 400 2 0 0\n")
        self.assertEqual(self.bus.snapshot()["frames"], 2)
        self.assertEqual(self.bus.snapshot()["rejected_frames"], 2)
        self.assertNotIn("writes", vars(self.device))
        self.assertNotIn("incoming", vars(self.device))
        self.assertEqual(self.reply(b"sim-stop 123\n")["state"], "off")
        self.device.close()
        self.assertEqual(self.bus.snapshot()["active_devices"], 0)

    def test_replies_have_fixed_capacity_and_export_has_no_session(self):
        for _ in range(1000):
            self.device.write(b"info\n")
        self.assertLessEqual(len(self.device._reply), 2048)
        self.assertGreater(self.bus.snapshot()["reply_overflows"], 0)
        exported = json.dumps(self.bus.snapshot())
        for forbidden in ("session", "port", "raw", "nonce"):
            self.assertNotIn(forbidden, exported)

    def test_numeric_aggregate_retains_only_summary(self):
        summary = soak.RunningStats()
        for number in range(1000):
            summary.add(number)
        self.assertEqual(summary.export()["count"], 1000)
        self.assertEqual(summary.export()["mean"], 499.5)
        self.assertEqual(summary.export()["max"], 999)
        self.assertFalse(any(isinstance(value, list) for value in vars(summary).values()))


if __name__ == "__main__":
    unittest.main()
