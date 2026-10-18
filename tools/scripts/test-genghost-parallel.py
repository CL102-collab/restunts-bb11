#!/usr/bin/env python3
"""Exercise isolated ghost workers, budgets, native playback, and cleanup."""

import json
import os
from pathlib import Path
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time
import zipfile

from regression_timeouts import scaled_timeout


REPLAY_INPUT_OFFSET = 1828
GAMESTATE_SIZE = 1120
RESTRICTED = ("--owoot", "--pg:off", "--lc:off", "--lcb:off")


def deterministic(value):
    """Wall clocks differ even when the inputs and simulated states match."""
    if isinstance(value, dict):
        return {key: deterministic(item) for key, item in value.items()
                if key not in ("search_seconds", "elapsed_seconds")}
    if isinstance(value, list):
        return [deterministic(item) for item in value]
    return value


def process_workers(parent):
    """Read actual child PIDs and CPU time; do not infer workers from a log."""
    children = Path(f"/proc/{parent}/task/{parent}/children")
    try:
        pids = [int(value) for value in children.read_text().split()]
    except FileNotFoundError:
        # Some container kernels omit /task/PID/children. PPID in /proc/PID/stat
        # provides the same relationship without requiring that kernel feature.
        pids = [int(path.name) for path in Path("/proc").iterdir() if path.name.isdecimal()]
    workers = {}
    for pid in pids:
        try:
            state = Path(f"/proc/{pid}/stat").read_text()
            fields = state[state.rfind(")") + 2:].split()
            if int(fields[1]) != parent:
                continue
            arguments = Path(f"/proc/{pid}/cmdline").read_bytes().split(b"\0")
            marker = arguments.index(b"--worker-status")
            status_path = Path(os.fsdecode(arguments[marker + 1]))
            workers[pid] = (int(fields[11]) + int(fields[12]), status_path.parent)
        except (FileNotFoundError, ProcessLookupError, ValueError, IndexError):
            continue
    return workers


def process_live(pid):
    try:
        state = Path(f"/proc/{pid}/stat").read_text()
        return state[state.rfind(")") + 2:].split()[0] != "Z"
    except FileNotFoundError:
        return False


def main():
    if len(sys.argv) != 5:
        print("Usage: test-genghost-parallel.py GENGHOST REPLDUMP DATA_DIRECTORY CORPUS")
        return 1
    genghost, repldump, data_directory, corpus = (Path(value).resolve() for value in sys.argv[1:])
    available = ({path.name.upper(): path for path in data_directory.iterdir()}
                 if data_directory.is_dir() else {})
    required = (("DEFAULT.TRK",), ("GAME.RES", "GAME.PRE"),
                ("CARCOUN.RES", "CARCOUN.PRE"), ("STCOUN.P3S", "STCOUN.3SH"),
                ("CARPMIN.RES", "CARPMIN.PRE"), ("STPMIN.P3S", "STPMIN.3SH"),
                ("CARLM02.RES", "CARLM02.PRE"), ("STLM02.P3S", "STLM02.3SH"))
    missing = [" or ".join(names) for names in required
               if not any(name in available for name in names)]
    if missing or not corpus.is_file():
        print("SKIP: Stunts resources and golden replays are needed: " + "; ".join(missing), flush=True)
        return 77
    environment = dict(os.environ, SDL_VIDEODRIVER="restunts-invalid-driver",
                       SDL_AUDIODRIVER="restunts-invalid-driver", RESTUNTS_AUDIO_TRACE="")
    with zipfile.ZipFile(corpus) as archive:
        human_seed = archive.read("0000.rpl")

    # Spaces in both cwd and user paths catch accidentally shell-joined worker
    # arguments. Keep the root short enough for repldump's legacy path buffer.
    with tempfile.TemporaryDirectory(prefix="gh parallel-") as directory:
        temporary = Path(directory)
        (temporary / "a track.TRK").write_bytes(available["DEFAULT.TRK"].read_bytes())
        (temporary / "human seed.RPL").write_bytes(human_seed)
        game_directory = data_directory
        if os.name == "posix":
            game_directory = temporary / "game assets"
            game_directory.symlink_to(data_directory, target_is_directory=True)

        def command(arguments):
            return [str(genghost), "--data-dir", str(game_directory), *map(str, arguments)]

        def run(arguments, expected=0):
            result = subprocess.run(command(arguments), cwd=temporary, env=environment,
                                    capture_output=True, text=True, timeout=scaled_timeout(40))
            assert result.returncode == expected, (
                result.returncode, expected, arguments, result.stdout, result.stderr)
            return result

        def read_report(output):
            return json.loads((temporary / (output + ".json")).read_text())

        def verify(output, car, flags=()):
            report = read_report(output)
            checked = json.loads(run([output, car, "--verify", *flags]).stdout)
            assert checked["finished"] and report["finished"], (checked, report)
            for field in ("finish_frame", "penalty_frames", "total_frames"):
                assert checked[field] == report[field], (field, checked, report)
            # Use an independent executable/state serializer, with a short
            # filename: repldump does not share the worker coordinator.
            replay = temporary / "check.RPL"
            replay.write_bytes((temporary / output).read_bytes())
            physics_flags = [flag for flag in flags
                             if flag in ("--owoot", "/amigagrass")
                             or flag.startswith(("--pg:", "--lc:", "--lcb:"))]
            result = subprocess.run([str(repldump), "--data-dir", str(data_directory),
                                     str(replay), "1", *physics_flags], cwd=temporary, env=environment,
                                    capture_output=True, text=True, timeout=scaled_timeout(15))
            assert result.returncode == 0, (result.stdout, result.stderr)
            dump = replay.with_suffix(".BNI").read_bytes()
            count = struct.unpack_from("<H", dump)[0]
            assert len(dump) == 2 + count * GAMESTATE_SIZE
            assert count == report["replay_frames"]
            state = dump[-GAMESTATE_SIZE:]
            assert state[0x21B] == 3, "Independent native playback did not finish"
            total = struct.unpack_from("<H", state, 0x142)[0]
            finish = struct.unpack_from("<H", state, 0x146)[0]
            assert (total, finish, (total - finish) & 0xFFFF) == (
                report["total_frames"], report["finish_frame"], report["penalty_frames"])
            if "--owoot" in flags:
                assert replay.with_suffix(".owo").read_text() == "pass"
            return report

        def generate(output, *, source="a track.TRK", car="PMIN", evaluations=201,
                     cores=None, seed=1, flags=()):
            arguments = [source, car, "--evaluations", evaluations, "--seed", seed,
                         "--output", output, *flags]
            if cores is not None:
                arguments.extend(("--cores", cores))
            run(arguments)
            report = verify(output, car, flags)
            assert report["evaluations"] == evaluations, report
            assert report["physics"] == {
                "pg": "--pg:off" not in flags, "lc": "--lc:off" not in flags,
                "lcb": "--lcb:off" not in flags, "owoot": "--owoot" in flags,
                "amiga_grass": "/amigagrass" in flags}, report
            assert report["cores_requested"] == (1 if cores is None else cores), report
            workers = report["worker_results"]
            assert len(workers) == report["cores_used"]
            assert [worker["index"] for worker in workers] == list(range(len(workers)))
            assert sum(worker["budget_evaluations"] for worker in workers) == evaluations
            assert sum(worker["evaluations"] for worker in workers) == evaluations
            assert sum(worker["simulated_frames"] for worker in workers) == report["simulated_frames"]
            assert len({worker["seed"] for worker in workers}) == len(workers)
            assert workers[0]["seed"] == seed
            for index, worker in enumerate(workers):
                assert worker["budget_evaluations"] == (
                    evaluations // len(workers) + (index < evaluations % len(workers))), workers
                assert worker["status"] == (0 if worker["finished"] else 2), workers
            finishes = [worker["total_frames"] for worker in workers if worker["finished"]]
            assert report["total_frames"] == min(finishes), report
            winner = workers[report["winner_worker"]]
            assert winner["finished"] and winner["total_frames"] == report["total_frames"]
            assert winner["seed"] == report["winner_seed"]
            return (temporary / output).read_bytes(), report

        default, default_report = generate("default.RPL")
        explicit, explicit_report = generate("one core.RPL", cores=1)
        assert default == explicit, "Explicit one-core search changed legacy replay inputs"
        assert deterministic(default_report) == deterministic(explicit_report)
        assert default_report["cores_used"] == 1
        print("Default and --cores 1 preserve identical replays and deterministic metrics", flush=True)

        first, first_report = generate("two first.RPL", cores=2)
        second, second_report = generate("two repeat.RPL", cores=2)
        assert first == second, "Fixed-budget parallel search changed its replay across runs"
        assert deterministic(first_report) == deterministic(second_report)
        assert first_report["cores_used"] == 2
        # Independently run each island at exactly its reported seed and budget.
        # This detects dropped flags, duplicated seeds, per-worker full budgets,
        # and a coordinator selecting a worse finish or altering driving inputs.
        singles = []
        for worker in first_report["worker_results"]:
            recording, report = generate(f"worker{worker['index']}.RPL", cores=1,
                                         evaluations=worker["budget_evaluations"], seed=worker["seed"])
            assert worker["evaluations"] == report["evaluations"]
            assert worker["total_frames"] == report["total_frames"]
            assert worker["simulated_frames"] == report["simulated_frames"]
            singles.append(recording)
        assert first == singles[first_report["winner_worker"]]
        print("Two workers split an odd total budget, reproduce their islands, and publish the best", flush=True)

        _, auto = generate("automatic cores.RPL", evaluations=2, cores=0)
        available_cpus = (len(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity")
                          else os.cpu_count() or 1)
        # A container quota may be narrower than its affinity mask.
        assert 1 <= auto["cores_used"] <= min(available_cpus, 2), auto
        if hasattr(os, "sched_setaffinity"):
            original_affinity = os.sched_getaffinity(0)
            try:
                os.sched_setaffinity(0, {min(original_affinity)})
                _, constrained = generate("affinity.RPL", evaluations=2, cores=0)
                assert constrained["cores_used"] == 1, constrained
            finally:
                os.sched_setaffinity(0, original_affinity)
        _, capped = generate("capped.RPL", evaluations=1, cores=256)
        assert capped["cores_used"] == 1
        print("Automatic/explicit worker counts respect the global candidate budget", flush=True)

        # Compatible replay seeds and inherited transmission must reach every
        # island. Two candidates means one seed evaluation in each island.
        _, seeded = generate("seeded.RPL", source="human seed.RPL", car="LM02", evaluations=2, cores=2)
        assert seeded["seed_replay_finished"] and seeded["seed_total_frames"] > 0
        assert seeded["transmission"] == ("auto" if human_seed[5] else "manual")
        assert all(worker["finished"] for worker in seeded["worker_results"])
        _, automatic = generate("automatic.RPL", car="COUN", evaluations=2, cores=2,
                                flags=("--transmission", "auto"))
        assert automatic["transmission"] == "auto"
        assert (temporary / "automatic.RPL").read_bytes()[5] == 1
        print("Replay seed inputs, inherited transmission, and explicit automatic gears propagate", flush=True)

        # PMIN has different DOS/Amiga grass coefficients. Compare both islands
        # with fresh single-worker runs using exactly the same rule and seed.
        grass_flags = ("/amigagrass",)
        grass, grass_report = generate("grass.RPL", cores=2, flags=grass_flags)
        assert grass_report["total_frames"] != first_report["total_frames"], (
            "Grass fixture must distinguish the two grip modes")
        stock = json.loads(run(["grass.RPL", "PMIN", "--verify"], expected=2).stdout)
        assert not stock["finished"], "The saved fixture must require Amiga grass grip"
        grass_islands = []
        for worker in grass_report["worker_results"]:
            recording, report = generate(f"grass{worker['index']}.RPL",
                                         evaluations=worker["budget_evaluations"],
                                         cores=1, seed=worker["seed"], flags=grass_flags)
            assert worker["total_frames"] == report["total_frames"]
            assert worker["simulated_frames"] == report["simulated_frames"]
            grass_islands.append(recording)
        assert grass == grass_islands[grass_report["winner_worker"]]
        print("Amiga Grass reaches both islands and matches fresh verification and native playback", flush=True)

        # Find a fully OWOOT-valid seed using the existing single-core engine,
        # then require both parallel workers to accept it under identical rules.
        # These restrictive searches are deliberately bounded at 1,000 inputs.
        generate("restricted seed.RPL", car="COUN", evaluations=1000, cores=1, flags=RESTRICTED)
        _, restricted = generate("restricted.RPL", source="restricted seed.RPL", car="COUN",
                                 evaluations=2, cores=2, flags=RESTRICTED)
        assert restricted["seed_replay_finished"]
        assert restricted["tile_evaluations"] == restricted["dualway_evaluations"] == 0
        assert restricted["tactic_evaluations"] == 0
        print("OWOOT and all three bug fixes survive worker launch and independent playback", flush=True)

        # An impossible tiny budget must remain an ordinary no-finish result,
        # rather than publishing an invalid winner or treating DNF as a crash.
        run(["a track.TRK", "COUN", "--cores", 2, "--evaluations", 2,
             "--output", "no ghost.RPL", *RESTRICTED], expected=2)
        assert not (temporary / "no ghost.RPL").exists()
        assert not (temporary / "no ghost.RPL.json").exists()
        assert not list(temporary.glob("no ghost.RPL.work-*"))

        for value in ("-1", "257", "4294967296", "abc", "1.5", ""):
            run(["a track.TRK", "PMIN", "--cores", value, "--evaluations", 2,
                 "--output", "invalid.RPL"], expected=1)
        run(["a track.TRK", "PMIN", "--cores"], expected=1)
        (temporary / "not a directory").write_text("block output parent")
        run(["a track.TRK", "PMIN", "--cores", 2, "--evaluations", 2,
             "--output", "not a directory/output.RPL"], expected=1)
        assert not (temporary / "invalid.RPL").exists()
        print("Invalid counts, unusable output directories, and clean no-finish handling passed", flush=True)

        started = time.monotonic()
        run(["human seed.RPL", "LM02", "--cores", 2, "--seconds", 1,
             "--evaluations", 1000000, "--output", "timed.RPL"])
        elapsed = time.monotonic() - started
        timed = verify("timed.RPL", "LM02")
        assert 0.8 <= elapsed < 8, (elapsed, timed)
        assert 0 < timed["evaluations"] < 1000000, timed
        assert sum(worker["evaluations"] for worker in timed["worker_results"]) == timed["evaluations"]
        assert all(worker["status"] == 0 for worker in timed["worker_results"]), timed
        print("A shared wall-clock deadline stops both islands and preserves a validated replay", flush=True)

        if sys.platform == "linux" and Path("/proc/self/task").is_dir():
            def stop_run(output, action="cancel"):
                arguments = ["human seed.RPL", "LM02", "--cores", 2, "--seconds", 0,
                             "--output", output]
                children = {}
                log_path = temporary / (output + ".log")
                with log_path.open("w") as log:
                    process = subprocess.Popen(command(arguments), cwd=temporary, env=environment,
                                               stdout=log, stderr=log)
                    try:
                        before = {}
                        deadline = time.monotonic() + 10
                        while time.monotonic() < deadline:
                            assert process.poll() is None, log_path.read_text()
                            observed = process_workers(process.pid)
                            children.update(observed)
                            if len(observed) == 2:
                                if not before:
                                    before = observed
                                progressed = (set(before) == set(observed) and
                                              all(observed[pid][0] > before[pid][0] for pid in observed))
                                try:
                                    report = read_report(output)
                                    saved = any(worker["finished"] for worker in report["worker_results"])
                                except (FileNotFoundError, json.JSONDecodeError):
                                    saved = False
                                if progressed and saved:
                                    break
                            time.sleep(0.02)
                        else:
                            raise AssertionError("Two live CPU-active workers and a saved finish were not observed\n"
                                                 + log_path.read_text())
                        if action == "failure":
                            os.kill(min(observed), signal.SIGKILL)
                        elif action == "orphan":
                            process.kill()
                        else:
                            process.send_signal(signal.SIGTERM)
                        return_code = process.wait(timeout=10)
                        expected = {"cancel": 0, "failure": 1, "orphan": -signal.SIGKILL}[action]
                        assert return_code == expected, log_path.read_text()
                        # A forcibly killed coordinator cannot reap children;
                        # adopted zombies are stopped, but live orphans are not.
                        deadline = time.monotonic() + 5
                        while any(process_live(pid) for pid in children) and time.monotonic() < deadline:
                            time.sleep(0.02)
                        assert not any(process_live(pid) for pid in children), "Live workers survived their parent"
                        if action != "orphan":
                            assert not any(Path(f"/proc/{pid}").exists() for pid in children), (
                                "Graceful coordinator exit did not reap its workers")
                    finally:
                        if process.poll() is None:
                            process.kill()
                            process.wait(timeout=5)
                        # Test failures must not leave runaway --seconds 0 children.
                        for pid in children:
                            try:
                                os.kill(pid, signal.SIGKILL)
                            except ProcessLookupError:
                                pass
                for pid, (_, scratch) in children.items():
                    if action == "orphan":
                        # SIGKILL prevents parent file cleanup. This test owns
                        # the scratch directory and removes its retained files.
                        if scratch.exists():
                            shutil.rmtree(scratch)
                    else:
                        assert not scratch.exists(), f"Private worker files survived: {scratch}"
                report = verify(output, "LM02")
                assert report["total_frames"] <= report["seed_total_frames"]
                if action == "failure":
                    assert any(worker["status"] == 1 for worker in report["worker_results"]), report

            stop_run("cancel.RPL")
            stop_run("failure.RPL", action="failure")
            stop_run("orphan.RPL", action="orphan")
            print("Concurrent CPU progress, cancellation, worker failure, forced parent death, and cleanup passed",
                  flush=True)
        else:
            print("SKIP: live process and cancellation assertions require Linux /proc", flush=True)
        assert not list(temporary.glob("*.work-*")), "Worker scratch directories leaked"
    return 0


if __name__ == "__main__":
    sys.exit(main())
