#!/usr/bin/env python3
"""Verify AppRun discovery and guest validation without starting the game or GUI."""

import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tarfile
import tempfile
import unittest


APPRUN = Path(__file__).resolve().parents[1] / "appimage" / "AppRun"
GUEST_TEST = Path(__file__).with_name("test-appimage-guest.sh")
LINKAGE_PROBE_COMPLETE = 73
ELF_CLASS_64 = 2
GUEST_PROBE_TIMEOUT_SECONDS = 30


class AppRunTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="restunts-apprun-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.appdir = self.root / "relocated image with spaces"
        executable_directory = self.appdir / "usr/bin"
        executable_directory.mkdir(parents=True)
        shutil.copyfile(APPRUN, self.appdir / "AppRun")
        (self.appdir / "AppRun").chmod(0o755)
        child = """#!/usr/bin/env python3
import json, os, sys
print(json.dumps({"program": os.path.basename(sys.argv[0]), "argv": sys.argv[1:],
                  "environment": {key: value for key, value in os.environ.items()
                                  if key.startswith(("PULSE_", "ALSA_", "RESTUNTS_ALSA"))
                                  or key in ("LD_LIBRARY_PATH", "XLOCALEDIR")}}))
"""
        for name in ("restunts", "restunts-launcher"):
            path = executable_directory / name
            path.write_text(child)
            path.chmod(0o755)
        helper = executable_directory / "restunts-gtk-modules"
        helper.write_text("#!/bin/sh\nexit 0\n")
        helper.chmod(0o755)
        self.module_directories = (
            self.appdir / "usr/lib/pango/1.6.0/modules",
            self.appdir / "usr/lib/gtk-2.0/2.10.0/loaders",
        )
        for directory in self.module_directories:
            directory.mkdir(parents=True)
            (directory / "libfixture.so").write_text("module fixture")
        self.runtime = self.root / "session runtime"
        (self.runtime / "pulse").mkdir(parents=True)
        self.server = socket.socket(socket.AF_UNIX)
        self.addCleanup(self.server.close)
        self.server.bind(str(self.runtime / "pulse/native"))
        self.environment = {key: value for key, value in os.environ.items()
                            if not key.startswith(("PULSE_", "ALSA_"))}
        self.environment["XDG_RUNTIME_DIR"] = str(self.runtime)
        self.environment["TMPDIR"] = str(self.root)

    def run_app(self, arguments=("--help",), **overrides):
        environment = dict(self.environment, **overrides)
        output = subprocess.check_output([str(self.appdir / "AppRun"), *arguments],
                                         env=environment, text=True)
        return json.loads(output)

    def test_modern_session_socket_is_discovered_for_game(self):
        result = self.run_app(("--data-dir", "folder with spaces", "--audio:on"))
        self.assertEqual(result["program"], "restunts")
        self.assertEqual(result["argv"], ["--data-dir", "folder with spaces", "--audio:on"])
        self.assertEqual(result["environment"]["PULSE_RUNTIME_PATH"],
                         str(self.runtime / "pulse"))
        self.assertNotIn("PULSE_SERVER", result["environment"])

    def test_launcher_inherits_discovery_and_cleans_gtk_cache(self):
        result = self.run_app(())
        self.assertEqual(result["program"], "restunts-launcher")
        self.assertEqual(result["argv"], ["--wait-for-game"])
        self.assertEqual(result["environment"]["PULSE_RUNTIME_PATH"],
                         str(self.runtime / "pulse"))
        self.assertEqual(list(self.root.glob("restunts-gtk.*")), [])

    def test_game_library_path_preserves_the_callers_search_path(self):
        caller_path = "/caller libraries:/another caller directory"
        result = self.run_app(LD_LIBRARY_PATH=caller_path)
        self.assertEqual(result["environment"]["LD_LIBRARY_PATH"],
                         str(self.appdir / "usr/lib") + os.pathsep + caller_path)

    def test_host_xlib_uses_its_default_locale_directory(self):
        self.environment.pop("XLOCALEDIR", None)
        self.assertNotIn("XLOCALEDIR", self.run_app()["environment"])

    def test_explicit_xlib_locale_directory_is_preserved(self):
        locale_directory = "/caller X11 locale modules"
        result = self.run_app(XLOCALEDIR=locale_directory)
        self.assertEqual(result["environment"]["XLOCALEDIR"], locale_directory)

    def test_builtin_gtk_modules_create_empty_isolated_caches(self):
        for directory in self.module_directories:
            shutil.rmtree(directory)
        helper = self.appdir / "usr/bin/restunts-gtk-modules"
        helper.write_text("#!/bin/sh\necho 'No external modules should be queried' >&2\nexit 1\n")
        launcher = self.appdir / "usr/bin/restunts-launcher"
        launcher.write_text('''#!/usr/bin/env python3
import json, os
from pathlib import Path
configuration = Path(os.environ["PANGO_RC_FILE"]).read_text().splitlines()
pango = next(line.partition("=")[2] for line in configuration if line.startswith("ModuleFiles="))
print(json.dumps({"pango": Path(pango).read_text(),
                  "pixbuf": Path(os.environ["GDK_PIXBUF_MODULE_FILE"]).read_text()}))
''')
        self.assertEqual(self.run_app(()), {"pango": "", "pixbuf": ""})
        self.assertEqual(list(self.root.glob("restunts-gtk.*")), [])

    def test_external_gtk_module_paths_are_queried_together_without_splitting(self):
        extra = self.module_directories[0] / "libmodule with spaces.so"
        extra.write_text("second module fixture")
        log = self.root / "module-arguments.log"
        helper = self.appdir / "usr/bin/restunts-gtk-modules"
        helper.write_text('''#!/usr/bin/env python3
import json, os, sys
with open(os.environ["RESTUNTS_TEST_MODULE_LOG"], "a") as output:
    print(json.dumps(sys.argv[1:]), file=output)
''')
        self.run_app((), RESTUNTS_TEST_MODULE_LOG=str(log))
        actual = [json.loads(line) for line in log.read_text().splitlines()]
        expected = [[mode, *map(str, sorted(directory.glob("*.so")))]
                    for mode, directory in zip(("pango", "pixbuf"), self.module_directories)]
        self.assertEqual(actual, expected)

    def test_external_gtk_module_query_failures_stop_launcher_and_clean_caches(self):
        helper = self.appdir / "usr/bin/restunts-gtk-modules"
        helper.write_text("#!/bin/sh\nexit 1\n")
        with self.assertRaises(subprocess.CalledProcessError):
            self.run_app(())
        self.assertEqual(list(self.root.glob("restunts-gtk.*")), [])

    def test_gui_library_path_does_not_reach_host_tools_or_cleanup(self):
        caller_path = "/caller libraries"
        expected_path = str(self.appdir / "usr/lib") + os.pathsep + caller_path
        host_tools = self.root / "host-tools"
        host_tools.mkdir()
        host_log, module_log = self.root / "host-tools.log", self.root / "modules.log"
        for name in ("dirname", "mktemp", "rm"):
            command = shutil.which(name)
            self.assertIsNotNone(command)
            wrapper = host_tools / name
            wrapper.write_text(f'''#!/bin/sh
if [ "${{LD_LIBRARY_PATH-}}" != "$RESTUNTS_TEST_CALLER_LIBRARY_PATH" ]; then
    echo '{name}: bundled libraries reached a host utility' >&2
    exit 127
fi
printf '{name}\\n' >> "$RESTUNTS_TEST_HOST_LOG"
exec "{command}" "$@"
''')
            wrapper.chmod(0o755)
        helper = self.appdir / "usr/bin/restunts-gtk-modules"
        helper.write_text('''#!/usr/bin/env python3
import os
with open(os.environ["RESTUNTS_TEST_MODULE_LOG"], "a") as output:
    print(os.environ.get("LD_LIBRARY_PATH"), file=output)
''')
        result = self.run_app(
            (), LD_LIBRARY_PATH=caller_path,
            PATH=str(host_tools) + os.pathsep + self.environment["PATH"],
            RESTUNTS_TEST_CALLER_LIBRARY_PATH=caller_path,
            RESTUNTS_TEST_HOST_LOG=str(host_log), RESTUNTS_TEST_MODULE_LOG=str(module_log))
        self.assertEqual(result["environment"]["LD_LIBRARY_PATH"], expected_path)
        self.assertEqual(module_log.read_text().splitlines(), [expected_path, expected_path])
        self.assertTrue({"dirname", "mktemp", "rm"}.issubset(host_log.read_text().splitlines()))
        self.assertEqual(list(self.root.glob("restunts-gtk.*")), [])

    def test_explicit_server_is_preserved(self):
        result = self.run_app(PULSE_SERVER="unix:/chosen/server")
        self.assertEqual(result["environment"]["PULSE_SERVER"], "unix:/chosen/server")
        self.assertNotIn("PULSE_RUNTIME_PATH", result["environment"])

    def test_explicit_runtime_directory_is_preserved(self):
        result = self.run_app(PULSE_RUNTIME_PATH="/chosen/runtime")
        self.assertEqual(result["environment"]["PULSE_RUNTIME_PATH"], "/chosen/runtime")

    def test_explicit_empty_server_is_preserved(self):
        result = self.run_app(PULSE_SERVER="")
        self.assertEqual(result["environment"]["PULSE_SERVER"], "")
        self.assertNotIn("PULSE_RUNTIME_PATH", result["environment"])

    def test_client_configuration_is_preserved_without_server_override(self):
        result = self.run_app(PULSE_CLIENTCONFIG="/chosen/client.conf")
        self.assertEqual(result["environment"]["PULSE_CLIENTCONFIG"], "/chosen/client.conf")
        self.assertNotIn("PULSE_SERVER", result["environment"])

    def test_missing_session_socket_keeps_legacy_discovery(self):
        result = self.run_app(XDG_RUNTIME_DIR=str(self.root / "missing"))
        self.assertNotIn("PULSE_RUNTIME_PATH", result["environment"])

    def test_regular_file_is_not_treated_as_server_socket(self):
        other_runtime = self.root / "other runtime"
        (other_runtime / "pulse").mkdir(parents=True)
        (other_runtime / "pulse/native").write_text("not a socket")
        result = self.run_app(XDG_RUNTIME_DIR=str(other_runtime))
        self.assertNotIn("PULSE_RUNTIME_PATH", result["environment"])

    def test_alsa_configuration_override_is_preserved(self):
        result = self.run_app(ALSA_CONFIG_PATH="/chosen/alsa.conf")
        self.assertEqual(result["environment"]["ALSA_CONFIG_PATH"], "/chosen/alsa.conf")


class GuestLibraryPathTests(unittest.TestCase):
    def test_guest_keeps_host_tools_outside_the_bundled_library_path(self):
        os_release = Path("/etc/os-release")
        if not os_release.is_file():
            self.skipTest("The guest script verifies a Linux userspace")
        release = {}
        for line in os_release.read_text().splitlines():
            key, separator, value = line.partition("=")
            if separator:
                release[key] = value.strip('"')
        if release.get("ID") not in ("ubuntu", "fedora"):
            self.skipTest("The guest script verifies an Ubuntu or Fedora userspace")
        with tempfile.TemporaryDirectory(prefix="restunts-guest-environment-") as temporary:
            root = Path(temporary)
            host_tools, appdir, data = root / "host-tools", root / "AppDir", root / "data"
            for directory in (host_tools, appdir / "usr/bin", appdir / "usr/lib", data):
                directory.mkdir(parents=True)
            def executable(path, source):
                path.write_text("#!/bin/sh\nset -eu\n" + source)
                path.chmod(0o755)
            # These stand in for host utilities that link a newer libselinux.
            # Reject pollution deterministically without requiring that distro.
            for name in ("mktemp", "rm", "setsid", "sleep", "od", "tr"):
                command = shutil.which(name)
                self.assertIsNotNone(command)
                action = (f"printf '{ELF_CLASS_64}\\n'" if name == "od"
                          else f'exec "{command}" "$@"')
                executable(host_tools / name, f'''
if [ "${{LD_LIBRARY_PATH+x}}" = x ]; then
    echo '{name}: bundled libraries reached a host utility' >&2
    exit 127
fi
printf '{name}\\n' >> "$RESTUNTS_TEST_HOST_LOG"
{action}
''')
            executable(host_tools / "ldd", '''
case "${LD_LIBRARY_PATH-}" in */squashfs-root/usr/lib) ;; *) exit 127 ;; esac
printf 'ldd\\n' >> "$RESTUNTS_TEST_HOST_LOG"
printf 'All target dependencies resolved\\n'
''')
            shutil.copyfile(APPRUN, appdir / "AppRun")
            (appdir / "AppRun").chmod(0o755)
            executable(appdir / "usr/bin/restunts", '''
case "$1" in
    --help) echo 'Usage: restunts' ;;
    --version) echo 'Version fixture' ;;
    --licenses) echo 'Nuked OPL2 Lite' ;;
esac
''')
            for name in ("repldump", "pixldump"):
                executable(appdir / "usr/bin" / name, "exit 0\n")
            executable(appdir / "usr/bin/genghost", '''
case "${LD_LIBRARY_PATH-}" in */squashfs-root/usr/lib) ;; *) exit 127 ;; esac
printf 'genghost\\n' >> "$RESTUNTS_TEST_HOST_LOG"
exit "$RESTUNTS_TEST_PROBE_COMPLETE"
''')
            (appdir / "usr/lib/libfixture.so.0").write_text("target library fixture")
            image = root / "restunts.AppImage"
            executable(image, '''
if [ "$1" = --appimage-extract ]; then
    cp -R "$RESTUNTS_TEST_APPDIR" squashfs-root
    exit 0
fi
exec "$RESTUNTS_TEST_APPDIR/AppRun" "$@"
''')
            archive = root / "release.tar.gz"
            with tarfile.open(archive, "w:gz") as output:
                output.add(image, arcname="bin/restunts.AppImage")
            log = root / "host-tools.log"
            environment = dict(os.environ, PATH=str(host_tools) + os.pathsep + os.environ["PATH"],
                               RESTUNTS_TEST_APPDIR=str(appdir), RESTUNTS_TEST_HOST_LOG=str(log),
                               RESTUNTS_TEST_PROBE_COMPLETE=str(LINKAGE_PROBE_COMPLETE))
            # The target stub ends the script after all linkage checks. Its
            # distinctive status proves the watchdog and EXIT cleanup ran too.
            result = subprocess.run(
                ["bash", str(GUEST_TEST), "linux-appimage-x64", str(archive), str(data),
                 release["ID"] + release["VERSION_ID"]],
                env=environment, capture_output=True, text=True, timeout=GUEST_PROBE_TIMEOUT_SECONDS)
            self.assertEqual(result.returncode, LINKAGE_PROBE_COMPLETE, result.stderr)
            self.assertNotIn("bundled libraries reached a host utility", result.stderr)
            self.assertTrue({"mktemp", "rm", "setsid", "od", "tr", "ldd", "genghost"}
                            .issubset(log.read_text().splitlines()))


if __name__ == "__main__":
    unittest.main()
