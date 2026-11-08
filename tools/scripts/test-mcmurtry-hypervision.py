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


class DetailedGeometryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = detailed.load_source()
        cls.shapes = detailed.build_shapes()
        companion = Path(__file__).resolve().parents[2] / "assets/cars/STMCMU.H3D"
        cls.installed_shapes = generator.hypervision_resources(companion.read_bytes())

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
                    self.assertGreaterEqual(len(points), target * (1 - VERTEX_TARGET_TOLERANCE))
                    self.assertLessEqual(len(points), target * (1 + VERTEX_TARGET_TOLERANCE))
            with self.subTest(shape=name, source="authoring"):
                self.assertGreaterEqual(len(self.shapes[name].vertices),
                                        target * (1 - VERTEX_TARGET_TOLERANCE))
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
