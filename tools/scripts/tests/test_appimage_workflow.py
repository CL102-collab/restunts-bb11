"""Exercise the separate RISC-V AppImage job without downloads or target binaries."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[3]
WORKFLOW = ROOT / ".github/workflows/build-packages.yml"
TARGET = "linux-appimage-riscv64"
COMMAND_TIMEOUT_SECONDS = 15


def block_after(source, heading):
    """Read one indented workflow block, without a YAML parser dependency."""
    lines = source.splitlines()
    start = lines.index(heading)
    indentation = len(heading) - len(heading.lstrip())
    body = []
    for line in lines[start + 1:]:
        if line.strip() and len(line) - len(line.lstrip()) <= indentation:
            break
        body.append(line)
    return "\n".join(body)


def step_script(job, name):
    step = block_after(job, "      - name: " + name)
    return textwrap.dedent(block_after(step, "        run: |"))


class AppImageWorkflowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workflow = WORKFLOW.read_text(encoding="utf-8")
        cls.job = block_after(cls.workflow, "  appimage-riscv64-compatibility:")

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="appimage workflow ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.scripts = self.root / "tools/scripts"
        self.scripts.mkdir(parents=True)
        self.environment = dict(os.environ, PACKAGE_TARGET=TARGET,
                                RESTUNTS_TOOLCHAIN_ROOT=str(self.root / "test toolchain"),
                                RUNNER_TEMP=str(self.root / "runner temp"))
        Path(self.environment["RUNNER_TEMP"]).mkdir()

    def run_step(self, name):
        return subprocess.run(
            ["bash", "--noprofile", "--norc", "-e", "-o", "pipefail", "-c",
             step_script(self.job, name)], cwd=self.root, env=self.environment,
            text=True, capture_output=True, timeout=COMMAND_TIMEOUT_SECONDS)

    def test_riscv_check_is_separate_and_consumes_the_uploaded_archive(self):
        self.assertIn("    name: Test linux-appimage-riscv64 on debian13", self.job)
        self.assertIn("    needs: appimage", self.job)
        self.assertIn("    container: debian:13@sha256:", self.job)
        self.assertIn("          name: packages-linux-appimage-riscv64", self.job)
        verification = step_script(self.job, "Verify the downloaded archive before testing")
        for argument in ('--target "$PACKAGE_TARGET"', '--directory dist/packages',
                         '--commit "$GITHUB_SHA"', '--run-id "$GITHUB_RUN_ID"',
                         '--run-attempt "$GITHUB_RUN_ATTEMPT"'):
            self.assertIn(argument, verification)
        self.assertLess(self.job.index("Verify the downloaded archive before testing"),
                        self.job.index("Test the exact archived RISC-V AppImage against Debian 13"))
        self.assertNotIn("build-package.sh", self.job)
        upload = block_after(self.job, "      - name: Upload RISC-V compatibility test evidence")
        self.assertIn("        if: always()", upload)
        self.assertIn("          name: appimage-tests-linux-appimage-riscv64", upload)
        producer = block_after(self.workflow, "  appimage:")
        for name in ("Download game data for ARM userspace tests",
                     "Test the exact ARM AppImage against its pinned userspace",
                     "Upload ARM compatibility test evidence"):
            step = block_after(producer, "      - name: " + name)
            self.assertIn("startsWith(matrix.target, 'linux-appimage-arm')", step)
            self.assertNotIn("riscv64", step)

    def test_runtime_setup_reuses_pinned_packages_and_exports_an_isolated_sysroot(self):
        shutil.copyfile(ROOT / "tools/scripts/package-toolchains.conf",
                        self.scripts / "package-toolchains.conf")
        commands = self.root / "commands"
        commands.mkdir()
        command_log = self.root / "sdk-command.json"
        self.environment.update(PATH=str(commands) + os.pathsep + os.environ["PATH"],
                                APPIMAGE_WORKFLOW_COMMAND_LOG=str(command_log))
        python = commands / "python3"
        python.write_text("#!" + sys.executable + "\n" + textwrap.dedent("""
            import json, os, sys
            from pathlib import Path
            arguments = sys.argv[1:]
            Path(os.environ["APPIMAGE_WORKFLOW_COMMAND_LOG"]).write_text(json.dumps(arguments))
            destination = Path(arguments[arguments.index("--destination") + 1])
            (destination / "usr/lib").mkdir(parents=True)
        """))
        python.chmod(0o755)
        qemu = commands / "qemu-riscv64"
        qemu.write_text("#!/bin/sh\nexit 0\n")
        qemu.chmod(0o755)
        result = self.run_step("Prepare the pinned Debian 13 RISC-V userspace")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        arguments = json.loads(command_log.read_text())
        self.assertEqual(arguments[:2], ["tools/scripts/appimage-sysroot.py", "debian-sdk"])
        self.assertEqual(arguments[-4:], ["libc6", "libx11-6", "libx11-xcb1", "libxcb1"])
        pins = dict(line.split("=", 1) for line in (self.scripts / "package-toolchains.conf")
                    .read_text().splitlines() if line.startswith("APPIMAGE_RISCV64_"))
        for option, pin in (("--index-url", "APPIMAGE_RISCV64_INDEX_URL"),
                            ("--index-sha256", "APPIMAGE_RISCV64_INDEX_SHA256"),
                            ("--archive-root", "APPIMAGE_RISCV64_ARCHIVE_ROOT")):
            self.assertEqual(arguments[arguments.index(option) + 1], pins[pin])
        toolchain = Path(self.environment["RESTUNTS_TOOLCHAIN_ROOT"])
        env_file = toolchain / "appimage-sysroots" / (TARGET + ".env")
        exports = subprocess.check_output(
            ["bash", "-c", 'source "$1"; printf "%s\\0%s" "$RESTUNTS_APPIMAGE_SYSROOT" '
             '"$RESTUNTS_APPIMAGE_QEMU"', "bash", str(env_file)], text=True)
        sysroot, emulator = exports.split("\0")
        self.assertEqual(sysroot, str(toolchain / "appimage-test-sysroots" / TARGET))
        self.assertEqual(emulator, str(qemu))
        self.assertEqual((Path(sysroot) / "lib").resolve(), Path(sysroot) / "usr/lib")

    def test_archived_runtime_step_preserves_bytes_and_test_failures(self):
        archive = self.root / "dist/packages" / ("restunts-" + TARGET + ".tar.gz")
        archive.parent.mkdir(parents=True)
        archive.write_bytes(b"exact uploaded archive")
        stub = self.scripts / "test-appimage-arm.sh"
        stub.write_text("""#!/bin/bash
set -eu
[[ "$1" == linux-appimage-riscv64 ]]
[[ "$2" == dist/packages/restunts-linux-appimage-riscv64.tar.gz ]]
[[ "$3" == "$RUNNER_TEMP/stunts" ]]
[[ "$4" == dist/appimage-tests/linux-appimage-riscv64 ]]
printf 'runtime test exit=%s\\n' "$APPIMAGE_WORKFLOW_TEST_EXIT"
exit "$APPIMAGE_WORKFLOW_TEST_EXIT"
""")
        for exit_code in (0, 17):
            with self.subTest(exit_code=exit_code):
                self.environment["APPIMAGE_WORKFLOW_TEST_EXIT"] = str(exit_code)
                result = self.run_step("Test the exact archived RISC-V AppImage against Debian 13")
                self.assertEqual(result.returncode, exit_code, result.stdout + result.stderr)
                self.assertEqual(archive.read_bytes(), b"exact uploaded archive")
                runner_log = self.root / "dist/appimage-tests" / TARGET / "runner.log"
                self.assertIn(f"runtime test exit={exit_code}", runner_log.read_text())


if __name__ == "__main__":
    unittest.main()
