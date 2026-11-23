import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import subprocess
import sys
import webbrowser


LOOPBACK_HOST = "127.0.0.1"
DEFAULT_PORT = 8000
WINDOWS_BROWSERS = (
    ("chrome.exe", "Google/Chrome/Application/chrome.exe"),
    ("chromium.exe", "Chromium/Application/chrome.exe"),
    ("msedge.exe", "Microsoft/Edge/Application/msedge.exe"),
)


class DemoRequestHandler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()


class DemoServer(ThreadingHTTPServer):
    # On Windows SO_REUSEADDR can let two listeners bind to the same port.
    allow_reuse_address = os.name != "nt"


def windows_browser_paths(executable, install_path):
    import winreg

    key = rf"Software\Microsoft\Windows\CurrentVersion\App Paths\{executable}"
    for hive in (winreg.HKEY_CURRENT_USER, winreg.HKEY_LOCAL_MACHINE):
        for view in (winreg.KEY_WOW64_64KEY, winreg.KEY_WOW64_32KEY):
            try:
                with winreg.OpenKey(hive, key, 0, winreg.KEY_READ | view) as entry:
                    value, value_type = winreg.QueryValueEx(entry, None)
                if isinstance(value, str) and value:
                    if value_type == winreg.REG_EXPAND_SZ:
                        value = winreg.ExpandEnvironmentStrings(value)
                    yield Path(value.strip().strip('"'))
            except OSError:
                continue

    # ProgramW6432 exposes the native Program Files directory to x86 processes.
    for variable in ("LOCALAPPDATA", "ProgramW6432", "ProgramFiles", "ProgramFiles(x86)"):
        root = os.environ.get(variable)
        if root:
            yield Path(root) / install_path

    for folder in os.get_exec_path():
        path = Path(folder.strip('"'))
        if path.is_absolute():
            yield path / executable


def open_demo_browser(url):
    if os.name != "nt":
        return webbrowser.open(url)

    for executable, install_path in WINDOWS_BROWSERS:
        for path in windows_browser_paths(executable, install_path):
            try:
                if path.is_absolute() and path.is_file():
                    subprocess.Popen([str(path), url], creationflags=subprocess.DETACHED_PROCESS)
                    return True
            except OSError:
                continue

    os.startfile(url)
    return True


def main():
    parser = argparse.ArgumentParser(description="Serve a local Chocolate Stunts browser build.")
    parser.add_argument("directory", nargs="?", type=Path, default=Path(__file__).resolve().parent,
                        help="directory containing restunts.html (default: directory of this script)")
    parser.add_argument("port", nargs="?", type=int, default=DEFAULT_PORT,
                        help="HTTP port (default: %(default)s)")
    parser.add_argument("--open-browser", action="store_true", help="open the demo in a browser")
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("port must be between 1 and 65535")

    directory = args.directory.resolve()
    if not (directory / "restunts.html").is_file():
        parser.error(f"restunts.html was not found in {directory}")

    handler = partial(DemoRequestHandler, directory=str(directory))
    address = (LOOPBACK_HOST, args.port)
    try:
        server = DemoServer(address, handler)
    except OSError as error:
        parser.exit(1, f"Could not start the local demo on {address[0]}:{address[1]}: {error}\n")

    with server:
        url = f"http://{address[0]}:{address[1]}/restunts.html"
        print(f"Open {url} in your browser.", flush=True)
        print("Press Ctrl+C to stop the server.", flush=True)
        try:
            if args.open_browser:
                try:
                    opened = open_demo_browser(url)
                except (OSError, webbrowser.Error):
                    opened = False
                if not opened:
                    print("Could not open a browser automatically. Open the URL above.",
                          file=sys.stderr, flush=True)
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
