"""Check script-free OpenBSD SDK extraction and packing-list path handling."""

import importlib.util
import io
from pathlib import Path
import tarfile
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "extract-openbsd-sdk-packages.py"
SPEC = importlib.util.spec_from_file_location("openbsd_cross_packages", SCRIPT)
PACKAGES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGES)


class OpenbsdCrossPackagesTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="OpenBSD SDK with spaces ")
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        self.sdk = self.directory / "sdk"
        self.sdk.mkdir()
        self.package = self.directory / "package.tgz"

    def write_package(self, contents, files):
        with tarfile.open(self.package, "w:gz") as archive:
            for name, data in {"+CONTENTS": contents, **files}.items():
                data = data.encode("utf-8")
                member = tarfile.TarInfo(name)
                member.size = len(data)
                member.mode = 0o644
                archive.addfile(member, io.BytesIO(data))

    def test_installs_headers_libraries_and_metadata_under_each_target_prefix(self):
        self.write_package("""@name sdk-1.0
@cwd /usr/local
include/gtk.h
@lib lib/libgtk-3.so.2201.0
@pkgconfig lib/pkgconfig/gtk+-3.0.pc
@cwd /usr/X11R6
include/X11/Xlib.h
""", {"include/gtk.h": "GTK header", "lib/libgtk-3.so.2201.0": "target library",
      "lib/pkgconfig/gtk+-3.0.pc": "prefix=/usr/local\n",
      "include/X11/Xlib.h": "X11 header"})
        PACKAGES.extract_package(self.sdk, self.package)
        self.assertEqual((self.sdk / "usr/local/include/gtk.h").read_text(), "GTK header")
        self.assertEqual((self.sdk / "usr/local/lib/libgtk-3.so.2201.0").read_text(),
                         "target library")
        self.assertEqual((self.sdk / "usr/local/lib/pkgconfig/gtk+-3.0.pc").read_text(),
                         "prefix=/usr/local\n")
        self.assertEqual((self.sdk / "usr/X11R6/include/X11/Xlib.h").read_text(), "X11 header")

    def test_restores_links_recorded_in_packing_list_instead_of_archive_members(self):
        self.write_package("""@cwd /usr/local
include/source.h
include/hard.h
@link include/source.h
include/relative.h
@symlink source.h
include/absolute.h
@symlink /usr/local/include/source.h
""", {"include/source.h": "target header"})
        PACKAGES.extract_package(self.sdk, self.package)
        include = self.sdk / "usr/local/include"
        for name in ("hard.h", "relative.h", "absolute.h"):
            self.assertEqual((include / name).read_text(), "target header")
        self.assertEqual((include / "hard.h").stat().st_ino,
                         (include / "source.h").stat().st_ino)
        self.assertEqual((include / "relative.h").readlink(), Path("source.h"))
        self.assertFalse((include / "absolute.h").readlink().is_absolute())

    def test_accepts_absolute_rcscript_paths_but_does_not_run_package_actions(self):
        marker = self.directory / "package-action-ran"
        self.write_package(f"""@cwd /usr/local
@rcscript /etc/rc.d/messagebus
@exec touch {marker}
@define-tag sdk-test at-end touch {marker}
@tag sdk-test
""", {"/etc/rc.d/messagebus": "#!/bin/sh\nexit 42\n",
      "+INSTALL": f"#!/bin/sh\ntouch {marker}\n"})
        PACKAGES.extract_package(self.sdk, self.package)
        self.assertTrue((self.sdk / "etc/rc.d/messagebus").is_file())
        self.assertFalse(marker.exists())
        self.assertFalse((self.sdk / "+CONTENTS").exists())
        self.assertFalse((self.sdk / "+INSTALL").exists())

    def test_rejects_unlisted_payload_and_traversal_before_writing_outside_sdk(self):
        for contents, name in (("@cwd /usr/local\ninclude/known.h\n", "include/unknown.h"),
                               ("@cwd /usr/local\n../../escaped\n", "../../escaped"),
                               ("@cwd /usr/local\n/etc/escaped\n", "/etc/escaped")):
            with self.subTest(name=name):
                self.write_package(contents, {name: "unexpected"})
                with self.assertRaises(ValueError):
                    PACKAGES.extract_package(self.sdk, self.package)
        self.assertFalse((self.directory / "escaped").exists())

    def test_does_not_follow_existing_sdk_symlink_to_host_directory(self):
        outside = self.directory / "host"
        outside.mkdir()
        (self.sdk / "usr").mkdir()
        (self.sdk / "usr/local").symlink_to(outside, target_is_directory=True)
        self.write_package("@cwd /usr/local\nescaped.h\n", {"escaped.h": "unexpected"})
        with self.assertRaisesRegex(ValueError, "escapes the SDK"):
            PACKAGES.extract_package(self.sdk, self.package)
        self.assertFalse((outside / "escaped.h").exists())

    def test_normalizes_release_set_links_and_rejects_relative_escapes(self):
        library = self.sdk / "usr/lib/libfixture.so"
        library.parent.mkdir(parents=True)
        library.symlink_to("/lib/libfixture.so.1")
        PACKAGES.normalize_links(self.sdk)
        self.assertEqual(library.readlink(), Path("../../lib/libfixture.so.1"))
        library.unlink()
        library.symlink_to("../../../host-library")
        with self.assertRaisesRegex(ValueError, "escapes its root"):
            PACKAGES.normalize_links(self.sdk)


if __name__ == "__main__":
    unittest.main()
