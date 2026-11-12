"""Synthetic SD client tests. The canonical unittest runner never opens hardware."""

import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).resolve().parents[2] / "tools" / "benchmark_sd.py"
SPEC = importlib.util.spec_from_file_location("benchmark_sd", SCRIPT)
client = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(client)


class FakeClock:
    def __init__(self):
        self.value = 0.0

    def monotonic(self):
        return self.value

    def sleep(self, seconds):
        self.value += seconds


class FakePort:
    def __init__(self, data=b""):
        self.data = bytearray(data)
        self.writes = []

    def read(self, count):
        data = self.data[:count]
        del self.data[:count]
        return data

    def write(self, data):
        self.writes.append(data)


def info_fixture():
    return {"firmware": "0.4.12-sd-io-bench", "build": "normal", "mode": "bench",
            "sensor_inputs_enabled": False, "actuators_enabled": False,
            "simulation_protocol": 1, "sd_io_benchmark_protocol": 1,
            "simulation_enabled": False, "sd_present": True, "sd_mounted": True,
            "sd_rw_ok": False, "sd_sectors": 61962240, "sd_mib": 30255,
            "sd_spi_hz": 8000000, "profile_persisted": True,
            "refresh_mode": 0, "reduced_motion": False}


def radios_fixture():
    return {"running": True, "memory_failure": False,
            **{key: False for key in client.RADIO_OFF}}


def status_fixture():
    status = {"kind": "sd_io_benchmark", "id": 7, "mode": 1,
              "state": "finished", "phase_started_us": 100,
              "phase_finished_us": 15_100_100, "okay": True, "created": True,
              "removed": True, "card_consistent": True, "error": 0,
              "setup_ms": 100, "duration_ms": 15000, "stack_min_bytes": 1024}
    for prefix in ("read", "write", "sync"):
        status.update({f"{prefix}_{field}": 0 for field in
                       ("ops", "bytes", "total_us", "p95_upper_us", "max_us")})
    status.update({"write_ops": 4, "write_bytes": 4096,
                   "write_total_us": 2_000_000, "sync_ops": 4,
                   "sync_total_us": 1_000_000})
    return status


def ui_fixture():
    return {"kind": "ui_motion_benchmark", "synthetic": True,
            "duration_ms": 10000, "interrupted": False,
            "observed_start_us": 2_100_100,
            "observed_end_us": 12_100_100}


class ScriptedBench:
    def __init__(self, *, radio_enabled=False, producer_hz=20, max_gap=.05,
                 lost_ack=False, status_error=False, removed=True):
        self.clock = FakeClock()
        self.token = None
        self.streaming = False
        self.started = 0.0
        self.stream_started_at = None
        self.frames = 0
        self.max_gap = max_gap
        self.radio_enabled = radio_enabled
        self.producer_hz = producer_hz
        self.lost_ack = lost_ack
        self.status_error = status_error
        self.removed = removed
        self.mode = 0
        self.status_step = 0
        self.events = []
        self.motion_offsets = []

    def startup(self):
        return info_fixture(), radios_fixture()

    def request(self, command, kind):
        if command == "sd-io-status":
            return {"state": "idle"}
        if command.startswith("sim-start "):
            return {"state": "ready", "session": self.token, "protocol": 1}
        if command == "sim-status":
            return {"live": True}
        if command.startswith("sim-stop "):
            self.events.append("sim-stop")
            return {"state": "off"}
        if command == "radios":
            return {**radios_fixture(), "wifi_enabled": self.radio_enabled}
        raise AssertionError(command)

    def start_stream(self, token):
        self.token = token
        self.streaming = True
        self.started = self.stream_started_at = self.clock.monotonic()

    def delay(self, seconds):
        self.clock.sleep(seconds)
        if self.streaming:
            self.frames = round((self.clock.monotonic() - self.stream_started_at) * self.producer_hz)

    def motion(self):
        self.motion_offsets.append(self.clock.monotonic() - self.started)

    def ui_report(self, timeout=15):
        return ui_fixture()

    def baseline_report(self, timeout=15):
        return ui_fixture()

    def send(self, command):
        if command.startswith("sd-io-bench "):
            self.mode = int(command.split()[-1])
            self.status_step = 0
            self.events.append("job-requested")

    def wait(self, predicate, timeout):
        if self.lost_ack:
            raise TimeoutError("ack lost")
        return "SD_IO_QUEUED"

    def sd_status(self, job_id, mode):
        self.status_step += 1
        if self.status_error and self.status_step == 1:
            raise OSError("raw transport detail")
        if self.status_error:
            self.events.append("job-finished")
            return {"state": "finished", "created": True,
                    "removed": self.removed, "card_consistent": True,
                    "mode": mode, "id": job_id}
        metrics = ({"read_ops": 4, "read_bytes": 4096} if mode == 4 else
                   {"write_ops": 4, "write_bytes": 4096, "sync_ops": 4})
        return {**status_fixture(), **metrics, "id": job_id, "mode": mode,
                "state": "measuring" if self.status_step <= 2 else "finished"}


class SdBenchClientTests(unittest.TestCase):
    def test_baseline_polls_status_without_losing_ui_and_rejects_active_sd(self):
        class StatusPort(FakePort):
            def __init__(self, state="idle", with_ui=True):
                super().__init__()
                self.queries = 0
                self.state, self.with_ui = state, with_ui

            def write(self, data):
                super().write(data)
                if data == b"sd-io-status\n":
                    self.queries += 1
                    # UI can precede the queried response in the same read.
                    if self.queries == 3 and self.with_ui:
                        self.data.extend(json.dumps(ui_fixture()).encode() + b"\n")
                    self.data.extend(json.dumps({"kind": "sd_io_benchmark", "state": self.state}).encode() + b"\n")

        port, clock = StatusPort(), FakeClock()
        bench = client.Bench(port, clock)
        self.assertEqual(bench.baseline_report(), ui_fixture())
        self.assertEqual(port.queries, 3)
        self.assertLess(clock.monotonic(), .3)
        with self.assertRaisesRegex(client.BenchError, "baseline_sd_not_idle"):
            client.Bench(StatusPort(state="measuring"), FakeClock()).baseline_report()
        timed = FakeClock()
        with self.assertRaisesRegex(client.BenchError, "baseline_ui_timeout"):
            client.Bench(StatusPort(with_ui=False), timed).baseline_report(timeout=.2)
        self.assertLess(timed.monotonic(), .3)

    def test_help_and_missing_flag_do_not_import_serial_or_create_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "result.json"
            (root / "serial.py").write_text("raise RuntimeError('serial imported')\n")
            env = {**os.environ, "PYTHONPATH": str(root)}
            for args, code in ((["--help"], 0),
                               (["--port", "/dev/ttyUSB0", "--expected-sd-mib", "30255",
                                 "--output", str(output)], 2)):
                with self.subTest(args=args):
                    process = subprocess.run([sys.executable, str(SCRIPT), *args],
                                             capture_output=True, text=True,
                                             check=False, env=env)
                    self.assertEqual(process.returncode, code)
            self.assertFalse(output.exists())

    def test_port_open_failure_writes_sanitized_final_result(self):
        class FailedPort:
            def __init__(self, **kwargs):
                pass

            def open(self):
                raise RuntimeError("raw device detail")

            def close(self):
                pass

        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "failure.json"
            with patch.dict(sys.modules, {"serial": SimpleNamespace(Serial=FailedPort)}):
                with self.assertRaisesRegex(SystemExit, "Bench incomplete"):
                    client.main(["--port", "/dev/fake", "--expected-sd-mib", "30255",
                                 "--output", str(output), "--execute-physical-bench"])
            data = output.read_text()
            self.assertNotIn("raw device detail", data)
            self.assertFalse(json.loads(data)["completed"])

    def test_receiver_discards_long_line_and_unknown_fields(self):
        data = (b"x" * 5000 + b"\n" +
                json.dumps({"kind": "ui_motion_benchmark", "synthetic": True,
                            "duration_ms": 10000, "secret": "do-not-save",
                            "observed_start_us": 200,
                            "observed_end_us": 300}).encode() + b"\n")
        bench = client.Bench(FakePort(data), FakeClock())
        events = bench.poll() + bench.poll() + bench.poll()
        self.assertEqual(events, [{"kind": "ui_motion_benchmark", "synthetic": True,
                                   "duration_ms": 10000, "observed_start_us": 200,
                                   "observed_end_us": 300}])
        self.assertEqual(len(bench.buffer), 0)

    def test_pending_ui_is_bounded(self):
        bench = client.Bench(FakePort(), FakeClock())
        bench.keep_ui(ui_fixture())
        bench.keep_ui(ui_fixture())
        with self.assertRaisesRegex(client.BenchError, "unexpected_excess_ui_reports"):
            bench.keep_ui(ui_fixture())

    def test_identity_accepts_unverified_rw_but_rejects_wrong_card_or_radio(self):
        info, radios = info_fixture(), radios_fixture()
        self.assertEqual(client.validate_identity(info, radios, 30255), 61962240)
        for changed_info, changed_radios in (
                ({**info, "sd_mib": 30254}, radios),
                (info, {**radios, "wifi_enabled": True}),
                ({**info, "sd_spi_hz": 4_000_000}, radios)):
            with self.subTest(info=changed_info, radios=changed_radios):
                with self.assertRaises(client.BenchError):
                    client.validate_identity(changed_info, changed_radios, 30255)

    def test_sd_case_requires_cleanup_time_io_and_ui_overlap(self):
        status, ui = status_fixture(), ui_fixture()
        aggregate = client.verify_case(status, ui, 7, 1)
        self.assertAlmostEqual(aggregate["effective_bytes_per_s"], 4096 / 15.1)
        self.assertNotIn("id", aggregate)
        for change in ({"removed": False}, {"card_consistent": False},
                       {"phase_finished_us": 12_100_099},
                       {"phase_finished_us": 14_999_999},
                       {"duration_ms": 14999}, {"write_bytes": 0},
                       {"write_ops": 0}, {"sync_ops": 0},
                       {"write_bytes": "4096"}):
            with self.subTest(change=change):
                with self.assertRaises(client.BenchError):
                    client.verify_case({**status, **change}, ui, 7, 1)
        for change in ({"observed_start_us": 99},
                       {"observed_start_us": 3_000_101},
                       {"interrupted": True}):
            with self.subTest(ui_change=change):
                with self.assertRaises(client.BenchError):
                    client.verify_case(status, {**ui, **change}, 7, 1)
        with self.assertRaises(client.BenchError):
            client.verify_case({**status, "mode": 4, "write_ops": 0,
                                "write_bytes": 0, "read_ops": 0,
                                "read_bytes": 0}, ui, 7, 4)

    def test_sim_frames_are_20hz_with_bounded_moving_wave(self):
        clock, port = FakeClock(), FakePort()
        bench = client.Bench(port, clock)
        bench.start_stream(123)
        for _ in range(160):
            bench.poll()
            clock.sleep(.05)
        frames = [item for item in port.writes if item.startswith(b"SIM1 ")]
        self.assertTrue(159 <= len(frames) <= 161)
        rpm = [int(item.split()[3]) for item in frames]
        speed = [int(item.split()[4]) for item in frames]
        self.assertTrue(1500 <= min(rpm) <= max(rpm) <= 3500)
        self.assertTrue(400 <= min(speed) <= max(speed) <= 800)

    def test_settle_job_waits_until_finished(self):
        clock = FakeClock()

        class FinishingJob:
            def __init__(self):
                self.clock = clock
                self.states = iter(("measuring", "verifying", "finished"))

            def sd_status(self, job_id, mode):
                self_test.assertEqual((job_id, mode), (42, 1))
                return {"state": next(self.states)}

            def delay(self, seconds):
                clock.sleep(seconds)

        self_test = self
        self.assertEqual(client.settle_job(FinishingJob(), 42, 1, 1),
                         {"state": "finished"})

    def test_cadence_rejects_large_gap_and_low_average(self):
        for frames, gap in ((160, .6), (128, .05)):
            with self.subTest(frames=frames, gap=gap):
                with self.assertRaisesRegex(client.BenchError,
                                            "producer_cadence_outside_budget"):
                    client.validate_cadence(frames, 8, gap)
        self.assertAlmostEqual(client.validate_cadence(160, 8, .05), 20)

    def test_prior_active_job_blocks_new_sim_session(self):
        class PriorJobBench:
            def __init__(self):
                self.commands = []

            def startup(self):
                return info_fixture(), radios_fixture()

            def request(self, command, kind):
                self.commands.append(command)
                return {"kind": "sd_io_benchmark", "state": "measuring"}

        bench = PriorJobBench()
        with self.assertRaisesRegex(client.BenchError, "prior_sd_job_not_clean"):
            client.run_bench(bench, 30255, {})
        self.assertEqual(bench.commands, ["sd-io-status"])

    def test_full_script_checks_cadence_radios_and_two_second_wave_reset(self):
        cases = ((True, 20, .05, False), (False, 20, .05, True),
                 (False, 16, .05, False), (False, 20, .6, False))
        for radio_enabled, producer_hz, gap, completed in cases:
            with self.subTest(radio_enabled=radio_enabled, producer_hz=producer_hz,
                              gap=gap):
                bench = ScriptedBench(radio_enabled=radio_enabled,
                                      producer_hz=producer_hz, max_gap=gap)
                with patch.object(client.secrets, "randbelow",
                                  side_effect=iter(range(41, 46))):
                    result = {}
                    client.run_bench(bench, 30255, result)
                self.assertTrue(result["sim_off_confirmed"])
                self.assertEqual(result["radios_off_confirmed"], not radio_enabled)
                self.assertEqual(result["completed"], completed)
                self.assertEqual(len(result["cases"]), 4)
                self.assertEqual(len(bench.motion_offsets), 5)
                self.assertTrue(all(2 <= offset < 2.1 for offset in
                                    bench.motion_offsets))
                if producer_hz != 20 or gap > .1:
                    self.assertEqual(result["failure"],
                                     "producer_cadence_outside_budget")

    def test_lost_ack_or_transport_error_drains_job_before_sim_stop(self):
        for removed, lost_ack in ((True, False), (False, False), (True, True)):
            with self.subTest(removed=removed, lost_ack=lost_ack):
                bench = ScriptedBench(lost_ack=lost_ack, status_error=True,
                                      removed=removed)
                with patch.object(client.secrets, "randbelow",
                                  side_effect=iter((41, 42))):
                    result = {}
                    with self.assertRaises((OSError, TimeoutError)):
                        client.run_bench(bench, 30255, result)
                self.assertEqual(bench.events,
                                 ["job-requested", "job-finished", "sim-stop"])
                self.assertEqual(result["job_cleanup_confirmed"], removed)
                self.assertFalse(result["completed"])


if __name__ == "__main__":
    unittest.main()
