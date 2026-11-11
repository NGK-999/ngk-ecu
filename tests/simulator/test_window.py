"""Real Qt interactions with a bounded in-memory link; no physical serial I/O."""
from __future__ import annotations

import os
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "simulator"))

from PySide6.QtCore import Qt
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication, QCheckBox, QComboBox, QDoubleSpinBox, QLabel, QPushButton, QSlider
from ngk_simulator.window import SimulatorWindow


class MemoryLink:
    def __init__(self):
        self.connected = []
        self.close_count = 0
        self.sample = None
        self.paused = False
        self.status = SimpleNamespace(state="offline", detail="Somente neste computador", port="",
                                      device_live=False, sequence=0)

    def connect(self, port):
        self.connected.append(port)
        self.status = SimpleNamespace(state="streaming", detail="Transporte em memoria", port=port,
                                      device_live=True, sequence=1)

    def disconnect(self):
        self.status = SimpleNamespace(state="offline", detail="Desconectado", port="",
                                      device_live=False, sequence=0)

    def close(self):
        self.close_count += 1

    def set_sample(self, sample, paused=False):
        self.sample, self.paused = sample, paused

    def snapshot(self):
        return self.status


class WindowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])

    def setUp(self):
        self.link = MemoryLink()
        with patch("ngk_simulator.window.available_ports", return_value=["MEMORY"], create=True):
            self.window = SimulatorWindow(link=self.link)
        self.window.show()
        QTest.qWait(45)

    def tearDown(self):
        self.window.close()
        self.window.deleteLater()
        self.app.processEvents()

    def widget(self, kind, name):
        result = self.window.findChild(kind, name)
        self.assertIsNotNone(result, name)
        return result

    def test_connection_requires_explicit_click_and_disconnection_updates_controls(self):
        self.assertEqual(self.link.connected, [])
        connect = self.widget(QPushButton, "connect_button")
        disconnect = self.widget(QPushButton, "disconnect_button")
        self.assertFalse(disconnect.isEnabled())
        QTest.mouseClick(connect, Qt.MouseButton.LeftButton)
        QTest.qWait(45)
        self.assertEqual(self.link.connected, ["MEMORY"])
        self.assertFalse(connect.isEnabled())
        self.assertTrue(disconnect.isEnabled())
        QTest.mouseClick(disconnect, Qt.MouseButton.LeftButton)
        QTest.qWait(45)
        self.assertTrue(connect.isEnabled())
        self.assertFalse(disconnect.isEnabled())

    def test_source_stale_keeps_connection_controls_active_and_names_the_problem(self):
        self.link.status = SimpleNamespace(state="source_stale", detail="Origem sem dados novos", port="MEMORY",
                                           device_live=False, sequence=4)
        self.window._sync_status()
        self.assertEqual(self.widget(QLabel, "connection_status").text(), "ORIGEM SEM DADOS")
        self.assertFalse(self.widget(QPushButton, "connect_button").isEnabled())
        self.assertTrue(self.widget(QPushButton, "disconnect_button").isEnabled())
        self.assertFalse(self.widget(QComboBox, "port_combo").isEnabled())
        self.assertFalse(self.widget(QPushButton, "refresh_ports_button").isEnabled())
        self.assertEqual(self.widget(QLabel, "connection_detail").text(), "Origem sem dados novos")

    def test_manual_inputs_and_driving_controls_reach_the_model(self):
        self.widget(QSlider, "throttle_slider").setValue(72)
        self.widget(QSlider, "brake_slider").setValue(21)
        self.widget(QCheckBox, "clutch_switch").setChecked(True)
        self.widget(QCheckBox, "engine_switch").setChecked(True)
        gears = self.widget(QComboBox, "gear_combo")
        gears.setCurrentIndex(gears.findData(3))
        self.widget(QCheckBox, "manual_switch").setChecked(True)
        self.widget(QSlider, "rpm_slider").setValue(8500)
        self.widget(QDoubleSpinBox, "speed_spin").setValue(95.5)
        self.widget(QDoubleSpinBox, "temperature_spin").setValue(130.5)
        QTest.qWait(45)
        controls = self.window.controls
        self.assertEqual((controls.throttle_pct, controls.brake_pct, controls.gear), (72, 21, 3))
        self.assertTrue(controls.clutch and controls.engine_on and controls.manual)
        self.assertEqual((controls.manual_rpm, controls.manual_speed_kph, controls.block_c), (8500, 95.5, 130.5))
        self.assertEqual((self.link.sample.rpm, self.link.sample.speed_kph), (8500, 95.5))

    def test_temperature_faults_are_exclusive_and_clear_preserves_controls(self):
        from ngk_simulator.model import Fault
        self.widget(QSlider, "throttle_slider").setValue(69)
        self.widget(QCheckBox, "fault_temp_open").setChecked(True)
        self.widget(QCheckBox, "fault_temp_short").setChecked(True)
        self.widget(QCheckBox, "fault_no_rpm").setChecked(True)
        self.assertFalse(self.window.controls.faults & Fault.TEMP_OPEN)
        self.assertTrue(self.window.controls.faults & Fault.TEMP_SHORT)
        self.assertFalse(self.widget(QCheckBox, "fault_temp_open").isChecked())
        QTest.qWait(45)
        self.assertEqual(self.widget(QLabel, "rpm_value").text(), "--")
        self.assertEqual(self.widget(QLabel, "temperature_value").text(), "--")
        QTest.mouseClick(self.widget(QPushButton, "clear_faults_button"), Qt.MouseButton.LeftButton)
        self.assertEqual(self.window.controls.faults, Fault.NONE)
        self.assertEqual(self.window.controls.throttle_pct, 69)

    def test_manual_tps_is_visible_and_independent_of_manual_rpm(self):
        self.widget(QCheckBox, "manual_switch").setChecked(True)
        self.widget(QDoubleSpinBox, "rpm_spin").setValue(4200)
        throttle = self.widget(QSlider, "throttle_slider")
        self.assertTrue(throttle.isVisible(), "TPS must remain editable in manual mode")
        throttle.setFocus()
        QTest.keyClick(throttle, Qt.Key.Key_End)
        QTest.qWait(45)
        self.assertEqual(self.link.sample.throttle_pct, 100)
        self.assertEqual(self.link.sample.rpm, 4200)
        self.widget(QCheckBox, "fault_tps_invalid").setChecked(True)
        QTest.qWait(45)
        self.assertEqual(self.widget(QLabel, "tps_value").text(), "--")
        self.assertEqual(self.link.sample.rpm, 4200)
        self.assertEqual(self.window.controls.manual_rpm, 4200)
        self.widget(QCheckBox, "fault_tps_invalid").setChecked(False)
        QTest.qWait(45)
        self.assertEqual(self.widget(QLabel, "tps_value").text(), "100 %")
        self.assertEqual(self.link.sample.rpm, 4200)

    def test_pause_and_close_are_forwarded_without_opening_a_device(self):
        self.widget(QCheckBox, "pause_usb_switch").setChecked(True)
        QTest.qWait(45)
        self.assertTrue(self.link.paused)
        self.widget(QCheckBox, "pause_usb_switch").setChecked(False)
        QTest.qWait(45)
        self.assertFalse(self.link.paused)
        self.window.close()
        self.window.close()
        self.assertEqual(self.link.close_count, 1)
        self.assertEqual(self.link.connected, [])

    def test_manual_9000_rpm_shows_critical_warning_and_missing_rpm_clears_it(self):
        self.widget(QCheckBox, "manual_switch").setChecked(True)
        self.widget(QDoubleSpinBox, "rpm_spin").setValue(9000)
        QTest.qWait(45)
        self.assertEqual(self.link.sample.rpm, 9000)
        self.assertEqual(self.widget(QLabel, "rpm_value").text(), "9000")
        self.assertEqual(self.widget(QLabel, "rpm_value").property("tone"), "fault")
        warning = self.widget(QLabel, "rpm_alert")
        self.assertIn("CRÍTICO", warning.text())
        self.widget(QDoubleSpinBox, "rpm_spin").setValue(4400)
        QTest.qWait(45)
        self.assertIn("CRÍTICO", warning.text())
        self.widget(QDoubleSpinBox, "rpm_spin").setValue(4300)
        QTest.qWait(45)
        self.assertIn("ALTA", warning.text())
        self.assertEqual(self.widget(QLabel, "rpm_value").property("tone"), "warning")
        self.widget(QCheckBox, "fault_no_rpm").setChecked(True)
        QTest.qWait(45)
        self.assertNotIn("ALTA", warning.text())
        self.assertNotIn("CRÍTICO", warning.text())

    def test_automatic_temperature_is_visible_and_manual_remains_exact(self):
        automatic = self.widget(QCheckBox, "thermal_auto_switch")
        self.assertTrue(automatic.isChecked())
        self.assertTrue(self.window.controls.thermal_auto)
        self.assertFalse(self.widget(QDoubleSpinBox, "temperature_spin").isEnabled())
        self.widget(QCheckBox, "manual_switch").setChecked(True)
        self.assertTrue(self.widget(QDoubleSpinBox, "temperature_spin").isEnabled())
        self.widget(QDoubleSpinBox, "temperature_spin").setValue(200)
        QTest.qWait(45)
        self.assertEqual(self.link.sample.block_c, 200)
        automatic.setChecked(False)
        self.assertFalse(self.window.controls.thermal_auto)

    def test_cold_preset_enables_dynamic_warmup_and_clears_previous_faults(self):
        self.widget(QCheckBox, "fault_no_rpm").setChecked(True)
        self.widget(QCheckBox, "manual_switch").setChecked(True)
        presets = self.widget(QComboBox, "preset_combo")
        presets.setCurrentIndex(presets.findData("cold"))
        QTest.mouseClick(self.widget(QPushButton, "preset_button"), Qt.MouseButton.LeftButton)
        QTest.qWait(45)
        self.assertFalse(self.window.controls.manual)
        self.assertTrue(self.window.controls.engine_on)
        self.assertTrue(self.window.controls.thermal_auto)
        self.assertEqual(self.window.controls.faults, 0)
        self.assertGreater(self.link.sample.block_c, 25)
        self.assertLess(self.link.sample.block_c, 26)

    def test_hot_preset_is_explicit_and_controls_fit_the_minimum_window(self):
        from ngk_simulator.model import Fault
        presets = self.widget(QComboBox, "preset_combo")
        presets.setCurrentIndex(presets.findData("hot"))
        QTest.mouseClick(self.widget(QPushButton, "preset_button"), Qt.MouseButton.LeftButton)
        self.assertGreaterEqual(self.window.controls.block_c, 150)
        self.assertTrue(self.window.controls.faults & Fault.OVERHEAT)
        self.window.resize(980, 680)
        self.app.processEvents()
        self.assertEqual(self.window.size().width(), 980)
        self.assertEqual(self.window.size().height(), 680)
        for name in ("rpm_value", "speed_value"):
            value = self.widget(QLabel, name)
            self.assertGreaterEqual(value.height(), value.fontMetrics().height(), name)
        central = self.window.centralWidget()
        for control in self.window.findChildren(QPushButton) + self.window.findChildren(QSlider):
            if control.isVisible():
                top_left = control.mapTo(central, control.rect().topLeft())
                bottom_right = control.mapTo(central, control.rect().bottomRight())
                self.assertTrue(central.rect().contains(top_left), control.objectName())
                self.assertTrue(central.rect().contains(bottom_right), control.objectName())


if __name__ == "__main__":
    unittest.main()
