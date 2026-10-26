#!/usr/bin/env python3
"""Run the ported 16-bit DOS game's startup and driving settings under DOSBox."""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


def main():
    repository = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dosbox", default="dosbox-x")
    parser.add_argument("--data-dir", type=Path, default=repository / "stunts")
    parser.add_argument("--output", type=Path, default=repository / "out/dos16")
    parser.add_argument("--timeout", type=float, default=45)
    args = parser.parse_args()
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("--timeout must be finite and positive")
    executables = {
        "RESTUNTS.EXE": repository / "src/restunts/dos/restunts.exe",
        "OPTIONS.EXE": repository / "src/restunts/tests/build/watcom/release/OPTIONS.EXE",
        "DOSTAIL.EXE": repository / "src/restunts/tests/build/watcom/release/DOSTAIL.EXE",
    }
    for executable in executables.values():
        if not executable.is_file():
            parser.error(f"Missing {executable}; build restunts and test-driving-options first")
    if not any((directory / "CARTERR.RES").is_file()
               for directory in (args.data_dir, args.data_dir / "cars")):
        parser.error("--data-dir must contain the game data and installed Tesla car")
    args.output.mkdir(parents=True, exist_ok=True)

    # The full DOS command tail permits all 63 one-letter arguments. This catches
    # the old six-argument limit in the shared dump-tool startup, independently
    # of any game's interpretation of the individual strings.
    cases = [
        ("HELP", "C:\\RESTUNTS --help", 0, "--owoot"),
        ("VERSION", "C:\\RESTUNTS --version", 0, "Chocolate Stunts (Restunts)"),
        ("TAIL", "C:\\DOSTAIL " + " ".join(["x"] * 63), 0, "DOS command tail passed"),
        ("QUOTED", 'C:\\DOSTAIL quoted "one two" three', 0, "DOS command tail passed"),
        ("DEFAULT", "C:\\OPTIONS --data-dir D:\\ defaults", 0, "DOS driving options passed"),
        ("CFGON", "C:\\OPTIONS --data-dir D:\\ defaults --cfg:on", 0, "CFG surfaces: road/road"),
        ("CFGOFF", "C:\\OPTIONS --data-dir D:\\ cfgfixed --cfg:off", 0, "CFG surfaces: grass/road"),
        ("CFGCASE", "C:\\OPTIONS --data-dir D:\\ cfgfixed --CFG:OFF", 0, "CFG surfaces: grass/road"),
        ("CFGLAST", "C:\\OPTIONS --data-dir D:\\ defaults --cfg:off --CFG:ON", 0,
         "CFG surfaces: road/road"),
        ("CFGINVAL", "C:\\OPTIONS --data-dir D:\\ cfgfixed --cfg:off --cfg:invalid", 0,
         "CFG surfaces: grass/road"),
        ("DWSON", "C:\\OPTIONS --data-dir D:\\ defaults --dws:on", 0, "DWS penalties: 0/0 seconds"),
        ("DWSOFF", "C:\\OPTIONS --data-dir D:\\ dwsfixed --dws:off", 0, "DWS penalties: 18/24 seconds"),
        ("DWSLAST", "C:\\OPTIONS --data-dir D:\\ defaults --dws:off --DWS:ON", 0,
         "DWS penalties: 0/0 seconds"),
        ("DWSCASE", "C:\\OPTIONS --data-dir D:\\ dwsfixed --DWS:OFF", 0,
         "DWS penalties: 18/24 seconds"),
        ("OPTIN", "C:\\OPTIONS --data-dir D:\\ optin --pg:off --lc:off --lcb:off "
         "--amigagrass --owoot --nointro /ns /sPC /nd --fps:on",
         0, "DOS driving options passed"),
        ("OVERRIDE", "C:\\OPTIONS --data-dir D:\\ defaults --pg:off --lc:off --lcb:off "
         "--pg:on --lc:on --lcb:on --fps:off", 0, "DOS driving options passed"),
        ("SOUNDAD", "C:\\OPTIONS --data-dir D:\\ defaults /sAD", 0, "DOS driving options passed"),
        ("SOUNDSB", "C:\\OPTIONS --data-dir D:\\ defaults /sSB", 0, "DOS driving options passed"),
        ("DIRQUOTE", 'C:\\OPTIONS --data-dir "C:\\DATA" defaults', 0, "DOS driving options passed"),
        ("DIRHELP", "C:\\RESTUNTS --data-dir missing --help", 0, "--data-dir DIR"),
        ("MISSING", "C:\\RESTUNTS --data-dir missing", 1, "Cannot open --data-dir directory"),
        ("NOARG", "C:\\RESTUNTS --data-dir", 1, "Cannot open --data-dir directory"),
        ("OPERAND", "C:\\RESTUNTS --data-dir --help", 1, "Cannot open --data-dir directory"),
        ("FAILDRV", "C:\\RESTUNTS --data-dir D:\\missing", 1, "Cannot open --data-dir directory"),
        # This directory exists but lacks MISC.RES: initialization exits after
        # selecting it, exercising the central error-exit restoration hook.
        ("NODATA", "C:\\RESTUNTS --data-dir D:\\SAVE", 1, ""),
    ]
    results = []
    with tempfile.TemporaryDirectory(prefix="restunts-dos-options-") as temporary:
        directory = Path(temporary)
        data = directory / "DATA"
        data.mkdir()
        (directory / "SHELL").mkdir()
        (data / "SAVE").mkdir()
        for source in args.data_dir.iterdir():
            if source.is_file():
                shutil.copyfile(source, data / source.name.upper())
        if (args.data_dir / "cars").is_dir():
            shutil.copytree(args.data_dir / "cars", data / "cars")
        for name, source in executables.items():
            shutil.copyfile(source, directory / name)
        commands = []
        for name, command, status, _ in cases:
            commands += ["d:", "cd \\", "cd SAVE", "c:", "cd \\", "cd SHELL",
                         f"{command} > C:\\{name}.TXT",
                         f"if {'not ' if status else ''}errorlevel 1 echo FAILED > C:\\{name}.ERR",
                         f"cd > C:\\{name}.CWD", "d:", f"cd > C:\\{name}.DRV"]
        commands += ["echo DONE > C:\\DONE.TXT"]
        (directory / "CHECK.BAT").write_bytes(("\r\n".join(commands) + "\r\n").encode("ascii"))
        shutil.copyfile(directory / "CHECK.BAT", args.output / "settings-check.bat")
        environment = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        command = [args.dosbox, "-conf", str(repository / "tools/scripts/dosbox.proc.conf"),
                   "-c", f'mount c "{directory}"', "-c", f'mount d "{data}"',
                   "-c", "c:", "-c", "CHECK.BAT"]
        with (args.output / "settings-dosbox.log").open("w") as log:
            process = subprocess.Popen(command, cwd=directory, env=environment,
                                       stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + args.timeout
                while not (directory / "DONE.TXT").exists():
                    if process.poll() is not None or time.monotonic() >= deadline:
                        break
                    time.sleep(0.1)
            finally:
                if process.poll() is None:
                    process.kill()
                process.wait()
        completed = (directory / "DONE.TXT").is_file()
        for name, command, status, expected in cases:
            output_path = directory / f"{name}.TXT"
            output = output_path.read_text(errors="replace") if output_path.exists() else ""
            error_path = directory / f"{name}.ERR"
            error = error_path.read_text(errors="replace") if error_path.exists() else ""
            caller_path = directory / f"{name}.CWD"
            target_path = directory / f"{name}.DRV"
            caller_cwd = caller_path.read_text(errors="replace").strip() if caller_path.exists() else ""
            target_cwd = target_path.read_text(errors="replace").strip() if target_path.exists() else ""
            restored = caller_cwd.upper() == "C:\\SHELL" and target_cwd.upper() == "D:\\SAVE"
            passed = completed and expected in output and not error.strip() and restored
            (args.output / f"settings-{name.lower()}.txt").write_text(output)
            results.append({"name": name.lower(), "command": command,
                            "expected_exit_status": status,
                            "command_tail_bytes": len(command.split(" ", 1)[1]) + 1, "passed": passed,
                            "caller_directory_after_exit": caller_cwd,
                            "target_drive_directory_after_exit": target_cwd,
                            "output": output.strip()})
    report = {
        "target": "16-bit DOS Restunts port; original assembly executable unchanged",
        "dosbox": args.dosbox,
        "executables": {name: {"bytes": source.stat().st_size,
                                  "sha256": hashlib.sha256(source.read_bytes()).hexdigest()}
                        for name, source in executables.items()},
        "ceiling_and_wall_ticks_per_setting": 200 * 4 * 2 * 2,
        "zero_speed_adhesion": "automatic resource capability; real wheel contact solver, 10 and 20 Hz; fand loaded from CARTERR.RES",
        "batch_completed": completed,
        "passed": all(result["passed"] for result in results),
        "cases": results,
    }
    (args.output / "verification.json").write_text(json.dumps(report, indent=2) + "\n")
    for result in results:
        print(f"{'PASS' if result['passed'] else 'FAIL'} {result['name']}: {result['output'][:100]}")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
