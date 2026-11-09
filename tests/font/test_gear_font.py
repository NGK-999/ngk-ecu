"""The small gear font must render N/R and reject holes in its sparse cmap."""
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
    @unittest.skipUnless(shutil.which('gcc') and LVGL.is_dir(), 'Requires local GCC and LVGL')
    def test_gear_glyphs_render_within_cell_and_unicode_holes_stay_empty(self):
        if not generator.dependencies_ready():
            self.skipTest('Pinned local converter/font cache required; no network in tests')
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            generated = generator.generate_gear(folder)
            self.assertEqual(generated.read_bytes(), (ROOT / 'firmware/ecu/src/ngk_gear_48.c').read_bytes())
            harness = folder / 'check.c'
            harness.write_text('''
#include "ngk_gear_48.h"
#include <assert.h>
#include <string.h>
int main(void) {
    const lv_font_t *font = &ngk_gear_48;
    assert(font->line_height <= 48);
    for (unsigned code = 32; code < 128; ++code) {
        lv_font_glyph_dsc_t glyph;
        const int wanted = strchr("-NR01234", code) != NULL;
        assert(font->get_glyph_dsc(font, &glyph, code, 0) == wanted);
        if (wanted) {
            assert(glyph.bpp == 4 && glyph.box_w > 0 && glyph.box_h > 0);
            assert(glyph.box_w <= 72 && glyph.box_h <= 48);
            assert(font->get_glyph_bitmap(font, code) != NULL);
        } else assert(font->get_glyph_bitmap(font, code) == NULL);
    }
    return 0;
}
''')
            flags = ['-std=c99', '-Wall', '-Wextra', '-Werror', '-DLV_CONF_SKIP',
                     '-DLV_USE_FONT_COMPRESSED=0', '-DLV_USE_LOG=0', '-DLV_LVGL_H_INCLUDE_SIMPLE',
                     '-I' + str(ROOT / 'firmware/ecu/include'), '-I' + str(LVGL)]
            subprocess.run(['gcc', *flags, str(harness), str(generated),
                            str(LVGL / 'src/font/lv_font_fmt_txt.c'), str(LVGL / 'src/misc/lv_utils.c'),
                            '-o', str(folder / 'check')], check=True, capture_output=True)
            subprocess.run([str(folder / 'check')], check=True, capture_output=True)


if __name__ == '__main__':
    unittest.main()
