#!/usr/bin/env python3
"""Require the exact AppImage to present through the host's OpenGL stack."""

import argparse
import os
from pathlib import Path
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import time


DISPLAY_STARTUP_SECONDS = 15
GAME_STARTUP_SECONDS = 30
RENDERER_SURVIVAL_SECONDS = 5
PROCESS_EXIT_SECONDS = 5
POLL_SECONDS = 0.1
OPENGL_RENDERER_MESSAGE = "Created renderer: opengl"
DRIVER_ENVIRONMENT_PREFIXES = ("SDL_", "MESA_", "LIBGL_", "__GL_", "GALLIUM_", "EGL_", "DRI_")


def stop_process_group(process):
    # Extract-and-run may leave a game child after its runtime has exited.
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=PROCESS_EXIT_SECONDS)
    except subprocess.TimeoutExpired:
        pass
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    process.wait(timeout=PROCESS_EXIT_SECONDS)


def test_opengl(appimage, data_directory):
    environment = {key: value for key, value in os.environ.items()
                   if key not in ("LD_PRELOAD", "LD_LIBRARY_PATH", "XAUTHORITY")
                   and not key.startswith(DRIVER_ENVIRONMENT_PREFIXES)}
    with tempfile.TemporaryDirectory(prefix="restunts-opengl-") as temporary:
        root = Path(temporary)
        game_data = root / "data"
        shutil.copytree(data_directory, game_data)
        display_log = root / "xvfb.log"
        with display_log.open("w") as errors:
            display = subprocess.Popen(
                ["Xvfb", "-displayfd", "1", "-screen", "0", "1024x768x24",
                 "-nolisten", "tcp", "+extension", "GLX", "-noreset"],
                env=environment, stdout=subprocess.PIPE, stderr=errors,
                text=True, start_new_session=True)
            try:
                if not select.select([display.stdout], [], [], DISPLAY_STARTUP_SECONDS)[0]:
                    raise ValueError("Xvfb did not publish a display before the startup deadline")
                display_number = display.stdout.readline().strip()
                if not display_number.isdecimal():
                    raise ValueError("Xvfb did not start: " + display_log.read_text())
                environment.update(
                    DISPLAY=":" + display_number,
                    APPIMAGE_EXTRACT_AND_RUN="1",
                    SDL_VIDEO_DRIVER="x11", SDL_AUDIO_DRIVER="dummy",
                    SDL_RENDER_DRIVER="opengl", SDL_LOGGING="render=info",
                    LIBGL_ALWAYS_SOFTWARE="1", RESTUNTS_VSYNC="0",
                    TMPDIR=str(root), XDG_CONFIG_HOME=str(root / "config"))
                game_log = root / "game.log"
                with game_log.open("w") as output:
                    game = subprocess.Popen(
                        [str(appimage), "--data-dir", str(game_data), "--nointro",
                         "--fullscreen:off", "/ns"],
                        cwd=root, env=environment, stdout=output, stderr=subprocess.STDOUT,
                        start_new_session=True)
                    try:
                        deadline = time.monotonic() + GAME_STARTUP_SECONDS
                        renderer_ready = None
                        while time.monotonic() < deadline:
                            log = game_log.read_text(errors="replace")
                            if game.poll() is not None:
                                raise ValueError(f"AppImage exited with {game.returncode}:\n{log}")
                            if display.poll() is not None:
                                raise ValueError("Xvfb exited during the OpenGL test:\n"
                                                 + display_log.read_text())
                            if OPENGL_RENDERER_MESSAGE in log.splitlines():
                                if renderer_ready is None:
                                    renderer_ready = time.monotonic()
                                    deadline = renderer_ready + RENDERER_SURVIVAL_SECONDS
                            time.sleep(POLL_SECONDS)
                        if renderer_ready is None:
                            raise ValueError("AppImage did not create the OpenGL renderer:\n" + log)
                        if game.poll() is not None:
                            raise ValueError(f"AppImage exited with {game.returncode}:\n{log}")
                        print(game_log.read_text(errors="replace"), end="")
                    finally:
                        stop_process_group(game)
            finally:
                display.stdout.close()
                stop_process_group(display)
    print("AppImage host OpenGL startup passed without library preloads.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("appimage", type=Path)
    parser.add_argument("game_data", type=Path)
    arguments = parser.parse_args()
    appimage = arguments.appimage.resolve(strict=True)
    data_directory = arguments.game_data.resolve(strict=True)
    if not appimage.is_file() or not os.access(appimage, os.X_OK):
        raise ValueError("The AppImage must be an executable file")
    if not data_directory.is_dir():
        raise ValueError("The game data must be a directory")
    test_opengl(appimage, data_directory)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"AppImage OpenGL check failed: {error}", file=sys.stderr)
        raise SystemExit(1)
