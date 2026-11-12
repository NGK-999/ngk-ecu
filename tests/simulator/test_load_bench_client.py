"""The load bench uses an in-memory UART and a deterministic clock only."""

import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch


SCRIPT = Path(__file__).resolve().parents[2] / "tools" / "benchmark_load.py"


def load_client():
    spec = importlib.util.spec_from_file_location("benchmark_load", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Clock:
    def __init__(self):
        self.now = 0.0

    def monotonic(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


def info_fixture():
    return {"project": "NGK ECU", "firmware": "0.4.11-load-bench",
            "build": "load-benchmark", "mode": "bench",
            "sensor_inputs_enabled": False, "actuators_enabled": False,
            "simulation_protocol": 1, "load_benchmark_protocol": 1,
            "simulation_enabled": False, "refresh_mode": 0,
            "reduced_motion": False, "profile_persisted": True,
            "sd_mounted": True, "profile_revision": 3, "profile_status": 12}


def radios_fixture():
    return {"kind": "radios", "running": True, "memory_failure": False,
            "wifi_enabled": False, "ble_enabled": False,
            "wifi_connected": False, "wifi_connecting": False,
            "wifi_scanning": False, "ble_advertising": False,
            "ble_connected": False, "ble_pairing_pending": False}


def status_fixture(**changes):
    result = {"kind": "load_benchmark", "protocol": 1, "id": 7,
              "mode": 1, "state": "done", "duration_ms": 15000,
              "elapsed_us": 15_000_000, "channels": 16, "rate_hz": 100,
              "phase_started_us": 1_000_000, "phase_finished_us": 16_000_000,
              "iterations": 32, "produced": 1500, "consumed": 1400,
              "overwritten": 100, "pending": 0, "sequence_gaps": 100,
              "missed_periods": 0, "dispatch_max_us": 20,
              "consume_max_us": 40, "kernel_total_us": 15000,
              "kernel_max_us": 10, "checksum": 12345,
              "heap_free_bytes": 100000, "heap_min_bytes": 95000,
              "heap_largest_bytes": 80000, "stack_main_min_bytes": 3000,
              "stack_sd_min_bytes": 4000, "stack_radio_min_bytes": 3000,
              "stack_load_min_bytes": 2000}
    result.update(changes)
    return result


def ui_fixture():
    return {"kind": "ui_motion_benchmark", "synthetic": True,
            "interrupted": False, "duration_ms": 10000,
            "presentations": 590, "observed_start_us": 2_000_000,
            "observed_end_us": 12_000_000, "interval_p95_us": 17000}


class MemoryPort:
    """A small stateful peer; all SIM writes are consumed, never retained."""

    def __init__(self, clock, *, lost_ack=False, ui_ok=True, observe_radio=True,
                 original_wifi=False, fail_stream=False):
        self.clock = clock
        self.incoming = bytearray()
        self.commands = []
        self.sim_live = False
        self.accepted = 0
        self.frames = 0
        self.load_started = None
        self.load_id = 0
        self.mode = 1
        self.duration = 15000
        self.load_stopped = False
        self.motion_started = None
        self.ui_sent = False
        self.lost_ack = lost_ack
        self.ui_ok = ui_ok
        self.observe_radio = observe_radio
        self.fail_stream = fail_stream
        self.radios = {**radios_fixture(), "wifi_enabled": original_wifi}
        self.revision = 3
        self.radio_lag = 0
        self.delayed_radio_off = False

    def queue(self, value):
        line = value if isinstance(value, str) else json.dumps(value)
        self.incoming.extend(line.encode() + b"\n")

    def read(self, count):
        if (self.motion_started is not None and not self.ui_sent
                and self.clock.now - self.motion_started >= 10):
            self.queue({**ui_fixture(), "interrupted": not self.ui_ok,
                        "observed_start_us": round(self.motion_started * 1e6),
                        "observed_end_us": round((self.motion_started + 10) * 1e6)})
            self.ui_sent = True
        data = self.incoming[:count]
        del self.incoming[:count]
        return data

    def write(self, data):
        line = data.decode().strip()
        if line.startswith("SIM1 "):
            if self.fail_stream:
                self.fail_stream = False
                raise OSError("private UART contents")
            self.frames += 1
            self.accepted = int(line.split()[2])
            return len(data)
        command = line.split()[0]
        self.commands.append(command)
        if command == "info":
            self.queue({**info_fixture(), "simulation_enabled": self.sim_live,
                        "profile_revision": self.revision})
        elif command == "radios":
            if self.radio_lag:
                self.radio_lag -= 1
                if not self.radio_lag:
                    for field in ("wifi_scanning", "ble_advertising", "wifi_connected",
                                  "wifi_connecting", "ble_connected", "ble_pairing_pending"):
                        self.radios[field] = False
            self.queue({**self.radios, "ssid": "PRIVATE NETWORK"})
        elif command == "sim-start":
            self.sim_live = True
            self.queue({"kind": "sim", "state": "ready", "protocol": 1,
                        "session": int(line.split()[1])})
        elif command in ("sim-status", "sim-stop"):
            if command == "sim-stop":
                self.sim_live = False
            self.queue({"kind": "sim", "protocol": 1, "live": self.sim_live,
                        "state": "live" if self.sim_live else "off", "seq": self.accepted})
        elif command == "load-start":
            _, job, mode, duration = line.split()
            self.load_id, self.mode, self.duration = int(job), int(mode), int(duration)
            self.load_started = self.clock.now
            if not self.lost_ack:
                self.queue("LOAD_QUEUED")
        elif command == "load-stop":
            self.load_stopped = True
            self.queue("LOAD_STOP_QUEUED")
        elif command == "load-status":
            elapsed = 0 if self.load_started is None else min(
                round((self.clock.now - self.load_started) * 1e6), self.duration * 1000)
            state = ("idle" if self.load_started is None else "stopped" if self.load_stopped
                     else "done" if elapsed >= self.duration * 1000 else "running")
            channels, rate, iterations = ((0, 0, 0), (16, 100, 32), (32, 1000, 128))[self.mode]
            produced = elapsed * rate // 1_000_000
            if state == "done":
                produced = (self.duration * rate + 999) // 1000
            self.queue(status_fixture(id=self.load_id, mode=self.mode, state=state,
                                      channels=channels, rate_hz=rate, iterations=iterations,
                                      kernel_total_us=produced * 10,
                                      kernel_max_us=10 if produced else 0,
                                      checksum=12345 if produced else 0,
                                      dispatch_max_us=20 if produced else 0,
                                      consume_max_us=40 if produced else 0,
                                      duration_ms=self.duration, elapsed_us=elapsed,
                                      phase_started_us=round((self.load_started or 0) * 1e6),
                                      phase_finished_us=(round((self.load_started or 0) * 1e6) + elapsed
                                                         if state in ("done", "stopped") else 0),
                                      produced=produced, consumed=produced,
                                      overwritten=0, sequence_gaps=0))
        elif command == "motion-bench":
            self.motion_started = self.clock.now
            self.queue("MOTION_BENCH_STARTED")
        elif command in ("wifi-on", "wifi-off", "ble-on", "ble-off"):
            wifi = command.startswith("wifi")
            enabled = command.endswith("on")
            self.radios["wifi_enabled" if wifi else "ble_enabled"] = enabled
            if not enabled:
                if self.delayed_radio_off:
                    self.radio_lag = 3
                else:
                    self.radios["wifi_scanning" if wifi else "ble_advertising"] = False
            self.queue("RADIO_QUEUED")
        elif command in ("wifi-scan", "ble-discover"):
            self.radios["wifi_scanning" if command == "wifi-scan"
                        else "ble_advertising"] = self.observe_radio
            self.queue("RADIO_QUEUED")
        elif command == "sd-refresh":
            self.revision += 1
            self.queue("SD_REFRESH_QUEUED")
        else:
            raise AssertionError("unexpected bench command")
        return len(data)


class LoadBenchTests(unittest.TestCase):
    def test_help_and_missing_execution_flag_never_import_serial(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "serial.py").write_text("raise RuntimeError('serial imported')\n")
            output = root / "result.json"
            env = {**os.environ, "PYTHONPATH": str(root)}
            for args, expected in ((["--help"], 0),
                                   (["--port", "unused", "--mode", "1",
                                     "--output", str(output)], 2)):
                result = subprocess.run([sys.executable, str(SCRIPT), *args],
                                        capture_output=True, text=True, env=env)
                self.assertEqual(result.returncode, expected, result.stderr)
                self.assertNotIn("serial imported", result.stderr)
            self.assertFalse(output.exists())

    def test_parser_is_bounded_and_removes_private_or_malformed_fields(self):
        client = load_client()
        port, clock = MemoryPort(Clock()), Clock()
        port.incoming.extend(b"x" * 5000 + b"\n{bad\n")
        port.queue({"kind": ["bad-type"], "ssid": "PRIVATE"})
        port.queue({**status_fixture(), "ssid": "PRIVATE", "sample": [1, 2],
                    "kernel_max_us": "PRIVATE", "heap_min_bytes": -1})
        bench = client.Bench(port, clock)
        events = []
        for _ in range(5):
            events.extend(bench.poll())
        self.assertEqual(len(events), 1)
        self.assertNotIn("PRIVATE", json.dumps(events))
        self.assertNotIn("sample", events[0])
        self.assertNotIn("heap_min_bytes", events[0])
        self.assertEqual(len(bench.buffer), 0)
        with self.assertRaises(client.BenchError):
            client.validate_status(events[0], 7, 1, 15000)

    def test_identity_requires_exact_version_protocol_and_physical_flags(self):
        client = load_client()
        client.validate_identity(info_fixture(), radios_fixture(), "off")
        for change in ({"firmware": "0.4.10"}, {"build": "normal"},
                       {"load_benchmark_protocol": True},
                       {"sensor_inputs_enabled": True}, {"actuators_enabled": True},
                       {"simulation_enabled": True}):
            with self.subTest(change=change), self.assertRaises(client.BenchError):
                client.validate_identity({**info_fixture(), **change}, radios_fixture(), "off")
        with self.assertRaises(client.BenchError):
            client.validate_identity(info_fixture(), {**radios_fixture(), "wifi_connected": True}, "off")
        with self.assertRaises(client.BenchError):
            client.validate_identity({**info_fixture(), "sd_mounted": False}, radios_fixture(), "sd-refresh")

    def test_status_rejects_bad_accounting_correlation_and_backwards_counters(self):
        client = load_client()
        good = status_fixture()
        client.validate_status(good, 7, 1, 15000)
        for change in ({"produced": 1}, {"pending": 2}, {"id": 8},
                       {"mode": 2}, {"protocol": True}, {"state": "PRIVATE"},
                       {"channels": 32}, {"rate_hz": 1000}, {"iterations": 128},
                       {"missed_periods": 1}, {"sequence_gaps": 99}):
            with self.subTest(change=change), self.assertRaises(client.BenchError):
                client.validate_status({**good, **change}, 7, 1, 15000)
        with self.assertRaises(client.BenchError):
            client.validate_status(good, 7, 1, 15000, {**good, "kernel_total_us": 999999})
        client.validate_status(status_fixture(duration_ms=15001, elapsed_us=15_001_000,
                                              phase_finished_us=16_001_000,
                                              produced=1501, consumed=1401), 7, 1, 15001)

    def test_wait_preserves_ui_events_and_pending_queue_is_limited(self):
        client = load_client()
        clock = Clock()
        port = MemoryPort(clock)
        port.queue(ui_fixture())
        port.queue(status_fixture())
        bench = client.Bench(port, clock)
        result = bench.request("load-status", "load_benchmark")
        self.assertEqual(result["state"], "done")
        self.assertEqual(bench.ui_report()["duration_ms"], 10000)
        bench.keep_ui(ui_fixture())
        bench.keep_ui(ui_fixture())
        with self.assertRaises(client.BenchError):
            bench.keep_ui(ui_fixture())

    def test_stream_skips_missed_slots_without_burst(self):
        client = load_client()
        clock = Clock()
        port = MemoryPort(clock)
        bench = client.Bench(port, clock)
        bench.start_stream(123, 20)
        bench.poll()
        clock.sleep(.5)
        bench.poll()
        bench.poll()
        self.assertEqual(port.frames, 2)
        self.assertGreaterEqual(bench.skipped_frames, 8)

    def test_ui_window_must_fit_the_device_load_interval(self):
        client = load_client()
        result = client.verify_window(status_fixture(), ui_fixture())
        self.assertEqual(result["ui_start_offset_ms"], 1000)
        for change in ({"observed_start_us": 999999},
                       {"observed_end_us": 16_000_001}, {"interrupted": True}):
            with self.subTest(change=change), self.assertRaises(client.BenchError):
                client.verify_window(status_fixture(), {**ui_fixture(), **change})

    def test_cleanup_waits_for_asynchronous_radio_cancellation(self):
        client = load_client()
        clock = Clock()
        port = MemoryPort(clock)
        port.radios["ble_enabled"] = port.radios["ble_advertising"] = True
        port.radios["ble_connected"] = True
        port.delayed_radio_off = True
        bench = client.Bench(port, clock)
        bench.token = 5
        result = {"completed": True}
        client.cleanup(bench, result, radios_fixture(), False)
        self.assertTrue(result["radios_restored"])
        self.assertTrue(result["completed"])
        self.assertFalse(port.radios["ble_advertising"])

    def test_cleanup_confirms_state_after_lost_stop_and_radio_acknowledgements(self):
        client = load_client()

        class LostCleanupAckPort(MemoryPort):
            def write(self, data):
                size = super().write(data)
                if data in (b"load-stop\n", b"wifi-off\n") or data.startswith(b"sim-stop "):
                    self.incoming.clear()
                return size

        clock = Clock()
        port = LostCleanupAckPort(clock)
        bench = client.Bench(port, clock)
        bench.token = 5
        result = {"completed": True}
        client.cleanup(bench, result, radios_fixture(), True)
        self.assertTrue(result["completed"])
        self.assertTrue(result["load_cleanup_confirmed"])
        self.assertTrue(result["sim_off_confirmed"])
        self.assertIn("ble-off", port.commands)

    def test_open_failure_result_and_stdout_never_include_exception_details(self):
        client = load_client()

        class FailedPort:
            def __init__(self, **kwargs):
                pass

            def open(self):
                raise OSError("PRIVATE SERIAL /private/device")

            def close(self):
                pass

        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "result.json"
            with patch.dict(sys.modules, {"serial": SimpleNamespace(Serial=FailedPort)}), patch("builtins.print") as display:
                code = client.main(["--port", "unused", "--output", str(output), "--execute-physical-bench"])
            self.assertEqual(code, 1)
            self.assertEqual(json.loads(output.read_text()),
                             {"completed": False, "failure": "control_or_transport_failure", "port_closed": True})
            self.assertNotIn("PRIVATE", str(display.call_args))

    def test_control_and_peak_use_their_declared_workload_parameters(self):
        client = load_client()
        for mode, expected in ((0, (0, 0, 0)), (2, (32, 1000, 128))):
            clock, result = Clock(), {}
            port = MemoryPort(clock)
            with self.subTest(mode=mode):
                client.run_bench(client.Bench(port, clock), result, mode=mode)
                self.assertTrue(result["completed"])
                metrics = result["load_metrics"]
                self.assertEqual((metrics["channels"], metrics["rate_hz"], metrics["iterations"]), expected)

    def test_success_collects_aggregates_and_cleans_all_resources(self):
        client = load_client()
        clock = Clock()
        port = MemoryPort(clock, original_wifi=True)
        bench, result = client.Bench(port, clock), {}
        client.run_bench(bench, result, mode=1, load="ble-advertising", duration=15000, rate=20)
        self.assertTrue(result["completed"])
        self.assertTrue(result["load_observed"])
        self.assertGreaterEqual(result["status_polls"], 14)
        self.assertGreater(result["producer_frames"], 300)
        self.assertEqual(result["producer_frames"], result["sim_latest_sequence_delta"])
        self.assertFalse(port.sim_live)
        self.assertTrue(port.load_stopped)
        self.assertTrue(port.radios["wifi_enabled"])
        self.assertFalse(port.radios["ble_enabled"])
        self.assertFalse(port.radios["ble_advertising"])
        rendered = json.dumps(result)
        for forbidden in ("PRIVATE", '"id"', '"session"', '"seq"', '"observed_start_us"'):
            self.assertNotIn(forbidden, rendered)

    def test_failure_and_lost_ack_still_stop_load_sim_and_restore_radios(self):
        client = load_client()
        for flags, load in (({"lost_ack": True}, "off"),
                            ({"ui_ok": False}, "off"),
                            ({"observe_radio": False}, "wifi-scan"),
                            ({"fail_stream": True}, "off")):
            clock = Clock()
            port = MemoryPort(clock, original_wifi=True, **flags)
            result = {}
            with self.subTest(flags=flags), self.assertRaises(Exception):
                client.run_bench(client.Bench(port, clock), result, mode=1,
                                 load=load, duration=15000, rate=20)
            self.assertFalse(result["completed"])
            self.assertFalse(port.sim_live)
            self.assertTrue(port.radios["wifi_enabled"])
            self.assertFalse(port.radios["wifi_scanning"])
            self.assertFalse(port.radios["ble_enabled"])
            if "load-start" in port.commands:
                self.assertTrue(port.load_stopped)

    def test_long_run_polls_once_per_second_without_storing_samples(self):
        client = load_client()
        clock = Clock()
        port = MemoryPort(clock)
        result = {}
        client.run_bench(client.Bench(port, clock), result, mode=1,
                         load="sd-refresh", duration=120000, rate=100)
        self.assertTrue(result["completed"])
        self.assertGreaterEqual(result["status_polls"], 119)
        self.assertLessEqual(result["status_polls"], 123)
        self.assertGreater(result["sim_latest_sequence_delta"], 11000)
        self.assertEqual(result["memory_minima"]["stack_load_min_bytes"], 2000)
        self.assertLess(len(json.dumps(result)), 6000)

    def test_terminal_metrics_wait_for_the_correlated_final_mailbox_drain(self):
        client = load_client()

        class PendingTerminalPort(MemoryPort):
            def __init__(self, clock):
                super().__init__(clock)
                self.pending_sent = False

            def queue(self, value):
                if (isinstance(value, dict) and value.get("kind") == "load_benchmark"
                        and value.get("state") == "done" and not self.pending_sent):
                    self.pending_sent = True
                    value = {**value, "pending": 1, "consumed": value["consumed"] - 1}
                super().queue(value)

        clock, result = Clock(), {}
        port = PendingTerminalPort(clock)
        client.run_bench(client.Bench(port, clock), result)
        self.assertTrue(result["completed"])
        self.assertTrue(port.pending_sent)
        self.assertEqual(result["load_metrics"]["pending"], 0)
        self.assertEqual(result["load_metrics"]["consumed"], result["load_metrics"]["produced"])
        self.assertGreaterEqual(result["status_polls"], 17)


if __name__ == "__main__":
    unittest.main()
