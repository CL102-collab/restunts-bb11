"""Check Munt's embedded exports and curated native/browser install components."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
COMMAND_TIMEOUT = 60
UPSTREAM_INSTALL_RULES = """install(TARGETS mt32emu EXPORT MT32EmuTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} ${libmt32emu_COMPONENT_DEVEL}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} NAMELINK_SKIP
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
# TODO: Use NAMELINK_COMPONENT with CMake 3.12 instead.
if(libmt32emu_SHARED)
  install(TARGETS mt32emu EXPORT MT32EmuTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} ${libmt32emu_COMPONENT_DEVEL} NAMELINK_ONLY
  )
endif()
"""


class MuntInstallTests(unittest.TestCase):
    def setUp(self):
        if os.name != "posix":
            self.skipTest("The isolated install probe uses POSIX library paths")
        self.commands = {name: shutil.which(name) for name in ("cmake", "ninja", "cc", "c++")}
        if not all(self.commands.values()):
            self.skipTest("CMake, Ninja and host C/C++ drivers are needed")
        temporary = tempfile.TemporaryDirectory(prefix="restunts Munt install ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.project = self.root / "project"
        self.project.mkdir()
        for name in ("cmake", "docs"):
            (self.project / name).symlink_to(ROOT / name, target_is_directory=True)
        self.munt = self.root / "munt"
        self.core = self.munt / "mt32emu"
        manifest = json.loads((ROOT / "cmake/mt32emu-source-manifest.json").read_text())
        for name in manifest["files"]:
            path = self.core / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("Fixture source for " + name + "\n")
        (self.core / "src/Synth.cpp").write_text("extern \"C\" void fixture_synth() {}\n")
        (self.core / "CMakeLists.txt").write_text("""cmake_minimum_required(VERSION 3.25)
project(libmt32emu LANGUAGES C CXX)
include(GNUInstallDirs)
if(libmt32emu_SHARED)
  add_library(mt32emu SHARED src/Synth.cpp)
else()
  add_library(mt32emu STATIC src/Synth.cpp)
endif()
set_target_properties(mt32emu PROPERTIES VERSION 2.8.6 SOVERSION 2)
set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME Runtime)
set(libmt32emu_COMPONENT_DEVEL COMPONENT Devel)
""" + UPSTREAM_INSTALL_RULES + """install(EXPORT MT32EmuTargets DESTINATION lib/cmake/MT32Emu
  NAMESPACE MT32Emu:: COMPONENT Devel)
""")
        self.openbsd_install = self.root / "openbsd-install.cmake"
        self.openbsd_install.write_text("""# OpenBSD's cmInstallCommand patch treats NAMELINK_ONLY as NAMELINK_SKIP.
# Emulate that relevant host behavior independently of CMAKE_SYSTEM_NAME.
function(install)
  set(arguments ${ARGV})
  list(TRANSFORM arguments REPLACE "^NAMELINK_ONLY$" "NAMELINK_SKIP")
  _install(${arguments})
endfunction()
""")
        (self.project / "CMakeLists.txt").write_text("""cmake_minimum_required(VERSION 3.25)
project(MuntInstallProbe LANGUAGES C CXX)
include(cmake/mt32emu.cmake)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/target-file.txt"
  CONTENT "$<TARGET_FILE:mt32emu>\\n$<TARGET_PROPERTY:mt32emu,TYPE>\\n")
""")

    def run_command(self, arguments):
        result = subprocess.run(arguments, capture_output=True, text=True,
                                timeout=COMMAND_TIMEOUT, check=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def configure(self, build, *options):
        command = [self.commands["cmake"], "-S", str(self.project), "-B", str(build),
                   "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                   "-DFETCHCONTENT_SOURCE_DIR_RESTUNTS_MUNT=" + str(self.munt),
                   "-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=" + str(self.openbsd_install), *options]
        for attempt in ("clean", "reconfigure"):
            with self.subTest(attempt=attempt):
                self.run_command(command)
                configured = (self.core / "CMakeLists.txt").read_bytes()
                self.assertEqual(configured.count(b"EXPORT MT32EmuTargets"), 2)
                self.assertIn(b"NAMELINK_COMPONENT Devel", configured)
                self.assertNotIn(b"NAMELINK_ONLY", configured)
                if attempt == "clean":
                    first = configured
                else:
                    self.assertEqual(configured, first)
        return (build / "target-file.txt").read_text().splitlines()

    def check_component(self, build, component, *, shared_library=None):
        prefix = self.root / (build.name + "-" + component)
        self.run_command([self.commands["cmake"], "--install", str(build),
                          "--prefix", str(prefix), "--component", component])
        source = prefix / "share/restunts/mt32emu"
        self.assertTrue((source / "src/Synth.cpp").is_file())
        self.assertIn("NAMELINK_COMPONENT Devel", (source / "CMakeLists.txt").read_text())
        self.assertTrue((source / "rebuild/CMakeLists.txt").is_file())
        self.assertTrue((prefix / "share/licenses/restunts/MT32EMU-COPYING.LESSER.txt").is_file())
        self.assertTrue((prefix / "share/licenses/restunts/MT32EMU-COPYING.txt").is_file())
        libraries = sorted(path.name for path in (prefix / "lib").glob("*mt32emu*"))
        self.assertEqual(libraries, [shared_library] if shared_library else [])
        self.assertFalse((prefix / "include").exists())
        self.assertFalse((prefix / "lib/cmake/MT32Emu").exists())
        return source

    def test_openbsd_exports_and_native_components_survive_reconfiguration(self):
        build = self.root / "native"
        library, kind = self.configure(build)
        self.assertEqual(kind, "SHARED_LIBRARY")
        self.assertIn(Path(library).name, ("libmt32emu.so", "libmt32emu.dylib"))
        self.run_command([self.commands["cmake"], "--build", str(build), "--target", "mt32emu"])
        for component in ("Runtime", "Tests"):
            source = self.check_component(build, component, shared_library=Path(library).name)
        # The shipped normalized source must configure independently of our patcher.
        rebuilt = self.root / "rebuilt"
        command = [self.commands["cmake"], "-S", str(source / "rebuild"),
                   "-B", str(rebuilt), "-G", "Ninja",
                   "-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=" + str(self.openbsd_install)]
        self.run_command(command)
        self.run_command(command)

    def test_browser_and_android_components_remain_source_only(self):
        for platform in ("EMSCRIPTEN", "ANDROID"):
            with self.subTest(platform=platform):
                build = self.root / platform.lower()
                library, kind = self.configure(build, "-D" + platform + "=ON")
                self.assertEqual(kind, "STATIC_LIBRARY" if platform == "EMSCRIPTEN" else "SHARED_LIBRARY")
                self.run_command([self.commands["cmake"], "--build", str(build), "--target", "mt32emu"])
                self.assertTrue(Path(library).is_file())
                for component in ("Runtime", "Tests"):
                    self.check_component(build, component)


if __name__ == "__main__":
    unittest.main()
