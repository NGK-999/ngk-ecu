#!/usr/bin/env python3
"""Render firmware's actual LVGL 8.4 UI without hardware or extra Python packages."""
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import struct
import subprocess
import sys
import zlib

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
FIRMWARE = ROOT / "firmware/ecu"
SCENES = (
    "menu", "painel-sem-sinal", "painel-sim", "painel-maximos", "painel-re",
    "painel-neutro", "painel-imperial", "painel-alertas", "painel-sem-link",
    "ajustes", "ajustes-tela",
)


def run(command: list[str], *, timeout: int = 120) -> str:
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=timeout)
    if result.returncode:
        raise RuntimeError(f"{Path(command[0]).name} falhou:\n{result.stderr or result.stdout}")
    return result.stdout


def digest_files(files: list[Path], base: Path) -> dict[str, str]:
    return {str(path.relative_to(base)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(files)}


def write_if_changed(path: Path, content: bytes) -> None:
    if not path.exists() or path.read_bytes() != content:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)


def png(width: int, height: int, pixels: bytes) -> bytes:
    if len(pixels) != width * height * 3:
        raise ValueError("Tamanho RGB incompatível com as dimensões")

    def chunk(kind: bytes, content: bytes) -> bytes:
        return (struct.pack(">I", len(content)) + kind + content
                + struct.pack(">I", zlib.crc32(kind + content) & 0xFFFFFFFF))

    rows = b"".join(b"\0" + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def check_lvgl(path: Path) -> None:
    metadata = path / "library.json"
    if not metadata.is_file():
        raise ValueError("LVGL local ausente; prepare as dependências fixadas do firmware ou use --lvgl-dir.")
    version = json.loads(metadata.read_text())["version"]
    if version != "8.4.0":
        raise ValueError(f"LVGL {version} incompatível: a prévia exige exatamente 8.4.0.")
    header = (path / "lvgl.h").read_text()
    for name, expected in (("MAJOR", 8), ("MINOR", 4), ("PATCH", 0)):
        if not re.search(rf"#define\s+LVGL_VERSION_{name}\s+{expected}\b", header):
            raise ValueError("Versão declarada em library.json difere de lvgl.h")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lvgl-dir", type=Path, default=FIRMWARE / ".pio/libdeps/ecu-normal/lvgl")
    parser.add_argument("--output", type=Path, default=ROOT / "local/ui-preview/images")
    parser.add_argument("--jobs", type=int, choices=range(1, 33), default=min(8, os.cpu_count() or 1),
                        metavar="1..32")
    args = parser.parse_args()
    if sys.version_info[:2] != (3, 12):
        parser.error("Use Python 3.12 da .venv local.")
    lvgl, output = args.lvgl_dir.resolve(), args.output.resolve()
    check_lvgl(lvgl)
    for compiler in ("gcc", "g++"):
        if not shutil.which(compiler):
            raise ValueError(f"Compilador host {compiler} ausente; nenhuma instalação automática é feita.")

    assets = sorted((FIRMWARE / "src").glob("ngk_*.c")) + [FIRMWARE / "src/ui_icons.c"]
    production_files = sorted((FIRMWARE / "include").rglob("*.h*")) + assets + [
        FIRMWARE / "src/ui.cpp", FIRMWARE / "src/simulation_service.cpp"]
    firmware_hashes = digest_files(production_files, FIRMWARE)
    support_files = [p for p in HERE.rglob("*") if p.is_file() and p.suffix in {".py", ".h", ".hpp", ".cpp", ".inc"}]
    support_hashes = digest_files(support_files, HERE)
    lvgl_files = sorted((lvgl / "src").rglob("*.c"))
    lvgl_hashes = digest_files(lvgl_files + sorted((lvgl / "src").rglob("*.h"))
                               + [lvgl / "lvgl.h", lvgl / "library.json"], lvgl)
    compilers = {cc: run([cc, "--version"]).splitlines()[0] for cc in ("gcc", "g++")}
    identity = {"firmware": firmware_hashes, "support": support_hashes,
                "lvgl": lvgl_hashes, "compilers": compilers, "flags": ["-O1", "C99", "C++17"]}
    source_hash = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
    build = ROOT / "local/ui-preview/build" / source_hash[:20]
    include = build / "include"
    include.mkdir(parents=True, exist_ok=True)
    for file in (FIRMWARE / "include").iterdir():
        if file.is_file():
            write_if_changed(include / file.name, file.read_bytes())
    for file in (HERE / "stubs").iterdir():
        write_if_changed(include / file.name, file.read_bytes())
    configuration = (FIRMWARE / "include/lv_conf.h").read_bytes()
    configuration += (b'\n#define LV_ASSERT_HANDLER_INCLUDE "headless_assert.h"\n'
                      b'#define LV_ASSERT_HANDLER headless_assert(__FILE__, __LINE__);\n')
    write_if_changed(include / "lv_conf.h", configuration)
    copied_ui = build / "ui.cpp"
    write_if_changed(copied_ui, (FIRMWARE / "src/ui.cpp").read_bytes()
                     + b"\n" + (HERE / "hooks.cpp.inc").read_bytes())
    options = ["-O1", "-DLV_CONF_INCLUDE_SIMPLE", "-I" + str(include), "-I" + str(lvgl)]
    c_files = lvgl_files + assets

    def compile_one(item: tuple[int, Path]) -> Path:
        index, source = item
        target = build / f"part-{index}.o"
        if not target.exists():
            temporary = target.with_suffix(".tmp.o")
            run(["gcc", "-std=c99", *options, "-c", str(source), "-o", str(temporary)])
            temporary.replace(target)
        return target

    executable = build / "render"
    if not executable.exists():
        print(f"Compilando host LVGL 8.4.0 ({len(c_files)} unidades; sem PlatformIO/USB)...", flush=True)
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            objects = list(pool.map(compile_one, enumerate(c_files)))
        run(["g++", "-std=c++17", *options, str(copied_ui), str(HERE / "harness.cpp"),
             str(FIRMWARE / "src/simulation_service.cpp"), *map(str, objects), "-lm", "-o", str(executable)])
    else:
        print("Cache host conferido pelos hashes das fontes e compiladores.", flush=True)
    output.mkdir(parents=True, exist_ok=True)
    report = run([str(executable), str(output)], timeout=20)
    print(report, end="")
    (output / "validation.txt").write_text(report)
    contact = bytearray(4 * 320 * 3 * 240 * 3)
    for index, name in enumerate(SCENES):
        ppm = (output / f"{name}.ppm").read_bytes()
        header = b"P6\n320 240\n255\n"
        if not ppm.startswith(header):
            raise ValueError(f"Cabeçalho de screenshot inválido: {name}")
        pixels = ppm[len(header):]
        (output / f"{name}.png").write_bytes(png(320, 240, pixels))
        x, y = (index % 4) * 320, (index // 4) * 240
        for row in range(240):
            start = ((y + row) * 1280 + x) * 3
            contact[start:start + 960] = pixels[row * 960:(row + 1) * 960]
    (output / "contato.png").write_bytes(png(1280, 720, bytes(contact)))
    manifest = {
        "schema": 1, "kind": "headless-host-only", "lvgl_version": "8.4.0",
        "resolution": [320, 240], "color": "RGB565 swapped -> PNG RGB888", "lvgl_pool_bytes": 32768,
        "firmware_base_commit": run(["git", "rev-parse", "HEAD"]).strip(),
        "source_sha256": source_hash, "provenance": identity,
        "command": shlex.join(["sh", "scripts/preview-ui", *sys.argv[1:]]),
        "effective_jobs": args.jobs, "scenes": list(SCENES),
        "limits": ["Host stubs: LCD, DMA, touch, Arduino clock, heap, NVS, SD, Wi-Fi, BLE and serial sink",
                   "No physical USB, firmware flashing, SDK upgrade or hardware validation",
                   "No interactive window; deterministic synthetic time; no FPS/latency claim"],
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")
    print(f"Prévia: {output / 'contato.png'}")
    print(f"Fontes SHA-256: {source_hash}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, RuntimeError, OSError, subprocess.TimeoutExpired) as error:
        print(f"Prévia falhou: {error}", file=sys.stderr)
        raise SystemExit(1)
