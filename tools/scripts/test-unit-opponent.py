#!/usr/bin/env python3
"""Benchmark native races against Skid and verify UNIT replay determinism."""

import argparse
import json
import os
from pathlib import Path
import subprocess

from regression_timeouts import scaled_timeout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("native", type=Path, help="test-unit-race executable")
    parser.add_argument("data", type=Path, help="original Stunts resource directory")
    parser.add_argument("tracks", nargs="+", type=Path)
    parser.add_argument("--prepare-ms", type=int, default=10000)
    parser.add_argument("--prepare-until-plan", action="store_true",
                        help="stop once a complete clean native lap is ready")
    parser.add_argument("--car", default="PMIN", help="four-character car identifier")
    parser.add_argument("--frame-rate", type=int, choices=(10, 20), default=20)
    parser.add_argument("--planner-yield-ms", type=int, default=2,
                        help="worker scheduling delay per prepared race tick (default: 2)")
    parser.add_argument("--require-worker", action="store_true")
    parser.add_argument("--planner-sync", action="store_true",
                        help="prepare and race with the bounded fallback and no workers")
    parser.add_argument("--frames", type=int, default=12000)
    parser.add_argument("--owoot", action="store_true")
    parser.add_argument("--manual", action="store_true", help="opponent uses manual transmission")
    parser.add_argument("--require-faster", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    environment = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    results = []
    for track in args.tracks:
        races = {}
        for opponent in (6, 7):
            command = [str(args.native.resolve()), "--data-dir", str(args.data.resolve()),
                       str(track.resolve()), str(opponent), "--frames", str(args.frames),
                       "--car", args.car, "--frame-rate", str(args.frame_rate)]
            if opponent == 7:
                command += ["--prepare-ms", str(args.prepare_ms), "--verify-replay",
                            "--planner-yield-ms", str(args.planner_yield_ms if args.prepare_ms and not args.planner_sync else 0)]
                if args.prepare_until_plan:
                    command.append("--prepare-until-plan")
                if args.require_worker:
                    command.append("--require-worker")
                if args.planner_sync:
                    command.append("--planner-sync")
            if args.owoot:
                command.append("--owoot:on")
            if args.manual:
                command.append("--manual")
            completed = subprocess.run(command, env=environment, text=True, capture_output=True,
                                       timeout=scaled_timeout(
                                           180 + (args.prepare_ms + args.frames * args.planner_yield_ms) / 1000))
            assert completed.returncode == 0, (command, completed.stdout, completed.stderr)
            line = next((line for line in completed.stdout.splitlines()
                         if line.startswith("opponent=")), None)
            assert line is not None, completed.stdout
            race = {key: float(value) for key, value in
                    (part.split("=", 1) for part in line.split())}
            if opponent == 7:
                for stats in completed.stdout.splitlines():
                    if stats.startswith(("planner_workers=", "pilot_ticks=", "pilot_matched=")):
                        race.update({key: float(value) for key, value in
                                     (part.split("=", 1) for part in stats.split())})
                assert race["finished"] == 1, (str(track), opponent, race)
                assert race["penalty_frames"] == 0, (str(track), opponent, race)
                assert f"replay_verified_frames={int(race['frames'])}" in completed.stdout
            races["skid" if opponent == 6 else "unit"] = race
        result = {"track": str(track), "car": args.car, "owoot": args.owoot,
                  "manual": args.manual, "frame_rate": args.frame_rate,
                  "planner_sync": args.planner_sync, **races}
        result["speedup"] = (races["skid"]["seconds"] / races["unit"]["seconds"]
                             if races["skid"]["finished"] else None)
        if args.require_faster:
            assert result["speedup"] is not None and result["speedup"] > 1, result
        results.append(result)
    output = json.dumps(results, indent=2) + "\n"
    print(output, end="")
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
