#!/usr/bin/env python3
"""Verify native UNIT passing, side commitment, rear clearance, and merging."""

import argparse
import csv
import os
from pathlib import Path
import re
import subprocess
import tempfile

from regression_timeouts import scaled_timeout


FRAME_RATE = 20
FRAME_LIMIT = FRAME_RATE * 20
PREPARATION_MS = 10000
PASS_CLEARANCE = 16.0
MERGE_LATERAL = 24.0
MAX_BLOCKED_FRAMES = FRAME_RATE * 5
HUMAN_TARGET_SPEED = 20.0
HUMAN_SPEED_TOLERANCE = 4.0
HUMAN_PEAK_SPEED_TOLERANCE = 6.0
SIDE_OFFSET = 16.0
CASES = {"blocked": ("blocked", 0.0), "left": ("blocked", -SIDE_OFFSET),
         "right": ("blocked", SIDE_OFFSET), "slow": ("slow", 0.0)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("native", type=Path)
    parser.add_argument("data", type=Path)
    parser.add_argument("track", type=Path,
                        help="A track with a long, flat opening straight, such as flat-rectangle.trk")
    parser.add_argument("--case", choices=CASES, default="blocked")
    parser.add_argument("--planner", choices=("sync", "threaded"), default="sync")
    parser.add_argument("--prepared", action="store_true")
    parser.add_argument("--owoot", action="store_true")
    parser.add_argument("--allow-contact", action="store_true",
                        help="Report nonfatal contact without failing a constrained passage")
    args = parser.parse_args()
    mode, offset = CASES[args.case]
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    with tempfile.TemporaryDirectory(prefix="restunts-unit-overtake-") as temporary:
        work = Path(temporary)
        trace = work / "traffic.csv"
        cache = work / "learning"
        cache.mkdir()
        command = [str(args.native.resolve()), "--data-dir", str(args.data.resolve()),
                   str(args.track.resolve()), "7", "--car", "PMIN", "--manual",
                   "--frame-rate", str(FRAME_RATE), "--frames", str(FRAME_LIMIT),
                   "--human", mode, "--human-offset", str(offset), "--require-overtake",
                   "--require-clean", "--verify-replay", "--trace", str(trace),
                   "--unit-cache-dir", str(cache)]
        if not args.allow_contact:
            command += ["--require-no-contact"]
        if args.planner == "sync":
            command += ["--planner-sync"]
        else:
            command += ["--planner-yield-ms", "2"]
        if args.prepared:
            command += ["--prepare-ms", str(PREPARATION_MS), "--prepare-until-plan"]
        if args.owoot:
            command += ["--owoot:on"]
        result = subprocess.run(
            command, env=env, text=True, capture_output=True, timeout=scaled_timeout(120))
        assert result.returncode == 0, (command, result.stdout, result.stderr)
        fields = {key: float(value) for key, value in
                  re.findall(r"(\w+)=(-?\d+(?:\.\d+)?)", result.stdout)}
        assert fields["passed_human"] == fields["merged_human"] == 1, result.stdout
        assert 0 < fields["pass_frame"] <= fields["merge_frame"] <= FRAME_LIMIT, result.stdout
        assert fields["rear_clearance"] > PASS_CLEARANCE, result.stdout
        assert abs(fields["road_lateral"]) <= MERGE_LATERAL, result.stdout
        assert fields["traffic_max_blocked_frames"] < MAX_BLOCKED_FRAMES, result.stdout
        assert fields["pass_side_changes"] == 0, result.stdout
        assert fields["pass_side"] != 0, result.stdout
        if offset:
            assert fields["pass_side"] * offset < 0, result.stdout
        assert fields["replay_verified_frames"] == fields["frames"], result.stdout
        assert fields["event"] == fields["human_event"] == fields["penalty_frames"] == 0
        if not args.allow_contact:
            assert fields["traffic_contact_frames"] == 0, result.stdout
        if args.planner == "sync":
            assert fields["planner_workers"] == 0, result.stdout
        elif args.prepared:
            assert fields["planner_workers"] == 1, result.stdout
        if args.prepared:
            assert fields["pilot_completed"] == 1 and fields["pilot_matched"] > 0, result.stdout
        if mode == "slow":
            assert fields["human_slow_frames"] >= FRAME_RATE // 2, result.stdout
            assert abs(fields["human_slow_mean_mph"] - HUMAN_TARGET_SPEED) <= \
                HUMAN_SPEED_TOLERANCE, result.stdout
            assert fields["human_peak_mph"] <= \
                HUMAN_TARGET_SPEED + HUMAN_PEAK_SPEED_TOLERANCE, result.stdout
        with trace.open(newline="") as source:
            rows = list(csv.DictReader(source))
        assert len(rows) == int(fields["frames"]), (len(rows), fields)
        assert int(rows[-1]["merged_human"]) == 1, rows[-1]
        assert float(rows[-1]["rear_clearance"]) > PASS_CLEARANCE, rows[-1]
        assert any(abs(float(row["relative_lateral"])) > MERGE_LATERAL for row in rows), \
            "The encounter must exercise a lateral pass, rather than an empty road"
        print(f"UNIT {args.case} pass ({args.planner}, prepared={args.prepared}, "
              f"OWOOT={args.owoot}): rear clear at {fields['pass_frame'] / FRAME_RATE:.2f}s, "
              f"merged at {fields['merge_frame'] / FRAME_RATE:.2f}s, "
              f"contact frames={int(fields['traffic_contact_frames'])}; replay verified.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
