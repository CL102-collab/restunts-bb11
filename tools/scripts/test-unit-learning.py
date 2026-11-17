#!/usr/bin/env python3
"""Verify persistent UNIT learning across isolated native race processes."""

import argparse
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile

from regression_timeouts import scaled_timeout


PREPARATION_MS = 600
SINGLE_FRAME_PREPARATION_MS = 150
FRAME_LIMIT = 6000
TRACK_GRID_CELLS = 900
TRACK_TERRAIN_OFFSET = TRACK_GRID_CELLS + 1
WATER_TERRAIN = 1
CSTRK_HEADER_SIZE = 32
CSTRK_HORIZON_OFFSET = 12
CSTRK_WIDTH_OFFSET = 16
CSTRK_NOTES_LENGTH_OFFSET = 20
CSTRK_OBJECT_COUNT_OFFSET = 24
CSTRK_PAYLOAD_LENGTH_OFFSET = 28
CSTRK_OBJECT_SIZE = 16
CSTRK_CELL_SIZE = 2
CSTRK_HORIZONS = 5
STUNT_PREPARATION_MS = 10000


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("native", type=Path)
    parser.add_argument("data", type=Path)
    parser.add_argument("track", type=Path)
    parser.add_argument("extended_track", type=Path)
    parser.add_argument("stunt_track", type=Path)
    args = parser.parse_args()
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    with tempfile.TemporaryDirectory(prefix="restunts-unit-learning-") as temporary:
        cache = Path(temporary)

        def race(*, manual=True, car="PMIN", owoot=False, frame_rate=20,
                 track=None, short=False, preparation_ms=None):
            if preparation_ms is None:
                preparation_ms = SINGLE_FRAME_PREPARATION_MS if short else PREPARATION_MS
            command = [str(args.native.resolve()), "--data-dir", str(args.data.resolve()),
                       str((track or args.track).resolve()), "7", "--car", car,
                       "--frame-rate", str(frame_rate), "--planner-sync",
                       "--prepare-ms", str(preparation_ms),
                       "--frames", "1" if short else str(FRAME_LIMIT),
                       "--unit-cache-dir", str(cache), "--verify-replay", "--require-clean"]
            if manual:
                command.append("--manual")
            if owoot:
                command.append("--owoot:on")
            if not short:
                command.append("--require-finish")
            result = subprocess.run(
                command, env=env, text=True, capture_output=True, timeout=scaled_timeout(60))
            assert result.returncode == 0, (command, result.stdout, result.stderr)
            fields = {key: float(value) for key, value in
                      re.findall(r"(\w+)=(-?\d+(?:\.\d+)?)", result.stdout)}
            assert fields["replay_verified_frames"] == fields["frames"], result.stdout
            assert fields["penalty_frames"] == 0, result.stdout
            assert fields["learning_enabled"] == 1, result.stdout
            assert fields["learning_saved"] == 1, result.stdout
            if not short:
                assert fields["finished"] == 1, result.stdout
                assert fields["learning_best_frames"] > 0, result.stdout
            return fields

        cold = race()
        files = sorted(cache.glob("*.ULC"))
        assert len(files) == 1, files
        original_file = files[0]
        assert cold["learning_loaded"] == 0, cold
        warm = race()
        assert warm["learning_loaded"] == 1, warm
        assert warm["learning_loaded_samples"] == cold["learning_search_samples"], (cold, warm)
        assert warm["learning_loaded_native_samples"] == cold["learning_native_samples"], (cold, warm)
        assert warm["learning_search_samples"] > cold["learning_search_samples"], (cold, warm)
        assert warm["learning_native_samples"] > cold["learning_native_samples"], (cold, warm)
        assert warm["learning_best_frames"] <= cold["learning_best_frames"], (cold, warm)
        assert len(list(cache.glob("*.ULC"))) == 1
        again = race()
        assert again["learning_loaded"] == 1, again
        assert again["learning_best_frames"] <= warm["learning_best_frames"], (warm, again)

        variants = [dict(manual=False), dict(car="ANSX"), dict(owoot=True), dict(frame_rate=10)]
        for variant in variants:
            result = race(short=True, **variant)
            assert result["learning_loaded"] == 0, (variant, result)
        changed = cache / "changed.trk"
        contents = bytearray(args.track.read_bytes())
        assert contents[0] == 0 and contents[TRACK_TERRAIN_OFFSET] == 0
        contents[TRACK_TERRAIN_OFFSET] = WATER_TERRAIN
        changed.write_bytes(contents)
        result = race(short=True, track=changed)
        assert result["learning_loaded"] == 0, result
        assert len(list(cache.glob("*.ULC"))) == 6

        original_file.write_bytes(b"invalid UNIT learning cache")
        recovered = race()
        assert recovered["learning_loaded"] == 0, recovered
        assert original_file.stat().st_size > len(b"invalid UNIT learning cache")

        extended = race(short=True, track=args.extended_track)
        assert extended["learning_loaded"] == 0, extended
        count = len(list(cache.glob("*.ULC")))
        contents = bytearray(args.extended_track.read_bytes())
        width, height = struct.unpack_from("<HH", contents, CSTRK_WIDTH_OFFSET)
        objects = struct.unpack_from("<I", contents, CSTRK_OBJECT_COUNT_OFFSET)[0]
        horizon = struct.unpack_from("<I", contents, CSTRK_HORIZON_OFFSET)[0]
        notes_offset = (CSTRK_HEADER_SIZE + (width + 1) * (height + 1)
                        + CSTRK_CELL_SIZE * width * height + CSTRK_OBJECT_SIZE * objects)
        notes = b"Different notes; identical driving geometry."
        custom_horizon = b"different-horizon.png"
        contents = contents[:notes_offset] + notes + struct.pack("<H", len(custom_horizon)) + custom_horizon
        struct.pack_into("<I", contents, CSTRK_HORIZON_OFFSET, (horizon + 1) % CSTRK_HORIZONS)
        struct.pack_into("<I", contents, CSTRK_NOTES_LENGTH_OFFSET, len(notes))
        struct.pack_into("<I", contents, CSTRK_PAYLOAD_LENGTH_OFFSET, len(contents) - CSTRK_HEADER_SIZE)
        metadata = cache / "renamed-with-different-metadata.cstrk"
        metadata.write_bytes(contents)
        reused = race(short=True, track=metadata)
        assert reused["learning_loaded"] == 1, reused
        assert len(list(cache.glob("*.ULC"))) == count

        stunt_cold = race(track=args.stunt_track, preparation_ms=STUNT_PREPARATION_MS)
        stunt_warm = race(track=args.stunt_track, preparation_ms=STUNT_PREPARATION_MS)
        assert stunt_cold["learning_loaded"] == 0, stunt_cold
        assert stunt_warm["learning_loaded"] == 1, stunt_warm
        assert stunt_warm["learning_loaded_samples"] == stunt_cold["learning_search_samples"]
        assert stunt_warm["learning_native_samples"] > stunt_cold["learning_native_samples"]
        assert stunt_warm["learning_best_frames"] <= stunt_cold["learning_best_frames"]
        print("Persistent UNIT learning: cold/warm process reload, continued search, best-lap "
              "retention, key isolation, horizon/notes reuse, corruption recovery, clean finishes "
              "and replays on ordinary roads and native stunts passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
