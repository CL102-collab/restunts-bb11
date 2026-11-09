#!/usr/bin/env python3
"""Original indexed-palette cockpit for the MCMU McMurtry Speirling car.

The supplied left, middle and right cockpit views guide the dark carbon
fascia, open steering grips and five rotary controls. Upright dark-grey metal
control plates sit ahead of the recessed steering column, with one rounded
open air scoop per side and a curved row of ten unlit shift-lamp lenses. A centered white digital
speedometer sits between a live white RPM bar and a fixed 90% battery bar.
The complete LCD turns with the wheel. Only the shallow symmetric roof edge
frames the top of the clear forward view; the exterior fender crowns have no
visible wheel-top openings from this driving position. Photograph pixels and
record-holder inscriptions are not reproduced.

``build_dashboards(repo)`` returns two raw VSH containers and SIMD byte patches.
The patches configure instruments only; the caller supplies vehicle physics.
Generic indexed-raster/resource helpers are shared with the Tesla authoring
tool, while every dashboard and steering sprite is drawn here.
"""

import argparse
import math
from pathlib import Path
import struct

from mcmurtry_physics import GEAR_RATIO, IDLE_RPM, MAX_RPM
from tesla_roadster_dashboard import (
    Canvas, _container, _digit, _helpers, _palette, _resource_dict, _shape,
    _preview_steering_dot, _steering_track, prepared_stda_size,
    steering_dot_positions, steering_dot_shapes, SHAPE_HEADER_SIZE, STEERING_DOTS_OFFSET,
)


WIDTH, HEIGHT = 320, 200
DASH_TOP = 125
DASH_OPAQUE_TOP = DASH_TOP
ROOF_HEIGHT = 10
ROOF_CENTER_DROP = 11
ROOF_RECT = (0, 0, WIDTH, ROOF_HEIGHT)
TOP_OVERLAY_RECT = (0, ROOF_HEIGHT, WIDTH, DASH_TOP - ROOF_HEIGHT)
CANOPY_HALF_WIDTH = (WIDTH - 1) / 2
CANOPY_WINDSCREEN_EDGE = tuple(
    (x, ROOF_HEIGHT + round(ROOF_CENTER_DROP *
                            (1 - ((x - CANOPY_HALF_WIDTH) / CANOPY_HALF_WIDTH) ** 2)))
    for x in range(WIDTH))
CANOPY_ROOF_OUTLINE = ((0, 0), (WIDTH - 1, 0)) + tuple(reversed(CANOPY_WINDSCREEN_EDGE))
CANOPY_TEXTURE_SCALE = 3
INSTRUMENT_RECT = (111, 135, 99, 38)
WHEEL_RECT = (72, 126, 176, 74)
WHEEL_TURN_DEGREES = 10
STEERING_INDICATOR_CENTER = (160, 126)
WHEEL_CENTER = (160, 163)
WHEEL_VERTICAL_SCALE = .72
# One broad web joins each full palm grip to the central wheel body.
GRIP_CONNECTOR = ((44, -12), (61, -15), (69, -8), (70, 7),
                  (63, 15), (45, 12))
GRIP_CONNECTOR_FACE = ((45, -9), (60, -12), (65, -7), (66, 7),
                       (61, 12), (45, 10))
GRIP_CONNECTOR_TOP = ((45, -10), (60, -13), (65, -8), (45, -5))
GRIP_OUTLINE = ((59, -34), (67, -38), (76, -37), (81, -31),
                (84, -20), (84, -3), (82, 16), (78, 30),
                (71, 38), (61, 37), (54, 31), (55, 23),
                (59, 14), (60, 3), (57, -7), (52, -17),
                (52, -26), (55, -32))
GRIP_PADDING = ((60, -32), (68, -35), (75, -33), (78, -28),
                (80, -17), (80, -1), (78, 15), (74, 28),
                (69, 34), (63, 33), (59, 29), (61, 22),
                (64, 13), (65, 2), (62, -9), (57, -18), (57, -25))
GRIP_PALM_FACE = ((70, -28), (76, -26), (78, -16), (78, 0),
                  (75, 18), (71, 30), (67, 33), (63, 30),
                  (67, 17), (69, 1), (66, -14), (64, -25))
GRIP_CAP = ((56, -32), (65, -38), (75, -37), (81, -31),
            (77, -27), (63, -27))
GRIP_OUTER_EDGE = GRIP_OUTLINE[2:10]
GRIP_CAP_HIGHLIGHT = ((59, -33), (66, -36), (74, -35), (78, -32))
GRIP_INNER_EDGE = ((60, 29), (62, 23), (65, 13), (66, 2), (63, -8))
CONTROL_CENTERS = ((-36, 28), (-19, 35), (0, 31), (23, 32), (37, 28))
DIGIT_WIDTH, DIGIT_HEIGHT = 8, 12
DIGIT_SPACING = 10
SPEED_CENTER = (49, 16)
DIGIT_POSITIONS = tuple((SPEED_CENTER[0] - (DIGIT_WIDTH + 2 * DIGIT_SPACING) // 2 +
                         index * DIGIT_SPACING, SPEED_CENTER[1]) for index in range(3))
RPM_BAR_RECT = (7, 15, 5, 18)
BATTERY_BAR_RECT = (86, 15, 5, 18)
BATTERY_CHARGE_PERCENT = 90
PERCENT_SCALE = 100
RPM_INDEX_SHIFT = 7
RPM_POINT_COUNT = (MAX_RPM >> RPM_INDEX_SHIFT) + 1
RPM_POINT_CAPACITY = 128
SPEED_FIXED_POINT_SHIFT = 8
GEAR_RATIO_SHIFT = 16
BRAND_TEXT = "MCMURTRY"
BRAND_ORIGIN = (-15, 17)
BRAND_SUPERSCRIPT_INDEX = 1
BRAND_SUPERSCRIPT_C = ("011", "100", "011")
FONT_GLYPH_HEIGHT, FONT_GLYPH_ADVANCE = 5, 4
BLACK, CHARCOAL, CARBON, EDGE = 0, 31, 29, 27
CANOPY_EDGE_SHADOW = CHARCOAL
WHITE, SILVER, RED = 15, 22, 39
TRANSPARENT = 255
FENDER_HIGHLIGHT, FENDER_SIDE = 121, 125
MOTOR_CENTER = (RPM_BAR_RECT[0] + RPM_BAR_RECT[2] // 2,
                RPM_BAR_RECT[1] + RPM_BAR_RECT[3] - 1)
INSTRUMENT_TRANSFORM_SHIFT = 14
INSTRUMENT_TRANSFORM_SCALE = 1 << INSTRUMENT_TRANSFORM_SHIFT
INSTRUMENT_TRANSFORM_HALF = INSTRUMENT_TRANSFORM_SCALE // 2
INSTRUMENT_TRANSFORM_WORD_MASK = 0xFFFF
INSTRUMENT_TRANSFORM_IDS = (("imt1", -1), ("imt3", 1))
INSTRUMENT_PIVOT = (WHEEL_CENTER[0] - INSTRUMENT_RECT[0],
                    WHEEL_CENTER[1] - INSTRUMENT_RECT[1])
COWL_CROWN_TOP, COWL_CROWN_RISE, COWL_BOTTOM = 118, 7, 132
FASCIA_UPPER_EDGE = ((8, 135), (28, 129), (72, 124), (120, 121), (160, 120),
                     (200, 121), (248, 124), (292, 130), (312, 138))
FASCIA_SEAM = 22
FASCIA_CLOUD_STRENGTH, FASCIA_GRAIN_STRENGTH = .12, .08
# The photographed outlets are deep open scoops, not a bank of grille slots.
# Their asymmetric rounded mouths have a thin upper edge and rolled near lip.
DUCT_SHELL = ((30, 139), (32, 132), (40, 126), (55, 122), (82, 121),
              (100, 123), (105, 127), (103, 134), (96, 141), (83, 147),
              (62, 151), (44, 151), (34, 147))
DUCT_MOUTH = ((36, 139), (38, 133), (45, 129), (58, 126), (84, 125),
              (98, 126), (100, 130), (96, 136), (87, 142), (72, 146),
              (52, 148), (41, 145))
DUCT_THROAT = ((42, 138), (46, 133), (59, 129), (84, 127), (96, 128),
               (96, 132), (88, 137), (74, 141), (55, 144), (45, 142))
DUCT_FLOOR = ((41, 143), (55, 144), (74, 141), (88, 137), (96, 132),
              (96, 136), (87, 142), (72, 146), (52, 148), (41, 145))
DUCT_UPPER_LIP = ((36, 138), (39, 133), (46, 129), (58, 126), (84, 125), (98, 126))
DUCT_LOWER_LIP = ((34, 146), (44, 150), (62, 150), (83, 146), (97, 137))
DUCT_INNER_LIP = ((41, 145), (52, 148), (72, 146), (87, 142), (96, 136))
DUCT_RIBS = (((50, 133), (51, 140)), ((59, 131), (60, 139)),
             ((68, 130), (69.6, 138)), ((80, 129), (81, 135)))
DUCT_RIB_SHADE = 30
# The front face follows both outlet lips and meets the same corner anchors
# as the metal side plates. Its depth extends into the well, not the road.
FASCIA_FRONT_LEFT = DUCT_LOWER_LIP + ((112, 135), (136, 133), ((WIDTH - 1) / 2, 132))
FASCIA_FRONT_EDGE = FASCIA_FRONT_LEFT + tuple(
    (WIDTH - 1 - x, y) for x, y in reversed(FASCIA_FRONT_LEFT[:-1]))
FASCIA_OUTLINE = FASCIA_UPPER_EDGE + tuple(reversed(FASCIA_FRONT_EDGE))
FASCIA_FACE_DEPTH = 4
FASCIA_SHADOW_DROP = 2
FASCIA_FACE_LIGHT = -.12
FASCIA_TOP_LIGHT = .05
# Nearly upright metal control plates stay ahead of the steering-column well.
# A broad flat face and narrow bevels replace the swept carbon consoles.
SIDE_PANEL_CURVE = (DUCT_LOWER_LIP[0], (31, 161), (32, 184), (39, 204))
SIDE_PANEL_NECK_HALF_WIDTH = 4
SIDE_PANEL_NECK_LENGTH = .16
SIDE_PANEL_STEPS = 20
SIDE_PANEL_HALF_WIDTH = 11
SIDE_PANEL_FLARE = 1
SIDE_PANEL_BANDS = (-1, -.86, -.7, .7, .86, 1)
SIDE_PANEL_LIGHT = (-.45, -.18, .02, .18, -.2)
SIDE_PANEL_SHADOW_OFFSET = (4, 2)
SIDE_CONTROL_STATIONS = (.13, .32, .51, .76)
SIDE_SCREW_PAIR_SPACING = 8
SIDE_TUB_OUTLINE = ((0, 132), (24, 134), (16, 150), (15, 173),
                    (22, 199), (0, 199))
COLUMN_RECESS = ((76, 154), (87, 142), (111, 132), (139, 128),
                 (160, 129), (181, 128), (209, 132), (233, 142),
                 (244, 154), (275, 199), (44, 199))
COLUMN_RIM = ((80, 150), (94, 139), (116, 131), (141, 128),
              (160, 129), (179, 128), (204, 131), (226, 139), (240, 150))
SHIFT_LED_COUNT = 10
SHIFT_LED_SPACING = 8
SHIFT_LED_CENTER_X = 160
SHIFT_LED_TOP = 137
SHIFT_LED_ARCH_DROP = 2
SHIFT_LED_RADIUS = 2
SHIFT_LED_HIGHLIGHT = 20
SHIFT_LED_LENS = (" 000 ", "01210", "02320", "01210", " 000 ")
DISPLAY_ARCH_HALF_WIDTH = 49
DISPLAY_ARCH_DROP = 4
CONTROL_RED_SHADOW, CONTROL_RED_FACE, CONTROL_RED_LIGHT = 37, 39, 41
SIDE_BUTTON_RADIUS = 5
POTENTIOMETER_RADIUS = 5
POTENTIOMETER_BITMAP_HALF_SIZE = POTENTIOMETER_RADIUS + 2
SCREW_RADIUS = 2
TEXTURE_SEED = 0x4D434D55
TEXTURE_HASH_X = 0x1F123BB5
TEXTURE_HASH_Y = 0x5F356495
TEXTURE_HASH_MIX = 0x45D9F3B
TEXTURE_HASH_MASK = 0xFFFFFFFF
TEXTURE_HASH_SHIFT = 16
TEXTURE_BYTE_SHIFT = 24
TEXTURE_BYTE_MAX = 255
TEXTURE_CLOUD_SIZE = 7
TEXTURE_BASE_LIGHT = .28
TEXTURE_VERTICAL_LIGHT = .35
TEXTURE_SIDE_LIGHT = .16
TEXTURE_CLOUD_STRENGTH = .28
TEXTURE_GRAIN_STRENGTH = .30
CARBON_WEAVE_WIDTH = 2
CARBON_WEAVE_ROW = 3
CARBON_WEAVE_PERIOD = 4
CARBON_WEAVE_DUTY = 2
CARBON_WEAVE_STRENGTH = .25
CARBON_WEAVE_DIRECTIONS = {"carbon": 1, "wheel_carbon": 1, "fascia_carbon": -1,
                           "duct": 1}
CONSOLE_CLOUD_STRENGTH, CONSOLE_GRAIN_STRENGTH = .02, .01
METAL_BRUSH_STRENGTH = .18
WHEEL_EDGE_HIGHLIGHT = 25
MATERIAL_TONES = {
    "carbon": (31, 30, 29, 28),
    "console_metal": (31, 30, 29, 28, 27, 26),
    "duct": (31, 30, 29, 28, 27, 26),
    # Foreground wheel surfaces stay lighter than the fixed cockpit texture.
    "wheel_carbon": (30, 28, 27, 26),
    "suede": (31, 30, 29, 28, 27),
    "fascia_carbon": (31, 30, 29, 28, 27, 26),
    "recess": (31, 30),
    "grip": (31, 30, 28, 27),
    "grip_face": (30, 29, 28, 27, 26),
    "paint": (127, 126, 125, 124, 123, 122),
    "fender": (126, 125, 124, 123, 122, 121, 120),
    "metal": (28, 26, 24, 22, 20, 18),
    "wheel_trim": (28, 27, 26, 25, 24),
}
DASHBOARD_REFERENCES = [
    {"file": "middle.png", "view": "Primary driving-view reference, corrected to a centered symmetric projection: shallow bowed roof edge without side pillars, solid visible fender crowns, dark carbon fascia and open steering wheel. The white speedometer is centered between the left RPM bar and the right battery bar, fixed at 90% in the game."},
    {"file": "left.png", "view": "Left cockpit and open grip detail, white vertical RPM bar, McMurtry branding above five fluted rotary controls. Record-holder and adjacent inscriptions are omitted."},
    {"file": "right.png", "view": "Right cockpit and open grip detail, white vertical battery bar and curved fender crown. The wheel-top opening is hidden from the driver's viewpoint."},
    {"url": "https://www.topgear.com/car-reviews/speirling/first-drive",
     "view": "September 2024 first-hand prototype review; single seat, single gear, fan and motor-power controls."},
    {"url": "https://www.topgear.com/sites/default/files/2024/09/DSC00356_3.jpg",
     "view": "Supplementary detached wheel detail for unlit shift-lamp lenses and textured rotary controls. Lettering follows the user's requested omissions."},
]


def _noise(x, y):
    """Stable material grain, independent of redraw order and steering frames."""
    value = (math.floor(x) * TEXTURE_HASH_X + math.floor(y) * TEXTURE_HASH_Y +
             TEXTURE_SEED) & TEXTURE_HASH_MASK
    value = ((value ^ (value >> TEXTURE_HASH_SHIFT)) * TEXTURE_HASH_MIX) & TEXTURE_HASH_MASK
    return (value >> TEXTURE_BYTE_SHIFT) / TEXTURE_BYTE_MAX - .5


def _cloud(x, y):
    x, y = x / TEXTURE_CLOUD_SIZE, y / TEXTURE_CLOUD_SIZE
    left, top = math.floor(x), math.floor(y)
    u, v = x - left, y - top
    return ((_noise(left, top) * (1 - u) + _noise(left + 1, top) * u) * (1 - v) +
            (_noise(left, top + 1) * (1 - u) + _noise(left + 1, top + 1) * u) * v)


def _surface(canvas, points, material, coordinates=None, light=0):
    """Palette-quantized lighting, mottling and grain within one material mask."""
    mask = Canvas()
    mask.polygon(points, 1)
    local = [coordinates(x, y) if coordinates else (x, y) for x, y in points]
    left, top = min(p[0] for p in local), min(p[1] for p in local)
    width = max(max(p[0] for p in local) - left, 1)
    height = max(max(p[1] for p in local) - top, 1)
    tones = MATERIAL_TONES[material]
    for y in range(max(0, math.floor(min(p[1] for p in points))),
                   min(HEIGHT, math.ceil(max(p[1] for p in points)) + 1)):
        for x in range(max(0, math.floor(min(p[0] for p in points))),
                       min(WIDTH, math.ceil(max(p[0] for p in points)) + 1)):
            offset = y * WIDTH + x
            if not mask.pixels[offset]:
                continue
            sx, sy = coordinates(x, y) if coordinates else (x, y)
            u, v = (sx - left) / width, (sy - top) / height
            cloud_strength, grain_strength = TEXTURE_CLOUD_STRENGTH, TEXTURE_GRAIN_STRENGTH
            if material == "fascia_carbon":
                cloud_strength, grain_strength = FASCIA_CLOUD_STRENGTH, FASCIA_GRAIN_STRENGTH
            elif material == "console_metal":
                cloud_strength, grain_strength = CONSOLE_CLOUD_STRENGTH, CONSOLE_GRAIN_STRENGTH
            level = (light + TEXTURE_BASE_LIGHT + TEXTURE_VERTICAL_LIGHT * (1 - v) +
                     TEXTURE_SIDE_LIGHT * (1 - abs(2 * u - 1)) +
                     cloud_strength * _cloud(sx, sy) +
                     grain_strength * _noise(sx, sy))
            if material in CARBON_WEAVE_DIRECTIONS:
                weave_x = sx * CARBON_WEAVE_DIRECTIONS[material]
                weave = ((math.floor(weave_x + sy) // CARBON_WEAVE_WIDTH +
                          math.floor(sy) // CARBON_WEAVE_ROW) % CARBON_WEAVE_PERIOD <
                         CARBON_WEAVE_DUTY)
                level += CARBON_WEAVE_STRENGTH * (weave - .5)
            elif material == "metal":
                level += METAL_BRUSH_STRENGTH * _noise(0, sy)
            index = max(0, min(len(tones) - 1, round(level * (len(tones) - 1))))
            color = tones[index]
            canvas.pixels[offset] = color


def _carbon(canvas, points):
    _surface(canvas, points, "carbon")


def _bolt(canvas, x, y):
    canvas.rect(x - 1, y - 1, x + 1, y + 1, SILVER)
    canvas.point(x, y, BLACK)


def _lcd(canvas):
    """Complete wheel-mounted display, before live speed and RPM are drawn."""
    arch = [(x, INSTRUMENT_RECT[1] + round(DISPLAY_ARCH_DROP *
                                          ((x - SHIFT_LED_CENTER_X) / DISPLAY_ARCH_HALF_WIDTH) ** 2))
            for x in range(INSTRUMENT_RECT[0], INSTRUMENT_RECT[0] + INSTRUMENT_RECT[2])]
    canvas.polygon(arch + [(209, 169), (204, 172), (115, 172), (111, 169)], EDGE)
    canvas.polygon([(x, y + 1) for x, y in arch[2:-2]] +
                   [(206, 168), (203, 170), (117, 170), (114, 168)], CARBON)
    for first, last in zip(arch, arch[1:]):
        canvas.line(first, last, SILVER)
    # Ten unlit lenses follow the bowed top of the display, including an
    # even-count central gap. Both the arch and lenses rotate with the LCD.
    half_span = (SHIFT_LED_COUNT - 1) * SHIFT_LED_SPACING / 2
    for index in range(SHIFT_LED_COUNT):
        x = round(SHIFT_LED_CENTER_X - half_span + index * SHIFT_LED_SPACING)
        y = SHIFT_LED_TOP + round(SHIFT_LED_ARCH_DROP *
                                  ((x - SHIFT_LED_CENTER_X) / half_span) ** 2)
        lens_colors = (BLACK, EDGE, SILVER, SHIFT_LED_HIGHLIGHT)
        for row, pixels in enumerate(SHIFT_LED_LENS):
            for column, pixel in enumerate(pixels):
                if pixel != " ":
                    canvas.point(x + column - SHIFT_LED_RADIUS,
                                 y + row - SHIFT_LED_RADIUS, lens_colors[int(pixel)])
    canvas.rect(115, 142, 204, 171, BLACK)
    canvas.line((117, 143), (202, 143), EDGE)
    canvas.text(117, 144, "RPM", WHITE)
    canvas.text(192, 144, "BAT", WHITE)
    canvas.text(154, 165, "MPH", WHITE)
    for x, y, width, height in (RPM_BAR_RECT, BATTERY_BAR_RECT):
        x, y = x + INSTRUMENT_RECT[0], y + INSTRUMENT_RECT[1]
        canvas.rect(x - 1, y - 1, x + width, y + height, EDGE)
        canvas.rect(x, y, x + width - 1, y + height - 1, CHARCOAL)
    _fill_bar(canvas, BATTERY_BAR_RECT, BATTERY_CHARGE_PERCENT, PERCENT_SCALE)
    _bolt(canvas, 116, 170)
    _bolt(canvas, 204, 170)


def _ellipse(canvas, x, y, rx, ry, color, material=None):
    points = [(x + rx * math.cos(step * math.pi / 8),
               y + ry * math.sin(step * math.pi / 8)) for step in range(16)]
    if material:
        _surface(canvas, points, material)
    else:
        canvas.polygon(points, color)


def _fill_bar(canvas, rectangle, value, maximum):
    """Fill a local LCD bar upward, matching the native dashboard renderer."""
    x, y, width, height = rectangle
    x, y = x + INSTRUMENT_RECT[0], y + INSTRUMENT_RECT[1]
    fill = height * min(max(value, 0), maximum) // maximum
    if fill:
        canvas.rect(x, y + height - fill, x + width - 1, y + height - 1, WHITE)


def _mirror_points(points, reflected):
    return [(WIDTH - 1 - x if reflected else x, y) for x, y in points]


def _seam(canvas, points, color):
    for first, last in zip(points, points[1:]):
        canvas.line(first, last, color)


def _air_outlet(canvas, reflected):
    """Rounded open mouth, recessed curved walls and a thick near-side lip."""
    def mirror(points):
        return _mirror_points(points, reflected)

    def texture(x, y):
        return (WIDTH - 1 - x if reflected else x), y

    _surface(canvas, mirror(DUCT_SHELL), "duct", texture)
    canvas.polygon(mirror(DUCT_MOUTH), CHARCOAL)
    canvas.polygon(mirror(DUCT_THROAT), BLACK)
    _surface(canvas, mirror(DUCT_FLOOR), "recess", texture)
    # Partial ribs recede into the throat instead of dividing it into slots.
    for first, last in DUCT_RIBS:
        canvas.line(*mirror([first, last]), DUCT_RIB_SHADE)
    _seam(canvas, mirror(DUCT_UPPER_LIP), CHARCOAL)
    _seam(canvas, mirror(DUCT_LOWER_LIP), EDGE)
    _seam(canvas, mirror(DUCT_INNER_LIP), CARBON)


def _fascia(canvas):
    """Raised upper surround flush with the side plates, ahead of the well."""
    # Recessed column geometry is behind the entire foreground surround.
    _surface(canvas, COLUMN_RECESS, "recess")
    _seam(canvas, COLUMN_RIM, BLACK)
    _seam(canvas, [(x, y + 1) for x, y in COLUMN_RIM], CARBON)
    _ellipse(canvas, 160, 143, 14, 13, BLACK)
    _ellipse(canvas, 160, 142, 11, 10, EDGE)
    _ellipse(canvas, 160, 144, 9, 10, BLACK)

    lower_edge = [(x, y + FASCIA_FACE_DEPTH) for x, y in FASCIA_FRONT_EDGE]
    shadow_edge = [(x, y + FASCIA_SHADOW_DROP) for x, y in lower_edge]
    canvas.polygon(list(FASCIA_FRONT_EDGE) + list(reversed(shadow_edge)), BLACK)
    _surface(canvas, list(FASCIA_FRONT_EDGE) + list(reversed(lower_edge)),
             "fascia_carbon", light=FASCIA_FACE_LIGHT)
    _surface(canvas, FASCIA_OUTLINE, "fascia_carbon", light=FASCIA_TOP_LIGHT)
    _seam(canvas, FASCIA_UPPER_EDGE, FASCIA_SEAM)
    _seam(canvas, FASCIA_FRONT_EDGE, EDGE)
    _seam(canvas, lower_edge, CHARCOAL)
    for reflected in (False, True):
        _air_outlet(canvas, reflected)


def _side_panel_point(t, across=0):
    """Projected upright control plate with narrow beveled side edges."""
    a, b, c, d = SIDE_PANEL_CURVE
    u = 1 - t
    x, y = (u ** 3 * a[axis] + 3 * u * u * t * b[axis] +
            3 * u * t * t * c[axis] + t ** 3 * d[axis] for axis in (0, 1))
    dx, dy = (3 * u * u * (b[axis] - a[axis]) +
              6 * u * t * (c[axis] - b[axis]) +
              3 * t * t * (d[axis] - c[axis]) for axis in (0, 1))
    # A narrow neck meets the outlet corner before broadening around the
    # controls; the metal stays clear of the opening and its lower lip.
    neck = min(t / SIDE_PANEL_NECK_LENGTH, 1)
    half_width = (SIDE_PANEL_NECK_HALF_WIDTH +
                  (SIDE_PANEL_HALF_WIDTH - SIDE_PANEL_NECK_HALF_WIDTH) * neck +
                  SIDE_PANEL_FLARE * t)
    radius = half_width * across / math.hypot(dx, dy)
    return x - dy * radius, y + dx * radius


def _panel_screw(canvas, x, y):
    _ellipse(canvas, x, y + 1, SCREW_RADIUS, SCREW_RADIUS, BLACK)
    _ellipse(canvas, x, y, SCREW_RADIUS, SCREW_RADIUS, EDGE)
    canvas.point(x, y - 1, SILVER)
    canvas.line((x - 1, y), (x + 1, y), BLACK)


def _potentiometer(canvas, x, y):
    """Short black rotary control with a raised cap and fluted sidewall."""
    _ellipse(canvas, x, y + 2, POTENTIOMETER_RADIUS + 1, POTENTIOMETER_RADIUS, BLACK)
    _ellipse(canvas, x, y, POTENTIOMETER_RADIUS, POTENTIOMETER_RADIUS, EDGE)
    _ellipse(canvas, x, y - 1, POTENTIOMETER_RADIUS - 1, POTENTIOMETER_RADIUS - 2, CHARCOAL)
    for offset in (-3, 0, 3):
        canvas.line((x + offset, y + 1), (x + offset, y + 3), CARBON)
    canvas.line((x - 2, y - 3), (x + 1, y - 3), EDGE)
    canvas.line((x + 1, y - 2), (x + 2, y), SILVER)


def _side_panels(canvas):
    """Upright dark-grey metal control plates ahead of the column well."""
    samples = [step / SIDE_PANEL_STEPS for step in range(SIDE_PANEL_STEPS + 1)]
    for reflected in (False, True):
        def mirror(points):
            return _mirror_points(points, reflected)

        def texture(x, y):
            x = WIDTH - 1 - x if reflected else x
            return x - (y - SIDE_PANEL_CURVE[0][1]) ** 2 / HEIGHT, y

        _surface(canvas, mirror(SIDE_TUB_OUTLINE), "carbon", texture)
        outline = ([_side_panel_point(t, -1) for t in samples] +
                   [_side_panel_point(t, 1) for t in reversed(samples)])
        shadow = [(x + SIDE_PANEL_SHADOW_OFFSET[0], y + SIDE_PANEL_SHADOW_OFFSET[1])
                  for x, y in outline]
        canvas.polygon(mirror(shadow), BLACK)
        for near, far, light in zip(SIDE_PANEL_BANDS, SIDE_PANEL_BANDS[1:], SIDE_PANEL_LIGHT):
            strip = ([_side_panel_point(t, near) for t in samples] +
                     [_side_panel_point(t, far) for t in reversed(samples)])
            _surface(canvas, mirror(strip), "console_metal", texture, light=light)
        _seam(canvas, mirror([_side_panel_point(t, 1) for t in samples]), EDGE)
        _seam(canvas, mirror([_side_panel_point(t, -1) for t in samples]), CHARCOAL)
        controls = mirror([_side_panel_point(t) for t in SIDE_CONTROL_STATIONS])
        (red_x, red_y), (screw_x, screw_y), (knob_x, knob_y), (pair_x, pair_y) = [
            (round(x), round(y)) for x, y in controls]
        if not reflected:
            _ellipse(canvas, red_x, red_y + 1, SIDE_BUTTON_RADIUS + 1, SIDE_BUTTON_RADIUS + 1, BLACK)
            _ellipse(canvas, red_x, red_y, SIDE_BUTTON_RADIUS, SIDE_BUTTON_RADIUS, CONTROL_RED_LIGHT)
            _ellipse(canvas, red_x, red_y, SIDE_BUTTON_RADIUS - 1, SIDE_BUTTON_RADIUS - 1, CONTROL_RED_SHADOW)
            _ellipse(canvas, red_x, red_y - 1, SIDE_BUTTON_RADIUS - 2, SIDE_BUTTON_RADIUS - 2, CONTROL_RED_FACE)
            canvas.point(red_x - 2, red_y - 3, CONTROL_RED_LIGHT)
        else:
            # The right reference has a projecting red release paddle.
            canvas.polygon([(red_x - 5, red_y - 6), (red_x + 3, red_y - 5),
                            (red_x + 5, red_y + 6), (red_x - 3, red_y + 7)], BLACK)
            canvas.polygon([(red_x - 4, red_y - 5), (red_x + 2, red_y - 4),
                            (red_x + 3, red_y + 3), (red_x - 2, red_y + 4)], CONTROL_RED_FACE)
            canvas.line((red_x - 3, red_y - 4), (red_x + 1, red_y - 3), CONTROL_RED_LIGHT)
            canvas.line((red_x - 1, red_y + 4), (red_x, red_y + 6), CONTROL_RED_SHADOW, 2)
        _panel_screw(canvas, screw_x, screw_y)
        _potentiometer(canvas, knob_x, knob_y)
        for offset in (-SIDE_SCREW_PAIR_SPACING // 2, SIDE_SCREW_PAIR_SPACING // 2):
            _panel_screw(canvas, pair_x + offset, pair_y)


def _fenders(canvas):
    """Static exterior body visible through the bottom of the windscreen.

    Only the smooth crowns are visible from the driver's eye line.
    The exterior mesh retains its openings, hidden here behind the bodywork.
    Their turquoise finish follows the requested exterior colour; native bitmap
    dashboards have no per-paint recolouring, so these stay turquoise for all paints.
    """
    for side in (-1, 1):
        def mirror(points):
            return [(x if side < 0 else WIDTH - 1 - x, y) for x, y in points]

        _surface(canvas, mirror([(0, 104), (17, 104), (35, 107), (52, 112),
                                 (65, 118), (80, 124), (96, 126), (0, 126)]), "paint")
        _surface(canvas, mirror([(0, 108), (16, 108), (34, 111), (49, 115),
                                 (61, 120), (74, 125), (0, 126)]), "fender")
        for first, last in (((0, 107), (17, 107)), ((17, 107), (35, 110)),
                            ((35, 110), (51, 115))):
            canvas.line(*mirror([first, last]), FENDER_HIGHLIGHT)
        canvas.line(*mirror([(62, 121), (75, 125)]), FENDER_SIDE)


def _cowl(canvas):
    """Gently bowed dashboard crown and lower windscreen rail.

    The photographs' wraparound cowl rises towards the central instruments.
    Draw a shallow seven-pixel crown instead of a straight full-width strip;
    the optional top/mask pair preserves the road above each curved sample.
    """
    half_width = (WIDTH - 1) / 2
    crown = [(x, COWL_CROWN_TOP + round(COWL_CROWN_RISE *
                                       ((x - half_width) / half_width) ** 2))
             for x in range(WIDTH)]
    _surface(canvas, crown + [(WIDTH - 1, COWL_BOTTOM), (0, COWL_BOTTOM)], "suede")
    for x, y in crown:
        canvas.point(x, y, BLACK)
        canvas.point(x, y + 1, CARBON)
        canvas.point(x, y + 2, CHARCOAL)
        # A faint seam follows the same curve, avoiding a horizontal band.
        canvas.point(x, y + 5, CARBON)


def _canopy(canvas):
    """Symmetric lower roof edge; the forward view has no side pillars."""
    def texture_coordinates(x, y):
        # Coarser horizontal weave keeps native RLE within the DOS RAM budget.
        return min(x, WIDTH - 1 - x) // CANOPY_TEXTURE_SCALE, y

    _surface(canvas, CANOPY_ROOF_OUTLINE, "carbon", texture_coordinates)
    for x, y in CANOPY_WINDSCREEN_EDGE:
        # Put the glazing seal inside the opaque roof, never in the road view.
        canvas.point(x, y - 1, CANOPY_EDGE_SHADOW)


def _panel():
    canvas = Canvas(color=TRANSPARENT)
    _surface(canvas, [(0, DASH_OPAQUE_TOP), (WIDTH - 1, DASH_OPAQUE_TOP),
                       (WIDTH - 1, HEIGHT), (0, HEIGHT)], "recess")
    _fenders(canvas)
    _cowl(canvas)
    _canopy(canvas)
    _fascia(canvas)
    _side_panels(canvas)
    _steering_track(canvas, STEERING_INDICATOR_CENTER)
    return canvas


def transform_wheel_points(vertices, direction):
    """Rigid wheel-plane rotation followed by the shared cockpit projection."""
    angle = math.radians(direction * WHEEL_TURN_DEGREES)
    cosine, sine = math.cos(angle), math.sin(angle)
    return [(WHEEL_CENTER[0] + x * cosine - y * sine,
             WHEEL_CENTER[1] + WHEEL_VERTICAL_SCALE * (x * sine + y * cosine))
            for x, y in vertices]


def _wheel(canvas, direction):
    """Draw full open grips, one solid connector per side and five knobs."""
    angle = math.radians(direction * WHEEL_TURN_DEGREES)
    cosine, sine = math.cos(angle), math.sin(angle)

    def local_coordinates(x, y):
        x, y = x - WHEEL_CENTER[0], (y - WHEEL_CENTER[1]) / WHEEL_VERTICAL_SCALE
        return x * cosine + y * sine, -x * sine + y * cosine

    def points(vertices):
        transformed = transform_wheel_points(vertices, direction)
        x, y, width, height = WHEEL_RECT
        if any(not x <= round(px) < x + width or
               not y <= round(py) < y + height for px, py in transformed):
            raise ValueError("Rigid wheel geometry escapes its redraw rectangle")
        return transformed

    def polygon(vertices, color, material=None):
        if material is None:
            material = {CARBON: "wheel_carbon", CHARCOAL: "grip", EDGE: "wheel_trim"}.get(color)
        transformed = points(vertices)
        if material:
            _surface(canvas, transformed, material, local_coordinates)
        else:
            canvas.polygon(transformed, color)

    def line(vertices, color, thickness=1):
        canvas.line(*points(vertices), color, thickness)

    def bitmap(source, origin):
        """Turn a native-sized detail with the wheel without dropping black."""
        origin_x, origin_y = origin
        corners = points([(origin_x, origin_y), (origin_x + source.width, origin_y),
                          (origin_x + source.width, origin_y + source.height / WHEEL_VERTICAL_SCALE),
                          (origin_x, origin_y + source.height / WHEEL_VERTICAL_SCALE)])
        for py in range(math.floor(min(y for _, y in corners)),
                        math.ceil(max(y for _, y in corners)) + 1):
            for px in range(math.floor(min(x for x, _ in corners)),
                            math.ceil(max(x for x, _ in corners)) + 1):
                local_x, local_y = local_coordinates(px, py)
                sx = round(local_x - origin_x)
                sy = round((local_y - origin_y) * WHEEL_VERTICAL_SCALE)
                if 0 <= sx < source.width and 0 <= sy < source.height:
                    color = source.pixels[sy * source.width + sx]
                    if color != TRANSPARENT:
                        canvas.point(px, py, color)

    for side in (-1, 1):
        def mirror(vertices):
            return [(side * x, y) for x, y in vertices]

        # The continuous web fills the former gap between the two spokes.
        # Its upper bevel and broad face are facets of the same solid part.
        polygon(mirror(GRIP_CONNECTOR), BLACK)
        polygon(mirror(GRIP_CONNECTOR_FACE), CHARCOAL)
        polygon(mirror(GRIP_CONNECTOR_TOP), CARBON)
        polygon(mirror(GRIP_OUTLINE), BLACK)
        polygon(mirror(GRIP_PADDING), CHARCOAL)
        polygon(mirror(GRIP_PALM_FACE), CHARCOAL, material="grip_face")
        for first, last in zip(GRIP_OUTER_EDGE, GRIP_OUTER_EDGE[1:]):
            line(mirror([first, last]), WHEEL_EDGE_HIGHLIGHT)
        for first, last in zip(GRIP_INNER_EDGE, GRIP_INNER_EDGE[1:]):
            line(mirror([first, last]), EDGE)
        # Broad, angled end caps sit level with the arched display top.
        polygon(mirror(GRIP_CAP), EDGE)
        for first, last in zip(GRIP_CAP_HIGHLIGHT, GRIP_CAP_HIGHLIGHT[1:]):
            line(mirror([first, last]), SILVER)

    plate = [(-48, 8), (48, 8), (45, 26), (33, 41),
             (-23, 43), (-42, 29)]
    polygon(plate, BLACK)
    polygon([(x * .96, y * .97) for x, y in plate], CARBON)
    line([(-44, 10), (43, 10)], EDGE)
    # The very same cap, index and fluted sidewall serve all seven controls.
    half_size = POTENTIOMETER_BITMAP_HALF_SIZE
    knob = Canvas(2 * half_size + 1, 2 * half_size + 1, color=TRANSPARENT)
    _potentiometer(knob, half_size, half_size)
    for x, y in CONTROL_CENTERS:
        bitmap(knob, (x - half_size, y - half_size / WHEEL_VERTICAL_SCALE))

    # The small C shares the other letters' cap line; its shorter height
    # supplies the raised appearance without projecting above the M.
    text_width = len(BRAND_TEXT) * FONT_GLYPH_ADVANCE - 1
    lettering = Canvas(text_width, FONT_GLYPH_HEIGHT, color=TRANSPARENT)
    for index, letter in enumerate(BRAND_TEXT):
        x = index * FONT_GLYPH_ADVANCE
        if index == BRAND_SUPERSCRIPT_INDEX:
            for row, pixels in enumerate(BRAND_SUPERSCRIPT_C):
                for column, pixel in enumerate(pixels):
                    if pixel == "1":
                        lettering.point(x + column, row, WHITE)
        else:
            lettering.text(x, 0, letter, WHITE)
    bitmap(lettering, BRAND_ORIGIN)


def instrument_transform(direction):
    """Q14 inverse of the wheel's rotation and vertical cockpit projection."""
    angle = math.radians(direction * WHEEL_TURN_DEGREES)
    cosine, sine = math.cos(angle), math.sin(angle)
    return tuple(round(value * INSTRUMENT_TRANSFORM_SCALE) for value in
                 (cosine, sine / WHEEL_VERTICAL_SCALE,
                  -sine * WHEEL_VERTICAL_SCALE, cosine))


def _instrument_panel():
    # A black backing keeps the same asset usable in the original game, whose
    # fixed instrument copy ignores the optional transform descriptors.
    canvas = Canvas(color=BLACK)
    _lcd(canvas)
    return canvas


def _preview_instruments(canvas, direction, speed):
    panel = _instrument_panel()
    digits = str(speed)
    start_x = SPEED_CENTER[0] - (DIGIT_WIDTH + (len(digits) - 1) * DIGIT_SPACING) // 2
    for index, number in enumerate(digits):
        x, y = start_x + index * DIGIT_SPACING, SPEED_CENTER[1]
        pixels = _speed_digit(int(number))
        for row in range(DIGIT_HEIGHT):
            for column in range(DIGIT_WIDTH):
                color = pixels[row * DIGIT_WIDTH + column]
                if color:
                    panel.point(INSTRUMENT_RECT[0] + x + column,
                                INSTRUMENT_RECT[1] + y + row, color)
    rpm = max(IDLE_RPM, (speed << SPEED_FIXED_POINT_SHIFT) * GEAR_RATIO >> GEAR_RATIO_SHIFT)
    _fill_bar(panel, RPM_BAR_RECT, rpm >> RPM_INDEX_SHIFT, RPM_POINT_COUNT - 1)
    pixels = panel.crop(INSTRUMENT_RECT)
    xx, xy, yx, yy = instrument_transform(direction)
    left, top, width, height = WHEEL_RECT
    for y in range(top, top + height):
        for x in range(left, left + width):
            dx, dy = x - WHEEL_CENTER[0], y - WHEEL_CENTER[1]
            sx = ((xx * dx + xy * dy + INSTRUMENT_TRANSFORM_HALF) //
                  INSTRUMENT_TRANSFORM_SCALE + INSTRUMENT_PIVOT[0])
            sy = ((yx * dx + yy * dy + INSTRUMENT_TRANSFORM_HALF) //
                  INSTRUMENT_TRANSFORM_SCALE + INSTRUMENT_PIVOT[1])
            if 0 <= sx < INSTRUMENT_RECT[2] and 0 <= sy < INSTRUMENT_RECT[3]:
                canvas.point(x, y, pixels[sy * INSTRUMENT_RECT[2] + sx])


def _speed_digit(number):
    """Reuse the shared seven-segment geometry in the McMurtry's white LCD."""
    return bytes(WHITE if pixel else BLACK for pixel in _digit(number))


def build_dashboards(repo: Path) -> tuple[bytes, bytes, dict[int, bytes]]:
    """Build STDA/STDB resources and the instrument-only SIMD changes."""
    repo = Path(repo)
    donor_a = (repo / "tools/scripts/cars/STDADIA3.VSH").read_bytes()
    donor_b = (repo / "tools/scripts/cars/STDBDIA3.VSH").read_bytes()
    donor_maps = _resource_dict(donor_b)
    # Preserve the game colour maps, not the donor's dashboard artwork.
    maps = {name: donor_maps[name] for name in ("!cg0", "!eg0")}
    canvas = _panel()
    primary = dict(maps)
    primary["dash"] = _shape(WIDTH, HEIGHT - DASH_TOP,
                              canvas.crop((0, DASH_TOP, WIDTH, HEIGHT - DASH_TOP)),
                              y=DASH_TOP)
    primary["roof"] = _shape(WIDTH, ROOF_HEIGHT, canvas.crop(ROOF_RECT))
    # A dash's Y is also the native windshield viewport boundary. The existing
    # roof clips only the opaque top strip. The optional top/mask pair draws
    # the curved roof edge and fenders without clipping road
    # rendering inside the windscreen. AND preserves glazing; OR supplies carbon
    # and bodywork silhouettes.
    x, y, width, height = TOP_OVERLAY_RECT
    upper = canvas.crop(TOP_OVERLAY_RECT)
    primary["dast"] = _shape(width, height,
                              bytes(0 if color == TRANSPARENT else color for color in upper), x, y)
    primary["dasm"] = _shape(width, height,
                              bytes(255 if color == TRANSPARENT else 0 for color in upper), x, y)
    for index, direction in enumerate((-1, 0, 1), 1):
        steered = _panel()
        _wheel(steered, direction)
        x, y, width, height = WHEEL_RECT
        if any(pixel != canvas.pixels[offset] and not
               (x <= offset % WIDTH < x + width and y <= offset // WIDTH < y + height)
               for offset, pixel in enumerate(steered.pixels)):
            raise ValueError("McMurtry steering escapes its redraw rectangle")
        primary[f"whl{index}"] = _shape(width, height, steered.crop(WHEEL_RECT), x, y)
    x, y, width, height = INSTRUMENT_RECT
    primary["ins2"] = _shape(width, height, _instrument_panel().crop(INSTRUMENT_RECT),
                               x, y, anchor=INSTRUMENT_PIVOT)
    primary["spdc"] = _shape(1, 1, bytes([BLACK]), *SPEED_CENTER)
    bar_x, bar_y, bar_width, bar_height = RPM_BAR_RECT
    primary["revb"] = _shape(bar_width, bar_height,
                              bytes([WHITE]) * bar_width * bar_height, bar_x, bar_y)
    # Optional header-only inverse transforms keep live instruments attached to
    # the wheel. The original game ignores these additional bitmap resources.
    for name, direction in INSTRUMENT_TRANSFORM_IDS:
        xx, xy, yx, yy = instrument_transform(direction)
        primary[name] = _shape(1, 1, bytes([BLACK]),
                                 yx & INSTRUMENT_TRANSFORM_WORD_MASK,
                                 yy & INSTRUMENT_TRANSFORM_WORD_MASK,
                                 anchor=(xx, xy))
        panel_x, panel_y, panel_width, panel_height = INSTRUMENT_RECT
        corners = [(px - WHEEL_CENTER[0],
                    (py - WHEEL_CENTER[1]) / WHEEL_VERTICAL_SCALE)
                   for px in (panel_x, panel_x + panel_width - 1)
                   for py in (panel_y, panel_y + panel_height - 1)]
        wheel_x, wheel_y, wheel_width, wheel_height = WHEEL_RECT
        if any(not wheel_x <= round(px) < wheel_x + wheel_width or
               not wheel_y <= round(py) < wheel_y + wheel_height
               for px, py in transform_wheel_points(corners, direction)):
            raise ValueError("Rotating display escapes the wheel redraw rectangle")
    for name, color in (("ins1", 0), ("ins3", 0), ("inm1", 255), ("inm3", 255)):
        primary[name] = _shape(1, 1, bytes([color]))
    primary["gbox"] = _shape(1, 1, bytes([BLACK]), 318, 198)
    secondary = dict(maps)
    secondary.update(steering_dot_shapes())
    for name, color in (("gnob", 0), ("gnab", 255)):
        secondary[name] = _shape(1, 1, bytes([color]))
    for number in range(10):
        secondary[f"dig{number}"] = _shape(DIGIT_WIDTH, DIGIT_HEIGHT, _speed_digit(number))
    stda, stdb = _container(primary), _container(secondary)

    if any(x < 0 or y < 0 or x + DIGIT_WIDTH > width or y + DIGIT_HEIGHT > height
           for x, y in DIGIT_POSITIONS):
        raise ValueError("McMurtry speed digit escapes its instrument rectangle")
    rev_points = bytearray()
    for step in range(RPM_POINT_CAPACITY):
        rise = ((RPM_BAR_RECT[3] - 1) * min(step, RPM_POINT_COUNT - 1) //
                (RPM_POINT_COUNT - 1))
        rev_points.extend((MOTOR_CENTER[0], MOTOR_CENTER[1] - rise))
    patches = {
        0x01E: bytes(26),
        STEERING_DOTS_OFFSET: steering_dot_positions(STEERING_INDICATOR_CENTER, DASH_TOP),
        0x128: struct.pack("<hhH", 0, 0, 1),
        0x12E: bytes(value for pair in DIGIT_POSITIONS for value in pair) + bytes(202),
        0x1FE: struct.pack("<hhH", *MOTOR_CENTER, RPM_POINT_COUNT),
        0x204: bytes(rev_points),
        0x0AE: struct.pack("<H", WHITE | (WHITE << 8)),
    }
    if len(stda) >= len(donor_a) or len(stdb) >= len(donor_b):
        raise ValueError("McMurtry dashboard must use less extracted memory than DIA3")
    if prepared_stda_size(stda) >= prepared_stda_size(donor_a):
        raise ValueError("McMurtry dashboard must use less prepared memory than DIA3")
    if width > 188 or height > 75:
        raise ValueError("McMurtry instrument scratch exceeds DIA3 dimensions")
    return stda, stdb, patches


def write_preview(repo: Path, path: Path, speed=150):
    """Save an indexed PNG illustration with neutral steering and sample speed."""
    canvas = Canvas()
    canvas.rect(0, 0, WIDTH - 1, 89, 116)
    canvas.rect(0, 90, WIDTH - 1, DASH_OPAQUE_TOP - 1, 84)
    canvas.polygon([(151, 90), (168, 90), (220, 124), (96, 124)], EDGE)
    canvas.line((159, 93), (159, 101), WHITE)
    canvas.line((159, 108), (159, 122), WHITE)
    overlay = _panel()
    _wheel(overlay, 0)
    _preview_instruments(overlay, 0, speed)
    _preview_steering_dot(overlay, STEERING_INDICATOR_CENTER)
    for offset, color in enumerate(overlay.pixels):
        if color != TRANSPARENT:
            canvas.pixels[offset] = color
    path = Path(path)
    path.write_bytes(_helpers(Path(repo))["indexed_png"](
        WIDTH, HEIGHT, bytes(canvas.pixels), _palette(Path(repo))))
    return path


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--preview", type=Path, default=Path("/tmp/mcmurtry-dashboard.png"))
    arguments = parser.parse_args()
    primary, secondary, _ = build_dashboards(arguments.repository)
    print(f"STDA {len(primary)} bytes; prepared {prepared_stda_size(primary)} bytes")
    print(f"STDB {len(secondary)} bytes")
    print(write_preview(arguments.repository, arguments.preview))
