#!/usr/bin/env python3
"""Reject an AppDir that exceeds its distribution ABI or loads build-host files."""

import argparse
from collections import namedtuple
import os
from pathlib import Path
import re
import subprocess
import sys

from appimage_runtime import SYSTEM_RUNTIME_LIBRARIES, is_system_graphics_library


TargetAbi = namedtuple("TargetAbi", "elf_class machine interpreter glibc_version distribution")
UBUNTU_LUCID = ((2, 11), "Ubuntu 10.04")
RASPBIAN_WHEEZY = ((2, 13), "Raspbian Wheezy")
DEBIAN_JESSIE = ((2, 19), "Debian Jessie")
ARM_EABI_VERSION = "Version5 EABI"
ARM_HARD_FLOAT_ATTRIBUTE = r"^\s*Tag_ABI_VFP_args:\s*VFP registers\s*$"
TARGETS = {
    "linux-appimage-x64": TargetAbi(
        "ELF64", "Advanced Micro Devices X86-64", "/lib64/ld-linux-x86-64.so.2", *UBUNTU_LUCID),
    "linux-appimage-x86": TargetAbi("ELF32", "Intel 80386", "/lib/ld-linux.so.2", *UBUNTU_LUCID),
    "linux-appimage-x86-no-sse2": TargetAbi(
        "ELF32", "Intel 80386", "/lib/ld-linux.so.2", *UBUNTU_LUCID),
    "linux-appimage-armv7": TargetAbi(
        "ELF32", "ARM", "/lib/ld-linux-armhf.so.3", *RASPBIAN_WHEEZY),
    "linux-appimage-arm64": TargetAbi(
        "ELF64", "AArch64", "/lib/ld-linux-aarch64.so.1", *DEBIAN_JESSIE),
}
SYSTEM_LIBRARIES = re.compile(
    r"(?:ld(?:-linux[^/]*|-[0-9][^/]*)?|"
    r"lib(?:c|m|dl|pthread|rt|resolv|nss_[^/]+)(?:-[0-9][^/]*)?)\.so(?:\..*)?$"
)
AUDIO_RUNTIME_LIBRARIES = ("libasound.so.2", "libpulse.so.0")


def is_elf(path):
    with path.open("rb") as stream:
        return stream.read(4) == b"\x7fELF"


def inspect(path):
    return subprocess.check_output(
        ["readelf", "--wide", "--file-header", "--program-headers", "--dynamic", "--version-info",
         "--arch-specific", str(path)],
        env=dict(os.environ, LC_ALL="C"),
        text=True,
        stderr=subprocess.STDOUT,
    )


def check_elf(path, target, *, static=False):
    description = inspect(path)
    target_abi = TARGETS[target]
    for field, value in (("Class", target_abi.elf_class), ("Machine", target_abi.machine)):
        if not re.search(rf"^\s*{field}:\s*{re.escape(value)}\s*$", description, re.MULTILINE):
            raise ValueError(f"{path}: expected {field} {value}")
    if target_abi.machine == "ARM":
        flags = re.search(r"^\s*Flags:\s*(.*)$", description, re.MULTILINE)
        # Wheezy's binutils records VFP calling conventions in attributes only.
        hard_float = (flags and "hard-float ABI" in flags[1]
                      or re.search(ARM_HARD_FLOAT_ATTRIBUTE, description, re.MULTILINE))
        if (not flags or ARM_EABI_VERSION not in flags[1]
                or "soft-float ABI" in flags[1] or not hard_float):
            raise ValueError(f"{path}: expected ARM EABI5 hard-float ABI")
    requested = re.search(r"Requesting program interpreter:\s*([^\]]+)", description)
    if static:
        if requested or re.search(r"\(NEEDED\)", description):
            raise ValueError(f"{path}: AppImage runtime must be static")
    elif requested and requested[1] != target_abi.interpreter:
        raise ValueError(f"{path}: unexpected interpreter {requested[1]}")
    for version in set(re.findall(r"\bGLIBC_[A-Za-z0-9_.]+", description)):
        suffix = version.removeprefix("GLIBC_")
        if (not re.fullmatch(r"\d+(?:\.\d+)*", suffix)
                or tuple(map(int, suffix.split("."))) > target_abi.glibc_version):
            raise ValueError(f"{path}: {version} exceeds the {target_abi.distribution} ABI")
    for needed in re.findall(r"\(NEEDED\).*?\[([^\]]+)\]", description):
        if "/" in needed:
            raise ValueError(f"{path}: absolute/build-relative dependency {needed}")
    for paths in re.findall(r"\((?:RPATH|RUNPATH)\).*?\[([^\]]*)\]", description):
        for entry in paths.split(":"):
            if not entry.startswith(("$ORIGIN/", "${ORIGIN}/")):
                raise ValueError(f"{path}: nonrelocatable library search path {entry!r}")
    return description


def check_dependency_closure(descriptions):
    libraries = {path.name for path in descriptions}
    for path, description in descriptions.items():
        for needed in re.findall(r"\(NEEDED\).*?\[([^\]]+)\]", description):
            if needed not in SYSTEM_RUNTIME_LIBRARIES and needed not in libraries:
                raise ValueError(f"{path}: missing bundled dependency {needed}")


def check_audio_runtime(root, descriptions):
    # SDL loads these backend roots with dlopen; DT_NEEDED cannot discover them.
    for name in AUDIO_RUNTIME_LIBRARIES:
        path = root / "usr/lib" / name
        if path not in descriptions:
            raise ValueError(f"{path}: missing ELF audio runtime dependency")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("appdir", type=Path)
    parser.add_argument("target", choices=TARGETS)
    parser.add_argument("--runtime", required=True, type=Path)
    args = parser.parse_args()
    root = args.appdir.resolve(strict=True)
    descriptions = {}
    for path in sorted(root.rglob("*")):
        if path.is_symlink() and not path.resolve(strict=True).is_relative_to(root):
            raise ValueError(f"{path}: symlink escapes the AppDir")
        if path.is_file() and is_elf(path):
            if SYSTEM_LIBRARIES.fullmatch(path.name):
                raise ValueError(f"{path}: do not bundle the system glibc/loader")
            if is_system_graphics_library(path.name):
                raise ValueError(f"{path}: do not bundle the host X11/XCB runtime")
            descriptions[path] = check_elf(path, args.target)
    check_audio_runtime(root, descriptions)
    check_dependency_closure(descriptions)
    for name in ("restunts", "restunts-launcher", "restunts-gtk-modules",
                 "repldump", "pixldump", "genghost"):
        path = root / "usr/bin" / name
        if not path.is_file() or not is_elf(path):
            raise ValueError(f"{path}: missing ELF executable")
        description = inspect(path)
        if not re.search(r"^\s*Type:\s*EXEC\b", description, re.MULTILINE):
            raise ValueError(f"{path}: use -no-pie for historical loader compatibility")
        if "[$ORIGIN/../lib]" not in description:
            raise ValueError(f"{path}: missing installed $ORIGIN/../lib search path")
    check_elf(args.runtime, args.target, static=True)
    glibc_version = ".".join(map(str, TARGETS[args.target].glibc_version))
    print(f"AppImage ABI passed: {args.target}; {len(descriptions)} ELF files; "
          f"GLIBC <= {glibc_version}; bundled dependency closure; static runtime")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"AppImage ABI check failed: {error}", file=sys.stderr)
        raise SystemExit(1)
