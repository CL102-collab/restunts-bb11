#!/usr/bin/env python3
"""Verify a public TempleOS package against the checked-out port sources."""

import hashlib
from pathlib import Path, PurePosixPath
import re
import runpy
import struct


ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "src/restunts/platform/templeos"
FORMAT = runpy.run_path(str(PORT / "tools/MAKE_DATA_CD.PY"))
PREPARE = runpy.run_path(str(PORT / "tools/PREPARE.PY"))
PREFIX = "RESTUNTS_STOCK_TEMPLEOS"
ISO = "RESTUNTS.ISO"
CHECKSUMS = "SHA256SUMS.TXT"
CONTENTS = "CONTENTS.TXT"
INSTALLER_FILES = ("INSTALL.HC", "PLAY.HC", "VERIFY.HH")
TOOL_FILES = ("MAKE_DATA_CD.PY", "IMPORT.HC", "VERIFY.HH", "LEGACY.HH")
PACKAGE_FILES = frozenset({ISO, "README.MD", "BUILD.TXT", CONTENTS, CHECKSUMS} |
                          {f"DATA_CD_TOOL/{name}" for name in TOOL_FILES})
CHECKSUM_LINE = re.compile(r"([0-9a-f]{64})  ([^\r\n]+)")
DIRECTORY_RECORD = struct.Struct("<H38sqqQ")
DIRECTORY_METADATA_RECORDS = 2  # Self and parent precede the ordinary children.
BOOT_FIELDS = struct.Struct("<5Q")
BOOT_SIGNATURE = struct.Struct("<H")
REDSEA_SIGNATURE_OFFSET = 3
PRIMARY_DESCRIPTOR = b"\x01CD001\x01"
SELF_NAME = "."
PARENT_NAME = ".."


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def disc_sources():
    """Mirror PREPARE.PY's public sources without creating a staging tree."""
    files = {"PORT/" + source.relative_to(PORT).as_posix(): source
             for source in PORT.rglob("*")
             if source.is_file() and source.suffix in PREPARE["SOURCE_EXTENSIONS"] and
             not {"tools", "package"}.intersection(source.relative_to(PORT).parts) and
             source.name != "REPLAY_EXPECTED.HH" and not source.name.startswith("TEST_")}
    files["PORT/README.TXT"] = PORT / "README.TXT"
    files |= {"PORT/CARS/" + name: source for name, source in PREPARE["collect_resources"](
        PREPARE["BUNDLED_CARS"], PREPARE["CAR_EXTENSIONS"]).items()}
    files |= {name: PORT / "package" / name for name in INSTALLER_FILES}
    files["README.MD"] = ROOT / "docs/templeos.md"
    return files


def tool_sources():
    return {"DATA_CD_TOOL/MAKE_DATA_CD.PY": PORT / "tools/MAKE_DATA_CD.PY",
            "DATA_CD_TOOL/LEGACY.HH": PORT / "LEGACY.HH",
            "DATA_CD_TOOL/IMPORT.HC": PORT / "package/IMPORT.HC",
            "DATA_CD_TOOL/VERIFY.HH": PORT / "package/VERIFY.HH",
            "README.MD": ROOT / "docs/templeos.md"}


def checksum_inventory(content, name):
    try:
        text = content.decode("ascii")
    except UnicodeError as error:
        raise ValueError(f"Invalid TempleOS checksum inventory: {name}") from error
    result = {}
    for line in text.splitlines():
        match = CHECKSUM_LINE.fullmatch(line)
        require(match is not None, f"Malformed TempleOS checksum line in {name}")
        digest, path = match.groups()
        require(path not in result, f"Duplicate TempleOS checksum path: {path}")
        result[path] = digest
    return result


def redsea_contents(image):
    """Read contiguous RedSea files, checking bounds, links and directory names.

    The layout is shared with MAKE_DATA_CD.PY and stock TempleOS:
    https://github.com/cia-foundation/TempleOS/blob/archive/Kernel/BlkDev/FileSysRedSea.HC
    """
    sector_bytes = FORMAT["SECTOR_BYTES"]
    cd_sector_bytes = FORMAT["CD_SECTOR_BYTES"]
    boot = FORMAT["REDSEA_OFFSET"] * sector_bytes
    require(len(image) >= boot + sector_bytes and len(image) % cd_sector_bytes == 0,
            "Invalid TempleOS RedSea image size")
    primary = FORMAT["PRIMARY_CD_SECTOR"] * cd_sector_bytes
    require(image[primary:primary + len(PRIMARY_DESCRIPTOR)] == PRIMARY_DESCRIPTOR,
            "Invalid TempleOS RedSea CD descriptor")
    require(image[boot + REDSEA_SIGNATURE_OFFSET] == FORMAT["REDSEA_SIGNATURE"] and
            BOOT_SIGNATURE.unpack_from(image, boot + FORMAT["BOOT_SIGNATURE_OFFSET"])[0] ==
            FORMAT["SECTOR_SIGNATURE"], "Invalid TempleOS RedSea boot signature")
    offset, sectors, root, bitmap, _ = BOOT_FIELDS.unpack_from(
        image, boot + FORMAT["BOOT_FIELDS_OFFSET"])
    require(offset == FORMAT["REDSEA_OFFSET"] and sectors > 0 and bitmap > 0 and
            offset + sectors <= len(image) // sector_bytes,
            "Invalid TempleOS RedSea filesystem bounds")
    first_cluster = offset + bitmap + 1
    last_cluster = offset + sectors
    regions = []

    def region(cluster, size, name):
        require(size >= 0 and first_cluster <= cluster < last_cluster and
                size <= (last_cluster - cluster) * sector_bytes,
                f"Invalid TempleOS RedSea file bounds: {name}")
        return image[cluster * sector_bytes:cluster * sector_bytes + size]

    def record(content, position):
        attributes, raw_name, cluster, size, _ = DIRECTORY_RECORD.unpack_from(content, position)
        try:
            name = raw_name.split(b"\x00", 1)[0].decode("ascii")
        except UnicodeError as error:
            raise ValueError("Invalid TempleOS RedSea directory name") from error
        return attributes, name, cluster, size

    def claim(cluster, size, name):
        region(cluster, size, name)
        blocks = (size + sector_bytes - 1) // sector_bytes
        regions.append((cluster, cluster + blocks, name))

    files = {}
    directories = set()
    visited = set()
    pending = [(root, root, "", None)]
    while pending:
        cluster, parent, path, expected_size = pending.pop()
        require(cluster not in visited, "Cyclic or aliased TempleOS RedSea directory")
        visited.add(cluster)
        header = region(cluster, DIRECTORY_RECORD.size, path)
        attributes, name, self_cluster, size = record(header, 0)
        require(attributes & FORMAT["ATTR_DIRECTORY"] and
                attributes & FORMAT["ATTR_CONTIGUOUS"] and self_cluster == cluster and
                name == (PurePosixPath(path).name if path else SELF_NAME) and
                size > 0 and size % sector_bytes == 0 and
                (expected_size is None or size == expected_size),
                f"Invalid TempleOS RedSea directory header: {path}")
        content = region(cluster, size, path)
        claim(cluster, size, path)
        directories.add(path)
        attributes, name, parent_cluster, _ = record(content, DIRECTORY_RECORD.size)
        require(attributes & FORMAT["ATTR_DIRECTORY"] and name == PARENT_NAME and
                parent_cluster == parent, f"Invalid TempleOS RedSea parent link: {path}")
        names = set()
        terminated = False
        for position in range(DIRECTORY_RECORD.size * DIRECTORY_METADATA_RECORDS,
                              size, DIRECTORY_RECORD.size):
            attributes, name, child_cluster, child_size = record(content, position)
            if not name:
                terminated = True
                break
            require(attributes & FORMAT["ATTR_CONTIGUOUS"] and name not in names and
                    name not in (SELF_NAME, PARENT_NAME) and name == PurePosixPath(name).name and
                    "/" not in name and "\\" not in name and ":" not in name and
                    all(ord(character) >= 32 for character in name),
                    f"Unsafe or duplicate TempleOS RedSea name: {name!r}")
            names.add(name)
            child = path + "/" + name if path else name
            if attributes & FORMAT["ATTR_DIRECTORY"]:
                pending.append((child_cluster, cluster, child, child_size))
            else:
                require(child_size >= 0, f"Invalid TempleOS RedSea file size: {child}")
                if child_size:
                    claim(child_cluster, child_size, child)
                    files[child] = region(child_cluster, child_size, child)
                else:
                    require(child_cluster == FORMAT["INVALID_CLUSTER"],
                            f"Invalid empty TempleOS RedSea file: {child}")
                    files[child] = b""
        require(terminated, f"Unterminated TempleOS RedSea directory: {path}")
    regions.sort()
    for previous, current in zip(regions, regions[1:]):
        require(previous[1] <= current[0],
                f"Overlapping TempleOS RedSea files: {previous[2]}, {current[2]}")
    return files, directories


def validate_package(files):
    require(set(files) == PACKAGE_FILES, "TempleOS distribution file set differs")
    expected_checksums = {name: sha256(content) for name, content in files.items()
                          if name != CHECKSUMS}
    require(checksum_inventory(files[CHECKSUMS], CHECKSUMS) == expected_checksums,
            "TempleOS package checksum inventory differs")
    for name, source in tool_sources().items():
        require(files[name] == source.read_bytes(),
                f"TempleOS package file differs from source: {name}")
    expected = {name: source.read_bytes() for name, source in disc_sources().items()}
    require(checksum_inventory(files[CONTENTS], CONTENTS) ==
            {name: sha256(content) for name, content in expected.items()},
            "TempleOS CD contents inventory differs from source")
    actual, directories = redsea_contents(files[ISO])
    expected_directories = {"", "DATA"} | {
        str(parent) for name in expected for parent in PurePosixPath(name).parents
        if str(parent) != SELF_NAME}
    require(actual.keys() == expected.keys() and directories == expected_directories,
            "TempleOS RedSea CD file set differs from public sources")
    for name, content in expected.items():
        require(actual[name] == content, f"TempleOS RedSea CD file differs from source: {name}")
