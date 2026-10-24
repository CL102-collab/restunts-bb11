#!/usr/bin/env python3
"""Exercise archive integrity failures before release publication."""

import argparse
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import stat
import struct
import subprocess
import tarfile
import tempfile
import unittest
from unittest import mock
import zipfile


SCRIPT = Path(__file__).with_name("release-packages.py")
SPEC = importlib.util.spec_from_file_location("release_packages", SCRIPT)
PACKAGES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGES)
COMMIT = "a" * 40
OTHER_COMMIT = "b" * 40
RUN_ID = "12345"
RUN_ATTEMPT = "2"
FILE_DATA = b"release fixture\n"
FIXTURE_ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)
ELF_TARGETS = {
    "freebsd-x64": "FreeBSD x86-64",
    "haiku-x64": "Haiku x86-64",
    "haiku-x86": "Haiku 32-bit x86 with SSE2",
    "openbsd-x64": "OpenBSD x86-64",
    "netbsd-x64": "NetBSD x86-64",
    "netbsd-x86": "NetBSD 32-bit x86 with SSE2",
    "netbsd-x86-no-sse2": "NetBSD 32-bit x86; SSE2 instructions are disabled",
}


class ReleasePackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.output = self.root / "archives"

    def copy_car_sources(self, source):
        for name in PACKAGES.CUSTOM_CAR_FILES:
            path = source / "assets/cars" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes((PACKAGES.ROOT / "assets/cars" / name).read_bytes())

    def elf_data(self, elf_class, machine, shared=False):
        header = bytearray(PACKAGES.ELF_HEADER_SIZES[elf_class])
        header[:len(PACKAGES.ELF_MAGIC)] = PACKAGES.ELF_MAGIC
        header[PACKAGES.ELF_CLASS_OFFSET] = elf_class
        header[PACKAGES.ELF_DATA_OFFSET] = PACKAGES.ELF_DATA_LSB
        struct.pack_into("<H", header, PACKAGES.ELF_MACHINE_OFFSET, machine)
        if shared:
            struct.pack_into("<H", header, PACKAGES.ELF_TYPE_OFFSET, PACKAGES.ELF_TYPE_SHARED)
        return bytes(header) + FILE_DATA

    def apk_data(self, target, replacements=None, missing=()):
        abi = PACKAGES.ANDROID_TARGETS[target]
        elf_class, machine = PACKAGES.ELF_ARCHITECTURES[target.split("-")[1]]
        files = {name: self.runtime_data(target, name) for name in
                 PACKAGES.android_required_assets() | PACKAGES.android_required_resources(target)}
        files |= {"AndroidManifest.xml": FILE_DATA, "classes.dex": FILE_DATA,
                  "classes2.dex": FILE_DATA, "resources.arsc": FILE_DATA}
        files |= {f"lib/{abi}/{name}": self.elf_data(elf_class, machine, shared=True)
                  for name in PACKAGES.ANDROID_LIBRARIES}
        files.update(replacements or {})
        for name in missing:
            files.pop(name)
        output = io.BytesIO()
        with zipfile.ZipFile(output, "w") as apk:
            for name, content in files.items():
                apk.writestr(zipfile.ZipInfo(name, FIXTURE_ZIP_TIMESTAMP), content)
        return output.getvalue()

    def pie_data(self, target, interpreter=None):
        elf_class, machine = PACKAGES.ELF_ARCHITECTURES[target.split("-")[1]]
        header = bytearray(self.elf_data(elf_class, machine, shared=True)[
                           :PACKAGES.ELF_HEADER_SIZES[elf_class]])
        word = "<I" if elf_class == PACKAGES.ELF_CLASS_32 else "<Q"
        phsize = 32 if elf_class == PACKAGES.ELF_CLASS_32 else 56
        interpreter = interpreter or (b"/system/bin/linker\0" if elf_class == PACKAGES.ELF_CLASS_32
                                      else b"/system/bin/linker64\0")
        struct.pack_into(word, header, 24, 4096)
        struct.pack_into(word, header, 28 if elf_class == PACKAGES.ELF_CLASS_32 else 32, len(header))
        struct.pack_into("<HH", header, 42 if elf_class == PACKAGES.ELF_CLASS_32 else 54, phsize, 1)
        program = bytearray(phsize)
        struct.pack_into("<I", program, 0, 3)  # PT_INTERP
        struct.pack_into(word, program, 4 if elf_class == PACKAGES.ELF_CLASS_32 else 8,
                         len(header) + phsize)
        struct.pack_into(word, program, 16 if elf_class == PACKAGES.ELF_CLASS_32 else 32,
                         len(interpreter))
        return bytes(header + program) + interpreter

    def genghost_apk_data(self, target, replacements=None, missing=()):
        abi = PACKAGES.ANDROID_TARGETS[target]
        files = {name: self.runtime_data(target, name) for name in
                 PACKAGES.ANDROID_GENGHOST_ASSETS | PACKAGES.ANDROID_GENGHOST_RESOURCES |
                 PACKAGES.custom_car_files("assets/cars")}
        files |= {"AndroidManifest.xml": FILE_DATA, "classes.dex": FILE_DATA,
                  "classes2.dex": FILE_DATA, "classes3.dex": FILE_DATA, "resources.arsc": FILE_DATA,
                  f"lib/{abi}/libgenghost_exec.so": self.pie_data(target)}
        files.update(replacements or {})
        for name in missing:
            files.pop(name)
        output = io.BytesIO()
        with zipfile.ZipFile(output, "w") as apk:
            for name, content in files.items():
                apk.writestr(zipfile.ZipInfo(name, FIXTURE_ZIP_TIMESTAMP), content)
        return output.getvalue()

    def runtime_data(self, target, name):
        if name == "share/restunts/restunts-launcher-audit.json":
            return json.dumps({"passed": True, "problems": [],
                               "sha256": PACKAGES.sha256(FILE_DATA),
                               "forbid_simd": target.endswith("-no-sse2")}).encode()
        car_source = PACKAGES.custom_car_source(name)
        if car_source is not None:
            return car_source.read_bytes()
        if target in PACKAGES.ANDROID_TARGETS and name == PACKAGES.ANDROID_APK:
            return self.apk_data(target)
        if target in PACKAGES.ANDROID_TARGETS and name == PACKAGES.ANDROID_GENGHOST_APK:
            return self.genghost_apk_data(target)
        if target in PACKAGES.APPIMAGE_TARGETS:
            elf_class, machine = PACKAGES.ELF_ARCHITECTURES[PACKAGES.APPIMAGE_TARGETS[target]]
            if name == PACKAGES.APPIMAGE:
                content = bytearray(self.elf_data(elf_class, machine))
                offset = PACKAGES.APPIMAGE_MAGIC_OFFSET
                content[offset:offset + len(PACKAGES.APPIMAGE_MAGIC)] = PACKAGES.APPIMAGE_MAGIC
                return bytes(content)
            if name in PACKAGES.ELF_FILES:
                return self.elf_data(elf_class, machine)
        if target not in ELF_TARGETS or name not in PACKAGES.ELF_FILES:
            return FILE_DATA
        elf_class, machine = PACKAGES.ELF_ARCHITECTURES[target.split("-")[1]]
        return self.elf_data(elf_class, machine)

    def create(self, target, asset_data=None):
        runtime = self.root / target
        for name in PACKAGES.required_files(target) - {PACKAGES.README}:
            path = runtime / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(self.runtime_data(target, name))
            if name in {f"bin/{executable}" for executable in PACKAGES.NATIVE_EXECUTABLES} | {
                    "run-restunts.sh", PACKAGES.MACOS_LAUNCHER, PACKAGES.APPIMAGE}:
                path.chmod(PACKAGES.EXECUTABLE_MODE)
        for name, content in (asset_data or {}).items():
            path = runtime / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
        arguments = argparse.Namespace(target=target, runtime=runtime,
                                       directory=self.output, commit=COMMIT)
        environment = {"GITHUB_RUN_ID": RUN_ID, "GITHUB_RUN_ATTEMPT": RUN_ATTEMPT}
        with mock.patch.object(subprocess, "check_output", return_value=COMMIT + "\n"), \
                mock.patch.dict(os.environ, environment), contextlib.redirect_stdout(io.StringIO()):
            PACKAGES.create_package(arguments)
        return self.output / PACKAGES.archive_name(target)

    def checksum(self, archive):
        archive.with_name(archive.name + ".sha256").write_text(
            f"{PACKAGES.sha256(archive.read_bytes())}  {archive.name}\n", encoding="ascii")

    def test_complete_matrix_and_executable_permissions_survive_archiving(self):
        for target in PACKAGES.TARGETS:
            self.create(target)
        arguments = argparse.Namespace(directory=self.output, commit=COMMIT, all=True,
                                       run_id=RUN_ID, run_attempt=RUN_ATTEMPT)
        with contextlib.redirect_stdout(io.StringIO()):
            PACKAGES.verify_directory(arguments)
        files, modes = PACKAGES.archive_contents(self.output / PACKAGES.archive_name("linux-x64"))
        self.assertTrue(modes["bin/restunts"] & stat.S_IXUSR)
        self.assertEqual(files["bin/restunts"], FILE_DATA)

    def test_custom_cars_survive_every_archive_and_both_android_apks(self):
        for target in PACKAGES.TARGETS:
            with self.subTest(target=target):
                archive = self.create(target)
                files, _ = PACKAGES.archive_contents(archive)
                if target in PACKAGES.ANDROID_TARGETS:
                    collections = []
                    for apk_name in (PACKAGES.ANDROID_APK, PACKAGES.ANDROID_GENGHOST_APK):
                        with zipfile.ZipFile(io.BytesIO(files[apk_name])) as apk:
                            collections.append({name: apk.read(name) for name in apk.namelist()})
                    directory = "assets/cars"
                else:
                    collections = [files]
                    directory = "cars" if target == "dos16" else "bin/cars"
                    if target == "browser":
                        directory = "share/restunts/wasm-relink/data/assets/cars"
                for contents in collections:
                    packaged = {name for name in contents if name.startswith(directory + "/")}
                    self.assertEqual(packaged, PACKAGES.custom_car_files(directory))
                    for name in packaged:
                        expected = PACKAGES.custom_car_source(name).read_bytes()
                        self.assertEqual(contents[name], expected)
                        self.assertEqual(PACKAGES.sha256(contents[name]), PACKAGES.sha256(expected))

    def test_custom_car_sources_are_required_and_cannot_be_substituted(self):
        for target in PACKAGES.TARGETS:
            if target in PACKAGES.ANDROID_TARGETS:
                continue
            files = {name: self.runtime_data(target, name) for name in PACKAGES.required_files(target)}
            modes = {name: PACKAGES.EXECUTABLE_MODE for name in files}
            for name in files:
                if PACKAGES.custom_car_source(name) is None:
                    continue
                with self.subTest(target=target, missing=name), \
                        self.assertRaisesRegex(ValueError, "missing packaged files"):
                    PACKAGES.validate_contents(target, {key: data for key, data in files.items()
                                                        if key != name}, modes)
                with self.subTest(target=target, replaced=name), \
                        self.assertRaisesRegex(ValueError, "Custom car resource differs from source"):
                    PACKAGES.validate_contents(target, {**files, name: FILE_DATA}, modes)
        for target in PACKAGES.ANDROID_TARGETS:
            for make_apk, validate in ((self.apk_data, PACKAGES.validate_android_apk),
                                       (self.genghost_apk_data, PACKAGES.validate_android_genghost_apk)):
                for name in PACKAGES.custom_car_files("assets/cars"):
                    with self.subTest(target=target, apk=make_apk.__name__, missing=name), \
                            self.assertRaisesRegex(ValueError, "missing.*APK members"):
                        validate(target, make_apk(target, missing=(name,)))
                    with self.subTest(target=target, apk=make_apk.__name__, replaced=name), \
                            self.assertRaisesRegex(ValueError, "Custom car resource differs from source"):
                        validate(target, make_apk(target, {name: FILE_DATA}))

    def test_custom_car_exception_allows_only_distributed_names_and_directories(self):
        for directory in PACKAGES.CUSTOM_CAR_DIRECTORIES:
            for name in PACKAGES.CUSTOM_CAR_FILES:
                self.assertEqual(PACKAGES.checked_path(f"{directory}/{name}").name, name)
            for name in ("MISC.RES", "STCOUN.3SH", "STDACOUN.VSH", "CARTERR.res", "CARMCMU.PVS"):
                with self.subTest(directory=directory, name=name), \
                        self.assertRaisesRegex(ValueError, "Original game content"):
                    PACKAGES.checked_path(f"{directory}/{name}")
        for directory in ("", "bin", "assets", "cars/nested", "other/cars"):
            name = f"{directory}/CARTERR.RES" if directory else "CARTERR.RES"
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, "Original game content"):
                PACKAGES.checked_path(name)

    def test_missing_target_and_unexpected_release_asset_are_rejected(self):
        self.create("dos16")
        arguments = argparse.Namespace(directory=self.output, commit=COMMIT, all=True,
                                       run_id=RUN_ID, run_attempt=RUN_ATTEMPT)
        with self.assertRaisesRegex(ValueError, "Release file set differs"):
            PACKAGES.verify_directory(arguments)
        (self.output / "injected.exe").write_bytes(FILE_DATA)
        with self.assertRaisesRegex(ValueError, "injected.exe"):
            PACKAGES.verify_directory(arguments)

    def test_appimage_archive_retains_complete_runtime_and_rebuild_kit(self):
        for target, architecture in PACKAGES.APPIMAGE_TARGETS.items():
            with self.subTest(target=target):
                archive = self.create(target)
                files, modes = PACKAGES.archive_contents(archive)
                self.assertEqual(set(files) - {PACKAGES.MANIFEST},
                                 PACKAGES.required_files(f"linux-{architecture}") | {PACKAGES.APPIMAGE})
                self.assertEqual(files[PACKAGES.APPIMAGE], self.runtime_data(target, PACKAGES.APPIMAGE))
                self.assertTrue(modes[PACKAGES.APPIMAGE] & stat.S_IXUSR)
                instructions = files[PACKAGES.README].decode("utf-8")
                self.assertIn("bin/restunts.AppImage --data-dir", instructions)
                self.assertIn("--appimage-extract", instructions)
                self.assertIn("Ubuntu 10.04 (glibc 2.11)", instructions)
                if target.endswith("-no-sse2"):
                    self.assertIn("without SSE2", instructions)

    def test_appimage_requires_native_architecture_type2_magic_and_execute_permission(self):
        for target, architecture in PACKAGES.APPIMAGE_TARGETS.items():
            with self.subTest(target=target):
                elf_class, machine = PACKAGES.ELF_ARCHITECTURES[architecture]
                files = {name: self.runtime_data(target, name) for name in PACKAGES.required_files(target)}
                modes = {name: PACKAGES.EXECUTABLE_MODE for name in files}
                PACKAGES.validate_contents(target, files, modes)
                with self.assertRaisesRegex(ValueError, "missing packaged files.*restunts.AppImage"):
                    PACKAGES.validate_contents(target, {name: data for name, data in files.items()
                                                       if name != PACKAGES.APPIMAGE}, modes)
                with self.assertRaisesRegex(ValueError, "Missing executable permission.*restunts.AppImage"):
                    PACKAGES.validate_contents(target, files, {**modes, PACKAGES.APPIMAGE: PACKAGES.FILE_MODE})
                wrong_magic = bytearray(files[PACKAGES.APPIMAGE])
                wrong_magic[PACKAGES.APPIMAGE_MAGIC_OFFSET + 2] = 1
                with self.assertRaisesRegex(ValueError, "Type-2 AppImage"):
                    PACKAGES.validate_contents(target, {**files, PACKAGES.APPIMAGE: wrong_magic}, modes)
                wrong_architecture = (PACKAGES.ELF_CLASS_32, PACKAGES.ELF_MACHINE_X86) if \
                    elf_class == PACKAGES.ELF_CLASS_64 else (PACKAGES.ELF_CLASS_64, PACKAGES.ELF_MACHINE_X64)
                for name in PACKAGES.ELF_FILES | {PACKAGES.APPIMAGE}:
                    for content in (FILE_DATA, self.elf_data(*wrong_architecture),
                                    self.elf_data(elf_class, PACKAGES.ELF_MACHINE_AARCH64)):
                        with self.subTest(name=name, content=content), self.assertRaisesRegex(ValueError, "ELF"):
                            PACKAGES.validate_contents(target, {**files, name: content}, modes)

    def test_elf_archives_include_native_runtime_and_platform_instructions(self):
        for target, platform in ELF_TARGETS.items():
            with self.subTest(target=target):
                archive = self.create(target)
                self.assertEqual(archive.name, f"restunts-{target}.tar.gz")
                files, modes = PACKAGES.archive_contents(archive)
                for executable in PACKAGES.NATIVE_EXECUTABLES:
                    name = f"bin/{executable}"
                    self.assertEqual(files[name], self.runtime_data(target, name))
                    self.assertTrue(modes[name] & stat.S_IXUSR)
                for required in ("lib/libnuked-opl2.so", "share/restunts/nuked-opl2-lite/opl2.c",
                                 "share/licenses/restunts/Nuked-OPL2-LICENSE",
                                 "bin/opponents/game/opp6.png", "bin/skyboxes/sky4-3.png",
                                 "bin/menus/main.png", "bin/menus/showroom.png",
                                 "bin/menus/opponent.png"):
                    self.assertIn(required, files)
                self.assertIn(platform, files[PACKAGES.README].decode("utf-8"))
                if target.startswith("haiku-"):
                    self.assertIn("share/docs/restunts/haiku.md", files)
                    self.assertIn("share/docs/restunts/haiku.md", files[PACKAGES.README].decode("utf-8"))

    def test_elf_executables_must_have_execute_permission(self):
        for target in ELF_TARGETS:
            files = {name: self.runtime_data(target, name) for name in PACKAGES.required_files(target)}
            modes = {name: PACKAGES.FILE_MODE for name in files}
            for executable in PACKAGES.NATIVE_EXECUTABLES:
                modes[f"bin/{executable}"] = PACKAGES.EXECUTABLE_MODE
            PACKAGES.validate_contents(target, files, modes)
            for executable in PACKAGES.NATIVE_EXECUTABLES:
                name = f"bin/{executable}"
                with self.subTest(target=target, executable=executable), \
                        self.assertRaisesRegex(ValueError, f"Missing executable permission: {name}"):
                    PACKAGES.validate_contents(target, files, {**modes, name: PACKAGES.FILE_MODE})

    def test_genghost_uses_desktop_executables_and_android_apks(self):
        for target in PACKAGES.TARGETS:
            with self.subTest(target=target):
                files = PACKAGES.required_files(target)
                if target.startswith("dos") or target == "browser":
                    self.assertFalse(any("genghost" in name for name in files))
                elif target in PACKAGES.ANDROID_TARGETS:
                    self.assertIn(PACKAGES.ANDROID_GENGHOST_APK, files)
                    self.assertIn("share/docs/restunts/genghost.md", files)
                    self.assertIn("share/docs/restunts/genghost-android.md", files)
                    self.assertNotIn("bin/genghost", files)
                    self.assertNotIn("bin/genghost.exe", files)
                else:
                    generator = "bin/genghost.exe" if target.startswith("windows") else "bin/genghost"
                    self.assertIn(generator, files)
                    self.assertIn("share/docs/restunts/genghost.md", files)
                    self.assertIn(generator, PACKAGES.readme(target, COMMIT).decode("utf-8"))

    def test_desktop_packages_require_genghost_and_its_guide(self):
        files = {name: self.runtime_data("windows-x64", name) for name in PACKAGES.required_files("windows-x64")}
        modes = {name: PACKAGES.FILE_MODE for name in files}
        for missing in ("bin/genghost.exe", "share/docs/restunts/genghost.md",
                        *(f"share/licenses/gcc-runtime/{name}" for name in
                          PACKAGES.GCC_RUNTIME_NOTICES)):
            incomplete = files.copy()
            incomplete.pop(missing)
            with self.subTest(missing=missing), self.assertRaisesRegex(ValueError, "missing packaged"):
                PACKAGES.validate_contents("windows-x64", incomplete, modes)

    def test_windows_launcher_audits_reject_stale_failed_or_wrong_policy_results(self):
        target = "windows-x86-no-sse2"
        audit_name = "share/restunts/restunts-launcher-audit.json"
        files = {name: self.runtime_data(target, name) for name in PACKAGES.required_files(target)}
        modes = {name: PACKAGES.FILE_MODE for name in files}
        audit = json.loads(files[audit_name])
        for replacement in ({"passed": False}, {"problems": ["Vista API"]},
                            {"sha256": "0" * 64}, {"forbid_simd": False}):
            changed = {**files, audit_name: json.dumps({**audit, **replacement}).encode()}
            with self.subTest(replacement=replacement), self.assertRaisesRegex(ValueError, "launcher"):
                PACKAGES.validate_contents(target, changed, modes)
        with self.assertRaisesRegex(ValueError, "differs from its compatibility audit"):
            PACKAGES.validate_contents(target, {**files, "bin/restunts-launcher.exe": b"changed"}, modes)

    def test_elf_archives_reject_wrong_or_missing_elf_architecture(self):
        for target in ELF_TARGETS:
            files = {name: self.runtime_data(target, name) for name in PACKAGES.required_files(target)}
            modes = {name: PACKAGES.EXECUTABLE_MODE for name in files}
            for name in PACKAGES.ELF_FILES:
                wrong_class = bytearray(files[name])
                wrong_class[PACKAGES.ELF_CLASS_OFFSET] = (
                    PACKAGES.ELF_CLASS_32 if target.endswith("-x64") else PACKAGES.ELF_CLASS_64)
                wrong_machine = bytearray(files[name])
                machine = PACKAGES.ELF_MACHINE_X86 if target.endswith("-x64") else PACKAGES.ELF_MACHINE_X64
                struct.pack_into("<H", wrong_machine, PACKAGES.ELF_MACHINE_OFFSET, machine)
                for content in (FILE_DATA, files[name][:PACKAGES.ELF_MACHINE_OFFSET],
                                wrong_class, wrong_machine):
                    with self.subTest(target=target, name=name, content=content), \
                            self.assertRaisesRegex(ValueError, "ELF"):
                        PACKAGES.validate_contents(target, {**files, name: content}, modes)

    def test_complete_release_requires_every_elf_archive(self):
        for target in PACKAGES.TARGETS:
            self.create(target)
        arguments = argparse.Namespace(directory=self.output, commit=COMMIT, all=True,
                                       run_id=RUN_ID, run_attempt=RUN_ATTEMPT)
        for target in ELF_TARGETS:
            archive = self.output / PACKAGES.archive_name(target)
            saved = self.root / archive.name
            archive.rename(saved)
            try:
                with self.subTest(target=target), \
                        self.assertRaisesRegex(ValueError, f"missing=.*{archive.name}"):
                    PACKAGES.verify_directory(arguments)
            finally:
                saved.rename(archive)

    def test_android_archives_include_installable_apk_sources_and_import_instructions(self):
        for target, abi in PACKAGES.ANDROID_TARGETS.items():
            with self.subTest(target=target):
                archive = self.create(target)
                self.assertEqual(archive.name, f"restunts-{target}.zip")
                files, _ = PACKAGES.archive_contents(archive)
                self.assertEqual(files[PACKAGES.ANDROID_APK], self.apk_data(target))
                self.assertEqual(files[PACKAGES.ANDROID_GENGHOST_APK], self.genghost_apk_data(target))
                for name in ("share/docs/restunts/android.md", "share/docs/restunts/genghost.md",
                             "share/docs/restunts/genghost-android.md", "share/licenses/restunts/SDL-LICENSE.txt",
                             "share/licenses/restunts/Nuked-OPL2-LICENSE",
                             "share/restunts/nuked-opl2-lite/opl2.c",
                             "share/restunts/nuked-opl2-lite/nuked-build-info.txt",
                             "share/restunts/nuked-opl2-lite/build-context/CMakeLists.txt"):
                    self.assertIn(name, files)
                self.assertFalse(any(name.startswith(("bin/music/", "bin/menus/", "bin/opponents/"))
                                     for name in files))
                instructions = files[PACKAGES.README].decode("utf-8")
                minimum = "Android 5.0 (API 21)"
                for detail in (abi, minimum, "landscape", "bin/restunts.apk", "bin/genghost.apk",
                               "share/docs/restunts/genghost.md", "share/docs/restunts/genghost-android.md",
                               "Choose game folder",
                               "Import Stunts ZIP", "exact destination folder",
                               "selected public folder", "private working cache",
                               "tracks, replays and screenshots", "persistent release signing key",
                               "PR/local debug APKs use debug keys", "same signing key",
                               "Folders selected through the system picker survive",
                               "TV fallback media folder", "is deleted on uninstall",
                               "Back up those files outside it beforehand",
                               "unexported pending writes", "share/docs/restunts/android.md"):
                    self.assertIn(detail, instructions)
                for obsolete in ("This development APK", "Import Stunts folder",
                                 "removes imported game files and saves"):
                    self.assertNotIn(obsolete, instructions)
                if target == "android-armv7":
                    self.assertIn("Samsung Galaxy S5 SM-G900F running Android 5.0 or newer", instructions)

    def test_android_minimum_sdk_and_launcher_resources_match_each_target(self):
        for target in PACKAGES.ANDROID_TARGETS:
            with self.subTest(target=target):
                self.assertEqual(PACKAGES.android_minimum_sdk(target), 21)
                with self.assertRaisesRegex(ValueError, "missing APK members.*mipmap-anydpi-v21"):
                    PACKAGES.validate_android_apk(target, self.apk_data(
                        target, missing=(PACKAGES.ANDROID_FALLBACK_ICON,)))
                PACKAGES.validate_android_apk(target, self.apk_data(target))
        settings = self.root / "toolchain.properties"
        with mock.patch.object(PACKAGES, "ANDROID_TOOLCHAIN_PROPERTIES", settings):
            for contents in ("ANDROID_MIN_SDK=28\n", "ANDROID_ARMV7_MIN_SDK=invalid\n"):
                settings.write_text(contents, encoding="utf-8")
                with self.subTest(contents=contents), self.assertRaisesRegex(ValueError, "toolchain property"):
                    PACKAGES.android_minimum_sdk("android-armv7")
            settings.write_text("ANDROID_ARMV7_MIN_SDK=19\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Unsupported Android package minimum API"):
                PACKAGES.android_minimum_sdk("android-armv7")

    def test_android_apks_require_exact_abi_and_all_shared_libraries(self):
        for target, abi in PACKAGES.ANDROID_TARGETS.items():
            with self.subTest(target=target):
                PACKAGES.validate_android_apk(target, self.apk_data(target))
            other = next(other for other in PACKAGES.ANDROID_TARGETS if other != target)
            with self.subTest(target=target, error="other ABI"), \
                    self.assertRaisesRegex(ValueError, "Unexpected Android ABI"):
                PACKAGES.validate_android_apk(target, self.apk_data(other))
            for name in PACKAGES.ANDROID_LIBRARIES:
                member = f"lib/{abi}/{name}"
                with self.subTest(target=target, missing=member), \
                        self.assertRaisesRegex(ValueError, "missing APK members"):
                    PACKAGES.validate_android_apk(target, self.apk_data(target, missing=(member,)))
            extra = f"lib/{PACKAGES.ANDROID_TARGETS[other]}/libmain.so"
            with self.subTest(target=target, extra=extra), \
                    self.assertRaisesRegex(ValueError, "Unexpected Android ABI"):
                PACKAGES.validate_android_apk(target, self.apk_data(target, {extra: FILE_DATA}))
            with self.subTest(target=target, error="extra library"), \
                    self.assertRaisesRegex(ValueError, "Unexpected Android ABI or native library"):
                PACKAGES.validate_android_apk(target, self.apk_data(target, {f"lib/{abi}/other.so": FILE_DATA}))

    def test_android_apks_reject_wrong_elf_headers_missing_assets_and_original_game_data(self):
        for target, abi in PACKAGES.ANDROID_TARGETS.items():
            elf_class, machine = PACKAGES.ELF_ARCHITECTURES[target.split("-")[1]]
            other_class = PACKAGES.ELF_CLASS_64 if elf_class == PACKAGES.ELF_CLASS_32 else PACKAGES.ELF_CLASS_32
            for library in PACKAGES.ANDROID_LIBRARIES:
                name = f"lib/{abi}/{library}"
                for content in (FILE_DATA, self.elf_data(elf_class, PACKAGES.ELF_MACHINE_X86, shared=True),
                                self.elf_data(other_class, machine, shared=True),
                                self.elf_data(elf_class, machine)):
                    with self.subTest(target=target, library=library, content=content), \
                            self.assertRaisesRegex(ValueError, "ELF"):
                        PACKAGES.validate_android_apk(target, self.apk_data(target, {name: content}))
            for missing in ("AndroidManifest.xml", "classes.dex", "res/drawable/tv_banner.xml",
                            "resources.arsc", "assets/licenses/Nuked-OPL2-LICENSE.txt",
                            "assets/skyboxes/city-sce2.png", "assets/menus/main.png",
                            "assets/opponents/game/opp6.png"):
                with self.subTest(target=target, missing=missing), \
                        self.assertRaisesRegex(ValueError, "missing APK members"):
                    PACKAGES.validate_android_apk(target, self.apk_data(target, missing=(missing,)))
            for extra in ("assets/MISC.RES", "assets/game/FONTDEF.FNT", "assets/data.zip",
                          "assets/DEFAULT.P3S", "stunts/notice.txt", "assets/private.dat"):
                with self.subTest(target=target, extra=extra), self.assertRaises(ValueError):
                    PACKAGES.validate_android_apk(target, self.apk_data(target, {extra: FILE_DATA}))

    def test_android_generator_apks_require_both_abis_and_exact_assets(self):
        assets = {"assets/genghost.md", "assets/genghost-android.md", "assets/licenses/SDL-LICENSE.txt",
                  "assets/licenses/Android-NDK-NOTICE.txt",
                  "assets/licenses/Android-NDK-NOTICE.toolchain.txt"}
        assets |= PACKAGES.custom_car_files("assets/cars")
        for target, abi in PACKAGES.ANDROID_TARGETS.items():
            content = self.genghost_apk_data(target)
            with self.subTest(target=target):
                PACKAGES.validate_android_genghost_apk(target, content)
                with zipfile.ZipFile(io.BytesIO(content)) as apk:
                    self.assertEqual({name for name in apk.namelist() if name.startswith("assets/")}, assets)
                    self.assertEqual({name for name in apk.namelist() if name.startswith("lib/")},
                                     {f"lib/{abi}/libgenghost_exec.so"})
            for missing in assets | {"AndroidManifest.xml", "classes.dex", "resources.arsc",
                                     "res/drawable/ic_genghost.xml", f"lib/{abi}/libgenghost_exec.so"}:
                with self.subTest(target=target, missing=missing), \
                        self.assertRaisesRegex(ValueError, "missing generator APK members"):
                    PACKAGES.validate_android_genghost_apk(target, self.genghost_apk_data(
                        target, missing=(missing,)))
            other = next(other for other in PACKAGES.ANDROID_TARGETS if other != target)
            with self.subTest(target=target, wrong_abi=True), \
                    self.assertRaisesRegex(ValueError, "Unexpected Android ABI"):
                PACKAGES.validate_android_genghost_apk(target, self.genghost_apk_data(other))
            for extra in (f"lib/{abi}/libmain.so", f"lib/{abi}/genghost",
                          f"lib/{PACKAGES.ANDROID_TARGETS[other]}/libgenghost_exec.so",
                          "bin/genghost", "assets/MISC.RES", "assets/game/FONTDEF.FNT",
                          "assets/data.zip", "assets/private.dat", "assets/music/titl.ogg",
                          "assets/licenses/Nuked-OPL2-LICENSE.txt", "stunts/notice.txt"):
                with self.subTest(target=target, extra=extra), self.assertRaises(ValueError):
                    PACKAGES.validate_android_genghost_apk(target, self.genghost_apk_data(
                        target, {extra: FILE_DATA}))

    def test_android_generator_apks_require_complete_pie_executables(self):
        for target, abi in PACKAGES.ANDROID_TARGETS.items():
            elf_class, machine = PACKAGES.ELF_ARCHITECTURES[target.split("-")[1]]
            other_class = PACKAGES.ELF_CLASS_64 if elf_class == PACKAGES.ELF_CLASS_32 else PACKAGES.ELF_CLASS_32
            word = "<I" if elf_class == PACKAGES.ELF_CLASS_32 else "<Q"
            valid = self.pie_data(target)
            nonpie = bytearray(valid)
            struct.pack_into("<H", nonpie, PACKAGES.ELF_TYPE_OFFSET, 2)  # ET_EXEC
            zero_entry = bytearray(valid)
            struct.pack_into(word, zero_entry, 24, 0)
            no_interpreter = bytearray(valid)
            struct.pack_into("<I", no_interpreter, PACKAGES.ELF_HEADER_SIZES[elf_class], 1)  # PT_LOAD
            wrong_table = bytearray(valid)
            struct.pack_into(word, wrong_table, 28 if elf_class == PACKAGES.ELF_CLASS_32 else 32,
                             len(valid))
            wrong_phsize = bytearray(valid)
            struct.pack_into("<H", wrong_phsize, 42 if elf_class == PACKAGES.ELF_CLASS_32 else 54, 1)
            cases = (FILE_DATA, valid[:PACKAGES.ELF_HEADER_SIZES[elf_class] - 1],
                     valid[:PACKAGES.ELF_HEADER_SIZES[elf_class] + 2], valid[:-1],
                     self.elf_data(other_class, machine, shared=True),
                     self.elf_data(elf_class, PACKAGES.ELF_MACHINE_X86, shared=True),
                     self.elf_data(elf_class, machine, shared=True), nonpie, zero_entry,
                     no_interpreter, wrong_table, wrong_phsize,
                     self.pie_data(target, b"/system/bin/wrong-linker\0"),
                     self.pie_data(target, b"/system/bin/linker64\0" if elf_class == PACKAGES.ELF_CLASS_32
                                   else b"/system/bin/linker\0"))
            for index, content in enumerate(cases):
                with self.subTest(target=target, case=index), self.assertRaisesRegex(ValueError, "ELF"):
                    PACKAGES.validate_android_genghost_apk(target, self.genghost_apk_data(
                        target, {f"lib/{abi}/libgenghost_exec.so": content}))

    def test_android_generator_apks_enforce_member_integrity_and_metadata(self):
        for target in PACKAGES.ANDROID_TARGETS:
            signatures = {"META-INF/MANIFEST.MF": FILE_DATA, "META-INF/CERT.SF": FILE_DATA,
                          "META-INF/CERT.RSA": FILE_DATA,
                          "META-INF/version-control-info.textproto": FILE_DATA,
                          "META-INF/com/android/build/gradle/app-metadata.properties": FILE_DATA}
            PACKAGES.validate_android_genghost_apk(target, self.genghost_apk_data(target, signatures))
            for name in ("assets/release.jks", "META-INF/signing.properties", "META-INF/other.textproto",
                         "res/drawable/ic_launcher_foreground.xml"):
                with self.subTest(target=target, unexpected=name), self.assertRaisesRegex(ValueError, "Unexpected APK"):
                    PACKAGES.validate_android_genghost_apk(target, self.genghost_apk_data(target, {name: FILE_DATA}))
            for name, mode, message in (("../outside.txt", stat.S_IFREG, "Unsafe"),
                                       ("assets/genghost.md", stat.S_IFREG, "Duplicate APK"),
                                       ("assets/genghost.md", stat.S_IFLNK, "Unsupported APK")):
                missing = (name,) if mode == stat.S_IFLNK else ()
                output = io.BytesIO(self.genghost_apk_data(target, missing=missing))
                with zipfile.ZipFile(output, "a") as apk:
                    entry = zipfile.ZipInfo(name)
                    entry.external_attr = (mode | PACKAGES.FILE_MODE) << 16
                    with contextlib.redirect_stderr(io.StringIO()):
                        apk.writestr(entry, FILE_DATA)
                with self.subTest(target=target, name=name, mode=mode), self.assertRaisesRegex(ValueError, message):
                    PACKAGES.validate_android_genghost_apk(target, output.getvalue())

    def test_android_apks_allow_only_named_gradle_version_control_metadata(self):
        metadata = {"META-INF/version-control-info.textproto":
                    b"generate_error_reason: NO_SUPPORTED_VCS_FOUND\n"}
        for target in PACKAGES.ANDROID_TARGETS:
            with self.subTest(target=target):
                content = self.apk_data(target, metadata)
                PACKAGES.validate_android_apk(target, content)
                self.create(target, {PACKAGES.ANDROID_APK: content})
            for name in ("META-INF/version-control-info.txt", "META-INF/other.textproto",
                         "META-INF/version-control-info.textproto.bak",
                         "assets/version-control-info.textproto"):
                with self.subTest(target=target, unexpected=name), \
                        self.assertRaisesRegex(ValueError, "Unexpected APK"):
                    PACKAGES.validate_android_apk(target, self.apk_data(target, {name: FILE_DATA}))

    def test_android_packages_reject_signing_credentials_but_allow_public_signatures(self):
        credentials = ("release.jks", "release.keystore", "release.p12", "signing-key.pem",
                       "keystore.base64", "signing.properties")
        for target in PACKAGES.ANDROID_TARGETS:
            with self.subTest(target=target, public_signatures=True):
                signatures = {"META-INF/MANIFEST.MF": FILE_DATA,
                              "META-INF/RESTUNTS.SF": FILE_DATA,
                              "META-INF/RESTUNTS.RSA": FILE_DATA}
                PACKAGES.validate_android_apk(target, self.apk_data(target, signatures))
            files = {name: self.runtime_data(target, name) for name in PACKAGES.required_files(target)}
            modes = {name: PACKAGES.FILE_MODE for name in files}
            for credential in credentials:
                for prefix in ("", "assets/", "assets/licenses/", "META-INF/", "res/raw/"):
                    name = prefix + credential
                    with self.subTest(target=target, apk_member=name), \
                            self.assertRaisesRegex(ValueError, "Unexpected APK"):
                        PACKAGES.validate_android_apk(target, self.apk_data(target, {name: FILE_DATA}))
                for prefix in ("", "share/licenses/restunts/", "share/restunts/nuked-opl2-lite/"):
                    name = prefix + credential
                    with self.subTest(target=target, package_member=name), \
                            self.assertRaisesRegex(ValueError, "Unexpected package file"):
                        PACKAGES.validate_contents(target, files | {name: FILE_DATA},
                                                   modes | {name: PACKAGES.FILE_MODE})

    def test_android_apks_reject_duplicate_members_symlinks_and_unsafe_paths(self):
        target = "android-armv7"
        for name, mode, message in (("../outside.txt", stat.S_IFREG, "Unsafe"),
                                    ("assets/menus/main.png", stat.S_IFREG, "Duplicate APK"),
                                    ("assets/menus/main.png", stat.S_IFLNK, "Unsupported APK")):
            missing = (name,) if mode == stat.S_IFLNK else ()
            output = io.BytesIO(self.apk_data(target, missing=missing))
            with zipfile.ZipFile(output, "a") as apk:
                entry = zipfile.ZipInfo(name)
                entry.external_attr = (mode | PACKAGES.FILE_MODE) << 16
                with contextlib.redirect_stderr(io.StringIO()):
                    apk.writestr(entry, FILE_DATA)
            with self.subTest(name=name, mode=mode), self.assertRaisesRegex(ValueError, message):
                PACKAGES.validate_android_apk(target, output.getvalue())

    def test_android_optional_source_music_is_required_inside_apk_only(self):
        source = self.root / "android-source"
        self.copy_car_sources(source)
        with mock.patch.object(PACKAGES, "ROOT", source):
            baseline = {target: self.apk_data(target) for target in PACKAGES.ANDROID_TARGETS}
            for track in PACKAGES.MUSIC_TRACKS:
                path = source / "assets/music" / f"{track}.ogg"
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"OggS Android music fixture " + track.encode("ascii"))
            audio = {f"assets/music/{track}.ogg":
                     (source / "assets/music" / f"{track}.ogg").read_bytes()
                     for track in PACKAGES.MUSIC_TRACKS}
            for target in PACKAGES.ANDROID_TARGETS:
                with self.subTest(target=target):
                    self.assertEqual(PACKAGES.optional_music_files(target), set())
                    with self.assertRaisesRegex(ValueError, "missing APK members.*assets/music"):
                        PACKAGES.validate_android_apk(target, baseline[target])
                    content = self.apk_data(target, audio)
                    PACKAGES.validate_android_apk(target, content)
                    archive = self.create(target, {PACKAGES.ANDROID_APK: content})
                    files, _ = PACKAGES.archive_contents(archive)
                    with zipfile.ZipFile(io.BytesIO(files[PACKAGES.ANDROID_APK])) as apk:
                        for name, music in audio.items():
                            self.assertEqual(apk.read(name), music)

    def test_android_docs_sources_licenses_and_apk_manifest_are_required(self):
        target = "android-armv7"
        files = {name: self.runtime_data(target, name) for name in PACKAGES.required_files(target)}
        modes = {name: PACKAGES.FILE_MODE for name in files}
        for missing in (PACKAGES.ANDROID_APK, PACKAGES.ANDROID_GENGHOST_APK,
                        "share/docs/restunts/android.md", "share/docs/restunts/genghost.md",
                        "share/docs/restunts/genghost-android.md",
                        "share/restunts/nuked-opl2-lite/opl2.c",
                        "share/restunts/nuked-opl2-lite/build-context/CMakeLists.txt",
                        "share/licenses/restunts/SDL-LICENSE.txt",
                        "share/licenses/restunts/Nuked-OPL2-LICENSE"):
            incomplete = files.copy()
            incomplete.pop(missing)
            with self.subTest(missing=missing), self.assertRaisesRegex(ValueError, "missing packaged files"):
                PACKAGES.validate_contents(target, incomplete, modes)
        archive = self.create(target)
        packaged, permissions = PACKAGES.archive_contents(archive)
        packaged[PACKAGES.ANDROID_APK] = self.apk_data(target, {"classes.dex": b"changed application"})
        with zipfile.ZipFile(archive, "w") as output:
            for name, content in packaged.items():
                entry = zipfile.ZipInfo(name)
                entry.external_attr = (stat.S_IFREG | permissions[name]) << 16
                output.writestr(entry, content)
        self.checksum(archive)
        with self.assertRaisesRegex(ValueError, "File differs from manifest: bin/restunts.apk"):
            PACKAGES.verify_package(archive, target, COMMIT)

    def test_complete_release_requires_both_android_archives(self):
        for target in PACKAGES.TARGETS:
            self.create(target)
        arguments = argparse.Namespace(directory=self.output, commit=COMMIT, all=True,
                                       run_id=RUN_ID, run_attempt=RUN_ATTEMPT)
        for target in PACKAGES.ANDROID_TARGETS:
            archive = self.output / PACKAGES.archive_name(target)
            saved = self.root / archive.name
            archive.rename(saved)
            try:
                with self.subTest(target=target), \
                        self.assertRaisesRegex(ValueError, f"missing=.*{archive.name}"):
                    PACKAGES.verify_directory(arguments)
            finally:
                saved.rename(archive)

    def test_modified_archive_fails_checksum(self):
        archive = self.create("dos16")
        with archive.open("ab") as output:
            output.write(b"modified after upload")
        with self.assertRaisesRegex(ValueError, "SHA256 mismatch"):
            PACKAGES.verify_package(archive, "dos16", COMMIT)

    def test_repackaged_binary_fails_file_manifest_even_with_new_archive_checksum(self):
        archive = self.create("linux-x64")
        files, modes = PACKAGES.archive_contents(archive)
        files["bin/restunts"] = b"substituted executable"
        with tarfile.open(archive, "w:gz") as output:
            for name, data in files.items():
                info = tarfile.TarInfo(name)
                info.size = len(data)
                info.mode = modes[name]
                output.addfile(info, io.BytesIO(data))
        self.checksum(archive)
        with self.assertRaisesRegex(ValueError, "File differs from manifest"):
            PACKAGES.verify_package(archive, "linux-x64", COMMIT)

    def test_wrong_source_run_and_attempt_are_rejected(self):
        archive = self.create("dos16")
        with self.assertRaisesRegex(ValueError, "Wrong source commit"):
            PACKAGES.verify_package(archive, "dos16", OTHER_COMMIT)
        with self.assertRaisesRegex(ValueError, "Wrong workflow run"):
            PACKAGES.verify_package(archive, "dos16", COMMIT, "unrelated-run", RUN_ATTEMPT)
        with self.assertRaisesRegex(ValueError, "Wrong run attempt"):
            PACKAGES.verify_package(archive, "dos16", COMMIT, RUN_ID, "1")

    def test_original_data_unknown_suffixes_and_other_extra_files_are_rejected(self):
        files = {name: self.runtime_data("dos32", name) for name in PACKAGES.required_files("dos32")}
        modes = {name: PACKAGES.FILE_MODE for name in files}
        for extra in ("bin/MISC.RES", "bin/DEFAULT.P3S", "assets.zip", "unrelated.exe"):
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                PACKAGES.validate_contents("dos32", dict(files, **{extra: FILE_DATA}), modes)

    def test_missing_artwork_library_source_and_license_are_rejected(self):
        files = {name: self.runtime_data("windows-arm64", name) for name in PACKAGES.required_files("windows-arm64")}
        modes = {name: PACKAGES.FILE_MODE for name in files}
        for missing in ("bin/opponents/game/opp6.png", "bin/skyboxes/sky4-3.png",
                        "bin/menus/main.png", "bin/menus/showroom.png", "bin/menus/opponent.png",
                        "share/docs/restunts/genghost-state-planner.md",
                        "bin/nuked-opl2.dll", "share/restunts/nuked-opl2-lite/opl2.c",
                        "share/licenses/restunts/Nuked-OPL2-LICENSE",
                        *(f"share/licenses/llvm-runtime/{name}" for name in
                          PACKAGES.LLVM_RUNTIME_NOTICES)):
            incomplete = files.copy()
            incomplete.pop(missing)
            with self.subTest(missing=missing), self.assertRaisesRegex(ValueError, "missing packaged"):
                PACKAGES.validate_contents("windows-arm64", incomplete, modes)

    def test_dos_runtime_filenames_do_not_require_long_filename_support(self):
        files = {name: self.runtime_data("dos32", name) for name in PACKAGES.required_files("dos32")}
        modes = {name: PACKAGES.FILE_MODE for name in files}
        PACKAGES.validate_contents("dos32", files, modes)
        for extra in ("bin/opponents/game/opp1.png", "bin/skyboxes/image.long", "bin/with space/a.png"):
            with self.subTest(extra=extra), self.assertRaisesRegex(ValueError, "8.3 names"):
                PACKAGES.validate_contents("dos32", dict(files, **{extra: FILE_DATA}), modes)

    def test_unsafe_paths_and_links_are_rejected_without_extraction(self):
        for path in ("../outside", "/absolute", "C:/windows.exe", "file:stream",
                     "folder\\file", "a/./b", ".", "bad\nname"):
            with self.subTest(path=path), self.assertRaisesRegex(ValueError, "Unsafe"):
                PACKAGES.checked_path(path)
        archive = self.root / "link.zip"
        with zipfile.ZipFile(archive, "w") as output:
            entry = zipfile.ZipInfo("symlink")
            entry.external_attr = (stat.S_IFLNK | PACKAGES.EXECUTABLE_MODE) << 16
            output.writestr(entry, "outside")
        with self.assertRaisesRegex(ValueError, "Unsupported ZIP member"):
            PACKAGES.archive_contents(archive)

    def test_browser_requires_relink_objects_and_dos_omits_nuked(self):
        files = {name: self.runtime_data("browser", name) for name in PACKAGES.required_files("browser")}
        for missing in ("share/restunts/wasm-relink/objects/main.c.o",
                        "share/restunts/wasm-relink/data/assets/menus/main.png",
                        "share/restunts/wasm-relink/data/assets/menus/showroom.png",
                        "share/restunts/wasm-relink/data/assets/menus/opponent.png"):
            incomplete = files.copy()
            incomplete.pop(missing)
            with self.subTest(missing=missing), self.assertRaisesRegex(ValueError, "missing packaged"):
                PACKAGES.validate_contents("browser", incomplete, {})
        for target in ("dos16", "dos32"):
            self.assertFalse(any("nuked" in name for name in PACKAGES.required_files(target)))

    def test_source_music_is_required_and_survives_archiving(self):
        source = self.root / "source"
        tracks = ("titl", "slct", "over", "vict")
        targets = {"linux-x64": "bin/music", "dos32": "bin/music",
                   "browser": "share/restunts/wasm-relink/data/assets/music"}
        self.copy_car_sources(source)
        with mock.patch.object(PACKAGES, "ROOT", source):
            baseline = {target: PACKAGES.required_files(target) for target in targets}
            for track in tracks:
                path = source / "assets/music" / f"{track}.ogg"
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"OggS archive payload " + track.encode("ascii"))
            for target, directory in targets.items():
                audio = {f"{directory}/{track}.ogg":
                         (source / "assets/music" / f"{track}.ogg").read_bytes() for track in tracks}
                files = {name: self.runtime_data(target, name) for name in baseline[target]} | audio
                modes = {name: PACKAGES.EXECUTABLE_MODE for name in files}
                with self.subTest(target=target):
                    PACKAGES.validate_contents(target, files, modes)
                    for missing in audio:
                        incomplete = files.copy()
                        incomplete.pop(missing)
                        with self.assertRaisesRegex(ValueError, "missing packaged files"):
                            PACKAGES.validate_contents(target, incomplete, modes)
                    for wrong in ("menu.ogg", "SLCT.ogg", "slct.wav"):
                        with self.assertRaisesRegex(ValueError, "Unexpected package file"):
                            PACKAGES.validate_contents(
                                target, files | {f"{directory}/{wrong}": FILE_DATA}, modes)
                    archive = self.create(target, audio)
                    archived, _ = PACKAGES.archive_contents(archive)
                    for name, content in audio.items():
                        self.assertEqual(archived[name], content)
            self.assertFalse(any("/music/" in name for name in PACKAGES.required_files("dos16")))

    def test_absent_music_is_optional(self):
        source = self.root / "empty-source"
        self.copy_car_sources(source)
        with mock.patch.object(PACKAGES, "ROOT", source):
            for target in ("linux-x64", "dos32", "browser"):
                with self.subTest(target=target):
                    self.assertEqual(PACKAGES.optional_music_files(target), set())
                    files = {name: self.runtime_data(target, name) for name in PACKAGES.required_files(target)}
                    modes = {name: PACKAGES.EXECUTABLE_MODE for name in files}
                    PACKAGES.validate_contents(target, files, modes)

    def test_decoder_files_follow_install_paths(self):
        decoder_files = {"THIRD-PARTY-NOTICES.txt", "share/licenses/restunts/stb-LICENSE",
                         "share/licenses/restunts/libvpx-LICENSE",
                         "share/licenses/restunts/libvpx-PATENTS",
                         "share/licenses/restunts/nestegg-LICENSE"}
        for target in ("linux-x64", "dos32", "browser"):
            expected = decoder_files.copy()
            if target == "browser":
                expected.add("share/restunts/wasm-relink/lib/librestunts_webm.a")
            files = {name: self.runtime_data(target, name) for name in PACKAGES.required_files(target)}
            modes = {name: PACKAGES.EXECUTABLE_MODE for name in files}
            with self.subTest(target=target):
                self.assertTrue(expected <= files.keys())
                PACKAGES.validate_contents(target, files, modes)
                for missing in expected:
                    incomplete = files.copy()
                    incomplete.pop(missing)
                    with self.assertRaisesRegex(ValueError, "missing packaged files"):
                        PACKAGES.validate_contents(target, incomplete, modes)


if __name__ == "__main__":
    unittest.main()
