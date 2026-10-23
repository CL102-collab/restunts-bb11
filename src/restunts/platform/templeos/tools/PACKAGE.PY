#!/usr/bin/env python3
"""Build a RedSea port CD with custom cars and a private-data import tool."""

import argparse
import hashlib
from pathlib import Path
import runpy
import shutil
import subprocess
import sys
import tempfile
import zipfile


TOOLS = Path(__file__).resolve().parent
PORT = TOOLS.parent
REPOSITORY = PORT.parents[3]
GUIDE = REPOSITORY / "docs/templeos.md"
ISO_NAME = "RESTUNTS.ISO"
GUEST_ISO_NAME = "GAME.ISO.C"
PACKAGE_NAME = "RESTUNTS_STOCK_TEMPLEOS"
BOOT_SECONDS = 35
BUILD_TIMEOUT_SECONDS = 180
SECTOR_BYTES = 512
PARTITION_SECTORS = 2048
FAT_STAGING_NAME_LIMIT = 25
HASH_CHUNK_BYTES = 1024 * 1024


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(HASH_CHUNK_BYTES), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_crlf(path, text):
    path.write_bytes(text.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))


def require_file(path):
    if not path.is_file():
        raise FileNotFoundError(path)
    return path.resolve()


def package(args):
    destination = args.output.resolve()
    archive = destination.parent / (destination.name + ".ZIP")
    if destination.exists() or archive.exists():
        raise ValueError("Choose a new output path; existing packages are never overwritten")
    iso = require_file(args.iso)
    guide = require_file(GUIDE)
    mcopy = args.mcopy or shutil.which("mcopy")
    if not mcopy:
        raise RuntimeError("Install mtools, or supply --mcopy /path/to/mcopy")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="restunts-stock-package-") as temporary:
        workspace = Path(temporary)
        source = workspace / "source"
        source.mkdir()
        disc = source / "DISC"
        prepare = runpy.run_path(str(TOOLS / "PREPARE.PY"))["prepare"]
        prepare(None, disc)
        for name in ("INSTALL.HC", "PLAY.HC", "VERIFY.HH"):
            shutil.copyfile(require_file(PORT / "package" / name), disc / name)
        shutil.copyfile(guide, disc / "README.MD")
        shutil.copyfile(require_file(PORT / "package/BUILD.HC"), source / "BUILD.HC")
        files = sorted(path for path in disc.rglob("*") if path.is_file())
        for path in files:
            for component in path.relative_to(disc).parts:
                if not component.isascii() or len(component) > FAT_STAGING_NAME_LIMIT:
                    raise ValueError("Stock TempleOS FAT staging needs ASCII names up to "
                                     f"{FAT_STAGING_NAME_LIMIT} characters: {path.name}")
        contents = "".join(f"{sha256(path)}  {path.relative_to(disc).as_posix()}\n"
                           for path in files)
        vm = workspace / "vm"
        command = [
            sys.executable, str(TOOLS / "VM.PY"), "--work", str(vm), "run",
            "--iso", str(iso), "--source", str(source), "--entry", "BUILD.HC",
            "--mcopy", str(mcopy), "--boot-seconds", str(BOOT_SECONDS),
            "--timeout", str(BUILD_TIMEOUT_SECONDS),
        ]
        try:
            subprocess.run(command, check=True)
        except subprocess.CalledProcessError:
            log = vm / "DEBUG.LOG"
            if log.is_file():
                print(log.read_text(errors="replace"), file=sys.stderr)
            raise
        built = workspace / ISO_NAME
        disk_spec = str(vm / "SHARE.IMG") + "@@" + str(PARTITION_SECTORS * SECTOR_BYTES)
        subprocess.run([str(mcopy), "-i", disk_spec, "::/" + GUEST_ISO_NAME, str(built)],
                       check=True)
        if not built.stat().st_size:
            raise RuntimeError("TempleOS produced an empty CD image")
        destination.mkdir()
        shutil.copyfile(built, destination / ISO_NAME)
        shutil.copyfile(guide, destination / "README.MD")
        toolkit = destination / "DATA_CD_TOOL"
        toolkit.mkdir()
        shutil.copyfile(require_file(TOOLS / "MAKE_DATA_CD.PY"), toolkit / "MAKE_DATA_CD.PY")
        shutil.copyfile(PORT / "LEGACY.HH", toolkit / "LEGACY.HH")
        for name in ("IMPORT.HC", "VERIFY.HH"):
            shutil.copyfile(require_file(PORT / "package" / name), toolkit / name)
        write_crlf(destination / "CONTENTS.TXT", contents)
        write_crlf(destination / "BUILD.TXT",
                   "Restunts stock TempleOS CD\n\n"
                   "Filesystem: native TempleOS RedSea CD, written by RedSeaISO.\n"
                   "GAME.ISO.C was extracted byte-for-byte and named RESTUNTS.ISO.\n"
                   "This port CD contains HolyC sources and bundled custom cars.\n"
                   f"CD contains {len(files)} files; CONTENTS.TXT records their SHA256 hashes.\n"
                   f"Build-system ISO SHA256: {sha256(iso)}\n"
                   "No original game data is included. DATA_CD_TOOL creates a separate\n"
                   "private CD from the player's own game files; do not distribute that CD.\n")
        checksums = "".join(f"{sha256(path)}  {path.relative_to(destination).as_posix()}\n"
                            for path in sorted(destination.rglob("*")) if path.is_file())
        write_crlf(destination / "SHA256SUMS.TXT", checksums)
        with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED) as output:
            for path in sorted(destination.rglob("*")):
                if path.is_file():
                    output.write(path, PACKAGE_NAME + "/" + path.relative_to(destination).as_posix())
    print("Game CD: " + str(destination / ISO_NAME))
    print("Instructions: " + str(destination / "README.MD"))
    print("Package: " + str(archive))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, required=True,
                        help="Official stock TempleOS ISO used to write the RedSea image")
    parser.add_argument("--output", type=Path, required=True,
                        help="New host package directory (a sibling .ZIP is also created)")
    parser.add_argument("--mcopy", help="Path to mtools mcopy when it is not on PATH")
    args = parser.parse_args()
    try:
        package(args)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
