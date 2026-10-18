#!/usr/bin/env python3
"""Fetch pinned public container layers and extract a link-only legacy SDK.

No program from an old root filesystem is executed.  The files selected here
are compiler inputs; prepare-appimage-sysroot.sh chooses the separate runtime
libraries that are actually shipped in the AppImage.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import tarfile
import tempfile
import time
import urllib.parse
import urllib.request


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def download(url, checksum, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.is_file() and sha256(destination) == checksum:
        return destination
    headers = {}
    parsed = urllib.parse.urlsplit(url)
    if parsed.hostname == "registry-1.docker.io":
        repository = parsed.path.removeprefix("/v2/").split("/blobs/", 1)[0]
        token_url = "https://auth.docker.io/token?" + urllib.parse.urlencode({
            "service": "registry.docker.io",
            "scope": f"repository:{repository}:pull",
        })
        with urllib.request.urlopen(token_url, timeout=60) as response:
            token = json.load(response)["token"]
        # The public registry bearer token is kept out of logs and artifacts.
        headers["Authorization"] = "Bearer " + token
    descriptor, partial_name = tempfile.mkstemp(prefix=destination.name + ".partial.", dir=destination.parent)
    os.close(descriptor)
    partial = Path(partial_name)
    try:
        for attempt in range(3):
            try:
                request = urllib.request.Request(url, headers=headers)
                with urllib.request.urlopen(request, timeout=60) as response, partial.open("wb") as output:
                    shutil.copyfileobj(response, output, 1024 * 1024)
                break
            except OSError:
                if attempt == 2:
                    raise
                time.sleep(2 ** attempt)
        actual = sha256(partial)
        if actual != checksum:
            raise RuntimeError(f"SHA256 mismatch for {destination.name}: {actual}")
        partial.replace(destination)
    finally:
        partial.unlink(missing_ok=True)
    return destination


def selected(name, runtime=False):
    if runtime:
        return name == "lib" or name.startswith("lib/") or name == "lib64" or name.startswith("lib64/")
    if name == "usr/include" or name.startswith("usr/include/"):
        return True
    for prefix in ("lib", "lib64", "usr/lib", "usr/lib64"):
        if name == prefix:
            return True
        if name.startswith(prefix + "/"):
            remainder = name[len(prefix) + 1:]
            # Keep development linker scripts, CRTs and libraries, not the
            # image's Python installations or its obsolete compiler binaries.
            if "/" not in remainder or remainder.startswith(("i686/", "pkgconfig/")):
                return True
    if name == "usr/share/X11" or name.startswith("usr/share/X11/"):
        return True
    if name.startswith("usr/share/doc/libX"):
        return True
    return False


def remove(path):
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.exists():
        shutil.rmtree(path)


def extract(archive, destination, runtime=False):
    destination.mkdir(parents=True, exist_ok=True)
    root = destination.resolve()
    with tarfile.open(archive) as source:
        for member in source:
            name = member.name.removeprefix("./").rstrip("/")
            if name in ("", "."):
                continue
            parts = PurePosixPath(name).parts
            if not parts or name.startswith("/") or ".." in parts:
                raise RuntimeError(f"Unsafe archive member: {member.name}")
            if not selected(name, runtime):
                continue
            path = destination.joinpath(*parts)
            if not path.parent.resolve().is_relative_to(root):
                raise RuntimeError(f"Archive parent escapes SDK: {member.name}")
            path.parent.mkdir(parents=True, exist_ok=True)
            if path.name.startswith(".wh."):
                if path.name == ".wh..wh..opq":
                    for child in path.parent.iterdir():
                        remove(child)
                else:
                    remove(path.with_name(path.name[4:]))
                continue
            if member.isdir():
                if path.is_symlink() or (path.exists() and not path.is_dir()):
                    remove(path)
                path.mkdir(exist_ok=True)
                path.chmod(0o755)
            elif member.issym():
                remove(path)
                target = member.linkname
                if target.startswith("/"):
                    target = os.path.relpath(destination / target.lstrip("/"), path.parent)
                resolved = (path.parent / target).resolve()
                if not resolved.is_relative_to(root):
                    raise RuntimeError(f"Archive symlink escapes SDK: {member.name}")
                path.symlink_to(target)
            elif member.isfile() or member.islnk():
                remove(path)
                with source.extractfile(member) as content, path.open("wb") as output:
                    shutil.copyfileobj(content, output, 1024 * 1024)
                path.chmod(0o644)
            # Device nodes and other container metadata are never needed.


def overlay_runtime(runtime, sysroot, lib_directory):
    output = sysroot / lib_directory
    output.mkdir(parents=True, exist_ok=True)
    sonames = (
        "libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0", "librt.so.1",
        "libresolv.so.2", "libutil.so.1", "libnsl.so.1", "libanl.so.1",
        "libnss_files.so.2", "libnss_dns.so.2", "libnss_compat.so.2",
        "libnss_nis.so.2", "libnss_nisplus.so.2", "libBrokenLocale.so.1",
        "ld-linux-x86-64.so.2" if lib_directory == "lib64" else "ld-linux.so.2",
    )
    for soname in sonames:
        candidates = sorted(runtime.rglob(soname), key=lambda path: len(path.parts))
        if not candidates:
            raise RuntimeError(f"Pinned glibc runtime is missing {soname}")
        path = output / soname
        remove(path)
        shutil.copyfile(candidates[0].resolve(), path)
        path.chmod(0o644)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    fetch = subparsers.add_parser("download")
    fetch.add_argument("url")
    fetch.add_argument("checksum")
    fetch.add_argument("destination", type=Path)
    prepare = subparsers.add_parser("prepare")
    prepare.add_argument("--cache", required=True, type=Path)
    prepare.add_argument("--sysroot", required=True, type=Path)
    prepare.add_argument("--runtime", required=True, type=Path)
    prepare.add_argument("--lib-directory", required=True, choices=("lib", "lib64"))
    for resource in ("base", "development", "runtime"):
        prepare.add_argument(f"--{resource}-url", required=True)
        prepare.add_argument(f"--{resource}-sha256", required=True)
    args = parser.parse_args()
    if args.command == "download":
        download(args.url, args.checksum, args.destination)
        return
    archives = {}
    for resource in ("base", "development", "runtime"):
        checksum = getattr(args, resource + "_sha256")
        archives[resource] = download(
            getattr(args, resource + "_url"), checksum,
            args.cache / (checksum + ".tar.gz"),
        )
    extract(archives["base"], args.sysroot)
    extract(archives["development"], args.sysroot)
    extract(archives["runtime"], args.runtime, runtime=True)
    overlay_runtime(args.runtime, args.sysroot, args.lib_directory)


if __name__ == "__main__":
    main()
