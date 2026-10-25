"""Exercise Apple's source-language rules through the real top-level CMake project."""

import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
CONFIGURE_TIMEOUT = 60
PROBE_COMPILER_VERSION = "13.0"
PROBE_DEFAULT_STANDARD = 17


class AppleLanguageTests(unittest.TestCase):
    def setUp(self):
        if os.name != "posix":
            self.skipTest("The isolated Darwin toolchain probe uses POSIX paths")
        self.commands = {name: shutil.which(name) for name in ("cmake", "ninja", "cc", "c++", "true")}
        if not all(self.commands.values()):
            self.skipTest("CMake, Ninja and host C/C++ drivers are needed for generation")
        temporary = tempfile.TemporaryDirectory(prefix="restunts Apple languages ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.project = self.root / "project"
        self.project.mkdir()
        (self.project / "CMakeLists.txt").write_bytes((ROOT / "CMakeLists.txt").read_bytes())
        for name in ("src", "third_party", "docs", "assets", "tools", "site"):
            (self.project / name).symlink_to(ROOT / name, target_is_directory=True)
        cmake = self.project / "cmake"
        cmake.mkdir()
        for path in (ROOT / "cmake").iterdir():
            if path.name not in ("webm.cmake", "launcher.cmake"):
                (cmake / path.name).symlink_to(path, target_is_directory=path.is_dir())
        (cmake / "launcher.cmake").write_bytes((ROOT / "cmake/launcher.cmake").read_bytes())
        # Codec contents are unrelated to language initialization; avoid downloads.
        (cmake / "webm.cmake").write_text("add_library(restunts_webm INTERFACE)\n")
        self.sdl = self.root / "sdl"
        self.sdl.mkdir()
        (self.sdl / "cocoa.m").write_text("")
        # Match SDL's child-scoped Objective-C enable and its Cocoa .m sources.
        (self.sdl / "CMakeLists.txt").write_text("""cmake_minimum_required(VERSION 3.25)
project(SDL3 LANGUAGES C)
enable_language(OBJC)
add_library(SDL3-static STATIC cocoa.m)
add_library(SDL3::SDL3 ALIAS SDL3-static)
""")
        self.wx = self.root / "wx"
        self.wx.mkdir()
        (self.wx / "cocoa.mm").write_text("")
        (self.wx / "CMakeLists.txt").write_text("""cmake_minimum_required(VERSION 3.25)
project(wxWidgets LANGUAGES C CXX OBJCXX)
add_library(wx_core STATIC cocoa.mm)
add_library(wx::core ALIAS wx_core)
add_library(wx_base INTERFACE)
add_library(wx::base ALIAS wx_base)
""")
        self.toolchain = self.root / "darwin.cmake"
        settings = ["set(CMAKE_SYSTEM_NAME Darwin)",
                    f'set(CMAKE_INSTALL_NAME_TOOL "{self.commands["true"]}")']
        # Only generate build rules: a host without Apple's SDK can still catch
        # directory-scope and .m/.mm classification failures. Never build targets.
        for language, compiler in (("C", "cc"), ("CXX", "c++"),
                                   ("OBJC", "cc"), ("OBJCXX", "c++")):
            settings.extend([
                f'set(CMAKE_{language}_COMPILER "{self.commands[compiler]}")',
                f"set(CMAKE_{language}_COMPILER_FORCED TRUE)",
                f"set(CMAKE_{language}_COMPILER_ID_RUN TRUE)",
                f"set(CMAKE_{language}_COMPILER_ID GNU)",
                f"set(CMAKE_{language}_COMPILER_VERSION {PROBE_COMPILER_VERSION})",
                f"set(CMAKE_{language}_STANDARD_COMPUTED_DEFAULT {PROBE_DEFAULT_STANDARD})",
                f"set(CMAKE_{language}_EXTENSIONS_COMPUTED_DEFAULT ON)",
            ])
        settings.append("set(CMAKE_CXX_COMPILE_FEATURES cxx_std_11)")
        self.toolchain.write_text("\n".join(settings) + "\n")

    def test_cocoa_languages_survive_dependency_scopes_and_reconfiguration(self):
        for launcher in ("ON", "OFF"):
            build = self.root / ("build-" + launcher)
            command = [self.commands["cmake"], "-S", str(self.project), "-B", str(build),
                       "-G", "Ninja", "-DCMAKE_TOOLCHAIN_FILE=" + str(self.toolchain),
                       "-DRESTUNTS_BUILD_TESTS=OFF", "-DRESTUNTS_BUILD_LAUNCHER=" + launcher,
                       "-DFETCHCONTENT_SOURCE_DIR_SDL3=" + str(self.sdl),
                       "-DFETCHCONTENT_SOURCE_DIR_WXWIDGETS=" + str(self.wx)]
            for attempt in ("clean", "reconfigure"):
                with self.subTest(launcher=launcher, attempt=attempt):
                    result = subprocess.run(command, text=True, capture_output=True,
                                            timeout=CONFIGURE_TIMEOUT, check=False)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    commands = {entry["file"]: shlex.split(entry["command"])
                                for entry in json.loads((build / "compile_commands.json").read_text())}
                    sdl = commands[str(self.sdl / "cocoa.m")]
                    self.assertEqual(sdl[sdl.index("-x") + 1], "objective-c")
                    if launcher == "ON":
                        wx = commands[str(self.wx / "cocoa.mm")]
                        self.assertEqual(wx[wx.index("-x") + 1], "objective-c++")
                    else:
                        self.assertNotIn(str(self.wx / "cocoa.mm"), commands)


if __name__ == "__main__":
    unittest.main()
