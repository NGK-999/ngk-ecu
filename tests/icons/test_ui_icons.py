"""Offline reproducibility and real LVGL 8 alpha4 interoperability."""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("generate_icons", ROOT / "tools/icons/generate.py")
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)
LVGL = ROOT / "firmware/ecu/.pio/libdeps/ecu-normal/lvgl"


class IconTests(unittest.TestCase):
    def test_pinned_offline_generation_matches_committed_assets(self):
        for path, content in generator.generate().items():
            self.assertEqual(path.read_text(), content, str(path.relative_to(ROOT)))

    @unittest.skipUnless(shutil.which("gcc") and LVGL.is_dir(), "Requires local GCC and LVGL 8")
    def test_every_alpha_pixel_decodes_with_real_lvgl_and_odd_rows_align(self):
        generator.verify_sources()
        generator.require_qt()
        references = []
        symbols = []
        for index, (name, symbol, size) in enumerate(generator.ICONS):
            alpha = generator.rasterize(name, size)
            expected = [(value * 15 + 127) // 255 * 17 for value in alpha]
            references.append(f"static const unsigned char reference_{index}[] = {{" +
                              ",".join(str(value) for value in expected) + "};")
            symbols.append(symbol)
        # Six deliberately different pixels catch reversed nibbles and odd-width padding.
        odd_alpha = bytes([0, 17, 34, 51, 68, 85])
        odd = generator.pack_alpha4(odd_alpha, 3, 2)
        odd_source = ",".join(str(value) for value in odd)
        harness = '''
#include "ui_icons.h"
#include <assert.h>
#include <stddef.h>
''' + "\n".join(references) + '''
int main(void) {
    const lv_img_dsc_t *images[] = {''' + ",".join("&" + symbol for symbol in symbols) + '''};
    const unsigned char *references[] = {reference_0, reference_1, reference_2, reference_3, reference_4};
    unsigned total = 0;
    for (unsigned i = 0; i < 5; ++i) {
        const lv_img_dsc_t *image = images[i];
        const unsigned size = i == 4 ? NGK_ICON_ALERT_SIZE : NGK_ICON_MENU_SIZE;
        unsigned opaque = 0, partial = 0;
        assert(image->header.cf == LV_IMG_CF_ALPHA_4BIT);
        assert(image->header.always_zero == 0 && image->header.reserved == 0);
        assert(image->header.w == size && image->header.h == size);
        assert(image->data_size == ((size + 1) / 2) * size);
        total += image->data_size;
        for (unsigned y = 0; y < size; ++y) {
            for (unsigned x = 0; x < size; ++x) {
                const unsigned alpha = lv_img_buf_get_px_alpha(image, x, y);
                assert(alpha == references[i][y * size + x]);
                if ((x == 0 || x + 1 == size) && (y == 0 || y + 1 == size)) assert(alpha == 0);
                opaque += alpha == 255;
                partial += alpha > 0 && alpha < 255;
            }
        }
        assert(opaque > 0 && partial > 0);
    }
    assert(total == 2248);
    static const unsigned char odd_data[] = {''' + odd_source + '''};
    const lv_img_dsc_t odd = {
        .header = { .cf = LV_IMG_CF_ALPHA_4BIT, .always_zero = 0, .w = 3, .h = 2 },
        .data_size = sizeof(odd_data), .data = odd_data
    };
    for (unsigned y = 0; y < 2; ++y)
        for (unsigned x = 0; x < 3; ++x)
            assert(lv_img_buf_get_px_alpha(&odd, x, y) == (y * 3 + x) * 17);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            check = folder / "check.c"
            check.write_text(harness)
            flags = ["-std=c99", "-Wall", "-Wextra", "-Werror", "-ffunction-sections",
                     "-fdata-sections", "-Wl,--gc-sections", "-DLV_CONF_SKIP", "-DLV_USE_LOG=0",
                     "-I" + str(ROOT / "firmware/ecu/include"), "-I" + str(LVGL)]
            build = subprocess.run(["gcc", *flags, str(check), str(ROOT / "firmware/ecu/src/ui_icons.c"),
                                    str(LVGL / "src/draw/lv_img_buf.c"), "-o", str(folder / "check")],
                                   capture_output=True, text=True, check=False)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(folder / "check")], capture_output=True, text=True, check=False)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
