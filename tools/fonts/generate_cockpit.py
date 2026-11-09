#!/usr/bin/env python3
"""Reproduce NGK Cockpit numeric fonts with pinned official sources and local tools."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from urllib.request import urlopen

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
NODE = ROOT / ".tools/node-v24.22.1-linux-x64/bin/node"
NPM = ROOT / ".tools/node-v24.22.1-linux-x64/lib/node_modules/npm/bin/npm-cli.js"
CONVERTER = HERE / "node_modules/lv_font_conv/lv_font_conv.js"
FONT = ROOT / ".tools/font-sources/BarlowCondensed-Bold.ttf"
SOURCE_COMMIT = "5bb28733cec313b63fbd415b574b5ae35c8aa8b8"
SOURCE_URL = f"https://raw.githubusercontent.com/google/fonts/{SOURCE_COMMIT}/ofl/barlowcondensed/BarlowCondensed-Bold.ttf"
SOURCE_SHA256 = "e476562ec9c1e16cf16475895b511f08c804f438cc9a9f80a44ea50a0eeb5b65"
SIZES = (64, 72)
GLYPHS = "-.0123456789"


def run(command: list[str | Path], cwd: Path) -> str:
    result = subprocess.run([str(item) for item in command], cwd=cwd, capture_output=True, text=True, check=False)
    if result.returncode:
        raise RuntimeError(result.stderr or result.stdout)
    return result.stdout


def dependencies_ready() -> bool:
    """No installation/network side effects; suitable for optional offline tests."""
    if not NODE.is_file() or not CONVERTER.is_file() or not FONT.is_file():
        return False
    return hashlib.sha256(FONT.read_bytes()).hexdigest() == SOURCE_SHA256


def prepare() -> None:
    if not NODE.is_file() or not NPM.is_file():
        raise RuntimeError("Install the project's isolated Node 24.22.1 runtime under .tools first")
    if run([NODE, "--version"], ROOT).strip() != "v24.22.1":
        raise RuntimeError("This generator is pinned to Node 24.22.1")
    if not FONT.is_file() or hashlib.sha256(FONT.read_bytes()).hexdigest() != SOURCE_SHA256:
        with urlopen(SOURCE_URL, timeout=60) as response:
            data = response.read(1024 * 1024)
        if hashlib.sha256(data).hexdigest() != SOURCE_SHA256:
            raise ValueError("Official font SHA-256 mismatch; cache not replaced")
        FONT.parent.mkdir(parents=True, exist_ok=True)
        temporary = FONT.with_suffix(".download")
        temporary.write_bytes(data)
        temporary.replace(FONT)
    if not CONVERTER.is_file():
        run([NODE, NPM, "ci", "--ignore-scripts", "--no-audit", "--no-fund",
             "--cache", ROOT / ".tools/npm-fonts"], HERE)
    if json.loads(CONVERTER.with_name("package.json").read_text())["version"] != "1.5.3":
        raise RuntimeError("Expected lv_font_conv 1.5.3; restore the pinned local npm install")


def normalize_cmap(source: str) -> str:
    """Replace only the converter's dense map, whose '/' hole aliases '-' in LVGL 8.4."""
    marker = "static const uint8_t glyph_id_ofs_list_0[]"
    custom_data = "/*--------------------\n *  ALL CUSTOM DATA"
    try:
        start = source.index(marker)
        end = source.index(custom_data, start)
    except ValueError as error:
        raise ValueError("Unrecognized official numeric font map") from error
    region = source[start:end]
    compact = "".join(region.split())
    required = (
        "0,1,0,2,3,4,5,6,7,8,9,10,11",
        ".range_start=45,.range_length=13,.glyph_id_start=1",
        ".list_length=13,.type=LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL",
    )
    if not all(token in compact for token in required):
        raise ValueError("Unexpected converter map; refusing a lossy rewrite")
    sparse = """/* LVGL 8.4 compatibility: explicit holes; rasterized glyph data is unchanged. */
static const uint16_t numeric_unicode_list[] = {0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
static const lv_font_fmt_txt_cmap_t cmaps[] = {
    {.range_start = 45, .range_length = 13, .glyph_id_start = 1,
     .unicode_list = numeric_unicode_list, .glyph_id_ofs_list = NULL,
     .list_length = 12, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}
};

"""
    # Font bitmaps/descriptors precede the map, and global metrics follow it.
    return source[:start] + sparse + source[end:]


def generate(size: int, output: Path) -> tuple[Path, Path]:
    """Generate raw and normalized C into a caller-owned temporary directory."""
    if size not in SIZES:
        raise ValueError("Only the reviewed 64/72 px sizes are supported")
    if not dependencies_ready():
        raise RuntimeError("Run tools/fonts/generate_cockpit.py --prepare first")
    output.mkdir(parents=True, exist_ok=True)
    shutil.copy2(FONT, output / FONT.name)
    name = f"ngk_cockpit_{size}"
    target = output / (name + ".c")
    run([NODE, CONVERTER, "--font", FONT.name, "--symbols=" + GLYPHS,
         "--size", str(size), "--bpp", "4", "--format", "lvgl", "--no-compress",
         "--no-kerning", "--lv-font-name", name, "--lv-include", "cockpit_fonts.h",
         "--output", target.name], output)
    original = target.read_text()
    raw = output / (name + ".official.c")
    raw.write_text(original)
    target.write_text(normalize_cmap(original))
    return raw, target


def generate_gear(output: Path) -> Path:
    """Only gear symbols, with the converter's native sparse Unicode map."""
    if not dependencies_ready():
        raise RuntimeError("Run tools/fonts/generate_cockpit.py --prepare first")
    output.mkdir(parents=True, exist_ok=True)
    shutil.copy2(FONT, output / FONT.name)
    target = output / "ngk_gear_48.c"
    run([NODE, CONVERTER, "--font", FONT.name, "--symbols=-NR01234",
         "--size", "48", "--bpp", "4", "--format", "lvgl", "--no-compress",
         "--no-kerning", "--lv-font-name", "ngk_gear_48", "--lv-include", "ngk_gear_48.h",
         "--output", target.name], output)
    return target


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--prepare", action="store_true", help="download verified TTF/install pinned local npm package only")
    mode.add_argument("--check", action="store_true", help="offline reproduction check; never downloads or writes production")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "firmware/ecu/src")
    arguments = parser.parse_args()
    try:
        if arguments.prepare:
            prepare()
            print("Pinned converter and verified font ready in project-local caches")
            return 0
        if not arguments.check:
            prepare()
        temporary_root = ROOT / "local"
        temporary_root.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="cockpit-font-", dir=temporary_root) as temporary:
            results = [generate(size, Path(temporary)) for size in SIZES]
            results.append((None, generate_gear(Path(temporary))))
            for _, generated in results:
                target = arguments.output_dir / generated.name
                if arguments.check:
                    if not target.is_file() or target.read_bytes() != generated.read_bytes():
                        raise ValueError(f"Generated font differs: {target.name}")
                else:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(generated.read_bytes())
        print("Checked" if arguments.check else "Generated", "NGK Cockpit 64/72 and Gear 48")
        return 0
    except (OSError, ValueError, RuntimeError) as error:
        print(f"cockpit fonts: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
