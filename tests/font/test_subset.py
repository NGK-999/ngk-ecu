"""Exercise glyph data and reproduction without installing font conversion tools."""

from __future__ import annotations

import hashlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from subset_lvgl_font import extract_subset, render_font  # noqa: E402


def synthetic_source() -> str:
    """ASCII glyph N has one byte equal to its codepoint and an independent advance."""
    descriptors = [
        "{.bitmap_index=0,.adv_w=0,.box_w=0,.box_h=0,.ofs_x=0,.ofs_y=0}"
    ]
    for codepoint in range(32, 127):
        descriptors.append(
            "{.bitmap_index=%d,.adv_w=%d,.box_w=2,.box_h=1,.ofs_x=-1,.ofs_y=2}"
            % (codepoint - 32, codepoint * 16)
        )
    return (
        "static const uint8_t glyph_bitmap[] = {"
        + ",".join(str(codepoint) for codepoint in range(32, 127))
        + "};\nstatic const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {"
        + ",".join(descriptors)
        + "};\nstatic const lv_font_fmt_txt_cmap_t cmaps[] = {"
        "{.range_start=32,.range_length=95,.glyph_id_start=1,.unicode_list=NULL,"
        ".glyph_id_ofs_list=NULL,.list_length=0,.type=LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY}};\n"
        "static const lv_font_fmt_txt_dsc_t font_dsc = {"
        ".glyph_bitmap=glyph_bitmap,.glyph_dsc=glyph_dsc,.cmaps=cmaps,"
        ".cmap_num=1,.bpp=4,.bitmap_format=0};\n"
        "const lv_font_t font = {.line_height=52,.base_line=9};\n"
    )


class SubsetTests(unittest.TestCase):
    def test_ascii_codepoints_use_their_mapped_glyph_ids_and_preserve_pixels(self) -> None:
        font = extract_subset(synthetic_source())
        self.assertEqual(tuple(glyph.codepoint for glyph in font.glyphs),
                         (45, 46, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57))
        self.assertEqual(tuple(glyph.bitmap for glyph in font.glyphs),
                         (b"-", b".", b"0", b"1", b"2", b"3", b"4", b"5", b"6", b"7", b"8", b"9"))
        self.assertEqual((font.glyphs[0].adv_w, font.glyphs[0].box_w, font.glyphs[0].box_h,
                          font.glyphs[0].ofs_x, font.glyphs[0].ofs_y), (720, 2, 1, -1, 2))
        self.assertEqual(font.glyphs[2].adv_w, 768)
        self.assertEqual((font.line_height, font.base_line), (52, 9))

    def test_packed_odd_pixel_glyph_includes_its_final_half_byte(self) -> None:
        source = synthetic_source().replace(".box_w=2,.box_h=1", ".box_w=1,.box_h=1")
        self.assertEqual(extract_subset(source).glyphs[0].bitmap, b"-")

    def test_compressed_or_different_bpp_fonts_are_rejected(self) -> None:
        for original, replacement in ((".bitmap_format=0", ".bitmap_format=1"),
                                      (".bpp=4", ".bpp=2")):
            with self.subTest(replacement=replacement), self.assertRaises(ValueError):
                extract_subset(synthetic_source().replace(original, replacement))

    def test_incompatible_ascii_cmaps_are_rejected(self) -> None:
        for original, replacement in ((".range_start=32", ".range_start=33"),
                                      (".range_length=95", ".range_length=94"),
                                      (".glyph_id_start=1", ".glyph_id_start=0"),
                                      (".unicode_list=NULL", ".unicode_list=other"),
                                      (".glyph_id_ofs_list=NULL", ".glyph_id_ofs_list=other"),
                                      (".list_length=0", ".list_length=95"),
                                      ("CMAP_FORMAT0_TINY", "CMAP_SPARSE_TINY"),
                                      (".cmap_num=1", ".cmap_num=2")):
            with self.subTest(replacement=replacement), self.assertRaises(ValueError):
                extract_subset(synthetic_source().replace(original, replacement))

    def test_missing_duplicate_and_out_of_bounds_glyph_data_are_rejected(self) -> None:
        source = synthetic_source()
        for broken in (
            source.replace(".bitmap_index=13,.adv_w=720", ".bitmap_index=999,.adv_w=720"),
            source.replace(".bitmap_index=13,.adv_w=720", ".bitmap_index=-1,.adv_w=720"),
            source.replace(".adv_w=720,", ".adv_w=720,.adv_w=721,"),
            source.replace(".adv_w=720,", ""),
            source.replace(".box_w=2,.box_h=1", ".box_w=2000,.box_h=1000", 1),
            source.replace(".box_w=2,.box_h=1", ".box_w=-1,.box_h=1", 1),
            source.replace("glyph_bitmap[] = {32,", "glyph_bitmap[] = {256,"),
        ):
            with self.subTest(source=broken[:100]), self.assertRaises(ValueError):
                extract_subset(broken)

    def test_cli_generates_and_checks_reproducible_output_without_changing_bad_output(self) -> None:
        script = ROOT / "tools/subset_lvgl_font.py"
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "input.c"
            output = Path(directory) / "digits.c"
            source.write_text(synthetic_source(), encoding="utf-8")
            command = [sys.executable, str(script), str(source), str(output)]
            generated = subprocess.run(command, capture_output=True, text=True, check=False)
            self.assertEqual(generated.returncode, 0, generated.stderr)
            expected = render_font(extract_subset(synthetic_source()))
            self.assertEqual(output.read_text(encoding="utf-8"), expected)
            checked = subprocess.run(command + ["--check"], capture_output=True, text=True, check=False)
            self.assertEqual(checked.returncode, 0, checked.stderr)
            output.write_text("stale", encoding="utf-8")
            stale = subprocess.run(command + ["--check"], capture_output=True, text=True, check=False)
            self.assertNotEqual(stale.returncode, 0)
            self.assertEqual(output.read_text(encoding="utf-8"), "stale")


REAL_FONT = ROOT / "firmware/ecu/.pio/libdeps/ecu-normal/lvgl/src/font/lv_font_montserrat_48.c"


@unittest.skipUnless(REAL_FONT.is_file(), "LVGL 8.4 source is not installed locally")
class RealFontTests(unittest.TestCase):
    def test_montserrat_metrics_and_bitmap_match_the_original_glyphs(self) -> None:
        font = extract_subset(REAL_FONT.read_text(encoding="utf-8"))
        self.assertEqual((font.line_height, font.base_line), (52, 9))
        for character, metrics in {
            "-": (294, 14, 5, 2, 11), ".": (174, 7, 7, 2, 0),
            "0": (512, 28, 34, 2, 0), "1": (284, 13, 34, 0, 0),
        }.items():
            glyph = next(glyph for glyph in font.glyphs if glyph.codepoint == ord(character))
            with self.subTest(character=character):
                self.assertEqual((glyph.adv_w, glyph.box_w, glyph.box_h, glyph.ofs_x, glyph.ofs_y), metrics)
                self.assertEqual(len(glyph.bitmap), (metrics[1] * metrics[2] + 1) // 2)
        pixels = b"".join(glyph.bitmap for glyph in font.glyphs)
        self.assertEqual(hashlib.sha256(pixels).hexdigest(), "3079274f1be2760180cceb322dbfa8182ed1cae79f29968f8c889dab23712825")

    def test_committed_subset_reproduces_from_the_local_source(self) -> None:
        font = extract_subset(REAL_FONT.read_text(encoding="utf-8"))
        self.assertEqual((ROOT / "firmware/ecu/src/ngk_digits_48.c").read_text(encoding="utf-8"), render_font(font))

    @unittest.skipUnless(shutil.which("gcc"), "gcc is required for the LVGL runtime integration")
    def test_generated_sparse_cmap_and_pixels_work_with_real_lvgl(self) -> None:
        """Compare executable font callbacks, including the '/' hole in the sparse cmap."""
        harness = r'''
#include "digits_font.h"
#include <assert.h>
#include <string.h>
int main(void) {
    const lv_font_fmt_txt_dsc_t * data = ngk_digits_48.dsc;
    assert(ngk_digits_48.line_height == 52 && ngk_digits_48.base_line == 9);
    assert(data->kern_dsc == NULL && data->kern_classes == 0 && data->bpp == 4);
    const char selected[] = "-.0123456789";
    for(unsigned character = 32; character <= 176; ++character) {
        const int retained = character <= 127 && strchr(selected, (int)character) != NULL;
        lv_font_glyph_dsc_t actual, original;
        assert(ngk_digits_48.get_glyph_dsc(&ngk_digits_48, &actual, character, 0) == retained);
        const uint8_t * pixels = ngk_digits_48.get_glyph_bitmap(&ngk_digits_48, character);
        if(!retained) {
            assert(pixels == NULL);
            continue;
        }
        assert(lv_font_montserrat_48.get_glyph_dsc(&lv_font_montserrat_48, &original, character, 0));
        assert(actual.adv_w == original.adv_w && actual.box_w == original.box_w && actual.box_h == original.box_h);
        assert(actual.ofs_x == original.ofs_x && actual.ofs_y == original.ofs_y && actual.bpp == original.bpp);
        const size_t bytes = ((size_t)original.box_w * original.box_h + 1) / 2;
        assert(memcmp(pixels, lv_font_montserrat_48.get_glyph_bitmap(&lv_font_montserrat_48, character), bytes) == 0);
    }
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            (folder / "main.c").write_text(harness, encoding="utf-8")
            font = extract_subset(REAL_FONT.read_text(encoding="utf-8"))
            (folder / "font.c").write_text(render_font(font), encoding="utf-8")
            lvgl = REAL_FONT.parents[2]
            command = ["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-DLV_CONF_SKIP",
                       "-DLV_USE_FONT_COMPRESSED=0", "-DLV_USE_LOG=0", "-DLV_FONT_MONTSERRAT_48=1",
                       "-I" + str(ROOT / "firmware/ecu/include"), "-I" + str(lvgl),
                       str(folder / "main.c"), str(folder / "font.c"), str(REAL_FONT),
                       str(lvgl / "src/font/lv_font_fmt_txt.c"), str(lvgl / "src/misc/lv_utils.c"),
                       "-o", str(folder / "check-font")]
            compiled = subprocess.run(command, capture_output=True, text=True, check=False)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            executed = subprocess.run([str(folder / "check-font")], capture_output=True, text=True, check=False)
            self.assertEqual(executed.returncode, 0, executed.stderr)


if __name__ == "__main__":
    unittest.main()
