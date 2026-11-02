#!/usr/bin/env python3
"""Check centered wheel contacts, grounded tyres, and encoded wheel-arch clearance."""

import importlib.util
import math
from pathlib import Path
import struct
import sys
import unittest


sys.dont_write_bytecode = True
SCRIPT = Path(__file__).with_name("generate-mcmurtry-speirling.py")
SPEC = importlib.util.spec_from_file_location("mcmurtry_generator", SCRIPT)
generator = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(generator)
model = generator.model

QUANTIZATIONS = ((1, 0), (model.SHOWROOM_SCALE, model.SHOWROOM_Y_OFFSET))
CLEAR_RADIUS = 10.0
SIDEWALL_OUTER_RADIUS = 14.0
FINE_SEGMENTS = 12
COARSE_SEGMENTS = 4
STEERING_SEGMENTS = 36
STATIC_CLEAR_RADII = (9.3, 9.5)
HOLE_TRIANGLE_RADIUS = 24.0
CHANNEL_X = (9, 13, 11)
CHANNEL_REACH = 12.0


def encoded(point, scale, shift):
    x, y, z = point
    return round(x * scale), round(y * scale) + shift, round(z * scale)


def radial_point(wheel, radius, angle, scale, shift):
    cx, cy, cz = model.WHEEL_CENTERS[wheel]
    x = math.copysign(abs(cx) + model.WHEEL_HALF_WIDTH, cx)
    return encoded((x, cy + radius * math.cos(angle),
                    cz + radius * math.sin(angle)), scale, shift)


def annular_sidewalls(scale, shift, radii=None, segments=FINE_SEGMENTS):
    """Author planar annuli independently of the generated fender topology."""
    controls = model._Shape(scale)
    model._controls(controls)
    points = [(x, y + shift, z) for x, y, z in controls.vertices]
    radii = radii or (CLEAR_RADIUS,) * len(model.WHEEL_CENTERS)
    triangles = []
    for wheel, radius in enumerate(radii):
        for step in range(segments):
            angle, following = [index * math.tau / segments
                                for index in (step, step + 1)]
            a, b = [radial_point(wheel, radius, value, scale, shift)
                    for value in (angle, following)]
            c, d = [radial_point(wheel, SIDEWALL_OUTER_RADIUS, value, scale, shift)
                    for value in (following, angle)]
            triangles.extend(((a, b, c), (a, c, d)))
    return points, triangles


class WheelArchClearanceTests(unittest.TestCase):
    def validate(self, points, triangles, scale, shift):
        return generator.validate_wheel_arch_clearance(
            points, triangles, scale, shift, "synthetic sidewalls")

    def test_finely_segmented_annuli_clear_both_quantizations(self):
        for scale, shift in QUANTIZATIONS:
            with self.subTest(scale=scale, shift=shift):
                points, triangles = annular_sidewalls(scale, shift)
                clearances = self.validate(points, triangles, scale, shift)
                self.assertEqual(len(model.WHEEL_CENTERS), len(clearances))
                self.assertGreaterEqual(min(clearances), generator.WHEEL_ARCH_MIN_CLEARANCE)

    def test_clear_vertices_do_not_excuse_chords_crossing_the_tyres(self):
        for scale, shift in QUANTIZATIONS:
            with self.subTest(scale=scale):
                points, triangles = annular_sidewalls(scale, shift, segments=COARSE_SEGMENTS)
                for wheel, (_, cy, cz) in enumerate(model.WHEEL_CENTERS):
                    for radius in (CLEAR_RADIUS, SIDEWALL_OUTER_RADIUS):
                        for step in range(COARSE_SEGMENTS):
                            vertex = radial_point(wheel, radius, step * math.tau / COARSE_SEGMENTS,
                                                  scale, shift)
                            distance = math.hypot((vertex[1] - shift) / scale - cy,
                                                  vertex[2] / scale - cz)
                            self.assertGreaterEqual(distance, model.WHEEL_RADIUS +
                                                    generator.WHEEL_ARCH_MIN_CLEARANCE)
                with self.assertRaisesRegex(ValueError, "intersects.*wheel 1"):
                    self.validate(points, triangles, scale, shift)

    def test_triangle_interior_cannot_fill_a_clear_opening(self):
        for scale, shift in QUANTIZATIONS:
            with self.subTest(scale=scale):
                points, triangles = annular_sidewalls(scale, shift)
                # This equilateral triangle encloses the wheel centre, while
                # every edge stays about twelve units from that centre.
                triangle = tuple(radial_point(0, HOLE_TRIANGLE_RADIUS, index * math.tau / 3,
                                              scale, shift) for index in range(3))
                with self.assertRaisesRegex(ValueError, "intersects.*wheel 1"):
                    self.validate(points, triangles + [triangle], scale, shift)

    def test_inner_channel_faces_are_outside_the_outer_side_band(self):
        for scale, shift in QUANTIZATIONS:
            with self.subTest(scale=scale):
                points, triangles = annular_sidewalls(scale, shift)
                expected = self.validate(points, triangles, scale, shift)
                for cx, cy, cz in model.WHEEL_CENTERS:
                    side = math.copysign(1, cx)
                    yz = ((-CHANNEL_REACH, -CHANNEL_REACH),
                          (-CHANNEL_REACH, CHANNEL_REACH), (CHANNEL_REACH, 0))
                    triangles.append(tuple(encoded((side * x, cy + dy, cz + dz), scale, shift)
                                           for x, (dy, dz) in zip(CHANNEL_X, yz)))
                self.assertEqual(expected, self.validate(points, triangles, scale, shift))

    def test_static_rear_clearance_does_not_allow_undersized_front_sweep(self):
        scale, shift = QUANTIZATIONS[-1]
        for radius in STATIC_CLEAR_RADII:
            with self.subTest(radius=radius):
                rear_only = (CLEAR_RADIUS, CLEAR_RADIUS, radius, radius)
                points, triangles = annular_sidewalls(scale, shift, rear_only, STEERING_SEGMENTS)
                self.assertGreaterEqual(min(self.validate(points, triangles, scale, shift)),
                                        generator.WHEEL_ARCH_MIN_CLEARANCE)
                front_only = (radius, radius, CLEAR_RADIUS, CLEAR_RADIUS)
                points, triangles = annular_sidewalls(scale, shift, front_only, STEERING_SEGMENTS)
                with self.assertRaisesRegex(ValueError, "intersects.*wheel 1"):
                    self.validate(points, triangles, scale, shift)

    def test_generated_showroom_and_both_racing_models_clear(self):
        shapes = generator.resources(model.build_models())
        for name, scale, shift in (("car0", *QUANTIZATIONS[-1]),
                                   ("car1", *QUANTIZATIONS[0]),
                                   ("car2", *QUANTIZATIONS[0])):
            with self.subTest(shape=name):
                points, triangles, _ = generator.decode_model(shapes[name], name)
                self.assertGreaterEqual(min(self.validate(points, triangles, scale, shift)),
                                        generator.WHEEL_ARCH_MIN_CLEARANCE)


PHYSICAL_WHEEL_OFFSET = 0xD2
PHYSICAL_WHEEL_RECORD_SIZE = struct.calcsize("<3h")
PHYSICAL_POSITION_SCALE = 64


class WheelContactGeometryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        car = generator.resources((generator.ROOT / "assets/cars/CARMCMU.RES").read_bytes())
        cls.simd = car["simd"]
        cls.shapes = generator.resources(model.build_models())

    def contacts(self, simd=None):
        return [struct.unpack_from("<3h", self.simd if simd is None else simd,
                                   PHYSICAL_WHEEL_OFFSET + wheel * PHYSICAL_WHEEL_RECORD_SIZE)
                for wheel in range(len(model.WHEEL_CENTERS))]

    def cases(self):
        for name, scale, shift in (("car0", *QUANTIZATIONS[-1]),
                                   ("car1", *QUANTIZATIONS[0]),
                                   ("car2", *QUANTIZATIONS[0])):
            points, _, _ = generator.decode_model(self.shapes[name], name)
            yield name, points, scale, shift

    def test_physical_wheels_are_centered_at_the_engine_origin(self):
        contacts = self.contacts()
        self.assertEqual(0, sum(point[0] for point in contacts))
        self.assertEqual(0, sum(point[2] for point in contacts))
        self.assertTrue(all(point[1] == 0 for point in contacts))
        wheelbase = (contacts[0][2] - contacts[-1][2]) / PHYSICAL_POSITION_SCALE
        self.assertEqual(generator.physics.REAL_SPECIFICATIONS["wheelbase_mm"] /
                         model.MM_PER_UNIT, wheelbase)

    def test_generated_physical_and_visual_wheels_share_their_grounded_geometry(self):
        contacts = self.contacts()
        for name, points, scale, shift in self.cases():
            with self.subTest(shape=name):
                generator.validate_wheel_contacts(self.simd, points, scale, shift, name)
                for wheel, contact in enumerate(contacts):
                    first = model.WHEEL_CONTROL_START + wheel * model.WHEEL_CONTROL_COUNT
                    inner, radial, outer = [points[first + index] for index in (0, 1, 3)]
                    for axis in (0, 2):
                        self.assertEqual((inner[axis] + outer[axis]) * PHYSICAL_POSITION_SCALE,
                                         2 * contact[axis] * scale)
                    self.assertEqual(model.CONTACT_Y * scale + shift,
                                     inner[1] - (radial[1] - inner[1]))
                    self.assertEqual(model.WHEEL_RADIUS * scale, radial[1] - inner[1])

    def test_translating_both_physics_and_visual_wheels_cannot_hide_an_offset_origin(self):
        for name, points, scale, shift in self.cases():
            for axis in (0, 2):
                with self.subTest(shape=name, axis=axis):
                    simd = bytearray(self.simd)
                    translated = list(points)
                    for wheel, contact in enumerate(self.contacts()):
                        offset = PHYSICAL_WHEEL_OFFSET + wheel * PHYSICAL_WHEEL_RECORD_SIZE
                        struct.pack_into("<h", simd, offset + axis * struct.calcsize("<h"),
                                         contact[axis] + PHYSICAL_POSITION_SCALE)
                        first = model.WHEEL_CONTROL_START + wheel * model.WHEEL_CONTROL_COUNT
                        for index in range(first, first + model.WHEEL_CONTROL_COUNT):
                            point = list(translated[index])
                            point[axis] += scale
                            translated[index] = tuple(point)
                    with self.assertRaises(ValueError):
                        generator.validate_wheel_contacts(simd, translated, scale, shift, name)

    def test_every_physical_wheel_requires_zero_y_even_when_the_average_is_zero(self):
        simd = bytearray(self.simd)
        for wheel, height in enumerate((PHYSICAL_POSITION_SCALE, -PHYSICAL_POSITION_SCALE)):
            offset = PHYSICAL_WHEEL_OFFSET + wheel * PHYSICAL_WHEEL_RECORD_SIZE
            struct.pack_into("<h", simd, offset + struct.calcsize("<h"), height)
        for name, points, scale, shift in self.cases():
            with self.subTest(shape=name):
                with self.assertRaises(ValueError):
                    generator.validate_wheel_contacts(simd, points, scale, shift, name)

    def test_visual_wheel_offsets_are_rejected_with_centered_physics(self):
        for name, points, scale, shift in self.cases():
            for axis in (0, 2):
                with self.subTest(shape=name, axis=axis):
                    displaced = list(points)
                    for index in range(model.WHEEL_CONTROL_START,
                                       model.WHEEL_CONTROL_START + model.WHEEL_CONTROL_COUNT):
                        point = list(displaced[index])
                        point[axis] += scale
                        displaced[index] = tuple(point)
                    with self.assertRaises(ValueError):
                        generator.validate_wheel_contacts(self.simd, displaced, scale, shift, name)

    def test_visual_tyres_cannot_float_above_the_engine_contact_plane(self):
        for name, points, scale, shift in self.cases():
            with self.subTest(shape=name):
                displaced = list(points)
                for index in range(model.WHEEL_CONTROL_START,
                                   model.WHEEL_CONTROL_START + model.WHEEL_CONTROL_COUNT):
                    x, y, z = displaced[index]
                    displaced[index] = (x, y + scale, z)
                with self.assertRaises(ValueError):
                    generator.validate_wheel_contacts(self.simd, displaced, scale, shift, name)

    def test_changed_tyres_cannot_hide_behind_the_same_contact_plane(self):
        for name, points, scale, shift in self.cases():
            with self.subTest(shape=name):
                changed = list(points)
                for index in range(model.WHEEL_CONTROL_START,
                                   model.WHEEL_CONTROL_START + model.WHEEL_CONTROL_COUNT):
                    x, y, z = changed[index]
                    extra = 2 if index in (model.WHEEL_CONTROL_START + 1,
                                          model.WHEEL_CONTROL_START + 4) else 1
                    changed[index] = (x, y + extra * scale, z)
                with self.assertRaises(ValueError):
                    generator.validate_wheel_contacts(self.simd, changed, scale, shift, name)


if __name__ == "__main__":
    unittest.main()
