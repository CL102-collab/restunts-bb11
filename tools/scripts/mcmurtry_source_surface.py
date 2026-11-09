"""Sample normalized car0 surfaces while authoring the connected front shell.

The source's wheel vents have no roof. A missing top ray therefore interpolates
the surrounding painted skin; it must not accidentally select the floor or
wheel-well fittings. Exact unfiltered source rays remain available separately.
All public top/front queries use the positive half, so mirrored reconstruction
has identical coordinates on both sides.
"""

from __future__ import annotations

from dataclasses import dataclass
import math


EPSILON = 1e-9
TRIANGLE_VERTICES = 3
TOP_AXES = (0, 2, 1)
FRONT_AXES = (0, 1, 2)
SIDE_AXES = (1, 2, 0)
VIEW_AXES = {"top": TOP_AXES, "front": FRONT_AXES, "side": SIDE_AXES}
BLACK_MATERIAL = 0
MINIMUM_TOP_NORMAL = .1
CANOPY_MAXIMUM_X = 9.0
CANOPY_MAXIMUM_Z = 13.5
CANOPY_MINIMUM_Y = 8.0
MINIMUM_PAINT_HEIGHT = 0.0
GAP_SEARCH_STEP = .25
GAP_SEARCH_LIMIT = 12.0
GAP_BOUNDARY_ITERATIONS = 12


@dataclass(frozen=True)
class SurfaceHit:
    point: tuple
    materials: tuple
    face: int
    distance: float = 0.0


class _Projection:
    def __init__(self, triangles, materials, face_indices, axes, np):
        self.np = np
        self.axes = axes
        projected = triangles[:, :, axes]
        first = projected[:, 0]
        edge_u = projected[:, 1] - first
        edge_v = projected[:, 2] - first
        determinant = edge_u[:, 0] * edge_v[:, 1] - edge_u[:, 1] * edge_v[:, 0]
        valid = abs(determinant) > EPSILON
        self.triangles = projected[valid]
        self.first = first[valid]
        self.edge_u = edge_u[valid]
        self.edge_v = edge_v[valid]
        self.determinant = determinant[valid]
        self.materials = materials[valid]
        self.face_indices = face_indices[valid]

    def _result(self, index, point, distance=0.0):
        restored = [0.0] * TRIANGLE_VERTICES
        for axis, value in zip(self.axes, point):
            restored[axis] = float(value)
        return SurfaceHit(tuple(restored), tuple(map(int, self.materials[index])),
                          int(self.face_indices[index]), float(distance))

    def hit(self, u, v):
        if not len(self.first):
            return None
        np = self.np
        offset = np.array((u, v)) - self.first[:, :2]
        a = (offset[:, 0] * self.edge_v[:, 1] -
             offset[:, 1] * self.edge_v[:, 0]) / self.determinant
        b = (self.edge_u[:, 0] * offset[:, 1] -
             self.edge_u[:, 1] * offset[:, 0]) / self.determinant
        covered = (a >= -EPSILON) & (b >= -EPSILON) & (a + b <= 1 + EPSILON)
        if not covered.any():
            return None
        depth = self.first[:, 2] + a * self.edge_u[:, 2] + b * self.edge_v[:, 2]
        depth[~covered] = -np.inf
        index = int(np.argmax(depth))
        return self._result(index, (u, v, depth[index]))

    def nearest(self, u, v):
        """Find the closest projected triangle, interpolating its edge depth."""
        hit = self.hit(u, v)
        if hit is not None:
            return hit
        if not len(self.first):
            return None
        np = self.np
        query = np.array((u, v))
        candidates = []
        for corner in range(TRIANGLE_VERTICES):
            start = self.triangles[:, corner]
            end = self.triangles[:, (corner + 1) % TRIANGLE_VERTICES]
            delta = end - start
            length = (delta[:, :2] ** 2).sum(axis=1)
            weight = ((query - start[:, :2]) * delta[:, :2]).sum(axis=1)
            weight = np.clip(weight / np.maximum(length, EPSILON), 0, 1)
            point = start + weight[:, None] * delta
            distance = ((query - point[:, :2]) ** 2).sum(axis=1)
            nearest = distance.min()
            tied = distance <= nearest + EPSILON
            index = int(np.argmax(np.where(tied, point[:, 2], -np.inf)))
            candidates.append((distance[index], -point[index, 2], index, point[index]))
        distance, _, index, point = min(candidates, key=lambda item: item[:2])
        return self._result(index, point, math.sqrt(distance))


class SourceSurface:
    """Normalized car0 ray intersections and a continuous skin-height guide.

    ``points``, ``faces`` and per-face ``materials`` are the arrays returned by
    generate-mcmurtry-lods.source_geometry. No source geometry is mutated.
    ``ray_hit`` queries the literal model; ``top_y`` deliberately excludes
    separate mirror pods, front antenna and underside fittings.
    """

    def __init__(self, points, faces, materials):
        import numpy as np

        self.np = np
        points = np.asarray(points, dtype=float)
        triangles = points[np.asarray(faces)]
        materials = np.asarray(materials)
        indices = np.arange(len(triangles))
        self.views = {}
        for name, axes in VIEW_AXES.items():
            selected = (triangles[:, :, 0].mean(axis=1) >= 0 if name == "side"
                        else np.ones(len(triangles), dtype=bool))
            self.views[name] = _Projection(triangles[selected], materials[selected],
                                           indices[selected], axes, np)
        normal = np.cross(triangles[:, 1] - triangles[:, 0],
                          triangles[:, 2] - triangles[:, 0])
        upward = abs(normal[:, 1]) >= MINIMUM_TOP_NORMAL * np.linalg.norm(normal, axis=1)
        centers = triangles.mean(axis=1)
        paint = ((np.ptp(materials, axis=1) > 0) & upward &
                 (centers[:, 1] > MINIMUM_PAINT_HEIGHT))
        canopy = (np.all(materials == BLACK_MATERIAL, axis=1) &
                  (abs(centers[:, 0]) < CANOPY_MAXIMUM_X) &
                  (centers[:, 2] < CANOPY_MAXIMUM_Z) &
                  (centers[:, 1] > CANOPY_MINIMUM_Y) & upward)
        selected = paint | canopy
        self.skin = _Projection(triangles[selected], materials[selected],
                                indices[selected], TOP_AXES, np)
        self._top_cache = {}

    @staticmethod
    def _coordinates(view, u, v):
        return (abs(u), v) if view in ("top", "front") else (u, v)

    def ray_hit(self, view, u, v):
        """Return the outermost literal source hit, or None for an open ray."""
        return self.views[view].hit(*self._coordinates(view, u, v))

    def nearest_hit(self, view, u, v):
        """Return a literal hit, falling back to the nearest projected face."""
        return self.views[view].nearest(*self._coordinates(view, u, v))

    def skin_hit(self, x, z):
        """Return only painted top skin or canopy, without filling its vents."""
        return self.skin.hit(abs(x), z)

    def _gap_boundary(self, x, z, axis, sign):
        previous = 0.0
        distance = GAP_SEARCH_STEP
        while distance <= GAP_SEARCH_LIMIT:
            query = [x, z]
            query[axis] += sign * distance
            hit = self.skin.hit(abs(query[0]), query[1])
            if hit is not None:
                lower, upper = previous, distance
                for _ in range(GAP_BOUNDARY_ITERATIONS):
                    middle = (lower + upper) / 2
                    query[axis] = (x, z)[axis] + sign * middle
                    candidate = self.skin.hit(abs(query[0]), query[1])
                    if candidate is None:
                        lower = middle
                    else:
                        upper, hit = middle, candidate
                return upper, hit.point[1]
            previous = distance
            distance += GAP_SEARCH_STEP
        return None

    def top_y(self, x, z):
        """Sample skin, interpolating across a vent from opposing boundaries.

        The shorter bracket contributes more weight. Outside the projected
        body, where no opposing pair exists, use the nearest painted/canopy
        point. Aperture walls and floors are authored after this height guide.
        """
        x = abs(float(x))
        z = float(z)
        key = (x, z)
        if key in self._top_cache:
            return self._top_cache[key]
        hit = self.skin.hit(x, z)
        if hit is not None:
            result = hit.point[1]
        else:
            heights = []
            weights = []
            for axis in range(2):
                lower = self._gap_boundary(x, z, axis, -1)
                upper = self._gap_boundary(x, z, axis, 1)
                if lower is None or upper is None:
                    continue
                span = lower[0] + upper[0]
                heights.append((lower[1] * upper[0] + upper[1] * lower[0]) / span)
                weights.append(1 / span)
            if heights:
                result = sum(y * weight for y, weight in zip(heights, weights)) / sum(weights)
            else:
                nearest = self.skin.nearest(x, z)
                if nearest is None:
                    raise ValueError("Source has no painted skin or canopy")
                result = nearest.point[1]
        self._top_cache[key] = result
        return result

    def front_z(self, x, y):
        hit = self.nearest_hit("front", x, y)
        if hit is None:
            raise ValueError("Source has no front projection")
        return hit.point[2]

    def side_x(self, y, z):
        hit = self.nearest_hit("side", y, z)
        if hit is None:
            raise ValueError("Source has no side projection")
        return hit.point[0]
