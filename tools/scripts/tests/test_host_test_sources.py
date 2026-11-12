"""Exercise the host runner's complete source graph without compiling fixtures."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


REPOSITORY = Path(__file__).resolve().parents[3]
RUNNER = Path("src/restunts/tests/run-host-tests.sh")
GRAPH_PASSES = 2
GRAPH_TIMEOUT_SECONDS = 60
EXECUTABLE_MODE = 0o755
PARALLEL_JOBS = 3
COMPILATION_FAILURE_EXIT = 37
EXECUTION_FAILURE_EXIT = 23
COMPILER = r'''#!/bin/bash
set -eu
arguments=("$@")
output=
while [[ "$#" -gt 0 ]]; do
    if [[ "$1" == -o ]]; then
        shift
        output="$1"
        break
    fi
    shift
done
# The runner first probes compiler warnings without requesting an executable.
if [[ -z "$output" ]]; then
    exit 0
fi
printf '%s\0' "${arguments[@]}" '' >> "$HOST_TEST_COMMAND_LOG"
printf '#!/bin/sh\nexit 0\n' > "$output"
chmod +x "$output"
'''
PARALLEL_COMPILER = r'''import fcntl, json, os, pathlib, sys, time

arguments = sys.argv[1:]
if "-o" not in arguments:
    raise SystemExit(0)
output = pathlib.Path(arguments[arguments.index("-o") + 1])
def record(event, active_delta):
    with open(os.environ["HOST_TEST_EVENTS"], "r+") as log:
        fcntl.flock(log, fcntl.LOCK_EX)
        state = json.load(log)
        state["active"] += active_delta
        state["peak"] = max(state["peak"], state["active"])
        state["events"].append([event, str(output), state["active"], arguments])
        log.seek(0)
        json.dump(state, log)
        log.truncate()
record("compile", 1)
try:
    time.sleep(0.02)
    if output.name == os.environ.get("HOST_TEST_FAIL_COMPILE"):
        raise SystemExit(int(os.environ["HOST_TEST_FAILURE_EXIT"]))
    runtime = (
        "#!" + sys.executable + "\n"
        "import fcntl, json, os, sys\n"
        "with open(os.environ['HOST_TEST_EVENTS'], 'r+') as log:\n"
        "    fcntl.flock(log, fcntl.LOCK_EX)\n"
        "    state = json.load(log)\n"
        "    state['events'].append(['run', " + repr(str(output)) + ", state['active'], "
        + repr(arguments) + "])\n"
        "    log.seek(0)\n"
        "    json.dump(state, log)\n"
        "    log.truncate()\n"
        "sys.exit(int(os.environ['HOST_TEST_FAILURE_EXIT']) if " + repr(output.name)
        + " == os.environ.get('HOST_TEST_FAIL_RUN') else 0)\n"
    )
    output.write_text(runtime)
    output.chmod(0o755)
finally:
    record("compiled", -1)
'''


@unittest.skipUnless(os.name == "posix" and shutil.which("bash"), "requires Bash and symlinks")
class HostTestSourceTests(unittest.TestCase):
    def test_complete_graph_is_stable_deduplicated_and_preserves_spaces(self):
        with tempfile.TemporaryDirectory(prefix="host source graph ") as temporary:
            directory = Path(temporary)
            checkout = directory / "checkout with spaces"
            checkout.symlink_to(REPOSITORY, target_is_directory=True)
            compiler = directory / "compiler with spaces.sh"
            compiler.write_text(COMPILER)
            compiler.chmod(EXECUTABLE_MODE)
            graphs = []
            for pass_index in range(GRAPH_PASSES):
                log = directory / f"commands-{pass_index}.bin"
                environment = dict(os.environ, CC=str(compiler), CFLAGS="", LDFLAGS="",
                                   HOST_TEST_COMMAND_LOG=str(log), RESTUNTS_BUILD_JOBS="1")
                result = subprocess.run(["bash", str(checkout / RUNNER)], env=environment,
                                        capture_output=True, text=True, timeout=GRAPH_TIMEOUT_SECONDS)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                commands = [record.decode().split("\0")
                            for record in log.read_bytes().split(b"\0\0") if record]
                graph = []
                for command in commands:
                    arguments = command[:-2]
                    sources = [Path(argument) for argument in arguments
                               if not argument.startswith("-")]
                    self.assertTrue(all(source.is_file() for source in sources), command)
                    resolved = [source.resolve() for source in sources]
                    self.assertEqual(len(resolved), len(set(resolved)), command)
                    graph.append((Path(command[-1]).name, arguments))
                graphs.append(graph)
            self.assertEqual(graphs[0], graphs[1])
            self.assertTrue(graphs[0])
            decompression = next(arguments for name, arguments in graphs[0]
                                 if name == "test-amiga-decompression")
            sources = {Path(argument).resolve() for argument in decompression
                       if argument.endswith(".c")}
            core = REPOSITORY / "src/restunts/c"
            self.assertEqual(sources, {REPOSITORY / "src/restunts/tests/test-amiga-decompression.c",
                                      core / "fileio.c", core / "extended/extended_fileio.c",
                                      core / "file_car.c", core / "file_decomp.c"})
            for name, arguments in graphs[0]:
                if name == "test-audio-engine":
                    # The fixture includes these modules; do not link them again.
                    for module in ("audio_engine.c", "audio_channels.c", "audio_context.c"):
                        self.assertFalse(any(argument.endswith("/" + module)
                                             for argument in arguments))


@unittest.skipUnless(os.name == "posix" and shutil.which("bash"), "requires Bash and file locks")
class HostTestParallelBuildTests(unittest.TestCase):
    def run_runner(self, **settings):
        temporary = tempfile.TemporaryDirectory(prefix="parallel host tests with spaces ")
        self.addCleanup(temporary.cleanup)
        directory = Path(temporary.name)
        compiler = directory / "compiler with spaces"
        compiler.write_text("#!" + sys.executable + "\n" + PARALLEL_COMPILER)
        compiler.chmod(EXECUTABLE_MODE)
        log = directory / "events.json"
        log.write_text(json.dumps({"active": 0, "peak": 0, "events": []}))
        environment = dict(os.environ, CC=str(compiler), CFLAGS="", LDFLAGS="",
                           HOST_TEST_EVENTS=str(log), RESTUNTS_BUILD_JOBS=str(PARALLEL_JOBS),
                           HOST_TEST_FAILURE_EXIT=str(COMPILATION_FAILURE_EXIT))
        environment.update(settings)
        result = subprocess.run(["bash", str(REPOSITORY / RUNNER)], env=environment,
                                capture_output=True, text=True, timeout=GRAPH_TIMEOUT_SECONDS)
        return result, json.loads(log.read_text())

    def assert_cleaned(self, state):
        self.assertEqual(state["active"], 0)
        outputs = [Path(event[1]) for event in state["events"] if event[0] == "compile"]
        self.assertTrue(outputs)
        self.assertTrue(all(not output.parent.parent.exists() for output in outputs))

    def test_builds_use_bounded_parallelism_then_run_distinct_variants_serially(self):
        result, state = self.run_runner()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertGreater(state["peak"], 1)
        self.assertLessEqual(state["peak"], PARALLEL_JOBS)
        builds = [event[1] for event in state["events"] if event[0] == "compile"]
        runs = [event for event in state["events"] if event[0] == "run"]
        self.assertEqual(len(builds), len(set(builds)), "Compiler outputs overlap between variants")
        self.assertEqual([int(Path(event[1]).parent.name) for event in runs], list(range(len(builds))))
        self.assertTrue(all(event[2] == 0 for event in runs), "Fixtures ran before builds finished")
        variants = [tuple(argument for argument in event[3] if argument.startswith("-D"))
                    for event in runs if Path(event[1]).name == "test-audio-engine"]
        self.assertGreater(len(variants), 1)
        self.assertEqual(len(variants), len(set(variants)), "A later variant replaced an earlier build")
        self.assert_cleaned(state)

    def test_compiler_failure_is_preserved_and_prevents_all_fixture_execution(self):
        result, state = self.run_runner(HOST_TEST_FAIL_COMPILE="test-amiga-display")
        self.assertEqual(result.returncode, COMPILATION_FAILURE_EXIT, result.stdout + result.stderr)
        self.assertFalse(any(event[0] == "run" for event in state["events"]))
        self.assert_cleaned(state)

    def test_fixture_failure_stops_serial_execution_and_cleans_completed_builds(self):
        result, state = self.run_runner(HOST_TEST_FAIL_RUN="test-amiga-display",
                                        HOST_TEST_FAILURE_EXIT=str(EXECUTION_FAILURE_EXIT))
        self.assertEqual(result.returncode, EXECUTION_FAILURE_EXIT, result.stdout + result.stderr)
        self.assertEqual([Path(event[1]).name for event in state["events"] if event[0] == "run"],
                         ["test-amiga-endian", "test-amiga-display"])
        self.assert_cleaned(state)

    def test_invalid_job_counts_fail_before_compiling(self):
        for jobs in ("0", "-1", "invalid"):
            with self.subTest(jobs=jobs):
                result, state = self.run_runner(RESTUNTS_BUILD_JOBS=jobs)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("RESTUNTS_BUILD_JOBS must be a positive integer", result.stderr)
                self.assertEqual(state["events"], [])


if __name__ == "__main__":
    unittest.main()
