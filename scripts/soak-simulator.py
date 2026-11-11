#!/usr/bin/env python3
"""Real-clock Qt/model/codec/link soak with bounded synthetic serial only.

Run with .venv/bin/python scripts/soak-simulator.py --seconds 1800 --output FILE.
The JSON contains aggregates only; this does not validate USB or ESP32 hardware.
"""
from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
import json
import math
import os
from pathlib import Path
import sys
import threading
import time
from unittest.mock import patch

os.environ["QT_QPA_PLATFORM"] = "offscreen"
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "simulator"))

from PySide6.QtCore import QTimer, Qt
from PySide6.QtWidgets import QApplication
from ngk_simulator.link import SerialLink
from ngk_simulator.window import SimulatorWindow


@dataclass
class RunningStats:
    count: int = 0
    total: float = 0.0
    low: float = math.inf
    high: float = -math.inf

    def add(self, value: float) -> None:
        self.count += 1
        self.total += value
        self.low = min(self.low, value)
        self.high = max(self.high, value)

    def export(self) -> dict:
        return {"count": self.count, "min": self.low if self.count else None,
                "max": self.high if self.count else None,
                "mean": self.total / self.count if self.count else None}


class MemoryBus:
    """Only counters survive a closed synthetic device, never sessions or data."""
    def __init__(self):
        self._lock = threading.Lock()
        self._counts = dict.fromkeys(("opens", "closes", "active_devices", "frames",
                                     "rejected_frames", "starts", "stops", "statuses",
                                     "reply_overflows", "invalid_commands", "sequence_errors"), 0)

    def add(self, **increments) -> None:
        with self._lock:
            for key, number in increments.items():
                self._counts[key] += number

    def snapshot(self) -> dict:
        with self._lock:
            return dict(self._counts)


class MemorySerial:
    """SIM1 device: one bounded reply buffer and one active session, in RAM only."""
    def __init__(self, bus: MemoryBus, clock=time.monotonic):
        self._bus, self._clock = bus, clock
        self._reply = bytearray()
        self._wake = threading.Event()
        self._opened = False
        self._session = self._sequence = 0
        self._last_frame: float | None = None
        self.timeout = .02

    def open(self) -> None:
        if not self._opened:
            self._opened = True
            self._bus.add(opens=1, active_devices=1)

    def close(self) -> None:
        if self._opened:
            self._opened = False
            self._session = self._sequence = 0
            self._last_frame = None
            self._reply.clear()
            self._bus.add(closes=1, active_devices=-1)
            self._wake.set()

    def reset_input_buffer(self) -> None:
        self._reply.clear()
        self._wake.clear()

    def _respond(self, value: dict) -> None:
        reply = json.dumps(value, separators=(",", ":")).encode("ascii") + b"\n"
        if len(self._reply) + len(reply) <= 2048:
            self._reply.extend(reply)
            self._wake.set()
        else:
            self._bus.add(reply_overflows=1)

    def read(self, size: int) -> bytes:
        if not self._reply:
            self._wake.wait(self.timeout)
        result = bytes(self._reply[:size])
        del self._reply[:size]
        if not self._reply:
            self._wake.clear()
        return result

    def write(self, data: bytes) -> int:
        if not self._opened:
            raise OSError("synthetic transport closed")
        if len(data) > 128 or not data.endswith(b"\n"):
            self._bus.add(invalid_commands=1)
            return len(data)
        fields = data.split()
        try:
            if data == b"info\n":
                self._respond({"project": "NGK ECU", "simulation_protocol": 1})
            elif len(fields) == 2 and fields[0] == b"sim-start":
                token = int(fields[1])
                if not 1 <= token <= 0xFFFFFFFF:
                    raise ValueError
                self._session, self._sequence, self._last_frame = token, 0, None
                self._bus.add(starts=1)
                self._respond({"kind": "sim", "state": "ready", "protocol": 1, "session": token})
            elif fields and fields[0] == b"SIM1":
                self._frame(fields)
            elif data == b"sim-status\n":
                live = (self._session != 0 and self._last_frame is not None
                        and self._clock() - self._last_frame < .75)
                self._bus.add(statuses=1)
                self._respond({"kind": "sim", "state": "live" if live else "stale",
                               "protocol": 1, "live": live})
            elif len(fields) == 2 and fields[0] == b"sim-stop":
                if int(fields[1]) == self._session:
                    self._session = self._sequence = 0
                    self._last_frame = None
                    self._bus.add(stops=1)
                    self._respond({"kind": "sim", "state": "off", "protocol": 1, "live": False})
            else:
                self._bus.add(invalid_commands=1)
        except (ValueError, OverflowError):
            self._bus.add(invalid_commands=1)
        return len(data)

    def _frame(self, fields: list[bytes]) -> None:
        try:
            if len(fields) != 10:
                raise ValueError
            token, seq, rpm, speed, temp, throttle, gear, oil, faults = map(int, fields[1:])
            if (not self._session or token != self._session or not 1 <= seq <= 0xFFFFFFFF
                    or not 0 <= rpm <= 12000 or not 0 <= speed <= 2500
                    or not -400 <= temp <= 2000 or not 0 <= throttle <= 1000
                    or not -1 <= gear <= 4 or not 0 <= oil <= 2 or not 0 <= faults <= 255):
                raise ValueError
            if seq != self._sequence + 1:
                self._bus.add(sequence_errors=1)
                raise ValueError
        except (ValueError, OverflowError):
            self._bus.add(rejected_frames=1)
            return
        self._sequence, self._last_frame = seq, self._clock()
        self._bus.add(frames=1)


class ObservedLink(SerialLink):
    def __init__(self, bus: MemoryBus):
        super().__init__(serial_factory=lambda: MemorySerial(bus))
        self.samples = 0
        self.invalid_samples = 0
        self.sample_gaps = RunningStats()
        self._previous_sample: float | None = None

    def set_sample(self, sample, paused=False) -> None:
        now = time.monotonic()
        if self._previous_sample is not None:
            self.sample_gaps.add(now - self._previous_sample)
        self._previous_sample = now
        self.samples += 1
        if not all(math.isfinite(value) for value in
                   (sample.rpm, sample.speed_kph, sample.block_c, sample.throttle_pct)):
            self.invalid_samples += 1
        super().set_sample(sample, paused=paused)


def rss_bytes() -> tuple[int, str]:
    try:
        pages = int(Path("/proc/self/statm").read_text().split()[1])
        return pages * os.sysconf("SC_PAGE_SIZE"), "resident_statm"
    except (OSError, ValueError, IndexError):
        import resource
        scale = 1 if sys.platform == "darwin" else 1024
        return int(resource.getrusage(resource.RUSAGE_SELF).ru_maxrss * scale), "peak_resource"


class Soak:
    def __init__(self, app: QApplication, seconds: float):
        self.app, self.seconds = app, seconds
        self.bus = MemoryBus()
        self.link = ObservedLink(self.bus)
        # Keep even port enumeration entirely synthetic. Only the injected
        # factory can open a transport; no pyserial.Serial instance is created.
        with patch("ngk_simulator.window.available_ports", return_value=["MEMORY"]):
            self.window = SimulatorWindow(link=self.link)
        self.original_controls = asdict(self.window.controls)
        self.started = time.monotonic()
        self.finished = False
        self.failures: set[str] = set()
        self.tick_gaps = RunningStats()
        self.rss = RunningStats()
        self.rss_start, self.rss_kind = rss_bytes()
        self.rss.add(self.rss_start)
        self.rss_last = self.rss_start
        self.thread_peak = threading.active_count()
        self.states = dict.fromkeys(("offline", "connecting", "streaming", "paused", "source_stale", "error"), 0)
        self.phases: dict[int, str] = {}
        self.max_cycle = 0
        self.pauses = self.freezes = self.disconnects = 0
        self.pause_recoveries = self.stale_recoveries = self.reconnect_recoveries = 0
        self.pause_ttl_observed = self.stale_ttl_observed = 0
        self._pending_pause = self._pending_stale = self._pending_reconnect = False
        self._saw_pause = self._saw_stale = self._saw_offline = False
        self._last_tick = self.started
        self._next_rss = 1.0
        self._next_checkpoint = 60.0
        self._actions = {"pause": 2.0, "freeze": 6.0, "disconnect": 10.0}
        self._restore_at: dict[str, float] = {}
        self.timer = QTimer()
        self.timer.setTimerType(Qt.TimerType.PreciseTimer)
        self.timer.setInterval(50)
        self.timer.timeout.connect(self.tick)
        self.deadline = QTimer()
        self.deadline.setSingleShot(True)
        self.deadline.setTimerType(Qt.TimerType.PreciseTimer)
        self.deadline.timeout.connect(self.finish)

    def start(self) -> None:
        self.window.show()
        self.window.cycle_repeat_switch.setChecked(True)
        self.window._start_cycle()
        self.window._connect()
        self.timer.start()
        self.deadline.start(math.ceil(self.seconds * 1000))

    def tick(self) -> None:
        try:
            self._tick()
        except Exception:
            self.failures.add("observer_exception")
            self.finish(force=True)

    def _tick(self) -> None:
        now = time.monotonic()
        elapsed = now - self.started
        self.tick_gaps.add(now - self._last_tick)
        self._last_tick = now
        status = self.link.snapshot()
        if status.state in self.states:
            self.states[status.state] += 1
        else:
            self.failures.add("unknown_link_state")
        if status.state == "error":
            self.failures.add("link_error")
        self._saw_pause |= status.state == "paused"
        self._saw_stale |= status.state == "source_stale"
        self._saw_offline |= self._pending_reconnect and status.state == "offline"
        if status.state == "paused" and not status.device_live:
            self.pause_ttl_observed += 1
        if status.state == "source_stale" and not status.device_live:
            self.stale_ttl_observed += 1
        if status.state == "streaming" and status.device_live:
            if self._pending_pause and self._saw_pause:
                self.pause_recoveries += 1
                self._pending_pause = self._saw_pause = False
            if self._pending_stale and self._saw_stale:
                self.stale_recoveries += 1
                self._pending_stale = self._saw_stale = False
            if self._pending_reconnect and self._saw_offline:
                self.reconnect_recoveries += 1
                self._pending_reconnect = self._saw_offline = False
        frame = self.window._cycle_frame
        if frame is not None:
            if 0 <= frame.index < 10:
                self.phases[frame.index] = frame.phase_name
            else:
                self.failures.add("invalid_phase_index")
            self.max_cycle = max(self.max_cycle, frame.cycle_number)
        if not self.window._cycle.running:
            self.failures.add("cycle_stopped_early")
        self.thread_peak = max(self.thread_peak, threading.active_count())
        if elapsed >= self._next_rss:
            self.rss_last, _ = rss_bytes()
            self.rss.add(self.rss_last)
            self._next_rss = elapsed + 1.0
        for action, restore_at in tuple(self._restore_at.items()):
            if elapsed >= restore_at:
                del self._restore_at[action]
                if action == "pause":
                    self.window.pause_usb_switch.setChecked(False)
                elif action == "freeze":
                    self.window._timer.start()
                else:
                    self.window._connect()
        for action, due in self._actions.items():
            # Leave enough real time for expiry and recovery before the deadline.
            if elapsed >= due and self.seconds - elapsed >= 3:
                self._actions[action] = due + 60
                if action == "pause":
                    self.window.pause_usb_switch.setChecked(True)
                    self.pauses += 1
                    self._pending_pause, self._saw_pause = True, False
                    duration = 1.6
                elif action == "freeze":
                    self.window._timer.stop()
                    self.freezes += 1
                    self._pending_stale, self._saw_stale = True, False
                    duration = 1.6
                else:
                    self.window._disconnect()
                    self.disconnects += 1
                    self._pending_reconnect, self._saw_offline = True, False
                    duration = .6
                self._restore_at[action] = elapsed + duration
        if elapsed >= self._next_checkpoint:
            self._next_checkpoint += 60
            print(json.dumps({"elapsed_s": round(elapsed, 3), "samples": self.link.samples,
                              "frames": self.bus.snapshot()["frames"], "rss_bytes": self.rss_last,
                              "threads": threading.active_count(), "phases": len(self.phases),
                              "errors": len(self.failures)}, sort_keys=True), flush=True)

    def finish(self, force=False) -> None:
        if self.finished:
            return
        remaining = self.seconds - (time.monotonic() - self.started)
        if remaining > 0 and not force:
            self.deadline.start(max(1, math.ceil(remaining * 1000)))
            return
        self.finished = True
        self.timer.stop()
        self.deadline.stop()
        self.elapsed = time.monotonic() - self.started
        self.window.pause_usb_switch.setChecked(False)
        self.window._timer.start()
        self.window._stop_cycle()
        self.controls_restored = asdict(self.window.controls) == self.original_controls
        self.window.close()
        self.link.close()
        self.app.quit()

    def report(self) -> dict:
        transport = self.bus.snapshot()
        worker_closed = not self.link._thread or not self.link._thread.is_alive()
        checks = {"real_deadline_reached": self.elapsed >= self.seconds,
                  "samples_produced": self.link.samples > 0,
                  "frames_transmitted": transport["frames"] > 0,
                  "valid_samples": self.link.invalid_samples == 0,
                  "valid_protocol": all(transport[key] == 0 for key in
                                        ("rejected_frames", "invalid_commands", "reply_overflows", "sequence_errors")),
                  "controls_restored": self.controls_restored,
                  "cycle_stopped_on_close": not self.window._cycle.running,
                  "worker_joined": worker_closed,
                  "devices_closed": transport["active_devices"] == 0 and transport["opens"] == transport["closes"],
                  "sessions_stopped": transport["starts"] == transport["stops"],
                  "pause_recovered": self.pauses == self.pause_recoveries,
                  "producer_watchdog_recovered": self.freezes == self.stale_recoveries,
                  "reconnection_recovered": self.disconnects == self.reconnect_recoveries,
                  "pause_ttl_expired": self.pauses == 0 or self.pause_ttl_observed > 0,
                  "producer_ttl_expired": self.freezes == 0 or self.stale_ttl_observed > 0,
                  "all_phases_for_full_cycle": self.seconds < 60 or len(self.phases) == 10}
        self.failures.update(key for key, passed in checks.items() if not passed)
        return {"schema_version": 1, "scope": "synthetic_qt_model_codec_link_only",
                "requested_seconds": self.seconds, "elapsed_seconds": round(self.elapsed, 6),
                "passed": not self.failures, "checks": checks, "failures": sorted(self.failures),
                "samples": self.link.samples, "invalid_samples": self.link.invalid_samples,
                "producer_gap_seconds": self.link.sample_gaps.export(),
                "observer_gap_seconds": self.tick_gaps.export(),
                "rss_bytes": {**self.rss.export(), "initial": self.rss_start,
                              "final": self.rss_last, "growth": self.rss_last - self.rss_start,
                              "measurement": self.rss_kind},
                "threads": {"peak": self.thread_peak, "final": threading.active_count(),
                            "link_worker_joined": worker_closed},
                "transport": transport, "state_observations": self.states,
                "cycle": {"phases": [{"index": index, "name": self.phases[index]}
                                     for index in sorted(self.phases)], "max_cycle_number": self.max_cycle},
                "perturbations": {"pauses": self.pauses, "pause_recoveries": self.pause_recoveries,
                                  "producer_stops": self.freezes, "producer_recoveries": self.stale_recoveries,
                                  "disconnects": self.disconnects, "reconnect_recoveries": self.reconnect_recoveries,
                                  "pause_ttl_observations": self.pause_ttl_observed,
                                  "producer_ttl_observations": self.stale_ttl_observed},
                "limits": ["No physical USB, ESP32, LCD, sensors or vehicle validation",
                           "Current RSS on Linux; peak RSS fallback on other systems",
                           "Short runs do not establish 30-minute stability or complete phase coverage"]}


def positive_seconds(value: str) -> float:
    number = float(value)
    if not math.isfinite(number) or number <= 0 or number * 1000 > 2147483647:
        raise argparse.ArgumentTypeError("seconds must be positive, finite and fit a Qt timer")
    return number


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=positive_seconds, default=1800.0)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    app = QApplication.instance() or QApplication([])
    app.setQuitOnLastWindowClosed(False)
    soak = Soak(app, args.seconds)
    previous_hook = sys.excepthook

    def exception_hook(kind, value, traceback):
        soak.failures.add("qt_slot_exception")
        QTimer.singleShot(0, lambda: soak.finish(force=True))

    sys.excepthook = exception_hook
    try:
        soak.start()
        app.exec()
    finally:
        if not soak.finished:
            soak.failures.add("event_loop_ended_early")
            soak.finish(force=True)
        sys.excepthook = previous_hook
    result = soak.report()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True, allow_nan=False) + "\n")
    print(json.dumps({"elapsed_s": result["elapsed_seconds"], "passed": result["passed"],
                      "samples": result["samples"], "frames": result["transport"]["frames"],
                      "failures": result["failures"]}, sort_keys=True), flush=True)
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
