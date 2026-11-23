#!/usr/bin/env python3
"""Smoke-test the threaded browser ZIP's launcher on Windows."""

import argparse
import os
from pathlib import Path
import runpy
import subprocess
import tempfile
import time
from urllib.error import HTTPError, URLError
from urllib.request import ProxyHandler, build_opener
import zipfile


LAUNCHER = "Start-LocalDemo.bat"
DEMO_FILE = "restunts.html"
DEMO_URL = f"http://127.0.0.1:8000/{DEMO_FILE}"
STARTUP_TIMEOUT = 30
REQUEST_TIMEOUT = 5
STOP_TIMEOUT = 10
EXPECTED_HEADERS = {
    "Cross-Origin-Opener-Policy": "same-origin",
    "Cross-Origin-Embedder-Policy": "require-corp",
}


def smoke_check(process, log, expected_html):
    opener = build_opener(ProxyHandler({}))
    deadline = time.monotonic() + STARTUP_TIMEOUT
    while time.monotonic() < deadline:
        exit_code = process.poll()
        if exit_code is not None:
            raise ValueError(f"The demo launcher exited before serving the game ({exit_code})")
        if f"Open {DEMO_URL} in your browser." in log.read_text(encoding="utf-8", errors="replace"):
            break
        time.sleep(0.1)
    else:
        raise ValueError(f"The demo launcher did not start listening within {STARTUP_TIMEOUT} seconds")

    try:
        with opener.open(DEMO_URL, timeout=REQUEST_TIMEOUT) as response:
            if response.url != DEMO_URL or response.status != 200:
                raise ValueError(f"Unexpected response for {DEMO_URL}: {response.status} {response.url}")
            for header, expected in EXPECTED_HEADERS.items():
                if response.headers.get(header) != expected:
                    raise ValueError(f"Expected {header}: {expected}")
            if response.read() != expected_html:
                raise ValueError(f"{DEMO_URL} returned different HTML from the package")
    except (HTTPError, URLError, TimeoutError) as error:
        raise ValueError(f"Could not read {DEMO_URL}: {error}") from error


def test_launcher(archive, commit):
    packages = runpy.run_path(str(Path(__file__).with_name("release-packages.py")))
    packages["verify_package"](archive, "browser-threaded", commit)

    system = Path(os.environ["SystemRoot"]) / "System32"
    with tempfile.TemporaryDirectory(prefix="restunts browser ") as temporary:
        directory = Path(temporary)
        package = directory / "Chocolate Stunts Browser"
        with zipfile.ZipFile(archive) as source:
            source.extractall(package)

        # Keep development Python off PATH and disallow machine-wide launchers
        # from providing a runtime if CMD can still find them.
        environment = dict(os.environ, PATH=str(directory / "empty path"),
                           PYLAUNCHER_DRYRUN="1", PYTHON_MANAGER_AUTOMATIC_INSTALL="0")
        log = directory / "launcher.log"
        expected_html = (package / DEMO_FILE).read_bytes()
        try:
            with log.open("wb") as output:
                process = subprocess.Popen(
                    [str(system / "cmd.exe"), "/d", "/c", f".\\{LAUNCHER}"],
                    cwd=package, env=environment, stdin=subprocess.DEVNULL,
                    stdout=output, stderr=subprocess.STDOUT,
                )
                try:
                    smoke_check(process, log, expected_html)
                finally:
                    if process.poll() is None:
                        try:
                            subprocess.run(
                                [str(system / "taskkill.exe"), "/T", "/F", "/PID", str(process.pid)],
                                capture_output=True, timeout=STOP_TIMEOUT, check=True,
                            )
                        finally:
                            if process.poll() is None:
                                process.kill()
                            process.wait(timeout=STOP_TIMEOUT)
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            output = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
            raise ValueError(f"{error}\nLauncher output:\n{output.rstrip()}") from error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path, help="threaded browser ZIP with its adjacent SHA-256 file")
    parser.add_argument("commit", help="expected source commit")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("this smoke test requires Windows")
    try:
        test_launcher(args.archive, args.commit)
    except (OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile) as error:
        parser.exit(1, f"Threaded browser launcher check failed: {error}\n")
    print(f"Verified {LAUNCHER} served the archived game from bundled Python at {DEMO_URL}")


if __name__ == "__main__":
    main()
