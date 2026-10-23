"""Compile the actual C decoder wrapper/upstream backend and decode High H.264."""
import ctypes as C
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest

APP = pathlib.Path(__file__).resolve().parents[1]
COMPONENT = APP / "components/openh264_decoder"


class Frame(C.Structure):
    _fields_ = [("data", C.c_void_p), ("size", C.c_size_t),
                ("width", C.c_uint32), ("height", C.c_uint32),
                ("pts", C.c_uint64), ("codec_status", C.c_int)]


class OpenH264Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not all(shutil.which(tool) for tool in ("cmake", "c++", "ffmpeg")):
            raise unittest.SkipTest("Native decoder validation requires cmake, C++, and FFmpeg")
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        build = pathlib.Path(cls.temp.name) / "build"
        subprocess.run(["cmake", "-S", str(COMPONENT), "-B", str(build),
                        "-DCMAKE_BUILD_TYPE=Release"], check=True, capture_output=True)
        subprocess.run(["cmake", "--build", str(build), "-j", "8"], check=True, capture_output=True)
        cls.lib = C.CDLL(str(build / "libopenh264_decoder.so"))
        cls.lib.openh264_decoder_create.argtypes = [C.c_uint32, C.POINTER(C.c_void_p)]
        cls.lib.openh264_decoder_process.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t,
                                                    C.c_uint64, C.POINTER(Frame)]
        cls.lib.openh264_decoder_delete.argtypes = [C.c_void_p]
        cls.lib.openh264_decoder_delete.restype = None
        cls.fixture = APP / "tests/fixtures/high-318x478.h264"
        data = cls.fixture.read_bytes()
        starts = [m.start() for m in re.finditer(b"\x00\x00\x00?\x01\x09", data)] + [len(data)]
        cls.units = [data[starts[i]:starts[i + 1]] for i in range(len(starts) - 1)]

    def create(self):
        h = C.c_void_p()
        self.assertEqual(self.lib.openh264_decoder_create(307200, C.byref(h)), 0)
        self.assertTrue(h.value)
        self.addCleanup(self.lib.openh264_decoder_delete, h)
        return h

    def decode(self, h, units, flush=True):
        frames = []
        for i, data in enumerate(units):
            f, buf = Frame(), C.create_string_buffer(data)
            self.assertEqual(self.lib.openh264_decoder_process(h, buf, len(data), i, C.byref(f)), 0)
            if f.size:
                self.assertEqual((f.width, f.height, f.size), (318, 478, 318 * 478 * 3 // 2))
                frames.append(C.string_at(f.data, f.size))
        if flush:
            for _ in range(16):
                f = Frame()
                self.assertEqual(self.lib.openh264_decoder_process(h, None, 0, 0, C.byref(f)), 0)
                if not f.size:
                    break
                frames.append(C.string_at(f.data, f.size))
            else:
                self.fail("Decoder did not finish flushing its delayed frames")
        return frames

    def test_high_cabac_bframes_crop_and_display_order(self):
        frames = self.decode(self.create(), self.units)
        self.assertEqual(len(frames), 12)
        expected = subprocess.check_output(["ffmpeg", "-hide_banner", "-loglevel", "error",
                                            "-i", str(self.fixture), "-f", "rawvideo",
                                            "-pix_fmt", "yuv420p", "-"])
        actual = b"".join(frames)
        self.assertEqual(len(actual), len(expected))
        # Independent decoder comparison: allow at most one 8-bit level of
        # rounding error. Wrong frame order/crop/strides exceed this bound.
        self.assertLessEqual(max(abs(a - b) for a, b in zip(actual, expected)), 1)
        self.assertLess(sum(a != b for a, b in zip(actual, expected)), len(actual) // 1000)

    def test_decoder_recreation_starts_with_fresh_references(self):
        first = self.decode(self.create(), self.units[:2], flush=False)
        second = self.decode(self.create(), self.units[:2], flush=False)
        self.assertEqual(len(first), 1)
        self.assertEqual(first, second)


if __name__ == "__main__":
    unittest.main()
