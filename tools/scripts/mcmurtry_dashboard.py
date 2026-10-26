#!/usr/bin/env python3
"""Original indexed-palette cockpit for the MCMU McMurtry Speirling car.

The studio interior photograph supplies the slim fascia layout, dark carbon
surround, empty display mount and two recessed passenger-side ducts. The fascia
uses light-grey carbon with its weave opposite the surrounding dashboard.
The wheel retains its continuous shaped carbon/metal rim and textured grips.
Material grain and directional shading give the original pixel artwork a
palette-reduced photographic finish.
The live LCD turns with the wheel, including its speed digits and motor needle.
The five identical, unlabelled fluted controls and
unlit shift-lamp lenses are artwork, not live telemetry. A native ``roof`` strip
and ``dast``/``dasm`` masks frame the wide forward windscreen with
the reference cockpit's curved dark-carbon canopy and peripheral pillars.

``build_dashboards(repo)`` returns two raw VSH containers and SIMD byte patches.
The patches configure instruments only; the caller supplies vehicle physics.
Generic indexed-raster/resource helpers are shared with the Tesla authoring
tool, while every dashboard and steering sprite is drawn here.
"""

import argparse
import math
from pathlib import Path
import struct

from tesla_roadster_dashboard import (
    Canvas, _container, _digit, _helpers, _palette, _resource_dict, _shape,
    _preview_steering_dot, _steering_track, prepared_stda_size,
    steering_dot_positions, steering_dot_shapes, SHAPE_HEADER_SIZE, STEERING_DOTS_OFFSET,
)


WIDTH, HEIGHT = 320, 200
DASH_TOP = 125
DASH_OPAQUE_TOP = DASH_TOP
ROOF_HEIGHT = 24
ROOF_RECT = (0, 0, WIDTH, ROOF_HEIGHT)
TOP_OVERLAY_RECT = (0, ROOF_HEIGHT, WIDTH, DASH_TOP - ROOF_HEIGHT)
CANOPY_ROOF_OUTLINE = (
    (0, 0), (319, 0), (319, 48), (309, 46), (303, 36), (296, 30),
    (289, 26), (274, 24), (239, 24), (199, 26), (160, 28),
    (120, 26), (80, 24), (45, 24), (30, 26), (23, 30),
    (16, 36), (10, 46), (0, 48),
)
# Left-side geometry is reflected about the centre of the indexed image.
CANOPY_PILLAR = ((0, 24), (23, 30), (18, 43), (15, 61), (13, 87),
                 (13, 107), (20, 125), (0, 125))
CANOPY_PILLAR_FOOT = ((0, 115), (15, 113), (24, 125), (0, 125))
CANOPY_WINDSCREEN_EDGE = ((160, 28), (120, 26), (80, 24), (45, 24),
                          (30, 26), (23, 30), (18, 43), (15, 61),
                          (13, 87), (13, 107), (20, 125))
CANOPY_TEXTURE_SCALE = 3
INSTRUMENT_RECT = (111, 135, 99, 38)
WHEEL_RECT = (70, 125, 180, 75)
WHEEL_TURN_DEGREES = 10
STEERING_INDICATOR_CENTER = (160, 126)
WHEEL_CENTER = (160, 163)
WHEEL_VERTICAL_SCALE = .72
# One unchanged local shape for every steering state. The full rim uses the
# same rotation and projection as the grips, lower return and control plate.
UPPER_RIM_OUTER = ((-68, -29), (-54, -37.5), (-45, -41.25), (-33, -42.5),
                   (33, -42.5), (45, -41.25), (54, -37.5), (68, -29))
UPPER_RIM_INNER = ((58, -21), (51, -32.5), (42, -36.25), (33, -38.75),
                   (-33, -38.75), (-42, -36.25), (-51, -32.5), (-58, -21))
CONTROL_CENTERS = ((-36, 28), (-19, 35), (0, 31), (23, 32), (37, 28))
DIGIT_WIDTH, DIGIT_HEIGHT = 8, 12
DIGIT_POSITIONS = ((11, 18), (21, 18), (31, 18))
BLACK, CHARCOAL, CARBON, EDGE = 0, 31, 29, 27
CANOPY_EDGE_SHADOW = CHARCOAL
WHITE, SILVER, RED = 19, 22, 39
TRANSPARENT = 255
FENDER_HIGHLIGHT, FENDER_SIDE = 121, 125
MOTOR_CENTER = (74, 32)
INSTRUMENT_TRANSFORM_SHIFT = 14
INSTRUMENT_TRANSFORM_SCALE = 1 << INSTRUMENT_TRANSFORM_SHIFT
INSTRUMENT_TRANSFORM_HALF = INSTRUMENT_TRANSFORM_SCALE // 2
INSTRUMENT_TRANSFORM_WORD_MASK = 0xFFFF
INSTRUMENT_TRANSFORM_IDS = (("imt1", -1), ("imt3", 1))
INSTRUMENT_PIVOT = (WHEEL_CENTER[0] - INSTRUMENT_RECT[0],
                    WHEEL_CENTER[1] - INSTRUMENT_RECT[1])
COWL_CROWN_TOP, COWL_CROWN_RISE, COWL_BOTTOM = 118, 7, 132
FASCIA_OUTLINE = ((8, 135), (28, 129), (72, 124), (120, 121), (160, 120),
                  (200, 121), (248, 124), (292, 130), (312, 138), (313, 146),
                  (301, 150), (272, 145), (232, 137), (194, 133), (160, 132),
                  (126, 133), (88, 137), (49, 144), (23, 148), (9, 144))
FASCIA_UPPER_EDGE = FASCIA_OUTLINE[:9]
FASCIA_LOWER_EDGE = FASCIA_OUTLINE[9:] + FASCIA_OUTLINE[:1]
FASCIA_SEAM, FASCIA_SHADOW = 22, 27
FASCIA_CLOUD_STRENGTH, FASCIA_GRAIN_STRENGTH = .12, .08
DISPLAY_MOUNT_OUTLINE = ((66, 123), (79, 121), (82, 145), (68, 147))
DISPLAY_MOUNT_FACE = ((69, 125), (77, 124), (79, 141), (70, 142))
DISPLAY_MOUNT_STEM = ((73, 146), (78, 146), (85, 156), (80, 159), (75, 153))
DISPLAY_MOUNT_EDGE = ((67, 124), (69, 144))
DISPLAY_MOUNT_CLIP = ((73, 144), (77, 144))
PASSENGER_DUCTS = ((265, 137), (280, 140))
DUCT_OUTER_RADII, DUCT_INNER_RADII = (6, 5), (4, 3)
DUCT_RIM = 28
DUCT_INNER_EDGE_OFFSETS = ((-2, 2), (1, 2))
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
CARBON_WEAVE_DIRECTIONS = {"carbon": 1, "wheel_carbon": 1, "fascia_carbon": -1}
METAL_BRUSH_STRENGTH = .18
WHEEL_EDGE_HIGHLIGHT = 25
MATERIAL_TONES = {
    "carbon": (31, 30, 29, 28),
    # Foreground wheel surfaces stay lighter than the fixed cockpit texture.
    "wheel_carbon": (30, 28, 27, 26),
    "suede": (31, 30, 29, 28, 27),
    "fascia_carbon": (25, 24, 23, 22, 21, 20),
    "recess": (31, 30),
    "grip": (31, 30, 28, 27),
    "paint": (127, 126, 125, 124, 123, 122),
    "fender": (126, 125, 124, 123, 122, 121, 120),
    "metal": (28, 26, 24, 22, 20, 18),
    "wheel_trim": (28, 27, 26, 25, 24),
}
DASHBOARD_REFERENCES = [
    {"url": "https://www.topgear.com/car-reviews/speirling/first-drive",
     "view": "September 2024 first-hand prototype review and photo gallery; single seat, single gear, fan and motor-power controls."},
    {"url": "https://www.topgear.com/sites/default/files/2024/09/DSC00094_3.jpg",
     "view": "Prototype cockpit: central rectangular LCD wheel, five rotary knobs, textured hand grips, bare carbon tub and red release handle."},
    {"url": "https://www.topgear.com/sites/default/files/2024/09/DSC00356_3.jpg",
     "view": "Detached prototype wheel close-up: eleven shift-lamp lenses, McMurtry/record-holder branding, TC/FAN/PWR/EPAS controls and textured grips."},
    {"file": "1-1000005914.jpg", "view": "User photograph: prototype carbon cockpit, curved upper trim, release handle, seams and fasteners."},
    {"file": "2-1000005913.jpg", "view": "User photograph: prototype LCD and five fluted rotary knobs; user requests all five match the unlabelled second knob."},
    {"file": "3-1000005912.jpg", "view": "Earlier user photograph: continuous shaped steering-wheel rim, padded carbon sills and front-fender crowns with upper openings."},
    {"file": "6.-McMurtry-Speirling-studio-interior-1-1024x683-2898273447.jpg", "view": "Dashboard layout reference: slim fascia, dark carbon cockpit, empty upright display mount and two dark passenger-side ducts. The user replaces the photo's tan suede with light-grey carbon whose weave runs opposite the rest of the dashboard; exterior fenders retain the requested turquoise finish and the central clock is omitted."},
    {"file": "5-Speirling-Interior-3083165339.webp", "view": "Canopy shape reference: broad bowed roof header and rounded windscreen corners. The high-FOV photograph's side windows are omitted as requested; one pair of pillars stays at the far screen edges around a wide forward windscreen. The surround uses the requested dark carbon, matching the dashboard; photograph pixels are not redistributed."},
    {"file": "mcmurtry-speirling.7-614860014.jpg", "view": "Open-canopy reference for the curved carbon roof ring, windscreen pillars and side-window surrounds."},
    {"file": "OIP-943499069.jpg", "view": "Open-canopy reference for the roof ring, dark side structure and glazing seals."},
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


def _surface(canvas, points, material, coordinates=None):
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
            level = (TEXTURE_BASE_LIGHT + TEXTURE_VERTICAL_LIGHT * (1 - v) +
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
    bezel = [(111, 138), (117, 135), (201, 135), (209, 138),
             (209, 169), (204, 172), (115, 172), (111, 169)]
    canvas.polygon(bezel, EDGE)
    canvas.polygon([(114, 138), (120, 136), (200, 136), (206, 138),
                    (206, 168), (203, 170), (117, 170), (114, 168)], CARBON)
    canvas.line((114, 138), (120, 136), SILVER)
    canvas.line((120, 136), (199, 136), SILVER)
    # The photographed lamps are unlit pale lenses, not fake shift telemetry.
    for index in range(11):
        x = 121 + index * 7
        canvas.rect(x - 1, 137, x + 1, 139, BLACK)
        canvas.point(x, 137, SILVER)
        canvas.point(x, 138, EDGE)
    canvas.rect(115, 141, 204, 171, BLACK)
    canvas.line((117, 143), (202, 143), EDGE)
    canvas.text(120, 145, "SPEED", WHITE)
    canvas.text(174, 145, "MOTOR", WHITE)
    canvas.text(152, 158, "MPH", SILVER)
    canvas.line((168, 147), (168, 169), CARBON)
    for index in range(13):
        angle = math.pi - index * math.pi / 12
        cx, cy = INSTRUMENT_RECT[0] + MOTOR_CENTER[0], INSTRUMENT_RECT[1] + MOTOR_CENTER[1]
        canvas.line((cx + 14 * math.cos(angle), cy - 11 * math.sin(angle)),
                    (cx + 12 * math.cos(angle), cy - 9 * math.sin(angle)),
                    RED if index > 9 else SILVER)
    canvas.text(120, 166, "MCMURTRY", SILVER)
    _bolt(canvas, 116, 170)
    _bolt(canvas, 204, 170)


def _ellipse(canvas, x, y, rx, ry, color, material=None):
    points = [(x + rx * math.cos(step * math.pi / 8),
               y + ry * math.sin(step * math.pi / 8)) for step in range(16)]
    if material:
        _surface(canvas, points, material)
    else:
        canvas.polygon(points, color)


def _fascia(canvas):
    """Light-grey carbon insert with reversed weave and recessed dark ducts."""
    _surface(canvas, FASCIA_OUTLINE, "fascia_carbon")
    for first, last in zip(FASCIA_UPPER_EDGE, FASCIA_UPPER_EDGE[1:]):
        canvas.line(first, last, FASCIA_SEAM)
    for first, last in zip(FASCIA_LOWER_EDGE, FASCIA_LOWER_EDGE[1:]):
        canvas.line(first, last, FASCIA_SHADOW)
    for x, y in PASSENGER_DUCTS:
        _ellipse(canvas, x, y, *DUCT_OUTER_RADII, DUCT_RIM)
        _ellipse(canvas, x, y, *DUCT_INNER_RADII, BLACK)
        canvas.line(*[(x + dx, y + dy) for dx, dy in DUCT_INNER_EDGE_OFFSETS], CHARCOAL)


def _display_mount(canvas):
    """Unoccupied upright mount; its face has no invented screen or labels."""
    _surface(canvas, DISPLAY_MOUNT_STEM, "carbon")
    _surface(canvas, DISPLAY_MOUNT_OUTLINE, "metal")
    canvas.polygon(DISPLAY_MOUNT_FACE, BLACK)
    canvas.line(*DISPLAY_MOUNT_EDGE, EDGE)
    canvas.line(*DISPLAY_MOUNT_CLIP, SILVER)


def _fenders(canvas):
    """Static exterior body visible through the bottom of the windscreen.

    The close crowns and upper openings follow the production cockpit photo.
    Their turquoise finish follows the requested exterior colour; native bitmap
    dashboards have no per-paint recolouring, so these stay turquoise for all paints.
    """
    for side in (-1, 1):
        def mirror(points):
            return [(160 + side * (x - 160), y) for x, y in points]

        _surface(canvas, mirror([(0, 125), (11, 118), (29, 109), (46, 106),
                                 (64, 110), (78, 117), (96, 125)]), "paint")
        _surface(canvas, mirror([(10, 124), (20, 117), (34, 111), (47, 109),
                                 (61, 112), (74, 119), (84, 125)]), "fender")
        canvas.line(*mirror([(20, 117), (35, 111)]), FENDER_HIGHLIGHT)
        canvas.line(*mirror([(35, 111), (47, 109)]), FENDER_HIGHLIGHT)
        _surface(canvas, mirror([(35, 110), (57, 110), (64, 115),
                                 (42, 117), (32, 114)]), "fender")
        canvas.polygon(mirror([(36, 111), (56, 111), (60, 114),
                               (42, 115), (35, 113)]), BLACK)
        canvas.line(*mirror([(39, 114), (57, 113)]), CHARCOAL)
        canvas.line(*mirror([(70, 119), (78, 124)]), FENDER_SIDE)


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
    """Broad bowed header and one pillar pair at the forward view's edges.

    The reference supplies the curvature; its high-FOV side windows stay out
    of this view. The native roof covers the opaque top, and masks preserve
    one wide live windscreen below it between the peripheral pillars.
    """
    def texture_coordinates(x, y):
        # Grouped horizontal samples retain the weave while allowing native RLE
        # to fit the larger canopy inside the existing dashboard RAM budget.
        return x // CANOPY_TEXTURE_SCALE, y

    surround = Canvas(color=TRANSPARENT)
    seals = Canvas(color=TRANSPARENT)
    _surface(surround, CANOPY_ROOF_OUTLINE, "carbon", texture_coordinates)
    for reflected in (False, True):
        def mirror(points):
            return [(WIDTH - 1 - x if reflected else x, y) for x, y in points]

        for outline in (CANOPY_PILLAR, CANOPY_PILLAR_FOOT):
            _surface(surround, mirror(outline), "carbon", texture_coordinates)
        points = mirror(CANOPY_WINDSCREEN_EDGE)
        for first, last in zip(points, points[1:]):
            seals.line(first, last, CANOPY_EDGE_SHADOW)
    for offset, color in enumerate(surround.pixels):
        if color != TRANSPARENT:
            # Rasterized seal lines stay on the carbon surface, preventing
            # detached outlines or hairline scenery gaps beside the glazing.
            seal = seals.pixels[offset]
            canvas.pixels[offset] = color if seal == TRANSPARENT else seal


def _panel():
    canvas = Canvas(color=TRANSPARENT)
    _surface(canvas, [(0, DASH_OPAQUE_TOP), (WIDTH - 1, DASH_OPAQUE_TOP),
                       (WIDTH - 1, HEIGHT), (0, HEIGHT)], "suede")
    _fenders(canvas)
    _cowl(canvas)
    _canopy(canvas)
    _fascia(canvas)
    _display_mount(canvas)
    # The light carbon insert sits above the dark carbon side plinths.
    _carbon(canvas, [(0, 146), (59, 149), (92, 161), (108, 199), (0, 199)])
    _carbon(canvas, [(319, 146), (261, 149), (228, 161), (212, 199), (319, 199)])
    canvas.polygon([(80, 145), (103, 143), (118, 199), (95, 199)], BLACK)
    canvas.polygon([(240, 145), (217, 143), (202, 199), (225, 199)], BLACK)
    _surface(canvas, [(112, 164), (208, 164), (221, 199), (99, 199)], "recess")
    for side in (-1, 1):
        def mirror(point):
            return (160 + side * (point[0] - 160), point[1])
        for first, last, color in (((4, 146), (57, 149), CARBON),
                                   ((59, 151), (89, 164), EDGE),
                                   ((89, 158), (105, 198), EDGE),
                                   ((67, 155), (83, 192), CARBON)):
            canvas.line(mirror(first), mirror(last), color)
        # Stitched impact padding and metal fasteners on the door structure.
        canvas.polygon([mirror(p) for p in [(9, 158), (47, 165), (59, 181),
                                            (49, 185), (12, 175)]], BLACK)
        for x, y in ((14, 160), (20, 161), (26, 163), (32, 164),
                     (38, 166), (43, 168), (47, 172)):
            canvas.line(mirror((x, y)), mirror((x + 2, y + 1)), EDGE)
        for px, py in ((8, 151), (71, 169), (80, 185)):
            _bolt(canvas, *mirror((px, py)))
    # Small red release and recessed mounting plate on the right tub wall.
    canvas.polygon([(277, 166), (290, 164), (296, 183), (283, 186)], BLACK)
    canvas.polygon([(281, 168), (287, 167), (292, 179), (287, 182)], RED)
    canvas.line((283, 169), (287, 178), 40, 2)
    _bolt(canvas, 280, 165)
    canvas.line((13, 190), (57, 195), EDGE)
    canvas.line((264, 195), (308, 190), EDGE)
    canvas.line((130, 197), (190, 197), BLACK, 3)
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
    """Draw a closed shaped rim, textured side grips and five-knob plate."""
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

    def polygon(vertices, color):
        material = {CARBON: "wheel_carbon", CHARCOAL: "grip", EDGE: "wheel_trim"}.get(color)
        transformed = points(vertices)
        if material:
            _surface(canvas, transformed, material, local_coordinates)
        else:
            canvas.polygon(transformed, color)

    def line(vertices, color, thickness=1):
        canvas.line(*points(vertices), color, thickness)

    # A non-circular lower return connects both hand grips around the controls.
    # It sits behind the control plate and its knobs, with a shaped central
    # scallop rather than a circular or simple oval rim.
    lower_outer = [(-75, 17), (-71, 29), (-60, 36), (-45, 41),
                   (-28, 45), (-17, 46), (-10, 47), (10, 47),
                   (17, 46), (28, 45), (45, 41), (60, 36),
                   (71, 29), (75, 17)]
    lower_inner = [(69, 16), (65, 25), (56, 31), (43, 36),
                   (27, 40), (19, 41), (12, 43), (-12, 43),
                   (-19, 41), (-27, 40), (-43, 36), (-56, 31),
                   (-65, 25), (-69, 16)]
    polygon(lower_outer + lower_inner, CARBON)
    for first, last in zip(lower_outer, lower_outer[1:]):
        line([first, last], WHEEL_EDGE_HIGHLIGHT)
    for first, last in zip(lower_inner, lower_inner[1:]):
        line([first, last], BLACK)
    for first, last in (((-58, 35), (-43, 40)), ((-43, 40), (-27, 44)),
                        ((27, 44), (43, 40)), ((43, 40), (58, 35))):
        line([first, last], SILVER)

    # Dark textured grip sections meet the contrasting carbon/metal bridges.
    for side in (-1, 1):
        def mirror(vertices):
            return [(side * x, y) for x, y in vertices]

        # Exposed metal spokes and deep negative space around each handle.
        polygon(mirror([(45, -10), (62, -15), (65, -8), (46, -4)]), EDGE)
        polygon(mirror([(45, 6), (63, 11), (61, 17), (45, 11)]), EDGE)
        grip = [(59, -27), (68, -29), (74, -24), (76, -11),
                (75, 17), (70, 29), (61, 31), (55, 25),
                (59, 16), (61, 4), (58, -7), (54, -19)]
        polygon(mirror(grip), BLACK)
        polygon(mirror([(60, -25), (67, -26), (71, -22), (72, -10),
                        (71, 15), (67, 25), (62, 27), (59, 23),
                        (63, 15), (65, 3), (62, -8), (58, -18)]), CHARCOAL)
        for first, last in zip(grip[:6], grip[1:7]):
            line(mirror([first, last]), WHEEL_EDGE_HIGHLIGHT)
        for y in range(-21, 24, 3):
            for x in (65, 69):
                line(mirror([(x, y), (x + 1, y + 1)]), CARBON)
        line(mirror([(60, -25), (67, -26)]), SILVER)
        line(mirror([(72, -9), (71, 12)]), EDGE)
        line(mirror([(62, 27), (67, 25)]), EDGE)
        polygon(mirror([(58, -26), (68, -29), (73, -24),
                        (71, -21), (60, -21)]), CARBON)
        line(mirror([(60, -25), (68, -27)]), SILVER)

    plate = [(-48, 8), (48, 8), (45, 26), (33, 41),
             (-23, 43), (-42, 29)]
    polygon(plate, BLACK)
    polygon([(x * .96, y * .97) for x, y in plate], CARBON)
    line([(-44, 10), (43, 10)], EDGE)
    for x, y in CONTROL_CENTERS:
        # All five match the previous unlabelled second control: a dark face
        # and fluted outer grip. There is no white interior mark or dial text.
        surround = [(x + 8 * math.cos(a * math.pi / 4),
                     y + 7 * math.sin(a * math.pi / 4)) for a in range(8)]
        polygon(surround, EDGE)
        polygon([(x + 6 * math.cos(a * math.pi / 4),
                  y + 5 * math.sin(a * math.pi / 4)) for a in range(8)], BLACK)
        for a in range(8):
            theta = a * math.pi / 4
            line([(x + 5 * math.cos(theta), y + 4 * math.sin(theta)),
                  (x + 6 * math.cos(theta), y + 5 * math.sin(theta))], SILVER)
    polygon(UPPER_RIM_OUTER + UPPER_RIM_INNER, CARBON)
    for first, last in zip(UPPER_RIM_OUTER, UPPER_RIM_OUTER[1:]):
        line([first, last], SILVER)
    for first, last in zip(UPPER_RIM_INNER, UPPER_RIM_INNER[1:]):
        line([first, last], BLACK)


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
    for number, (x, y) in zip(f"{speed:03d}", DIGIT_POSITIONS):
        pixels = _digit(int(number))
        for row in range(DIGIT_HEIGHT):
            for column in range(DIGIT_WIDTH):
                color = pixels[row * DIGIT_WIDTH + column]
                if color:
                    panel.point(INSTRUMENT_RECT[0] + x + column,
                                INSTRUMENT_RECT[1] + y + row, color)
    motor_x = INSTRUMENT_RECT[0] + MOTOR_CENTER[0]
    motor_y = INSTRUMENT_RECT[1] + MOTOR_CENTER[1]
    panel.line((motor_x, motor_y), (motor_x + 12, motor_y - 11), RED)
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
    # the curved header, peripheral pillars and fenders without clipping road
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
        secondary[f"dig{number}"] = _shape(DIGIT_WIDTH, DIGIT_HEIGHT, _digit(number))
    stda, stdb = _container(primary), _container(secondary)

    if any(x < 0 or y < 0 or x + DIGIT_WIDTH > width or y + DIGIT_HEIGHT > height
           for x, y in DIGIT_POSITIONS):
        raise ValueError("McMurtry speed digit escapes its instrument rectangle")
    rev_points = bytearray()
    for step in range(128):
        angle = math.pi - step * math.pi / 127
        rev_points.extend((round(MOTOR_CENTER[0] + 11 * math.cos(angle)),
                           round(MOTOR_CENTER[1] - 7 * math.sin(angle))))
    patches = {
        0x01E: bytes(26),
        STEERING_DOTS_OFFSET: steering_dot_positions(STEERING_INDICATOR_CENTER, DASH_TOP),
        0x128: struct.pack("<hhH", 0, 0, 1),
        0x12E: bytes(value for pair in DIGIT_POSITIONS for value in pair) + bytes(202),
        0x1FE: struct.pack("<hhH", *MOTOR_CENTER, 128),
        0x204: bytes(rev_points),
        0x0AE: struct.pack("<H", RED | (RED << 8)),
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
