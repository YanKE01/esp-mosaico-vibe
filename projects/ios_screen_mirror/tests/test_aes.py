"""Exercise the production AES adapter and mirror stream with a host provider."""
import ctypes as C
import hashlib
import pathlib
import subprocess
import tempfile
import unittest

APP = pathlib.Path(__file__).resolve().parents[1]
PORT = APP / "components/airplay_receiver"
HOST = APP / "tests/aes_host"
KEY = bytes.fromhex("2b7e151628aed2a6abf7158809cf4f3c")
CTR_IV = bytes.fromhex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff")
PLAIN = bytes.fromhex("6bc1bee22e409f96e93d7e117393172a"
                      "ae2d8a571e03ac9c9eb76fac45af8e51"
                      "30c81c46a35ce411e5fbc1191a0a52ef"
                      "f69f2445df4f9b17ad2b417be66c3710")


def buf(data):
    return C.create_string_buffer(data, max(1, len(data)))


class AESTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        library = pathlib.Path(cls.temp.name) / "aes.so"
        subprocess.run(["cc", "-std=gnu11", "-shared", "-fPIC", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-Wno-deprecated-declarations", "-I", str(HOST), "-I", str(PORT / "vendor/rpiplay/lib"),
                        str(PORT / "src/ios_aes.c"), str(PORT / "vendor/rpiplay/lib/mirror_buffer.c"),
                        str(HOST / "backend.c"), "-Wl,--wrap=calloc", "-lcrypto", "-o", str(library)],
                       check=True, capture_output=True)
        cls.lib = C.CDLL(str(library))
        for mode in ("ctr", "cbc"):
            create = getattr(cls.lib, "airplay_aes_" + mode + "_init")
            create.argtypes = [C.c_void_p, C.c_void_p] + ([C.c_int] if mode == "cbc" else [])
            create.restype = C.c_void_p
            for name in ("encrypt", "decrypt"):
                getattr(cls.lib, "airplay_aes_" + mode + "_" + name).argtypes = [C.c_void_p, C.c_void_p, C.c_void_p, C.c_int]
            getattr(cls.lib, "airplay_aes_" + mode + "_destroy").argtypes = [C.c_void_p]
            getattr(cls.lib, "airplay_aes_" + mode + "_reset").argtypes = [C.c_void_p]
        cls.lib.test_ctr_reference.argtypes = [C.c_void_p] * 4 + [C.c_size_t]
        cls.lib.test_driver_fail.argtypes = [C.c_size_t]
        for name in ("test_allocations", "test_driver_calls", "test_driver_largest"):
            getattr(cls.lib, name).restype = C.c_size_t
        cls.lib.mirror_buffer_init.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p]
        cls.lib.mirror_buffer_init.restype = C.c_void_p
        cls.lib.mirror_buffer_init_aes.argtypes = [C.c_void_p, C.c_uint64]
        cls.lib.mirror_buffer_decrypt.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p, C.c_int]
        cls.lib.mirror_buffer_destroy.argtypes = [C.c_void_p]

    def setUp(self):
        self.lib.test_driver_reset()

    def context(self, mode="ctr", iv=CTR_IV, direction=1):
        create = getattr(self.lib, "airplay_aes_" + mode + "_init")
        h = create(buf(KEY), buf(iv), *([direction] if mode == "cbc" else []))
        self.assertTrue(h)
        self.addCleanup(getattr(self.lib, "airplay_aes_" + mode + "_destroy"), h)
        return h

    def reference(self, data, key=KEY, iv=CTR_IV):
        out = C.create_string_buffer(len(data))
        self.assertEqual(self.lib.test_ctr_reference(buf(key), buf(iv), buf(data), out, len(data)), 0)
        return out.raw

    def test_nist_ctr_vector_and_empty_input(self):
        expected = bytes.fromhex("874d6191b620e3261bef6864990db6ce"
                                 "9806f66b7970fdff8617187bb9fffdff"
                                 "5ae4df3edbd5d35e5b4f09020db03eab"
                                 "1e031dda2fbe03d1792170a0f3009cee")
        h, out = self.context(), C.create_string_buffer(len(PLAIN))
        self.assertEqual(self.lib.airplay_aes_ctr_encrypt(h, None, None, 0), 0)
        self.assertEqual(self.lib.airplay_aes_ctr_encrypt(h, buf(PLAIN), out, len(PLAIN)), 0)
        self.assertEqual(out.raw, expected)

    def test_large_unaligned_in_place_packets_do_not_allocate(self):
        data = bytes(range(256)) * 1024 + b"tail"
        expected = self.reference(data)
        h, storage = self.context(), C.create_string_buffer(len(data) + 1)
        C.memmove(C.addressof(storage) + 1, data, len(data))
        allocated = self.lib.test_allocations()
        self.assertEqual(self.lib.airplay_aes_ctr_encrypt(h, C.byref(storage, 1), C.byref(storage, 1), len(data)), 0)
        self.assertEqual(storage.raw[1:], expected)
        self.assertEqual(self.lib.test_allocations(), allocated)
        self.assertEqual(self.lib.test_driver_largest(), 128)

    def test_ctr_partial_blocks_cross_packet_boundaries(self):
        data = bytes(range(256)) * 12
        expected, actual, pos = self.reference(data), bytearray(), 0
        h = self.context()
        for count in (1, 2, 5, 15, 16, 17, 127, 128, 129, 1025, len(data)):
            part = data[pos:pos + count]
            if not part:
                break
            out = C.create_string_buffer(len(part))
            self.assertEqual(self.lib.airplay_aes_ctr_encrypt(h, buf(part), out, len(part)), 0)
            actual.extend(out.raw)
            pos += len(part)
        self.assertEqual(bytes(actual), expected)
        self.assertLessEqual(self.lib.test_driver_largest(), 128)

    def test_failure_wipes_output_and_invalidates_counter_state(self):
        h, data, out = self.context(), bytes(range(256)), C.create_string_buffer(b"x" * 256, 256)
        self.lib.test_driver_fail(2)
        self.assertEqual(self.lib.airplay_aes_ctr_encrypt(h, buf(data), out, len(data)), -1)
        self.assertEqual(out.raw, bytes(256))
        calls = self.lib.test_driver_calls()
        self.lib.test_driver_fail(0)
        self.assertEqual(self.lib.airplay_aes_ctr_encrypt(h, buf(data), out, len(data)), -1)
        self.assertEqual(self.lib.test_driver_calls(), calls)
        self.lib.airplay_aes_ctr_reset(h)
        self.assertEqual(self.lib.airplay_aes_ctr_encrypt(h, buf(data), out, len(data)), 0)
        self.assertEqual(out.raw, self.reference(data))

    def test_nist_cbc_and_chunked_round_trip(self):
        iv = bytes(range(16))
        expected = bytes.fromhex("7649abac8119b246cee98e9b12e9197d"
                                 "5086cb9b507219ee95db113a917678b2"
                                 "73bed6b8e3c1743b7116e69e22229516"
                                 "3ff1caa1681fac09120eca307586e1a7")
        h, out = self.context("cbc", iv), C.create_string_buffer(64)
        self.lib.airplay_aes_cbc_encrypt(h, buf(PLAIN), out, len(PLAIN))
        self.assertEqual(out.raw, expected)
        data = PLAIN * 64
        encrypted, decrypted = C.create_string_buffer(len(data)), C.create_string_buffer(len(data))
        self.lib.airplay_aes_cbc_reset(h)
        self.lib.airplay_aes_cbc_encrypt(h, buf(data), encrypted, len(data))
        self.lib.airplay_aes_cbc_decrypt(self.context("cbc", iv, 0), encrypted, decrypted, len(data))
        self.assertEqual(decrypted.raw, data)
        self.assertLessEqual(self.lib.test_driver_largest(), 128)

    def test_mirror_key_derivation_and_short_packets(self):
        secret, stream_id = bytes(range(32)), 123456789
        h = self.lib.mirror_buffer_init(None, buf(KEY), buf(secret))
        self.assertTrue(h)
        self.addCleanup(self.lib.mirror_buffer_destroy, h)
        self.lib.mirror_buffer_init_aes(h, stream_id)
        base = hashlib.sha512(KEY + secret).digest()[:16]
        key = hashlib.sha512(b"AirPlayStreamKey" + str(stream_id).encode() + base).digest()[:16]
        iv = hashlib.sha512(b"AirPlayStreamIV" + str(stream_id).encode() + base).digest()[:16]
        data = bytes(range(256)) * 8
        encrypted = self.reference(data, key, iv)
        actual, pos = bytearray(), 0
        allocations = self.lib.test_allocations()
        for count in (1, 1, 2, 5, 15, 3, 127, 129, len(data)):
            part = encrypted[pos:pos + count]
            out = C.create_string_buffer(len(part))
            self.assertEqual(self.lib.mirror_buffer_decrypt(h, buf(part), out, len(part)), 0)
            actual.extend(out.raw)
            pos += len(part)
        self.assertEqual(bytes(actual), data)
        self.assertEqual(self.lib.test_allocations(), allocations)
        self.lib.test_driver_fail(self.lib.test_driver_calls() + 1)
        self.assertEqual(self.lib.mirror_buffer_decrypt(h, buf(b"abcd"), C.create_string_buffer(4), 4), -1)

    def test_key_allocation_failure_is_reported_without_crash(self):
        self.lib.test_calloc_fail(1)
        self.assertFalse(self.lib.airplay_aes_ctr_init(buf(KEY), buf(CTR_IV)))
        self.lib.test_calloc_fail(0)
        h = self.lib.mirror_buffer_init(None, buf(KEY), buf(bytes(32)))
        self.assertTrue(h)
        self.addCleanup(self.lib.mirror_buffer_destroy, h)
        self.lib.test_calloc_fail(1)
        self.lib.mirror_buffer_init_aes(h, 1)
        self.lib.test_calloc_fail(0)
        self.assertEqual(self.lib.mirror_buffer_decrypt(h, buf(b"abcd"), C.create_string_buffer(4), 4), -1)


if __name__ == "__main__":
    unittest.main()
