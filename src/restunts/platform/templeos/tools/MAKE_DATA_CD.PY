#!/usr/bin/env python3
"""Make a private TempleOS data CD from your own Stunts files; Python 3 only.

The RedSea layout follows the public-domain stock TempleOS sources:
https://templeos.info/Wb/Doc/RedSea.DD.HTML
https://templeos.info/Wb/Adam/Opt/Boot/DskISORedSea.HC.HTML
https://templeos.info/Wb/Kernel/BlkDev/FileSysRedSea.HC.HTML
No game resources, OS files, boot code, or reference ISO are embedded here.
"""

import argparse
from datetime import datetime, timezone
import hashlib
from pathlib import Path
import struct


SECTOR_BYTES = 512
CD_SECTOR_BYTES = 2048
SECTORS_PER_CD_SECTOR = CD_SECTOR_BYTES // SECTOR_BYTES
BITS_PER_BYTE = 8
BITMAP_SECTOR_BITS = SECTOR_BYTES * BITS_PER_BYTE
DIRECTORY_ENTRY_BYTES = 64
DIRECTORY_NAME_BYTES = 38
DIRECTORY_RESERVED_ENTRIES = 3  # Self, parent, terminating empty entry.
ATTR_DIRECTORY = 0x10
ATTR_CONTIGUOUS = 0x800
INVALID_CLUSTER = -1
REDSEA_SIGNATURE = 0x88
SECTOR_SIGNATURE = 0xAA55
REDSEA_BOOT_CD_SECTOR = 22
REDSEA_OFFSET = REDSEA_BOOT_CD_SECTOR * SECTORS_PER_CD_SECTOR
BOOT_FIELDS_OFFSET = 8
BOOT_SIGNATURE_OFFSET = SECTOR_BYTES - 2
PRIMARY_CD_SECTOR = 16
BOOT_RECORD_CD_SECTOR = 17
SUPPLEMENTARY_CD_SECTOR = 18
TERMINATOR_CD_SECTOR = 19
CATALOG_CD_SECTOR = 20
ISO_PRIMARY_TYPE = 1
ISO_BOOT_TYPE = 0
ISO_SUPPLEMENTARY_TYPE = 2
ISO_TERMINATOR_TYPE = 255
ISO_VOLUME_SIZE_OFFSET = 80
ISO_VOLUME_SET_OFFSET = 120
ISO_VOLUME_SEQUENCE_OFFSET = 124
ISO_BLOCK_SIZE_OFFSET = 128
# These are TempleOS's packed CISOPriDesc offsets, not generic ISO9660 offsets.
ISO_ROOT_OFFSET = 152
ISO_PUBLISHER_OFFSET = 314
ISO_FILE_VERSION_OFFSET = 877
ISO_CATALOG_OFFSET = 0x47
CATALOG_ID_OFFSET = 4
CATALOG_CHECKSUM_OFFSET = 28
CATALOG_SIGNATURE_OFFSET = 30
CATALOG_VALIDATION_BYTES = 32
U16_MASK = 0xFFFF
DATE_TIME_BITS = 32
SECONDS_PER_DAY = 86400
FILENAME_BITMAP = (0, 0x03FF73FB, 0xEFFFFFFF, 0x2FFFFFFF)
FILENAME_BITMAP_BITS = 32
HELPER_NAMES = ("IMPORT.HC", "VERIFY.HH", "LEGACY.HH")
# Keep this whitelist identical to tools/PREPARE.PY.
DATA_EXTENSIONS = {
    ".RES", ".PRE", ".PVS", ".VSH", ".XVS", ".PES", ".ESH", ".3SH", ".P3S",
    ".TRK", ".RPL", ".HIG", ".CFG", ".DAT", ".DRV", ".VCE", ".SFX",
    ".KMS", ".FNT", ".PLB",
}


def round_up(value, alignment):
    return (value + alignment - 1) // alignment * alignment


def check_name(name):
    if not name.isascii() or not 0 < len(name) < DIRECTORY_NAME_BYTES:
        raise ValueError("TempleOS filenames must contain 1–37 ASCII characters: " + name)
    for character in name:
        value = ord(character)
        if not FILENAME_BITMAP[value // FILENAME_BITMAP_BITS] & (
                1 << (value % FILENAME_BITMAP_BITS)):
            raise ValueError("Unsupported TempleOS filename: " + name)


def temple_date(timestamp):
    """CDate is days since AD0 in the high word, fraction of a day in the low."""
    date = datetime.fromtimestamp(timestamp, timezone.utc)
    previous_year = date.year - 1
    days = (date.year * 365 + previous_year // 4 - previous_year // 100 +
            previous_year // 400 - previous_year // 4000 + date.timetuple().tm_yday - 1)
    seconds = date.hour * 3600 + date.minute * 60 + date.second
    return (days << DATE_TIME_BITS) | (seconds << DATE_TIME_BITS) // SECONDS_PER_DAY


def read_entry(name, source):
    check_name(name)
    return {"name": name, "data": source.read_bytes(), "date": temple_date(source.stat().st_mtime)}


def helper_file(name):
    """Public ZIP keeps helpers beside this script; repository sources also work."""
    folder = Path(__file__).resolve().parent
    candidates = [folder / name]
    port = folder.parent
    candidates.append(port / ("LEGACY.HH" if name == "LEGACY.HH" else "package/" + name))
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise ValueError("Missing helper " + name + "; keep the public package files together")


def collect_data(data):
    assets = {}
    for source in sorted(data.iterdir()):
        if source.is_file() and source.suffix.upper() in DATA_EXTENSIONS:
            name = source.name.upper()
            if name in assets:
                raise ValueError("Files have the same uppercase name: " + name)
            assets[name] = read_entry(name, source)
    if not any(name in assets for name in ("MISC.RES", "MISC.PRE")):
        raise ValueError("The game folder must contain MISC.RES or MISC.PRE")
    car_directories = [source for source in data.iterdir()
                       if source.is_dir() and source.name.upper() == "CARS"]
    if len(car_directories) > 1:
        raise ValueError("Directories have the same uppercase name: CARS")
    if car_directories:
        cars = {}
        for source in sorted(car_directories[0].iterdir()):
            if source.is_file() and source.suffix.upper() in DATA_EXTENSIONS:
                name = source.name.upper()
                if name in cars:
                    raise ValueError("Files have the same uppercase name: " + name)
                cars[name] = read_entry(name, source)
        if cars:
            assets["CARS"] = {"name": "CARS", "children": [cars[name] for name in sorted(cars)]}
    return [assets[name] for name in sorted(assets)]


def directory_record(name, cluster, size, date, directory=False):
    attributes = ATTR_CONTIGUOUS | (ATTR_DIRECTORY if directory else 0)
    return struct.pack("<H38sqqQ", attributes, name.encode("ascii"), cluster, size, date)


def put(image, offset, value):
    image[offset:offset + len(value)] = value


def palindrome(buffer, offset, value, width):
    put(buffer, offset, value.to_bytes(width, "little") + value.to_bytes(width, "big"))


def volume_descriptors(image, root_cluster):
    """Stock ISOInit recognizes the native publisher and the boot-record count."""
    primary = bytearray(CD_SECTOR_BYTES)
    put(primary, 0, bytes((ISO_PRIMARY_TYPE,)) + b"CD001\x01")
    palindrome(primary, ISO_VOLUME_SIZE_OFFSET, len(image) // CD_SECTOR_BYTES, 4)
    palindrome(primary, ISO_VOLUME_SET_OFFSET, 1, 2)
    palindrome(primary, ISO_VOLUME_SEQUENCE_OFFSET, 1, 2)
    palindrome(primary, ISO_BLOCK_SIZE_OFFSET, CD_SECTOR_BYTES, 2)
    palindrome(primary, ISO_ROOT_OFFSET, root_cluster, 4)
    put(primary, ISO_PUBLISHER_OFFSET, b"TempleOS RedSea\x00")
    primary[ISO_FILE_VERSION_OFFSET] = 1
    put(image, PRIMARY_CD_SECTOR * CD_SECTOR_BYTES, primary)
    primary[0] = ISO_SUPPLEMENTARY_TYPE
    put(image, SUPPLEMENTARY_CD_SECTOR * CD_SECTOR_BYTES, primary)
    boot = bytearray(CD_SECTOR_BYTES)
    put(boot, 0, bytes((ISO_BOOT_TYPE,)) + b"CD001\x01EL TORITO SPECIFICATION")
    struct.pack_into("<I", boot, ISO_CATALOG_OFFSET, CATALOG_CD_SECTOR)
    put(image, BOOT_RECORD_CD_SECTOR * CD_SECTOR_BYTES, boot)
    put(image, TERMINATOR_CD_SECTOR * CD_SECTOR_BYTES,
        bytes((ISO_TERMINATOR_TYPE,)) + b"CD001\x01")
    catalog = bytearray(CD_SECTOR_BYTES)
    catalog[0] = 1
    put(catalog, CATALOG_ID_OFFSET, b"TempleOS")
    struct.pack_into("<H", catalog, CATALOG_SIGNATURE_OFFSET, SECTOR_SIGNATURE)
    checksum = -sum(struct.unpack_from("<16H", catalog)) & U16_MASK
    struct.pack_into("<H", catalog, CATALOG_CHECKSUM_OFFSET, checksum)
    # Initial/default entry is all zero: explicitly nonbootable, with no OS boot code.
    put(image, CATALOG_CD_SECTOR * CD_SECTOR_BYTES, catalog)


def build_image(helpers, assets):
    root = {"name": ".", "children": helpers + [{"name": "DATA", "children": assets}]}
    directories = []
    entries = []

    def collect(directory, parent, prefix):
        directory["parent"] = parent
        directory["size"] = round_up((len(directory["children"]) + DIRECTORY_RESERVED_ENTRIES) *
                                      DIRECTORY_ENTRY_BYTES, SECTOR_BYTES)
        directories.append(directory)
        for entry in directory["children"]:
            entry["path"] = prefix + entry["name"]
            if "children" in entry:
                collect(entry, directory, entry["path"] + "/")
            else:
                entries.append(entry)

    collect(root, root, "")
    payload_sectors = sum(directory["size"] // SECTOR_BYTES for directory in directories)
    payload_sectors += sum(round_up(len(entry["data"]), SECTOR_BYTES) // SECTOR_BYTES
                           for entry in entries)
    bitmap_sectors = 1
    while True:
        total_sectors = round_up(REDSEA_OFFSET + 1 + bitmap_sectors + payload_sectors,
                                 SECTORS_PER_CD_SECTOR)
        filesystem_sectors = total_sectors - REDSEA_OFFSET
        required = round_up(filesystem_sectors, BITMAP_SECTOR_BITS) // BITMAP_SECTOR_BITS
        if required == bitmap_sectors:
            break
        bitmap_sectors = required
    image = bytearray(total_sectors * SECTOR_BYTES)
    root_cluster = REDSEA_OFFSET + 1 + bitmap_sectors
    next_cluster = root_cluster
    for directory in directories:
        directory["cluster"] = next_cluster
        next_cluster += directory["size"] // SECTOR_BYTES
    fingerprint = hashlib.sha256()
    for entry in entries:
        content = entry["data"]
        entry["cluster"] = next_cluster if content else INVALID_CLUSTER
        put(image, next_cluster * SECTOR_BYTES, content)
        next_cluster += round_up(len(content), SECTOR_BYTES) // SECTOR_BYTES
        fingerprint.update(entry["path"].encode("ascii") + b"\x00" + content)
    date = max(entry["date"] for entry in entries)
    for directory in directories:
        records = [directory_record(directory["name"], directory["cluster"],
                                    directory["size"], date, True),
                   directory_record("..", directory["parent"]["cluster"], 0, date, True)]
        for entry in directory["children"]:
            is_directory = "children" in entry
            records.append(directory_record(entry["name"], entry["cluster"],
                                            entry["size"] if is_directory else len(entry["data"]),
                                            date if is_directory else entry["date"], is_directory))
        put(image, directory["cluster"] * SECTOR_BYTES, b"".join(records))
    boot_offset = REDSEA_OFFSET * SECTOR_BYTES
    image[boot_offset + 3] = REDSEA_SIGNATURE
    struct.pack_into("<5Q", image, boot_offset + BOOT_FIELDS_OFFSET,
                     REDSEA_OFFSET, filesystem_sectors, root_cluster, bitmap_sectors,
                     int.from_bytes(fingerprint.digest()[:8], "little"))
    struct.pack_into("<H", image, boot_offset + BOOT_SIGNATURE_OFFSET, SECTOR_SIGNATURE)
    # Stock data_area is offset+bitmap_sectors, overlapping the final bitmap sector.
    # RedSeaFmt reserves bitmap bit0, then allocates the root starting at bit1.
    used_bits = next_cluster - (REDSEA_OFFSET + bitmap_sectors)
    bitmap_offset = (REDSEA_OFFSET + 1) * SECTOR_BYTES
    full_bytes, remaining_bits = divmod(used_bits, BITS_PER_BYTE)
    put(image, bitmap_offset, b"\xff" * full_bytes)
    if remaining_bits:
        image[bitmap_offset + full_bytes] = (1 << remaining_bits) - 1
    volume_descriptors(image, root_cluster)
    return image


def make_data_cd(data, output):
    data = data.resolve(strict=True)
    output = output.resolve()
    if not data.is_dir():
        raise ValueError("--data must name your original Stunts game folder")
    if output.exists():
        raise ValueError("Choose a new ISO filename; existing files are never overwritten")
    if output == data or data in output.parents:
        raise ValueError("The output ISO must be outside the original game folder")
    assets = collect_data(data)
    helpers = [read_entry(name, helper_file(name)) for name in HELPER_NAMES]
    image = build_image(helpers, assets)
    with output.open("xb") as stream:
        stream.write(image)
    print("Created private RedSea data CD: " + str(output))
    file_count = sum(len(entry["children"]) if "children" in entry else 1 for entry in assets)
    print(f"{file_count} game files; {len(image)} bytes; "
          f"SHA256 {hashlib.sha256(image).hexdigest()}")
    print("Insert it in the running TempleOS VM, then use DskChg('T'); Cd(\"T:/\");")
    print('Run #include "IMPORT.HC"; to copy into the installed game.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        make_data_cd(args.data, args.output)
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
