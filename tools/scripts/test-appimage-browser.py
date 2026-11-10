#!/usr/bin/env python3
"""Exercise the AppImage Track Editor button and a host-linked browser probe.

Run on modern Linux with Xvfb, xwininfo, libX11, libXtst, xdg-utils, a C compiler,
and host GLib 2.32+ installed. No browser, game data, or FUSE mount is required.
ImageMagick and Tesseract optionally capture and check the portal-denial status.
Real xdg-mime/xdg-settings associations make an HTML editor the file handler
and a separate browser the HTTP/HTTPS handler, with BROWSER unset. The browser
fixture records its environment and runs a host-linked g_bytes_unref probe
that fails against the old bundled GLib. The editor must be a byte-exact copy
in Downloads that survives launcher exit and deletion of the AppImage temporary
tree. A Flatpak fixture tests explicit document grants and denied access.
"""

import argparse
import ctypes
import json
import os
from pathlib import Path
import re
import select
import shlex
import shutil
import signal
import subprocess
import tempfile
import time
from urllib.parse import unquote, urlparse


STARTUP_SECONDS = 45
BROWSER_SECONDS = 15
PROCESS_EXIT_SECONDS = 5
POLL_SECONDS = 0.1
HOST_VARIABLES = (
    "LD_LIBRARY_PATH", "PANGO_RC_FILE", "GDK_PIXBUF_MODULE_FILE", "FONTCONFIG_PATH",
    "FONTCONFIG_FILE", "LIBTHAI_DICTDIR", "GIO_EXTRA_MODULES", "RESTUNTS_ALSA_CONFIG_DIR",
    "ALSA_CONFIG_PATH", "PULSE_RUNTIME_PATH",
)
LAUNCHER_TITLE = "Chocolate Stunts"
PROBE_SOURCE = """#include "legacy.h"
extern void g_bytes_unref(void *);
legacy_int main(void)
{
    void (*volatile probe)(void *) = g_bytes_unref;
    return probe == 0;
}
"""


def stop(process):
    if process is None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        process.wait(timeout=PROCESS_EXIT_SECONDS)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait(timeout=PROCESS_EXIT_SECONDS)


def press_editor(display_name, window):
    x11 = ctypes.CDLL("libX11.so.6")
    xtest = ctypes.CDLL("libXtst.so.6")
    x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
    x11.XOpenDisplay.restype = ctypes.c_void_p
    x11.XSetInputFocus.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
    x11.XStringToKeysym.argtypes = [ctypes.c_char_p]
    x11.XStringToKeysym.restype = ctypes.c_ulong
    x11.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
    x11.XKeysymToKeycode.restype = ctypes.c_ubyte
    x11.XFlush.argtypes = [ctypes.c_void_p]
    x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
    xtest.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int,
                                      ctypes.c_ulong]
    display = x11.XOpenDisplay(display_name.encode())
    if not display:
        raise ValueError("Cannot connect to the test display")
    try:
        x11.XSetInputFocus(display, window, 2, 0)  # RevertToParent, CurrentTime.
        alt = x11.XKeysymToKeycode(display, x11.XStringToKeysym(b"Alt_L"))
        letter = x11.XKeysymToKeycode(display, x11.XStringToKeysym(b"t"))
        for key, pressed in ((alt, 1), (letter, 1), (letter, 0), (alt, 0)):
            xtest.XTestFakeKeyEvent(display, key, pressed, 0)
        x11.XFlush(display)
    finally:
        x11.XCloseDisplay(display)


def write_opener(root, probe, html_editor, nested):
    fixture = root / "bin" / "browser-fixture"
    record = root / "browser"
    record.mkdir()
    script = ["#!/bin/sh"]
    for name in HOST_VARIABLES:
        script.append(f'if [ "${{{name}+x}}" = x ]; then '
                      f'printf %s "1${{{name}}}"; else printf %s 0; fi > '
                      + shlex.quote(str(record / name)))
    script.extend([
        'printf %s "$1" > ' + shlex.quote(str(record / "argument")),
        shlex.quote(str(probe)) + " > " + shlex.quote(str(record / "probe.log")) + " 2>&1",
        'printf %s "$?" > ' + shlex.quote(str(record / "exit")),
    ])
    fixture.write_text("\n".join(script) + "\n")
    fixture.chmod(0o755)
    editor = root / "bin" / "html-editor-fixture"
    editor.write_text('#!/bin/sh\nprintf %s "$1" > ' +
                      shlex.quote(str(root / "wrong-handler")) + "\n")
    editor.chmod(0o755)
    browser_id = "vendor-browser.desktop" if nested else "browser-fixture.desktop"
    browser_entry = root / ("data/applications/vendor/browser.desktop" if nested else
                            "data/applications/browser-fixture.desktop")
    browser_entry.parent.mkdir(exist_ok=True)
    browser_entry.write_text(
        "[Desktop Entry]\nType=Application\nName=Host browser fixture\n"
        "Exec=browser-fixture %u\nMimeType=x-scheme-handler/http;x-scheme-handler/https;\n"
        "Terminal=false\n")
    (root / "data/applications/editor-fixture.desktop").write_text(
        "[Desktop Entry]\nType=Application\nName=HTML editor fixture\n"
        "Exec=html-editor-fixture %u\nMimeType=text/html;application/xhtml+xml;x-scheme-handler/file;\n"
        "Terminal=false\n")
    html_default = "editor-fixture.desktop" if html_editor else browser_id
    defaults = (f"[Default Applications]\ntext/html={html_default};\n"
                f"application/xhtml+xml={html_default};\n"
                f"x-scheme-handler/file={html_default};\n"
                f"x-scheme-handler/http={browser_id};\n"
                f"x-scheme-handler/https={browser_id};\n")
    (root / "data/applications/defaults.list").write_text(defaults)
    (root / "config/mimeapps.list").write_text(defaults)
    return record, browser_id


def write_portal_fixture(root, deny):
    record = root / "portal-record"
    record.mkdir()
    exported = root / "portal/doc/fixture/tracked.html"
    exported.parent.mkdir(parents=True)
    script = ["#!/bin/sh", 'test "$#" = 3 || exit 2',
              'test "$1" = document-export || exit 3',
              'test "$2" = --app=org.mozilla.firefox || exit 4']
    for name in HOST_VARIABLES:
        script.append(f'if [ "${{{name}+x}}" = x ]; then '
                      f'printf %s "1${{{name}}}"; else printf %s 0; fi > '
                      + shlex.quote(str(record / name)))
    script.extend(['printf %s "$2" > ' + shlex.quote(str(record / "application")),
                   'printf %s "$3" > ' + shlex.quote(str(record / "source"))])
    if deny:
        script.extend(['printf %s denied > ' + shlex.quote(str(record / "denied")),
                       'printf "%s\\n" "portal request denied by fixture" >&2', 'exit 1'])
    else:
        script.extend(['cp -- "$3" ' + shlex.quote(str(exported)),
                       'printf "%s\\n" ' + shlex.quote(str(exported))])
    executable = root / "bin/flatpak"
    executable.write_text("\n".join(script) + "\n")
    executable.chmod(0o755)
    entry = root / "data/applications/browser-fixture.desktop"
    with entry.open("a") as output:
        output.write("X-Flatpak=org.mozilla.firefox\n")
    return record


def without_gio_path(root, original):
    directory = root / "host-path-without-gio"
    directory.mkdir()
    for source in original.split(os.pathsep):
        location = Path(source)
        if not location.is_dir():
            continue
        for executable in location.iterdir():
            target = directory / executable.name
            if (executable.name != "gio" and not target.exists() and executable.is_file()
                    and os.access(executable, os.X_OK)):
                target.symlink_to(executable.resolve())
    if shutil.which("gio", path=str(directory)):
        raise ValueError("Legacy fixture unexpectedly exposes gio")
    return str(directory)


def run_case(image, root, display, base_environment, probe, caller, *, html_editor=True,
             no_gio=False, portal=None):
    for name in ("bin", "cache", "config", "data/applications", "Downloads", "runtime", "tmp"):
        (root / name).mkdir(parents=True)
    (root / "runtime").chmod(0o700)
    downloads = str(root / "Downloads").replace("\\", "\\\\").replace('"', '\\"')
    (root / "config/user-dirs.dirs").write_text(f'XDG_DOWNLOAD_DIR="{downloads}"\n')
    record, browser_id = write_opener(root, probe, html_editor, nested=no_gio)
    portal_record = write_portal_fixture(root, portal == "deny") if portal else None
    host_path = without_gio_path(root, base_environment["PATH"]) if no_gio else base_environment["PATH"]
    environment = dict(base_environment, DISPLAY=display, APPIMAGE_EXTRACT_AND_RUN="1",
                       XDG_CACHE_HOME=str(root / "cache"),
                       XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
                       XDG_RUNTIME_DIR=str(root / "runtime"), TMPDIR=str(root / "tmp"),
                       XDG_CURRENT_DESKTOP="X-Generic", PATH=str(root / "bin") + os.pathsep +
                       host_path)
    for name in (*HOST_VARIABLES, "BROWSER"):
        environment.pop(name, None)
    original = {name: None for name in HOST_VARIABLES}
    if caller is not None:
        original["LD_LIBRARY_PATH"] = caller
    if caller:
        original["PANGO_RC_FILE"] = str(root / "caller.pangorc")
        original["GDK_PIXBUF_MODULE_FILE"] = str(root / "caller-pixbuf.loaders")
        original["FONTCONFIG_PATH"] = str(root / "caller-fonts")
        original["FONTCONFIG_FILE"] = str(root / "caller-fonts.conf")
        original["LIBTHAI_DICTDIR"] = str(root / "caller-thai")
        original["GIO_EXTRA_MODULES"] = str(root / "caller-gio")
    environment.update({name: value for name, value in original.items() if value is not None})
    associations = {}
    html_default = "editor-fixture.desktop" if html_editor else browser_id
    for mime, expected in (("text/html", html_default),
                           ("x-scheme-handler/http", browser_id),
                           ("x-scheme-handler/https", browser_id)):
        actual = subprocess.check_output(["xdg-mime", "query", "default", mime],
                                         env=environment, text=True).strip()
        if actual != expected:
            raise ValueError(f"Fixture association {mime} is {actual!r}; expected {expected!r}")
        associations[mime] = actual
    browser_default = subprocess.check_output(["xdg-settings", "get", "default-web-browser"],
                                             env=environment, text=True).strip()
    if browser_default != browser_id:
        raise ValueError("Fixture default web browser is " + browser_default)
    associations["default-web-browser"] = browser_default
    (root / "associations.json").write_text(json.dumps(associations, indent=2) + "\n")
    process = None
    try:
        with (root / "launcher.log").open("w") as log:
            process = subprocess.Popen([str(image)], cwd=root, env=environment, stdout=log,
                                       stderr=subprocess.STDOUT, start_new_session=True)
            deadline = time.monotonic() + STARTUP_SECONDS
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise ValueError("Launcher exited: " + (root / "launcher.log").read_text())
                tree = subprocess.check_output(["xwininfo", "-root", "-tree"], env=environment,
                                               text=True)
                match = re.search(r'(0x[0-9a-fA-F]+) "' + re.escape(LAUNCHER_TITLE) + '"', tree)
                if match:
                    details = subprocess.check_output(
                        ["xwininfo", "-id", match[1]], env=environment, text=True)
                    if "Map State: IsViewable" in details:
                        break
                time.sleep(POLL_SECONDS)
            else:
                raise ValueError("Launcher window was not mapped")
            press_editor(display, int(match[1], 16))
            deadline = time.monotonic() + BROWSER_SECONDS
            while time.monotonic() < deadline:
                if portal == "deny" and (portal_record / "denied").exists():
                    time.sleep(POLL_SECONDS)
                    if (record / "argument").exists():
                        raise ValueError("Browser was invoked after portal access was denied")
                    if process.poll() is not None:
                        raise ValueError("Launcher exited after portal denial")
                    screenshot = root / "portal-denied.png"
                    if shutil.which("import"):
                        subprocess.run(["import", "-window", match[1], str(screenshot)],
                                       env=environment, check=True)
                        if shutil.which("tesseract"):
                            text = subprocess.check_output(
                                ["tesseract", str(screenshot), "stdout"], env=environment,
                                stderr=subprocess.DEVNULL, text=True)
                            (root / "portal-denied-status.txt").write_text(text)
                            if "portal" not in text.lower() or "cannot" not in text.lower():
                                raise ValueError("Launcher did not show its portal error: " + text)
                    print(f"PASS {root.name}: denied portal export reports an error and "
                          "never invokes the browser", flush=True)
                    return
                if (root / "wrong-handler").exists():
                    raise ValueError("Track Editor opened the HTML file editor instead of the "
                                     "default web browser: " +
                                     (root / "wrong-handler").read_text())
                if (record / "exit").exists():
                    break
                time.sleep(POLL_SECONDS)
            else:
                raise ValueError("Track Editor did not invoke the desktop opener: " +
                                 (root / "launcher.log").read_text())
            for name in HOST_VARIABLES:
                value = original[name]
                expected = "1" + value if value is not None else "0"
                actual = (record / name).read_text()
                if actual != expected:
                    raise ValueError(f"Host browser inherited {name}={actual!r}; expected "
                                     f"{expected!r}. " + (record / "probe.log").read_text())
            if (record / "exit").read_text() != "0":
                raise ValueError("Host GLib browser probe failed: " +
                                 (record / "probe.log").read_text())
            argument = (record / "argument").read_text()
            parsed = urlparse(argument)
            if parsed.scheme and parsed.scheme != "file":
                raise ValueError("Editor opener received a non-file URI: " + argument)
            editor = Path(unquote(parsed.path) if parsed.scheme else argument)
            if editor.name != "tracked.html" or not editor.is_file():
                raise ValueError("Editor file cannot be read: " + argument)
            payload = editor.read_bytes()
            if b"<html" not in payload.lower():
                raise ValueError("Editor file is not HTML")
            temporary = (root / "tmp").resolve()
            copies = list(temporary.rglob("tracked.html"))
            if not any(source.read_bytes() == payload for source in copies):
                raise ValueError("Persistent editor differs from the packaged HTML payload")
            downloads = (root / "Downloads").resolve()
            persistent = editor
            if portal_record:
                if (portal_record / "application").read_text() != "--app=org.mozilla.firefox":
                    raise ValueError("Portal export did not grant the selected Flatpak browser")
                persistent = Path((portal_record / "source").read_text())
                if persistent.read_bytes() != payload:
                    raise ValueError("Portal HTML differs from the persistent editor")
                if (root / "portal").resolve() not in editor.resolve().parents:
                    raise ValueError("Flatpak browser did not receive the portal-granted file")
                for name in HOST_VARIABLES:
                    value = original[name]
                    expected = "1" + value if value is not None else "0"
                    if (portal_record / name).read_text() != expected:
                        raise ValueError("Portal command inherited bundled environment: " + name)
            if downloads not in persistent.resolve().parents:
                raise ValueError("Editor is not in the browser-visible Downloads directory: " +
                                 argument)
            if temporary == editor.resolve() or temporary in editor.resolve().parents:
                raise ValueError("Editor still points into the private AppImage temporary tree: " +
                                 argument)
            if any(part.startswith((".mount_", "appimage_extracted_"))
                   for part in editor.resolve().parts):
                raise ValueError("Editor still points into the AppImage mount: " + argument)
            if process.poll() is not None:
                raise ValueError("Launcher exited after opening the editor")
            if shutil.which("import"):
                subprocess.run(["import", "-window", match[1], str(root / "editor-opened.png")],
                               env=environment, check=True)
            stop(process)
            process = None
            # Extract-and-run can leave an unpacked tree after SIGTERM. Remove
            # our private tree explicitly to model the completed FUSE unmount.
            shutil.rmtree(temporary, ignore_errors=True)
            if any(source.exists() for source in copies):
                raise ValueError("The test AppImage source was not removed after launcher exit")
            if (not editor.is_file() or editor.read_bytes() != payload or
                    not persistent.is_file() or persistent.read_bytes() != payload):
                raise ValueError("Editor vanished or changed when the launcher exited: " + argument)
            (record / "persistent-editor").write_text(str(persistent))
            print(f"PASS {root.name}: real Track Editor button, clean host environment, "
                  "persistent editor after AppImage exit, web browser association, and host GLib probe",
                  flush=True)
    finally:
        stop(process)


def check_image(args, output):
    source = output / "host-browser-probe.c"
    source.write_text(PROBE_SOURCE)
    probe = output / "host-browser-probe"
    legacy = Path(__file__).resolve().parents[2] / "src/restunts/c"
    subprocess.run([*shlex.split(args.cc), "-I", str(legacy), str(source), "-o", str(probe),
                    "-Wl,--no-as-needed", "-l:libglib-2.0.so.0"], check=True)
    subprocess.run([str(probe)], check=True)
    environment = dict(os.environ)
    for name in ("LD_LIBRARY_PATH", "LD_PRELOAD", "DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY"):
        environment.pop(name, None)
    server = None
    try:
        with (output / "xvfb.log").open("w") as errors:
            server = subprocess.Popen(
                ["Xvfb", "-displayfd", "1", "-screen", "0", "1280x1024x24", "-nolisten", "tcp",
                 "+extension", "GLX", "-noreset"], env=environment, stdout=subprocess.PIPE,
                stderr=errors, text=True, start_new_session=True)
            if not select.select([server.stdout], [], [], STARTUP_SECONDS)[0]:
                raise ValueError("Xvfb startup timed out")
            number = server.stdout.readline().strip()
            if not number.isdecimal():
                raise ValueError("Xvfb startup failed: " + (output / "xvfb.log").read_text())
            run_case(args.appimage, output / "browser-file-default", ":" + number,
                     environment, probe, None, html_editor=False)
            for label, caller in (("unset", None), ("empty", ""),
                                  ("custom", str(output / "host libraries"))):
                run_case(args.appimage, output / label, ":" + number, environment, probe, caller)
            run_case(args.appimage, output / "legacy-no-gio", ":" + number, environment, probe,
                     None, no_gio=True)
            run_case(args.appimage, output / "flatpak", ":" + number, environment, probe,
                     None, portal="grant")
            run_case(args.appimage, output / "flatpak-denied", ":" + number, environment, probe,
                     None, portal="deny")
    finally:
        stop(server)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("appimage", type=lambda value: Path(value).resolve(strict=True))
    parser.add_argument("--output-directory", type=Path,
                        help="Keep logs and optional screenshots in a new output directory")
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"), help="Host C compiler")
    args = parser.parse_args()
    try:
        if args.output_directory:
            output = args.output_directory.resolve()
            output.mkdir(parents=True, exist_ok=False)
            check_image(args, output)
        else:
            with tempfile.TemporaryDirectory(prefix="restunts-appimage-browser-") as temporary:
                check_image(args, Path(temporary))
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
