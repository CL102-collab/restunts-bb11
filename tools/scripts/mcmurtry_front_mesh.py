"""Connected, symmetric front panels measured from the car0 showroom mesh.

Author one half with shared panel edges. The wheel vents are bounded recesses;
triangle reduction must never decide which pieces of the bonnet survive.
"""

import math

import mcmurtry_model as model
from mcmurtry_lod_retopology import mirror_half
from mcmurtry_source_surface import SourceSurface
from mcmurtry_front_details import build_mirror_details


TOP_STATIONS = (6., 9., 12.5, 16., 19., 22., 25., 28., 31., 34.)
CANOPY_PROFILE = ((6., 5.8), (9., 4.8), (12.5, .2))
CANOPY_END_Z = CANOPY_PROFILE[-1][0]
HOOD_INNER_X = 9.
HOOD_SHOULDER_X = 11.5
RIDGE_COLUMNS = (5, 6)
RIDGE_START_Z = 16.
FENDER_WIDTH = 18.875
FENDER_RIM_HEIGHT = .2
ARCH_RADIUS = 10.3
WHEEL_CENTER_Y = 2.
WHEEL_CENTER_Z = 25.
WELL_INNER_X = 12.5
WELL_INNER_RADIUS = 9.0
VENT_DEPTH = .5
RIDGE_OUTWARD_ALLOWANCE = 3 / 16
FLOOR_Y = -5.25
FLOOR_TRIANGLE_EPSILON = 1e-9
BUMPER_MIDDLE_Y = -1.
MAXIMUM_BUMPER_RECESS = 2.
VENT_START = 19.
VENT_END = 31.
VENT_COLUMNS = (7, 8)
VENT_PROFILE = ((16., 14., 17.25), (19., 16.5, 16.5),
                (22., 13.6, 17.25), (25., 12.35, 17.25),
                (28., 12.25, 17.25), (31., 12.8, 12.8),
                (34., 13.5, 17.25))
# Positive-half samples of the source's painted nose edge, in racing units.
NOSE_EDGE = ((0., 2.456, 41.478), (2., 3.05, 40.767),
             (4., 3.1, 40.564), (6., 3.08, 40.39),
             (8., 2.991, 40.123), (10., 2.902, 39.857),
             (12., 2.592, 41.667), (13., 2.493, 41.414),
             (14., 2.464, 40.671), (16., 1.564, 39.329),
             (17., 2.114, 38.3), (18.5, 1.691, 36.297))
NOSE_COLUMNS = (0., 3., 6., 9., 10., 10.833333, 11.666667, 12.5, 16.5, 18.5)
PALETTE_BLACK = (model.BLACK,) * len(model.PAINT_TOP)
PALETTE_CARBON = (model.CARBON,) * len(model.PAINT_TOP)
COORDINATE_KEY_DIGITS = 8
SEAM_CANOPY_MAXIMUM_X = 6.7
SEAM_CANOPY_MINIMUM_Y = 10.5
SEAM_UNDERBODY_MAXIMUM_Y = -2.
SEAM_SIDE_MINIMUM_X = 17.


def _interpolate(rows, value, column):
    for first, second in zip(rows, rows[1:]):
        if value <= second[0]:
            weight = max(0., (value - first[0]) / (second[0] - first[0]))
            return first[column] * (1 - weight) + second[column] * weight
    return rows[-1][column]


class _Mesh:
    def __init__(self, points, triangles, materials):
        self.points = [tuple(p) for p in points]
        self.triangles = [tuple(f) for f in triangles]
        self.materials = [tuple(m) for m in materials]
        self.lookup = {tuple(round(float(c), COORDINATE_KEY_DIGITS) for c in p): i
                       for i, p in enumerate(self.points)}

    def vertex(self, point):
        key = tuple(round(float(c), COORDINATE_KEY_DIGITS) for c in point)
        if key not in self.lookup:
            self.lookup[key] = len(self.points)
            self.points.append(tuple(map(float, point)))
        return self.lookup[key]

    def face(self, indices, palette):
        if len(set(indices)) == 3:
            self.triangles.append(tuple(indices))
            self.materials.append(tuple(palette))

    def quad(self, a, b, c, d, palette):
        self.face((a, b, c), palette)
        self.face((a, c, d), palette)

    def strip(self, first, second, palette):
        for a, b, c, d in zip(first, first[1:], second[1:], second):
            self.quad(a, b, c, d, palette)

    def zipper(self, first, second, reverse=False):
        def positions(ring):
            lengths = [0.]
            for a, b in zip(ring, ring[1:]):
                lengths.append(lengths[-1] + math.dist(self.points[a], self.points[b]))
            return [value / lengths[-1] for value in lengths]
        a, b = positions(first), positions(second)
        i = j = 0
        while i < len(first) - 1 or j < len(second) - 1:
            if j == len(second) - 1 or (i < len(first) - 1 and a[i + 1] < b[j + 1]):
                face = first[i], first[i + 1], second[j]
                i += 1
            else:
                face = first[i], second[j + 1], second[j]
                j += 1
            vertices = [self.points[k] for k in face]
            if (max(p[0] for p in vertices) < SEAM_CANOPY_MAXIMUM_X and
                    min(p[1] for p in vertices) > SEAM_CANOPY_MINIMUM_Y):
                palette = PALETTE_BLACK
            elif max(p[1] for p in vertices) < SEAM_UNDERBODY_MAXIMUM_Y:
                palette = PALETTE_CARBON
            elif min(p[0] for p in vertices) > SEAM_SIDE_MINIMUM_X:
                palette = model.PAINT_SIDE
            else:
                palette = model.PAINT_TOP
            self.face(face[::-1] if reverse else face, palette)


def rebuild_front(source_points, source_faces, source_materials,
                  rear_points, rear_faces, rear_materials, seam):
    """Join source-sampled front panels to a clipped positive-half rear shell."""
    source = SourceSurface(source_points, source_faces, source_materials)
    mesh = _Mesh(rear_points, rear_faces, rear_materials)
    top = []
    for z in TOP_STATIONS:
        canopy_width = _interpolate(CANOPY_PROFILE, z, 1)
        inner = _interpolate(VENT_PROFILE, z, 1)
        outer = _interpolate(VENT_PROFILE, z, 2)
        xs = (0., canopy_width / 2 if z <= CANOPY_END_Z else 3.,
              canopy_width if z <= CANOPY_END_Z else 6., HOOD_INNER_X, HOOD_SHOULDER_X,
              (2 * HOOD_SHOULDER_X + inner) / 3, (HOOD_SHOULDER_X + 2 * inner) / 3,
              inner, outer, FENDER_WIDTH)
        row = []
        for column, x in enumerate(xs):
            y = source.top_y(x, z)
            # A small conservative lift preserves the source's grazing
            # silhouette between sampled sections and fixed-point vertices.
            if column in RIDGE_COLUMNS and RIDGE_START_Z <= z <= VENT_END:
                y += RIDGE_OUTWARD_ALLOWANCE
            if x == FENDER_WIDTH and abs(z - WHEEL_CENTER_Z) < ARCH_RADIUS:
                y = max(y, WHEEL_CENTER_Y + math.sqrt(
                    ARCH_RADIUS ** 2 - (z - WHEEL_CENTER_Z) ** 2) + FENDER_RIM_HEIGHT)
            row.append(mesh.vertex((x, y, z)))
        top.append(row)
    last = [mesh.points[i] for i in top[-1]]
    nose = [(x, _interpolate(NOSE_EDGE, x, 1), _interpolate(NOSE_EDGE, x, 2))
            for x in NOSE_COLUMNS]
    halfway = []
    for first, second in zip(last, nose):
        x, _, z = [(a + b) / 2 for a, b in zip(first, second)]
        halfway.append(mesh.vertex((x, source.top_y(x, z), z)))
    top.extend((halfway, [mesh.vertex(p) for p in nose]))
    for row, (first, second) in enumerate(zip(top, top[1:])):
        for column in range(len(first) - 1):
            if (column == VENT_COLUMNS[0] and
                    VENT_START <= mesh.points[first[column]][2] < VENT_END):
                continue
            palette = PALETTE_BLACK if row < 2 and column < 2 else model.PAINT_TOP
            mesh.quad(first[column], second[column], second[column + 1],
                      first[column + 1], palette)

    # Give the designed vents an enclosed dark interior, sharing every rim edge.
    # It remains below the surrounding skin and above the animated tyre crown.
    vent_rows = [i for i, z in enumerate(TOP_STATIONS) if VENT_START <= z <= VENT_END]
    rim = [top[i][VENT_COLUMNS[0]] for i in vent_rows]
    rim += [top[i][VENT_COLUMNS[1]] for i in reversed(vent_rows)]
    rim = list(dict.fromkeys(rim))
    floor = [mesh.vertex((mesh.points[i][0], mesh.points[i][1] - VENT_DEPTH, mesh.points[i][2]))
             for i in rim]
    mesh.strip(rim + rim[:1], floor + floor[:1], PALETTE_CARBON)
    floor_lookup = dict(zip(rim, floor))
    for first, second in zip(vent_rows, vent_rows[1:]):
        mesh.quad(floor_lookup[top[first][VENT_COLUMNS[0]]],
                  floor_lookup[top[second][VENT_COLUMNS[0]]],
                  floor_lookup[top[second][VENT_COLUMNS[1]]],
                  floor_lookup[top[first][VENT_COLUMNS[1]]], PALETTE_BLACK)

    outer = [row[-1] for row in top]
    nose_x, _, nose_z = nose[-1]
    nose_floor_z = max(nose_z - MAXIMUM_BUMPER_RECESS,
                       min(nose_z, source.front_z(nose_x, FLOOR_Y)))
    lower, inner, floor_center, floor_side = [], [], [], []
    for index in outer:
        x, _, z = mesh.points[index]
        dz = z - WHEEL_CENTER_Z
        arch = abs(dz) < ARCH_RADIUS
        y = WHEEL_CENTER_Y + math.sqrt(ARCH_RADIUS ** 2 - dz ** 2) if arch else FLOOR_Y
        # End the liner at the bumper foot, whose lower edge recedes behind
        # the painted nose. Extending it to the upper edge crosses the cheek.
        lower_z = nose_floor_z if index == outer[-1] else z
        lower.append(mesh.vertex((x, y, lower_z)))
        ratio = WELL_INNER_RADIUS / ARCH_RADIUS if arch else 1.
        inner.append(mesh.vertex((WELL_INNER_X,
                                  WHEEL_CENTER_Y + (y - WHEEL_CENTER_Y) * ratio,
                                  min(WHEEL_CENTER_Z + dz * ratio, nose_floor_z))))
        floor_z = min(z, nose_floor_z)
        floor_center.append(mesh.vertex((0., FLOOR_Y, floor_z)))
        floor_side.append(mesh.vertex((WELL_INNER_X, FLOOR_Y, floor_z)))
    mesh.strip(outer[:-1], lower[:-1], model.PAINT_SIDE)
    # The last cheek turns back toward the bumper foot. Use its other diagonal
    # so the painted panel stays outside the preceding wheel-well roof segment.
    mesh.face((outer[-2], outer[-1], lower[-2]), model.PAINT_SIDE)
    mesh.face((outer[-1], lower[-1], lower[-2]), model.PAINT_SIDE)
    mesh.strip(lower, inner, PALETTE_CARBON)
    mesh.strip(inner, floor_side, PALETTE_CARBON)
    mesh.strip(floor_side, floor_center, PALETTE_CARBON)

    # A single continuous carbon mouth, with painted outer cheeks and a thin lip.
    middle, bottom = [], []
    for x, _, z in nose:
        middle_z = max(z - MAXIMUM_BUMPER_RECESS,
                       min(z, source.front_z(x, BUMPER_MIDDLE_Y)))
        bottom_z = max(z - MAXIMUM_BUMPER_RECESS, min(z, source.front_z(x, FLOOR_Y)))
        middle.append(mesh.vertex((x, BUMPER_MIDDLE_Y, middle_z)))
        bottom.append(mesh.vertex((x, FLOOR_Y, bottom_z)))
    for column in range(len(nose) - 1):
        palette = PALETTE_CARBON if column < len(nose) - 2 else model.PAINT_SIDE
        mesh.quad(top[-1][column], middle[column], middle[column + 1],
                  top[-1][column + 1], palette)
        mesh.quad(middle[column], bottom[column], bottom[column + 1],
                  middle[column + 1], palette)
    mesh.face((outer[-1], lower[-1], middle[-1]), model.PAINT_SIDE)
    mesh.face((middle[-1], lower[-1], bottom[-1]), model.PAINT_SIDE)
    # Close the underside of the curved nose to the last transverse floor edge.
    # The sampled front contour is concave. Ear clipping preserves that contour
    # and each shared boundary vertex without folding triangles over one another.
    back = [floor_center[-1], floor_side[-1], lower[-1]]
    polygon = list(dict.fromkeys(back + list(reversed(bottom))))

    def floor_cross(first, second, third):
        a, b, c = (mesh.points[index] for index in (first, second, third))
        return (b[0] - a[0]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[0] - a[0])

    while len(polygon) > 3:
        for corner, current in enumerate(polygon):
            previous, following = polygon[corner - 1], polygon[(corner + 1) % len(polygon)]
            if floor_cross(previous, current, following) <= FLOOR_TRIANGLE_EPSILON:
                continue
            if any(min(floor_cross(previous, current, point),
                       floor_cross(current, following, point),
                       floor_cross(following, previous, point)) >= -FLOOR_TRIANGLE_EPSILON
                   for point in polygon if point not in (previous, current, following)):
                continue
            mesh.face((previous, current, following), PALETTE_CARBON)
            del polygon[corner]
            break
        else:
            raise ValueError("The nose underside must have a simple ordered boundary")
    mesh.face(polygon, PALETTE_CARBON)

    initial = top[0] + [lower[0], inner[0], floor_center[0]]
    mesh.zipper(list(map(int, seam)), initial, reverse=True)
    detail_points, detail_faces, detail_materials = build_mirror_details(
        source_points, source_faces, source_materials, surface=source)
    detail_indices = [mesh.vertex(point) for point in detail_points]
    for face, palette in zip(detail_faces, detail_materials):
        mesh.face(tuple(detail_indices[index] for index in face), palette)
    return mirror_half(mesh.points, mesh.triangles, mesh.materials)
