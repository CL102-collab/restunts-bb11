"""Native-library import and Linux Haiku SDK wrapper regressions."""

import importlib.util
import io
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest

SCRIPTS = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("haiku_cross_sdk", SCRIPTS / "haiku-cross-sdk.py")
SDK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SDK)


class HaikuCrossSdkTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        self.sdk = self.directory / "sdk"
        self.sdk.mkdir()

    def tearDown(self):
        self.temporary.cleanup()

    def archive(self, target="haiku-x64", extra=None):
        secondary = "x86/" if target == "haiku-x86" else ""
        files = {
            "restunts-target": target + "\n",
            "restunts-release": "Haiku test R1~beta6 x86_64 Haiku\n",
            "restunts-installed-packages.txt": "haiku r1~beta6_hrev12345\n",
            "boot/system/develop/headers/posix/stdio.h": "/* target stdio */\n",
            "boot/system/develop/headers/os/app/Application.h": "/* target Application */\n",
            "boot/system/develop/headers/" + secondary + "opengl/GLView.h": "/* target GL */\n",
            "boot/system/lib/" + secondary + "libbe.so": "target be library\n",
            "boot/system/lib/" + secondary + "libGL.so": "target GL library\n",
            "boot/system/data/Qt5/mkspecs/haiku-g++/qplatformdefs.h": "/* Haiku Qt */\n",
            "boot/system/develop/lib/" + secondary + "cmake/Qt5/Qt5Config.cmake":
                'set(IMPORTED_LOCATION "/packages/qt5-5.15.14-3/.self/lib/Qt5Widgets.so")\n'
                'set(OPENGL "/packages/qt5_devel-5.15.19-2/devel~libgl/develop/lib/'
                + secondary + 'libGL.so")\n',
            "boot/system/lib/" + secondary + "wx/config/native-qt":
                '#!/bin/sh\nprefix=/packages/wxqt-3.2.4-2/.self\ncase "$1" in\n'
                '  --version) echo 3.2.4 ;;\n'
                '  --selected-config) basename "$0" ;;\n'
                '  --prefix) echo "$prefix" ;;\n'
                '  --cflags) echo "-I$prefix/develop/headers/wx" ;;\n'
                '  --libs) echo "-L$prefix/lib $prefix/lib/libbe.so '
                '-Wl,-rpath,$prefix/lib -Wl,-rpath-link,$prefix/lib" ;;\nesac\n',
        }
        if extra:
            files.update(extra)
        archive_path = self.directory / (target + ".tar.gz")
        with tarfile.open(archive_path, "w:gz") as archive:
            directories = {str(parent) for name in files
                           for parent in Path(name).parents if str(parent) != "."}
            for name in sorted(directories):
                member = tarfile.TarInfo(name)
                member.type = tarfile.DIRTYPE
                member.mode = 0o555
                archive.addfile(member)
            for name, content in files.items():
                member = tarfile.TarInfo(name)
                data = content.encode()
                member.size = len(data)
                member.mode = 0o444
                archive.addfile(member, io.BytesIO(data))
            member = tarfile.TarInfo("boot/system/bin/" + secondary + "wx-config")
            member.type = tarfile.SYMTYPE
            member.linkname = "/packages/wxqt-3.2.4-2/.self/lib/" + secondary + "wx/config/native-qt"
            archive.addfile(member)
            member = tarfile.TarInfo("boot/system/lib/" + secondary + "alias.so")
            member.type = tarfile.SYMTYPE
            member.linkname = "/system/lib/" + secondary + "libbe.so"
            archive.addfile(member)
        return archive_path

    def prepare(self, target="haiku-x64", extra=None):
        SDK.prepare_sysroot(target, self.sdk, self.archive(target, extra))
        SDK.prepare_wrappers(target, self.sdk)
        toolchain = self.sdk / "toolchain/bin"
        toolchain.mkdir(parents=True, exist_ok=True)
        for compiler in ("gcc", "g++"):
            binary = toolchain / (SDK.MACHINES[target] + "-" + compiler)
            binary.write_text('#!/bin/sh\ncase "$*" in\n'
                              '  *-print-search-dirs*) echo "libraries: =/some/root/" ;;\n'
                              '  *) printf "%s\\n" "$@" ;;\nesac\n')
            binary.chmod(0o755)

    def command(self, program, *arguments):
        return subprocess.check_output([self.sdk / "bin" / program, *arguments], text=True).strip()

    def test_import_paths_preserve_guest_runtime_paths(self):
        self.prepare()
        sysroot = str(self.sdk / "sysroot")
        target_script = self.sdk / "sysroot/boot/system/bin/wx-config"
        self.assertTrue(target_script.resolve().is_relative_to(self.sdk / "sysroot"))
        self.assertEqual((self.sdk / "sysroot/boot/system/lib/alias.so").resolve(),
                         self.sdk / "sysroot/boot/system/lib/libbe.so")
        qt_config = self.sdk / "sysroot/boot/system/develop/lib/cmake/Qt5/Qt5Config.cmake"
        self.assertIn(sysroot + "/boot/system/lib/Qt5Widgets.so", qt_config.read_text())
        self.assertIn(sysroot + "/boot/system/develop/lib/libGL.so", qt_config.read_text())
        self.assertEqual(SDK.rebase_paths(qt_config.read_text(), self.sdk / "sysroot"),
                         qt_config.read_text())
        self.assertEqual(self.command("wx-config", "--version"), "3.2.4")
        self.assertEqual(self.command("wx-config", "--selected-config"), "native-qt")
        self.assertEqual(self.command("wx-config", "--prefix"), sysroot + "/boot/system")
        self.assertEqual(self.command("wx-config", "--cflags"),
                         "-I" + sysroot + "/boot/system/develop/headers/wx")
        flags = self.command("wx-config", "--libs").split()
        self.assertIn("-L" + sysroot + "/boot/system/lib", flags)
        self.assertIn(sysroot + "/boot/system/lib/libbe.so", flags)
        self.assertIn("-Wl,-rpath,/boot/system/lib", flags)
        self.assertIn("-Wl,-rpath-link," + sysroot + "/boot/system/lib", flags)

    def test_compilers_forward_arguments_and_modern_hybrid_metadata(self):
        self.prepare("haiku-x86")
        arguments = self.command("cc", "-c", "a file.c").splitlines()
        self.assertEqual(arguments, ["--sysroot=" + str(self.sdk / "sysroot"),
                                     "-specs=" + str(self.sdk / "libexec/haiku-runtime.specs"),
                                     "-L" + str(self.sdk / "sysroot/boot/system/lib/x86"),
                                     "-Wl,-rpath-link," + str(self.sdk / "sysroot/boot/system/lib/x86"),
                                     "-c", "a file.c"])
        self.assertTrue(self.command("c++", "-print-search-dirs").endswith(
            ":/boot/system/develop/lib/x86/"))
        self.assertEqual((self.sdk / "restunts-machine").read_text().strip(), "i586-pc-haiku")

    def test_refresh_and_wrong_architecture_preserve_existing_sdk(self):
        self.prepare()
        changed = {"boot/system/lib/libbe.so": "new target library\n"}
        SDK.prepare_sysroot("haiku-x64", self.sdk, self.archive(extra=changed))
        library = self.sdk / "sysroot/boot/system/lib/libbe.so"
        self.assertEqual(library.read_text(), "new target library\n")
        with self.assertRaises(ValueError):
            SDK.prepare_sysroot("haiku-x64", self.sdk, self.archive("haiku-x86"))
        self.assertEqual(library.read_text(), "new target library\n")

    def test_rejects_archive_paths_outside_sdk(self):
        with self.assertRaises(ValueError):
            SDK.prepare_sysroot("haiku-x64", self.sdk,
                                self.archive(extra={"../escape": "not a target file"}))
        self.assertFalse((self.directory / "escape").exists())

    def test_rejects_absolute_links_outside_target_system(self):
        archive_path = self.directory / "unsafe.tar.gz"
        with tarfile.open(archive_path, "w:gz") as archive:
            member = tarfile.TarInfo("boot/system/lib/host")
            member.type = tarfile.SYMTYPE
            member.linkname = "/etc/passwd"
            archive.addfile(member)
        with self.assertRaises(ValueError):
            SDK.prepare_sysroot("haiku-x64", self.sdk, archive_path)


if __name__ == "__main__":
    unittest.main()
