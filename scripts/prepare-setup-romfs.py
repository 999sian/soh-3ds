#!/usr/bin/env python3
"""Stage the allowlisted PC-free setup RomFS tree.

Only Torch YAML metadata, the generated ROM lookup table, and the matching SoH
support archive are admitted.  The support archive is rewritten with stored
entries when necessary because deflate allocations have failed on 3DS at run
time. Installer payloads are gzip-compressed in RomFS and streamed onto SD
only during first-run setup.
"""

from __future__ import annotations

import argparse
import gzip
import tarfile
import os
import re
import shutil
import sys
import tempfile
import zipfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath


HASH_LINE = re.compile(r"^([0-9a-fA-F]{40}):\s*(?:#.*)?$")
FIELD_LINE = re.compile(r"^(\s*)(name|path|binary):\s*(.*?)\s*$")
ALLOWED_OUTPUTS = {"oot.o2r", "oot-mq.o2r"}
COPY_BUFFER_SIZE = 1024 * 1024


@dataclass(frozen=True)
class RomDefinition:
    sha1: str
    name: str
    metadata_path: str
    output: str


def _plain_yaml_scalar(value: str, field: str) -> str:
    value = value.strip()
    if not value:
        raise ValueError(f"config.yml has an empty {field}")
    if value[0] in "'\"":
        if len(value) < 2 or value[-1] != value[0]:
            raise ValueError(f"config.yml has an unterminated quoted {field}")
        value = value[1:-1]
    if "\t" in value or "\n" in value or "\r" in value:
        raise ValueError(f"config.yml {field} cannot contain tabs or newlines")
    return value


def parse_config(config_path: Path) -> list[RomDefinition]:
    """Read the small top-level subset needed for roms.tsv.

    The shipped config uses 40-hex SHA-1 mapping keys and plain scalar
    name/path/output fields.  Rejecting ambiguity here keeps the staging command
    independent of a host PyYAML installation.
    """
    definitions: list[RomDefinition] = []
    current_hash: str | None = None
    current: dict[str, str] = {}

    def finish() -> None:
        nonlocal current_hash, current
        if current_hash is None:
            return
        missing = [field for field in ("name", "path", "binary") if field not in current]
        if missing:
            raise ValueError(f"config.yml entry {current_hash} is missing {', '.join(missing)}")
        output = current["binary"]
        if output not in ALLOWED_OUTPUTS:
            raise ValueError(f"config.yml entry {current_hash} has unsafe output {output!r}")
        definitions.append(
            RomDefinition(current_hash.lower(), current["name"], current["path"], output)
        )
        current_hash = None
        current = {}

    with config_path.open("r", encoding="utf-8") as config:
        for line_number, raw_line in enumerate(config, 1):
            line = raw_line.rstrip("\n\r")
            hash_match = HASH_LINE.fullmatch(line)
            if hash_match:
                finish()
                current_hash = hash_match.group(1)
                continue
            if current_hash is None or not line.strip() or line.lstrip().startswith("#"):
                continue
            field_match = FIELD_LINE.fullmatch(line)
            if not field_match:
                continue
            indentation, field, raw_value = field_match.groups()
            expected_indent = 2 if field in {"name", "path"} else 6
            if len(indentation) != expected_indent:
                continue
            if field in current:
                raise ValueError(
                    f"config.yml entry {current_hash} repeats {field} at line {line_number}"
                )
            current[field] = _plain_yaml_scalar(raw_value, field)
    finish()
    if not definitions:
        raise ValueError("config.yml contains no supported ROM definitions")
    return definitions


def _is_on_device_supported(definition: RomDefinition) -> bool:
    # The debug dumps are 54/64 MiB. Measured low-memory extraction of a 32 MiB
    # retail ROM already reaches 73.1 MiB live allocation, so these definitions
    # stay available to host Torch but are excluded from the 124 MiB 3DS setup.
    return not definition.metadata_path.endswith("_dbg")


def _validate_metadata(
    metadata_dir: Path,
    definitions: list[RomDefinition],
    excluded_paths: set[str],
) -> list[Path]:
    yaml_files: list[Path] = []
    for candidate in sorted(metadata_dir.rglob("*.yml")):
        if candidate.is_symlink():
            raise ValueError(f"metadata symlinks are not allowed: {candidate}")
        if candidate.is_file():
            relative = candidate.relative_to(metadata_dir)
            if any(relative == Path(path) or Path(path) in relative.parents for path in excluded_paths):
                continue
            yaml_files.append(candidate)
    if metadata_dir / "config.yml" not in yaml_files:
        raise ValueError(f"required metadata file is missing: {metadata_dir / 'config.yml'}")

    for definition in definitions:
        relative = PurePosixPath(definition.metadata_path)
        if not re.fullmatch(r"[a-z0-9_-]{1,64}", definition.metadata_path):
            raise ValueError(
                f"config.yml entry {definition.sha1} has unsafe metadata path "
                f"{definition.metadata_path!r}"
            )
        variant_dir = metadata_dir.joinpath(*relative.parts)
        if not variant_dir.is_dir() or not any(
            file == variant_dir or variant_dir in file.parents for file in yaml_files
        ):
            raise ValueError(f"metadata for config path {definition.metadata_path!r} is missing")
    return yaml_files


def _expected_port_version(version: str) -> bytes:
    parts = version.split(".")
    if len(parts) != 3:
        raise ValueError(f"expected version must be major.minor.patch, got {version!r}")
    try:
        numbers = [int(part) for part in parts]
    except ValueError as error:
        raise ValueError(f"expected version must be numeric, got {version!r}") from error
    if any(number < 0 or number > 0xFFFF for number in numbers):
        raise ValueError(f"expected version component is out of range: {version!r}")
    return b"\x01" + b"".join(number.to_bytes(2, "big") for number in numbers)


def _safe_archive_name(name: str) -> bool:
    path = PurePosixPath(name)
    return bool(name) and not path.is_absolute() and ".." not in path.parts and "\\" not in name


def stage_support_archive(source: Path, destination: Path, expected_version: str) -> None:
    if not source.is_file():
        raise ValueError(f"support archive is missing: {source}")
    wanted_port_version = _expected_port_version(expected_version)
    try:
        with zipfile.ZipFile(source, "r") as input_archive:
            entries = input_archive.infolist()
            if not entries:
                raise ValueError("support archive is empty")
            if any(not _safe_archive_name(entry.filename) for entry in entries):
                raise ValueError("support archive contains an unsafe path")
            version_entries = [entry for entry in entries if entry.filename == "portVersion"]
            if len(version_entries) != 1:
                raise ValueError("support archive must contain exactly one portVersion entry")
            with input_archive.open(version_entries[0], "r") as version_file:
                actual_port_version = version_file.read()
            if actual_port_version != wanted_port_version:
                raise ValueError(
                    "support archive portVersion does not match "
                    f"{expected_version}: {actual_port_version.hex()}"
                )

            with zipfile.ZipFile(destination, "w", zipfile.ZIP_STORED, allowZip64=True) as output_archive:
                output_archive.comment = input_archive.comment
                for entry in entries:
                    staged_entry = zipfile.ZipInfo(entry.filename, entry.date_time)
                    staged_entry.compress_type = zipfile.ZIP_STORED
                    staged_entry.comment = entry.comment
                    staged_entry.extra = entry.extra
                    staged_entry.create_system = entry.create_system
                    staged_entry.external_attr = entry.external_attr
                    staged_entry.internal_attr = entry.internal_attr
                    with input_archive.open(entry, "r") as input_file:
                        with output_archive.open(staged_entry, "w", force_zip64=True) as output_file:
                            shutil.copyfileobj(input_file, output_file, COPY_BUFFER_SIZE)
    except zipfile.BadZipFile as error:
        raise ValueError(f"support archive is not a valid ZIP: {source}") from error


def _replace_directory(staged: Path, output: Path) -> None:
    backup = output.with_name(f".{output.name}.previous-{os.getpid()}")
    if backup.exists():
        shutil.rmtree(backup)
    had_output = output.exists()
    if had_output:
        output.rename(backup)
    try:
        staged.rename(output)
    except BaseException:
        if had_output and backup.exists() and not output.exists():
            backup.rename(output)
        raise
    if backup.exists():
        shutil.rmtree(backup)


def prepare(metadata_dir: Path, support_archive: Path, output_dir: Path, expected_version: str) -> None:
    metadata_dir = metadata_dir.resolve()
    support_archive = support_archive.resolve()
    output_dir = output_dir.resolve()
    if not metadata_dir.is_dir():
        raise ValueError(f"metadata directory is missing: {metadata_dir}")
    if output_dir == metadata_dir or metadata_dir in output_dir.parents:
        raise ValueError("output directory cannot be inside the metadata directory")

    all_definitions = parse_config(metadata_dir / "config.yml")
    definitions = [definition for definition in all_definitions if _is_on_device_supported(definition)]
    if not definitions:
        raise ValueError("config.yml contains no ROM definitions supported by on-device setup")
    excluded_paths = {
        definition.metadata_path for definition in all_definitions if not _is_on_device_supported(definition)
    }
    yaml_files = _validate_metadata(metadata_dir, definitions, excluded_paths)
    output_dir.parent.mkdir(parents=True, exist_ok=True)
    staged = Path(tempfile.mkdtemp(prefix=f".{output_dir.name}.staging-", dir=output_dir.parent))
    try:
        torch_dir = staged / "torch"
        torch_dir.mkdir()
        for variant in sorted({definition.metadata_path for definition in definitions}):
            selected = [metadata_dir / "config.yml"] + [
                source for source in yaml_files if source.relative_to(metadata_dir).parts[0] == variant
            ]
            if len(selected) > 4096:
                raise ValueError(f"too many metadata files in {variant}")
            tar_size = 0
            with (torch_dir / f"{variant}.tar.gz").open("wb") as packed:
                with gzip.GzipFile(filename="", mode="wb", fileobj=packed, mtime=0, compresslevel=9) as compressed:
                    with tarfile.open(fileobj=compressed, mode="w|", format=tarfile.USTAR_FORMAT) as archive:
                        for source in selected:
                            name = source.relative_to(metadata_dir).as_posix()
                            if len(name) > 255 or len(PurePosixPath(name).parts) > 16 or any(
                                part in {"", ".", ".."} for part in name.split("/")
                            ) or any(ord(c) < 32 or ord(c) >= 127 or c in "\\:" for c in name):
                                raise ValueError(f"unsupported metadata path: {name}")
                            entry = tarfile.TarInfo(name)
                            entry.size = source.stat().st_size
                            entry.mode = 0o644
                            if entry.size > 1024 * 1024:
                                raise ValueError(f"metadata file exceeds runtime limit: {name}")
                            tar_size += 512 + ((entry.size + 511) // 512) * 512
                            with source.open("rb") as data:
                                archive.addfile(entry, data)
            if ((tar_size + 1024 + 10239) // 10240) * 10240 > 16 * 1024 * 1024:
                raise ValueError(f"metadata bundle exceeds runtime limit: {variant}")

        with (staged / "roms.tsv").open("w", encoding="utf-8", newline="\n") as manifest:
            for definition in definitions:
                manifest.write(f"{definition.sha1}\t{definition.output}\t{definition.name}\t{definition.metadata_path}\n")
        stored_support = staged / "soh.o2r"
        stage_support_archive(support_archive, stored_support, expected_version)
        if stored_support.stat().st_size > 32 * 1024 * 1024:
            raise ValueError("support archive exceeds runtime decompression limit")
        with stored_support.open("rb") as data, (staged / "soh.o2r.gz").open("wb") as packed:
            with gzip.GzipFile(filename="", mode="wb", fileobj=packed, mtime=0, compresslevel=9) as compressed:
                shutil.copyfileobj(data, compressed, COPY_BUFFER_SIZE)
        stored_support.unlink()
        _replace_directory(staged, output_dir)
    finally:
        if staged.exists():
            shutil.rmtree(staged)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--metadata-dir", type=Path, required=True)
    parser.add_argument("--support-archive", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--expected-version", required=True)
    args = parser.parse_args(argv)
    try:
        prepare(args.metadata_dir, args.support_archive, args.output_dir, args.expected_version)
    except (OSError, ValueError) as error:
        print(f"prepare-setup-romfs: {error}", file=sys.stderr)
        return 1
    print(f"prepared setup RomFS: {args.output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
