#!/usr/bin/env python3
"""Reduce the current H3D car0 into cached near and distant racing geometry.

Offline authoring requires NumPy and SciPy. Normal car regeneration reads the resulting
cache using only the standard library. The source showroom payload is unchanged.
"""

import argparse
import copy
import gzip
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import struct

import mcmurtry_hypervision as detailed
import mcmurtry_model as model


ROOT = Path(__file__).resolve().parents[2]
GENERATOR_SPEC = importlib.util.spec_from_file_location(
    "mcmurtry_generator", Path(__file__).with_name("generate-mcmurtry-speirling.py"))
generator = importlib.util.module_from_spec(GENERATOR_SPEC)
GENERATOR_SPEC.loader.exec_module(generator)
CONTROLS_END = model.WHEEL_CONTROL_START + detailed.WHEEL_COUNT * model.WHEEL_CONTROL_COUNT
MINIMUM_COMPONENT_AREA = {"car1": 10.0, "car2": 20.0}
MAXIMUM_ARCH_EXPANSION = 2.0
NEAR_REAR_BASE_VERTICES = 1_000


def source_geometry(path):
    """Decode the showroom shell before its procedural lamps are attached.

    The installed car0 includes final lamps that must not consume the QEM
    body budget. Validate it against current authoring, then use the unchanged
    source shell; normal generation attaches the lamps after simplification.
    """
    payload = generator.hypervision_resources(Path(path).read_bytes())["car0"]
    source = detailed.load_source()
    base = detailed._showroom_shape({**source, **source["lods"]["car0"]}, model)
    final = copy.deepcopy(base)
    detailed.lamps.apply(final, model, showroom=True)
    detailed._fit_bounds(final, model)
    if payload not in (base.encode_hypervision(), final.encode_hypervision()):
        raise ValueError("Source showroom does not match current authoring inputs")
    payload = base.encode_hypervision()
    points, _, _ = generator.decode_model(payload, "car0", hypervision=True)
    vertices, primitives, paints, header, index_size = generator.model_layout(
        payload, "car0", hypervision=True)
    cursor = header + vertices * struct.calcsize("<3h") + primitives * 2 * struct.calcsize("<I")
    triangles, materials = [], []
    for _ in range(primitives):
        kind, flags = payload[cursor:cursor + 2]
        count = kind if kind <= 10 else {model.SPHERE_PRIMITIVE: 2, model.WHEEL_PRIMITIVE: 6}[kind]
        palette = tuple(payload[cursor + 2:cursor + 2 + paints])
        indices = struct.unpack_from(f"<{count}{'H' if index_size == 2 else 'B'}",
                                     payload, cursor + 2 + paints)
        if kind != model.WHEEL_PRIMITIVE:
            if kind != detailed.TRIANGLE_VERTICES:
                raise ValueError("Expected triangulated car0 body geometry")
            triangles.append(tuple(i - CONTROLS_END for i in indices))
            materials.append(palette)
        cursor += 2 + paints + count * index_size
    normalized = [(x / model.SHOWROOM_SCALE,
                   (y - model.SHOWROOM_Y_OFFSET) / model.SHOWROOM_SCALE,
                   z / model.SHOWROOM_SCALE) for x, y, z in points[CONTROLS_END:]]
    return normalized, triangles, materials, hashlib.sha256(payload).hexdigest()


def expand_arch(point, margin):
    """Compensate for low-poly arch chords within the existing fender blend."""
    x, y, z = point
    weight = min(1.0, max(0.0, (abs(x) - detailed.ARCH_INNER_X) /
                         (detailed.ARCH_OUTER_X - detailed.ARCH_INNER_X)))
    _, cy, cz = min(model.WHEEL_CENTERS, key=lambda center: abs(center[2] - z))
    dy, dz = y - cy, z - cz
    radius = math.hypot(dy, dz)
    if not radius or not weight or radius >= detailed.ARCH_BLEND_RADIUS:
        return tuple(point)
    blend = min(1.0, (detailed.ARCH_BLEND_RADIUS - radius) /
                (detailed.ARCH_BLEND_RADIUS - detailed.ARCH_TARGET_RADIUS["car0"]))
    expanded = radius + margin * weight * blend
    y = cy + dy * expanded / radius
    z = cz + dz * expanded / radius
    if y < model.CONTACT_Y:
        y = model.CONTACT_Y
        z = cz + math.copysign(math.sqrt(max(0.0, expanded * expanded - (y - cy)**2)), dz)
    return x, y, z


def encoded_shape(points, triangles, materials, name, margin):
    """Quantize once, drop collapsed/coincident faces and compact body indices."""
    shape = detailed._DetailedShape(1, detailed.FRACTION_BITS[name])
    classic = model._car(simple=name == "car2")
    shape.vertices = list(classic.vertices[:CONTROLS_END])
    shape.primitives = [p for p in classic.primitives if p[0] == model.WHEEL_PRIMITIVE]
    factor = 1 << shape.coordinate_fraction_bits
    quantized = [tuple(round(c * factor) / factor for c in expand_arch(p, margin)) for p in points]
    seen = set()
    for face, palette in zip(triangles, materials):
        triangle = tuple(quantized[i] for i in face)
        normal = detailed._cross(triangle)
        original_normal = detailed._cross([points[i] for i in face])
        key = tuple(sorted(triangle))
        if sum(a * b for a, b in zip(normal, original_normal)) <= 0 or key in seen:
            continue
        seen.add(key)
        shape.primitives.append((detailed.TRIANGLE_VERTICES, model.TWO_SIDED_FLAG,
                                 tuple(int(c) for c in palette),
                                 tuple(shape.vertex(p) for p in triangle)))
    detailed._fit_bounds(shape, model)
    return shape


def finish_shape(points, triangles, materials, name):
    step = 1 / (1 << detailed.FRACTION_BITS[name])
    for iteration in range(round(MAXIMUM_ARCH_EXPANSION / step) + 1):
        margin = iteration * step
        shape = encoded_shape(points, triangles, materials, name, margin)
        decoded, faces, _ = generator.decode_model(shape.encode_hypervision(), name, hypervision=True)
        try:
            generator.validate_wheel_arch_clearance(decoded, faces, 1, 0, name)
        except ValueError:
            continue
        lower, upper = detailed.RACING_VERTEX_LIMITS[name]
        if not lower <= len(shape.vertices) <= upper:
            raise ValueError(f"Quantized {name} has {len(shape.vertices)} vertices; expected {lower}-{upper}")
        return shape, margin
    raise ValueError(f"Unable to preserve native tyre clearance for {name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "assets/cars/STMCMU.H3D")
    parser.add_argument("--output", type=Path, default=detailed.RACING_SOURCE_PATH)
    parser.add_argument("--lod", choices=tuple(detailed.RACING_VERTEX_TARGETS),
                        help="rebuild one LOD and retain the other cached mesh")
    args = parser.parse_args()
    from mcmurtry_lod_simplify import simplify
    points, triangles, materials, digest = source_geometry(args.source)
    cache = {"version": detailed.RACING_CACHE_VERSION, "source_car0_sha256": digest,
             "coordinate_fraction_bits": {name: detailed.FRACTION_BITS[name]
                                          for name in detailed.RACING_VERTEX_TARGETS},
             "simplification": {
                 "algorithm": {"car1": "connected source-sampled front panels, repaired rear profiles and mirrored body",
                               "car2": "material-constrained quadric edge collapse"},
                 "generator": "tools/scripts/generate-mcmurtry-lods.py",
                 "wing_blades": "closed convex envelopes of source blade components",
                 "protected_features": "source wing, glass extrema and wheel-arch contours",
                 "near_body_symmetry": "positive X half reflected across shared centreline vertices",
                 "near_front_panels": "shared hood, wheel-vent recess, wheel-well and bumper edges",
                 "near_rear_panels": "joined canopy sill, source shoulder contour, fender lips and bumper corners",
                 "near_rear_base_vertices": NEAR_REAR_BASE_VERTICES,
                 "minimum_component_area_game_units_squared": MINIMUM_COMPONENT_AREA,
                 "arch_expansion_game_units": {}}, "lods": {}}
    if args.lod:
        previous = json.loads(gzip.decompress(args.output.read_bytes()))
        if (previous["source_car0_sha256"] != digest or
                previous["coordinate_fraction_bits"] != cache["coordinate_fraction_bits"]):
            raise ValueError("Cannot retain LODs from a different source or coordinate precision")
        cache["lods"] = previous["lods"]
        cache["simplification"]["arch_expansion_game_units"] = previous["simplification"][
            "arch_expansion_game_units"]
    for name, target in detailed.RACING_VERTEX_TARGETS.items():
        if args.lod and name != args.lod:
            continue
        if name == "car1":
            from mcmurtry_canopy_seam import repair_canopy_seam
            from mcmurtry_front_mesh import rebuild_front
            from mcmurtry_lod_retopology import extract_rear_half
            from mcmurtry_rear_fenders import repair_fenders
            from mcmurtry_rear_shoulder import repair_shoulder
            base = simplify(points, triangles, materials, NEAR_REAR_BASE_VERTICES,
                            MINIMUM_COMPONENT_AREA[name])
            rear = extract_rear_half(*base)
            rear_geometry, front_seam = rear[:3], rear[3]
            for repair in (repair_canopy_seam, repair_shoulder, repair_fenders):
                rear_geometry = repair(points, triangles, materials, *rear_geometry)
            reduced = rebuild_front(points, triangles, materials, *rear_geometry, front_seam)
        else:
            reduced = simplify(points, triangles, materials, target - CONTROLS_END,
                               MINIMUM_COMPONENT_AREA[name])
        shape, margin = finish_shape(*reduced, name)
        factor = 1 << detailed.FRACTION_BITS[name]
        cache["lods"][name] = {
            "vertices": [[round(c * factor) for c in p] for p in shape.vertices[CONTROLS_END:]],
            "triangles": [[[i - CONTROLS_END for i in indices], list(palette)]
                          for kind, _, palette, indices in shape.primitives
                          if kind != model.WHEEL_PRIMITIVE]}
        cache["simplification"]["arch_expansion_game_units"][name] = margin
        print(f"{name}: {len(shape.vertices)} vertices, {len(shape.primitives)} primitives; "
              f"arch expansion {margin:g} units", flush=True)
    packed = json.dumps(cache, separators=(",", ":"), sort_keys=True).encode("ascii")
    args.output.write_bytes(gzip.compress(packed, compresslevel=9, mtime=0))


if __name__ == "__main__":
    main()
