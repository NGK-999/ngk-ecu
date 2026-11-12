"""Driver regressions: reject LVGL drift and preserve valid RGB PNG payloads."""
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

from render import check_lvgl, png


class PreviewDriverTests(unittest.TestCase):
    def test_png_round_trip_and_chunk_checksums(self):
        # Includes all channel extremes and multiple rows: catches bad row strides
        # and missing filter bytes independently of the production UI.
        pixels = bytes((255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255))
        image = png(2, 2, pixels)
        self.assertEqual(image[:8], b"\x89PNG\r\n\x1a\n")
        offset, chunks = 8, {}
        while offset < len(image):
            length = struct.unpack(">I", image[offset:offset + 4])[0]
            kind = image[offset + 4:offset + 8]
            data = image[offset + 8:offset + 8 + length]
            checksum = struct.unpack(">I", image[offset + 8 + length:offset + 12 + length])[0]
            self.assertEqual(checksum, zlib.crc32(kind + data) & 0xFFFFFFFF)
            chunks[kind] = data
            offset += length + 12
        self.assertEqual(struct.unpack(">IIBBBBB", chunks[b"IHDR"]), (2, 2, 8, 2, 0, 0, 0))
        self.assertEqual(zlib.decompress(chunks[b"IDAT"]), b"\0" + pixels[:6] + b"\0" + pixels[6:])

    def test_png_rejects_truncated_pixels(self):
        with self.assertRaises(ValueError):
            png(320, 240, b"\0\0\0")

    def test_lvgl_rejects_major_drift_and_mislabeled_header(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            with self.assertRaises(ValueError):
                check_lvgl(path)
            (path / "library.json").write_text(json.dumps({"version": "9.6.0"}))
            with self.assertRaisesRegex(ValueError, "exatamente 8.4.0"):
                check_lvgl(path)
            (path / "library.json").write_text(json.dumps({"version": "8.4.0"}))
            header = "#define LVGL_VERSION_MAJOR 9\n#define LVGL_VERSION_MINOR 4\n#define LVGL_VERSION_PATCH 0\n"
            (path / "lvgl.h").write_text(header)
            with self.assertRaisesRegex(ValueError, "difere"):
                check_lvgl(path)
            (path / "lvgl.h").write_text(header.replace("MAJOR 9", "MAJOR 8"))
            check_lvgl(path)


if __name__ == "__main__":
    unittest.main()
