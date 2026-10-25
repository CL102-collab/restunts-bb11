#!/usr/bin/env python3
"""Fetch pinned public container layers and extract a link-only legacy SDK.

No program from an old root filesystem is executed.  The files selected here
are compiler inputs; prepare-appimage-sysroot.sh chooses the separate runtime
libraries that are actually shipped in the AppImage.
"""

import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shlex
import shutil
import subprocess
import tarfile
import tempfile
import time
import urllib.parse
import urllib.request

from appimage_runtime import SYSTEM_RUNTIME_LIBRARIES


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


LEGACY_CXX_PREFIX = "opt/rh/devtoolset-2/root/usr"
LEGACY_X86_GCC_DIRECTORY = LEGACY_CXX_PREFIX + "/lib/gcc/i386-redhat-linux/4.8.2"
GCC_X86_MULTILIB_DIRECTORY = "32"
LEGACY_COMPILER_INPUTS = frozenset({
    "libstdc++.a", "libsupc++.a", "libgcc.a", "libgcc_eh.a",
    "crtbegin.o", "crtbeginS.o", "crtbeginT.o", "crtend.o", "crtendS.o", "crtfastmath.o",
    "crtprec32.o", "crtprec64.o", "crtprec80.o",
})
GTK_ARCHIVE_ROOT = "https://archive.debian.org/debian/"
GTK_ROOT_PACKAGES = (
    "libgtk2.0-dev", "libglib2.0-dev", "libpango1.0-dev", "fontconfig-config", "libpulse-dev",
)
GTK_RUNTIME_ROOTS = ("libgtk-x11-2.0.so.0", "libSM.so.6", "libasound.so.2", "libpulse.so.0")
GTK_EXTERNAL_PACKAGES = frozenset({
    "libc6", "libc6-dev", "libc-dev", "libc-bin", "libgcc1", "libstdc++6", "libxml2-utils",
})


def selected(name, runtime=False, gtk=False, sdk=False):
    if runtime:
        return (
            name == "lib" or name.startswith("lib/") or name == "lib64" or name.startswith("lib64/")
            or name.startswith("usr/share/common-licenses/")
        )
    if name.startswith(LEGACY_CXX_PREFIX + "/share/doc/devtoolset-2-gcc-"):
        return PurePosixPath(name).name.startswith("COPYING")
    if name.startswith(LEGACY_CXX_PREFIX + "/include/c++/"):
        return True
    if name.startswith(LEGACY_CXX_PREFIX + "/lib/gcc/"):
        return PurePosixPath(name).name in LEGACY_COMPILER_INPUTS
    if name == "usr/include" or name.startswith("usr/include/"):
        return True
    if sdk:
        if name.startswith("usr/lib/gcc/"):
            return PurePosixPath(name).name in LEGACY_COMPILER_INPUTS | {"libatomic.a"}
        if name.startswith(("lib/", "usr/lib/")) and name.endswith(".o"):
            return True
        if name.startswith(("usr/share/alsa/", "usr/share/X11/",
                            "usr/share/common-licenses/")):
            return True
        if name.startswith("usr/share/doc/"):
            return PurePosixPath(name).name in ("copyright", "COPYING", "COPYING3",
                                                "COPYING.RUNTIME")
        return selected(name, gtk=True)
    if gtk:
        if name.startswith("usr/share/pkgconfig/"):
            return True
        if name.startswith(("etc/fonts/", "etc/gtk-2.0/", "etc/pango/", "usr/share/doc/",
                            "usr/share/gtk-2.0/", "usr/share/fontconfig/", "usr/share/libthai/")):
            return True
        if name.startswith(("lib/", "usr/lib/")):
            basename = PurePosixPath(name).name
            return basename.endswith((".h", ".pc", ".a", ".la", ".so")) or ".so." in basename
        return False
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


def extract(archive, destination, runtime=False, gtk=False, sdk=False):
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
            if not selected(name, runtime, gtk, sdk):
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
                path.chmod(0o755 if member.mode & 0o111 else 0o644)
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


def expose_x86_multilib(sysroot, lib_directory):
    """Keep host GCC's -m32 searches inside the single-architecture SDK."""
    if lib_directory != "lib":
        return
    # GCC tries the multilib suffix across every -B prefix before trying any
    # unsuffixed prefix. Without these aliases, an installed host lib32 can
    # supply crt1.o, libc and libgcc ahead of our pinned i386 files.
    for relative in (LEGACY_X86_GCC_DIRECTORY, "usr/lib"):
        directory = sysroot / relative
        alias = directory / GCC_X86_MULTILIB_DIRECTORY
        if alias.is_symlink() and alias.resolve() == directory.resolve():
            continue
        if alias.exists() or alias.is_symlink():
            raise RuntimeError(f"Unexpected SDK multilib directory: {alias}")
        alias.symlink_to(".")


def prepare_gtk(index_url, index_checksum, cache, destination):
    """Extract a dependency closure from one immutable, verified Debian index."""
    index = download(index_url, index_checksum, cache / (index_checksum + ".Packages.gz"))
    packages = {}
    for stanza in gzip.decompress(index.read_bytes()).decode("utf-8").split("\n\n"):
        fields = {}
        for line in stanza.splitlines():
            if line[:1].isspace():
                continue
            key, separator, value = line.partition(": ")
            if separator:
                fields[key] = value
        if "Package" in fields:
            packages[fields["Package"]] = fields
    names = set()
    pending = list(GTK_ROOT_PACKAGES)
    while pending:
        name = pending.pop()
        if name in names:
            continue
        names.add(name)
        for group in packages[name].get("Depends", "").split(","):
            dependency = group.strip().split(" ", 1)[0]
            if dependency in GTK_EXTERNAL_PACKAGES:
                continue
            if dependency in packages and (
                dependency.startswith(("lib", "zlib")) or dependency.endswith("-dev")
            ):
                pending.append(dependency)
    manifest = []
    for name in sorted(names):
        package = packages[name]
        checksum = package["SHA256"]
        url = GTK_ARCHIVE_ROOT + package["Filename"]
        archive = download(url, checksum, cache / (checksum + ".deb"))
        with tempfile.TemporaryDirectory(prefix="restunts-gtk-package-") as directory:
            contents = Path(directory) / "contents.tar"
            with contents.open("wb") as output:
                subprocess.run(["dpkg-deb", "--fsys-tarfile", str(archive)], stdout=output,
                               check=True)
            extract(contents, destination, gtk=True)
        manifest.append(f"{name} {package['Version']} {checksum} {url}")
    patch_gtk_headers(destination)
    (destination / "packages.txt").write_text("\n".join(manifest) + "\n", encoding="utf-8")


def debian_package_index(index):
    packages = {}
    for stanza in gzip.decompress(index.read_bytes()).decode("utf-8").split("\n\n"):
        fields = {}
        for line in stanza.splitlines():
            key, separator, value = line.partition(": ")
            if separator and not line[:1].isspace():
                fields[key] = value
        if "Package" in fields:
            packages[fields["Package"]] = fields
    return packages


def compiler_include_arguments(compiler, sysroot, triple):
    """Retain compiler intrinsics while excluding the host cross-libc headers."""
    directories = []
    for resource in ("include", "include-fixed"):
        path = Path(subprocess.check_output(
            [compiler, "-print-file-name=" + resource], text=True).strip())
        if path.is_dir():
            directories.append(path)
        elif resource == "include":
            raise RuntimeError("The cross compiler is missing its intrinsic headers")
    directories.extend((sysroot / "usr/include" / triple, sysroot / "usr/include"))
    return ["-nostdinc", *("-isystem" + str(path) for path in directories)]


def debian_sdk_packages(packages, roots):
    """Follow library/header dependencies without extracting obsolete programs."""
    names, pending = set(), list(roots)
    while pending:
        name = pending.pop()
        if name in names:
            continue
        if name not in packages:
            raise RuntimeError(f"Missing pinned SDK package: {name}")
        names.add(name)
        for field in ("Depends", "Pre-Depends"):
            for group in packages[name].get(field, "").split(","):
                alternatives = [alternative.strip().split(" ", 1)[0].split(":", 1)[0]
                                for alternative in group.split("|")]
                dependency = next((item for item in alternatives if item in packages), None)
                if dependency and (dependency.startswith(("lib", "zlib")) or
                                   dependency.endswith("-dev") or dependency in roots):
                    pending.append(dependency)
    return sorted(names)


def prepare_debian_sdk(index_url, index_checksum, archive_root, cache, destination, roots):
    index = download(index_url, index_checksum, cache / (index_checksum + ".Packages.gz"))
    packages = debian_package_index(index)
    manifest = []
    for name in debian_sdk_packages(packages, roots):
        package = packages[name]
        checksum = package["SHA256"]
        url = urllib.parse.urljoin(archive_root, package["Filename"])
        archive = download(url, checksum, cache / (checksum + ".deb"))
        with tempfile.TemporaryDirectory(prefix="restunts-sdk-package-") as directory:
            contents = Path(directory) / "contents.tar"
            with contents.open("wb") as output:
                subprocess.run(["dpkg-deb", "--fsys-tarfile", str(archive)], stdout=output,
                               check=True)
            extract(contents, destination, sdk=True)
        manifest.append(f"{name} {package['Version']} {checksum} {url}")
    (destination / "packages.txt").write_text("\n".join(manifest) + "\n", encoding="utf-8")


def copy_debian_runtime(sysroot, destination, triple):
    """Flatten multiarch SONAMEs while retaining GTK's module directories."""
    module_directories = frozenset(("gtk-2.0", "gdk-pixbuf-2.0", "pango", "gio"))
    output = destination / "lib"
    output.mkdir(parents=True, exist_ok=True)
    for relative in ("lib", "usr/lib"):
        library_root = sysroot / relative
        for library in library_root.rglob("*"):
            if not library.is_file() or ".so" not in library.name:
                continue
            with library.open("rb") as stream:
                if stream.read(4) != b"\x7fELF":
                    continue
            parts = library.relative_to(library_root).parts
            if parts[0] == triple:
                parts = parts[1:]
            target = output.joinpath(*parts) if parts[0] in module_directories else output / library.name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(library.resolve(), target)
    for relative, target in (("etc/fonts", "etc/fonts"), ("usr/share/alsa", "share/alsa"),
                             ("usr/share/X11", "share/X11"), ("usr/share/libthai", "share/libthai"),
                             ("usr/share/common-licenses", "share/licenses/debian")):
        source = sysroot / relative
        if source.is_dir():
            shutil.copytree(source, destination / target, symlinks=False, dirs_exist_ok=True)
    for copyright in (sysroot / "usr/share/doc").rglob("copyright"):
        if copyright.is_file():
            target = destination / "share/licenses/packages" / copyright.relative_to(sysroot / "usr/share/doc")
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(copyright.resolve(), target)
    source_directory = destination / "share/sources"
    source_directory.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(sysroot / "packages.txt", source_directory / "appimage-sdk-packages.txt")
    loaders = output / "gdk-pixbuf-2.0/2.10.0/loaders"
    legacy_loaders = output / "gtk-2.0/2.10.0/loaders"
    if loaders.is_dir() and not legacy_loaders.exists():
        legacy_loaders.parent.mkdir(parents=True, exist_ok=True)
        legacy_loaders.symlink_to(os.path.relpath(loaders, legacy_loaders.parent))


def prune_runtime(directory, roots=GTK_RUNTIME_ROOTS):
    """Retain the GUI/audio library closure and matching optional GTK modules."""
    libraries = [path for path in directory.rglob("*") if path.is_file() and
                 (path.name.endswith(".so") or ".so." in path.name)]
    pending = [directory / name for name in roots]
    # The startup dialog has no printing UI. Other module families provide
    # text input, font shaping and image decoding and remain relocatable.
    pending.extend(path for path in libraries if path.parent != directory and
                   "printbackends" not in path.relative_to(directory).parts)
    required = set()
    while pending:
        path = pending.pop()
        if path in required:
            continue
        if not path.is_file():
            raise RuntimeError(f"Missing bundled runtime dependency: {path.name}")
        required.add(path)
        needed = subprocess.check_output(["patchelf", "--print-needed", str(path)], text=True)
        for name in needed.splitlines():
            if "/" in name:
                raise RuntimeError(f"Nonrelocatable runtime dependency: {name}")
            if name not in SYSTEM_RUNTIME_LIBRARIES:
                pending.append(directory / name)
    # Resolve the entire graph before removing anything, so a missing library
    # cannot leave a partially pruned SDK behind.
    for path in libraries:
        if path not in required:
            path.unlink()


def patch_cxx_headers(destination, header_relative=None):
    # Modern GCC emits this C++11 ABI helper; the pinned runtime predates it.
    # Keep the standard exception type and inheritance instead of substituting
    # bad_alloc or increasing the glibc requirement with a newer runtime.
    header = destination / (header_relative or (LEGACY_CXX_PREFIX + "/include/c++/4.8.2/new"))
    text = header.read_text()
    if "class bad_array_new_length" not in text:
        declaration = """
#if __cplusplus >= 201103L
  class bad_array_new_length : public bad_alloc
  {
  public:
    bad_array_new_length() throw() { }
    virtual ~bad_array_new_length() throw();
    virtual decltype(bad_alloc().what()) what() const throw();
  };
#endif

"""
        marker = "  struct nothrow_t"
        if text.count(marker) != 1:
            raise RuntimeError("Unexpected pinned C++ allocation header layout")
        header.write_text(text.replace(marker, declaration + marker))


def patch_gtk_headers(destination):
    # wxWidgets uses this GLib 2.28 convenience API with GTK2. Its complete
    # semantics need no newer runtime symbol: destroy each payload, free nodes.
    header = destination / "usr/include/glib-2.0/glib/gslist.h"
    text = header.read_text()
    if "g_slist_free_full" not in text:
        compatibility = """
/* Restunts legacy SDK compatibility for GLib before 2.28. */
static inline void restunts_g_slist_free_full(GSList *list, GDestroyNotify destroy)
{
    if (destroy != NULL) {
        for (GSList *node = list; node != NULL; node = node->next) {
            destroy(node->data);
        }
    }
    g_slist_free(list);
}
#define g_slist_free_full restunts_g_slist_free_full
"""
        if text.count("\nG_END_DECLS") != 1:
            raise RuntimeError("Unexpected pinned GLib singly-linked-list header layout")
        header.write_text(text.replace("\nG_END_DECLS", compatibility + "\nG_END_DECLS"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    fetch = subparsers.add_parser("download")
    fetch.add_argument("url")
    fetch.add_argument("checksum")
    fetch.add_argument("destination", type=Path)
    gtk = subparsers.add_parser("gtk")
    gtk.add_argument("--index-url", required=True)
    gtk.add_argument("--index-sha256", required=True)
    gtk.add_argument("--cache", required=True, type=Path)
    gtk.add_argument("--destination", required=True, type=Path)
    debian = subparsers.add_parser("debian-sdk")
    debian.add_argument("--index-url", required=True)
    debian.add_argument("--index-sha256", required=True)
    debian.add_argument("--archive-root", required=True)
    debian.add_argument("--cache", required=True, type=Path)
    debian.add_argument("--destination", required=True, type=Path)
    debian.add_argument("packages", nargs="+")
    copy_runtime = subparsers.add_parser("debian-runtime")
    copy_runtime.add_argument("sysroot", type=Path)
    copy_runtime.add_argument("destination", type=Path)
    copy_runtime.add_argument("triple")
    cxx = subparsers.add_parser("patch-cxx")
    cxx.add_argument("sysroot", type=Path)
    cxx.add_argument("header")
    includes = subparsers.add_parser("compiler-includes")
    includes.add_argument("compiler")
    includes.add_argument("sysroot", type=Path)
    includes.add_argument("triple")
    includes.add_argument("--shell", action="store_true")
    runtime = subparsers.add_parser("prune-runtime")
    runtime.add_argument("directory", type=Path)
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
    if args.command == "gtk":
        prepare_gtk(args.index_url, args.index_sha256, args.cache, args.destination)
        return
    if args.command == "debian-sdk":
        prepare_debian_sdk(args.index_url, args.index_sha256, args.archive_root,
                           args.cache, args.destination, args.packages)
        return
    if args.command == "debian-runtime":
        copy_debian_runtime(args.sysroot, args.destination, args.triple)
        return
    if args.command == "patch-cxx":
        patch_cxx_headers(args.sysroot, args.header)
        return
    if args.command == "compiler-includes":
        arguments = compiler_include_arguments(args.compiler, args.sysroot, args.triple)
        print(shlex.join(arguments) if args.shell else "\n".join(arguments))
        return
    if args.command == "prune-runtime":
        prune_runtime(args.directory)
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
    patch_cxx_headers(args.sysroot)
    extract(archives["runtime"], args.runtime, runtime=True)
    overlay_runtime(args.runtime, args.sysroot, args.lib_directory)
    expose_x86_multilib(args.sysroot, args.lib_directory)


if __name__ == "__main__":
    main()
