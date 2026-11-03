#!/usr/bin/env python3
"""Compare live Software/Vulkan replay physics and RNG across both scene modes."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


GAMESTATE_SIZE = 1120
RANDOM_SEED_SIZE = 6
RECORD_SIZE = GAMESTATE_SIZE + RANDOM_SEED_SIZE
REPLAY_LIMIT = 60
TOGGLED_RENDERING_MODE = 2
TIMEOUT_SECONDS = 180
VULKAN_UNAVAILABLE = 77


def main():
    software, vulkan, data_directory = map(lambda value: Path(value).resolve(), sys.argv[1:4])
    replay = next(path.stem for path in data_directory.iterdir()
                  if path.name.upper() == "DEFAULT.RPL")
    environment = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
                       RESTUNTS_AUDIO_TRACE="")
    timeout = int(os.environ.get("RESTUNTS_RENDER_REPLAY_TIMEOUT_SECONDS", TIMEOUT_SECONDS))
    if timeout <= 0:
        raise ValueError("RESTUNTS_RENDER_REPLAY_TIMEOUT_SECONDS must be a positive integer")
    recordings = {}
    with tempfile.TemporaryDirectory(prefix="restunts-vulkan-replay-") as directory:
        # Probe Vulkan first so hosts without a compute device can report a skip.
        # Initialization or rendering failures on enumerated devices remain failures.
        for label, executable in (("Vulkan", vulkan), ("Software", software)):
            output = Path(directory) / f"{label}.bin"
            result = subprocess.run([str(executable), "--data-dir", str(data_directory), replay,
                                     str(output), str(TOGGLED_RENDERING_MODE), str(REPLAY_LIMIT),
                                     "0", "0", "0", "0"],
                                    env=environment, timeout=timeout, check=False)
            if label == "Vulkan" and result.returncode == VULKAN_UNAVAILABLE:
                return VULKAN_UNAVAILABLE
            result.check_returncode()
            actual = output.read_bytes()
            expected_size = (REPLAY_LIMIT + 1) * RECORD_SIZE
            if len(actual) != expected_size:
                raise AssertionError(f"{label}: expected {expected_size} replay bytes, "
                                     f"got {len(actual)}")
            recordings[label] = actual
    if recordings["Software"] != recordings["Vulkan"]:
        offset = next(index for index, values in enumerate(zip(recordings["Software"],
                                                               recordings["Vulkan"]))
                      if values[0] != values[1])
        field_offset = offset % RECORD_SIZE
        field = "gamestate" if field_offset < GAMESTATE_SIZE else "RNG seed"
        if field == "RNG seed":
            field_offset -= GAMESTATE_SIZE
        raise AssertionError(f"Software/Vulkan {field} differs at tick "
                             f"{offset // RECORD_SIZE}, byte {field_offset}")
    print(f"Software/Vulkan: {REPLAY_LIMIT + 1} identical gamestates and RNG seeds "
          "while toggling classic and HyperVision scene rendering", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
