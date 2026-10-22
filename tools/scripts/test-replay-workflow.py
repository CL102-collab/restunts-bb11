"""Check target selection and replay workflow wiring without running DOSBox."""

import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import textwrap
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github" / "workflows" / "build-and-validate.yml"
REPLAY_WORKFLOW = WORKFLOW.with_name("replay-tests.yml")
PLANNER = ROOT / "tools" / "scripts" / "plan-replay-shards.py"
SPEC = importlib.util.spec_from_file_location("replay_plan", PLANNER)
planner = importlib.util.module_from_spec(SPEC)
sys.dont_write_bytecode = True
SPEC.loader.exec_module(planner)

TEST_SHARDS = 3
TEST_PARTITIONS = 1
TEST_PERCENTAGE = 40
TEST_REPLAY_COUNT = 10
TEST_SHORT_TICKS = 100
TEST_LONG_TICKS = 60000
COMMAND_TIMEOUT_SECONDS = 30
FIRST_CAMERA = "${{ fromJSON(needs.plan.outputs.cameras)[0] }}"
FIRST_TARGET = "${{ fromJSON(needs.plan.outputs.targets)[0] }}"
MATRIX_TARGET = "${{ matrix.target }}"


def block_after(source, heading):
    """Read one indented workflow block; this is not a general YAML parser."""
    lines = source.splitlines()
    start = lines.index(heading)
    indentation = len(heading) - len(heading.lstrip())
    body = []
    for line in lines[start + 1:]:
        if line.strip() and len(line) - len(line.lstrip()) <= indentation:
            break
        body.append(line)
    return "\n".join(body)


def step_script(workflow, name):
    step = block_after(workflow, "      - name: " + name)
    if "        run: |" in step.splitlines():
        return textwrap.dedent(block_after(step, "        run: |"))
    return next(line.removeprefix("        run: ") for line in step.splitlines()
                if line.startswith("        run: "))


class ReplayWorkflowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workflow = WORKFLOW.read_text(encoding="utf-8")
        cls.replay_workflow = REPLAY_WORKFLOW.read_text(encoding="utf-8")

    def run_step(self, name, directory, workflow=None, **environment):
        output = directory / "github-output.txt"
        output.write_text("", encoding="utf-8")
        result = subprocess.run(
            ["bash", "--noprofile", "--norc", "-e", "-o", "pipefail", "-c",
             step_script(workflow or self.workflow, name)],
            cwd=directory,
            env=os.environ | environment | {
                "GITHUB_OUTPUT": str(output),
                "GITHUB_ENV": str(directory / "github-env.txt"),
                "GITHUB_STEP_SUMMARY": str(directory / "summary.md"),
            },
            capture_output=True, text=True, timeout=COMMAND_TIMEOUT_SECONDS,
        )
        outputs = dict(line.split("=", 1) for line in output.read_text().splitlines())
        return result, outputs

    def write_stub(self, directory, name, source):
        path = directory / name
        path.write_text("#!" + sys.executable + "\n" + textwrap.dedent(source), encoding="utf-8")
        path.chmod(0o755)

    def make_game_fixture(self, directory):
        binaries = directory / "stub-bin"
        binaries.mkdir()
        self.write_stub(binaries, "curl", """
            from pathlib import Path
            import sys
            Path(sys.argv[sys.argv.index("--output") + 1]).write_bytes(b"archive")
        """)
        self.write_stub(binaries, "unzip", """
            from pathlib import Path
            import sys
            destination = Path(sys.argv[sys.argv.index("-d") + 1])
            destination.mkdir(parents=True, exist_ok=True)
            if any(argument.endswith("BB11.zip") for argument in sys.argv):
                (destination / "repldumo.exe").write_bytes(b"asset physics")
                (destination / "pixldumo.exe").write_bytes(b"asset renderer")
            else:
                (destination / "golden.rpl").write_bytes(b"golden replay")
        """)
        cars = directory / "tools" / "scripts" / "cars"
        cars.mkdir(parents=True)
        (cars / "car.res").write_bytes(b"car resource")
        archived = directory / "tools" / "oracles" / "borland"
        archived.mkdir(parents=True)
        for name, contents in (("repldumo.exe", b"archived physics"),
                               ("pixldumo.exe", b"archived renderer")):
            (archived / name).write_bytes(contents)
        exes = directory / "exes"
        exes.mkdir()
        (exes / "repldumo.exe").write_bytes(b"built physics")
        (exes / "pixldumo.exe").write_bytes(b"built renderer")
        runner = directory / "runner"
        runner.mkdir()
        return {
            "PATH": str(binaries) + os.pathsep + os.environ["PATH"],
            "RUNNER_TEMP": str(runner), "STUNTS_URL": "test-assets",
        }

    def test_game_assembly_uses_built_original_references_for_both_phases(self):
        for platform, physics in (("sdl3", True), ("dos", True),
                                  ("sdl3", False), ("dos", False)):
            with self.subTest(platform=platform, physics=physics), \
                    tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                environment = self.make_game_fixture(directory)
                result, _ = self.run_step(
                    "Assemble the game directory", directory, workflow=self.replay_workflow,
                    CANDIDATE_PLATFORM=platform, PHYSICS_TESTS=str(physics).lower(),
                    **environment)
                self.assertEqual(0, result.returncode, result.stdout + result.stderr)
                game = Path(environment["RUNNER_TEMP"]) / "gamedir"
                self.assertEqual(b"built physics", (game / "repldumo.exe").read_bytes())
                self.assertEqual(b"built renderer", (game / "pixldumo.exe").read_bytes())

    def test_game_assembly_rejects_missing_or_empty_built_original_references(self):
        cases = ((platform, physics, oracle, empty) for platform in ("sdl3", "dos")
                 for physics in (True, False) for oracle in ("repldumo", "pixldumo")
                 for empty in (False, True))
        for platform, physics, oracle, empty in cases:
            with self.subTest(platform=platform, physics=physics, oracle=oracle, empty=empty), \
                    tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                environment = self.make_game_fixture(directory)
                reference = directory / "exes" / f"{oracle}.exe"
                if empty:
                    reference.write_bytes(b"")
                else:
                    reference.unlink()
                result, _ = self.run_step(
                    "Assemble the game directory", directory, workflow=self.replay_workflow,
                    CANDIDATE_PLATFORM=platform, PHYSICS_TESTS=str(physics).lower(), **environment)
                self.assertNotEqual(0, result.returncode)
                self.assertIn("built original dump reference", result.stdout + result.stderr)
                self.assertFalse((directory / "github-env.txt").exists())

    def test_artifact_validation_requires_built_references_for_every_platform_and_phase(self):
        for platform, physics, dos_artifact, native_artifact, success in (
                ("sdl3", "true", "dos-build", "native-build", True),
                ("sdl3", "true", "", "native-build", False),
                ("sdl3", "false", "", "native-build", False),
                ("sdl3", "false", "dos-build", "native-build", True),
                ("sdl3", "true", "dos-build", "", False),
                ("dos", "true", "dos-build", "", True),
                ("dos", "false", "dos-build", "", True),
                ("dos", "false", "", "", False)):
            with self.subTest(platform=platform, physics=physics, dos=dos_artifact), \
                    tempfile.TemporaryDirectory() as temporary:
                result, _ = self.run_step(
                    "Validate candidate platform", Path(temporary), workflow=self.replay_workflow,
                    CANDIDATE_PLATFORM=platform, PHYSICS_TESTS=physics,
                    DOS_ARTIFACT=dos_artifact, NATIVE_ARTIFACT=native_artifact)
                self.assertEqual(success, result.returncode == 0, result.stdout + result.stderr)
        download = block_after(self.replay_workflow,
                               "      - name: Download the DOS executables and original references")
        self.assertNotIn("        if:", download)
        self.assertIn("name: ${{ inputs.exe-artifact }}", download)

    def test_sdl3_only_build_publishes_original_references_without_full_dos_build(self):
        build = block_after(self.workflow, "  build:")
        self.assertFalse(any(line.startswith("    if:") for line in build.splitlines()))
        self.assertIn("BUILD_DOS_EXECUTABLES: ${{ inputs.build-dos-artifact || "
                      "contains(fromJSON(needs.plan.outputs.platforms), 'dos') }}", build)
        for name in ("Build all six DOS executables",
                     "Check the DOS platform ABI and dump timer masking",
                     "Verify DOS16 startup and driving settings"):
            step = block_after(build, "      - name: " + name)
            self.assertIn("if: env.BUILD_DOS_EXECUTABLES == 'true'", step)
        for name in ("Build original dump references", "Install DOSBox-X",
                     "Check the renderer address model against the built oracle",
                     "Check original dump failure cleanup"):
            step = block_after(build, "      - name: " + name)
            self.assertNotIn("        if:", step)
        self.assertIn("dosbox-x", step_script(self.workflow, "Install DOSBox-X"))
        physics = block_after(self.workflow, "  physics:")
        self.assertIn("needs.build.result == 'success'", physics)
        self.assertNotIn("needs.build.result == 'skipped'", physics)
        self.assertIn("exe-artifact: ${{ needs.build.outputs.exe-artifact }}", physics)
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            binaries = directory / "stub-bin"
            binaries.mkdir()
            self.write_stub(binaries, "make", """
                from pathlib import Path
                import sys
                assert sys.argv[1:] == ["-C", "src/restunts", "repldump-original",
                                        "pixldump-original"]
                stunts = Path("stunts")
                stunts.mkdir()
                (stunts / "repldumo.exe").write_bytes(b"built physics")
                (stunts / "pixldumo.exe").write_bytes(b"built renderer")
            """)
            environment = {"BUILD_DOS_EXECUTABLES": "false",
                           "PATH": str(binaries) + os.pathsep + os.environ["PATH"]}
            result, _ = self.run_step("Build original dump references", directory, **environment)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            result, _ = self.run_step("Collect the executables", directory, **environment)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertEqual(["pixldumo.exe", "repldumo.exe"],
                             sorted(path.name for path in (directory / "dist").iterdir()))
            shutil.rmtree(directory / "dist")
            full_names = ("restunts", "restunto", "repldump", "repldumo", "pixldump", "pixldumo")
            for name in full_names:
                (directory / "stunts" / f"{name}.exe").write_bytes(name.encode())
            environment["BUILD_DOS_EXECUTABLES"] = "true"
            result, _ = self.run_step("Collect the executables", directory, **environment)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertEqual(sorted(f"{name}.exe" for name in full_names),
                             sorted(path.name for path in (directory / "dist").iterdir()))

    def test_targets_input_is_a_json_array_defaulting_to_both_targets(self):
        inputs = block_after(self.workflow, "    inputs:")
        target_input = block_after(inputs, "      targets:")
        self.assertIn("        type: string", target_input)
        self.assertIn("        default: '[0,1]'", target_input)
        self.assertNotIn("      target:", inputs)
        plan = block_after(self.workflow, "  plan:")
        self.assertIn("      targets: ${{ steps.targets.outputs.targets }}", plan)
        self.assertIn("          name: replay-shard-plan", plan)
        self.assertIn("          path: shard-plans/*.json", plan)

    def test_target_validation_preserves_selection_and_order(self):
        for value, expected in (
            ("[0]", [planner.TARGET_PLAYER]),
            ("[1]", [planner.TARGET_OPPONENT]),
            ("[0,1]", [planner.TARGET_PLAYER, planner.TARGET_OPPONENT]),
            ("[1,0]", [planner.TARGET_OPPONENT, planner.TARGET_PLAYER]),
            (" [ 0,\n 1 ] ", [planner.TARGET_PLAYER, planner.TARGET_OPPONENT]),
            ("[-0,1.0]", [planner.TARGET_PLAYER, planner.TARGET_OPPONENT]),
            ("[1e0]", [planner.TARGET_OPPONENT]),
        ):
            with self.subTest(value=value), tempfile.TemporaryDirectory() as temporary:
                result, outputs = self.run_step(
                    "Validate renderer targets", Path(temporary), TARGETS=value)
                self.assertEqual(0, result.returncode, result.stdout + result.stderr)
                self.assertEqual(expected, json.loads(outputs["targets"]))
                self.assertEqual(json.dumps(expected, separators=(",", ":")), outputs["targets"])

    def test_invalid_targets_fail_before_publishing_a_matrix(self):
        for value in (
            "", "not JSON", "[]", "[0,0]", "[1,1]", "[0,-0]", "[1,1.0]",
            "[0,1,0]", "[-1]", "[2]",
            "[0.5]", '["0"]', "[true]", "[false]", "[null]", "[[0]]", "null",
            "0", "1", "true", "{}", '[{"target":0}]', "[0]\n[1]",
        ):
            with self.subTest(value=value), tempfile.TemporaryDirectory() as temporary:
                result, outputs = self.run_step(
                    "Validate renderer targets", Path(temporary), TARGETS=value)
                self.assertNotEqual(0, result.returncode)
                self.assertIn("::error::targets", result.stdout + result.stderr)
                self.assertNotIn("targets", outputs)

    def make_replay_fixture(self, directory):
        scripts = directory / "tools" / "scripts"
        corpus = scripts / "rpls_golden" / "replays.zip"
        corpus.parent.mkdir(parents=True)
        shutil.copyfile(PLANNER, scripts / PLANNER.name)
        with zipfile.ZipFile(corpus, "w") as archive:
            for index in range(TEST_REPLAY_COUNT):
                has_opponent = index % 2
                header = bytearray(planner.REPLAY_HEADER_SIZE)
                header[planner.REPLAY_OPPONENT_OFFSET] = has_opponent
                ticks = TEST_SHORT_TICKS if has_opponent else TEST_LONG_TICKS
                struct.pack_into("<H", header, planner.REPLAY_FRAMES_OFFSET, ticks)
                archive.writestr(f"r{index:02}.rpl", header)

    def test_planning_isolates_targets_and_keeps_shared_physics_coverage(self):
        selections = ("[0]", "[1]", "[0,1]", "[1,0]", "[-0,1.0]", "[1e0]")
        expected_renderer = {
            planner.TARGET_PLAYER: ["r00.rpl", "r02.rpl", "r05.rpl", "r07.rpl"],
            planner.TARGET_OPPONENT: ["r01.rpl", "r05.rpl"],
        }
        shared_physics = None
        for selection in selections:
            with self.subTest(targets=selection), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                self.make_replay_fixture(directory)
                result, outputs = self.run_step(
                    "Validate renderer targets", directory, TARGETS=selection)
                self.assertEqual(0, result.returncode, result.stdout + result.stderr)
                targets = json.loads(outputs["targets"])
                result, outputs = self.run_step(
                    "Split the golden set into shards", directory,
                    TARGETS=outputs["targets"], SHARDS=str(TEST_SHARDS),
                    PARTITIONS_PER_SHARD=str(TEST_PARTITIONS),
                    RENDERER_TEST_PERCENTAGE=str(TEST_PERCENTAGE),
                )
                self.assertEqual(0, result.returncode, result.stdout + result.stderr)
                self.assertEqual(list(range(TEST_SHARDS)), json.loads(outputs["shards"]))
                paths = sorted(directory.glob("shard-plans/*.json"))
                self.assertEqual([f"target{target}.json" for target in sorted(targets)],
                                 [path.name for path in paths])
                for path in paths:
                    plan = json.loads(path.read_text(encoding="utf-8"))
                    self.assertEqual(f'target{plan["target"]}.json', path.name)
                    self.assertEqual(TEST_PERCENTAGE, plan["rendererTestPercentage"])
                    self.assertEqual(TEST_SHARDS, len(plan["shards"]))
                    renderer = sorted(name for shard in plan["shards"]
                                      for name in shard["renderer"])
                    self.assertEqual(expected_renderer[plan["target"]], renderer)
                    physics = [(shard["physics"], shard["physicsTicks"])
                               for shard in plan["shards"]]
                    if shared_physics is None:
                        shared_physics = physics
                    self.assertEqual(shared_physics, physics)
                    self.assertEqual([f"r{index:02}.rpl" for index in range(TEST_REPLAY_COUNT)],
                                     sorted(name for names, _ in physics for name in names))

    def test_renderer_matrix_reuses_one_physics_run_per_platform(self):
        physics = block_after(self.workflow, "  physics:")
        matrix = block_after(physics, "      matrix:")
        self.assertIn("        platform: ${{ fromJSON(needs.plan.outputs.platforms) }}", matrix)
        self.assertNotIn("        camera:", matrix)
        self.assertNotIn("        target:", matrix)
        self.assertIn("      camera: " + FIRST_CAMERA, physics)
        self.assertIn("      target: " + FIRST_TARGET, physics)
        for job_name in ("renderer", "report"):
            job = block_after(self.workflow, f"  {job_name}:")
            matrix = block_after(job, "      matrix:")
            for axis in ("platform", "camera", "target"):
                self.assertIn(f"        {axis}: ${{{{ fromJSON(needs.plan.outputs.{axis}s) }}}}",
                              matrix)
        renderer = block_after(self.workflow, "  renderer:")
        self.assertIn("      target: " + MATRIX_TARGET, renderer)

    def test_reports_select_shared_physics_and_the_matching_renderer_target(self):
        report = block_after(self.workflow, "  report:")
        artifact_names = [line.strip() for line in report.splitlines()
                          if line.startswith("          name:")]
        self.assertEqual(3, len(artifact_names))
        self.assertIn("physics-cam" + FIRST_CAMERA + "-target" + FIRST_TARGET + "-report",
                      artifact_names[0])
        for name in artifact_names[1:]:
            self.assertIn("cam${{ matrix.camera }}-target" + MATRIX_TARGET, name)

    def test_each_replay_command_consumes_the_selected_target_plan(self):
        self.assertIn("  SHARD_PLAN: shard-plan/target${{ inputs.target }}.json",
                      self.replay_workflow)
        for name in (
            "Download and extract this shard's precomputed oracle outputs",
            "Run this shard's ${{ inputs.phase }} tests",
            "Merge the shard results and check coverage",
        ):
            with self.subTest(step=name):
                script = step_script(self.replay_workflow, name)
                self.assertIn('-ShardPlan "$SHARD_PLAN"', script)
                self.assertIn('-Target "$TARGET"', script)


if __name__ == "__main__":
    unittest.main()
