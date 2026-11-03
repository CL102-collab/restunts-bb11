"""Reject blank screenshots, wrong UI stages and unusable intro audio evidence."""

from array import array
import importlib.util
import math
from pathlib import Path
import random
import struct
import sys
import tempfile
import unittest
import wave

try:
    from PIL import Image, ImageDraw
except ImportError:
    Image = ImageDraw = None


SCRIPT = Path(__file__).resolve().parents[1] / "macos_smoke_validation.py"
REPOSITORY = SCRIPT.parents[2]
SPEC = importlib.util.spec_from_file_location("macos_smoke_validation", SCRIPT)
VALIDATION = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VALIDATION)
SAMPLE_RATE = 22050
CAPTURE_SECONDS = 6
SIGNAL_AMPLITUDE = 6000
SIGNAL_FREQUENCY = 440
IMAGE_SIZE = (640, 480)
MENU_TEXT = ("Let's Drive", "Car", "Track", "Opponent", "Option")


def text_boxes(*values):
    return [{"text": value, "x": 0.1, "y": 0.1, "width": 0.2, "height": 0.05}
            for value in values]


class TemporaryEvidence(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="macos-smoke-evidence-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)


@unittest.skipUnless(Image is not None, "macOS screenshot checks require the pinned Pillow dependency")
class ScreenshotEvidenceTests(TemporaryEvidence):
    def save_image(self, image, name="screenshot.png"):
        path = self.root / name
        image.save(path)
        return path

    def detailed_image(self):
        image = Image.new("RGB", IMAGE_SIZE)
        image.putdata([(x % 256, y % 256, (x + y) % 256)
                       for y in range(image.height) for x in range(image.width)])
        return image

    def menu(self):
        return self.save_image(self.detailed_image(), "menu.png")

    def cockpit(self):
        image = Image.new("RGB", IMAGE_SIZE, (50, 180, 245))
        draw = ImageDraw.Draw(image)
        draw.rectangle((0, 165, 640, 315), fill=(35, 135, 40))
        draw.polygon(((285, 165), (355, 165), (595, 315), (45, 315)), fill=(100, 100, 100))
        draw.line((320, 170, 320, 310), fill=(230, 230, 120), width=5)
        draw.rectangle((0, 315, 640, 480), fill=(90, 55, 30))
        for center in (40, 155, 285, 410, 535, 615):
            draw.ellipse((center - 39, 340, center + 39, 445), fill=(15, 15, 15),
                         outline=(160, 160, 160), width=5)
            for number in range(10):
                angle = math.pi * number / 9
                x = center + math.cos(angle) * 27
                y = 393 + math.sin(angle) * 34
                draw.line((center, 393, x, y), fill=(230, 230, 230), width=3)
        return self.save_image(image, "race.png")

    def test_launcher_controls_and_real_menu_labels_are_recognized(self):
        path = self.menu()
        launcher = VALIDATION.validate_screenshot(
            path, "launcher", text_boxes("Start game", "Startup options", "HyperVision"))
        self.assertEqual(launcher["stage"], "launcher")
        menu = VALIDATION.validate_screenshot(path, "menu", text_boxes(*MENU_TEXT))
        self.assertIn("drive", menu["menu_labels"])

    def test_intro_and_a_distinct_cockpit_are_valid_evidence(self):
        menu = self.menu()
        intro = VALIDATION.validate_screenshot(menu, "intro", text_boxes("Distinctive Software"))
        self.assertIn("distinctive", intro["intro_words"])
        race = VALIDATION.validate_screenshot(self.cockpit(), "race", [], previous=menu)
        self.assertGreater(race["changed_fraction"], 0.5)
        self.assertGreaterEqual(race["instrument_groups"], 3)

    def test_native_window_title_does_not_identify_the_intro(self):
        title = text_boxes("Chocolate Stunts")
        title[0]["y"] = 0.95
        metrics = VALIDATION.validate_screenshot(self.menu(), "intro", title)
        self.assertEqual(metrics["intro_words"], [])

    def test_blank_and_transparent_images_are_rejected(self):
        for image in (Image.new("RGB", IMAGE_SIZE, "black"),
                      Image.new("RGBA", IMAGE_SIZE, (255, 0, 0, 0))):
            with self.subTest(mode=image.mode):
                with self.assertRaises(ValueError):
                    VALIDATION.validate_screenshot(self.save_image(image), "menu", text_boxes(*MENU_TEXT))

    def test_title_bar_does_not_hide_blank_game_content(self):
        image = Image.new("RGB", IMAGE_SIZE, "black")
        title = self.detailed_image().crop((0, 0, IMAGE_SIZE[0], 32))
        image.paste(title, (0, 0))
        with self.assertRaisesRegex(ValueError, "blank"):
            VALIDATION.validate_screenshot(self.save_image(image), "intro", text_boxes("Chocolate Stunts"))

    def test_existing_default_countach_cockpit_passes_with_window_chrome(self):
        menu = REPOSITORY / "assets/menus/main.png"
        cockpit = REPOSITORY / "site/assets/templeos/default-cockpit.png"
        with Image.open(cockpit) as source:
            image = Image.new("RGB", (source.width, source.height + 22), (225, 225, 225))
            image.paste(source.convert("RGB"), (0, 22))
        result = VALIDATION.validate_screenshot(self.save_image(image), "race", [], previous=menu)
        self.assertGreaterEqual(result["instrument_groups"], 3)

    def test_corrupt_and_truncated_pngs_are_rejected(self):
        path = self.menu()
        original = path.read_bytes()
        for content in (b"not a PNG", original[:len(original) // 2], original[:-12]):
            with self.subTest(length=len(content)):
                path.write_bytes(content)
                with self.assertRaises(ValueError):
                    VALIDATION.validate_screenshot(path, "menu", text_boxes(*MENU_TEXT))

    def test_menu_is_rejected_as_intro_or_race(self):
        path = self.menu()
        for stage in ("intro", "race"):
            with self.subTest(stage=stage):
                with self.assertRaises(ValueError):
                    VALIDATION.validate_screenshot(path, stage, text_boxes(*MENU_TEXT), previous=path)

    def test_a_changed_textured_image_is_not_enough_to_claim_a_race(self):
        menu = self.menu()
        randomizer = random.Random(431)
        noise = Image.new("RGB", IMAGE_SIZE)
        noise.putdata([tuple(randomizer.randrange(256) for _ in range(3))
                       for _ in range(noise.width * noise.height)])
        with self.assertRaisesRegex(ValueError, "coherent road and cockpit"):
            VALIDATION.validate_screenshot(self.save_image(noise), "race", [], previous=menu)

    def test_error_dialog_and_unchanged_cockpit_are_rejected(self):
        menu, race = self.menu(), self.cockpit()
        with self.assertRaisesRegex(ValueError, "error message"):
            VALIDATION.validate_screenshot(race, "race", text_boxes("Cannot load game data"), previous=menu)
        with self.assertRaisesRegex(ValueError, "materially change"):
            VALIDATION.validate_screenshot(race, "race", [], previous=race)

    def test_small_image_and_unrelated_menu_text_are_rejected(self):
        small = self.save_image(self.detailed_image().resize((160, 100)))
        with self.assertRaises(ValueError):
            VALIDATION.validate_screenshot(small, "menu", text_boxes(*MENU_TEXT))
        with self.assertRaisesRegex(ValueError, "menu labels"):
            VALIDATION.validate_screenshot(self.menu(), "menu", text_boxes("Help", "Preferences"))


class AudioEvidenceTests(TemporaryEvidence):
    def save_audio(self, values, channels=2, sample_bytes=2, rate=SAMPLE_RATE):
        path = self.root / "intro.wav"
        if sample_bytes == 2:
            samples = array("h", values)
            if sys.byteorder != "little":
                samples.byteswap()
            payload = samples.tobytes()
        else:
            payload = bytes(values)
        with wave.open(str(path), "wb") as recording:
            recording.setnchannels(channels)
            recording.setsampwidth(sample_bytes)
            recording.setframerate(rate)
            recording.writeframes(payload)
        return path

    def signal(self, seconds=CAPTURE_SECONDS, channels=2):
        for frame in range(round(seconds * SAMPLE_RATE)):
            sample = round(SIGNAL_AMPLITUDE * math.sin(2 * math.pi * SIGNAL_FREQUENCY * frame / SAMPLE_RATE))
            for _ in range(channels):
                yield sample

    def test_sustained_pcm_is_accepted_with_duration_and_levels(self):
        metrics = VALIDATION.validate_audio(self.save_audio(self.signal()))
        self.assertEqual(metrics["sample_rate"], SAMPLE_RATE)
        self.assertEqual(metrics["duration_seconds"], CAPTURE_SECONDS)
        self.assertEqual(metrics["active_seconds"], CAPTURE_SECONDS)
        self.assertGreater(metrics["ac_rms"], 0.01)

    def test_initial_silence_does_not_hide_later_intro_music(self):
        samples = [0] * (2 * SAMPLE_RATE * 2)
        samples.extend(self.signal(seconds=4))
        metrics = VALIDATION.validate_audio(self.save_audio(samples))
        self.assertEqual(metrics["active_seconds"], 4)

    def test_silence_and_constant_dc_are_rejected(self):
        for values in ([0] * (CAPTURE_SECONDS * SAMPLE_RATE * 2),
                       [12000] * (CAPTURE_SECONDS * SAMPLE_RATE * 2),
                       [32767] * (CAPTURE_SECONDS * SAMPLE_RATE * 2),
                       [12000, -12000] * (CAPTURE_SECONDS * SAMPLE_RATE)):
            with self.subTest(first=values[:2]):
                with self.assertRaisesRegex(ValueError, "silent, constant"):
                    VALIDATION.validate_audio(self.save_audio(values))

    def test_one_loud_second_is_not_sustained_intro_audio(self):
        samples = list(self.signal(seconds=1))
        samples.extend([0] * (5 * SAMPLE_RATE * 2))
        with self.assertRaisesRegex(ValueError, "sustained sound"):
            VALIDATION.validate_audio(self.save_audio(samples))

    def test_clicks_in_every_second_are_not_sustained_sound(self):
        samples = [0] * (CAPTURE_SECONDS * SAMPLE_RATE * 2)
        for second in range(CAPTURE_SECONDS):
            offset = second * SAMPLE_RATE * 2
            samples[offset:offset + 40] = [SIGNAL_AMPLITUDE, -SIGNAL_AMPLITUDE] * 20
        with self.assertRaisesRegex(ValueError, "sustained sound"):
            VALIDATION.validate_audio(self.save_audio(samples))

    def test_short_or_truncated_audio_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "shorter"):
            VALIDATION.validate_audio(self.save_audio(self.signal(seconds=1)))
        path = self.save_audio(self.signal())
        path.write_bytes(path.read_bytes()[:-10])
        with self.assertRaisesRegex(ValueError, "truncated"):
            VALIDATION.validate_audio(path)

    def test_incomplete_pcm_frame_is_rejected_even_with_consistent_riff_size(self):
        path = self.save_audio(self.signal())
        raw = bytearray(path.read_bytes()[:-2])
        struct.pack_into("<I", raw, 4, len(raw) - 8)
        struct.pack_into("<I", raw, 40, len(raw) - 44)
        path.write_bytes(raw)
        with self.assertRaisesRegex(ValueError, "complete PCM frame"):
            VALIDATION.validate_audio(path)

    def test_wrong_sample_format_or_implausible_rate_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "PCM16"):
            VALIDATION.validate_audio(self.save_audio([128] * (SAMPLE_RATE * 2), sample_bytes=1))
        with self.assertRaisesRegex(ValueError, "implausible"):
            VALIDATION.validate_audio(self.save_audio([0] * 100, rate=100))


class MenuTargetTests(unittest.TestCase):
    def test_drive_target_is_distinct_from_other_menu_controls(self):
        option = {"text": "Option", "x": 0.80, "y": 0.20, "width": 0.15, "height": 0.10}
        drive = {"text": "Let's Drlve", "x": 0.42, "y": 0.23, "width": 0.17, "height": 0.07}
        track = {"text": "Track", "x": 0.61, "y": 0.55, "width": 0.13, "height": 0.09}
        self.assertEqual(VALIDATION.menu_drive_target([option, drive, track]), drive)

    def test_missing_or_empty_drive_target_is_rejected(self):
        for entries in (text_boxes("Option", "Track"),
                        [{"text": "Drive", "x": 0.4, "y": 0.2, "width": 0, "height": 0.05}],
                        [{"text": "Drive", "x": 0.9, "y": 0.2, "width": 0.2, "height": 0.05}]):
            with self.subTest(entries=entries):
                with self.assertRaisesRegex(ValueError, "usable OCR target"):
                    VALIDATION.menu_drive_target(entries)


if __name__ == "__main__":
    unittest.main()
