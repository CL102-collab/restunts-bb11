"""Clip and join source-derived racing surfaces during offline LOD authoring."""

from __future__ import annotations

from collections import Counter, defaultdict


FRONT_SEAM_Z = 4.0
GEOMETRY_EPSILON = 1e-9
WELD_DECIMAL_PLACES = 8
MAXIMUM_SEAM_SPUR_LENGTH = 1 / 8
MAXIMUM_CUT_FITTING_SPAN = .5


def _compact(points, triangles, materials):
    """Weld coincident vertices, removing collapsed and duplicate triangles."""
    import numpy as np

    vertices, faces, palettes = [], [], []
    lookup, seen = {}, set()
    for face, palette in zip(triangles, materials):
        triangle = np.asarray([points[index] for index in face], dtype=float)
        normal = np.cross(triangle[1] - triangle[0], triangle[2] - triangle[0])
        if np.linalg.norm(normal) <= GEOMETRY_EPSILON:
            continue
        indices = []
        for point in triangle:
            key = tuple(np.round(point, WELD_DECIMAL_PLACES))
            if key not in lookup:
                lookup[key] = len(vertices)
                vertices.append(point)
            indices.append(lookup[key])
        key = tuple(sorted(indices))
        if len(set(indices)) != len(indices) or key in seen:
            continue
        seen.add(key)
        faces.append(indices)
        palettes.append(palette)
    faces = np.asarray(faces, dtype=np.int64).reshape((-1, 3))
    vertices = np.asarray(vertices, dtype=float).reshape((-1, 3))
    used, inverse = np.unique(faces, return_inverse=True)
    return (vertices[used], inverse.reshape((-1, 3)),
            np.asarray(palettes, dtype=np.int64))


def _clip_polygon(polygon, axis, boundary, keep_greater):
    """Intersect a triangle polygon with a coordinate half-space."""
    result = []
    for first, second in zip(polygon, polygon[1:] + polygon[:1]):
        first_distance = first[axis] - boundary
        second_distance = second[axis] - boundary
        if not keep_greater:
            first_distance, second_distance = -first_distance, -second_distance
        first_inside = first_distance >= -GEOMETRY_EPSILON
        second_inside = second_distance >= -GEOMETRY_EPSILON
        if first_inside:
            point = first.copy()
            if abs(first_distance) <= GEOMETRY_EPSILON:
                point[axis] = boundary
            result.append(point)
        if first_inside != second_inside:
            fraction = (boundary - first[axis]) / (second[axis] - first[axis])
            point = first + fraction * (second - first)
            point[axis] = boundary
            result.append(point)
    return result


def _cut_adjacency(points, triangles, seam_z):
    """Return open edges lying on the replacement plane."""
    counts = Counter(tuple(sorted((first, second)))
                     for face in triangles
                     for first, second in zip(face, (face[1], face[2], face[0])))
    adjacency = defaultdict(list)
    for (first, second), count in sorted(counts.items()):
        if (count == 1 and abs(points[first, 2] - seam_z) <= GEOMETRY_EPSILON and
                abs(points[second, 2] - seam_z) <= GEOMETRY_EPSILON):
            adjacency[first].append(second)
            adjacency[second].append(first)
    return adjacency


def _cap_cut_fittings(points, triangles, materials, adjacency, outer_boundary):
    """Close tiny trim pieces crossing the seam without joining them to the body."""
    import numpy as np

    remaining = set(adjacency) - set(outer_boundary)
    faces, palettes = list(triangles), list(materials)
    while remaining:
        start = min(remaining)
        loop, previous, current = [], None, start
        while True:
            if len(adjacency[current]) != 2:
                raise ValueError("A detached cut fitting must have a closed boundary")
            loop.append(current)
            following = [vertex for vertex in adjacency[current] if vertex != previous]
            previous, current = current, following[0]
            if current == start:
                break
            if current in loop:
                raise ValueError("A detached cut fitting has a self-connected boundary")
        remaining.difference_update(loop)
        if np.max(np.ptp(points[loop], axis=0)) > MAXIMUM_CUT_FITTING_SPAN:
            raise ValueError("A detached cut surface is too large to cap as trim")
        edge = set(loop[:2])
        palette = next(palette for face, palette in zip(triangles, materials)
                       if edge.issubset(face))
        for corner in range(1, len(loop) - 1):
            face = [loop[0], loop[corner], loop[corner + 1]]
            triangle = points[face]
            normal = np.cross(triangle[1] - triangle[0], triangle[2] - triangle[0])
            if normal[2] < 0:
                face.reverse()
            faces.append(face)
            palettes.append(palette)
    return np.asarray(faces, dtype=np.int64), np.asarray(palettes, dtype=np.int64)


def extract_rear_half(points, triangles, materials, seam_z=FRONT_SEAM_Z):
    """Return the positive-X rear mesh and its ordered front boundary.

    Triangle clipping preserves the source winding and every paint-table entry.
    The boundary runs from the upper centreline, around the right side, to the
    lower centreline. Sub-pixel pendant edges at the glass/body junction are
    collapsed into their attached seam vertex before the new front is joined.
    Small detached trim sections are capped at the cut plane.
    """
    import numpy as np

    points = np.asarray(points, dtype=float)
    clipped_points, clipped_faces, clipped_materials = [], [], []
    for face, palette in zip(triangles, materials):
        polygon = _clip_polygon(list(points[list(face)]), 0, 0.0, True)
        if polygon:
            polygon = _clip_polygon(polygon, 2, seam_z, False)
        if len(polygon) < 3:
            continue
        start = len(clipped_points)
        clipped_points.extend(polygon)
        for corner in range(1, len(polygon) - 1):
            clipped_faces.append((start, start + corner, start + corner + 1))
            clipped_materials.append(palette)
    vertices, faces, palettes = _compact(clipped_points, clipped_faces, clipped_materials)
    while True:
        adjacency = _cut_adjacency(vertices, faces, seam_z)
        merge = None
        for vertex, neighbors in sorted(adjacency.items()):
            if len(neighbors) != 1 or abs(vertices[vertex, 0]) <= GEOMETRY_EPSILON:
                continue
            neighbor = neighbors[0]
            if (len(adjacency[neighbor]) > 2 and
                    np.linalg.norm(vertices[vertex] - vertices[neighbor]) <=
                    MAXIMUM_SEAM_SPUR_LENGTH):
                merge = vertex, neighbor
                break
        if merge is None:
            break
        vertex, neighbor = merge
        vertices[vertex] = vertices[neighbor]
        vertices, faces, palettes = _compact(vertices, faces, palettes)

    centre = [vertex for vertex in adjacency
              if abs(vertices[vertex, 0]) <= GEOMETRY_EPSILON]
    if len(centre) != 2:
        raise ValueError("The rear cut must meet the centreline at its top and bottom")
    start, end = sorted(centre, key=lambda vertex: -vertices[vertex, 1])
    order, previous, current = [], None, start
    while True:
        order.append(current)
        if current == end:
            break
        following = [vertex for vertex in adjacency[current] if vertex != previous]
        if len(following) != 1 or following[0] in order:
            raise ValueError("The rear cut must provide one continuous outer boundary")
        previous, current = current, following[0]
    if len(order) != len(adjacency):
        faces, palettes = _cap_cut_fittings(vertices, faces, palettes, adjacency, order)
    return vertices, faces, palettes, np.asarray(order, dtype=np.int64)


def mirror_half(points, triangles, materials):
    """Reflect positive-X triangles, reversing winding and welding the centre."""
    import numpy as np

    vertices = np.asarray(points, dtype=float)
    faces = np.asarray(triangles, dtype=np.int64)
    palettes = np.asarray(materials, dtype=np.int64)
    if np.any(vertices[:, 0] < -GEOMETRY_EPSILON):
        raise ValueError("A mirrored half must have nonnegative X coordinates")
    reflected = vertices.copy()
    reflected[:, 0] *= -1
    return _compact(np.concatenate((vertices, reflected)),
                    np.concatenate((faces, faces[:, (0, 2, 1)] + len(vertices))),
                    np.concatenate((palettes, palettes)))
