"""Fitted wheel-top recesses cut into the smooth McMurtry racing bodywork.

The front pair retains the measured rectangular outline; the rear pair uses
its authored swept outline. Clip the actual roof skin, then reuse its surface
for the recessed floor so the openings cannot disappear beneath body panels.
"""

from bisect import bisect_left, bisect_right
import math


AXES = 3
EPSILON = 1e-9
TRIANGLE_VERTICES = 3
MAX_POLYGON_VERTICES = 10
RECESS_DEPTH = .5
MINIMUM_TYRE_CLEARANCE = 1.0


def _average(points):
    return tuple(sum(point[axis] for point in points) / len(points) for axis in range(AXES))


def _side(point, first, second):
    return ((second[0] - first[0]) * (point[2] - first[2]) -
            (second[2] - first[2]) * (point[0] - first[0]))


def _compact(points):
    result = []
    for point in points:
        if not result or point != result[-1]:
            result.append(point)
    if len(result) > 1 and result[0] == result[-1]:
        result.pop()
    return result


def _split(polygon, first, second):
    """Split a roof polygon by an aperture edge, interpolating all coordinates."""
    inside, outside = [], []
    for start, end in zip(polygon, polygon[1:] + polygon[:1]):
        a, b = _side(start, first, second), _side(end, first, second)
        if a >= -EPSILON:
            inside.append(start)
        if a <= EPSILON:
            outside.append(start)
        if (a < -EPSILON and b > EPSILON) or (a > EPSILON and b < -EPSILON):
            weight = a / (a - b)
            intersection = tuple(x + (y - x) * weight for x, y in zip(start, end))
            inside.append(intersection)
            outside.append(intersection)
    return _compact(inside), _compact(outside)


def _cut(polygon, aperture):
    current, outside = polygon, []
    for first, second in zip(aperture, aperture[1:] + aperture[:1]):
        current, remainder = _split(current, first, second)
        if len(remainder) >= TRIANGLE_VERTICES:
            outside.append(remainder)
        if len(current) < TRIANGLE_VERTICES:
            return [polygon], []
    return outside, current


def _stitch(polygons):
    """Share partition junctions before rounding, avoiding quantized T-junctions."""
    vertices = sorted(set(point for polygon in polygons for point in polygon))
    positions = [point[0] for point in vertices]
    result = []
    for polygon in polygons:
        joined = []
        for first, second in zip(polygon, polygon[1:] + polygon[:1]):
            delta = tuple(b - a for a, b in zip(first, second))
            axis = max(range(AXES), key=lambda i: abs(delta[i]))
            if abs(delta[axis]) <= EPSILON:
                continue
            samples = []
            start = bisect_left(positions, min(first[0], second[0]) - EPSILON)
            stop = bisect_right(positions, max(first[0], second[0]) + EPSILON)
            for point in vertices[start:stop]:
                weight = (point[axis] - first[axis]) / delta[axis]
                if -EPSILON <= weight < 1 - EPSILON and all(
                        abs(point[i] - first[i] - weight * delta[i]) <= EPSILON
                        for i in range(AXES)):
                    samples.append((weight, point))
            joined.extend(point for _, point in sorted(samples))
        result.append(_compact(joined))
    return result


def _height(triangle, x, z):
    a, b, c = triangle
    ux, uz = b[0] - a[0], b[2] - a[2]
    vx, vz = c[0] - a[0], c[2] - a[2]
    determinant = ux * vz - uz * vx
    if abs(determinant) <= EPSILON:
        return None
    px, pz = x - a[0], z - a[2]
    u, v = (px * vz - pz * vx) / determinant, (ux * pz - uz * px) / determinant
    if min(u, v, 1 - u - v) < -EPSILON:
        return None
    return a[1] + u * (b[1] - a[1]) + v * (c[1] - a[1])


def _triangulate(polygon, normal, model):
    """Ear-clip rounded boundaries instead of fanning across small concavities."""
    axis = max(range(AXES), key=lambda index: abs(normal[index]))
    planar = [index for index in range(AXES) if index != axis]

    def turn(first, second, third):
        return ((second[planar[0]] - first[planar[0]]) *
                (third[planar[1]] - first[planar[1]]) -
                (second[planar[1]] - first[planar[1]]) *
                (third[planar[0]] - first[planar[0]]))

    area = sum(first[planar[0]] * second[planar[1]] -
               second[planar[0]] * first[planar[1]]
               for first, second in zip(polygon, polygon[1:] + polygon[:1]))
    sense = 1 if area > 0 else -1
    remaining = list(polygon)
    result = []
    while len(remaining) > TRIANGLE_VERTICES:
        ears = []
        for index, point in enumerate(remaining):
            first, last = remaining[index - 1], remaining[(index + 1) % len(remaining)]
            triangle = first, point, last
            cross = turn(*triangle) * sense
            if cross <= EPSILON:
                continue
            if any(other not in triangle and
                   all(turn(a, b, other) * sense >= -EPSILON
                       for a, b in zip(triangle, triangle[1:] + triangle[:1]))
                   for other in remaining):
                continue
            child_normal = model._normal(triangle)
            if sum(a * b for a, b in zip(normal, child_normal)) <= EPSILON:
                continue
            rest = remaining[:index] + remaining[index + 1:]
            if sum(a * b for a, b in zip(normal, model._normal(rest))) <= EPSILON:
                continue
            ears.append((cross, index, triangle))
        if not ears:
            raise ValueError("Rounded aperture boundary cannot be triangulated without folding")
        _, index, triangle = max(ears)
        result.append(triangle)
        del remaining[index]
    if model._normal(remaining) != [0, 0, 0]:
        if sum(a * b for a, b in zip(normal, model._normal(remaining))) <= EPSILON:
            raise ValueError("Rounded aperture triangle reverses its parent surface")
        result.append(tuple(remaining))
    return result


def _emit(shape, polygon, materials, flags, model, factor):
    normal = model._normal(polygon)
    polygon = _compact([tuple(round(value * factor) / factor for value in point)
                        for point in polygon])
    if len(set(polygon)) < TRIANGLE_VERTICES or model._normal(polygon) == [0, 0, 0]:
        return
    if sum(a * b for a, b in zip(normal, model._normal(polygon))) < 0:
        polygon.reverse()
    for triangle in _triangulate(polygon, normal, model):
        shape.primitives.append((TRIANGLE_VERTICES, flags, materials,
                                 tuple(shape.vertex(point) for point in triangle)))


def _floor(point, wheel, model, factor):
    x, y, z = point
    _, cy, cz = model.WHEEL_CENTERS[wheel]
    dz = z - cz
    tyre = cy + math.sqrt(max(0., model.WHEEL_RADIUS ** 2 - dz ** 2))
    # Round the required envelope outward before quantizing the well floor.
    minimum = math.ceil((tyre + MINIMUM_TYRE_CLEARANCE) * factor) / factor
    return x, max(y - RECESS_DEPTH, minimum), z


def _compact_shape(shape, controls_end):
    vertices = list(shape.vertices[:controls_end])
    lookup, mapping = {}, {}
    for _, _, _, indices in shape.primitives:
        for index in indices:
            if index < controls_end or index in mapping:
                continue
            point = shape.vertices[index]
            if point not in lookup:
                lookup[point] = len(vertices)
                vertices.append(point)
            mapping[index] = lookup[point]
    shape.vertices = vertices
    shape._lookup = lookup
    shape.primitives = [(kind, flags, materials,
                         tuple(index if index < controls_end else mapping[index] for index in indices))
                        for kind, flags, materials, indices in shape.primitives]


def populate_apertures(shape, model):
    """Cut four visible recesses without changing native bounds or wheel slots."""
    factor = 1 << shape.coordinate_fraction_bits
    paints = (model.PAINT_TOP, model.PAINT_SIDE, model.PAINT_EDGE)
    roof = []
    for kind, _, materials, indices in shape.primitives:
        if TRIANGLE_VERTICES <= kind <= MAX_POLYGON_VERTICES and materials in paints:
            polygon = [shape.vertices[index] for index in indices]
            for corner in range(1, len(polygon) - 1):
                triangle = [polygon[0], polygon[corner], polygon[corner + 1]]
                if model._normal(triangle)[1] > EPSILON:
                    roof.append(triangle)
    dark = (model.BLACK,) * model.PAINT_COUNT
    wall = (model.CARBON,) * model.PAINT_COUNT
    apertures = []
    for wheel in range(len(model.WHEEL_CENTERS)):
        aperture = [(x, y, z - model.BODY_ORIGIN_Z)
                    for x, y, z in model.vent_geometry(wheel)["lip"]]
        area = sum(a[0] * b[2] - b[0] * a[2]
                   for a, b in zip(aperture, aperture[1:] + aperture[:1]))
        if area < 0:
            aperture.reverse()
        apertures.append(aperture)
    original, shape.primitives = shape.primitives, []
    pending = []
    for primitive in original:
        kind, flags, materials, indices = primitive
        if kind != TRIANGLE_VERTICES or materials not in paints:
            shape.primitives.append(primitive)
            continue
        polygon = [shape.vertices[index] for index in indices]
        if model._normal(polygon)[1] <= EPSILON:
            shape.primitives.append(primitive)
            continue
        pieces = [(polygon, materials, flags)]
        for wheel, aperture in enumerate(apertures):
            outside, inside = _cut(polygon, aperture)
            if not inside or model._normal(inside)[1] <= EPSILON:
                continue
            center = _average(inside)
            heights = [height for triangle in roof
                       if (height := _height(triangle, center[0], center[2])) is not None]
            if not heights or center[1] < max(heights) - EPSILON:
                continue
            pieces = [(remainder, materials, flags) for remainder in outside]
            floor = [_floor(point, wheel, model, factor) for point in inside]
            pieces.append((floor, dark, model.TWO_SIDED_FLAG))
            for first, second, lower_first, lower_second in zip(
                    inside, inside[1:] + inside[:1], floor, floor[1:] + floor[:1]):
                if any(abs(_side(first, a, b)) <= EPSILON and
                       abs(_side(second, a, b)) <= EPSILON
                       for a, b in zip(aperture, aperture[1:] + aperture[:1])):
                    pieces.append(([first, second, lower_second, lower_first], wall,
                                   model.TWO_SIDED_FLAG))
            break
        pending.extend(pieces)
    # A cut can introduce a junction on the edge of an otherwise unchanged
    # neighboring roof triangle. Stitch all resulting polygons before any
    # rounding; local-only clipping leaves tiny gaps at those T-junctions.
    stitched = _stitch([polygon for polygon, _, _ in pending])
    for polygon, (_, materials, flags) in zip(stitched, pending):
        _emit(shape, polygon, materials, flags, model, factor)
    controls_end = model.WHEEL_CONTROL_START + len(model.WHEEL_CENTERS) * model.WHEEL_CONTROL_COUNT
    _compact_shape(shape, controls_end)
