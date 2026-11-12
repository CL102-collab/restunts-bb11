"""Exercise native BSD target checks and cross-build configuration without compiling."""

import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
CONFIGURE_SENTINEL = 74
MOCK_COMMAND = """#!/usr/bin/env python3
import json, os, sys
from pathlib import Path

command = Path(sys.argv[0]).name
if command == "uname":
    print({"-s": os.environ["BUILD_TARGET_TEST_SYSTEM"],
           "-m": os.environ["BUILD_TARGET_TEST_MACHINE"],
           "-p": os.environ["BUILD_TARGET_TEST_PROCESSOR"]}[sys.argv[1]])
elif command == "setarch":
    print(os.environ["PATH"])
else:
    with open(os.environ["BUILD_TARGET_TEST_EVENTS"], "a") as output:
        output.write(json.dumps({"command": command, "arguments": sys.argv[1:],
                                 "pkg_config_path": os.environ.get("PKG_CONFIG_PATH"),
                                 "lang": os.environ.get("LANG"),
                                 "lc_all": os.environ.get("LC_ALL")}) + "\\n")
    if command == "cmake":
        sys.exit(int(os.environ["BUILD_TARGET_TEST_SENTINEL"]))
"""


class BuildPackageTargetTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="build package targets ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        scripts = self.root / "tools/scripts"
        scripts.mkdir(parents=True)
        self.helper = scripts / "build-package.sh"
        shutil.copy2(ROOT / "tools/scripts/build-package.sh", self.helper)
        self.commands = self.root / "commands"
        self.commands.mkdir()
        for name in ("uname", "git", "cmake", "setarch"):
            command = self.commands / name
            command.write_text(MOCK_COMMAND, encoding="utf-8")
            command.chmod(0o755)
        self.events = self.root / "events.jsonl"
        self.environment = dict(os.environ)
        self.environment.update({
            "PATH": str(self.commands) + os.pathsep + os.environ["PATH"],
            "RESTUNTS_TOOLCHAIN_ROOT": str(self.root / "toolchains"),
            "PKG_CONFIG_PATH": "/existing/native/pkgconfig",
            "BUILD_TARGET_TEST_EVENTS": str(self.events),
            "BUILD_TARGET_TEST_SENTINEL": str(CONFIGURE_SENTINEL),
        })

    def configure_target(self, target, system="Linux", machine="x86_64", processor=None, extra=None):
        self.events.unlink(missing_ok=True)
        environment = dict(self.environment, BUILD_TARGET_TEST_SYSTEM=system,
                           BUILD_TARGET_TEST_MACHINE=machine,
                           BUILD_TARGET_TEST_PROCESSOR=processor or machine)
        environment.update(extra or {})
        result = subprocess.run(["bash", str(self.helper), target], cwd=self.root,
                                env=environment, text=True, capture_output=True,
                                timeout=15, check=False)
        events = ([json.loads(line) for line in self.events.read_text().splitlines()]
                  if self.events.exists() else [])
        return result, events

    def require_configure(self, result, events):
        # The fake compiler driver exits at configuration, before any build,
        # installation, runtime smoke test, or archive creation can run.
        self.assertEqual(result.returncode, CONFIGURE_SENTINEL, result.stdout + result.stderr)
        configurations = [event for event in events if event["command"] == "cmake"]
        self.assertEqual(len(configurations), 1, events)
        self.assertIn("-S", configurations[0]["arguments"])
        return configurations[0]

    def test_bsd_machine_and_processor_names_accept_matching_64_bit_userspaces(self):
        cases = (
            ("freebsd-arm64", "FreeBSD", "arm64", "aarch64"),
            ("freebsd-riscv64", "FreeBSD", "riscv", "riscv64"),
            ("freebsd-riscv64", "FreeBSD", "riscv64", "riscv64"),
            ("openbsd-arm64", "OpenBSD", "arm64", "arm64"),
            ("openbsd-riscv64", "OpenBSD", "riscv64", "riscv64"),
            ("netbsd-arm64", "NetBSD", "evbarm", "aarch64"),
            ("netbsd-arm64", "NetBSD", "aarch64", "aarch64"),
            ("netbsd-riscv64", "NetBSD", "riscv", "riscv64"),
        )
        for target, system, machine, processor in cases:
            with self.subTest(target=target, machine=machine, processor=processor):
                result, events = self.configure_target(target, system, machine, processor)
                self.require_configure(result, events)

    def test_wrong_bsd_userspace_fails_before_source_checks_or_building(self):
        cases = (
            ("freebsd-arm64", "FreeBSD", "arm", "armv7"),
            ("freebsd-arm64", "OpenBSD", "arm64", "arm64"),
            ("freebsd-riscv64", "FreeBSD", "riscv", "riscv32"),
            ("openbsd-riscv64", "OpenBSD", "amd64", "amd64"),
            ("netbsd-arm64", "NetBSD", "evbarm", "earmv7hf"),
            ("netbsd-riscv64", "NetBSD", "evbarm", "aarch64"),
        )
        for target, system, machine, processor in cases:
            with self.subTest(target=target, system=system, machine=machine, processor=processor):
                result, events = self.configure_target(target, system, machine, processor)
                self.assertNotEqual(result.returncode, 0)
                self.assertNotEqual(result.returncode, CONFIGURE_SENTINEL)
                self.assertIn("requires its matching native BSD userspace", result.stderr)
                self.assertEqual(events, [])

    def test_netbsd_new_targets_keep_native_paths_and_precompiled_header_workaround(self):
        for target, machine, processor in (("netbsd-arm64", "evbarm", "aarch64"),
                                            ("netbsd-riscv64", "riscv", "riscv64")):
            with self.subTest(target=target):
                result, events = self.configure_target(target, "NetBSD", machine, processor)
                configuration = self.require_configure(result, events)
                self.assertIn("-DCMAKE_PREFIX_PATH=/usr/pkg;/usr/X11R7", configuration["arguments"])
                self.assertIn("-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON", configuration["arguments"])
                self.assertEqual(configuration["pkg_config_path"],
                                 "/usr/pkg/lib/pkgconfig:/usr/X11R7/lib/pkgconfig:/existing/native/pkgconfig")

    def test_freebsd_riscv64_selects_available_gtk2_repository_toolkit(self):
        result, events = self.configure_target("freebsd-riscv64", "FreeBSD", "riscv", "riscv64")
        configuration = self.require_configure(result, events)
        self.assertIn("-DRESTUNTS_WX_TOOLKIT=gtk2", configuration["arguments"])

    def test_haiku_builds_use_a_supported_utf8_locale_for_unicode_paths(self):
        for target, machine in (("haiku-x86", "BePC"), ("haiku-x64", "x86_64")):
            for inherited_locale in ("C", "C.UTF-8"):
                with self.subTest(target=target, inherited_locale=inherited_locale):
                    result, events = self.configure_target(
                        target, "Haiku", machine,
                        extra={"LANG": inherited_locale, "LC_ALL": inherited_locale})
                    configuration = self.require_configure(result, events)
                    self.assertEqual(configuration["lang"], "en_US.UTF-8")
                    self.assertEqual(configuration["lc_all"], "en_US.UTF-8")

    def prepare_bsd_sdk(self, target):
        sdk = self.root / "cross SDK with spaces"
        sdk.mkdir(exist_ok=True)
        (sdk / "restunts-target").write_text(target + "\n", encoding="utf-8")
        return sdk

    def test_bsd_cross_targets_use_prepared_sdk_with_native_x64_tools(self):
        for system in ("FreeBSD", "OpenBSD", "NetBSD"):
            architectures = ["arm64", "riscv64"]
            if system in ("FreeBSD", "OpenBSD"):
                architectures.append("x86")
            if system in ("FreeBSD", "NetBSD"):
                architectures.append("armv7")
            for architecture in architectures:
                target = system.lower() + "-" + architecture
                sdk = self.prepare_bsd_sdk(target)
                with self.subTest(target=target):
                    result, events = self.configure_target(
                        target, system, "amd64", extra={"RESTUNTS_BSD_SYSROOT": str(sdk)})
                    configuration = self.require_configure(result, events)
                    self.assertIn("-DRESTUNTS_BSD_SYSROOT=" + str(sdk), configuration["arguments"])
                    self.assertIn("-DRESTUNTS_BSD_TARGET=" + target, configuration["arguments"])
                    self.assertIn("-DCMAKE_TOOLCHAIN_FILE=" + str(self.root / "cmake/toolchains/bsd-cross.cmake"),
                                  configuration["arguments"])
                    if target == "freebsd-riscv64":
                        self.assertIn("-DRESTUNTS_WX_TOOLKIT=gtk2", configuration["arguments"])
                    if architecture == "x86":
                        self.assertIn("-DRESTUNTS_SSE2=ON", configuration["arguments"])

    def test_bsd_cross_sdk_rejects_wrong_host_cpu_or_system_before_building(self):
        sdk = self.prepare_bsd_sdk("openbsd-arm64")
        for system, machine in (("Linux", "x86_64"), ("FreeBSD", "amd64"),
                                ("OpenBSD", "arm64")):
            with self.subTest(system=system, machine=machine):
                result, events = self.configure_target(
                    "openbsd-arm64", system, machine, extra={"RESTUNTS_BSD_SYSROOT": str(sdk)})
                self.assertNotEqual(result.returncode, 0)
                self.assertNotEqual(result.returncode, CONFIGURE_SENTINEL)
                self.assertIn("requires a matching x64 BSD host", result.stderr)
                self.assertEqual(events, [])

    def test_bsd_cross_sdk_rejects_missing_or_mismatched_marker_before_building(self):
        sdk = self.prepare_bsd_sdk("netbsd-riscv64")
        for marker in (None, "freebsd-riscv64\n"):
            with self.subTest(marker=marker):
                path = sdk / "restunts-target"
                if marker is None:
                    path.unlink(missing_ok=True)
                else:
                    path.write_text(marker, encoding="utf-8")
                result, events = self.configure_target(
                    "netbsd-riscv64", "NetBSD", "amd64", extra={"RESTUNTS_BSD_SYSROOT": str(sdk)})
                self.assertNotEqual(result.returncode, 0)
                self.assertNotEqual(result.returncode, CONFIGURE_SENTINEL)
                self.assertIn("Missing or mismatched BSD cross SDK", result.stderr)
                self.assertEqual(events, [])

    def test_linux_riscv64_selects_the_riscv_cross_toolchain(self):
        result, events = self.configure_target("linux-riscv64")
        configuration = self.require_configure(result, events)
        self.assertIn("-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/linux-riscv64.cmake",
                      configuration["arguments"])

    def test_linux_riscv64_appimage_uses_prepared_sdk_and_compatible_sdl_options(self):
        environments = self.root / "toolchains/appimage-sysroots"
        environments.mkdir(parents=True)
        toolchain = str(self.root / "SDK with spaces/riscv64.cmake")
        (environments / "linux-appimage-riscv64.env").write_text(
            "export RESTUNTS_APPIMAGE_CMAKE_TOOLCHAIN=" + shlex.quote(toolchain) + "\n",
            encoding="utf-8")
        result, events = self.configure_target("linux-appimage-riscv64")
        configuration = self.require_configure(result, events)
        self.assertIn("-DCMAKE_TOOLCHAIN_FILE=" + toolchain, configuration["arguments"])
        for feature in ("WAYLAND", "VULKAN", "GPU_VULKAN"):
            self.assertIn("-DSDL_" + feature + "=OFF", configuration["arguments"])


class RiscvToolchainIsolationTests(unittest.TestCase):
    def test_host_pkgconfig_environment_is_discarded_and_scalar_abi_is_selected(self):
        cmake = shutil.which("cmake")
        pkg_config = shutil.which("pkg-config")
        if not cmake or not pkg_config:
            self.skipTest("CMake and pkg-config are needed for the cross-toolchain probe")
        with tempfile.TemporaryDirectory(prefix="riscv pkgconfig isolation ") as temporary:
            root = Path(temporary)
            host_pc = root / "host pkgconfig"
            host_pc.mkdir()
            (host_pc / "restunts-host-only.pc").write_text(
                "Name: restunts-host-only\nDescription: unwanted host dependency\n"
                "Version: 1.0\nLibs: -L/host/x86_64/lib -lhost\nCflags: -I/host/x86_64/include\n",
                encoding="utf-8")
            script = root / "probe.cmake"
            output = root / "toolchain.txt"
            script.write_text('''include("${TOOLCHAIN}")
execute_process(COMMAND "${PKG_CONFIG}" --exists restunts-host-only RESULT_VARIABLE found)
if(found EQUAL 0)
    message(FATAL_ERROR "Cross toolchain imported the host pkg-config directory")
endif()
if(NOT "$ENV{PKG_CONFIG_SYSROOT_DIR}" STREQUAL "")
    message(FATAL_ERROR "Cross toolchain retained an unrelated pkg-config sysroot")
endif()
file(WRITE "${OUTPUT}" "${CMAKE_SYSTEM_PROCESSOR}\\n${CMAKE_C_FLAGS_INIT}\\n${CMAKE_CXX_FLAGS_INIT}\\n")
''', encoding="utf-8")
            environment = dict(os.environ, PKG_CONFIG_PATH=str(host_pc),
                               PKG_CONFIG_LIBDIR=str(host_pc), PKG_CONFIG_SYSROOT_DIR="/wrong/host-sdk")
            result = subprocess.run([
                cmake, "-DTOOLCHAIN=" + str(ROOT / "cmake/toolchains/linux-riscv64.cmake"),
                "-DPKG_CONFIG=" + pkg_config, "-DOUTPUT=" + str(output), "-P", str(script),
            ], env=environment, text=True, capture_output=True, timeout=15, check=False)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            processor, c_flags, cxx_flags = output.read_text().splitlines()
            self.assertEqual(processor, "riscv64")
            for flags in (c_flags, cxx_flags):
                self.assertEqual(shlex.split(flags), ["-march=rv64gc", "-mabi=lp64d"])


if __name__ == "__main__":
    unittest.main()
