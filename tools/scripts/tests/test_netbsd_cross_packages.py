"""Check target dependency resolution and script-free NetBSD SDK extraction."""

import gzip
import importlib.util
import io
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).resolve().parents[1] / "netbsd-cross-packages.py"
SPEC = importlib.util.spec_from_file_location("netbsd_cross_packages", SCRIPT)
PACKAGES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGES)


class NetbsdCrossPackagesTests(unittest.TestCase):
    def test_armv7_summary_requires_netbsd_hard_float_machine_architecture(self):
        with tempfile.TemporaryDirectory() as temporary:
            summary = Path(temporary) / "pkg_summary.gz"
            with gzip.open(summary, "wt") as stream:
                stream.write("PKGNAME=gtk3+-3.24\nOPSYS=NetBSD\nMACHINE_ARCH=earmv7hf\n\n")
            self.assertEqual(set(PACKAGES.read_summary(summary, "earmv7hf")), {"gtk3+-3.24"})
            # LLVM's processor spelling must not substitute for pkgsrc's ABI
            # name, or silently select a soft-float or different ARM release.
            for architecture in ("armv7", "earmv7", "earmv6hf", "aarch64"):
                with self.subTest(architecture=architecture):
                    with self.assertRaisesRegex(RuntimeError, "different target"):
                        PACKAGES.read_summary(summary, architecture)

    def test_target_summary_rejects_host_packages(self):
        with tempfile.TemporaryDirectory() as temporary:
            summary = Path(temporary) / "pkg_summary.gz"
            with gzip.open(summary, "wt") as stream:
                stream.write("PKGNAME=gtk3+-3.24\nOPSYS=NetBSD\nMACHINE_ARCH=x86_64\n\n")
            with self.assertRaisesRegex(RuntimeError, "different target"):
                PACKAGES.read_summary(summary, "aarch64")

    def test_closure_follows_versioned_and_wildcard_dependencies_without_host_packages(self):
        packages = {
            "gtk3+-3.24": {"DEPENDS": ["glib2>=2.80", "mozilla-rootcerts-[0-9]*"]},
            "glib2-2.82": {"DEPENDS": ["gtk3+>=3.0"]},
            "mozilla-rootcerts-20250804": {},
            "unrelated-1.0": {},
        }
        expected = {
            ("gtk3+>=0", "gtk3+-3.24"),
            ("gtk3+>=3.0", "gtk3+-3.24"),
            ("glib2>=2.80", "glib2-2.82"),
            ("mozilla-rootcerts-[0-9]*", "mozilla-rootcerts-20250804"),
        }

        def pmatch(arguments, **unused):
            self.assertEqual(arguments[:2], ["pkg_admin", "pmatch"])
            self.assertIn(tuple(arguments[2:]), expected)
            return subprocess.CompletedProcess(arguments, 0)

        with patch.object(PACKAGES.subprocess, "run", side_effect=pmatch):
            selected = PACKAGES.dependency_closure(packages, ["gtk3+>=0"])
        self.assertEqual(set(selected), set(packages) - {"unrelated-1.0"})

    def test_missing_dependency_fails(self):
        with self.assertRaisesRegex(RuntimeError, "No package satisfies"):
            PACKAGES.dependency_closure({}, ["gtk3+>=0"])

    def test_uses_native_version_order_when_summary_contains_multiple_versions(self):
        packages = {"libxml2-2.9": {}, "libxml2-2.15": {}}
        with patch.object(PACKAGES.subprocess, "run") as pmatch:
            pmatch.return_value = subprocess.CompletedProcess([], 0)
            selected = PACKAGES.dependency_closure(packages, ["libxml2>=2.9"])
        self.assertEqual(set(selected), {"libxml2-2.15"})
        self.assertEqual(pmatch.call_args.args[0],
                         ["pkg_admin", "pmatch", "libxml2>2.9", "libxml2-2.15"])

    def write_package(self, path, entries, prefix="/usr/pkg"):
        with tarfile.open(path, "w:gz") as archive:
            entries = {"+CONTENTS": f"@cwd {prefix}\n", **entries}
            for name, content in entries.items():
                data = content.encode()
                member = tarfile.TarInfo(name)
                member.size = len(data)
                archive.addfile(member, io.BytesIO(data))

    def test_extracts_headers_without_package_scripts_or_metadata(self):
        with tempfile.TemporaryDirectory() as temporary:
            package = Path(temporary) / "gtk.tgz"
            prefix = Path(temporary) / "sdk/usr/pkg"
            self.write_package(package, {"include/gtk.h": "target header",
                                         "+INSTALL": "exit 77\n"})
            PACKAGES.extract_package(package, prefix)
            self.assertEqual((prefix / "include/gtk.h").read_text(), "target header")
            self.assertFalse((prefix / "+CONTENTS").exists())
            self.assertFalse((prefix / "+INSTALL").exists())

    def test_rejects_unexpected_prefix_and_path_traversal(self):
        for entries, prefix in (({"../../escaped": "bad"}, "/usr/pkg"),
                                ({"include/gtk.h": "header"}, "/usr/local")):
            with self.subTest(prefix=prefix), tempfile.TemporaryDirectory() as temporary:
                package = Path(temporary) / "gtk.tgz"
                self.write_package(package, entries, prefix)
                with self.assertRaises((RuntimeError, tarfile.FilterError)):
                    PACKAGES.extract_package(package, Path(temporary) / "sdk/usr/pkg")

    def test_rewrites_absolute_sdk_links_and_rejects_relative_escapes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "sdk"
            (root / "usr/lib").mkdir(parents=True)
            library = root / "usr/lib/libc.so"
            library.symlink_to("/lib/libc.so.12")
            PACKAGES.normalize_links(root)
            self.assertEqual(library.readlink(), Path("../../lib/libc.so.12"))
            library.unlink()
            library.symlink_to("../../../outside")
            with self.assertRaisesRegex(RuntimeError, "escapes its root"):
                PACKAGES.normalize_links(root)


if __name__ == "__main__":
    unittest.main()
