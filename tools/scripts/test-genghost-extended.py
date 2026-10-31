#!/usr/bin/env python3
"""Generate and replay extended ghosts with independent binary-format fixtures."""

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
CSTRK_VERSION = 8
CSRPL_VERSION = 11
CSTRK_HEADER_SIZE = 32
CSRPL_HEADER_SIZE = 64
CSTRK_OBJECT_SIZE = 16
MAP_SIZE = 90
TILE_SIZE = 1024
LONG_FRAMES = 70001
BRAKE_INPUT = 2
RULE_AMIGA_GRASS = 0x20
RULE_OWOOT = 0x40
RESTRICTED = ("--owoot", "--pg:off", "--lc:off", "--lcb:off", "--dws:off")


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
    result["rotation"] = turns
    return result


def track_bytes(kind="flat", rotation=0, surface=0):
    """Build a rectangular lap, optionally with the new slope/stunt modifiers."""
    barrel = kind == "barrel"
    left, right = (70, 76) if barrel else (72, 78)
    top, bottom = (20, 55) if barrel else (8, 16)
    start = 22 if barrel else (bottom - 1 if kind == "combined" else top + 3)
    pieces = [object_record(6, left, top), object_record(7, right, top),
              object_record(8, left, bottom), object_record(9, right, bottom)]
    for column in range(left + 1, right):
        pieces.extend((object_record(5, column, top), object_record(5, column, bottom)))
    for row in range(top + 1, bottom):
        pieces.append(object_record(4, right, row))
        if barrel and 43 <= row < 48:
            continue
        tile = (179 if barrel else 1) if row == start else 4
        combined = kind == "combined" and row == top + 4
        pieces.append(object_record(115 if combined else tile, left, row,
                                    combined=int(combined)))
    if barrel:
        pieces.append(object_record(85, left, 43, width=1, height=2, mask=3, roll=1))
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
    if kind == "dense":
        notes = notes.ljust(8000, b" ")
    payload = heights + bytes(2 * MAP_SIZE ** 2) + encoded + notes
    header = struct.pack("<8sHHIHHIII", CSTRK_MAGIC, CSTRK_VERSION, CSTRK_HEADER_SIZE,
                         4, MAP_SIZE, MAP_SIZE, len(notes), len(pieces), len(payload))
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


def decode_replay(recording):
    assert recording[:8] == CSRPL_MAGIC
    version, header = struct.unpack_from("<HH", recording, 8)
    assert version >= CSRPL_VERSION and header == CSRPL_HEADER_SIZE
    assert recording[12:16] == bytes(4) and recording[50:64] == bytes(14)
    track_size, frames, rate, rules = struct.unpack_from("<IIHH", recording, 16)
    assert rate in (10, 20) and not rules & ~0x7F
    assert len(recording) == CSRPL_HEADER_SIZE + track_size + frames
    track = recording[CSRPL_HEADER_SIZE:CSRPL_HEADER_SIZE + track_size]
    inputs = recording[CSRPL_HEADER_SIZE + track_size:]
    assert track[:8] == CSTRK_MAGIC and all(value <= 63 for value in inputs)
    return dict(version=version, frames=frames, rate=rate, rules=rules, track=track,
                inputs=inputs, car=recording[28:32], material=recording[32],
                transmission=recording[33], opponent=recording[34])


def main():
    if len(sys.argv) not in (4, 5):
        raise SystemExit("Usage: test-genghost-extended.py GENGHOST HARNESS DATA-DIR [OUTPUT-DIR]")
    genghost, harness, data = (Path(argument).resolve() for argument in sys.argv[1:4])
    available = {p.name.upper() for p in data.iterdir()} if data.is_dir() else set()
    required = (("GAME.PRE", "GAME.RES"), ("CARCOUN.PRE", "CARCOUN.RES"),
                ("STCOUN.P3S", "STCOUN.3SH"), ("CARP962.PRE", "CARP962.RES"),
                ("STP962.P3S", "STP962.3SH"))
    if any(not any(name in available for name in names) for names in required):
        print("SKIP: original Stunts game and COUN/P962 car resources are needed", flush=True)
        return 77
    environment = dict(os.environ, SDL_VIDEODRIVER="restunts-invalid-driver",
                       SDL_AUDIODRIVER="restunts-invalid-driver", RESTUNTS_AUDIO_TRACE="")
    context = (contextlib.nullcontext(str(Path(sys.argv[4]).resolve())) if len(sys.argv) == 5
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
        assert replayed["maximum_x"] > 65535 and replayed["maximum_z"] > 65535
        second, _, _, _ = generate(wide_path, "COUN", "repeat.csrpl")
        assert first.read_bytes() == second.read_bytes(), "Fixed-budget extended generation is not repeatable"
        print("Wide CSTRK generation, full embedded track and deterministic CSRPL playback passed", flush=True)

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
        dense_path = output / "dense.cstrk"
        dense_path.write_bytes(track_bytes("dense"))
        assert len(dense_path.read_bytes()) > 65536
        _, dense_view, _, _ = generate(dense_path, "COUN", "dense.csrpl")
        assert dense_view["track"] == dense_path.read_bytes()
        print("Format sniffing, large CSTRK payloads and malformed file rejection passed", flush=True)

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

        combined_path = output / "combined.cstrk"
        combined_path.write_bytes(track_bytes("combined"))
        _, combined_view, _, _ = generate(combined_path, "COUN", "combined.csrpl", evaluations=512)
        assert combined_view["track"] == combined_path.read_bytes()
        print("Fresh generation on graded tunnel-with-slalom CSTRK passed", flush=True)

        for rotation in range(4):
            barrel_path = output / f"barrel{rotation}.cstrk"
            barrel_path.write_bytes(track_bytes("barrel", rotation=rotation))
            for rate in (20, 10):
                source = barrel_path
                filename = f"barrel{rotation}.csrpl"
                if rate == 10:
                    source = output / f"barrel{rotation}-source10.csrpl"
                    source.write_bytes(blank_replay(barrel_path.read_bytes(), rate))
                    filename = f"barrel{rotation}-10.csrpl"
                _, barrel_view, _, checked = generate(source, "P962", filename,
                                                      evaluations=1024,
                                                      flags=("--owoot", "--pg:off"))
                assert barrel_view["rate"] == rate
                assert barrel_view["track"] == barrel_path.read_bytes()
                assert checked["barrel_present"] and checked["barrel_complete"], checked
                assert checked["airborne_roll"] >= 1024 - 16, checked
        print("Fresh barrel-roll ghosts finish with full airborne rolls and upright road landings in all orientations at both frame rates", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
