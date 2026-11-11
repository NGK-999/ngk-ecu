"""Explicit physical SD/UI diagnostic. Stores bounded aggregates only."""

import argparse
import json
import math
from pathlib import Path
import secrets
import time


INFO = set("firmware build mode sensor_inputs_enabled actuators_enabled simulation_protocol sd_io_benchmark_protocol simulation_enabled refresh_mode reduced_motion sd_present sd_mounted sd_rw_ok sd_sectors sd_mib sd_spi_hz profile_persisted".split())
RADIOS = set("running wifi_enabled wifi_connected wifi_connecting wifi_scanning ble_enabled ble_advertising ble_connected ble_pairing_pending worker_wakeups memory_failure".split())
UI = set("synthetic duration_ms presentations interval_p95_us interval_max_us max_pixels transferred_pixels touch_poll_max_gap_us ui_tick_total_us ui_tick_max_us refresh_mode reduced_motion ramp_updates ramp_gap_p95_upper_us ramp_gap_max_us interrupted ui_cpu_total_us ui_wait_total_us instrument_passes number_passes rpm_number_passes speed_number_passes rpm_number_gap_max_us speed_number_gap_max_us instrument_gap_max_us ramp_stable_passes ramp_gap_without_stable_max_us ramp_gap_with_stable_max_us detail_passes idle_skips sample_presentations sample_to_present_p95_ms sample_to_present_max_ms observed_start_us observed_end_us".split())
SD = set("id mode state phase_started_us phase_finished_us okay created removed card_consistent error setup_ms duration_ms stack_min_bytes".split())
for _prefix in ("read", "write", "sync"):
    SD.update(f"{_prefix}_{field}" for field in ("ops", "bytes", "total_us", "p95_upper_us", "max_us"))
ACK = {"MOTION_BENCH_STARTED", "MOTION_BENCH_REFUSED", "SD_IO_QUEUED", "SD_IO_REFUSED"}
RADIO_OFF = ("wifi_enabled", "wifi_connected", "wifi_connecting", "wifi_scanning",
             "ble_enabled", "ble_advertising", "ble_connected", "ble_pairing_pending")


class BenchError(RuntimeError):
    """A fixed, non-serial failure reason safe for an aggregate report."""


def bounded(value, keys):
    return {key: val for key, val in value.items() if key in keys and
            (type(val) in (int, bool) or key in {"firmware", "build", "mode", "state"}
             and type(val) is str and len(val) <= 32)}


def validate_identity(info, radios, expected_mib):
    expected = {"firmware": "0.4.11-sd-io-bench", "build": "normal", "mode": "bench",
                "sensor_inputs_enabled": False, "actuators_enabled": False,
                "simulation_protocol": 1, "sd_io_benchmark_protocol": 1,
                "simulation_enabled": False, "sd_present": True,
                "sd_mounted": True,
                "profile_persisted": True, "refresh_mode": 0,
                "reduced_motion": False, "sd_mib": expected_mib}
    if any(info.get(key) != value or type(info.get(key)) is not type(value)
           for key, value in expected.items()):
        raise BenchError("identity_or_card_mismatch")
    sectors = info.get("sd_sectors")
    if type(sectors) is not int or not 0 < sectors <= 0xffffffff:
        raise BenchError("invalid_card_geometry")
    if type(info.get("sd_spi_hz")) is not int or info["sd_spi_hz"] not in (1_000_000, 8_000_000):
        raise BenchError("unexpected_sd_spi_clock")
    if (radios.get("running") is not True or radios.get("memory_failure") is not False
            or any(radios.get(key) is not False for key in RADIO_OFF)):
        raise BenchError("radios_must_already_be_off")
    return sectors


def verify_case(status, ui, job_id, mode):
    if not (SD | {"kind"}) <= set(status):
        raise BenchError("incomplete_sd_status")
    booleans = {"okay", "created", "removed", "card_consistent"}
    numeric = SD - booleans - {"state"}
    if (status.get("state") not in {"idle", "queued", "preparing", "measuring", "verifying", "finished"}
            or any(type(status.get(key)) is not bool for key in booleans)
            or any(type(status.get(key)) is not int or not 0 <= status[key] <= 0xffffffffffffffff for key in numeric)):
        raise BenchError("invalid_sd_status")
    if status.get("kind") != "sd_io_benchmark" or status.get("id") != job_id or status.get("mode") != mode:
        raise BenchError("sd_job_correlation_failed")
    if status.get("state") != "finished" or any(status.get(key) is not True for key in
                                                ("okay", "created", "removed", "card_consistent")):
        raise BenchError("sd_job_failed_or_cleanup_incomplete")
    if status.get("error") != 0:
        raise BenchError("sd_job_error")
    if status["duration_ms"] < 15000:
        raise BenchError("sd_phase_too_short")
    if (mode == 4 and (status["read_ops"] == 0 or status["read_bytes"] == 0)) or (
            mode in (1, 2, 3) and (status["write_ops"] == 0 or status["write_bytes"] == 0
                                 or status["sync_ops"] == 0)):
        raise BenchError("sd_phase_without_expected_io")
    if ui.get("kind") != "ui_motion_benchmark" or ui.get("synthetic") is not True or ui.get("interrupted") is not False or ui.get("duration_ms", 0) < 9900:
        raise BenchError("ui_measurement_incomplete")
    times = (status.get("phase_started_us"), ui.get("observed_start_us"),
             ui.get("observed_end_us"), status.get("phase_finished_us"))
    if any(type(value) is not int or value < 0 for value in times) or not times[0] <= times[1] < times[2] <= times[3]:
        raise BenchError("ui_outside_sd_measurement")
    if times[1] - times[0] >= 3_000_000:
        raise BenchError("ui_started_too_late_in_sd_phase")
    elapsed = (times[3] - times[0]) / 1_000_000
    if elapsed < 15:
        raise BenchError("sd_phase_wall_too_short")
    aggregate = {key: value for key, value in status.items() if key in SD and key not in
                 {"id", "phase_started_us", "phase_finished_us", "state"}}
    aggregate["mode"] = mode
    aggregate["phase_wall_ms"] = round(elapsed * 1000)
    aggregate["effective_bytes_per_s"] = (status.get("read_bytes", 0) + status.get("write_bytes", 0)) / elapsed
    aggregate["ui"] = {key: value for key, value in ui.items() if key in UI and key not in
                       {"observed_start_us", "observed_end_us"}}
    return aggregate


class Bench:
    def __init__(self, port, clock=time):
        self.port = port
        self.clock = clock
        self.buffer = bytearray()
        self.discard_line = False
        self.pending_ui = []
        self.token = None
        self.sequence = 0
        self.streaming = False
        self.started = 0.0
        self.stream_started_at = None
        self.next_frame = 0.0
        self.frames = 0
        self.max_gap = 0.0
        self.previous_frame = None

    def send(self, command):
        self.port.write((command + "\n").encode("ascii"))

    def start_stream(self, token):
        self.token = token
        self.sequence = 0
        self.streaming = True
        self.started = self.next_frame = self.clock.monotonic()
        self.stream_started_at = self.started

    def poll(self):
        now = self.clock.monotonic()
        if self.streaming and now >= self.next_frame:
            wave = 1 - math.cos((now - self.started) * 2 * math.pi / 8)
            rpm = round(1500 + 1000 * wave)
            speed_tenths = round(400 + 200 * wave)
            self.sequence += 1
            self.port.write(f"SIM1 {self.token} {self.sequence} {rpm} {speed_tenths} 850 350 3 0 0\n".encode("ascii"))
            self.frames += 1
            if self.previous_frame is not None:
                self.max_gap = max(self.max_gap, now - self.previous_frame)
            self.previous_frame = now
            self.next_frame += .05
            if self.next_frame <= now:  # Do not burst delayed frames.
                self.next_frame = now + .05
        events = []
        for byte in self.port.read(2048):
            if byte == 10:
                if not self.discard_line:
                    line = bytes(self.buffer).strip()
                    if line in {item.encode("ascii") for item in ACK}:
                        events.append(line.decode("ascii"))
                    else:
                        try:
                            event = json.loads(line)
                        except (ValueError, UnicodeError):
                            event = None
                        if isinstance(event, dict):
                            if event.get("project") == "NGK ECU":
                                events.append({"kind": "info", **bounded(event, INFO)})
                            elif event.get("kind") == "radios":
                                events.append({"kind": "radios", **bounded(event, RADIOS)})
                            elif event.get("kind") == "ui_motion_benchmark":
                                events.append({"kind": "ui_motion_benchmark", **bounded(event, UI)})
                            elif event.get("kind") == "sd_io_benchmark":
                                events.append({"kind": "sd_io_benchmark", **bounded(event, SD)})
                            elif event.get("kind") == "sim":
                                events.append({"kind": "sim", **bounded(event, {"state", "live", "session", "protocol"})})
                self.buffer.clear()
                self.discard_line = False
            elif not self.discard_line and len(self.buffer) < 4096:
                self.buffer.append(byte)
            else:
                self.buffer.clear()
                self.discard_line = True
        return events

    def wait(self, predicate, timeout):
        end = self.clock.monotonic() + timeout
        while self.clock.monotonic() < end:
            match = None
            for event in self.poll():
                if predicate(event):
                    match = event
                elif isinstance(event, dict) and event.get("kind") == "ui_motion_benchmark":
                    self.keep_ui(event)
                if isinstance(event, dict) and event.get("kind") == "sim" and event.get("state") in ("rejected", "busy"):
                    raise BenchError("sim_refused")
            if match is not None:
                return match
            self.clock.sleep(.001)
        raise BenchError("control_timeout")

    def request(self, command, kind, timeout=2):
        self.send(command)
        return self.wait(lambda event: isinstance(event, dict) and event.get("kind") == kind, timeout)

    def delay(self, seconds):
        end = self.clock.monotonic() + seconds
        while self.clock.monotonic() < end:
            self.poll_into_pending()
            self.clock.sleep(.001)

    def poll_into_pending(self):
        for event in self.poll():
            if isinstance(event, dict) and event.get("kind") == "ui_motion_benchmark":
                self.keep_ui(event)

    def keep_ui(self, event):
        if len(self.pending_ui) >= 2:
            raise BenchError("unexpected_excess_ui_reports")
        self.pending_ui.append(event)

    def ui_report(self, timeout=15):
        if self.pending_ui:
            return self.pending_ui.pop(0)
        return self.wait(lambda event: isinstance(event, dict) and event.get("kind") == "ui_motion_benchmark", timeout)

    def baseline_report(self, timeout=15):
        # Apply the same status-query load as the SD cases during UI measurement.
        end = self.clock.monotonic() + timeout
        while self.clock.monotonic() < end:
            if self.pending_ui:
                return self.ui_report(0)
            state = self.request("sd-io-status", "sd_io_benchmark",
                                 min(2, max(0, end - self.clock.monotonic())))
            if state.get("state") != "idle" and not (
                    state.get("state") == "finished" and state.get("okay") is True
                    and state.get("removed") is True and state.get("card_consistent") is True):
                raise BenchError("baseline_sd_not_idle")
            if self.pending_ui:
                return self.ui_report(0)
            self.delay(min(.05, max(0, end - self.clock.monotonic())))
        raise BenchError("baseline_ui_timeout")

    def startup(self):
        end = self.clock.monotonic() + 8
        while self.clock.monotonic() < end:
            try:
                info = self.request("info", "info", min(1, end - self.clock.monotonic()))
                radios = self.request("radios", "radios", min(1, end - self.clock.monotonic()))
                if info.get("profile_persisted") is True and info.get("sd_mounted") is True and radios.get("running") is True:
                    return info, radios
            except BenchError:
                pass
            self.delay(min(.1, max(0, end - self.clock.monotonic())))
        raise BenchError("boot_not_ready_within_8s")

    def motion(self, timeout=2):
        end = self.clock.monotonic() + timeout
        while self.clock.monotonic() < end:
            self.send("motion-bench")
            ack = self.wait(lambda event: event in ("MOTION_BENCH_STARTED", "MOTION_BENCH_REFUSED") if isinstance(event, str) else False,
                            end - self.clock.monotonic())
            if ack == "MOTION_BENCH_STARTED":
                return
            sim = self.request("sim-status", "sim", end - self.clock.monotonic())
            if sim.get("live") is not True:
                raise BenchError("sim_not_live")
            self.delay(min(.05, max(0, end - self.clock.monotonic())))
        raise BenchError("motion_refused")

    def sd_status(self, job_id, mode):
        status = self.request("sd-io-status", "sd_io_benchmark")
        if status.get("id") != job_id or status.get("mode") != mode:
            raise BenchError("sd_job_correlation_failed")
        if status.get("state") not in {"queued", "preparing", "measuring", "verifying", "finished"}:
            raise BenchError("invalid_sd_state")
        return status


def settle_job(bench, job_id, mode, deadline):
    """Keep SIM streaming until the worker reports its terminal cleanup state."""
    consecutive_errors = 0
    while bench.clock.monotonic() < deadline:
        try:
            status = bench.sd_status(job_id, mode)
            if status.get("state") == "finished":
                return status
            consecutive_errors = 0
        except Exception:
            consecutive_errors += 1
            if consecutive_errors >= 3:
                return None
        try:
            bench.delay(.05)
        except Exception:
            return None
    return None


def cleanup_confirmed(status):
    return (status is not None and status.get("state") == "finished"
            and status.get("created") is True and status.get("removed") is True
            and status.get("card_consistent") is True)


def validate_cadence(frames, elapsed, max_gap):
    if elapsed <= 0 or frames <= 0 or max_gap > .1 or not 19 <= frames / elapsed <= 21:
        raise BenchError("producer_cadence_outside_budget")
    return frames / elapsed


def failed_status_aggregate(status):
    result = {key: value for key, value in status.items() if key in SD and key not in
              {"id", "phase_started_us", "phase_finished_us", "state"} and type(value) in (int, bool)}
    if status.get("state") in {"queued", "preparing", "measuring", "verifying", "finished"}:
        result["state"] = status["state"]
    return result


def run_bench(bench, expected_mib, result):
    result.update({"completed": False, "producer_hz_target": 20, "baseline": None, "cases": []})
    info, radios = bench.startup()
    sectors = validate_identity(info, radios, expected_mib)
    result["card_mib"] = expected_mib
    result["sd_spi_hz"] = info["sd_spi_hz"]
    result["radio_before"] = radios
    prior = bench.request("sd-io-status", "sd_io_benchmark")
    if prior.get("state") != "idle" and not (
            prior.get("state") == "finished" and prior.get("okay") is True
            and prior.get("removed") is True and prior.get("card_consistent") is True):
        raise BenchError("prior_sd_job_not_clean")
    token = secrets.randbelow(0xffffffff) + 1
    bench.token = token
    try:
        ready = bench.request(f"sim-start {token}", "sim")
        if ready.get("state") != "ready" or ready.get("session") != token or ready.get("protocol") != 1:
            raise BenchError("sim_handshake_failed")
        bench.start_stream(token)
        bench.delay(2)
        if bench.request("sim-status", "sim").get("live") is not True:
            raise BenchError("sim_not_live")
        bench.motion()
        baseline = bench.baseline_report()
        if baseline.get("synthetic") is not True or baseline.get("interrupted") is not False or baseline.get("duration_ms", 0) < 9900:
            raise BenchError("baseline_ui_incomplete")
        result["baseline"] = {key: val for key, val in baseline.items() if key in UI and key not in
                              {"observed_start_us", "observed_end_us"}}
        used_ids = set()
        for mode in range(1, 5):
            bench.delay(2)
            for _ in range(8):
                job_id = secrets.randbelow(0xffffffff) + 1
                if job_id != token and job_id not in used_ids:
                    break
            else:
                raise BenchError("job_id_generation_failed")
            used_ids.add(job_id)
            result["job_cleanup_confirmed"] = False
            deadline = bench.clock.monotonic() + 90
            motion_started = False
            finished = None
            should_drain = True
            try:
                bench.send(f"sd-io-bench {sectors} {job_id} {mode}")
                ack = bench.wait(lambda event: event in ("SD_IO_QUEUED", "SD_IO_REFUSED") if isinstance(event, str) else False, 2)
                if ack != "SD_IO_QUEUED":
                    should_drain = False
                    raise BenchError("sd_request_refused")
                while bench.clock.monotonic() < deadline:
                    status = bench.sd_status(job_id, mode)
                    if status.get("state") == "measuring" and not motion_started:
                        bench.started = bench.clock.monotonic()
                        bench.delay(2)
                        status = bench.sd_status(job_id, mode)
                        if status.get("state") != "measuring":
                            raise BenchError("sd_measurement_ended_during_settle")
                        bench.motion()
                        motion_started = True
                    if status.get("state") == "finished":
                        finished = status
                        break
                    bench.delay(.05)
                if finished is None:
                    raise BenchError("sd_job_timeout")
                if not motion_started:
                    raise BenchError("sd_measurement_not_observed")
                ui = bench.ui_report(2)
                result["cases"].append(verify_case(finished, ui, job_id, mode))
                result["job_cleanup_confirmed"] = cleanup_confirmed(finished)
            except Exception:
                if should_drain and finished is None:
                    try:
                        finished = settle_job(bench, job_id, mode, deadline)
                    except Exception:
                        finished = None
                if finished is not None:
                    result["cases"].append(failed_status_aggregate(finished))
                result["job_cleanup_confirmed"] = cleanup_confirmed(finished)
                raise
        result["completed"] = True
        return result
    finally:
        stream_start = getattr(bench, "stream_started_at", None)
        if stream_start is not None:
            elapsed = max(0, bench.clock.monotonic() - stream_start)
            result["producer_elapsed_ms"] = round(elapsed * 1000)
            result["producer_hz_observed"] = bench.frames / elapsed if elapsed > 0 else 0
            if result.get("completed"):
                try:
                    validate_cadence(bench.frames, elapsed, bench.max_gap)
                except BenchError as error:
                    result["completed"] = False
                    result["failure"] = str(error)
        bench.streaming = False
        if bench.token is not None:
            try:
                off = bench.request(f"sim-stop {bench.token}", "sim")
                confirmed = off.get("state") == "off"
                if not confirmed:
                    confirmed = bench.request("sim-status", "sim").get("state") == "off"
                result["sim_off_confirmed"] = confirmed
            except Exception:
                result["sim_off_confirmed"] = False
        try:
            current_radios = bench.request("radios", "radios")
            result["radios_off_confirmed"] = (current_radios.get("running") is True
                and current_radios.get("memory_failure") is False
                and all(current_radios.get(key) is False for key in RADIO_OFF))
        except Exception:
            result["radios_off_confirmed"] = False
        if result.get("sim_off_confirmed") is not True or result["radios_off_confirmed"] is not True:
            result["completed"] = False
        result["producer_frames"] = bench.frames
        result["producer_max_gap_us"] = round(bench.max_gap * 1_000_000)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--expected-sd-mib", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--execute-physical-bench", action="store_true")
    args = parser.parse_args(argv)
    if not args.execute_physical_bench:
        parser.error("Explicit --execute-physical-bench required")
    if args.expected_sd_mib <= 0 or args.output.exists():
        parser.error("Invalid card capacity or output already exists")
    result = {"completed": False}
    port = None
    try:
        import serial  # Delayed until after the explicit physical execution guard.

        port = serial.Serial(port=None, baudrate=115200, timeout=.001,
                             write_timeout=1, exclusive=True)
        port.port = args.port
        port.dtr = False
        port.rts = False
        port.open()
        port.reset_input_buffer()
        bench = Bench(port)
        run_bench(bench, args.expected_sd_mib, result)
    except BenchError as error:
        result["failure"] = str(error)
    except Exception:
        result["failure"] = "unexpected_client_or_transport_error"
    finally:
        if port is not None:
            try:
                port.close()
                result["port_closed"] = True
            except Exception:
                result["port_closed"] = False
                result["completed"] = False
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("x", encoding="utf-8") as output:
            json.dump(result, output, indent=2, sort_keys=True)
            output.write("\n")
    if not result.get("completed") or result.get("sim_off_confirmed") is not True or result.get("radios_off_confirmed") is not True:
        raise SystemExit("Bench incomplete; inspect aggregate result")


if __name__ == "__main__":
    main()
