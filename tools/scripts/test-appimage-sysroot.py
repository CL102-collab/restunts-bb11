#!/usr/bin/env python3
"""Regression checks for verified downloads and isolated SDK extraction."""

import hashlib
import importlib.util
import io
from pathlib import Path
import tarfile
import tempfile
import unittest


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


if __name__ == "__main__":
    unittest.main()
