"""Explicit physical synthetic-load/UI bench; persist bounded aggregates only.

Import and --help never import pyserial or open hardware. No configuration,
profile, NVS, calibration or GPIO writes are issued by this driver.
"""

import argparse
import json
import math
from pathlib import Path
import secrets
import signal
import time


INFO = set("firmware build mode sensor_inputs_enabled actuators_enabled simulation_protocol load_benchmark_protocol simulation_enabled refresh_mode reduced_motion sd_mounted profile_persisted profile_status profile_revision".split())
RADIOS = set("running wifi_enabled wifi_connected wifi_connecting wifi_scanning ble_enabled ble_advertising ble_connected ble_pairing_pending memory_failure worker_wakeups".split())
UI = set("synthetic duration_ms presentations interval_p95_us interval_max_us max_pixels transferred_pixels touch_poll_max_gap_us ui_tick_total_us ui_tick_max_us refresh_mode reduced_motion ramp_updates ramp_gap_p95_upper_us ramp_gap_max_us interrupted ui_cpu_total_us ui_wait_total_us instrument_passes number_passes rpm_number_passes speed_number_passes rpm_number_gap_max_us speed_number_gap_max_us instrument_gap_max_us ramp_stable_passes ramp_gap_without_stable_max_us ramp_gap_with_stable_max_us detail_passes idle_skips sample_presentations sample_to_present_p95_ms sample_to_present_max_ms observed_start_us observed_end_us".split())
MEMORY = set("heap_free_bytes heap_min_bytes heap_largest_bytes stack_main_min_bytes stack_sd_min_bytes stack_radio_min_bytes stack_load_min_bytes".split())
LOAD = set("protocol id mode state duration_ms elapsed_us phase_started_us phase_finished_us channels rate_hz iterations produced consumed overwritten pending sequence_gaps missed_periods dispatch_max_us consume_max_us kernel_total_us kernel_max_us checksum".split()) | MEMORY
SIM = {"state", "protocol", "session", "live", "seq"}  # Ephemeral; never copied to reports.
ACK = {"LOAD_QUEUED", "LOAD_REFUSED", "LOAD_STOP_QUEUED",
       "MOTION_BENCH_STARTED", "MOTION_BENCH_REFUSED", "RADIO_QUEUED",
       "RADIO_REFUSED", "SD_REFRESH_QUEUED", "SD_REFRESH_REFUSED queue"}
ACTIVE_RADIO = ("wifi_connected", "wifi_connecting", "wifi_scanning",
                "ble_connected", "ble_advertising", "ble_pairing_pending")
STATES = {"idle", "queued", "running", "stopping", "done", "stopped", "error"}
MONOTONIC = {"elapsed_us", "produced", "consumed", "overwritten", "sequence_gaps",
             "missed_periods", "dispatch_max_us", "consume_max_us", "kernel_total_us",
             "kernel_max_us"}
STRINGS = {"firmware": {"0.4.11-load-bench"}, "build": {"load-benchmark"},
           "mode": {"bench"}, "state": STATES | {"off", "ready", "live", "stale", "busy", "rejected"}}
BOOLS = {"sensor_inputs_enabled", "actuators_enabled", "simulation_enabled",
         "reduced_motion", "sd_mounted", "profile_persisted", "synthetic",
         "interrupted", "live", *RADIOS - {"worker_wakeups"}}


class BenchError(RuntimeError):
    """Only fixed, non-serial reason codes may be supplied here."""


def bounded(value, keys):
    result = {}
    for key in keys & value.keys():
        item = value[key]
        if key in BOOLS:
            if type(item) is bool:
                result[key] = item
        elif key in STRINGS and type(item) is str:
            if item in STRINGS[key]:
                result[key] = item
        elif type(item) is int and 0 <= item <= 0xffffffffffffffff:
            result[key] = item
    return result


def validate_identity(info, radios, load):
    required = {"firmware": "0.4.11-load-bench", "build": "load-benchmark",
                "mode": "bench", "sensor_inputs_enabled": False,
                "actuators_enabled": False, "simulation_protocol": 1,
                "load_benchmark_protocol": 1, "simulation_enabled": False,
                "refresh_mode": 0, "reduced_motion": False, "profile_persisted": True}
    if any(info.get(key) != value or type(info.get(key)) is not type(value)
           for key, value in required.items()):
        raise BenchError("target_or_preconditions_mismatch")
    if (radios.get("running") is not True or radios.get("memory_failure") is not False
            or any(radios.get(key) is not False for key in ACTIVE_RADIO)
            or any(type(radios.get(key)) is not bool for key in ("wifi_enabled", "ble_enabled"))):
        raise BenchError("radios_active_or_unknown")
    if load == "sd-refresh" and (info.get("sd_mounted") is not True
                                 or type(info.get("profile_revision")) is not int):
        raise BenchError("sd_profile_not_ready")


def validate_status(status, job_id, mode, duration, previous=None):
    if (not LOAD <= status.keys() or status.get("kind") != "load_benchmark"
            or status.get("state") not in STATES
            or any(type(status.get(key)) is not int or not 0 <= status[key] <= 0xffffffffffffffff
                   for key in LOAD - {"state"})):
        raise BenchError("invalid_load_status")
    if (status["protocol"] != 1 or status["id"] != job_id or status["mode"] != mode
            or status["duration_ms"] != duration):
        raise BenchError("load_correlation_failed")
    expected = ((0, 0, 0), (16, 100, 32), (32, 1000, 128))[mode]
    if (status["channels"], status["rate_hz"], status["iterations"]) != expected:
        raise BenchError("load_configuration_mismatch")
    if (status["pending"] not in (0, 1)
            or status["produced"] != status["consumed"] + status["overwritten"] + status["pending"]):
        raise BenchError("mailbox_accounting_failed")
    if (status["sequence_gaps"] > status["overwritten"] or
            status["pending"] == 0 and status["sequence_gaps"] != status["overwritten"]):
        raise BenchError("mailbox_sequence_accounting_failed")
    if status["state"] == "done" and (
            status["produced"] + status["missed_periods"] != (duration * status["rate_hz"] + 999) // 1000
            or status["elapsed_us"] < duration * 1000):
        raise BenchError("load_release_accounting_failed")
    if mode == 0 and any(status[key] for key in MONOTONIC - {"elapsed_us"} | {"pending", "checksum"}):
        raise BenchError("control_performed_work")
    if previous and any(status[key] < previous[key] for key in MONOTONIC):
        raise BenchError("load_counter_regression")
    if status["state"] == "error" or any(status[key] == 0 for key in MEMORY):
        raise BenchError("load_memory_or_worker_failure")
    return status


def verify_window(status, ui):
    if (ui.get("synthetic") is not True or ui.get("interrupted") is not False
            or type(ui.get("duration_ms")) is not int or ui["duration_ms"] < 9900
            or type(ui.get("presentations")) is not int or ui["presentations"] <= 0):
        raise BenchError("ui_measurement_incomplete")
    values = (status["phase_started_us"], ui.get("observed_start_us"),
              ui.get("observed_end_us"), status["phase_finished_us"])
    if (any(type(value) is not int for value in values)
            or not values[0] <= values[1] < values[2] <= values[3]
            or values[3] - values[0] < status["duration_ms"] * 1000
            or values[2] - values[1] < 9_900_000):
        raise BenchError("ui_outside_load_window")
    return {"ui_start_offset_ms": round((values[1] - values[0]) / 1000),
            "ui_window_ms": round((values[2] - values[1]) / 1000),
            "load_wall_ms": round((values[3] - values[0]) / 1000)}


class Bench:
    def __init__(self, port, clock=time):
        self.port, self.clock = port, clock
        self.buffer = bytearray()
        self.discard_line = False
        self.pending_ui = []
        self.token = None
        self.sequence = self.frames = self.skipped_frames = 0
        self.streaming = False
        self.started = self.next_frame = 0.0
        self.max_gap = 0.0
        self.previous_frame = None

    def send(self, command):
        self.port.write((command + "\n").encode("ascii"))

    def start_stream(self, token, rate):
        self.token, self.rate = token, rate
        self.started = self.next_frame = self.clock.monotonic()
        self.streaming = True

    def poll(self):
        now = self.clock.monotonic()
        if self.streaming and now >= self.next_frame:
            wave = 1 - math.cos((now - self.started) * 2 * math.pi / 8)
            self.sequence += 1
            self.send(f"SIM1 {self.token} {self.sequence} {round(1500 + 1000 * wave)} {round(400 + 200 * wave)} 850 350 3 0 0")
            self.frames += 1
            if self.previous_frame is not None:
                self.max_gap = max(self.max_gap, now - self.previous_frame)
            self.previous_frame = now
            self.next_frame += 1 / self.rate
            if self.next_frame <= now:
                self.skipped_frames += int((now - self.next_frame) * self.rate) + 1
                self.next_frame = now + 1 / self.rate
        events = []
        for byte in self.port.read(2048):
            if byte == 10:
                if not self.discard_line:
                    line = bytes(self.buffer).strip()
                    try:
                        value = json.loads(line)
                    except (ValueError, UnicodeError, RecursionError):
                        value = None
                    if isinstance(value, dict):
                        kind = "info" if value.get("project") == "NGK ECU" else value.get("kind")
                        keys = ({"info": INFO, "radios": RADIOS, "sim": SIM,
                                 "ui_motion_benchmark": UI, "load_benchmark": LOAD}.get(kind)
                                if type(kind) is str else None)
                        if keys:
                            events.append({"kind": kind, **bounded(value, keys)})
                    elif line in {item.encode("ascii") for item in ACK}:
                        events.append(line.decode("ascii"))
                self.buffer.clear()
                self.discard_line = False
            elif not self.discard_line and len(self.buffer) < 4096:
                self.buffer.append(byte)
            else:
                self.buffer.clear()
                self.discard_line = True
        return events

    def keep_ui(self, event):
        if len(self.pending_ui) >= 2:
            raise BenchError("excess_ui_reports")
        self.pending_ui.append(event)

    def consume(self, predicate):
        match = None
        for event in self.poll():
            if isinstance(event, dict) and event.get("kind") == "sim" and event.get("state") in {"busy", "rejected"}:
                raise BenchError("sim_refused")
            if match is None and predicate(event):
                match = event
            elif isinstance(event, dict) and event.get("kind") == "ui_motion_benchmark":
                self.keep_ui(event)
        return match

    def wait(self, predicate, timeout=2):
        deadline = self.clock.monotonic() + timeout
        while self.clock.monotonic() < deadline:
            match = self.consume(predicate)
            if match is not None:
                return match
            self.clock.sleep(.001)
        raise BenchError("control_timeout")

    def request(self, command, kind, timeout=2):
        self.send(command)
        return self.wait(lambda event: isinstance(event, dict) and event.get("kind") == kind, timeout)

    def ack(self, command, accepted, refused, timeout=2):
        self.send(command)
        reply = self.wait(lambda event: isinstance(event, str) and event in (accepted, refused), timeout)
        if reply != accepted:
            raise BenchError("command_refused")

    def delay(self, seconds):
        deadline = self.clock.monotonic() + seconds
        while self.clock.monotonic() < deadline:
            self.consume(lambda event: False)
            self.clock.sleep(min(.001, max(0, deadline - self.clock.monotonic())))

    def ui_report(self, timeout=2):
        if self.pending_ui:
            return self.pending_ui.pop(0)
        return self.wait(lambda event: isinstance(event, dict) and event.get("kind") == "ui_motion_benchmark", timeout)

    def startup(self):
        deadline = self.clock.monotonic() + 8
        while self.clock.monotonic() < deadline:
            try:
                info = self.request("info", "info", min(1, deadline - self.clock.monotonic()))
                radios = self.request("radios", "radios", min(1, max(0, deadline - self.clock.monotonic())))
                if info.get("profile_persisted") is True and radios.get("running") is True:
                    return info, radios
            except BenchError:
                pass
            self.delay(min(.1, max(0, deadline - self.clock.monotonic())))
        raise BenchError("startup_timeout")

    def radio(self, wifi, ble, tolerate_ack_failure=False):
        for command in ("wifi-on" if wifi else "wifi-off", "ble-on" if ble else "ble-off"):
            try:
                self.ack(command, "RADIO_QUEUED", "RADIO_REFUSED")
            except (Exception, KeyboardInterrupt):
                if not tolerate_ack_failure:
                    raise
        deadline = self.clock.monotonic() + 8
        while self.clock.monotonic() < deadline:
            state = self.request("radios", "radios")
            if state.get("memory_failure") is not False or (not tolerate_ack_failure and any(
                    state.get(key) is not False for key in
                    ("wifi_connected", "wifi_connecting", "ble_connected", "ble_pairing_pending"))):
                raise BenchError("radio_memory_or_activity_failure")
            if (state.get("running") is True and state.get("wifi_enabled") is wifi
                    and state.get("ble_enabled") is ble and state.get("wifi_scanning") is False
                    and state.get("ble_advertising") is False
                    and all(state.get(key) is False for key in ACTIVE_RADIO)):
                return state
            self.delay(.05)
        raise BenchError("radio_settle_timeout")

    def motion(self):
        deadline = self.clock.monotonic() + 2
        while self.clock.monotonic() < deadline:
            self.send("motion-bench")
            reply = self.wait(lambda event: isinstance(event, str) and event in
                              {"MOTION_BENCH_STARTED", "MOTION_BENCH_REFUSED"},
                              deadline - self.clock.monotonic())
            if reply == "MOTION_BENCH_STARTED":
                return
            if self.request("sim-status", "sim").get("live") is not True:
                raise BenchError("sim_not_live")
            self.delay(.05)
        raise BenchError("motion_refused")


def observe_extra_load(bench, load, original_revision):
    if load == "off":
        return True
    command = {"sd-refresh": "sd-refresh", "wifi-scan": "wifi-scan",
               "ble-advertising": "ble-discover"}[load]
    bench.ack(command, "SD_REFRESH_QUEUED" if load == "sd-refresh" else "RADIO_QUEUED",
              "SD_REFRESH_REFUSED queue" if load == "sd-refresh" else "RADIO_REFUSED")
    deadline = bench.clock.monotonic() + 3
    while bench.clock.monotonic() < deadline:
        if load == "sd-refresh":
            state = bench.request("info", "info")
            if (state.get("sd_mounted") is True and state.get("profile_persisted") is True
                    and state.get("profile_status") == 12
                    and type(state.get("profile_revision")) is int
                    and state["profile_revision"] > original_revision):
                return True
        else:
            state = bench.request("radios", "radios")
            if state.get("memory_failure") is not False:
                raise BenchError("radio_memory_failure")
            field = "wifi_scanning" if load == "wifi-scan" else "ble_advertising"
            if state.get(field) is True:
                return True
        bench.delay(.02)
    raise BenchError("extra_load_not_observed")


def cleanup(bench, result, original, load_attempted):
    # Independent attempts: one failed stop must not suppress later restoration.
    bench.streaming = False
    if load_attempted:
        try:
            bench.ack("load-stop", "LOAD_STOP_QUEUED", "LOAD_REFUSED")
        except (Exception, KeyboardInterrupt):
            pass
        try:
            deadline = bench.clock.monotonic() + 3
            while bench.clock.monotonic() < deadline:
                status = bench.request("load-status", "load_benchmark")
                if status.get("state") in {"idle", "done", "stopped"} and status.get("pending") == 0:
                    result["load_cleanup_confirmed"] = True
                    break
                bench.delay(.05)
        except (Exception, KeyboardInterrupt):
            pass
    else:
        result["load_cleanup_confirmed"] = True
    if bench.token is not None:
        try:
            final = bench.request("sim-status", "sim")
            if type(final.get("seq")) is int:
                result["sim_latest_sequence_delta"] = final["seq"]
        except (Exception, KeyboardInterrupt):
            pass
        try:
            off = bench.request(f"sim-stop {bench.token}", "sim")
            result["sim_off_confirmed"] = off.get("state") == "off"
        except (Exception, KeyboardInterrupt):
            pass
        if result.get("sim_off_confirmed") is not True:
            try:
                result["sim_off_confirmed"] = bench.request("sim-status", "sim").get("state") == "off"
            except (Exception, KeyboardInterrupt):
                pass
    # Each radio command is attempted even after a preceding ACK is lost.
    try:
        bench.radio(False, False, tolerate_ack_failure=True)
    except (Exception, KeyboardInterrupt):
        pass
    try:
        bench.radio(original["wifi_enabled"], original["ble_enabled"], tolerate_ack_failure=True)
        result["radios_restored"] = True
    except (Exception, KeyboardInterrupt):
        pass
    if not all(result.get(key) is True for key in
               ("load_cleanup_confirmed", "sim_off_confirmed", "radios_restored")):
        result["completed"] = False


def run_bench(bench, result, *, mode=1, load="off", duration=15000, rate=20):
    if mode not in (0, 1, 2) or load not in {"off", "sd-refresh", "wifi-scan", "ble-advertising"} or not 15000 <= duration <= 120000 or rate not in (20, 50, 100):
        raise BenchError("invalid_bench_options")
    result.update({"completed": False, "mode": mode, "load": load,
                   "requested_duration_ms": duration, "producer_hz_target": rate,
                   "load_cleanup_confirmed": False, "sim_off_confirmed": False,
                   "radios_restored": False, "status_polls": 0,
                   "measurement_notes": ["SIM sequence is a watermark, not accepted-frame count or loss rate.",
                                         "Heap minimum and stack high-water marks are cumulative since boot/task start.",
                                         "UI covers one ten-second window; tick times are wall time, not free CPU."]})
    info, original = bench.startup()
    validate_identity(info, original, load)
    prior = bench.request("load-status", "load_benchmark")
    if prior.get("state") not in {"idle", "done", "stopped"} or prior.get("pending") != 0:
        raise BenchError("prior_load_not_idle")
    if bench.request("sim-status", "sim").get("state") != "off":
        raise BenchError("prior_sim_not_idle")
    result["info_before"] = bounded(info, INFO - {"profile_revision"})
    result["radio_before"] = bounded(original, RADIOS)
    load_attempted = False
    try:
        bench.radio(load == "wifi-scan", load == "ble-advertising")
        bench.token = secrets.randbelow(0xffffffff) + 1
        ready = bench.request(f"sim-start {bench.token}", "sim")
        if ready.get("state") != "ready" or ready.get("session") != bench.token or ready.get("protocol") != 1:
            raise BenchError("sim_handshake_failed")
        bench.start_stream(bench.token, rate)
        bench.delay(2)
        if bench.request("sim-status", "sim").get("live") is not True:
            raise BenchError("sim_not_live")
        job_id = secrets.randbelow(0xffffffff) + 1
        load_attempted = True
        bench.ack(f"load-start {job_id} {mode} {duration}", "LOAD_QUEUED", "LOAD_REFUSED")
        deadline = bench.clock.monotonic() + duration / 1000 + 5
        previous, ui = None, None
        next_status = bench.clock.monotonic()
        motion_started = False
        while bench.clock.monotonic() < deadline:
            bench.delay(max(0, next_status - bench.clock.monotonic()))
            status = validate_status(bench.request("load-status", "load_benchmark"),
                                     job_id, mode, duration, previous)
            result["status_polls"] += 1
            previous = status
            if "memory_start" not in result:
                result["memory_start"] = {key: status[key] for key in MEMORY}
                result["memory_minima"] = dict(result["memory_start"])
            for key in MEMORY:
                result["memory_minima"][key] = min(result["memory_minima"][key], status[key])
            result["memory_end"] = {key: status[key] for key in MEMORY}
            if status["state"] == "running" and not motion_started:
                bench.motion()
                motion_started = True
                result["load_observed"] = observe_extra_load(bench, load, info.get("profile_revision"))
            if bench.pending_ui and ui is None:
                ui = bench.ui_report()
            radios = bench.request("radios", "radios")
            if (radios.get("running") is not True or radios.get("memory_failure") is not False
                    or radios.get("wifi_enabled") is not (load == "wifi-scan")
                    or radios.get("ble_enabled") is not (load == "ble-advertising")
                    or any(radios.get(key) is not False for key in
                    ("wifi_connected", "wifi_connecting", "ble_connected", "ble_pairing_pending"))):
                raise BenchError("unexpected_radio_activity_or_memory_failure")
            if bench.request("sim-status", "sim").get("live") is not True:
                raise BenchError("sim_liveness_lost")
            if status["state"] == "done" and status["pending"] == 1:
                # Main may drain the last item after this terminal snapshot.
                # Re-enter the normal correlated, monotonic validation path.
                next_status = bench.clock.monotonic() + 1
                continue
            if status["state"] in {"done", "stopped"}:
                if status["state"] != "done" or not motion_started:
                    raise BenchError("load_ended_early")
                if ui is None:
                    ui = bench.ui_report()
                result.update(verify_window(status, ui))
                result["load_metrics"] = {key: status[key] for key in LOAD -
                                          {"id", "phase_started_us", "phase_finished_us"}}
                result["ui_metrics"] = bounded(ui, UI - {"observed_start_us", "observed_end_us"})
                if mode and not status["produced"]:
                    raise BenchError("load_did_no_work")
                result["completed"] = True
                break
            next_status += 1
            if next_status <= bench.clock.monotonic():
                next_status = bench.clock.monotonic() + 1
        else:
            raise BenchError("load_deadline_exceeded")
    finally:
        elapsed = max(0, bench.clock.monotonic() - bench.started) if bench.frames else 0
        result["producer_frames"] = bench.frames
        result["producer_elapsed_ms"] = round(elapsed * 1000)
        result["producer_hz_observed"] = bench.frames / elapsed if elapsed else 0
        result["producer_max_gap_us"] = round(bench.max_gap * 1e6)
        result["producer_skipped_slots"] = bench.skipped_frames
        cleanup(bench, result, original, load_attempted)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--mode", type=int, choices=(0, 1, 2), default=1)
    parser.add_argument("--load", choices=("off", "sd-refresh", "wifi-scan", "ble-advertising"), default="off")
    parser.add_argument("--rate", type=int, choices=(20, 50, 100), default=20)
    parser.add_argument("--duration", type=int, default=15000, help="Load duration in milliseconds, 15000..120000")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--execute-physical-bench", action="store_true")
    args = parser.parse_args(argv)
    if not args.execute_physical_bench:
        parser.error("Explicit --execute-physical-bench required")
    if not 15000 <= args.duration <= 120000:
        parser.error("Duration must be 15000..120000 ms")
    if args.output.exists():
        parser.error("Refusing to overwrite an existing result")
    result, port = {"completed": False}, None
    previous_sigterm = None
    try:
        import serial  # Import/open only after explicit physical execution.

        port = serial.Serial(port=None, baudrate=115200, timeout=.001, write_timeout=1, exclusive=True)
        port.port, port.dtr, port.rts = args.port, False, False
        port.open()
        port.reset_input_buffer()
        def interrupt_bench(signum, frame):
            raise KeyboardInterrupt

        previous_sigterm = signal.signal(signal.SIGTERM, interrupt_bench)
        run_bench(Bench(port), result, mode=args.mode, load=args.load,
                  duration=args.duration, rate=args.rate)
    except (Exception, KeyboardInterrupt) as error:
        result["completed"] = False
        result["failure"] = str(error) if isinstance(error, BenchError) else "control_or_transport_failure"
    finally:
        if previous_sigterm is not None:
            signal.signal(signal.SIGTERM, previous_sigterm)
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
    print(json.dumps({key: result[key] for key in ("completed", "failure", "port_closed") if key in result}))
    return 0 if result["completed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
