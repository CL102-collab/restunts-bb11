"""Check pinned Android source preparation without an SDK installation or downloads."""

import hashlib
import importlib.util
import io
import os
from pathlib import Path
import shutil
import tarfile
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[3]


def load_script(name):
    script = ROOT / "tools/scripts" / (name + ".py")
    spec = importlib.util.spec_from_file_location(name.replace("-", "_"), script)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ANDROID = load_script("prepare-android")
GENGHOST = load_script("prepare-genghost-android")


def source_archive(files):
    output = io.BytesIO()
    with tarfile.open(fileobj=output, mode="w:gz") as archive:
        for path, content in files.items():
            member = tarfile.TarInfo("upstream/" + path)
            data = content.encode()
            member.size = len(data)
            archive.addfile(member, io.BytesIO(data))
    return output.getvalue()


class AndroidPreparationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="android preparation ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.url = "https://example.test/sdl.tar.gz"
        self.archive = source_archive({
            "LICENSE.txt": "SDL license\n",
            "android-project/gradlew": "#!/bin/sh\nexit 0\n",
            "android-project/gradle/wrapper/gradle-wrapper.properties": "fixture=true\n",
        })
        self.checksum = hashlib.sha256(self.archive).hexdigest()

    def write_pin(self, source=None):
        if source is None:
            source = ("FetchContent_Declare(SDL3\n    URL\n    " + self.url +
                      "\n    URL_HASH SHA256=" + self.checksum +
                      "\n    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)\n")
        (self.root / "CMakeLists.txt").write_text(source)

    def test_pin_accepts_cmake_whitespace_quotes_and_option_order(self):
        declarations = (
            f"FetchContent_Declare(SDL3 URL {self.url} URL_HASH SHA256={self.checksum})",
            f"FetchContent_Declare(SDL3\n URL\n {self.url}\n URL_HASH\n SHA256={self.checksum})",
            f"FetchContent_Declare ( SDL3\tURL\t{self.url}\tURL_HASH\tSHA256={self.checksum} )",
            f'fetchcontent_declare(SDL3 URL_HASH "SHA256={self.checksum.upper()}"\n'
            f' DOWNLOAD_EXTRACT_TIMESTAMP TRUE URL "{self.url}")',
        )
        for declaration in declarations:
            with self.subTest(declaration=declaration):
                self.write_pin(declaration)
                self.assertEqual(ANDROID.dependency_pin(self.root, "SDL3", "CMakeLists.txt"),
                                 (self.url, self.checksum))

    def test_repository_pins_are_read_from_their_actual_cmake_layouts(self):
        for name, configuration in (("SDL3", "CMakeLists.txt"),
                                    ("restunts_vpx", "cmake/webm.cmake"),
                                    ("restunts_nestegg", "cmake/webm.cmake")):
            with self.subTest(name=name):
                url, checksum = ANDROID.dependency_pin(ROOT, name, configuration)
                self.assertTrue(url.startswith("https://github.com/"), url)
                self.assertRegex(checksum, r"^[0-9a-f]{64}$")

    def test_pin_does_not_take_a_checksum_from_another_dependency(self):
        self.write_pin(f"FetchContent_Declare(SDL3 URL {self.url})\n"
                       f"FetchContent_Declare(other URL {self.url} URL_HASH SHA256={self.checksum})")
        with self.assertRaisesRegex(RuntimeError, "Cannot locate the pinned SDL3"):
            ANDROID.dependency_pin(self.root, "SDL3", "CMakeLists.txt")

    def test_missing_or_invalid_sha256_is_rejected_before_downloading(self):
        for digest in ("", "SHA256=abc", "MD5=" + self.checksum, "SHA256=" + "g" * 64):
            with self.subTest(digest=digest):
                self.write_pin(f"FetchContent_Declare(SDL3 URL {self.url} URL_HASH {digest})")
                with mock.patch.object(ANDROID.urllib.request, "urlopen") as download:
                    with self.assertRaisesRegex(RuntimeError, "Cannot locate the pinned SDL3"):
                        ANDROID.dependency(self.root, "SDL3", "CMakeLists.txt", "sdl")
                    download.assert_not_called()

    def test_download_is_verified_and_reused_from_the_checksum_cache(self):
        self.write_pin()
        with mock.patch.object(ANDROID.urllib.request, "urlopen",
                               return_value=io.BytesIO(self.archive)) as download:
            destination = ANDROID.dependency(self.root, "SDL3", "CMakeLists.txt", "sdl")
        download.assert_called_once_with(self.url, timeout=120)
        self.assertEqual((destination / "LICENSE.txt").read_text(), "SDL license\n")
        self.assertEqual((destination / ".restunts-checksum").read_text(), self.checksum + "\n")
        with mock.patch.object(ANDROID.urllib.request, "urlopen") as download:
            self.assertEqual(ANDROID.dependency(self.root, "SDL3", "CMakeLists.txt", "sdl"),
                             destination)
            download.assert_not_called()

    def test_bad_download_preserves_the_existing_source_and_marker(self):
        self.write_pin()
        destination = self.root / "out/android-deps/sdl"
        destination.mkdir(parents=True)
        (destination / ".restunts-checksum").write_text("old checksum\n")
        (destination / "LICENSE.txt").write_text("old source\n")
        with mock.patch.object(ANDROID.urllib.request, "urlopen",
                               return_value=io.BytesIO(b"wrong download")):
            with self.assertRaisesRegex(RuntimeError, "SDL3 source checksum mismatch"):
                ANDROID.dependency(self.root, "SDL3", "CMakeLists.txt", "sdl")
        self.assertEqual((destination / ".restunts-checksum").read_text(), "old checksum\n")
        self.assertEqual((destination / "LICENSE.txt").read_text(), "old source\n")
        self.assertFalse(destination.with_name("sdl-importing").exists())

    def test_both_preparation_entrypoints_use_the_verified_shared_sdl_source(self):
        self.write_pin()
        vpx = source_archive({"LICENSE": "vpx license\n", "PATENTS": "vpx patents\n"})
        nestegg = source_archive({"LICENSE": "nestegg license\n"})
        vulkan = source_archive({"LICENSE.md": "Vulkan headers notice\n",
                                 "LICENSES/Apache-2.0.txt": "Apache license\n",
                                 "LICENSES/MIT.txt": "MIT license\n"})
        archives = {self.url: self.archive, "https://example.test/vpx.tar.gz": vpx,
                    "https://example.test/nestegg.tar.gz": nestegg,
                    "https://example.test/vulkan.tar.gz": vulkan}
        (self.root / "cmake").mkdir()
        (self.root / "cmake/webm.cmake").write_text("\n".join(
            f"FetchContent_Declare(restunts_{name}\n URL https://example.test/{name}.tar.gz\n"
            f" URL_HASH SHA256={hashlib.sha256(data).hexdigest()})"
            for name, data in (("vpx", vpx), ("nestegg", nestegg))))
        (self.root / "cmake/graphics.cmake").write_text(
            "FetchContent_Declare(restunts_vulkan_headers\n URL https://example.test/vulkan.tar.gz\n"
            f" URL_HASH SHA256={hashlib.sha256(vulkan).hexdigest()})\n")
        for path in ("THIRD-PARTY-NOTICES.txt", "third_party/nuked-opl2-lite/LICENSE",
                     "third_party/stb/LICENSE", "docs/genghost.md", "docs/genghost-android.md"):
            destination = self.root / path
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(path + "\n")
        helper = self.root / "tools/scripts/prepare-android.py"
        helper.parent.mkdir(parents=True)
        shutil.copy2(ROOT / "tools/scripts/prepare-android.py", helper)
        (self.root / "android").mkdir()
        (self.root / "android/toolchain.properties").write_text("ANDROID_NDK=fixture-version\n")
        sdk = self.root / "sdk"
        ndk = sdk / "ndk/fixture-version"
        ndk.mkdir(parents=True)
        for name in ("NOTICE", "NOTICE.toolchain"):
            (ndk / name).write_text(name + "\n")
        with mock.patch.object(ANDROID.urllib.request, "urlopen",
                               side_effect=lambda url, **kwargs: io.BytesIO(archives[url])) as download:
            ANDROID.prepare(self.root)
            self.assertEqual(download.call_count, len(archives))
        wrapper = self.root / "out/android-gradle/gradlew"
        self.assertTrue(wrapper.stat().st_mode & 0o111)
        self.assertTrue((wrapper.parent / "gradle/wrapper/gradle-wrapper.properties").is_file())
        self.assertEqual((self.root / "android/generated-assets/licenses/SDL-LICENSE.txt").read_text(),
                         "SDL license\n")
        self.assertEqual((self.root / "android/generated-assets/licenses/Vulkan-Headers-MIT.txt").read_text(),
                         "MIT license\n")
        with mock.patch.dict(os.environ, {"ANDROID_HOME": str(sdk)}), \
                mock.patch.object(ANDROID.urllib.request, "urlopen") as download:
            GENGHOST.prepare(self.root)
            download.assert_not_called()
        licenses = self.root / "android/genghost/generated-assets/licenses"
        self.assertEqual((licenses / "SDL-LICENSE.txt").read_text(), "SDL license\n")
        self.assertEqual((licenses / "Android-NDK-NOTICE.toolchain.txt").read_text(), "NOTICE.toolchain\n")
        self.assertTrue((licenses.parent / "genghost.md").is_file())


if __name__ == "__main__":
    unittest.main()
