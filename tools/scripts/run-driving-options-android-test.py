#!/usr/bin/env python3
"""Build and run driving-option regressions on an attached Android device.

Uses the NDK's Android compiler and adb, so assertions execute against Bionic
inside Android rather than a host build with Android preprocessor definitions.
For example:

  python3 tools/scripts/run-driving-options-android-test.py \
      --ndk "$ANDROID_NDK_ROOT" --adb "$ANDROID_SDK_ROOT/platform-tools/adb" \
      --serial emulator-5556 --sdk "$ANDROID_SDK_ROOT"
"""

import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
TARGETS = {
    "arm64-v8a": "aarch64-linux-android21-clang",
    "armeabi-v7a": "armv7a-linux-androideabi21-clang",
    "x86": "i686-linux-android21-clang",
    "x86_64": "x86_64-linux-android21-clang",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ndk", type=Path, default=os.environ.get("ANDROID_NDK_ROOT"),
                        help="Installed Android NDK (or ANDROID_NDK_ROOT).")
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--sdk", type=Path,
                        help="Also compile launcher-option tests and run them on Android ART.")
    parser.add_argument("--java", default="java", help="Java runtime containing jdk.compiler.")
    parser.add_argument("--serial", help="adb device serial; required with multiple devices.")
    parser.add_argument("--abi", choices=TARGETS,
                        help="Target ABI; defaults to the attached device's primary ABI.")
    parser.add_argument("--output-dir", type=Path,
                        help="Build and log directory (default: out/android-dws-tests/ABI).")
    args = parser.parse_args()
    if args.ndk is None:
        parser.error("Supply --ndk or ANDROID_NDK_ROOT.")
    adb = [args.adb] + (["-s", args.serial] if args.serial else [])
    abi = args.abi or subprocess.check_output(
        adb + ["shell", "getprop", "ro.product.cpu.abi"], text=True).strip()
    if abi not in TARGETS:
        parser.error("Unsupported Android ABI: " + abi)
    toolchains = list((args.ndk / "toolchains/llvm/prebuilt").glob("*/bin/" + TARGETS[abi]))
    if len(toolchains) != 1:
        parser.error("Cannot locate the NDK compiler for " + abi)
    # Compiler wrappers select their target from the executable's original name.
    compiler = str(toolchains[0])
    output = args.output_dir or ROOT / "out/android-dws-tests" / abi
    output.mkdir(parents=True, exist_ok=True)
    tests = ROOT / "src/restunts/tests"
    source = ROOT / "src/restunts/c"
    shared = ["-std=c99", "-O2", "-Wall", "-Wextra", "-ffunction-sections",
              "-fdata-sections", "-Wl,--gc-sections"]
    fixtures = [
        ("test-penalty-routing", ["race_route.c", "strlib.c"],
         ["-Wno-sign-compare"],
         "DWS default/on/off, option parsing, switched-route penalties, legal routes"),
        ("test-command-line", ["command_line.c"],
         ["-DRESTUNTS_SDL3",
          '-DRESTUNTS_GIT_HASH="0123456789abcdef0123456789abcdef01234567"',
          '-DRESTUNTS_BUILD_DATE="Feb 12 1991"'],
         "Android command-line help and option classification"),
        ("test-startup", ["math.c", "carsetup.c", "frame_adaptive.c", "owoot.c", "trackobj.c",
                          "statecar.c", "strlib.c", "physics_collision.c", "physics_grip.c",
                          "race_route.c", "full_data.c", "full_strings.c", "headless_data.c",
                          "legacy.c"],
         ["-DRESTUNTS_SDL3", "-Wno-pointer-sign", "-Wno-missing-field-initializers",
          "-Wno-unused-variable"],
         "Android game startup with default, enabled, and disabled driving options"),
    ]
    remote = "/data/local/tmp/restunts-driving-options"
    with (output / "android-runtime.log").open("w") as log:
        def run(command):
            log.write("$ " + shlex.join(map(str, command)) + "\n")
            result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT)
            log.write(result.stdout)
            log.flush()
            if result.returncode:
                raise RuntimeError(result.stdout or "Command failed: " + str(command))
            return result.stdout

        version = run(adb + ["shell", "getprop", "ro.build.version.release"]).strip()
        print("Android " + version + ", ABI " + abi)
        run(adb + ["shell", "mkdir", "-p", remote])
        for name, sources, flags, description in fixtures:
            binary = output / name
            run([compiler] + shared + flags + [str(tests / (name + ".c"))]
                + [str(source / item) for item in sources] + ["-o", str(binary)])
            run(adb + ["push", str(binary), remote + "/" + name])
            run(adb + ["shell", "chmod", "755", remote + "/" + name])
            result = run(adb + ["shell", remote + "/" + name
                               + '; echo RESTUNTS_TEST_EXIT=$?'])
            # Older Android adb shells do not forward the remote exit status.
            if not re.search(r"RESTUNTS_TEST_EXIT=0\s*$", result):
                raise RuntimeError(name + " failed on Android:\n" + result)
            print("PASS " + name + ": " + description)
        if args.sdk:
            properties = dict(line.split("=", 1) for line in
                              (ROOT / "android/toolchain.properties").read_text().splitlines()
                              if "=" in line and not line.startswith("#"))
            platform = args.sdk / "platforms" / (
                "android-" + properties["ANDROID_COMPILE_SDK"]) / "android.jar"
            d8 = args.sdk / "build-tools" / properties["ANDROID_BUILD_TOOLS"] / "d8"
            classes = output / "java-classes"
            classes.mkdir(exist_ok=True)
            run([args.java, "com.sun.tools.javac.Main", "-source", "8", "-target", "8",
                 "-bootclasspath", str(platform), "-d", str(classes),
                 str(ROOT / "android/app/src/main/java/org/restunts/android/LaunchOptions.java"),
                 str(ROOT / "android/tests/LaunchOptionsTest.java")])
            dex = output / "launch-options-test.jar"
            run([str(d8), "--min-api", "21", "--lib", str(platform), "--output", str(dex)]
                + [str(item) for item in sorted(classes.rglob("*.class"))])
            run(adb + ["push", str(dex), remote + "/" + dex.name])
            result = run(adb + ["shell", "CLASSPATH=" + remote + "/" + dex.name
                               + " app_process /system/bin org.restunts.android.LaunchOptionsTest"
                               + "; echo RESTUNTS_TEST_EXIT=$?"])
            if not re.search(r"RESTUNTS_TEST_EXIT=0\s*$", result):
                raise RuntimeError("Launcher option tests failed on Android ART:\n" + result)
            print("PASS LaunchOptionsTest: Android ART, defaults and all driving-rule combinations")
    print("Execution log: " + str(output / "android-runtime.log"))


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
