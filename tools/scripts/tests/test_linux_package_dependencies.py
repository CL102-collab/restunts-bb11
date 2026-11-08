"""Check the Debian riscv64 baseline and target dependency selection without apt."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
HELPER = "install-package-dependencies.sh"
MOCK_COMMAND = """#!/usr/bin/env python3
import json, os, sys
from pathlib import Path

command = Path(sys.argv[0]).name
if command == "dpkg-vendor":
    print(os.environ["LINUX_HELPER_TEST_VENDOR"])
elif command == "dpkg-query":
    print(os.environ["LINUX_HELPER_TEST_BASE_FILES"])
else:
    with open(os.environ["LINUX_HELPER_TEST_EVENTS"], "a") as output:
        output.write(json.dumps({"command": command, "arguments": sys.argv[1:]}) + "\\n")
"""


class LinuxPackageDependencyTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="linux dependency helper ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.scripts = self.root / "tools/scripts"
        self.scripts.mkdir(parents=True)
        for name in (HELPER, "package-toolchains.conf"):
            shutil.copy2(ROOT / "tools/scripts" / name, self.scripts / name)
        self.commands = self.root / "commands"
        self.commands.mkdir()
        for name in ("dpkg-vendor", "dpkg-query", "dpkg", "apt-get", "curl"):
            command = self.commands / name
            command.write_text(MOCK_COMMAND, encoding="utf-8")
            command.chmod(0o755)
        for name in ("prepare-appimage-riscv64-sysroot.sh", "build-appimage-runtime.sh"):
            (self.scripts / name).write_text(
                'exec python3 "' + str(self.commands / "mock-helper") + '" "${0##*/}" "$@"\n',
                encoding="utf-8")
        (self.commands / "mock-helper").write_text(MOCK_COMMAND, encoding="utf-8")
        self.events = self.root / "events.jsonl"
        self.environment = dict(os.environ)
        self.environment.update({
            "PATH": str(self.commands) + os.pathsep + os.environ["PATH"],
            "RESTUNTS_TOOLCHAIN_ROOT": str(self.root / "toolchains"),
            "LINUX_HELPER_TEST_VENDOR": "Debian",
            "LINUX_HELPER_TEST_BASE_FILES": "13.8+deb13u1",
            "LINUX_HELPER_TEST_EVENTS": str(self.events),
        })

    def run_helper(self, target="linux-riscv64", environment=None):
        self.events.unlink(missing_ok=True)
        values = self.environment.copy()
        values.update(environment or {})
        result = subprocess.run(["bash", str(self.scripts / HELPER), target],
                                cwd=self.root, env=values, text=True, capture_output=True,
                                timeout=15, check=False)
        events = ([json.loads(line) for line in self.events.read_text().splitlines()]
                  if self.events.exists() else [])
        return result, events

    def test_riscv64_installs_target_driver_headers_and_cross_compilers(self):
        result, events = self.run_helper()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(events[0], {"command": "dpkg", "arguments": ["--add-architecture", "riscv64"]})
        installations = [event["arguments"] for event in events
                         if event["command"] == "apt-get" and event["arguments"][0] == "install"]
        packages = {argument for arguments in installations for argument in arguments[1:]
                    if not argument.startswith("-")}
        self.assertIn("gcc-riscv64-linux-gnu", packages)
        self.assertIn("g++-riscv64-linux-gnu", packages)
        self.assertIn("qemu-user", packages)
        for dependency in ("libgtk-3-dev", "libasound2-dev", "libpulse-dev", "libx11-dev",
                           "libwayland-dev", "libegl1-mesa-dev"):
            self.assertIn(dependency + ":riscv64", packages)
            self.assertNotIn(dependency, packages)

    def test_riscv64_rejects_other_distribution_baselines_before_installing(self):
        for target in ("linux-riscv64", "linux-appimage-riscv64"):
            for vendor, version in (("Ubuntu", "13ubuntu10"), ("Debian", "12.4+deb12u1"),
                                    ("Debian", "14.1")):
                with self.subTest(target=target, vendor=vendor, version=version):
                    result, events = self.run_helper(target, {
                        "LINUX_HELPER_TEST_VENDOR": vendor,
                        "LINUX_HELPER_TEST_BASE_FILES": version,
                    })
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("must be built in Debian 13", result.stderr)
                    self.assertEqual(events, [])

    def test_riscv64_appimage_prepares_its_sdk_and_builds_its_own_loader(self):
        result, events = self.run_helper("linux-appimage-riscv64")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([event["arguments"] for event in events
                          if event["command"] == "mock-helper"], [
                              ["prepare-appimage-riscv64-sysroot.sh", "linux-appimage-riscv64"],
                              ["build-appimage-runtime.sh", "linux-appimage-riscv64"],
                          ])
        self.assertFalse(any(event["command"] in ("curl", "dpkg") for event in events))
        packages = next(event["arguments"] for event in events
                        if event["command"] == "apt-get" and event["arguments"][0] == "install")
        self.assertIn("gcc-riscv64-linux-gnu", packages)
        self.assertIn("g++-riscv64-linux-gnu", packages)
        self.assertIn("squashfs-tools", packages)


if __name__ == "__main__":
    unittest.main()
