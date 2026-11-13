#!/usr/bin/env python3
"""Regression checks for verified downloads and isolated SDK extraction."""

import hashlib
import http.client
import http.server
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import threading
import unittest
from unittest import mock


spec = importlib.util.spec_from_file_location(
    "appimage_sysroot", Path(__file__).with_name("appimage-sysroot.py"))
sdk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sdk)


class SysrootTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def archive(self, filename, entries):
        path = self.root / filename
        with tarfile.open(path, "w:gz") as output:
            for name, content, target in entries:
                member = tarfile.TarInfo(name)
                if target is not None:
                    member.type = tarfile.SYMTYPE
                    member.linkname = target
                    output.addfile(member)
                else:
                    member.size = len(content)
                    output.addfile(member, io.BytesIO(content))
        return path

    def test_download_is_verified_before_installation(self):
        source = self.root / "source"
        source.write_bytes(b"verified compiler input")
        destination = self.root / "cache" / "archive"
        destination.parent.mkdir()
        destination.write_bytes(b"previous cached download")
        with mock.patch.object(sdk.time, "sleep"), \
                mock.patch.object(sdk.urllib.request, "urlopen", wraps=sdk.urllib.request.urlopen) as opened:
            with self.assertRaisesRegex(RuntimeError, "SHA256 mismatch") as failure:
                sdk.download(source.as_uri(), "0" * 64, destination)
        self.assertEqual(opened.call_count, sdk.DOWNLOAD_ATTEMPTS)
        self.assertIn("expected " + "0" * 64, str(failure.exception))
        self.assertIn("got " + sdk.sha256(source), str(failure.exception))
        self.assertIn(f"({source.stat().st_size} bytes)", str(failure.exception))
        self.assertEqual(destination.read_bytes(), b"previous cached download")
        self.assertEqual(list(destination.parent.glob("*.partial.*")), [])
        sdk.download(source.as_uri(), hashlib.sha256(source.read_bytes()).hexdigest(), destination)
        self.assertEqual(destination.read_bytes(), source.read_bytes())
        with mock.patch.object(sdk.urllib.request, "urlopen") as opened:
            sdk.download(source.as_uri(), sdk.sha256(source), destination)
        opened.assert_not_called()

    def test_incomplete_and_corrupt_http_downloads_are_retried(self):
        content = b"verified compiler input"
        responses = [content[:5], b"x" * len(content), content]
        requests = []
        destination = self.root / "cache" / "archive"
        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                requests.append(self.path)
                self.send_response(200)
                self.send_header("Content-Length", str(len(content)))
                self.end_headers()
                self.wfile.write(responses[len(requests) - 1])

            def log_message(self, *args):
                pass

        with http.server.HTTPServer(("127.0.0.1", 0), Handler) as server:
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                url = f"http://127.0.0.1:{server.server_port}/archive"
                with mock.patch.object(sdk.time, "sleep"):
                    sdk.download(url, hashlib.sha256(content).hexdigest(), destination)
            finally:
                server.shutdown()
                thread.join()
        self.assertEqual(requests, ["/archive"] * len(responses))
        self.assertEqual(destination.read_bytes(), content)
        self.assertEqual(list(destination.parent.glob("*.partial.*")), [])

    def test_http_read_failure_is_retried(self):
        content = b"verified compiler input"
        class IncompleteResponse(io.BytesIO):
            def read(self, size=-1):
                raise http.client.IncompleteRead(b"partial response")

        destination = self.root / "cache" / "archive"
        responses = [IncompleteResponse(), io.BytesIO(content)]
        with mock.patch.object(sdk.time, "sleep"), \
                mock.patch.object(sdk.urllib.request, "urlopen", side_effect=responses) as opened:
            sdk.download("https://example.org/archive", hashlib.sha256(content).hexdigest(), destination)
        self.assertEqual(opened.call_count, len(responses))
        self.assertEqual(destination.read_bytes(), content)
        self.assertEqual(list(destination.parent.glob("*.partial.*")), [])

    def test_absolute_sdk_symlink_stays_inside_sysroot(self):
        archive = self.archive("root.tar.gz", [
            (".", b"", None),
            ("lib/libexample.so.1", b"legacy library", None),
            ("usr/lib/libexample.so", b"", "/lib/libexample.so.1"),
            ("usr/bin/obsolete-compiler", b"must not be extracted", None),
        ])
        destination = self.root / "sysroot"
        sdk.extract(archive, destination)
        link = destination / "usr/lib/libexample.so"
        self.assertTrue(link.is_symlink())
        self.assertEqual(link.read_bytes(), b"legacy library")
        self.assertTrue(link.resolve().is_relative_to(destination))
        self.assertFalse((destination / "usr/bin").exists())

    def test_parent_traversal_is_rejected(self):
        archive = self.archive("traversal.tar.gz", [
            ("usr/include/../../../escape", b"unsafe", None),
        ])
        with self.assertRaisesRegex(RuntimeError, "Unsafe archive member"):
            sdk.extract(archive, self.root / "sysroot")
        self.assertFalse((self.root / "escape").exists())

    def test_selected_target_loader_keeps_its_executable_permission(self):
        archive = self.root / "loader.tar.gz"
        content = b"target ELF loader"
        with tarfile.open(archive, "w:gz") as output:
            member = tarfile.TarInfo("lib/ld-linux-armhf.so.3")
            member.size, member.mode = len(content), 0o755
            output.addfile(member, io.BytesIO(content))
        sdk.extract(archive, self.root / "sysroot", sdk=True)
        loader = self.root / "sysroot/lib/ld-linux-armhf.so.3"
        self.assertEqual(loader.stat().st_mode & 0o777, 0o755)

    def test_relative_symlink_escape_is_rejected(self):
        archive = self.archive("symlink.tar.gz", [
            ("usr/include/escape", b"", "../../../../escape"),
        ])
        with self.assertRaisesRegex(RuntimeError, "symlink escapes SDK"):
            sdk.extract(archive, self.root / "sysroot")

    def test_layer_whiteout_removes_an_old_library(self):
        destination = self.root / "sysroot"
        base = self.archive("base.tar.gz", [("lib/old.so", b"old", None)])
        layer = self.archive("layer.tar.gz", [("lib/.wh.old.so", b"", None)])
        sdk.extract(base, destination)
        sdk.extract(layer, destination)
        self.assertFalse((destination / "lib/old.so").exists())
        self.assertFalse((destination / "lib/.wh.old.so").exists())

    def test_cxx_inputs_are_kept_without_obsolete_compilers(self):
        prefix = "opt/rh/devtoolset-2/root/usr"
        header = prefix + "/include/c++/4.8.2/vector"
        archive_name = prefix + "/lib/gcc/x86_64-redhat-linux/4.8.2/libstdc++.a"
        startup = prefix + "/lib/gcc/x86_64-redhat-linux/4.8.2/crtbeginS.o"
        compiler = prefix + "/bin/g++"
        archive = self.archive("cxx.tar.gz", [
            (header, b"C++11 declarations", None),
            (archive_name, b"old ABI archive", None),
            (startup, b"target startup object", None),
            (compiler, b"must not execute", None),
            ("usr/lib/python/site-packages/unrelated.so", b"unrelated", None),
        ])
        destination = self.root / "sysroot"
        sdk.extract(archive, destination)
        self.assertEqual((destination / header).read_bytes(), b"C++11 declarations")
        self.assertEqual((destination / archive_name).read_bytes(), b"old ABI archive")
        self.assertEqual((destination / startup).read_bytes(), b"target startup object")
        self.assertFalse((destination / compiler).exists())
        self.assertFalse((destination / "usr/lib/python").exists())

    def test_cxx_exception_header_patch_is_idempotent(self):
        header = self.root / sdk.LEGACY_CXX_PREFIX / "include/c++/4.8.2/new"
        header.parent.mkdir(parents=True)
        header.write_text("namespace std {\n  struct nothrow_t { };\n}\n")
        unique_ptr = header.parent / "bits/unique_ptr.h"
        unique_ptr.parent.mkdir()
        unique_ptr.write_text("// No colliding private helper in this header.\n")
        sdk.patch_cxx_headers(self.root)
        patched = header.read_text()
        self.assertIn("class bad_array_new_length : public bad_alloc", patched)
        sdk.patch_cxx_headers(self.root)
        self.assertEqual(header.read_text(), patched)

    def test_unique_ptr_builtin_collision_patch_covers_legacy_sdk_layouts(self):
        for prefix in (sdk.LEGACY_CXX_PREFIX + "/include/c++/4.8.2",
                       "usr/include/c++/4.7", "usr/include/c++/4.9"):
            with self.subTest(prefix=prefix):
                header = self.root / prefix / "new"
                header.parent.mkdir(parents=True)
                header.write_text("class bad_array_new_length : public bad_alloc {};\n")
                unique_ptr = header.parent / "bits/unique_ptr.h"
                unique_ptr.parent.mkdir()
                unique_ptr.write_text("""template<typename _Tp> struct default_delete {
    template<typename _Up> using __remove_cv = typename remove_cv<_Up>::type;
    typedef is_same<__remove_cv<_Tp>, __remove_cv<const _Tp>> same_type;
};
// A different helper must keep its original spelling.
struct __remove_cv_other;
""")
                sdk.patch_cxx_headers(self.root, prefix + "/new")
                patched = unique_ptr.read_text()
                self.assertIn("using __restunts_remove_cv =", patched)
                self.assertIn("is_same<__restunts_remove_cv<_Tp>, "
                              "__restunts_remove_cv<const _Tp>>", patched)
                self.assertIn("struct __remove_cv_other;", patched)
                self.assertEqual(header.read_text(),
                                 "class bad_array_new_length : public bad_alloc {};\n")
                sdk.patch_cxx_headers(self.root, prefix + "/new")
                self.assertEqual(unique_ptr.read_text(), patched)

    def test_cxx_patch_discovers_all_legacy_headers_and_ignores_modern_headers(self):
        prefixes = (sdk.LEGACY_CXX_PREFIX + "/include/c++/4.8.2",
                    "usr/include/c++/4.7", "usr/include/c++/4.9")
        modern = "usr/include/c++/14"
        historical = self.root / "usr/include/c++/4.1.1/new"
        historical.parent.mkdir(parents=True)
        historical.write_text("namespace std {\n  struct nothrow_t { };\n}\n")
        for prefix in (*prefixes, modern):
            directory = self.root / prefix
            (directory / "bits").mkdir(parents=True)
            (directory / "new").write_text("namespace std {\n  struct nothrow_t { };\n}\n")
            (directory / "bits/unique_ptr.h").write_text(
                "template<class T> using __remove_cv = typename remove_cv<T>::type;\n")
        sdk.patch_cxx_headers(self.root)
        for prefix in prefixes:
            self.assertIn("class bad_array_new_length", (self.root / prefix / "new").read_text())
            self.assertIn("using __restunts_remove_cv =",
                          (self.root / prefix / "bits/unique_ptr.h").read_text())
        self.assertNotIn("bad_array_new_length", (self.root / modern / "new").read_text())
        self.assertIn("using __remove_cv =",
                      (self.root / modern / "bits/unique_ptr.h").read_text())
        self.assertEqual(historical.read_text(),
                         "namespace std {\n  struct nothrow_t { };\n}\n")
        before = {path: path.read_bytes() for path in self.root.rglob("*") if path.is_file()}
        subprocess.run([sys.executable, str(Path(sdk.__file__)), "patch-cxx", str(self.root)],
                       check=True)
        self.assertEqual(before, {path: path.read_bytes() for path in before})

    def test_cxx_patch_rejects_sdk_without_legacy_headers(self):
        modern = self.root / "usr/include/c++/14/new"
        modern.parent.mkdir(parents=True)
        modern.write_text("modern compiler headers must stay unchanged\n")
        with self.assertRaisesRegex(RuntimeError, "No pinned GCC 4.x C\\+\\+ headers found"):
            sdk.patch_cxx_headers(self.root)
        self.assertEqual(modern.read_text(), "modern compiler headers must stay unchanged\n")

    def cxx_overlay_inputs(self):
        sysroot = self.root / "read only SDK"
        roots = [sysroot / "usr/include/c++" / version for version in ("4.8.2", "4.9")]
        for root in roots:
            (root / "bits").mkdir(parents=True)
            (root / "new").write_text("namespace std {\n  struct nothrow_t { };\n}\n")
            (root / "bits/unique_ptr.h").write_text(
                "template<class T> using __remove_cv = typename remove_cv<T>::type;\n")
        toolchain = self.root / "original-toolchain.cmake"
        toolchain.write_text("set(CMAKE_C_FLAGS_INIT -DOVERLAY_C_ONLY=1)\n"
                             "set(CMAKE_CXX_FLAGS_INIT [=[-isystem\"" + str(roots[0]) +
                             "\" -DOVERLAY_BASE=1]=])\n")
        return sysroot, toolchain, roots

    def test_cxx_overlay_preserves_read_only_sdk_and_caches_unchanged_copies(self):
        sysroot, toolchain, roots = self.cxx_overlay_inputs()
        # A copied symlink must not let patching write into the original SDK.
        shared = sysroot / "shared-unique-ptr.h"
        unique_ptr = roots[0] / "bits/unique_ptr.h"
        unique_ptr.replace(shared)
        unique_ptr.symlink_to(shared)
        paths = [sysroot, *sysroot.rglob("*"), toolchain]
        try:
            for path in paths:
                path.chmod(0o555 if path.is_dir() else 0o444)
            before = {path: (path.read_bytes(), path.stat().st_mtime_ns, path.stat().st_mode)
                      for path in paths if path.is_file()}
            output = self.root / "writable header overlay"
            script = sdk.cxx_overlay(sysroot, toolchain, output)
            copied = output / "include/root-0"
            self.assertTrue(script.is_file())
            self.assertIn("class bad_array_new_length", (copied / "new").read_text())
            self.assertIn("using __restunts_remove_cv =",
                          (copied / "bits/unique_ptr.h").read_text())
            self.assertFalse((copied / "bits/unique_ptr.h").is_symlink())
            self.assertEqual(list((output / "include").iterdir()), [copied])
            self.assertEqual(before, {
                path: (path.read_bytes(), path.stat().st_mtime_ns, path.stat().st_mode)
                for path in before})
            cached = {path: (path.read_bytes(), path.stat().st_mtime_ns)
                      for path in output.rglob("*") if path.is_file()}
            self.assertEqual(sdk.cxx_overlay(sysroot, toolchain, output), script)
            self.assertEqual(cached, {path: (path.read_bytes(), path.stat().st_mtime_ns)
                                      for path in cached})
        finally:
            for path in paths:
                path.chmod(0o755 if path.is_dir() else 0o644)

    def test_cxx_overlay_initializes_and_updates_cached_cmake_cxx_flags_only(self):
        cmake = shutil.which("cmake")
        if cmake is None:
            self.skipTest("CMake is needed to exercise fresh and cached compiler flags")
        sysroot, toolchain, _ = self.cxx_overlay_inputs()
        script = sdk.cxx_overlay(sysroot, toolchain, self.root / "header overlay")
        source = self.root / "project"
        source.mkdir()
        (source / "CMakeLists.txt").write_text("""cmake_minimum_required(VERSION 3.25)
project(OverlayProbe LANGUAGES C)
enable_language(CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_library(c_probe OBJECT probe.c)
add_executable(cxx_probe probe.cpp)
""")
        (source / "probe.c").write_text("#ifdef OVERLAY_BASE\n#error CXX flags affected C\n#endif\n"
                                       "int c_probe(void) { return 0; }\n")
        (source / "probe.cpp").write_text("#ifndef OVERLAY_BASE\n#error Missing original SDK flags\n"
                                         "#endif\nint main() { return 0; }\n")
        def run(command):
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

        for cached in (False, True):
            with self.subTest(cached=cached):
                build = self.root / ("cached-build" if cached else "fresh-build")
                configure = [cmake, "-S", str(source), "-B", str(build),
                             "-DCMAKE_TOOLCHAIN_FILE=" + str(toolchain)]
                if cached:
                    run(configure)
                configure.append("-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=" + str(script))
                run(configure)
                run(configure)
                run([cmake, "--build", str(build)])
                commands = json.loads((build / "compile_commands.json").read_text())
                for entry in commands:
                    command = entry["command"]
                    overlay = str(script.parent / "include/root-0")
                    if entry["file"].endswith(".cpp"):
                        self.assertEqual(command.count(overlay), 1)
                        self.assertLess(command.index(overlay), command.index("read only SDK"))
                        self.assertIn("-DOVERLAY_BASE=1", command)
                    else:
                        self.assertNotIn(overlay, command)
                        self.assertIn("-DOVERLAY_C_ONLY=1", command)

    def test_x86_multilib_keeps_pinned_startup_libc_and_compiler_runtime(self):
        compiler = shutil.which("gcc")
        if compiler is None:
            self.skipTest("GCC is needed to exercise multilib library selection")
        probe = subprocess.run([compiler, "-m32", "-print-multi-directory"],
                               capture_output=True, text=True)
        if probe.returncode or probe.stdout.strip() != sdk.GCC_X86_MULTILIB_DIRECTORY:
            self.skipTest("GCC's x86 -m32 multilib layout is needed")
        compiler_directory = self.root / sdk.LEGACY_X86_GCC_DIRECTORY
        development_directory = self.root / "usr/lib"
        host_directory = self.root / "host"
        host_multilib = host_directory / sdk.GCC_X86_MULTILIB_DIRECTORY
        for directory in (compiler_directory, development_directory, host_multilib):
            directory.mkdir(parents=True)
        pinned = {"crt1.o": development_directory, "libc.so": development_directory,
                  "libgcc.a": compiler_directory}
        for name, directory in pinned.items():
            (directory / name).write_bytes(b"pinned target input")
            (host_multilib / name).write_bytes(b"incompatible host input")
        command = [compiler, "-m32", f"-B{compiler_directory}/",
                   f"-B{development_directory}/", f"-B{host_directory}/"]
        def selected(name):
            return Path(subprocess.check_output(
                command + ["-print-file-name=" + name], text=True).strip()).resolve()
        # Reproduce the host multilib taking precedence over existing -B inputs.
        for name in pinned:
            self.assertEqual(selected(name), host_multilib / name)
        sdk.expose_x86_multilib(self.root, "lib")
        sdk.expose_x86_multilib(self.root, "lib")
        for name, directory in pinned.items():
            self.assertEqual(selected(name), directory / name)

    def test_multilib_alias_does_not_replace_existing_sdk_data(self):
        directory = self.root / sdk.LEGACY_X86_GCC_DIRECTORY
        existing = directory / sdk.GCC_X86_MULTILIB_DIRECTORY
        existing.mkdir(parents=True)
        with self.assertRaisesRegex(RuntimeError, "Unexpected SDK multilib directory"):
            sdk.expose_x86_multilib(self.root, "lib")
        self.assertTrue(existing.is_dir())

    def test_debian_sdk_closure_keeps_baseline_libraries_without_old_programs(self):
        packages = {
            "sdk-root": {"Depends": "libc6, missing | libchosen:any, python:any, gcc-base"},
            "libc6": {"Pre-Depends": "libgcc1"}, "libgcc1": {}, "libchosen": {},
            "python": {}, "gcc-base": {},
        }
        self.assertEqual(sdk.debian_sdk_packages(packages, ("sdk-root", "gcc-base")),
                         ["gcc-base", "libc6", "libchosen", "libgcc1", "sdk-root"])
        with self.assertRaisesRegex(RuntimeError, "Missing pinned SDK package"):
            sdk.debian_sdk_packages(packages, ("missing-root",))

    def test_compiler_header_isolation_keeps_pinned_headers_ahead_of_host_headers(self):
        compiler = shutil.which("gcc")
        if compiler is None:
            self.skipTest("GCC is needed to exercise compiler header search ordering")
        sysroot, host = self.root / "sysroot", self.root / "host-cross-libc"
        pinned = sysroot / "usr/include"
        for directory in (host, pinned):
            directory.mkdir(parents=True)
        (host / "baseline.h").write_text("#define RESTUNTS_HEADER_ORIGIN host\n")
        (pinned / "baseline.h").write_text("#define RESTUNTS_HEADER_ORIGIN pinned\n")
        environment = dict(os.environ, C_INCLUDE_PATH=str(host))
        command = [compiler, "--sysroot=" + str(sysroot), "-E", "-dM", "-x", "c",
                   "-include", "baseline.h", "-"]
        before = subprocess.check_output(command, input="", env=environment, text=True)
        self.assertIn("#define RESTUNTS_HEADER_ORIGIN host", before)
        includes = sdk.compiler_include_arguments(compiler, sysroot, "target-triple")
        after = subprocess.check_output(command + includes, input="", env=environment, text=True)
        self.assertIn("#define RESTUNTS_HEADER_ORIGIN pinned", after)
        self.assertIn("-nostdinc", includes)

    def test_debian_sdk_extracts_target_crt_and_archives_without_compiler_programs(self):
        triple = "arm-linux-gnueabihf"
        gcc = "usr/lib/gcc/" + triple + "/4.7/"
        startup = "usr/lib/" + triple + "/crt1.o"
        header = "usr/include/c++/4.7/" + triple + "/bits/c++config.h"
        archive = self.archive("arm-sdk.tar.gz", [
            (startup, b"target startup", None), (gcc + "libgcc.a", b"target runtime", None),
            (header, b"target C++ declarations", None),
            (gcc + "liblto_plugin.so", b"obsolete compiler plugin", None),
            (gcc + "cc1", b"obsolete compiler", None),
            ("usr/bin/old-gcc", b"obsolete driver", None),
        ])
        destination = self.root / "sysroot"
        sdk.extract(archive, destination, sdk=True)
        self.assertEqual((destination / startup).read_bytes(), b"target startup")
        self.assertEqual((destination / (gcc + "libgcc.a")).read_bytes(), b"target runtime")
        self.assertTrue((destination / header).is_file())
        self.assertFalse((destination / (gcc + "liblto_plugin.so")).exists())
        self.assertFalse((destination / (gcc + "cc1")).exists())
        self.assertFalse((destination / "usr/bin").exists())

    def test_debian_runtime_flattens_multiarch_libraries_and_retains_gtk_modules(self):
        triple = "aarch64-linux-gnu"
        sysroot, destination = self.root / "sysroot", self.root / "deps"
        libraries = sysroot / "usr/lib" / triple
        module = "gdk-pixbuf-2.0/2.10.0/loaders/libpixbufloader-png.so"
        for name in ("libgtk-x11-2.0.so.0", "pulseaudio/libpulsecommon-5.0.so", module):
            path = libraries / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"\x7fELFtarget runtime fixture")
        (libraries / "libc.so").write_text("GROUP ( /lib/libc.so.6 )")
        (sysroot / "packages.txt").write_text("pinned package manifest\n")
        sdk.copy_debian_runtime(sysroot, destination, triple)
        self.assertTrue((destination / "lib/libgtk-x11-2.0.so.0").is_file())
        self.assertTrue((destination / "lib/libpulsecommon-5.0.so").is_file())
        self.assertTrue((destination / "lib" / module).is_file())
        alias = destination / "lib/gtk-2.0/2.10.0/loaders"
        self.assertTrue(alias.is_symlink())
        self.assertTrue(alias.resolve().is_relative_to(destination))
        self.assertTrue((alias / "libpixbufloader-png.so").is_file())
        self.assertFalse((destination / "lib/libc.so").exists())
        self.assertEqual((destination / "share/sources/appimage-sdk-packages.txt").read_text(),
                         "pinned package manifest\n")

    def test_runtime_pruning_keeps_transitive_and_module_dependencies(self):
        libraries = self.root / "lib"
        module = "gtk-2.0/immodules/im-test.so"
        printer = "gtk-2.0/printbackends/cups.so"
        graph = {"libroot.so.1": "libdependency.so.1\nlibc.so.6\n",
                 "libdependency.so.1": "libz.so.1\n", "libz.so.1": "",
                 module: "libroot.so.1\n", printer: "libmissing-printer.so.1\n",
                 "libunused.so.1": ""}
        for name in graph:
            path = libraries / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"library fixture")
        def dependencies(command, **kwargs):
            return graph[str(Path(command[-1]).relative_to(libraries))]
        with mock.patch.object(sdk.subprocess, "check_output", side_effect=dependencies):
            sdk.prune_runtime(libraries, roots=("libroot.so.1",))
        remaining = {str(path.relative_to(libraries)) for path in libraries.rglob("*")
                     if path.is_file()}
        self.assertEqual(remaining, {"libroot.so.1", "libdependency.so.1", "libz.so.1", module})

    def test_runtime_pruning_retains_native_pulse_client_and_its_private_library(self):
        libraries = self.root / "lib"
        libraries.mkdir()
        graph = {name: "" for name in sdk.GTK_RUNTIME_ROOTS}
        graph["libpulse.so.0"] = "libpulsecommon-0.9.21.so\nlibc.so.6\n"
        graph["libpulsecommon-0.9.21.so"] = "libwrap.so.0\nlibdbus-1.so.3\n"
        graph["libwrap.so.0"] = "libc.so.6\n"
        graph["libdbus-1.so.3"] = "libpthread.so.0\n"
        graph["libunused-audio.so.1"] = ""
        for name in graph:
            (libraries / name).write_bytes(b"audio library fixture")
        def dependencies(command, **kwargs):
            return graph[Path(command[-1]).name]
        with mock.patch.object(sdk.subprocess, "check_output", side_effect=dependencies):
            sdk.prune_runtime(libraries)
        self.assertTrue((libraries / "libpulse.so.0").is_file())
        self.assertTrue((libraries / "libpulsecommon-0.9.21.so").is_file())
        self.assertTrue((libraries / "libwrap.so.0").is_file())
        self.assertFalse((libraries / "libunused-audio.so.1").exists())
        self.assertFalse((libraries / "libnsl.so.1").exists())
        graph["libwrap.so.0"] = "libnsl.so.1\nlibc.so.6\n"
        with mock.patch.object(sdk.subprocess, "check_output", side_effect=dependencies):
            with self.assertRaisesRegex(RuntimeError, "Missing bundled runtime dependency: libnsl"):
                sdk.prune_runtime(libraries)

    def test_runtime_pruning_uses_host_graphics_without_changing_the_link_sdk(self):
        triple = "aarch64-linux-gnu"
        sysroot, destination = self.root / "sysroot", self.root / "deps"
        source = sysroot / "usr/lib" / triple
        source.mkdir(parents=True)
        graph = {
            "libgtk-x11-2.0.so.0": "libX11.so.6\nlibXrender.so.1\nlibc.so.6\n",
            "libXrender.so.1": "libX11.so.6\n",
            "libpulse.so.0": "libpulsecommon-5.0.so\n",
            "libpulsecommon-5.0.so": "libX11-xcb.so.1\nlibX11.so.6\nlibxcb.so.1\n",
        }
        graphics = ("libX11.so.6.2.1", "libX11-xcb.so.1.0.0", "libxcb.so.1.1.0")
        for name in (*graph, *graphics):
            (source / name).write_bytes(b"\x7fELFtarget runtime fixture")
        for name, target in (("libX11.so.6", "libX11.so.6.2.1"),
                             ("libX11.so", "libX11.so.6"),
                             ("libX11-xcb.so.1", "libX11-xcb.so.1.0.0"),
                             ("libX11-xcb.so", "libX11-xcb.so.1"),
                             ("libxcb.so.1", "libxcb.so.1.1.0"),
                             ("libxcb.so", "libxcb.so.1")):
            (source / name).symlink_to(target)
        original = {path.name: (path.read_bytes(), path.readlink() if path.is_symlink() else None)
                    for path in source.iterdir()}
        (sysroot / "packages.txt").write_text("pinned graphics development packages\n")
        sdk.copy_debian_runtime(sysroot, destination, triple)
        libraries = destination / "lib"
        def dependencies(command, **kwargs):
            return graph[Path(command[-1]).name]
        with mock.patch.object(sdk.subprocess, "check_output", side_effect=dependencies):
            sdk.prune_runtime(libraries, roots=("libgtk-x11-2.0.so.0", "libpulse.so.0"))
        self.assertEqual({path.name for path in libraries.iterdir()}, set(graph))
        self.assertEqual({path.name: (path.read_bytes(), path.readlink() if path.is_symlink() else None)
                          for path in source.iterdir()}, original)

    def test_runtime_missing_dependency_fails_before_pruning(self):
        libraries = self.root / "lib"
        libraries.mkdir()
        root = libraries / "libroot.so.1"
        unused = libraries / "libunused.so.1"
        root.write_bytes(b"root")
        unused.write_bytes(b"unused")
        with mock.patch.object(sdk.subprocess, "check_output", return_value="libmissing.so.1\n"):
            with self.assertRaisesRegex(RuntimeError, "Missing bundled runtime dependency"):
                sdk.prune_runtime(libraries, roots=(root.name,))
        self.assertTrue(unused.exists())

    def test_gtk_nested_headers_and_modules_exclude_old_tools(self):
        header = "usr/lib/glib-2.0/include/glibconfig.h"
        module = "usr/lib/pango/1.6.0/modules/pango-basic-fc.so"
        archive = self.archive("gtk.tar.gz", [
            (header, b"target configuration", None),
            (module, b"matching renderer", None),
            ("usr/bin/pango-querymodules", b"obsolete tool", None),
            ("usr/lib/gtk-2.0/old-helper", b"obsolete helper", None),
        ])
        destination = self.root / "gtk"
        sdk.extract(archive, destination, gtk=True)
        self.assertEqual((destination / header).read_bytes(), b"target configuration")
        self.assertEqual((destination / module).read_bytes(), b"matching renderer")
        self.assertFalse((destination / "usr/bin").exists())
        self.assertFalse((destination / "usr/lib/gtk-2.0/old-helper").exists())


if __name__ == "__main__":
    unittest.main()
