#!/usr/bin/env python3
"""Compare live Software/GPU replay physics and RNG across both scene modes."""

import argparse
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
GPU_UNAVAILABLE = 77


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("software", type=Path)
    parser.add_argument("gpu", type=Path)
    parser.add_argument("data_directory", type=Path)
    parser.add_argument("--renderer-name", choices=("Vulkan", "Metal"), default="Vulkan",
                        help="Native GPU backend used by the instrumented replay executable")
    parser.add_argument("--present", action="store_true",
                        help="Use a display driver for the Vulkan window replay executable")
    arguments = parser.parse_args()
    if arguments.present and arguments.renderer_name != "Vulkan":
        parser.error("--present requires --renderer-name Vulkan")
    software, gpu, data_directory = (arguments.software.resolve(), arguments.gpu.resolve(),
                                     arguments.data_directory.resolve())
    renderer = arguments.renderer_name
    replay = next(path.stem for path in data_directory.iterdir()
                  if path.name.upper() == "DEFAULT.RPL")
    environment = dict(os.environ, SDL_AUDIODRIVER="dummy", RESTUNTS_AUDIO_TRACE="")
    software_environment = dict(environment, SDL_VIDEODRIVER="dummy")
    gpu_environment = (dict(environment, RESTUNTS_VSYNC="0") if arguments.present
                       else software_environment)
    timeout = int(os.environ.get("RESTUNTS_RENDER_REPLAY_TIMEOUT_SECONDS", TIMEOUT_SECONDS))
    if timeout <= 0:
        raise ValueError("RESTUNTS_RENDER_REPLAY_TIMEOUT_SECONDS must be a positive integer")
    recordings = {}
    with tempfile.TemporaryDirectory(prefix="restunts-gpu-replay-") as directory:
        # Probe the GPU first so hosts without a compute device can report a skip.
        # Initialization or rendering failures on enumerated devices remain failures.
        for label, executable, replay_environment in (
                (renderer, gpu, gpu_environment), ("Software", software, software_environment)):
            output = Path(directory) / f"{label}.bin"
            result = subprocess.run([str(executable), "--data-dir", str(data_directory), replay,
                                     str(output), str(TOGGLED_RENDERING_MODE), str(REPLAY_LIMIT),
                                     "0", "0", "0", "0"],
                                    env=replay_environment, timeout=timeout, check=False)
            if label == renderer and result.returncode == GPU_UNAVAILABLE:
                return GPU_UNAVAILABLE
            result.check_returncode()
            actual = output.read_bytes()
            expected_size = (REPLAY_LIMIT + 1) * RECORD_SIZE
            if len(actual) != expected_size:
                raise AssertionError(f"{label}: expected {expected_size} replay bytes, "
                                     f"got {len(actual)}")
            recordings[label] = actual
    if recordings["Software"] != recordings[renderer]:
        offset = next(index for index, values in enumerate(zip(recordings["Software"],
                                                               recordings[renderer]))
                      if values[0] != values[1])
        field_offset = offset % RECORD_SIZE
        field = "gamestate" if field_offset < GAMESTATE_SIZE else "RNG seed"
        if field == "RNG seed":
            field_offset -= GAMESTATE_SIZE
        raise AssertionError(f"Software/{renderer} {field} differs at tick "
                             f"{offset // RECORD_SIZE}, byte {field_offset}")
    presentation = " with window presentation" if arguments.present else ""
    print(f"Software/{renderer}: {REPLAY_LIMIT + 1} identical gamestates and RNG seeds "
          f"while toggling classic and HyperVision scene rendering{presentation}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
