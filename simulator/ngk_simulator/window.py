"""Native Qt controls and live readouts for the illustrative local NGK model."""
from __future__ import annotations

from collections import deque
from pathlib import Path
import sys
import time

from PySide6.QtCore import QPointF, QRectF, QSignalBlocker, Qt, QTimer
from PySide6.QtGui import QColor, QCloseEvent, QIcon, QPainter, QPainterPath, QPalette, QPen
from PySide6.QtWidgets import (
    QApplication, QCheckBox, QComboBox, QDoubleSpinBox, QFrame, QGridLayout,
    QHBoxLayout, QLabel, QMainWindow, QProgressBar, QPushButton, QSizePolicy,
    QSlider, QStackedWidget, QVBoxLayout, QWidget,
)

from .model import Controls, Fault, VehicleModel, set_fault
from .alerts import RpmWarning, rpm_warning
from .scenario import DriveCycle


def available_ports() -> list[str]:
    from .link import available_ports as enumerate_ports
    return enumerate_ports()


STYLE = """
QMainWindow, QWidget#workspace { background: #0b111b; color: #edf4fc; }
QWidget { color: #edf4fc; font-family: 'DejaVu Sans'; font-size: 12px; }
QFrame[role='card'] { background: #141e2b; border: 1px solid #28364a; border-radius: 12px; }
QLabel { background: transparent; border: 0; }
QLabel[role='title'] { font-size: 22px; font-weight: 700; color: #ffffff; }
QLabel[role='section'] { font-size: 14px; font-weight: 700; color: #f0f6ff; }
QLabel[role='muted'] { color: #91a4bc; font-size: 11px; }
QLabel[role='number'] { font-size: 52px; font-weight: 700; color: #f7fbff; }
QLabel[role='small_number'] { font-size: 25px; font-weight: 700; color: #eaf5ff; }
QLabel[role='tag'] { background: #103b46; color: #6ce9ff; border: 1px solid #246276;
                    border-radius: 7px; padding: 7px 10px; font-size: 10px; font-weight: 700; }
QLabel[tone='fault'] { color: #ff8b83; }
QLabel[tone='warning'] { color: #ffc857; }
QLabel[tone='good'] { color: #74e3b0; }
QPushButton { background: #203045; border: 1px solid #354a63; border-radius: 7px;
              min-height: 30px; padding: 0 12px; font-weight: 600; }
QPushButton:hover { background: #2a4058; border-color: #58cde7; }
QPushButton:pressed { background: #16495a; }
QPushButton:disabled { background: #182331; color: #5d728b; border-color: #233144; }
QPushButton#connect_button, QPushButton#preset_button { background: #51d8ef; color: #09212b; border-color: #51d8ef; }
QPushButton#connect_button:disabled { background: #1b3744; color: #648a9a; border-color: #294c5b; }
QComboBox, QDoubleSpinBox { background: #0d1724; border: 1px solid #35465e; border-radius: 6px;
                           padding: 5px 8px; min-height: 23px; selection-background-color: #21556b; }
QComboBox:disabled { color: #6c8097; }
QComboBox QAbstractItemView { background: #142333; color: #edf4fc; selection-background-color: #24536b; }
QCheckBox { spacing: 7px; padding: 3px 0; }
QCheckBox::indicator { width: 16px; height: 16px; background: #0d1724; border: 1px solid #536b85; border-radius: 4px; }
QCheckBox::indicator:checked { background: #4bd5ed; border: 1px solid #85efff; }
QSlider::groove:horizontal { height: 5px; background: #293b51; border-radius: 2px; }
QSlider::sub-page:horizontal { background: #47cde9; border-radius: 2px; }
QSlider::handle:horizontal { width: 15px; height: 15px; margin: -6px 0; background: #b5f3ff;
                            border: 2px solid #4acfe9; border-radius: 8px; }
QProgressBar { border: 0; background: #26354a; border-radius: 3px; max-height: 6px; min-height: 6px; }
QProgressBar::chunk { background: #4dd7ee; border-radius: 3px; }
QProgressBar[tone='warning']::chunk { background: #ffc857; }
QProgressBar[tone='fault']::chunk { background: #ff736a; }
"""


def label(text: str, role: str = "", name: str = "") -> QLabel:
    widget = QLabel(text)
    widget.setTextFormat(Qt.TextFormat.PlainText)
    if role:
        widget.setProperty("role", role)
    if name:
        widget.setObjectName(name)
    return widget


def card(name: str) -> tuple[QFrame, QVBoxLayout]:
    frame = QFrame()
    frame.setObjectName(name)
    frame.setProperty("role", "card")
    layout = QVBoxLayout(frame)
    layout.setContentsMargins(14, 12, 14, 12)
    layout.setSpacing(8)
    return frame, layout


class HistoryStrip(QWidget):
    """A bounded ten-second RPM preview; no sample persistence or external chart stack."""
    def __init__(self):
        super().__init__()
        self.setObjectName("rpm_history")
        self.setMinimumHeight(40)
        self.setMaximumHeight(72)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Preferred)
        self._points: deque[tuple[float, float | None]] = deque(maxlen=400)

    def append(self, timestamp: float, rpm: float | None) -> None:
        self._points.append((timestamp, rpm))
        while self._points and timestamp - self._points[0][0] > 10:
            self._points.popleft()
        self.update()

    def paintEvent(self, event) -> None:
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        bounds = QRectF(0, 15, self.width(), max(1, self.height() - 17))
        painter.setPen(QColor("#8297af"))
        painter.drawText(QRectF(0, 0, self.width(), 14), "RPM  /  ÚLTIMOS 10 s EM MEMÓRIA")
        painter.setPen(QPen(QColor("#27384b"), 1))
        for fraction in (0.0, 0.5, 1.0):
            y = bounds.bottom() - fraction * bounds.height()
            painter.drawLine(QPointF(0, y), QPointF(self.width(), y))
        if not self._points:
            return
        path = QPainterPath()
        now = self._points[-1][0]
        started = False
        for timestamp, rpm in self._points:
            if rpm is None:
                started = False
                continue
            point = QPointF(bounds.right() - (now - timestamp) * bounds.width() / 10,
                            bounds.bottom() - min(12000, max(0, rpm)) * bounds.height() / 12000)
            if started:
                path.lineTo(point)
            else:
                path.moveTo(point)
                started = True
        painter.setPen(QPen(QColor("#57dcef"), 2))
        painter.drawPath(path)


class SimulatorWindow(QMainWindow):
    def __init__(self, link=None):
        super().__init__()
        if link is None:
            from .link import SerialLink
            link = SerialLink()
        self._link = link
        self.controls = Controls(thermal_auto=True)
        self.model = VehicleModel()
        self._cycle = DriveCycle()
        self._cycle_frame = None
        self._rpm_warning = RpmWarning.NORMAL
        self._closed = False
        self._last_step = time.monotonic()
        self._fault_boxes: dict[Fault, QCheckBox] = {}
        self.setWindowTitle("NGK ECU · Simulador de bancada")
        self.resize(1120, 760)
        self.setMinimumSize(980, 680)
        palette = self.palette()
        for role, color in ((QPalette.ColorRole.Window, "#0b111b"), (QPalette.ColorRole.Base, "#0d1724"),
                            (QPalette.ColorRole.Text, "#edf4fc"), (QPalette.ColorRole.WindowText, "#edf4fc"),
                            (QPalette.ColorRole.Button, "#203045"), (QPalette.ColorRole.ButtonText, "#edf4fc"),
                            (QPalette.ColorRole.Highlight, "#24536b"), (QPalette.ColorRole.HighlightedText, "#ffffff")):
            palette.setColor(role, QColor(color))
        self.setPalette(palette)
        self.setStyleSheet(STYLE)
        self._build()
        self._sync_controls()
        self._refresh_ports()
        self._sync_status()
        self._timer = QTimer(self)
        self._timer.setInterval(33)
        self._timer.timeout.connect(self._advance)
        self._timer.start()
        self._advance()

    def _build(self) -> None:
        workspace = QWidget()
        workspace.setObjectName("workspace")
        self.setCentralWidget(workspace)
        root = QVBoxLayout(workspace)
        root.setContentsMargins(18, 16, 18, 14)
        root.setSpacing(12)
        header = QHBoxLayout()
        identity = QVBoxLayout()
        identity.setSpacing(2)
        identity.addWidget(label("NGK / SIMULADOR", "title"))
        identity.addWidget(label("FUSCA 1300 · DEMONSTRAÇÃO · LIMITES ILUSTRATIVOS", "muted"))
        header.addLayout(identity)
        header.addStretch()
        header.addWidget(label("DADOS SINTÉTICOS", "tag"))
        root.addLayout(header)
        body = QHBoxLayout()
        body.setSpacing(14)
        left = QVBoxLayout()
        left.setSpacing(12)
        left.addWidget(self._readouts(), 5)
        left.addWidget(self._controls(), 4)
        right = QVBoxLayout()
        right.setSpacing(12)
        right.addWidget(self._faults(), 4)
        right.addWidget(self._presets(), 2)
        right.addWidget(self._transport_pause(), 2)
        body.addLayout(left, 3)
        body.addLayout(right, 2)
        root.addLayout(body, 1)
        root.addWidget(self._connection())
        footer = QHBoxLayout()
        footer.addWidget(label("SIMULAÇÃO LOCAL · SEM ATUAÇÃO FÍSICA", "muted"))
        footer.addStretch()
        footer.addWidget(label("Sem gravação de amostras", "muted"))
        root.addLayout(footer)

    def _readouts(self) -> QWidget:
        frame, layout = card("readout_panel")
        heading = QHBoxLayout()
        heading.addWidget(label("Instrumentos", "section"))
        heading.addStretch()
        self.mode_label = label("CONDUÇÃO", "muted", "model_mode")
        heading.addWidget(self.mode_label)
        layout.addLayout(heading)
        self.rpm_bar = QProgressBar()
        self.rpm_bar.setObjectName("rpm_bar")
        self.rpm_bar.setRange(0, 6000)
        self.rpm_bar.setToolTip("Escala 0–6000 RPM; o valor numérico aceita até 12000 no modo manual.")
        self.rpm_bar.setTextVisible(False)
        layout.addWidget(self.rpm_bar)
        gauges = QHBoxLayout()
        for title, attribute, unit in (("ROTAÇÃO", "rpm", "RPM"), ("VELOCIDADE", "speed", "km/h")):
            column = QVBoxLayout()
            column.setSpacing(0)
            column.addWidget(label(title, "muted"))
            number = label("0", "number", f"{attribute}_value")
            column.addWidget(number)
            column.addWidget(label(unit, "muted"))
            setattr(self, f"{attribute}_value", number)
            gauges.addLayout(column, 1)
        layout.addLayout(gauges)
        row = QHBoxLayout()
        for title, name, initial in (("MARCHA", "gear_value", "N"), ("ACELERADOR", "tps_value", "0 %"),
                                     ("BLOCO", "temperature_value", "85 °C")):
            column = QVBoxLayout()
            column.setSpacing(1)
            column.addWidget(label(title, "muted"))
            value = label(initial, "small_number", name)
            column.addWidget(value)
            setattr(self, name, value)
            row.addLayout(column, 1)
        layout.addLayout(row)
        self.oil_value = label("AVISO DE ÓLEO · NORMAL", "muted", "oil_value")
        alerts = QHBoxLayout()
        alerts.addWidget(self.oil_value)
        alerts.addStretch()
        self.rpm_alert = label("RPM · REF. 4000 / 4500", "muted", "rpm_alert")
        self.rpm_alert.setToolTip("Referências visuais de bancada, não um limite seguro certificado do motor.")
        alerts.addWidget(self.rpm_alert)
        layout.addLayout(alerts)
        self.history = HistoryStrip()
        layout.addWidget(self.history)
        return frame

    def _percent_row(self, name: str, caption: str, field: str) -> QWidget:
        row = QWidget()
        layout = QHBoxLayout(row)
        layout.setContentsMargins(0, 0, 0, 0)
        title = label(caption)
        title.setMinimumWidth(80)
        slider = QSlider(Qt.Orientation.Horizontal)
        slider.setObjectName(f"{name}_slider")
        slider.setRange(0, 100)
        value = label("0 %")
        value.setMinimumWidth(42)
        value.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        def changed(number):
            setattr(self.controls, field, float(number))
            value.setText(f"{number} %")
        slider.valueChanged.connect(changed)
        setattr(self, f"{name}_slider", slider)
        layout.addWidget(title)
        layout.addWidget(slider, 1)
        layout.addWidget(value)
        return row

    def _numeric_row(self, name: str, caption: str, field: str, low: float, high: float,
                     decimals: int, step: float, suffix: str) -> QWidget:
        row = QWidget()
        layout = QHBoxLayout(row)
        layout.setContentsMargins(0, 0, 0, 0)
        title = label(caption)
        title.setMinimumWidth(80)
        slider = QSlider(Qt.Orientation.Horizontal)
        slider.setObjectName(f"{name}_slider")
        factor = 10 if decimals else 1
        slider.setRange(round(low * factor), round(high * factor))
        slider.setSingleStep(max(1, round(step * factor)))
        spin = QDoubleSpinBox()
        spin.setObjectName(f"{name}_spin")
        spin.setRange(low, high)
        spin.setDecimals(decimals)
        spin.setSingleStep(step)
        spin.setSuffix(suffix)
        spin.setMinimumWidth(110)
        slider.valueChanged.connect(lambda number: spin.setValue(number / factor))
        def changed(number):
            setattr(self.controls, field, float(number))
            blocker = QSignalBlocker(slider)
            slider.setValue(round(number * factor))
            del blocker
        spin.valueChanged.connect(changed)
        setattr(self, f"{name}_slider", slider)
        setattr(self, f"{name}_spin", spin)
        layout.addWidget(title)
        layout.addWidget(slider, 1)
        layout.addWidget(spin)
        return row

    def _controls(self) -> QWidget:
        frame, layout = card("control_panel")
        layout.setContentsMargins(14, 10, 14, 10)
        layout.setSpacing(6)
        heading = QHBoxLayout()
        heading.addWidget(label("Controles", "section"))
        heading.addStretch()
        self.engine_switch = QCheckBox("Motor ligado")
        self.engine_switch.setObjectName("engine_switch")
        self.engine_switch.toggled.connect(lambda checked: setattr(self.controls, "engine_on", checked))
        heading.addWidget(self.engine_switch)
        self.manual_switch = QCheckBox("Modo manual")
        self.manual_switch.setObjectName("manual_switch")
        self.manual_switch.toggled.connect(self._manual_changed)
        heading.addWidget(self.manual_switch)
        layout.addLayout(heading)
        layout.addWidget(self._percent_row("throttle", "Acelerador", "throttle_pct"))
        self.control_pages = QStackedWidget()
        drive = QWidget()
        drive_layout = QVBoxLayout(drive)
        drive_layout.setContentsMargins(0, 0, 0, 0)
        drive_layout.setSpacing(6)
        drive_layout.addWidget(self._percent_row("brake", "Freio", "brake_pct"))
        self.control_pages.addWidget(drive)
        manual = QWidget()
        manual_layout = QVBoxLayout(manual)
        manual_layout.setContentsMargins(0, 0, 0, 0)
        manual_layout.setSpacing(6)
        manual_layout.addWidget(self._numeric_row("rpm", "Rotação", "manual_rpm", 0, 12000, 0, 100, " rpm"))
        manual_layout.addWidget(self._numeric_row("speed", "Velocidade", "manual_speed_kph", 0, 250, 1, 1, " km/h"))
        self.control_pages.addWidget(manual)
        layout.addWidget(self.control_pages)
        row = QHBoxLayout()
        row.addWidget(label("Marcha"))
        self.gear_combo = QComboBox()
        self.gear_combo.setObjectName("gear_combo")
        for text, gear in (("R", -1), ("N", 0), ("1", 1), ("2", 2), ("3", 3), ("4", 4)):
            self.gear_combo.addItem(text, gear)
        self.gear_combo.currentIndexChanged.connect(lambda _: setattr(self.controls, "gear", self.gear_combo.currentData()))
        row.addWidget(self.gear_combo, 1)
        self.clutch_switch = QCheckBox("Embreagem pressionada")
        self.clutch_switch.setObjectName("clutch_switch")
        self.clutch_switch.toggled.connect(lambda checked: setattr(self.controls, "clutch", checked))
        row.addWidget(self.clutch_switch)
        layout.addLayout(row)
        self.thermal_auto_switch = QCheckBox("Temperatura automática · superfície do bloco simulada")
        self.thermal_auto_switch.setObjectName("thermal_auto_switch")
        self.thermal_auto_switch.setToolTip("Modelo térmico ilustrativo. A medição real requer posição e calibração do sensor.")
        self.thermal_auto_switch.toggled.connect(self._thermal_changed)
        layout.addWidget(self.thermal_auto_switch)
        layout.addWidget(self._numeric_row("temperature", "Bloco", "block_c", -40, 200, 1, 1, " °C"))
        self.temperature_spin.setToolTip("Valor inicial no automático; extremo de 200 °C para testes manuais, não referência normal.")
        return frame

    def _faults(self) -> QWidget:
        frame, layout = card("fault_panel")
        layout.setContentsMargins(14, 10, 14, 10)
        layout.addWidget(label("Injetar falhas", "section"))
        description = label("As falhas afetam as leituras simuladas, sem acionar saídas.", "muted")
        description.setWordWrap(True)
        layout.addWidget(description)
        grid = QGridLayout()
        grid.setHorizontalSpacing(10)
        grid.setVerticalSpacing(6)
        choices = (("Sem RPM", Fault.NO_RPM, "no_rpm"), ("Sem velocidade", Fault.NO_SPEED, "no_speed"),
                   ("Bloco aberto", Fault.TEMP_OPEN, "temp_open"), ("Bloco em curto", Fault.TEMP_SHORT, "temp_short"),
                   ("Óleo baixo", Fault.LOW_OIL, "low_oil"), ("Superaquecimento", Fault.OVERHEAT, "overheat"),
                   ("TPS inválido", Fault.TPS_INVALID, "tps_invalid"), ("Marcha inválida", Fault.GEAR_INVALID, "gear_invalid"))
        for index, (caption, flag, name) in enumerate(choices):
            box = QCheckBox(caption)
            box.setObjectName(f"fault_{name}")
            box.toggled.connect(lambda checked, fault=flag: self._fault_changed(fault, checked))
            self._fault_boxes[flag] = box
            grid.addWidget(box, index // 2, index % 2)
        layout.addLayout(grid)
        layout.addStretch()
        self.clear_faults_button = QPushButton("Limpar falhas")
        self.clear_faults_button.setObjectName("clear_faults_button")
        self.clear_faults_button.clicked.connect(self._clear_faults)
        layout.addWidget(self.clear_faults_button)
        return frame

    def _presets(self) -> QWidget:
        frame, layout = card("preset_panel")
        layout.setContentsMargins(14, 8, 14, 8)
        layout.setSpacing(5)
        layout.addWidget(label("Cenários rápidos", "section"))
        self.preset_combo = QComboBox()
        self.preset_combo.setObjectName("preset_combo")
        for caption, key in (("Marcha lenta quente · oscilação", "idle"), ("Partida fria · aquecimento", "cold"),
                             ("Cruzeiro em 3ª", "cruise"), ("Giro alto · teste de alerta", "high_rpm"),
                             ("Motor quente · falha", "hot"), ("Falha de RPM", "rpm_loss")):
            self.preset_combo.addItem(caption, key)
        preset_row = QHBoxLayout()
        self.preset_combo.setSizeAdjustPolicy(QComboBox.SizeAdjustPolicy.AdjustToMinimumContentsLengthWithIcon)
        self.preset_combo.setMinimumContentsLength(12)
        preset_row.addWidget(self.preset_combo, 1)
        self.preset_button = QPushButton("Aplicar")
        self.preset_button.setObjectName("preset_button")
        self.preset_button.clicked.connect(self._apply_preset)
        preset_row.addWidget(self.preset_button)
        layout.addLayout(preset_row)
        row = QHBoxLayout()
        row.setSpacing(6)
        self.cycle_start_button = QPushButton("Iniciar ciclo")
        self.cycle_start_button.setObjectName("cycle_start_button")
        self.cycle_start_button.clicked.connect(self._start_cycle)
        row.addWidget(self.cycle_start_button)
        self.cycle_stop_button = QPushButton("Parar")
        self.cycle_stop_button.setObjectName("cycle_stop_button")
        self.cycle_stop_button.setEnabled(False)
        self.cycle_stop_button.clicked.connect(self._stop_cycle)
        row.addWidget(self.cycle_stop_button)
        self.cycle_repeat_switch = QCheckBox("Repetir")
        self.cycle_repeat_switch.setObjectName("cycle_repeat_switch")
        self.cycle_repeat_switch.setChecked(True)
        row.addWidget(self.cycle_repeat_switch)
        layout.addLayout(row)
        self.cycle_status_label = label("Ciclo parado · 60 s por volta", "muted", "cycle_status_label")
        layout.addWidget(self.cycle_status_label)
        return frame

    def _transport_pause(self) -> QWidget:
        frame, layout = card("transport_panel")
        layout.addWidget(label("Teste de continuidade", "section"))
        self.pause_usb_switch = QCheckBox("Simular perda USB")
        self.pause_usb_switch.setObjectName("pause_usb_switch")
        layout.addWidget(self.pause_usb_switch)
        description = label("Pausa os frames enviados. O painel local continua; o ESP deve indicar dados vencidos.", "muted")
        description.setWordWrap(True)
        layout.addWidget(description)
        return frame

    def _connection(self) -> QWidget:
        frame, layout = card("connection_panel")
        layout.setSpacing(4)
        row = QHBoxLayout()
        row.addWidget(label("USB / ESP32", "section"))
        self.port_combo = QComboBox()
        self.port_combo.setObjectName("port_combo")
        self.port_combo.setEditable(True)
        self.port_combo.setMinimumWidth(170)
        self.port_combo.lineEdit().setPlaceholderText("Escolha a porta USB")
        self.port_combo.currentTextChanged.connect(lambda _: self._sync_status())
        row.addWidget(self.port_combo, 1)
        self.refresh_ports_button = QPushButton("Atualizar")
        self.refresh_ports_button.setObjectName("refresh_ports_button")
        self.refresh_ports_button.clicked.connect(self._refresh_ports)
        row.addWidget(self.refresh_ports_button)
        self.connect_button = QPushButton("Conectar")
        self.connect_button.setObjectName("connect_button")
        self.connect_button.clicked.connect(self._connect)
        row.addWidget(self.connect_button)
        self.disconnect_button = QPushButton("Desconectar")
        self.disconnect_button.setObjectName("disconnect_button")
        self.disconnect_button.clicked.connect(self._disconnect)
        row.addWidget(self.disconnect_button)
        self.connection_status = label("OFFLINE", "tag", "connection_status")
        row.addWidget(self.connection_status)
        layout.addLayout(row)
        self.connection_detail = label("Somente neste computador. Conecte quando quiser transmitir.", "muted", "connection_detail")
        self.connection_detail.setWordWrap(True)
        layout.addWidget(self.connection_detail)
        return frame

    def _set_cycle_controls(self, running: bool) -> None:
        # Connection/transport pause remain accessible independently of the cycle.
        for name in ("control_panel", "fault_panel"):
            self.findChild(QFrame, name).setEnabled(not running)
        self.preset_combo.setEnabled(not running)
        self.preset_button.setEnabled(not running)
        self.cycle_start_button.setEnabled(not running)
        self.cycle_stop_button.setEnabled(running)
        self.cycle_repeat_switch.setEnabled(not running)
        if not running:
            self._sync_thermal_controls()

    def _start_cycle(self) -> None:
        if self._cycle.running or self._closed:
            return
        now = time.monotonic()
        self._cycle_frame = self._cycle.start(now, self.controls, repeat=self.cycle_repeat_switch.isChecked())
        self.controls = self._cycle_frame.controls
        self.model.reset()
        self._rpm_warning = RpmWarning.NORMAL
        self._last_step = now
        self._sync_controls()
        self._set_cycle_controls(True)
        self._update_cycle_label()

    def _stop_cycle(self) -> None:
        restored = self._cycle.stop()
        self._cycle_frame = None
        if restored is not None:
            self.controls = restored
            self._sync_controls()
        self._set_cycle_controls(False)
        self.cycle_status_label.setText("Ciclo parado · controles restaurados")

    def _update_cycle_label(self) -> None:
        if self._cycle_frame is None:
            return
        frame = self._cycle_frame
        text = f"Volta {frame.cycle_number} · {frame.phase_name}"
        if self.cycle_status_label.text() != text:
            self.cycle_status_label.setText(text)

    def _manual_changed(self, checked: bool) -> None:
        self.controls.manual = checked
        self.control_pages.setCurrentIndex(1 if checked else 0)
        self.mode_label.setText("MANUAL" if checked else "CONDUÇÃO ILUSTRATIVA")
        self._sync_thermal_controls()

    def _thermal_changed(self, checked: bool) -> None:
        self.controls.thermal_auto = checked
        self._sync_thermal_controls()

    def _sync_thermal_controls(self) -> None:
        editable = self.controls.manual or not self.controls.thermal_auto
        self.temperature_spin.setEnabled(editable)
        self.temperature_slider.setEnabled(editable)

    def _fault_changed(self, flag: Fault, checked: bool) -> None:
        set_fault(self.controls, flag, checked)
        self._sync_faults()

    def _sync_faults(self) -> None:
        for flag, box in self._fault_boxes.items():
            blocker = QSignalBlocker(box)
            box.setChecked(bool(self.controls.faults & flag))
            del blocker

    def _clear_faults(self) -> None:
        self.controls.faults = Fault.NONE
        self._sync_faults()

    def _sync_controls(self) -> None:
        pairs = ((self.engine_switch, self.controls.engine_on), (self.clutch_switch, self.controls.clutch),
                 (self.manual_switch, self.controls.manual),
                 (self.thermal_auto_switch, self.controls.thermal_auto))
        blockers = [QSignalBlocker(widget) for widget, _ in pairs]
        for widget, value in pairs:
            widget.setChecked(value)
        del blockers
        self.throttle_slider.setValue(round(self.controls.throttle_pct))
        self.brake_slider.setValue(round(self.controls.brake_pct))
        self.gear_combo.setCurrentIndex(self.gear_combo.findData(self.controls.gear))
        self.rpm_spin.setValue(self.controls.manual_rpm)
        self.speed_spin.setValue(self.controls.manual_speed_kph)
        self.temperature_spin.setValue(self.controls.block_c)
        self._manual_changed(self.controls.manual)
        self._sync_faults()

    def _apply_preset(self) -> None:
        key = self.preset_combo.currentData()
        self.controls.engine_on = True
        self.controls.clutch = False
        self.controls.brake_pct = 0
        self.controls.faults = Fault.NONE
        self.controls.manual = key not in ("idle", "cold")
        self.controls.thermal_auto = True
        scenarios = {"idle": (700, 0, 0, 0, 85), "cold": (1000, 0, 0, 0, 25),
                     "cruise": (3200, 60, 3, 35, 95), "high_rpm": (4700, 55, 2, 90, 100),
                     "hot": (2800, 40, 2, 20, 155), "rpm_loss": (3000, 45, 3, 25, 90)}
        rpm, speed, gear, throttle, temperature = scenarios[key]
        self.controls.manual_rpm, self.controls.manual_speed_kph = rpm, speed
        self.controls.gear, self.controls.throttle_pct, self.controls.block_c = gear, throttle, temperature
        if key == "hot":
            self.controls.faults = Fault.OVERHEAT
        elif key == "rpm_loss":
            self.controls.faults = Fault.NO_RPM
        self.model.reset()
        self._rpm_warning = RpmWarning.NORMAL
        self._sync_controls()

    def _refresh_ports(self) -> None:
        selected = self.port_combo.currentText()
        blocker = QSignalBlocker(self.port_combo)
        self.port_combo.clear()
        self.port_combo.addItems(available_ports())
        if selected:
            self.port_combo.setCurrentText(selected)
        del blocker
        self._sync_status()

    def _connect(self) -> None:
        port = self.port_combo.currentText().strip()
        if port:
            self._link.connect(port)
        self._sync_status()

    def _disconnect(self) -> None:
        self._link.disconnect()
        self._sync_status()

    def _sync_status(self) -> None:
        if not hasattr(self, "connection_detail"):
            return
        status = self._link.snapshot()
        active = status.state in {"connecting", "streaming", "paused", "source_stale"}
        self.connect_button.setEnabled(bool(self.port_combo.currentText().strip()) and not active)
        self.disconnect_button.setEnabled(active)
        self.port_combo.setEnabled(not active)
        self.refresh_ports_button.setEnabled(not active)
        names = {"offline": "OFFLINE", "connecting": "CONECTANDO", "streaming": "USB AO VIVO",
                 "paused": "USB PAUSADO", "source_stale": "ORIGEM SEM DADOS", "error": "ERRO DE LINK"}
        self.connection_status.setText(names.get(status.state, "OFFLINE"))
        self.connection_detail.setText(status.detail)

    @staticmethod
    def _tone(widget: QWidget, fault: bool | str) -> None:
        tone = fault if isinstance(fault, str) else ("fault" if fault else "")
        if widget.property("tone") != tone:
            widget.setProperty("tone", tone)
            widget.style().unpolish(widget)
            widget.style().polish(widget)

    def _advance(self) -> None:
        if self._closed:
            return
        now = time.monotonic()
        delta = max(0.0, min(0.1, now - self._last_step))
        self._last_step = now
        if self._cycle.running:
            frame = self._cycle.update(now)
            if frame is None:
                self._stop_cycle()
            else:
                changed = self._cycle_frame is None or (frame.index, frame.cycle_number) != (
                    self._cycle_frame.index, self._cycle_frame.cycle_number)
                new_cycle = self._cycle_frame is not None and frame.cycle_number != self._cycle_frame.cycle_number
                if new_cycle:
                    self.model.reset()
                    self._rpm_warning = RpmWarning.NORMAL
                self._cycle_frame = frame
                self.controls = frame.controls
                if changed:
                    self._sync_controls()
                    self._update_cycle_label()
        sample = self.model.step(self.controls, delta)
        self._link.set_sample(sample, paused=self.pause_usb_switch.isChecked())
        faults = sample.faults
        self.rpm_value.setText("--" if faults & Fault.NO_RPM else f"{sample.rpm:.0f}")
        self.speed_value.setText("--" if faults & Fault.NO_SPEED else f"{sample.speed_kph:.1f}")
        self.tps_value.setText("--" if faults & Fault.TPS_INVALID else f"{sample.throttle_pct:.0f} %")
        self.gear_value.setText("--" if faults & Fault.GEAR_INVALID else {-1: "R", 0: "N"}.get(sample.gear, str(sample.gear)))
        invalid_temperature = bool(faults & (Fault.TEMP_OPEN | Fault.TEMP_SHORT))
        self.temperature_value.setText("--" if invalid_temperature else f"{sample.block_c:.1f} °C")
        self.oil_value.setText({0: "AVISO DE ÓLEO · NORMAL", 1: "AVISO DE ÓLEO · BAIXO", 2: "ÓLEO · SEM LEITURA"}[sample.oil])
        self._rpm_warning = rpm_warning(sample.rpm, not bool(faults & Fault.NO_RPM), self._rpm_warning)
        rpm_tone = {RpmWarning.NORMAL: "", RpmWarning.HIGH: "warning", RpmWarning.CRITICAL: "fault"}[self._rpm_warning]
        self._tone(self.rpm_value, "fault" if faults & Fault.NO_RPM else rpm_tone)
        self._tone(self.rpm_bar, rpm_tone)
        self._tone(self.rpm_alert, rpm_tone)
        self.rpm_alert.setText({RpmWarning.NORMAL: "RPM · REF. 4000 / 4500", RpmWarning.HIGH: "SIM · ROTAÇÃO ALTA",
                                RpmWarning.CRITICAL: "SIM · GIRO CRÍTICO"}[self._rpm_warning])
        self._tone(self.speed_value, bool(faults & Fault.NO_SPEED))
        self._tone(self.tps_value, bool(faults & Fault.TPS_INVALID))
        self._tone(self.gear_value, bool(faults & Fault.GEAR_INVALID))
        self._tone(self.temperature_value, invalid_temperature or bool(faults & Fault.OVERHEAT))
        self._tone(self.oil_value, sample.oil != 0)
        self.rpm_bar.setValue(0 if faults & Fault.NO_RPM else min(self.rpm_bar.maximum(), round(sample.rpm)))
        self.history.append(now, None if faults & Fault.NO_RPM else sample.rpm)
        self._sync_status()

    def closeEvent(self, event: QCloseEvent) -> None:
        if not self._closed:
            self._stop_cycle()
            self._closed = True
            self._timer.stop()
            self._link.close()
        super().closeEvent(event)

    def resizeEvent(self, event) -> None:
        super().resizeEvent(event)
        if hasattr(self, "history"):
            # Readouts and controls take priority over the optional plot on small windows.
            self.history.setVisible(self.height() >= 720)


def main() -> int:
    app = QApplication.instance() or QApplication(sys.argv)
    app.setApplicationName("NGK ECU Simulator")
    app.setDesktopFileName("ngk-ecu-simulator")
    app.setWindowIcon(QIcon(str(Path(__file__).resolve().parents[2] / "assets/simulator.svg")))
    app.setStyle("Fusion")
    window = SimulatorWindow()
    window.show()
    return app.exec()
