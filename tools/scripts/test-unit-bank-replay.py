#!/usr/bin/env python3
"""Verify bank approach planning against the recorded Ninh Binh failure."""

import argparse
import csv
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


RECORDED_FRAMES = 3811
APPROACH_FRAME = 3400
PREPARATION_MS = 10000
FRAME_LIMIT = 12000
FIRST_BANK_ROUTE = 80
LAST_BANK_ROUTE = 83
BELOW_BANK_HEIGHT = 1700.0
GRASS_CONTACT_SUM = 16
POSITION_TOLERANCE = 1.0 / 64.0
MCMU_FILES = ("CARMCMU.RES", "STMCMU.3SH", "STDAMCMU.VSH", "STDBMCMU.VSH")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("native", type=Path)
    parser.add_argument("data", type=Path)
    parser.add_argument("assets", type=Path)
    parser.add_argument("replay", type=Path)
    args = parser.parse_args()
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    with tempfile.TemporaryDirectory(prefix="restunts-unit-bank-") as temporary:
        work = Path(temporary)
        game = work / "game"
        shutil.copytree(args.data.resolve(), game)
        for name in MCMU_FILES:
            destination = game / name
            destination.unlink(missing_ok=True)
            shutil.copyfile(args.assets / name, destination)
        cache = work / "learning"
        cache.mkdir()

        for unsupported in ("--require-no-stalls", "--require-worker"):
            command = [str(args.native.resolve()), "--data-dir", str(game),
                       "--source-replay", str(args.replay.resolve()), "--replay-only", unsupported]
            rejected = subprocess.run(command, env=env, text=True, capture_output=True, timeout=10)
            assert rejected.returncode != 0 and "does not support" in rejected.stderr, rejected

        def run(name, *extra):
            trace = work / (name + ".csv")
            command = [str(args.native.resolve()), "--data-dir", str(game),
                       "--source-replay", str(args.replay.resolve()), "--trace", str(trace),
                       "--verify-replay", "--unit-cache-dir", str(cache), *extra]
            result = subprocess.run(command, env=env, text=True, capture_output=True, timeout=120)
            assert result.returncode == 0, (command, result.stdout, result.stderr)
            fields = {key: float(value) for key, value in
                      re.findall(r"(\w+)=(-?\d+(?:\.\d+)?)", result.stdout)}
            assert fields["source_replay_version"] == 21, result.stdout
            assert fields["source_replay_rules"] == 31, result.stdout
            assert "player_car=COUN player_transmission=1" in result.stdout, result.stdout
            assert "opponent_car=MCMU opponent_transmission=0" in result.stdout, result.stdout
            assert fields["replay_verified_frames"] == fields["frames"], result.stdout
            assert fields["penalty_frames"] == 0, result.stdout
            with trace.open(newline="") as source:
                rows = list(csv.DictReader(source))
            return fields, rows

        recorded, original = run("recorded", "--replay-only")
        assert recorded["frames"] == RECORDED_FRAMES and recorded["event"] == 1, recorded
        final = original[-1]
        assert (int(final["route"]), int(final["point"])) == (83, 16), final
        for key, expected in (("x", 22303.688), ("y", 1710.453), ("z", 32115.188)):
            assert abs(float(final[key]) - expected) <= POSITION_TOLERANCE, (key, final)
        assert recorded["learning_enabled"] == 0
        assert not list(cache.glob("*.ULC"))
        original_states = {row["frame"]: row["gamestate_hex"] for row in original}

        branch, corrected = run("approach", "--branch-frame", str(APPROACH_FRAME),
                                "--prepare-ms", str(PREPARATION_MS), "--planner-sync",
                                "--frames", str(FRAME_LIMIT), "--require-finish", "--require-clean")
        assert branch["finished"] == 1, branch
        assert branch["learning_enabled"] == 0, branch
        assert not list(cache.glob("*.ULC")), "A mid-race suffix must not become a learned lap"
        for row in corrected:
            if row["phase"] == "recorded":
                assert row["gamestate_hex"] == original_states[row["frame"]], row["frame"]
        bank = [row for row in corrected if row["phase"] == "live"
                and FIRST_BANK_ROUTE <= int(row["route"]) <= LAST_BANK_ROUTE]
        assert bank, "The corrected continuation must traverse the reported bank"
        unsupported = [row for row in bank if int(row["contact"]) == GRASS_CONTACT_SUM
                       and float(row["y"]) < BELOW_BANK_HEIGHT]
        assert not unsupported, unsupported[:1]

        fresh, _ = run("fresh", "--branch-frame", "0", "--prepare-ms", str(PREPARATION_MS),
                       "--planner-yield-ms", "2", "--frames", str(FRAME_LIMIT),
                       "--require-finish", "--require-clean")
        assert fresh["finished"] == 1 and fresh["learning_saved"] == 1, fresh
        assert len(list(cache.glob("*.ULC"))) == 1
        print("Recorded bank crash preserved; exact approach and fresh UNIT races finish cleanly, "
              "with verified replays and no below-deck bank pursuit or suffix-cache writes.")
        print(f"UNIT finish: fresh={fresh['seconds']:.2f}s, "
              f"recorded approach={branch['seconds']:.2f}s (total race time).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
