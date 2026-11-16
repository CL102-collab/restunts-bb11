#!/usr/bin/env python3
"""Build deterministic ZIP archives for the bundled custom cars."""

import argparse
import io
from pathlib import Path
import stat
import zipfile


ROOT = Path(__file__).resolve().parents[2]
CAR_ARCHIVE_MEMBERS = {
    "TERR.zip": ("CARTERR.RES", "STTERR.3SH", "STDATERR.VSH", "STDBTERR.VSH"),
    "MCMU.zip": ("CARMCMU.RES", "STMCMU.3SH", "STMCMU.H3D", "STDAMCMU.VSH", "STDBMCMU.VSH"),
}
ZIP_EPOCH = (1980, 1, 1, 0, 0, 0)
ZIP_UNIX_SYSTEM = 3
ZIP_MODE_SHIFT = 16
FILE_MODE = 0o644
COMPRESSION_LEVEL = 9


def build_archives(source_directory):
    """Return the complete archives without depending on source file metadata."""
    source_directory = Path(source_directory)
    archives = {}
    for name, members in CAR_ARCHIVE_MEMBERS.items():
        output = io.BytesIO()
        with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED,
                             compresslevel=COMPRESSION_LEVEL, allowZip64=False) as archive:
            for member in sorted(members):
                source = source_directory / member
                content = source.read_bytes()
                if not content:
                    raise ValueError(f"Empty custom car resource: {source}")
                entry = zipfile.ZipInfo(member, ZIP_EPOCH)
                entry.create_system = ZIP_UNIX_SYSTEM
                entry.external_attr = (stat.S_IFREG | FILE_MODE) << ZIP_MODE_SHIFT
                entry.compress_type = zipfile.ZIP_DEFLATED
                archive.writestr(entry, content, compresslevel=COMPRESSION_LEVEL)
        archives[name] = output.getvalue()
    return archives


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, default=ROOT / "assets/cars")
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    try:
        archives = build_archives(args.source_dir)
        args.output_dir.mkdir(parents=True, exist_ok=True)
        for name, content in archives.items():
            destination = args.output_dir / name
            if not destination.is_file() or destination.read_bytes() != content:
                destination.write_bytes(content)
            destination.chmod(FILE_MODE)
        print(f"Prepared {len(archives)} custom car archives in {args.output_dir}")
    except (OSError, ValueError, zipfile.LargeZipFile) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
