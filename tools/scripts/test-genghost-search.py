#!/usr/bin/env python3
"""Exercise offline ghost generation, repeatability, rules, and replay playback."""

import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zipfile


REPLAY_HEADER_SIZE = 26
REPLAY_INPUT_OFFSET = 1828
GAMESTATE_SIZE = 1120
RESTRICTED = ("--owoot", "--pg:off", "--lc:off", "--lcb:off")


def main():
    genghost, repldump, data_directory, corpus = (Path(argument).resolve() for argument in sys.argv[1:])
    available = {path.name.upper(): path for path in data_directory.iterdir()} if data_directory.is_dir() else {}
    required = (("DEFAULT.RPL",), ("DEFAULT.TRK",), ("GAME.RES", "GAME.PRE"),
                ("CARCOUN.RES", "CARCOUN.PRE"), ("STCOUN.P3S", "STCOUN.3SH"),
                ("CARLM02.RES", "CARLM02.PRE"), ("STLM02.P3S", "STLM02.3SH"))
    missing = [" or ".join(names) for names in required if not any(name in available for name in names)]
    if missing:
        print("SKIP: original Stunts resources are needed: " + "; ".join(missing), flush=True)
        return 77
    environment = dict(os.environ, SDL_VIDEODRIVER="restunts-invalid-driver",
                       SDL_AUDIODRIVER="restunts-invalid-driver", RESTUNTS_AUDIO_TRACE="")
    default = available["DEFAULT.RPL"].read_bytes()
    track = available["DEFAULT.TRK"].read_bytes()
    with zipfile.ZipFile(corpus) as archive:
        seed = archive.read("0000.rpl")

    with tempfile.TemporaryDirectory(prefix="ghsearch-") as directory:
        temporary = Path(directory)

        def run(arguments, *, expected=0):
            command = [str(genghost), "--data-dir", str(data_directory), *map(str, arguments)]
            result = subprocess.run(command, cwd=temporary, env=environment, capture_output=True,
                                    text=True, timeout=90)
            if result.returncode != expected:
                raise AssertionError(f"Command exited {result.returncode}, expected {expected}: {command}\n"
                                     f"{result.stdout}{result.stderr}")
            return result

        def verify(path, car, flags=()):
            verified = json.loads(run([path, car, "--verify", *flags]).stdout)
            assert verified["finished"], verified
            return verified

        def independent_playback(path, report, flags):
            result = subprocess.run([str(repldump), "--data-dir", str(data_directory),
                                     str(path), "1", *flags],
                                    cwd=temporary, env=environment, capture_output=True,
                                    text=True, timeout=90)
            if result.returncode:
                raise AssertionError(f"Independent replay failed: {result.stdout}{result.stderr}")
            dump = path.with_suffix(".BNI").read_bytes()
            count = struct.unpack_from("<H", dump)[0]
            assert len(dump) == 2 + count * GAMESTATE_SIZE
            assert count == report["replay_frames"]
            # Fixed offsets in the original serialized GAMESTATE (stateio.c).
            state = dump[-GAMESTATE_SIZE:]
            assert state[0x21B] == 3  # playerstate.car_crashBmpFlag == CRASH_EVENT_FINISH
            total, finish, penalty = (struct.unpack_from("<H", state, offset)[0]
                                      for offset in (0x142, 0x146, 0x14A))
            assert (finish, penalty, total) == (report["finish_frame"], report["penalty_frames"],
                                                report["total_frames"])
            if "--owoot" in flags:
                assert path.with_suffix(".owo").read_text() == "pass"

        def generate(source, car, output, evaluations, flags=()):
            run([source, car, "--evaluations", evaluations, "--seed", "1", "--output", output, *flags])
            path = temporary / output
            recording = path.read_bytes()
            report = json.loads(path.with_name(path.name + ".json").read_text())
            assert recording[:4] == car.encode("ascii")
            assert recording[6] == 0  # Generated ghosts are solo races.
            frame_rate, count = struct.unpack_from("<HH", recording, 22)
            assert frame_rate == 20 and len(recording) == REPLAY_INPUT_OFFSET + count
            assert report["replay_frames"] == count and report["evaluations"] == evaluations
            assert report["finished"] and not report["globally_optimal"]
            assert 0 <= report["guided_improvements"] <= report["guided_evaluations"] <= report["input_evaluations"]
            assert report["policy_evaluations"] + report["input_evaluations"] <= evaluations
            assert 0 <= report["tile_improvements"] <= report["tile_evaluations"] <= evaluations
            assert 0 <= report["dualway_improvements"] <= report["dualway_evaluations"] <= evaluations
            assert report["dualway_pairs"] >= 0
            assert report["routes_finished"] <= report["routes_attempted"] <= report["route_combinations"]
            assert report["physics"] == {"pg": "--pg:off" not in flags, "lc": "--lc:off" not in flags,
                                         "lcb": "--lcb:off" not in flags, "owoot": "--owoot" in flags}
            assert report["total_seconds"] == report["total_frames"] / 20
            verified = verify(path, car, flags)
            for field in ("finish_frame", "penalty_frames", "total_frames"):
                assert verified[field] == report[field], (field, verified, report)
            independent_playback(path, report, flags)
            return recording, report

        # A genuine track-only search: there are no seed driving inputs.
        (temporary / "track.TRK").write_bytes(track)
        (temporary / "source.RPL").write_bytes(default)
        first, first_report = generate("track.TRK", "COUN", "first.RPL", 200)
        second, second_report = generate("track.TRK", "COUN", "second.RPL", 200)
        assert first == second, "Fixed evaluation budget and seed changed the replay"
        for report in (first_report, second_report):
            report.pop("search_seconds")
            assert report["route_combinations"] == 2 and report["routes_attempted"] == 2
            assert not report["seed_replay_finished"] and report["seed_total_frames"] == 0
            assert report["tile_evaluations"] > 0, "Tile-line search was not exercised"
            assert report["dualway_pairs"] == 6164
            assert report["dualway_evaluations"] > 0, "Dual-way alternatives were not exercised"
            assert report["policy_evaluations"] + report["input_evaluations"] == report["evaluations"]
        assert first_report == second_report, "Repeatable search metrics differ"
        assert first[REPLAY_HEADER_SIZE:REPLAY_INPUT_OFFSET] == track
        print("Track-only search, both branches, deterministic replay, and independent playback passed", flush=True)

        # Replay extraction must support changing the original replay's car.
        extracted, _ = generate("source.RPL", "COUN", "extract.RPL", 200)
        assert default[:4] != extracted[:4]
        assert extracted[REPLAY_HEADER_SIZE:REPLAY_INPUT_OFFSET] == default[REPLAY_HEADER_SIZE:REPLAY_INPUT_OFFSET]
        _, clean_report = generate("track.TRK", "COUN", "clean.RPL", 2000, RESTRICTED)
        assert clean_report["route_combinations"] == 2 and clean_report["routes_attempted"] == 2
        assert clean_report["tile_evaluations"] == clean_report["tile_improvements"] == 0
        assert clean_report["dualway_evaluations"] == clean_report["dualway_improvements"] == 0
        _, fixed_report = generate("track.TRK", "COUN", "fixed.RPL", 500,
                                   ("--pg:off", "--lc:off", "--lcb:off"))
        assert fixed_report["dualway_pairs"] == 6164
        assert fixed_report["dualway_evaluations"] > 0, "Collision/PG fixes incorrectly disabled switching"
        print("Replay track extraction and OWOOT with all three bug fixes passed", flush=True)

        (temporary / "seed.RPL").write_bytes(seed)
        seed_score = verify("seed.RPL", "LM02")["total_frames"]
        _, seeded_report = generate("seed.RPL", "LM02", "seeded.RPL", 100)
        assert seeded_report["total_frames"] <= seed_score, "Search regressed its valid input replay"
        assert seeded_report["seed_replay_finished"] and seeded_report["seed_total_frames"] == seed_score
        assert seeded_report["policy_evaluations"] + seeded_report["input_evaluations"] + 1 == 100
        assert seeded_report["guided_evaluations"] > 0, "Seed refinement did not exercise trajectory guidance"
        assert seeded_report["tile_evaluations"] == seeded_report["tile_improvements"] == 0
        assert seeded_report["dualway_evaluations"] == seeded_report["dualway_improvements"] == 0

        # The game's picker ignores filenames longer than twelve bytes. Default
        # outputs must remain selectable even with a long or unusual source name.
        for source, destination in (("seed.RPL", "SEE-LM02.rpl"),
                                    ("a b long.RPL", "A_B-LM02.rpl"),
                                    ("x.RPL", "X-LM02.rpl")):
            (temporary / source).write_bytes(seed)
            run([source, "LM02", "--evaluations", "1"])
            generated = temporary / destination
            assert generated.is_file(), f"Missing default replay {destination}"
            assert len(generated.stem) <= 8 and len(generated.name.encode("ascii")) <= 12
            assert generated.suffix.lower() == ".rpl"
            assert verify(generated, "LM02")["total_frames"] <= seed_score
        print("Default DOS 8.3 replay names remain selectable in the game", flush=True)

        # Waiting until the recording limit is legal. Keep the final finishing
        # input but remove trailing replay padding: this caught zero-sized windows.
        late = bytearray(seed[:REPLAY_INPUT_OFFSET] + (bytes(9709) + seed[REPLAY_INPUT_OFFSET:])[:12000])
        struct.pack_into("<H", late, 24, 12000)
        (temporary / "late.RPL").write_bytes(late)
        late_score = verify("late.RPL", "LM02")["total_frames"]
        _, late_report = generate("late.RPL", "LM02", "lateseed.RPL", 1)
        assert late_report["total_frames"] <= late_score and late_report["replay_frames"] <= 12000
        print("Seed quality preservation and the 12000-frame late finish passed", flush=True)

        # Information flags work without a valid data directory or video driver.
        for option in ("--help", "--version"):
            result = run(["--data-dir", temporary / "missing", option])
            assert "genghost" in result.stdout

        malformed = bytearray(seed)
        struct.pack_into("<H", malformed, 24, len(seed) - REPLAY_INPUT_OFFSET + 1)
        (temporary / "bad.RPL").write_bytes(malformed)
        (temporary / "bad.TRK").write_bytes(b"truncated track")
        failures = [
            ["track.TRK", "COUN", "--evaluations", "0"],
            ["track.TRK", "COUN", "--evaluations", "-1"],
            ["track.TRK", "COUN", "--seed", "4294967296"],
            ["track.TRK", "COUN", "--seconds", "nan"],
            ["track.TRK", "COUN", "--pg:maybe"],
            ["track.TRK", "COUN", "--transmission", "invalid"],
            ["track.TRK", "TOOLONG"],
            ["bad.RPL", "LM02"],
            ["bad.TRK", "COUN"],
            ["absent.RPL", "COUN"],
            ["track.TRK", "COUN", "--verify"],
            ["seed.RPL", "COUN", "--verify"],
        ]
        for arguments in failures:
            run([*arguments, "--output", "invalid.RPL"], expected=1)
            assert not (temporary / "invalid.RPL").exists()
            assert not (temporary / "invalid.RPL.json").exists()
        print("Invalid arguments, malformed sources, and asset-free information flags passed", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
