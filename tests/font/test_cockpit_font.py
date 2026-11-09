"""Exercise generated cockpit data using real LVGL font readers, without hardware."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("generate_cockpit", ROOT / "tools/fonts/generate_cockpit.py")
assert spec and spec.loader
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)
LVGL = ROOT / "firmware/ecu/.pio/libdeps/ecu-normal/lvgl"

HARNESS = r'''
#include "cockpit_fonts.h"
#include <assert.h>
#include <string.h>
#ifdef COMPARE_ORIGINAL
LV_FONT_DECLARE(ngk_cockpit_64_original);
LV_FONT_DECLARE(ngk_cockpit_72_original);
#endif
static void check_font(const lv_font_t* font, const lv_font_t* original) {
    const char allowed[] = "-.0123456789";
    for (unsigned code = 32; code <= 176; ++code) {
        lv_font_glyph_dsc_t glyph;
        const int expected = code < 128 && strchr(allowed, (int)code) != NULL;
        assert(font->get_glyph_dsc(font, &glyph, code, 0) == expected);
        if (!expected) assert(font->get_glyph_bitmap(font, code) == NULL);
    }
    int ascender = 0, lowest = 0;
    for (unsigned index = 0; allowed[index]; ++index) {
        lv_font_glyph_dsc_t glyph;
        assert(font->get_glyph_dsc(font, &glyph, allowed[index], 0));
        assert(glyph.bpp == 4);
        if (original) {
            lv_font_glyph_dsc_t before;
            assert(original->get_glyph_dsc(original, &before, allowed[index], 0));
            assert(glyph.adv_w == before.adv_w && glyph.box_w == before.box_w && glyph.box_h == before.box_h);
            assert(glyph.ofs_x == before.ofs_x && glyph.ofs_y == before.ofs_y && glyph.bpp == before.bpp);
            assert(memcmp(font->get_glyph_bitmap(font, allowed[index]),
                          original->get_glyph_bitmap(original, allowed[index]),
                          ((unsigned)glyph.box_w * glyph.box_h + 1) / 2) == 0);
        }
        if (allowed[index] >= '0' && allowed[index] <= '9') {
            if (glyph.box_h + glyph.ofs_y > ascender) ascender = glyph.box_h + glyph.ofs_y;
            if (glyph.ofs_y < lowest) lowest = glyph.ofs_y;
        }
    }
    assert(font->line_height == ascender - lowest && font->base_line == -lowest);
    if (original) assert(font->line_height == original->line_height && font->base_line == original->base_line);
}
int main(void) {
#ifdef COMPARE_ORIGINAL
    check_font(&ngk_cockpit_64, &ngk_cockpit_64_original);
    check_font(&ngk_cockpit_72, &ngk_cockpit_72_original);
#else
    check_font(&ngk_cockpit_64, NULL);
    check_font(&ngk_cockpit_72, NULL);
#endif
}
'''


class CockpitFontTests(unittest.TestCase):
    def run_command(self, command: list[str], cwd: Path) -> None:
        result = subprocess.run(command, cwd=cwd, capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr or result.stdout)

    def test_normalizer_rejects_unrecognized_font_maps(self) -> None:
        with self.assertRaises(ValueError):
            generator.normalize_cmap("not an official numeric font")

    def compile_runtime(self, folder: Path, originals: dict[int, Path] | None = None) -> None:
        (folder / "check.c").write_text(HARNESS)
        flags = ["-std=c99", "-Wall", "-Wextra", "-Werror", "-DLV_CONF_SKIP",
                 "-DLV_USE_FONT_COMPRESSED=0", "-DLV_USE_LOG=0", "-DLV_LVGL_H_INCLUDE_SIMPLE",
                 "-I" + str(ROOT / "firmware/ecu/include"), "-I" + str(LVGL)]
        objects = []
        if originals:
            for size, source in originals.items():
                output = folder / f"original-{size}.o"
                self.run_command(["gcc", *flags, f"-Dngk_cockpit_{size}=ngk_cockpit_{size}_original",
                                  "-c", str(source), "-o", str(output)], folder)
                objects.append(str(output))
            flags.append("-DCOMPARE_ORIGINAL")
        self.run_command(["gcc", *flags, str(folder / "check.c"),
                          str(ROOT / "firmware/ecu/src/ngk_cockpit_64.c"),
                          str(ROOT / "firmware/ecu/src/ngk_cockpit_72.c"),
                          str(LVGL / "src/font/lv_font_fmt_txt.c"), str(LVGL / "src/misc/lv_utils.c"),
                          *objects, "-o", str(folder / "check")], folder)
        self.run_command([str(folder / "check")], folder)

    @unittest.skipUnless(shutil.which("gcc") and LVGL.is_dir(), "host GCC and project LVGL 8.4 are required")
    def test_actual_fonts_reject_holes_and_have_no_empty_vertical_margin(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            self.compile_runtime(Path(temporary))

    def test_regeneration_and_pixels_match_pinned_official_converter(self) -> None:
        if not generator.dependencies_ready():
            self.skipTest("Run generate_cockpit.py --prepare for the optional offline conversion test")
        if not shutil.which("gcc") or not LVGL.is_dir():
            self.skipTest("host GCC and project LVGL 8.4 are required")
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            originals = {}
            for size in generator.SIZES:
                raw, final = generator.generate(size, folder)
                self.assertEqual(final.read_bytes(), (ROOT / f"firmware/ecu/src/ngk_cockpit_{size}.c").read_bytes())
                originals[size] = raw
            self.compile_runtime(folder, originals)


if __name__ == "__main__":
    unittest.main()
