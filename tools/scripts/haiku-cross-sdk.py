"""Import native Haiku development files without running target executables."""

import argparse
from pathlib import Path, PurePosixPath
import posixpath
import re
import shutil
import tarfile
import tempfile


MACHINES = {"haiku-x64": "x86_64-unknown-haiku", "haiku-x86": "i586-pc-haiku"}
SYSTEM_PREFIX = "/boot/system"
PACKAGE_PREFIX = re.compile(
    r"/packages/[^/\s\"']+/(?:\.self|[^/\s\"']+~[^/\s\"']+)(?=/|[^\w.-]|$)")


def rebase_paths(source, sysroot):
    prefix = str(sysroot)
    source = PACKAGE_PREFIX.sub(SYSTEM_PREFIX, source)
    source = re.sub(r"(?<![\w/.-])/system(?=/|[^\w.-]|$)", SYSTEM_PREFIX, source)
    source = re.sub(r"(-[IL])/system(?=/|[^\w.-]|$)", r"\1" + SYSTEM_PREFIX, source)
    rooted_prefix = prefix + SYSTEM_PREFIX
    placeholder = "\0RESTUNTS_HAIKU_PREFIX\0"
    source = source.replace(rooted_prefix, placeholder)
    source = source.replace(SYSTEM_PREFIX, prefix + SYSTEM_PREFIX)
    source = source.replace(placeholder, rooted_prefix)
    for option in ("-Wl,-rpath,", "-Wl,-rpath=", "-Wl,--rpath,", "-Wl,--rpath=",
                   "-Wl,-R,", "-Wl,-R"):
        source = source.replace(option + prefix, option)
    return source


def safe_name(name):
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts:
        raise ValueError("Unsafe SDK archive path: " + name)
    return str(path)


def extract_archive(archive_path, destination):
    with tarfile.open(archive_path, "r:gz") as archive:
        members = archive.getmembers()
        for member in members:
            member.name = safe_name(member.name)
            if member.isdev() or member.isfifo():
                raise ValueError("Unexpected special file in SDK: " + member.name)
            if member.isdir():
                # Packagefs directories may be read-only. The SDK owner must
                # be able to refresh or remove this private development tree.
                member.mode |= 0o700
            if member.issym():
                link = PACKAGE_PREFIX.sub(SYSTEM_PREFIX, member.linkname)
                if link == "/system" or link.startswith("/system/"):
                    link = "/boot" + link
                if link.startswith("/"):
                    if not link.startswith(SYSTEM_PREFIX + "/"):
                        raise ValueError("SDK link leaves target system: " + link)
                    link = posixpath.relpath(link.lstrip("/"),
                                            posixpath.dirname(member.name))
                resolved = posixpath.normpath(posixpath.join(
                    posixpath.dirname(member.name), link))
                safe_name(resolved)
                member.linkname = link
            elif member.islnk():
                member.linkname = safe_name(member.linkname)
        archive.extractall(destination, members=members, filter="data")


def prepare_sysroot(target, sdk, archive_path):
    staged = Path(tempfile.mkdtemp(prefix="sysroot-", dir=sdk))
    try:
        extract_archive(archive_path, staged)
        if (staged / "restunts-target").read_text().strip() != target:
            raise ValueError("The exported Haiku sysroot has the wrong architecture")
        release = (staged / "restunts-release").read_text()
        packages = (staged / "restunts-installed-packages.txt").read_text()
        if "beta6" not in release and "r1~beta6" not in packages:
            raise ValueError("The exported sysroot must use Haiku R1/beta6")
        system = staged / "boot/system"
        secondary = "x86/" if target == "haiku-x86" else ""
        required = ["develop/headers/posix/stdio.h", "develop/headers/os/app/Application.h",
                    "lib/" + secondary + "libbe.so",
                    "bin/" + secondary + "wx-config"]
        for name in required:
            if not (system / name).is_file():
                raise ValueError("Missing Haiku SDK dependency: " + name)
        gl_libraries = [system / prefix / secondary / "libGL.so"
                        for prefix in ("develop/lib", "lib")]
        if not any(library.is_file() for library in gl_libraries):
            raise ValueError("Missing Haiku SDK OpenGL development library")
        if not list((system / "develop/headers").rglob("GLView.h")):
            raise ValueError("Missing Haiku SDK OpenGL C++ headers")
        qt_roots = [system / "lib" / secondary / "cmake/Qt5",
                    system / "develop/lib" / secondary / "cmake/Qt5"]
        if not any((root / "Qt5Config.cmake").is_file() for root in qt_roots):
            raise ValueError("Missing Qt5 development package in Haiku SDK")
        qt_data_roots = [system / "data/Qt5", system / "data/Qt5/x86",
                         system / "data/x86/Qt5"]
        if not any((root / "mkspecs/haiku-g++/qplatformdefs.h").is_file()
                   for root in qt_data_roots):
            raise ValueError("Missing Qt5 Haiku platform specifications in SDK")
        wx_script = (system / "bin" / secondary / "wx-config").resolve()
        if not wx_script.is_relative_to(staged):
            raise ValueError("wx-config leaves the imported SDK")
        wx_relative = wx_script.relative_to(staged)
        wx_source = wx_script.read_text()
        sysroot = sdk / "sysroot"
        wx_script.chmod(wx_script.stat().st_mode | 0o200)
        wx_script.write_text(rebase_paths(wx_source, sysroot))
        # Qt package imports validate absolute locations while loading. Rebase
        # target files before publishing the SDK; target Qt tools remain unused.
        for cmake_file in (staged / "boot/system").rglob("*.cmake"):
            source = cmake_file.read_text()
            rebased = rebase_paths(source, sysroot)
            if rebased != source:
                cmake_file.chmod(cmake_file.stat().st_mode | 0o200)
                cmake_file.write_text(rebased)
        if sysroot.exists():
            if (sysroot / "restunts-target").read_text().strip() != target:
                raise ValueError("Refusing to replace a different target's sysroot")
            shutil.rmtree(sysroot)
        staged.rename(sysroot)
        staged = None
        libexec = sdk / "libexec"
        libexec.mkdir(exist_ok=True)
        (libexec / "wx-config-target").write_text(rebase_paths(wx_source, sysroot))
        (libexec / "wx-config-target-path").write_text(str(wx_relative) + "\n")
    finally:
        if staged is not None:
            shutil.rmtree(staged)


def prepare_wrappers(target, sdk):
    scripts = Path(__file__).resolve().parent
    libexec = sdk / "libexec"
    libexec.mkdir(exist_ok=True)
    # The bootstrap compiler is built --disable-shared and defaults to its own
    # static unwinder. Native Haiku libraries use libgcc_s; mixing the two
    # prevents exceptions from traversing application and shared-library frames.
    (libexec / "haiku-runtime.specs").write_text(
        "*libgcc:\n%{static|static-libgcc|static-pie:-lgcc;:-lgcc_s -lgcc}\n\n")
    driver = libexec / "haiku-cross-driver.py"
    shutil.copyfile(scripts / "haiku-cross-driver.py", driver)
    bin_dir = sdk / "bin"
    bin_dir.mkdir(exist_ok=True)
    machine = MACHINES[target]
    runtime_directory = "/boot/system/lib/x86" if target == "haiku-x86" else "/boot/system/lib"
    for name in ("cc", "c++", "wx-config"):
        wrapper = bin_dir / name
        python_entry = ('RESTUNTS_HAIKU_DRIVER=' + name +
                        ' exec python3 "$sdk/libexec/haiku-cross-driver.py" "$@"\n')
        source = '#!/bin/sh\nset -eu\nsdk=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)\n'
        if name == "wx-config":
            source += python_entry
        else:
            # Compilation is the common path; avoid starting Python for every
            # source file. Only x86 CMake discovery needs transformed metadata.
            if target == "haiku-x86":
                source += ('if [ "$#" -eq 1 ] && [ "$1" = -print-search-dirs ]; then\n'
                           + python_entry + 'fi\n')
            compiler = "gcc" if name == "cc" else "g++"
            source += ('exec "$sdk/toolchain/bin/' + machine + '-' + compiler +
                       '" --sysroot="$sdk/sysroot" '
                       '-specs="$sdk/libexec/haiku-runtime.specs" '
                       '-L"$sdk/sysroot' + runtime_directory + '" '
                       '-Wl,-rpath-link,"$sdk/sysroot' + runtime_directory + '" "$@"\n')
        wrapper.write_text(source)
        wrapper.chmod(0o755)
    for name in ("ar", "ranlib", "ld", "readelf", "strip", "objdump", "nm"):
        tool = bin_dir / name
        if tool.is_symlink() or tool.exists():
            tool.unlink()
        tool.symlink_to("../toolchain/bin/" + machine + "-" + name)
    (sdk / "restunts-target").write_text(target + "\n")
    (sdk / "restunts-machine").write_text(machine + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", choices=MACHINES)
    parser.add_argument("sdk", type=Path)
    parser.add_argument("archive", type=Path)
    args = parser.parse_args()
    args.sdk = args.sdk.resolve()
    args.sdk.mkdir(parents=True, exist_ok=True)
    prepare_sysroot(args.target, args.sdk, args.archive)
    prepare_wrappers(args.target, args.sdk)


if __name__ == "__main__":
    main()
