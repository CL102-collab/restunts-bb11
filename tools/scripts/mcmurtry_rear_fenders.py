"""Restore the source profiles of sparse rear fender and bumper panels."""

from __future__ import annotations

from collections import defaultdict

import mcmurtry_model as model
from mcmurtry_source_surface import SourceSurface


FENDER_Z_RANGE = (-34.0, -12.0)
FENDER_MINIMUM_X = 16.0
FENDER_MINIMUM_Y = 9.0
MINIMUM_FENDER_EDGE_SPAN = 8.0
CORNER_SAMPLE_Z = -37.5
CORNER_FORWARD_Z = -35.0
CORNER_REAR_Z = -38.0
CORNER_MINIMUM_Y = 3.0
CORNER_MINIMUM_OUTER_X = 18.0
QUANTIZATION_ALLOWANCE = 1 / 16
ARCH_CENTER_Z = -25.0
ARCH_LIP_MINIMUM_X = 18.5
ARCH_LIP_SAMPLE_STEP = 1 / 32
CORNER_RIM_MINIMUM_Y = 7.5
CORNER_RIM_MAXIMUM_Y = 13.0
CORNER_RIM_MAXIMUM_Z = -38.5
PROFILE_SAMPLES = 17
PROFILE_CHORD_ALLOWANCE = 1 / 16
CORNER_RIM_MINIMUM_X = 14.0
REAR_BODY_MAXIMUM_Y = 14.0
MAXIMUM_PROFILE_DEPARTURE = .25
MAXIMUM_LINER_INSET = .5
LINER_SAMPLE_WEIGHTS = (.25, .5, .75, 1.)


def repair_fenders(source_points, source_faces, source_materials,
                   points, faces, materials):
    """Lift exterior paint panels onto car0 and split their long shared edges.

    The source body is sampled independently of the wing and its endplates.
    Inner wheel-well faces retain their dark materials. New points are inserted
    into every face using an affected edge, so the shell keeps shared topology.
    """
    import numpy as np
    from scipy.sparse import coo_matrix
    from scipy.sparse.csgraph import connected_components

    source_points = np.asarray(source_points, dtype=float)
    source_faces = np.asarray(source_faces, dtype=np.int64)
    source_materials = np.asarray(source_materials, dtype=np.int64)
    edges = np.concatenate((source_faces[:, [0, 1]], source_faces[:, [1, 2]],
                            source_faces[:, [2, 0]]))
    graph = coo_matrix((np.ones(len(edges)), edges.T),
                       shape=(len(source_points), len(source_points)))
    _, components = connected_components(graph, directed=False)
    body = np.bincount(components[source_faces[:, 0]]).argmax()
    selected = ((components[source_faces[:, 0]] == body) &
                (np.ptp(source_materials, axis=1) > 0))
    source_triangles = source_points[source_faces]
    selected &= ~((source_triangles[:, :, 2].max(axis=1) < FENDER_Z_RANGE[0]) &
                  (source_triangles[:, :, 1].max(axis=1) > REAR_BODY_MAXIMUM_Y))
    source = SourceSurface(source_points, source_faces[selected], source_materials[selected])

    def envelope_point(first, second, point, axis):
        """Allow curved-profile chords a bounded offset from the literal skin."""
        source_limit = point[axis] + MAXIMUM_PROFILE_DEPARTURE
        station = (point[2] - first[2]) / (second[2] - first[2])
        chord = first + station * (second - first)
        offset = point[axis] - chord[axis]
        for weight in np.linspace(0., 1., PROFILE_SAMPLES)[1:-1]:
            sample = first + weight * (second - first)
            if axis == 1:
                target = source.top_y(sample[0], sample[2])
            else:
                hit = source.ray_hit("side", sample[1], sample[2])
                if hit is None:
                    continue
                target = hit.point[0]
            influence = (weight / station if weight < station else
                         (1 - weight) / (1 - station))
            offset = max(offset, (target + PROFILE_CHORD_ALLOWANCE - sample[axis]) / influence)
        point[axis] = min(chord[axis] + offset, source_limit)
        return point

    vertices = np.asarray(points, dtype=float).copy()
    triangles = [tuple(map(int, face)) for face in faces]
    palettes = [tuple(map(int, palette)) for palette in materials]
    top_faces = [face for face, palette in zip(triangles, palettes)
                 if palette == model.PAINT_TOP]
    top_vertices = set(vertex for face in top_faces for vertex in face)
    paint_vertices = set(vertex for face, palette in zip(triangles, palettes)
                         if len(set(palette)) > 1 for vertex in face)
    for vertex in sorted(top_vertices):
        x, y, z = vertices[vertex]
        if (x >= FENDER_MINIMUM_X and y >= FENDER_MINIMUM_Y and
                FENDER_Z_RANGE[0] <= z <= FENDER_Z_RANGE[1]):
            vertices[vertex, 1] = max(y, source.top_y(x, z) + QUANTIZATION_ALLOWANCE)
    for vertex in sorted(paint_vertices):
        x, y, z = vertices[vertex]
        if (x >= CORNER_RIM_MINIMUM_X and
                CORNER_RIM_MINIMUM_Y <= y <= CORNER_RIM_MAXIMUM_Y and
                z <= CORNER_RIM_MAXIMUM_Z):
            vertices[vertex, 1] = max(y, source.top_y(x, z) + QUANTIZATION_ALLOWANCE)

    edge_faces = defaultdict(list)
    for index, face in enumerate(triangles):
        for first, second in zip(face, (face[1], face[2], face[0])):
            edge_faces[tuple(sorted((first, second)))].append(index)
    requests = []
    for edge, incident in sorted(edge_faces.items()):
        a, b = vertices[list(edge)]
        has_top = any(palettes[index] == model.PAINT_TOP for index in incident)
        if (has_top and min(a[0], b[0]) >= FENDER_MINIMUM_X and
                all(palettes[index] == model.PAINT_TOP for index in incident) and
                min(a[1], b[1]) >= FENDER_MINIMUM_Y and
                min(a[2], b[2]) >= FENDER_Z_RANGE[0] and
                max(a[2], b[2]) <= FENDER_Z_RANGE[1] and
                abs(a[2] - b[2]) > MINIMUM_FENDER_EDGE_SPAN):
            point = (a + b) / 2
            point[1] = source.top_y(point[0], point[2]) + QUANTIZATION_ALLOWANCE
            requests.append((edge, envelope_point(a, b, point, 1)))
        elif (min(a[2], b[2]) < CORNER_REAR_Z and
                max(a[2], b[2]) > CORNER_FORWARD_Z and
                max(a[0], b[0]) >= CORNER_MINIMUM_OUTER_X and
                min(a[1], b[1]) >= CORNER_MINIMUM_Y and
                all(len(set(palettes[index])) > 1 for index in incident)):
            point = a + (b - a) * (CORNER_SAMPLE_Z - a[2]) / (b[2] - a[2])
            if has_top and all(palettes[index] == model.PAINT_TOP for index in incident):
                point[1] = source.top_y(point[0], point[2]) + QUANTIZATION_ALLOWANCE
                point = envelope_point(a, b, point, 1)
            else:
                hit = source.ray_hit("side", point[1], point[2])
                if hit is None:
                    hit = source.nearest_hit("side", point[1], point[2])
                point[0] = hit.point[0] + QUANTIZATION_ALLOWANCE
                point = envelope_point(a, b, point, 0)
            requests.append((edge, point))

    points_out = vertices.tolist()
    for (first, second), point in requests:
        new_vertex = len(points_out)
        points_out.append(point.tolist())
        new_faces, new_materials = [], []
        for face, palette in zip(triangles, palettes):
            if first not in face or second not in face:
                new_faces.append(face)
                new_materials.append(palette)
                continue
            for corner in range(len(face)):
                a, b = face[corner], face[(corner + 1) % len(face)]
                if {a, b} == {first, second}:
                    opposite = face[(corner + 2) % len(face)]
                    new_faces.extend(((a, new_vertex, opposite),
                                      (new_vertex, b, opposite)))
                    new_materials.extend((palette, palette))
                    break
        triangles, palettes = new_faces, new_materials

    # The reduced crown also serves as the wheel opening's lip. Separate these
    # profiles so lifting the crown cannot expose the dark wheel well below it.
    edge_vertices = set(vertex for face, palette in zip(triangles, palettes)
                        if palette == model.PAINT_EDGE for vertex in face)
    candidates = [vertex for vertex in top_vertices & edge_vertices
                  if vertices[vertex, 0] >= ARCH_LIP_MINIMUM_X and
                  abs(vertices[vertex, 2] - ARCH_CENTER_Z) < model.WHEEL_RADIUS / 2]
    if candidates:
        crown = min(candidates, key=lambda vertex: abs(vertices[vertex, 2] - ARCH_CENTER_Z))
        _, crown_y, crown_z = points_out[crown]
        lip = None
        for y in np.arange(model.WHEEL_CENTERS[0][1], crown_y, ARCH_LIP_SAMPLE_STEP):
            hit = source.ray_hit("side", y, crown_z)
            if hit is not None and hit.point[0] >= ARCH_LIP_MINIMUM_X:
                lip = (hit.point[0], y + QUANTIZATION_ALLOWANCE, crown_z)
                break
        if lip is not None:
            new_vertex = len(points_out)
            points_out.append(lip)
            keep = [palette in (model.PAINT_TOP, model.PAINT_SIDE) for palette in palettes]
            incident = defaultdict(list)
            for index, face in enumerate(triangles):
                if crown not in face:
                    continue
                for corner in range(len(face)):
                    first, second = face[corner], face[(corner + 1) % len(face)]
                    if crown in (first, second):
                        incident[tuple(sorted((first, second)))].append((index, first, second))
            bridges = []
            for edges in incident.values():
                retained = [(first, second) for index, first, second in edges if keep[index]]
                if retained and any(not keep[index] for index, _, _ in edges):
                    first, second = retained[0]
                    bridges.append((second, first, new_vertex))
            triangles = [tuple(new_vertex if vertex == crown and not retain else vertex
                               for vertex in face)
                         for face, retain in zip(triangles, keep)]
            for bridge in bridges:
                neighbor = next(vertex for vertex in bridge if vertex not in (crown, new_vertex))
                if points_out[neighbor][2] < crown_z:
                    triangles.append(bridge)
                    palettes.append(model.PAINT_SIDE)
                    continue
                adjoining = next((index for index, (face, palette) in
                                  enumerate(zip(triangles, palettes))
                                  if palette == model.PAINT_EDGE and
                                  neighbor in face and new_vertex in face), None)
                if adjoining is None:
                    triangles.append(bridge)
                    palettes.append(model.PAINT_SIDE)
                    continue
                face = triangles[adjoining]
                for corner in range(len(face)):
                    first, second = face[corner], face[(corner + 1) % len(face)]
                    if {first, second} == {neighbor, new_vertex}:
                        opposite = face[(corner + 2) % len(face)]
                        triangles[adjoining] = (opposite, first, crown)
                        palettes[adjoining] = model.PAINT_SIDE
                        triangles.append((opposite, crown, second))
                        palettes.append(model.PAINT_SIDE)
                        break

    # Use the existing bowed back rim as the diagonal of the outer corner.
    # Its original diagonal bypassed that curve and passed behind the liner.
    adjacency = defaultdict(list)
    for index, face in enumerate(triangles):
        for first, second in zip(face, (face[1], face[2], face[0])):
            adjacency[tuple(sorted((first, second)))].append(index)
    for index, (face, palette) in enumerate(zip(triangles, palettes)):
        if palette != model.PAINT_EDGE:
            continue
        coordinates = np.asarray([points_out[vertex] for vertex in face])
        if (coordinates[:, 0].min() < CORNER_RIM_MINIMUM_X or
                coordinates[:, 1].min() < CORNER_MINIMUM_Y or
                coordinates[:, 2].max() > CORNER_RIM_MAXIMUM_Z):
            continue
        for corner in range(len(face)):
            first, second = face[corner], face[(corner + 1) % len(face)]
            opposite = face[(corner + 2) % len(face)]
            neighbors = adjacency[tuple(sorted((first, second)))]
            other = next((neighbor for neighbor in neighbors if neighbor != index and
                          palettes[neighbor] == model.PAINT_SIDE), None)
            if other is None:
                continue
            bow = next(vertex for vertex in triangles[other] if vertex not in (first, second))
            if bow < len(vertices) or points_out[bow][2] <= CORNER_REAR_Z:
                continue
            triangles[index] = (opposite, first, bow)
            triangles[other] = (opposite, bow, second)
            palettes[index] = model.PAINT_SIDE
            break

    # A reduced inner liner can pierce an otherwise continuous painted panel.
    # Where car0 confirms exterior paint, keep dark-only controls behind the
    # reconstructed skin; shared paint-rim controls and real vents stay fixed.
    paint_mask = np.ptp(np.asarray(palettes), axis=1) > 0
    paint_indices = np.asarray(triangles, dtype=np.int64)[paint_mask]
    paint_controls = set(paint_indices.flat)
    painted = SourceSurface(np.asarray(points_out), paint_indices,
                            np.asarray(palettes)[paint_mask])
    literal_body = components[source_faces[:, 0]] == body
    literal = SourceSurface(source_points, source_faces[literal_body],
                            source_materials[literal_body])
    for vertex, point in enumerate(points_out):
        x, y, z = point
        if (vertex in paint_controls or x < ARCH_LIP_MINIMUM_X or
                not FENDER_Z_RANGE[0] <= z <= FENDER_Z_RANGE[1] or
                y < FENDER_MINIMUM_Y):
            continue
        limit = x
        for face in triangles:
            if vertex not in face:
                continue
            others = [index for index in face if index != vertex]
            for weight in LINER_SAMPLE_WEIGHTS:
                for fraction in LINER_SAMPLE_WEIGHTS:
                    rest = ((1 - weight) * fraction * np.asarray(points_out[others[0]]) +
                            (1 - weight) * (1 - fraction) * np.asarray(points_out[others[1]]))
                    sample = weight * np.asarray(point) + rest
                    original = literal.ray_hit("side", sample[1], sample[2])
                    if original is None or len(set(original.materials)) == 1:
                        continue
                    outer = painted.ray_hit("side", sample[1], sample[2])
                    if outer is not None:
                        limit = min(limit, (outer.point[0] - QUANTIZATION_ALLOWANCE - rest[0]) /
                                    weight)
        points_out[vertex][0] = max(x - MAXIMUM_LINER_INSET, limit)
    return (np.asarray(points_out, dtype=float), np.asarray(triangles, dtype=np.int64),
            np.asarray(palettes, dtype=np.int64))
