#!/usr/bin/env python3
"""Measure stunt approaches in new, independently replayed human recordings.

Reserve disjoint track hashes before inspecting their geometry or telemetry. The
old ranking is used only to discover cases; all selected reference candidates
are replayed with the current binary before measuring their inputs or states.
Only the fastest verified replay per exact track hash is analyzed, across cars;
equal-best ties use a deterministic archive filename order.
"""

import argparse
import collections
import concurrent.futures
import hashlib
import importlib.util
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

SPEC = importlib.util.spec_from_file_location(
    "human_replays", Path(__file__).with_name("analyze-genghost-replays.py"))
HUMAN = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HUMAN)
MODELS = {10: "highway_entrance", 11: "highway", 16: "ramp", 17: "solid_ramp",
          18: "elevated_road", 19: "elevated_span", 20: "solid_road",
          21: "elevated_corner", 22: "overpass", 23: "banked_entrance",
          24: "banked_entrance", 25: "banked_road", 26: "banked_corner",
          27: "loop", 28: "tunnel", 29: "pipe_entrance", 30: "pipe",
          31: "half_pipe", 32: "corkscrew_up", 33: "corkscrew_down",
          34: "slalom", 35: "corkscrew_lr"}
TARGETS = {11, 18, 27, 32, 33, 34, 35}


def stable(value):
    return hashlib.sha256(("genghost-stunts-2026-v1:" + value).encode()).hexdigest()


def input_provenance(args):
    return {**HUMAN.run_provenance(args),
            "metadata_source_sha256": hashlib.sha256(args.track_metadata_source.read_bytes()).hexdigest()}


def metadata(path):
    source = path.read_text().split("struct TRACKOBJECT trkObjectList[215] = {", 1)[1].split("};", 1)[0]
    result = []
    for body in re.findall(r"\{([^{}]+)\}", source):
        values = body.split(",")
        result.append({"element": len(result), "orientation": int(values[1]),
                       "multi": int(values[7]), "model": int(values[8])})
    if len(result) != 215:
        raise ValueError("Track object metadata schema changed")
    return result


def local(x, z, orientation):
    return {0: (x, z), 256: (-z, x), 512: (-x, -z), 768: (z, -x)}[orientation]


def tile_object(track, objects, x, z):
    # Element rows run from low to high world z; terrain rows are reversed.
    col, row = int(x // 1024), int(z // 1024)
    if not (0 <= row < 30 and 0 <= col < 30):
        return None
    element = track[row * 30 + col]
    if element in (253, 254):
        row += 1
    if element in (253, 255):
        col -= 1
    if not (0 <= row < 30 and 0 <= col < 30):
        return None
    element = track[row * 30 + col]
    if not (0 < element < len(objects)):
        return None
    obj = objects[element]
    center_x = (col + (1 if obj["multi"] & 2 else .5)) * 1024
    center_z = (row + (0 if obj["multi"] & 1 else .5)) * 1024
    lx, lz = local(x - center_x, z - center_z, obj["orientation"])
    terrain = track[901 + (29 - row) * 30 + col]
    return {**obj, "column": col, "row": 29 - row, "raw_row": row, "center_x": center_x,
            "center_z": center_z, "local_x": lx, "local_z": lz,
            "terrain": terrain, "base_y": 450 if terrain == 6 else 0}


def select(args, inventory, old_rows, archive, objects):
    excluded = {row["track_sha256"] for row in inventory if row["track"] in HUMAN.PRIOR_TRACKS}
    for path in args.exclude_manifest:
        excluded.update(row["track_sha256"] for row in json.loads(path.read_text()))
    hashes = sorted({row["track_sha256"] for row in inventory} - excluded, key=stable)
    reserved = set(hashes[:args.validation_tracks])
    available = set(hashes[args.validation_tracks:])
    HUMAN.write_json(args.output / "split.json", {
        "method": "Sort SHA256(genghost-stunts-2026-v1: + track_sha256) before inspecting geometry or traces",
        "excluded_track_hashes": sorted(excluded), "validation_track_hashes": sorted(reserved),
        "available_tuning_track_hashes": sorted(available)})
    # Discovery only: current reference times are established by fresh replay.
    completed = [row for row in old_rows if row.get("verify_exit") == 0 and row["verified"].get("finished")]
    validation = HUMAN.select_cases(completed, reserved, args.validation_tracks)
    by_track = collections.defaultdict(list)
    for row in completed:
        if row["track_sha256"] in available:
            by_track[row["track_sha256"]].append(row)
    info = {}
    for track_hash, rows in by_track.items():
        track = archive.read(rows[0]["replay"])[26:1828]
        counts = collections.Counter(objects[tile]["model"] for tile in track[:900] if tile < len(objects))
        info[track_hash] = counts
    selected, used, car_counts = [], set(), collections.Counter()
    # Rotate targeted model priorities, favor broad car coverage and rich maps.
    for index in range(args.telemetry_tracks):
        target = sorted(TARGETS)[index % len(TARGETS)]
        options = [key for key in by_track if key not in used and info[key][target]]
        if not options:
            options = [key for key in by_track if key not in used and set(info[key]) & TARGETS]
        if not options:
            break
        key = min(options, key=lambda k: (min(car_counts[r["car"]] for r in by_track[k]),
                  -sum(bool(info[k][model]) for model in TARGETS), stable(k)))
        groups = collections.defaultdict(list)
        for row in by_track[key]:
            groups[row["car"]].append(row)
        car = min(groups, key=lambda c: (car_counts[c], -len(groups[c]), c))
        rows = sorted(groups[car], key=lambda r: (r["verified"]["total_frames"], r["replay"]))
        selected.append({**rows[0], "name": rows[0]["track"], "members": [r["replay"] for r in rows],
                         "discovery_model_counts": dict(info[key])})
        used.add(key)
        car_counts[car] += 1
    HUMAN.write_json(args.output / "discovery-tuning.json", selected)
    HUMAN.write_json(args.output / "discovery-validation.json", validation)
    return selected, validation


def best_track_replays(rows):
    """One overall winner per exact track, using real time plus penalties."""
    grouped = collections.defaultdict(list)
    for row in rows:
        if row["verify_exit"] == 0 and row["verified"].get("finished"):
            grouped[row["track_sha256"]].append(row)
    return {track_hash: min(members, key=lambda row: (row["verified"]["total_frames"], row["replay"]))
            for track_hash, members in grouped.items()}


def prepare_cases(args, discovery, all_rows, archive):
    by_track = collections.defaultdict(list)
    for row in all_rows:
        by_track[row["track_sha256"]].append(row)
    winners = best_track_replays(all_rows)
    cases = []
    for case in discovery:
        track_hash = case["track_sha256"]
        if track_hash not in winners:
            continue
        best = winners[track_hash]
        rows = sorted((row for row in by_track[track_hash]
                       if row["verify_exit"] == 0 and row["verified"].get("finished")),
                      key=lambda row: (row["verified"]["total_frames"], row["replay"]))
        stem = f"{re.sub(r'[^A-Za-z0-9_.-]', '_', best['track'])}-{best['car']}-{track_hash[:8]}"
        directory = args.output / "cases" / stem
        directory.mkdir(parents=True, exist_ok=True)
        data = archive.read(best["replay"])
        (directory / "TRACK.TRK").write_bytes(data[26:1828])
        (directory / "HUMAN.RPL").write_bytes(data)
        cases.append({**best, "name": best["track"], "case_id": stem,
                      "reference_selection": "fastest_verified_per_track_across_cars",
                      "human_candidates": len(by_track[track_hash]), "human_successful": len(rows),
                      "members": [row["replay"] for row in rows],
                      "track_path": str(directory / "TRACK.TRK"), "replay_path": str(directory / "HUMAN.RPL")})
    return cases


def segment_box(first, second, bounds):
    low, high = 0.0, 1.0
    for a, b, (minimum, maximum) in zip(first, second, bounds):
        delta = b - a
        if abs(delta) < 1e-12:
            if a < minimum or a > maximum:
                return False
        else:
            t0, t1 = sorted(((minimum - a) / delta, (maximum - a) / delta))
            low, high = max(low, t0), min(high, t1)
            if low > high:
                return False
    return True


def wheel_contact_count(surfaces):
    # Native car_sumSurfAllWheels sums material codes, rather than wheel count:
    # one dirt contact and two paved contacts both produce the value two.
    return sum(surface != 0 for surface in surfaces)


def classify_contacts(event):
    """Check swept wheel points and actual highway median/road occupancy."""
    if event["model"] not in (11, 34):
        return
    cx, cy, cz = event["origin"]
    previous, stone_hits, straddles = None, [], []
    boxes = (((23, 97), (0, 44), (-271, -241)), ((-97, -23), (0, 44), (241, 271)))
    for sample in event["samples"]:
        wheels = []
        for x, y, z in sample["wheels"]:
            lx, lz = local(x - cx, z - cz, event["orientation"])
            wheels.append((lx, y - cy, lz))
        if event["model"] == 34 and previous:
            for wheel, (first, second) in enumerate(zip(previous, wheels)):
                if any(segment_box(first, second, box) for box in boxes):
                    stone_hits.append({"frame": sample["frame"], "wheel": wheel})
        if event["model"] == 11:
            supported = [wheel for wheel, surface in zip(wheels, sample["surfaces"]) if surface != 0]
            median = [wheel for wheel in supported if abs(wheel[0]) <= 120]
            road = [wheel for wheel in supported if 120 < abs(wheel[0]) <= 360]
            if len(median) == 2 and len(road) == 2:
                straddles.append({"frame": sample["frame"], "speed_mph": sample["speed_mph"],
                                  "local_center": sample["local"], "roll": sample["roll"],
                                  "surfaces": sample["surfaces"],
                                  "median_wheel_height": statistics.mean(wheel[1] for wheel in median),
                                  "road_wheel_height": statistics.mean(wheel[1] for wheel in road)})
        previous = wheels
    if event["model"] == 34:
        event["stone_swept_wheel_crossings"] = stone_hits
    else:
        event["median_straddle_samples"] = straddles


def trace(args, row, objects, environment):
    source = args.output / "corpus" / Path(row["replay"]).name
    replay = source.read_bytes()
    with tempfile.TemporaryDirectory(prefix="ghstunt-") as directory:
        path = Path(directory) / "H.RPL"
        path.write_bytes(replay)
        run = subprocess.run([str(args.repldump), "--data-dir", str(args.data_dir), str(path), "1"],
                             capture_output=True, text=True, timeout=60, cwd=directory, env=environment)
        if run.returncode:
            return {"replay": row["replay"], "error": run.stderr[-2000:], "exit": run.returncode}
        dump = path.with_suffix(".BNI").read_bytes()
    count = struct.unpack_from("<H", dump)[0]
    if len(dump) != 2 + count * HUMAN.STATE_SIZE:
        raise ValueError(f"Invalid dump size: {source}")
    final = dump[-HUMAN.STATE_SIZE:]
    actual = [struct.unpack_from("<H", final, offset)[0] for offset in (0x142, 0x146, 0x14A)]
    expected = [row["verified"][field] for field in ("total_frames", "finish_frame", "penalty_frames")]
    if actual != expected or final[0x21B] != 3:
        raise ValueError(f"Independent playback mismatch: {source}: {actual} != {expected}")
    count = min(count, row["verified"]["finish_frame"])
    fields = {name: [struct.unpack_from("<" + fmt, dump, 2 + i * HUMAN.STATE_SIZE + HUMAN.PLAYER + offset)[0]
                     for i in range(count)] for name, (offset, fmt) in HUMAN.FIELDS.items()}
    fields["penalty"] = [struct.unpack_from("<H", dump, 2 + i * HUMAN.STATE_SIZE + 0x14A)[0] for i in range(count)]
    fields["confirmed"] = [struct.unpack_from("<h", dump, 2 + i * HUMAN.STATE_SIZE + 0x2F2)[0] for i in range(count)]
    fields["input"] = list(replay[1828:1828 + count])
    fields["collision"] = [dump[2 + i * HUMAN.STATE_SIZE + HUMAN.PLAYER + 200] for i in range(count)]
    fields["crash"] = [dump[2 + i * HUMAN.STATE_SIZE + HUMAN.PLAYER + 201] for i in range(count)]
    track = replay[26:1828]
    positions = [(fields["x"][i] / 64, fields["y"][i] / 64, fields["z"][i] / 64) for i in range(count)]
    elements = [tile_object(track, objects, x, z) for x, y, z in positions]
    keys = [(obj["column"], obj["row"], obj["model"]) if obj and obj["model"] in MODELS else None for obj in elements]
    events, begin, previous = [], 0, None
    for i, key in enumerate([*keys, None]):
        if key != previous:
            if previous is not None:
                events.append((begin, i, previous))
            begin, previous = i, key
    def sample(i):
        x, y, z = positions[i]
        obj = elements[i]
        wheels = [struct.unpack_from("<hhh", dump, 2 + i * HUMAN.STATE_SIZE + HUMAN.PLAYER + 116 + w * 6)
                  for w in range(4)]
        return {"frame": i, "position": [x, y, z], "speed_mph": fields["speed"][i] / 256,
                "yaw": fields["yaw"][i], "pitch": fields["pitch"][i], "roll": fields["roll"][i],
                "steering": fields["steering"][i], "rpm": fields["rpm"][i], "gear": fields["gear"][i],
                "front": fields["front"][i], "rear": fields["rear"][i], "contact": fields["contact"][i],
                "surfaces": [fields[f"surface{w}"][i] for w in range(4)], "wheels": wheels,
                "sliding": fields["sliding"][i], "input": fields["input"][i],
                "collision": fields["collision"][i], "confirmed": fields["confirmed"][i],
                "penalty": fields["penalty"][i],
                "local": [obj["local_x"], y - obj["base_y"], obj["local_z"]] if obj else None}
    output_events = []
    for begin, end, key in events:
        obj = elements[begin]
        samples = [sample(i) for i in range(begin, end)]
        entry = max(0, begin - 1)
        exit_frame = end - 1
        dx, dz = local(positions[min(begin + 1, count - 1)][0] - positions[entry][0],
                       positions[min(begin + 1, count - 1)][2] - positions[entry][2], obj["orientation"])
        event = {"model": obj["model"], "type": MODELS[obj["model"]], "element": obj["element"],
                 "orientation": obj["orientation"], "tile": list(key[:2]), "terrain": obj["terrain"],
                 "origin": [obj["center_x"], obj["base_y"], obj["center_z"]],
                 "start_frame": begin, "end_frame": end, "seconds": (end - begin) / 20,
                 "entry_speed_mph": fields["speed"][entry] / 256,
                 "exit_speed_mph": fields["speed"][exit_frame] / 256,
                 "local_entry_angle_degrees": math.degrees(math.atan2(dx, dz)),
                 "height_min": min(s["local"][1] for s in samples),
                 "height_max": max(s["local"][1] for s in samples),
                 "ground_frames": sum(fields["contact"][i] > 0 for i in range(begin, end)),
                 "air_frames": sum(fields["contact"][i] == 0 for i in range(begin, end)),
                 "two_contact_frames": sum(wheel_contact_count(fields[f"surface{w}"][i] for w in range(4)) == 2
                                           for i in range(begin, end)),
                 "penalty_delta_frames": fields["penalty"][exit_frame] - fields["penalty"][entry],
                 "throttle_frames": sum(bool(fields["input"][i] & 1) for i in range(begin, end)),
                 "brake_frames": sum(bool(fields["input"][i] & 2) for i in range(begin, end)),
                 "steering_frames": sum(bool(fields["input"][i] & 12) for i in range(begin, end)),
                 "local_x_min": min(s["local"][0] for s in samples),
                 "local_x_max": max(s["local"][0] for s in samples),
                 "local_z_min": min(s["local"][2] for s in samples),
                 "local_z_max": max(s["local"][2] for s in samples), "samples": samples}
        if obj["model"] == 34:
            boxes = (((23, 97), (0, 44), (-271, -241)), ((-97, -23), (0, 44), (241, 271)))
            crossing = []
            # Center segment intersections are direct evidence; wheel envelope
            # intersections are separate because the car can straddle a stone.
            for i in range(max(1, begin), min(count, end + 1)):
                a, b = positions[i - 1], positions[i]
                la = local(a[0] - obj["center_x"], a[2] - obj["center_z"], obj["orientation"])
                lb = local(b[0] - obj["center_x"], b[2] - obj["center_z"], obj["orientation"])
                first, second = (la[0], a[1] - obj["base_y"], la[1]), (lb[0], b[1] - obj["base_y"], lb[1])
                if any(segment_box(first, second, box) for box in boxes):
                    crossing.append(i)
            event["stone_center_segment_crossing_frames"] = crossing
        classify_contacts(event)
        output_events.append(event)
    airborne = []
    for begin, end in HUMAN.intervals([v == 0 for v in fields["contact"]]):
        if end - begin < 4:
            continue
        before, after = max(0, begin - 1), min(count - 1, end)
        obj = elements[before]
        dx = positions[after][0] - positions[before][0]
        dz = positions[after][2] - positions[before][2]
        airborne.append({"start_frame": begin, "end_frame": end, "seconds": (end - begin) / 20,
                        "launch_model": obj["model"] if obj else None,
                        "launch_type": MODELS.get(obj["model"], "other") if obj else "empty",
                        "launch_orientation": obj["orientation"] if obj else None,
                        "horizontal_displacement": math.hypot(dx, dz),
                        "entry_speed_mph": fields["speed"][before] / 256,
                        "exit_speed_mph": fields["speed"][after] / 256,
                        "maximum_y": max(y for x, y, z in positions[begin:end]),
                        "penalty_delta_frames": fields["penalty"][after] - fields["penalty"][before],
                        "gear_changes": sum(fields["gear"][i] != fields["gear"][i - 1] for i in range(max(1, begin), end)),
                        "throttle_frames": sum(bool(fields["input"][i] & 1) for i in range(begin, end)),
                        "brake_frames": sum(bool(fields["input"][i] & 2) for i in range(begin, end)),
                        "steering_frames": sum(bool(fields["input"][i] & 12) for i in range(begin, end)),
                        "launch": sample(before), "landing": sample(after)})
    result = {"replay": row["replay"], "track": row["track"], "car": row["car"],
              "track_sha256": row["track_sha256"], "transmission": row["transmission"],
              "independent_playback_verified": True, **row["verified"],
              "horizontal_distance": sum(math.hypot(b[0] - a[0], b[2] - a[2]) for a, b in zip(positions, positions[1:])),
              "events": output_events, "airborne": airborne,
              "input_pulse_frames": {name: HUMAN.quantiles([end - start for start, end in HUMAN.intervals(values)])
                   for name, values in (("throttle", [bool(v & 1) for v in fields["input"]]),
                                        ("brake", [bool(v & 2) for v in fields["input"]]),
                                        ("steering", [bool(v & 12) for v in fields["input"]]),
                                        ("coast", [not(v & 3) for v in fields["input"]]))}}
    HUMAN.write_json(args.output / "telemetry" / (Path(row["replay"]).stem + ".json"), result)
    # Full raw columns are separate from the compact summary/event index.
    HUMAN.write_json(args.output / "frames" / (Path(row["replay"]).stem + ".json"), fields)
    return result


def summarize(traces, cases):
    success = [row for row in traces if row.get("independent_playback_verified")]
    indexed = [{"replay": row["replay"], "track": row["track"], "car": row["car"],
                "total_frames": row["total_frames"], "penalty_frames": row["penalty_frames"],
                **{key: value for key, value in event.items() if key != "samples"}}
               for row in success for event in row["events"]]
    air = [{"replay": row["replay"], "track": row["track"], "car": row["car"],
            "total_frames": row["total_frames"], "penalty_frames": row["penalty_frames"], **event}
           for row in success for event in row["airborne"]]
    crossings = [event for event in indexed if event.get("stone_center_segment_crossing_frames")]
    long_jumps = [event for event in air if event["launch_model"] in (27, 32, 33)
                  and event["horizontal_displacement"] >= 2048]
    straddles = [event for event in indexed if len(event.get("median_straddle_samples", [])) >= 4]
    return {"recordings": len(success), "tracks": len({row["track_sha256"] for row in success}),
            "driving_frames": sum(row["finish_frame"] for row in success),
            "selection_mode": "fastest_verified_per_track_across_cars",
            "slalom_center_crossing_events": len(crossings),
            "slalom_center_and_wheel_crossing_events": sum(bool(event["stone_swept_wheel_crossings"])
                                                          for event in crossings),
            "slalom_center_crossing_entry_speed_mph": HUMAN.quantiles([event["entry_speed_mph"] for event in crossings]),
            "loop_corkscrew_flights_at_least_two_tiles": len(long_jumps),
            "loop_corkscrew_long_flight_tracks": len({event["track"] for event in long_jumps}),
            "loop_corkscrew_long_flight_displacement": HUMAN.quantiles([event["horizontal_displacement"] for event in long_jumps]),
            "highway_median_straddle_events_at_least_four_frames": len(straddles),
            "cars": sorted({row["car"] for row in success}),
            "zero_penalty_recordings": sum(row["penalty_frames"] == 0 for row in success),
            "event_types": dict(collections.Counter(event["type"] for event in indexed)),
            "events": indexed, "airborne": air,
            "interpretation_limits": [
                "Center position in a stunt tile does not prove that its intended surface was followed.",
                "Route index differences are not counts of skipped pieces across branches.",
                "A two-contact frame does not identify which side is supported without wheel-position analysis.",
                "No-contact jumps can originate beside a stunt; launch samples must be inspected.",
                "Height relative to terrain uses flat/raised anchor terrain; hill slopes require the native sampler.",
                "Only one deterministic overall winner per exact track hash supplies driving evidence.",
                "Different winners can use different car resources; speeds and handling do not transfer universally."]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--repldump", required=True, type=Path)
    parser.add_argument("--data-dir", required=True, type=Path)
    parser.add_argument("--corpus", default=Path("tools/scripts/rpls_golden/replays.zip"), type=Path)
    parser.add_argument("--discovery-ranking", default=Path("build/expanded-human/ranking.json"), type=Path)
    parser.add_argument("--exclude-manifest", action="append", type=Path)
    parser.add_argument("--track-metadata-source", default=Path("src/restunts/c/headless_trackdata.c"), type=Path)
    parser.add_argument("--output", default=Path("build/stunt-human"), type=Path)
    parser.add_argument("--telemetry-tracks", type=int, default=60)
    parser.add_argument("--validation-tracks", type=int, default=20)
    parser.add_argument("--jobs", type=int, choices=(1, 2), default=2)
    parser.add_argument("--prepare-only", action="store_true")
    parser.add_argument("--reuse-ranking", action="store_true")
    parser.add_argument("--reuse-traces", action="store_true")
    args = parser.parse_args()
    if args.telemetry_tracks <= 0 or args.validation_tracks <= 0:
        parser.error("Track counts must be positive")
    for field in ("binary", "repldump", "data_dir", "corpus", "discovery_ranking", "track_metadata_source", "output"):
        setattr(args, field, getattr(args, field).resolve())
    args.exclude_manifest = args.exclude_manifest or [Path("build/expanded-human/tuning-manifest.json"),
                                                     Path("build/expanded-human/validation-manifest.json")]
    args.output.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, SDL_VIDEODRIVER="restunts-invalid-driver",
                       SDL_AUDIODRIVER="restunts-invalid-driver", RESTUNTS_AUDIO_TRACE="")
    objects = metadata(args.track_metadata_source)
    provenance = input_provenance(args)
    analysis_provenance = {**provenance,
        "selection_mode": "fastest_verified_per_track_across_cars",
        "analyzer_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "helper_sha256": hashlib.sha256(Path(HUMAN.__file__).read_bytes()).hexdigest()}
    analysis_provenance_path = args.output / "analysis-provenance.json"
    if args.reuse_traces:
        if not analysis_provenance_path.exists() or json.loads(analysis_provenance_path.read_text()) != analysis_provenance:
            raise ValueError("Cached traces have missing or different analysis provenance")
    discovery = json.loads(args.discovery_ranking.read_text())
    if discovery.get("corpus_sha256") != provenance["corpus_sha256"]:
        raise ValueError("Discovery ranking refers to a different replay archive")
    old_rows = discovery["recordings"]
    with zipfile.ZipFile(args.corpus) as archive:
        items = HUMAN.inventory(archive)
        tuning, validation = select(args, items, old_rows, archive, objects)
        selected_hashes = {case["track_sha256"] for case in [*tuning, *validation]}
        eligible = [row for row in items if row["track_sha256"] in selected_hashes
                    and row["fps"] == 20 and row["opponent"] == 0 and row["complete_file"]]
        selected_names = {row["replay"] for row in eligible}
        (args.output / "corpus").mkdir(exist_ok=True)
        for row in eligible:
            (args.output / "corpus" / Path(row["replay"]).name).write_bytes(archive.read(row["replay"]))
        ranking_path = args.output / "ranking.json"
        if args.reuse_ranking and ranking_path.exists():
            cached = json.loads(ranking_path.read_text())
            if any(cached.get(key) != value for key, value in provenance.items()):
                raise ValueError("Cached ranking provenance does not match")
            rows = cached["recordings"]
            if {row["replay"] for row in rows} != selected_names:
                raise ValueError("Cached ranking covers a different selected sample")
        else:
            with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
                rows = []
                for index, result in enumerate(pool.map(lambda row: HUMAN.verify(args, row, environment), eligible)):
                    rows.append(result)
                    if index % 100 == 0:
                        print(f"Verified {index + 1}/{len(eligible)}", flush=True)
            if input_provenance(args) != provenance:
                raise ValueError("Engine, replay driver, archive, resources or metadata changed during ranking")
            HUMAN.write_json(ranking_path, {**provenance, "recordings": rows})
        tuning = prepare_cases(args, tuning, rows, archive)
        validation = prepare_cases(args, validation, rows, archive)
        HUMAN.write_json(args.output / "tuning-manifest.json", tuning)
        HUMAN.write_json(args.output / "validation-manifest.json", validation)
    if input_provenance(args) != provenance:
        raise ValueError("Engine, replay driver, archive, resources or metadata changed during preparation")
    HUMAN.write_json(args.output / "provenance.json", provenance)
    print(f"Manifests ready: {len(tuning)} new tuning tracks and {len(validation)} reserved tracks", flush=True)
    if args.prepare_only:
        return
    by_replay = {row["replay"]: row for row in rows}
    selected = [by_replay[case["replay"]] for case in tuning]
    HUMAN.write_json(args.output / "telemetry-selection.json", selected)
    def measure(row):
        path = args.output / "telemetry" / (Path(row["replay"]).stem + ".json")
        if args.reuse_traces and path.exists():
            return json.loads(path.read_text())
        return trace(args, row, objects, environment)
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        traces = []
        for index, result in enumerate(pool.map(measure, selected)):
            traces.append(result)
            if index % 20 == 0:
                print(f"Measured {index + 1}/{len(selected)}", flush=True)
    if input_provenance(args) != provenance:
        raise ValueError("Engine, archive, resources or metadata changed during analysis")
    summary = summarize(traces, tuning)
    HUMAN.write_json(analysis_provenance_path, analysis_provenance)
    HUMAN.write_json(args.output / "analysis.json", summary)
    print(json.dumps({key: value for key, value in summary.items() if key not in ("events", "airborne", "paired_human_comparisons")}, indent=2))


if __name__ == "__main__":
    main()
