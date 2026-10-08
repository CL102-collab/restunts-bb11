#!/usr/bin/env python3
"""Benchmark scratch driving and human-replay refinement separately.

Golden replay inputs are extracted only into the requested output directory.
Every successful generated replay is played again before its score is accepted.
Times include the game's actual penalties, not the recording's padded length.
"""

import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time
import zipfile


INPUT_OFFSET = 1828
TRACK_OFFSET = 26
GAMESTATE_SIZE = 1120
# Stable non-default tracks covering every stock car and flexible power gear.
DEFAULT_CASES = ("ZCT1", "ZCT2", "ZCT3", "ZCT4", "ZCT5", "ZCT6", "ZCT7",
                 "ZCT8", "ZCT9", "ZCT10", "ZCT20", "ZCT68")


def resource_hashes(directory):
    extensions = {".RES", ".PRE", ".3SH", ".P3S", ".VSH", ".PVS", ".VCE",
                  ".SFX", ".FNT", ".PES", ".ESH", ".PAL"}
    return {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(directory.iterdir())
            if path.is_file() and path.suffix.upper() in extensions}


def replay_metadata(name, contents):
    if len(contents) < INPUT_OFFSET:
        raise ValueError(f"Truncated replay: {name}")
    rate, frames = struct.unpack_from("<HH", contents, 22)
    if len(contents) < INPUT_OFFSET + frames:
        raise ValueError(f"Truncated replay inputs: {name}")
    return {"replay": name, "car": contents[:4].decode("ascii"),
            "track": contents[13:22].split(b"\0", 1)[0].decode("ascii"),
            "fps": rate, "opponent": contents[6], "transmission": contents[5],
            "recording_frames": frames,
            "track_sha256": hashlib.sha256(contents[TRACK_OFFSET:INPUT_OFFSET]).hexdigest()}


def simulate(binary, data, source, car, environment, flags):
    result = subprocess.run([str(binary), "--data-dir", str(data), str(source), car,
                             "--verify", *flags], capture_output=True, text=True,
                            timeout=60, env=environment)
    try:
        report = json.loads(result.stdout)
    except ValueError:
        report = {"error": result.stdout + result.stderr}
    return result.returncode, report


def prepare_suite(args, environment, directory):
    inventory = []
    with zipfile.ZipFile(args.corpus) as archive:
        for name in archive.namelist():
            if name.lower().endswith(".rpl"):
                inventory.append(replay_metadata(name, archive.read(name)))
        directory.mkdir(parents=True, exist_ok=True)
        (directory.parent / "inventory.json").write_text(json.dumps(inventory, indent=2) + "\n")
        if args.suite:
            selections = json.loads(args.suite.read_text())
            if (not isinstance(selections, list) or not selections
                    or any(not isinstance(item, dict) or not all(isinstance(item.get(field), str) for field in
                           ("name", "replay", "car", "track_sha256")) for item in selections)):
                raise ValueError("--suite must contain a nonempty list of case objects with "
                                 "name, replay, car, and track_sha256")
            if args.case:
                requested = set(args.case)
                selections = [item for item in selections if item.get("name") in requested]
                if {item["name"] for item in selections} != requested:
                    raise ValueError("Some requested case names are absent from --suite")
        else:
            selections = [{"name": name} for name in args.case or DEFAULT_CASES]
        suite = []
        used_names = set()
        for selection in selections:
            name = selection["name"]
            if (not isinstance(name, str) or not name or name in (".", "..")
                    or "/" in name or "\\" in name or name in used_names):
                raise ValueError(f"Unsafe or duplicate case name: {name!r}")
            used_names.add(name)
            references = [item for item in inventory
                          if (item["replay"] == selection["replay"] if args.suite
                              else item["track"] == name)]
            if not references:
                raise ValueError(f"No matching golden replay for {name}")
            # A manifest selects exact track bytes/car, even when a name was
            # reused for different tracks. Human scores are always re-simulated.
            reference = references[0]
            if args.suite and any(selection[field] != reference[field]
                                  for field in ("car", "track_sha256")):
                raise ValueError(f"Suite metadata does not match its replay: {name}")
            candidates = [item for item in inventory
                          if item["track_sha256"] == reference["track_sha256"]
                          and item["car"] == reference["car"]
                          and item["fps"] == 20 and item["opponent"] == 0]
            verified = []
            for item in candidates:
                path = directory / Path(item["replay"]).name
                path.write_bytes(archive.read(item["replay"]))
                status, report = simulate(args.binary, args.data_dir, path, item["car"],
                                          environment, args.physics)
                verified.append({**item, "verify_exit": status, "verified": report})
            completed = [item for item in verified
                         if item["verify_exit"] == 0 and item["verified"].get("finished")]
            (directory.parent / f"{name}-humans.json").write_text(
                json.dumps(verified, indent=2) + "\n")
            if not completed:
                raise ValueError(f"No compatible completed human replay on {name}")
            best = min(completed, key=lambda item: (item["verified"]["total_frames"], item["replay"]))
            best = {**best, "name": name, "human_candidates": len(verified),
                    "human_successful": len(completed)}
            (directory / f"{name}.TRK").write_bytes(
                archive.read(best["replay"])[TRACK_OFFSET:INPUT_OFFSET])
            suite.append(best)
    (directory.parent / "suite.json").write_text(json.dumps(suite, indent=2) + "\n")
    return suite


def independent_playback(binary, data, replay, expected, environment, flags):
    """Check final serialized engine state using the separate repldump driver."""
    # repldump still has legacy DOS path limits. A short temporary name also
    # keeps those limits independent of a caller's benchmark output directory.
    with tempfile.TemporaryDirectory(prefix="ghbench-") as directory:
        source = Path(directory) / "GHOST.RPL"
        source.write_bytes(replay.read_bytes())
        result = subprocess.run([str(binary), "--data-dir", str(data), str(source), "1", *flags],
                                capture_output=True, text=True, timeout=60, env=environment,
                                cwd=directory)
        (replay.parent / "repldump.stdout.log").write_text(result.stdout)
        (replay.parent / "repldump.stderr.log").write_text(result.stderr)
        if result.returncode:
            raise ValueError(f"Independent replay driver failed ({result.returncode}): "
                             f"{replay}: {result.stderr}")
        dump = source.with_suffix(".BNI").read_bytes()
        replay.with_suffix(".BNI").write_bytes(dump)
        owo_passed = "--owoot" not in flags or source.with_suffix(".owo").read_text().strip() == "pass"
        if "--owoot" in flags:
            replay.with_suffix(".owo").write_text("pass" if owo_passed else "fail")
    count = struct.unpack_from("<H", dump)[0]
    recorded = struct.unpack_from("<H", replay.read_bytes(), 24)[0]
    if count != recorded or len(dump) != 2 + count * GAMESTATE_SIZE or count == 0:
        raise ValueError(f"Invalid state dump size: {replay}")
    state = dump[-GAMESTATE_SIZE:]
    total, finish, penalty = (struct.unpack_from("<H", state, offset)[0]
                              for offset in (0x142, 0x146, 0x14A))
    score = {"driver": "repldump", "finished": state[0x21B] == 3,
             "replay_frames": count, "total_frames": total,
             "finish_frame": finish, "penalty_frames": penalty}
    if not score["finished"] or any(score[field] != expected[field]
                                    for field in ("total_frames", "finish_frame", "penalty_frames")):
        raise ValueError(f"Independent playback mismatch: {replay}: {score}, expected {expected}")
    if not owo_passed:
        raise ValueError(f"Independent playback violates OWOOT: {replay}")
    return score


def benchmark(args, case, mode, environment, corpus):
    directory = args.output / args.label / f"{case['name']}-{case['car']}" / mode
    directory.mkdir(parents=True, exist_ok=True)
    replay = directory / "GHOST.RPL"
    for old in (replay, replay.with_name(replay.name + ".json")):
        old.unlink(missing_ok=True)
    source = corpus / (f"{case['name']}.TRK" if mode == "scratch" else Path(case["replay"]).name)
    command = [str(args.binary), "--data-dir", str(args.data_dir), str(source), case["car"],
               "--seed", str(args.seed), "--output", str(replay), *args.physics]
    if args.evaluations is not None:
        command += ["--evaluations", str(args.evaluations)]
    else:
        command += ["--seconds", str(args.seconds)]
    # A matching transmission keeps seeded and scratch runs comparable.
    command += ["--transmission", "auto" if case["transmission"] else "manual"]
    started = time.monotonic()
    with (directory / "stdout.log").open("w") as stdout, (directory / "stderr.log").open("w") as stderr:
        try:
            result = subprocess.run(command, cwd=directory, env=environment, stdout=stdout,
                                    stderr=stderr, timeout=args.seconds + 120 if args.evaluations is None
                                    else args.timeout)
            exit_code = result.returncode
        except subprocess.TimeoutExpired:
            exit_code = "timeout"
    result = {"case": case["name"], "car": case["car"], "mode": mode,
              "human_replay": case["replay"], "track_sha256": case["track_sha256"],
              "human_total_frames": case["verified"]["total_frames"],
              "human_total_seconds": case["verified"]["total_frames"] / 20,
              "command": command, "exit_code": exit_code,
              "elapsed_seconds": round(time.monotonic() - started, 3), "finished": False}
    if replay.is_file():
        report_path = replay.with_name(replay.name + ".json")
        result["report"] = json.loads(report_path.read_text()) if report_path.exists() else {}
        status, score = simulate(args.binary, args.data_dir, replay, case["car"], environment,
                                 args.physics)
        result["playback_verification"] = score
        result["verification_exit"] = status
        result["finished"] = status == 0 and score.get("finished", False)
        if result["finished"]:
            for field in ("finish_frame", "penalty_frames", "total_frames"):
                if score[field] != result["report"].get(field):
                    raise ValueError(f"Verification mismatch for {directory}: {field}")
            result["total_seconds"] = score["total_frames"] / 20
            result["human_gap_seconds"] = (score["total_frames"] - result["human_total_frames"]) / 20
            result["human_gap_percent"] = round(100 * (score["total_frames"] /
                                                        result["human_total_frames"] - 1), 3)
            if args.repldump:
                result["independent_driver_verification"] = independent_playback(
                    args.repldump, args.data_dir, replay, score, environment, args.physics)
    (directory / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    score_text = f"{result['total_seconds']:.2f}s ({result['human_gap_percent']:+.2f}%)" \
        if result["finished"] else "NO FINISH"
    print(f"{case['name']:7s} {case['car']} {mode:6s} {score_text}; "
          f"human {result['human_total_seconds']:.2f}s; exit {exit_code}", flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--repldump", type=Path,
                        help="Additionally validate successful outputs with the independent replay driver")
    parser.add_argument("--data-dir", required=True, type=Path)
    parser.add_argument("--corpus", type=Path, default=Path("tools/scripts/rpls_golden/replays.zip"))
    parser.add_argument("--output", type=Path, default=Path("build/golden-benchmark"))
    parser.add_argument("--label", default="current")
    parser.add_argument("--seconds", type=int, default=20)
    parser.add_argument("--evaluations", type=int)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--jobs", type=int, choices=(1, 2), default=2)
    parser.add_argument("--case", action="append", help="Golden embedded track name; may repeat")
    parser.add_argument("--suite", type=Path,
                        help="Exact replay/car/track JSON manifest from analyze-genghost-replays.py")
    parser.add_argument("--modes", nargs="+", choices=("scratch", "seeded"),
                        default=("scratch", "seeded"))
    parser.add_argument("--physics", action="append", default=[],
                        help="Engine rule flag, e.g. --physics=--owoot")
    args = parser.parse_args()
    for field in ("binary", "data_dir", "corpus", "output"):
        setattr(args, field, getattr(args, field).resolve())
    if args.repldump:
        args.repldump = args.repldump.resolve()
    if args.suite:
        args.suite = args.suite.resolve()
    if args.seconds <= 0 or (args.evaluations is not None and args.evaluations <= 0):
        parser.error("Search budgets must be positive")
    if not args.label or Path(args.label).name != args.label or args.label in (".", ".."):
        parser.error("--label must be a single directory name")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    environment = dict(os.environ, SDL_VIDEODRIVER="restunts-invalid-driver",
                       SDL_AUDIODRIVER="restunts-invalid-driver", RESTUNTS_AUDIO_TRACE="")
    resources = resource_hashes(args.data_dir)
    corpus = args.output / "corpus"
    suite = prepare_suite(args, environment, corpus)
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        futures = [pool.submit(benchmark, args, case, mode, environment, corpus)
                   for case in suite for mode in args.modes]
        results = [future.result() for future in futures]
    if resource_hashes(args.data_dir) != resources:
        raise ValueError("Game resources changed during the benchmark")
    report = {"binary": str(args.binary), "binary_sha256": hashlib.sha256(args.binary.read_bytes()).hexdigest(),
              "corpus_sha256": hashlib.sha256(args.corpus.read_bytes()).hexdigest(),
              "data_resource_sha256": resources,
              "repldump_sha256": hashlib.sha256(args.repldump.read_bytes()).hexdigest() if args.repldump else None,
              "suite": str(args.suite) if args.suite else None,
              "suite_sha256": hashlib.sha256(args.suite.read_bytes()).hexdigest() if args.suite else None,
              "comparison": {"human": "Best verified total for the same track bytes and car in this corpus",
                             "scratch": "Track bytes only; no human driving inputs",
                             "seeded": "The best matching human recording supplies starting driving inputs"},
              "seed": args.seed, "jobs": args.jobs, "seconds": None if args.evaluations else args.seconds,
              "evaluations": args.evaluations, "physics": args.physics, "results": results}
    (args.output / args.label / "results.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
