"""Extract verified OpenBSD package payloads without executing package actions."""

import argparse
import os
from pathlib import Path
import posixpath
import shutil
import tarfile


FILE_DIRECTIVES = {"@file", "@bin", "@lib", "@static-lib", "@so", "@pkgconfig",
                   "@ltlib", "@man", "@info", "@rcscript", "@shell"}


def rooted_path(sysroot, path):
    destination = sysroot / path.lstrip("/")
    if not destination.resolve().is_relative_to(sysroot):
        raise ValueError(f"Package path escapes the SDK: {path}")
    return destination


def package_files(contents):
    """Map archive member names to installed paths and packing-list links."""
    current_directory = None
    last_file = None
    files = {}
    links = []
    for line in contents.splitlines():
        directive, _, argument = line.partition(" ")
        if directive == "@cwd":
            if not argument.startswith("/"):
                raise ValueError(f"Expected an absolute package directory: {argument}")
            current_directory = posixpath.normpath(argument)
        elif directive in {"@symlink", "@link"}:
            if last_file is None:
                raise ValueError(f"Package link has no preceding file: {line}")
            links.append((last_file, directive, argument, current_directory))
        elif directive in FILE_DIRECTIVES or not line.startswith("@"):
            name = argument if directive in FILE_DIRECTIVES else line
            if not name or name.startswith("+") or name.endswith("/"):
                continue
            absolute_rcscript = directive == "@rcscript" and name.startswith("/etc/rc.d/")
            if (current_directory is None or (name.startswith("/") and not absolute_rcscript)
                    or ".." in Path(name).parts):
                raise ValueError(f"Invalid package filename: {name}")
            last_file = posixpath.join(current_directory, name)
            files[name] = last_file
    return files, links


def extract_package(sysroot, archive_path):
    with tarfile.open(archive_path, "r:gz") as archive:
        contents = archive.extractfile("+CONTENTS")
        if contents is None:
            raise ValueError(f"Package has no packing list: {archive_path}")
        files, links = package_files(contents.read().decode("utf-8"))
        for member in archive:
            if member.name.startswith("+"):
                continue
            if member.name not in files or not member.isfile():
                raise ValueError(f"Unexpected package payload: {member.name}")
            destination = rooted_path(sysroot, files[member.name])
            destination.parent.mkdir(parents=True, exist_ok=True)
            with archive.extractfile(member) as source, destination.open("wb") as output:
                shutil.copyfileobj(source, output)
            destination.chmod(member.mode & 0o777)

        # OpenBSD records links in +CONTENTS rather than as tar members.
        for filename, directive, target, current_directory in links:
            destination = rooted_path(sysroot, filename)
            destination.parent.mkdir(parents=True, exist_ok=True)
            if destination.is_symlink() or destination.exists():
                destination.unlink()
            if directive == "@symlink":
                linked_path = posixpath.normpath(posixpath.join(posixpath.dirname(filename), target))
                linked_destination = rooted_path(sysroot, linked_path)
                # Keep absolute package links inside the SDK as well.
                destination.symlink_to(os.path.relpath(linked_destination, destination.parent))
            else:
                source = rooted_path(sysroot, posixpath.join(current_directory, target))
                destination.hardlink_to(source)


def normalize_links(sysroot):
    # Release sets contain absolute links meant for the installed target root.
    # Relative links keep compiler and pkg-config lookups inside this SDK.
    for directory, directories, filenames in os.walk(sysroot):
        for name in directories + filenames:
            link = Path(directory) / name
            if link.is_symlink() and link.readlink().is_absolute():
                target = sysroot / str(link.readlink()).lstrip("/")
                link.unlink()
                link.symlink_to(os.path.relpath(target, link.parent))
    for directory, directories, filenames in os.walk(sysroot):
        for name in directories + filenames:
            link = Path(directory) / name
            if link.is_symlink() and not link.resolve().is_relative_to(sysroot):
                raise ValueError(f"SDK link escapes its root: {link}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sysroot", type=Path)
    parser.add_argument("cache", type=Path, nargs="?")
    parser.add_argument("--normalize-links-only", action="store_true")
    arguments = parser.parse_args()
    sysroot = arguments.sysroot.resolve()
    normalize_links(sysroot)
    if arguments.normalize_links_only:
        return
    if arguments.cache is None:
        parser.error("The verified package cache is required")
    archives = sorted(arguments.cache.glob("*.tgz"))
    if not archives:
        parser.error("The verified package cache is empty")
    for archive in archives:
        extract_package(sysroot, archive)


if __name__ == "__main__":
    main()
