#!/usr/bin/env python3
"""Behavior tests for the PC-free setup RomFS staging command."""

from __future__ import annotations

import gzip
import io
import tarfile
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
STAGER = REPO_ROOT / "scripts" / "prepare-setup-romfs.py"


CONFIG = """\
1111111111111111111111111111111111111111:
  name: Test Ocarina of Time
  path: ntsc_test
  config:
    output:
      binary: oot.o2r
2222222222222222222222222222222222222222:
  name: Test Master Quest
  path: mq_test
  config:
    output:
      binary: oot-mq.o2r
3333333333333333333333333333333333333333:
  name: Test Debug ROM
  path: pal_gc_dbg
  config:
    output:
      binary: oot.o2r
"""


class SetupRomfsPackagingTest(unittest.TestCase):
    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory()
        self.root = Path(self._temporary.name)
        self.metadata = self.root / "metadata"
        self.output = self.root / "romfs"
        self.support = self.root / "soh.o2r"
        self.metadata.mkdir()

    def tearDown(self) -> None:
        self._temporary.cleanup()

    def write_support_archive(self) -> None:
        with zipfile.ZipFile(self.support, "w", zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("portVersion", b"\x01\x00\x09\x00\x02\x00\x03")
            archive.writestr("assets/support.bin", b"support payload")

    def run_stager(self) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                str(STAGER),
                "--metadata-dir",
                str(self.metadata),
                "--support-archive",
                str(self.support),
                "--output-dir",
                str(self.output),
                "--expected-version",
                "9.2.3",
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

    def test_stages_only_metadata_manifest_and_stored_support_archive(self) -> None:
        # Catches a broad directory copy leaking a ROM/game archive, incomplete
        # metadata traversal, an incorrect ROM manifest, or deflated RomFS O2R.
        (self.metadata / "config.yml").write_text(CONFIG, encoding="utf-8")
        (self.metadata / "ntsc_test").mkdir()
        (self.metadata / "ntsc_test" / "assets.yml").write_text("assets: []\n", encoding="utf-8")
        (self.metadata / "mq_test").mkdir()
        (self.metadata / "mq_test" / "scenes.yml").write_text("scenes: []\n", encoding="utf-8")
        (self.metadata / "pal_gc_dbg").mkdir()
        (self.metadata / "pal_gc_dbg" / "debug.yml").write_text("debug: []\n", encoding="utf-8")
        (self.metadata / "rom.z64").write_bytes(b"user ROM must never ship")
        (self.metadata / "oot.o2r").write_bytes(b"generated game archive must never ship")
        (self.metadata / "notes.txt").write_text("not extraction metadata", encoding="utf-8")
        self.write_support_archive()

        result = self.run_stager()

        self.assertEqual(result.returncode, 0, result.stderr)
        files = {
            path.relative_to(self.output).as_posix()
            for path in self.output.rglob("*")
            if path.is_file()
        }
        self.assertEqual(
            files,
            {
                "roms.tsv",
                "soh.o2r.gz",
                "torch/mq_test.tar.gz",
                "torch/ntsc_test.tar.gz",
            },
        )
        self.assertEqual(
            (self.output / "roms.tsv").read_text(encoding="utf-8"),
            "1111111111111111111111111111111111111111\toot.o2r\tTest Ocarina of Time\tntsc_test\n"
            "2222222222222222222222222222222222222222\toot-mq.o2r\tTest Master Quest\tmq_test\n",
        )
        with zipfile.ZipFile(io.BytesIO(gzip.decompress((self.output / "soh.o2r.gz").read_bytes()))) as archive:
            self.assertEqual(
                [(entry.filename, entry.compress_type) for entry in archive.infolist()],
                [("portVersion", zipfile.ZIP_STORED), ("assets/support.bin", zipfile.ZIP_STORED)],
            )
            self.assertEqual(archive.read("assets/support.bin"), b"support payload")

        for variant, filename in [("ntsc_test", "assets.yml"), ("mq_test", "scenes.yml")]:
            with tarfile.open(self.output / "torch" / (variant + ".tar.gz")) as bundle:
                self.assertEqual(bundle.getnames(), ["config.yml", variant + "/" + filename])
                for entry in bundle:
                    self.assertTrue(entry.isfile())
                    self.assertEqual(bundle.extractfile(entry).read(), (self.metadata / entry.name).read_bytes())
        before = {path.relative_to(self.output): path.read_bytes() for path in self.output.rglob("*") if path.is_file()}
        self.assertEqual(self.run_stager().returncode, 0)
        after = {path.relative_to(self.output): path.read_bytes() for path in self.output.rglob("*") if path.is_file()}
        self.assertEqual(before, after, "packaging must be deterministic")

    def test_missing_variant_metadata_fails_without_replacing_previous_output(self) -> None:
        # Catches a successful package that cannot extract a config-listed ROM.
        (self.metadata / "config.yml").write_text(CONFIG, encoding="utf-8")
        (self.metadata / "ntsc_test").mkdir()
        (self.metadata / "ntsc_test" / "assets.yml").write_text("assets: []\n", encoding="utf-8")
        self.write_support_archive()
        self.output.mkdir()
        (self.output / "keep.txt").write_text("previous good staging", encoding="utf-8")

        result = self.run_stager()

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("mq_test", result.stderr)
        self.assertEqual(
            {path.name for path in self.output.iterdir()},
            {"keep.txt"},
        )


if __name__ == "__main__":
    unittest.main()
