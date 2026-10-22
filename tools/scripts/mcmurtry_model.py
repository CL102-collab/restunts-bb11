"""Original photo reconstruction at 2026 McMurtry Speirling PURE dimensions.

Manufacturer front, side and rear renders and the user's production-car
detail photographs guide the shapes, not mesh data from another car.
Current production dimensions calibrate the body; wheels use their own
integer contact geometry to keep the engine's six-unit tyre contact plane.
Coordinates use Stunts' +Z nose, +Y up convention.  Forty-four millimetres
per unit matches the stock Corvette/NSX body scale; the showroom is 20x.
The first eight vertices are bounds and the next 24 are animated wheels.
"""

from __future__ import annotations

import math
import struct
from dataclasses import dataclass, field
from collections.abc import Sequence


MM_PER_UNIT = 44.0
REAL_DIMENSIONS_MM = {"length": 3815, "width": 1795, "height": 1056,
                      "wheelbase": 2200}
LENGTH, WIDTH, HEIGHT = (REAL_DIMENSIONS_MM[key] / MM_PER_UNIT
                        for key in ("length", "width", "height"))
MODEL_SCALE = (WIDTH / 36, HEIGHT / 23, LENGTH / 78)
# Common integer radius approximates the production 680/710 mm tyres.
# Centre Y=2 keeps an eight-unit tyre on the engine's fixed Y=-6 plane.
WHEEL_RADIUS = 8
WHEEL_HALF_WIDTH = 3
WHEEL_CENTERS = ((-16, 2, 25), (16, 2, 25),
                 (16, 2, -25), (-16, 2, -25))
_AUTHORED_WHEEL_CENTERS = ((-14, 1, 23), (14, 1, 23),
                          (14, 1, -23), (-14, 1, -23))
VENT_WIDTH = 5 * MODEL_SCALE[0]
VENT_LENGTH = 9 * MODEL_SCALE[2]
VENT_DEPTH = .8 * MODEL_SCALE[1]
VENT_FLOOR_OFFSET = .2 * MODEL_SCALE[1]
VENTS = tuple({"wheel": wheel, "center_x": x * MODEL_SCALE[0],
               "center_z": z * MODEL_SCALE[2]}
              for wheel, (x, _, z) in enumerate(_AUTHORED_WHEEL_CENTERS))
SHOWROOM_SCALE = 20
SHOWROOM_Y_OFFSET = 6 * SHOWROOM_SCALE
PAINT_NAMES = ("Turquoise", "Purple", "Orange", "Yellow",
               "Blue", "White", "Light gray")

# These address Stunts' material table, not the VGA palette.  Keep the
# carbon skirt, windows, wheel vents and wing dark in every paint scheme.
PAINT_TOP = (66, 75, 99, 62, 51, 15, 78)
PAINT_SIDE = (65, 74, 6, 61, 50, 79, 77)
PAINT_EDGE = (64, 73, 108, 60, 49, 78, 76)
BLACK, CARBON, GREY, SILVER, WHITE, BRAKE_LIGHT = 0, 39, 19, 40, 15, 45
HEADLIGHT_GLASS = 88
Point = tuple[float, float, float]


def _calibrate(point):
    x, y, z = point
    return x * MODEL_SCALE[0], (y + 6) * MODEL_SCALE[1] - 6, z * MODEL_SCALE[2]


def _uncalibrate(point):
    x, y, z = point
    return x / MODEL_SCALE[0], (y + 6) / MODEL_SCALE[1] - 6, z / MODEL_SCALE[2]


def _normal(points):
    normal = [0, 0, 0]
    for a, b in zip(points, points[1:] + points[:1]):
        normal[0] += (a[1] - b[1]) * (a[2] + b[2])
        normal[1] += (a[2] - b[2]) * (a[0] + b[0])
        normal[2] += (a[0] - b[0]) * (a[1] + b[1])
    return normal


@dataclass
class _Shape:
    scale: int
    vertices: list[tuple[int, int, int]] = field(default_factory=list)
    primitives: list[tuple[int, int, tuple[int, ...], tuple[int, ...]]] = field(default_factory=list)
    _lookup: dict[tuple[int, int, int], int] = field(default_factory=dict)
    _wheel_roofs: dict[int, list[tuple[int, int, tuple[int, ...], tuple[int, ...]]]] = field(default_factory=dict)
    _wheel_vents: dict[int, list[tuple[int, int, tuple[int, ...], tuple[int, ...]]]] = field(default_factory=dict)
    _wheel_lenses: dict[int, list[tuple[int, int, tuple[int, ...], tuple[int, ...]]]] = field(default_factory=dict)

    def point(self, point):
        return tuple(round(c * self.scale) for c in _calibrate(point))

    def fixed(self, point):
        self.vertices.append(self.point(point))

    def fixed_model(self, point):
        """Keep rebuilt wheel controls in final game units, without scaling twice."""
        self.vertices.append(tuple(round(c * self.scale) for c in point))

    def vertex(self, point):
        if point not in self._lookup:
            self._lookup[point] = len(self.vertices)
            self.vertices.append(point)
        return self._lookup[point]

    def polygon(self, points: Sequence[Point], material, *, facing=None, flags=1):
        points = [self.point(p) for p in points]
        compact = []
        for point in points:
            if not compact or point != compact[-1]:
                compact.append(point)
        if len(compact) > 1 and compact[0] == compact[-1]:
            compact.pop()
        if len(set(compact)) < 3 or _normal(compact) == [0, 0, 0]:
            return
        if facing is not None:
            direction = sum(a * b for a, b in zip(_normal(compact), facing))
            if direction == 0:
                return
            if direction < 0:
                compact.reverse()
            # Native culling uses the first three vertices. Start on a
            # non-collinear outward corner even when narrow trim rounds.
            for index in range(len(compact)):
                triangle = [compact[(index + j) % len(compact)] for j in range(3)]
                if sum(a * b for a, b in zip(_normal(triangle), facing)) > 0:
                    compact = compact[index:] + compact[:index]
                    break
            else:
                return
            flags &= ~1
        materials = (material,) * 7 if isinstance(material, int) else tuple(material)
        self.primitives.append((len(compact), flags, materials,
                                tuple(self.vertex(p) for p in compact)))

    def line(self, points, material=CARBON):
        points = [self.point(p) for p in points]
        if points[0] != points[1]:
            materials = (material,) * 7 if isinstance(material, int) else tuple(material)
            self.primitives.append((2, 2, materials, tuple(self.vertex(p) for p in points)))

    def encode(self):
        if len(self.vertices) > 255 or len(self.primitives) > 255:
            raise ValueError(f"Shape exceeds native byte limits: {len(self.vertices)} vertices, {len(self.primitives)} primitives")
        data = bytearray((len(self.vertices), len(self.primitives), 7, 0))
        for point in self.vertices:
            data.extend(struct.pack("<3h", *point))
        data.extend(struct.pack("<I", 0xFFFFFFFF) * len(self.primitives))
        data.extend(struct.pack("<I", 0) * len(self.primitives))
        for kind, flags, materials, indices in self.primitives:
            data.extend((kind, flags, *materials, *indices))
        return bytes(data + b"\0\0\0")


def _controls(shape):
    for y in (-6, 17):
        for z in (39, -39):
            for x in (-18, 18):
                shape.fixed((x, y, z))
    for index, (x, y, z) in enumerate(WHEEL_CENTERS):
        inside = math.copysign(abs(x) - WHEEL_HALF_WIDTH, x)
        outside = math.copysign(abs(x) + WHEEL_HALF_WIDTH, x)
        direction = 1 if x < 0 else -1
        for rx, sign in ((inside, direction), (outside, -direction)):
            shape.fixed_model((rx, y, z))
            shape.fixed_model((rx, y + WHEEL_RADIUS, z))
            shape.fixed_model((rx, y, z + sign * WHEEL_RADIUS))
        # Native wheel material 118 is a black racing tyre with grey rim.
        shape.primitives.append((12, 0, (118,) * 7,
                                 tuple(range(8 + index * 6, 14 + index * 6))))


# z, outer half width, fender crown, inner edge half width and height.
# Fenders rise independently above the lower central hood/waist; this is
# the characteristic compact fan-car silhouette, not a scaled saloon.
_SECTIONS = ((39, 14, 5.5, 9, 3.5), (32, 17, 7, 10, 4),
             (23, 18, 11, 9, 5.5), (14, 17, 7.5, 7.5, 6),
             (8, 16, 6, 7.5, 6), (-9, 16, 5.5, 7.5, 6),
             (-15, 17, 8, 8.5, 6), (-23, 18, 11, 10, 6.5),
             (-31, 17, 8, 10, 5.5), (-39, 16, 8.5, 9, 8))

# Inner shoulders make each wheel pod a cover, rather than a ramp which
# cuts through the inner half of the tyre.  The low central bonnet stays
# at the section's inner_y.  The crowns are lifted in the authoring mesh
# to clear the production car's rebuilt eight-unit-radius racing tyres.
_FENDER_CROWNS = {32: (11, 7), 23: (10, 11), 14: (10, 8),
                  -15: (10, 8), -23: (10, 11), -31: (11, 8)}


def _section(z):
    for a, b in zip(_SECTIONS, _SECTIONS[1:]):
        if a[0] >= z >= b[0]:
            t = (a[0] - z) / (a[0] - b[0])
            return tuple(a[i] + (b[i] - a[i]) * t for i in range(1, 5))
    raise ValueError(z)


def _side_point(side, z, y):
    return side * _section(z)[0], y, z


def _fender_roofs(shape, side):
    """Closed upper wheel pods, shared by showroom and both racing LODs."""
    rings = []
    for z, width, y, inner_width, inner_y in _SECTIONS:
        crown_width, crown_y = _FENDER_CROWNS.get(z, (inner_width, inner_y))
        rings.append(((side * width, y, z),
                      (side * crown_width, crown_y, z),
                      (side * inner_width, inner_y, z)))
    for front, rear in zip(rings, rings[1:]):
        # Explicit triangles avoid treating a warped quad as one plane in
        # native rasterizers and make quantized tyre clearance checkable.
        outer_a, crown_a, inner_a = front
        outer_b, crown_b, inner_b = rear
        first_primitive = len(shape.primitives)
        for triangle in ((outer_a, outer_b, crown_b),
                         (outer_a, crown_b, crown_a)):
            shape.polygon(triangle, PAINT_TOP, facing=(0, 1, 0))
        for triangle in ((crown_a, crown_b, inner_b),
                         (crown_a, inner_b, inner_a)):
            shape.polygon(triangle, PAINT_TOP, facing=(-side, 1, 0))
        for centre_z, wheel in ((23, 0 if side < 0 else 1),
                                (-23, 3 if side < 0 else 2)):
            if (outer_a[2] >= centre_z >= outer_b[2] or
                    centre_z == 23 and outer_a[2] == 39):
                # A native wheel is one atom sorted at its rim centre;
                # independently sorted roof triangles can therefore draw
                # behind its upper tread despite physically enclosing it.
                # Flag 2 attaches the cover to its own wheel's sorting
                # group, so the wheel is painted before its closed roof.
                # Bit 0 stays clear: outward face culling is retained.
                roofs = shape.primitives[first_primitive:]
                del shape.primitives[first_primitive:]
                shape._wheel_roofs.setdefault(wheel, []).extend(
                    (kind, flags | 2, materials, indices)
                    for kind, flags, materials, indices in roofs)
                break


def _authored_fender_surface_height(x, z):
    """Height of the authored outer fender roof at a vent sample point."""
    x = abs(x)
    for front, rear in zip(_SECTIONS, _SECTIONS[1:]):
        if not front[0] >= z >= rear[0]:
            continue
        rings = []
        for station_z, width, y, inner_width, inner_y in (front, rear):
            crown_width, crown_y = _FENDER_CROWNS.get(station_z, (inner_width, inner_y))
            rings.append(((width, y, station_z), (crown_width, crown_y, station_z)))
        (outer_a, crown_a), (outer_b, crown_b) = rings
        for a, b, c in ((outer_a, outer_b, crown_b), (outer_a, crown_b, crown_a)):
            ux, uz, vx, vz = b[0] - a[0], b[2] - a[2], c[0] - a[0], c[2] - a[2]
            determinant = ux * vz - uz * vx
            if abs(determinant) < 1e-9:
                continue
            px, pz = x - a[0], z - a[2]
            u = (px * vz - pz * vx) / determinant
            v = (ux * pz - uz * px) / determinant
            if min(u, v, 1 - u - v) >= -1e-8:
                return a[1] + u * (b[1] - a[1]) + v * (c[1] - a[1])
    raise ValueError("Vent point outside the closed fender roof")


def fender_surface_height(x, z):
    """Return the production-calibrated roof height in final game units."""
    authored = _uncalibrate((x, 0, z))
    y = _authored_fender_surface_height(authored[0], authored[2])
    return _calibrate((authored[0], y, authored[2]))[1]


def _authored_vent_geometry(wheel):
    """Return the bounded lip, recess floor and cross-rib before calibration.

    The six-sided lip follows the photographed rounded/chamfered openings.
    An inset bowl centre and a grey cross-rib suggest the dark cavity and
    hardware visible in the user's close-up. The sealed roof remains
    below this small relief, so no uncontrolled native wheel raster can
    escape into the opening or the neighbouring painted body.
    """
    cx, _, cz = _AUTHORED_WHEEL_CENTERS[wheel]
    width, length = 2.5, 4.5
    lip_plan = ((-width, -length + 1.5), (-width, length - 1.5),
                (0, length), (width, length - 1.5),
                (width, -length + 1.5), (0, -length))
    floor_plan = ((0, 0),)

    def point(dx, dz, offset):
        x, z = cx + dx, cz + dz
        return x, _authored_fender_surface_height(x, z) + offset, z

    lip = tuple(point(x, z, 1.0) for x, z in lip_plan)
    floor = tuple(point(x, z, .2) for x, z in floor_plan)
    return {"lip": lip, "floor": floor, "cross_rib": (lip[1], lip[3])}


def vent_geometry(wheel):
    """Return the bounded recess geometry in final production game units."""
    return {name: tuple(_calibrate(point) for point in points)
            for name, points in _authored_vent_geometry(wheel).items()}


def _fender_vents(shape, side):
    for wheel, (x, _, _) in enumerate(WHEEL_CENTERS):
        if (x < 0) != (side < 0):
            continue
        geometry = _authored_vent_geometry(wheel)
        lip, floor = geometry["lip"], geometry["floor"]
        first_primitive = len(shape.primitives)
        # Six-sided bowl and cross vane preserve the photographed opening
        # while freeing detail vertices for the production front/rear ducts.
        for index, (first, last) in enumerate(zip(lip, lip[1:] + lip[:1])):
            material = PAINT_EDGE if index == 2 else BLACK
            shape.polygon((first, last, floor[0]), material,
                          facing=(0, 1, 0), flags=2)
        for index in (0, 2, 4):
            shape.line((lip[index], lip[(index + 1) % len(lip)]), PAINT_TOP)
        shape.line(geometry["cross_rib"], GREY)
        shape._wheel_vents[wheel] = shape.primitives[first_primitive:]
        del shape.primitives[first_primitive:]


def _triangulate_side(shape, side, points, material):
    """Ear-clip the side outline, including both open wheel cutouts."""
    def turn(a, b, c):
        a, b, c = [shape.point(p) for p in (a, b, c)]
        return (b[2] - a[2]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[2] - b[2])
    area = sum(a[2] * b[1] - b[2] * a[1]
               for a, b in zip(points, points[1:] + points[:1]))
    sense = 1 if area > 0 else -1
    while len(points) > 3:
        for index, b in enumerate(points):
            a, c = points[index - 1], points[(index + 1) % len(points)]
            if turn(a, b, c) * sense <= 0:
                continue
            if any(p not in (a, b, c) and
                   all(turn(u, v, p) * sense >= 0
                       for u, v in ((a, b), (b, c), (c, a))) for p in points):
                continue
            shape.polygon((a, b, c), material, facing=(side, 0, 0))
            points.pop(index)
            break
        else:
            raise ValueError("Cannot triangulate a quantized wheel arch")
    shape.polygon(points, material, facing=(side, 0, 0))


def _body(shape, simple):
    for side in (-1, 1):
        outer = [(side * width, y, z) for z, width, y, _, _ in _SECTIONS]
        # Rebuild the arches in final game units rather than stretching
        # old circles into ellipses. Radius nine leaves one unit around
        # the eight-unit production tyre, centred on its physical axle.
        outline = outer + [_side_point(side, -39, -4)]
        for centre in (-25, 25):
            radius, axle_y, sill = 9, 2, _calibrate((0, -4, 0))[1]
            end = math.sqrt(radius * radius - (sill - axle_y) ** 2)
            half = radius / 2
            diagonal = half * math.sqrt(3)
            arc = ((-end, sill), (-radius, axle_y), (-diagonal, axle_y + half),
                   (-half, axle_y + diagonal), (0, axle_y + radius),
                   (half, axle_y + diagonal), (diagonal, axle_y + half),
                   (radius, axle_y), (end, sill))
            for dz, y in arc:
                authored = _uncalibrate((0, y, centre + dz))
                outline.append(_side_point(side, authored[2], authored[1]))
        outline.append(_side_point(side, 39, -4))
        _triangulate_side(shape, side, outline, PAINT_SIDE)

        _fender_roofs(shape, side)
        _fender_vents(shape, side)
        # The very low fan skirt is deliberately inset behind the tyres.
        shape.polygon(((side * 16, -4, 15), (side * 16, -4, -15),
                       (side * 15, -5, -15), (side * 15, -5, 15)),
                      CARBON, facing=(side, 0, 0))
        # Sculpted side inlet: one broad black trapezoid behind each front
        # wheel, with a blue leading buttress and a pale sill edge.
        shape.polygon(((side * 16.15, 4.8, 11), (side * 16.15, 4.0, 5),
                       (side * 16.15, -3.7, 8), (side * 16.15, -3.7, 12)),
                      CARBON, facing=(side, 0, 0))

    # Low central bonnet and the rear deck remain beneath the canopy.
    for front, rear in zip(_SECTIONS, _SECTIONS[1:]):
        if rear[0] < -23:
            continue  # The production rear ducts occupy this former solid deck.
        z0, _, _, w0, y0 = front
        z1, _, _, w1, y1 = rear
        shape.polygon(((-w0, y0, z0), (w0, y0, z0),
                       (w1, y1, z1), (-w1, y1, z1)), PAINT_TOP, facing=(0, 1, 0))
    # A closed floor is visible when the adhesion car drives inverted.
    shape.polygon(((-15, -5, 15), (-15, -5, -15),
                   (15, -5, -15), (15, -5, 15)), CARBON, facing=(0, -1, 0))


def _canopy(shape, simple):
    # A tiny, centrally seated bubble, with a blue roof and dark wraparound
    # glass. Calibration places its roof at 1056/44 above tyre contact.
    # z, half-width at lower shoulder, shoulder y, upper half-width, roof y
    sections = ((14, 7.5, 6, 5.2, 7.5),
                (8, 7.5, 6, 5.3, 15.5),
                (-9, 7.5, 6, 4.8, 17),
                (-20, 4.4, 6.2, 4.4, 6.2))
    for index, facing, material in ((0, (0, 0, 1), BLACK),
                                    (-1, (0, 0, -1), PAINT_EDGE)):
        z, width, y, top_width, top_y = sections[index]
        shape.polygon(((-width, y, z), (width, y, z),
                       (top_width, top_y, z), (-top_width, top_y, z)),
                      material, facing=facing)
    for index, (a, b) in enumerate(zip(sections, sections[1:])):
        z0, w0, y0, top_w0, top_y0 = a
        z1, w1, y1, top_w1, top_y1 = b
        material = BLACK if index == 0 else PAINT_TOP
        shape.polygon(((-top_w0, top_y0, z0), (top_w0, top_y0, z0),
                       (top_w1, top_y1, z1), (-top_w1, top_y1, z1)),
                      material, facing=(0, 1, 0))
        for side in (-1, 1):
            shape.polygon(((side * w0, y0, z0), (side * top_w0, top_y0, z0),
                           (side * top_w1, top_y1, z1), (side * w1, y1, z1)),
                          BLACK if index < 3 else PAINT_EDGE, facing=(side, 0, 0))
    for side in (-1, 1):
        if not simple:
            # Blue roof rim/A pillar and the door's lower frame.
            shape.line(((side * 7.5, 6, 14), (side * 5.3, 15.5, 8)), PAINT_TOP)
            shape.line(((side * 7.5, 6, -9), (side * 4.4, 6.2, -20)), PAINT_EDGE)
            # Small stemmed mirrors clear the glass and sit well inside the
            # outer fenders, as in the launch front view.
            shape.line(((side * 7.5, 6, 8), (side * 9, 10, 12)), CARBON)
            shape.polygon(((side * 9, 10, 12), (side * 12, 10, 12),
                           (side * 11, 11.5, 10)),
                          PAINT_TOP, facing=(0, 1, 1))


def _front(shape):
    """Production nose: upper fender lenses and two levels of deep inlets."""
    sides = {}
    for side in (-1, 1):
        # Dark oval-like lenses follow the curved fender skin, rather than
        # masquerading as white strips on the lower mouth's outside edge.
        lens = []
        for x, z in ((11.4, 36.4), (14.6, 36.4),
                     (14.7, 35.0), (11.7, 35.0)):
            lens.append((side * x, _authored_fender_surface_height(x, z) + .65, z))
        first_primitive = len(shape.primitives)
        shape.polygon(lens, HEADLIGHT_GLASS, facing=(0, 1, 1), flags=2)
        shape.line((lens[0], lens[3]), SILVER)
        wheel = 0 if side < 0 else 1
        shape._wheel_lenses[wheel] = shape.primitives[first_primitive:]
        del shape.primitives[first_primitive:]

        outer_top = (side * 14, 5.5, 39)
        outer_bottom = (side * 14, -4, 39)
        hood = (side * 9, 3.5, 39)
        upper_inner = (side * .8, 2.3, 39)
        upper_outer = (side * 11.5, 2.3, 39)
        back_outer = (side * 10.5, .2, 36)
        back_inner = (side * 1.5, .2, 36)
        lower_outer = (side * 11.5, -3.3, 39)
        lower_inner = (side * 2, -3.3, 39)
        sides[side] = (upper_inner, upper_outer, lower_outer, lower_inner)
        shape.polygon((upper_inner, upper_outer, back_outer, back_inner),
                      BLACK, facing=(0, 0, 1))
        shape.polygon((back_inner, back_outer, lower_outer, lower_inner),
                      BLACK, facing=(0, 0, 1))
        # Close the inward wall beside the centre divider; without this
        # triangle an oblique camera can see the road through the inlet.
        shape.polygon((upper_inner, back_inner, lower_inner),
                      BLACK, facing=(side, 0, 0))
        shape.polygon((outer_top, upper_outer, lower_outer, outer_bottom),
                      PAINT_SIDE, facing=(0, 0, 1))
        shape.polygon((upper_outer, back_outer, lower_outer),
                      CARBON, facing=(0, 0, 1))
        shape.line((back_inner, back_outer), GREY)
        shape.polygon((outer_top, hood, upper_outer), PAINT_TOP, facing=(0, 0, 1))
    left, right = sides[-1], sides[1]
    shape.polygon((left[0], right[0], right[3], left[3]), PAINT_EDGE, facing=(0, 0, 1))
    shape.polygon(((-9, 3.5, 39), (9, 3.5, 39), right[1], left[1]),
                  PAINT_TOP, facing=(0, 0, 1))
    shape.polygon(((-14, -4, 39), (14, -4, 39), right[2], left[2]),
                  CARBON, facing=(0, 0, 1))
    shape.line(((-14, -4, 39), (14, -4, 39)), GREY)
    shape.polygon(((-14, -4, 39), (-15, -5, 15),
                   (15, -5, 15), (14, -4, 39)), CARBON, facing=(0, -1, 0))


def _rear(shape, simple):
    """Scalloped production cowl, hollow upper ducts and separate diffuser."""
    centre_top, centre_low = (0, 6.5, -39), (0, -2, -38)
    mouths = {}
    diffusers = {}
    for side in (-1, 1):
        outer_top, outer_bottom = (side * 16, 8.5, -39), (side * 16, -4, -39)
        deck = (side * 9, 8, -39)
        a, b = (side * 1.2, 6.2, -39), (side * 11, 7, -39)
        c, d = (side * 12, 3.2, -39), (side * 1.2, 2.8, -39)
        h, e, g, f = ((side * 4, 6, -34), (side * 10, 6, -34),
                       (side * 10, 3.5, -34), (side * 4, 3.3, -34))
        mouths[side] = (a, b, c, d)
        # Dark ceilings, floors and backcaps surround a narrow painted
        # sidewall, keeping each upper duct visibly recessed in native VGA.
        shape.polygon((a, b, e, h), BLACK, facing=(0, 0, -1))
        shape.polygon((b, c, g, e), PAINT_EDGE, facing=(0, 0, -1))
        shape.polygon((d, c, g, f), BLACK, facing=(0, 0, -1))
        shape.polygon((a, h, f, d), BLACK, facing=(0, 0, -1))
        shape.polygon((h, e, g, f), BLACK, facing=(0, 0, -1))
        shape.polygon((outer_top, deck, b), PAINT_TOP, facing=(0, 0, -1))
        shape.polygon((deck, a, b), PAINT_SIDE, facing=(0, 0, -1))
        # Tall outer carbon lamp recesses carry four small red lenses;
        # there are no invented exposed turbine faces on the real rear.
        shape.polygon((b, outer_top, outer_bottom, c), BLACK, facing=(0, 0, -1))
        for y in (6.5, .5):
            lamp = tuple(_uncalibrate((side * (15.5 + dx), y + dy,
                                       -39 * MODEL_SCALE[2]))
                         for dx, dy in ((-.5, -.5), (-.5, .5), (.5, .5), (.5, -.5)))
            shape.polygon(lamp, BRAKE_LIGHT, facing=(0, 0, -1), flags=2)
        diffuser_lip = (side * 10, -4.5, -39)
        diffuser_back = g
        diffusers[side] = (diffuser_lip, diffuser_back)
        shape.polygon((c, outer_bottom, diffuser_lip), CARBON, facing=(0, 0, -1))
        shape.polygon((c, diffuser_lip, d), BLACK, facing=(0, 0, -1))
        shape.line((c, d), CARBON)
        shape.polygon((centre_low, diffuser_lip, diffuser_back), BLACK, facing=(0, 1, -1))
        shape.polygon((d, centre_low, f), BLACK, facing=(0, 0, -1))
    left, right = mouths[-1], mouths[1]
    # Close the two central gaps between the sloping channel floors. They
    # must be dark body surfaces, not accidental views of the grey road.
    shape.polygon((centre_low, diffusers[-1][0], diffusers[1][0]),
                  BLACK, facing=(0, 0, -1))
    shape.polygon((centre_low, diffusers[-1][1], diffusers[1][1]),
                  BLACK, facing=(0, 0, -1))
    shape.polygon((left[0], centre_top, right[0]), PAINT_TOP, facing=(0, 0, -1))
    shape.polygon(((-4.4, 6.2, -20), (4.4, 6.2, -20), right[0], left[0]),
                  PAINT_SIDE, facing=(0, 1, 0))
    shape.polygon((left[0], right[0], right[3], centre_low, left[3]),
                  CARBON, facing=(0, 0, -1))
    shape.polygon(((-15, -5, -15), (-10, -4.5, -39),
                   (10, -4.5, -39), (15, -5, -15)), CARBON, facing=(0, -1, 0))

    # Swan-neck struts hook back onto a thin upper wing with endplates.
    for side in (-1, 1):
        shape.polygon(((side * 4.4, 6.2, -20), (side * 6, 10.5, -25),
                       (side * 6, 16, -32), (side * 5, 15, -35)),
                      CARBON)
    shape.polygon(((-17, 16, -29), (17, 16, -29),
                   (17, 16, -37), (-17, 16, -37)), CARBON)
    shape.line(((-17, 16, -37), (17, 16, -37)), GREY)
    for side in (-1, 1):
        shape.polygon(((side * 17, 13, -29), (side * 17, 16, -29),
                       (side * 17, 17, -38), (side * 17, 13, -38)),
                      CARBON)


def _car(scale=1, *, simple=False):
    shape = _Shape(scale)
    _controls(shape)
    _body(shape, simple)
    _canopy(shape, simple)
    _front(shape)
    _rear(shape, simple)
    wheels, body = shape.primitives[:4], shape.primitives[4:]
    shape.primitives = []
    for index, wheel in enumerate(wheels):
        shape.primitives.append(wheel)
        shape.primitives.extend(shape._wheel_roofs.get(index, ()))
        shape.primitives.extend(shape._wheel_vents.get(index, ()))
        shape.primitives.extend(shape._wheel_lenses.get(index, ()))
    shape.primitives.extend(body)
    return shape


def _showroom_car():
    shape = _car(SHOWROOM_SCALE)
    shape.vertices = [(x, y + SHOWROOM_Y_OFFSET, z) for x, y, z in shape.vertices]
    return shape


def _debris(index):
    shape = _Shape(1)
    points, material = (
        (((-6, 0, -4), (7, 0, -4), (5, 2, 6), (-4, 1, 5)), PAINT_TOP),
        (((-5, 0, -3), (6, 1, -3), (3, 0, 5), (-3, 0, 4)), PAINT_SIDE),
        (((-4, 0, -4), (5, 1, -3), (2, 1, 5), (-3, 0, 4)), BLACK),
        (((-7, 0, -2), (7, 0, -2), (7, 1, 2), (-7, 1, 2)), CARBON),
    )[index]
    shape.polygon(points, material)
    return shape.encode()


def _container(resources):
    payload = bytearray()
    offsets = []
    for _, data in resources:
        offsets.append(len(payload))
        payload.extend(data)
    count = len(resources)
    return (struct.pack("<IH", 6 + count * 8 + len(payload), count)
            + b"".join(name.encode("ascii") for name, _ in resources)
            + struct.pack(f"<{count}I", *offsets) + payload)


def build_models():
    """Build car0 showroom, car1/2 racing and four original debris shapes."""
    resources = [("car0", _showroom_car().encode()),
                 ("car1", _car().encode()), ("car2", _car(simple=True).encode())]
    resources.extend((f"exp{i}", _debris(i)) for i in range(4))
    return _container(resources)


def export_obj(paint=0):
    """Export the authored showroom mesh and explicit tyres in metres.

    OBJ has ordinary faces, unlike the native Stunts wheel primitive. Its
    ground is Y=0, +Z is forwards, and all native control/bounds vertices
    are omitted. This editable source is generated from our own geometry.
    """
    if not 0 <= paint < len(PAINT_NAMES):
        raise ValueError("Invalid paint index")
    shape = _showroom_car()
    factor = MM_PER_UNIT / (1000 * SHOWROOM_SCALE)
    vertices = []
    faces = []
    lines = []
    lookup = {}

    def vertex(point):
        point = tuple(point)
        if point not in lookup:
            lookup[point] = len(vertices) + 1
            vertices.append(point)
        return lookup[point]

    for kind, flags, materials, indices in shape.primitives:
        if kind == 12:
            continue
        target = lines if kind == 2 else faces
        points = [tuple(c * factor for c in shape.vertices[index]) for index in indices]
        target.append((materials[paint], [vertex(p) for p in points]))
        if flags & 1 and kind != 2:
            # Preserve genuinely two-sided surfaces in ordinary OBJ viewers.
            target.append((materials[paint], [vertex(p) for p in reversed(points)]))

    # Twenty-four radial samples give an editable version of the engine's
    # analytic racing tyres, with a dark tyre wall and silver rim.
    for index, (x, y, z) in enumerate(WHEEL_CENTERS):
        rings = []
        for rx in (x - WHEEL_HALF_WIDTH, x + WHEEL_HALF_WIDTH):
            rings.append([vertex((rx * MM_PER_UNIT / 1000,
                                  (y + 6 + WHEEL_RADIUS * math.sin(angle)) * MM_PER_UNIT / 1000,
                                  (z + WHEEL_RADIUS * math.cos(angle)) * MM_PER_UNIT / 1000))
                          for angle in (step * math.tau / 24 for step in range(24))])
        for step in range(24):
            following = (step + 1) % 24
            faces.append((BLACK, [rings[0][step], rings[1][step],
                                  rings[1][following], rings[0][following]]))
        for side, ring in enumerate(rings):
            rx = x - WHEEL_HALF_WIDTH if side == 0 else x + WHEEL_HALF_WIDTH
            centre = vertex((rx * MM_PER_UNIT / 1000,
                             (y + 6) * MM_PER_UNIT / 1000,
                             z * MM_PER_UNIT / 1000))
            for step in range(24):
                triangle = [centre, ring[step], ring[(step + 1) % 24]]
                faces.append((SILVER, triangle if side == 0 else list(reversed(triangle))))

    result = ["# Original McMurtry Speirling PURE (2026 production dimensions).",
              "# Metres. Y up, Z forward. Ground at Y=0.",
              f"# Paint: {PAINT_NAMES[paint]}", "mtllib MCMU.mtl", "o McMurtry_Speirling_PURE"]
    result.extend("v " + " ".join(f"{c:.6f}" for c in point) for point in vertices)
    for primitive, items in (("f", faces), ("l", lines)):
        previous = None
        for material, indices in items:
            if material != previous:
                result.append(f"usemtl material_{material}")
                previous = material
            result.append(primitive + " " + " ".join(map(str, indices)))
    return "\n".join(result) + "\n"


def export_mtl(paint=0):
    """Portable approximate RGB counterparts of the selected Stunts paint."""
    if not 0 <= paint < len(PAINT_NAMES):
        raise ValueError("Invalid paint index")
    colours = ((0, .8, .8), (.5, 0, .8), (.67, .33, .03),
               (1, .965, .125), (0, 0, .82), (.87, .87, .87),
               (.76, .76, .76))
    body = colours[paint]
    materials = {BLACK: (.008, .009, .012), CARBON: (.06, .065, .075),
                 GREY: (.28, .29, .31), SILVER: (.65, .68, .72),
                 WHITE: (.95, .96, .98), BRAKE_LIGHT: (.65, .005, .008),
                 HEADLIGHT_GLASS: (.01, .025, .07)}
    for shade, multiplier in ((PAINT_TOP[paint], 1.2),
                              (PAINT_SIDE[paint], 1.0), (PAINT_EDGE[paint], .75)):
        materials[shade] = tuple(min(1, c * multiplier) for c in body)
    result = ["# Original model; material names retain their native Stunts IDs."]
    for material, colour in sorted(materials.items()):
        result.extend((f"newmtl material_{material}",
                       "Kd " + " ".join(f"{c:.4f}" for c in colour),
                       "Ka 0.04 0.04 0.04", "Ks 0.15 0.15 0.15", "Ns 32", ""))
    return "\n".join(result)


if __name__ == "__main__":
    for name, shape in (("car0", _showroom_car()), ("car1", _car()),
                        ("car2", _car(simple=True))):
        print(name, len(shape.vertices), "vertices,", len(shape.primitives),
              "primitives,", len(shape.encode()), "bytes")
