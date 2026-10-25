#!/usr/bin/env python3
"""Keep AppImage dependencies closed without bundling the host C library."""

import importlib.util
from pathlib import Path
import unittest
from unittest import mock


spec = importlib.util.spec_from_file_location(
    "appimage_abi", Path(__file__).with_name("check-appimage-abi.py"))
abi = importlib.util.module_from_spec(spec)
spec.loader.exec_module(abi)


def dependencies(*names):
    return "\n".join(f"  (NEEDED) Shared library: [{name}]" for name in names)


def elf_description(target, *, version=None, interpreter=None, flags=None):
    target_abi = abi.TARGETS[target]
    description = f"  Class: {target_abi.elf_class}\n  Machine: {target_abi.machine}\n"
    if flags is not None:
        description += f"  Flags: {flags}\n"
    elif target_abi.machine == "ARM":
        description += "  Flags: 0x5000400, Version5 EABI, hard-float ABI\n"
    if interpreter is not None:
        description += f"  [Requesting program interpreter: {interpreter}]\n"
    if version is not None:
        description += f"  Name: GLIBC_{version}\n"
    return description


class ElfAbiTests(unittest.TestCase):
    def test_each_target_accepts_its_distribution_glibc_and_interpreter(self):
        for target, target_abi in abi.TARGETS.items():
            with self.subTest(target=target):
                version = ".".join(map(str, target_abi.glibc_version))
                description = elf_description(target, version=version, interpreter=target_abi.interpreter)
                with mock.patch.object(abi, "inspect", return_value=description):
                    abi.check_elf(Path("restunts"), target)

    def test_each_target_rejects_glibc_newer_than_its_distribution(self):
        for target, target_abi in abi.TARGETS.items():
            with self.subTest(target=target):
                major, minor = target_abi.glibc_version
                description = elf_description(target, version=f"{major}.{minor + 1}")
                with mock.patch.object(abi, "inspect", return_value=description):
                    with self.assertRaisesRegex(ValueError, f"exceeds the {target_abi.distribution} ABI"):
                        abi.check_elf(Path("restunts"), target)

    def test_arm_targets_reject_the_other_arm_machine(self):
        for target, other in (("linux-appimage-armv7", "linux-appimage-arm64"),
                              ("linux-appimage-arm64", "linux-appimage-armv7")):
            with self.subTest(target=target):
                with mock.patch.object(abi, "inspect", return_value=elf_description(other)):
                    with self.assertRaisesRegex(ValueError, "expected Class"):
                        abi.check_elf(Path("restunts"), target)

    def test_arm_targets_reject_an_incompatible_interpreter(self):
        for target in ("linux-appimage-armv7", "linux-appimage-arm64"):
            with self.subTest(target=target):
                description = elf_description(target, interpreter="/lib/ld-linux.so.3")
                with mock.patch.object(abi, "inspect", return_value=description):
                    with self.assertRaisesRegex(ValueError, "unexpected interpreter"):
                        abi.check_elf(Path("restunts"), target)

    def test_armv7_requires_eabi5_and_hard_float(self):
        target = "linux-appimage-armv7"
        for flags in ("", "0x5000200, Version5 EABI, soft-float ABI",
                      "0x4000400, Version4 EABI, hard-float ABI"):
            with self.subTest(flags=flags):
                description = elf_description(target, flags=flags)
                with mock.patch.object(abi, "inspect", return_value=description):
                    with self.assertRaisesRegex(ValueError, "expected ARM EABI5 hard-float ABI"):
                        abi.check_elf(Path("restunts"), target)

    def test_armv7_accepts_old_binutils_hard_float_attributes(self):
        target = "linux-appimage-armv7"
        description = (elf_description(target, flags="0x5000002, Version5 EABI, <unknown>")
                       + "  Tag_ABI_VFP_args: VFP registers\n")
        with mock.patch.object(abi, "inspect", return_value=description):
            abi.check_elf(Path("libsound.so.1"), target)

    def test_armv7_rejects_conflicting_soft_float_flags_and_vfp_attributes(self):
        target = "linux-appimage-armv7"
        description = (elf_description(target, flags="0x5000200, Version5 EABI, soft-float ABI")
                       + "  Tag_ABI_VFP_args: VFP registers\n")
        with mock.patch.object(abi, "inspect", return_value=description):
            with self.assertRaisesRegex(ValueError, "expected ARM EABI5 hard-float ABI"):
                abi.check_elf(Path("libsound.so.1"), target)

    def test_arm_runtimes_must_have_no_interpreter_or_shared_libraries(self):
        for target in ("linux-appimage-armv7", "linux-appimage-arm64"):
            target_abi = abi.TARGETS[target]
            variants = (elf_description(target, interpreter=target_abi.interpreter),
                        elf_description(target) + dependencies("libc.so.6"))
            for description in variants:
                with self.subTest(target=target, description=description):
                    with mock.patch.object(abi, "inspect", return_value=description):
                        with self.assertRaisesRegex(ValueError, "AppImage runtime must be static"):
                            abi.check_elf(Path("runtime"), target, static=True)

    def test_arm_runtimes_accept_static_pie_without_glibc(self):
        for target in ("linux-appimage-armv7", "linux-appimage-arm64"):
            with self.subTest(target=target):
                description = elf_description(target) + "  Type: DYN (Position-Independent Executable file)\n"
                with mock.patch.object(abi, "inspect", return_value=description):
                    abi.check_elf(Path("runtime"), target, static=True)


class DependencyClosureTests(unittest.TestCase):
    def test_dynamic_audio_roots_are_required_without_direct_dependencies(self):
        root = Path("AppDir")
        for omitted in abi.AUDIO_RUNTIME_LIBRARIES:
            with self.subTest(library=omitted):
                descriptions = {root / "usr/lib" / name: ""
                                for name in abi.AUDIO_RUNTIME_LIBRARIES if name != omitted}
                with self.assertRaisesRegex(ValueError, omitted + ": missing ELF audio runtime"):
                    abi.check_audio_runtime(root, descriptions)

    def test_dynamic_audio_roots_exist_in_the_loader_directory(self):
        root = Path("AppDir")
        descriptions = {root / "usr/lib" / name: "" for name in abi.AUDIO_RUNTIME_LIBRARIES}
        abi.check_audio_runtime(root, descriptions)

    def test_bundled_transitive_dependencies_are_accepted(self):
        abi.check_dependency_closure({
            Path("usr/bin/restunts"): dependencies("libsound.so.1", "libc.so.6"),
            Path("usr/lib/libsound.so.1"): dependencies("libcodec.so.2"),
            Path("usr/lib/libcodec.so.2"): dependencies("libm.so.6"),
        })

    def test_missing_direct_dependency_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "restunts: missing bundled dependency libsound"):
            abi.check_dependency_closure({
                Path("usr/bin/restunts"): dependencies("libsound.so.1"),
            })

    def test_optional_host_dependency_is_not_treated_as_glibc(self):
        for filename in ("libwrap.so.0", "libasyncns.so.0"):
            with self.subTest(library=filename):
                with self.assertRaisesRegex(ValueError, "missing bundled dependency libnsl.so.1"):
                    abi.check_dependency_closure({
                        Path("usr/lib/libpulse.so.0"): dependencies(filename),
                        Path("usr/lib") / filename: dependencies("libnsl.so.1", "libc.so.6"),
                    })

    def test_system_c_library_and_loader_stay_external(self):
        abi.check_dependency_closure({
            Path("usr/bin/restunts"): dependencies(
                "libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0", "librt.so.1",
                "libresolv.so.2", "ld-linux-x86-64.so.2", "ld-linux-armhf.so.3",
                "ld-linux-aarch64.so.1"),
        })

    def test_library_soname_does_not_replace_its_missing_loader_filename(self):
        with self.assertRaisesRegex(ValueError, "missing bundled dependency libsound.so.1"):
            abi.check_dependency_closure({
                Path("usr/bin/restunts"): dependencies("libsound.so.1"),
                Path("usr/lib/libsound.so.1.2"): "  (SONAME) Library soname: [libsound.so.1]",
            })


if __name__ == "__main__":
    unittest.main()
