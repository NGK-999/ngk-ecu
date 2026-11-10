"""Gear fonts render only their eight symbols, using the real LVGL callbacks."""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('generate_cockpit', ROOT / 'tools/fonts/generate_cockpit.py')
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)
LVGL = ROOT / 'firmware/ecu/.pio/libdeps/ecu-normal/lvgl'


class GearFontTests(unittest.TestCase):
    def test_default_48_and_explicit_64_regenerate_exactly(self):
        if not generator.dependencies_ready():
            self.skipTest('Pinned local converter/font cache required; no network in tests')
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            generated = generator.generate_gear(folder)
            self.assertEqual(generated.read_bytes(), (ROOT / 'firmware/ecu/src/ngk_gear_48.c').read_bytes())
            target = ROOT / 'firmware/ecu/src/ngk_gear_64.c'
            self.assertTrue(target.is_file(), 'The larger gear font must be available to the panel')
            generated = generator.generate_gear(folder, size=64)
            self.assertEqual(generated.read_bytes(), target.read_bytes())

    def test_offline_check_detects_a_modified_gear_64(self):
        if not generator.dependencies_ready():
            self.skipTest('Pinned local converter/font cache required; no network in tests')
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            for name in ('ngk_cockpit_48.c', 'ngk_cockpit_64.c', 'ngk_cockpit_72.c',
                         'ngk_cockpit_84.c', 'ngk_gear_48.c', 'ngk_gear_64.c'):
                source = ROOT / 'firmware/ecu/src' / name
                self.assertTrue(source.is_file(), 'All reviewed font sizes must reproduce offline')
                shutil.copy2(source, folder / name)
            command = [str(ROOT / '.venv/bin/python'), str(ROOT / 'tools/fonts/generate_cockpit.py'),
                       '--check', '--output-dir', str(folder)]
            result = subprocess.run(command, check=False, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr or result.stdout)
            with (folder / 'ngk_gear_64.c').open('a') as output:
                output.write('\n/* altered font */\n')
            result = subprocess.run(command, check=False, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0, 'Offline check must include Gear 64')
            self.assertIn('ngk_gear_64.c', result.stderr)

    @unittest.skipUnless(shutil.which('gcc') and LVGL.is_dir(), 'Requires local GCC and LVGL')
    def test_gear_glyphs_render_within_cell_and_unicode_holes_stay_empty(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            harness = folder / 'check.c'
            harness.write_text('''
#include "ngk_gear_48.h"
#include "ngk_gear_64.h"
#include <assert.h>
#include <string.h>
static void check_font(const lv_font_t *font, int height, int baseline,
                       int max_width) {
    assert(font->line_height == height && font->base_line == baseline);
    unsigned found = 0;
    int top = 0, bottom = 0;
    for (unsigned code = 32; code <= 176; ++code) {
        lv_font_glyph_dsc_t glyph;
        const int wanted = code < 128 && strchr("-NR01234", code) != NULL;
        assert(font->get_glyph_dsc(font, &glyph, code, 0) == wanted);
        if (wanted) {
            ++found;
            assert(glyph.bpp == 4 && glyph.box_w > 0 && glyph.box_h > 0);
            assert(glyph.box_w <= max_width && glyph.box_h <= height);
            assert(glyph.ofs_y >= -baseline);
            assert(glyph.ofs_y + glyph.box_h <= (int)height - baseline);
            assert(font->get_glyph_bitmap(font, code) != NULL);
            if (glyph.ofs_y + glyph.box_h > top) top = glyph.ofs_y + glyph.box_h;
            if (glyph.ofs_y < bottom) bottom = glyph.ofs_y;
        } else assert(font->get_glyph_bitmap(font, code) == NULL);
    }
    assert(found == 8);
    assert(top - bottom == (int)height && -bottom == baseline);
}
int main(void) {
    check_font(&ngk_gear_48, 34, 0, 23);
    check_font(&ngk_gear_64, 48, 1, 31);
    return 0;
}
''')
            flags = ['-std=c99', '-Wall', '-Wextra', '-Werror', '-DLV_CONF_SKIP',
                     '-DLV_USE_FONT_COMPRESSED=0', '-DLV_USE_LOG=0', '-DLV_LVGL_H_INCLUDE_SIMPLE',
                     '-I' + str(ROOT / 'firmware/ecu/include'), '-I' + str(LVGL)]
            subprocess.run(['gcc', *flags, str(harness),
                            str(ROOT / 'firmware/ecu/src/ngk_gear_48.c'),
                            str(ROOT / 'firmware/ecu/src/ngk_gear_64.c'),
                            str(LVGL / 'src/font/lv_font_fmt_txt.c'), str(LVGL / 'src/misc/lv_utils.c'),
                            '-o', str(folder / 'check')], check=True, capture_output=True)
            subprocess.run([str(folder / 'check')], check=True, capture_output=True)


if __name__ == '__main__':
    unittest.main()
