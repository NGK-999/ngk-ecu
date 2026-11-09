#!/usr/bin/env python3
"""Receive an explicit ESP32 LCD transfer capture; persist only a verified PNG."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import struct
import sys
import time
import zlib

HEADER = b"NGK_SCREEN RGB565_BE 320 240 153600\n"
PIXEL_BYTES = 320 * 240 * 2


def png_from_rgb565(pixels: bytes, width: int, height: int) -> bytes:
    if width <= 0 or height <= 0 or len(pixels) != width * height * 2:
        raise ValueError("Dimensoes ou pixels RGB565 invalidos")
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            i = (y * width + x) * 2
            color = (pixels[i] << 8) | pixels[i + 1]
            rows.extend((((color >> 11) & 31) * 255 // 31,
                         ((color >> 5) & 63) * 255 // 63,
                         (color & 31) * 255 // 31))
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def receive_capture(stream, timeout: float = 35) -> tuple[bytes, int]:
    """Read fixed geometry and exact binary length; LF inside pixels is data."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = stream.readline(1024)
        if line == HEADER:
            break
        if (line.startswith(b"NGK_SCREEN") or b"screen_capture_blocked" in line
                or b"screen_capture_tx_busy" in line):
            raise ValueError("Captura recusada ou formato desconhecido")
    else:
        raise TimeoutError("ESP32 nao iniciou a captura")
    pixels = bytearray()
    while len(pixels) < PIXEL_BYTES and time.monotonic() < deadline:
        part = stream.read(min(4096, PIXEL_BYTES - len(pixels)))
        if not part:
            time.sleep(.001)
            continue
        pixels.extend(part)
    if len(pixels) != PIXEL_BYTES:
        raise TimeoutError("Captura incompleta; nenhum PNG foi salvo")
    if stream.read(1) != b"\n":
        raise ValueError("Termino da captura invalido")
    trailer = stream.readline(128)
    match = re.fullmatch(rb"NGK_SCREEN_END 153600 ([0-9a-fA-F]{8})\r?\n", trailer)
    checksum = zlib.crc32(pixels)
    if not match or int(match.group(1), 16) != checksum:
        raise ValueError("CRC32 da captura invalido; nenhum PNG foi salvo")
    return png_from_rgb565(pixels, 320, 240), checksum


def identify(stream, timeout: float = 10) -> dict:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        stream.write(b"info\n")
        until = time.monotonic() + .5
        while time.monotonic() < until:
            try:
                value = json.loads(stream.readline(2048))
            except (ValueError, UnicodeError):
                continue
            if isinstance(value, dict) and value.get("project") == "NGK ECU":
                if value.get("build") != "normal" or value.get("sensor_inputs_enabled") is not False or value.get("actuators_enabled") is not False:
                    raise ValueError("Captura exige firmware normal de bancada, sem entradas/atuadores")
                if value.get("screen_capture_protocol") != 1:
                    raise ValueError("Firmware nao oferece captura de tela; atualizar para a versao com protocolo 1")
                return value
    raise TimeoutError("NGK ECU nao identificada")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--output", type=Path, default=Path("artifacts/esp32-screen.png"))
    args = parser.parse_args()
    import serial
    port = serial.Serial()
    port.port = args.port
    port.baudrate = 115200
    port.timeout = .5
    port.write_timeout = 2
    port.exclusive = True
    port.dtr = False
    port.rts = False
    try:
        port.open()
        info = identify(port)
        port.write(b"screen-capture\n")
        png, checksum = receive_capture(port)
    finally:
        port.close()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_name(args.output.name + ".part")
    temporary.write_bytes(png)
    temporary.replace(args.output)
    print(json.dumps({"captured_from": "ESP32_LCD_transfer", "firmware": info["firmware"],
                      "width": 320, "height": 240, "crc32": f"{checksum:08x}",
                      "png": str(args.output)}))


if __name__ == "__main__":
    try:
        main()
    except (OSError, TimeoutError, ValueError) as error:
        print(f"Captura nao concluida: {error}", file=sys.stderr)
        raise SystemExit(1)
