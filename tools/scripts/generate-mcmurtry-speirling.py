#!/usr/bin/env python3
"""Build and validate the MCMU classic car and detailed HyperVision companion.

Run with --install stunts to install the game resources in stunts/cars, or --check to
verify that the checked-in files match their deterministic authoring sources.
The recorded reference GLB and graphics packages are not needed to regenerate them.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

import mcmurtry_dashboard as dashboard
import mcmurtry_hypervision as detailed_model
import mcmurtry_model as model
import mcmurtry_physics as physics


ROOT = Path(__file__).resolve().parents[2]
CAR_ID = "MCMU"
MODEL_DIMENSION_TOLERANCE = 1.1
WHEEL_ARCH_MIN_CLEARANCE = 1.0
WHEEL_ARCH_SIDE_BAND = 1.0
FRONT_STEERED_WHEEL_COUNT = 2
GEOMETRY_TOLERANCE = 1e-9
PHYSICS_POSITION_SCALE = 64
SIMD_WHEEL_COORDINATE_OFFSET = 0xD2
SIMD_WHEEL_COORDINATE_SIZE = struct.calcsize("<3h")
SIMD_COLLISION_BOUNDS_OFFSET = 0xC8
GAME_FILES = ("CARMCMU.RES", "STMCMU.3SH", "STDAMCMU.VSH", "STDBMCMU.VSH", "STMCMU.H3D")
HYPERVISION_SHAPE_HEADER_SIZE = struct.calcsize("<4H")
HYPERVISION_FRACTION_BITS_OFFSET = struct.calcsize("<3H")
CAR_BODY_VERTEX_START = (model.WHEEL_CONTROL_START +
                         model.WHEEL_CONTROL_COUNT * len(model.WHEEL_CENTERS))
DOWNLOADED_MODEL = {
    "title": "2026 McMurtry Spéirling",
    "author": "Dave Love SketchFab",
    "author_url": "https://sketchfab.com/Tyler_Dave",
    "source_url": "https://sketchfab.com/3d-models/2026-mcmurtry-speirling-24a39a15bde049acbb3ede571cee0c8b",
    "license": "CC-BY-4.0",
    "license_url": "http://creativecommons.org/licenses/by/4.0/",
    "attribution_basis": "Author, title, source and license recorded in the supplied GLB asset.extras metadata.",
    "supplied_archive": "2026-mcmurtry-speirling.zip",
    "glb_sha256": "c32649493eaebec5005e7b8824bb429d77aa3fdbab277282e37b1b918b7f1013",
    "measured_source_profile": "tools/scripts/mcmurtry_source_profile.json",
    "source_triangles": 211145, "source_meshes": 110, "source_materials": 43,
    "embedded_source_name": "exmods_mcmurtry_speirling22.fbx",
    "geometry_reference_policy": "Use the supplied high-resolution model for the overall exterior design. Use curved body-coloured front and rear panels around compact dark-grey insets instead of oversized black caps. Simplify the highlighted side duct behind each front wheel into a narrow upright recessed gill which opens toward the rear, surrounded by body-coloured skin. Prioritize rounded wheel surrounds and native tyre clearance over exact source geometry around the wheels; preserve the accepted canopy surface and omit the thin centre fin above the roof and rear cowl. The internal filename suggests an earlier prototype despite the 2026 download title.",
    "modifications": "Completely rebuilt native low-polygon exterior topology for the showroom and both racing detail levels. Rounded wheel pods and enlarged wheel openings intentionally depart from the source where needed for a visible tyre-to-chassis gap, including native quantization and front steering. The front fender crown follows the source's shallow side profile, with only the lift needed over the enlarged native wheel arch. The oversized flat black front mouth is replaced by curved body-coloured upper and lower bands and rolled side cheeks around a narrow dark-grey inset. The four white front lamp markers are omitted. Mirror stalks join existing fender-crown vertices, preserving room for the curved nose and side ducts; mirror heads are retained. A tall, narrow, tapered dark-grey gill replaces the flat skin behind each front wheel, with an inward centre behind its outer lip so the grey opening faces rearward. Its surrounding painted panels follow every existing wheel-arch chord. The rear equipment-bay cap is replaced by painted rolled shoulders and a lower bumper around a small recessed dark-grey panel. Four small circular brake-light rings form an upper and lower pair at each rear corner, following the supplied rear photographs. Their nominal diameter is reduced by 10%, with dark concentric centres like the latest rear-view reference. Shared crown controls, merged coplanar sill triangles and a tapered hidden floor retain the native vertex budget. A connected rising rear undertray ends at the body fascia, replacing the projecting low flap and central strake. Its exposed diffuser ramp and corner returns use the same darker grey as the underbody. The thin centre fin above the canopy and rear cowl is omitted for a clean roof silhouette; the canopy surface is preserved. Other changes include uniform length scaling, animated wheels, palette materials and seven paint schemes. The body origin is translated to the axle midpoint, and native wheel controls and arches are adapted to a centered 50-unit wheelbase. The original GLB and textures are not included.",
}
SOURCES = list(dict.fromkeys(physics.REAL_SPECIFICATIONS["sources"] + [
    DOWNLOADED_MODEL["source_url"],
    "https://www.zr1netregistry.com/Information/C4ZR-1GeneralInfo/Dimensions.aspx",
    "https://acuranews.com/en-US/releases/release-ff6633ebcc698302413a3d004c34ca74-1991-acura-nsx-specifications",
]))
REFERENCE_IMAGES = [
    {"view": "2026 production front", "url": "https://mcmurtry.com/wp-content/uploads/2026/07/McMurtry-Speirling-PURE-front-2048x1280.jpg"},
    {"view": "2026 production front three-quarter", "url": "https://mcmurtry.com/wp-content/uploads/2026/07/McMurtry-Speirling-PURE-front-3_4-2048x1279.jpg"},
    {"view": "2026 production front detail", "url": "https://mcmurtry.com/wp-content/uploads/2026/07/McMurtry-Speirling-PURE-front-detail-2048x1280.jpg"},
    {"view": "2026 production rear", "url": "https://mcmurtry.com/wp-content/uploads/2026/07/McMurtry-Speirling-PURE-rear-2048x1280.jpg"},
    {"view": "2026 production rear three-quarter", "url": "https://mcmurtry.com/wp-content/uploads/2026/07/McMurtry-Speirling-PURE-rear-3_4-2048x1280.jpg"},
    {"view": "2026 production rear studio detail", "url": "https://mcmurtry.com/wp-content/uploads/2026/06/rear-FINAL-2048x1365.jpg"},
    {"view": "2026 production rear track detail", "url": "https://mcmurtry.com/wp-content/uploads/2026/06/TRACK-REAR-1-2048x1366.jpg"},
    {"view": "2026 production overhead", "url": "https://mcmurtry.com/wp-content/uploads/2026/06/TRACK-OVERHEAD-1-2048x1365.jpg"},
]
INITIAL_REFERENCE_IMAGES = [
    {"view": "front", "url": "https://mcmurtry.com/wp-content/uploads/2024/06/McMurtry-Speirling-PURE-Front-View-1-1024x723-1.jpg"},
    {"view": "side", "url": "https://mcmurtry.com/wp-content/uploads/2024/06/McMurtry-Speirling-PURE-Side-View-1-1024x723-1.jpg"},
    {"view": "rear", "url": "https://mcmurtry.com/wp-content/uploads/2024/06/McMurtry-Speirling-PURE-Rear-3-4-1024x723-1.jpg"},
    {"view": "front three-quarter", "url": "https://mcmurtry.com/wp-content/uploads/2024/06/Speirling-PURE-Hero-Shot-with-logos-1024x723-1.jpg"},
]
DETAIL_REFERENCE_IMAGES = [
    {"file": "1000005898.jpg", "view": "Close-up of front wheel-crown aperture, rim, dark recess and cross rib."},
    {"file": "1000005901.jpg", "view": "Rear wheel-crown apertures beside the wing."},
    {"file": "1000005899.jpg", "view": "Front three-quarter view showing both front apertures."},
    {"file": "1000005900.jpg", "view": "Low front three-quarter view showing the aperture within the fender crown."},
]


def resources(data):
    """Validate a raw DSI container, including an unsorted stock directory."""
    if len(data) < 6:
        raise ValueError("Truncated DSI container")
    size, count = struct.unpack_from("<IH", data)
    base = 6 + 8 * count
    if size != len(data) or not count or base >= size:
        raise ValueError("Invalid DSI header")
    ids = [data[6 + 4 * i:10 + 4 * i].decode("ascii") for i in range(count)]
    offsets = list(struct.unpack_from(f"<{count}I", data, 6 + 4 * count))
    if len(set(ids)) != count or len(set(offsets)) != count or min(offsets) != 0:
        raise ValueError("Duplicate DSI entry or missing first payload")
    ordered = sorted(offsets) + [size - base]
    ends = dict(zip(ordered, ordered[1:]))
    if any(offset >= size - base for offset in offsets):
        raise ValueError("DSI offset outside container")
    return {name: data[base + offset:base + ends[offset]]
            for name, offset in zip(ids, offsets)}


def hypervision_resources(data):
    """Validate the optional companion directory before decoding its meshes."""
    cursor, count, _ = detailed_model.container_header(data)
    directory_end = cursor + count * model.HYPERVISION_CONTAINER_ENTRY_SIZE
    if directory_end > len(data):
        raise ValueError("Truncated HyperVision directory")
    result = {}
    previous_end = directory_end
    for _ in range(count):
        name, offset, size = struct.unpack_from("<4sII", data, cursor)
        name = name.decode("ascii")
        if name in result or offset != previous_end or offset + size > len(data):
            raise ValueError("Invalid HyperVision entry")
        if size < HYPERVISION_SHAPE_HEADER_SIZE:
            raise ValueError("Truncated HyperVision shape")
        fraction_bits = struct.unpack_from("<H", data, offset + HYPERVISION_FRACTION_BITS_OFFSET)[0]
        if fraction_bits > detailed_model.MAX_FRACTION_BITS:
            raise ValueError("Invalid HyperVision coordinate precision")
        result[name] = data[offset:offset + size]
        previous_end = offset + size
        cursor += model.HYPERVISION_CONTAINER_ENTRY_SIZE
    if previous_end != len(data):
        raise ValueError("Unreferenced HyperVision data")
    return result


def model_layout(data, name, *, hypervision=False):
    header_size = HYPERVISION_SHAPE_HEADER_SIZE if hypervision else 4
    if len(data) < header_size:
        raise ValueError(f"Truncated {name}")
    counts = struct.unpack_from("<4H", data) if hypervision else data[:4]
    vertices, primitives, paints, reserved = counts
    if (not vertices or not primitives or paints != model.PAINT_COUNT or
            (reserved > detailed_model.MAX_FRACTION_BITS if hypervision else reserved)):
        raise ValueError(f"Invalid shape header in {name}")
    index_size = 2 if hypervision and vertices > model.NATIVE_LIMIT else 1
    return vertices, primitives, paints, header_size, index_size


def decode_model(data, name, *, hypervision=False):
    vertices, primitives, paints, header_size, index_size = model_layout(
        data, name, hypervision=hypervision)
    cursor = header_size + vertices * 6 + primitives * 8
    if cursor > len(data):
        raise ValueError(f"Truncated shape tables in {name}")
    points = [struct.unpack_from("<3h", data, header_size + i * 6) for i in range(vertices)]
    fraction_bits = (struct.unpack_from("<H", data, HYPERVISION_FRACTION_BITS_OFFSET)[0]
                     if hypervision else 0)
    if fraction_bits:
        divisor = 1 << fraction_bits
        points[CAR_BODY_VERTEX_START:] = [tuple(c / divisor for c in point)
                                         for point in points[CAR_BODY_VERTEX_START:]]
    wheels = []
    triangles = []
    for _ in range(primitives):
        if cursor + 2 + paints > len(data):
            raise ValueError(f"Truncated primitive in {name}")
        kind, flags = data[cursor:cursor + 2]
        count = kind if 1 <= kind <= 10 else {11: 2, 12: 6}.get(kind)
        if count is None or flags & ~3:
            raise ValueError(f"Invalid primitive type or flags in {name}")
        materials = data[cursor + 2:cursor + 2 + paints]
        index_cursor = cursor + 2 + paints
        if index_cursor + count * index_size > len(data):
            raise ValueError(f"Truncated primitive indices in {name}")
        indices = struct.unpack_from(f"<{count}{'H' if index_size == 2 else 'B'}", data, index_cursor)
        if max(indices) >= vertices or max(materials) > 126:
            raise ValueError(f"Invalid primitive index or material in {name}")
        if kind == 12:
            wheels.append(tuple(indices))
        if 3 <= kind <= 10:
            a, b, c = [points[i] for i in indices[:3]]
            u, v = [tuple(p[k] - a[k] for k in range(3)) for p in (b, c)]
            normal = (u[1] * v[2] - u[2] * v[1],
                      u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
            if normal == (0, 0, 0):
                raise ValueError(f"Degenerate first triangle in {name}")
            triangles.extend(tuple(points[i] for i in (indices[0], indices[j], indices[j + 1]))
                             for j in range(1, len(indices) - 1))
        cursor += 2 + paints + count * index_size
    valid_end = (cursor == len(data) if hypervision else
                 cursor < len(data) and not any(data[cursor:]))
    if not valid_end:
        raise ValueError(f"Invalid shape terminator in {name}")
    if name.startswith("car"):
        if wheels[:4] != [tuple(range(i, i + 6)) for i in (8, 14, 20, 26)]:
            raise ValueError(f"Animated wheel slots disagree with Stunts in {name}")
        for p in points[8:]:
            if any(not min(b[k] for b in points[:8]) <= p[k] <=
                   max(b[k] for b in points[:8]) for k in range(3)):
                raise ValueError(f"Vertex outside shape bounds in {name}")
    return points, triangles, {"vertices": vertices, "primitives": primitives, "paint_jobs": paints}


def wheel_cover_groups(data, points):
    """Collect roof faces which the native painter draws after each wheel."""
    vertices, primitives, paints, header_size, index_size = model_layout(data, "wheel cover groups")
    cursor = header_size + vertices * 6 + primitives * 8
    groups = []
    parent = None
    for _ in range(primitives):
        kind, flags = data[cursor:cursor + 2]
        count = kind if 1 <= kind <= 10 else {11: 2, 12: 6}[kind]
        indices = struct.unpack_from(f"<{count}{'H' if index_size == 2 else 'B'}", data,
                                     cursor + 2 + paints)
        if kind == 12:
            groups.append([])
            parent = len(groups) - 1
        elif not flags & 2:
            parent = None
        elif parent is not None and 3 <= kind <= 10:
            groups[parent].extend(tuple(points[i] for i in (indices[0], indices[j], indices[j + 1]))
                                  for j in range(1, len(indices) - 1))
        cursor += 2 + paints + count * index_size
    return groups


def validate_wheel_covers(points, triangles, scale, shift, name, *, wheel_groups=None):
    """Ray-test the encoded fenders against the entire upper tyre envelope.

    Side-view arches can look correct while their sloping top panels cut
    through the inner tread. Test the quantized mesh at quarter-unit intervals
    along each tyre and half-unit intervals across its width, in game units.
    When wheel_groups is supplied, only cover triangles attached to their own
    wheel count: an independently sorted face can be physically above a tyre
    yet still paint behind its rim. Native raster comparisons additionally
    check the actual occlusion in angled views.
    """
    def normalize(point):
        return point[0] / scale, (point[1] - shift) / scale, point[2] / scale

    def project(faces):
        result = []
        for triangle in faces:
            a, b, c = map(normalize, triangle)
            ux, uz, vx, vz = b[0] - a[0], b[2] - a[2], c[0] - a[0], c[2] - a[2]
            determinant = ux * vz - uz * vx
            if abs(determinant) > 1e-9:
                result.append((a, b, c, ux, uz, vx, vz, determinant))
        return result

    if wheel_groups is not None and len(wheel_groups) != 4:
        raise ValueError(f"Expected four wheel cover groups in {name}")
    projected_groups = ([project(group) for group in wheel_groups]
                        if wheel_groups is not None else [project(triangles)] * 4)
    minimum = math.inf
    for wheel in range(4):
        projected = projected_groups[wheel]
        inner, radial, outer = [normalize(points[8 + 6 * wheel + j]) for j in (0, 1, 3)]
        cx, cy, cz = [(inner[a] + outer[a]) / 2 for a in range(3)]
        radius = radial[1] - inner[1]
        half_width = abs(outer[0] - inner[0]) / 2
        across = [0] + [-half_width + .25 + i * .5
                        for i in range(round(half_width * 4))]
        for dx in across:
            for step in range(-round(radius * 4) + 1, round(radius * 4)):
                dz = step / 4
                x, z = cx + dx, cz + dz
                tyre_top = cy + math.sqrt(radius * radius - dz * dz)
                heights = []
                for a, b, c, ux, uz, vx, vz, determinant in projected:
                    px, pz = x - a[0], z - a[2]
                    u = (px * vz - pz * vx) / determinant
                    v = (ux * pz - uz * px) / determinant
                    if min(u, v, 1 - u - v) >= -1e-8:
                        heights.append(a[1] + u * (b[1] - a[1]) + v * (c[1] - a[1]))
                clearance = max(heights, default=-math.inf) - tyre_top
                if clearance < WHEEL_ARCH_MIN_CLEARANCE:
                    raise ValueError(f"Tyre protrudes through {name} wheel {wheel + 1} "
                                     f"at X={x:g}, Z={z:g}: clearance {clearance:.3f}")
                minimum = min(minimum, clearance)
    return round(minimum, 3)


def validate_wheel_arch_clearance(points, triangles, scale, shift, name):
    """Measure the encoded side panels against the swept tyre silhouette.

    Upper-cover rays do not detect a wheel arch whose vertices clear the tyre
    but whose connecting chords cut through it. Clip the actual side-facing
    triangles to a narrow band around the outer tyre face, then measure the
    exact closest point of their YZ projections. This also detects a triangle
    filling an intended opening after integer quantization. The front tyre's
    enclosing projected radius includes its full width under any steering
    angle; the rear tyres use their ordinary unsteered radius.
    """
    def normalize(point):
        return point[0] / scale, (point[1] - shift) / scale, point[2] / scale

    def clip_x(polygon, boundary, sense):
        result = []
        for a, b in zip(polygon, polygon[1:] + polygon[:1]):
            a_inside = (a[0] - boundary) * sense >= -GEOMETRY_TOLERANCE
            b_inside = (b[0] - boundary) * sense >= -GEOMETRY_TOLERANCE
            if a_inside:
                result.append(a)
            if a_inside != b_inside:
                fraction = (boundary - a[0]) / (b[0] - a[0])
                result.append(tuple(a[axis] + fraction * (b[axis] - a[axis])
                                    for axis in range(3)))
        return result

    def projected_distance(polygon, cy, cz):
        projected = [(point[1] - cy, point[2] - cz) for point in polygon]
        edges = list(zip(projected, projected[1:] + projected[:1]))
        crosses = [a[0] * b[1] - a[1] * b[0] for a, b in edges]
        if abs(sum(crosses)) > GEOMETRY_TOLERANCE and (
                min(crosses) >= -GEOMETRY_TOLERANCE or
                max(crosses) <= GEOMETRY_TOLERANCE):
            return 0.0
        minimum = math.inf
        for a, b in edges:
            dy, dz = b[0] - a[0], b[1] - a[1]
            length_squared = dy * dy + dz * dz
            fraction = (max(0.0, min(1.0, -(a[0] * dy + a[1] * dz) / length_squared))
                        if length_squared > GEOMETRY_TOLERANCE else 0.0)
            minimum = min(minimum, math.hypot(a[0] + fraction * dy,
                                            a[1] + fraction * dz))
        return minimum

    side_faces = []
    for triangle in triangles:
        a, b, c = map(normalize, triangle)
        u, v = [tuple(point[axis] - a[axis] for axis in range(3)) for point in (b, c)]
        normal = (u[1] * v[2] - u[2] * v[1],
                  u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        if (abs(normal[0]) > GEOMETRY_TOLERANCE and
                abs(normal[0]) >= max(abs(normal[1]), abs(normal[2]))):
            side_faces.append([a, b, c])

    clearances = []
    for wheel in range(len(model.WHEEL_CENTERS)):
        first = model.WHEEL_CONTROL_START + wheel * model.WHEEL_CONTROL_COUNT
        inner, radial, outer = [normalize(points[first + index]) for index in (0, 1, 3)]
        cy, cz = [(inner[axis] + outer[axis]) / 2 for axis in (1, 2)]
        radius = abs(radial[1] - inner[1])
        half_width = abs(outer[0] - inner[0]) / 2
        swept_radius = (math.hypot(radius, half_width)
                        if wheel < FRONT_STEERED_WHEEL_COUNT else radius)
        minimum = math.inf
        for triangle in side_faces:
            polygon = clip_x(triangle, outer[0] - WHEEL_ARCH_SIDE_BAND, 1)
            if polygon:
                polygon = clip_x(polygon, outer[0] + WHEEL_ARCH_SIDE_BAND, -1)
            if len(polygon) >= 3:
                minimum = min(minimum, projected_distance(polygon, cy, cz) - swept_radius)
        if not math.isfinite(minimum):
            raise ValueError(f"No encoded wheel-arch side panels in {name} wheel {wheel + 1}")
        if minimum + GEOMETRY_TOLERANCE < WHEEL_ARCH_MIN_CLEARANCE:
            raise ValueError(f"Wheel arch intersects the tyre clearance envelope in {name} "
                             f"wheel {wheel + 1}: clearance {minimum:.3f}, "
                             f"required {WHEEL_ARCH_MIN_CLEARANCE:g}")
        clearances.append(round(minimum, 3))
    return clearances


def dashboard_ram_usage(primary, secondary):
    """Classic-mode arena payloads, matching dashboard.c and sprite_make_wnd.

    STDA remains RLE-encoded after loading; STDB stays extracted. Instrument
    and gearbox panels share scratch storage when either contains the other.
    The sprite definitions/row tables live in an already allocated global
    table, rather than additional per-dashboard arena chunks.
    """
    bitmaps = resources(primary)
    instrument = struct.unpack_from("<HH", bitmaps["ins2"])
    gearbox = struct.unpack_from("<HH", bitmaps["gbox"])
    contains = lambda outer, inner: all(a >= b for a, b in zip(outer, inner))
    scratch = gearbox if contains(gearbox, instrument) else instrument
    panels = [scratch]
    if not contains(scratch, gearbox):
        panels.append(gearbox)
    # sprite_make_wnd includes the 16-byte SHAPE2D header, then reserves one
    # extra paragraph after truncating to paragraphs (even for exact multiples).
    scratch_bytes = sum(((width * height + 16) // 16 + 1) * 16
                        for width, height in panels)
    prepared_bytes = dashboard.prepared_stda_size(primary) + (len(secondary) + 15) // 16 * 16
    return {
        "extracted_resources_bytes": len(primary) + len(secondary),
        "prepared_resources_bytes": prepared_bytes,
        "scratch_bitmap_bytes": scratch_bytes,
        "scratch_dimensions": [list(panel) for panel in panels],
        "resident_arena_bytes": prepared_bytes + scratch_bytes,
    }


def validate_wheel_contacts(simd, points, scale, shift, label):
    """Enforce the original engine's centered-wheel asset contract."""
    contacts = [struct.unpack_from("<3h", simd, SIMD_WHEEL_COORDINATE_OFFSET +
                                   wheel * SIMD_WHEEL_COORDINATE_SIZE)
                for wheel in range(len(model.WHEEL_CENTERS))]
    if any(sum(point[axis] for point in contacts) != 0 for axis in (0, 2)):
        raise ValueError(f"Wheel coordinates must be centered on the car origin in {label}")
    if any(point[1] != 0 for point in contacts):
        raise ValueError(f"Ignored physical wheel Y coordinates must be zero in {label}")
    floor = shift + model.CONTACT_Y * scale
    for wheel, contact in enumerate(contacts):
        inner, radial, outer = [points[model.WHEEL_CONTROL_START +
                                      model.WHEEL_CONTROL_COUNT * wheel + index]
                                 for index in (0, 1, 3)]
        # SIMD Y is unused by native suspension. Horizontal contact locations
        # must match the animated wheel controls exactly at either model scale.
        for axis in (0, 2):
            if ((inner[axis] + outer[axis]) * (PHYSICS_POSITION_SCALE // 2) !=
                    contact[axis] * scale):
                raise ValueError(f"Physical and visual wheel centres disagree in {label}")
        radius = radial[1] - inner[1]
        if radius != model.WHEEL_RADIUS * scale:
            raise ValueError(f"Incorrect visual wheel radius in {label}")
        if inner[1] - radius != floor or outer[1] - radius != floor:
            raise ValueError(f"Wheel does not meet engine contact plane in {label}")
    return contacts


def validate_car_geometry(shape, name, simd):
    points, triangles, metadata = decode_model(shape, name)
    scale = model.SHOWROOM_SCALE if name == "car0" else 1
    shift = model.SHOWROOM_Y_OFFSET if name == "car0" else 0
    floor = shift + model.CONTACT_Y * scale
    contacts = validate_wheel_contacts(simd, points, scale, shift, name)
    half_width, _, half_length, collision_radius = struct.unpack_from(
        "<4h", simd, SIMD_COLLISION_BOUNDS_OFFSET)
    if any(abs(point[0]) > half_width * scale or abs(point[2]) > half_length * scale
           for point in points[model.WHEEL_CONTROL_START:]):
        raise ValueError(f"Collision bounds do not enclose the centered model in {name}")
    if collision_radius < math.ceil(math.hypot(half_width, half_length)):
        raise ValueError(f"Collision radius does not enclose the centered bounds in {name}")
    if min(p[1] for p in points[32:]) < floor:
        raise ValueError(f"Body below tyre contact plane in {name}")
    dimensions = {
        "length": (max(p[2] for p in points[8:]) - min(p[2] for p in points[8:])) / scale,
        "width": (max(p[0] for p in points[8:]) - min(p[0] for p in points[8:])) / scale,
        "height": (max(p[1] for p in points[8:]) - floor) / scale,
        "wheelbase": (contacts[0][2] - contacts[3][2]) / 64,
    }
    expected = {"length": model.LENGTH, "width": model.WIDTH, "height": model.BODY_HEIGHT,
                "wheelbase": model.WHEEL_CENTERS[0][2] - model.WHEEL_CENTERS[3][2]}
    for axis, value in expected.items():
        if abs(dimensions[axis] - value) > MODEL_DIMENSION_TOLERANCE:
            raise ValueError(f"Incorrect adapted model dimensions for {axis} in {name}: {dimensions}")
    metadata["dimensions_game_units"] = dimensions
    wheel_groups = wheel_cover_groups(shape, points)
    metadata["minimum_upper_tyre_clearance_game_units"] = validate_wheel_covers(
        points, triangles, scale, shift, name, wheel_groups=wheel_groups)
    metadata["minimum_side_arch_tyre_clearance_game_units"] = validate_wheel_arch_clearance(
        points, triangles, scale, shift, name)
    metadata["wheel_cover_triangles_attached_to_each_wheel"] = [len(group) for group in wheel_groups]
    return metadata


def validate_hypervision_geometry(shape, name, classic, simd):
    """Check detailed companions against the shared car and H3D contracts.

    The source mesh has its own topology and silhouette. The classic fender
    attachment groups and photo-derived body dimensions do not describe it.
    """
    points, triangles, metadata = decode_model(shape, name, hypervision=True)
    original, _, _ = decode_model(classic, name)
    if points[model.WHEEL_CONTROL_START:CAR_BODY_VERTEX_START] != original[
            model.WHEEL_CONTROL_START:CAR_BODY_VERTEX_START]:
        raise ValueError(f"HyperVision wheel controls differ from classic {name}")
    if not (model.NATIVE_LIMIT < metadata["vertices"] <= model.HYPERVISION_LIMIT and
            model.NATIVE_LIMIT < metadata["primitives"] <= model.HYPERVISION_LIMIT):
        raise ValueError(f"Expected a detailed mesh within H3D limits in {name}")
    scale = model.SHOWROOM_SCALE if name == "car0" else 1
    shift = model.SHOWROOM_Y_OFFSET if name == "car0" else 0
    validate_wheel_contacts(simd, points, scale, shift, name)
    floor = shift + model.CONTACT_Y * scale
    if min(point[1] for point in points[CAR_BODY_VERTEX_START:]) < floor:
        raise ValueError(f"HyperVision body below tyre contact plane in {name}")
    half_width, _, half_length, _ = struct.unpack_from(
        "<4h", simd, SIMD_COLLISION_BOUNDS_OFFSET)
    if any(abs(point[0]) > half_width * scale or abs(point[2]) > half_length * scale
           for point in points[model.WHEEL_CONTROL_START:]):
        raise ValueError(f"HyperVision body exceeds shared collision envelope in {name}")
    metadata["dimensions_game_units"] = {
        "length": (max(p[2] for p in points[8:]) - min(p[2] for p in points[8:])) / scale,
        "width": (max(p[0] for p in points[8:]) - min(p[0] for p in points[8:])) / scale,
        "height": (max(p[1] for p in points[8:]) - floor) / scale,
    }
    metadata["animated_wheels"] = len(model.WHEEL_CENTERS)
    metadata["minimum_side_arch_tyre_clearance_game_units"] = validate_wheel_arch_clearance(
        points, triangles, scale, shift, name)
    if name in detailed_model.RACING_VERTEX_LIMITS:
        lower, upper = detailed_model.RACING_VERTEX_LIMITS[name]
        if not lower <= metadata["vertices"] <= upper:
            raise ValueError(f"HyperVision {name} exceeds its racing vertex budget")
    # Imported car0 has open wheel-top vents. Its derived LODs share those
    # openings, so the classic continuous-roof ray test does not apply here.
    fraction_bits = struct.unpack_from("<H", shape, HYPERVISION_FRACTION_BITS_OFFSET)[0]
    metadata["coordinate_fraction_bits"] = fraction_bits
    metadata["coordinate_step_mm"] = model.MM_PER_UNIT / (scale * (1 << fraction_bits))
    return metadata


def validate(outputs, repository):
    car = resources(outputs["CARMCMU.RES"])
    simd = car["simd"]
    if len(simd) != 776 or simd[0] != 1:
        raise ValueError("Expected a 776-byte single-speed EV SIMD")
    if car.get("fand") != b"\x01" or car.get("adhs") != struct.pack("<BHHB", 1, 12, 8192, 15):
        raise ValueError("Expected enabled fan adhesion with explicit default settings")
    if car.get("ster") != bytes((physics.STEERING_VERSION, physics.STEERING_RATE_MULTIPLIER)):
        raise ValueError("Expected the authored McMurtry steering response rate")
    if struct.unpack_from("<H", simd, 0xAE)[0] != dashboard.WHITE | (dashboard.WHITE << 8):
        raise ValueError("Expected white dashboard instruments")
    for key in ("gnam", "gsna", "edes"):
        if not car[key].endswith(b"\0"):
            raise ValueError(f"Unterminated car text: {key}")
    shapes = resources(outputs["STMCMU.3SH"])
    if set(shapes) != {"car0", "car1", "car2", "exp0", "exp1", "exp2", "exp3"}:
        raise ValueError("Missing showroom, racing, LOD, or crash shape")
    metadata = {}
    for name, shape in shapes.items():
        if name.startswith("car"):
            metadata[name] = validate_car_geometry(shape, name, simd)
        else:
            _, _, metadata[name] = decode_model(shape, name)
    budgets = {}
    for part, required in (("A", set("dash roof dast dasm whl1 whl2 whl3 ins1 ins2 ins3 inm1 inm3 gbox imt1 imt3 revb spdc".split())),
                           ("B", {"dot ", "dota", "dot1", "dot2", "gnob", "gnab"} |
                            {f"dig{i}" for i in range(10)})):
        payload = outputs[f"STD{part}MCMU.VSH"]
        bitmaps = resources(payload)
        if not required <= bitmaps.keys():
            raise ValueError(f"Missing dashboard {part} resources")
        for name, bitmap in bitmaps.items():
            if len(bitmap) < 16:
                raise ValueError(f"Truncated dashboard bitmap {name}")
            width, height = struct.unpack_from("<HH", bitmap)
            if not width or not height or len(bitmap) != 16 + width * height or bitmap[14] & 0xF0:
                raise ValueError(f"Invalid dashboard bitmap {name}")
        if part == "A":
            roof = bitmaps["roof"]
            roof_width, roof_height, _, _, roof_x, roof_y = struct.unpack_from("<HHhhHH", roof)
            if (roof_width != dashboard.WIDTH or roof_x != 0 or roof_y != 0 or
                    not 0 < roof_height < dashboard.DASH_TOP or
                    dashboard.TRANSPARENT in roof[dashboard.SHAPE_HEADER_SIZE:]):
                raise ValueError("Canopy roof must be an opaque full-width top strip")
            top, mask = bitmaps["dast"], bitmaps["dasm"]
            if top[:16] != mask[:16]:
                raise ValueError("Canopy and fender artwork and mask are misaligned")
            width, height = struct.unpack_from("<HH", top)
            x, y = struct.unpack_from("<HH", top, 8)
            if (width != dashboard.WIDTH or x != 0 or y != roof_height or
                    y + height != dashboard.DASH_TOP):
                raise ValueError("Canopy overlay must join the roof strip to the dashboard edge")
            if set(mask[16:]) != {0, 255}:
                raise ValueError("Canopy and fender mask must contain opaque and clear areas")
            if any(pixel and clear == 255 for pixel, clear in zip(top[16:], mask[16:])):
                raise ValueError("Canopy and fender artwork would alter the clear road view")
        limit = len((repository / f"tools/scripts/cars/STD{part}DIA3.VSH").read_bytes())
        if len(payload) >= limit:
            raise ValueError(f"Dashboard {part} must use less extracted memory than DIA3")
        budgets[part] = {"MCMU": len(payload), "DIA3": limit}
    ram = {}
    for car_id, a, b in (("MCMU", outputs["STDAMCMU.VSH"], outputs["STDBMCMU.VSH"]),
                         ("DIA3", (repository / "tools/scripts/cars/STDADIA3.VSH").read_bytes(),
                          (repository / "tools/scripts/cars/STDBDIA3.VSH").read_bytes())):
        ram[car_id] = dashboard_ram_usage(a, b)
    for stage in ("extracted_resources_bytes", "prepared_resources_bytes", "resident_arena_bytes"):
        if ram["MCMU"][stage] >= ram["DIA3"][stage]:
            raise ValueError(f"Dashboard must use less RAM than DIA3: {stage}")
    return metadata, budgets, ram


def generate(repository):
    specs = physics.REAL_SPECIFICATIONS
    a, b, patches = dashboard.build_dashboards(repository)
    outputs = {"CARMCMU.RES": physics.build_car(repository, patches),
               "STMCMU.3SH": model.build_models(), "STDAMCMU.VSH": a, "STDBMCMU.VSH": b,
               "STMCMU.H3D": model.build_hypervision_models()}
    models, budgets, ram = validate(outputs, repository)
    enhanced = hypervision_resources(outputs["STMCMU.H3D"])
    if set(enhanced) != {"car0", "car1", "car2"}:
        raise ValueError("Missing HyperVision showroom or racing mesh")
    simd = resources(outputs["CARMCMU.RES"])["simd"]
    classic_models = resources(outputs["STMCMU.3SH"])
    hypervision_models = {name: validate_hypervision_geometry(
                             shape, name, classic_models[name], simd)
                          for name, shape in enhanced.items()}
    _, _, presentation_scale = detailed_model.container_header(outputs["STMCMU.H3D"])
    for metadata in hypervision_models.values():
        metadata["displayed_dimensions_game_units"] = {
            axis: value * presentation_scale
            for axis, value in metadata["dimensions_game_units"].items()}
        metadata["displayed_wheelbase_game_units"] = 2 * model.HALF_WHEELBASE * presentation_scale
    performance = [physics.simulate_performance(fps) for fps in (20, 10)]
    for result in performance:
        if (abs(result["zero_to_60_mph_with_one_foot_rollout_seconds"] -
                specs["zero_to_60_mph_seconds"]) > 1 / result["fps"] + .01 or
                abs(result["peak_speed_mph"] - specs["top_speed_mph"]) > 1):
            raise ValueError("Straight-line tuning no longer matches the car's performance targets")
    outputs["MCMU.obj"] = model.export_obj().encode("ascii")
    outputs["MCMU.mtl"] = model.export_mtl().encode("ascii")
    manifest = {
        "car_id": CAR_ID, "name": "McMurtry Spéirling PURE", "variant": specs["variant"],
        "model_year": specs["model_year"], "specifications_verified_date": specs["verified_date"],
        "game_name": "McMurtry Speirling PURE", "geometry": "Native low-polygon exterior rebuilt from the supplied high-resolution GLB, with curved painted front and rear panels and compact dark-grey insets, upright rear-facing recessed side gills, rounded enlarged wheel surrounds for tyre clearance, and the accepted canopy surface preserved without its thin centre fin.",
        "geometry_revision": "The car origin is centered between native axles at Z=+25 and Z=-25, matching the specified 2200 mm wheelbase. The source body is translated by 1.5 units before quantization, and the arches follow the adapted wheel controls. SIMD wheel Y fields are zero, while the visible tyres retain their correct contact height. Collision bounds enclose the recentered body. The side skin behind each front wheel is retriangulated around a narrow upright dark-grey gill, following the requested fish-gill simplification. Its centre pulls inward behind the outer vertical lip, turning the grey wall toward the rear and screening it from the front. Its upper and lower ends taper into the body-coloured skin. The surrounding painted panels follow every existing aft arch chord, preserving tyre clearance. The four white front lamp markers are omitted. Mirror stalks joined to existing crown vertices retain the native vertex budget; the mirror heads are preserved. The rear has painted rolled shoulders and a lower bumper framing a compact dark-grey inset with one game unit of depth. The complete existing roof-ring edge closes the upper fascia; the bumper joins the existing rising undertray. The exposed rear ramp and corner returns use the same darker grey as the underbody. Four small circular red rings form vertical pairs at the rear corners, with a nominal 10% size reduction and dark concentric centres following the latest rear-view photograph. Nested native round primitives retain brake brightening on the red rims. Shared crown controls, merged coplanar sill triangles and a tapered hidden floor retain the vertex budget. Curved painted upper and lower nose bands and rolled side cheeks replace the oversized black front mouth; only a narrow centre inset is dark grey. Shared rows and explicit outward triangles retain the curvature at native racing precision, with the white front lamp markers omitted. Rounded four-point transverse wheel-pod sections, bowed outer shoulders and enlarged circular wheel openings replace the slab fenders and tight arches. The front lip, shoulder and crown have a shallow, nearly level side profile toward the cockpit instead of a raised peak over the axle. Encoded side-panel chords clear a conservative front-steering envelope by at least one game unit; upper covers also maintain a one-unit tyre gap. The rear floor rises into the fascia with shared seams and no low panel projecting behind the body. The main floor stays inside the tyre paths and widens behind the rear openings. The thin roof/cowl centre fin is omitted; the canopy surface, main rear wing and wheel controls are preserved.",
        "downloaded_model": DOWNLOADED_MODEL,
        "historical_exterior_reference_images": REFERENCE_IMAGES,
        "historical_initial_reconstruction_reference_images": INITIAL_REFERENCE_IMAGES,
        "historical_user_supplied_exterior_detail_references": DETAIL_REFERENCE_IMAGES,
        "side_duct_detail_reference": {"file": "detail.png",
                                      "view": "User-highlighted side duct behind the front wheel.",
                                      "usage": "Simplified into a narrow vertical fish-like gill on both sides, opening toward the rear as subsequently requested; reference image is not redistributed."},
        "front_wheel_crown_openings": {
            "plan_source_xz_game_units": model.FRONT_CROWN_OPENING_PLAN,
            "shape": "Mirrored rectangles with parallel longitudinal sides and squared ends, projected onto the existing fender crowns.",
            "reference": "User-supplied overhead photograph, Pasted image (4).png; reference image is not redistributed.",
            "rendering": "Four-corner native dark surfaces at every detail level, retaining the existing wheel attachment and vertex budget."},
        "reference_image_usage": "Exterior photo URLs are historical records of the previous photo-based model. The supplied GLB is the ground truth for the rebuilt exterior. Manufacturer photographs are not redistributed in the car package.",
        "rear_lamps": {"count": 4, "layout": "Upper and lower circular red ring with a dark centre at each outer rear corner.",
                       "centres_game_units": [(x, y, z - model.BODY_ORIGIN_Z)
                                               for side in (-1, 1)
                                               for x, y, z in model.rear_lamp_centres(side)],
                       "nominal_outer_diameter_mm": (model.REAR_LAMP_DIAMETER *
                                                     model.REAR_LAMP_SIZE_SCALE * model.MM_PER_UNIT),
                       "size_scale": model.REAR_LAMP_SIZE_SCALE,
                       "inner_diameter_ratio": model.REAR_LAMP_INNER_RATIO,
                       "quantization_note": "Whole-unit racing coordinates can round outer diameters back to their previous size; dark centres still reduce the filled red area.",
                       "rendering": "Nested native round primitives: brake-light red rim and black centre. OBJ exports annular rear lenses without overlapping faces.",
                       "user_reference_images": ["mcmurtry-speirling-rear-view-900373069.jpg",
                                                 "mcmurtry-speirling.3_l-2885740449.jpg",
                                                 "McMurtry_Speirling_PURE_VP1_at_Castle_Combe_-_Rear_Side-1024x552-1480087278.jpg",
                                                 "mas-rapido-que-un-formula-1-con-1000-cv-y-menos-de-una-tonelada-por-fin-llegan-los-primeros-mcmurtry-speirling-pure-2026112494-1770713231_3-2452372269.jpg"],
                       "reference_usage": "Lamp placement and approximate size only; reference photographs are not redistributed."},
        "specifications": specs,
        "performance_note": "The 2026 production-car claim of 1.55 s to 60 mph includes one-foot rollout. Both standing-start and rollout-adjusted game timings are recorded separately; native tuning is an approximation of the production figures.",
        "game_straight_line_performance": performance,
        "performance_measurement": "Full-throttle level-ground integer drivetrain update with all four tyres grounded; checked against native statecar.c. This isolates gearing and torque from track and steering effects. The native redline brake produces a small limiter sawtooth.",
        "steering": {"resource": "ster", "version": physics.STEERING_VERSION,
                     "rate_multiplier": physics.STEERING_RATE_MULTIPLIER,
                     "note": "Threefold steering input and return-to-center rate in updated Restunts, retaining the speed-dependent response curve and maximum angle. The wheel and red dot follow the actual steering angle. Original Stunts ignores this resource. Replays require the same steering settings used when recording."},
        "sizing": {"millimetres_per_game_unit": model.MM_PER_UNIT, "showroom_scale": model.SHOWROOM_SCALE,
                   "showroom_y_offset": model.SHOWROOM_Y_OFFSET, "wheel_radius_game_units": model.WHEEL_RADIUS,
                   "wheel_centres_game_units": model.WHEEL_CENTERS,
                   "body_origin_source_z_game_units": model.BODY_ORIGIN_Z,
                   "physical_wheel_y": 0,
                   "adapted_wheelbase_mm": 2 * model.HALF_WHEELBASE * model.MM_PER_UNIT,
                   "reference_glb_bounds": model.SOURCE_BOUNDS,
                   "reference_glb_uniform_scale": model.SOURCE_SCALE,
                   "reference_model_dimensions_game_units": {"length": model.LENGTH,
                       "width": model.WIDTH, "height": model.HEIGHT},
                   "adapted_body_height_game_units": model.BODY_HEIGHT,
                   "reference": "Stock Corvette body 103 units / 4506 mm and NSX 100 units / 4405 mm; stock cars are stylized, so exact ratios to every stock car are impossible.",
                   "rounding": "The supplied mesh is uniformly scaled to the nominal car length, preserving its source width and height proportions. The source body is translated to the axle midpoint before quantization. The wheelbase is adapted to the specified 2200 mm using centered integer controls at Z=+25 and Z=-25; arches use those same centers. Ignored SIMD wheel Y fields are zero; visual tyre bottoms meet the engine contact plane. Racing coordinates round to whole game units; the showroom retains sub-unit precision at 20x. Native culling and collision bounds enclose the recentered geometry."},
        "wheel_arch_clearance": {"minimum_game_units": WHEEL_ARCH_MIN_CLEARANCE,
                                 "minimum_mm": WHEEL_ARCH_MIN_CLEARANCE * model.MM_PER_UNIT,
                                 "authored_arch_radius_game_units": model.ARCH_RADIUS,
                                 "tyre_radius_game_units": model.WHEEL_RADIUS,
                                 "validation": "Exact closest distance to encoded side-panel triangles clipped at the outer tyre face. Front wheels use the enclosing YZ radius hypot(tyre_radius, half_width) for any steering angle. Upper covers are independently ray-tested across the tread."},
        "paint_jobs": list(model.PAINT_NAMES),
        "adhesion": {"fand": 1, "resource": "adhs", "version": 1, "max_distance_world_units": 12,
                     "min_alignment_cosine_q14": 8192, "max_alignment_degrees": 60, "surface_mask": 15,
                     "surfaces": ["paved", "dirt", "ice", "grass"],
                     "note": "Restunts surface attachment is a game approximation of fan downforce. No water adhesion. Original Stunts ignores these extra resources."},
        "dashboard_note": "Original indexed artwork following the supplied left.png, middle.png and right.png cockpit views. Upright side control panels have smooth dark-grey metal faces, beveled edges and a slight inward curve beside the recessed central steering column. The upper carbon fascia and its air outlets sit flush with the raised side panels, forming a continuous surround ahead of the steering-column well. Each panel joins the outer corner of its rounded air scoop, which has a deep curved mouth and lower lip, and a mostly open throat with four subdued internal ribs. The left panel has a red circular control, a screw, a black potentiometer and two lower screws; the right substitutes a red release handle for the circular control. A centered white digital speedometer sits between a live white vertical RPM bar on the left and a matching battery bar fixed at 90% on the right. The wheel has substantial textured side grips, each joined to the central body by one broad connector, with no upper or lower connecting rim. Its upper bezel and ten unlit LEDs follow a gentle arch. McMurtry branding has a small C aligned with the top of the M, above five unlabelled knobs matching the side-panel potentiometers, each with a raised black cap, fluted sidewall and short silver index mark. Record-holder text and adjacent inscriptions are omitted. The display, grips, controls and deterministic material texture turn together. Optional spdc centers the visible speed digits; imt1/imt3 Q14 inverse transforms rotate the composed instrument bitmap. The optional revb descriptor fills the RPM bar from the bottom; original Stunts ignores these descriptors, keeps the display upright and draws a thin vertical white RPM indicator. A native steering dot shows the angle between wheel poses. The shallow dark-carbon roof edge is centered and symmetric, with no side pillars obstructing the forward view. Native roof and dast/dasm artwork preserve live scenery above the curved cowl and solid turquoise exterior fender crowns; wheel-top openings are not visible from the cockpit. Palette-reduced material shading and carbon weave retain the native DIA3 dashboard memory limits without redistributing photograph pixels.",
        "dashboard_reference": "https://www.topgear.com/car-reviews/speirling/first-drive",
        "dashboard_reference_images": dashboard.DASHBOARD_REFERENCES,
        "dashboard_decompressed_bytes": budgets,
        "dashboard_game_prepared_bytes": {car_id: usage["prepared_resources_bytes"]
                                          for car_id, usage in ram.items()},
        "dashboard_ram_bytes": ram,
        "dashboard_ram_note": "Classic 320x200 native arena payloads including 16-byte allocation rounding and shared instrument/gearbox scratch bitmaps. Sprite definitions and row tables use the common preallocated window table. Generation requires strictly less extracted, prepared and resident dashboard RAM than DIA3.",
        "models": models,
        "hypervision": {
            "resource": "STMCMU.H3D", "format_version": detailed_model.CONTAINER_VERSION,
            "maximum_vertices_per_shape": model.HYPERVISION_LIMIT,
            "maximum_primitives_per_shape": model.HYPERVISION_LIMIT,
            "selection": "Optional companion meshes are used only by HyperVision; classic rendering and original Stunts retain the unchanged STMCMU.3SH shapes.",
            "geometry": "Detailed exterior geometry imported from the supplied Sketchfab GLB, with transformed source bodywork, canopy, wing and underbody. Source wheel assemblies are replaced with the classic animated wheels. Source materials are mapped into the native palette and seven paint schemes; welded, quantized meshes provide separate showroom, near and distant racing detail levels.",
            "source_mesh": detailed_model.metadata(),
            "presentation_scale": detailed_model.presentation_metadata(),
            "models": hypervision_models},
        "obj_export": "Classic car0 showroom geometry with explicit tyres and annular rear lamps.",
        "sources": SOURCES,
        "files": {name: {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
                  for name, data in outputs.items()},
    }
    outputs["MCMU.json"] = (json.dumps(manifest, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    outputs["READMCMU.TXT"] = (f"""MCMURTRY SPEIRLING PURE ({specs['model_year']} PRODUCTION) - MCMU

INSTALL
Restunts loads this car automatically from its cars resource folder. The
repository keeps CARMCMU.RES, STMCMU.3SH, STDAMCMU.VSH and STDBMCMU.VSH in
assets/cars; --install GAME_DIRECTORY copies them to GAME_DIRECTORY/cars.
For original Stunts, copy these four files directly into your
Stunts game-data folder and select McMurtry Speirling PURE in the car menu.
The optional fifth file, STMCMU.H3D, supplies an imported showroom mesh and
car0-derived low-polygon racing meshes for HyperVision. The generator installs it
beside the classic files. Connected racing panels retain the source-guided
bodywork, canopy, wing and underbody with palette materials and animated wheels.
Classic rendering uses the unchanged STMCMU.3SH. Original Stunts does not need H3D.
Paints: Turquoise (default), Purple, Orange, Yellow, Blue, White, Light gray.
Single-speed electric drive. No stock car is replaced.

Fan adhesion is enabled by fand=1 and adhs=01 0C 00 00 20 0F. Updated
Restunts attaches tyre contacts to supported driving surfaces automatically,
including vertical/inverted surfaces, with a 12-unit gap and 60-degree
alignment limit on paved, dirt, ice and grass. No water adhesion. This is
a game approximation, not a physical aerodynamic simulation. The original
DOS game ignores these extra resources. Use identical car resources when
recording and playing back a replay.

Steering uses ster=01 03 to build and recenter the steering angle three times
faster in updated Restunts. The speed-dependent response and maximum angle
are retained; the wheel and red dot follow the actual steering angle.
Original Stunts ignores ster. Earlier McMurtry replays need their original
car resources to retain the recorded trajectory.

REAL CAR AND SCALE
The driving specification targets the {specs['model_year']} production PURE:
{specs['length_mm']} x {specs['width_mm']} x {specs['height_mm']} mm,
{specs['wheelbase_mm']} mm wheelbase, approximately {specs['weight_kg']} kg (subject to options),
{specs['peak_power_bhp']} bhp ({specs['peak_power_kw']} kW), {specs['top_speed_mph']} mph, {specs['battery_kwh']} kWh, twin-motor rear-wheel drive.
One fixed forward ratio is represented. The production car was unveiled
in July 2026 and publicly debuted at Monterey Car Week in August 2026.
The manufacturer's {specs['zero_to_60_mph_seconds']:.2f}-second 0-60 mph claim uses one-foot rollout.
Normal 20 Hz game tuning gives {performance[0]['zero_to_60_mph_seconds']:.2f} s from rest, approximately
{performance[0]['zero_to_60_mph_with_one_foot_rollout_seconds']:.2f} s after excluding the first foot of travel.
Mass, grip, gearing and torque bytes are Stunts tuning, not SI measurements.

Wikipedia was checked alongside the current McMurtry production sources.
Its obsolete under-1000/900 kg weight and rounded 1050 mm height conflict
with the factory's current approximately 1350 kg and 1056 mm values.
MCMU.json records the factory figures. The in-game description uses the
user's supplied display text.

Geometry uses 44 mm per racing-model unit, calibrated to the stock Corvette
and NSX. The supplied mesh is uniformly scaled to the nominal car length;
its width and height retain the source proportions. Both race LODs have the
same size; the showroom uses the stock 20x scale.
Wheel locations follow the supplied mesh with native integer quantization.
Wheels and physical contacts agree on the engine's six-unit contact plane.

MODEL SOURCES
The classic exterior is a native low-polygon adaptation guided by the supplied
2026-mcmurtry-speirling.zip GLB. Its embedded asset.extras metadata credits:
2026 McMurtry Speirling by Dave Love SketchFab
https://sketchfab.com/Tyler_Dave
{DOWNLOADED_MODEL['source_url']}
License: CC BY 4.0 - {DOWNLOADED_MODEL['license_url']}
Supplied GLB SHA-256:
{DOWNLOADED_MODEL['glb_sha256']}
The source hierarchy names exmods_mcmurtry_speirling22.fbx, suggesting an
earlier prototype despite its 2026 title. The supplied high-resolution mesh
is the exterior ground truth for the nose, canopy, main rear wing and wheels.
All three classic exterior topologies are rebuilt from scratch for native Stunts
primitives, uniform length scaling, animated wheels and seven paint schemes.
The HyperVision showroom imports the detailed exterior directly from that
GLB. A compressed transformed source mesh is retained in
tools/scripts/cars/mcmurtry_hypervision_source.json.gz for deterministic
regeneration without the upload or third-party Python packages. Racing meshes
are reduced directly from the current H3D car0, preserving palette seams,
open boundaries and the native wheel clearance. All models
use the game's four animated wheels, palette materials and seven paints. H3D does not store
textures or transparency. The original GLB, textures and manufacturer
photographs are not included.
MCMU.json records the supplied GLB hash, attribution, changes, photo URLs,
specifications, dimensions and resource hashes. The cockpit remains original
indexed-colour artwork based on the previously supplied references.
MCMU.obj and MCMU.mtl are editable Wavefront source exports in metres,
Y-up with tyre bottoms at ground level; keep these two files together.
The car origin is centered between axles at Z=+25 and Z=-25, giving the
specified 2200 mm wheelbase. Body vertices shift by 1.5 units from the source
origin before rounding; native wheel controls and arches stay aligned.
Physical wheel Y fields are zero. Visual tyres retain their eight-unit radius
and meet the engine contact plane. Bounds enclose the recentered body.
The nose has curved body-coloured upper and lower bands and rolled side
cheeks around a narrow dark-grey inset, replacing the oversized black mouth.
The four white front lamp markers are omitted.
A tall, narrow, recessed dark-grey gill sits behind each front wheel. The
highlighted side duct in detail.png is simplified into a vertical fish-like
gill, as requested, with tapered ends and body-coloured panels around it.
Its grey wall faces rearward, behind an outer lip that screens it from the front.
The old side skin is replaced so the recess remains visible at oblique angles.
Its surrounding panels follow every existing arch chord to retain tyre clearance.
Mirror heads are retained; stalks join existing crown vertices to save space.
The wheel pods have rounded transverse sections and bowed outer shoulders.
The front crown follows the shallow source profile toward the cockpit,
without a tall peak above the axle. A small lift retains the arch lip.
Enlarged circular arches leave a visible gap around the tyres; side panels
are checked against the encoded front-steering envelope and rear circles.
The minimum side and upper-cover gap is {WHEEL_ARCH_MIN_CLEARANCE:g} game unit
({WHEEL_ARCH_MIN_CLEARANCE * model.MM_PER_UNIT:g} mm), checked after integer quantization in every model.
The rear has painted rolled shoulders and a lower bumper around a compact
dark-grey inset, replacing the broad black equipment-bay cap. Its lower
corners are recessed {model.REAR_INSET_RECESS:g} game unit, giving the painted bands real depth.
Four small circular red rings form an upper and lower pair at each rear corner.
Their nominal outer diameter is reduced by 10%, with dark concentric centres
following mcmurtry-speirling-rear-view-900373069.jpg. Red rims brighten under
braking while the centres stay dark. Whole-unit racing coordinates can round
outer diameters back to their previous size. Shared crown controls, merged
coplanar sill triangles and a tapered hidden floor retain the vertex budget.
The editable OBJ represents the lamps as annular lenses with dark centres.
The rear undertray rises into the body fascia with shared floor and tail
seams, replacing the projecting low flap and central strake. Its exposed
ramp and corner returns use the same darker grey as the underbody. The main floor
stays inside the tyre paths and widens behind the rear openings.
The thin centre fin above the canopy and rear cowl is omitted.
The canopy surface, main rear wing and wheel controls are preserved.
Each wheel's cover triangles follow it with native primitive flag 2, so
the painter draws the fender after its wheel even at shallow angles.
The four bounded dark crown openings are native quadrilateral surfaces within
the fenders. The front pair has parallel long sides and squared ends, following
the supplied overhead photograph; the rear pair retains its source contour.
These classic geometry refinements remain in STMCMU.3SH and the OBJ export.
STMCMU.H3D retains the detailed imported showroom and derives both racing
meshes directly from that car0 payload. The near mesh has connected source-sampled
hood, wheel-vent recesses and bumper panels. Its complete body is built from
one half and mirrored, including all paint tables. The painted canopy sill
joins the body skin, and the rear shoulder, arch lips and bumper corners follow
source contours so internal dark surfaces stay behind the exterior panels.
Material-constrained edge
collapse retains the rear and distant mesh, preserving the canopy, wing and
palette boundaries while removing small separate fittings. The showroom scale
and height offset are removed before racing coordinates are quantized.
Wheel-arch chords are checked after encoding
and locally expanded only as needed to retain the native steering clearance.
The racing budgets are 1000-1200 vertices for car1 and 400-500 for car2,
including bounds and wheel controls. Deterministic cached geometry
keeps each mesh within H3D version {detailed_model.CONTAINER_VERSION}'s
65,535-vertex and 65,535-primitive limits:
car0: {hypervision_models['car0']['vertices']} vertices, {hypervision_models['car0']['primitives']} primitives;
car1: {hypervision_models['car1']['vertices']} vertices, {hypervision_models['car1']['primitives']} primitives;
car2: {hypervision_models['car2']['vertices']} vertices, {hypervision_models['car2']['primitives']} primitives.
All three H3D models display at {100 * detailed_model.PRESENTATION_SCALE:.3f}% of their authored size,
using one exact uniform scale ({detailed_model.PRESENTATION_SCALE_NUMERATOR}/{detailed_model.PRESENTATION_SCALE_DENOMINATOR}) for bodywork, bounds and animated wheels.
The scale matches Wikipedia's 1050/1344 mm height ratio against the stock
Audi Sport Quattro S1 E2: its showroom height is 26 game units, versus the
unscaled McMurtry's 27.3. The real wheelbases are 2200/2204 mm; the stock
Audi's proportions prevent matching both ratios without deforming the McMurtry.
The H3D v3 header carries the scale; all mesh payloads stay byte-identical.
Tyre ground planes remain Y=0 in the showroom and Y=-6 while driving.
Classic fallback models, physics contact points and collision sizes are unchanged.
Showroom body coordinates have {hypervision_models['car0']['coordinate_step_mm']:g} mm precision;
racing body coordinates have {hypervision_models['car1']['coordinate_step_mm']:g} mm precision.
Vertex counts include eight bounds and 24 animated wheel controls; primitive
counts include four native wheels. Bounds and wheel controls retain native
integer coordinates. Simplified panels omit tiny source features.
Ordinary generation uses Python's standard library and preserves
the compact source and its precomputed meshes. Racing cache provenance is tied
to the exact car0 payload checksum. Classic meshes retain their
original byte limits, wheel controls and driving specifications.
The cockpit artwork follows the supplied left.png, middle.png and right.png
views, with a centered, symmetric roof edge and no side pillars. Solid turquoise
fender crowns frame the live road view; wheel-top openings are not visible
from the cockpit. Upright side control panels have smooth dark-grey metal
faces, beveled edges and a slight inward curve beside the recessed central
steering column. The upper carbon fascia and its air outlets sit flush with
the raised side panels, forming a continuous surround ahead of the
steering-column well. Each panel joins the outer corner of its rounded air scoop.
The scoop has a deep curved mouth and lower lip, with four subdued ribs
inside its mostly open throat.
The left panel carries a red circular control, a screw, a black potentiometer
and two lower screws; the right uses the same vertical sequence with a red
release handle at the top.
The white digital speedometer sits in the middle of the wheel display,
between a live white vertical RPM bar on the left and a matching battery bar
fixed at 90% on the right. Substantial textured side grips each join the
central body through one broad connector, leaving the wheel open at the top
and bottom. Its upper bezel and ten unlit LEDs follow a gentle arch. McMurtry
branding has a small C aligned with the top of the M, above five unlabelled
knobs matching the side-panel potentiometers: raised black caps,
fluted sidewalls and short silver index marks. Record-holder text and the
adjacent inscriptions are omitted.
The display, grips and controls turn around the same hub. Optional spdc centers
the visible speed digits; imt1/imt3
Q14 inverse transforms rotate the composed instrument bitmap, and revb fills
the RPM bar upward from its base. Original Stunts ignores these descriptors,
keeps its display upright and shows RPM as a thin vertical white indicator.
A native steering dot shows the angle between wheel poses. Palette-reduced
shading and carbon weave follow the wheel as it turns. Native roof artwork
and dast/dasm masks preserve live scenery around the roof edge, curved cowl
and fender silhouettes. The original artwork stays within the native DIA3
dashboard memory limits; reference photograph pixels are not redistributed.

REBUILD
python3 tools/scripts/generate-mcmurtry-speirling.py --install stunts
python3 tools/scripts/generate-mcmurtry-speirling.py --check --install stunts

The generator validates all model, wheel, bitmap and adhesion resources
and requires dashboard memory use to stay strictly below DIA3:
Extracted A+B: {ram['MCMU']['extracted_resources_bytes']:,} vs {ram['DIA3']['extracted_resources_bytes']:,} bytes.
Prepared A+B: {ram['MCMU']['prepared_resources_bytes']:,} vs {ram['DIA3']['prepared_resources_bytes']:,} bytes.
With scratch: {ram['MCMU']['resident_arena_bytes']:,} vs {ram['DIA3']['resident_arena_bytes']:,} bytes.
These are classic-mode arena payloads; shared window tables are preallocated.
See docs/mcmurtry-speirling.md
for dimensional calibration, source details and validation.

SOURCES
""" + "\n".join(SOURCES) + "\n").replace("\n", "\r\n").encode("ascii")
    return outputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository-root", type=Path, default=ROOT)
    parser.add_argument("--output-directory", type=Path)
    parser.add_argument("--install", type=Path, metavar="GAME_DIRECTORY")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    outputs = generate(args.repository_root)
    game_outputs = {name: outputs[name] for name in GAME_FILES}
    if args.output_directory:
        targets = [(args.output_directory, outputs)]
    else:
        targets = [(args.repository_root / "assets/cars", game_outputs),
                   (args.repository_root / "tools/scripts/cars",
                    {name: data for name, data in outputs.items() if name not in GAME_FILES})]
    if args.install:
        targets.append((args.install / "cars", game_outputs))
    for directory, files in targets:
        if not args.check:
            directory.mkdir(parents=True, exist_ok=True)
        for name, data in files.items():
            path = directory / name
            if args.check:
                if not path.exists() or path.read_bytes() != data:
                    raise ValueError(f"Missing or stale generated file: {path}")
            else:
                path.write_bytes(data)
    print(f"{'Verified' if args.check else 'Generated'} McMurtry Speirling PURE (MCMU)")
    for name in GAME_FILES:
        print(f"{name}: {len(outputs[name]):,} bytes")


if __name__ == "__main__":
    main()
