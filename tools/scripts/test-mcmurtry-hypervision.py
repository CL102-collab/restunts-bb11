#!/usr/bin/env python3
"""Check source provenance, detailed H3D geometry and shared animated wheels."""

import hashlib
import importlib.util
import math
from pathlib import Path
import struct
import tempfile
import unittest

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
RACING_VERTEX_TARGETS = {"car1": 1_000, "car2": 500}
VERTEX_TARGET_TOLERANCE = .1
SHOWROOM_PAYLOAD_SHA256 = "cd6267449c1a10c19c19dc1c8375aafe3dc221d41dbcc6cbbd96d7e64852dd03"
PLANAR_HEIGHT = 2.0
NEARBY_LAYER_GAP = 1 / (1 << detailed.RACING_FRACTION_BITS)
APERTURE_SAMPLE_FRACTIONS = (.2, .5, .8)
MINIMUM_APERTURE_DEPTH = .25
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

    def test_four_wheel_top_openings_are_visible_recesses(self):
        black = (model.BLACK,) * model.PAINT_COUNT
        for name in RACING_VERTEX_TARGETS:
            shape = self.shapes[name]
            uncut = detailed._DetailedShape(1, detailed.FRACTION_BITS[name])
            racing.populate_shape(uncut, name, model)
            for wheel in range(detailed.WHEEL_COUNT):
                lip = model.vent_geometry(wheel)["lip"]
                for u in APERTURE_SAMPLE_FRACTIONS:
                    for v in APERTURE_SAMPLE_FRACTIONS:
                        point = tuple((1 - v) * ((1 - u) * lip[0][axis] + u * lip[1][axis]) +
                                      v * ((1 - u) * lip[3][axis] + u * lip[2][axis])
                                      for axis in range(detailed.AXES))
                        x, z = point[0], point[2] - model.BODY_ORIGIN_Z
                        with self.subTest(shape=name, wheel=wheel, u=u, v=v):
                            hit = self.roof_hit(shape, x, z)
                            roof = self.roof_hit(uncut, x, z)
                            self.assertIsNotNone(hit)
                            self.assertIsNotNone(roof)
                            self.assertEqual(black, hit[1])
                            self.assertGreaterEqual(roof[0] - hit[0], MINIMUM_APERTURE_DEPTH)

    def test_racing_mirror_stalks_are_omitted(self):
        for name in RACING_VERTEX_TARGETS:
            with self.subTest(shape=name):
                self.assertFalse(any(kind == model.LINE_PRIMITIVE
                                     for kind, _, _, _ in self.shapes[name].primitives))

    def test_recessed_roofs_clear_the_entire_upper_tyre_envelope(self):
        for name in RACING_VERTEX_TARGETS:
            with self.subTest(shape=name):
                points, triangles, _ = generator.decode_model(
                    self.shapes[name].encode_hypervision(), name, hypervision=True)
                clearance = generator.validate_wheel_covers(points, triangles, 1, 0, name)
                self.assertGreaterEqual(clearance, generator.WHEEL_ARCH_MIN_CLEARANCE)

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
        for name, target in RACING_VERTEX_TARGETS.items():
            payloads = {"generated": self.shapes[name].encode_hypervision(),
                        "installed": self.installed_shapes[name]}
            for source, payload in payloads.items():
                with self.subTest(shape=name, source=source):
                    points, _, metadata = generator.decode_model(payload, name, hypervision=True)
                    # The complete encoded table includes the first 32 bounds
                    # and animated-wheel controls, as requested by the budget.
                    self.assertEqual(len(points), metadata["vertices"])
                    # Simpler coherent surfaces may use less than the target.
                    self.assertGreater(len(points), model.NATIVE_LIMIT)
                    self.assertLessEqual(len(points), target * (1 + VERTEX_TARGET_TOLERANCE))
            with self.subTest(shape=name, source="authoring"):
                self.assertLessEqual(len(self.shapes[name].vertices),
                                     target * (1 + VERTEX_TARGET_TOLERANCE))

    def test_showroom_payload_stays_byte_identical(self):
        payloads = {"generated": self.shapes["car0"].encode_hypervision(),
                    "installed": self.installed_shapes["car0"]}
        for source, payload in payloads.items():
            with self.subTest(source=source):
                self.assertEqual(SHOWROOM_PAYLOAD_SHA256, hashlib.sha256(payload).hexdigest())

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

    def test_lods_reduce_the_actual_geometry(self):
        counts = [len(self.shapes[name].primitives) for name in ("car0", "car1", "car2")]
        self.assertGreater(counts[0], counts[1])
        self.assertGreater(counts[1], counts[2])


if __name__ == "__main__":
    unittest.main()
