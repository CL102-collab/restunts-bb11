#!/usr/bin/env python3
"""Build the original, compact Tesla Roadster Rocket VGA cockpit artwork.

``build_dashboards(repo)`` returns ``(stda_bytes, stdb_bytes, simd_patches)``.
The first two values are complete, uncompressed VSH resource containers. Apply
each ``{offset: bytes}`` patch relative to the beginning of the CAR file's
``simd`` resource, after selecting the physics donor. Physics, gear count, and
motor ratios remain the caller's responsibility. The returned patches configure
the digital speed display, motor tachometer, steering dot, and stationary EV selector.

Artwork is drawn directly in the game's indexed palette using the standard
library. The centre display and steering yoke occupy separate rectangles, so
steering never erases live instruments. Required instrument masks and selector
sprites are valid one-pixel identity operations. ``write_preview`` draws a
static cockpit illustration; the game draws the actual live speed and RPM.
The raised dash uses the game's optional ``dast``/``dasm`` AND/OR pair to leave
scenery visible above its contoured silhouette.
"""

import argparse
import math
from pathlib import Path
import runpy
import struct


WIDTH, HEIGHT = 320, 200
DASH_TOP = 132
DASH_CONTOUR_TOP = 108
INSTRUMENT_RECT = (194, 138, 116, 30)
WHEEL_RECT = (12, 132, 172, 68)
WHEEL_TURN_DEGREES = 10
STEERING_INDICATOR_CENTER = (96, 135)
STEERING_DOTS_OFFSET = 0x0EA
STEERING_POSITION_COUNT = 31
STEERING_DOT_WIDTH, STEERING_DOT_HEIGHT = 8, 2
STEERING_DOT_ANCHOR = (2, 1)
STEERING_DOT_ALIGNMENT = 8
STEERING_DOT_BUFFER_WIDTH = STEERING_DOT_WIDTH + STEERING_DOT_ALIGNMENT
STEERING_DOT_COLOR = 41
STEERING_DOT_ROWS = ("01110000", "11111000")
STEERING_TRACK_HALF_WIDTH = STEERING_POSITION_COUNT + 2
STEERING_TRACK_HALF_HEIGHT = 1
SHAPE_HEADER_SIZE = struct.calcsize("<HHhhHH4B")
DIGIT_WIDTH, DIGIT_HEIGHT = 8, 12
DIGIT_POSITIONS = ((8, 8), (18, 8), (28, 8))
MOTOR_CENTER = (85, 22)
TRANSPARENT = 255
BLACK, RED, WHITE = 0, 39, 19
CHARCOAL, EDGE, SHADE = 31, 27, 29

# A deliberately small block font keeps the artwork crisp at 320 x 200.
FONT = {
    "A": "010101111101101", "B": "110101110101110",
    "C": "011100100100011", "D": "110101101101110",
    "E": "111100110100111", "F": "111100110100100",
    "G": "011100101101011", "H": "101101111101101",
    "I": "111010010010111", "J": "001001001101010",
    "K": "101101110101101", "L": "100100100100111",
    "M": "101111111101101", "N": "101111111111101",
    "O": "010101101101010", "P": "110101110100100",
    "Q": "010101101111011", "R": "110101110101101",
    "S": "011100010001110", "T": "111010010010010",
    "U": "101101101101111", "V": "101101101101010",
    "W": "101101111111101", "X": "101101010101101",
    "Y": "101101010010010", "Z": "111001010100111",
    "0": "111101101101111", "1": "010110010010111",
    "2": "110001010100111", "3": "110001010001110",
    "4": "101101111001001", "5": "111100110001110",
    "6": "011100110101010", "7": "111001010010010",
    "8": "010101010101010", "9": "010101011001110",
    "-": "000000111000000", ".": "000000000000010",
    " ": "000000000000000",
}


class Canvas:
    """Small clipped indexed raster with deterministic integer drawing."""

    def __init__(self, width=WIDTH, height=HEIGHT, color=BLACK):
        self.width, self.height = width, height
        self.pixels = bytearray([color]) * (width * height)

    def point(self, x, y, color):
        x, y = int(round(x)), int(round(y))
        if 0 <= x < self.width and 0 <= y < self.height:
            self.pixels[y * self.width + x] = color

    def rect(self, x0, y0, x1, y1, color):
        for y in range(max(0, y0), min(self.height, y1 + 1)):
            left, right = max(0, x0), min(self.width, x1 + 1)
            if left < right:
                start = y * self.width + left
                self.pixels[start:start + right - left] = bytes([color]) * (right - left)

    def line(self, first, last, color, thickness=1):
        x0, y0 = first
        x1, y1 = last
        count = max(abs(round(x1 - x0)), abs(round(y1 - y0)), 1)
        for step in range(count + 1):
            x = round(x0 + (x1 - x0) * step / count)
            y = round(y0 + (y1 - y0) * step / count)
            self.rect(x, y, x + thickness - 1, y + thickness - 1, color)

    def polygon(self, points, color):
        for y in range(max(0, math.floor(min(p[1] for p in points))),
                       min(self.height, math.ceil(max(p[1] for p in points)) + 1)):
            crossings = []
            for first, last in zip(points, points[1:] + points[:1]):
                if min(first[1], last[1]) <= y + 0.5 < max(first[1], last[1]):
                    crossings.append(first[0] + (y + 0.5 - first[1]) *
                                     (last[0] - first[0]) / (last[1] - first[1]))
            crossings.sort()
            for left, right in zip(crossings[::2], crossings[1::2]):
                self.rect(math.ceil(left), y, math.floor(right), y, color)

    def text(self, x, y, text, color, spacing=1):
        for letter in text:
            glyph = FONT[letter]
            for index, pixel in enumerate(glyph):
                if pixel == "1":
                    self.point(x + index % 3, y + index // 3, color)
            x += 3 + spacing

    def crop(self, rect):
        x, y, width, height = rect
        return b"".join(self.pixels[(y + row) * self.width + x:
                                   (y + row) * self.width + x + width]
                        for row in range(height))


def _shape(width, height, pixels, x=0, y=0, *, anchor=(0, 0)):
    if len(pixels) != width * height:
        raise ValueError("Bitmap dimensions and pixel count disagree")
    return struct.pack("<HHhhHH4B", width, height, *anchor, x, y, 1, 2, 4, 8) + bytes(pixels)


def steering_dot_shapes():
    """Native masked dot and per-page saves, including DOS alignment padding."""
    pixels = bytes(STEERING_DOT_COLOR if pixel == "1" else BLACK
                   for row in STEERING_DOT_ROWS for pixel in row)
    mask = bytes(0 if pixel else TRANSPARENT for pixel in pixels)
    saved = _shape(STEERING_DOT_BUFFER_WIDTH, STEERING_DOT_HEIGHT,
                   bytes(STEERING_DOT_BUFFER_WIDTH * STEERING_DOT_HEIGHT))
    return {
        "dot ": _shape(STEERING_DOT_WIDTH, STEERING_DOT_HEIGHT, pixels,
                       anchor=STEERING_DOT_ANCHOR),
        "dota": _shape(STEERING_DOT_WIDTH, STEERING_DOT_HEIGHT, mask,
                       anchor=STEERING_DOT_ANCHOR),
        "dot1": saved,
        "dot2": saved,
    }


def steering_dot_positions(center, dashboard_top):
    """Firebird-style horizontal travel; the game mirrors X for left steering."""
    x, y = center
    if y - max(STEERING_DOT_ANCHOR[1], STEERING_TRACK_HALF_HEIGHT) < dashboard_top:
        raise ValueError("Steering indicator overlaps the windshield redraw area")
    return bytes(value for step in range(STEERING_POSITION_COUNT)
                 for value in (x + step, y))


def _steering_track(canvas, center):
    x, y = center
    canvas.rect(x - STEERING_TRACK_HALF_WIDTH, y - STEERING_TRACK_HALF_HEIGHT,
                x + STEERING_TRACK_HALF_WIDTH, y + STEERING_TRACK_HALF_HEIGHT - 1, BLACK)
    travel = STEERING_POSITION_COUNT - 1
    canvas.line((x - travel, y), (x + travel, y), SHADE)
    for tick in (x - travel, x, x + travel):
        canvas.line((tick, y - 1), (tick, y + 1), EDGE)


def _preview_steering_dot(canvas, center):
    x, y = center
    shape = steering_dot_shapes()["dot "]
    for row in range(STEERING_DOT_HEIGHT):
        for column in range(STEERING_DOT_WIDTH):
            color = shape[SHAPE_HEADER_SIZE + row * STEERING_DOT_WIDTH + column]
            if color:
                canvas.point(x + column - STEERING_DOT_ANCHOR[0],
                             y + row - STEERING_DOT_ANCHOR[1], color)


def _container(resources):
    offsets, payload = [], bytearray()
    for name, resource in resources.items():
        if len(name) != 4:
            raise ValueError("Resource IDs must contain exactly four characters")
        offsets.append(len(payload))
        payload.extend(resource)
    size = 6 + 8 * len(resources) + len(payload)
    return (struct.pack("<IH", size, len(resources)) +
            "".join(resources).encode("ascii") +
            b"".join(struct.pack("<I", offset) for offset in offsets) + payload)


def _helpers(repo):
    return runpy.run_path(str(repo / "tools/scripts/extract-skyboxes.py"))


def _palette(repo):
    unpack = runpy.run_path(str(repo / "tools/scripts/generate-owoot-road-geometry.py"))["unpack"]
    shapes = _helpers(repo)["resource_shapes"]
    palette = shapes(unpack((repo / "stunts/SDMAIN.PVS").read_bytes()))["!pal"][16:]
    return bytes((value & 63) * 255 // 63 for value in palette)


def _panel():
    # The padded dash rolls down into the steering-column recess. Pixels above
    # its outline become a separate AND/OR overlay, leaving the world visible.
    canvas = Canvas(color=TRANSPARENT)
    contour = [(0, 124), (14, 120), (34, 117), (56, 115), (79, 114),
               (111, 116), (131, 122), (145, 132), (159, 131),
               (175, 123), (193, 119), (218, 118), (250, 119),
               (280, 122), (302, 121), (319, 117)]
    canvas.polygon(contour + [(320, 200), (0, 200)], CHARCOAL)
    canvas.polygon(contour + [(319, 141), (281, 145), (224, 140),
                             (179, 141), (152, 148), (125, 139),
                             (61, 133), (26, 139), (0, 144)], SHADE)
    for first, last in zip(contour, contour[1:]):
        canvas.line(first, last, 25)
        canvas.line((first[0], first[1] + 2), (last[0], last[1] + 2), 30)
    # Restrained suede flecks, rather than a solid band across the windshield.
    for y in range(117, 142):
        for x in range(WIDTH):
            if canvas.pixels[y * WIDTH + x] == SHADE and (x * 5 + y * 3) % 29 == 0:
                canvas.point(x, y, 28)

    # Carbon-lined column pocket, deep vent slot, and layered lower fascia.
    canvas.polygon([(42, 134), (48, 126), (58, 122), (119, 123),
                    (132, 133), (141, 155), (43, 155)], BLACK)
    canvas.line((49, 127), (60, 124), 24)
    canvas.line((60, 124), (117, 125), EDGE)
    canvas.line((117, 125), (129, 135), 24)
    for x in range(54, 121, 4):
        canvas.line((x, 129), (x + 2, 131), 30)
    canvas.polygon([(63, 136), (122, 136), (135, 174), (51, 174)], 30)
    canvas.polygon([(72, 139), (111, 139), (123, 173), (61, 173)], BLACK)
    for y in (144, 147, 150):
        canvas.line((2, y), (39, y - 4), BLACK, 2)
        canvas.line((2, y + 2), (39, y - 2), 28)
        canvas.line((147, y + 1), (185, y - 3), BLACK, 2)
    canvas.line((0, 159), (37, 150), 25)
    canvas.line((146, 155), (186, 150), EDGE)
    canvas.polygon([(0, 164), (18, 169), (25, 192), (0, 186)], 30)
    canvas.line((0, 167), (16, 172), 43, 2)
    canvas.line((0, 170), (15, 175), RED)
    canvas.polygon([(0, 190), (24, 196), (47, 200), (0, 200)], BLACK)
    canvas.rect(147, 170, 185, 199, BLACK)
    for x in range(151, 185, 4):
        canvas.line((x, 178), (x - 7, 199), 30)

    # The photo's freestanding rectangular centre screen carries the live
    # instruments, leaving room for a much larger wheel on the driver's side.
    canvas.rect(187, 113, 315, 185, BLACK)
    canvas.rect(189, 109, 312, 182, EDGE)
    canvas.line((190, 109), (310, 109), 22)
    canvas.line((189, 110), (189, 179), 24)
    canvas.rect(192, 112, 309, 179, BLACK)
    canvas.text(200, 118, "TESLA", WHITE, 2)
    canvas.text(200, 126, "ROADSTER", RED)
    route = [(269, 121), (275, 117), (279, 118), (278, 123),
             (286, 125), (291, 120), (298, 122), (294, 130),
             (288, 131), (286, 128), (279, 128), (269, 121)]
    for first, last in zip(route, route[1:]):
        canvas.line(first, last, 43)
    canvas.line((199, 135), (301, 135), SHADE)
    canvas.text(202, 139, "SPEED", 23)
    canvas.text(235, 152, "MPH", RED)
    canvas.text(263, 139, "MOTOR", 23)
    cx = INSTRUMENT_RECT[0] + MOTOR_CENTER[0]
    cy = INSTRUMENT_RECT[1] + MOTOR_CENTER[1]
    for step in range(17):
        angle = math.pi - step * math.pi / 16
        start = (cx + 20 * math.cos(angle), cy - 10 * math.sin(angle))
        end = (cx + 18 * math.cos(angle), cy - 8 * math.sin(angle))
        canvas.line(start, end, RED if step > 12 else EDGE)
    canvas.text(273, 164, "EV", 23)
    canvas.text(200, 171, "FAN ON", 23)
    canvas.rect(263, 171, 297, 176, 24)
    canvas.rect(264, 172, 296, 175, BLACK)
    canvas.rect(298, 172, 300, 175, 24)
    for x in range(266, 294, 7):
        canvas.rect(x, 173, x + 4, 174, 43)

    # Thin metallic console rails around a carbon-fibre centre spine.
    canvas.polygon([(220, 184), (285, 184), (302, 200), (208, 200)], 30)
    for y in range(188, 200, 3):
        for x in range(222, 284, 6):
            canvas.line((x, y), (x + 2, y + 1), CHARCOAL)
    canvas.line((219, 184), (211, 199), 22)
    canvas.line((222, 185), (216, 199), BLACK)
    canvas.line((285, 184), (299, 199), 24)
    canvas.rect(234, 188, 269, 199, BLACK)
    canvas.line((236, 189), (267, 189), EDGE)
    canvas.line((236, 190), (236, 199), 25)
    _steering_track(canvas, STEERING_INDICATOR_CENTER)
    return canvas


def _wheel(canvas, direction):
    # Photo reference: upright, silver-edged side grips around a broad dark
    # hub, with open gaps and separate control spokes. Rotate every part in
    # the wheel plane, then project with a modest tilt into the cockpit.
    # Negative steering (whl1) is anticlockwise in screen coordinates.
    angle = math.radians(direction * WHEEL_TURN_DEGREES)
    cosine, sine = math.cos(angle), math.sin(angle)

    def points(vertices):
        return [(96 + 1.12 * (x * cosine - y * sine),
                 169 + 0.78 * (x * sine + y * cosine)) for x, y in vertices]

    def polygon(vertices, color):
        canvas.polygon(points(vertices), color)

    def line(vertices, color, thickness=1):
        canvas.line(*points(vertices), color, thickness)

    for side in (-1, 1):
        def mirrored(vertices):
            return [(side * x, y) for x, y in vertices]

        # Thin upper and lower support rods, with silver paddle faces behind
        # the horizontal switches. The holes remain open to the dashboard.
        for rod in ([(17, -22), (39, -27), (40, -23), (18, -18)],
                    [(17, 18), (40, 24), (39, 28), (16, 22)]):
            polygon(mirrored(rod), EDGE)
            line(mirrored([rod[0], rod[1]]), 22)
        for paddle in ([(36, -18), (44, -18), (42, -11), (34, -11)],
                       [(34, 11), (40, 11), (43, 19), (36, 19)]):
            polygon(mirrored(paddle), 22)
            line(mirrored(paddle[:2]), WHITE)
            line(mirrored([(37, -16), (40, -16)]), BLACK)

        polygon(mirrored([(18, -10), (44, -8), (45, 8), (18, 7)]), BLACK)
        polygon(mirrored([(20, -7), (42, -6), (42, 5), (20, 4)]), CHARCOAL)
        line(mirrored([(19, -9), (44, -7)]), 22)
        line(mirrored([(20, 6), (44, 7)]), EDGE)
        polygon(mirrored([(29, -5), (33, -5), (33, 3), (29, 3)]), BLACK)
        line(mirrored([(30, -3), (30, 1)]), EDGE)
        for x, y in ((25, -4), (25, 1), (37, -3), (38, 2)):
            line(mirrored([(x, y), (x + 1, y)]), 22)
        # Recessed scroll roller and separate tactile switches on each spoke.
        polygon(mirrored([(29, -4), (32, -4), (32, 3), (29, 3)]), EDGE)
        for y in (-3, 0, 2):
            line(mirrored([(29, y), (32, y)]), BLACK)
        line(mirrored([(24, -5), (26, -5)]), WHITE)
        line(mirrored([(37, 0), (39, 0)]), 23)

        # One concave grip outlines each open side; there is no lower bridge.
        rim = [(29, -27), (42, -30), (51, -28), (57, -23),
               (60, -14), (60, 15), (57, 25), (53, 29),
               (43, 29), (34, 27), (20, 19), (22, 14),
               (38, 20), (46, 20), (48, 16), (48, -12),
               (44, -18), (40, -19), (29, -17)]
        polygon(mirrored(rim), 22)
        grip = [(30, -24), (42, -27), (50, -25), (54, -21),
                (57, -13), (57, 15), (54, 23), (51, 26),
                (43, 26), (35, 24), (24, 18), (25, 17),
                (38, 22), (48, 22), (51, 17), (51, -13),
                (46, -21), (40, -22), (30, -20)]
        polygon(mirrored(grip), CHARCOAL)
        for first, last in zip(rim[:7], rim[1:8]):
            line(mirrored([first, last]), WHITE)
        line(mirrored([(56, -12), (56, 13)]), SHADE)
        line(mirrored([(52, -15), (52, 15)]), BLACK)
        # Padded thumb rests, brushed upper caps, and leather seam stitches.
        polygon(mirrored([(32, -25), (41, -27), (47, -24),
                          (43, -19), (31, -18)]), 24)
        line(mirrored([(33, -25), (41, -26)]), 18)
        line(mirrored([(44, 24), (51, 24)]), 23)
        for y in range(-10, 15, 4):
            line(mirrored([(58, y), (58, y + 1)]), 26)
        line(mirrored([(38, 21), (46, 22)]), BLACK)

    # The large softly faceted pad is the defining centre of the new wheel.
    hub = [(-9, -25), (9, -25), (17, -18), (22, -6), (21, 12),
           (16, 25), (-16, 25), (-21, 12), (-22, -6), (-17, -18)]
    polygon(hub, BLACK)
    polygon([(x * 0.94, y * 0.94) for x, y in hub], SHADE)
    polygon([(x * 0.84, y * 0.85) for x, y in hub], CHARCOAL)
    polygon([(-8, -22), (8, -22), (15, -17), (18, -8),
             (1, -13), (-17, -9), (-14, -17)], 30)
    line([(-9, -21), (9, -21)], 30)
    line([(-15, 18), (15, 18)], 30)
    line([(-18, 0), (-17, 11)], 29)
    line([(18, 0), (17, 11)], BLACK)
    # Subtle shield badge, instead of the old bright stationary T.
    polygon([(-4, -5), (4, -5), (3, 3), (0, 7), (-3, 3)], SHADE)
    polygon([(-2, -3), (2, -3), (1, 3), (0, 4), (-1, 3)], BLACK)
    line([(0, -2), (0, 2)], 30)


def _digit(number):
    canvas = Canvas(DIGIT_WIDTH, DIGIT_HEIGHT)
    # Conventional a..g layout: three horizontal bars and four side bars.
    # https://en.wikipedia.org/wiki/Seven-segment_display#Characters
    # All horizontal strokes are two pixels thick, including the single g
    # bar. The upper/lower openings are symmetric; the seven parts do not
    # overlap. Twelve rows keep the centre display readable at native resolution.
    segments = ("abcdef", "bc", "abdeg", "abcdg", "bcfg", "acdfg",
                "acdefg", "abc", "abcdefg", "abcdfg")
    rectangles = {"a": (1, 0, 6, 1), "b": (6, 2, 7, 4),
                  "c": (6, 7, 7, 9), "d": (1, 10, 6, 11),
                  "e": (0, 7, 1, 9), "f": (0, 2, 1, 4),
                  "g": (1, 5, 6, 6)}
    for segment in segments[number]:
        x0, y0, x1, y1 = rectangles[segment]
        # Exposed vertical ends reach the same cap height and baseline as
        # digits with horizontal top/bottom bars (notably 1, 4 and 7).
        if segment in "bf" and "a" not in segments[number]:
            y0 = 0
        if segment in "ce" and "d" not in segments[number]:
            y1 = DIGIT_HEIGHT - 1
        canvas.rect(x0, y0, x1, y1, RED)
    return bytes(canvas.pixels)


def prepared_stda_size(data):
    """Exact retained RLE byte count produced by the game's parse_shape2d.

    The original starts each literal block with one byte and converts repeats
    longer than three bytes, splitting both kinds of run at 127. Containers
    and each encoded bitmap retain their headers; the arena rounds to 16 bytes.
    """
    resources = _resource_dict(data)
    size = 6 + 8 * len(resources)
    for shape in resources.values():
        width, height = struct.unpack_from("<HH", shape)
        pixels = shape[16:16 + width * height]
        remaining, scan, literals, encoded = len(pixels), 1, 1, 0
        while remaining:
            repeats = 0
            if scan < len(pixels):
                while scan + repeats < len(pixels) and pixels[scan + repeats] == pixels[scan]:
                    repeats += 1
            if repeats <= 3 and literals < remaining:
                scan += 1
                literals += 1
                continue
            encoded += literals + (literals + 126) // 127
            remaining -= literals
            repeats = min(repeats, remaining)
            while repeats > 127:
                encoded += 2
                repeats -= 127
                remaining -= 127
                scan += 127
            if repeats > 3:
                encoded += 2
                remaining -= repeats
                scan += repeats
            scan += 1
            literals = 1
        size += 16 + encoded + 1
    return (size + 15) // 16 * 16


def _resource_dict(data):
    count = struct.unpack_from("<H", data, 4)[0]
    base = 6 + 8 * count
    starts = [base + struct.unpack_from("<I", data, 6 + 4 * count + 4 * i)[0]
              for i in range(count)]
    return {data[6 + 4 * i:10 + 4 * i].decode("ascii"):
            data[start:starts[i + 1] if i + 1 < count else len(data)]
            for i, start in enumerate(starts)}


def build_dashboards(repo: Path) -> tuple[bytes, bytes, dict[int, bytes]]:
    """Return the two raw VSH files and dashboard-only simd byte patches."""
    repo = Path(repo)
    donor_a = (repo / "tools/scripts/cars/STDADIA3.VSH").read_bytes()
    donor_b = (repo / "tools/scripts/cars/STDBDIA3.VSH").read_bytes()
    donor_maps = _resource_dict(donor_b)
    maps = {name: donor_maps[name] for name in ("!cg0", "!eg0")}
    canvas = _panel()
    a = dict(maps)
    a["dash"] = _shape(WIDTH, HEIGHT - DASH_TOP,
                       canvas.crop((0, DASH_TOP, WIDTH, HEIGHT - DASH_TOP)), y=DASH_TOP)
    top_rect = (0, DASH_CONTOUR_TOP, WIDTH, DASH_TOP - DASH_CONTOUR_TOP)
    top_pixels = canvas.crop(top_rect)
    a["dast"] = _shape(WIDTH, top_rect[3],
                       bytes(0 if pixel == TRANSPARENT else pixel for pixel in top_pixels),
                       y=DASH_CONTOUR_TOP)
    a["dasm"] = _shape(WIDTH, top_rect[3],
                       bytes(255 if pixel == TRANSPARENT else 0 for pixel in top_pixels),
                       y=DASH_CONTOUR_TOP)
    for index, direction in enumerate((-1, 0, 1), 1):
        steering = _panel()
        _wheel(steering, direction)
        x, y, width, height = WHEEL_RECT
        if any(pixel != canvas.pixels[offset] and not
               (x <= offset % WIDTH < x + width and
                y <= offset // WIDTH < y + height)
               for offset, pixel in enumerate(steering.pixels)):
            raise ValueError("Steering yoke escapes its redraw rectangle")
        a[f"whl{index}"] = _shape(width, height, steering.crop(WHEEL_RECT), x, y)
    x, y, width, height = INSTRUMENT_RECT
    a["ins2"] = _shape(width, height, canvas.crop(INSTRUMENT_RECT), x, y)
    for name, color in (("ins1", 0), ("ins3", 0), ("inm1", 255), ("inm3", 255)):
        a[name] = _shape(1, 1, bytes([color]))
    a["gbox"] = _shape(1, 1, bytes([CHARCOAL]), 318, 198)

    b = dict(maps)
    b.update(steering_dot_shapes())
    for name, color in (("gnob", 0), ("gnab", 255)):
        b[name] = _shape(1, 1, bytes([color]))
    for digit in range(10):
        b[f"dig{digit}"] = _shape(DIGIT_WIDTH, DIGIT_HEIGHT, _digit(digit))
    stda, stdb = _container(a), _container(b)

    if any(x < 0 or y < 0 or x + DIGIT_WIDTH > INSTRUMENT_RECT[2] or
           y + DIGIT_HEIGHT > INSTRUMENT_RECT[3] for x, y in DIGIT_POSITIONS):
        raise ValueError("Speed digit escapes the live instrument rectangle")
    if INSTRUMENT_RECT[1] < DASH_TOP or WHEEL_RECT[1] < DASH_TOP:
        raise ValueError("Dynamic dashboard artwork overlaps the world viewport")
    ix, iy, iw, ih = INSTRUMENT_RECT
    wx, wy, ww, wh = WHEEL_RECT
    if ix < wx + ww and wx < ix + iw and iy < wy + wh and wy < iy + ih:
        raise ValueError("Steering redraw overlaps the live instruments")
    speed_points = bytes(value for pair in DIGIT_POSITIONS for value in pair) + bytes(202)
    rev_points = bytearray()
    for step in range(128):
        angle = math.pi - step * math.pi / 127
        rev_points.extend((round(MOTOR_CENTER[0] + 18 * math.cos(angle)),
                           round(MOTOR_CENTER[1] - 8 * math.sin(angle))))
    patches = {
        0x01E: bytes(26),  # Selector centre line plus all six fixed knob positions.
        STEERING_DOTS_OFFSET: steering_dot_positions(STEERING_INDICATOR_CENTER, DASH_TOP),
        0x128: struct.pack("<hhH", 0, 0, 1),  # Centre Y=0 selects digital speed.
        0x12E: speed_points,
        0x1FE: struct.pack("<hhH", *MOTOR_CENTER, 128),
        0x204: bytes(rev_points),
        0x0AE: struct.pack("<H", RED | (RED << 8)),
    }
    if len(stda) > len(donor_a) or len(stdb) > len(donor_b):
        raise ValueError("Rocket dashboard exceeds the extracted DIA3 resource budget")
    if prepared_stda_size(stda) > prepared_stda_size(donor_a):
        raise ValueError("Rocket dashboard exceeds the game-prepared DIA3 memory budget")
    if width > 188 or height > 75:
        raise ValueError("Rocket dashboard scratch sprite exceeds DIA3's dimensions")
    return stda, stdb, patches


def write_preview(repo: Path, path: Path, speed=230):
    """Write an indexed PNG illustration with the neutral yoke and sample speed."""
    canvas = Canvas()
    canvas.rect(0, 0, 319, 89, 116)
    canvas.rect(0, 90, 319, DASH_TOP - 1, 84)
    canvas.polygon([(151, 90), (168, 90), (230, DASH_TOP - 1), (86, DASH_TOP - 1)], 27)
    canvas.line((159, 93), (159, 101), WHITE)
    canvas.line((159, 108), (159, DASH_TOP - 1), WHITE)
    for offset, pixel in enumerate(_panel().pixels):
        if pixel != TRANSPARENT:
            canvas.pixels[offset] = pixel
    _wheel(canvas, 0)
    _preview_steering_dot(canvas, STEERING_INDICATOR_CENTER)
    for digit, (x, y) in zip(f"{speed:03d}", DIGIT_POSITIONS):
        pixels = _digit(int(digit))
        for row in range(DIGIT_HEIGHT):
            for column in range(DIGIT_WIDTH):
                color = pixels[row * DIGIT_WIDTH + column]
                if color:
                    canvas.point(INSTRUMENT_RECT[0] + x + column,
                                 INSTRUMENT_RECT[1] + y + row, color)
    cx, cy = (INSTRUMENT_RECT[index] + MOTOR_CENTER[index] for index in (0, 1))
    canvas.line((cx, cy), (cx + 13, cy - 6), RED)
    path = Path(path)
    path.write_bytes(_helpers(Path(repo))["indexed_png"](
        WIDTH, HEIGHT, bytes(canvas.pixels), _palette(Path(repo))))
    return path


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--preview", type=Path, default=Path("/tmp/tesla_roadster_dashboard.png"))
    args = parser.parse_args()
    primary, secondary, _ = build_dashboards(args.repository)
    print(f"STDA {len(primary)} bytes; prepared {prepared_stda_size(primary)} bytes")
    print(f"STDB {len(secondary)} bytes; total extracted {len(primary) + len(secondary)} bytes")
    print(write_preview(args.repository, args.preview))
