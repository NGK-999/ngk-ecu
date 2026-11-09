"""Synthetic byte streams only: no imports or use of serial hardware."""
import importlib.util
import io
from pathlib import Path
import struct
import unittest
import zlib

spec = importlib.util.spec_from_file_location("capture_screen", Path(__file__).resolve().parents[2] / "tools/capture_screen.py")
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)


class CaptureTests(unittest.TestCase):
    def test_rgb565_byte_order_and_channels(self):
        png = capture.png_from_rgb565(bytes.fromhex("f80007e0001f"), 3, 1)
        self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
        offset = 8
        while offset < len(png):
            length = struct.unpack(">I", png[offset:offset+4])[0]
            kind = png[offset+4:offset+8]
            payload = png[offset+8:offset+8+length]
            if kind == b"IDAT":
                self.assertEqual(zlib.decompress(payload), b"\0\xff\0\0\0\xff\0\0\0\xff")
                return
            offset += length + 12
        self.fail("PNG sem IDAT")

    def packet(self, pixels):
        return capture.HEADER + pixels + f"\nNGK_SCREEN_END 153600 {zlib.crc32(pixels):08x}\n".encode()

    def test_binary_newlines_are_not_protocol_lines(self):
        pixels = (b"\nNGK_SCREEN\r\n\0\xff" * 12000)[:capture.PIXEL_BYTES]
        pixels = pixels.ljust(capture.PIXEL_BYTES, b"\0")
        png, checksum = capture.receive_capture(io.BytesIO(b"startup\n" + self.packet(pixels)))
        self.assertEqual(checksum, zlib.crc32(pixels))
        self.assertTrue(png.startswith(b"\x89PNG"))

    def test_transient_read_timeout_preserves_capture(self):
        class GapStream(io.BytesIO):
            gap = True
            def read(self, size=-1):
                if self.gap:
                    self.gap = False
                    return b""
                return super().read(size)
        pixels = b"\0" * capture.PIXEL_BYTES
        png, checksum = capture.receive_capture(GapStream(self.packet(pixels)), timeout=1)
        self.assertEqual(checksum, zlib.crc32(pixels))
        self.assertTrue(png.startswith(b"\x89PNG"))

    def test_truncated_capture_rejected(self):
        with self.assertRaises(TimeoutError):
            capture.receive_capture(io.BytesIO(capture.HEADER + b"\0" * 64), timeout=.01)

    def test_crc_mismatch_rejected(self):
        pixels = b"\0" * capture.PIXEL_BYTES
        packet = self.packet(pixels).replace(f"{zlib.crc32(pixels):08x}".encode(), b"ffffffff")
        with self.assertRaises(ValueError):
            capture.receive_capture(io.BytesIO(packet))

    def test_geometry_or_blocked_reply_rejected(self):
        for response in (b"NGK_SCREEN RGB565_BE 640 480 614400\n",
                         b'{"error":"screen_capture_blocked"}\n',
                         b'{"error":"screen_capture_tx_busy"}\n'):
            with self.subTest(response=response):
                with self.assertRaises(ValueError):
                    capture.receive_capture(io.BytesIO(response))


if __name__ == "__main__":
    unittest.main()
