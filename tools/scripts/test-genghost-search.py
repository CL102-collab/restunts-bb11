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
                ("CARLM02.RES", "CARLM02.PRE"), ("STLM02.P3S", "STLM02.3SH"),
                ("CARPMIN.RES", "CARPMIN.PRE"), ("STPMIN.P3S", "STPMIN.3SH"))
    missing = [" or ".join(names) for names in required if not any(name in available for name in names)]
    if missing:
        print("SKIP: original Stunts resources are needed: " + "; ".join(missing), flush=True)
        return 77
    environment = dict(os.environ, SDL_VIDEODRIVER="restunts-invalid-driver",
                       SDL_AUDIODRIVER="restunts-invalid-driver", RESTUNTS_AUDIO_TRACE="")
    default = available["DEFAULT.RPL"].read_bytes()
    track = available["DEFAULT.TRK"].read_bytes()
    fallback_resources = (("CARZMP4.RES", "CARZMP4.PRE"), ("STZMP4.P3S", "STZMP4.3SH"))
    fallback_available = all(any(name in available for name in names) for names in fallback_resources)
    catalog_resources = (("CARGATE.RES", "CARGATE.PRE"), ("STGATE.P3S", "STGATE.3SH"))
    catalog_available = all(any(name in available for name in names) for names in catalog_resources)
    with zipfile.ZipFile(corpus) as archive:
        seed = archive.read("0000.rpl")
        tail = bytearray(archive.read("0722.rpl")[:REPLAY_INPUT_OFFSET + 642])
        catalog_track = (archive.read("r0389.rpl")[REPLAY_HEADER_SIZE:REPLAY_INPUT_OFFSET]
                         if catalog_available else None)
        fallback_track = (archive.read("1531.rpl")[REPLAY_HEADER_SIZE:REPLAY_INPUT_OFFSET]
                          if fallback_available else None)

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
            total, finish = (struct.unpack_from("<H", state, offset)[0]
                             for offset in (0x142, 0x146))
            # Canonical headless replay starts at elapsed_time1=0. Live route
            # penalties may change during the braking tail after score latching.
            penalty = (total - finish) & 0xFFFF
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
            assert 0 <= report["tactic_improvements"] <= report["tactic_evaluations"] <= evaluations
            assert report["dualway_pairs"] >= 0
            assert report["routes_finished"] <= report["routes_attempted"] <= report["route_combinations"]
            assert report["physics"] == {"pg": "--pg:off" not in flags, "lc": "--lc:off" not in flags,
                                         "lcb": "--lcb:off" not in flags, "owoot": "--owoot" in flags,
                                         "amiga_grass": "/amigagrass" in flags}
            assert report["total_seconds"] == report["total_frames"] / 20
            verified = verify(path, car, flags)
            for field in ("finish_frame", "penalty_frames", "total_frames"):
                assert verified[field] == report[field], (field, verified, report)
            independent_playback(path, report, flags)
            return recording, report

        # A small input delta from the winning ZCT079 replay crosses the finish
        # in 622 ticks, then its braking tail changes the live penalty tracker.
        # Scoring must keep the penalty latched at the finish, not that late value.
        changes = {
            329: 9, 402: 5, 406: 1, 413: 9, 421: 1, 422: 5, 448: 9, 505: 1,
            511: 9, 513: 1, 519: 9, 523: 9, 534: 9, 535: 9, 541: 1, 542: 1,
            547: 5, 548: 5, 549: 1, 550: 1, 557: 9, 558: 9, 559: 5, 560: 5,
            562: 1, 563: 1, 565: 5, 566: 9, 567: 9, 569: 5, 570: 5, 571: 1,
            597: 9, 598: 9, 602: 1, 603: 1, 605: 5, 606: 5, 622: 0, 623: 0,
            624: 0,
        }
        for frame, value in changes.items():
            tail[REPLAY_INPUT_OFFSET + frame] = value
        struct.pack_into("<H", tail, 24, 642)
        tail_path = temporary / "tail.RPL"
        tail_path.write_bytes(tail)
        tail_report = verify("tail.RPL", "PMIN")
        assert (tail_report["frames"], tail_report["finish_frame"], tail_report["total_frames"],
                tail_report["penalty_frames"]) == (642, 622, 622, 0), tail_report
        independent_playback(tail_path, {**tail_report, "replay_frames": 642}, ())
        dump = tail_path.with_suffix(".BNI").read_bytes()
        first_finish = next(2 + frame * GAMESTATE_SIZE for frame in range(642)
                            if dump[2 + frame * GAMESTATE_SIZE + 0x21B] == 3)
        assert struct.unpack_from("<H", dump, first_finish + 0x14A)[0] == 0
        assert struct.unpack_from("<H", dump, len(dump) - GAMESTATE_SIZE + 0x14A)[0] == 3420
        print("Finish score excludes penalties appearing only in the replay's braking tail", flush=True)

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

        # DEFAULT/PMIN originally followed the complete horizontal cork. Its
        # first route policies must use the short line, before replay mutations
        # or refinements of a finished parent can occur.
        _, early_report = generate("track.TRK", "PMIN", "early.RPL", 2)
        assert early_report["tactic_evaluations"] > 0, "Initial policies did not try the horizontal-cork cut"
        assert early_report["input_evaluations"] == 0, "Unexpected replay mutation in initial population"
        assert early_report["penalty_frames"] == 0

        # Inspect actual independent playback, rather than accepting a tactic
        # counter or a faster lap as evidence of bypassing the wall. DEFAULT's
        # horizontal cork occupies columns 12/13 of bottom-up object row 22;
        # its native origin is (13312, 23040), rotated one quarter turn. Local
        # +x is driving-right and local +z points along this traversal.
        assert track[22 * 30 + 12] == 86, "DEFAULT horizontal-cork fixture changed"
        dump = (temporary / "early.BNI").read_bytes()
        passage = []
        for frame in range(struct.unpack_from("<H", dump)[0]):
            offset = 2 + frame * GAMESTATE_SIZE + 0x152
            x, y, z = struct.unpack_from("<iii", dump, offset)
            roll = struct.unpack_from("<h", dump, offset + 28)[0]
            local_x, local_z = 23040 - z / 64, x / 64 - 13312
            if abs(local_x) < 512 and abs(local_z) <= 1024:
                passage.append((local_x, y / 64, local_z, abs((roll + 512) % 1024 - 512)))
        assert passage and passage[0][2] < -900 and passage[-1][2] > 900, "Cork was not crossed"
        assert all(after[2] > before[2] for before, after in zip(passage, passage[1:])), "Cork traversal turned back"
        beside_wall = [sample for sample in passage if abs(sample[2]) <= 64]
        assert beside_wall and all(sample[0] > 60 for sample in beside_wall), "Car did not pass right of the red wall"
        assert max(sample[1] for sample in passage) < 160, "Car climbed the full corkscrew"
        assert max(sample[3] for sample in passage) < 256, "Car rolled through the full corkscrew"
        print("Initial DEFAULT/PMIN replay crosses right of the cork wall without a full roll", flush=True)

        # Some cars/approaches need an ordinary traversal parent to discover
        # the rest of the lap. On ZCT141 the failed cut parents reached farther
        # and previously crowded out every ordinary fallback, losing a finish.
        # Extract only the map: the human replay must not seed this regression.
        if fallback_track is not None:
            (temporary / "zct141.TRK").write_bytes(fallback_track)
            _, fallback_report = generate("zct141.TRK", "ZMP4", "fallback.RPL", 5000)
            assert not fallback_report["seed_replay_finished"] and fallback_report["seed_total_frames"] == 0
            assert fallback_report["tactic_evaluations"] > 0, "Fallback fixture never attempted cork cuts"
            print("ZCT141/ZMP4 ordinary fallback finishes after unsuccessful initial cork cuts", flush=True)
        else:
            print("SKIP: optional ZCT141 fallback regression needs the custom ZMP4 car", flush=True)

        # A failed catalog proposal must not displace the ordinary policies
        # that can finish. R4K9/GATE previously lost a finish after 62 baseline
        # attempts and still failed at 3,000 when failed probes entered selection.
        # This map-only fixture finishes within 100 attempts with isolated probes.
        if catalog_track is not None:
            (temporary / "catalog.TRK").write_bytes(catalog_track)
            _, catalog_report = generate("catalog.TRK", "GATE", "catalog.RPL", 100, ("--lc:off",))
            assert not catalog_report["seed_replay_finished"] and catalog_report["seed_total_frames"] == 0
            print("Failed stunt probes preserve the ordinary R4K9/GATE completion lineage", flush=True)
        else:
            print("SKIP: optional catalog population regression needs the custom GATE car", flush=True)

        # Replay extraction must support changing the original replay's car.
        extracted, _ = generate("source.RPL", "COUN", "extract.RPL", 200)
        assert default[:4] != extracted[:4]
        assert extracted[REPLAY_HEADER_SIZE:REPLAY_INPUT_OFFSET] == default[REPLAY_HEADER_SIZE:REPLAY_INPUT_OFFSET]
        _, clean_report = generate("track.TRK", "COUN", "clean.RPL", 2000, RESTRICTED)
        assert clean_report["route_combinations"] == 2 and clean_report["routes_attempted"] == 2
        assert clean_report["tile_evaluations"] == clean_report["tile_improvements"] == 0
        assert clean_report["dualway_evaluations"] == clean_report["dualway_improvements"] == 0
        # A clear slalom center is a legal road line under OWOOT. The common
        # verifier above checks every saved frame against the native road rule;
        # tile cuts and dual-way switches remain disabled.
        assert clean_report["tactic_evaluations"] > 0, "Legal slalom center was never tried"
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
