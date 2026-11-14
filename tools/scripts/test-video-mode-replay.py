#!/usr/bin/env python3
"""Verify DOS video modes and HyperVision switches preserve replay physics."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


GAMESTATE_SIZE = 1120
RANDOM_SEED_SIZE = 6
RECORD_SIZE = GAMESTATE_SIZE + RANDOM_SEED_SIZE
REPLAY_TICK_LIMIT = 60
REPLAY_TIMEOUT_SECONDS = 60
RENDER_MODES = (("classic", 0), ("toggled", 2))
VIDEO_MODES = (("vga", ()), ("ega", ("--videomode:ega",)),
               ("cga", ("--videomode:cga",)))


def assert_identical(actual, expected, label):
    if actual == expected:
        return
    offset = next((index for index, pair in enumerate(zip(actual, expected))
                   if pair[0] != pair[1]), min(len(actual), len(expected)))
    field_offset = offset % RECORD_SIZE
    field = "gamestate" if field_offset < GAMESTATE_SIZE else "RNG seed"
    if field == "RNG seed":
        field_offset -= GAMESTATE_SIZE
    raise AssertionError(f"{label}: {field} differs at tick {offset // RECORD_SIZE}, "
                         f"byte {field_offset}")


def main():
    executable, data_directory = (Path(value).resolve() for value in sys.argv[1:3])
    available = {path.stem.upper(): path.stem for path in data_directory.iterdir()
                 if path.suffix.upper() == ".RPL"}
    replay = available["DEFAULT"]
    environment = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
                       RESTUNTS_AUDIO_TRACE="")
    expected_bytes = (REPLAY_TICK_LIMIT + 1) * RECORD_SIZE
    baseline = None
    with tempfile.TemporaryDirectory(prefix="restunts-video-mode-replay-") as directory:
        for rendering, mode in RENDER_MODES:
            for video_mode, options in VIDEO_MODES:
                label = f"{video_mode}/{rendering}"
                output = Path(directory) / f"{video_mode}-{rendering}.bin"
                # Exercise both original positional forms, with and without
                # the optional real video-mode startup argument.
                settling = ("0", "0") if mode else ()
                command = [str(executable), "--data-dir", str(data_directory),
                           replay, str(output), str(mode), str(REPLAY_TICK_LIMIT), "0", "0",
                           *settling, *options]
                result = subprocess.run(command, env=environment, text=True, capture_output=True,
                                        timeout=REPLAY_TIMEOUT_SECONDS, check=False)
                if result.returncode:
                    raise AssertionError(f"{label}: replay process failed ({result.returncode})\n"
                                         f"{result.stdout}{result.stderr}")
                actual = output.read_bytes()
                if len(actual) != expected_bytes:
                    raise AssertionError(f"{label}: expected {expected_bytes} state/seed bytes, "
                                         f"received {len(actual)}")
                if baseline is None:
                    baseline = actual
                else:
                    assert_identical(actual, baseline, label)
                print(f"{label}: {REPLAY_TICK_LIMIT + 1} identical gamestates and RNG seeds",
                      flush=True)
    print("VGA, EGA and CGA preserve replay physics with classic and toggled HyperVision rendering.",
          flush=True)


if __name__ == "__main__":
    main()
