"""Check that captured media and intro timing actually gate the macOS smoke result."""

from array import array
import importlib.util
import json
import math
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
import wave


SCRIPT = Path(__file__).resolve().parents[1] / "test-macos-smoke.py"
SPEC = importlib.util.spec_from_file_location("macos_smoke_driver", SCRIPT)
DRIVER = importlib.util.module_from_spec(SPEC)
with mock.patch.object(sys, "path", [str(SCRIPT.parent)] + sys.path):
    SPEC.loader.exec_module(DRIVER)
SAMPLE_RATE = 22050
RECORDING_SECONDS = 6
GAME_PID = 123


class MacOSSmokeDriverTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="macos-smoke-driver-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.smoke = DRIVER.SmokeTest(self.root, self.root, self.root, self.root / "helper", {})
        self.smoke.game_pid = GAME_PID

    def intro_recording(self, *, silent=False, marker=True, wrong_pid=False,
                        wrong_frames=False, audio_interval=(100, 109),
                        recording_seconds=RECORDING_SECONDS, recording_error=None):
        frames = recording_seconds * SAMPLE_RATE
        samples = array("h")
        for frame in range(frames):
            value = 0 if silent else round(6000 * math.sin(2 * math.pi * 440 * frame / SAMPLE_RATE))
            samples.extend((value, value))
        if sys.byteorder != "little":
            samples.byteswap()
        with wave.open(str(self.root / "intro.wav"), "wb") as recording:
            recording.setnchannels(2)
            recording.setsampwidth(2)
            recording.setframerate(SAMPLE_RATE)
            recording.writeframes(samples.tobytes())
        (self.root / "audio-ready.json").write_text(json.dumps({"pid": GAME_PID}))
        captured = {"pid": GAME_PID + 1 if wrong_pid else GAME_PID,
                    "frames": frames + 1 if wrong_frames else frames,
                    "sample_rate": SAMPLE_RATE, "channels": 2,
                    "started_at": audio_interval[0], "ended_at": audio_interval[1]}
        recorder = mock.Mock()
        recorder.poll.return_value = None
        recorder.returncode = 1 if recording_error else 0
        active = False
        events = []

        def start_recording(*arguments, **options):
            nonlocal active
            active = True
            events.append("recording-start")
            return recorder

        def finish_recording(**options):
            nonlocal active
            events.append("recording-stop")
            active = False
            return json.dumps(captured), recording_error or ""

        recorder.communicate.side_effect = finish_recording

        def screenshot(stage, pid):
            self.assertFalse(active, "Screenshot helper must not overlap the audio helper")
            events.append(stage)
            timestamp = 99 if stage == "intro" else 110
            self.smoke.evidence["screenshots"][stage] = {
                "captured_at": timestamp,
                "validation": {"intro_words": ["stunts"] if marker else []},
            }

        with mock.patch.object(DRIVER.subprocess, "Popen", side_effect=start_recording), \
                mock.patch.object(self.smoke, "capture", side_effect=screenshot), \
                mock.patch.object(self.smoke, "native", side_effect=AssertionError(
                    "Native input must not be sent during the intro audio check")):
            self.smoke.intro_audio()
        return events

    def test_valid_intro_capture_records_decoded_audio_metrics(self):
        self.intro_recording()
        evidence = self.smoke.evidence
        self.assertEqual(evidence["audio"]["validation"]["duration_seconds"], RECORDING_SECONDS)
        self.assertFalse(evidence["intro_input_sent"])

    def test_screenshot_helpers_bracket_the_complete_audio_process(self):
        events = self.intro_recording()
        self.assertEqual(events, ["intro", "recording-start", "recording-stop", "intro-end"])

    def test_silent_audio_cannot_pass_through_the_driver(self):
        with self.assertRaisesRegex(ValueError, "silent"):
            self.intro_recording(silent=True)
        self.assertNotIn("audio", self.smoke.evidence)

    def test_non_menu_images_alone_do_not_prove_an_intro(self):
        with self.assertRaisesRegex(RuntimeError, "identifies the game's intro"):
            self.intro_recording(marker=False)

    def test_recording_from_the_wrong_process_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "different process"):
            self.intro_recording(wrong_pid=True)

    def test_capture_counts_must_agree_with_the_decoded_wav(self):
        with self.assertRaisesRegex(RuntimeError, "frame count"):
            self.intro_recording(wrong_frames=True)

    def test_audio_must_not_start_before_the_first_intro_capture(self):
        with self.assertRaisesRegex(RuntimeError, "bracket"):
            self.intro_recording(audio_interval=(98, 109))

    def test_audio_must_not_end_after_the_last_intro_capture(self):
        with self.assertRaisesRegex(RuntimeError, "bracket"):
            self.intro_recording(audio_interval=(100, 111))

    def test_reversed_recording_timestamps_are_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "bracket"):
            self.intro_recording(audio_interval=(109, 100))

    def test_truncated_recording_cannot_pass_with_valid_bracketing_images(self):
        with self.assertRaisesRegex(ValueError, "shorter"):
            self.intro_recording(recording_seconds=3)
        self.assertNotIn("audio", self.smoke.evidence)

    def test_failed_recorder_preserves_native_diagnostics(self):
        message = "Original stream failure: test interruption"
        with self.assertRaisesRegex(RuntimeError, "System audio recording failed"):
            self.intro_recording(recording_error=message)
        self.assertEqual((self.root / "audio.log").read_text(), message)
        self.assertNotIn("audio", self.smoke.evidence)

    def test_dummy_drivers_and_injected_libraries_are_not_inherited(self):
        with mock.patch.dict(os.environ, {"SDL_AUDIODRIVER": "dummy", "SDL_VIDEODRIVER": "dummy",
                                          "SDL_AUDIO_DRIVER": "disk",
                                          "DYLD_INSERT_LIBRARIES": "/wrong/library.dylib",
                                          "RESTUNTS_AUDIO_TRACE": "/wrong/trace",
                                          "SMOKE_TEST_KEEP": "present"}):
            environment = DRIVER.clean_environment()
        self.assertEqual(environment["SMOKE_TEST_KEEP"], "present")
        self.assertNotIn("SDL_AUDIODRIVER", environment)
        self.assertNotIn("SDL_VIDEODRIVER", environment)
        self.assertNotIn("SDL_AUDIO_DRIVER", environment)
        self.assertNotIn("DYLD_INSERT_LIBRARIES", environment)
        self.assertNotIn("RESTUNTS_AUDIO_TRACE", environment)

    def test_existing_launcher_preferences_are_restored_after_failure(self):
        settings = self.root / "Library/Application Support/Restunts/launcher.ini"
        settings.parent.mkdir(parents=True)
        settings.write_bytes(b"existing launcher preferences")
        settings.chmod(0o640)
        with mock.patch.object(DRIVER.Path, "home", return_value=self.root):
            with self.assertRaisesRegex(RuntimeError, "test failure"):
                with DRIVER.preserve_launcher_settings():
                    settings.write_bytes(b"changed by test")
                    raise RuntimeError("test failure")
        self.assertEqual(settings.read_bytes(), b"existing launcher preferences")
        self.assertEqual(settings.stat().st_mode & 0o777, 0o640)

    def test_test_created_launcher_preferences_are_removed(self):
        settings = self.root / "Library/Application Support/Restunts/launcher.ini"
        with mock.patch.object(DRIVER.Path, "home", return_value=self.root):
            with DRIVER.preserve_launcher_settings():
                settings.parent.mkdir(parents=True)
                settings.write_bytes(b"test preferences")
        self.assertFalse(settings.exists())


if __name__ == "__main__":
    unittest.main()
