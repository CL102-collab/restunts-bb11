"""Verify Haiku SDK isolation and launcher ABI selection without running Haiku."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
TOOLCHAIN = ROOT / "cmake/toolchains/haiku-cross.cmake"
TOOLS = ("cc", "c++", "ar", "ranlib", "ld", "readelf", "strip", "objdump", "wx-config")


class HaikuCrossToolchainTests(unittest.TestCase):
    def setUp(self):
        self.cmake = shutil.which("cmake")
        self.pkg_config = shutil.which("pkg-config")
        if not self.cmake:
            self.skipTest("CMake is needed for Haiku cross configuration probes")
        temporary = tempfile.TemporaryDirectory(prefix="Haiku SDK with spaces ")
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        self.host = self.directory / "host"
        (self.host / "lib/pkgconfig").mkdir(parents=True)
        (self.host / "include").mkdir()
        (self.host / "lib/librestunts-host-only.so").touch()
        (self.host / "include/restunts-host-only.h").touch()
        (self.host / "lib/pkgconfig/restunts-host-only.pc").write_text(
            "prefix=" + str(self.host) + "\nName: host-only\nDescription: Host contamination probe\n"
            "Version: 1.0\nLibs: -L${prefix}/lib -lrestunts-host-only\n"
            "Cflags: -I${prefix}/include\n", encoding="utf-8")
        self.sdk = self.directory / "sdk"
        (self.sdk / "bin").mkdir(parents=True)
        (self.sdk / "sysroot/boot/system/develop").mkdir(parents=True)
        for tool in TOOLS:
            script = self.sdk / "bin" / tool
            script.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            script.chmod(0o755)
        self.marker = self.sdk / "restunts-target"
        self.marker.write_text("haiku-x64\n", encoding="utf-8")

    def probe(self, target="haiku-x64", source="", sdk=None):
        script = self.directory / "probe.cmake"
        script.write_text(
            f'set(CMAKE_PREFIX_PATH "{self.host.as_posix()}")\n'
            f'set(ENV{{PKG_CONFIG_PATH}} "{(self.host / "lib/pkgconfig").as_posix()}")\n'
            f'include("{TOOLCHAIN.as_posix()}")\n' + source, encoding="utf-8")
        return subprocess.run([
            self.cmake, f"-DRESTUNTS_HAIKU_SDK_ROOT={sdk or self.sdk}",
            f"-DRESTUNTS_HAIKU_TARGET={target}", "-P", str(script),
        ], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)

    def test_both_sdk_abis_isolate_headers_libraries_and_pkg_config(self):
        if not self.pkg_config:
            self.skipTest("pkg-config is needed for target dependency isolation")
        for target, processor, suffix in (("haiku-x64", "x86_64", ""),
                                           ("haiku-x86", "i586", "/x86")):
            with self.subTest(target=target):
                self.marker.write_text(target + "\n", encoding="utf-8")
                prefix = self.sdk / "sysroot/boot/system/develop"
                library = prefix / ("lib" + suffix)
                headers = prefix / "headers"
                packages = library / "pkgconfig"
                packages.mkdir(parents=True, exist_ok=True)
                headers.mkdir(exist_ok=True)
                (library / "librestunts-target-only.so").touch()
                (headers / "restunts-target-only.h").touch()
                (packages / "restunts-target-only.pc").write_text(
                    "prefix=/boot/system/develop\nName: target-only\nDescription: Haiku SDK probe\n"
                    "Version: 1.0\nLibs: -L${prefix}/lib" + suffix + " -lrestunts-target-only\n"
                    "Cflags: -I${prefix}/headers\n", encoding="utf-8")
                result = self.probe(target, f'''
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Haiku" OR NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "{processor}")
    message(FATAL_ERROR "Wrong target system/processor")
endif()
if(NOT CMAKE_C_COMPILER STREQUAL "${{RESTUNTS_HAIKU_SDK_ROOT}}/bin/cc" OR
        NOT CMAKE_CXX_COMPILER STREQUAL "${{RESTUNTS_HAIKU_SDK_ROOT}}/bin/c++")
    message(FATAL_ERROR "Target compiler wrapper was not selected")
endif()
foreach(mode LIBRARY INCLUDE PACKAGE)
    if(NOT CMAKE_FIND_ROOT_PATH_MODE_${{mode}} STREQUAL "ONLY")
        message(FATAL_ERROR "Target search escaped its SDK")
    endif()
endforeach()
if(NOT CMAKE_FIND_ROOT_PATH_MODE_PROGRAM STREQUAL "NEVER")
    message(FATAL_ERROR "Build tools must run on the Linux host")
endif()
find_library(target_library NAMES restunts-target-only REQUIRED)
find_path(target_header restunts-target-only.h PATHS /boot/system/develop/headers REQUIRED)
find_library(host_library NAMES restunts-host-only)
find_path(host_header restunts-host-only.h)
if(host_library OR host_header)
    message(FATAL_ERROR "Target imported host headers or libraries")
endif()
execute_process(COMMAND "{self.pkg_config}" --exists restunts-host-only
    RESULT_VARIABLE host_missing)
if(host_missing EQUAL 0)
    message(FATAL_ERROR "Target imported a host pkg-config dependency")
endif()
if(NOT target_library STREQUAL "{library.as_posix()}/librestunts-target-only.so" OR
        NOT target_header STREQUAL "{headers.as_posix()}")
    message(FATAL_ERROR "Incorrect target dependency paths: ${{target_library}};${{target_header}}")
endif()
execute_process(COMMAND "{self.pkg_config}" --cflags --libs restunts-target-only
    RESULT_VARIABLE failed OUTPUT_VARIABLE flags)
if(failed OR NOT flags MATCHES "restunts-target-only")
    message(FATAL_ERROR "Target pkg-config dependency was not found")
endif()
list(FIND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES RESTUNTS_HAIKU_SDK_ROOT root_index)
list(FIND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES RESTUNTS_HAIKU_TARGET target_index)
if(root_index LESS 0 OR target_index LESS 0)
    message(FATAL_ERROR "Compiler probes lost the target SDK selection")
endif()
''')
                self.assertEqual(result.returncode, 0, result.stdout)

    def test_rejects_mismatched_missing_or_incomplete_sdk(self):
        result = self.probe("haiku-x86")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("SDK does not match", result.stdout)
        result = self.probe(sdk="relative-sdk")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("absolute prepared SDK", result.stdout)
        self.marker.unlink()
        result = self.probe()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("restunts-target marker", result.stdout)
        self.marker.write_text("haiku-x64\n", encoding="utf-8")
        (self.sdk / "bin/c++").unlink()
        result = self.probe()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing its host tool", result.stdout)

    def test_launcher_uses_sdk_wx_config_and_matching_qt_abi(self):
        if not shutil.which("c++"):
            self.skipTest("A native C++ compiler is needed for launcher configuration")
        source = self.directory / "launcher-probe"
        source.mkdir()
        modules = source / "modules"
        modules.mkdir()
        (modules / "FindwxWidgets.cmake").write_text('''
if(NOT wxWidgets_CONFIG_EXECUTABLE STREQUAL "${RESTUNTS_HAIKU_SDK_ROOT}/bin/wx-config")
    message(FATAL_ERROR "Launcher discovered a host or target-native wx-config")
endif()
set(wxWidgets_FOUND TRUE)
set(wxWidgets_VERSION_STRING 3.2.4)
set(wxWidgets_LIBRARIES fixture-wx)
set(wxWidgets_CXX_FLAGS "-pthread")
set(wxWidgets_DEFINITIONS __WXQT__)
''', encoding="utf-8")
        (modules / "FetchContent.cmake").write_text('''
macro(FetchContent_Declare)
endmacro()
macro(FetchContent_MakeAvailable name)
    set(${name}_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
endmacro()
''', encoding="utf-8")
        for target, suffix in (("haiku-x64", ""), ("haiku-x86", "/x86")):
            with self.subTest(target=target):
                qt = self.sdk / ("sysroot/boot/system/lib" + suffix + "/cmake/Qt5")
                qt.mkdir(parents=True, exist_ok=True)
                (qt / "Qt5Config.cmake").write_text('''
set(Qt5_FOUND TRUE)
add_library(Qt5::Widgets INTERFACE IMPORTED)
''', encoding="utf-8")
                (source / "CMakeLists.txt").write_text(f'''
cmake_minimum_required(VERSION 3.25)
project(launcher-probe CXX)
set(CMAKE_SYSTEM_NAME Haiku)
set(CMAKE_CROSSCOMPILING TRUE)
set(CMAKE_SYSROOT "{(self.sdk / 'sysroot').as_posix()}")
set(CMAKE_FIND_ROOT_PATH "${{CMAKE_SYSROOT}}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(RESTUNTS_HAIKU_SDK_ROOT "{self.sdk.as_posix()}")
set(RESTUNTS_HAIKU_TARGET "{target}")
set(RESTUNTS_SYSTEM_WX ON)
set(RESTUNTS_BUILD_TESTS OFF)
set(CMAKE_SOURCE_DIR "{ROOT.as_posix()}")
list(PREPEND CMAKE_MODULE_PATH "{modules.as_posix()}")
include("{(ROOT / 'cmake/launcher.cmake').as_posix()}")
if(NOT Qt5_DIR STREQUAL "{qt.as_posix()}")
    message(FATAL_ERROR "Wrong Qt ABI: ${{Qt5_DIR}}")
endif()
get_target_property(links restunts-launcher LINK_LIBRARIES)
if(NOT "Qt5::Widgets" IN_LIST links)
    message(FATAL_ERROR "Launcher lost Qt forwarding")
endif()
''', encoding="utf-8")
                result = subprocess.run([
                    self.cmake, "-S", str(source), "-B", str(source / target),
                ], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
                self.assertEqual(result.returncode, 0, result.stdout)


if __name__ == "__main__":
    unittest.main()
