#!/usr/bin/env python3
"""Verify UNIT drives identically toward a human until physical car contact."""

import argparse
import csv
import os
from pathlib import Path
import re
import subprocess
import tempfile

from regression_timeouts import scaled_timeout


FRAME_RATE = 20
FRAME_LIMIT = FRAME_RATE * 6
FAR_DISTANCE = 3500
DEFAULT_DISTANCE = 350
PREPARATION_MS = 10000
PREPARATION_STEPS = 64
MINIMUM_COMPARISON_FRAMES = 8
MINIMUM_APPROACH = 50.0
UNIT_FIELDS = (
    "frame", "time", "x", "y", "z", "speed", "steering", "route", "point", "event",
    "grip", "demanded", "yaw", "pitch", "roll", "contact", "penalty_frames",
    "penalty_status", "penalty_confirmations", "penalty_last_skipped", "penalty_recovering",
    "shortcuts_allowed", "throttle", "brake",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("native", type=Path)
    parser.add_argument("data", type=Path)
    parser.add_argument("track", type=Path)
    parser.add_argument("--human-mode", choices=("blocked", "slow"), default="blocked")
    parser.add_argument("--distance", type=int, default=DEFAULT_DISTANCE)
    parser.add_argument("--planner", choices=("sync", "threaded"), default="sync")
    parser.add_argument("--prepared", action="store_true")
    parser.add_argument("--owoot", action="store_true")
    args = parser.parse_args()
    assert 0 < args.distance < FAR_DISTANCE
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    with tempfile.TemporaryDirectory(prefix="restunts-unit-human-independence-") as temporary:
        work = Path(temporary)

        def race(label, distance):
            trace = work / f"{label}.csv"
            cache = work / label
            cache.mkdir()
            command = [str(args.native.resolve()), "--data-dir", str(args.data.resolve()),
                       str(args.track.resolve()), "7", "--car", "PMIN", "--manual",
                       "--frame-rate", str(FRAME_RATE), "--frames", str(FRAME_LIMIT),
                       "--human", args.human_mode, "--human-distance", str(distance),
                       "--verify-replay", "--trace", str(trace), "--unit-cache-dir", str(cache)]
            if args.planner == "sync":
                command.append("--planner-sync")
            else:
                command += ["--planner-yield-ms", "2"]
            if args.prepared:
                command += ["--prepare-ms", str(scaled_timeout(PREPARATION_MS)),
                            "--prepare-steps", str(PREPARATION_STEPS)]
            if args.owoot:
                command.append("--owoot:on")
            result = subprocess.run(command, env=env, text=True, capture_output=True,
                                    timeout=scaled_timeout(120))
            assert result.returncode == 0, (command, result.stdout, result.stderr)
            fields = {key: float(value) for key, value in
                      re.findall(r"(\w+)=(-?\d+(?:\.\d+)?)", result.stdout)}
            assert fields["replay_verified_frames"] == fields["frames"], result.stdout
            if args.planner == "sync":
                assert fields["planner_workers"] == 0, result.stdout
            elif args.prepared:
                assert fields["planner_workers"] == 1, result.stdout
            if args.prepared:
                assert fields["fixed_work"] == 1, result.stdout
                assert fields["preparation_steps"] == PREPARATION_STEPS, result.stdout
                assert fields["pilot_matched"] > 0, result.stdout
            with trace.open(newline="") as source:
                rows = list(csv.DictReader(source))
            assert len(rows) == int(fields["frames"]), (len(rows), result.stdout)
            return rows

        baseline = race("far", FAR_DISTANCE)
        encounter = race("encounter", args.distance)
        contact = next((index for index, row in enumerate(encounter)
                        if int(row["traffic_contact"])), None)
        assert contact is not None, "The native encounter must exercise actual car contact"
        assert contact >= MINIMUM_COMPARISON_FRAMES, (contact, MINIMUM_COMPARISON_FRAMES)
        assert len(baseline) > contact, (len(baseline), contact)
        for index in range(contact):
            expected = {key: baseline[index][key] for key in UNIT_FIELDS}
            actual = {key: encounter[index][key] for key in UNIT_FIELDS}
            assert actual == expected, (index + 1, expected, actual)
            assert not int(baseline[index]["traffic_contact"]), baseline[index]
        before_contact = encounter[:contact]
        assert any(int(row["throttle"]) for row in before_contact), "No pre-contact acceleration"
        initial_ahead = float(before_contact[0]["relative_ahead"])
        final_ahead = float(before_contact[-1]["relative_ahead"])
        assert final_ahead - initial_ahead >= MINIMUM_APPROACH, (initial_ahead, final_ahead)
        print(f"UNIT ignores {args.human_mode} human at {args.distance} units "
              f"({args.planner}, prepared={args.prepared}, OWOOT={args.owoot}): "
              f"{contact} identical pre-contact frames; actual collision and replay verified.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
