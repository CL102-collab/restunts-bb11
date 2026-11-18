#!/usr/bin/env python3
"""Resume UNIT's recorded Ninh Binh checkpoint stall and finish the lap."""

import argparse
import csv
import os
from pathlib import Path
import subprocess
import tempfile

from regression_timeouts import scaled_timeout
from unit_replay_assets import stage_mcmu_game


BRAKING_FRAME = 3528
STALLED_FRAME = 3650
STALLED_ROUTE = 81
STALLED_POINT = 1
STALLED_POSITION = (28801.938, 1808.453, 38399.469)
POSITION_TOLERANCE = 1.0 / 64.0
FRAME_LIMIT = 6500
RECOVERY_FRAME_BUDGET = 200


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("native", type=Path)
    parser.add_argument("data", type=Path)
    parser.add_argument("assets", type=Path)
    parser.add_argument("replay", type=Path)
    args = parser.parse_args()
    environment = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")

    with tempfile.TemporaryDirectory(prefix="restunts-unit-checkpoint-") as temporary:
        work = Path(temporary)
        game = work / "game"
        stage_mcmu_game(args.data, args.assets, game)

        def run(name, *extra):
            trace = work / f"{name}.csv"
            command = [str(args.native.resolve()), "--data-dir", str(game),
                       "--source-replay", str(args.replay.resolve()), "--trace", str(trace),
                       "--verify-replay", *extra]
            result = subprocess.run(command, env=environment, text=True, capture_output=True,
                                    timeout=scaled_timeout(120))
            assert result.returncode == 0, (command, result.stdout, result.stderr)
            summary = next(line for line in result.stdout.splitlines()
                           if line.startswith("source_replay_version="))
            fields = dict(field.split("=", 1) for field in summary.split())
            assert f"replay_verified_frames={fields['frames']}" in result.stdout.splitlines(), result.stdout
            with trace.open(newline="") as source:
                return fields, list(csv.DictReader(source))

        recorded, original = run("recorded", "--replay-only")
        assert int(recorded["frames"]) == STALLED_FRAME and recorded["event"] == "0", recorded
        stalled = original[-1]
        stalled_route = int(stalled["route"]), int(stalled["point"])
        assert stalled_route == (STALLED_ROUTE, STALLED_POINT), stalled
        assert float(stalled["speed"]) == 0.0, stalled
        for axis, expected in zip(("x", "y", "z"), STALLED_POSITION):
            assert abs(float(stalled[axis]) - expected) <= POSITION_TOLERANCE, (axis, stalled)

        for start in (BRAKING_FRAME, STALLED_FRAME):
            result, resumed = run(f"branch-{start}", "--branch-frame", str(start),
                                  "--prepare-ms", "0", "--planner-sync", "--frames", str(FRAME_LIMIT),
                                  "--require-finish", "--require-clean")
            assert result["finished"] == "1" and result["penalty_frames"] == "0", result
            recorded_states = [row["gamestate_hex"] for row in resumed if row["phase"] == "recorded"]
            assert recorded_states == [row["gamestate_hex"] for row in original[:start + 1]], start
            recovery_window = resumed[start + 1:start + RECOVERY_FRAME_BUDGET + 1]
            assert any(int(row["route"]) > STALLED_ROUTE for row in recovery_window), start
            print(f"UNIT cleared the checkpoint and finished cleanly from frame {start}. "
                  f"Verified {result['frames']} replay frames.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
