"""Rebuild the shortened source shoulder behind the McMurtry canopy.

The reduced mesh extends its deep side channel into the rear deck. Replace the
connected fan around that junction with a sampled roof and channel wall. Its
outer boundary stays shared with the surrounding canopy, deck and lower shell.
"""

from __future__ import annotations

from collections import defaultdict
import math

import mcmurtry_hypervision as detailed


ANCHOR_X_RANGE = (5.0, 7.0)
ANCHOR_Y_RANGE = (11.0, 14.0)
ANCHOR_Z_RANGE = (-30.0, -26.0)
OUTER_X_RANGE = (10.0, 12.5)
OUTER_Y_MINIMUM = 11.0
OUTER_Z_RANGE = (-22.0, -18.0)
CANOPY_Y_MINIMUM = 14.0
CANOPY_Z_MINIMUM = -23.5
REAR_X_MINIMUM = 8.0
REAR_Z_MAXIMUM = -32.0
REAR_Y_MINIMUM = 9.0
CONTOUR_X_STATIONS = (7.5, 9.5)
CONTOUR_SEARCH_START_Z = -28.0
CONTOUR_SEARCH_END_Z = -16.0
CONTOUR_SEARCH_STEP = .05
CONTOUR_MINIMUM_HEIGHT = 9.5
CONTOUR_BISECTION_STEPS = 16
CONTOUR_GRID = 1 / (1 << detailed.FRACTION_BITS["car1"])
PLATEAU_X = 9.0
PLATEAU_Z = -24.0
TOP_MATERIAL = 66
SIDE_MATERIAL = 65
EPSILON = 1e-9


def _inside(value, bounds):
    return bounds[0] <= value <= bounds[1]


def _one(candidates, name):
    if len(candidates) != 1:
        raise ValueError(f"Expected one {name} in the rear shoulder, found {len(candidates)}")
    return candidates[0]


def _leading_edge(surface, x):
    """Find the first opening ahead of the contiguous source upper deck."""
    last_z = CONTOUR_SEARCH_START_Z
    last = surface.skin_hit(x, last_z)
    if last is None or last.point[1] < CONTOUR_MINIMUM_HEIGHT:
        raise ValueError("Source shoulder must start on the upper rear deck")
    z = last_z + CONTOUR_SEARCH_STEP
    while z <= CONTOUR_SEARCH_END_Z:
        hit = surface.skin_hit(x, z)
        if hit is None or hit.point[1] < CONTOUR_MINIMUM_HEIGHT:
            lower, upper = last_z, z
            for _ in range(CONTOUR_BISECTION_STEPS):
                middle = (lower + upper) / 2
                candidate = surface.skin_hit(x, middle)
                if candidate is None or candidate.point[1] < CONTOUR_MINIMUM_HEIGHT:
                    upper = middle
                else:
                    lower, last = middle, candidate
            # Round the forward boundary toward the channel. Nearest rounding
            # can retract a roof edge and expose the floor through a thin gap.
            encoded_z = math.ceil((lower - EPSILON) / CONTOUR_GRID) * CONTOUR_GRID
            return (float(x), last.point[1], encoded_z)
        last_z, last = z, hit
        z += CONTOUR_SEARCH_STEP
    raise ValueError("Source shoulder has no forward channel boundary")


def repair_shoulder(source_points, source_faces, source_materials,
                    points, faces, materials):
    """Replace one local fan, preserving existing indices and adding four.

    Input is the positive rear half returned by extract_rear_half. Source car0
    supplies every new surface height and the channel's forward contour. The
    existing outer junction moves to that contour; no lower floor is repainted.
    """
    import numpy as np

    from mcmurtry_source_surface import SourceSurface

    points = np.asarray(points, dtype=float).copy()
    faces = np.asarray(faces, dtype=np.int64)
    materials = np.asarray(materials, dtype=np.int64)
    candidates = []
    for index, point in enumerate(points):
        if not (_inside(point[0], ANCHOR_X_RANGE) and
                _inside(point[1], ANCHOR_Y_RANGE) and
                _inside(point[2], ANCHOR_Z_RANGE)):
            continue
        incident = np.any(faces == index, axis=1)
        labels = set(materials[incident, 0])
        if TOP_MATERIAL in labels and SIDE_MATERIAL in labels:
            candidates.append(index)
    anchor = _one(candidates, "canopy/deck junction")
    incident = np.flatnonzero(np.any(faces == anchor, axis=1))
    adjacency = defaultdict(set)
    edge_faces = {}
    for index in incident:
        first, second = [vertex for vertex in faces[index] if vertex != anchor]
        adjacency[first].add(second)
        adjacency[second].add(first)
        edge_faces[frozenset((first, second))] = int(index)
    neighbors = list(adjacency)
    outer = _one([index for index in neighbors
                  if _inside(points[index, 0], OUTER_X_RANGE) and
                  points[index, 1] >= OUTER_Y_MINIMUM and
                  _inside(points[index, 2], OUTER_Z_RANGE)], "outer shoulder junction")
    canopy = _one([index for index in neighbors
                   if points[index, 1] >= CANOPY_Y_MINIMUM and
                   points[index, 2] >= CANOPY_Z_MINIMUM], "forward canopy junction")
    rear = _one([index for index in neighbors
                 if points[index, 0] >= REAR_X_MINIMUM and
                 points[index, 1] >= REAR_Y_MINIMUM and
                 points[index, 2] <= REAR_Z_MAXIMUM], "rear deck junction")
    if outer not in adjacency[rear]:
        raise ValueError("Shoulder roof must join its outer and rear junctions")
    path = [rear, outer]
    while path[-1] != canopy:
        following = adjacency[path[-1]] - {path[-2]}
        if len(following) != 1:
            raise ValueError("Rear shoulder replacement requires one connected surface fan")
        vertex = following.pop()
        if vertex in path:
            raise ValueError("Rear shoulder fan does not reach the canopy")
        path.append(vertex)
    front = path[-2]
    lower = path[2:-2]
    if not lower:
        raise ValueError("Rear shoulder fan must include its lower channel wall")
    removed = {edge_faces[frozenset((a, b))] for a, b in zip(path, path[1:])}
    roof_face = edge_faces[frozenset((rear, outer))]
    canopy_face = edge_faces[frozenset((front, canopy))]
    side_face = edge_faces[frozenset((lower[-1], front))]
    surface = SourceSurface(source_points, source_faces, source_materials)
    contour = [_leading_edge(surface, x) for x in CONTOUR_X_STATIONS]
    outer_roof_y = surface.top_y(points[outer, 0], points[outer, 2])
    points[outer] = _leading_edge(surface, points[outer, 0])
    # This shared vertex also anchors the existing fender roof. Retain its
    # source deck height while advancing its front edge: lowering the entire
    # adjoining roof to the channel-wall lip opens oblique rays above it.
    points[outer, 1] = max(points[outer, 1], outer_roof_y)
    join_z = contour[0][2]
    fraction = (join_z - points[anchor, 2]) / (points[front, 2] - points[anchor, 2])
    if not EPSILON < fraction < 1 - EPSILON:
        raise ValueError("Source shoulder closure must intersect the existing canopy edge")
    join_x = points[anchor, 0] + fraction * (points[front, 0] - points[anchor, 0])
    join = (join_x, surface.top_y(join_x, join_z), join_z)
    plateau = (PLATEAU_X, surface.top_y(PLATEAU_X, PLATEAU_Z), PLATEAU_Z)
    first = len(points)
    points = np.concatenate((points, np.asarray([join, *contour, plateau])))
    join_index = first
    contour_indices = list(range(first + 1, first + 1 + len(contour)))
    plateau_index = len(points) - 1
    roof_boundary = [rear, anchor, join_index, *contour_indices, outer]
    new_faces, new_materials = [], []

    def add(face, palette):
        new_faces.append(tuple(face))
        new_materials.append(palette)

    for a, b in zip(roof_boundary, roof_boundary[1:] + roof_boundary[:1]):
        add((plateau_index, a, b), materials[roof_face])
    add((canopy, front, join_index), materials[canopy_face])
    add((canopy, join_index, anchor), materials[canopy_face])
    add((front, lower[-1], join_index), materials[side_face])
    wall_boundary = [outer, *reversed(contour_indices), join_index, *reversed(lower[1:])]
    for a, b in zip(wall_boundary, wall_boundary[1:]):
        add((lower[0], a, b), materials[side_face])
    # The cyclic order above follows (rear, anchor, outer). Reverse the whole
    # replacement if the input fan was authored with the opposite winding.
    original = list(faces[roof_face])
    start = original.index(rear)
    if original[(start + 1) % len(original)] != anchor:
        new_faces = [(a, c, b) for a, b, c in new_faces]
    keep = np.array([index not in removed for index in range(len(faces))])
    return (points, np.concatenate((faces[keep], np.asarray(new_faces, dtype=np.int64))),
            np.concatenate((materials[keep], np.asarray(new_materials, dtype=np.int64))))
