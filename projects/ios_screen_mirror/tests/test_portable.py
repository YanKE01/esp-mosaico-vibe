"""Exercise device-owned C parsers and rendering on the host; no S31 timing claim."""
import ctypes as C
import pathlib
import plistlib
import random
import struct
import subprocess
import tempfile
import unittest

APP = pathlib.Path(__file__).resolve().parents[1]
PORT = APP / "components/airplay_receiver"


class SPS(C.Structure):
    _fields_ = [(n, C.c_uint8) for n in ("profile", "constraints", "level")] + [
        (n, C.c_uint32) for n in
        ("width", "height", "coded_width", "coded_height", "crop_left", "crop_top", "refs")
    ] + [("full_range", C.c_bool), ("bt709", C.c_bool)]


def buffer(data):
    return (C.c_uint8 * len(data)).from_buffer_copy(data)


class PortableTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        lib = pathlib.Path(cls.temp.name) / "mirror.so"
        subprocess.run([
            "cc", "-shared", "-fPIC", "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror",
            "-I", str(APP / "main"), "-I", str(PORT / "src"), "-I", str(PORT / "include"),
            str(APP / "main/mirror_h264.c"), str(APP / "main/mirror_pixels.c"),
            str(PORT / "src/airplay_packet.c"), str(PORT / "src/plist.c"),
            "-o", str(lib)], check=True)
        cls.lib = C.CDLL(str(lib))
        cls.lib.mirror_h264_read_sps.argtypes = [C.c_void_p, C.c_size_t, C.POINTER(SPS)]
        cls.lib.mirror_h264_read_sps.restype = C.c_bool
        cls.lib.mirror_h264_has_nal.argtypes = [C.c_void_p, C.c_size_t, C.c_uint]
        cls.lib.mirror_h264_has_nal.restype = C.c_bool
        cls.lib.mirror_pixels_render.argtypes = [C.c_void_p, C.c_size_t, C.POINTER(SPS), C.c_void_p, C.c_uint, C.c_uint]
        cls.lib.mirror_pixels_render.restype = C.c_bool
        cls.lib.airplay_packet_to_annexb.argtypes = [C.c_void_p, C.c_size_t]
        cls.lib.airplay_packet_to_annexb.restype = C.c_bool
        cls.lib.airplay_config_to_annexb.argtypes = [C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t]
        cls.lib.airplay_config_to_annexb.restype = C.c_size_t
        cls.lib.plist_from_bin.argtypes = [C.c_void_p, C.c_uint32, C.POINTER(C.c_void_p)]
        cls.lib.plist_to_bin.argtypes = [C.c_void_p, C.POINTER(C.c_void_p), C.POINTER(C.c_uint32)]
        cls.lib.plist_free.argtypes = [C.c_void_p]
        cls.libc = C.CDLL(None)
        cls.libc.free.argtypes = [C.c_void_p]
        cls.fixture = (APP / "tests/fixtures/baseline-318x478.h264").read_bytes()

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_real_x264_sps_coded_size_and_crop(self):
        s = SPS()
        self.assertTrue(self.lib.mirror_h264_read_sps(buffer(self.fixture), len(self.fixture), C.byref(s)))
        self.assertEqual((s.profile, s.width, s.height, s.coded_width, s.coded_height),
                         (66, 318, 478, 320, 480))
        self.assertTrue(s.constraints & 0x40)
        self.assertTrue(self.lib.mirror_h264_has_nal(buffer(self.fixture), len(self.fixture), 5))
        for n in range(8):
            self.assertFalse(self.lib.mirror_h264_read_sps(buffer(self.fixture[:n]), n, C.byref(s)))

    def test_high_profile_sps_geometry(self):
        data = (APP / "tests/fixtures/high-318x478.h264").read_bytes()
        s = SPS()
        self.assertTrue(self.lib.mirror_h264_read_sps(buffer(data), len(data), C.byref(s)))
        self.assertEqual((s.profile, s.constraints, s.level), (100, 0, 31))
        self.assertEqual((s.width, s.height, s.coded_width, s.coded_height), (318, 478, 320, 480))

    def test_reject_truncated_high_sps_and_exp_golomb(self):
        s = SPS()
        for data in (b"\x00\x00\x01\x67\x64\x00\x1f\xff",
                     b"\x00\x00\x01\x67\x42\xc0\x1f" + b"\x00" * 48):
            self.assertFalse(self.lib.mirror_h264_read_sps(buffer(data), len(data), C.byref(s)))

    def test_avcc_packet_lengths_are_validated_before_mutation(self):
        data = bytearray(b"\x00\x00\x00\x02\x65\x01\x00\x00\x00\x01\x09")
        b = buffer(data)
        self.assertTrue(self.lib.airplay_packet_to_annexb(b, len(data)))
        self.assertEqual(bytes(b), b"\x00\x00\x00\x01\x65\x01\x00\x00\x00\x01\x09")
        for malformed in (b"\x00", b"\xff\xff\xff\xff\x65", b"\x00\x00\x00\x00", bytes(data[:-1])):
            b = buffer(malformed)
            self.assertFalse(self.lib.airplay_packet_to_annexb(b, len(malformed)))
            self.assertEqual(bytes(b), malformed)

    def test_avcc_config_bounds_and_annexb_output(self):
        avcc = b"\x01\x42\xc0\x1f\xff\xe1\x00\x04\x67\x42\xc0\x1f\x01\x00\x02\x68\xce"
        out = (C.c_uint8 * 64)()
        n = self.lib.airplay_config_to_annexb(buffer(avcc), len(avcc), out, len(out))
        self.assertEqual(bytes(out[:n]), b"\x00\x00\x00\x01\x67\x42\xc0\x1f\x00\x00\x00\x01\x68\xce")
        for n in range(len(avcc)):
            self.assertEqual(self.lib.airplay_config_to_annexb(buffer(avcc[:n]), n, out, len(out)), 0)
        self.assertEqual(self.lib.airplay_config_to_annexb(buffer(avcc), len(avcc), out, 8), 0)

    def render(self, y, u=128, v=128, width=2, height=2, output=(2, 2), **kwargs):
        s = SPS(width=width, height=height, coded_width=width, coded_height=height, **kwargs)
        data = bytes([y] * (width * height) + [u] * (width * height // 4) + [v] * (width * height // 4))
        out = (C.c_uint16 * (output[0] * output[1]))()
        self.assertTrue(self.lib.mirror_pixels_render(buffer(data), len(data), C.byref(s), out, *output))
        return bytes(out)

    def test_rgb565_color_range_and_wire_byte_order(self):
        self.assertEqual(self.render(16), b"\x00\x00" * 4)
        self.assertEqual(self.render(235), b"\xff\xff" * 4)
        self.assertEqual(self.render(81, 90, 240), b"\xf8\x00" * 4)
        self.assertEqual(self.render(255, full_range=True), b"\xff\xff" * 4)
        self.assertNotEqual(self.render(235, full_range=True), b"\xff\xff" * 4)

    def test_letterbox_and_crop_do_not_stretch(self):
        out = self.render(235, width=2, height=4, output=(8, 8))
        for y in range(8):
            self.assertEqual(out[y * 16:(y + 1) * 16], b"\x00" * 4 + b"\xff" * 8 + b"\x00" * 4)
        s = SPS(width=2, height=4, coded_width=4, coded_height=4, crop_left=2)
        data = bytes([16, 16, 235, 235] * 4 + [128] * 8)
        out = (C.c_uint16 * 8)()
        self.assertTrue(self.lib.mirror_pixels_render(buffer(data), len(data), C.byref(s), out, 2, 4))
        self.assertEqual(bytes(out), b"\xff" * 16)
        s.crop_left = 0xffffffff
        self.assertFalse(self.lib.mirror_pixels_render(buffer(data), len(data), C.byref(s), out, 2, 4))

    def test_binary_plist_interoperates_with_python(self):
        expected = {"name": "Mosaico", "port": 7000, "streams": [{"type": 110}], "data": b"\x00\xff", "rate": 1 / 15, "flag": True}
        data = plistlib.dumps(expected, fmt=plistlib.FMT_BINARY)
        root, out, size = C.c_void_p(), C.c_void_p(), C.c_uint32()
        self.lib.plist_from_bin(buffer(data), len(data), C.byref(root))
        self.assertTrue(root.value)
        try:
            self.lib.plist_to_bin(root, C.byref(out), C.byref(size))
            self.assertTrue(out.value)
            self.assertEqual(plistlib.loads(C.string_at(out, size.value)), expected)
        finally:
            self.lib.plist_free(root)
            self.libc.free(out)

    def test_random_short_packets_and_sps_stay_bounded(self):
        rng = random.Random(31)
        for _ in range(2000):
            data = bytes(rng.randrange(256) for _ in range(rng.randrange(128)))
            b, s = buffer(data), SPS()
            self.lib.mirror_h264_read_sps(b, len(data), C.byref(s))
            self.lib.airplay_packet_to_annexb(b, len(data))
            out = (C.c_uint8 * 128)()
            self.lib.airplay_config_to_annexb(b, len(data), out, len(out))


if __name__ == "__main__":
    unittest.main()
