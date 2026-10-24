#!/usr/bin/env python3
"""Keep AppImage dependencies closed without bundling the host C library."""

import importlib.util
from pathlib import Path
import unittest


spec = importlib.util.spec_from_file_location(
    "appimage_abi", Path(__file__).with_name("check-appimage-abi.py"))
abi = importlib.util.module_from_spec(spec)
spec.loader.exec_module(abi)


def dependencies(*names):
    return "\n".join(f"  (NEEDED) Shared library: [{name}]" for name in names)


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
                "libresolv.so.2", "ld-linux-x86-64.so.2"),
        })

    def test_library_soname_does_not_replace_its_missing_loader_filename(self):
        with self.assertRaisesRegex(ValueError, "missing bundled dependency libsound.so.1"):
            abi.check_dependency_closure({
                Path("usr/bin/restunts"): dependencies("libsound.so.1"),
                Path("usr/lib/libsound.so.1.2"): "  (SONAME) Library soname: [libsound.so.1]",
            })


if __name__ == "__main__":
    unittest.main()
