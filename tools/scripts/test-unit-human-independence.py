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
    parser.add_argument("--human-mode", choices=("blocked", "slow", "grid-then-park"),
                        default="blocked")
    parser.add_argument("--distance", type=int, default=DEFAULT_DISTANCE)
    parser.add_argument("--planner", choices=("sync", "threaded"), default="sync")
    parser.add_argument("--transmission", choices=("manual", "automatic"), default="manual")
    parser.add_argument("--prepared", action="store_true")
    parser.add_argument("--prepare-steps", type=int, default=PREPARATION_STEPS)
    parser.add_argument("--prepare-ms", type=int,
                        help="preparation safety ceiling; defaults to the scaled ten-second budget")
    parser.add_argument("--owoot", action="store_true")
    args = parser.parse_args()
    grid = args.human_mode == "grid-then-park"
    assert 0 < args.distance < FAR_DISTANCE
    assert args.prepare_steps > 0
    assert args.prepare_ms is None or args.prepare_ms > 0
    preparation_ms = (args.prepare_ms if args.prepare_ms is not None else
                      scaled_timeout(PREPARATION_MS))
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    with tempfile.TemporaryDirectory(prefix="restunts-unit-human-independence-") as temporary:
        work = Path(temporary)

        def race(label, distance, mode):
            trace = work / f"{label}.csv"
            cache = work / label
            cache.mkdir()
            command = [str(args.native.resolve()), "--data-dir", str(args.data.resolve()),
                       str(args.track.resolve()), "7", "--car", "PMIN",
                       "--frame-rate", str(FRAME_RATE), "--frames", str(FRAME_LIMIT),
                       "--verify-replay", "--trace", str(trace), "--unit-cache-dir", str(cache)]
            if args.transmission == "manual":
                command.append("--manual")
            if mode is not None:
                command += ["--human", mode]
                if mode != "grid-then-park":
                    command += ["--human-distance", str(distance)]
            if args.planner == "sync":
                command.append("--planner-sync")
            else:
                command += ["--planner-yield-ms", "2"]
            if args.prepared:
                command += ["--prepare-ms", str(preparation_ms),
                            "--prepare-steps", str(args.prepare_steps)]
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
                if grid and fields["planner_workers"] == 0:
                    # A still-valid native continuation pauses the real worker.
                    assert not fields["pilot_completed"] and not fields["pilot_exhausted"], (
                        result.stdout)
                else:
                    assert fields["planner_workers"] == 1, result.stdout
            if args.prepared:
                assert fields["fixed_work"] == 1, result.stdout
                assert fields["preparation_steps"] == args.prepare_steps, result.stdout
                assert fields["pilot_matched"] > 0, result.stdout
            with trace.open(newline="") as source:
                rows = list(csv.DictReader(source))
            assert len(rows) == int(fields["frames"]), (len(rows), result.stdout)
            if grid:
                diagnostic_keys = ("preparation_steps", "selected_preview_native_ticks",
                                   "pilot_ticks", "pilot_completed", "pilot_exhausted",
                                   "planner_workers", "worker_accepted", "worker_commands_applied",
                                   "pilot_matched",
                                   "pilot_rejected")
                print(label + ": " + " ".join(f"{key}={fields[key]:g}" for key in diagnostic_keys))
            return rows

        # Grid mode changes only recorded human inputs and its initial parking.
        # Both runs retain UNIT's actual starting-grid pose and route provenance.
        baseline = race("far", FAR_DISTANCE, None if grid else args.human_mode)
        encounter = race("encounter", args.distance, args.human_mode)
        contacts = [index for rows in (baseline, encounter)
                    for index, row in enumerate(rows) if int(row["traffic_contact"])]
        contact = min(contacts) if contacts else None
        if not grid:
            assert any(int(row["traffic_contact"]) for row in encounter), (
                "The native encounter must exercise actual car contact")
        if contact is None:
            assert len(baseline) == len(encounter) == FRAME_LIMIT, (len(baseline), len(encounter))
            compared = FRAME_LIMIT
        else:
            assert len(baseline) > contact and len(encounter) > contact, (
                len(baseline), len(encounter), contact)
            compared = contact
        assert compared >= MINIMUM_COMPARISON_FRAMES, (compared, MINIMUM_COMPARISON_FRAMES)
        for index in range(compared):
            expected = {key: baseline[index][key] for key in UNIT_FIELDS}
            actual = {key: encounter[index][key] for key in UNIT_FIELDS}
            assert actual == expected, (index + 1, expected, actual)
            assert not int(baseline[index]["traffic_contact"]), baseline[index]
        before_contact = encounter[:compared]
        assert any(int(row["throttle"]) for row in before_contact), "No pre-contact acceleration"
        if grid:
            assert any(baseline[index]["human_input"] != encounter[index]["human_input"]
                       for index in range(compared)), "Human inputs never differed before contact"
            assert any(any(baseline[index][key] != encounter[index][key]
                           for key in ("human_x", "human_y", "human_z"))
                       for index in range(compared)), (
                           "Human positions never differed before contact")
        else:
            initial_ahead = float(before_contact[0]["relative_ahead"])
            final_ahead = float(before_contact[-1]["relative_ahead"])
            assert final_ahead - initial_ahead >= MINIMUM_APPROACH, (initial_ahead, final_ahead)
        location = "on the native grid" if grid else f"at {args.distance} units"
        contact_result = "actual collision" if contact is not None else "full contact-free window"
        print(f"UNIT ignores {args.human_mode} human {location} "
              f"({args.planner}, {args.transmission}, prepared={args.prepared}, "
              f"OWOOT={args.owoot}): "
              f"{compared} identical pre-contact frames; {contact_result} and replay verified.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
