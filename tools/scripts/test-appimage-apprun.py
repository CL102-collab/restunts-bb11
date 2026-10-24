#!/usr/bin/env python3
"""Verify relocated AppRun audio discovery without starting the game or GUI."""

import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import unittest


APPRUN = Path(__file__).resolve().parents[1] / "appimage" / "AppRun"


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
                                  if key.startswith(("PULSE_", "ALSA_", "RESTUNTS_ALSA"))}}))
"""
        for name in ("restunts", "restunts-launcher"):
            path = executable_directory / name
            path.write_text(child)
            path.chmod(0o755)
        helper = executable_directory / "restunts-gtk-modules"
        helper.write_text("#!/bin/sh\nexit 0\n")
        helper.chmod(0o755)
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


if __name__ == "__main__":
    unittest.main()
