"""Smooth racing shells built from the connected native McMurtry retopology.

Keep the authored panels and material boundaries instead of welding unrelated
layers of the imported exterior. The near shell uses one crease-aware
Catmull-Clark pass; the distant shell retains the original panel outlines.
"""

from collections import defaultdict
import math


AXES = 3
TRIANGLE_VERTICES = 3
MAX_POLYGON_VERTICES = 10
CREASE_ANGLE_DEGREES = 50
DISTANT_ARCH_MIN_X = 13.0
MIN_SMOOTHING_WEIGHT = 1 / 256


def _average(points):
    return tuple(sum(p[axis] for p in points) / len(points) for axis in range(AXES))


def _edge(first, second):
    return min(first, second), max(first, second)


def _constrain_winding(linear, smoothed, faces, parents, normals, model, coordinate_factor):
    """Back off shared smoothing where encoded children would fold or collapse.

    Linear subdivision retains each authored panel's winding. Blend affected
    vertices toward that baseline, testing every neighboring panel again after
    each change. This retains connectivity instead of deleting folded faces.
    """
    weights = [1.] * len(smoothed)
    parent_vertices = [set() for _ in normals]
    for (indices, _), parent in zip(faces, parents):
        parent_vertices[parent].update(indices)
    while True:
        points = [tuple(a + (b - a) * weight for a, b in zip(first, second))
                  for first, second, weight in zip(linear, smoothed, weights)]
        encoded = ([tuple(round(value * coordinate_factor) / coordinate_factor for value in point)
                    for point in points] if coordinate_factor else points)
        bad = set()
        for (indices, _), parent in zip(faces, parents):
            for corner in range(1, len(indices) - 1):
                triangle = [encoded[index] for index in
                            (indices[0], indices[corner], indices[corner + 1])]
                normal = model._normal(triangle)
                if sum(a * b for a, b in zip(normals[parent], normal)) <= 0:
                    bad.add(parent)
        if not bad:
            return points
        changed = False
        for index in set().union(*(parent_vertices[parent] for parent in bad)):
            if weights[index]:
                weight = weights[index] / 2
                weights[index] = weight if weight >= MIN_SMOOTHING_WEIGHT else 0.
                changed = True
        if not changed:
            raise ValueError("Authored panel folds or collapses at the H3D coordinate precision")


def _smooth_panels(points, faces, controls_end, model, *, distant, coordinate_factor=None):
    """Apply standard Catmull-Clark masks, retaining corners and sharp creases."""
    edge_faces = defaultdict(list)
    vertex_faces = defaultdict(set)
    vertex_edges = defaultdict(set)
    centers, normals = [], []
    for face, (indices, _) in enumerate(faces):
        polygon = [points[index] for index in indices]
        centers.append(_average(polygon))
        normal = model._normal(polygon)
        magnitude = math.sqrt(sum(value * value for value in normal))
        normals.append(tuple(value / magnitude for value in normal)
                       if magnitude else (0.,) * AXES)
        for first, second in zip(indices, indices[1:] + indices[:1]):
            edge = _edge(first, second)
            edge_faces[edge].append(face)
            vertex_faces[first].add(face)
            vertex_edges[first].add(edge)
            vertex_edges[second].add(edge)
    cosine = math.cos(math.radians(CREASE_ANGLE_DEGREES))
    creases = {
        edge for edge, adjacent in edge_faces.items()
        if len(adjacent) != 2 or
        sum(a * b for a, b in zip(normals[adjacent[0]], normals[adjacent[1]])) < cosine}
    moved = []
    for index, point in enumerate(points):
        edges = sorted(vertex_edges[index])
        sharp = [edge for edge in edges if edge in creases]
        boundary = any(len(edge_faces[edge]) != 2 for edge in edges)
        # Distant panels have no intermediate arch samples: retain their
        # authored perimeter so straight chords still clear the steered tyres.
        fixed = distant and (boundary or abs(point[0]) > DISTANT_ARCH_MIN_X)
        if index < controls_end or not edges or len(sharp) > 2 or fixed:
            moved.append(point)
        elif len(sharp) == 2:
            neighbors = [edge[0] if edge[1] == index else edge[1] for edge in sharp]
            moved.append(tuple(.75 * point[axis] + .125 * sum(points[n][axis]
                               for n in neighbors) for axis in range(AXES)))
        elif sharp:
            moved.append(point)
        else:
            face_mean = _average([centers[face] for face in sorted(vertex_faces[index])])
            edge_mean = _average([_average([points[a], points[b]]) for a, b in edges])
            valence = len(edges)
            moved.append(tuple((face_mean[axis] + 2 * edge_mean[axis] +
                                (valence - 3) * point[axis]) / valence
                               for axis in range(AXES)))
    if distant:
        result, parents = [], []
        linear = list(points)
        for face, (indices, materials) in enumerate(faces):
            # A triangle is already planar. Its center fan would add storage
            # and work without changing the distant surface.
            if len(indices) == TRIANGLE_VERTICES:
                result.append((indices, materials))
                parents.append(face)
                continue
            center = len(moved)
            moved.append(_average([moved[index] for index in indices]))
            linear.append(centers[face])
            result.extend(((first, second, center), materials)
                          for first, second in zip(indices, indices[1:] + indices[:1]))
            parents.extend([face] * len(indices))
        return _constrain_winding(linear, moved, result, parents, normals,
                                  model, coordinate_factor), result
    linear = list(points)
    midpoints = {}
    for edge, adjacent in edge_faces.items():
        samples = [points[index] for index in edge]
        if edge not in creases:
            samples.extend(centers[face] for face in adjacent)
        midpoints[edge] = len(moved)
        moved.append(_average(samples))
        linear.append(_average([points[index] for index in edge]))
    result, parents = [], []
    for face, (indices, materials) in enumerate(faces):
        center = len(moved)
        moved.append(centers[face])
        linear.append(centers[face])
        for corner, index in enumerate(indices):
            previous, following = indices[corner - 1], indices[(corner + 1) % len(indices)]
            result.append(((index, midpoints[_edge(index, following)], center,
                            midpoints[_edge(index, previous)]), materials))
            parents.append(face)
    return _constrain_winding(linear, moved, result, parents, normals,
                              model, coordinate_factor), result


def populate_shape(shape, name, model):
    """Populate a racing H3D shell while keeping native controls and lamps."""
    factor = 1 << shape.coordinate_fraction_bits
    distant = name == "car2"
    # Both racing shells omit the showroom's mirror heads and stalks.
    source = model._car(factor, simple=True)
    classic = model._car(simple=distant)
    controls_end = model.WHEEL_CONTROL_START + len(model.WHEEL_CENTERS) * model.WHEEL_CONTROL_COUNT
    shape.vertices = list(classic.vertices[:controls_end])
    shape.primitives = [primitive for primitive in classic.primitives
                        if primitive[0] == model.WHEEL_PRIMITIVE]
    original = [tuple(value / factor for value in point) for point in source.vertices]
    # Crown apertures are cut into the smoothed roof afterward. Their old
    # floating black overlays must not become part of the surface topology.
    overlays = {primitive for primitives in source._wheel_vents.values() for primitive in primitives}
    faces = [(indices, materials) for primitive in source.primitives
             for kind, _, materials, indices in (primitive,)
             if TRIANGLE_VERTICES <= kind <= MAX_POLYGON_VERTICES and primitive not in overlays]
    points, faces = _smooth_panels(original, faces, controls_end, model, distant=distant,
                                   coordinate_factor=factor)

    def quantized(point):
        return tuple(round(value * factor) / factor for value in point)

    for indices, materials in faces:
        polygon = [quantized(points[index]) for index in indices]
        for corner in range(1, len(polygon) - 1):
            triangle = polygon[0], polygon[corner], polygon[corner + 1]
            if len(set(triangle)) < TRIANGLE_VERTICES or model._normal(triangle) == [0, 0, 0]:
                continue
            shape.primitives.append((TRIANGLE_VERTICES, model.TWO_SIDED_FLAG, materials,
                                     tuple(shape.vertex(point) for point in triangle)))
    # Use original feature controls: smoothing a shared diameter endpoint
    # would distort the concentric tail lamps. Keep body references separate
    # from the first 32 mutable native bound/wheel-control slots.
    details = (model.POINT_PRIMITIVE, model.LINE_PRIMITIVE, model.SPHERE_PRIMITIVE)
    for kind, flags, materials, indices in source.primitives:
        if kind in details:
            shape.primitives.append((kind, flags, materials,
                                     tuple(shape.vertex(quantized(original[index])) for index in indices)))
