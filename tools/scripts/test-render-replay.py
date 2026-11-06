#!/usr/bin/env python3
"""Verify interpolated rendering preserves every replay tick and RNG seed."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


GAMESTATE_SIZE = 1120
RANDOM_SEED_SIZE = 6
RECORD_SIZE = GAMESTATE_SIZE + RANDOM_SEED_SIZE
REPLAY_TIMEOUT_SECONDS = 120
FULL_REPLAY_TIMEOUT_SECONDS = 300


def main():
    executable, data_directory = map(Path, sys.argv[1:3])
    full_replays = "--full" in sys.argv[3:]
    default_timeout = FULL_REPLAY_TIMEOUT_SECONDS if full_replays else REPLAY_TIMEOUT_SECONDS
    timeout_seconds = int(os.environ.get("RESTUNTS_RENDER_REPLAY_TIMEOUT_SECONDS", default_timeout))
    if timeout_seconds <= 0:
        raise ValueError("RESTUNTS_RENDER_REPLAY_TIMEOUT_SECONDS must be a positive integer")
    executable = executable.resolve()
    data_directory = data_directory.resolve()
    environment = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
                       RESTUNTS_AUDIO_TRACE="")
    available = {path.stem.upper(): path.stem for path in data_directory.iterdir()
                 if path.suffix.upper() == ".RPL"}
    # Cover 10 Hz, 20 Hz collisions/debris, both cars, and the hard landing at
    # 16.00-16.50 seconds, plus the loop exit after 40 seconds in SHAKING.
    # Check bounded interpolation through the HARDLAND landing and SHAKING loop exit.
    fixtures = [(available["DEFAULT"], 240, 0, 0)]
    for replay, limit, first, last in [("DEFCRSH", 0, 0, 0), ("0A0A", 240, 0, 0),
                                      ("HARDLAND", 0, 320, 330), ("SHAKING", 0, 0, 0)]:
        if replay in available:
            fixtures.append((available[replay], limit, first, last))
    if full_replays:
        fixtures = [(replay, 0, first, last) for replay, _, first, last in fixtures]
    with tempfile.TemporaryDirectory(prefix="restunts-render-replay-") as directory:
        for replay, limit, first, last in fixtures:
            settling = ("800", "900") if replay.upper() == "SHAKING" else ("0", "0")
            baseline = None
            for mode in range(4):
                output = Path(directory) / f"{replay}-{mode}.bin"
                subprocess.run([str(executable), "--data-dir", str(data_directory),
                                replay, str(output), str(mode), str(limit),
                                str(first), str(last), *settling],
                               env=environment, check=True,
                               timeout=timeout_seconds)
                actual = output.read_bytes()
                assert actual and len(actual) % RECORD_SIZE == 0
                if baseline is None:
                    baseline = actual
                elif actual != baseline:
                    offset = next((index for index, pair in enumerate(zip(actual, baseline))
                                   if pair[0] != pair[1]), min(len(actual), len(baseline)))
                    field_offset = offset % RECORD_SIZE
                    field = "gamestate" if field_offset < GAMESTATE_SIZE else "RNG seed"
                    if field == "RNG seed":
                        field_offset -= GAMESTATE_SIZE
                    raise AssertionError(f"{replay}, mode {mode}: {field} differs at "
                                         f"tick {offset // RECORD_SIZE}, byte {field_offset}")
            if replay.upper() == "DEFAULT":
                # Keep the physics comparison tied to the existing classic baseline;
                # both renderers must draw the real showroom meshes without changing it.
                for mode in (0, 1):
                    output = Path(directory) / f"{replay}-showroom-{mode}.bin"
                    subprocess.run([str(executable), "--data-dir", str(data_directory),
                                    replay, str(output), str(mode), str(limit),
                                    str(first), str(last), *settling, "--showroomcar:on"],
                                   env=environment, check=True, timeout=timeout_seconds)
                    assert output.read_bytes() == baseline, (
                        f"{replay}, mode {mode}: showroom geometry changed simulation or RNG")
                print(f"{replay}: showroom car geometry has matching scale and axle origin; "
                      "classic and HyperVision preserve every gamestate and RNG seed", flush=True)
            print(f"{replay}: {len(baseline) // RECORD_SIZE} identical gamestates and RNG seeds "
                  "with rendering off, on, toggled and adaptive quality swept", flush=True)


if __name__ == "__main__":
    main()
