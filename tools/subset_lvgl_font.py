#!/usr/bin/env python3
"""Copy the dashboard digits from an uncompressed LVGL 8.4 C font, without rasterizing."""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
from dataclasses import dataclass
from pathlib import Path

CHARACTERS = "-.0123456789"
GLYPH_FIELDS = {"bitmap_index", "adv_w", "box_w", "box_h", "ofs_x", "ofs_y"}
CMAP_FIELDS = {
    "range_start", "range_length", "glyph_id_start", "unicode_list",
    "glyph_id_ofs_list", "list_length", "type",
}


@dataclass(frozen=True)
class Glyph:
    codepoint: int
    bitmap: bytes
    adv_w: int
    box_w: int
    box_h: int
    ofs_x: int
    ofs_y: int


@dataclass(frozen=True)
class FontSubset:
    glyphs: tuple[Glyph, ...]
    line_height: int
    base_line: int
    source_sha256: str


def _number(value: str) -> int:
    if not re.fullmatch(r"-?(?:0[xX][0-9a-fA-F]+|[0-9]+)", value):
        raise ValueError(f"unsupported integer: {value!r}")
    return int(value, 16 if "x" in value.lower() else 10)


def _initializer(source: str, name: str) -> str:
    matches = list(re.finditer(r"\b" + re.escape(name) + r"\s*\[\s*\]\s*=\s*\{", source))
    if len(matches) != 1:
        raise ValueError(f"expected one {name} array")
    start = matches[0].end()
    depth = 1
    for position in range(start, len(source)):
        if source[position] == "{":
            depth += 1
        elif source[position] == "}":
            depth -= 1
            if depth == 0:
                return source[start:position]
    raise ValueError(f"unterminated {name} array")


def _records(initializer: str) -> list[dict[str, str]]:
    records = []
    end = 0
    for match in re.finditer(r"\{([^{}]*)\}", initializer):
        if initializer[end:match.start()].strip(" \t\r\n,"):
            raise ValueError("unsupported array contents")
        fields = {}
        for assignment in match[1].split(","):
            if not assignment.strip():
                continue
            field = re.fullmatch(r"\s*\.([a-zA-Z_]\w*)\s*=\s*(.*?)\s*", assignment)
            if not field or field[1] in fields:
                raise ValueError("invalid or duplicate descriptor field")
            fields[field[1]] = field[2]
        records.append(fields)
        end = match.end()
    if initializer[end:].strip(" \t\r\n,") or not records:
        raise ValueError("invalid or empty descriptor array")
    return records


def _assignment(source: str, name: str) -> str:
    values = re.findall(r"\." + re.escape(name) + r"\s*=\s*([^,\n}]+)", source)
    if len(values) != 1:
        raise ValueError(f"expected one {name} assignment")
    return values[0].strip()


def extract_subset(source: str) -> FontSubset:
    """Reject unsupported layouts and return exact selected glyph bytes and metrics."""
    clean = re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.DOTALL)
    if _number(_assignment(clean, "bpp")) != 4:
        raise ValueError("only 4 bpp fonts are supported")
    if _number(_assignment(clean, "bitmap_format")) != 0:
        raise ValueError("compressed fonts are not supported")
    for name, target in (("glyph_bitmap", "glyph_bitmap"), ("glyph_dsc", "glyph_dsc"), ("cmaps", "cmaps")):
        if _assignment(clean, name) != target:
            raise ValueError(f"unsupported {name} binding")

    bitmap_values = [_number(value.strip()) for value in _initializer(clean, "glyph_bitmap").split(",") if value.strip()]
    if not bitmap_values or any(value < 0 or value > 255 for value in bitmap_values):
        raise ValueError("bitmap elements must be bytes")
    bitmap = bytes(bitmap_values)
    descriptors = _records(_initializer(clean, "glyph_dsc"))
    if len(descriptors) < 96:
        raise ValueError("ASCII glyph descriptors are incomplete")
    metrics = []
    for descriptor in descriptors:
        if descriptor.keys() != GLYPH_FIELDS:
            raise ValueError("glyph descriptor fields are incomplete or unsupported")
        values = {key: _number(value) for key, value in descriptor.items()}
        if any(values[key] < 0 for key in ("bitmap_index", "adv_w", "box_w", "box_h")):
            raise ValueError("glyph dimensions, offsets and advances must not be negative")
        metrics.append(values)
    if any(metrics[0].values()):
        raise ValueError("glyph zero must remain reserved")
    for index, glyph in enumerate(metrics):
        end = glyph["bitmap_index"] + (glyph["box_w"] * glyph["box_h"] + 1) // 2
        if end > len(bitmap) or (index + 1 < len(metrics) and end > metrics[index + 1]["bitmap_index"]):
            raise ValueError("glyph bitmap is out of bounds or overlaps the next glyph")

    cmaps = _records(_initializer(clean, "cmaps"))
    if _number(_assignment(clean, "cmap_num")) != len(cmaps):
        raise ValueError("cmap count does not match the declared arrays")
    expected_ascii = {
        "range_start": "32", "range_length": "95", "glyph_id_start": "1",
        "unicode_list": "NULL", "glyph_id_ofs_list": "NULL", "list_length": "0",
        "type": "LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY",
    }
    if cmaps[0] != expected_ascii:
        raise ValueError("expected an ASCII 32..126 FORMAT0_TINY cmap starting at glyph 1")
    for cmap in cmaps[1:]:
        if cmap.keys() != CMAP_FIELDS:
            raise ValueError("unsupported cmap fields")
        start = _number(cmap["range_start"])
        length = _number(cmap["range_length"])
        if start < 0 or length <= 0 or (start <= 126 and start + length > 32):
            raise ValueError("additional cmap overlaps ASCII or has an invalid range")

    line_height = _number(_assignment(clean, "line_height"))
    base_line = _number(_assignment(clean, "base_line"))
    if line_height <= 0 or base_line < 0 or base_line >= line_height:
        raise ValueError("invalid font line metrics")
    selected = []
    for character in CHARACTERS:
        codepoint = ord(character)
        glyph = metrics[1 + codepoint - 32]
        start = glyph["bitmap_index"]
        size = (glyph["box_w"] * glyph["box_h"] + 1) // 2
        selected.append(Glyph(codepoint, bitmap[start:start + size], glyph["adv_w"], glyph["box_w"],
                              glyph["box_h"], glyph["ofs_x"], glyph["ofs_y"]))
    return FontSubset(tuple(selected), line_height, base_line, hashlib.sha256(source.encode("utf-8")).hexdigest())


def render_font(font: FontSubset) -> str:
    """Emit an LVGL 8.4 font with a sparse cmap and no kerning tables."""
    lines = [
        "/* Generated by tools/subset_lvgl_font.py; do not edit.",
        " * NGK Digits 48: modified subset of LVGL 8.4 Montserrat-Medium, 4 bpp.",
        " * Copyright 2011 The Montserrat Project Authors (SIL OFL 1.1).",
        " * LVGL font structures: Copyright (c) 2021 LVGL Kft (MIT).",
        " * Licenses and attribution: third_party/licenses/README.md.",
        f" * Source SHA-256: {font.source_sha256}",
        " */", '#include "digits_font.h"', "",
        "static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {",
    ]
    descriptors = ["    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},"]
    offset = 0
    for glyph in font.glyphs:
        lines.append(f"    /* U+{glyph.codepoint:04X} '{chr(glyph.codepoint)}' */")
        for position in range(0, len(glyph.bitmap), 12):
            lines.append("    " + ", ".join(f"0x{byte:02x}" for byte in glyph.bitmap[position:position + 12]) + ",")
        descriptors.append(
            "    {.bitmap_index = %d, .adv_w = %d, .box_w = %d, .box_h = %d, .ofs_x = %d, .ofs_y = %d},"
            % (offset, glyph.adv_w, glyph.box_w, glyph.box_h, glyph.ofs_x, glyph.ofs_y)
        )
        offset += len(glyph.bitmap)
    lines.extend(["};", "", "static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {", *descriptors, "};", "",
                  "static const uint16_t unicode_list[] = {", "    " + ", ".join(str(glyph.codepoint - 45) for glyph in font.glyphs),
                  "};", "", "static const lv_font_fmt_txt_cmap_t cmaps[] = {", "    {",
                  "        .range_start = 45, .range_length = 13, .glyph_id_start = 1,",
                  "        .unicode_list = unicode_list, .glyph_id_ofs_list = NULL,",
                  "        .list_length = 12, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY", "    }", "};", "",
                  "static lv_font_fmt_txt_glyph_cache_t cache;", "static const lv_font_fmt_txt_dsc_t font_dsc = {",
                  "    .glyph_bitmap = glyph_bitmap,", "    .glyph_dsc = glyph_dsc,", "    .cmaps = cmaps,",
                  "    .kern_dsc = NULL,", "    .kern_scale = 0,", "    .cmap_num = 1,", "    .bpp = 4,",
                  "    .kern_classes = 0,", "    .bitmap_format = 0,", "    .cache = &cache", "};", "",
                  "const lv_font_t ngk_digits_48 = {", "    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,",
                  "    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,", f"    .line_height = {font.line_height},",
                  f"    .base_line = {font.base_line},", "    .subpx = LV_FONT_SUBPX_NONE,", "    .underline_position = -4,",
                  "    .underline_thickness = 2,", "    .dsc = &font_dsc", "};", ""])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="LVGL 8.4 uncompressed Montserrat 48 C source")
    parser.add_argument("output", type=Path, help="generated ngk_digits_48.c")
    parser.add_argument("--check", action="store_true", help="compare reproduction without writing")
    args = parser.parse_args()
    try:
        font = extract_subset(args.source.read_text(encoding="utf-8"))
        generated = render_font(font).encode("utf-8")
        if args.check:
            if not args.output.is_file() or args.output.read_bytes() != generated:
                print("generated font is missing or differs from its source", file=sys.stderr)
                return 1
        else:
            args.output.write_bytes(generated)
    except (OSError, ValueError) as error:
        print(f"font subset: {error}", file=sys.stderr)
        return 2
    print(f"{'Checked' if args.check else 'Generated'} {len(font.glyphs)} glyphs, "
          f"{sum(len(glyph.bitmap) for glyph in font.glyphs)} bitmap bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
