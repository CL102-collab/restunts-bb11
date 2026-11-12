#!/usr/bin/env python3
"""Check source provenance, detailed H3D geometry and shared animated wheels."""

import gzip
import hashlib
import importlib.util
import json
import math
from collections import Counter
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock

import mcmurtry_hypervision as detailed
import mcmurtry_model as model
import mcmurtry_racing as racing
import mcmurtry_apertures as apertures


SCRIPT = Path(__file__).with_name("generate-mcmurtry-speirling.py")
SPEC = importlib.util.spec_from_file_location("mcmurtry_generator", SCRIPT)
generator = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(generator)
CONTROLS_END = model.WHEEL_CONTROL_START + detailed.WHEEL_COUNT * model.WHEEL_CONTROL_COUNT
SOURCE_LENGTH_TOLERANCE = .1
MINIMUM_SOURCE_TRIANGLES = 100_000
# The connected base shell retains its original 1,200-vertex cap. The near
# mesh has a separate bounded allowance for four rings and a kite-shaped lamp.
RACING_VERTEX_BUDGETS = {"car1": (1_000, 1_304), "car2": (400, 500)}
REAR_LAMP_COUNT = 5
REAR_SPOT_COUNT = 4
REAR_SPOT_MINIMUM_DIAMETER = 1.0
REAR_HOLE_SAMPLE_OFFSET = .2
REAR_RIM_SAMPLE_FRACTION = .75
REAR_LAMP_NEAR_CENTRE_X = 3.0
REAR_LAMP_MAXIMUM_Z = -30.0
SOURCE_EXTENT_TOLERANCE = 1.5
WING_SAMPLE_X = (-12., 12.)
WING_SAMPLE_Z = (-40., -42.)
WING_HEIGHT_TOLERANCE = 1.5
FRONT_FENDER_WITNESSES = ((12.5, 21.5), (13.25, 27.5), (17.375, 26.25))
FRONT_FENDER_RAY_HEIGHT = 12.0
FRONT_FENDER_ABS_X_BOUNDS = (11.0, 20.0)
FRONT_FENDER_Z_BOUNDS = (13.0, 37.0)
FRONT_FENDER_MINIMUM_Y = 6.0
FRONT_FENDER_DEPTH_TOLERANCE = 1.0
FRONT_FENDER_MINIMUM_PAINTED_RAYS = 20
FRONT_FENDER_VIEW_DIRECTIONS = ((0., 1., 0.), (1., 1., 0.), (-1., 1., 0.),
                                (1., 1., 1.), (1., 1., -1.))
FRONT_PANEL_SAMPLE_X = tuple(float(x) for x in range(1, 18, 2))
HOOD_SAMPLE_Z = tuple(float(z) for z in range(17, 42, 4))
HOOD_MINIMUM_Y = 6.0
HOOD_MINIMUM_PAINTED_RAYS = 35
BUMPER_SAMPLE_Y = (.5, 2.5, 4.5, 6.5)
BUMPER_MINIMUM_Z = 34.0
BUMPER_MINIMUM_PAINTED_RAYS = 8
BUMPER_MATERIAL_BOUNDARY_MARGIN = 1.5
FRONT_PANEL_DEPTH_TOLERANCE = 1.5
FRONT_JOIN_Z = 4.0
REAR_PAINT_SAMPLE_MARGIN = .25
REAR_PAINT_DEPTH_TOLERANCE = 1.5
REAR_OBLIQUE_DIRECTIONS = ((1., .5, -1.), (1., .5, 1.))
REAR_ARCH_SOURCE_BOUNDS = ((13.5, 20.), (9., 15.8), (-33., -15.))
REAR_ARCH_SAMPLE_Y = (12., 12.5, 13.)
REAR_ARCH_SAMPLE_Z = (-28., -26., -24., -22.)
REAR_ARCH_OBLIQUE_Z = (-20., -24., -28.)
REAR_ARCH_OBLIQUE_XY = (14., 10.)
REAR_ARCH_MINIMUM_PAINTED_RAYS = 14
REAR_CROWN_SOURCE_BOUNDS = ((16., 18.5), (9., 16.), (-34., -16.))
REAR_CROWN_SAMPLE_X = (16., 16.5, 17., 17.5, 18., 18.5)
REAR_CROWN_SAMPLE_Z = tuple(float(z) for z in range(-34, -15, 2))
REAR_CROWN_MINIMUM_PAINTED_RAYS = 35
REAR_CROWN_HEIGHT_TOLERANCE = 1.0
REAR_CORNER_SOURCE_BOUNDS = ((10., 20.), (4., 15.), (-43., -34.))
REAR_CORNER_SAMPLE_X = (16.5, 17.5, 18.5)
REAR_CORNER_SAMPLE_Y = (8., 10.)
REAR_CORNER_OBLIQUE_Z = (-36., -40.)
REAR_CORNER_OBLIQUE_XY = (17., 10.)
REAR_CORNER_MINIMUM_PAINTED_RAYS = 5
REAR_SHOULDER_SOURCE_BOUNDS = ((4., 16.), (8., 21.), (-30., -12.))
REAR_SHOULDER_SAMPLE_X = (7.5, 8.5, 9.5, 10.5)
REAR_SHOULDER_SAMPLE_Z = (-20., -22., -24., -26.)
REAR_SHOULDER_OBLIQUE_Z = (-20., -24.)
REAR_SHOULDER_OBLIQUE_XY = (11., 14.)
REAR_SHOULDER_MINIMUM_PAINTED_RAYS = 14
CANOPY_SILL_SOURCE_BOUNDS = ((7., 10.), (7., 14.), (-19., 1.))
CANOPY_SILL_SAMPLE_X = (8., 8.5, 9.)
CANOPY_SILL_SAMPLE_Z = (-12., -8., -4., -3., 0.)
CANOPY_SILL_OBLIQUE_Z = (-8., -4., -3., 0.)
CANOPY_SILL_OBLIQUE_XY = (10.5, 11.)
CANOPY_SILL_MINIMUM_PAINTED_RAYS = 6
SHOWROOM_PAYLOAD_SHA256 = "cd6267449c1a10c19c19dc1c8375aafe3dc221d41dbcc6cbbd96d7e64852dd03"
DISTANT_PAYLOAD_SHA256 = "6b12bbd7432b693c6802c0b5ba7fdeaf3e690bc94b5d13787dfdbcfffa0a96bb"
BASE_PAYLOAD_SHA256 = {
    "car0": SHOWROOM_PAYLOAD_SHA256,
    "car1": "58f411141b3a7b99f2360ec0c5c0e80e96bebe1e90ab08fa85d4e74579032215",
    "car2": DISTANT_PAYLOAD_SHA256,
}
PLANAR_HEIGHT = 2.0
NEARBY_LAYER_GAP = 1 / (1 << detailed.RACING_FRACTION_BITS)
NARROW_PANEL_WIDTH = 2 * NEARBY_LAYER_GAP
PANEL_RIDGE_HEIGHT = .5


class SourceImportTests(unittest.TestCase):
    def test_nested_column_major_node_transform_and_reflection(self):
        rotation = {"rotation": (0, math.sqrt(.5), 0, math.sqrt(.5)),
                    "translation": (4, 5, 6), "scale": (2, 3, 4)}
        # A local X axis becomes -Z after the parent's quarter turn.
        parent = detailed._node_matrix(rotation)
        local = detailed._node_matrix({"translation": (1, 0, 0)})
        result = detailed._transform(detailed._multiply(parent, local), (0, 0, 0))
        for actual, expected in zip(result, (4, 5, 4)):
            self.assertAlmostEqual(expected, actual)
        reflected = detailed._node_matrix({"scale": (-1, 1, 1)})
        self.assertLess(detailed._determinant(reflected), 0)
        self.assertGreater(detailed._determinant(parent), 0)

    def test_interleaved_glb_accessor_offsets(self):
        binary = b"HEAD" + struct.pack("<3fI3fI", 1, 2, 3, 99, 4, 5, 6, 98)
        document = {"accessors": [{"bufferView": 0, "componentType": 5126,
                                    "count": 2, "type": "VEC3"}],
                    "bufferViews": [{"buffer": 0, "byteOffset": 4,
                                     "byteLength": len(binary) - 4, "byteStride": 16}]}
        self.assertEqual([(1., 2., 3.), (4., 5., 6.)],
                         detailed._accessor(document, binary, 0))

    def test_import_rejects_unattributed_source_without_writing_output(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "other.glb"
            output = Path(folder) / "converted.json.gz"
            source.write_bytes(b"a different model")
            with self.assertRaisesRegex(ValueError, "differs from.*reference"):
                detailed.import_source(source, output)
            self.assertFalse(output.exists())

    def test_cached_racing_lods_reject_a_different_showroom_source(self):
        cached = json.loads(gzip.decompress(detailed.RACING_SOURCE_PATH.read_bytes()))
        cached["source_car0_sha256"] = hashlib.sha256(b"another showroom mesh").hexdigest()
        with tempfile.TemporaryDirectory() as folder:
            stale = Path(folder) / "stale_racing_lods.json.gz"
            stale.write_bytes(gzip.compress(json.dumps(cached).encode("utf-8")))
            with mock.patch.object(detailed, "RACING_SOURCE_PATH", stale):
                with self.assertRaisesRegex(ValueError, "rebuilt from the current car0"):
                    detailed.build_shapes()

    def test_lod_authoring_uses_the_base_shell_of_a_detailed_showroom(self):
        spec = importlib.util.spec_from_file_location(
            "mcmurtry_lod_generator", Path(__file__).with_name("generate-mcmurtry-lods.py"))
        lod_generator = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(lod_generator)
        companion = Path(__file__).resolve().parents[2] / "assets/cars/STMCMU.H3D"
        points, triangles, materials, digest = lod_generator.source_geometry(companion)
        source = detailed.load_source()
        base = detailed._showroom_shape({**source, **source["lods"]["car0"]}, model)
        self.assertEqual(SHOWROOM_PAYLOAD_SHA256, digest)
        self.assertEqual([(x / model.SHOWROOM_SCALE,
                           (y - model.SHOWROOM_Y_OFFSET) / model.SHOWROOM_SCALE,
                           z / model.SHOWROOM_SCALE)
                          for x, y, z in base.vertices[CONTROLS_END:]], points)
        self.assertEqual([tuple(index - CONTROLS_END for index in indices)
                          for kind, _, _, indices in base.primitives
                          if kind != model.WHEEL_PRIMITIVE], triangles)
        self.assertEqual([palette for kind, _, palette, _ in base.primitives
                          if kind != model.WHEEL_PRIMITIVE], materials)


class RacingSurfaceTests(unittest.TestCase):
    @staticmethod
    def components(faces):
        neighbors = {}
        for indices, _ in faces:
            for index in indices:
                neighbors.setdefault(index, set()).update(indices)
        remaining = set(neighbors)
        components = 0
        while remaining:
            pending = {remaining.pop()}
            while pending:
                index = pending.pop()
                adjacent = neighbors[index] & remaining
                remaining.difference_update(adjacent)
                pending.update(adjacent)
            components += 1
        return components

    def test_planar_panels_keep_winding_and_shared_topology(self):
        points = [(x, PLANAR_HEIGHT, z) for z in (0., 1.) for x in (0., 1., 2.)]
        faces = [((0, 1, 4, 3), model.PAINT_TOP),
                 ((1, 2, 5, 4), model.PAINT_TOP)]
        for distant in (False, True):
            with self.subTest(distant=distant):
                smoothed, panels = racing._smooth_panels(points, faces, 0, model, distant=distant)
                self.assertTrue(all(point[1] == PLANAR_HEIGHT for point in smoothed))
                self.assertEqual(len(smoothed), len(set(smoothed)))
                self.assertEqual(1, self.components(panels))
                for indices, _ in panels:
                    # The fixture faces downward; smoothing must not invert
                    # its surface or create collapsed replacement panels.
                    self.assertLess(model._normal([smoothed[i] for i in indices])[1], 0)

    def test_nearby_disconnected_layers_are_never_welded(self):
        layers = (PLANAR_HEIGHT, PLANAR_HEIGHT + NEARBY_LAYER_GAP)
        points = [(x, y, z) for y in layers for x, z in ((0., 0.), (1., 0.),
                                                       (1., 1.), (0., 1.))]
        faces = [((0, 1, 2, 3), model.PAINT_TOP),
                 ((4, 5, 6, 7), (model.CARBON,) * model.PAINT_COUNT)]
        for distant in (False, True):
            with self.subTest(distant=distant):
                smoothed, panels = racing._smooth_panels(points, faces, 0, model, distant=distant)
                self.assertEqual(2, self.components(panels))
                self.assertEqual(set(layers), {point[1] for point in smoothed})
                for indices, _ in panels:
                    self.assertEqual(1, len({smoothed[i][1] for i in indices}))

    def test_narrow_ridged_panels_keep_every_encoded_child_facing_outward(self):
        # A narrow strip meets broad tilted panels. Unconstrained smoothing
        # folds one of its children after fixed-point rounding.
        grid = [(x, z) for z in (0., NARROW_PANEL_WIDTH, PLANAR_HEIGHT)
                for x in (0., NARROW_PANEL_WIDTH, PLANAR_HEIGHT)]
        heights = (PANEL_RIDGE_HEIGHT, 0., PANEL_RIDGE_HEIGHT,
                   -PANEL_RIDGE_HEIGHT, 0., 0.,
                   PANEL_RIDGE_HEIGHT, PANEL_RIDGE_HEIGHT, -PANEL_RIDGE_HEIGHT)
        points = [(x, height, z) for (x, z), height in zip(grid, heights)]
        faces = [(indices, model.PAINT_TOP) for indices in
                 ((0, 1, 4, 3), (1, 2, 5, 4), (3, 4, 7, 6), (4, 5, 8, 7))]
        factor = 1 << detailed.RACING_FRACTION_BITS
        smoothed, children = racing._smooth_panels(points, faces, 0, model, distant=False,
                                                   coordinate_factor=factor)
        encoded = [tuple(round(value * factor) / factor for value in point) for point in smoothed]
        self.assertEqual(sum(len(indices) for indices, _ in faces), len(children))
        cursor = 0
        for indices, _ in faces:
            outward = model._normal([points[index] for index in indices])
            for child, _ in children[cursor:cursor + len(indices)]:
                for corner in range(1, len(child) - 1):
                    triangle = [encoded[index] for index in
                                (child[0], child[corner], child[corner + 1])]
                    normal = model._normal(triangle)
                    self.assertGreater(sum(a * b for a, b in zip(outward, normal)), 0)
            cursor += len(indices)


class ApertureSurfaceTests(unittest.TestCase):
    def test_rounded_boundary_keeps_area_without_reversed_slivers(self):
        polygon = [(0., 0., 0.), (1., 0., NEARBY_LAYER_GAP), (2., 0., 0.),
                   (2., 0., 2.), (0., 0., 2.)]
        parent = model._normal(polygon)
        triangles = apertures._triangulate(polygon, parent, model)
        self.assertEqual(set(polygon), {point for triangle in triangles for point in triangle})
        self.assertAlmostEqual(abs(parent[1]),
                               sum(abs(model._normal(triangle)[1]) for triangle in triangles))
        for triangle in triangles:
            normal = model._normal(triangle)
            self.assertGreater(sum(a * b for a, b in zip(parent, normal)), 0)


class DetailedGeometryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = detailed.load_source()
        cls.shapes = detailed.build_shapes()
        companion = Path(__file__).resolve().parents[2] / "assets/cars/STMCMU.H3D"
        cls.installed_shapes = generator.hypervision_resources(companion.read_bytes())

    @staticmethod
    def encoded_body_faces(payload, name):
        """Read coordinates and complete paint tables from the serialized asset."""
        points, _, _ = generator.decode_model(payload, name, hypervision=True)
        vertices, primitives, paints, header_size, index_size = generator.model_layout(
            payload, name, hypervision=True)
        cursor = header_size + vertices * struct.calcsize("<3h") + primitives * struct.calcsize("<II")
        faces = []
        for _ in range(primitives):
            kind, flags = payload[cursor:cursor + 2]
            count = kind if kind <= racing.MAX_POLYGON_VERTICES else {
                model.SPHERE_PRIMITIVE: 2, model.WHEEL_PRIMITIVE: model.WHEEL_CONTROL_COUNT}[kind]
            materials = tuple(payload[cursor + 2:cursor + 2 + paints])
            indices = struct.unpack_from(f"<{count}{'H' if index_size == 2 else 'B'}",
                                         payload, cursor + 2 + paints)
            cursor += 2 + paints + count * index_size
            if kind != model.WHEEL_PRIMITIVE:
                faces.append((kind, flags, materials, tuple(points[index] for index in indices)))
        return points[CONTROLS_END:], faces

    def test_encoded_near_body_is_exactly_mirrored_with_all_paint_jobs(self):
        payloads = {"generated": self.shapes["car1"].encode_hypervision(),
                    "installed": self.installed_shapes["car1"]}
        for source, payload in payloads.items():
            vertices, faces = self.encoded_body_faces(payload, "car1")
            with self.subTest(source=source, geometry="vertices"):
                self.assertEqual(Counter(vertices), Counter((-x, y, z) for x, y, z in vertices))
            # Canonical spatial triangles make this independent of vertex IDs,
            # primitive order and winding of the two-sided surfaces. Include
            # every paint byte so matching turquoise cannot hide another
            # paint job's mismatched panels.
            original = Counter((kind, flags, materials, tuple(sorted(points)))
                               for kind, flags, materials, points in faces)
            reflected = Counter((kind, flags, materials,
                                 tuple(sorted((-x, y, z) for x, y, z in points)))
                                for kind, flags, materials, points in faces)
            with self.subTest(source=source, geometry="triangles and paint tables"):
                self.assertEqual(original, reflected)

    def test_encoded_near_front_has_no_open_surface_edges(self):
        payloads = {"generated": self.shapes["car1"].encode_hypervision(),
                    "installed": self.installed_shapes["car1"]}
        for source, payload in payloads.items():
            _, faces = self.encoded_body_faces(payload, "car1")
            edges = Counter(tuple(sorted((first, second)))
                            for kind, _, _, points in faces
                            if detailed.TRIANGLE_VERTICES <= kind <= racing.MAX_POLYGON_VERTICES
                            for first, second in zip(points, points[1:] + points[:1]))
            # Vents are recesses with walls and floors. Their rims, the hood
            # and the bumper must meet adjacent triangles even after encoding.
            # The rear source body joins at Z=4; its independent panel edges
            # do not describe the connected front surface under test.
            open_edges = [edge for edge, count in edges.items()
                          if count < 2 and max(point[2] for point in edge) > FRONT_JOIN_Z]
            with self.subTest(source=source):
                self.assertFalse(open_edges, f"{len(open_edges)} open front edges: {open_edges[:5]}")

    @staticmethod
    def roof_hit(shape, x, z):
        hits = []
        for kind, _, materials, indices in shape.primitives:
            if not detailed.TRIANGLE_VERTICES <= kind <= racing.MAX_POLYGON_VERTICES:
                continue
            for corner in range(1, len(indices) - 1):
                a, b, c = [shape.vertices[index] for index in
                           (indices[0], indices[corner], indices[corner + 1])]
                ux, uz, vx, vz = b[0] - a[0], b[2] - a[2], c[0] - a[0], c[2] - a[2]
                determinant = ux * vz - uz * vx
                if abs(determinant) <= generator.GEOMETRY_TOLERANCE:
                    continue
                px, pz = x - a[0], z - a[2]
                u = (px * vz - pz * vx) / determinant
                v = (ux * pz - uz * px) / determinant
                if min(u, v, 1 - u - v) >= -generator.GEOMETRY_TOLERANCE:
                    hits.append((a[1] + u * (b[1] - a[1]) + v * (c[1] - a[1]), materials))
        return max(hits, default=None)

    @staticmethod
    def body_ray_faces(shape):
        """Precompute normalized body triangles for independent sightline tests."""
        shift = model.SHOWROOM_Y_OFFSET if shape.scale == model.SHOWROOM_SCALE else 0
        points = [(x / shape.scale, (y - shift) / shape.scale, z / shape.scale)
                  for x, y, z in shape.vertices]
        faces = []
        for kind, _, materials, indices in shape.primitives:
            if not detailed.TRIANGLE_VERTICES <= kind <= racing.MAX_POLYGON_VERTICES:
                continue
            for corner in range(1, len(indices) - 1):
                a, b, c = [points[index] for index in
                           (indices[0], indices[corner], indices[corner + 1])]
                faces.append((a, tuple(y - x for x, y in zip(a, b)),
                              tuple(y - x for x, y in zip(a, c)), materials))
        return faces

    @staticmethod
    def body_ray_hit(faces, anchor, direction):
        """Return the first surface seen from infinity along a two-sided ray."""
        dx, dy, dz = direction
        nearest = None
        for a, edge, other, materials in faces:
            px = dy * other[2] - dz * other[1]
            py = dz * other[0] - dx * other[2]
            pz = dx * other[1] - dy * other[0]
            determinant = edge[0] * px + edge[1] * py + edge[2] * pz
            if abs(determinant) <= generator.GEOMETRY_TOLERANCE:
                continue
            tx, ty, tz = (anchor[axis] - a[axis] for axis in range(detailed.AXES))
            u = (tx * px + ty * py + tz * pz) / determinant
            if u < -generator.GEOMETRY_TOLERANCE or u > 1 + generator.GEOMETRY_TOLERANCE:
                continue
            qx = ty * edge[2] - tz * edge[1]
            qy = tz * edge[0] - tx * edge[2]
            qz = tx * edge[1] - ty * edge[0]
            v = (dx * qx + dy * qy + dz * qz) / determinant
            if v < -generator.GEOMETRY_TOLERANCE or u + v > 1 + generator.GEOMETRY_TOLERANCE:
                continue
            depth = (other[0] * qx + other[1] * qy + other[2] * qz) / determinant
            if nearest is None or depth > nearest[0]:
                nearest = depth, materials
        return nearest

    def test_near_front_fender_skin_blocks_source_sightlines(self):
        source = self.body_ray_faces(self.shapes["car0"])
        near = self.body_ray_faces(self.shapes["car1"])
        painted_rays = 0
        for side in (-1, 1):
            for x, z in FRONT_FENDER_WITNESSES:
                anchor = side * x, FRONT_FENDER_RAY_HEIGHT, z
                for view in FRONT_FENDER_VIEW_DIRECTIONS:
                    length = math.sqrt(sum(component * component for component in view))
                    direction = side * view[0] / length, view[1] / length, view[2] / length
                    original = self.body_ray_hit(source, anchor, direction)
                    # Open source vents and views blocked by another component
                    # do not imply that the fender skin should fill that ray.
                    if original is None or len(set(original[1])) == 1:
                        continue
                    source_hit = [coordinate + original[0] * component
                                  for coordinate, component in zip(anchor, direction)]
                    if (not FRONT_FENDER_ABS_X_BOUNDS[0] <= abs(source_hit[0]) <=
                            FRONT_FENDER_ABS_X_BOUNDS[1] or
                            not FRONT_FENDER_Z_BOUNDS[0] <= source_hit[2] <= FRONT_FENDER_Z_BOUNDS[1] or
                            source_hit[1] <= FRONT_FENDER_MINIMUM_Y):
                        continue
                    painted_rays += 1
                    with self.subTest(side=side, anchor=anchor, direction=view):
                        simplified = self.body_ray_hit(near, anchor, direction)
                        self.assertIsNotNone(simplified)
                        self.assertGreaterEqual(simplified[0],
                                                original[0] - FRONT_FENDER_DEPTH_TOLERANCE)
        self.assertGreaterEqual(painted_rays, FRONT_FENDER_MINIMUM_PAINTED_RAYS)

    def test_near_hood_retains_painted_source_coverage_between_wheel_vents(self):
        source = self.body_ray_faces(self.shapes["car0"])
        near = self.body_ray_faces(self.shapes["car1"])
        painted_rays = 0
        paint_tables = {model.PAINT_TOP, model.PAINT_SIDE, model.PAINT_EDGE}
        for x in FRONT_PANEL_SAMPLE_X:
            for z in HOOD_SAMPLE_Z:
                original = self.body_ray_hit(source, (x, 0., z), (0., 1., 0.))
                # The retained source has real wheel vents and glass; those
                # openings must not be turned into painted hole-filling caps.
                if (original is None or original[1] not in paint_tables or
                        original[0] <= HOOD_MINIMUM_Y):
                    continue
                painted_rays += 1
                for side in (-1, 1):
                    with self.subTest(side=side, x=x, z=z):
                        simplified = self.body_ray_hit(near, (side * x, 0., z), (0., 1., 0.))
                        self.assertIsNotNone(simplified)
                        self.assertIn(simplified[1], paint_tables)
                        self.assertAlmostEqual(original[0], simplified[0],
                                               delta=FRONT_PANEL_DEPTH_TOLERANCE)
        self.assertGreaterEqual(painted_rays, HOOD_MINIMUM_PAINTED_RAYS)

    def test_near_front_bumper_retains_continuous_source_paint(self):
        source = self.body_ray_faces(self.shapes["car0"])
        near = self.body_ray_faces(self.shapes["car1"])
        painted_rays = 0
        paint_tables = {model.PAINT_TOP, model.PAINT_SIDE, model.PAINT_EDGE}
        for y in BUMPER_SAMPLE_Y:
            for x in FRONT_PANEL_SAMPLE_X:
                original = self.body_ray_hit(source, (x, y, 0.), (0., 0., 1.))
                # Only the source's painted nose supplies expectations; its
                # lower intake and dark splitter remain legitimate surfaces.
                if (original is None or original[1] not in paint_tables or
                        original[0] <= BUMPER_MINIMUM_Z):
                    continue
                margin = BUMPER_MATERIAL_BOUNDARY_MARGIN
                neighbors = [self.body_ray_hit(source, (x + dx, y + dy, 0.), (0., 0., 1.))
                             for dx, dy in ((-margin, 0.), (margin, 0.),
                                            (0., -margin), (0., margin))]
                # The reduced outline approximates the paint/carbon seam;
                # enforce clean color inside the painted band, away from it.
                if any(hit is None or hit[1] not in paint_tables for hit in neighbors):
                    continue
                painted_rays += 1
                for side in (-1, 1):
                    with self.subTest(side=side, x=x, y=y):
                        simplified = self.body_ray_hit(near, (side * x, y, 0.), (0., 0., 1.))
                        self.assertIsNotNone(simplified)
                        self.assertIn(simplified[1], paint_tables)
                        self.assertAlmostEqual(original[0], simplified[0],
                                               delta=FRONT_PANEL_DEPTH_TOLERANCE)
        self.assertGreaterEqual(painted_rays, BUMPER_MINIMUM_PAINTED_RAYS)

    def assert_source_painted_probes(self, probes, source_bounds, minimum_rays, *,
                                    depth_tolerance=REAR_PAINT_DEPTH_TOLERANCE):
        """Check visible skin against the entire source, excluding material seams."""
        source = self.body_ray_faces(self.shapes["car0"])
        near = self.body_ray_faces(self.shapes["car1"])
        paint_tables = {model.PAINT_TOP, model.PAINT_SIDE, model.PAINT_EDGE}
        painted_rays = 0
        for anchor, view in probes:
            length = math.sqrt(sum(value * value for value in view))
            direction = tuple(value / length for value in view)
            original = self.body_ray_hit(source, anchor, direction)
            if original is None or original[1] not in paint_tables:
                continue
            point = [coordinate + original[0] * component
                     for coordinate, component in zip(anchor, direction)]
            if any(not lower <= coordinate <= upper
                   for coordinate, (lower, upper) in zip(point, source_bounds)):
                continue
            dx, dy, dz = direction
            horizontal = (dz, 0., -dx) if dx or dz else (1., 0., 0.)
            length = math.sqrt(sum(value * value for value in horizontal))
            horizontal = tuple(value / length for value in horizontal)
            hx, hy, hz = horizontal
            vertical = (dy * hz - dz * hy, dz * hx - dx * hz, dx * hy - dy * hx)
            neighbors = [self.body_ray_hit(source, tuple(
                coordinate + sign * REAR_PAINT_SAMPLE_MARGIN * component
                for coordinate, component in zip(anchor, tangent)), direction)
                for tangent in (horizontal, vertical) for sign in (-1, 1)]
            # Real vents, glass boundaries and source silhouettes are not
            # missing paint. Require a painted footprint around each witness.
            if any(hit is None or hit[1] not in paint_tables for hit in neighbors):
                continue
            painted_rays += 1
            for side in (-1, 1):
                mirrored_anchor = (side * anchor[0], anchor[1], anchor[2])
                mirrored_direction = (side * direction[0], direction[1], direction[2])
                with self.subTest(side=side, anchor=anchor, direction=view):
                    simplified = self.body_ray_hit(near, mirrored_anchor, mirrored_direction)
                    self.assertIsNotNone(simplified)
                    self.assertIn(simplified[1], paint_tables)
                    # A painted floor seen through absent roof triangles has
                    # the right color but fails this independent depth check.
                    self.assertAlmostEqual(original[0], simplified[0],
                                           delta=depth_tolerance)
        self.assertGreaterEqual(painted_rays, minimum_rays)

    def test_near_rear_arch_retains_source_paint_from_side_and_oblique_views(self):
        probes = [((0., y, z), (1., 0., 0.))
                  for y in REAR_ARCH_SAMPLE_Y for z in REAR_ARCH_SAMPLE_Z]
        probes.extend(((*REAR_ARCH_OBLIQUE_XY, z), direction)
                      for z in REAR_ARCH_OBLIQUE_Z for direction in REAR_OBLIQUE_DIRECTIONS)
        self.assert_source_painted_probes(probes, REAR_ARCH_SOURCE_BOUNDS,
                                          REAR_ARCH_MINIMUM_PAINTED_RAYS)

    def test_near_rear_fender_crown_stays_within_the_source_height_profile(self):
        # Closing a gap must not create a raised spike. Independently sample
        # the visible source skin across the crown instead of inspecting the
        # repair's selected control points or its profile construction.
        probes = [((x, 0., z), (0., 1., 0.))
                  for x in REAR_CROWN_SAMPLE_X for z in REAR_CROWN_SAMPLE_Z]
        self.assert_source_painted_probes(probes, REAR_CROWN_SOURCE_BOUNDS,
                                          REAR_CROWN_MINIMUM_PAINTED_RAYS,
                                          depth_tolerance=REAR_CROWN_HEIGHT_TOLERANCE)

    def test_near_upper_rear_corners_retain_source_paint_and_depth(self):
        probes = [((x, y, 0.), (0., 0., -1.))
                  for x in REAR_CORNER_SAMPLE_X for y in REAR_CORNER_SAMPLE_Y]
        probes.extend(((*REAR_CORNER_OBLIQUE_XY, z), direction)
                      for z in REAR_CORNER_OBLIQUE_Z for direction in REAR_OBLIQUE_DIRECTIONS)
        self.assert_source_painted_probes(probes, REAR_CORNER_SOURCE_BOUNDS,
                                          REAR_CORNER_MINIMUM_PAINTED_RAYS)

    def test_near_aft_canopy_shoulder_covers_the_source_painted_roof(self):
        probes = [((x, 0., z), (0., 1., 0.))
                  for x in REAR_SHOULDER_SAMPLE_X for z in REAR_SHOULDER_SAMPLE_Z]
        probes.extend(((*REAR_SHOULDER_OBLIQUE_XY, z), direction)
                      for z in REAR_SHOULDER_OBLIQUE_Z for direction in REAR_OBLIQUE_DIRECTIONS)
        self.assert_source_painted_probes(probes, REAR_SHOULDER_SOURCE_BOUNDS,
                                          REAR_SHOULDER_MINIMUM_PAINTED_RAYS)

    def test_near_canopy_sill_meets_the_source_painted_body(self):
        probes = [((x, 0., z), (0., 1., 0.))
                  for x in CANOPY_SILL_SAMPLE_X for z in CANOPY_SILL_SAMPLE_Z]
        probes.extend(((*CANOPY_SILL_OBLIQUE_XY, z), direction)
                      for z in CANOPY_SILL_OBLIQUE_Z for direction in REAR_OBLIQUE_DIRECTIONS)
        self.assert_source_painted_probes(probes, CANOPY_SILL_SOURCE_BOUNDS,
                                          CANOPY_SILL_MINIMUM_PAINTED_RAYS)

    def test_racing_envelopes_follow_the_showroom_source(self):
        showroom = self.shapes["car0"]
        source = [(x / showroom.scale,
                   (y - model.SHOWROOM_Y_OFFSET) / showroom.scale,
                   z / showroom.scale)
                  for x, y, z in showroom.vertices[CONTROLS_END:]]
        for name in RACING_VERTEX_BUDGETS:
            body = self.shapes[name].vertices[CONTROLS_END:]
            for axis in range(detailed.AXES):
                for bound in (min, max):
                    with self.subTest(shape=name, axis=axis, bound=bound.__name__):
                        self.assertAlmostEqual(bound(p[axis] for p in source),
                                               bound(p[axis] for p in body),
                                               delta=SOURCE_EXTENT_TOLERANCE)

    def test_rear_wing_survives_on_both_sides_of_the_body(self):
        showroom = self.shapes["car0"]
        for name in RACING_VERTEX_BUDGETS:
            for x in WING_SAMPLE_X:
                for z in WING_SAMPLE_Z:
                    with self.subTest(shape=name, x=x, z=z):
                        original = self.roof_hit(showroom, x * showroom.scale,
                                                 z * showroom.scale)
                        simplified = self.roof_hit(self.shapes[name], x, z)
                        self.assertIsNotNone(original)
                        self.assertIsNotNone(simplified)
                        source_height = ((original[0] - model.SHOWROOM_Y_OFFSET) /
                                         showroom.scale)
                        self.assertAlmostEqual(source_height, simplified[0],
                                               delta=WING_HEIGHT_TOLERANCE)
                        self.assertEqual(original[1], simplified[1])

    def test_outer_fender_crowns_retain_source_coverage(self):
        showroom = self.shapes["car0"]
        for name in RACING_VERTEX_BUDGETS:
            for x, _, z in model.WHEEL_CENTERS:
                x += math.copysign(model.WHEEL_HALF_WIDTH -
                                   generator.WHEEL_ARCH_SIDE_BAND, x)
                with self.subTest(shape=name, x=x, z=z):
                    original = self.roof_hit(showroom, x * showroom.scale,
                                             z * showroom.scale)
                    simplified = self.roof_hit(self.shapes[name], x, z)
                    self.assertIsNotNone(original)
                    self.assertIsNotNone(simplified)
                    source_height = ((original[0] - model.SHOWROOM_Y_OFFSET) /
                                     showroom.scale)
                    self.assertAlmostEqual(source_height, simplified[0],
                                           delta=SOURCE_EXTENT_TOLERANCE)

    def test_racing_body_has_no_unused_vertices_or_collapsed_duplicate_faces(self):
        for name in RACING_VERTEX_BUDGETS:
            shape = self.shapes[name]
            used, faces = set(), set()
            for kind, _, _, indices in shape.primitives:
                if kind == model.WHEEL_PRIMITIVE:
                    continue
                used.update(indices)
                for corner in range(1, len(indices) - 1):
                    triangle = tuple(shape.vertices[index] for index in
                                     (indices[0], indices[corner], indices[corner + 1]))
                    with self.subTest(shape=name, indices=indices, corner=corner):
                        self.assertNotEqual((0, 0, 0), detailed._cross(triangle))
                        key = tuple(sorted(triangle))
                        self.assertNotIn(key, faces)
                        faces.add(key)
            with self.subTest(shape=name):
                self.assertEqual(set(range(CONTROLS_END, len(shape.vertices))), used)

    def test_racing_materials_retain_source_palette_and_all_seven_paints(self):
        source_materials = {materials for _, _, materials, _ in self.shapes["car0"].primitives}
        for name in RACING_VERTEX_BUDGETS:
            materials = {colors for _, _, colors, _ in self.shapes[name].primitives}
            with self.subTest(shape=name):
                self.assertLessEqual(materials, source_materials)
                self.assertIn((model.BLACK,) * model.PAINT_COUNT, materials)
                self.assertIn((model.CARBON,) * model.PAINT_COUNT, materials)
                painted = materials & {model.PAINT_TOP, model.PAINT_SIDE, model.PAINT_EDGE}
                self.assertTrue(painted)
                self.assertTrue(all(len(set(colors)) == model.PAINT_COUNT for colors in painted))

    def test_source_provenance_dimensions_and_exterior_selection(self):
        self.assertEqual(detailed.SOURCE_SHA256, self.source["source_sha256"])
        self.assertGreater(self.source["source_triangles"], MINIMUM_SOURCE_TRIANGLES)
        self.assertGreater(len(self.source["triangles"]), MINIMUM_SOURCE_TRIANGLES)
        self.assertTrue(self.source["source_url"].startswith("https://sketchfab.com/"))
        self.assertEqual(detailed.SOURCE_LICENSE, self.source["license"])
        self.assertEqual(detailed.EXTERIOR_MESHES,
                         {mesh["mesh"] for mesh in self.source["meshes"]})
        z = [p[2] / detailed.SOURCE_UNITS for p in self.source["vertices"]]
        self.assertAlmostEqual(model.LENGTH, max(z) - min(z), delta=SOURCE_LENGTH_TOLERANCE)
        self.assertGreaterEqual(min(p[1] for p in self.source["vertices"]),
                                model.CONTACT_Y * detailed.SOURCE_UNITS)

    def test_all_lods_have_wide_indices_with_classic_wheel_controls(self):
        for name, shape in self.shapes.items():
            with self.subTest(shape=name):
                classic = model._showroom_car() if name == "car0" else model._car(simple=name == "car2")
                self.assertEqual(classic.vertices[model.WHEEL_CONTROL_START:CONTROLS_END],
                                 shape.vertices[model.WHEEL_CONTROL_START:CONTROLS_END])
                wheels = [p for p in shape.primitives if p[0] == model.WHEEL_PRIMITIVE]
                self.assertEqual([p for p in classic.primitives if p[0] == model.WHEEL_PRIMITIVE], wheels)
                self.assertEqual(detailed.WHEEL_COUNT, len(wheels))
                self.assertTrue(all(min(indices) >= CONTROLS_END
                                    for kind, _, _, indices in shape.primitives
                                    if kind != model.WHEEL_PRIMITIVE))
                self.assertGreater(len(shape.vertices), model.NATIVE_LIMIT)
                self.assertLessEqual(len(shape.vertices), model.HYPERVISION_LIMIT)
                self.assertLessEqual(len(shape.primitives), model.HYPERVISION_LIMIT)
                payload = shape.encode_hypervision()
                self.assertEqual((len(shape.vertices), len(shape.primitives), model.PAINT_COUNT, shape.coordinate_fraction_bits),
                                 struct.unpack_from("<4H", payload))
                self.assertTrue(all(len(materials) == model.PAINT_COUNT
                                    for _, _, materials, _ in shape.primitives))
                self.assertTrue(any(len(set(materials)) > 1
                                    for _, _, materials, _ in shape.primitives))

    def test_racing_vertex_budgets_include_bounds_and_wheel_controls(self):
        for name, (lower, upper) in RACING_VERTEX_BUDGETS.items():
            payloads = {"generated": self.shapes[name].encode_hypervision(),
                        "installed": self.installed_shapes[name]}
            for source, payload in payloads.items():
                with self.subTest(shape=name, source=source):
                    points, _, metadata = generator.decode_model(payload, name, hypervision=True)
                    # The complete encoded table includes the first 32 bounds
                    # and animated-wheel controls, as requested by the budget.
                    self.assertEqual(len(points), metadata["vertices"])
                    self.assertGreaterEqual(len(points), lower)
                    self.assertLessEqual(len(points), upper)
            with self.subTest(shape=name, source="authoring"):
                self.assertGreaterEqual(len(self.shapes[name].vertices), lower)
                self.assertLessEqual(len(self.shapes[name].vertices), upper)

    def test_installed_meshes_match_their_authoring_inputs(self):
        for name, shape in self.shapes.items():
            with self.subTest(shape=name):
                self.assertEqual(shape.encode_hypervision(), self.installed_shapes[name])

    @staticmethod
    def rear_brake_components(faces, scale):
        """Group brake surfaces by shared encoded positions, independently of authoring."""
        brake = (model.BRAKE_LIGHT,) * model.PAINT_COUNT
        neighbors = {}
        for _, _, materials, triangle in faces:
            if materials != brake or max(point[2] for point in triangle) / scale >= REAR_LAMP_MAXIMUM_Z:
                continue
            for point in triangle:
                neighbors.setdefault(point, set()).update(triangle)
        remaining = set(neighbors)
        components = []
        while remaining:
            pending = {remaining.pop()}
            component = set(pending)
            while pending:
                point = pending.pop()
                adjacent = neighbors[point] & remaining
                remaining.difference_update(adjacent)
                component.update(adjacent)
                pending.update(adjacent)
            components.append(component)
        return components

    @staticmethod
    def rear_ray_materials(faces, x, y):
        """Sample the rear surface without assuming the lamp's triangulation."""
        hits = []
        for _, _, materials, polygon in faces:
            for corner in range(1, len(polygon) - 1):
                a, b, c = polygon[0], polygon[corner], polygon[corner + 1]
                ux, uy = b[0] - a[0], b[1] - a[1]
                vx, vy = c[0] - a[0], c[1] - a[1]
                determinant = ux * vy - uy * vx
                if abs(determinant) <= generator.GEOMETRY_TOLERANCE:
                    continue
                px, py = x - a[0], y - a[1]
                u = (px * vy - py * vx) / determinant
                v = (ux * py - uy * px) / determinant
                if min(u, v, 1 - u - v) >= -generator.GEOMETRY_TOLERANCE:
                    hits.append((a[2] + u * (b[2] - a[2]) + v * (c[2] - a[2]), materials))
        if not hits:
            return None
        depth = min(hit[0] for hit in hits)
        return {materials for z, materials in hits
                if abs(z - depth) <= generator.GEOMETRY_TOLERANCE}

    def test_rear_lamps_have_four_visible_sized_rings_and_a_centre_kite(self):
        for name in ("car0", "car1"):
            scale = self.shapes[name].scale
            for source, payload in (("generated", self.shapes[name].encode_hypervision()),
                                    ("installed", self.installed_shapes[name])):
                _, faces = self.encoded_body_faces(payload, name)
                components = self.rear_brake_components(faces, scale)
                with self.subTest(shape=name, source=source):
                    self.assertEqual(REAR_LAMP_COUNT, len(components))
                    spots, kites = [], []
                    for component in components:
                        lower = tuple(min(p[axis] for p in component) for axis in range(detailed.AXES))
                        upper = tuple(max(p[axis] for p in component) for axis in range(detailed.AXES))
                        center = tuple((a + b) / 2 for a, b in zip(lower, upper))
                        target = kites if abs(center[0] / scale) < REAR_LAMP_NEAR_CENTRE_X else spots
                        target.append((component, lower, upper, center))
                    self.assertEqual(REAR_SPOT_COUNT, len(spots))
                    self.assertEqual(1, len(kites))
                    self.assertEqual(2, sum(center[0] < 0 for _, _, _, center in spots))
                    self.assertEqual(2, sum(center[0] > 0 for _, _, _, center in spots))
                    for component, lower, upper, center in spots:
                        self.assertGreaterEqual((upper[0] - lower[0]) / scale,
                                                REAR_SPOT_MINIMUM_DIAMETER)
                        self.assertAlmostEqual(upper[0] - lower[0], upper[1] - lower[1],
                                               delta=scale / (1 << detailed.FRACTION_BITS[name]))
                        radius = (upper[0] - lower[0]) * REAR_RIM_SAMPLE_FRACTION / 2
                        for dx, dy in ((radius, 0.), (-radius, 0.), (0., radius), (0., -radius)):
                            self.assertEqual({(model.BRAKE_LIGHT,) * model.PAINT_COUNT},
                                             self.rear_ray_materials(faces, center[0] + dx,
                                                                     center[1] + dy))
                        for dx, dy in ((0., 0.), (REAR_HOLE_SAMPLE_OFFSET, 0.),
                                       (-REAR_HOLE_SAMPLE_OFFSET, 0.), (0., REAR_HOLE_SAMPLE_OFFSET),
                                       (0., -REAR_HOLE_SAMPLE_OFFSET)):
                            self.assertEqual({(model.BLACK,) * model.PAINT_COUNT},
                                             self.rear_ray_materials(faces, center[0] + dx * scale,
                                                                     center[1] + dy * scale))
                    component, lower, upper, center = kites[0]
                    self.assertAlmostEqual(0., center[0])
                    self.assertGreater(upper[1] - lower[1], upper[0] - lower[0])
                    # The horizontal extrema sit between the pointed upper and
                    # lower ends. Its upper sides must be shorter than the
                    # lower sides, distinguishing the deltoid from a rhombus.
                    for x in (lower[0], upper[0]):
                        for point in component:
                            if point[0] == x:
                                self.assertGreater(point[1], lower[1])
                                self.assertLess(point[1], upper[1])
                                upper_side = math.dist(point, (center[0], upper[1], point[2]))
                                lower_side = math.dist(point, (center[0], lower[1], point[2]))
                                self.assertLess(upper_side, lower_side)

    def test_base_meshes_and_unrelated_surfaces_stay_unchanged(self):
        for name, base in detailed.build_shapes(rear_lamps=False).items():
            with self.subTest(shape=name):
                self.assertEqual(BASE_PAYLOAD_SHA256[name],
                                 hashlib.sha256(base.encode_hypervision()).hexdigest())
                if name in detailed.RACING_VERTEX_LIMITS:
                    self.assertLessEqual(len(base.vertices), detailed.RACING_VERTEX_LIMITS[name][1])
                final = self.shapes[name]
                self.assertEqual(base.vertices, final.vertices[:len(base.vertices)])
                self.assertGreaterEqual(len(final.primitives), len(base.primitives))
                for before, after in zip(base.primitives, final.primitives):
                    kind, flags, materials, indices = before
                    self.assertEqual((kind, flags, indices), (after[0], after[1], after[3]))
                    if materials != after[2]:
                        self.assertEqual((model.BRAKE_LIGHT,) * model.PAINT_COUNT, materials)
                        self.assertEqual((model.BLACK,) * model.PAINT_COUNT, after[2])
                        self.assertTrue(all(base.vertices[index][2] / base.scale < REAR_LAMP_MAXIMUM_Z
                                            for index in indices))

    def test_envelope_contains_all_geometry_and_stays_above_ground(self):
        for name, shape in self.shapes.items():
            with self.subTest(shape=name):
                bounds = shape.vertices[:model.WHEEL_CONTROL_START]
                lower = tuple(min(p[a] for p in bounds) for a in range(detailed.AXES))
                upper = tuple(max(p[a] for p in bounds) for a in range(detailed.AXES))
                self.assertTrue(all(all(lower[a] <= p[a] <= upper[a] for a in range(detailed.AXES))
                                    for p in shape.vertices))
                shift = model.SHOWROOM_Y_OFFSET if name == "car0" else 0
                self.assertGreaterEqual(min(p[1] for p in shape.vertices[CONTROLS_END:]),
                                        model.CONTACT_Y * shape.scale + shift)

    def test_encoded_side_triangles_clear_steered_native_tyres(self):
        for name, shape in self.shapes.items():
            with self.subTest(shape=name):
                points, triangles, _ = generator.decode_model(shape.encode_hypervision(), name,
                                                               hypervision=True)
                shift = model.SHOWROOM_Y_OFFSET if name == "car0" else 0
                clearances = generator.validate_wheel_arch_clearance(points, triangles,
                                                                       shape.scale, shift, name)
                self.assertGreaterEqual(min(clearances), generator.WHEEL_ARCH_MIN_CLEARANCE)

    def test_racing_precision_survives_encoding_and_controls_stay_native(self):
        for name in ("car1", "car2"):
            shape = self.shapes[name]
            payload = shape.encode_hypervision()
            points, _, _ = generator.decode_model(payload, name, hypervision=True)
            self.assertEqual(shape.vertices, points)
            self.assertEqual(detailed.RACING_FRACTION_BITS, struct.unpack_from("<H", payload, 6)[0])
        near = self.shapes["car1"]
        self.assertTrue(any(c != round(c) for point in near.vertices[CONTROLS_END:] for c in point))

    def test_classic_model_bytes_stay_unchanged(self):
        classic = Path(__file__).resolve().parents[2] / "assets/cars/STMCMU.3SH"
        self.assertEqual(classic.read_bytes(), model.build_models())

    def test_container_is_deterministic(self):
        self.assertEqual(detailed.build_container(), detailed.build_container())

    def test_distant_payload_stays_byte_identical(self):
        generated = generator.hypervision_resources(detailed.build_container())
        for source, payload in (("installed", self.installed_shapes["car2"]),
                                ("generated", generated["car2"])):
            with self.subTest(source=source):
                self.assertEqual(DISTANT_PAYLOAD_SHA256, hashlib.sha256(payload).hexdigest())

    def test_presentation_scale_matches_audi_height_reference(self):
        data = detailed.build_container()
        _, _, scale = detailed.container_header(data)
        shape = self.shapes["car0"]
        height = max(point[1] for point in shape.vertices[CONTROLS_END:]) / model.SHOWROOM_SCALE
        self.assertAlmostEqual(1050 / 1344, height * scale / 26)
        self.assertLess(scale, 1)

    def test_legacy_unscaled_container_remains_readable(self):
        resources = list(self.installed_shapes.items())
        offset = model.HYPERVISION_CONTAINER_HEADER_SIZE + len(resources) * model.HYPERVISION_CONTAINER_ENTRY_SIZE
        directory, payloads = bytearray(), bytearray()
        for name, payload in resources:
            directory.extend(struct.pack("<4sII", name.encode("ascii"), offset, len(payload)))
            payloads.extend(payload)
            offset += len(payload)
        legacy = (struct.pack("<4sHH", model.HYPERVISION_CONTAINER_MAGIC,
                              detailed.LEGACY_CONTAINER_VERSION, len(resources)) + directory + payloads)
        self.assertEqual(1, detailed.container_header(legacy)[2])
        self.assertEqual(self.installed_shapes, generator.hypervision_resources(legacy))

    def test_invalid_presentation_scale_is_rejected(self):
        for numerator, denominator in ((0, 1), (1, 0), (0, 0)):
            header = detailed.CONTAINER_HEADER.pack(model.HYPERVISION_CONTAINER_MAGIC,
                                                    detailed.CONTAINER_VERSION, 1,
                                                    numerator, denominator)
            with self.subTest(numerator=numerator, denominator=denominator):
                with self.assertRaisesRegex(ValueError, "presentation scale"):
                    generator.hypervision_resources(header)
        header = detailed.CONTAINER_HEADER.pack(model.HYPERVISION_CONTAINER_MAGIC,
                                                detailed.CONTAINER_VERSION, 1, 1, 1)
        with self.assertRaisesRegex(ValueError, "Truncated HyperVision scale header"):
            detailed.container_header(header[:-1])

    def test_lods_reduce_the_actual_geometry(self):
        counts = [len(self.shapes[name].primitives) for name in ("car0", "car1", "car2")]
        self.assertGreater(counts[0], counts[1])
        self.assertGreater(counts[1], counts[2])


if __name__ == "__main__":
    unittest.main()
