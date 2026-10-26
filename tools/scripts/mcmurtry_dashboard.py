#!/usr/bin/env python3
"""Original indexed-palette cockpit for the MCMU McMurtry Speirling car.

The wheel uses a continuous shaped carbon/metal rim with dark textured grips,
following the production cockpit photograph. The vent pods and sculpted front
fenders use the requested turquoise finish. Silver vents and panel details
follow the user's three cockpit photographs. This is original pixel artwork,
not a photograph conversion.
The live LCD remains upright while the surrounding wheel steers: a native
dashboard approximation. The five identical, unlabelled fluted controls and
unlit shift-lamp lenses are artwork, not live telemetry. Optional ``dast`` and
``dasm`` sprites mask the fender silhouettes onto the road above the dashboard.

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
    steering_dot_positions, steering_dot_shapes, STEERING_DOTS_OFFSET,
)


WIDTH, HEIGHT = 320, 200
DASH_TOP = 125
DASH_OPAQUE_TOP = DASH_TOP
TOP_OVERLAY_RECT = (0, 103, WIDTH, DASH_TOP - 103)
INSTRUMENT_RECT = (116, 142, 88, 29)
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
DIGIT_POSITIONS = ((6, 11), (16, 11), (26, 11))
BLACK, CHARCOAL, CARBON, EDGE = 0, 31, 29, 27
WHITE, SILVER, RED = 19, 22, 39
TRANSPARENT = 255
POD_HIGHLIGHT, POD_FACE, POD_SHADOW = 121, 124, 126
FENDER_HIGHLIGHT, FENDER_TOP, FENDER_SIDE = 121, 123, 125
MOTOR_CENTER = (69, 25)
DASHBOARD_REFERENCES = [
    {"url": "https://www.topgear.com/car-reviews/speirling/first-drive",
     "view": "September 2024 first-hand prototype review and photo gallery; single seat, single gear, fan and motor-power controls."},
    {"url": "https://www.topgear.com/sites/default/files/2024/09/DSC00094_3.jpg",
     "view": "Prototype cockpit: central rectangular LCD wheel, five rotary knobs, textured hand grips, bare carbon tub and red release handle."},
    {"url": "https://www.topgear.com/sites/default/files/2024/09/DSC00356_3.jpg",
     "view": "Detached prototype wheel close-up: eleven shift-lamp lenses, McMurtry/record-holder branding, TC/FAN/PWR/EPAS controls and textured grips."},
    {"file": "1-1000005914.jpg", "view": "User photograph: prototype carbon cockpit, curved upper trim, release handle, seams and fasteners."},
    {"file": "2-1000005913.jpg", "view": "User photograph: prototype LCD and five fluted rotary knobs; user requests all five match the unlabelled second knob."},
    {"file": "3-1000005912.jpg", "view": "User photograph: continuous shaped steering-wheel rim, circular silver vents, padded carbon sills and turquoise front-fender crowns with upper openings. Vent-pod trim is turquoise as requested."},
]


def _carbon(canvas, points, light=30, dark=31):
    """Clipped two-over-two weave, kept subtle in the actual VGA palette."""
    mask = Canvas()
    mask.polygon(points, 1)
    for offset, pixel in enumerate(mask.pixels):
        if pixel:
            x, y = offset % WIDTH, offset // WIDTH
            canvas.pixels[offset] = light if ((x + y) // 2 + y // 3) % 4 < 2 else dark


def _bolt(canvas, x, y):
    canvas.rect(x - 1, y - 1, x + 1, y + 1, SILVER)
    canvas.point(x, y, BLACK)


def _lcd(canvas):
    """Stationary live screen and actual prototype's shaped display surround."""
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


def _ellipse(canvas, x, y, rx, ry, color):
    canvas.polygon([(x + rx * math.cos(step * math.pi / 8),
                     y + ry * math.sin(step * math.pi / 8)) for step in range(16)], color)


def _vent(canvas, x, y):
    """Concentric silver vent bezel and dark adjustable inner vanes."""
    _ellipse(canvas, x, y, 10, 8, BLACK)
    _ellipse(canvas, x, y, 9, 7, SILVER)
    _ellipse(canvas, x, y, 7, 6, WHITE)
    _ellipse(canvas, x, y, 6, 5, CARBON)
    _ellipse(canvas, x, y, 5, 4, SILVER)
    _ellipse(canvas, x, y, 4, 3, BLACK)
    canvas.line((x - 3, y - 1), (x + 3, y - 1), EDGE)
    canvas.line((x - 3, y + 1), (x + 3, y + 1), EDGE)
    canvas.point(x - 5, y - 4, WHITE)


def _fenders(canvas):
    """Static exterior body visible through the bottom of the windscreen.

    The close crowns and upper openings follow the production cockpit photo.
    Their turquoise finish matches the default paint; native bitmap dashboards
    have no per-paint recolouring, so this detail stays turquoise for all paints.
    """
    for side in (-1, 1):
        def mirror(points):
            return [(160 + side * (x - 160), y) for x, y in points]

        canvas.polygon(mirror([(0, 125), (11, 118), (29, 109), (46, 106),
                               (64, 110), (78, 117), (96, 125)]), FENDER_SIDE)
        canvas.polygon(mirror([(10, 124), (20, 117), (34, 111), (47, 109),
                               (61, 112), (74, 119), (84, 125)]), FENDER_TOP)
        canvas.line(*mirror([(20, 117), (35, 111)]), FENDER_HIGHLIGHT)
        canvas.line(*mirror([(35, 111), (47, 109)]), FENDER_HIGHLIGHT)
        canvas.polygon(mirror([(35, 110), (57, 110), (64, 115),
                               (42, 117), (32, 114)]), FENDER_HIGHLIGHT)
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
    crown = [(x, 118 + round(7 * ((x - half_width) / half_width) ** 2))
             for x in range(WIDTH)]
    canvas.polygon(crown + [(WIDTH - 1, 132), (0, 132)], CHARCOAL)
    for x, y in crown:
        canvas.point(x, y, BLACK)
        canvas.point(x, y + 1, CARBON)
        canvas.point(x, y + 2, CHARCOAL)
        # A faint seam follows the same curve, avoiding a horizontal band.
        canvas.point(x, y + 5, CARBON)


def _vent_pod(canvas, side):
    def mirror(points):
        return [(160 + side * (x - 160), y) for x, y in points]

    outline = [(8, 129), (34, 126), (65, 130), (88, 138), (103, 155),
               (98, 162), (81, 163), (60, 156), (35, 148), (14, 145)]
    canvas.polygon(mirror(outline), POD_SHADOW)
    canvas.polygon(mirror([(12, 130), (35, 128), (63, 132), (84, 140),
                           (97, 154), (94, 158), (82, 158), (62, 152),
                           (37, 144), (17, 142)]), POD_FACE)
    canvas.line(*mirror([(16, 130), (35, 128)]), POD_HIGHLIGHT)
    canvas.line(*mirror([(35, 128), (63, 132)]), POD_HIGHLIGHT)
    canvas.line(*mirror([(63, 132), (84, 140)]), POD_HIGHLIGHT)
    # Short edge seams follow the shaped turquoise trim pod.
    for x, y in ((22, 141), (29, 143), (36, 145), (44, 147), (52, 150),
                 (61, 153), (70, 156), (80, 159)):
        canvas.line(*mirror([(x, y), (x + 2, y + 1)]), POD_HIGHLIGHT)
    for x, y in ((44, 139), (74, 150)):
        actual_x, actual_y = mirror([(x, y)])[0]
        _vent(canvas, actual_x, actual_y)


def _panel():
    canvas = Canvas(color=TRANSPARENT)
    canvas.rect(0, DASH_OPAQUE_TOP, WIDTH - 1, HEIGHT - 1, CHARCOAL)
    _fenders(canvas)
    _cowl(canvas)
    # Turquoise vent pods sit above the padded carbon side plinths.
    _carbon(canvas, [(0, 146), (59, 149), (92, 161), (108, 199), (0, 199)])
    _carbon(canvas, [(319, 146), (261, 149), (228, 161), (212, 199), (319, 199)])
    canvas.polygon([(80, 145), (103, 143), (118, 199), (95, 199)], BLACK)
    canvas.polygon([(240, 145), (217, 143), (202, 199), (225, 199)], BLACK)
    _carbon(canvas, [(112, 164), (208, 164), (221, 199), (99, 199)])
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
        _vent_pod(canvas, side)
    # Small red release and recessed mounting plate, below the right vent pod.
    canvas.polygon([(277, 166), (290, 164), (296, 183), (283, 186)], BLACK)
    canvas.polygon([(281, 168), (287, 167), (292, 179), (287, 182)], RED)
    canvas.line((283, 169), (287, 178), 40, 2)
    _bolt(canvas, 280, 165)
    canvas.line((13, 190), (57, 195), EDGE)
    canvas.line((264, 195), (308, 190), EDGE)
    canvas.line((130, 197), (190, 197), BLACK, 3)
    _lcd(canvas)
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
    # Keep the actual instrument face identical in every wheel sprite. The
    # game's dashboard_update_instruments redraws it after wheel restoration,
    # including when steering changes while the speed/RPM are unchanged.
    lcd_rect = (111, 135, 99, 38)
    lcd_pixels = canvas.crop(lcd_rect)

    def points(vertices):
        transformed = transform_wheel_points(vertices, direction)
        x, y, width, height = WHEEL_RECT
        if any(not x <= round(px) < x + width or
               not y <= round(py) < y + height for px, py in transformed):
            raise ValueError("Rigid wheel geometry escapes its redraw rectangle")
        return transformed

    def polygon(vertices, color):
        canvas.polygon(points(vertices), color)

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
        line([first, last], EDGE)
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
        if min(round(py) for _, py in points(surround)) < lcd_rect[1] + lcd_rect[3]:
            raise ValueError("Rotary knob overlaps the restored LCD")
        polygon(surround, EDGE)
        polygon([(x + 6 * math.cos(a * math.pi / 4),
                  y + 5 * math.sin(a * math.pi / 4)) for a in range(8)], BLACK)
        for a in range(8):
            theta = a * math.pi / 4
            line([(x + 5 * math.cos(theta), y + 4 * math.sin(theta)),
                  (x + 6 * math.cos(theta), y + 5 * math.sin(theta))], SILVER)
    # The LCD is anchored for native rendering rather than a fake tilted image.
    x, y, width, height = lcd_rect
    for row in range(height):
        start = (y + row) * WIDTH + x
        canvas.pixels[start:start + width] = lcd_pixels[row * width:(row + 1) * width]
    # Restore the upright LCD first, then draw the complete rigid bridge over
    # it. Layout clearance keeps every bridge pixel outside the live display,
    # so native instrument redraw cannot erase part of the rotating rim.
    live_pixels = canvas.crop(INSTRUMENT_RECT)
    polygon(UPPER_RIM_OUTER + UPPER_RIM_INNER, CARBON)
    for first, last in zip(UPPER_RIM_OUTER, UPPER_RIM_OUTER[1:]):
        line([first, last], SILVER)
    for first, last in zip(UPPER_RIM_INNER, UPPER_RIM_INNER[1:]):
        line([first, last], BLACK)
    if canvas.crop(INSTRUMENT_RECT) != live_pixels:
        raise ValueError("Rigid upper rim overlaps the live instrument rectangle")


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
    # A dash's Y is also the native windshield viewport boundary. The existing
    # optional top/mask pair draws peripheral fenders without clipping all road
    # rendering at their highest point. AND mask preserves outside pixels; OR
    # artwork supplies only the opaque exterior silhouette.
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
    primary["ins2"] = _shape(width, height, canvas.crop(INSTRUMENT_RECT), x, y)
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
    _preview_steering_dot(overlay, STEERING_INDICATOR_CENTER)
    for offset, color in enumerate(overlay.pixels):
        if color != TRANSPARENT:
            canvas.pixels[offset] = color
    for number, (x, y) in zip(f"{speed:03d}", DIGIT_POSITIONS):
        pixels = _digit(int(number))
        for row in range(DIGIT_HEIGHT):
            for column in range(DIGIT_WIDTH):
                color = pixels[row * DIGIT_WIDTH + column]
                if color:
                    canvas.point(INSTRUMENT_RECT[0] + x + column,
                                 INSTRUMENT_RECT[1] + y + row, color)
    motor_x = INSTRUMENT_RECT[0] + MOTOR_CENTER[0]
    motor_y = INSTRUMENT_RECT[1] + MOTOR_CENTER[1]
    canvas.line((motor_x, motor_y), (motor_x + 12, motor_y - 11), RED)
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
