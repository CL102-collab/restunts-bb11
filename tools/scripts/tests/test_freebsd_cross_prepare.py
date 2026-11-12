"""Exercise FreeBSD SDK selection, verified inputs, and mounted-image cleanup."""

import hashlib
import io
import json
import lzma
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / "tools/scripts/prepare-freebsd-cross.sh"
FAILURE_EXIT_CODE = 37
HOST_COMMAND = r'''import hashlib, json, os, pathlib, shutil, subprocess, sys, tarfile

command = pathlib.Path(sys.argv[0]).name
arguments = sys.argv[1:]
with open(os.environ["SDK_EVENTS"], "a") as events:
    events.write(json.dumps([command, arguments]) + "\n")
if command == "uname":
    print({"-s": "FreeBSD", "-m": "amd64", "-r": os.environ["SDK_HOST_RELEASE"]}[arguments[0]])
elif command == "id":
    print("0")
elif command == "clang":
    print(os.environ.get("SDK_CLANG_TARGETS", "    x86 - X86\n    arm - ARM\n    aarch64 - AArch64"))
elif command == "ld.lld":
    raise SystemExit("The linker should only be located, never run during SDK preparation")
elif command == "pkg":
    pass
elif command == "fetch":
    shutil.copyfile(os.environ["SDK_ARCHIVE"], arguments[2])
elif command == "sha256":
    print(hashlib.sha256(pathlib.Path(arguments[1]).read_bytes()).hexdigest())
elif command == "mdconfig":
    if arguments[0] == "-a":
        print("md42")
elif command == "mount":
    with tarfile.open(os.environ["SDK_IMAGE_ROOT"]) as archive:
        archive.extractall(arguments[-1], filter="data")
elif command == "umount":
    pass
elif command == "tar":
    arguments = [argument for argument in arguments
                 if argument not in ("--no-same-owner", "--no-fflags")]
    result = subprocess.run([os.environ["SDK_REAL_TAR"], *arguments])
    if result.returncode:
        raise SystemExit(result.returncode)
    if arguments[0] == "-cf" and os.environ.get("SDK_FAIL_IMAGE_COPY"):
        raise SystemExit(int(os.environ["SDK_FAIL_IMAGE_COPY"]))
else:
    raise SystemExit("Unexpected host command: " + command)
'''


class FreebsdCrossPreparationTests(unittest.TestCase):
    def setUp(self):
        self.real_tar = shutil.which("tar")
        if not self.real_tar or not shutil.which("xz"):
            self.skipTest("tar and xz are needed for FreeBSD SDK input checks")
        temporary = tempfile.TemporaryDirectory(prefix="FreeBSD SDK with spaces ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.script = self.root / SCRIPT.name
        shutil.copyfile(SCRIPT, self.script)
        self.sdk = self.root / "target SDK"
        self.events_path = self.root / "events.jsonl"
        self.tools = self.root / "host tools"
        self.tools.mkdir()
        dispatcher = self.tools / "host-command"
        dispatcher.write_text("#!" + sys.executable + "\n" + HOST_COMMAND, encoding="utf-8")
        dispatcher.chmod(0o755)
        for command in ("uname", "id", "clang", "ld.lld", "pkg", "fetch", "sha256",
                        "mdconfig", "mount", "umount", "tar"):
            (self.tools / command).symlink_to(dispatcher)
        (self.tools / "python3.13").symlink_to(sys.executable)
        self.fixture = self.root / "base.txz"
        with tarfile.open(self.fixture, "w:xz") as archive:
            for name, content in (("usr/include/target.h", b"SDK header\n"),
                                  ("usr/lib/libtarget.so", b"SDK library\n")):
                entry = tarfile.TarInfo(name)
                entry.size = len(content)
                archive.addfile(entry, io.BytesIO(content))
            entry = tarfile.TarInfo("usr/lib/libtarget-link.so")
            entry.type = tarfile.SYMTYPE
            entry.linkname = "libtarget.so"
            archive.addfile(entry)
        self.image = self.root / "GENERICSD.img.xz"
        self.image.write_bytes(lzma.compress(b"Pinned SD image fixture"))

    def run_preparation(self, target, release="14.5", *, corrupt_pin=False, **settings):
        image = target == "freebsd-armv7"
        archive = self.image if image else self.fixture
        name = target + ("-GENERICSD.img.xz" if image else "-base.txz")
        checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
        if corrupt_pin:
            checksum = "0" * len(checksum)
        (self.root / "freebsd-cross-sets.sha256").write_text(
            checksum + "  " + name + "\n", encoding="utf-8")
        environment = dict(os.environ, PATH=str(self.tools) + os.pathsep + os.environ["PATH"],
                           SDK_EVENTS=str(self.events_path), SDK_ARCHIVE=str(archive),
                           SDK_IMAGE_ROOT=str(self.fixture), SDK_REAL_TAR=self.real_tar,
                           SDK_HOST_RELEASE=release + "-RELEASE-p1", **settings)
        return subprocess.run(["sh", str(self.script), target, str(self.sdk)],
                              env=environment, text=True, capture_output=True, timeout=30)

    def events(self, command):
        if not self.events_path.exists():
            return []
        return [arguments for name, arguments in
                map(json.loads, self.events_path.read_text(encoding="utf-8").splitlines())
                if name == command]

    def assert_image_detached(self):
        self.assertEqual(self.events("umount"), [[self.events("mount")[0][-1]]])
        self.assertEqual(self.events("mdconfig")[-1], ["-d", "-u", "md42"])
        image_path = Path(self.events("mdconfig")[0][4])
        self.assertFalse(image_path.parent.exists())

    def test_new_targets_use_matching_release_target_abi_and_native_clang_backend(self):
        cases = (("freebsd-x86", "i386/i386/14.5-RELEASE/base.txz", "i386"),
                 ("freebsd-armv7", "arm/armv7/ISO-IMAGES/14.5/"
                  "FreeBSD-14.5-RELEASE-arm-armv7-GENERICSD.img.xz", "armv7"))
        for target, path, architecture in cases:
            with self.subTest(target=target):
                result = self.run_preparation(target)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(self.events("fetch")[-1][-1],
                                 "https://download.freebsd.org/releases/" + path)
                install = self.events("pkg")[-1]
                self.assertEqual(install[:2], ["-r", str(self.sdk)])
                self.assertIn("ABI=FreeBSD:14:" + architecture, install)
                self.assertIn("RUN_SCRIPTS=false", install)
                self.assertIn("gtk3", install)
                self.assertEqual((self.sdk / "usr/include/target.h").read_bytes(), b"SDK header\n")
                self.assertEqual((self.sdk / "usr/lib/libtarget-link.so").read_bytes(),
                                 b"SDK library\n")
                self.assertEqual((self.sdk / "restunts-target").read_text(), target + "\n")
                if target == "freebsd-armv7":
                    self.assertEqual(self.events("mount")[-1][:5],
                                     ["-t", "ufs", "-o", "ro,noexec", "/dev/md42s2a"])
                    self.assertIn("readonly", self.events("mdconfig")[0])
                    self.assert_image_detached()
                shutil.rmtree(self.sdk)

    def test_arm64_base_set_path_and_release_remain_unchanged(self):
        result = self.run_preparation("freebsd-arm64", release="14.4")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.events("fetch")[0][-1],
                         "https://download.freebsd.org/releases/arm64/aarch64/14.4-RELEASE/base.txz")
        self.assertIn("ABI=FreeBSD:14:aarch64", self.events("pkg")[-1])
        self.assertFalse(self.events("mdconfig"))

    def test_wrong_host_release_is_rejected_before_installing_or_downloading(self):
        result = self.run_preparation("freebsd-armv7", release="15.1")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("requires a FreeBSD 14.5 amd64 build host", result.stderr)
        self.assertFalse(self.events("pkg"))
        self.assertFalse(self.events("fetch"))

    def test_input_checksum_is_verified_before_extracting_or_mounting(self):
        for target in ("freebsd-x86", "freebsd-armv7"):
            with self.subTest(target=target):
                result = self.run_preparation(target, corrupt_pin=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("checksum mismatch", result.stderr)
                self.assertFalse(self.events("tar"))
                self.assertFalse(self.events("mdconfig"))
                self.assertFalse((self.sdk / "restunts-target").exists())

    def test_failed_image_copy_propagates_producer_error_and_detaches_image(self):
        result = self.run_preparation("freebsd-armv7", SDK_FAIL_IMAGE_COPY=str(FAILURE_EXIT_CODE))
        self.assertEqual(result.returncode, FAILURE_EXIT_CODE, result.stdout + result.stderr)
        self.assert_image_detached()
        self.assertEqual(len(self.events("pkg")), 1)
        self.assertFalse((self.sdk / "restunts-target").exists())

    def test_missing_arm_backend_fails_before_fetching_inputs(self):
        result = self.run_preparation("freebsd-armv7", SDK_CLANG_TARGETS="    aarch64 - AArch64")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Clang does not support arm for armv7", result.stderr)
        self.assertFalse(self.events("fetch"))


if __name__ == "__main__":
    unittest.main()
