"""Exercise target SDK isolation and relocated-runtime checks without BSD VMs."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
TOOLCHAIN = ROOT / "cmake/toolchains/bsd-cross.cmake"
RELOCATED_RUNTIME = ROOT / "tools/scripts/test-relocated-runtime.sh"
TARGETS = (
    ("freebsd-arm64", "FreeBSD", "aarch64", "14.4"),
    ("freebsd-riscv64", "FreeBSD", "riscv64", "15.0"),
    ("openbsd-arm64", "OpenBSD", "aarch64", "7.9"),
    ("openbsd-riscv64", "OpenBSD", "riscv64", "7.9"),
    ("netbsd-arm64", "NetBSD", "aarch64", "10.2"),
    ("netbsd-riscv64", "NetBSD", "riscv64", "11.0"),
)
PROBE = '''include("${TOOLCHAIN}")
execute_process(COMMAND "${PKG_CONFIG}" --exists restunts-host-only RESULT_VARIABLE host_found)
if(host_found EQUAL 0)
    message(FATAL_ERROR "Target imported a host pkg-config dependency")
endif()
execute_process(COMMAND "${PKG_CONFIG}" --cflags --libs restunts-target-only
    RESULT_VARIABLE target_missing OUTPUT_VARIABLE target_flags OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT target_missing EQUAL 0)
    message(FATAL_ERROR "Target pkg-config dependency was not found")
endif()
find_library(target_library NAMES restunts-target-only REQUIRED)
find_library(host_library NAMES restunts-host-only)
find_path(target_include restunts-target-only.h REQUIRED)
find_path(host_include restunts-host-only.h)
if(host_library OR host_include)
    message(FATAL_ERROR "Target imported host headers or libraries")
endif()
# FindPkgConfig adds CMake prefixes separately from pkg-config's environment.
# A host prefix must remain excluded through that path as well.
set(CMAKE_PREFIX_PATH "${HOST_PREFIX}")
include(FindPkgConfig)
pkg_check_modules(host_module QUIET restunts-host-only)
if(host_module_FOUND)
    message(FATAL_ERROR "FindPkgConfig imported a host dependency from a CMake prefix")
endif()
pkg_check_modules(target_module REQUIRED restunts-target-only)
file(WRITE "${OUTPUT}"
    "${CMAKE_SYSTEM_NAME}\\n${CMAKE_SYSTEM_PROCESSOR}\\n${CMAKE_SYSTEM_VERSION}\\n"
    "${CMAKE_C_COMPILER_TARGET}\\n${CMAKE_CXX_COMPILER_TARGET}\\n"
    "${target_flags}\\n${target_library}\\n${target_include}\\n${CMAKE_CXX_FLAGS_INIT}\\n"
    "${CMAKE_EXE_LINKER_FLAGS_INIT}\\n${CMAKE_SHARED_LINKER_FLAGS_INIT}\\n"
    "${CMAKE_MODULE_LINKER_FLAGS_INIT}\\n")
'''
SMOKE_PROGRAM = '''import json, os, sys
from pathlib import Path

executable = Path(sys.argv[0]).resolve()
assert sys.argv[1:] == ["--licenses"], sys.argv
assert (executable.parent.parent / "lib/companion.txt").read_text() == "runtime companion"
Path(os.environ["BSD_SMOKE_EVENTS"]).write_text(json.dumps({
    "executable": str(executable),
    "environment": {name: os.environ.get(name) for name in
                    ("LD_LIBRARY_PATH", "LIBRARY_PATH", "ADDON_PATH")},
}))
sys.exit(int(os.environ.get("BSD_SMOKE_EXIT", "0")))
'''
FAILURE_EXIT_CODE = 37


class BsdCrossToolchainTests(unittest.TestCase):
    def setUp(self):
        self.cmake = shutil.which("cmake")
        self.pkg_config = shutil.which("pkg-config")
        if not self.cmake or not self.pkg_config:
            self.skipTest("CMake and pkg-config are needed for SDK isolation probes")
        temporary = tempfile.TemporaryDirectory(prefix="BSD SDK with spaces ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.sdk = self.root / "target SDK"
        self.sdk.mkdir()
        self.host = self.root / "host dependencies"
        for directory in (self.sdk / "usr/local", self.host):
            (directory / "lib/pkgconfig").mkdir(parents=True)
            (directory / "include").mkdir()
        for prefix, name in ((self.sdk / "usr/local", "restunts-target-only"),
                             (self.host, "restunts-host-only")):
            (prefix / "lib" / ("lib" + name + ".so")).touch()
            (prefix / "include" / (name + ".h")).touch()
            (prefix / "lib/pkgconfig" / (name + ".pc")).write_text(
                "prefix=/usr/local\nName: " + name + "\nDescription: SDK probe\nVersion: 1.0\n"
                "Libs: -L${prefix}/lib -l" + name + "\nCflags: -I${prefix}/include\n",
                encoding="utf-8")
        self.probe = self.root / "probe.cmake"
        self.probe.write_text(PROBE, encoding="utf-8")
        self.output = self.root / "probe.txt"

    def probe_target(self, target):
        (self.sdk / "restunts-target").write_text(target + "\n", encoding="utf-8")
        environment = dict(os.environ, PKG_CONFIG_PATH=str(self.host / "lib/pkgconfig"),
                           PKG_CONFIG_LIBDIR=str(self.host / "lib/pkgconfig"),
                           PKG_CONFIG_SYSROOT_DIR="/unrelated/SDK")
        return subprocess.run([
            self.cmake, "-DTOOLCHAIN=" + str(TOOLCHAIN),
            "-DRESTUNTS_BSD_TARGET=" + target, "-DRESTUNTS_BSD_SYSROOT=" + str(self.sdk),
            # This script-mode probe checks configuration, never invokes a compiler.
            "-DCMAKE_C_COMPILER=" + sys.executable, "-DCMAKE_CXX_COMPILER=" + sys.executable,
            "-DCMAKE_LINKER=" + sys.executable, "-DCMAKE_AR=" + sys.executable,
            "-DCMAKE_RANLIB=" + sys.executable,
            "-DCMAKE_LIBRARY_PATH=" + str(self.host / "lib"),
            "-DCMAKE_INCLUDE_PATH=" + str(self.host / "include"),
            "-DHOST_PREFIX=" + str(self.host), "-DPKG_CONFIG_EXECUTABLE=" + self.pkg_config,
            "-DPKG_CONFIG=" + self.pkg_config, "-DOUTPUT=" + str(self.output),
            "-P", str(self.probe),
        ], env=environment, text=True, capture_output=True, timeout=15, check=False)

    def test_all_bsd_sdks_select_target_abi_and_exclude_host_dependencies(self):
        for target, system, processor, version in TARGETS:
            with self.subTest(target=target):
                result = self.probe_target(target)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                lines = self.output.read_text().splitlines()
                self.assertEqual(lines[:3], [system, processor, version])
                triple = processor + "-unknown-" + system.lower() + version
                self.assertEqual(lines[3:5], [triple, triple])
                self.assertIn(str(self.sdk / "usr/local/include"), lines[5])
                self.assertIn(str(self.sdk / "usr/local/lib"), lines[5])
                self.assertEqual(lines[6], str(self.sdk / "usr/local/lib/librestunts-target-only.so"))
                self.assertEqual(lines[7], str(self.sdk / "usr/local/include"))
                if system == "NetBSD":
                    self.assertIn("-stdlib=libstdc++", lines[8])
                self.assertEqual(lines[9:12], ["-fuse-ld=" + sys.executable] * 3)

    def test_freebsd_ports_metadata_is_found_in_libdata_pkgconfig(self):
        # FreeBSD ports put GTK and its dependency metadata in libdata, while
        # libraries remain in lib. Exercise pkg-config and FindPkgConfig with
        # that actual layout instead of assuming the Linux lib/pkgconfig path.
        prefix = self.sdk / "usr/local"
        (prefix / "libdata").mkdir()
        (prefix / "lib/pkgconfig").rename(prefix / "libdata/pkgconfig")
        for target in ("freebsd-arm64", "freebsd-riscv64"):
            with self.subTest(target=target):
                result = self.probe_target(target)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                flags = self.output.read_text().splitlines()[5]
                self.assertIn(str(prefix / "include"), flags)
                self.assertIn(str(prefix / "lib"), flags)


class RelocatedRuntimeTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="relocated BSD runtime ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.runtime = self.root / "original package"
        (self.runtime / "bin").mkdir(parents=True)
        (self.runtime / "lib").mkdir()
        executable = self.runtime / "bin/restunts"
        executable.write_text("#!" + sys.executable + "\n" + SMOKE_PROGRAM, encoding="utf-8")
        executable.chmod(0o755)
        (self.runtime / "lib/companion.txt").write_text("runtime companion", encoding="utf-8")
        self.events = self.root / "events.json"
        self.environment = dict(os.environ, TMPDIR=str(self.root), BSD_SMOKE_EVENTS=str(self.events),
                                LD_LIBRARY_PATH="/incorrect/loader", LIBRARY_PATH="/incorrect/library",
                                ADDON_PATH="/incorrect/addon")

    def run_smoke(self, exit_code=0):
        environment = dict(self.environment, BSD_SMOKE_EXIT=str(exit_code))
        result = subprocess.run(["sh", str(RELOCATED_RUNTIME), str(self.runtime)], env=environment,
                                text=True, capture_output=True, timeout=15, check=False)
        event = json.loads(self.events.read_text())
        relocated = Path(event["executable"])
        self.assertNotEqual(relocated, self.runtime / "bin/restunts")
        self.assertFalse(relocated.parent.parent.exists(), "Temporary relocated runtime was not removed")
        self.assertTrue((self.runtime / "bin/restunts").exists())
        self.assertEqual(event["environment"],
                         {name: None for name in ("LD_LIBRARY_PATH", "LIBRARY_PATH", "ADDON_PATH")})
        return result

    def test_runtime_starts_after_relocation_without_loader_environment_overrides(self):
        result = self.run_smoke()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_runtime_failure_is_propagated_and_temporary_tree_is_removed(self):
        result = self.run_smoke(FAILURE_EXIT_CODE)
        self.assertEqual(result.returncode, FAILURE_EXIT_CODE, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
