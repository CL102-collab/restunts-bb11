"""Photo-based low polygon Tesla Roadster Rocket shapes for the TERR custom car.

Coordinates follow Stunts: +Z is the nose, +Y is up, and the first 32
vertices of every car shape are reserved for bounds and animated wheels.
The form follows the supplied October 2026 demonstrator photographs: metallic
grey crowned fenders, a bowed nose, a black fastback canopy with tapered rear
glass, dark aero discs, closed doors, a recessed rear light and four nozzles.
No geometry or shape resources are copied from another car.
"""

from __future__ import annotations

import math
import struct
from collections.abc import Sequence
from dataclasses import dataclass, field

# Match the stock Corvette/NSX dimensional convention using the reported 2026
# width (~2 m) and wheelbase (2825 mm). Height remains a photo-based estimate.
# Stock shapes use different proportions; see docs/tesla-sizing.md.
BODY_SCALE = (45 / 34, 1.20, 1.40)
WHEEL_CENTERS = tuple(tuple(round(c * axis) for c, axis in zip(point, BODY_SCALE))
                      for point in ((-14, 0, 23), (14, 0, 23), (14, 0, -23), (-14, 0, -23)))
WHEEL_SIZE_MULTIPLIER = .75
# Round once in gameplay units, then use exactly the same wheels at 20x in
# the showroom. Radius six also preserves the legacy engine's tyre contact.
WHEEL_RADIUS = round(6 * BODY_SCALE[2] * WHEEL_SIZE_MULTIPLIER)
WHEEL_HALF_WIDTH = round(2 * BODY_SCALE[0] * WHEEL_SIZE_MULTIPLIER)
SHOWROOM_SCALE = 20
# Align tyre bottoms with the stock Corvette's actual wheel-circle bottom.
SHOWROOM_Y_OFFSET = WHEEL_RADIUS * SHOWROOM_SCALE
PAINT_NAMES = ("Demonstrator grey", "Silver", "Graphite", "Deep red", "Midnight blue", "White", "Aubergine")

# Indices address Stunts' material table, not VGA palette indices. The first
# paint is neutral metallic grey: resolved VGA RGB 178, 125 and 97.
# The sixth paint has a white body with black lower trim.
PAINT_TOP = (76, 78, 56, 86, 49, 15, 90)
PAINT_SIDE = (59, 76, 19, 85, 48, 15, 89)
PAINT_EDGE = (57, 59, 20, 84, 88, 0, 88)
BLACK = 0
DARK = 39  # Solid charcoal; material 23 is the patterned version.
PAINT_SILL = (DARK, DARK, DARK, DARK, DARK, BLACK, DARK)
SILVER = 40
# Every window and the continuous glazed canopy are opaque black.
GLASS_BLACK = BLACK
BRAKE_LIGHT = 45  # Renderer replaces material 45 while braking.

Point = tuple[float, float, float]


def _surface_point(point: Point) -> Point:
    """Bow the bumpers and windscreen base in plan, using shared coordinates."""
    x, y, z = point
    if z > 29:
        z += (z - 29) / 7 * (1.3 - 2.6 * (x / 14) ** 2)
    elif z < -30:
        z += (-z - 30) / 7 * (-.7 + 1.7 * (x / 15) ** 2)
    elif 9 < z < 25:
        z += (1 - abs(z - 17) / 8) * 1.4 * max(0, 1 - (x / 14.4) ** 2)
    return x, y, z


@dataclass
class _Shape:
    scale: int
    vertices: list[tuple[int, int, int]] = field(default_factory=list)
    primitives: list[tuple[int, int, tuple[int, ...], tuple[int, ...]]] = field(default_factory=list)
    _lookup: dict[tuple[int, int, int], int] = field(default_factory=dict)

    def point(self, point: Point) -> tuple[int, int, int]:
        return tuple(round(c * axis * self.scale) for c, axis in zip(point, BODY_SCALE))

    def fixed(self, point: Point) -> int:
        """Reserve an unshared control vertex; wheel controls must never merge."""
        self.vertices.append(self.point(point))
        return len(self.vertices) - 1

    def fixed_model(self, point: Point) -> int:
        """Reserve an already calibrated wheel control without body distortion."""
        self.vertices.append(tuple(round(c * self.scale) for c in point))
        return len(self.vertices) - 1

    def vertex(self, point: Point) -> int:
        return self.quantized_vertex(self.point(point))

    def quantized_vertex(self, value: tuple[int, int, int]) -> int:
        if value not in self._lookup:
            self._lookup[value] = len(self.vertices)
            self.vertices.append(value)
        return self._lookup[value]

    def polygon(self, points: Sequence[Point], material: int | Sequence[int], *,
                flags: int = 1, facing: Point | None = None) -> None:
        # Cull quantized duplicate/collinear points, so thin selection-screen
        # trim remains valid after the gameplay model is rounded to integers.
        values = [self.point(_surface_point(p)) for p in points]
        compact = []
        for value in values:
            if not compact or value != compact[-1]:
                compact.append(value)
        if len(compact) > 1 and compact[0] == compact[-1]:
            compact.pop()
        if len(set(compact)) < 3:
            return
        a = compact[0]
        area = False
        for i in range(1, len(compact) - 1):
            u = tuple(compact[i][k] - a[k] for k in range(3))
            v = tuple(compact[i + 1][k] - a[k] for k in range(3))
            if (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]) != (0, 0, 0):
                area = True
                break
        if not area:
            return
        if facing is not None:
            # Orient the quantized polygon, not its floating-point source.
            # The renderer expects a right-hand normal pointing outwards.
            normal = [0, 0, 0]
            for a, b in zip(compact, compact[1:] + compact[:1]):
                normal[0] += (a[1] - b[1]) * (a[2] + b[2])
                normal[1] += (a[2] - b[2]) * (a[0] + b[0])
                normal[2] += (a[0] - b[0]) * (a[1] + b[1])
            direction = sum(a * b for a, b in zip(normal, facing))
            assert direction != 0, (compact, facing)
            if direction < 0:
                compact.reverse()
            # Native culling examines only the initial three vertices. Rotate
            # past a collinear corner so a valid narrow face stays visible.
            for i in range(len(compact)):
                a, b, c = (compact[(i + j) % len(compact)] for j in range(3))
                u = tuple(b[k] - a[k] for k in range(3))
                v = tuple(c[k] - a[k] for k in range(3))
                cross = (u[1] * v[2] - u[2] * v[1],
                         u[2] * v[0] - u[0] * v[2],
                         u[0] * v[1] - u[1] * v[0])
                if sum(a * b for a, b in zip(cross, facing)) > 0:
                    compact = compact[i:] + compact[:i]
                    break
            else:
                raise AssertionError((compact, facing))
            flags &= ~1
        indices = tuple(self.quantized_vertex(p) for p in compact)
        materials = (material,) * len(PAINT_NAMES) if isinstance(material, int) else tuple(material)
        self.primitives.append((len(indices), flags, materials, indices))

    def line(self, points: Sequence[Point], material: int | Sequence[int] = DARK, *, flags: int = 2) -> None:
        indices = tuple(self.vertex(_surface_point(p)) for p in points)
        if indices[0] != indices[1]:
            materials = (material,) * len(PAINT_NAMES) if isinstance(material, int) else tuple(material)
            self.primitives.append((2, flags, materials, indices))

    def disc(self, centre: Point, radius: float) -> None:
        # A stationary zero-thickness native wheel gives each nozzle a smooth
        # perspective-correct ring with only three shared control vertices.
        x, y, z = self.point(_surface_point(centre))
        radius = round(radius * BODY_SCALE[0] * self.scale)
        c = self.quantized_vertex((x, y, z))
        a = self.quantized_vertex((x, y + radius, z))
        b = self.quantized_vertex((x + radius, y, z))
        self.primitives.append((12, 2, (18,) * len(PAINT_NAMES), (c, a, b, c, b, a)))

    def wheel(self, start: int) -> None:
        # Material 118 gives black tread, grey rim and a charcoal aero disc.
        self.primitives.append((12, 0, (118,) * len(PAINT_NAMES), tuple(range(start, start + 6))))

    def encode(self) -> bytes:
        assert len(self.vertices) <= 255 and len(self.primitives) <= 255
        assert all(len(p[2]) == len(PAINT_NAMES) for p in self.primitives)
        assert all(0 <= i < len(self.vertices) for p in self.primitives for i in p[3])
        result = bytearray((len(self.vertices), len(self.primitives), len(PAINT_NAMES), 0))
        for vertex in self.vertices:
            result.extend(struct.pack("<hhh", *vertex))
        # Test every viewing direction using the actual face winding. A full
        # front-facing mask would bypass backface culling even with flag 0.
        # Thin diffuser vanes/debris explicitly retain the two-sided flag.
        result.extend(struct.pack("<I", 0xFFFFFFFF) * len(self.primitives))
        result.extend(struct.pack("<I", 0) * len(self.primitives))
        for kind, flags, materials, indices in self.primitives:
            result.extend((kind, flags, *materials, *indices))
        result.extend(b"\0\0\0")
        return bytes(result)


def _controls(shape: _Shape) -> None:
    # Conservative bounds also cover the tiny rear light/trim offsets.
    for y in (-6, 15):
        for z in (40, -40):
            for x in (-17, 17):
                shape.fixed((x, y, z))
    for index, (x, y, z) in enumerate(WHEEL_CENTERS):
        # Symmetric integer rims keep the native steering pivot exactly at
        # the SIMD wheel centre, without exposing the inner tread at the hood.
        inside = math.copysign(abs(x) - WHEEL_HALF_WIDTH, x)
        outside = math.copysign(abs(x) + WHEEL_HALF_WIDTH, x)
        # Opposite winding of the two rims keeps their visible face consistent
        # with the original car's wheel convention on both sides of the body.
        direction = 1 if x < 0 else -1
        for rx, sign in ((inside, direction), (outside, -direction)):
            shape.fixed_model((rx, y, z))
            shape.fixed_model((rx, y + WHEEL_RADIUS, z))
            shape.fixed_model((rx, y, z + sign * WHEEL_RADIUS))
        shape.wheel(8 + index * 6)


# z, shoulder half-width/height, upper inner edge half-width/height.
# The plan view narrows at both bumpers and at the door waist. The upper
# crease follows the wheel crowns rather than describing a rectangular box.
_STATIONS = (
    (36, 14.0, 3.8, 12.3, 3.2),
    (32, 16.0, 6.1, 11.8, 4.4),
    (23, 17.0, 7.9, 10.7, 6.4),
    (17, 16.4, 7.5, 14.4, 7.3),
    (8, 15.8, 6.8, 14.7, 6.5),
    (-2, 15.6, 7.3, 14.9, 7.0),
    (-12, 16.3, 8.7, 15.2, 8.4),
    (-23, 17.0, 9.4, 14.1, 9.2),
    (-30, 16.4, 8.7, 12.5, 8.2),
    (-37, 14.8, 8.1, 12.4, 8.1),
)
# Keep lower door strips broad so the vertex budget can be spent on circular
# wheel lips; the complete upper shoulder contour is retained below.
_SIDE_Z = (36, 32, 16, -12, -16, -30, -37)


def _section(z: float) -> tuple[float, float, float, float]:
    for front, back in zip(_STATIONS, _STATIONS[1:]):
        if front[0] >= z >= back[0]:
            t = (front[0] - z) / (front[0] - back[0])
            return tuple(front[k] + (back[k] - front[k]) * t for k in range(1, 5))
    raise ValueError(z)


def _side_bottom(side: int, z: float) -> Point:
    w, _, _, _ = _section(z)
    return (side * (w - 1.5), -2, z)


def _arch_panel(shape: _Shape, side: int, front: float, back: float, centre: float) -> None:
    # Nine points follow a true circle down past the axle to the sill. Build
    # the circle in final game units, then undo the body's separate axis
    # scales, so the wheel lips remain round in both showroom and racing LODs.
    # The old five-point cutout mixed a semicircle with the bowed bumper
    # surface, producing unequal corners and a flat, oversized opening.
    radius = 7.0  # One game unit of clearance around the six-unit tyre.
    sill_y = -2.0 * BODY_SCALE[1]
    end_z = math.sqrt(radius ** 2 - sill_y ** 2)
    half = radius / 2
    diagonal = radius * math.sqrt(3) / 2
    # Mirror exact coordinates rather than evaluating sin/cos independently:
    # floating-point drift at half integers otherwise rounds opposite lips
    # differently in the racing mesh (for example +3.5 -> 4, -3.499... -> -3).
    circle = ((end_z, sill_y), (radius, 0), (diagonal, half),
              (half, diagonal), (0, radius), (-half, diagonal),
              (-diagonal, half), (-radius, 0), (-end_z, sill_y))
    wheel_z = round(centre * BODY_SCALE[2])
    arc = [(side * 17, y / BODY_SCALE[1], (wheel_z + z) / BODY_SCALE[2])
           for z, y in circle]
    top_z = (front, *(p[0] for p in _STATIONS if front > p[0] > back), back)
    points = [(side * _section(z)[0], _section(z)[1], z) for z in top_z]
    points += [_side_bottom(side, back), *reversed(arc), _side_bottom(side, front)]
    # Ear-clip the concave side silhouette without introducing any vertices.
    # Coordinates for orientation tests are Z/Y, viewed directly from the side.
    def turn(a: Point, b: Point, c: Point) -> int:
        # Quantize before clipping: a valid showroom triangle can become
        # edge-on in the smaller racing mesh, which the native culler rejects.
        a, b, c = [shape.point(_surface_point(p)) for p in (a, b, c)]
        return (b[2] - a[2]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[2] - b[2])
    area = sum(a[2] * b[1] - b[2] * a[1] for a, b in zip(points, points[1:] + points[:1]))
    sense = 1 if area > 0 else -1
    while len(points) > 3:
        for i, b in enumerate(points):
            a, c = points[i - 1], points[(i + 1) % len(points)]
            if turn(a, b, c) * sense <= 0:
                continue
            if any(p not in (a, b, c)
                   and all(turn(u, v, p) * sense >= 0
                           for u, v in ((a, b), (b, c), (c, a))) for p in points):
                continue
            shape.polygon((a, b, c), PAINT_SIDE, facing=(side, 0, 0))
            points.pop(i)
            break
        else:
            raise AssertionError(points)
    shape.polygon(points, PAINT_SIDE, facing=(side, 0, 0))


def _body(shape: _Shape, simple: bool) -> None:
    for side in (-1, 1):
        for front_z, back_z in zip(_SIDE_Z, _SIDE_Z[1:]):
            if (front_z, back_z) in ((32, 16), (-16, -30)):
                _arch_panel(shape, side, front_z, back_z, 23 if front_z == 32 else -23)
                continue
            fw, fy, _, _ = _section(front_z)
            bw, by, _, _ = _section(back_z)
            a, b = (side * fw, fy, front_z), (side * bw, by, back_z)
            c, d = _side_bottom(side, back_z), _side_bottom(side, front_z)
            if 16 >= front_z and back_z >= -16:
                # A real crease and a darker, inward sloping lower door panel
                # break up the long side. These are facets, not painted lines.
                e, f = (side * (fw - .35), 1.6, front_z), (side * (bw - .35), 1.6, back_z)
                if front_z == 16:
                    # Retain the sculpted shoulder while sharing a continuous
                    # crease and sill beneath it. No hidden cross-strips need
                    # extra vertices that are better used around the wheels.
                    top = [a, *((side * _section(z)[0], _section(z)[1], z) for z in (8, -2)), b]
                    for first, second in zip(top, top[1:]):
                        shape.polygon((e, first, second), PAINT_SIDE, facing=(side, 0, 0))
                    shape.polygon((e, b, f), PAINT_SIDE, facing=(side, 0, 0))
                else:
                    shape.polygon((a, b, f, e), PAINT_SIDE, facing=(side, 0, 0))
                if not simple:
                    if back_z == -12:
                        shape.line((b, f), PAINT_EDGE)
                    if front_z == 16:
                        shape.line((a, e), PAINT_EDGE)
                        shape.line(((side * 16.2, 5.7, 16), (side * 16.0, 5.7, 14)), 14)
                shape.polygon((e, f, c, d), PAINT_EDGE, facing=(side, 0, 0))
                if not simple:
                    if back_z == -12:
                        shape.line((f, c), DARK)
                    if front_z == 16:
                        shape.line((e, d), DARK)
                shape.polygon((d, c, (side * (bw - 1.8), -3.5, back_z),
                               (side * (fw - 1.8), -3.5, front_z)), PAINT_SILL, facing=(side, 0, 0))
            else:
                shape.polygon((a, b, c, d), PAINT_SIDE, facing=(side, 0, 0))
        for front, back in zip(_STATIONS, _STATIONS[1:]):
            z, w, y, iw, iy = front
            bz, bw, by, biw, biy = back
            if bz == 17:
                biw = 10.0
            # Extra cross-section crowns round the front fenders and rear
            # haunches. Shared ridge vertices replace a single broad slab.
            ridge = ((side * (w * .68 + iw * .32), y + .35, z)
                     if z in (32, 23, -23, -30) else (side * w, y, z))
            back_ridge = ((side * (bw * .68 + biw * .32), by + .35, bz)
                          if bz in (32, 23, -23, -30) else (side * bw, by, bz))
            shape.polygon(((side * w, y, z), (side * bw, by, bz),
                           back_ridge, ridge), PAINT_SIDE, facing=(0, 1, 0))
            inner_face = (ridge, back_ridge, (side * biw, biy, bz), (side * iw, iy, z))
            if bz == 17:
                # Stitch both the narrow hood corner and broad windscreen
                # corner into the cowl; a straight four-point face leaves a
                # gap when their shared boundary bows in plan.
                inner_face = (ridge, back_ridge, (side * 14.4, 7.3, 17),
                              (side * biw, biy, bz), (side * iw, iy, z))
            if bz == 17:
                for a, b in zip(inner_face[1:], inner_face[2:]):
                    shape.polygon((inner_face[0], a, b), PAINT_TOP, facing=(0, 1, 0))
            else:
                shape.polygon(inner_face, PAINT_TOP, facing=(0, 1, 0))
            if not simple and z == 36:
                shape.polygon(((side * 12.3, 3.2, 36), (side * 15.8, 6.0, 32),
                               (side * 11.8, 4.45, 32)), DARK, flags=2, facing=(0, 1, 0))
                shape.polygon(((side * 12.4, 3.9, 34.7), (side * 14.8, 5.65, 32.6),
                               (side * 12.4, 4.7, 32.7)), 15, flags=2, facing=(0, 1, 0))
    # Only exposed hood and tail are skinned. A hidden full-length deck used
    # to sort over the canopy and flatten it in the original DOS painter.
    for front, back in zip(_STATIONS, _STATIONS[1:]):
        z, _, _, w, y = front
        bz, _, _, bw, by = back
        if z > 17 or z <= -30:
            if bz == 17:
                bw = 10.0
            for side in (-1, 1):
                cy = y + (.2 if z == 17 else .35)
                bcy = by + (.2 if bz == 17 else .35)
                front_edge = ((side * w, y, z), (0, cy, z))
                if z == 36:
                    front_edge = ((side * w, y, z), (side * w / 2, (y + cy) / 2, z), (0, cy, z))
                shape.polygon((*front_edge, (0, bcy, bz), (side * bw, by, bz)),
                              PAINT_TOP, facing=(0, 1, 0))
                if z == 36:
                    # The shared blade borders two independently sorted
                    # surfaces; attach it to both so neither erases the edge.
                    for a, b in zip(front_edge, front_edge[1:]):
                        shape.line((a, b), 15)
                if z > 17 and not simple:
                    shape.line(((side * w, y, z), (side * bw, by, bz)), PAINT_EDGE)
    # Reuse the sill's lower seam to close the floor, including when seen
    # upside down on a loop. Axle sections stay split for DOS depth sorting.
    floor_z = (36, 16, -12, -16, -37)
    for front_z, back_z in zip(floor_z, floor_z[1:]):
        fw, bw = (_section(z)[0] - 1.8 for z in (front_z, back_z))
        shape.polygon(((-fw, -3.5, front_z), (-bw, -3.5, back_z),
                       (bw, -3.5, back_z), (fw, -3.5, front_z)), BLACK, facing=(0, -1, 0))
    # A bowed four-section nose follows the curved central hood edge. Outer
    # cheeks sweep back into the fenders instead of forming square corners.
    for side in (-1, 1):
        for inner, outer in ((0.0, .5), (.5, 1.0)):
            a = (side * 12.3 * inner, 3.55 - .35 * inner, 36)
            b = (side * 12.3 * outer, 3.55 - .35 * outer, 36)
            c = (side * 12.5 * outer, -2.0, 34.5)
            d = (side * 12.5 * inner, -2.0, 34.5)
            shape.polygon((a, b, c, d), BLACK, facing=(0, 0, 1))
            # A native line stays one pixel thick in every model scale and
            # follows all four facets of the bowed leading edge.
            shape.line((a, b), 15)
        shape.polygon(((side * 14, 3.8, 36), (side * 12.3, 3.2, 36),
                       (side * 12.5, -2.0, 34.5), _side_bottom(side, 36)), PAINT_SIDE, facing=(side, 0, 1))
        shape.polygon(((0, -2.8, 35.3), (side * 13.1, -2.8, 35.3),
                       (side * 12.5, -2.0, 34.5), (0, -2.0, 34.5)), DARK, facing=(0, 1, 0))


# Shared lower glass/body boundaries eliminate overlapping shoulder skins.
# z, lower width/height, roof-edge width/height, roof-centre height.
_CANOPY = (
    (17, 14.4, 7.3, 14.4, 7.3, 7.5),
    (8, 14.7, 6.5, 11.3, 12.1, 12.8),
    (-2, 14.9, 7.0, 10.7, 13.6, 14.4),
    (-12, 15.2, 8.4, 11.0, 13.2, 14.0),
    (-23, 14.1, 9.2, 8.0, 10.6, 11.0),
    (-30, 12.5, 8.2, 6.0, 8.2, 8.5),
)


def _canopy(shape: _Shape, simple: bool) -> None:
    for front, back in zip(_CANOPY, _CANOPY[1:]):
        z, lw, ly, rw, ry, cy = front
        bz, blw, bly, brw, bry, bcy = back
        for side in (-1, 1):
            glass_front = ((side * rw, ry, z), (0, cy, z))
            if z == 17:
                glass_front = ((side * rw, ry, z), (side * 10.0, 7.3, z), (0, cy, z))
            if z == 17:
                # The bowed base is non-planar. Explicit triangles keep the
                # original convex rasterizer from cutting a notch in it.
                anchor = (0, bcy, bz)
                perimeter = ((side * brw, bry, bz), *glass_front)
                for a, b in zip(perimeter, perimeter[1:]):
                    shape.polygon((anchor, a, b), GLASS_BLACK, facing=(0, 1, 0))
            else:
                shape.polygon((*glass_front, (0, bcy, bz), (side * brw, bry, bz)),
                              GLASS_BLACK, facing=(0, 1, 0))
            # Full-depth swept side glass exposes the canopy's actual arch;
            # the rear sail meets it in a point above the rear wheel.
            a, b = (side * lw, ly, z), (side * rw, ry, z)
            c, d = (side * brw, bry, bz), (side * blw, bly, bz)
            if z == -12:
                # A pointed rear side-window ends inside the grey sail.
                # Shared triangles close the quarter panel without a broad
                # black strip continuing beneath the rear roof shoulder.
                tip = (side * 13.8, 10.0, -20)
                shape.polygon((a, b, tip), GLASS_BLACK, facing=(side, 0, 0))
                for first, second in ((b, c), (c, d), (d, a)):
                    shape.polygon((first, second, tip), PAINT_TOP, facing=(side, 0, 0))
            else:
                shape.polygon((a, b, c, d), GLASS_BLACK if z >= -12 else PAINT_TOP,
                              facing=(side, 0, 0))
            if z in (17, 8, -2):
                # Attached immediately to the matching side glass, the thin
                # metallic rail follows the A pillar and arched roof edge.
                # Its upward component survives scale-1 rounding of the lip.
                shape.polygon(((side * rw, ry, z), (side * brw, bry, bz),
                               (side * (brw + .65), bry - .35, bz),
                               (side * (rw + .65), ry - .35, z)),
                              PAINT_TOP, flags=2, facing=(side, 1, 0))


def _rear(shape: _Shape, simple: bool) -> None:
    # Narrowed corners and the sloping lower bumper continue the side's
    # tumble-under into a recessed diffuser, under a flush red light blade.
    shape.polygon(((-14.8, 8.1, -37), (14.8, 8.1, -37),
                   (13.0, -3.2, -35.5), (-13.0, -3.2, -35.5)), PAINT_SIDE, facing=(0, 0, -1))
    shape.polygon(((-14.4, 7.7, -37.1), (14.4, 7.7, -37.1),
                   (14.0, 5.3, -36.8), (-14.0, 5.3, -36.8)), BLACK, flags=2, facing=(0, 0, -1))
    shape.polygon(((-14.2, 6.4, -37.15), (14.2, 6.4, -37.15),
                   (14.2, 7.1, -37.15), (-14.2, 7.1, -37.15)), BRAKE_LIGHT, flags=2, facing=(0, 0, -1))
    shape.polygon(((-11.5, 1.8, -36.8), (11.5, 1.8, -36.8),
                   (12.9, -3.3, -35.7), (-12.9, -3.3, -35.7)), BLACK, flags=2, facing=(0, 0, -1))
    shape.polygon(((-2.5, 2.4, -36.7), (2.5, 2.4, -36.7),
                   (2.5, 4.6, -36.9), (-2.5, 4.6, -36.9)), SILVER, flags=2, facing=(0, 0, -1))
    for x in (-8.4, -2.8, 2.8, 8.4):
        shape.disc((x, -.2, -36.4), 1.35)
    for side in (-1, 1):
        shape.polygon(((side * 14.0, 5.3, -36.8), (side * 12.9, -3.3, -35.7),
                       (side * 11.5, 1.8, -36.8)), DARK, flags=2, facing=(0, 0, -1))
    for side in (-1, 1):
        shape.polygon(((side * 14.8, 8.1, -37), _side_bottom(side, -37),
                       (side * 13, -3.2, -35.5)), PAINT_EDGE, facing=(side, 0, 0))
    for x in (-10.5, 0.0, 10.5):
        shape.polygon(((x, -3.2, -35.5), (x, -3.8, -36), (x, -3.5, -30)), DARK)

    shape.polygon(((-15.8, 8.8, -34.8), (15.8, 8.8, -34.8),
                   (15.8, 8.5, -36.8), (-15.8, 8.5, -36.8)), PAINT_TOP)
    for side in (-1, 1):
        shape.polygon(((side * 15.8, 8.8, -34.8), (side * 15.8, 8.5, -36.8),
                       (side * 16.0, 4.8, -36.2)), PAINT_EDGE)


def _car(scale: int, *, simple: bool = False) -> _Shape:
    shape = _Shape(scale)
    _controls(shape)
    _body(shape, simple)
    _canopy(shape, simple)
    _rear(shape, simple)
    return shape


def _showroom_car() -> _Shape:
    shape = _car(SHOWROOM_SCALE)
    shape.vertices = [(x, y + SHOWROOM_Y_OFFSET, z) for x, y, z in shape.vertices]
    return shape


def _debris(index: int) -> bytes:
    shape = _Shape(1)
    pieces = (
        (((-7, 0, -5), (8, 0, -5), (6, 1, 6), (-5, 1, 7)), PAINT_TOP),
        (((-6, 0, -5), (7, 1, -3), (1, 0, 7)), PAINT_SIDE),
        (((-5, 0, -4), (4, 0, -4), (5, 1, 5), (-3, 1, 4)), GLASS_BLACK),
        (((-4, 0, -3), (5, 0, -3), (3, 1, 4), (-4, 1, 4)), BLACK),
    )
    points, material = pieces[index]
    shape.polygon(points, material)
    return shape.encode()


def _container(resources: Sequence[tuple[str, bytes]]) -> bytes:
    data = bytearray()
    offsets = []
    for name, resource in resources:
        assert len(name) == 4
        offsets.append(len(data))
        data.extend(resource)
    size = 6 + len(resources) * 8 + len(data)
    return (struct.pack("<IH", size, len(resources))
            + b"".join(name.encode("ascii") for name, _ in resources)
            + struct.pack("<" + "I" * len(resources), *offsets) + data)


def build_models() -> bytes:
    """Return a complete uncompressed STTERR.3SH resource container."""
    resources = [("car0", _showroom_car().encode()), ("car1", _car(1).encode()),
                 ("car2", _car(1, simple=True).encode())]
    resources.extend((f"exp{index}", _debris(index)) for index in range(4))
    return _container(resources)


if __name__ == "__main__":
    for name, shape in (("car0", _showroom_car()), ("car1", _car(1)), ("car2", _car(1, simple=True))):
        encoded = shape.encode()
        print(f"{name}: {len(shape.vertices)} vertices, {len(shape.primitives)} primitives, {len(encoded)} bytes")
    print(f"STTERR.3SH: {len(build_models())} bytes")
