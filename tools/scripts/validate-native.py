#!/usr/bin/env python3
"""Compare native replay and renderer dumps with the archived DOS oracles."""

import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import time
import zipfile

ROOT = Path(__file__).resolve().parents[2]
ORACLES = ROOT / "tools/oracles/borland"
DOS_POLL_INTERVAL_SECONDS = 0.05
DOS_SEGMENT_MAX = (1 << 16) - 1
REPLAY_OPPONENT_OFFSET = 6
spec = importlib.util.spec_from_file_location("toolchain_validation", Path(__file__).with_name("validate-toolchain.py"))
validation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(validation)


def fingerprint(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def dos_capture(args, directory, command, identifier):
    done = directory / "DONE.TXT"
    failed = directory / "FAILED.TXT"
    done.unlink(missing_ok=True)
    failed.unlink(missing_ok=True)
    environment = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    log_path = directory.parent / (identifier + "-dos.log")
    invocation = [args.dosbox, "-conf", str(ROOT / "tools/scripts/dosbox.proc.conf"),
                  "-c", "config -set cpu core=dynamic", "-c", "config -set cpu cycles=max",
                  "-c", f'mount c "{directory}"', "-c", "c:",
                  "-c", command + " > CAPTURE.TXT",
                  "-c", "if errorlevel 1 echo FAILED > FAILED.TXT",
                  "-c", "echo DONE > DONE.TXT"]
    with log_path.open("w") as log:
        process = subprocess.Popen(invocation, cwd=directory, env=environment,
                                   stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + args.timeout
            while not done.exists():
                if process.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError(f"DOS capture failed or timed out: {command}; see {log_path}")
                time.sleep(DOS_POLL_INTERVAL_SECONDS)
        finally:
            if process.poll() is None:
                process.kill()
            process.wait()
    capture = directory / "CAPTURE.TXT"
    if capture.exists():
        shutil.copyfile(capture, directory.parent / (identifier + "-dos.txt"))
    if failed.exists() and failed.read_text().strip():
        raise RuntimeError(f"DOS oracle returned failure: {command}")


def detect_oracle_psp(args, directory):
    # This 8086 .COM prints CS, which is its PSP, as four hexadecimal digits.
    # Match the oracle's 8.3 filename length so the DOS environment allocation
    # includes the same-sized executable path. DOSBox shell/environment options
    # can change this segment; native pointer addresses cannot tell us its value.
    code = bytearray([0x0e, 0x5b, 0xb9, 4, 0])  # push cs; pop bx; mov cx,4
    loop = len(code)
    code += bytes([0xd1, 0xc3] * 4)  # rol bx,1, four times
    code += bytes([0x88, 0xda, 0x80, 0xe2, 0x0f, 0x80, 0xc2, 0x30,
                   0x80, 0xfa, 0x39, 0x76, 3, 0x80, 0xc2, 7,
                   0xb4, 2, 0xcd, 0x21])  # format low nibble, DOS putchar
    code += bytes([0xe2, (loop - (len(code) + 2)) & 255,
                   0xb8, 0, 0x4c, 0xcd, 0x21])  # loop; DOS exit(0)
    (directory / "PSPDUMPO.COM").write_bytes(code)
    dos_capture(args, directory, "PSPDUMPO.COM", "oracle-psp")
    value = int((directory.parent / "oracle-psp-dos.txt").read_text().strip(), 16)
    if not 0 < value <= DOS_SEGMENT_MAX:
        raise ValueError("DOS oracle PSP probe returned an invalid segment")
    return hex(value)


def native_capture(args, directory, program, arguments, identifier):
    environment = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    environment["RESTUNTS_ORACLE_PSP_SEGMENT"] = args.oracle_psp
    environment["RESTUNTS_ORACLE_PROGRAM_PATH"] = "C:\\PIXLDUMP.EXE"
    with (directory.parent / (identifier + "-native.txt")).open("w") as log:
        subprocess.run([str(program), *arguments], cwd=directory, env=environment,
                       stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout, check=True)


def compare(reference, candidate):
    left, right = reference.read_bytes(), candidate.read_bytes()
    if not left or not right:
        raise ValueError(f"Empty dump: {reference} or {candidate}")
    if left != right:
        first = next((index for index, pair in enumerate(zip(left, right))
                      if pair[0] != pair[1]), min(len(left), len(right)))
        raise ValueError(f"Dump mismatch at byte {first}: {reference.name} ({len(left)} bytes), "
                         f"{candidate.name} ({len(right)} bytes)")
    return {"bytes": len(left), "sha256": fingerprint(reference)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-directory", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="New directory; never reuses old dumps")
    parser.add_argument("--count", type=int, default=3)
    parser.add_argument("--replay", action="append", default=[], help="Exact corpus replay name; repeatable")
    parser.add_argument("--camera", type=int, choices=range(1, 5), default=2)
    parser.add_argument("--target", type=int, choices=(0, 1), default=0)
    parser.add_argument("--dosbox", default="dosbox")
    parser.add_argument("--timeout", type=float, default=980)
    parser.add_argument("--oracle-psp", help="Override the automatically measured oracle PSP segment")
    args = parser.parse_args()
    if args.count < 1 or not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("--count and --timeout must be finite and positive")
    args.output = args.output.resolve()
    args.build_directory = args.build_directory.resolve()
    programs = {name: args.build_directory / (name + (".exe" if os.name == "nt" else ""))
                for name in ("repldump", "pixldump")}
    for program in programs.values():
        if not program.is_file():
            parser.error(f"Missing native executable: {program}")
    args.output.mkdir(parents=True, exist_ok=False)
    game = args.output / "game"
    game.mkdir()
    validation.copy_assets(ROOT / "stunts", game)
    validation.copy_assets(ROOT / "tools/scripts/cars", game)
    validation.copy_assets(ROOT / "assets/cars", game)
    for line in (ORACLES / "SHA256SUMS").read_text().splitlines():
        expected, filename = line.split()
        source = ORACLES / filename
        if fingerprint(source) != expected:
            raise ValueError(f"Oracle checksum mismatch: {source}")
        shutil.copyfile(source, game / filename.upper())
    if args.oracle_psp is None:
        args.oracle_psp = detect_oracle_psp(args, game)
    corpus = ROOT / "tools/scripts/rpls_golden/replays.zip"
    with zipfile.ZipFile(corpus) as archive:
        eligible = sorted(name for name in archive.namelist()
                          if Path(name).name == name and name.lower().endswith(".rpl")
                          and (not args.target or archive.read(name)[REPLAY_OPPONENT_OFFSET]))
        names_by_case = {name.upper(): name for name in eligible}
        if args.replay:
            missing = [name for name in args.replay if name.upper() not in names_by_case]
            if missing:
                parser.error("Unknown or ineligible replay: " + ", ".join(missing))
            names = [names_by_case[name.upper()] for name in args.replay]
        else:
            count = min(args.count, len(eligible))
            names = [eligible[index * len(eligible) // count] for index in range(count)]
        if not names:
            raise ValueError("No eligible replays")
        for name in names:
            (game / name.upper()).write_bytes(archive.read(name))
    manifest = {
        "native": {name: fingerprint(path) for name, path in programs.items()},
        "oracles": {path.name: fingerprint(path) for path in sorted(game.glob("*.EXE"))},
        "assets": {path.name: fingerprint(path) for path in sorted(game.iterdir())
                   if path.suffix.lower() in validation.ASSET_EXTENSIONS},
        "replays": {name: fingerprint(game / name.upper()) for name in names},
        "camera": args.camera, "target": args.target, "oracle_psp": args.oracle_psp,
        "results": {},
    }
    report = args.output / "results.json"
    report.write_text(json.dumps(manifest, indent=2) + "\n")
    for name in names:
        stem = Path(name).stem.upper()
        for kind, oracle, reference_ext, candidate_ext, arguments in (
            ("repldump", "REPLDUMO.EXE", "BIN", "BNI", [stem, "1"]),
            ("pixldump", "PIXLDUMO.EXE", "PDO", "PDD", [stem, str(args.camera), str(args.target)]),
        ):
            identifier = f"{stem}-{kind}"
            dos_capture(args, game, " ".join([oracle, *arguments]), identifier)
            native_capture(args, game, programs[kind], arguments, identifier)
            manifest["results"][identifier] = compare(game / f"{stem}.{reference_ext}",
                                                       game / f"{stem}.{candidate_ext}")
            report.write_text(json.dumps(manifest, indent=2) + "\n")
            print(f"PASS {identifier}: {manifest['results'][identifier]['bytes']} bytes", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
