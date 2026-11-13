#!/usr/bin/env python3
"""Generate and replay extended ghosts with independent binary-format fixtures."""

import argparse
import contextlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from regression_timeouts import scaled_timeout


CSTRK_MAGIC = b"CSTRK\x1a\r\n"
CSRPL_MAGIC = b"CSRPL\r\n\x1a"
CSTRK_VERSION = 15
CSRPL_VERSION = 20
CSRPL_MIN_VERSION = 11
CSRPL_PRE_CUSTOM_HORIZON_VERSION = 19
CSRPL_FUTURE_VERSION = 101
CSTRK_HEADER_SIZE = 32
CSTRK_OBJECT_SIZE = 16
CSTRK_CELL_SIZE = 2
CSTRK_CUSTOM_HORIZON_VERSION = 15
CSTRK_MAX_HORIZON_NAME = 127
CSTRK_CUSTOM_HORIZON_LENGTH_SIZE = 2
CUSTOM_HORIZON_UNICODE = "Ghost fixture missing — Őrség.png"
CUSTOM_HORIZON_BOUNDARY = "Ő" * 59 + " dusk.png"
CSRPL_HEADER_SIZE = 64
CSRPL_LONG_TRACK_NAME_VERSION = 17
CSRPL_TRACK_NAME_OFFSET = 41
CSRPL_LEGACY_TRACK_NAME_SIZE = 9
CSRPL_TRACK_NAME_SIZE = 13
MAP_SIZE = 90
LONG_FRAMES = 70001
MAX_FRAMES = 120000
CSTRK_MAX_NOTES = 8000
BRAKE_INPUT = 2
RULE_AMIGA_GRASS = 0x20
RULE_OWOOT = 0x40
RESTRICTED = ("--owoot", "--pg:off", "--lc:off", "--lcb:off", "--dws:off")
FORMAT_PART = 1
REPLAY_PART = 2
STUNT_PART = 3
ROUTE_PART = 4
TEST_PARTS = (FORMAT_PART, REPLAY_PART, STUNT_PART, ROUTE_PART)
PART_SHARD_COUNTS = {FORMAT_PART: 2, REPLAY_PART: 2, STUNT_PART: 2, ROUTE_PART: 3}
BARREL_ROTATION_COUNT = 4
BLUE_CHECKPOINT_ID = 218
RED_CHECKPOINT_ID = 219
COMBINED_CHECKPOINT_ID = 220
CHECKPOINT_ROWS = (10, 14)
BRANCH_TOP, BRANCH_SPLIT, BRANCH_BOTTOM, BRANCH_START = 8, 18, 26, 22
BRANCH_LEFT, BRANCH_MERGE, BRANCH_RIGHT = 72, 78, 84
BRANCH_CHECKPOINT_ROW, BRANCH_CHECKPOINT_COLUMN = 13, 75
BRANCH_SPLIT_ID, BRANCH_MERGE_ID = 79, 76
BRANCH_SHARED_ROW = BRANCH_START - 2
# Measured native runtimes balance each orientation/rate pair with a part's
# format, replay or stunt work; its collision modes may use separate shards.
BARREL_PARTS = {
    (0, 20): REPLAY_PART, (0, 10): STUNT_PART,
    (1, 20): FORMAT_PART, (1, 10): FORMAT_PART,
    (2, 20): STUNT_PART, (2, 10): REPLAY_PART,
    (3, 20): REPLAY_PART, (3, 10): STUNT_PART,
}
# Fixed assignments use measured native search times. Keep collision pairs
# together except the small 10 Hz pair that balances the replay part's halves.
BARREL_SHARDS = {
    (0, 20): {"on": 1, "off": 1}, (0, 10): {"on": 1, "off": 1},
    (1, 20): {"on": 1, "off": 1}, (1, 10): {"on": 2, "off": 2},
    (2, 20): {"on": 2, "off": 2}, (2, 10): {"on": 1, "off": 2},
    (3, 20): {"on": 2, "off": 2}, (3, 10): {"on": 1, "off": 1},
}
SHARED_SHARDS = {
    ("shared", True): 2, ("shared", False): 2,
    ("shared-blue", True): 2, ("shared-red", True): 2,
    ("both-shared", True): 3, ("branches-shared", True): 3,
    ("branches-shared", False): 3,
}


def object_record(tile, x, y, *, width=1, height=1, mask=1,
                  surface=0, lower=0, combined=0, roll=0):
    return dict(tile=tile, x=x, y=y, rotation=0, surface=surface,
                width=width, height=height, mask=mask, lower=lower,
                combined=combined, roll=roll)


def rotate_object(piece, turns):
    result = dict(piece)
    for _ in range(turns):
        width, height, mask = result["width"], result["height"], result["mask"]
        rotated_mask = sum(1 << (column * height + height - 1 - row)
                           for row in range(height) for column in range(width)
                           if mask & (1 << (row * width + column)))
        result.update(x=MAP_SIZE - result["y"] - height, y=result["x"],
                      width=height, height=width, mask=rotated_mask)
    result["rotation"] = (result["rotation"] + turns) % 4
    return result


def track_bytes(kind="flat", rotation=0, surface=0, *, custom_horizon="", horizon=4):
    """Build a rectangular lap with versioned stunts or an optional custom horizon."""
    tight_barrel = kind == "barrel-tight"
    barrel = kind in ("barrel", "barrel-tight")
    left, right = (45, 51) if tight_barrel else ((70, 76) if barrel else (72, 78))
    top, bottom = (15, 45) if tight_barrel else ((20, 55) if barrel else (8, 16))
    start = top + 2 if barrel else (bottom - 1 if kind == "combined" else top + 3)
    if kind.startswith("branches"):
        left, right, top, bottom, start = (BRANCH_LEFT, BRANCH_RIGHT, BRANCH_TOP,
                                           BRANCH_BOTTOM, BRANCH_START)
    launch = bottom - 7 if tight_barrel else 43
    landing = bottom - 1 if tight_barrel else 48
    pieces = [object_record(6, left, top), object_record(7, right, top),
              object_record(8, left, bottom), object_record(9, right, bottom)]
    for column in range(left + 1, right):
        pieces.extend((object_record(5, column, top), object_record(5, column, bottom)))
    for row in range(top + 1, bottom):
        pieces.append(object_record(4, right, row))
        if barrel and launch <= row < landing:
            continue
        tile = (179 if barrel else 1) if row == start else 4
        combined = ((kind == "combined" and row == top + 4) or
                    (tight_barrel and row == landing))
        piece = object_record(115 if combined else tile, left, row, combined=int(combined))
        if tight_barrel and combined:
            piece["rotation"] = 2
        pieces.append(piece)
    if barrel:
        pieces.append(object_record(85, left, launch, width=1, height=2, mask=3, roll=1))
    if kind.startswith("branches"):
        branch_kind = kind.replace("-shared", "")
        for piece in pieces:
            if (piece["x"], piece["y"]) == (left, BRANCH_SPLIT):
                piece["tile"] = BRANCH_SPLIT_ID
            elif (piece["x"], piece["y"]) == (BRANCH_MERGE, top):
                piece["tile"] = BRANCH_MERGE_ID
            elif branch_kind != "branches-red" and (piece["x"], piece["y"]) in (
                    (left, BRANCH_CHECKPOINT_ROW), (BRANCH_CHECKPOINT_COLUMN, top)):
                piece["tile"] = BLUE_CHECKPOINT_ID
                piece["rotation"] = int(piece["y"] == top)
        pieces.append(object_record(9, BRANCH_MERGE, BRANCH_SPLIT))
        for column in range(left + 1, BRANCH_MERGE):
            gate = branch_kind != "branches-blue" and column == BRANCH_CHECKPOINT_COLUMN
            piece = object_record(RED_CHECKPOINT_ID if gate else 5, column, BRANCH_SPLIT)
            piece["rotation"] = int(gate)
            pieces.append(piece)
        for row in range(top + 1, BRANCH_SPLIT):
            gate = branch_kind != "branches-blue" and row == BRANCH_CHECKPOINT_ROW
            pieces.append(object_record(RED_CHECKPOINT_ID if gate else 4, BRANCH_MERGE, row))
    if kind.endswith("-shared") and kind.startswith("branches"):
        for piece in pieces:
            if (piece["x"], piece["y"]) in ((left, BRANCH_SHARED_ROW),
                                              (right, BRANCH_CHECKPOINT_ROW)):
                piece["tile"] = COMBINED_CHECKPOINT_ID
    if kind in ("blue", "red", "both", "both-shared"):
        for piece in pieces:
            if piece["y"] in CHECKPOINT_ROWS and piece["x"] in (left, right):
                piece["tile"] = (RED_CHECKPOINT_ID if kind == "red" or
                                 (kind in ("both", "both-shared") and piece["x"] == right) else BLUE_CHECKPOINT_ID)
    if kind in ("shared", "shared-blue", "shared-red"):
        for piece in pieces:
            if piece["y"] in CHECKPOINT_ROWS and piece["x"] in (left, right):
                piece["tile"] = (COMBINED_CHECKPOINT_ID if kind == "shared" or piece["x"] == left
                                 else BLUE_CHECKPOINT_ID if kind == "shared-blue" else RED_CHECKPOINT_ID)
    if kind == "both-shared":
        for piece in pieces:
            if piece["x"] == (left + right) // 2 and piece["y"] == bottom:
                piece["tile"] = COMBINED_CHECKPOINT_ID
                piece["rotation"] = 1
    if kind == "maximum":
        occupied = {(piece["x"], piece["y"]) for piece in pieces}
        for row in range(MAP_SIZE):
            for column in range(MAP_SIZE):
                if (column, row) not in occupied:
                    pieces.append(object_record(153, column, row))
    if kind == "dense":
        # Keep scenery clear of the lap while exercising a valid track payload
        # larger than the old entire-file reader limit.
        for row in range(MAP_SIZE):
            for column in range(MAP_SIZE):
                if left - 3 <= column <= right + 3 and top - 3 <= row <= bottom + 3:
                    continue
                pieces.append(object_record(153, column, row))
    heights = bytearray((MAP_SIZE + 1) ** 2)
    for row in range(MAP_SIZE + 1):
        value = max(0, min(4, row - (top + 2))) if kind == "combined" else 0
        for column in range(MAP_SIZE + 1):
            if tight_barrel:
                value = 3 + int(column > left)
            x, y = column, row
            for _ in range(rotation):
                x, y = MAP_SIZE - y, x
            heights[y * (MAP_SIZE + 1) + x] = value
    encoded = bytearray()
    for original in pieces:
        piece = rotate_object(original, rotation)
        corner = piece["y"] * (MAP_SIZE + 1) + piece["x"]
        base = heights[corner]
        slope_x = heights[corner + 1] - base
        slope_y = heights[corner + MAP_SIZE + 1] - base
        encoded.extend(struct.pack("<H8B2b4B", piece["tile"], piece["x"], piece["y"],
                                   piece["rotation"], surface, piece["width"], piece["height"],
                                   piece["mask"], base, slope_x, slope_y, piece["lower"],
                                   piece["combined"], piece["roll"], 0))
    notes = b"Independent extended ghost fixture; wide world and versioned stunts."
    if kind in ("dense", "maximum"):
        notes = notes.ljust(CSTRK_MAX_NOTES, b" ")
    payload = heights + bytes(2 * MAP_SIZE ** 2) + encoded + notes
    if custom_horizon:
        horizon_name = custom_horizon.encode("utf-8")
        assert len(horizon_name) <= CSTRK_MAX_HORIZON_NAME
        payload += struct.pack("<H", len(horizon_name)) + horizon_name
    header = struct.pack("<8sHHIHHIII", CSTRK_MAGIC, CSTRK_VERSION, CSTRK_HEADER_SIZE,
                         horizon, MAP_SIZE, MAP_SIZE, len(notes), len(pieces), len(payload))
    return header + payload


def blank_replay(track, rate, car=b"P962"):
    """A valid unfinished CSRPL supplies timing and track, without a driving seed."""
    header = bytearray(CSRPL_HEADER_SIZE)
    header[:8] = CSRPL_MAGIC
    struct.pack_into("<HHIIIHH", header, 8, CSRPL_VERSION, CSRPL_HEADER_SIZE,
                     0, len(track), 1, rate, 0x1F)
    header[28:32] = car
    header[33] = 1
    header[41:47] = b"BARREL"
    return header + track + bytes(1)


def decode_track_horizon(track):
    """Read the optional horizon trailer independently of the production codec."""
    assert track[:8] == CSTRK_MAGIC
    version, header, horizon, width, height, notes_size, objects, payload = struct.unpack_from("<HHIHHIII", track, 8)
    assert header == CSTRK_HEADER_SIZE and len(track) == header + payload
    standard_payload = ((width + 1) * (height + 1) + CSTRK_CELL_SIZE * width * height +
                        CSTRK_OBJECT_SIZE * objects + notes_size)
    assert standard_payload <= payload
    trailer = track[header + standard_payload:]
    filename = ""
    if trailer:
        assert version >= CSTRK_CUSTOM_HORIZON_VERSION
        assert len(trailer) >= CSTRK_CUSTOM_HORIZON_LENGTH_SIZE
        name_size = struct.unpack_from("<H", trailer)[0]
        assert 0 < name_size <= CSTRK_MAX_HORIZON_NAME
        assert len(trailer) == CSTRK_CUSTOM_HORIZON_LENGTH_SIZE + name_size
        filename = trailer[CSTRK_CUSTOM_HORIZON_LENGTH_SIZE:].decode("utf-8")
    return horizon, filename


def decode_replay(recording):
    assert recording[:8] == CSRPL_MAGIC
    version, header = struct.unpack_from("<HH", recording, 8)
    assert version >= CSRPL_MIN_VERSION and header == CSRPL_HEADER_SIZE
    name_size = (CSRPL_LEGACY_TRACK_NAME_SIZE if version < CSRPL_LONG_TRACK_NAME_VERSION
                 else CSRPL_TRACK_NAME_SIZE)
    reserved_start = CSRPL_TRACK_NAME_OFFSET + name_size
    assert recording[12:16] == bytes(4)
    assert recording[reserved_start:CSRPL_HEADER_SIZE] == bytes(CSRPL_HEADER_SIZE - reserved_start)
    track_size, frames, rate, rules = struct.unpack_from("<IIHH", recording, 16)
    assert rate in (10, 20) and not rules & ~0x7F
    assert len(recording) == CSRPL_HEADER_SIZE + track_size + frames
    track = recording[CSRPL_HEADER_SIZE:CSRPL_HEADER_SIZE + track_size]
    inputs = recording[CSRPL_HEADER_SIZE + track_size:]
    assert track[:8] == CSTRK_MAGIC and all(value <= 63 for value in inputs)
    horizon, custom_horizon = decode_track_horizon(track)
    return dict(version=version, frames=frames, rate=rate, rules=rules, track=track,
                horizon=horizon, custom_horizon=custom_horizon,
                inputs=inputs, car=recording[28:32], material=recording[32],
                transmission=recording[33], opponent=recording[34],
                track_name=recording[CSRPL_TRACK_NAME_OFFSET:reserved_start].split(b"\0", 1)[0])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("genghost", type=Path)
    parser.add_argument("harness", type=Path)
    parser.add_argument("data", type=Path)
    parser.add_argument("output", nargs="?", type=Path)
    parser.add_argument("--part", type=int, choices=TEST_PARTS,
                        help="Run one balanced part; omit to run the complete suite")
    parser.add_argument("--shard", type=int,
                        help="Run one subdivision of --part; omit to run the whole part")
    args = parser.parse_args()
    if args.shard is not None:
        if args.part is None:
            parser.error("--shard requires --part")
        if not 1 <= args.shard <= PART_SHARD_COUNTS[args.part]:
            parser.error(f"--part {args.part} accepts --shard 1 through {PART_SHARD_COUNTS[args.part]}")

    def selected(part, shard):
        return args.part in (None, part) and args.shard in (None, shard)

    genghost, harness, data = (path.resolve() for path in (args.genghost, args.harness, args.data))
    available = {p.name.upper() for p in data.iterdir()} if data.is_dir() else set()
    required = (("GAME.PRE", "GAME.RES"), ("CARCOUN.PRE", "CARCOUN.RES"),
                ("STCOUN.P3S", "STCOUN.3SH"), ("CARP962.PRE", "CARP962.RES"),
                ("STP962.P3S", "STP962.3SH"), ("CARPMIN.PRE", "CARPMIN.RES"),
                ("STPMIN.P3S", "STPMIN.3SH"))
    if any(not any(name in available for name in names) for names in required):
        print("SKIP: original Stunts game and COUN/P962/PMIN car resources are needed", flush=True)
        return 77
    environment = dict(os.environ, SDL_VIDEODRIVER="restunts-invalid-driver",
                       SDL_AUDIODRIVER="restunts-invalid-driver", RESTUNTS_AUDIO_TRACE="")
    context = (contextlib.nullcontext(str(args.output.resolve())) if args.output is not None
               else tempfile.TemporaryDirectory(prefix="ghextended-"))
    with context as directory:
        output = Path(directory)
        output.mkdir(parents=True, exist_ok=True)

        def run(binary, arguments, *, expected=0):
            command = [str(binary), "--data-dir", str(data), *map(str, arguments)]
            result = subprocess.run(command, cwd=output, env=environment, capture_output=True,
                                    text=True, timeout=scaled_timeout(180))
            if result.returncode != expected:
                raise AssertionError(f"Expected exit {expected}, got {result.returncode}: {command}\n"
                                     f"{result.stdout}{result.stderr}")
            return result

        def verify(path, *, expected=0):
            return json.loads(run(genghost, [path, "--verify"], expected=expected).stdout)

        def playback(path):
            return json.loads(run(harness, ["--replay", path]).stdout)

        def generate(source, car, name, *, evaluations=128, flags=RESTRICTED):
            destination = output / name
            run(genghost, [source, car, "--seed", 1, "--evaluations", evaluations,
                           "--output", destination, *flags])
            recording = destination.read_bytes()
            view = decode_replay(recording)
            report = json.loads(destination.with_name(destination.name + ".json").read_text())
            assert view["car"] == car.encode("ascii") and view["transmission"] == 0
            assert view["opponent"] == 0 and report["finished"]
            assert view["frames"] == report["replay_frames"]
            verified, replayed = verify(destination), playback(destination)
            for field in ("finish_frame", "penalty_frames", "total_frames"):
                assert verified[field] == replayed[field] == report[field], (field, verified, replayed, report)
            assert replayed["finished"] and not replayed["crashed"]
            assert verified["frames_per_second"] == replayed["frames_per_second"] == view["rate"]
            assert report["total_seconds"] == report["total_frames"] / view["rate"]
            return destination, view, report, replayed

        wide_path = output / "wide.cstrk"
        wide_path.write_bytes(track_bytes())
        first, view, report, replayed = generate(wide_path, "COUN", "wide.csrpl")
        assert view["track"] == wide_path.read_bytes()
        assert view["version"] == CSRPL_VERSION
        assert replayed["maximum_x"] > 65535 and replayed["maximum_z"] > 65535
        if selected(FORMAT_PART, 1):
            assert len(CUSTOM_HORIZON_BOUNDARY.encode("utf-8")) == CSTRK_MAX_HORIZON_NAME
            custom_outputs = []
            for number, (filename, fallback) in enumerate(((CUSTOM_HORIZON_UNICODE, 4),
                                                          (CUSTOM_HORIZON_BOUNDARY, 2))):
                custom_path = output / f"missing-horizon-{number}.cstrk"
                custom_path.write_bytes(track_bytes(custom_horizon=filename, horizon=fallback))
                assert not (data / filename).exists() and not (output / filename).exists()
                custom_output, custom_view, custom_report, _ = generate(
                    custom_path, "COUN", f"missing-horizon-{number}.csrpl")
                assert custom_view["version"] == CSRPL_VERSION
                assert custom_view["track"] == custom_path.read_bytes()
                assert (custom_view["horizon"], custom_view["custom_horizon"]) == (fallback, filename)
                assert custom_view["inputs"] == view["inputs"]
                assert custom_report["total_frames"] == report["total_frames"]
                custom_outputs.append((custom_output, custom_view))
            maximum_track = track_bytes("maximum", custom_horizon=CUSTOM_HORIZON_BOUNDARY)
            maximum_recording = bytearray(blank_replay(maximum_track, 20, car=b"COUN"))
            maximum_recording.extend(bytes(MAX_FRAMES - 1))
            struct.pack_into("<I", maximum_recording, 20, MAX_FRAMES)
            old_maximum_size = (CSRPL_HEADER_SIZE + CSTRK_HEADER_SIZE + (MAP_SIZE + 1) ** 2 +
                                CSTRK_CELL_SIZE * MAP_SIZE ** 2 + CSTRK_OBJECT_SIZE * MAP_SIZE ** 2 +
                                CSTRK_MAX_NOTES + MAX_FRAMES)
            assert len(maximum_recording) == old_maximum_size + CSTRK_CUSTOM_HORIZON_LENGTH_SIZE + CSTRK_MAX_HORIZON_NAME
            maximum_source = output / "maximum-custom-horizon.csrpl"
            maximum_source.write_bytes(maximum_recording)
            maximum_score = verify(maximum_source, expected=2)
            maximum_playback = playback(maximum_source)
            assert maximum_score["frames"] == maximum_playback["frames"] == MAX_FRAMES
            assert not maximum_score["finished"] and not maximum_playback["finished"]
            custom_output, custom_view = custom_outputs[0]
            old_custom = bytearray(custom_output.read_bytes())
            struct.pack_into("<H", old_custom, 8, CSRPL_PRE_CUSTOM_HORIZON_VERSION)
            custom_rules = custom_view["rules"] | RULE_AMIGA_GRASS
            struct.pack_into("<H", old_custom, 26, custom_rules)
            custom_seed = output / "missing-horizon-old.csrpl"
            custom_seed.write_bytes(old_custom)
            custom_baseline = verify(custom_seed)
            for strategy in ("legacy", "state"):
                custom_refined = output / f"missing-horizon-{strategy}.csrpl"
                run(genghost, [custom_seed, "--fine-tune-gears-only", "--cores", 2,
                               "--strategy", strategy, "--evaluations", 2, "--output", custom_refined])
                refined_custom_view = decode_replay(custom_refined.read_bytes())
                assert refined_custom_view["version"] == CSRPL_PRE_CUSTOM_HORIZON_VERSION
                assert refined_custom_view["rules"] == custom_rules
                assert refined_custom_view["track"] == custom_view["track"]
                assert (refined_custom_view["horizon"], refined_custom_view["custom_horizon"]) == (4, CUSTOM_HORIZON_UNICODE)
                refined_custom_score, replayed_custom = verify(custom_refined), playback(custom_refined)
                assert refined_custom_score["total_frames"] <= custom_baseline["total_frames"]
                assert replayed_custom["total_frames"] == refined_custom_score["total_frames"]
            _, upgraded_custom, _, _ = generate(custom_seed, "COUN", "missing-horizon-upgraded.csrpl", evaluations=32)
            assert upgraded_custom["version"] == CSRPL_VERSION
            assert upgraded_custom["rules"] == custom_rules
            assert upgraded_custom["track"] == custom_view["track"]
            assert custom_seed.read_bytes() == old_custom
            print("Missing custom PNGs, Unicode/127-byte names, fallback horizons and older CSRPL metadata survive generation/refinement", flush=True)
            for kind, rotation, surface in (("blue", 0, 0), ("red", 1, 1), ("both", 2, 2)):
                checkpoint_path = output / f"checkpoints-{kind}.cstrk"
                checkpoint_path.write_bytes(track_bytes(kind, rotation, surface))
                checkpoint_ghost, checkpoint_view, checkpoint_report, checkpoint_playback = generate(
                    checkpoint_path, "COUN", f"checkpoints-{kind}.csrpl", evaluations=256)
                assert checkpoint_view["track"] == checkpoint_path.read_bytes()
                assert checkpoint_report["route_combinations"] == (2 if kind == "both" else 1)
                assert checkpoint_report["routes_finished"] == (2 if kind == "both" else 1)
                if kind == "both":
                    assert checkpoint_playback["blue_passed"] == 2 or checkpoint_playback["red_passed"] == 2
                else:
                    assert checkpoint_playback["blue_passed"] == (0 if kind == "red" else 4)
                    assert checkpoint_playback["red_passed"] == (0 if kind == "blue" else 4)
                # The same mandatory gate protection applies to permissive driving.
                generate(checkpoint_path, "COUN", f"checkpoints-{kind}-free.csrpl",
                         evaluations=256, flags=("--pg:off", "--lc:off"))
            print("Blue-only, red-only and mixed checkpoints generate verified ghosts on paved, dirt and ice", flush=True)

        # Forks form one route shard; the shared-gate cases form two similar batches.
        if selected(ROUTE_PART, 1):
            for kind in ("branches", "branches-blue", "branches-red"):
                branch_path = output / f"{kind}.cstrk"
                branch_path.write_bytes(track_bytes(kind))
                _, _, branch_report, branch_playback = generate(
                    branch_path, "COUN", f"{kind}.csrpl", evaluations=512)
                combinations = 2 if kind == "branches" else 1
                assert branch_report["route_combinations"] == combinations
                assert branch_report["routes_attempted"] == combinations
                assert branch_report["routes_finished"] == combinations
                if kind != "branches":
                    required = "blue_passed" if kind == "branches-blue" else "red_passed"
                    assert branch_playback[required] == 2
            print("Checkpoint forks compile and finish every blue/red route case", flush=True)

        if selected(ROUTE_PART, 2) or selected(ROUTE_PART, 3):
            shared_cases = (("shared", 0, 0, True), ("shared", 0, 0, False),
                            ("shared-blue", 1, 1, True), ("shared-red", 2, 2, True),
                            ("both-shared", 3, 0, True), ("branches-shared", 0, 0, True),
                            ("branches-shared", 0, 0, False))
            for kind, rotation, surface, restricted in shared_cases:
                if not selected(ROUTE_PART, SHARED_SHARDS[kind, restricted]):
                    continue
                name = kind + ("-restricted" if restricted else "-free")
                shared_path = output / f"{name}.cstrk"
                shared_path.write_bytes(track_bytes(kind, rotation, surface))
                _, _, shared_report, shared_playback = generate(
                    shared_path, "COUN", f"{name}.csrpl", evaluations=512,
                    flags=RESTRICTED if restricted else ("--pg:off", "--lc:off"))
                assert shared_report["route_combinations"] == 2
                assert shared_report["routes_attempted"] == shared_report["routes_finished"] == 2
                expected_shared = 4 if kind == "shared" else 1 if kind == "both-shared" else 2
                assert shared_playback["shared_passed"] == expected_shared
            print("Shared blue/red gates remain mandatory for both generated color cases and snapshots", flush=True)

        if selected(FORMAT_PART, 1):
            second, _, _, _ = generate(wide_path, "COUN", "repeat.csrpl")
            assert first.read_bytes() == second.read_bytes(), "Fixed-budget extended generation is not repeatable"
            print("Wide CSTRK generation, full embedded track and deterministic CSRPL playback passed", flush=True)

            # The replay name is independent of the source filename and must retain
            # all twelve extended characters when a fine-tuned replay is published.
            long_track_name = b"FLOWERFIELD1"
            assert len(long_track_name) == CSRPL_TRACK_NAME_SIZE - 1
            named_recording = bytearray(first.read_bytes())
            named_recording[CSRPL_TRACK_NAME_OFFSET:CSRPL_TRACK_NAME_OFFSET + CSRPL_TRACK_NAME_SIZE] = long_track_name + b"\0"
            named_source = output / "long-name-seed.csrpl"
            named_source.write_bytes(named_recording)
            named_refined = output / "long-name-refined.csrpl"
            run(genghost, [named_source, "--fine-tune-gears-only", "--evaluations", 1,
                           "--output", named_refined])
            named_view = decode_replay(named_refined.read_bytes())
            named_report = json.loads(named_refined.with_name(named_refined.name + ".json").read_text())
            assert named_view["track_name"] == long_track_name
            assert named_report["track"] == long_track_name.decode("ascii")
            assert named_view["track"] == view["track"] and named_view["version"] == CSRPL_VERSION
            assert verify(named_refined)["total_frames"] <= report["total_frames"]
            assert named_source.read_bytes() == named_recording
            print("Twelve-character CSRPL names survive fine-tuning and report output", flush=True)

            # Stored revisions select playback physics. Verification and refinement
            # retain v11, while a fresh search uses the current physics revision.
            old_recording = bytearray(first.read_bytes())
            struct.pack_into("<H", old_recording, 8, CSRPL_MIN_VERSION)
            old_source = output / "previous-physics.csrpl"
            old_source.write_bytes(old_recording)
            old_score = verify(old_source)
            assert old_score["total_frames"] == report["total_frames"]
            for strategy in ("legacy", "state"):
                refined_old = output / f"previous-{strategy}.csrpl"
                run(genghost, [old_source, "--fine-tune-gears-only", "--cores", 2,
                               "--strategy", strategy, "--evaluations", 2, "--output", refined_old])
                assert decode_replay(refined_old.read_bytes())["version"] == CSRPL_MIN_VERSION
                assert verify(refined_old)["total_frames"] <= old_score["total_frames"]
            _, upgraded_view, _, _ = generate(old_source, "COUN", "new-physics.csrpl", evaluations=32)
            assert upgraded_view["version"] == CSRPL_VERSION
            future_recording = bytearray(first.read_bytes())
            struct.pack_into("<H", future_recording, 8, CSRPL_FUTURE_VERSION)
            future_track = bytearray(view["track"])
            struct.pack_into("<H", future_track, 8, CSRPL_FUTURE_VERSION)
            future_recording[CSRPL_HEADER_SIZE:CSRPL_HEADER_SIZE + len(future_track)] = future_track
            future_source = output / "future-physics.csrpl"
            future_source.write_bytes(future_recording)
            future_score = verify(future_source)
            assert future_score["total_frames"] == report["total_frames"]
            refined_future = output / "future-refined.csrpl"
            run(genghost, [future_source, "--fine-tune-gears-only", "--evaluations", 1,
                           "--output", refined_future])
            refined_future_view = decode_replay(refined_future.read_bytes())
            assert refined_future_view["version"] == CSRPL_FUTURE_VERSION
            assert refined_future_view["track"] == future_track
            assert verify(refined_future)["total_frames"] <= future_score["total_frames"]
            _, current_view, _, _ = generate(future_source, "COUN", "current-physics.csrpl",
                                             evaluations=32)
            assert current_view["version"] == CSRPL_VERSION
            assert current_view["track"] == future_track
            assert future_source.read_bytes() == future_recording
            obsolete = bytearray(old_recording)
            struct.pack_into("<H", obsolete, 8, CSRPL_MIN_VERSION - 1)
            obsolete_path = output / "obsolete-physics.csrpl"
            obsolete_path.write_bytes(obsolete)
            run(genghost, [obsolete_path, "--verify"], expected=1)
            print("Previous CSRPL physics retained for playback/refinement; fresh searches upgrade", flush=True)

            default_path = output / "wide-COUN.csrpl"
            default_path.unlink(missing_ok=True)
            run(genghost, [wide_path.with_suffix(""), "COUN", "--evaluations", 32, *RESTRICTED])
            assert decode_replay(default_path.read_bytes())["track"] == wide_path.read_bytes()
            assert verify(default_path)["finished"]

            # The generator must inspect magic rather than assume a large binary is
            # a legacy RPL. Names without suffix also resolve the new track format.
            renamed = output / "misnamed.trk"
            renamed.write_bytes(wide_path.read_bytes())
            generated, renamed_view, _, _ = generate(renamed, "COUN", "misnamed.csrpl", evaluations=32)
            assert renamed_view["track"] == wide_path.read_bytes()
            run(genghost, [wide_path, "COUN", "--evaluations", 1, "--output", output / "wrong.rpl"], expected=1)
            bad_rules = bytearray(first.read_bytes())
            struct.pack_into("<H", bad_rules, 26, 0x8000)
            bad_input = bytearray(first.read_bytes())
            bad_input[-1] = 64
            for number, malformed in enumerate((wide_path.read_bytes()[:31],
                                                wide_path.read_bytes()[:-1],
                                                first.read_bytes()[:-1],
                                                first.read_bytes() + b"\x00",
                                                bad_rules, bad_input)):
                path = output / f"bad{number}.cstrk"
                path.write_bytes(malformed)
                run(genghost, [path, "COUN", "--evaluations", 1, "--output", output / "bad.csrpl"], expected=1)
            print("Format sniffing and malformed file rejection passed", flush=True)

        # The dense payload balances the format part's other checks and 20 Hz barrels.
        if selected(FORMAT_PART, 2):
            dense_path = output / "dense.cstrk"
            dense_path.write_bytes(track_bytes("dense"))
            assert len(dense_path.read_bytes()) > 65536
            _, dense_view, _, _ = generate(dense_path, "COUN", "dense.csrpl")
            assert dense_view["track"] == dense_path.read_bytes()
            print("Large CSTRK payloads generate independently verified ghosts", flush=True)

        if selected(REPLAY_PART, 1):
            for rate, mode in ((20, "--snapshot"), (10, "--snapshot10")):
                long_path = output / f"stationary{rate}.csrpl"
                snapshot = json.loads(run(harness, [mode, wide_path, long_path]).stdout)
                assert snapshot["frames"] == LONG_FRAMES and snapshot["restored"]
                long_view = decode_replay(long_path.read_bytes())
                assert long_view["frames"] == LONG_FRAMES and long_view["rate"] == rate
                verified = verify(long_path, expected=2)
                assert verified["frames"] == LONG_FRAMES and verified["frames_per_second"] == rate
                assert not verified["finished"]
            _, ten_view, _, _ = generate(output / "stationary10.csrpl", "COUN", "from-tenhz.csrpl")
            assert ten_view["rate"] == 10
            ten_hz = output / "tenhz.csrpl"
            run(harness, ["--drive10", wide_path, ten_hz])
            assert verify(ten_hz)["frames_per_second"] == 10
            for strategy in ("legacy", "state"):
                refined_ten = output / f"tenhz-{strategy}.csrpl"
                run(genghost, [ten_hz, "--fine-tune-gears-only", "--cores", 2,
                               "--strategy", strategy, "--evaluations", 2, "--output", refined_ten])
                assert decode_replay(refined_ten.read_bytes())["rate"] == 10
                assert verify(refined_ten)["frames_per_second"] == 10
            print("70,001-frame snapshots at both rates and 10 Hz CSRPL worker fine-tuning passed", flush=True)

            # A complete long replay exercises parent/worker CSRPL seed extraction
            # above both the old file-size and frame-counter limits. The tail does
            # not alter the already latched finish score.
            long_finished = bytearray(first.read_bytes())
            tail = LONG_FRAMES - view["frames"]
            long_finished.extend(bytes([BRAKE_INPUT]) * tail)
            struct.pack_into("<I", long_finished, 20, LONG_FRAMES)
            long_finished[32] = 3
            late_frame = 65536
            track_size = len(view["track"])
            long_finished[CSRPL_HEADER_SIZE + track_size + late_frame] |= 16
            rules = view["rules"] | RULE_AMIGA_GRASS
            struct.pack_into("<H", long_finished, 26, rules)
            long_seed = output / "long-finished.csrpl"
            long_seed.write_bytes(long_finished)
            baseline = verify(long_seed)
            assert baseline["frames"] == LONG_FRAMES and baseline["total_frames"] == report["total_frames"]
            for strategy in ("legacy", "state"):
                refined = output / f"refined-{strategy}.csrpl"
                run(genghost, [long_seed, "--fine-tune-gears-only", "--cores", 2,
                               "--strategy", strategy, "--evaluations", 2, "--output", refined,
                               "--owoot:off", "--amigagrass:off"])
                refined_view = decode_replay(refined.read_bytes())
                assert refined_view["frames"] == LONG_FRAMES
                assert refined_view["track"] == view["track"] and refined_view["material"] == 3
                assert refined_view["rules"] == rules & ~(RULE_AMIGA_GRASS | RULE_OWOOT)
                seed_view = decode_replay(long_finished)
                assert all((before & 15) == (after & 15)
                           for before, after in zip(seed_view["inputs"], refined_view["inputs"]))
                refined_report = json.loads(refined.with_name(refined.name + ".json").read_text())
                assert refined_report["cores_used"] == 2
                assert verify(refined)["total_frames"] <= baseline["total_frames"]
            print("Long CSRPL fine-tuning, metadata and explicit worker rules passed with both strategies", flush=True)

        if selected(STUNT_PART, 1):
            combined_path = output / "combined.cstrk"
            combined_path.write_bytes(track_bytes("combined"))
            _, combined_view, _, _ = generate(combined_path, "COUN", "combined.csrpl", evaluations=512)
            assert combined_view["track"] == combined_path.read_bytes()
            print("Fresh generation on graded tunnel-with-slalom CSTRK passed", flush=True)

            tight_path = output / "barrel-budget.cstrk"
            tight_path.write_bytes(track_bytes("barrel-tight"))
            budget = json.loads(run(harness, ["--barrel-budget", tight_path]).stdout)
            assert budget["evaluations"] == 32 and budget["native_inputs"] > 0
            assert budget["cancelled_at"] < budget["evaluations"] and budget["deterministic"]
            print("Unfinished barrel repair honors native candidate budgets, cancellation and repeatability", flush=True)

        for rotation in range(BARREL_ROTATION_COUNT):
            barrel_path = output / f"barrel{rotation}.cstrk"
            barrel_path.write_bytes(track_bytes("barrel", rotation=rotation))
            for rate in (20, 10):
                if args.part is not None and BARREL_PARTS[rotation, rate] != args.part:
                    continue
                source = barrel_path
                filename = f"barrel{rotation}.csrpl"
                if rate == 10:
                    source = output / f"barrel{rotation}-source10.csrpl"
                    source.write_bytes(blank_replay(barrel_path.read_bytes(), rate))
                    filename = f"barrel{rotation}-10.csrpl"
                for collision in ("on", "off"):
                    if not selected(BARREL_PARTS[rotation, rate], BARREL_SHARDS[rotation, rate][collision]):
                        continue
                    collision_name = filename.replace(".csrpl", f"-{collision}.csrpl")
                    generated, barrel_view, _, checked = generate(
                        source, "P962", collision_name, evaluations=1024,
                        flags=("--owoot", "--pg:off", f"--lc:{collision}"))
                    assert barrel_view["version"] == CSRPL_VERSION
                    assert barrel_view["rate"] == rate
                    assert barrel_view["track"] == barrel_path.read_bytes()
                    assert checked["barrel_present"] and checked["barrel_complete"], checked
                    assert checked["airborne_roll"] >= 1024 - 16, checked
                    if rotation == 0 and rate == 20 and collision == "off":
                        # Identical fixed-collision inputs retain the earlier
                        # quantized contact behavior when recorded as v11.
                        previous = bytearray(generated.read_bytes())
                        struct.pack_into("<H", previous, 8, CSRPL_MIN_VERSION)
                        previous_path = output / "previous-barrel-physics.csrpl"
                        previous_path.write_bytes(previous)
                        rejected = verify(previous_path, expected=2)
                        assert not rejected["finished"]
                        assert rejected["frames"] < checked["finish_frame"]
        if args.part is None or args.part in BARREL_PARTS.values():
            print("Fresh barrel-roll ghosts finish with full airborne rolls and upright road landings "
                  f"for part {args.part or 'all'} in both collision modes", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
