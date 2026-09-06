#!/usr/bin/env python3
"""Exercise the production streaming decoder with valid and damaged bundles."""
import gzip
import io
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def tar_bytes(entries):
    output = io.BytesIO()
    with tarfile.open(fileobj=output, mode="w", format=tarfile.USTAR_FORMAT) as archive:
        for name, data, kind in entries:
            member = tarfile.TarInfo(name)
            member.type = kind
            member.size = len(data)
            archive.addfile(member, io.BytesIO(data))
    return output.getvalue()


VALID = [("config.yml", b"config: test\n", tarfile.REGTYPE),
         ("ntsc_test/scenes/test.yml", b"assets: []\n" * 40000, tarfile.REGTYPE)]


class SetupBundleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="soh-bundle-build-")
        cls.binary = Path(cls.build.name) / "bundle"
        subprocess.run(["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "src"),
                        str(ROOT / "tests/setup_bundle_cli.cpp"), str(ROOT / "src/setup/setup_bundle.cpp"),
                        "-lz", "-o", str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="soh-bundle-")
        self.root = Path(self.temp.name)
        self.source = self.root / "source.gz"
        self.dest = self.root / "output"

    def tearDown(self):
        self.temp.cleanup()

    def run_decoder(self, data, mode="metadata", argument="ntsc_test", cancel=False):
        self.source.write_bytes(data)
        return subprocess.run([str(self.binary), mode, str(self.source), str(self.dest), argument,
                               "cancel" if cancel else "run"], capture_output=True, text=True)

    def test_selected_metadata_roundtrip(self):
        result = self.run_decoder(gzip.compress(tar_bytes(VALID)))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual({p.relative_to(self.dest).as_posix() for p in self.dest.rglob("*") if p.is_file()},
                         {row[0] for row in VALID})
        for name, data, _ in VALID:
            self.assertEqual((self.dest / name).read_bytes(), data)

    def test_support_roundtrip_and_size_limit(self):
        payload = bytes(range(256)) * 4096
        compressed = gzip.compress(payload)
        result = self.run_decoder(compressed, "support", str(len(payload)))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.dest.read_bytes(), payload)
        result = self.run_decoder(compressed, "support", str(len(payload) - 1))
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.dest.read_bytes(), payload)
        self.assertFalse(Path(str(self.dest) + ".part").exists())

    def test_rejects_bad_gzip_without_leaving_staged_files(self):
        compressed = gzip.compress(tar_bytes(VALID))
        crc = bytearray(compressed)
        crc[-8] ^= 1
        for data in [compressed[:-1], compressed[:len(compressed)//2], bytes(crc), b"plain bytes",
                     compressed + b"garbage", compressed + gzip.compress(b"more")]:
            for mode, arg in [("metadata", "ntsc_test"), ("support", "16777216")]:
                with self.subTest(mode=mode, size=len(data)):
                    result = self.run_decoder(data, mode, arg)
                    self.assertNotEqual(result.returncode, 0, result.stdout)
                    self.assertFalse(self.dest.exists())
                    self.assertFalse(Path(str(self.dest) + ".part").exists())

    def test_rejects_unsafe_paths_types_duplicates_and_missing_config(self):
        for name in ["../escape.yml", "/absolute.yml", "ntsc_test/../../escape.yml",
                     "ntsc_test/./x.yml", "ntsc_test//x.yml", "ntsc_test/evil\\name.yml",
                     "ntsc_test/c:/x.yml", "other/assets.yml", "ntsc_test/rom.z64"]:
            with self.subTest(name=name):
                result = self.run_decoder(gzip.compress(tar_bytes(VALID[:1] + [(name, b"x", tarfile.REGTYPE)])))
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(self.dest.exists())
        for entries in [VALID + VALID, VALID[1:], VALID[:1],
                        VALID + [("ntsc_test/link.yml", b"", tarfile.SYMTYPE)],
                        VALID + [("ntsc_test/hard.yml", b"", tarfile.LNKTYPE)]]:
            result = self.run_decoder(gzip.compress(tar_bytes(entries)))
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(self.dest.exists())

    def test_tar_integrity_and_limits(self):
        valid = tar_bytes(VALID)
        checksum = bytearray(valid)
        checksum[10] ^= 1
        no_end = valid.rstrip(b"\0")
        trailing = bytearray(valid)
        trailing[-1] = 1
        oversized = tar_bytes([VALID[0], ("ntsc_test/huge.yml", b"x" * (1024*1024 + 1), tarfile.REGTYPE)])
        too_many = tar_bytes([VALID[0]] + [(f"ntsc_test/{i}.yml", b"x", tarfile.REGTYPE) for i in range(4096)])
        too_large = tar_bytes([VALID[0]] + [(f"ntsc_test/{i}.yml", b"x" * 1024*1024, tarfile.REGTYPE) for i in range(17)])
        for data in [bytes(checksum), no_end, bytes(trailing), oversized, too_many, too_large]:
            result = self.run_decoder(gzip.compress(data))
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(self.dest.exists())

    def test_cancellation_cleans_only_owned_output(self):
        for mode, arg in [("metadata", "ntsc_test"), ("support", "16777216")]:
            result = self.run_decoder(gzip.compress(tar_bytes(VALID)), mode, arg, cancel=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("cancel", result.stderr.lower())
            self.assertFalse(self.dest.exists())
            self.assertFalse(Path(str(self.dest) + ".part").exists())

    def test_existing_directory_is_preserved(self):
        self.dest.mkdir()
        keep = self.dest / "keep"
        keep.write_bytes(b"existing")
        result = self.run_decoder(gzip.compress(tar_bytes(VALID)))
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(keep.read_bytes(), b"existing")


if __name__ == "__main__":
    unittest.main()
