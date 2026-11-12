"""Check BSD dependency repository selection without installing packages or downloading sets."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
HELPER = "install-bsd-package-dependencies.sh"
CONFIGURED_PATH = (
    "https://first.example/NetBSD/x86_64/10.0_2026Q3/All;"
    "https://second.example/NetBSD/x86_64/10.0_2026Q3/All"
)
MOCK_COMMAND = """#!/usr/bin/env python3
import json, os, sys
from pathlib import Path

command = Path(sys.argv[0]).name
package_path = os.environ.get("PKG_PATH", os.environ.get("BSD_HELPER_TEST_CONFIG_PATH", ""))
if command == "uname":
    print({"-s": os.environ.get("BSD_HELPER_TEST_SYSTEM", "NetBSD"),
           "-m": os.environ["BSD_HELPER_TEST_ARCH"],
           "-p": os.environ.get("BSD_HELPER_TEST_PROCESSOR", os.environ["BSD_HELPER_TEST_ARCH"]),
           "-r": os.environ.get("BSD_HELPER_TEST_RELEASE", "10.2")}[sys.argv[1]])
elif command == "pkg_admin":
    if sys.argv[1:] != ["config-var", "PKG_PATH"]:
        sys.exit("Unexpected pkg_admin arguments")
    if os.environ.get("BSD_HELPER_TEST_CONFIG_FAILURE"):
        sys.exit("Cannot query package configuration")
    print(package_path)
elif command in ("pkg_add", "pkg"):
    with open(os.environ["BSD_HELPER_TEST_EVENTS"], "a") as output:
        output.write(json.dumps({"arguments": sys.argv[1:], "path": package_path,
                                 "environment_path": os.environ.get("PKG_PATH")}) + "\\n")
elif command in ("curl", "cksum"):
    with open(os.environ["BSD_HELPER_TEST_DOWNLOADS"], "a") as output:
        output.write(json.dumps({"command": command, "arguments": sys.argv[1:],
                                 "input": sys.stdin.read() if command == "cksum" else ""}) + "\\n")
elif command != "tar":
    sys.exit("Unexpected mock command")
"""


class BsdPackageDependencyTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="bsd dependency helper ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.scripts = self.root / "tools/scripts"
        self.scripts.mkdir(parents=True)
        for name in (HELPER, "netbsd-x64-sets.sha512", "netbsd-x86-sets.sha512",
                     "netbsd-arm64-sets.sha512", "netbsd-armv7-sets.sha512",
                     "netbsd-riscv64-sets.sha512"):
            shutil.copy2(ROOT / "tools/scripts" / name, self.scripts / name)
        x11 = self.root / "X11R6/include/X11/Xlib.h"
        x11.parent.mkdir(parents=True)
        x11.touch()
        helper = self.scripts / HELPER
        helper.write_text(helper.read_text().replace("/usr/X11R6", str(self.root / "X11R6")))
        self.commands = self.root / "commands"
        self.commands.mkdir()
        for name in ("uname", "pkg_admin", "pkg_add", "pkg", "curl", "cksum", "tar"):
            command = self.commands / name
            command.write_text(MOCK_COMMAND, encoding="utf-8")
            command.chmod(0o755)
        self.events = self.root / "events.jsonl"
        self.downloads = self.root / "downloads.jsonl"
        self.environment = dict(os.environ)
        self.environment.pop("PKG_PATH", None)
        self.environment.update({
            "PATH": str(self.commands) + os.pathsep + os.environ["PATH"],
            "BSD_HELPER_TEST_ARCH": "amd64",
            "BSD_HELPER_TEST_CONFIG_PATH": "",
            "BSD_HELPER_TEST_EVENTS": str(self.events),
            "BSD_HELPER_TEST_DOWNLOADS": str(self.downloads),
        })

    def run_helper(self, target="netbsd-x64", environment=None):
        self.events.unlink(missing_ok=True)
        self.downloads.unlink(missing_ok=True)
        values = self.environment.copy()
        values.update(environment or {})
        result = subprocess.run(["sh", str(self.scripts / HELPER), target],
                                cwd=self.root, env=values, text=True, capture_output=True,
                                timeout=15, check=False)
        events = ([json.loads(line) for line in self.events.read_text().splitlines()]
                  if self.events.exists() else [])
        return result, events

    def assert_repositories(self, result, events, package_path, environment_path):
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(events), 2)
        self.assertIn("curl", events[0]["arguments"])
        self.assertEqual(events[1]["arguments"], ["-u", "gtk3+"])
        for event in events:
            self.assertEqual(event["path"], package_path)
            self.assertEqual(event["environment_path"], environment_path)

    def test_vm_configured_mirrors_are_preserved(self):
        result, events = self.run_helper(environment={"BSD_HELPER_TEST_CONFIG_PATH": CONFIGURED_PATH})
        self.assert_repositories(result, events, CONFIGURED_PATH, None)

    def test_explicit_environment_takes_precedence_over_vm_configuration(self):
        package_path = "/local package cache;https://custom.example/pkgsrc/All"
        result, events = self.run_helper(environment={
            "PKG_PATH": package_path,
            "BSD_HELPER_TEST_CONFIG_PATH": CONFIGURED_PATH,
        })
        self.assert_repositories(result, events, package_path, package_path)

    def test_unconfigured_native_and_i386_chroot_get_matching_fallbacks(self):
        for target, architecture, package_architecture in (
                ("netbsd-x64", "amd64", "x86_64"),
                ("netbsd-x64", "x86_64", "x86_64"),
                ("netbsd-x86", "i386", "i386"),
                ("netbsd-x86-no-sse2", "i386", "i386")):
            with self.subTest(target=target, architecture=architecture):
                result, events = self.run_helper(target, {"BSD_HELPER_TEST_ARCH": architecture})
                expected = ("https://cdn.netbsd.org/pub/pkgsrc/packages/NetBSD/"
                            f"{package_architecture}/10.0/All/")
                self.assert_repositories(result, events, expected, expected)

    def test_cross_targets_use_matching_netbsd_sets_and_package_abis(self):
        for target, machine, processor, release, set_architecture, suffix in (
                ("netbsd-arm64", "evbarm", "aarch64", "10.2", "evbarm-aarch64", "tar.xz"),
                ("netbsd-arm64", "aarch64", "aarch64", "10.2", "evbarm-aarch64", "tar.xz"),
                ("netbsd-armv7", "evbarm", "earmv7hf", "10.2", "evbarm-earmv7hf", "tgz"),
                ("netbsd-armv7", "earmv7hf", "earmv7hf", "10.2", "evbarm-earmv7hf", "tgz"),
                ("netbsd-riscv64", "riscv", "riscv64", "11.0", "riscv-riscv64", "tgz"),
                ("netbsd-riscv64", "riscv64", "riscv64", "11.0", "riscv-riscv64", "tgz")):
            with self.subTest(target=target, machine=machine):
                result, events = self.run_helper(target, {
                    "BSD_HELPER_TEST_ARCH": machine,
                    "BSD_HELPER_TEST_PROCESSOR": processor,
                    "BSD_HELPER_TEST_RELEASE": release,
                })
                major = release.split(".")[0]
                expected = ("https://cdn.netbsd.org/pub/pkgsrc/packages/NetBSD/"
                            f"{processor}/{major}.0/All/")
                self.assert_repositories(result, events, expected, expected)
                downloads = [json.loads(line) for line in self.downloads.read_text().splitlines()]
                for index, set_name in enumerate(("xbase", "xcomp")):
                    archive = f"{set_name}.{suffix}"
                    expected_url = (f"https://cdn.netbsd.org/pub/NetBSD/NetBSD-{release}/"
                                    f"{set_architecture}/binary/sets/{archive}")
                    self.assertIn(expected_url, downloads[index * 2]["arguments"])
                    manifest = (ROOT / "tools/scripts" /
                                f"{target}-sets.sha512").read_text().splitlines()
                    self.assertIn(downloads[index * 2 + 1]["input"].strip(), manifest)

    def test_freebsd_and_openbsd_cross_targets_install_native_dependencies(self):
        for system, target, machine, processor in (
                ("FreeBSD", "freebsd-arm64", "arm64", "aarch64"),
                ("FreeBSD", "freebsd-x86", "i386", "i386"),
                ("FreeBSD", "freebsd-armv7", "arm", "armv7"),
                ("FreeBSD", "freebsd-armv7", "armv7", "armv7"),
                ("FreeBSD", "freebsd-riscv64", "riscv", "riscv64"),
                ("OpenBSD", "openbsd-arm64", "arm64", "aarch64"),
                ("OpenBSD", "openbsd-x86", "i386", "i386"),
                ("OpenBSD", "openbsd-riscv64", "riscv64", "riscv64")):
            with self.subTest(target=target):
                result, events = self.run_helper(target, {
                    "BSD_HELPER_TEST_SYSTEM": system,
                    "BSD_HELPER_TEST_ARCH": machine,
                    "BSD_HELPER_TEST_PROCESSOR": processor,
                })
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(len(events), 1)
                cmake_package = "cmake-core" if target == "freebsd-riscv64" else "cmake"
                gtk_package = ("gtk2" if target == "freebsd-riscv64" else
                               "gtk3" if system == "FreeBSD" else "gtk+3")
                self.assertIn(cmake_package, events[0]["arguments"])
                self.assertIn(gtk_package, events[0]["arguments"])

    def test_wrong_architecture_is_rejected_before_installing_packages(self):
        for target in ("freebsd-x86", "freebsd-armv7", "freebsd-arm64", "openbsd-x86",
                       "openbsd-riscv64", "netbsd-armv7", "netbsd-arm64", "netbsd-riscv64"):
            with self.subTest(target=target):
                result, events = self.run_helper(target)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("requires its matching native BSD userspace", result.stderr)
                self.assertEqual(events, [])
                self.assertFalse(self.downloads.exists())

    def test_configuration_query_error_fails_before_package_installation(self):
        result, events = self.run_helper(environment={"BSD_HELPER_TEST_CONFIG_FAILURE": "1"})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Cannot query package configuration", result.stderr)
        self.assertEqual(events, [])


if __name__ == "__main__":
    unittest.main()
