"""Validate visible game output and recorded PCM from the macOS package smoke test."""

from array import array
from difflib import SequenceMatcher
import io
import math
from pathlib import Path
import re
import struct
import sys
import unicodedata
import wave


MINIMUM_IMAGE_WIDTH = 320
MINIMUM_IMAGE_HEIGHT = 200
MAXIMUM_IMAGE_PIXELS = 16 * 1024 * 1024
ANALYSIS_IMAGE_SIZE = (320, 240)
CLIENT_DETAIL_REGION = (0.02, 0.10, 0.98, 0.98)
MINIMUM_IMAGE_COLORS = 12
MINIMUM_IMAGE_RANGE = 80
MINIMUM_IMAGE_DEVIATION = 8
MINIMUM_OPAQUE_FRACTION = 0.95
MINIMUM_CHANGED_FRACTION = 0.40
MINIMUM_MEAN_CHANGE = 18
PIXEL_CHANGE_THRESHOLD = 24
OCR_MATCH_RATIO = 0.78
MENU_LABELS = ("car", "track", "opponent", "option")
ERROR_WORDS = ("error", "fatal", "cannot", "failed", "exception", "missing")
ERROR_PHRASES = ("not found", "does not exist", "unable to", "invalid game")
INTRO_WORDS = ("stunts", "broderbund", "distinctive", "software", "nuked")
MAXIMUM_INTRO_TEXT_MIDPOINT = 0.90
RACE_HUD_WORDS = ("mph", "kmh", "rpm", "gear", "time", "lap")
RACE_SKY_REGION = (0.05, 0.08, 0.95, 0.40)
RACE_TERRAIN_REGION = (0.05, 0.25, 0.95, 0.63)
RACE_ROAD_REGION = (0.30, 0.35, 0.70, 0.63)
RACE_DASHBOARD_REGION = (0.04, 0.65, 0.96, 0.97)
SKY_MINIMUM_BLUE = 80
SKY_RED_SEPARATION = 20
SKY_GREEN_TOLERANCE = 35
GRASS_MINIMUM_GREEN = 55
GRASS_RED_MULTIPLIER = 1.2
GRASS_BLUE_MULTIPLIER = 1.1
ROAD_MINIMUM_BRIGHTNESS = 40
ROAD_MAXIMUM_BRIGHTNESS = 200
ROAD_MAXIMUM_COLOR_SPREAD = 35
DASHBOARD_DARK_THRESHOLD = 65
DASHBOARD_LIGHT_THRESHOLD = 155
DASHBOARD_MAXIMUM_LIGHT_SPREAD = 65
INSTRUMENT_GROUP_COUNT = 8
MINIMUM_INSTRUMENT_DARK_FRACTION = 0.05
MINIMUM_INSTRUMENT_LIGHT_FRACTION = 0.01
MINIMUM_INSTRUMENT_GROUPS = 3
MINIMUM_DASHBOARD_DARK_FRACTION = 0.10
MINIMUM_DASHBOARD_LIGHT_FRACTION = 0.01
MINIMUM_SKY_FRACTION = 0.12
MINIMUM_GRASS_FRACTION = 0.10
MINIMUM_ROAD_FRACTION = 0.15
MAXIMUM_HUD_TEXT_MIDPOINT = 0.48
MINIMUM_RACE_HUD_WORDS = 2
MAXIMUM_AUDIO_BYTES = 128 * 1024 * 1024
PCM_SAMPLE_BYTES = 2
PCM_SCALE = 32768
MINIMUM_AUDIO_RATE = 22050
MAXIMUM_AUDIO_RATE = 96000
MINIMUM_AUDIO_RMS = 0.001
MINIMUM_AUDIO_PEAK = 0.005
MINIMUM_AUDIO_AC_RATIO = 0.10
MAXIMUM_AUDIO_CLIPPED_FRACTION = 0.10
MINIMUM_ACTIVE_SAMPLE_FRACTION = 0.02
MINIMUM_ACTIVE_BLOCK_FRACTION = 0.50
MINIMUM_ACTIVE_BLOCKS = 3
CLIPPED_SAMPLE_AMPLITUDE = PCM_SCALE - 8


def _load_png(path):
    # Pillow is only needed by the macOS image checks, not the host-only audio tests.
    from PIL import Image, ImageStat

    try:
        with Image.open(path) as source:
            if source.format != "PNG":
                raise ValueError("screenshot must be a PNG image")
            width, height = source.size
            if width < MINIMUM_IMAGE_WIDTH or height < MINIMUM_IMAGE_HEIGHT:
                raise ValueError("screenshot is smaller than the game viewport")
            if width * height > MAXIMUM_IMAGE_PIXELS:
                raise ValueError("screenshot dimensions exceed the smoke-test limit")
            source.verify()
        # verify() checks chunk CRCs, while load() also checks the compressed pixels.
        with Image.open(path) as source:
            source.load()
            alpha = source.convert("RGBA").getchannel("A").histogram()
            opaque_fraction = sum(alpha[250:]) / (width * height)
            if opaque_fraction < MINIMUM_OPAQUE_FRACTION:
                raise ValueError("screenshot is mostly transparent")
            image = source.convert("RGB")
        analysis = image.resize(ANALYSIS_IMAGE_SIZE)
        # OS title bars can remain perfectly valid while the game client is
        # black or transparent. Require detail inside the client area itself.
        left, top, right, bottom = CLIENT_DETAIL_REGION
        client = analysis.crop((int(left * analysis.width), int(top * analysis.height),
                                int(right * analysis.width), int(bottom * analysis.height)))
        luminance = client.convert("L")
        histogram = luminance.histogram()
        occupied = [index for index, count in enumerate(histogram) if count]
        luminance_range = occupied[-1] - occupied[0]
        # Saturated DOS colors can contrast strongly without approaching white.
        # Compare each channel spatially so a uniform saturated image still fails.
        dynamic_range = max(high - low for low, high in client.getextrema())
        deviation = ImageStat.Stat(luminance).stddev[0]
        colors = len(client.getcolors(client.width * client.height))
        if (colors < MINIMUM_IMAGE_COLORS or dynamic_range < MINIMUM_IMAGE_RANGE
                or deviation < MINIMUM_IMAGE_DEVIATION):
            raise ValueError("screenshot is blank or lacks meaningful rendered detail")
        return image, analysis, {
            "width": width, "height": height, "colors": colors,
            "dynamic_range": dynamic_range, "luminance_range": luminance_range,
            "luminance_deviation": deviation,
            "opaque_fraction": opaque_fraction,
        }
    except (OSError, EOFError, SyntaxError) as error:
        raise ValueError(f"screenshot cannot be fully decoded: {error}") from error


def _normalized_text(value):
    text = unicodedata.normalize("NFKD", value).lower()
    return " ".join(re.findall(r"[a-z0-9]+", text))


def _ocr_entries(texts):
    entries = []
    for entry in texts:
        if not isinstance(entry, dict) or not isinstance(entry.get("text"), str):
            raise ValueError("OCR evidence must contain text and normalized bounding boxes")
        rectangle = {}
        for field in ("x", "y", "width", "height"):
            value = entry.get(field, 0)
            if not isinstance(value, (int, float)) or not math.isfinite(value) or not 0 <= value <= 1:
                raise ValueError("OCR bounding boxes must use normalized coordinates")
            rectangle[field] = value
        entries.append(dict(rectangle, text=_normalized_text(entry["text"])))
    return entries


def _has_word(entries, wanted):
    for entry in entries:
        words = entry["text"].split()
        candidates = words + ["".join(words)]
        for word in candidates:
            word = word.replace("0", "o")
            if word == wanted:
                return True
            # Three-letter labels need an exact match to avoid unrelated words.
            if len(wanted) > 3 and abs(len(word) - len(wanted)) <= 1:
                if SequenceMatcher(None, word, wanted).ratio() >= OCR_MATCH_RATIO:
                    return True
    return False


def _menu_labels(entries):
    return [label for label in ("drive",) + MENU_LABELS if _has_word(entries, label)]


def menu_drive_target(texts):
    """Locate the visible Drive label, preserving its normalized capture rectangle."""
    candidates = []
    for original, entry in zip(texts, _ocr_entries(texts)):
        if (_has_word([entry], "drive") and entry["width"] > 0 and entry["height"] > 0
                and entry["x"] + entry["width"] <= 1
                and entry["y"] + entry["height"] <= 1):
            candidates.append(dict(entry, text=original["text"]))
    if not candidates:
        raise ValueError("main-menu Drive label has no usable OCR target rectangle")
    # Prefer the button label over a larger OCR line containing nearby words.
    return min(candidates, key=lambda entry: entry["width"] * entry["height"])


def _fraction(image, rectangle, predicate):
    left, top, right, bottom = rectangle
    box = (int(left * image.width), int(top * image.height),
           int(right * image.width), int(bottom * image.height))
    pixels = _pixels(image.crop(box))
    return sum(predicate(*pixel) for pixel in pixels) / len(pixels)


def _pixels(image):
    # Keep host tests compatible with older Pillow while using its current API
    # in the package job, which installs the pinned macOS smoke dependency.
    if hasattr(image, "get_flattened_data"):
        return image.get_flattened_data()
    return list(image.getdata())


def _race_structure(image):
    # A fresh DEFAULT-track cockpit has sky above terrain/road and a detailed
    # instrument panel below. Color diversity alone would also accept a dialog
    # or desktop, so require these spatial relationships in the game window.
    sky = _fraction(image, RACE_SKY_REGION,
                    lambda r, g, b: b > r + SKY_RED_SEPARATION
                    and b >= g - SKY_GREEN_TOLERANCE and b > SKY_MINIMUM_BLUE)
    grass = _fraction(image, RACE_TERRAIN_REGION,
                      lambda r, g, b: g > GRASS_MINIMUM_GREEN
                      and g > r * GRASS_RED_MULTIPLIER and g > b * GRASS_BLUE_MULTIPLIER)
    road = _fraction(image, RACE_ROAD_REGION,
                     lambda r, g, b: ROAD_MINIMUM_BRIGHTNESS <= (r + g + b) / 3
                     <= ROAD_MAXIMUM_BRIGHTNESS
                     and max(r, g, b) - min(r, g, b) <= ROAD_MAXIMUM_COLOR_SPREAD)
    dashboard_dark = _fraction(image, RACE_DASHBOARD_REGION,
                               lambda r, g, b: max(r, g, b) < DASHBOARD_DARK_THRESHOLD)
    dashboard_light = _fraction(image, RACE_DASHBOARD_REGION,
                                lambda r, g, b: min(r, g, b) > DASHBOARD_LIGHT_THRESHOLD
                                and max(r, g, b) - min(r, g, b) < DASHBOARD_MAXIMUM_LIGHT_SPREAD)
    instrument_groups = 0
    for index in range(INSTRUMENT_GROUP_COUNT):
        region = (index / INSTRUMENT_GROUP_COUNT, RACE_DASHBOARD_REGION[1],
                  (index + 1) / INSTRUMENT_GROUP_COUNT, RACE_DASHBOARD_REGION[3])
        dark = _fraction(image, region,
                         lambda r, g, b: max(r, g, b) < DASHBOARD_DARK_THRESHOLD)
        light = _fraction(image, region,
                          lambda r, g, b: min(r, g, b) > DASHBOARD_LIGHT_THRESHOLD
                          and max(r, g, b) - min(r, g, b) < DASHBOARD_MAXIMUM_LIGHT_SPREAD)
        instrument_groups += (dark >= MINIMUM_INSTRUMENT_DARK_FRACTION
                              and light >= MINIMUM_INSTRUMENT_LIGHT_FRACTION)
    return {"sky_fraction": sky, "grass_fraction": grass, "road_fraction": road,
            "dashboard_dark_fraction": dashboard_dark,
            "dashboard_light_fraction": dashboard_light,
            "instrument_groups": instrument_groups}


def _image_change(image, previous):
    _, old, _ = _load_png(previous)
    differences = [sum(abs(a - b) for a, b in zip(first, second)) / 3
                   for first, second in zip(_pixels(image), _pixels(old))]
    return {"changed_fraction": sum(value > PIXEL_CHANGE_THRESHOLD for value in differences)
            / len(differences), "mean_change": sum(differences) / len(differences)}


def validate_screenshot(path, stage, texts, previous=None):
    """Return decoded-image metrics and require evidence of the requested stage."""
    if stage not in ("launcher", "intro", "menu", "race"):
        raise ValueError(f"unknown screenshot stage: {stage}")
    _, image, metrics = _load_png(Path(path))
    entries = _ocr_entries(texts)
    recognized = " ".join(entry["text"] for entry in entries)
    if (any(_has_word(entries, word) for word in ERROR_WORDS)
            or any(phrase in recognized for phrase in ERROR_PHRASES)):
        raise ValueError(f"{stage} screenshot contains an error message")
    labels = _menu_labels(entries)
    is_menu = "drive" in labels and len(set(labels) & set(MENU_LABELS)) >= 2
    metrics.update(stage=stage, text=recognized, menu_labels=labels)
    if stage == "launcher":
        compact = recognized.replace(" ", "")
        if ("startgame" not in compact
                or not any(label in compact for label in ("startupoptions", "drivingrules",
                                                          "hypervision", "fullscreen"))):
            raise ValueError("launcher screenshot does not show the startup controls")
    elif stage == "intro":
        if is_menu:
            raise ValueError("intro screenshot already shows the main menu")
        # The native title bar says Chocolate Stunts throughout the whole game.
        # Only text inside the rendered client content can identify an intro.
        client_text = [entry for entry in entries
                       if entry["y"] + entry["height"] / 2 < MAXIMUM_INTRO_TEXT_MIDPOINT]
        metrics["intro_words"] = [word for word in INTRO_WORDS if _has_word(client_text, word)]
    elif stage == "menu":
        if not is_menu:
            raise ValueError("main-menu screenshot lacks the Drive and menu labels")
    else:
        if is_menu:
            raise ValueError("race screenshot still shows the main menu")
        if previous is None:
            raise ValueError("race screenshot requires the preceding main-menu image")
        metrics.update(_image_change(image, previous))
        if (metrics["changed_fraction"] < MINIMUM_CHANGED_FRACTION
                or metrics["mean_change"] < MINIMUM_MEAN_CHANGE):
            raise ValueError("race screenshot did not materially change from the main menu")
        structure = _race_structure(image)
        metrics.update(structure)
        lower_text = [entry for entry in entries if entry["y"] + entry["height"] / 2
                      < MAXIMUM_HUD_TEXT_MIDPOINT]
        hud_words = [word for word in RACE_HUD_WORDS if _has_word(lower_text, word)]
        metrics["race_hud_words"] = hud_words
        has_dashboard = (structure["dashboard_dark_fraction"] >= MINIMUM_DASHBOARD_DARK_FRACTION
                         and structure["dashboard_light_fraction"] >= MINIMUM_DASHBOARD_LIGHT_FRACTION
                         and structure["instrument_groups"] >= MINIMUM_INSTRUMENT_GROUPS)
        has_world = (structure["sky_fraction"] >= MINIMUM_SKY_FRACTION
                     and structure["grass_fraction"] >= MINIMUM_GRASS_FRACTION
                     and structure["road_fraction"] >= MINIMUM_ROAD_FRACTION)
        has_hud = (len(hud_words) >= MINIMUM_RACE_HUD_WORDS
                   and structure["road_fraction"] >= MINIMUM_ROAD_FRACTION)
        if not has_dashboard or not (has_world or has_hud):
            raise ValueError("race screenshot does not show a coherent road and cockpit")
    return metrics


def _wave_data_bytes(raw, frame_bytes):
    if (len(raw) < 12 or raw[:4] != b"RIFF" or raw[8:12] != b"WAVE"
            or struct.unpack_from("<I", raw, 4)[0] + 8 != len(raw)):
        raise ValueError("audio WAV is truncated or has an invalid RIFF length")
    offset = 12
    data_sizes = []
    while offset < len(raw):
        if offset + 8 > len(raw):
            raise ValueError("audio WAV has an incomplete chunk header")
        size = struct.unpack_from("<I", raw, offset + 4)[0]
        end = offset + 8 + size
        if end + (size & 1) > len(raw):
            raise ValueError("audio WAV has a truncated chunk")
        if raw[offset:offset + 4] == b"data":
            data_sizes.append(size)
        offset = end + (size & 1)
    if len(data_sizes) != 1 or data_sizes[0] % frame_bytes:
        raise ValueError("audio WAV must contain one complete PCM frame sequence")
    return data_sizes[0]


def _audio_statistics(samples, channels):
    count = len(samples)
    means = [sum(samples[channel::channels]) / (count // channels)
             for channel in range(channels)]
    mean_square = sum(sample * sample for sample in samples) / count
    centered_square = max(0, mean_square - sum(mean * mean for mean in means) / channels)
    active = sum(abs(sample - means[index % channels]) > MINIMUM_AUDIO_RMS * PCM_SCALE
                 for index, sample in enumerate(samples))
    return {"rms": math.sqrt(mean_square) / PCM_SCALE,
            "ac_rms": math.sqrt(centered_square) / PCM_SCALE,
            "peak": max(abs(sample) for sample in samples) / PCM_SCALE,
            "dc_offset": max(abs(mean) for mean in means) / PCM_SCALE,
            "active_fraction": active / count,
            "clipped_fraction": sum(abs(sample) >= CLIPPED_SAMPLE_AMPLITUDE
                                    for sample in samples) / count}


def validate_audio(path, minimum_seconds=5):
    """Require complete, sustained, non-silent PCM16 audio rather than a header or DC."""
    if not math.isfinite(minimum_seconds) or minimum_seconds <= 0:
        raise ValueError("minimum audio duration must be positive")
    path = Path(path)
    try:
        if path.stat().st_size > MAXIMUM_AUDIO_BYTES:
            raise ValueError("audio capture exceeds the smoke-test size limit")
        raw = path.read_bytes()
        with wave.open(io.BytesIO(raw), "rb") as recording:
            channels, sample_bytes, rate, frames, compression, _ = recording.getparams()
            if sample_bytes != PCM_SAMPLE_BYTES or compression != "NONE":
                raise ValueError("audio capture must contain uncompressed PCM16 samples")
            if channels not in (1, 2) or not MINIMUM_AUDIO_RATE <= rate <= MAXIMUM_AUDIO_RATE:
                raise ValueError("audio capture has an implausible channel count or sample rate")
            frame_bytes = channels * sample_bytes
            data_bytes = _wave_data_bytes(raw, frame_bytes)
            payload = recording.readframes(frames)
            if len(payload) != frames * frame_bytes or len(payload) != data_bytes:
                raise ValueError("audio capture does not contain every declared PCM frame")
    except (OSError, EOFError, wave.Error) as error:
        raise ValueError(f"audio capture cannot be decoded: {error}") from error
    duration = frames / rate
    if duration < minimum_seconds:
        raise ValueError("audio capture is shorter than the required intro interval")
    samples = array("h")
    samples.frombytes(payload)
    if sys.byteorder != "little":
        samples.byteswap()
    metrics = _audio_statistics(samples, channels)
    if (metrics["rms"] < MINIMUM_AUDIO_RMS or metrics["peak"] < MINIMUM_AUDIO_PEAK
            or metrics["ac_rms"] < max(MINIMUM_AUDIO_RMS,
                                       metrics["rms"] * MINIMUM_AUDIO_AC_RATIO)
            or metrics["clipped_fraction"] > MAXIMUM_AUDIO_CLIPPED_FRACTION):
        raise ValueError("audio capture is silent, constant, or dominated by clipped DC")
    block_samples = rate * channels
    blocks = [_audio_statistics(samples[start:start + block_samples], channels)
              for start in range(0, len(samples) - block_samples + 1, block_samples)]
    active_blocks = sum(block["ac_rms"] >= MINIMUM_AUDIO_RMS
                        and block["peak"] >= MINIMUM_AUDIO_PEAK
                        and block["active_fraction"] >= MINIMUM_ACTIVE_SAMPLE_FRACTION
                        for block in blocks)
    required_blocks = max(min(MINIMUM_ACTIVE_BLOCKS, len(blocks)),
                          math.ceil(len(blocks) * MINIMUM_ACTIVE_BLOCK_FRACTION))
    if active_blocks < required_blocks:
        raise ValueError("audio capture lacks sustained sound across multiple intro seconds")
    return dict(metrics, channels=channels, sample_rate=rate, sample_frames=frames,
                duration_seconds=duration, active_seconds=active_blocks,
                measured_seconds=len(blocks), block_metrics=blocks)
