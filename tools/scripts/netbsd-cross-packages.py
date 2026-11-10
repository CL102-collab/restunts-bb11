#!/usr/bin/env python3
"""Extract the target GTK dependency closure without running package programs."""

import gzip
import os
import re
import subprocess
import sys
import tarfile
from pathlib import Path
from urllib.parse import quote


def download(repository, name, destination):
    subprocess.run(["curl", "--fail", "--location", "--retry", "3", "--connect-timeout", "20",
                    repository + "/" + quote(name), "--output", str(destination)], check=True)


def read_summary(path, architecture):
    packages = {}
    with gzip.open(path, "rt", encoding="utf-8", errors="replace") as stream:
        for block in stream.read().split("\n\n"):
            fields = {}
            for line in block.splitlines():
                key, separator, value = line.partition("=")
                if separator:
                    fields.setdefault(key, []).append(value)
            if not fields:
                continue
            if fields.get("OPSYS") != ["NetBSD"] or fields.get("MACHINE_ARCH") != [architecture]:
                raise RuntimeError("Package summary contains a different target OS or architecture")
            name = fields["PKGNAME"][0]
            packages[name] = fields
    return packages


def dependency_closure(packages, roots):
    selected = {}
    pending = list(roots)
    by_base = {}
    for name in packages:
        by_base.setdefault(name.rsplit("-", 1)[0], []).append(name)
    while pending:
        pattern = pending.pop()
        base = re.split(r"[<>=*?\[{]", pattern, maxsplit=1)[0].rstrip("-")
        candidates = by_base.get(base, [name for name in packages if name.startswith(base)])
        matches = []
        for name in candidates:
            result = subprocess.run(["pkg_admin", "pmatch", pattern, name], check=False,
                                    stdout=subprocess.DEVNULL)
            if result.returncode == 0:
                matches.append(name)
            elif result.returncode != 1:
                raise RuntimeError(f"Cannot evaluate package dependency: {pattern}")
        if not matches:
            raise RuntimeError(f"No package satisfies {pattern}")
        if len({name.rsplit("-", 1)[0] for name in matches}) != 1:
            raise RuntimeError(f"Ambiguous package dependency: {pattern}")
        name = matches[0]
        for candidate in matches[1:]:
            package_base, version = name.rsplit("-", 1)
            result = subprocess.run(["pkg_admin", "pmatch", f"{package_base}>{version}", candidate],
                                    check=False, stdout=subprocess.DEVNULL)
            if result.returncode == 0:
                name = candidate
            elif result.returncode != 1:
                raise RuntimeError(f"Cannot compare target package versions: {pattern}")
        if name not in selected:
            selected[name] = packages[name]
            pending.extend(packages[name].get("DEPENDS", []))
    return selected


def extract_package(path, prefix):
    with tarfile.open(path, "r:gz") as archive:
        contents = archive.extractfile("+CONTENTS").read().decode("utf-8")
        for line in contents.splitlines():
            if line.startswith("@cwd ") and line != "@cwd /usr/pkg":
                raise RuntimeError(f"Unexpected target package prefix: {line}")
        # Package scripts and metadata stay out of the SDK. The data filter
        # rejects traversal, escaping links, devices, and other special files.
        members = [member for member in archive.getmembers() if not member.name.startswith("+")]
        archive.extractall(prefix, members=members, filter="data")


def normalize_links(root):
    root = root.resolve()
    links = []
    for directory, subdirectories, filenames in os.walk(root, followlinks=False):
        for name in subdirectories + filenames:
            path = Path(directory, name)
            if not path.is_symlink():
                continue
            links.append(path)
            target = os.readlink(path)
            if os.path.isabs(target):
                path.unlink()
                path.symlink_to(os.path.relpath(root / target.lstrip("/"), path.parent))
    for path in links:
        try:
            path.resolve().relative_to(root)
        except (ValueError, RuntimeError) as error:
            raise RuntimeError(f"NetBSD SDK symlink escapes its root: {path}") from error


def main():
    repository, architecture, sysroot, cache = sys.argv[1:]
    cache = Path(cache)
    summary = cache / "pkg_summary.gz"
    download(repository, summary.name, summary)
    selected = dependency_closure(read_summary(summary, architecture), ["gtk3+>=0"])
    prefix = Path(sysroot) / "usr/pkg"
    prefix.mkdir(parents=True, exist_ok=True)
    for name, fields in sorted(selected.items()):
        filename = fields["FILE_NAME"][0]
        if Path(filename).name != filename:
            raise RuntimeError(f"Invalid package filename: {filename}")
        destination = cache / filename
        download(repository, filename, destination)
        if destination.stat().st_size != int(fields["FILE_SIZE"][0]):
            raise RuntimeError(f"Package size differs from repository summary: {filename}")
        print(f"Extracting target package {name}", flush=True)
        extract_package(destination, prefix)
    normalize_links(Path(sysroot))


if __name__ == "__main__":
    main()
