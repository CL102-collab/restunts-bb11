#!/usr/bin/env python3
"""Regression checks for verified downloads and isolated SDK extraction."""

import hashlib
import importlib.util
import io
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest import mock


spec = importlib.util.spec_from_file_location(
    "appimage_sysroot", Path(__file__).with_name("appimage-sysroot.py"))
sdk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sdk)


class SysrootTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def archive(self, filename, entries):
        path = self.root / filename
        with tarfile.open(path, "w:gz") as output:
            for name, content, target in entries:
                member = tarfile.TarInfo(name)
                if target is not None:
                    member.type = tarfile.SYMTYPE
                    member.linkname = target
                    output.addfile(member)
                else:
                    member.size = len(content)
                    output.addfile(member, io.BytesIO(content))
        return path

    def test_download_is_verified_before_installation(self):
        source = self.root / "source"
        source.write_bytes(b"verified compiler input")
        destination = self.root / "cache" / "archive"
        with self.assertRaisesRegex(RuntimeError, "SHA256 mismatch"):
            sdk.download(source.as_uri(), "0" * 64, destination)
        self.assertFalse(destination.exists())
        self.assertEqual(list(destination.parent.glob("*.partial.*")), [])
        sdk.download(source.as_uri(), hashlib.sha256(source.read_bytes()).hexdigest(), destination)
        self.assertEqual(destination.read_bytes(), source.read_bytes())

    def test_absolute_sdk_symlink_stays_inside_sysroot(self):
        archive = self.archive("root.tar.gz", [
            (".", b"", None),
            ("lib/libexample.so.1", b"legacy library", None),
            ("usr/lib/libexample.so", b"", "/lib/libexample.so.1"),
            ("usr/bin/obsolete-compiler", b"must not be extracted", None),
        ])
        destination = self.root / "sysroot"
        sdk.extract(archive, destination)
        link = destination / "usr/lib/libexample.so"
        self.assertTrue(link.is_symlink())
        self.assertEqual(link.read_bytes(), b"legacy library")
        self.assertTrue(link.resolve().is_relative_to(destination))
        self.assertFalse((destination / "usr/bin").exists())

    def test_parent_traversal_is_rejected(self):
        archive = self.archive("traversal.tar.gz", [
            ("usr/include/../../../escape", b"unsafe", None),
        ])
        with self.assertRaisesRegex(RuntimeError, "Unsafe archive member"):
            sdk.extract(archive, self.root / "sysroot")
        self.assertFalse((self.root / "escape").exists())

    def test_relative_symlink_escape_is_rejected(self):
        archive = self.archive("symlink.tar.gz", [
            ("usr/include/escape", b"", "../../../../escape"),
        ])
        with self.assertRaisesRegex(RuntimeError, "symlink escapes SDK"):
            sdk.extract(archive, self.root / "sysroot")

    def test_layer_whiteout_removes_an_old_library(self):
        destination = self.root / "sysroot"
        base = self.archive("base.tar.gz", [("lib/old.so", b"old", None)])
        layer = self.archive("layer.tar.gz", [("lib/.wh.old.so", b"", None)])
        sdk.extract(base, destination)
        sdk.extract(layer, destination)
        self.assertFalse((destination / "lib/old.so").exists())
        self.assertFalse((destination / "lib/.wh.old.so").exists())

    def test_cxx_inputs_are_kept_without_obsolete_compilers(self):
        prefix = "opt/rh/devtoolset-2/root/usr"
        header = prefix + "/include/c++/4.8.2/vector"
        archive_name = prefix + "/lib/gcc/x86_64-redhat-linux/4.8.2/libstdc++.a"
        startup = prefix + "/lib/gcc/x86_64-redhat-linux/4.8.2/crtbeginS.o"
        compiler = prefix + "/bin/g++"
        archive = self.archive("cxx.tar.gz", [
            (header, b"C++11 declarations", None),
            (archive_name, b"old ABI archive", None),
            (startup, b"target startup object", None),
            (compiler, b"must not execute", None),
            ("usr/lib/python/site-packages/unrelated.so", b"unrelated", None),
        ])
        destination = self.root / "sysroot"
        sdk.extract(archive, destination)
        self.assertEqual((destination / header).read_bytes(), b"C++11 declarations")
        self.assertEqual((destination / archive_name).read_bytes(), b"old ABI archive")
        self.assertEqual((destination / startup).read_bytes(), b"target startup object")
        self.assertFalse((destination / compiler).exists())
        self.assertFalse((destination / "usr/lib/python").exists())

    def test_cxx_exception_header_patch_is_idempotent(self):
        header = self.root / sdk.LEGACY_CXX_PREFIX / "include/c++/4.8.2/new"
        header.parent.mkdir(parents=True)
        header.write_text("namespace std {\n  struct nothrow_t { };\n}\n")
        sdk.patch_cxx_headers(self.root)
        patched = header.read_text()
        self.assertIn("class bad_array_new_length : public bad_alloc", patched)
        sdk.patch_cxx_headers(self.root)
        self.assertEqual(header.read_text(), patched)

    def test_runtime_pruning_keeps_transitive_and_module_dependencies(self):
        libraries = self.root / "lib"
        module = "gtk-2.0/immodules/im-test.so"
        printer = "gtk-2.0/printbackends/cups.so"
        graph = {"libroot.so.1": "libdependency.so.1\nlibc.so.6\n",
                 "libdependency.so.1": "libz.so.1\n", "libz.so.1": "",
                 module: "libroot.so.1\n", printer: "libmissing-printer.so.1\n",
                 "libunused.so.1": ""}
        for name in graph:
            path = libraries / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"library fixture")
        def dependencies(command, **kwargs):
            return graph[str(Path(command[-1]).relative_to(libraries))]
        with mock.patch.object(sdk.subprocess, "check_output", side_effect=dependencies):
            sdk.prune_runtime(libraries, roots=("libroot.so.1",))
        remaining = {str(path.relative_to(libraries)) for path in libraries.rglob("*")
                     if path.is_file()}
        self.assertEqual(remaining, {"libroot.so.1", "libdependency.so.1", "libz.so.1", module})

    def test_runtime_pruning_retains_native_pulse_client_and_its_private_library(self):
        libraries = self.root / "lib"
        libraries.mkdir()
        graph = {name: "" for name in sdk.GTK_RUNTIME_ROOTS}
        graph["libpulse.so.0"] = "libpulsecommon-0.9.21.so\nlibc.so.6\n"
        graph["libpulsecommon-0.9.21.so"] = "libwrap.so.0\nlibdbus-1.so.3\n"
        graph["libwrap.so.0"] = "libc.so.6\n"
        graph["libdbus-1.so.3"] = "libpthread.so.0\n"
        graph["libunused-audio.so.1"] = ""
        for name in graph:
            (libraries / name).write_bytes(b"audio library fixture")
        def dependencies(command, **kwargs):
            return graph[Path(command[-1]).name]
        with mock.patch.object(sdk.subprocess, "check_output", side_effect=dependencies):
            sdk.prune_runtime(libraries)
        self.assertTrue((libraries / "libpulse.so.0").is_file())
        self.assertTrue((libraries / "libpulsecommon-0.9.21.so").is_file())
        self.assertTrue((libraries / "libwrap.so.0").is_file())
        self.assertFalse((libraries / "libunused-audio.so.1").exists())
        self.assertFalse((libraries / "libnsl.so.1").exists())
        graph["libwrap.so.0"] = "libnsl.so.1\nlibc.so.6\n"
        with mock.patch.object(sdk.subprocess, "check_output", side_effect=dependencies):
            with self.assertRaisesRegex(RuntimeError, "Missing bundled runtime dependency: libnsl"):
                sdk.prune_runtime(libraries)

    def test_runtime_missing_dependency_fails_before_pruning(self):
        libraries = self.root / "lib"
        libraries.mkdir()
        root = libraries / "libroot.so.1"
        unused = libraries / "libunused.so.1"
        root.write_bytes(b"root")
        unused.write_bytes(b"unused")
        with mock.patch.object(sdk.subprocess, "check_output", return_value="libmissing.so.1\n"):
            with self.assertRaisesRegex(RuntimeError, "Missing bundled runtime dependency"):
                sdk.prune_runtime(libraries, roots=(root.name,))
        self.assertTrue(unused.exists())

    def test_gtk_nested_headers_and_modules_exclude_old_tools(self):
        header = "usr/lib/glib-2.0/include/glibconfig.h"
        module = "usr/lib/pango/1.6.0/modules/pango-basic-fc.so"
        archive = self.archive("gtk.tar.gz", [
            (header, b"target configuration", None),
            (module, b"matching renderer", None),
            ("usr/bin/pango-querymodules", b"obsolete tool", None),
            ("usr/lib/gtk-2.0/old-helper", b"obsolete helper", None),
        ])
        destination = self.root / "gtk"
        sdk.extract(archive, destination, gtk=True)
        self.assertEqual((destination / header).read_bytes(), b"target configuration")
        self.assertEqual((destination / module).read_bytes(), b"matching renderer")
        self.assertFalse((destination / "usr/bin").exists())
        self.assertFalse((destination / "usr/lib/gtk-2.0/old-helper").exists())


if __name__ == "__main__":
    unittest.main()
