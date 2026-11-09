"""Compact mirror fittings measured from the positive half of car0."""

from __future__ import annotations

import math


BLACK_MATERIAL = 0
MIRROR_MINIMUM_X = 12.5
MIRROR_MAXIMUM_X = 19.0
MIRROR_MINIMUM_Y = 10.0
MIRROR_MINIMUM_Z = 5.0
MIRROR_MAXIMUM_Z = 12.0
POD_MINIMUM_Y = 13.5
POD_VERTEX_LIMIT = 8
STALK_BASE_HEIGHT_BAND = 1.2
POD_ATTACHMENT_WIDTH = .7
POD_ATTACHMENT_INSET_X = .15
POD_ATTACHMENT_INSET_Y = .2
STALK_RADIUS = .22
STALK_EMBED_DEPTH = .08
STALK_RING_VERTICES = 3
EPSILON = 1e-9


def _largest_component(triangles, np):
    """Weld source coordinates before identifying the mirror and its stalk."""
    points, inverse = np.unique(triangles.reshape(-1, 3), axis=0, return_inverse=True)
    faces = inverse.reshape(-1, 3)
    parent = list(range(len(points)))

    def root(index):
        while parent[index] != index:
            parent[index] = parent[parent[index]]
            index = parent[index]
        return index

    for face in faces:
        first = root(face[0])
        for index in face[1:]:
            parent[root(index)] = first
    labels = np.array([root(index) for index in range(len(points))])
    groups, counts = np.unique(labels, return_counts=True)
    return points[labels == groups[np.argmax(counts)]]


def _pod_hull(points, np):
    """Retain eight actual source vertices with the least hull-volume loss."""
    from scipy.spatial import ConvexHull

    points = points[ConvexHull(points).vertices]
    while len(points) > POD_VERTEX_LIMIT:
        candidates = []
        for index in range(len(points)):
            reduced = np.delete(points, index, axis=0)
            candidates.append((ConvexHull(reduced).volume, index))
        _, index = max(candidates)
        points = np.delete(points, index, axis=0)
    hull = ConvexHull(points)
    faces = []
    for face, equation in zip(hull.simplices, hull.equations):
        first, second, third = points[face]
        if np.dot(np.cross(second - first, third - first), equation[:3]) < 0:
            face = face[[0, 2, 1]]
        faces.append(tuple(map(int, face)))
    return points, faces


def build_mirror_details(source_points, source_faces, source_materials, surface=None):
    """Return positive-half points, triangles and palettes for one mirror.

    The closed pod uses eight source vertices. A six-vertex triangular stalk
    joins its lower inside corner to the sampled hood. The caller mirrors both
    coordinates and winding together with the rest of the front reconstruction.
    """
    import numpy as np

    from mcmurtry_source_surface import SourceSurface

    points = np.asarray(source_points, dtype=float)
    materials = np.asarray(source_materials)
    triangles = points[np.asarray(source_faces)]
    centers = triangles.mean(axis=1)
    selected = (np.all(materials == BLACK_MATERIAL, axis=1) &
                (centers[:, 0] >= MIRROR_MINIMUM_X) &
                (centers[:, 0] <= MIRROR_MAXIMUM_X) &
                (centers[:, 1] >= MIRROR_MINIMUM_Y) &
                (centers[:, 2] >= MIRROR_MINIMUM_Z) &
                (centers[:, 2] <= MIRROR_MAXIMUM_Z))
    if not selected.any():
        raise ValueError("Source has no positive-half mirror")
    source = _largest_component(triangles[selected], np)
    pod_source = source[source[:, 1] >= POD_MINIMUM_Y]
    if len(pod_source) < POD_VERTEX_LIMIT:
        raise ValueError("Source mirror has insufficient pod geometry")
    pod, faces = _pod_hull(pod_source, np)
    base_source = source[source[:, 1] <= source[:, 1].min() + STALK_BASE_HEIGHT_BAND]
    base = base_source.mean(axis=0)
    inner = pod_source[(pod_source[:, 0] <= pod_source[:, 0].min() + POD_ATTACHMENT_WIDTH) &
                       (pod_source[:, 1] <= np.median(pod_source[:, 1]))]
    attachment = inner.mean(axis=0)
    attachment += np.array((POD_ATTACHMENT_INSET_X, POD_ATTACHMENT_INSET_Y, 0))
    if surface is None:
        surface = SourceSurface(source_points, source_faces, source_materials)
    base[1] = surface.top_y(base[0], base[2]) - STALK_EMBED_DEPTH
    direction = attachment - base
    direction /= np.linalg.norm(direction)
    radial = np.array((1.0, 0.0, 0.0))
    radial -= direction * np.dot(radial, direction)
    radial /= max(np.linalg.norm(radial), EPSILON)
    lateral = np.cross(direction, radial)
    stalk = []
    for center in (base, attachment):
        for index in range(STALK_RING_VERTICES):
            angle = 2 * math.pi * index / STALK_RING_VERTICES
            stalk.append(center + STALK_RADIUS * (math.cos(angle) * radial +
                                                  math.sin(angle) * lateral))
    stalk = np.asarray(stalk)
    for vertex in stalk[:STALK_RING_VERTICES]:
        vertex[1] = surface.top_y(vertex[0], vertex[2]) - STALK_EMBED_DEPTH
    stalk_faces = [(0, 1, 2), (3, 4, 5)]
    for first in range(STALK_RING_VERTICES):
        second = (first + 1) % STALK_RING_VERTICES
        stalk_faces.extend(((first, second, second + STALK_RING_VERTICES),
                            (first, second + STALK_RING_VERTICES, first + STALK_RING_VERTICES)))
    center = stalk.mean(axis=0)
    for face in stalk_faces:
        first, second, third = stalk[list(face)]
        normal = np.cross(second - first, third - first)
        if np.dot(normal, (first + second + third) / 3 - center) < 0:
            face = (face[0], face[2], face[1])
        faces.append(tuple(index + len(pod) for index in face))
    result_points = np.concatenate((pod, stalk))
    result_faces = np.asarray(faces)
    palette = materials[np.flatnonzero(selected)[0]]
    result_materials = np.tile(palette, (len(faces), 1))
    return result_points, result_faces, result_materials
