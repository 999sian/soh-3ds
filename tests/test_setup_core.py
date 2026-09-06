#!/usr/bin/env python3
"""Build and run the portable setup-core test without the project CMake."""

from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="soh-setup-test-") as build_dir:
        executable = Path(build_dir) / "test_setup_core"
        command = [
            "c++",
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(ROOT / "src"),
            str(ROOT / "tests" / "test_setup_core.cpp"),
            str(ROOT / "src" / "setup" / "setup_core.cpp"),
            str(ROOT / "src" / "setup" / "setup_bundle.cpp"),
            "-lzip",
            "-lz",
            "-o",
            str(executable),
        ]
        subprocess.run(command, check=True, cwd=ROOT)
        subprocess.run([str(executable)], check=True, cwd=ROOT)
        support = ROOT / "third_party" / "shipwright" / "soh.o2r"
        game = Path("/var/tmp/soh-setup-reference/oot.o2r")
        if support.is_file() and game.is_file():
            subprocess.run([str(executable), str(support), str(game)], check=True, cwd=ROOT)


if __name__ == "__main__":
    main()
