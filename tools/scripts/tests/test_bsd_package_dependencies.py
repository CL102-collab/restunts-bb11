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
CDN = "https://cdn.NetBSD.org/pub/pkgsrc/packages/NetBSD/"
JAIST = "https://ftp.jaist.ac.jp/pub/pkgsrc/packages/NetBSD/"
MOCK_COMMAND = """#!/usr/bin/env python3
import gzip, json, os, shutil, sys
from pathlib import Path

command = Path(sys.argv[0]).name
package_path = os.environ.get("PKG_PATH", os.environ.get("BSD_HELPER_TEST_CONFIG_PATH", ""))
bootstrap_packages = ("pkg_install-1.tgz", "sqlite3-2.tgz", "pkgin-3.tgz")
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
elif command == "timeout":
    os.environ["BSD_HELPER_TEST_DEADLINE"] = sys.argv[1]
    os.execvp(sys.argv[2], sys.argv[2:])
elif command in ("pkg_add", "pkg", "pkgin"):
    arguments = sys.argv[1:]
    event = {"command": command, "arguments": arguments, "path": package_path,
             "environment_path": os.environ.get("PKG_PATH"), "repository": os.environ.get("PKG_REPOS")}
    if command == "pkg" and "-R" in arguments:
        directory = Path(arguments[arguments.index("-R") + 1])
        event["repo_config"] = (directory / "FreeBSD.conf").read_text()
        event["deadline"] = os.environ.get("BSD_HELPER_TEST_DEADLINE")
        event["fetch_timeout"] = os.environ.get("FETCH_TIMEOUT")
        event["fetch_retry"] = os.environ.get("FETCH_RETRY")
    with open(os.environ["BSD_HELPER_TEST_EVENTS"], "a") as output:
        output.write(json.dumps(event) + "\\n")
    if "repo_config" in event:
        failure = os.environ.get("BSD_HELPER_TEST_FREEBSD_FAILURE")
        if failure == "catalogue" and "update" in arguments and 'url: "https://' in event["repo_config"]:
            sys.exit("No route to host")
        if failure == "download" and "-F" in arguments:
            sys.exit("Package download interrupted")
    if command == "pkg_add" and sys.argv[-1].endswith("/pkgin-3.tgz"):
        if any((Path(package_path) / name).read_bytes() != name.encode()
               for name in bootstrap_packages):
            sys.exit("pkg_add received incomplete bootstrap packages")
        shutil.copy2(Path(sys.argv[0]).with_name("pkgin.template"), Path(sys.argv[0]).with_name("pkgin"))
    if command == "pkgin" and sys.argv[1] == "-yd":
        if os.environ.get("BSD_HELPER_TEST_FAIL_DOWNLOAD") in (
                "all", os.environ["PKG_REPOS"].split("/")[2]):
            sys.exit("Package download interrupted")
elif command == "ftp":
    url = sys.argv[-1]
    destination = Path(sys.argv[sys.argv.index("-o") + 1])
    name = url.rsplit("/", 1)[1]
    with open(os.environ["BSD_HELPER_TEST_EVENTS"], "a") as output:
        output.write(json.dumps({"command": command, "url": url}) + "\\n")
    if name == "pkg_summary.gz":
        summary = "\\n\\n".join(f"PKGNAME={entry[:-4]}\\nFILE_NAME={entry}\\nFILE_SIZE={len(entry)}"
                              for entry in bootstrap_packages)
        destination.write_bytes(gzip.compress(summary.encode()))
    elif name in bootstrap_packages:
        data = name.encode()
        if name == "pkgin-3.tgz" and "cdn.NetBSD.org" in url:
            data = data[:-1]
        destination.write_bytes(data)
    else:
        sys.exit("Unexpected FTP URL")
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
        for name in (HELPER, "install-freebsd-packages.sh", "install-netbsd-packages.sh",
                     "netbsd-x64-sets.sha512", "netbsd-x86-sets.sha512",
                     "netbsd-arm64-sets.sha512", "netbsd-armv7-sets.sha512",
                     "netbsd-riscv64-sets.sha512"):
            shutil.copy2(ROOT / "tools/scripts" / name, self.scripts / name)
        x11 = self.root / "X11R6/include/X11/Xlib.h"
        x11.parent.mkdir(parents=True)
        x11.touch()
        for name in (HELPER, "install-netbsd-packages.sh"):
            helper = self.scripts / name
            helper.write_text(helper.read_text().replace("/usr/X11R6", str(self.root / "X11R6"))
                              .replace('export PATH="/usr/pkg/sbin:/usr/pkg/bin:$PATH"', ''))
        self.commands = self.root / "commands"
        self.commands.mkdir()
        for name in ("uname", "pkg_admin", "pkg_add", "pkg", "pkgin", "curl", "cksum", "tar", "ftp", "timeout"):
            command = self.commands / name
            command.write_text(MOCK_COMMAND, encoding="utf-8")
            command.chmod(0o755)
        self.events = self.root / "events.jsonl"
        self.downloads = self.root / "downloads.jsonl"
        self.environment = dict(os.environ)
        self.environment.pop("PKG_PATH", None)
        self.environment.pop("PKG_REPOS", None)
        self.environment.pop("RESTUNTS_FREEBSD_PACKAGE_CHANNEL", None)
        self.environment.update({
            "PATH": str(self.commands) + os.pathsep + os.environ["PATH"],
            "BSD_HELPER_TEST_ARCH": "amd64",
            "BSD_HELPER_TEST_CONFIG_PATH": "",
            "BSD_HELPER_TEST_EVENTS": str(self.events),
            "BSD_HELPER_TEST_DOWNLOADS": str(self.downloads),
        })

    def run_helper(self, target="netbsd-x64", environment=None, installer=HELPER, arguments=None):
        self.events.unlink(missing_ok=True)
        self.downloads.unlink(missing_ok=True)
        values = self.environment.copy()
        values.update(environment or {})
        result = subprocess.run(["sh", str(self.scripts / installer), *(arguments or (target,))],
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
            self.assertEqual(event["command"], "pkg_add")
            self.assertEqual(event["path"], package_path)
            self.assertEqual(event["environment_path"], environment_path)

    def assert_staged(self, result, events, repository):
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([event["command"] for event in events], ["pkgin"] * 4)
        self.assertEqual([event["repository"] for event in events], [repository] * 4)
        self.assertEqual([event["arguments"][0] for event in events], ["-yd", "-y"] * 2)
        self.assertIn("curl", events[0]["arguments"])
        self.assertEqual(events[3]["arguments"], ["-y", "install", "gtk3+"])

    def test_vm_configured_mirrors_are_preserved(self):
        result, events = self.run_helper(environment={"BSD_HELPER_TEST_CONFIG_PATH": CONFIGURED_PATH})
        self.assert_repositories(result, events, CONFIGURED_PATH, None)

    def test_official_mirror_failover_keeps_quarter_and_gates_install(self):
        package_tree = "x86_64/11.0_2026Q3/All"
        result, events = self.run_helper(environment={
            "BSD_HELPER_TEST_CONFIG_PATH": CDN + package_tree + ";" + JAIST + package_tree,
            "BSD_HELPER_TEST_FAIL_DOWNLOAD": "cdn.NetBSD.org",
        }, installer="install-netbsd-packages.sh", arguments=("clang", "lld"))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([event["repository"] for event in events],
                         [CDN + package_tree, JAIST + package_tree, JAIST + package_tree])
        self.assertEqual([event["arguments"] for event in events],
                         [["-yd", "install", "clang", "lld"], ["-yd", "install", "clang", "lld"],
                          ["-y", "install", "clang", "lld"]])

    def test_exhausted_downloads_never_install(self):
        result, events = self.run_helper(environment={"BSD_HELPER_TEST_FAIL_DOWNLOAD": "all"})
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual([event["arguments"][0] for event in events], ["-yd", "-yd"])

    def test_i386_bootstrap_verifies_archives_before_offline_pkg_add(self):
        (self.commands / "pkgin").rename(self.commands / "pkgin.template")
        result, events = self.run_helper(environment={"BSD_HELPER_TEST_ARCH": "i386"},
                                         installer="install-netbsd-packages.sh", arguments=("cmake",))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Incomplete NetBSD bootstrap package: pkgin-3.tgz", result.stderr)
        self.assertEqual([event["command"] for event in events],
                         ["ftp"] * 8 + ["pkg_add", "pkgin", "pkgin"])
        self.assertTrue(events[0]["url"].startswith(CDN + "i386/10.0/All/"))
        self.assertTrue(events[4]["url"].startswith(JAIST + "i386/10.0/All/"))
        staging = Path(events[8]["environment_path"])
        self.assertEqual(Path(events[8]["arguments"][-1]).parent, staging)
        self.assertFalse(staging.exists())

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
                self.assert_staged(result, events, CDN + f"{package_architecture}/10.0/All")

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
                self.assert_staged(result, events, CDN + f"{processor}/{major}.0/All")
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
                ("FreeBSD", "freebsd-x64", "amd64", "amd64"),
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

    def test_freebsd_ci_uses_signed_failover_and_installs_after_staging(self):
        result, events = self.run_helper("freebsd-x64", {
            "BSD_HELPER_TEST_SYSTEM": "FreeBSD", "RESTUNTS_FREEBSD_PACKAGE_CHANNEL": "latest",
            "BSD_HELPER_TEST_FREEBSD_FAILURE": "catalogue",
        })
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([event["arguments"][3:5] for event in events],
                         [["update", "-f"]] * 3 + [["install", "-y"]] * 2)
        self.assertIn("No route to host", result.stderr)
        self.assertIn('url: "https://pkg.freebsd.org/${ABI}/latest"', events[0]["repo_config"])
        self.assertIn('url: "https://pkg0.nyi.freebsd.org/${ABI}/latest"', events[1]["repo_config"])
        for event in events[2:]:
            self.assertIn('url: "http://pkg0.pao.freebsd.org/${ABI}/latest"', event["repo_config"])
        for event in events:
            self.assertEqual(event["arguments"][:2], ["-4", "-R"])
            self.assertEqual(event["fetch_timeout"], "20")
            self.assertEqual(event["fetch_retry"], "1")
            self.assertIn('mirror_type: "none"', event["repo_config"])
            self.assertIn('signature_type: "fingerprints"', event["repo_config"])
            self.assertIn('fingerprints: "/usr/share/keys/pkg"', event["repo_config"])
            self.assertNotIn("FreeBSD-kmods", event["repo_config"])
            self.assertFalse(Path(event["arguments"][2]).exists())
        self.assertEqual([event["deadline"] for event in events], ["2m"] * 3 + ["10m", None])
        download, install = (event["arguments"][5:] for event in events[-2:])
        self.assertEqual(download, ["-U", "-F", *install[1:]])
        self.assertEqual(install[0], "-U")
        self.assertIn("gtk3", install)

    def test_freebsd_ci_uses_direct_https_when_available(self):
        result, events = self.run_helper("freebsd-x64", {
            "BSD_HELPER_TEST_SYSTEM": "FreeBSD", "RESTUNTS_FREEBSD_PACKAGE_CHANNEL": "quarterly",
        })
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([event["arguments"][3] for event in events], ["update", "install", "install"])
        for event in events:
            self.assertIn('url: "https://pkg.freebsd.org/${ABI}/quarterly"', event["repo_config"])

    def test_freebsd_ci_does_not_install_when_downloads_fail(self):
        result, events = self.run_helper("freebsd-x64", {
            "BSD_HELPER_TEST_SYSTEM": "FreeBSD", "RESTUNTS_FREEBSD_PACKAGE_CHANNEL": "latest",
            "BSD_HELPER_TEST_FREEBSD_FAILURE": "download",
        })
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Could not download FreeBSD packages", result.stderr)
        self.assertEqual([event["arguments"][3] for event in events], ["update", "install"] * 3)
        self.assertTrue(all("-F" in event["arguments"] for event in events[1::2]))

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
