"""Check that Pages assembly preserves browser distribution boundaries."""

import argparse
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import stat
import tempfile
import unittest
from unittest import mock
import zipfile


SCRIPT = Path(__file__).resolve().parents[1] / "build-pages.py"
SPEC = importlib.util.spec_from_file_location("build_pages", SCRIPT)
PAGES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PAGES)
PACKAGES = PAGES.PACKAGES
COMMIT = "a" * 40
OTHER_COMMIT = "b" * 40
FIXTURE_DATA = b"Pages fixture\n"


class BuildPagesTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.site = self.root / "site"
        (self.site / "assets").mkdir(parents=True)
        for name in PAGES.WEBSITE_FILES:
            path = self.site / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(FIXTURE_DATA)
        (self.site / "assets/icon.svg").write_bytes(b"<svg/>")
        downloads = self.site / PAGES.TEMPLEOS_DOWNLOAD_DIRECTORY
        downloads.mkdir(parents=True)
        for name in PAGES.TEMPLEOS_DOWNLOAD_FILES:
            (downloads / name).write_bytes(FIXTURE_DATA)
        (downloads / PAGES.TEMPLEOS_CHECKSUM_FILE).write_text(
            "".join(f"{PACKAGES.sha256(FIXTURE_DATA)}  {name}\n"
                    for name in PAGES.TEMPLEOS_DOWNLOAD_FILES), encoding="ascii")
        runtime = self.root / "runtime"
        for name in PACKAGES.required_files("browser") - {PACKAGES.README}:
            path = runtime / name
            path.parent.mkdir(parents=True, exist_ok=True)
            car_source = PACKAGES.custom_car_source(name)
            path.write_bytes(car_source.read_bytes() if car_source is not None else FIXTURE_DATA)
        args = argparse.Namespace(target="browser", runtime=runtime,
                                  directory=self.root / "packages", commit=COMMIT)
        with mock.patch.object(PACKAGES.subprocess, "check_output", return_value=COMMIT + "\n"), \
                contextlib.redirect_stdout(io.StringIO()):
            PACKAGES.create_package(args)
        self.archive = args.directory / PACKAGES.archive_name("browser")
        self.output = self.root / "published"

    def build(self, commit=COMMIT):
        with contextlib.redirect_stdout(io.StringIO()):
            PAGES.build_pages(self.archive, self.output, commit, source=self.site)

    def rewrite_archive(self, files, modes):
        with zipfile.ZipFile(self.archive, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for name, data in files.items():
                info = zipfile.ZipInfo(name)
                info.create_system = 3
                info.external_attr = (stat.S_IFREG | modes.get(name, PACKAGES.FILE_MODE)) << 16
                archive.writestr(info, data)
        self.archive.with_name(self.archive.name + ".sha256").write_text(
            f"{PACKAGES.sha256(self.archive.read_bytes())}  {self.archive.name}\n", encoding="ascii")

    def test_complete_browser_distribution_and_only_landing_assets_are_published(self):
        (self.site / "MISC.RES").write_bytes(b"must not be published")
        downloads = self.site / PAGES.TEMPLEOS_DOWNLOAD_DIRECTORY
        (downloads / "STUNTSDATA.ISO").write_bytes(b"private game data must not be published")
        screenshots = self.site / "assets/templeos"
        screenshots.mkdir()
        (screenshots / "cockpit.png").write_bytes(FIXTURE_DATA)
        self.build()
        published_archive = self.output / "game" / PAGES.DOWNLOAD_NAME
        self.assertEqual(published_archive.read_bytes(), self.archive.read_bytes())
        PACKAGES.verify_package(published_archive, "browser", COMMIT)
        for name in PAGES.WEBSITE_FILES:
            self.assertEqual((self.output / name).read_bytes(), FIXTURE_DATA)
        for name in PAGES.HOSTED_GAME_FILES:
            self.assertEqual((self.output / "game" / name).read_bytes(), FIXTURE_DATA)
        self.assertTrue((self.output / ".nojekyll").is_file())
        self.assertEqual((self.output / "assets/icon.svg").read_bytes(), b"<svg/>")
        self.assertFalse((self.output / "MISC.RES").exists())
        self.assertEqual((self.output / "assets/templeos/cockpit.png").read_bytes(), FIXTURE_DATA)
        published_downloads = self.output / PAGES.TEMPLEOS_DOWNLOAD_DIRECTORY
        self.assertEqual({path.name for path in published_downloads.iterdir()},
                         {*PAGES.TEMPLEOS_DOWNLOAD_FILES, PAGES.TEMPLEOS_CHECKSUM_FILE})
        for name in PAGES.TEMPLEOS_DOWNLOAD_FILES:
            self.assertEqual((published_downloads / name).read_bytes(), FIXTURE_DATA)

    def test_modified_templeos_download_fails_before_writing_output(self):
        downloads = self.site / PAGES.TEMPLEOS_DOWNLOAD_DIRECTORY
        (downloads / PAGES.TEMPLEOS_DOWNLOAD_FILES[0]).write_bytes(b"corrupt download")
        with self.assertRaisesRegex(ValueError, "TempleOS download SHA256 mismatch"):
            self.build()
        self.assertFalse(self.output.exists())

    def test_missing_templeos_downloads_fail_before_writing_output(self):
        downloads = self.site / PAGES.TEMPLEOS_DOWNLOAD_DIRECTORY
        for name in (*PAGES.TEMPLEOS_DOWNLOAD_FILES, PAGES.TEMPLEOS_CHECKSUM_FILE):
            with self.subTest(name=name):
                path = downloads / name
                content = path.read_bytes()
                path.unlink()
                try:
                    with self.assertRaisesRegex(ValueError, "Missing regular website file"):
                        self.build()
                    self.assertFalse(self.output.exists())
                finally:
                    path.write_bytes(content)

    def test_templeos_download_symlinks_fail_before_writing_output(self):
        downloads = self.site / PAGES.TEMPLEOS_DOWNLOAD_DIRECTORY
        for name in (*PAGES.TEMPLEOS_DOWNLOAD_FILES, PAGES.TEMPLEOS_CHECKSUM_FILE):
            with self.subTest(name=name):
                path = downloads / name
                external = self.root / name
                path.rename(external)
                path.symlink_to(external)
                try:
                    with self.assertRaisesRegex(ValueError, "Missing regular website file"):
                        self.build()
                    self.assertFalse(self.output.exists())
                finally:
                    path.unlink()
                    external.rename(path)

    def test_nested_website_directory_symlink_is_rejected(self):
        subsite = self.site / "templeos"
        relocated = self.root / "external-subsite"
        subsite.rename(relocated)
        subsite.symlink_to(relocated, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, "symlink"):
            self.build()
        self.assertFalse(self.output.exists())

    def test_wrong_source_revision_fails_before_writing_output(self):
        with self.assertRaisesRegex(ValueError, "Wrong source commit"):
            self.build(OTHER_COMMIT)
        self.assertFalse(self.output.exists())

    def test_modified_package_fails_before_writing_output(self):
        with self.archive.open("ab") as archive:
            archive.write(b"unexpected change")
        with self.assertRaisesRegex(ValueError, "SHA256 mismatch"):
            self.build()
        self.assertFalse(self.output.exists())

    def test_missing_relink_material_fails_even_with_updated_inventory_and_checksum(self):
        files, modes = PACKAGES.archive_contents(self.archive)
        omitted = "share/restunts/wasm-relink/objects/main.c.o"
        del files[omitted]
        manifest = json.loads(files[PACKAGES.MANIFEST])
        manifest["files"] = [entry for entry in manifest["files"] if entry["path"] != omitted]
        files[PACKAGES.MANIFEST] = json.dumps(manifest).encode()
        self.rewrite_archive(files, modes)
        with self.assertRaisesRegex(ValueError, "missing packaged files"):
            self.build()
        self.assertFalse(self.output.exists())

    def test_original_resources_and_traversal_fail_before_writing_output(self):
        files, modes = PACKAGES.archive_contents(self.archive)
        for name in ("MISC.RES", "../outside.txt"):
            with self.subTest(name=name):
                self.rewrite_archive({**files, name: FIXTURE_DATA}, modes)
                with self.assertRaises(ValueError):
                    self.build()
                self.assertFalse(self.output.exists())
                self.assertFalse((self.root / "outside.txt").exists())

    def test_asset_symlinks_and_original_data_are_rejected(self):
        link = self.site / "assets/outside.svg"
        link.symlink_to(self.archive)
        with self.assertRaisesRegex(ValueError, "symlink"):
            self.build()
        self.assertFalse(self.output.exists())
        link.unlink()
        (self.site / "assets/MISC.RES").write_bytes(FIXTURE_DATA)
        with self.assertRaisesRegex(ValueError, "Original game content"):
            self.build()
        self.assertFalse(self.output.exists())

    def test_existing_output_is_preserved(self):
        self.output.mkdir()
        previous = self.output / "keep.txt"
        previous.write_bytes(FIXTURE_DATA)
        with self.assertRaisesRegex(ValueError, "must be empty"):
            self.build()
        self.assertEqual(previous.read_bytes(), FIXTURE_DATA)
        self.assertEqual(list(self.output.iterdir()), [previous])


if __name__ == "__main__":
    unittest.main()
