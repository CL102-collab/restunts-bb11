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
    print({"-s": "NetBSD", "-m": os.environ["BSD_HELPER_TEST_ARCH"], "-r": "10.2"}[sys.argv[1]])
elif command == "pkg_admin":
    if sys.argv[1:] != ["config-var", "PKG_PATH"]:
        sys.exit("Unexpected pkg_admin arguments")
    if os.environ.get("BSD_HELPER_TEST_CONFIG_FAILURE"):
        sys.exit("Cannot query package configuration")
    print(package_path)
elif command == "pkg_add":
    with open(os.environ["BSD_HELPER_TEST_EVENTS"], "a") as output:
        output.write(json.dumps({"arguments": sys.argv[1:], "path": package_path,
                                 "environment_path": os.environ.get("PKG_PATH")}) + "\\n")
elif command not in ("curl", "cksum", "tar"):
    sys.exit("Unexpected mock command")
"""


class BsdPackageDependencyTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="bsd dependency helper ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.scripts = self.root / "tools/scripts"
        self.scripts.mkdir(parents=True)
        for name in (HELPER, "netbsd-x64-sets.sha512", "netbsd-x86-sets.sha512"):
            shutil.copy2(ROOT / "tools/scripts" / name, self.scripts / name)
        self.commands = self.root / "commands"
        self.commands.mkdir()
        for name in ("uname", "pkg_admin", "pkg_add", "curl", "cksum", "tar"):
            command = self.commands / name
            command.write_text(MOCK_COMMAND, encoding="utf-8")
            command.chmod(0o755)
        self.events = self.root / "events.jsonl"
        self.environment = dict(os.environ)
        self.environment.pop("PKG_PATH", None)
        self.environment.update({
            "PATH": str(self.commands) + os.pathsep + os.environ["PATH"],
            "BSD_HELPER_TEST_ARCH": "amd64",
            "BSD_HELPER_TEST_CONFIG_PATH": "",
            "BSD_HELPER_TEST_EVENTS": str(self.events),
        })

    def run_helper(self, target="netbsd-x64", environment=None):
        self.events.unlink(missing_ok=True)
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

    def test_configuration_query_error_fails_before_package_installation(self):
        result, events = self.run_helper(environment={"BSD_HELPER_TEST_CONFIG_FAILURE": "1"})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Cannot query package configuration", result.stderr)
        self.assertEqual(events, [])


if __name__ == "__main__":
    unittest.main()
