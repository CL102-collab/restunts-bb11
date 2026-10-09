#!/usr/bin/env python3
"""Verify explicit replay refinement, seed preservation, and native playback."""

import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zipfile


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
                                    text=True, timeout=timeout)
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
                                      env=environment, capture_output=True, text=True, timeout=30)
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

        def refine(output, *, source="human.RPL", car="PMIN", strategy="legacy",
                   cores=1, budget=2001, flags=(), transmission=None):
            baseline = score(source, car, flags)
            original = (temporary / source).read_bytes()
            arguments = [source, car, "--fine-tune", "--strategy", strategy,
                         "--cores", cores, "--evaluations", budget, "--seed", 1,
                         "--output", output, *flags]
            if transmission:
                arguments.extend(("--transmission", transmission))
            run(arguments)
            report, states = verify(output, car, flags)
            saved = baseline["total_frames"] - report["total_frames"]
            assert saved >= 0, (baseline, report)
            assert report["fine_tune"] is True
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
            assert report["transmission"] == ("auto" if original[5] else "manual")
            assert report["physics"] == {
                "pg": "--pg:off" not in flags, "lc": "--lc:off" not in flags,
                "lcb": "--lcb:off" not in flags, "owoot": "--owoot" in flags}
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
            assert replay[5] == original[5]
            track_name = original[13:22].split(b"\0")[0]
            assert replay[13:22].split(b"\0")[0] == track_name, (
                "Embedded track identity was replaced by a filename")
            assert report["track"] == track_name.decode("ascii")
            assert replay[26:INPUT_OFFSET] == original[26:INPUT_OFFSET]
            return replay, report, states

        assert "--fine-tune" in run(["--help"]).stdout
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
            ["human.RPL", "COUN"],
            ["human.RPL", "PMIN", "--transmission", "auto"],
            ["opponent.RPL", "PMIN"],
            ["tenhz.RPL", "PMIN"],
            ["unfinished.RPL", "PMIN"],
            ["broken.RPL", "PMIN"],
            ["human.RPL", "PMIN", "--pg:off"],
            ["human.RPL", "PMIN", "--owoot"],
            ["human.RPL", "PMIN", "--verify"],
        ]
        for index, arguments in enumerate(rejected):
            output = f"bad{index}.RPL"
            original = (temporary / arguments[0]).read_bytes()
            result = run([*arguments, "--fine-tune", "--evaluations", 1,
                          "--cores", 2, "--output", output], success=False)
            assert result.stderr.strip(), "Rejected input needs a useful diagnostic"
            assert not (temporary / output).exists()
            assert not (temporary / (output + ".json")).exists()
            assert (temporary / arguments[0]).read_bytes() == original
        print("Incompatible, unfinished, physics-invalid, and conflicting inputs are rejected", flush=True)

        # Refusal must happen before any save callback can overwrite the input,
        # including aliases that do not share the same spelling.
        aliases = ["human.RPL", "./human.RPL"]
        if os.name == "posix":
            (temporary / "alias.RPL").symlink_to("human.RPL")
            os.link(temporary / "human.RPL", temporary / "hard.RPL")
            aliases.extend(("alias.RPL", "hard.RPL"))
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
            _, minimum, _ = refine("min" + strategy[:1] + ".RPL", strategy=strategy,
                                    budget=1, cores=2)
            assert minimum["total_frames"] == baseline["total_frames"]
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

        for strategy in ("legacy", "state"):
            for explicit in (None, "auto"):
                output = ("autol" if strategy == "legacy" else "autos") + ("x" if explicit else "i") + ".RPL"
                refine(output, source="auto seed.RPL", car="LM02", strategy=strategy,
                       budget=17, cores=2, transmission=explicit)
        print("Human refinement improves a native score; automatic transmission reaches every worker", flush=True)

        if native:
            subprocess.run([str(native), "--data-dir", str(data),
                            str(temporary / "auto seed.RPL"), "1", "--test-fine-tune"],
                           cwd=temporary, env=environment, check=True, timeout=90)
            print("Native engines preserve the baseline and reject invalid direct-call seeds", flush=True)

        # Build a seed legal under the selected restrictions without relying on
        # whether a historical human replay obeyed modern native OWOOT checks.
        run(["track.TRK", "COUN", "--evaluations", 1000, "--output", "clean.RPL", *RESTRICTED])
        clean, _ = verify("clean.RPL", "COUN", RESTRICTED)
        assert clean["fine_tune"] is False
        assert all(clean[key] is None for key in COMPARISON_FIELDS)
        for strategy in ("legacy", "state"):
            refine(("cleanl" if strategy == "legacy" else "cleans") + ".RPL",
                   source="clean.RPL", car="COUN", strategy=strategy,
                   cores=2, budget=33, flags=RESTRICTED)
        print("Both refinement strategies preserve OWOOT and all physics restrictions", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
