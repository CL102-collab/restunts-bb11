#!/usr/bin/env python3
"""Verify DOS16 repldump time reports in DOSBox using replay corpus fixtures."""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time


def main():
    repository = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dosbox", default="dosbox-x")
    parser.add_argument("--data-dir", type=Path, default=repository / "stunts")
    parser.add_argument("--fixtures-dir", type=Path, required=True,
                        help="Directory containing 0000.rpl, 0722.rpl, r0019.rpl, and 1904.rpl")
    parser.add_argument("--executable", type=Path,
                        default=repository / "src/restunts/repldump/repldump.exe")
    parser.add_argument("--output", type=Path, default=repository / "out/dos16-time")
    parser.add_argument("--timeout", type=float, default=120)
    args = parser.parse_args()
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("--timeout must be finite and positive")
    required = [args.executable, *(args.fixtures_dir / name for name in
                                 ("0000.rpl", "0722.rpl", "r0019.rpl", "1904.rpl"))]
    for path in required:
        if not path.is_file():
            parser.error(f"Missing {path}")
    if not args.data_dir.is_dir():
        parser.error("--data-dir must contain the Stunts game resources")
    args.output.mkdir(parents=True, exist_ok=True)
    cases = [
        {"name": "TIMEBASE", "source": "0000.rpl", "flags": "--time",
         "time": b"1:53.60\n0:00", "exit": 0},
        {"name": "TIMEDWS", "source": "0722.rpl", "flags": "--time --dws:off",
         "time": b"0:31.25\n3:18", "exit": 0},
        {"name": "TIME10HZ", "source": "1904.rpl", "flags": "--time",
         "time": b"1:38.70\n0:00", "exit": 0},
        {"name": "TIMEOWO", "source": "r0019.rpl", "flags": "--TiMe --OWOOT",
         "extension": ".RpL", "time": b"", "owoot": b"fail", "exit": 0},
        {"name": "NOFLAG", "source": "0000.rpl", "flags": "",
         "time": b"previous time", "seed": b"previous time", "exit": 0},
        {"name": "UNFINISH", "source": "0000.rpl", "flags": "--time",
         "time": b"", "seed": b"1:00.00", "truncate": True, "exit": 0},
        {"name": "BADTIME", "source": "0000.rpl", "flags": "--time",
         "directory": True, "exit": 1},
    ]
    results = []
    with tempfile.TemporaryDirectory(prefix="repldump-time-dos-") as temporary:
        directory = Path(temporary)
        for source in args.data_dir.iterdir():
            if source.is_file():
                shutil.copyfile(source, directory / source.name.upper())
        shutil.copyfile(args.executable, directory / "REPLDUMP.EXE")
        commands = ["@echo off"]
        for case in cases:
            name = case["name"]
            recording = (args.fixtures_dir / case["source"]).read_bytes()
            if case.get("truncate"):
                recording = bytearray(recording[:1829])
                struct.pack_into("<H", recording, 24, 1)
            (directory / f"{name}.RPL").write_bytes(recording)
            if case.get("directory"):
                (directory / f"{name}.TIM").mkdir()
            elif "seed" in case:
                (directory / f"{name}.TIM").write_bytes(case["seed"])
            if "owoot" not in case:
                (directory / f"{name}.OWO").write_bytes(b"previous verdict")
            replay_argument = name + case.get("extension", "")
            command = f"REPLDUMP {replay_argument} {case['flags']}".rstrip()
            case["command"] = command
            commands += [f"{command} > {name}.LOG",
                         f"if errorlevel 1 echo NONZERO > {name}.ERR"]
        commands += ["echo DONE > DONE.TXT"]
        batch = ("\r\n".join(commands) + "\r\n").encode("ascii")
        (directory / "CHECK.BAT").write_bytes(batch)
        (args.output / "check.bat").write_bytes(batch)
        command = [args.dosbox, "-conf", str(repository / "tools/scripts/dosbox.proc.conf"),
                   "-c", f'mount c "{directory}"', "-c", "c:", "-c", "CHECK.BAT", "-c", "exit"]
        environment = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        with (args.output / "dosbox.log").open("w") as log:
            process = subprocess.Popen(command, cwd=directory, env=environment,
                                       stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + args.timeout
                while not (directory / "DONE.TXT").is_file():
                    if process.poll() is not None or time.monotonic() >= deadline:
                        break
                    time.sleep(0.1)
            finally:
                if process.poll() is None:
                    process.kill()
                process.wait()
            emulator_status = process.returncode
        completed = (directory / "DONE.TXT").is_file()
        for case in cases:
            name = case["name"]
            time_path = directory / f"{name}.TIM"
            actual_time = time_path.read_bytes() if time_path.is_file() else None
            owoot_path = directory / f"{name}.OWO"
            actual_owoot = owoot_path.read_bytes() if owoot_path.is_file() else None
            expected_owoot = case.get("owoot", b"previous verdict")
            status_path = directory / f"{name}.ERR"
            nonzero_status = status_path.is_file() and bool(status_path.read_text().strip())
            dump_exists = (directory / f"{name}.BNI").is_file()
            passed = (completed and nonzero_status == bool(case["exit"]) and
                      actual_owoot == expected_owoot)
            if case.get("directory"):
                passed = passed and time_path.is_dir() and not dump_exists
            else:
                passed = passed and actual_time == case["time"] and dump_exists
                if actual_time is not None:
                    (args.output / f"{name.lower()}.tim").write_bytes(actual_time)
            output = directory / f"{name}.LOG"
            if output.is_file():
                shutil.copyfile(output, args.output / f"{name.lower()}.log")
            results.append({"name": name.lower(), "command": case["command"],
                            "expected_exit_status": case["exit"], "passed": passed,
                            "time": actual_time.decode("ascii") if actual_time is not None else None,
                            "owoot": actual_owoot.decode("ascii") if actual_owoot is not None else None,
                            "dump_created": dump_exists})
    report = {"target": "16-bit DOS repldump under DOSBox", "dosbox": args.dosbox,
              "emulator_status": emulator_status, "batch_completed": completed,
              "executable_sha256": hashlib.sha256(args.executable.read_bytes()).hexdigest(),
              "passed": all(result["passed"] for result in results), "cases": results}
    (args.output / "verification.json").write_text(json.dumps(report, indent=2) + "\n")
    for result in results:
        print(f"{'PASS' if result['passed'] else 'FAIL'} {result['name']}: {result['time']!r}")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
