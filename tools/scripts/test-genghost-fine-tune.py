#!/usr/bin/env python3
"""Verify manual replay refinement, automatic seed conversion, and native playback."""

import errno
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zipfile

from regression_timeouts import scaled_timeout


INPUT_OFFSET = 1828
STATE_SIZE = 1120
RESTRICTED = ("--owoot", "--pg:off", "--lc:off", "--lcb:off")
COMPARISON_FIELDS = ("baseline_total_frames", "baseline_total_seconds", "improved",
                     "saved_frames", "saved_seconds")


def main():
    if len(sys.argv) not in (5, 6):
        print("Usage: test-genghost-fine-tune.py GENGHOST REPLDUMP DATA_DIR CORPUS [NATIVE_TEST]")
        return 1
    binary, repldump, data, corpus = (Path(value).resolve() for value in sys.argv[1:5])
    native = Path(sys.argv[5]).resolve() if len(sys.argv) == 6 else None
    available = {p.name.upper(): p for p in data.iterdir()} if data.is_dir() else {}
    required = (("DEFAULT.TRK",), ("GAME.RES", "GAME.PRE"),
                ("CARPMIN.RES", "CARPMIN.PRE"), ("STPMIN.P3S", "STPMIN.3SH"),
                ("CARCOUN.RES", "CARCOUN.PRE"), ("STCOUN.P3S", "STCOUN.3SH"),
                ("CARLM02.RES", "CARLM02.PRE"), ("STLM02.P3S", "STLM02.3SH"))
    missing = [" or ".join(names) for names in required if not any(n in available for n in names)]
    if missing or not corpus.is_file():
        print("SKIP: Stunts resources and golden replays are needed: " + "; ".join(missing))
        return 77
    environment = dict(os.environ, SDL_VIDEODRIVER="restunts-invalid-driver",
                       SDL_AUDIODRIVER="restunts-invalid-driver", RESTUNTS_AUDIO_TRACE="")
    with zipfile.ZipFile(corpus) as archive:
        human = archive.read("0722.rpl")  # Ayrton's 625-frame DEFAULT/PMIN lap.
        automatic = archive.read("0000.rpl")
    assert human[:4] == b"PMIN" and human[5] == 0 and automatic[5] == 1

    # Keep replay basenames short for the independent driver's legacy buffers.
    # Spaces in the directory and source name exercise worker argument passing.
    with tempfile.TemporaryDirectory(prefix="gh fine-") as directory:
        temporary = Path(directory)
        (temporary / "human.RPL").write_bytes(human)
        (temporary / "auto seed.RPL").write_bytes(automatic)
        (temporary / "track.TRK").write_bytes(human[26:INPUT_OFFSET])

        def run(arguments, *, success=True, timeout=90):
            result = subprocess.run([str(binary), "--data-dir", str(data), *map(str, arguments)],
                                    cwd=temporary, env=environment, capture_output=True,
                                    text=True, timeout=scaled_timeout(timeout))
            assert (result.returncode == 0) == success, (
                arguments, result.returncode, result.stdout, result.stderr)
            return result

        def score(source, car, flags=()):
            value = json.loads(run([source, car, "--verify", *flags]).stdout)
            assert value["finished"], value
            return value

        def verify(output, car, flags=()):
            report = json.loads((temporary / (output + ".json")).read_text())
            fresh = score(output, car, flags)
            assert report["finished"]
            for key in ("finish_frame", "penalty_frames", "total_frames"):
                assert report[key] == fresh[key], (key, report, fresh)
            check = temporary / "check.RPL"
            check.write_bytes((temporary / output).read_bytes())
            playback = subprocess.run([str(repldump), "--data-dir", str(data),
                                       str(check), "1", *flags], cwd=temporary,
                                      env=environment, capture_output=True, text=True, timeout=scaled_timeout(30))
            assert playback.returncode == 0, (playback.stdout, playback.stderr)
            states = check.with_suffix(".BNI").read_bytes()
            count = struct.unpack_from("<H", states)[0]
            assert len(states) == 2 + count * STATE_SIZE
            assert count == report["replay_frames"]
            final = states[-STATE_SIZE:]
            assert final[0x21B] == 3, "Independent replay did not finish"
            total, finish = (struct.unpack_from("<H", final, offset)[0]
                             for offset in (0x142, 0x146))
            assert (total, finish, (total - finish) & 0xFFFF) == (
                report["total_frames"], report["finish_frame"], report["penalty_frames"])
            if "--owoot" in flags:
                assert check.with_suffix(".owo").read_text() == "pass"
            return report, states

        def refine(output, *, source="human.RPL", car=None, strategy="legacy",
                   cores=1, budget=2001, flags=(), gears_only=False, seed=1):
            original = (temporary / source).read_bytes()
            replay_car = original[:4].decode("ascii")
            baseline = score(source, replay_car, flags)
            arguments = [source]
            if car is not None:
                arguments.append(car)
            arguments.extend(("--fine-tune-gears-only" if gears_only else "--fine-tune",
                              "--strategy", strategy, "--cores", cores,
                              "--evaluations", budget, "--seed", seed, "--output", output, *flags))
            process = run(arguments)
            if car is not None:
                assert "ignor" in process.stderr.lower() and "car" in process.stderr.lower(), process.stderr
            report, states = verify(output, replay_car, flags)
            saved = baseline["total_frames"] - report["total_frames"]
            assert saved >= 0, (baseline, report)
            assert report["fine_tune"] is True
            assert report["fine_tune_gears_only"] is gears_only
            assert report["car"] == replay_car
            assert report["baseline_total_frames"] == baseline["total_frames"]
            assert report["baseline_total_seconds"] == baseline["total_frames"] / 20
            assert report["saved_frames"] == saved and report["saved_seconds"] == saved / 20
            assert report["improved"] is (saved > 0)
            assert report["seed_replay_finished"]
            assert report["seed_total_frames"] == baseline["total_frames"]
            assert report["evaluations"] == budget
            assert report["strategy"] == strategy
            assert report["cores_requested"] == cores
            assert report["cores_used"] == min(cores, budget)
            assert report["transmission"] == "manual"
            assert report["physics"] == {
                "pg": "--pg:off" not in flags, "lc": "--lc:off" not in flags,
                "lcb": "--lcb:off" not in flags, "dws": "--dws:off" not in flags,
                "owoot": "--owoot" in flags,
                "amiga_grass": "/amigagrass" in flags,
                "chicane_fast_grass": "--cfg:off" not in flags}
            # Local state macros share the historical policy counter. Only
            # whole-route pilots are forbidden; native task-level checks below
            # also verify that section continuations rejoin the retained lap.
            if strategy == "legacy":
                assert report["policy_evaluations"] == 0
                assert report["tile_evaluations"] == report["dualway_evaluations"] == 0
            else:
                assert report["policy_evaluations"] == report["state_macro_evaluations"]
                assert report["routes_attempted"] == 0
            assert report["globally_optimal"] is False
            assert (temporary / source).read_bytes() == original, "Fine-tuning modified its input"
            replay = (temporary / output).read_bytes()
            assert replay[5] == 0, "Fine-tuned replays must use manual transmission"
            if gears_only:
                assert len(replay) == len(original), "Gear refinement changed the input timeline"
                assert bytes(value & ~0x30 for value in replay[INPUT_OFFSET:]) == bytes(
                    value & ~0x30 for value in original[INPUT_OFFSET:]), (
                        "Gear refinement changed pedals/steering or the post-finish tail")
            track_name = original[13:22].split(b"\0")[0]
            assert replay[13:22].split(b"\0")[0] == track_name, (
                "Embedded track identity was replaced by a filename")
            assert report["track"] == track_name.decode("ascii")
            assert replay[26:INPUT_OFFSET] == original[26:INPUT_OFFSET]
            return replay, report, states

        help_text = run(["--help"]).stdout
        assert "--fine-tune" in help_text and "--fine-tune-gears-only" in help_text
        assert "--transmission" not in help_text
        baseline = score("human.RPL", "PMIN")
        assert baseline["total_frames"] == 625 and baseline["penalty_frames"] == 0

        opponent = bytearray(human)
        opponent[6] = 1
        slow = bytearray(human)
        struct.pack_into("<H", slow, 22, 10)
        unfinished = bytearray(human)
        unfinished[INPUT_OFFSET:] = bytes(len(unfinished) - INPUT_OFFSET)
        for name, contents in (("opponent.RPL", opponent), ("tenhz.RPL", slow),
                               ("unfinished.RPL", unfinished), ("broken.RPL", human[:-1])):
            (temporary / name).write_bytes(contents)
        rejected = [
            ["track.TRK", "PMIN"],
            ["opponent.RPL", "PMIN"],
            ["tenhz.RPL", "PMIN"],
            ["unfinished.RPL", "PMIN"],
            ["broken.RPL", "PMIN"],
            ["human.RPL", "PMIN", "--pg:off"],
            ["human.RPL", "PMIN", "--owoot"],
            ["human.RPL", "PMIN", "--verify"],
        ]
        for mode in ("--fine-tune", "--fine-tune-gears-only"):
            for index, arguments in enumerate(rejected):
                output = f"bad{index}.RPL"
                original = (temporary / arguments[0]).read_bytes()
                result = run([*arguments, mode, "--evaluations", 1,
                              "--cores", 2, "--output", output], success=False)
                assert result.stderr.strip(), "Rejected input needs a useful diagnostic"
                assert not (temporary / output).exists()
                assert not (temporary / (output + ".json")).exists()
                assert (temporary / arguments[0]).read_bytes() == original
        for index, arguments in enumerate((
                ["human.RPL", "--fine-tune", "--fine-tune-gears-only"],
                ["human.RPL", "--fine-tune-gears-only", "--fine-tune"],
                ["human.RPL", "--fine-tune", "--transmission", "manual"],
                ["auto seed.RPL", "--fine-tune-gears-only", "--transmission", "auto"],
                ["track.TRK", "PMIN", "--transmission", "manual"])):
            output = f"args{index}.RPL"
            result = run([*arguments, "--evaluations", 1, "--output", output], success=False)
            assert result.stderr.strip()
            if "--transmission" in arguments:
                assert "unknown" in result.stderr.lower() and "--transmission" in result.stderr
            assert not (temporary / output).exists()
            assert not (temporary / (output + ".json")).exists()
        print("Incompatible, unfinished, physics-invalid, and conflicting inputs are rejected", flush=True)

        # Refusal must happen before any save callback can overwrite the input,
        # including aliases that do not share the same spelling.
        aliases = ["human.RPL", "./human.RPL"]
        if os.name == "posix":
            (temporary / "alias.RPL").symlink_to("human.RPL")
            aliases.append("alias.RPL")
            try:
                os.link(temporary / "human.RPL", temporary / "hard.RPL")
            except OSError as error:
                # Haiku's BFS rejects hard links with B_NOT_ALLOWED (EPERM).
                # Keep the other aliases and all refinement checks enabled.
                if not sys.platform.startswith("haiku") or error.errno != errno.EPERM:
                    raise
                print("SKIP: hard-link alias check (unsupported by Haiku BFS)", flush=True)
            else:
                aliases.append("hard.RPL")
        for output in aliases:
            run(["human.RPL", "PMIN", "--fine-tune", "--evaluations", 1,
                 "--output", output], success=False)
            assert (temporary / "human.RPL").read_bytes() == human
        (temporary / "report.RPL.json").write_bytes(human)
        run(["report.RPL.json", "PMIN", "--fine-tune", "--evaluations", 1,
             "--output", "report.RPL"], success=False)
        assert (temporary / "report.RPL.json").read_bytes() == human
        assert not (temporary / "report.RPL").exists()
        (temporary / "ABCDE-FT.rpl").write_bytes(human)
        run(["ABCDE-FT.rpl", "PMIN", "--fine-tune", "--evaluations", 1], success=False)
        assert (temporary / "ABCDE-FT.rpl").read_bytes() == human

        run(["human.RPL", "PMIN", "--fine-tune", "--evaluations", 1])
        default = temporary / "HUMAN-FT.rpl"
        assert default.is_file() and len(default.stem) == 8
        default_report, _ = verify(default.name, "PMIN")
        assert default_report["baseline_total_frames"] == default_report["total_frames"] == 625
        assert default_report["improved"] is False and default_report["saved_frames"] == 0
        assert default.read_bytes()[INPUT_OFFSET:INPUT_OFFSET + baseline["finish_frame"]] == (
            human[INPUT_OFFSET:INPUT_OFFSET + baseline["finish_frame"]])
        assert default.read_bytes()[13:22] == human[13:22]
        assert (temporary / "human.RPL").read_bytes() == human
        for strategy in ("legacy", "state"):
            for gears_only in (False, True):
                _, minimum, _ = refine(f"min{strategy[0]}{int(gears_only)}.RPL",
                                        strategy=strategy, budget=1, cores=2,
                                        gears_only=gears_only)
                assert minimum["total_frames"] == baseline["total_frames"]
        for gears_only in (False, True):
            for index, ignored in enumerate(("COUN", "malformed-car-code", "x")):
                refine(f"car{int(gears_only)}{index}.RPL", car=ignored, budget=1,
                       gears_only=gears_only)
        print("Input aliases are protected; default output preserves the validated seed", flush=True)

        improvements = []
        for strategy in ("legacy", "state"):
            first = refine(strategy + "1.RPL", strategy=strategy, cores=1)
            parallel = refine(strategy + "2.RPL", strategy=strategy, cores=2)
            improvements.extend((first[1]["saved_frames"], parallel[1]["saved_frames"]))
            if strategy == "state":
                assert first[0] == parallel[0], "State fine-tuning changed inputs with worker count"
                assert first[2] == parallel[2], "State fine-tuning changed native states with worker count"
                assert first[1]["state_task_hash"] == parallel[1]["state_task_hash"]
            print(f"{strategy}: saved {first[1]['saved_frames']} / {parallel[1]['saved_frames']} "
                  "frames with one / two workers", flush=True)
        if not any(improvements):
            deeper = refine("deeper.RPL", budget=10001, cores=2)
            improvements.append(deeper[1]["saved_frames"])
        assert any(value > 0 for value in improvements), "No real human lap improvement was demonstrated"

        automatic_baseline = score("auto seed.RPL", "LM02")
        for strategy in ("legacy", "state"):
            for cores in (1, 2):
                output = f"auto{strategy[0]}{cores}.RPL"
                replay, report, _ = refine(output, source="auto seed.RPL", car="LM02",
                                          strategy=strategy, budget=1, cores=cores)
                assert report["total_frames"] == automatic_baseline["total_frames"]
                finish = automatic_baseline["finish_frame"]
                before = automatic[INPUT_OFFSET:INPUT_OFFSET + finish]
                after = replay[INPUT_OFFSET:INPUT_OFFSET + finish]
                assert len(before) == len(after) == finish
                assert bytes(value & ~0x30 for value in before) == bytes(
                    value & ~0x30 for value in after), "Conversion changed pedals or steering"
                assert any(value & 0x30 for value in after), "Automatic shifts were not recorded"
            # These seeds keep every pedal/steering input through the new
            # finish intact: both the upshift and downshift schedules improve.
            normalized = replay
            shifted, report, _ = refine(f"shift{strategy[0]}.RPL", source="auto seed.RPL",
                                        car="LM02", strategy=strategy, budget=257,
                                        seed=2 if strategy == "legacy" else 1)
            assert report["saved_frames"] > 0, "Gear timing did not improve the automatic lap"
            finish = report["finish_frame"]
            before = normalized[INPUT_OFFSET:INPUT_OFFSET + finish]
            after = shifted[INPUT_OFFSET:INPUT_OFFSET + finish]
            assert bytes(value & ~0x30 for value in before) == bytes(
                value & ~0x30 for value in after), "Gear-only refinement changed driving controls"
            for gear in (0x10, 0x20):
                assert any((old ^ new) & gear for old, new in zip(before, after)), (
                    "Both upshift and downshift timing should improve", strategy, gear)
            for cores in (1, 2):
                _, preserved, _ = refine(f"gmin{strategy[0]}{cores}.RPL", source="auto seed.RPL",
                                          strategy=strategy, budget=1, cores=cores, gears_only=True)
                assert preserved["total_frames"] == automatic_baseline["total_frames"]
                shifted, report, _ = refine(f"gear{strategy[0]}{cores}.RPL", source="auto seed.RPL",
                                            strategy=strategy, budget=257, cores=cores, gears_only=True)
                assert report["saved_frames"] > 0, "Gear-only search did not improve the automatic lap"
                before = normalized[INPUT_OFFSET:]
                after = shifted[INPUT_OFFSET:]
                for gear in (0x10, 0x20):
                    assert any((old ^ new) & gear for old, new in zip(before, after)), (
                        "Both upshift and downshift timing should improve", strategy, cores, gear)
        print("Fine-tuning converts automatic seeds to manual and preserves native scores on every worker", flush=True)

        if native:
            subprocess.run([str(native), "--data-dir", str(data),
                            str(temporary / "auto seed.RPL"), "1", "--test-fine-tune"],
                           cwd=temporary, env=environment, check=True, timeout=scaled_timeout(90))
            print("Native engines preserve the baseline and reject invalid direct-call seeds", flush=True)

        # Build a seed legal under the selected restrictions without relying on
        # whether a historical human replay obeyed modern native OWOOT checks.
        run(["track.TRK", "COUN", "--evaluations", 1000, "--output", "clean.RPL", *RESTRICTED])
        clean, _ = verify("clean.RPL", "COUN", RESTRICTED)
        assert clean["fine_tune"] is False and clean["fine_tune_gears_only"] is False
        assert all(clean[key] is None for key in COMPARISON_FIELDS)
        for strategy in ("legacy", "state"):
            refine(("cleanl" if strategy == "legacy" else "cleans") + ".RPL",
                   source="clean.RPL", car="COUN", strategy=strategy,
                   cores=2, budget=33, flags=RESTRICTED)
        print("Both refinement strategies preserve OWOOT and all physics restrictions", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
