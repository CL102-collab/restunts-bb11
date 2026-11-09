"""Offline material-constrained quadric reduction of the McMurtry showroom.

Normal generation uses the retained LOD cache. This authoring pass requires
NumPy and SciPy and never imports the original GLB or the native racing shell.
"""

from __future__ import annotations

import heapq
import itertools
import math


PAINT_LABEL = 127
BOUNDARY_QUADRIC_WEIGHT = 50.0
MINIMUM_FACE_WEIGHT = .001
GEOMETRY_EPSILON = 1e-9
SOLVE_EPSILON = 1e-12
MINIMUM_NORMAL_COSINE = .1
MAXIMUM_EDGE_EXCURSION = 2.0
WING_MINIMUM_Y = 8.0
WING_MAXIMUM_Z = -25.0
WING_QUADRIC_WEIGHT = 20.0
MINIMUM_WING_COMPONENT_AREA = 3.0
WING_BLADE_MINIMUM_SPAN = 20.0
WING_CARBON_PALETTE = 39
UNDERSIDE_NORMAL_THRESHOLD = -.35
BLACK_PALETTE = 0
GLASS_MAXIMUM_ABS_X = 10.0
GLASS_MINIMUM_Y = 8.0
GLASS_MAXIMUM_ABS_Z = 30.0
MINIMUM_GLASS_COMPONENT_AREA = 20.0
WHEEL_SIDE_BAND = 1.0
MINIMUM_WHEEL_CLEARANCE = 1.0
QUANTIZATION_CLEARANCE_MARGIN = 1 / 16
FRONT_WHEEL_COUNT = 2
ARCH_PROFILE_ANGLES = (0, math.pi / 4, math.pi / 2, 3 * math.pi / 4, math.pi)
ARCH_ROOF_ANGLES = (math.pi / 4, math.pi / 2, 3 * math.pi / 4)
ARCH_PROFILE_RADIUS_MARGIN = 2.0
FENDER_QUADRIC_WEIGHT = 5.0
FENDER_MINIMUM_ABS_X = 13.0
FENDER_RADIUS_RANGE = (9.0, 14.0)
FENDER_SAMPLE_OUTWARD_OFFSETS = (1.0, 2.0, 3.0)
FENDER_SAMPLE_LONGITUDINAL_OFFSETS = (-6.0, -3.0, 0.0, 3.0, 6.0)
FENDER_MAXIMUM_HEIGHT_LOSS = 1.25


def simplify(points, triangles, materials, target_vertices, minimum_component_area):
    """Return a compact mesh, retaining palette seams and open boundary curves.

    A shared seam can collapse along itself, and an interior vertex can collapse
    onto it. Edges crossing material boundaries cannot collapse. Orthogonal
    boundary quadrics preserve the outlines of glass, apertures, and thin panels.
    Disconnected fittings below the requested area are omitted before reduction.
    """
    import numpy as np
    from scipy.sparse import coo_matrix
    from scipy.sparse.csgraph import connected_components
    from scipy.spatial import ConvexHull

    vertices = np.asarray(points, dtype=float).copy()
    faces = np.asarray(triangles, dtype=np.int64).copy()
    palettes = np.asarray(materials, dtype=np.int64).copy()
    if len(faces) != len(palettes):
        raise ValueError("Every source face must have a palette")
    normals = np.cross(vertices[faces[:, 1]] - vertices[faces[:, 0]],
                       vertices[faces[:, 2]] - vertices[faces[:, 0]])
    areas = np.linalg.norm(normals, axis=1) / 2
    valid = areas > GEOMETRY_EPSILON
    faces, palettes, areas = faces[valid], palettes[valid], areas[valid]
    edges = np.concatenate((faces[:, [0, 1]], faces[:, [1, 2]], faces[:, [2, 0]]))
    graph = coo_matrix((np.ones(len(edges)), edges.T),
                       shape=(len(vertices), len(vertices)))
    component_count, components = connected_components(graph, directed=False)
    component_areas = np.bincount(components[faces[:, 0]], weights=areas,
                                  minlength=component_count)
    component_sizes = np.bincount(components, minlength=component_count)
    component_centers = np.array([
        np.bincount(components, weights=vertices[:, axis], minlength=component_count)
        for axis in range(3)]).T / np.maximum(component_sizes[:, None], 1)
    wing_components = ((component_centers[:, 1] > WING_MINIMUM_Y) &
                       (component_centers[:, 2] < WING_MAXIMUM_Z))
    # The two horizontal wing blades are convex airfoils. Their quantized
    # imported surfaces contain tiny disconnected gaps which edge collapse can
    # enlarge. Close each blade with its source convex envelope before reduction.
    blade_components = set()
    blade_faces, blade_palettes = [], []
    for component in np.flatnonzero(wing_components):
        members = np.flatnonzero(components == component)
        if np.ptp(vertices[members, 0]) <= WING_BLADE_MINIMUM_SPAN:
            continue
        hull = ConvexHull(vertices[members])
        blade_components.add(component)
        for local_face, equation in zip(hull.simplices, hull.equations):
            face = members[local_face].copy()
            triangle = vertices[face]
            normal = np.cross(triangle[1] - triangle[0], triangle[2] - triangle[0])
            if normal @ equation[:3] < 0:
                face = face[::-1]
            palette = (WING_CARBON_PALETTE if equation[1] > UNDERSIDE_NORMAL_THRESHOLD
                       else BLACK_PALETTE)
            blade_faces.append(face)
            blade_palettes.append([palette] * palettes.shape[1])
    if blade_components:
        keep = np.array([component not in blade_components for component in components[faces[:, 0]]])
        faces = np.concatenate((faces[keep], np.asarray(blade_faces)))
        palettes = np.concatenate((palettes[keep], np.asarray(blade_palettes)))
    component_limits = np.where(wing_components,
                               min(minimum_component_area, MINIMUM_WING_COMPONENT_AREA),
                               minimum_component_area)
    keep = component_areas[components[faces[:, 0]]] >= component_limits[components[faces[:, 0]]]
    faces, palettes = faces[keep], palettes[keep]
    indices, inverse = np.unique(faces, return_inverse=True)
    vertices = vertices[indices]
    retained_components = components[indices]
    protected = np.zeros(len(vertices), dtype=bool)
    # Keep the extrema of the independent wing blades, endplates and supports.
    # Collapsing a narrow planar panel otherwise costs almost nothing and can
    # erase the rear tip or leave a floating blade at the distant budget.
    for component in sorted(set(retained_components)):
        if wing_components[component]:
            members = np.flatnonzero(retained_components == component)
            for axis in range(3):
                protected[members[np.argmin(vertices[members, axis])]] = True
                protected[members[np.argmax(vertices[members, axis])]] = True
    manifold_blade = np.isin(retained_components, list(blade_components))
    for component in sorted(blade_components):
        members = np.flatnonzero(retained_components == component)
        outline = ConvexHull(vertices[members][:, [0, 2]])
        protected[members[outline.vertices]] = True
    faces = inverse.reshape((-1, 3))
    semantics = np.where(np.ptp(palettes, axis=1) > 0, PAINT_LABEL, palettes[:, 0])
    # The windshield and side windows have distinct dark material components.
    # Retain their extrema so a very coarse shell cannot erase a window corner.
    glass_faces = faces[semantics == BLACK_PALETTE]
    if len(glass_faces):
        glass_edges = np.concatenate((glass_faces[:, [0, 1]], glass_faces[:, [1, 2]],
                                      glass_faces[:, [2, 0]]))
        glass_graph = coo_matrix((np.ones(len(glass_edges)), glass_edges.T),
                                shape=(len(vertices), len(vertices)))
        glass_count, glass_components = connected_components(glass_graph, directed=False)
        glass_normals = np.cross(vertices[glass_faces[:, 1]] - vertices[glass_faces[:, 0]],
                                 vertices[glass_faces[:, 2]] - vertices[glass_faces[:, 0]])
        glass_areas = np.bincount(glass_components[glass_faces[:, 0]],
                                  weights=np.linalg.norm(glass_normals, axis=1) / 2,
                                  minlength=glass_count)
        for component in np.flatnonzero(glass_areas >= MINIMUM_GLASS_COMPONENT_AREA):
            members = np.flatnonzero(glass_components == component)
            center = vertices[members].mean(axis=0)
            if (abs(center[0]) < GLASS_MAXIMUM_ABS_X and center[1] > GLASS_MINIMUM_Y and
                    abs(center[2]) < GLASS_MAXIMUM_ABS_Z):
                for axis in range(3):
                    protected[members[np.argmin(vertices[members, axis])]] = True
                    protected[members[np.argmax(vertices[members, axis])]] = True
    import mcmurtry_model as model
    # Preserve sampled source contours on both edges of each curved fender.
    # A side-only clearance constraint permits the roof to collapse downward
    # into the side, losing the recognizable rounded wheel-pod silhouette.
    radius = model.WHEEL_RADIUS + ARCH_PROFILE_RADIUS_MARGIN
    for x, y, z in model.WHEEL_CENTERS:
        for edge, angles in ((1, ARCH_PROFILE_ANGLES), (-1, ARCH_ROOF_ANGLES)):
            sample_x = x + edge * math.copysign(model.WHEEL_HALF_WIDTH, x)
            for angle in angles:
                sample = np.array((sample_x, y + radius * math.sin(angle),
                                   z + radius * math.cos(angle)))
                distances = np.sum((vertices - sample)**2, axis=1)
                protected[np.argmin(distances)] = True
    adjacency = [set() for _ in vertices]
    active = np.ones(len(vertices), dtype=bool)
    alive = np.ones(len(faces), dtype=bool)
    versions = np.zeros(len(vertices), dtype=np.int64)
    quadrics = np.zeros((len(vertices), 4, 4))
    normals = np.cross(vertices[faces[:, 1]] - vertices[faces[:, 0]],
                       vertices[faces[:, 2]] - vertices[faces[:, 0]])
    magnitudes = np.linalg.norm(normals, axis=1)
    normals /= magnitudes[:, None]
    edges = {}
    for face_index, face in enumerate(faces):
        plane = np.r_[normals[face_index], -normals[face_index] @ vertices[face[0]]]
        weight = max(magnitudes[face_index] / 2, MINIMUM_FACE_WEIGHT)
        centroid = vertices[face].mean(axis=0)
        if centroid[1] > WING_MINIMUM_Y and centroid[2] < WING_MAXIMUM_Z:
            weight *= WING_QUADRIC_WEIGHT
        if abs(centroid[0]) >= FENDER_MINIMUM_ABS_X:
            _, center_y, center_z = min(model.WHEEL_CENTERS,
                                         key=lambda center: abs(center[2] - centroid[2]))
            radial = math.hypot(centroid[1] - center_y, centroid[2] - center_z)
            if (centroid[1] > center_y and FENDER_RADIUS_RANGE[0] <= radial <=
                    FENDER_RADIUS_RANGE[1]):
                weight *= FENDER_QUADRIC_WEIGHT
        quadric = np.outer(plane, plane) * weight
        for vertex in face:
            adjacency[vertex].add(face_index)
            quadrics[vertex] += quadric
        for first, second in zip(face, np.roll(face, -1)):
            edges.setdefault(tuple(sorted((first, second))), []).append(face_index)
    for (first, second), incident in edges.items():
        if len(incident) != 1 and len(set(semantics[incident])) == 1:
            continue
        direction = vertices[second] - vertices[first]
        length = np.linalg.norm(direction)
        if length < GEOMETRY_EPSILON:
            continue
        for face_index in incident:
            normal = np.cross(direction / length, normals[face_index])
            magnitude = np.linalg.norm(normal)
            if magnitude < GEOMETRY_EPSILON:
                continue
            normal /= magnitude
            plane = np.r_[normal, -normal @ vertices[first]]
            quadric = np.outer(plane, plane) * length * BOUNDARY_QUADRIC_WEIGHT
            quadrics[first] += quadric
            quadrics[second] += quadric

    def boundary(vertex):
        counts = {}
        for face_index in sorted(adjacency[vertex]):
            for neighbor in faces[face_index]:
                if neighbor != vertex:
                    counts[neighbor] = counts.get(neighbor, 0) + 1
        return {neighbor for neighbor, count in counts.items() if count == 1}

    def candidate(first, second):
        common = adjacency[first] & adjacency[second]
        if not common or adjacency[first] | adjacency[second] == common:
            return None
        if manifold_blade[first] or manifold_blade[second]:
            first_neighbors = {vertex for face_index in adjacency[first]
                               for vertex in faces[face_index] if vertex != first}
            second_neighbors = {vertex for face_index in adjacency[second]
                                for vertex in faces[face_index] if vertex != second}
            opposites = {vertex for face_index in common for vertex in faces[face_index]
                         if vertex not in (first, second)}
            if len(common) != 2 or first_neighbors & second_neighbors != opposites:
                return None
        first_materials = {semantics[index] for index in adjacency[first]}
        second_materials = {semantics[index] for index in adjacency[second]}
        if (len(first_materials) > 1 and len(second_materials) > 1 and
                first_materials != second_materials):
            return None
        first_boundary, second_boundary = boundary(first), boundary(second)
        quadric = quadrics[first] + quadrics[second]
        fixed = None
        if first_materials != second_materials:
            if first_materials < second_materials:
                fixed = second
            elif second_materials < first_materials:
                fixed = first
            else:
                return None
        if first_boundary and not second_boundary:
            fixed = first
        elif second_boundary and not first_boundary:
            fixed = second
        elif first_boundary and second_boundary and second not in first_boundary:
            return None
        if protected[first] and protected[second]:
            if np.linalg.norm(vertices[first] - vertices[second]) > GEOMETRY_EPSILON:
                return None
            fixed = first
        elif protected[first]:
            fixed = first
        elif protected[second]:
            fixed = second
        if fixed is not None:
            position = vertices[fixed].copy()
        elif first_boundary or len(first_materials) > 1:
            base = np.r_[vertices[first], 1]
            delta = np.r_[vertices[second] - vertices[first], 0]
            denominator = delta @ quadric @ delta
            fraction = (np.clip(-(base @ quadric @ delta) / denominator, 0, 1)
                        if denominator > SOLVE_EPSILON else .5)
            position = vertices[first] + fraction * (vertices[second] - vertices[first])
        else:
            midpoint = (vertices[first] + vertices[second]) / 2
            try:
                position = np.linalg.solve(quadric[:3, :3], -quadric[:3, 3])
            except np.linalg.LinAlgError:
                position = midpoint
            if (np.linalg.norm(position - midpoint) > MAXIMUM_EDGE_EXCURSION *
                    np.linalg.norm(vertices[first] - vertices[second])):
                position = midpoint
        homogeneous = np.r_[position, 1]
        error = max(0, homogeneous @ quadric @ homogeneous)
        return error, position

    wheel_envelopes = []
    for wheel, (x, y, z) in enumerate(model.WHEEL_CENTERS):
        outer_x = x + math.copysign(model.WHEEL_HALF_WIDTH, x)
        radius = (math.hypot(model.WHEEL_RADIUS, model.WHEEL_HALF_WIDTH)
                  if wheel < FRONT_WHEEL_COUNT else model.WHEEL_RADIUS)
        radius += MINIMUM_WHEEL_CLEARANCE + QUANTIZATION_CLEARANCE_MARGIN
        wheel_envelopes.append((outer_x, y, z, radius))

    def clear_of_wheels(triangle, normal):
        if abs(normal[0]) < max(abs(normal[1]), abs(normal[2])):
            return True
        for outer_x, center_y, center_z, radius in wheel_envelopes:
            lower, upper = outer_x - WHEEL_SIDE_BAND, outer_x + WHEEL_SIDE_BAND
            if (min(point[0] for point in triangle) > upper or
                    max(point[0] for point in triangle) < lower or
                    min(point[1] for point in triangle) > center_y + radius or
                    max(point[1] for point in triangle) < center_y - radius or
                    min(point[2] for point in triangle) > center_z + radius or
                    max(point[2] for point in triangle) < center_z - radius):
                continue
            polygon = [tuple(point) for point in triangle]
            for limit, sense in ((lower, 1), (upper, -1)):
                clipped = []
                for start, end in zip(polygon, polygon[1:] + polygon[:1]):
                    start_inside = (start[0] - limit) * sense >= -GEOMETRY_EPSILON
                    end_inside = (end[0] - limit) * sense >= -GEOMETRY_EPSILON
                    if start_inside:
                        clipped.append(start)
                    if start_inside != end_inside:
                        fraction = (limit - start[0]) / (end[0] - start[0])
                        clipped.append(tuple(start[axis] + fraction * (end[axis] - start[axis])
                                             for axis in range(3)))
                polygon = clipped
                if not polygon:
                    break
            if len(polygon) < 3:
                continue
            projected = [(point[1] - center_y, point[2] - center_z) for point in polygon]
            sides = list(zip(projected, projected[1:] + projected[:1]))
            crosses = [start[0] * end[1] - start[1] * end[0] for start, end in sides]
            if (abs(sum(crosses)) > GEOMETRY_EPSILON and
                    (min(crosses) >= -GEOMETRY_EPSILON or max(crosses) <= GEOMETRY_EPSILON)):
                return False
            for start, end in sides:
                dy, dz = end[0] - start[0], end[1] - start[1]
                length_squared = dy * dy + dz * dz
                fraction = (max(0, min(1, -(start[0] * dy + start[1] * dz) / length_squared))
                            if length_squared > GEOMETRY_EPSILON else 0)
                if math.hypot(start[0] + fraction * dy, start[1] + fraction * dz) < radius:
                    return False
        return True

    roof_samples = np.array([
        (x + math.copysign(offset, x), z + longitudinal)
        for x, _, z in model.WHEEL_CENTERS
        for offset in FENDER_SAMPLE_OUTWARD_OFFSETS
        for longitudinal in FENDER_SAMPLE_LONGITUDINAL_OFFSETS])

    def roof_heights(triangles):
        first, second, third = triangles[:, 0], triangles[:, 1], triangles[:, 2]
        u, v = second - first, third - first
        determinant = u[:, 0] * v[:, 2] - u[:, 2] * v[:, 0]
        valid = np.abs(determinant) > GEOMETRY_EPSILON
        divisor = np.where(valid, determinant, 1)[:, None]
        dx = roof_samples[None, :, 0] - first[:, None, 0]
        dz = roof_samples[None, :, 1] - first[:, None, 2]
        a = (dx * v[:, None, 2] - dz * v[:, None, 0]) / divisor
        b = (u[:, None, 0] * dz - u[:, None, 2] * dx) / divisor
        inside = valid[:, None] & (a >= -GEOMETRY_EPSILON) & (b >= -GEOMETRY_EPSILON)
        inside &= a + b <= 1 + GEOMETRY_EPSILON
        heights = first[:, None, 1] + a * u[:, None, 1] + b * v[:, None, 1]
        return np.where(inside, heights, -np.inf)

    roof_hits = roof_heights(vertices[faces])
    source_roof = roof_hits.max(axis=0)
    roof_members = [set(np.flatnonzero(np.isfinite(roof_hits[:, sample])))
                    for sample in range(len(roof_samples))]

    def roof_coverage_preserved(incident, replacements, replacement_hits):
        affected_samples = np.flatnonzero(np.isfinite(roof_hits[list(incident)]).any(axis=0))
        for sample in affected_samples:
            untouched = roof_members[sample] - incident
            outside = max((roof_hits[index, sample] for index in untouched), default=-np.inf)
            changed = max(replacement_hits[:, sample], default=-np.inf)
            if max(outside, changed) < source_roof[sample] - FENDER_MAXIMUM_HEIGHT_LOSS:
                return False
        return True

    heap = []
    serial = itertools.count()

    def push(first, second):
        first, second = sorted((first, second))
        result = candidate(first, second)
        if result is not None:
            error, position = result
            heapq.heappush(heap, (error, next(serial), first, second,
                                  versions[first], versions[second], position))

    for first, second in sorted(edges):
        push(first, second)
    remaining = len(vertices)
    while heap and remaining > target_vertices:
        _, _, first, second, first_version, second_version, position = heapq.heappop(heap)
        if (not active[first] or not active[second] or
                versions[first] != first_version or versions[second] != second_version):
            continue
        incident = adjacency[first] | adjacency[second]
        common = adjacency[first] & adjacency[second]
        if not common:
            continue
        valid = True
        replacements = sorted(incident - common)
        replacement_triangles = []
        for face_index in replacements:
            face = faces[face_index]
            triangle = vertices[face].copy()
            triangle[(face == first) | (face == second)] = position
            replacement_triangles.append(triangle)
            normal = np.cross(triangle[1] - triangle[0], triangle[2] - triangle[0])
            previous = np.cross(vertices[face[1]] - vertices[face[0]],
                                vertices[face[2]] - vertices[face[0]])
            magnitude = np.linalg.norm(normal)
            product = magnitude * np.linalg.norm(previous)
            if (magnitude <= GEOMETRY_EPSILON or
                    normal @ previous < MINIMUM_NORMAL_COSINE * product or
                    not clear_of_wheels(triangle, normal)):
                valid = False
                break
        if not valid:
            continue
        replacement_hits = roof_heights(np.asarray(replacement_triangles).reshape((-1, 3, 3)))
        if not roof_coverage_preserved(incident, replacements, replacement_hits):
            continue
        for face_index in sorted(incident):
            for sample in np.flatnonzero(np.isfinite(roof_hits[face_index])):
                roof_members[sample].discard(face_index)
            roof_hits[face_index] = -np.inf
        for face_index, hits in zip(replacements, replacement_hits):
            roof_hits[face_index] = hits
            for sample in np.flatnonzero(np.isfinite(hits)):
                roof_members[sample].add(face_index)
        affected = set()
        for face_index in sorted(incident):
            affected.update(faces[face_index])
            for vertex in faces[face_index]:
                adjacency[vertex].discard(face_index)
            if face_index in common:
                alive[face_index] = False
            else:
                faces[face_index][faces[face_index] == second] = first
                for vertex in faces[face_index]:
                    adjacency[vertex].add(face_index)
        vertices[first] = position
        quadrics[first] += quadrics[second]
        protected[first] = protected[first] or protected[second]
        active[second] = False
        remaining -= 1
        affected.discard(second)
        for vertex in sorted(affected):
            versions[vertex] += 1
        for vertex in sorted(affected):
            if not adjacency[vertex]:
                if active[vertex]:
                    active[vertex] = False
                    remaining -= 1
                continue
            neighbors = {neighbor for face_index in adjacency[vertex]
                         for neighbor in faces[face_index] if neighbor != vertex}
            for neighbor in sorted(neighbors):
                push(vertex, neighbor)
    faces = faces[alive]
    indices, inverse = np.unique(faces, return_inverse=True)
    return vertices[indices], inverse.reshape((-1, 3)), palettes[alive]
