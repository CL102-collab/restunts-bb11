"""Join the separately reduced painted canopy sill to the adjacent body skin."""

from collections import Counter, defaultdict
import heapq
import math

from mcmurtry_source_surface import SourceSurface
import mcmurtry_model as model


SILL_X_BOUNDS = (7., 10.)
SILL_Y_BOUNDS = (7., 14.)
SILL_Z_BOUNDS = (-19., 1.)
SILL_MINIMUM_TRIANGLES = 5
ENDPOINT_MAXIMUM_DISTANCE = .4
SHARED_SILL_VERTEX_DISTANCE = .25
MAXIMUM_SOURCE_PROJECTION = 1.5


def _paint_components(faces, palettes):
    adjacent = defaultdict(list)
    remaining = set()
    for index, (face, palette) in enumerate(zip(faces, palettes)):
        if len(set(palette)) > 1:
            remaining.add(index)
            for vertex in face:
                adjacent[vertex].append(index)
    components = []
    while remaining:
        pending = [min(remaining)]
        remaining.remove(pending[0])
        component = []
        while pending:
            index = pending.pop()
            component.append(index)
            for vertex in faces[index]:
                for neighbor in adjacent[vertex]:
                    if neighbor in remaining:
                        remaining.remove(neighbor)
                        pending.append(neighbor)
        components.append(component)
    return components


def _boundary(faces, component):
    edges = Counter(tuple(sorted((a, b))) for index in component
                    for a, b in zip(faces[index], (faces[index][1], faces[index][2], faces[index][0])))
    adjacent = defaultdict(list)
    for (a, b), count in edges.items():
        if count == 1:
            adjacent[a].append(b)
            adjacent[b].append(a)
    return adjacent


def _path(points, adjacent, start, end, excluded=()):
    pending = [(0., (start,))]
    visited = set(excluded)
    while pending:
        distance, path = heapq.heappop(pending)
        vertex = path[-1]
        if vertex in visited:
            continue
        if vertex == end:
            return list(path)
        visited.add(vertex)
        for neighbor in adjacent[vertex]:
            if neighbor not in visited:
                heapq.heappush(pending, (distance + math.dist(points[vertex], points[neighbor]),
                                       path + (neighbor,)))
    raise ValueError("The canopy sill must have a continuous painted boundary")


def repair_canopy_seam(source_points, source_faces, source_materials, points, faces, palettes):
    """Close the source-painted gap between two independently reduced shells.

    The canopy and body have distinct boundary vertices along the painted sill.
    Their long low-poly edges no longer coincide. Join those existing edges,
    sampling their heights from source paint, instead of exposing the carbon
    underside between them. Vertex indices remain stable for the front seam.
    """
    import numpy as np

    points = np.asarray(points, dtype=float).copy()
    faces = np.asarray(faces, dtype=np.int64)
    palettes = np.asarray(palettes, dtype=np.int64)
    components = _paint_components(faces, palettes)
    body = max(components, key=len)
    candidates = []
    for component in components:
        vertices = points[np.unique(faces[component])]
        if (len(component) >= SILL_MINIMUM_TRIANGLES and
                all(vertices[:, axis].min() >= bounds[0] and
                    vertices[:, axis].max() <= bounds[1]
                    for axis, bounds in enumerate((SILL_X_BOUNDS, SILL_Y_BOUNDS, SILL_Z_BOUNDS)))):
            candidates.append(component)
    if len(candidates) != 1:
        raise ValueError("Expected one separately reduced painted canopy sill")
    sill = _boundary(faces, candidates[0])
    body_boundary = _boundary(faces, body)
    start = max(sill, key=lambda vertex: points[vertex, 2])
    end = min(sill, key=lambda vertex: points[vertex, 2])
    first = _path(points, sill, start, end)
    second = _path(points, sill, start, end, excluded=first[1:-1])
    rim = min((first, second), key=lambda path: sum(points[i, 1] for i in path) / len(path))
    endpoints = [min(body_boundary, key=lambda vertex: math.dist(points[vertex], points[index]))
                 for index in (start, end)]
    if any(math.dist(points[near], points[original]) > ENDPOINT_MAXIMUM_DISTANCE
           for near, original in zip(endpoints, (start, end))):
        raise ValueError("The canopy sill endpoints must meet the body skin")
    skin = _path(points, body_boundary, *endpoints)
    source_faces = np.asarray(source_faces)
    source_materials = np.asarray(source_materials)
    painted = np.ptp(source_materials, axis=1) > 0
    surface = SourceSurface(source_points, source_faces[painted], source_materials[painted])
    for index in set(rim + skin):
        x, y, z = points[index]
        sampled = surface.top_y(x, z)
        if abs(sampled - y) <= MAXIMUM_SOURCE_PROJECTION:
            points[index, 1] = sampled
    # Coincident source seams were reduced independently. Reuse the nearby
    # body coordinates at their joins so encoding welds the painted panels.
    for index in rim:
        nearest = min(skin, key=lambda vertex: math.dist(points[index], points[vertex]))
        if math.dist(points[index], points[nearest]) <= SHARED_SILL_VERTEX_DISTANCE:
            points[index] = points[nearest]
    # Walk the common longitudinal parameter, retaining every original edge.
    joined = []
    a = b = 0
    while a < len(rim) - 1 or b < len(skin) - 1:
        if b == len(skin) - 1 or (a < len(rim) - 1 and
                                  points[rim[a + 1], 2] > points[skin[b + 1], 2]):
            joined.append((rim[a], skin[b], rim[a + 1]))
            a += 1
        else:
            joined.append((rim[a], skin[b], skin[b + 1]))
            b += 1
    return (points, np.concatenate((faces, np.asarray(joined))),
            np.concatenate((palettes, np.tile(model.PAINT_SIDE, (len(joined), 1)))))
