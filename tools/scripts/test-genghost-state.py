#!/usr/bin/env python3
"""Exercise native state expansion, cooperative search, rules, and saved replays."""

import json
import os
from pathlib import Path
import signal
import struct
import subprocess
import sys
import tempfile
import time
import zipfile


INPUT_OFFSET = 1828
GAMESTATE_SIZE = 1120
RESTRICTED = ("--owoot", "--pg:off", "--lc:off", "--lcb:off")


def children(parent):
    path = Path(f"/proc/{parent}/task/{parent}/children")
    try:
        return [int(value) for value in path.read_text().split()]
    except FileNotFoundError:
        result = []
        for candidate in Path("/proc").iterdir():
            if not candidate.name.isdecimal():
                continue
            try:
                data = (candidate / "stat").read_text()
                if int(data[data.rfind(")") + 2:].split()[1]) == parent:
                    result.append(int(candidate.name))
            except (FileNotFoundError, ProcessLookupError):
                pass
        return result


def alive(pid):
    try:
        data = Path(f"/proc/{pid}/stat").read_text()
        return data[data.rfind(")") + 2:].split()[0] != "Z"
    except FileNotFoundError:
        return False


def main():
    if len(sys.argv) != 6:
        print("Usage: test-genghost-state.py GENGHOST REPLDUMP NATIVE_TEST DATA_DIR CORPUS")
        return 1
    genghost, repldump, native, data, corpus = (Path(value).resolve() for value in sys.argv[1:])
    available = {path.name.upper(): path for path in data.iterdir()} if data.is_dir() else {}
    required = (("DEFAULT.TRK",), ("GAME.RES", "GAME.PRE"),
                ("CARPMIN.RES", "CARPMIN.PRE"), ("STPMIN.P3S", "STPMIN.3SH"),
                ("CARCOUN.RES", "CARCOUN.PRE"), ("STCOUN.P3S", "STCOUN.3SH"),
                ("CARLM02.RES", "CARLM02.PRE"), ("STLM02.P3S", "STLM02.3SH"))
    missing = [" or ".join(names) for names in required if not any(name in available for name in names)]
    if missing or not corpus.is_file():
        print("SKIP: Stunts resources and golden replays are needed: " + "; ".join(missing), flush=True)
        return 77
    environment = dict(os.environ, SDL_VIDEODRIVER="restunts-invalid-driver",
                       SDL_AUDIODRIVER="restunts-invalid-driver", RESTUNTS_AUDIO_TRACE="")
    with zipfile.ZipFile(corpus) as archive:
        human = archive.read("0000.rpl")
        uphill = archive.read("0722.rpl")
        chicane = archive.read("r0720.rpl")
        airborne = archive.read("1152.rpl")
        failed_pilot = archive.read("1860.rpl")
    # Keep names short for the independent replay serializer's legacy buffers;
    # spaces in the directory and arguments still exercise worker IPC quoting.
    with tempfile.TemporaryDirectory(prefix="gh state-") as directory:
        temporary = Path(directory)
        (temporary / "track.TRK").write_bytes(available["DEFAULT.TRK"].read_bytes())
        (temporary / "human.RPL").write_bytes(human)
        (temporary / "uphill.RPL").write_bytes(uphill)
        (temporary / "airborne.RPL").write_bytes(airborne)

        def command(arguments):
            return [str(genghost), "--data-dir", str(data), *map(str, arguments)]

        def run(arguments, expected=0, timeout=90):
            result = subprocess.run(command(arguments), cwd=temporary, env=environment,
                                    capture_output=True, text=True, timeout=timeout)
            assert result.returncode == expected, (
                arguments, result.returncode, expected, result.stdout, result.stderr)
            return result

        def report(name):
            return json.loads((temporary / (name + ".json")).read_text())

        def verify(name, car, flags=()):
            result = report(name)
            fresh = json.loads(run([name, car, "--verify", *flags]).stdout)
            assert result["finished"] and fresh["finished"], (result, fresh)
            for key in ("finish_frame", "penalty_frames", "total_frames"):
                assert result[key] == fresh[key], (key, result, fresh)
            playback = temporary / "check.RPL"
            playback.write_bytes((temporary / name).read_bytes())
            checked = subprocess.run([str(repldump), "--data-dir", str(data),
                                      str(playback), "1", *flags], cwd=temporary,
                                     env=environment, capture_output=True, text=True, timeout=30)
            assert checked.returncode == 0, (checked.stdout, checked.stderr)
            states = playback.with_suffix(".BNI").read_bytes()
            count = struct.unpack_from("<H", states)[0]
            assert len(states) == 2 + count * GAMESTATE_SIZE
            assert count == result["replay_frames"]
            final = states[-GAMESTATE_SIZE:]
            assert final[0x21B] == 3, "Independent replay driver did not cross the finish"
            total, finish = (struct.unpack_from("<H", final, offset)[0]
                             for offset in (0x142, 0x146))
            assert (total, finish, (total - finish) & 0xFFFF) == (
                result["total_frames"], result["finish_frame"], result["penalty_frames"])
            if "--owoot" in flags:
                assert playback.with_suffix(".owo").read_text() == "pass"
            return result, states

        def generate(name, *, source="track.TRK", car="PMIN", budget=257,
                     cores=1, strategy="state", flags=()):
            run([source, car, "--strategy", strategy, "--cores", cores,
                 "--evaluations", budget, "--seed", 1, "--output", name, *flags])
            result, states = verify(name, car, flags)
            assert result["evaluations"] == budget, result
            assert result["strategy"] == strategy, result
            assert result["physics"] == {
                "pg": "--pg:off" not in flags, "lc": "--lc:off" not in flags,
                "lcb": "--lcb:off" not in flags, "owoot": "--owoot" in flags}
            assert result["globally_optimal"] is False
            assert result["total_seconds"] == result["total_frames"] / 20
            if strategy == "state":
                assert result["state_evaluations"] > 0, result
                assert result["state_macro_evaluations"] > 0, result
                assert result["state_frontier_peak"] > 1, result
                assert result["state_input_evaluations"] <= result["input_evaluations"], result
                if "--pg:off" in flags:
                    assert result["state_pg_evaluations"] == 0
                    assert result["state_pg_opportunities"] == 0
                if "--owoot" in flags:
                    assert result["state_chicane_evaluations"] == 0
            return (temporary / name).read_bytes(), result, states

        help_text = run(["--help"]).stdout
        assert "--strategy" in help_text and "state" in help_text and "legacy" in help_text
        bad = run(["--strategy", "unknown"], expected=1)
        assert "strategy" in (bad.stdout + bad.stderr).lower()

        # A native fixture verifies that planning never alters physics and
        # checkpoint reuse restores hidden route, penalty, collision and RNG
        # state, including prefixes differing in a single input byte.
        for prefix, flags in ((261, ("--expect-powergear",)), (200, ("--lc:off",)), (40, RESTRICTED)):
            subprocess.run([str(native), "--data-dir", str(data), str(temporary / "uphill.RPL"), str(prefix),
                            *flags], cwd=temporary, env=environment, check=True, timeout=90)
        subprocess.run([str(native), "--data-dir", str(data), str(temporary / "airborne.RPL"),
                        "440", "--expect-landing"], cwd=temporary, env=environment,
                       check=True, timeout=90)
        p962 = (("CARP962.RES", "CARP962.PRE"), ("STP962.P3S", "STP962.3SH"))
        if all(any(name in available for name in names) for names in p962):
            (temporary / "chicane.RPL").write_bytes(chicane)
            subprocess.run([str(native), "--data-dir", str(data), str(temporary / "chicane.RPL"), "1624",
                            "--expect-chicane"], cwd=temporary, env=environment,
                           check=True, timeout=90)
        (temporary / "recover.RPL").write_bytes(failed_pilot)
        subprocess.run([str(native), "--data-dir", str(data), str(temporary / "recover.RPL"),
                        "1", "--search-recovery"], cwd=temporary, env=environment,
                       check=True, timeout=90)
        print("Native checkpoint identity, maneuver rule gates, and chicane fast grass passed", flush=True)

        # Explicit legacy selection must preserve existing fixed-budget replay
        # behavior. A separately frozen release binary can strengthen this
        # compatibility check without becoming a CTest resource dependency.
        legacy, _, _ = generate("legacy.RPL", strategy="legacy", budget=201)
        legacy_repeat, _, _ = generate("repeat.RPL", strategy="legacy", budget=201)
        assert legacy == legacy_repeat
        frozen = os.environ.get("GENGHOST_LEGACY_BASELINE")
        if frozen:
            result = subprocess.run([frozen, "--data-dir", str(data), "track.TRK", "PMIN",
                                     "--cores", "1", "--evaluations", "201", "--seed", "1",
                                     "--output", "frozen.RPL"], cwd=temporary, env=environment,
                                    capture_output=True, text=True, timeout=90)
            assert result.returncode == 0, (result.stdout, result.stderr)
            assert legacy == (temporary / "frozen.RPL").read_bytes()
        print("Explicit legacy search preserves replay inputs", flush=True)

        # A single global evaluation can validate the supplied seed, but must
        # not start a funded expansion in each requested worker afterwards.
        run(["human.RPL", "LM02", "--strategy", "state", "--cores", "256",
             "--evaluations", "1", "--output", "budget1.RPL"])
        minimum, _ = verify("budget1.RPL", "LM02")
        assert minimum["evaluations"] == 1 and minimum["state_evaluations"] == 0
        assert minimum["cores_used"] == 1
        assert minimum["seed_replay_finished"]

        # Keep a complete valid human seed while exploring different states.
        # All worker counts must process the same fixed-budget strategy, with
        # byte-identical driving inputs and independent native replay states.
        first = generate("single.RPL", source="human.RPL", car="LM02", budget=129)
        runs = [first]
        for name, cores in (("two.RPL", 2), ("repeat2.RPL", 2), ("four.RPL", 4)):
            runs.append(generate(name, source="human.RPL", car="LM02", budget=129, cores=cores))
        for recording, result, states in runs:
            assert recording == first[0], "Changing core count changed deterministic replay inputs"
            assert states == first[2], "Changing core count changed native playback states"
            assert result["seed_replay_finished"]
            assert result["total_frames"] <= result["seed_total_frames"]
            for field, value in first[1].items():
                if field.startswith("state_") or field in ("evaluations", "routes_attempted",
                        "routes_finished", "policy_evaluations", "input_evaluations",
                        "guided_evaluations", "guided_improvements"):
                    assert result[field] == value, (field, result[field], value)
        print("One/two/four workers agree on fixed-budget search, replay, and native states", flush=True)

        # A deeper budget reaches the separately retained section frontier.
        # Repeat at different worker counts after its coordinated continuations
        # and fine input refinements have affected selection, not only the
        # initial full-lap scouts and the first expansion from the grid.
        deep = generate("deep1.RPL", source="human.RPL", car="LM02", budget=2501)
        parallel_deep = generate("deep4.RPL", source="human.RPL", car="LM02",
                                 budget=2501, cores=4)
        assert deep[1]["state_section_starts"] > 0, deep[1]
        assert deep[1]["state_section_evaluations"] > 0, deep[1]
        assert deep[1]["state_input_evaluations"] > 0, deep[1]
        assert deep[1]["guided_evaluations"] > 0, deep[1]
        assert parallel_deep[0] == deep[0] and parallel_deep[2] == deep[2]
        for field, value in deep[1].items():
            if field.startswith("state_") or field in ("evaluations", "routes_attempted",
                    "routes_finished", "policy_evaluations", "input_evaluations",
                    "guided_evaluations", "guided_improvements"):
                assert parallel_deep[1][field] == value, (field, parallel_deep[1][field], value)
        print("Section frontiers, continuations, and input refinement remain identical across cores", flush=True)

        generated = generate("trackonly.RPL", budget=257, cores=2)
        assert generated[0][26:INPUT_OFFSET] == available["DEFAULT.TRK"].read_bytes()
        assert not generated[1]["seed_replay_finished"]
        generate("fixed.RPL", budget=257, cores=2, flags=("--pg:off", "--lc:off", "--lcb:off"))
        generate("owoot.RPL", car="COUN", budget=2000, cores=2, flags=RESTRICTED)
        print("Track-only state search and matching-physics replay/OWOOT validation passed", flush=True)

        if os.name == "posix" and Path("/proc").is_dir():
            for mode in ("cancel", "failed"):
                log = temporary / (mode + ".log")
                name = mode + ".RPL"
                with log.open("w") as output:
                    process = subprocess.Popen(command(["human.RPL", "LM02", "--strategy", "state",
                        "--cores", "2", "--seconds", "0", "--output", name]),
                        cwd=temporary, env=environment, stdout=output, stderr=subprocess.STDOUT)
                    worker_ids = []
                    try:
                        deadline = time.monotonic() + 15
                        while time.monotonic() < deadline:
                            worker_ids = children(process.pid)
                            if len(worker_ids) == 2 and (temporary / (name + ".json")).is_file():
                                break
                            assert process.poll() is None, log.read_text()
                            time.sleep(0.05)
                        assert len(worker_ids) == 2, "No persistent native state workers were running"
                        assert (temporary / (name + ".json")).is_file(), log.read_text()
                        if mode == "cancel":
                            process.send_signal(signal.SIGINT)
                            assert process.wait(timeout=15) in (0, 130), log.read_text()
                        else:
                            os.kill(worker_ids[0], signal.SIGKILL)
                            assert process.wait(timeout=15) == 1, log.read_text()
                    finally:
                        if process.poll() is None:
                            process.kill()
                            process.wait(timeout=5)
                    assert not any(alive(pid) for pid in worker_ids), "Search left live native workers"
                    verify(name, "LM02")
            print("Cancellation and worker failure preserve a verified finish and reap all workers", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
