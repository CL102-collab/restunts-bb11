"""Exercise the standalone editor's real CMake generation and Runtime install."""

from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[3]
MODULE = ROOT / "cmake/track-editor.cmake"
SOURCE_DIRECTORY = Path("src/restunts/platform/wasm")
EDITOR_DIRECTORY = Path("share/restunts/editor")
DOCUMENT_DIRECTORY = Path("share/docs/restunts")
DOCUMENTS = {"cstrk-format.md", "csrpl-format.md", "cshig-format.md", "track-editor.md"}
COMMAND_TIMEOUT_SECONDS = 60


class TrackEditorInstallTests(unittest.TestCase):
    def setUp(self):
        self.cmake = shutil.which("cmake")
        if self.cmake is None:
            self.skipTest("CMake is needed to check Runtime installation")
        temporary = tempfile.TemporaryDirectory(prefix="restunts editor install ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.project = self.root / "source"
        self.project.mkdir()
        for name in ("src", "tools", "docs"):
            (self.project / name).symlink_to(ROOT / name, target_is_directory=True)
        (self.project / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.25)\n"
            "project(editor_install LANGUAGES NONE)\n"
            f'include("{MODULE.as_posix()}")\n', encoding="utf-8")

    def run_cmake(self, *arguments):
        result = subprocess.run([self.cmake, *map(str, arguments)],
                                text=True, capture_output=True,
                                timeout=COMMAND_TIMEOUT_SECONDS, check=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def configure(self, build, *options):
        generator = ("-G", "Ninja") if shutil.which("ninja") else ()
        self.run_cmake("-S", self.project, "-B", build, *generator,
                       f"-DPython3_EXECUTABLE={sys.executable}", *options)

    def install(self, build, destination):
        self.run_cmake("--install", build, "--prefix", destination, "--component", "Runtime")

    def test_runtime_contains_one_self_contained_editor_for_every_platform(self):
        cases = (
            ("native", (), EDITOR_DIRECTORY),
            ("dos32", ("-DCMAKE_SYSTEM_NAME=DOS",), EDITOR_DIRECTORY),
            ("android", ("-DANDROID=ON",), EDITOR_DIRECTORY),
            ("browser", ("-DEMSCRIPTEN=ON",), Path(".")),
        )
        for name, options, editor_directory in cases:
            with self.subTest(platform=name):
                build = self.root / f"build-{name}"
                destination = self.root / f"package-{name}"
                self.configure(build, *options)
                self.install(build, destination)
                expected_editor = destination / editor_directory / "tracked.html"
                self.assertEqual(list(destination.rglob("*.html")), [expected_editor])
                html = expected_editor.read_text(encoding="utf-8")
                self.assertIn("<canvas", html)
                self.assertIn("<style>", html)
                self.assertIn("TrackEditorModel", html)
                self.assertNotIn("<script src=", html)
                self.assertNotIn('href="track-editor.css"', html)
                files = {path.relative_to(destination) for path in destination.rglob("*")
                         if path.is_file()}
                expected_files = {editor_directory / "tracked.html"}
                if name != "browser":
                    expected_files.update(DOCUMENT_DIRECTORY / document for document in DOCUMENTS)
                    for document in DOCUMENTS:
                        self.assertEqual((destination / DOCUMENT_DIRECTORY / document).read_bytes(),
                                         (ROOT / "docs" / document).read_bytes())
                self.assertEqual(files, expected_files)
                self.assertEqual(expected_editor.read_bytes(), (build / "tracked.html").read_bytes())

    def test_editor_asset_change_regenerates_bundle_before_installation(self):
        if shutil.which("ninja") is None and shutil.which("make") is None:
            self.skipTest("Ninja or Make is needed to test automatic regeneration")
        # Copy the real inputs so editing the fixture never changes repository files.
        for name in ("src", "tools"):
            (self.project / name).unlink()
        source = self.project / SOURCE_DIRECTORY
        source.mkdir(parents=True)
        for path in (ROOT / SOURCE_DIRECTORY).glob("track-editor*"):
            shutil.copyfile(path, source / path.name)
        builder = self.project / "tools/scripts/build-track-editor.py"
        builder.parent.mkdir(parents=True)
        shutil.copyfile(ROOT / "tools/scripts/build-track-editor.py", builder)
        build = self.root / "build-regeneration"
        self.configure(build)
        initial = (build / "tracked.html").read_bytes()
        stylesheet = source / "track-editor.css"
        marker = "/* editor installation regeneration fixture */"
        with stylesheet.open("a", encoding="utf-8") as stream:
            stream.write("\n" + marker + "\n")
        # Give the modified prerequisite a timestamp newer than the generated
        # build files even on filesystems with coarse modification resolution.
        timestamp = max(time.time(), (build / "tracked.html").stat().st_mtime) + 1
        os.utime(stylesheet, (timestamp, timestamp))
        self.run_cmake("--build", build)
        regenerated = (build / "tracked.html").read_bytes()
        self.assertNotEqual(initial, regenerated)
        self.assertIn(marker.encode("ascii"), regenerated)
        destination = self.root / "regenerated-package"
        self.install(build, destination)
        self.assertEqual((destination / EDITOR_DIRECTORY / "tracked.html").read_bytes(), regenerated)


if __name__ == "__main__":
    unittest.main()
