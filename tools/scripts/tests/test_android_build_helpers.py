"""Exercise Android helper argument handling without SDK downloads or Gradle."""

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
HELPERS = {
    "build-android.sh": ("app", "prepare-android.py"),
    "build-genghost-android.sh": ("genghost", "prepare-genghost-android.py"),
}
SIGNING_VARIABLES = (
    "ANDROID_KEYSTORE_FILE", "ANDROID_KEYSTORE_PASSWORD", "ANDROID_KEY_ALIAS", "ANDROID_KEY_PASSWORD",
)


class AndroidBuildHelperTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="android helper ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.scripts = self.root / "tools/scripts"
        self.scripts.mkdir(parents=True)
        (self.root / "android").mkdir()
        self.events = self.root / "events.jsonl"
        wrapper = self.root / "out/android-gradle/gradlew"
        wrapper.parent.mkdir(parents=True)
        wrapper.write_text(
            "#!/usr/bin/env python3\n"
            "import json, os, sys\n"
            "with open(os.environ['ANDROID_HELPER_TEST_EVENTS'], 'a') as output:\n"
            "    output.write(json.dumps({'step': 'gradle', 'arguments': sys.argv[1:], "
            "'keystore': os.environ.get('ANDROID_KEYSTORE_FILE')}) + '\\n')\n",
            encoding="utf-8")
        wrapper.chmod(0o755)
        for helper, (_, prepare) in HELPERS.items():
            shutil.copy2(ROOT / "tools/scripts" / helper, self.scripts / helper)
            (self.scripts / prepare).write_text(
                "import json, os\n"
                "with open(os.environ['ANDROID_HELPER_TEST_EVENTS'], 'a') as output:\n"
                "    output.write(json.dumps({'step': 'prepare'}) + '\\n')\n",
                encoding="utf-8")
        self.environment = dict(os.environ)
        for name in (*SIGNING_VARIABLES, "RESTUNTS_ANDROID_BUILD_TYPE"):
            self.environment.pop(name, None)
        self.environment["ANDROID_HELPER_TEST_EVENTS"] = str(self.events)
        self.keystore = self.root / "signing keys/release key.jks"
        self.keystore.parent.mkdir()
        self.keystore.write_bytes(b"fixture: only helper file checks are exercised")

    def run_helper(self, helper, arguments=(), environment=None):
        self.events.unlink(missing_ok=True)
        values = self.environment.copy()
        values.update(environment or {})
        result = subprocess.run(["bash", str(self.scripts / helper), *arguments],
                                cwd=self.root, env=values, text=True, capture_output=True,
                                timeout=15, check=False)
        events = ([json.loads(line) for line in self.events.read_text().splitlines()]
                  if self.events.exists() else [])
        return result, events

    def signing_environment(self):
        return {
            "ANDROID_KEYSTORE_FILE": str(self.keystore.relative_to(self.root)),
            "ANDROID_KEYSTORE_PASSWORD": "fixture store password",
            "ANDROID_KEY_ALIAS": "fixture release alias",
            "ANDROID_KEY_PASSWORD": "fixture key password",
        }

    def assert_gradle(self, result, events, module, task, options=()):
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([event["step"] for event in events], ["prepare", "gradle"])
        self.assertEqual(events[-1]["arguments"],
                         ["-p", str(self.root / "android"), f":{module}:{task}", *options])

    def test_debug_target_selection_and_default_all(self):
        for helper, (module, _) in HELPERS.items():
            for arguments, task in (((), "assembleDebug"), (("all",), "assembleDebug"),
                                    (("android-armv7",), "assembleArmv7Debug"),
                                    (("android-arm64",), "assembleArm64Debug")):
                with self.subTest(helper=helper, arguments=arguments):
                    result, events = self.run_helper(helper, arguments)
                    self.assert_gradle(result, events, module, task)

    def test_release_flag_and_environment_select_signed_variants(self):
        for helper, (module, _) in HELPERS.items():
            for target, suffix in (("all", ""), ("android-armv7", "Armv7"),
                                   ("android-arm64", "Arm64")):
                for explicit in (True, False):
                    with self.subTest(helper=helper, target=target, explicit=explicit):
                        environment = self.signing_environment()
                        arguments = [target]
                        if explicit:
                            arguments.append("--release")
                        else:
                            environment["RESTUNTS_ANDROID_BUILD_TYPE"] = "release"
                        result, events = self.run_helper(helper, arguments, environment)
                        self.assert_gradle(result, events, module, f"assemble{suffix}Release",
                                           ["--no-daemon"])
                        self.assertEqual(events[-1]["keystore"], str(self.keystore))

    def test_explicit_release_overrides_debug_environment(self):
        for helper, (module, _) in HELPERS.items():
            with self.subTest(helper=helper):
                environment = self.signing_environment()
                environment["RESTUNTS_ANDROID_BUILD_TYPE"] = "debug"
                result, events = self.run_helper(helper, ["android-arm64", "--release"], environment)
                self.assert_gradle(result, events, module, "assembleArm64Release", ["--no-daemon"])

    def test_missing_release_credentials_fail_before_dependency_preparation(self):
        for helper in HELPERS:
            for missing in SIGNING_VARIABLES:
                with self.subTest(helper=helper, missing=missing):
                    environment = self.signing_environment()
                    environment[missing] = ""
                    result, events = self.run_helper(helper, ["--release"], environment)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(missing, result.stderr)
                    self.assertEqual(events, [], "Invalid signing must not prepare or build dependencies")

    def test_release_rejects_missing_or_directory_keystore(self):
        for helper in HELPERS:
            for keystore in (self.root / "missing key.jks", self.keystore.parent):
                with self.subTest(helper=helper, keystore=keystore):
                    environment = self.signing_environment()
                    environment["ANDROID_KEYSTORE_FILE"] = str(keystore)
                    result, events = self.run_helper(helper, ["--release"], environment)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("readable file", result.stderr)
                    self.assertEqual(events, [])

    def test_unknown_target_or_build_type_fails_before_preparation(self):
        for helper in HELPERS:
            for arguments, environment, message in (
                    (["android-x86"], {}, "Unknown Android build target"),
                    ([], {"RESTUNTS_ANDROID_BUILD_TYPE": "unsigned"}, "Unknown Android build type")):
                with self.subTest(helper=helper, arguments=arguments, environment=environment):
                    result, events = self.run_helper(helper, arguments, environment)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(message, result.stderr)
                    self.assertEqual(events, [])

    def test_gradle_options_preserve_spaces_newlines_and_option_boundary(self):
        forwarded = ["--no-daemon", "-PrestuntsAndroidBuildRoot=/tmp/Android build path",
                     "-Pfixture=first line\nsecond line", "--release"]
        for helper, (module, _) in HELPERS.items():
            with self.subTest(helper=helper):
                result, events = self.run_helper(helper, ["android-arm64", "--", *forwarded])
                self.assert_gradle(result, events, module, "assembleArm64Debug", forwarded)

    def test_help_needs_no_dependencies_or_signing(self):
        for helper in HELPERS:
            for argument in ("-h", "--help"):
                with self.subTest(helper=helper, argument=argument):
                    result, events = self.run_helper(helper, [argument],
                                                     {"RESTUNTS_ANDROID_BUILD_TYPE": "release"})
                    self.assertEqual(result.returncode, 0)
                    self.assertIn("Usage:", result.stdout)
                    self.assertEqual(events, [])

    def test_package_build_forwards_worker_count_to_gradle_and_native_environment(self):
        package_helper = "build-android-package.sh"
        shutil.copy2(ROOT / "tools/scripts" / package_helper, self.scripts / package_helper)
        shutil.copy2(ROOT / "android/toolchain.properties", self.root / "android/toolchain.properties")
        expected_build_failure = 37
        (self.scripts / "build-android.sh").write_text(
            "#!/usr/bin/env python3\n"
            "import json, os, sys\n"
            "with open(os.environ['ANDROID_HELPER_TEST_EVENTS'], 'a') as output:\n"
            "    output.write(json.dumps({'step': 'native-build', 'arguments': sys.argv[1:], "
            "'parallel': os.environ.get('CMAKE_BUILD_PARALLEL_LEVEL')}) + '\\n')\n"
            f"sys.exit({expected_build_failure})\n",
            encoding="utf-8")
        # The packager invokes its helpers through Bash. Record the forwarded
        # values before an intentional build failure avoids SDK/signing work.
        recorder = self.scripts / "build-android.sh"
        recorder.rename(self.scripts / "record-build.py")
        recorder.write_text('#!/usr/bin/env bash\nexec python3 "$(dirname "$0")/record-build.py" "$@"\n',
                            encoding="utf-8")
        result, events = self.run_helper(package_helper, ["android-armv7", str(self.root / "runtime")], {
            "ANDROID_HOME": str(self.root / "SDK"),
            "RESTUNTS_BUILD_JOBS": "7", "CMAKE_BUILD_PARALLEL_LEVEL": "1",
        })
        self.assertEqual(result.returncode, expected_build_failure, result.stderr)
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["parallel"], "7")
        self.assertIn("--max-workers=7", events[0]["arguments"])


class AndroidNativeBuildJobsTests(unittest.TestCase):
    def setUp(self):
        self.cmake = shutil.which("cmake")
        self.ninja = shutil.which("ninja")
        if not self.cmake or not self.ninja or not shutil.which("cc"):
            self.skipTest("CMake, Ninja and a C compiler are needed to inspect native build pools")
        temporary = tempfile.TemporaryDirectory(prefix="android native jobs ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.build = self.root / "build"
        (self.root / "probe.c").write_text("void native_probe(void) {}\n", encoding="utf-8")
        (self.root / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.20)\nproject(AndroidJobs LANGUAGES C)\n'
            f'include("{(ROOT / "cmake/android-build-jobs.cmake").as_posix()}")\n'
            'add_library(native_shared SHARED probe.c)\n'
            'add_library(native_static STATIC probe.c)\n', encoding="utf-8")

    def configure(self, environment=None, jobs=None):
        values = dict(os.environ)
        for name in ("RESTUNTS_BUILD_JOBS", "CMAKE_BUILD_PARALLEL_LEVEL"):
            values.pop(name, None)
        values.update(environment or {})
        arguments = [self.cmake, "-S", str(self.root), "-B", str(self.build), "-G", "Ninja",
                     "-DCMAKE_MAKE_PROGRAM=" + self.ninja]
        if jobs is not None:
            arguments.append("-DRESTUNTS_ANDROID_BUILD_JOBS=" + str(jobs))
        return subprocess.run(arguments, env=values, text=True, capture_output=True,
                              timeout=30, check=False)

    def assert_native_pool(self, result, jobs):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        rules = (self.build / "CMakeFiles/rules.ninja").read_text()
        self.assertRegex(rules, rf"pool restunts_android_build\s+depth = {jobs}\b")
        build = (self.build / "build.ninja").read_text()
        # Both target compilations and both links share one total CPU budget.
        self.assertEqual(len(re.findall(r"^  pool = restunts_android_build$", build, re.MULTILINE)), 4)

    def test_requested_jobs_reach_compile_and_link_pools(self):
        result = self.configure({"RESTUNTS_BUILD_JOBS": "6", "CMAKE_BUILD_PARALLEL_LEVEL": "2"})
        self.assert_native_pool(result, 6)

    def test_cmake_parallel_environment_supplies_native_default(self):
        result = self.configure({"CMAKE_BUILD_PARALLEL_LEVEL": "5"})
        self.assert_native_pool(result, 5)

    def test_gradle_argument_replaces_a_cached_single_job_limit(self):
        self.assert_native_pool(self.configure(jobs=1), 1)
        self.assert_native_pool(self.configure(jobs=7), 7)

    def test_invalid_native_job_count_fails_configuration(self):
        result = self.configure(jobs=0)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must be a positive integer", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
