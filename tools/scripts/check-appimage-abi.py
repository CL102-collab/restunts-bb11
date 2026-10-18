#!/usr/bin/env python3
"""Reject an AppDir that loses the Ubuntu 10.04 ABI or loads build-host files."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys


TARGETS = {
    "linux-appimage-x64": ("ELF64", "Advanced Micro Devices X86-64", "/lib64/ld-linux-x86-64.so.2"),
    "linux-appimage-x86": ("ELF32", "Intel 80386", "/lib/ld-linux.so.2"),
    "linux-appimage-x86-no-sse2": ("ELF32", "Intel 80386", "/lib/ld-linux.so.2"),
}
SYSTEM_LIBRARIES = re.compile(
    r"(?:ld(?:-linux[^/]*|-[0-9][^/]*)?|"
    r"lib(?:c|m|dl|pthread|rt|resolv|nss_[^/]+)(?:-[0-9][^/]*)?)\.so(?:\..*)?$"
)


def is_elf(path):
    with path.open("rb") as stream:
        return stream.read(4) == b"\x7fELF"


def inspect(path):
    return subprocess.check_output(
        ["readelf", "--wide", "--file-header", "--program-headers", "--dynamic", "--version-info", str(path)],
        env=dict(os.environ, LC_ALL="C"),
        text=True,
        stderr=subprocess.STDOUT,
    )


def check_elf(path, target, *, static=False):
    description = inspect(path)
    elf_class, machine, interpreter = TARGETS[target]
    for field, value in (("Class", elf_class), ("Machine", machine)):
        if not re.search(rf"^\s*{field}:\s*{re.escape(value)}\s*$", description, re.MULTILINE):
            raise ValueError(f"{path}: expected {field} {value}")
    requested = re.search(r"Requesting program interpreter:\s*([^\]]+)", description)
    if static:
        if requested or re.search(r"\(NEEDED\)", description):
            raise ValueError(f"{path}: AppImage runtime must be static")
    elif requested and requested[1] != interpreter:
        raise ValueError(f"{path}: unexpected interpreter {requested[1]}")
    for version in set(re.findall(r"\bGLIBC_[A-Za-z0-9_.]+", description)):
        suffix = version.removeprefix("GLIBC_")
        if not re.fullmatch(r"\d+(?:\.\d+)*", suffix) or tuple(map(int, suffix.split("."))) > (2, 11):
            raise ValueError(f"{path}: {version} exceeds the Ubuntu 10.04 ABI")
    for needed in re.findall(r"\(NEEDED\).*?\[([^\]]+)\]", description):
        if "/" in needed:
            raise ValueError(f"{path}: absolute/build-relative dependency {needed}")
    for paths in re.findall(r"\((?:RPATH|RUNPATH)\).*?\[([^\]]*)\]", description):
        for entry in paths.split(":"):
            if not entry.startswith(("$ORIGIN/", "${ORIGIN}/")):
                raise ValueError(f"{path}: nonrelocatable library search path {entry!r}")
    return description


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("appdir", type=Path)
    parser.add_argument("target", choices=TARGETS)
    parser.add_argument("--runtime", required=True, type=Path)
    args = parser.parse_args()
    root = args.appdir.resolve(strict=True)
    checked = 0
    for path in sorted(root.rglob("*")):
        if path.is_symlink() and not path.resolve(strict=True).is_relative_to(root):
            raise ValueError(f"{path}: symlink escapes the AppDir")
        if path.is_file() and is_elf(path):
            if SYSTEM_LIBRARIES.fullmatch(path.name):
                raise ValueError(f"{path}: do not bundle the system glibc/loader")
            check_elf(path, args.target)
            checked += 1
    for name in ("restunts", "repldump", "pixldump", "genghost"):
        path = root / "usr/bin" / name
        if not path.is_file() or not is_elf(path):
            raise ValueError(f"{path}: missing ELF executable")
        description = inspect(path)
        if not re.search(r"^\s*Type:\s*EXEC\b", description, re.MULTILINE):
            raise ValueError(f"{path}: use -no-pie for historical loader compatibility")
        if "[$ORIGIN/../lib]" not in description:
            raise ValueError(f"{path}: missing installed $ORIGIN/../lib search path")
    check_elf(args.runtime, args.target, static=True)
    print(f"AppImage ABI passed: {args.target}; {checked} ELF files; GLIBC <= 2.11; static runtime")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"AppImage ABI check failed: {error}", file=sys.stderr)
        raise SystemExit(1)
