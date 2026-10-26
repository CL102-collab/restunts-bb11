"""Native retopology of the user-supplied McMurtry Speirling GLB.

The downloaded mesh is the exterior ground truth, including its prototype
nose, long canopy and rear wing. Profiles are sampled after all GLB node
transforms, then scaled uniformly to 3815 mm overall length. The thin
canopy/cowl fin is omitted. No geometry from the previous photo reconstruction
is retained. Integer wheel controls accommodate the native animated-wheel/contact
convention.
Measured cross-sections are retained in mcmurtry_source_profile.json.
See MCMU.json and THIRD-PARTY-NOTICES.txt for source attribution and changes.
"""

from __future__ import annotations

import math
import struct
from dataclasses import dataclass, field
from collections.abc import Sequence


MM_PER_UNIT = 44.0
REAL_DIMENSIONS_MM = {"length": 3815, "width": 1795, "height": 1056,
                      "wheelbase": 2200}
LENGTH = REAL_DIMENSIONS_MM["length"] / MM_PER_UNIT
# Uniform scaling preserves the downloaded mesh's aspect ratio. These
# measured visual extents deliberately differ from factory specifications.
SOURCE_BOUNDS = ((-.007688376135638886, -.00002053976013049806, -.017174607131452687),
                 (.00768837673168532, .010808423515966005, .01717460593935982))
SOURCE_SCALE = LENGTH / (SOURCE_BOUNDS[1][2] - SOURCE_BOUNDS[0][2])
WIDTH = (SOURCE_BOUNDS[1][0] - SOURCE_BOUNDS[0][0]) * SOURCE_SCALE
HEIGHT = (SOURCE_BOUNDS[1][1] - SOURCE_BOUNDS[0][1]) * SOURCE_SCALE
SHOWROOM_SCALE = 20
CONTACT_Y = -6
SHOWROOM_Y_OFFSET = -CONTACT_Y * SHOWROOM_SCALE
NATIVE_LIMIT = 255
PAINT_COUNT = 7
POINT_PRIMITIVE = 1
LINE_PRIMITIVE = 2
SPHERE_PRIMITIVE = 11
WHEEL_PRIMITIVE = 12
TWO_SIDED_FLAG = 1
ATTACHED_FLAG = 2
ALL_DIRECTIONS_MASK = 0xFFFFFFFF
WHEEL_CONTROL_START = 8
WHEEL_CONTROL_COUNT = 6
WHEEL_RADIUS = 8
WHEEL_HALF_WIDTH = 3
WHEEL_CENTERS = ((-16, 2, 27), (16, 2, 27),
                 (16, 2, -24), (-16, 2, -24))
WHEEL_MATERIAL = 118
ARCH_RADIUS = WHEEL_RADIUS + 2.0
ARCH_SKIN_THICKNESS = 1.2
ARCH_ANGLE_STEP = math.pi / 6
SIDE_BULGE_Y = 4.0
SIDE_BULGE_WIDTH = .25
SIDE_SILL_INSET = 1.0
FRONT_WHEEL_COUNT = 2
EXTRA_OUTER_STATIONS = (32, 22, -19, -29)
BODY_SILL_Y = -4.8
PAINT_NAMES = ("Turquoise", "Purple", "Orange", "Yellow",
               "Blue", "White", "Light gray")
PAINT_TOP = (66, 75, 99, 62, 51, 15, 78)
PAINT_SIDE = (65, 74, 6, 61, 50, 79, 77)
PAINT_EDGE = (64, 73, 108, 60, 49, 78, 76)
BLACK, CARBON, GREY, SILVER, WHITE, BRAKE_LIGHT = 0, 39, 19, 40, 15, 45
Point = tuple[float, float, float]


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

    def point(self, point):
        return tuple(round(c * self.scale) for c in point)

    def fixed(self, point):
        self.vertices.append(self.point(point))

    def vertex(self, point):
        if point not in self._lookup:
            self._lookup[point] = len(self.vertices)
            self.vertices.append(point)
        return self._lookup[point]

    def polygon(self, points: Sequence[Point], material, *, facing=None, flags=TWO_SIDED_FLAG):
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
            flags &= ~TWO_SIDED_FLAG
        materials = (material,) * PAINT_COUNT if isinstance(material, int) else tuple(material)
        self.primitives.append((len(compact), flags, materials,
                                tuple(self.vertex(p) for p in compact)))

    def line(self, points, material=CARBON, *, minimum=False):
        authored = points
        points = [self.point(p) for p in points]
        if minimum and points[0] == points[1]:
            axis = max(range(3), key=lambda i: abs(authored[1][i] - authored[0][i]))
            step = 1 if authored[1][axis] > authored[0][axis] else -1
            points[1] = tuple(c + (step if i == axis else 0)
                              for i, c in enumerate(points[1]))
        if points[0] != points[1]:
            materials = (material,) * PAINT_COUNT if isinstance(material, int) else tuple(material)
            self.primitives.append((LINE_PRIMITIVE, ATTACHED_FLAG, materials, tuple(self.vertex(p) for p in points)))

    def dot(self, point, material):
        self.primitives.append((POINT_PRIMITIVE, ATTACHED_FLAG, (material,) * PAINT_COUNT,
                                (self.vertex(self.point(point)),)))

    def sphere(self, center, diameter_point, material):
        """Use the native round primitive; control-point distance is its diameter."""
        indices = tuple(self.vertex(self.point(p)) for p in (center, diameter_point))
        self.primitives.append((SPHERE_PRIMITIVE, ATTACHED_FLAG,
                                (material,) * PAINT_COUNT, indices))

    def encode(self):
        if len(self.vertices) > NATIVE_LIMIT or len(self.primitives) > NATIVE_LIMIT:
            raise ValueError(f"Shape exceeds native byte limits: {len(self.vertices)} vertices, {len(self.primitives)} primitives")
        data = bytearray((len(self.vertices), len(self.primitives), PAINT_COUNT, 0))
        for point in self.vertices:
            data.extend(struct.pack("<3h", *point))
        data.extend(struct.pack("<I", ALL_DIRECTIONS_MASK) * len(self.primitives))
        data.extend(struct.pack("<I", 0) * len(self.primitives))
        for kind, flags, materials, indices in self.primitives:
            data.extend((kind, flags, *materials, *indices))
        return bytes(data + b"\0\0\0")


def _controls(shape):
    for y in (CONTACT_Y, HEIGHT + CONTACT_Y):
        for z in (LENGTH / 2, -LENGTH / 2):
            for x in (-WIDTH / 2, WIDTH / 2):
                shape.fixed((x, y, z))
    for wheel, (x, y, z) in enumerate(WHEEL_CENTERS):
        inside = math.copysign(abs(x) - WHEEL_HALF_WIDTH, x)
        outside = math.copysign(abs(x) + WHEEL_HALF_WIDTH, x)
        direction = 1 if x < 0 else -1
        for rx, sign in ((inside, direction), (outside, -direction)):
            shape.fixed((rx, y, z))
            shape.fixed((rx, y + WHEEL_RADIUS, z))
            shape.fixed((rx, y, z + sign * WHEEL_RADIUS))
        first = WHEEL_CONTROL_START + wheel * WHEEL_CONTROL_COUNT
        shape.primitives.append((WHEEL_PRIMITIVE, 0, (WHEEL_MATERIAL,) * PAINT_COUNT,
                                 tuple(range(first, first + WHEEL_CONTROL_COUNT))))


def _fit_car_bounds(shape):
    """Enclose quantized skin details while retaining the source envelope."""
    bounds = shape.vertices[:WHEEL_CONTROL_START]
    rendered = shape.vertices[WHEEL_CONTROL_START:]
    minimum = [min(point[axis] for point in bounds) for axis in range(3)]
    lower = [min(point[axis] for point in rendered) for axis in range(3)]
    upper = [max(point[axis] for point in rendered) for axis in range(3)]
    shape.vertices[:WHEEL_CONTROL_START] = [
        tuple(min(point[axis], lower[axis]) if point[axis] == minimum[axis]
              else max(point[axis], upper[axis]) for axis in range(3))
        for point in bounds]


# Z, outer lip X/Y, outer shoulder X/Y, inner crown X/Y, channel X/Y.
# The distinct shoulder rolls over the wheel pods. The enlarged openings
# deliberately take precedence over exact source dimensions at the tyres.
# The front crown stays nearly level toward the cockpit, as in the source;
# its small lift above that profile leaves a lip over the quantized arch.
_SECTIONS = ((43.35, 13.0, 2.7, 11.0, 3.0, 11.0, 3.0, 7.0, 2.1),
             (36, 19.05, 7.8, 16.0, 8.8, 13.5, 8.8, 8.5, 5.9),
             (27, 19.05, 12.6, 17.1, 12.6, 12.8, 12.0, 9.5, 7.0),
             (16, 19.05, 8.5, 17.3, 12.0, 13.0, 11.8, 10.0, 7.5),
             (-12, 19.05, 8.5, 17.4, 11.3, 13.0, 10.5, 10.0, 3.8),
             (-24, 19.05, 13.1, 17.3, 14.0, 12.8, 14.5, 10.0, 11.0),
             (-34, 19.05, 6.0, 17.0, 11.0, 13.0, 11.4, 10.0, 10.6),
             (-39, 17.6, 8.5, 16.0, 10.3, 13.0, 9.8, 8.0, 9.2))

REAR_BODY_Z = _SECTIONS[-1][0]
REAR_DIFFUSER_START_Z = -35.0
REAR_DIFFUSER_EXIT_Y = .4
REAR_DIFFUSER_HALF_WIDTH = 9.0
REAR_INSET_LOWER_Y = 6.0
REAR_INSET_RECESS = 1.0
REAR_LAMP_X = 14.39
REAR_LAMP_UPPER_Y = 8.33
REAR_LAMP_LOWER_Y = -1.5
REAR_LAMP_FACE_OFFSET = .05
REAR_LAMP_DIAMETER = 2.0
REAR_LAMP_SIZE_SCALE = .9
REAR_LAMP_INNER_RATIO = .6
LAMP_EXPORT_SEGMENTS = 16
SIDE_DUCT_REAR_Z = 12.0
SIDE_DUCT_CENTER_Z = 14.0
# Keep the widest part at axle height so the lower return facets retain
# nonzero outward normals after racing coordinates round to whole units.
SIDE_DUCT_CENTER_Y = WHEEL_CENTERS[0][1]
SIDE_DUCT_DEPTH = 1.5
SIDE_DUCT_FRONT_Z = next(section[0] for section in _SECTIONS
                         if section[0] < WHEEL_CENTERS[0][2] - ARCH_RADIUS)


def _section(z):
    for front, rear in zip(_SECTIONS, _SECTIONS[1:]):
        if front[0] >= z >= rear[0]:
            t = (front[0] - z) / (front[0] - rear[0])
            return tuple(a + (b - a) * t for a, b in zip(front[1:], rear[1:]))
    raise ValueError(f"Body station outside source shell: {z}")


def _outer_profile():
    profile = [(z, outer, y) for z, outer, y, *_ in _SECTIONS]
    for z in EXTRA_OUTER_STATIONS:
        outer, y, *_ = _section(z)
        for _, cy, cz in WHEEL_CENTERS:
            dz = z - cz
            if abs(dz) < ARCH_RADIUS:
                y = max(y, cy + math.sqrt(ARCH_RADIUS ** 2 - dz ** 2) + ARCH_SKIN_THICKNESS)
        profile.append((z, outer, y))
    return sorted(profile, reverse=True)


def _outer_height(z):
    profile = _outer_profile()
    for front, rear in zip(profile, profile[1:]):
        if front[0] >= z >= rear[0]:
            t = (front[0] - z) / (front[0] - rear[0])
            return front[2] + (rear[2] - front[2]) * t
    raise ValueError(f"Outer fender station outside shell: {z}")


def _side_point(side, z, y):
    outer = _section(z)[0]
    if y < SIDE_BULGE_Y:
        fraction = min(1.0, (SIDE_BULGE_Y - y) / (SIDE_BULGE_Y - BODY_SILL_Y))
        width = outer - SIDE_SILL_INSET * fraction ** 2
    else:
        fraction = min(1.0, (y - SIDE_BULGE_Y) / (_outer_height(z) - SIDE_BULGE_Y))
        width = outer + SIDE_BULGE_WIDTH * (1 - fraction ** 2)
    return side * width, y, z


def _roof_rings(side):
    return [((side * outer, edge_y, z), (side * shoulder, shoulder_y, z),
             (side * crown, crown_y, z), (side * inner, inner_y, z))
            for z, outer, edge_y, shoulder, shoulder_y, crown, crown_y, inner, inner_y in _SECTIONS]


def _shoulder_faces(side):
    rings = _roof_rings(side)
    for front, rear in zip(rings, rings[1:]):
        outer = [(side * x, y, z) for z, x, y in _outer_profile()
                 if rear[0][2] <= z <= front[0][2]]
        # The extra outer stations bow around the arch without spending
        # another entire transverse ring at every circular-arc sample.
        for a, b in zip(outer, outer[1:]):
            yield (front[0][2] + rear[0][2]) / 2, (front[1], a, b), PAINT_SIDE
        yield (front[0][2] + rear[0][2]) / 2, (front[1], outer[-1], rear[1]), PAINT_SIDE
        for strip, material in ((1, PAINT_TOP), (2, PAINT_EDGE)):
            yield (front[0][2] + rear[0][2]) / 2, (
                front[strip], rear[strip], rear[strip + 1], front[strip + 1]), material


def _roof_triangles():
    for _, face, _ in _shoulder_faces(1):
        for index in range(1, len(face) - 1):
            yield face[0], face[index], face[index + 1]


def fender_surface_height(x, z):
    """Interpolate the new source-derived shell for bounded crown openings."""
    for a, b, c in _roof_triangles():
        ux, uz, vx, vz = b[0] - a[0], b[2] - a[2], c[0] - a[0], c[2] - a[2]
        determinant = ux * vz - uz * vx
        if abs(determinant) < 1e-9:
            continue
        px, pz = abs(x) - a[0], z - a[2]
        u = (px * vz - pz * vx) / determinant
        v = (ux * pz - uz * px) / determinant
        if min(u, v, 1 - u - v) >= -1e-8:
            return a[1] + u * (b[1] - a[1]) + v * (c[1] - a[1])
    raise ValueError("Crown opening outside the source shell")


def _attach(shape, wheel, start, target):
    faces = shape.primitives[start:]
    del shape.primitives[start:]
    target.setdefault(wheel, []).extend(
        (kind, flags | ATTACHED_FLAG, paints, indices) for kind, flags, paints, indices in faces)


def _shoulders(shape, side):
    for z, face, material in _shoulder_faces(side):
        start = len(shape.primitives)
        shape.polygon(face, material, facing=(0, 1, 0))
        wheel = (0 if side < 0 else 1) if z > 0 else (3 if side < 0 else 2)
        _attach(shape, wheel, start, shape._wheel_roofs)


# Wheel-crown slots follow the downloaded shell. Their bounded dark faces
# approximate the exposed tread while the solid cover controls native sorting.
VENTS = tuple({"wheel": wheel, "center_x": x, "center_z": z}
              for wheel, x, z in ((0, -15, 27), (1, 15, 27),
                                  (2, 15, -25), (3, -15, -25)))


def vent_geometry(wheel):
    side = -1 if wheel in (0, 3) else 1
    plan = (((12.4, 31), (17.2, 30), (17.2, 22), (15.3, 22))
            if wheel < 2 else
            ((13.3, -21), (17.0, -20.3), (17.1, -28.2), (13.0, -29)))
    lip = tuple((side * x, fender_surface_height(x, z) + .25, z) for x, z in plan)
    return {"lip": lip}


def _openings(shape, side):
    for vent in VENTS:
        if (vent["center_x"] < 0) != (side < 0):
            continue
        geometry = vent_geometry(vent["wheel"])
        lip = geometry["lip"]
        start = len(shape.primitives)
        shape.polygon(lip, BLACK, facing=(0, 1, 0))
        _attach(shape, vent["wheel"], start, shape._wheel_vents)


def _triangulate_side(shape, side, points):
    """Ear-clip the source silhouette around the two open wheel arches."""
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
            shape.polygon((a, b, c), PAINT_SIDE, facing=(side, 0, 0))
            points.pop(index)
            break
        else:
            raise ValueError("Cannot triangulate a quantized source wheel arch")
    shape.polygon(points, PAINT_SIDE, facing=(side, 0, 0))


def _wheel_arch(wheel):
    _, cy, cz = WHEEL_CENTERS[wheel]
    end = math.sqrt(ARCH_RADIUS ** 2 - (BODY_SILL_Y - cy) ** 2)
    samples = [(-end, BODY_SILL_Y)]
    # Extra lower front samples clear the tyre's full steering envelope;
    # both axles use 30-degree upper chords rather than the old 45 degrees.
    first = -1 if wheel < FRONT_WHEEL_COUNT else 0
    last = 7 if wheel < FRONT_WHEEL_COUNT else 6
    for step in range(first, last + 1):
        angle = step * ARCH_ANGLE_STEP
        samples.append((-ARCH_RADIUS * math.cos(angle), cy + ARCH_RADIUS * math.sin(angle)))
    samples.append((end, BODY_SILL_Y))
    return [(cz + dz, y) for dz, y in samples]


def _duct_side(shape, side, outline, front_wheel):
    """Split the skin around an upright gill which opens toward the rear."""
    top_y = _outer_height(SIDE_DUCT_FRONT_Z)
    front = _side_point(side, SIDE_DUCT_FRONT_Z, top_y)
    lower = _side_point(side, SIDE_DUCT_REAR_Z, BODY_SILL_Y)
    lip_bottom = _side_point(side, SIDE_DUCT_FRONT_Z, BODY_SILL_Y)
    center = list(_side_point(side, SIDE_DUCT_CENTER_Z, SIDE_DUCT_CENTER_Y))
    center[0] -= side * SIDE_DUCT_DEPTH
    center = tuple(center)
    arch_start = _side_point(side, *_wheel_arch(front_wheel)[0])
    insert = outline.index(arch_start)
    outline[insert:insert] = [lower, lip_bottom]
    # The rear skin ends at the recessed centre; the front skin ends at
    # the outer lip. This gives the grey wall a rearward-facing normal,
    # with no old side panel left across the opening.
    # Merge the coplanar sill subdivision to reserve vertices for lamp rings.
    rear_panel = outline[outline.index(front):outline.index(lower)] + [lip_bottom, center]
    front_panel = outline[outline.index(lip_bottom):] + outline[:outline.index(front) + 1]
    _triangulate_side(shape, side, rear_panel)
    _triangulate_side(shape, side, front_panel)
    shape.polygon((lip_bottom, front, center), GREY, facing=(side, 0, 0))


def _shell(shape):
    for side in (-1, 1):
        outline = [(side * width, y, z) for z, width, y in _outer_profile()]
        outline.append(_side_point(side, _SECTIONS[-1][0], BODY_SILL_Y))
        outline.append(_side_point(side, REAR_DIFFUSER_START_Z, BODY_SILL_Y))
        for wheel in (3 if side < 0 else 2, 0 if side < 0 else 1):
            outline.extend(_side_point(side, z, y) for z, y in _wheel_arch(wheel))
        outline.append(_side_point(side, _SECTIONS[0][0], BODY_SILL_Y))
        _duct_side(shape, side, outline, 0 if side < 0 else 1)
        _shoulders(shape, side)
        _openings(shape, side)
    for front, rear in zip(_SECTIONS, _SECTIONS[1:]):
        z0, *_, x0, y0 = front
        z1, *_, x1, y1 = rear
        shape.polygon(((-x0, y0, z0), (x0, y0, z0),
                       (x1, y1, z1), (-x1, y1, z1)), PAINT_TOP, facing=(0, 1, 0))
    _undertray(shape)


def _undertray(shape):
    """Join the flat floor to a rising, closed diffuser inside the tail.

    The wing reaches behind the body, but the low undertray does not. Shared
    fascia and floor seams keep this a continuous underside at every LOD.
    """
    front = _side_point(1, _SECTIONS[0][0], BODY_SILL_Y)
    shoulder = _side_point(1, REAR_DIFFUSER_START_Z, BODY_SILL_Y)
    corner = _side_point(1, REAR_BODY_Z, BODY_SILL_Y)
    entry = (REAR_DIFFUSER_HALF_WIDTH, BODY_SILL_Y, REAR_DIFFUSER_START_Z)
    exit = (REAR_DIFFUSER_HALF_WIDTH, REAR_DIFFUSER_EXIT_Y, REAR_BODY_Z)

    def mirror(point):
        return -point[0], point[1], point[2]

    # Taper the hidden floor directly to the existing diffuser entry. This
    # stays inside the tyre paths and saves two controls for hollow lamps.
    shape.polygon((mirror(front), mirror(entry), entry, front,
                   (0, BODY_SILL_Y, front[2])),
                  CARBON, facing=(0, -1, 0))
    # The exposed rear ramp and corner returns match the darker underbody.
    shape.polygon((mirror(entry), entry, exit, mirror(exit)),
                  CARBON, facing=(0, -1, 0))
    for side in (-1, 1):
        a, b, c, d = [(side * x, y, z) for x, y, z in (shoulder, corner, exit, entry)]
        # Explicit triangles retain the curved lower rear corners without
        # a warped quad crossing the new rising centre panel.
        shape.polygon((a, b, c), CARBON, facing=(0, -1, 0))
        shape.polygon((a, c, d), CARBON, facing=(0, -1, 0))


# Each source canopy ring has lower shoulder, upper shoulder and curved
# centre ridge. The glass slopes rearward to a roof peak around Z=-10.
_CANOPY = ((15, 8.2, 7.5, 4.5, 8.0, 9.0),
           (1, 8.4, 10.5, 5.7, 16.0, 18.3),
           (-12, 8.3, 11.3, 5.7, 18.0, 19.9),
           (-24, 6.8, 7.4, 4.7, 15.6, 17.6),
           (-39, 2.5, 9.0, 1.8, 11.6, 12.6))
# The roof is the highest visible surface after removing the centre fin.
# Keep the source envelope as the baseline for native culling and provenance.
BODY_HEIGHT = max(section[-1] for section in _CANOPY) - CONTACT_Y


def _canopy(shape, simple):
    rings = []
    for z, width, y, upper, upper_y, ridge in _CANOPY:
        # Source side windows sit above a painted lower shoulder. Shared
        # points collapse the extra strip outside the glazed part.
        glazed = z in (1, -12)
        base_y = 7.0 if glazed else y
        rings.append(((-width, base_y, z), (-width, y, z), (-upper, upper_y, z),
                      (0, ridge, z), (upper, upper_y, z), (width, y, z),
                      (width, base_y, z)))
    shape.polygon(rings[0], BLACK, facing=(0, 0, 1))
    for station, (front, rear) in enumerate(zip(rings, rings[1:])):
        for strip in range(6):
            glass = station == 0 or station == 1 and strip in (1, 4)
            shape.polygon((front[strip], front[strip + 1],
                           rear[strip + 1], rear[strip]),
                          BLACK if glass else PAINT_TOP,
                          facing=(0, 1, 0) if strip in (2, 3) else
                          (-1 if strip < 2 else 1, 0, 0))
    shape.polygon(rings[-1], PAINT_EDGE, facing=(0, 0, -1))
    if not simple:
        for side in (-1, 1):
            stem = (side * 14.0, 14.0, 10)
            mount = next(ring[2] for ring in _roof_rings(side)
                         if ring[0][2] == SIDE_DUCT_FRONT_Z)
            shape.line((mount, stem), CARBON)
            shape.polygon((stem, (side * 18.5, 14.0, 10),
                           (side * 18.5, 15.8, 8), (side * 14.0, 15.5, 8)), BLACK)



def _nose(shape):
    # Bowed paint bands and rolled cheeks frame a shallow grey centre inset.
    # Shared rows describe the curvature instead of a flat black mouth.
    upper = ((-16.5, 2.8, 39), (-13, 2.7, 43.35), (0, 2.14, 42.7),
             (13, 2.7, 43.35), (16.5, 2.8, 39))
    inset_top = ((-16.0, -.1, 40.3), (-11.0, -.1, 41.1), (0, -.1, 42.2),
                 (11.0, -.1, 41.1), (16.0, -.1, 40.3))
    inset_bottom = ((-15.8, -2.3, 39.9), (-11.0, -2.3, 40.9), (0, -2.3, 42.1),
                    (11.0, -2.3, 40.9), (15.8, -2.3, 39.9))
    front_sill = _side_point(1, _SECTIONS[0][0], BODY_SILL_Y)
    lower = ((-15.6, BODY_SILL_Y, 38.5), (-front_sill[0], front_sill[1], front_sill[2]),
             (0, BODY_SILL_Y, front_sill[2]), front_sill, (15.6, BODY_SILL_Y, 38.5))
    bands = ((upper, inset_top, (PAINT_SIDE, PAINT_TOP, PAINT_TOP, PAINT_SIDE)),
             (inset_top, inset_bottom, (PAINT_SIDE, GREY, GREY, PAINT_SIDE)),
             (inset_bottom, lower, (PAINT_EDGE, PAINT_SIDE, PAINT_SIDE, PAINT_EDGE)))
    roof = _roof_rings(1)[0]
    roof_edge = [(-x, y, z) for x, y, z in (roof[0], roof[1], roof[-1])]
    roof_edge.extend((roof[-1], roof[1], roof[0]))
    for a, b in zip(roof_edge, roof_edge[1:]):
        triangle = (upper[len(upper) // 2], a, b)
        # The thin folded hood lip is visible from above and below.
        shape.polygon(triangle, PAINT_TOP)
    for top, bottom, materials in bands:
        for index, material in enumerate(materials):
            a, b, c, d = top[index], top[index + 1], bottom[index + 1], bottom[index]
            for triangle in ((a, b, c), (a, c, d)):
                shape.polygon(triangle, material, facing=(0, 0, 1))

    for side in (-1, 1):
        edge = 0 if side < 0 else -1
        corner = _side_point(side, _SECTIONS[0][0], BODY_SILL_Y)
        cheek = [row[edge] for row in (upper, inset_top, inset_bottom, lower)]
        for a, b in zip(cheek, cheek[1:]):
            triangle = (corner, a, b)
            shape.polygon(triangle, PAINT_SIDE, facing=(side, 0, 1))
    arch_end = math.sqrt(ARCH_RADIUS ** 2 - (BODY_SILL_Y - WHEEL_CENTERS[0][1]) ** 2)
    end_z = WHEEL_CENTERS[0][2] + arch_end
    end = _side_point(1, end_z, BODY_SILL_Y)
    front = _side_point(1, _SECTIONS[0][0], BODY_SILL_Y)
    shape.polygon(((-front[0], front[1], front[2]), lower[len(lower) // 2], front,
                   end, (-end[0], end[1], end[2])), CARBON, facing=(0, -1, 0))


def rear_lamp_centres(side):
    """Slightly shrink the upper lens around its existing diameter control."""
    lamp_z = REAR_BODY_Z - REAR_LAMP_FACE_OFFSET
    crown = _roof_rings(side)[-1][2]
    upper = tuple(crown[axis] + (value - crown[axis]) * REAR_LAMP_SIZE_SCALE
                  for axis, value in enumerate(
                      (side * REAR_LAMP_X, REAR_LAMP_UPPER_Y, lamp_z)))
    return upper, (side * REAR_LAMP_X, REAR_LAMP_LOWER_Y, lamp_z)


def _lamp_ring(shape, center, diameter_point):
    """Keep native circular lenses with a smaller dark concentric centre."""
    first, last = shape.point(center), shape.point(diameter_point)
    inner_diameter = round(math.dist(first, last) * REAR_LAMP_INNER_RATIO)
    inner_point = tuple(value / shape.scale for value in
                        (first[0] + inner_diameter, first[1], first[2]))
    shape.sphere(center, diameter_point, BRAKE_LIGHT)
    shape.sphere(center, inner_point, BLACK)


def _tail(shape):
    # Painted rolled shoulders and a lower bumper frame a small grey inset.
    # The roof and undertray supply the shared outer seams; only the lower
    # inset corners need new vertices to give the rear real depth.
    z = REAR_BODY_Z
    inset_top = []
    inset_bottom = []
    bumper_bottom = []
    for side in (-1, 1):
        roof = _roof_rings(side)[-1]
        lower = (roof[-1][0], REAR_INSET_LOWER_Y, z + REAR_INSET_RECESS)
        low_outer = _side_point(side, z, BODY_SILL_Y)
        low_inner = (side * REAR_DIFFUSER_HALF_WIDTH, REAR_DIFFUSER_EXIT_Y, z)
        for a, b, material in zip(roof, roof[1:], (PAINT_SIDE, PAINT_TOP, PAINT_TOP)):
            shape.polygon((a, b, lower), material, facing=(0, 0, -1))
        shape.polygon((roof[0], low_outer, lower), PAINT_SIDE, facing=(0, 0, -1))
        shape.polygon((low_outer, low_inner, lower), PAINT_EDGE, facing=(0, 0, -1))
        inset_top.append(roof[-1])
        inset_bottom.append(lower)
        bumper_bottom.append(low_inner)
    for top, bottom, material in ((inset_top, inset_bottom, GREY),
                                  (inset_bottom, bumper_bottom, PAINT_SIDE)):
        a, b, c, d = top[0], top[1], bottom[1], bottom[0]
        shape.polygon((a, b, c), material, facing=(0, 0, -1))
        shape.polygon((a, c, d), material, facing=(0, 0, -1))
    # Smaller red rings with dark centres follow the rear reference. Reuse
    # the crown as the upper diameter control to retain the native budget.
    for side in (-1, 1):
        crown = _roof_rings(side)[-1][2]
        upper, lower = rear_lamp_centres(side)
        diameter = round(REAR_LAMP_DIAMETER * REAR_LAMP_SIZE_SCALE * shape.scale)
        center = shape.point(lower)
        diameter_point = tuple(value / shape.scale for value in
                               (center[0] + side * diameter, center[1], center[2]))
        _lamp_ring(shape, upper, crown)
        _lamp_ring(shape, lower, diameter_point)
    # Two measured aerofoil elements and swept endplates. Source mounts
    # sit near the outer shoulders rather than on central swan-neck struts.
    for side in (-1, 1):
        shape.polygon(((side * 17.9, 12.3, -30.5), (side * 17.9, 18.7, -34.3),
                       (side * 17.9, 18.14, -40.95), (side * 17.9, 18.2, -43.35),
                       (side * 17.9, 12.1, -40), (side * 17.9, 10.4, -36.7)), CARBON)
    shape.polygon(((-17.9, 17.6, -34.7), (17.9, 17.6, -34.7),
                   (17.65, 16.9, -39.6), (-17.65, 16.9, -39.6)), CARBON)
    shape.polygon(((-17.65, 17.3, -38.6), (17.65, 17.3, -38.6),
                   (17.9, 18.14, -40.95), (-17.9, 18.14, -40.95)), CARBON)



def _car(scale=1, *, simple=False):
    shape = _Shape(scale)
    _controls(shape)
    _shell(shape)
    _canopy(shape, simple)
    _nose(shape)
    _tail(shape)
    wheels, body = shape.primitives[:4], shape.primitives[4:]
    shape.primitives = []
    for index, wheel in enumerate(wheels):
        shape.primitives.append(wheel)
        shape.primitives.extend(shape._wheel_roofs.get(index, ()))
        shape.primitives.extend(shape._wheel_vents.get(index, ()))
    shape.primitives.extend(body)
    _fit_car_bounds(shape)
    return shape


def _showroom_car():
    shape = _car(SHOWROOM_SCALE)
    shape.vertices = [(x, y + SHOWROOM_Y_OFFSET, z) for x, y, z in shape.vertices]
    return shape


def _debris(index):
    """Crash fragments taken from the new fender, door, glass and wing."""
    front, rear = _roof_rings(1)[1:3]
    panels = (
        ((front[0], rear[0], rear[1], front[1]), PAINT_TOP),
        (((19.05, 10.6, 16), (19.05, 9.8, -12),
          (19.05, BODY_SILL_Y, -12), (19.05, BODY_SILL_Y, 16)), PAINT_SIDE),
        (((-4.5, 8.0, 15), (4.5, 8.0, 15),
          (5.7, 16.0, 1), (-5.7, 16.0, 1)), BLACK),
        (((-17.9, 17.6, -34.7), (17.9, 17.6, -34.7),
          (17.65, 16.9, -39.6), (-17.65, 16.9, -39.6)), CARBON),
    )
    points, material = panels[index]
    centre = tuple(sum(point[axis] for point in points) / len(points) for axis in range(3))
    shape = _Shape(1)
    shape.polygon(tuple(tuple(c - centre[axis] for axis, c in enumerate(point))
                        for point in points), material)
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
    """Build the new showroom, racing LODs and four matching crash fragments."""
    resources = [("car0", _showroom_car().encode()),
                 ("car1", _car().encode()), ("car2", _car(simple=True).encode())]
    resources.extend((f"exp{i}", _debris(i)) for i in range(4))
    return _container(resources)


def export_obj(paint=0):
    """Export the authored showroom mesh and explicit tyres in metres.

    OBJ has ordinary faces, unlike the native Stunts wheel primitive. Its
    ground is Y=0, +Z is forwards, and all native control/bounds vertices
    are omitted. This editable source is generated from the new source-derived topology.
    """
    if not 0 <= paint < len(PAINT_NAMES):
        raise ValueError("Invalid paint index")
    shape = _showroom_car()
    factor = MM_PER_UNIT / (1000 * SHOWROOM_SCALE)
    vertices = []
    faces = []
    lines = []
    dots = []
    lookup = {}
    lamp_inners = {indices[0]: indices[1]
                   for kind, _, materials, indices in shape.primitives
                   if kind == SPHERE_PRIMITIVE and materials[paint] == BLACK}

    def vertex(point):
        point = tuple(point)
        if point not in lookup:
            lookup[point] = len(vertices) + 1
            vertices.append(point)
        return lookup[point]

    for kind, flags, materials, indices in shape.primitives:
        if kind == WHEEL_PRIMITIVE:
            continue
        points = [tuple(c * factor for c in shape.vertices[index]) for index in indices]
        if kind == SPHERE_PRIMITIVE:
            if materials[paint] == BLACK and indices[0] in lamp_inners:
                continue
            # Native spheres are round screen-facing lenses. Export their
            # visible rear lens, including a true annulus around dark centres.
            center, diameter_point = points
            radius = math.dist(center, diameter_point) / 2
            ring = [vertex((center[0] + radius * math.cos(angle),
                            center[1] + radius * math.sin(angle), center[2]))
                    for angle in (step * math.tau / LAMP_EXPORT_SEGMENTS
                                  for step in range(LAMP_EXPORT_SEGMENTS))]
            if materials[paint] == BRAKE_LIGHT and indices[0] in lamp_inners:
                inner_point = tuple(c * factor for c in
                                    shape.vertices[lamp_inners[indices[0]]])
                inner_radius = math.dist(center, inner_point) / 2
                inner = [vertex((center[0] + inner_radius * math.cos(angle),
                                 center[1] + inner_radius * math.sin(angle), center[2]))
                         for angle in (step * math.tau / LAMP_EXPORT_SEGMENTS
                                       for step in range(LAMP_EXPORT_SEGMENTS))]
                for step in range(LAMP_EXPORT_SEGMENTS):
                    following = (step + 1) % LAMP_EXPORT_SEGMENTS
                    faces.append((materials[paint],
                                  [ring[step], inner[step], inner[following], ring[following]]))
                faces.append((BLACK, list(reversed(inner))))
            else:
                faces.append((materials[paint], list(reversed(ring))))
            continue
        target = dots if kind == POINT_PRIMITIVE else lines if kind == LINE_PRIMITIVE else faces
        target.append((materials[paint], [vertex(p) for p in points]))
        if flags & TWO_SIDED_FLAG and kind > LINE_PRIMITIVE:
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

    result = ["# McMurtry Speirling: retopology of the user-supplied GLB.",
              "# Adapted from 2026 McMurtry Speirling by Dave Love SketchFab.",
              "# https://sketchfab.com/3d-models/2026-mcmurtry-speirling-24a39a15bde049acbb3ede571cee0c8b",
              "# CC BY 4.0: https://creativecommons.org/licenses/by/4.0/",
              "# Changes: native retopology, uniform scaling, animated wheels and palette paints.",
              "# Metres. Y up, Z forward. Ground at Y=0.",
              f"# Paint: {PAINT_NAMES[paint]}", "mtllib MCMU.mtl", "o McMurtry_Speirling_PURE"]
    result.extend("v " + " ".join(f"{c:.6f}" for c in point) for point in vertices)
    for primitive, items in (("f", faces), ("l", lines), ("p", dots)):
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
                 WHITE: (.95, .96, .98), BRAKE_LIGHT: (.65, .005, .008)}
    for shade, multiplier in ((PAINT_TOP[paint], 1.2),
                              (PAINT_SIDE[paint], 1.0), (PAINT_EDGE[paint], .75)):
        materials[shade] = tuple(min(1, c * multiplier) for c in body)
    result = ["# Source-derived model; material names retain native Stunts IDs."]
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
