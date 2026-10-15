#!/usr/bin/env python3
"""Build with `make -C src/restunts test-ghost-racing-line`, then run in DOSBox.

Check real DOS16 replay reconstruction and rasterization in classic and
SuperSight views. Captures stay on disk so the fixture uses no near heap.
"""

import argparse
import math
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


def main():
    repository = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--executable", type=Path,
        default=repository / "src/restunts/tests/build/watcom/release/GHOSTLIN.EXE",
    )
    parser.add_argument("--game-directory", type=Path, default=repository / "stunts")
    parser.add_argument("--dosbox", default="dosbox")
    parser.add_argument("--capture-directory", type=Path)
    parser.add_argument("--timeout", type=float, default=90)
    args = parser.parse_args()
    if not args.executable.is_file() or not args.game_directory.is_dir():
        parser.error("The DOS test executable and game data directory must exist")
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("--timeout must be finite and positive")

    with tempfile.TemporaryDirectory(prefix="restunts-ghost-line-") as temporary:
        directory = Path(temporary)
        for source in args.game_directory.iterdir():
            if source.is_file() and source.suffix.lower() not in (".exe", ".com", ".bat"):
                shutil.copyfile(source, directory / source.name.upper())
        shutil.copyfile(args.executable, directory / "GHOSTLIN.EXE")
        environment = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        command = [
            args.dosbox, "-silent", "-conf", str(repository / "tools/scripts/dosbox.proc.conf"),
            "-c", f'mount c "{directory}"', "-c", "c:",
            "-c", "GHOSTLIN.EXE --nointro /ns > RESULT.TXT",
            "-c", "if errorlevel 1 echo FAILED > STATUS.TXT",
            "-c", "echo DONE > DONE.TXT",
            "-c", "exit",
        ]
        with (directory / "dosbox.log").open("w") as log:
            process = subprocess.Popen(command, cwd=directory, env=environment,
                                       stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + args.timeout
                while not (directory / "DONE.TXT").exists():
                    if process.poll() is not None or time.monotonic() >= deadline:
                        break
                    time.sleep(0.1)
            finally:
                if process.poll() is None:
                    process.kill()
                process.wait()
        result_file = directory / "RESULT.TXT"
        result = result_file.read_text(errors="replace") if result_file.exists() else ""
        print(result.strip() or "DOS ghost test did not finish")
        if args.capture_directory is not None:
            args.capture_directory.mkdir(parents=True, exist_ok=True)
            for capture in directory.glob("*.RAW"):
                shutil.copyfile(capture, args.capture_directory / capture.name)
        status_file = directory / "STATUS.TXT"
        if (not (directory / "DONE.TXT").exists()
                or (status_file.exists() and status_file.read_text().strip())
                or not result.strip().endswith("DOS ghost racing line check passed")):
            print((directory / "dosbox.log").read_text(errors="replace"))
            return 1
        for mode in range(2):
            frames = [(directory / f"{color}{mode}.RAW").read_bytes()
                      for color in ("GREEN", "RED", "YELLOW")]
            if any(len(frame) != 320 * 200 for frame in frames):
                raise RuntimeError("Incomplete DOS frame capture")
            colored = sum(green == 10 and red == 12
                          for green, red in zip(frames[0], frames[1]))
            neutral = sum(green == 10 and red == 12 and yellow == 14
                          for green, red, yellow in zip(*frames))
            print(f"DOS racing line mode {mode}: {colored} green/red, {neutral} yellow pixels")
            if colored == 0 or neutral == 0:
                raise RuntimeError("DOS racing line speed colors are missing")
            if ((directory / f"BASE{mode}.RAW").read_bytes()
                    != (directory / f"DENSE{mode}.RAW").read_bytes()):
                raise RuntimeError(f"DOS dense guidance hides world geometry in mode {mode}")
        print("DOS dense guidance preserves the complete classic and SuperSight scenes")
        if ((directory / "CLEAR0.RAW").read_bytes()
                != (directory / "CLEAR1.RAW").read_bytes()):
            raise RuntimeError("Rendering depends on live speed after ghost cleanup")
        if list(directory.glob("G[HL][0-9]*.TMP")):
            raise RuntimeError("Ghost temporary files remain after cleanup")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
