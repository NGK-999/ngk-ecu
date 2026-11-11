"""Explicit USB connection. Only this worker opens/reads/writes a serial port."""
from __future__ import annotations

from dataclasses import dataclass, replace
import json
import math
import secrets
import threading
import time
from typing import Callable

import serial
from serial.tools import list_ports

from .model import Sample
from .protocol import encode_frame


@dataclass(frozen=True)
class LinkStatus:
    state: str = 'offline'
    detail: str = 'Simulação local. USB desconectado.'
    port: str = ''
    device_live: bool = False
    sequence: int = 0


class _JsonLines:
    def __init__(self, capacity: int = 2048):
        self.capacity = capacity
        self._bytes = bytearray()
        self._discard = False

    def feed(self, data: bytes) -> list[dict]:
        result = []
        for byte in data:
            if byte == 10:
                if not self._discard:
                    try:
                        value = json.loads(self._bytes.decode('ascii'))
                        if isinstance(value, dict):
                            result.append(value)
                    except (ValueError, UnicodeError):
                        pass
                self._bytes.clear()
                self._discard = False
            elif not self._discard:
                if len(self._bytes) >= self.capacity:
                    self._bytes.clear()
                    self._discard = True
                else:
                    self._bytes.append(byte)
        return result


class _LinkFailure(Exception):
    pass


def available_ports() -> list[str]:
    """Enumeration only: it does not open or probe devices."""
    devices = [port.device for port in list_ports.comports()]
    return sorted(devices, key=lambda port: (not port.rsplit("/", 1)[-1].startswith(("ttyUSB", "ttyACM")), port))


class SerialLink:
    def __init__(self, serial_factory: Callable | None = None, *,
                 handshake_timeout: float = 8.0, reply_timeout: float = 3.0,
                 session_factory: Callable[[], int] | None = None,
                 producer_timeout: float = .25):
        if (isinstance(producer_timeout, bool) or not isinstance(producer_timeout, (int, float))
                or not math.isfinite(producer_timeout) or producer_timeout <= 0):
            raise ValueError('producer_timeout deve ser um número positivo e finito')
        self._factory = serial_factory or serial.Serial
        self._session_factory = session_factory or (lambda: secrets.randbelow(0xFFFFFFFF) + 1)
        self._handshake_timeout = handshake_timeout
        self._reply_timeout = reply_timeout
        self._producer_timeout = producer_timeout
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self._status = LinkStatus()
        self._sample: Sample | None = None
        self._produced_at: float | None = None
        self._paused = False

    def snapshot(self) -> LinkStatus:
        with self._lock:
            return self._status

    def _update(self, **values) -> None:
        with self._lock:
            self._status = replace(self._status, **values)

    def set_sample(self, sample: Sample, paused: bool = False) -> None:
        # Sample is immutable. Only the latest state is retained, never a queue.
        with self._lock:
            self._sample, self._paused = sample, bool(paused)
            # Identical values still represent fresh production, unlike a
            # frozen GUI leaving the worker to retransmit an old snapshot.
            self._produced_at = time.monotonic()

    def connect(self, port: str) -> bool:
        if self._thread and self._thread.is_alive():
            return False
        if not isinstance(port, str) or not port or len(port) > 255:
            self._update(state='error', detail='Selecione uma porta USB válida.')
            return False
        self._stop = threading.Event()
        self._update(state='connecting', detail='Identificando o painel NGK ECU…',
                     port=port, device_live=False, sequence=0)
        self._thread = threading.Thread(target=self._run, args=(port, self._stop),
                                        name='ngk-simulator-usb', daemon=True)
        self._thread.start()
        return True

    def disconnect(self) -> None:
        self._stop.set()
        if not self._thread or not self._thread.is_alive():
            self._update(state='offline', detail='Simulação local. USB desconectado.', device_live=False)

    def close(self, timeout: float = 1.0) -> None:
        self.disconnect()
        if self._thread and self._thread is not threading.current_thread():
            self._thread.join(timeout=max(0.0, min(timeout, 1.0)))

    def _run(self, port: str, stop: threading.Event) -> None:
        device, session = None, 0
        failed = False
        try:
            device = self._factory()
            device.port, device.baudrate = port, 115200
            device.timeout, device.write_timeout = .02, .2
            device.dtr, device.rts = False, False
            device.exclusive = True
            device.open()
            device.reset_input_buffer()
            lines = _JsonLines()
            phase = 'identify'
            sequence = 0
            deadline = time.monotonic() + self._handshake_timeout
            next_request = next_frame = next_status = 0.0
            last_reply = time.monotonic()

            def send(data: bytes) -> None:
                if device.write(data) != len(data):
                    raise _LinkFailure('Envio USB incompleto. Reconecte o painel.')

            while not stop.is_set():
                now = time.monotonic()
                if phase in ('identify', 'start') and now >= deadline:
                    raise _LinkFailure('O painel não respondeu. Confira USB, porta e firmware.')
                if phase == 'identify' and now >= next_request:
                    send(b'info\n')
                    next_request = now + .5
                if phase == 'streaming':
                    if now - last_reply > self._reply_timeout:
                        raise _LinkFailure('O painel parou de responder. Clique Conectar para retomar.')
                    with self._lock:
                        sample, paused, produced_at = self._sample, self._paused, self._produced_at
                    if now >= next_frame:
                        next_frame += .05
                        if next_frame < now:
                            next_frame = now + .05
                        fresh = (sample is not None and produced_at is not None
                                 and now - produced_at < self._producer_timeout)
                        if not paused and fresh:
                            if sequence >= 0xFFFFFFFF:
                                raise _LinkFailure('Sessão concluída. Conecte novamente para continuar.')
                            sequence += 1
                            send(encode_frame(session, sequence, sample))
                        self._update(state='paused' if paused else 'streaming' if fresh else 'source_stale',
                                     sequence=sequence,
                                     detail='Transmissão pausada: testando perda de comunicação.' if paused
                                     else 'Enviando simulação ao painel por USB.' if fresh
                                     else 'Origem sem dados novos. Envio suspenso até a simulação retomar.')
                    if now >= next_status:
                        send(b'sim-status\n')
                        next_status = now + .5
                for value in lines.feed(device.read(512)):
                    if phase == 'identify' and 'project' in value:
                        if value.get('project') != 'NGK ECU':
                            raise _LinkFailure('A porta selecionada não identificou um painel NGK ECU.')
                        if type(value.get('simulation_protocol')) is not int or value['simulation_protocol'] != 1:
                            raise _LinkFailure('Atualize o firmware NGK ECU para usar o simulador USB.')
                        session = self._session_factory()
                        if type(session) is not int or not 1 <= session <= 0xFFFFFFFF:
                            raise _LinkFailure('Identificador de sessão inválido.')
                        # Know the token before starting, so cancellation can
                        # send stop even if the ready response has not arrived.
                        send(f'sim-start {session}\n'.encode('ascii'))
                        phase, deadline = 'start', now + self._handshake_timeout
                    elif value.get('kind') == 'sim':
                        if phase == 'start':
                            token = value.get('session')
                            if value.get('state') != 'ready' or type(value.get('protocol')) is not int or value['protocol'] != 1:
                                raise _LinkFailure('O painel está ocupado. Feche calibração ou teste de fluidez.')
                            if type(token) is not int or token != session:
                                raise _LinkFailure('Resposta de sessão USB inválida.')
                            session, phase = token, 'streaming'
                            last_reply = next_frame = time.monotonic()
                            next_status = 0.0
                        elif phase == 'streaming':
                            if value.get('state') in ('off', 'rejected', 'busy'):
                                raise _LinkFailure('A sessão foi encerrada na placa. Conecte novamente para retomar.')
                            if value.get('state') in ('live', 'stale') and value.get('protocol') == 1:
                                last_reply = time.monotonic()
                                self._update(device_live=value.get('live') is True)
        except _LinkFailure as error:
            failed = True
            self._update(state='error', detail=str(error), device_live=False)
        except (OSError, serial.SerialException) as error:
            failed = True
            denied = getattr(error, 'errno', None) == 13 or 'permission denied' in str(error).lower()
            self._update(state='error', device_live=False,
                         detail='Sem acesso à porta USB. Confira a liberação de acesso no Linux.' if denied
                         else 'Conexão USB interrompida. Confira o cabo e conecte novamente.')
        except (ValueError, TypeError):
            failed = True
            self._update(state='error', detail='Dados de simulação inválidos. Revise os controles.', device_live=False)
        finally:
            if device is not None:
                if session:
                    try:
                        device.write(f'sim-stop {session}\n'.encode('ascii'))
                    except (OSError, serial.SerialException):
                        pass
                try:
                    device.close()
                except (OSError, serial.SerialException):
                    pass
            if stop.is_set() or not failed:
                self._update(state='offline', detail='Simulação local. USB desconectado.', device_live=False)
