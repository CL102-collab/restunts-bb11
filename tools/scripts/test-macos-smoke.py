#!/usr/bin/env python3
"""Launch an archived macOS package through its startup window and inspect real A/V output."""

import argparse
from contextlib import contextmanager
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time

from macos_smoke_validation import menu_drive_target, validate_audio, validate_screenshot


SCRIPTS = Path(__file__).resolve().parent
TARGET = "macos-universal"
POLL_SECONDS = 1
COMMAND_TIMEOUT = 30
COMPILE_TIMEOUT = 120
STARTUP_TIMEOUT = 45
FRAME_TIMEOUT = 60
AUDIO_SECONDS = 8
AUDIO_READY_TIMEOUT = 15
AUDIO_FINISH_TIMEOUT = AUDIO_SECONDS + COMMAND_TIMEOUT
ACCELERATION_SECONDS = 2
RACE_ACTION_HOLD_SECONDS = 0.3
RACE_START_SETTLE_SECONDS = 1
MINIMUM_MOTION_FRACTION = 0.01
PIXEL_CHANGE_THRESHOLD = 12
REFERENCE_IMAGE_SIZE = (320, 200)
KILL_TIMEOUT = 5
HELPER_NAME = "macos-smoke"
HELPER_MINIMUM_MACOS = "14.0"
FORBIDDEN_ENVIRONMENT = {"DYLD_INSERT_LIBRARIES", "DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH",
                         "SDL_VIDEODRIVER", "SDL_AUDIODRIVER", "RESTUNTS_AUDIO_TRACE"}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def package_helper():
    specification = importlib.util.spec_from_file_location(
        "macos_smoke_packages", SCRIPTS / "release-packages.py")
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


def clean_environment():
    # A package test must resolve its own libraries and use real Cocoa/CoreAudio.
    return {key: value for key, value in os.environ.items()
            if key not in FORBIDDEN_ENVIRONMENT and not key.startswith("SDL_AUDIO_")}


@contextmanager
def preserve_launcher_settings():
    settings = Path.home() / "Library/Application Support/Restunts/launcher.ini"
    original = settings.read_bytes() if settings.exists() else None
    mode = settings.stat().st_mode if original is not None else None
    try:
        yield
    finally:
        if original is not None:
            settings.write_bytes(original)
            settings.chmod(mode)
        else:
            settings.unlink(missing_ok=True)


def stop(process):
    if process is None or process.poll() is not None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
        process.wait(timeout=KILL_TIMEOUT)
    except ProcessLookupError:
        process.wait()
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait(timeout=KILL_TIMEOUT)


class SmokeTest:
    def __init__(self, runtime, game_data, results, helper, environment):
        self.runtime = runtime
        self.game_data = game_data
        self.results = results
        self.helper = helper
        self.environment = environment
        self.launcher = None
        self.start_requested = False
        self.game_pid = None
        self.audio = None
        self.evidence = {"passed": False, "target": TARGET, "screenshots": {},
                         "capture": "ScreenCaptureKit app audio and window images"}

    def log(self, message):
        print(message, flush=True)
        with (self.results / "runner.log").open("a", encoding="utf-8") as output:
            output.write(message + "\n")

    def command(self, arguments, *, timeout=COMMAND_TIMEOUT, check=True):
        result = subprocess.run([str(value) for value in arguments],
                                env=self.environment, capture_output=True, text=True,
                                timeout=timeout)
        if result.stderr:
            self.log(result.stderr.rstrip())
        if check:
            require(result.returncode == 0,
                    f"{Path(arguments[0]).name} failed ({result.returncode}): {result.stdout}")
        return result

    def native(self, *arguments, check=True):
        result = self.command([self.helper, *arguments], check=check)
        return json.loads(result.stdout) if result.returncode == 0 else None

    def wait_for(self, label, callback, timeout):
        deadline = time.monotonic() + timeout
        last_error = None
        while time.monotonic() < deadline:
            if self.launcher is not None:
                status = self.launcher.poll()
                require(status is None or (self.start_requested and status == 0),
                        f"Launcher exited before {label}; see launcher.log")
            try:
                value = callback()
                if value is not None:
                    return value
            except (RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
                last_error = str(error)
            time.sleep(POLL_SECONDS)
        raise RuntimeError(f"Timed out waiting for {label}: {last_error}")

    def capture(self, stage, pid, previous=None):
        image = self.results / f"{stage}.png"
        metadata = self.native("capture", pid, image)
        require(metadata["pid"] == pid, "Screenshot belongs to a different process")
        write_json(self.results / f"{stage}-capture.json", metadata)
        validation_stage = "intro" if stage.startswith("intro") else stage
        metrics = validate_screenshot(image, validation_stage, metadata["texts"], previous=previous)
        require(metrics["width"] == metadata["width"] and
                metrics["height"] == metadata["height"], "Screenshot dimensions do not match capture")
        self.evidence["screenshots"][stage] = dict(metadata, validation=metrics)
        return metrics

    def configure_launcher(self):
        result = self.command(["/usr/bin/osascript", SCRIPTS / "macos-smoke.applescript",
                               "configure", self.launcher.pid, self.game_data], check=False)
        if result.returncode != 0:
            raise RuntimeError("Startup controls are not ready: " + result.stderr)
        self.log(result.stdout.rstrip())
        return True

    def intro_audio(self):
        ready = self.results / "audio-ready.json"
        wave_path = self.results / "intro.wav"
        # Bracket recording with screenshots instead of starting another
        # ScreenCaptureKit helper while the audio stream is active.
        self.capture("intro", self.game_pid)
        self.audio = subprocess.Popen(
            [str(self.helper), "audio", str(self.game_pid), str(wave_path),
             str(AUDIO_SECONDS), str(ready)], env=self.environment,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            start_new_session=True)

        def recording_ready():
            require(self.audio.poll() is None, "Audio recorder exited before it was ready")
            return json.loads(ready.read_text()) if ready.is_file() else None

        started = self.wait_for("app audio recording", recording_ready, AUDIO_READY_TIMEOUT)
        require(started["pid"] == self.game_pid, "Audio filter uses the wrong process")
        # No other capture helper or input runs until recording has finished.
        output, errors = self.audio.communicate(timeout=AUDIO_FINISH_TIMEOUT)
        (self.results / "audio.log").write_text(errors, encoding="utf-8")
        (self.results / "audio-capture.json").write_text(output, encoding="utf-8")
        require(self.audio.returncode == 0, "System audio recording failed; see audio.log")
        recorded = json.loads(output)
        self.capture("intro-end", self.game_pid)
        require(self.evidence["screenshots"]["intro"]["validation"]["intro_words"] or
                self.evidence["screenshots"]["intro-end"]["validation"]["intro_words"],
                "Neither audio-bracketing screenshot identifies the game's intro")
        require(recorded["pid"] == self.game_pid, "Recorded audio belongs to a different process")
        audio_metrics = validate_audio(wave_path)
        require(recorded["frames"] == audio_metrics["sample_frames"],
                "WAV frame count differs from capture")
        require(recorded["sample_rate"] == audio_metrics["sample_rate"] and
                recorded["channels"] == audio_metrics["channels"],
                "WAV format differs from the captured stream")
        intro_start = self.evidence["screenshots"]["intro"]["captured_at"]
        intro_end = self.evidence["screenshots"]["intro-end"]["captured_at"]
        require(intro_start <= recorded["started_at"] <= recorded["ended_at"] <= intro_end,
                "Intro screenshots do not bracket the audio recording")
        self.evidence["audio"] = dict(recorded, validation=audio_metrics)
        self.evidence["intro_input_sent"] = False
        self.log(f"Recorded {audio_metrics['duration_seconds']:.2f}s of validated intro audio")

    def run(self):
        self.evidence["preflight"] = self.native("preflight")
        profile = self.command(["/usr/sbin/system_profiler", "SPAudioDataType", "-json"])
        (self.results / "audio-devices.json").write_text(profile.stdout, encoding="utf-8")
        wrapper = self.runtime / "Restunts.app/Contents/MacOS/restunts-launcher"
        require(wrapper.is_file() and os.access(wrapper, os.X_OK), "Packaged Restunts.app is missing")
        with (self.results / "launcher.log").open("w", encoding="utf-8") as output:
            self.launcher = subprocess.Popen(
                [str(wrapper), "--data-dir", str(self.game_data)],
                stdout=output, stderr=subprocess.STDOUT, cwd=self.runtime,
                env=self.environment, start_new_session=True)
        self.wait_for("startup window controls", self.configure_launcher, STARTUP_TIMEOUT)
        self.capture("launcher", self.launcher.pid)
        self.start_requested = True
        self.command(["/usr/bin/osascript", SCRIPTS / "macos-smoke.applescript",
                      "start", self.launcher.pid, self.game_data])
        game = self.wait_for("the launched game", lambda: self.native(
            "find", self.runtime / "bin/restunts", check=False), STARTUP_TIMEOUT)
        self.game_pid = game["pid"]
        self.evidence["game"] = game
        self.evidence["launcher_pid"] = self.launcher.pid
        self.wait_for("the intro window", lambda: self.capture(
            "intro-before-audio", self.game_pid), STARTUP_TIMEOUT)
        self.intro_audio()
        self.native("key", self.game_pid, "space")
        self.wait_for("main menu", lambda: self.capture("menu", self.game_pid), FRAME_TIMEOUT)
        self.log("Main menu image and menu labels validated")
        menu_capture = self.evidence["screenshots"]["menu"]
        drive = menu_drive_target(menu_capture["texts"])
        click_x = menu_capture["window_x"] + (drive["x"] + drive["width"] / 2) * \
            menu_capture["window_width"]
        click_y = menu_capture["window_y"] + (1 - drive["y"] - drive["height"] / 2) * \
            menu_capture["window_height"]
        self.evidence["drive_click"] = self.native("click", self.game_pid, click_x, click_y)
        menu = self.results / "menu.png"
        self.wait_for("a new game's cockpit", lambda: self.capture(
            "race", self.game_pid, previous=menu), FRAME_TIMEOUT)
        # The first cockpit can still be the paused start-flag animation. Space
        # is the game's action button; acceleration alone does not skip it.
        self.native("key", self.game_pid, "space", RACE_ACTION_HOLD_SECONDS)
        time.sleep(RACE_START_SETTLE_SECONDS)
        self.capture("race", self.game_pid, previous=menu)
        initial = self.results / "race.png"
        before = self.results / "race-before-input.png"
        shutil.copyfile(initial, before)
        self.native("key", self.game_pid, "up", ACCELERATION_SECONDS)
        self.capture("race", self.game_pid, previous=menu)
        from PIL import Image, ImageChops
        with Image.open(before) as old, Image.open(initial) as new:
            size = REFERENCE_IMAGE_SIZE
            difference = ImageChops.difference(old.convert("RGB").resize(size),
                                             new.convert("RGB").resize(size))
            # The upper viewport must react to driving, not just a HUD clock.
            viewport = difference.crop((0, 0, size[0], size[1] // 2)).convert("L")
            changed = sum(count for value, count in enumerate(viewport.histogram())
                          if value > PIXEL_CHANGE_THRESHOLD)
            motion = changed / (viewport.width * viewport.height)
        require(motion >= MINIMUM_MOTION_FRACTION, "New game did not visibly respond to acceleration")
        self.evidence["driving_motion_fraction"] = motion
        self.evidence["passed"] = True
        self.log("New game, screenshot content, and visible response to acceleration validated")

    def close(self):
        stop(self.audio)
        if self.audio is not None:
            output, errors = self.audio.communicate(timeout=KILL_TIMEOUT)
            (self.results / "audio.log").write_text(errors, encoding="utf-8")
            (self.results / "audio-capture.json").write_text(output, encoding="utf-8")
        if self.game_pid is not None:
            try:
                os.kill(self.game_pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        stop(self.launcher)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("game_data", type=Path)
    parser.add_argument("results", type=Path)
    parser.add_argument("--commit", required=True)
    args = parser.parse_args()
    require(sys.platform == "darwin", "This smoke test requires a logged-in macOS desktop")
    args.archive = args.archive.resolve(strict=True)
    args.game_data = args.game_data.resolve(strict=True)
    args.results = args.results.resolve()
    args.results.mkdir(parents=True, exist_ok=False)
    helper = args.results / HELPER_NAME
    environment = clean_environment()
    packages = package_helper()
    packages.verify_package(args.archive, TARGET, args.commit,
                            os.environ.get("GITHUB_RUN_ID"), os.environ.get("GITHUB_RUN_ATTEMPT"))
    source_commit = args.commit
    with tempfile.TemporaryDirectory(prefix="restunts-macos-smoke-") as temporary, \
            preserve_launcher_settings():
        work = Path(temporary)
        runtime, data = work / "runtime", work / "game-data"
        runtime.mkdir()
        files, modes = packages.archive_contents(args.archive)
        for name, contents in files.items():
            destination = runtime / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(contents)
            destination.chmod(modes[name])
        shutil.copytree(args.game_data, data)
        smoke = SmokeTest(runtime, data, args.results, helper, environment)
        smoke.evidence.update(source_commit=source_commit,
                              archive_sha256=hashlib.sha256(args.archive.read_bytes()).hexdigest())
        try:
            smoke.command(["/usr/bin/xcrun", "swiftc", "-swift-version", "5", "-parse-as-library",
                           "-target", f"{os.uname().machine}-apple-macosx{HELPER_MINIMUM_MACOS}",
                           SCRIPTS / "macos-smoke.swift", "-o", helper,
                           "-framework", "ScreenCaptureKit", "-framework", "Vision",
                           "-framework", "AVFoundation", "-framework", "AppKit"],
                          timeout=COMPILE_TIMEOUT)
            smoke.run()
        except Exception as error:
            smoke.evidence["error"] = str(error)
            smoke.log(str(error))
            raise
        finally:
            try:
                smoke.close()
            except Exception as error:
                smoke.evidence.update(passed=False, cleanup_error=str(error))
                raise
            finally:
                write_json(args.results / "validation.json", smoke.evidence)
                helper.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
