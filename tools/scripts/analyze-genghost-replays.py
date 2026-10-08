#!/usr/bin/env python3
"""Rank golden replays with real physics and measure a disjoint human sample.

The archive is never changed. A deterministic split reserves entire track hashes
before selecting telemetry recordings. Validation tracks are only resimulated to
establish references; their inputs and state traces are not analyzed here.
"""

import argparse
import collections
import concurrent.futures
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import statistics
import struct
import subprocess
import tempfile
import zipfile


PRIOR_TRACKS = {f"ZCT{i}" for i in (*range(1, 11), 20, 68, 276, 284, 297, 300)}
STOCK_CARS = ("LM02", "ANSX", "FGTO", "JAGU", "COUN", "AUDI", "P962", "LANC",
              "PC04", "VETT", "PMIN")
STATE_SIZE = 1120
PLAYER = 0x152
# Compressed and raw encodings share one resource identity. Keep the loader's
# preferred encoding first when the supplied asset directory contains both.
CAR_RESOURCE_FAMILIES = ((".RES", ".PRE"), (".P3S", ".3SH"), (".PVS", ".VSH"))
# Model sizes and the small sound/font resources also affect the legacy penalty
# alias, even in a headless run. Hash all installed resources in these formats,
# including files shadowed by a preferred compressed/raw encoding.
DATA_RESOURCE_EXTENSIONS = frozenset((
    ".RES", ".PRE", ".P3S", ".3SH", ".PVS", ".VSH", ".PES", ".ESH",
    ".VCE", ".SFX", ".FNT", ".PAL",
))
FIELDS = {
    "x": (0, "i"), "y": (4, "i"), "z": (8, "i"),
    "yaw": (24, "h"), "pitch": (26, "h"), "roll": (28, "h"),
    "steering": (32, "h"), "rpm": (34, "h"), "speed": (44, "H"),
    "route": (74, "h"), "gear": (190, "B"), "front": (191, "B"),
    "rear": (192, "B"), "contact": (193, "B"),
    "surface0": (194, "B"), "surface1": (195, "B"),
    "surface2": (196, "B"), "surface3": (197, "B"),
    "sliding": (199, "B"), "changing_gear": (202, "B"),
}


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n")


def install_missing_car_assets(source, destination):
    """Preserve installed resources, including alternative compressed forms."""
    families = {suffix: family for family in CAR_RESOURCE_FAMILIES for suffix in family}
    existing = {path.name.upper() for path in destination.iterdir()}
    candidates = [path for path in source.iterdir()
                  if path.is_file() and path.suffix.upper() in families]
    candidates.sort(key=lambda path: (path.stem.upper(),
                                      families[path.suffix.upper()].index(path.suffix.upper()),
                                      path.name))
    copied = []
    for path in candidates:
        family = families[path.suffix.upper()]
        if any(path.stem.upper() + suffix in existing for suffix in family):
            continue
        name = path.name.upper()
        shutil.copyfile(path, destination / name)
        existing.add(name)
        copied.append(name)
    return copied


def data_resource_hashes(directory):
    return {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(directory.iterdir())
            if path.is_file() and path.suffix.upper() in DATA_RESOURCE_EXTENSIONS}


def run_provenance(args):
    return {"schema_version": 2,
            "binary_sha256": hashlib.sha256(args.binary.read_bytes()).hexdigest(),
            "repldump_sha256": hashlib.sha256(args.repldump.read_bytes()).hexdigest(),
            "corpus_sha256": hashlib.sha256(args.corpus.read_bytes()).hexdigest(),
            "data_resource_sha256": data_resource_hashes(args.data_dir)}


def stable_hash(value):
    return hashlib.sha256(("genghost-expanded-2026-v1:" + value).encode()).hexdigest()


def quantiles(values):
    if not values:
        return {}
    ordered = sorted(values)
    return {"count": len(ordered), "minimum": ordered[0],
            "median": statistics.median(ordered),
            "p90": ordered[min(len(ordered) - 1, int(len(ordered) * .9))],
            "maximum": ordered[-1]}


def intervals(values):
    """Return half-open ranges of nonzero samples."""
    result, start = [], None
    for index, value in enumerate((*values, False)):
        if value and start is None:
            start = index
        elif not value and start is not None:
            result.append((start, index))
            start = None
    return result


def replay_tile_id(replay, column, row):
    """Read an object using top-down route coordinates from bottom-up track bytes."""
    if not 0 <= column < 30 or not 0 <= row < 30:
        return None
    return replay[26 + (29 - row) * 30 + column]


def inventory(archive):
    result = []
    basenames = set()
    for name in archive.namelist():
        if not name.lower().endswith(".rpl"):
            continue
        # Corpus extraction and telemetry use basenames. Reject collisions
        # before extracting anything, including names differing only by case.
        basename = name.replace("\\", "/").rsplit("/", 1)[-1].casefold()
        if basename in basenames:
            raise ValueError(f"Duplicate replay basename in archive: {name}")
        basenames.add(basename)
        data = archive.read(name)
        if len(data) < 1828:
            continue
        fps, frames = struct.unpack_from("<HH", data, 22)
        result.append({"replay": name, "car": data[:4].decode("ascii"),
                       "track": data[13:22].split(b"\0")[0].decode("ascii"),
                       "transmission": data[5], "opponent": data[6],
                       "fps": fps, "recording_frames": frames,
                       "track_sha256": hashlib.sha256(data[26:1828]).hexdigest(),
                       "complete_file": len(data) >= 1828 + frames})
    return result


def verify(args, item, environment):
    source = args.output / "corpus" / Path(item["replay"]).name
    try:
        run = subprocess.run([str(args.binary), "--data-dir", str(args.data_dir),
                              str(source), item["car"], "--verify"],
                             capture_output=True, text=True, timeout=60, env=environment)
        try:
            report = json.loads(run.stdout)
        except ValueError:
            report = {"error": (run.stdout + run.stderr)[-2000:]}
        return {**item, "verify_exit": run.returncode, "verified": report}
    except subprocess.TimeoutExpired:
        return {**item, "verify_exit": "timeout", "verified": {"error": "timeout"}}


def select_cases(rows, reserved, count):
    groups = collections.defaultdict(list)
    for item in rows:
        if item["verify_exit"] == 0 and item["verified"].get("finished"):
            groups[item["track_sha256"]].append(item)
    cars = collections.defaultdict(list)
    for track_hash, members in groups.items():
        if track_hash not in reserved:
            continue
        ordered = sorted(members, key=lambda row: (row["verified"]["total_frames"], row["replay"]))
        best = ordered[0]
        case = {**best, "name": best["track"], "human_candidates": len(members),
                "human_successful": len(members), "members": [r["replay"] for r in ordered]}
        cars[best["car"]].append(case)
    for values in cars.values():
        values.sort(key=lambda case: stable_hash(case["track_sha256"]))
    # Only each exact track's overall winner supplies driving evidence. Car
    # diversity chooses between tracks; it never replaces a faster recording.
    order = [car for car in STOCK_CARS if car in cars] + sorted(set(cars) - set(STOCK_CARS))
    selected, used = [], set()
    while len(selected) < count:
        progress = False
        for car in order:
            while cars[car] and cars[car][0]["track_sha256"] in used:
                cars[car].pop(0)
            if cars[car]:
                candidate = cars[car].pop(0)
                selected.append(candidate)
                used.add(candidate["track_sha256"])
                progress = True
                if len(selected) == count:
                    break
        if not progress:
            break
    return selected


def trace_metrics(args, row, environment):
    source = args.output / "corpus" / Path(row["replay"]).name
    replay = source.read_bytes()
    with tempfile.TemporaryDirectory(prefix="ghhuman-") as directory:
        path = Path(directory) / "H.RPL"
        path.write_bytes(replay)
        run = subprocess.run([str(args.repldump), "--data-dir", str(args.data_dir), str(path), "1"],
                             capture_output=True, text=True, timeout=60,
                             cwd=directory, env=environment)
        if run.returncode:
            return {"replay": row["replay"], "error": run.stderr[-2000:], "exit": run.returncode}
        dump = path.with_suffix(".BNI").read_bytes()
    count = struct.unpack_from("<H", dump)[0]
    if len(dump) != 2 + count * STATE_SIZE:
        raise ValueError(f"Invalid dump size: {source}")
    final = dump[-STATE_SIZE:]
    actual = [struct.unpack_from("<H", final, offset)[0] for offset in (0x142, 0x146, 0x14A)]
    expected = [row["verified"][field] for field in ("total_frames", "finish_frame", "penalty_frames")]
    if actual != expected or final[0x21B] != 3:
        raise ValueError(f"Independent playback mismatch: {source}: {actual} != {expected}")
    length = min(count, row["verified"]["finish_frame"])
    fields = {name: [struct.unpack_from("<" + fmt, dump, 2 + i * STATE_SIZE + PLAYER + offset)[0]
                     for i in range(length)] for name, (offset, fmt) in FIELDS.items()}
    inputs = replay[1828:1828 + length]
    road_wheels = [sum(1 <= fields[f"surface{w}"][i] <= 3 for w in range(4))
                   for i in range(length)]
    any_grass = [any(fields[f"surface{w}"][i] == 4 for w in range(4)) for i in range(length)]
    all_grass = [all(fields[f"surface{w}"][i] == 4 for w in range(4)) for i in range(length)]
    air = [contact == 0 for contact in fields["contact"]]
    # Simulated state i is AFTER input i; the input and contact at i-1 are the
    # approach context for gear changes and threshold crossing events.
    high_speed = [speed > 220 * 256 for speed in fields["speed"]]
    first_fast = next((i for i, value in enumerate(high_speed) if value), None)
    shifts = []
    for i in range(1, length):
        if fields["gear"][i] != fields["gear"][i - 1]:
            shifts.append({"frame": i, "from": fields["gear"][i - 1], "to": fields["gear"][i],
                           "speed_mph": fields["speed"][i - 1] / 256,
                           "rpm": fields["rpm"][i - 1], "rear_contact": fields["rear"][i - 1],
                           "ground_contact": fields["contact"][i - 1]})
    position = [(fields["x"][i] / 64, fields["y"][i] / 64, fields["z"][i] / 64)
                for i in range(length)]
    distance = sum(math.hypot(b[0] - a[0], b[2] - a[2]) for a, b in zip(position, position[1:]))
    tiles, visits, entry = {}, [], 0
    previous = None
    for i, (x, y, z) in enumerate(position):
        column, zrow = int(x // 1024), int(z // 1024)
        key = (column, 29 - zrow)
        center_distance = math.hypot(x - (column + .5) * 1024, z - (zrow + .5) * 1024)
        item = tiles.setdefault(key, {"column": key[0], "row": key[1], "frames": 0,
                                     "nearest_center": center_distance, "road_contact_frames": 0})
        item["frames"] += 1
        item["nearest_center"] = min(item["nearest_center"], center_distance)
        item["road_contact_frames"] += road_wheels[i] > 0
        if key != previous:
            if previous is not None:
                visits.append({"tile": list(previous), "enter": entry, "leave": i,
                               "frames": i - entry})
            previous, entry = key, i
    if previous is not None:
        visits.append({"tile": list(previous), "enter": entry, "leave": length,
                       "frames": length - entry})
    for visit in visits:
        start, end = visit["enter"], visit["leave"]
        column, tile_row = visit["tile"]
        center_x, center_z = (column + .5) * 1024, (29 - tile_row + .5) * 1024
        visit["nearest_center"] = round(min(math.hypot(x - center_x, z - center_z)
                                            for x, _, z in position[start:end]), 2)
        visit["maximum_inset"] = round(max(min(x - column * 1024,
                                                (column + 1) * 1024 - x,
                                                z - (29 - tile_row) * 1024,
                                                (30 - tile_row) * 1024 - z)
                                            for x, _, z in position[start:end]), 2)
        visit["entry_speed_mph"] = fields["speed"][start] / 256
        visit["exit_speed_mph"] = fields["speed"][end - 1] / 256
        visit["road_contact_frames"] = sum(value > 0 for value in road_wheels[start:end])
    for item in tiles.values():
        item["nearest_center"] = round(item["nearest_center"], 2)
        column, rowindex = item["column"], item["row"]
        item["tile_id"] = replay_tile_id(replay, column, rowindex)
    penalties = [struct.unpack_from("<H", dump, 2 + i * STATE_SIZE + 0x14A)[0]
                 for i in range(length)]
    confirmed = [struct.unpack_from("<h", dump, 2 + i * STATE_SIZE + 0x2F2)[0]
                 for i in range(length)]
    confirmations = []
    for i in range(1, length):
        if confirmed[i] != confirmed[i - 1]:
            x, _, z = position[i]
            confirmations.append({"frame": i, "from": confirmed[i - 1], "to": confirmed[i],
                                  "delta": confirmed[i] - confirmed[i - 1],
                                  "penalty_before": penalties[i - 1], "penalty_after": penalties[i],
                                  "tile": [int(x // 1024), 29 - int(z // 1024)],
                                  "speed_mph": fields["speed"][i] / 256,
                                  "road_wheels": road_wheels[i],
                                  "tile_inset": round(min(x % 1024, 1024 - x % 1024,
                                                          z % 1024, 1024 - z % 1024), 2)})
    route_sequence = [fields["route"][0]] if length else []
    transitions = []
    for i in range(1, length):
        old, new = fields["route"][i - 1], fields["route"][i]
        if old != new:
            route_sequence.append(new)
            transitions.append({"frame": i, "from": old, "to": new,
                                "penalty_before": penalties[i - 1], "penalty_after": penalties[i]})
    pedal = {name: [bool(value & bit) for value in inputs] for name, bit in (("throttle", 1), ("brake", 2))}
    pedal["coast"] = [not (value & 3) for value in inputs]
    steering = [bool(value & 12) for value in inputs]
    pulse = {name: quantiles([end - start for start, end in intervals(values)])
             for name, values in (*pedal.items(), ("steering", steering))}
    grass_ranges = intervals(all_grass)
    grass_events = [{"start_frame": start, "end_frame": end,
                     "seconds": (end - start) / 20,
                     "penalty_delta_frames": penalties[end - 1] - penalties[max(0, start - 1)],
                     "entry_speed_mph": fields["speed"][start] / 256,
                     "exit_speed_mph": fields["speed"][end - 1] / 256,
                     "route_at_start": fields["route"][start], "route_at_end": fields["route"][end - 1]}
                    for start, end in grass_ranges if end - start >= 4]
    metrics = {"replay": row["replay"], "track": row["track"], "car": row["car"],
               "track_sha256": row["track_sha256"], "transmission": row["transmission"],
               "independent_playback_verified": True, **row["verified"],
               "horizontal_distance": round(distance, 2),
               "speed_max_mph": max(fields["speed"]) / 256,
               "speed_mean_mph": statistics.mean(fields["speed"]) / 256,
               "first_over_220_frame": first_fast,
               "over_220_frames": sum(high_speed),
               "air_frames": sum(air), "any_grass_frames": sum(any_grass),
               "all_grass_frames": sum(all_grass),
               "over_220_all_grass_frames": sum(a and b for a, b in zip(high_speed, all_grass)),
               "road_wheel_histogram": dict(collections.Counter(road_wheels)),
               "sliding_frames": sum(bool(s) for s in fields["sliding"]),
               "sliding_throttle_frames": sum(bool(s) and bool(k & 1) for s, k in zip(fields["sliding"], inputs)),
               "throttle_frames": sum(pedal["throttle"]), "brake_frames": sum(pedal["brake"]),
               "coast_frames": sum(pedal["coast"]), "steering_frames": sum(steering),
               "pulse_frames": pulse, "shifts": shifts, "tiles": list(tiles.values()),
               "tile_visits": visits, "route_sequence": route_sequence,
               "route_transitions": transitions, "route_confirmations": confirmations,
               "all_grass_events": grass_events}
    write_json(args.output / "telemetry" / (Path(row["replay"]).stem + ".json"), metrics)
    return metrics


def summarize(traces, cases):
    successful = [row for row in traces if row.get("independent_playback_verified")]
    total_frames = sum(row["finish_frame"] for row in successful)
    zero = [row for row in successful if row["penalty_frames"] == 0]
    by_replay = {row["replay"]: row for row in successful}
    comparisons = []
    for case in cases:
        members = sorted((by_replay[name] for name in case["members"] if name in by_replay),
                         key=lambda row: row["total_frames"])
        if len(members) < 2 or members[0]["total_frames"] == members[-1]["total_frames"]:
            continue
        best, slower = members[0], members[-1]
        comparisons.append({"track": case["name"], "car": case["car"],
                            "best": best["replay"], "slower": slower["replay"],
                            "best_transmission": best["transmission"],
                            "slower_transmission": slower["transmission"],
                            "time_saved_seconds": (slower["total_frames"] - best["total_frames"]) / 20,
                            "distance_saved_percent": round(100 * (1 - best["horizontal_distance"] /
                                                                      slower["horizontal_distance"]), 2),
                            "best_penalty": best["penalty_frames"], "slower_penalty": slower["penalty_frames"],
                            "best_grass_seconds": best["all_grass_frames"] / 20,
                            "slower_grass_seconds": slower["all_grass_frames"] / 20,
                            "best_visited_tiles": len(best["tiles"]), "slower_visited_tiles": len(slower["tiles"]),
                            "best_coast_seconds": best["coast_frames"] / 20,
                            "slower_coast_seconds": slower["coast_frames"] / 20})
    confirmations = [event for row in zero for event in row["route_confirmations"]]
    grazes = [visit for row in zero for visit in row["tile_visits"]
              if visit["road_contact_frames"] and visit["nearest_center"] > 512]
    return {"recordings": len(successful), "tracks": len({r["track_sha256"] for r in successful}),
            "cars": sorted({r["car"] for r in successful}), "zero_penalty_recordings": len(zero),
            "zero_penalty_with_all_grass": sum(row["all_grass_frames"] > 0 for row in zero),
            "zero_penalty_all_grass_seconds": quantiles([row["all_grass_frames"] / 20 for row in zero]),
            "aggregate_frame_percent": {field: round(100 * sum(row[field] for row in successful) / total_frames, 3)
                                        for field in ("throttle_frames", "brake_frames", "coast_frames",
                                                      "sliding_frames", "air_frames", "all_grass_frames")},
            "road_wheel_histogram": dict(sum((collections.Counter({int(k): v for k, v in r["road_wheel_histogram"].items()})
                                              for r in successful), collections.Counter())),
            "zero_penalty_confirmed_route_numeric_delta_histogram": dict(collections.Counter(event["delta"]
                                                                                             for event in confirmations)),
            "zero_penalty_route_confirmation_tile_inset": quantiles([event["tile_inset"] for event in confirmations]),
            "zero_penalty_road_contact_corner_grazes": len(grazes),
            "corner_graze_duration_frames": quantiles([visit["frames"] for visit in grazes]),
            "corner_graze_maximum_inset": quantiles([visit["maximum_inset"] for visit in grazes]),
            "paired_human_comparisons": comparisons,
            "interpretation_limits": [
                "Over 220 mph is a measured threshold, not proof of power gear for a custom car.",
                "All numeric route-index differences, including 2 and 3, can reflect graph numbering; "
                "they do not by themselves count skipped elements or prove skipped mandatory gates.",
                "Tile visits track the car center; wheel contact is recorded separately.",
                "Paired human runs are observational comparisons, not controlled causal experiments."]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--repldump", required=True, type=Path)
    parser.add_argument("--data-dir", required=True, type=Path)
    parser.add_argument("--corpus", type=Path, default=Path("tools/scripts/rpls_golden/replays.zip"))
    parser.add_argument("--car-assets", type=Path, help="Copy missing car resources into data-dir")
    parser.add_argument("--output", type=Path, default=Path("build/expanded-human"))
    parser.add_argument("--jobs", type=int, choices=(1, 2), default=2)
    parser.add_argument("--telemetry-tracks", type=int, default=80)
    parser.add_argument("--validation-tracks", type=int, default=16)
    parser.add_argument("--reuse-ranking", action="store_true")
    args = parser.parse_args()
    if args.telemetry_tracks <= 0 or args.validation_tracks <= 0:
        parser.error("Telemetry and validation track counts must be positive")
    for field in ("binary", "repldump", "data_dir", "corpus", "output"):
        setattr(args, field, getattr(args, field).resolve())
    args.output.mkdir(parents=True, exist_ok=True)
    if args.car_assets:
        install_missing_car_assets(args.car_assets, args.data_dir)
    environment = dict(os.environ, SDL_VIDEODRIVER="restunts-invalid-driver",
                       SDL_AUDIODRIVER="restunts-invalid-driver", RESTUNTS_AUDIO_TRACE="")
    with zipfile.ZipFile(args.corpus) as archive:
        items = inventory(archive)
        prior_hashes = {row["track_sha256"] for row in items if row["track"] in PRIOR_TRACKS}
        hashes = sorted({row["track_sha256"] for row in items} - prior_hashes, key=stable_hash)
        validation_hashes = set(hashes[:args.validation_tracks])
        tuning_hashes = set(hashes[args.validation_tracks:])
        write_json(args.output / "split.json", {"method": "Sort SHA256(genghost-expanded-2026-v1: + track_sha256)",
                                              "prior_track_hashes": sorted(prior_hashes),
                                              "validation_track_hashes": sorted(validation_hashes),
                                              "tuning_track_hashes": sorted(tuning_hashes)})
        eligible = [row for row in items if row["fps"] == 20 and row["opponent"] == 0 and row["complete_file"]]
        (args.output / "corpus").mkdir(exist_ok=True)
        for row in eligible:
            (args.output / "corpus" / Path(row["replay"]).name).write_bytes(archive.read(row["replay"]))
        write_json(args.output / "inventory.json", items)
        ranking_path = args.output / "ranking.json"
        provenance = run_provenance(args)
        if args.reuse_ranking and ranking_path.exists():
            cached = json.loads(ranking_path.read_text())
            if any(cached.get(field) != provenance[field] for field in
                   ("binary_sha256", "repldump_sha256", "corpus_sha256")):
                raise ValueError("Cached ranking has missing or different engine, replay driver, "
                                 "or archive hashes; rerun without --reuse-ranking")
            if cached.get("data_resource_sha256") != provenance["data_resource_sha256"]:
                raise ValueError("Cached ranking has missing or different data-resource hashes; "
                                 "rerun without --reuse-ranking")
            rows = cached["recordings"]
        else:
            with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
                rows = []
                for index, result in enumerate(pool.map(lambda row: verify(args, row, environment), eligible)):
                    rows.append(result)
                    if index % 500 == 0:
                        print(f"Verified {index + 1}/{len(eligible)}", flush=True)
            if run_provenance(args) != provenance:
                raise ValueError("Engine, archive, or data resources changed during ranking")
            write_json(ranking_path, {**provenance, "recordings": rows})
        write_json(args.output / "provenance.json", provenance)
        tuning = select_cases(rows, tuning_hashes, args.telemetry_tracks)
        validation = select_cases(rows, validation_hashes, args.validation_tracks)
        for label, cases in (("tuning", tuning), ("validation", validation)):
            for case in cases:
                safe_name = re.sub(r"[^A-Za-z0-9_.-]", "_", case["name"])
                stem = f"{safe_name}-{case['car']}-{case['track_sha256'][:8]}"
                case["case_id"] = stem
                path = args.output / "cases" / stem
                path.mkdir(parents=True, exist_ok=True)
                data = archive.read(case["replay"])
                (path / "TRACK.TRK").write_bytes(data[26:1828])
                (path / "HUMAN.RPL").write_bytes(data)
                case["track_path"] = str(path / "TRACK.TRK")
                case["replay_path"] = str(path / "HUMAN.RPL")
            write_json(args.output / f"{label}-manifest.json", cases)
    print(f"Manifests ready: {len(tuning)} tuning and {len(validation)} validation tracks", flush=True)
    by_replay = {row["replay"]: row for row in rows}
    selected = [by_replay[case["replay"]] for case in tuning]
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        traces = list(pool.map(lambda row: trace_metrics(args, row, environment), selected))
    if run_provenance(args) != provenance:
        raise ValueError("Engine, replay driver, archive, or data resources changed during analysis")
    summary = summarize(traces, tuning)
    write_json(args.output / "analysis.json", summary)
    print(json.dumps({key: value for key, value in summary.items()
                      if key not in ("paired_human_comparisons", "interpretation_limits")}, indent=2))


if __name__ == "__main__":
    main()
