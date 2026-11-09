"""Detailed McMurtry HyperVision meshes adapted from the supplied Sketchfab GLB.

Normal car generation uses Python's standard library, the compact showroom
source in cars/mcmurtry_hypervision_source.json.gz, and cached racing meshes
reduced directly from car0. Reimport the original archive with --source
to rebuild the showroom input. Native wheel primitives retain the game's
steering, suspension and tyre rendering.
"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import math
from pathlib import Path
import struct
import zipfile
from dataclasses import dataclass, field


SOURCE_PATH = Path(__file__).with_name("cars") / "mcmurtry_hypervision_source.json.gz"
SOURCE_SHA256 = "c32649493eaebec5005e7b8824bb429d77aa3fdbab277282e37b1b918b7f1013"
SOURCE_ARCHIVE_MEMBER = "source/2026_mcmurtry_speirling.glb"
SOURCE_TITLE = "2026 McMurtry Speirling"
SOURCE_AUTHOR = "Dave Love SketchFab"
SOURCE_URL = "https://sketchfab.com/3d-models/2026-mcmurtry-speirling-24a39a15bde049acbb3ede571cee0c8b"
SOURCE_LICENSE = "https://creativecommons.org/licenses/by/4.0/"
SOURCE_UNITS = 1000
SOURCE_FORMAT_VERSION = 2
CONTAINER_VERSION = 2
MAX_FRACTION_BITS = 8
RACING_FRACTION_BITS = 4
FRACTION_BITS = {"car0": 0, "car1": RACING_FRACTION_BITS, "car2": RACING_FRACTION_BITS}
QEM_TARGETS = {"car0": 32_000, "car1": 12_000}
SHOWROOM_CLUSTER = .05
RACING_VERTEX_TARGETS = {"car1": 1_200, "car2": 500}
RACING_VERTEX_LIMITS = {"car1": (1_000, 1_200), "car2": (400, 500)}
RACING_SOURCE_PATH = Path(__file__).with_name("cars") / "mcmurtry_racing_lods.json.gz"
RACING_CACHE_VERSION = 1
TRIANGLE_VERTICES = 3
AXES = 3
HOMOGENEOUS_AXES = 4
WHEEL_COUNT = 4
ARCH_INNER_X = 11.5
ARCH_OUTER_X = 13.0
ARCH_SOURCE_RADIUS = 8.5
ARCH_BLEND_RADIUS = 16.0
ARCH_TARGET_RADIUS = {"car0": 10.0, "car1": 10.0, "car2": 10.7}
# Retain exterior panels, glass, wing, diffuser, lights and mirror surfaces.
# The removed cockpit is behind opaque palette glass. Wheels/brakes/calipers
# are replaced by the shared native animated tyre primitives.
EXTERIOR_MESHES = frozenset((0, 3, 4, 7, 8, 9, 10, 11, 12, 14, 15, 16,
                            19, 20, 23, 24, 25, 26, 27,
                            28, 29, 30, 31, 32, 60, 61, 64, 65, 66, 68,
                            70, 72, 109))
PAINT, CARBON, GLASS, LAMP, RED, SILVER, BLACK, GREY = range(8)
MATERIAL_NAMES = ("paint", "carbon", "glass", "lamp", "red", "silver", "black", "grey")
IDENTITY = ((1., 0., 0., 0.), (0., 1., 0., 0.),
            (0., 0., 1., 0.), (0., 0., 0., 1.))
COMPONENTS = {5120: "b", 5121: "B", 5122: "h", 5123: "H", 5125: "I", 5126: "f"}
WIDTHS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def _multiply(a, b):
    return tuple(tuple(sum(a[row][k] * b[k][column] for k in range(4))
                       for column in range(4)) for row in range(4))


def _node_matrix(node):
    if "matrix" in node:
        flat = node["matrix"]
        return tuple(tuple(flat[column * 4 + row] for column in range(4))
                     for row in range(4))
    x, y, z, w = node.get("rotation", (0., 0., 0., 1.))
    sx, sy, sz = node.get("scale", (1., 1., 1.))
    tx, ty, tz = node.get("translation", (0., 0., 0.))
    return (((1 - 2 * (y*y + z*z)) * sx, 2 * (x*y - z*w) * sy, 2 * (x*z + y*w) * sz, tx),
            (2 * (x*y + z*w) * sx, (1 - 2 * (x*x + z*z)) * sy, 2 * (y*z - x*w) * sz, ty),
            (2 * (x*z - y*w) * sx, 2 * (y*z + x*w) * sy, (1 - 2 * (x*x + y*y)) * sz, tz),
            (0., 0., 0., 1.))


def _transform(matrix, point):
    p = (*point, 1.)
    return tuple(sum(row[k] * p[k] for k in range(4)) for row in matrix[:3])


def _determinant(matrix):
    a, b, c = (row[:3] for row in matrix[:3])
    return (a[0] * (b[1]*c[2] - b[2]*c[1]) -
            a[1] * (b[0]*c[2] - b[2]*c[0]) +
            a[2] * (b[0]*c[1] - b[1]*c[0]))


def _accessor(document, binary, index):
    accessor = document["accessors"][index]
    if "sparse" in accessor:
        raise ValueError("Sparse GLB accessors are not supported")
    view = document["bufferViews"][accessor["bufferView"]]
    if view.get("buffer", 0):
        raise ValueError("Expected the GLB embedded buffer")
    fmt = "<" + COMPONENTS[accessor["componentType"]] * WIDTHS[accessor["type"]]
    width = struct.calcsize(fmt)
    stride = view.get("byteStride", width)
    start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    if stride < width or start + (accessor["count"] - 1) * stride + width > len(binary):
        raise ValueError("GLB accessor lies outside its binary buffer")
    return [struct.unpack_from(fmt, binary, start + i * stride)
            for i in range(accessor["count"])]


def _read_glb(data):
    if len(data) < 20 or struct.unpack_from("<4sII", data) != (b"glTF", 2, len(data)):
        raise ValueError("Expected a version 2 binary glTF file")
    chunks = {}
    cursor = 12
    while cursor < len(data):
        size, kind = struct.unpack_from("<I4s", data, cursor)
        cursor += 8
        if size > len(data) - cursor or kind in chunks:
            raise ValueError("Invalid GLB chunk directory")
        chunks[kind] = data[cursor:cursor + size]
        cursor += size
    return json.loads(chunks[b"JSON"]), chunks[b"BIN\0"]


def _source_material(name):
    if name.startswith(("CSR2_CarPaint", "CSR2_OEM_McMurtry")):
        return PAINT
    if "Window" in name:
        return GLASS
    if name == "CSR2_Light":
        return LAMP
    if name.startswith("red_ext"):
        return RED
    if name == "mirrors" or name.startswith("chrome"):
        return SILVER
    if name.startswith("black_gloss") or "Grille" in name:
        return BLACK
    if name.startswith("CSR2_Base"):
        return GREY
    return CARBON


def _simplified_lods(source):
    """Offline QEM authoring; normal generation never imports these packages."""
    try:
        import numpy as np
        import fast_simplification as fs
    except ImportError as error:
        raise RuntimeError("Reimporting requires tools/scripts/requirements-mcmurtry-hypervision.txt") from error
    positions = np.asarray(source["vertices"], dtype=float) / SOURCE_UNITS
    faces = np.asarray(source["triangles"], dtype=np.int32)
    lods = {}
    for name, target_count in QEM_TARGETS.items():
        vertices, triangles, meshes = [], [], []
        start = 0
        for mesh in source["meshes"]:
            stop = start + mesh["triangles"]
            source_faces = faces[start:stop, :TRIANGLE_VERTICES]
            start = stop
            ids, inverse = np.unique(source_faces, return_inverse=True)
            points = positions[ids]
            local_faces = inverse.reshape((-1, TRIANGLE_VERTICES)).astype(np.int32)
            target = min(len(local_faces), max(4, round(len(local_faces) * target_count / len(faces))))
            if target < len(local_faces):
                points, local_faces = fs.simplify(points, local_faces, target_count=target,
                                                    agg=5, preserve_border=True)
            offset = len(vertices)
            vertices.extend(np.round(points * SOURCE_UNITS).astype(np.int64).tolist())
            surface = _source_material(mesh["material"])
            triangles.extend([[int(a) + offset, int(b) + offset, int(c) + offset, surface]
                              for a, b, c in local_faces])
            meshes.append({**mesh, "source_triangles": mesh["triangles"], "triangles": len(local_faces)})
        lods[name] = {"vertices": vertices, "triangles": triangles, "meshes": meshes,
                      "target_triangles": target_count}
    return lods


def import_source(path, output=SOURCE_PATH):
    """Extract and preserve transformed exterior topology from this source asset."""
    import mcmurtry_model as model
    path = Path(path)
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as archive:
            data = archive.read(SOURCE_ARCHIVE_MEMBER)
    else:
        data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != SOURCE_SHA256:
        raise ValueError("Source GLB differs from the attributed McMurtry reference")
    document, binary = _read_glb(data)
    vertices, triangles, meshes, lookup = [], [], [], {}
    source_triangles = sum(len(_accessor(document, binary, p["indices"])) // 3
                           for mesh in document["meshes"] for p in mesh["primitives"])
    ground = model.CONTACT_Y - model.SOURCE_BOUNDS[0][1] * model.SOURCE_SCALE

    def vertex(point):
        point = tuple(round((c * model.SOURCE_SCALE + (ground if axis == 1 else 0)) * SOURCE_UNITS)
                      for axis, c in enumerate(point))
        if point not in lookup:
            lookup[point] = len(vertices)
            vertices.append(point)
        return lookup[point]

    def visit(index, parent):
        node = document["nodes"][index]
        matrix = _multiply(parent, _node_matrix(node))
        if node.get("mesh") in EXTERIOR_MESHES:
            mesh_index = node["mesh"]
            for primitive in document["meshes"][mesh_index]["primitives"]:
                if primitive.get("mode", 4) != 4:
                    raise ValueError("Expected triangulated source geometry")
                material = document["materials"][primitive["material"]]["name"]
                surface = _source_material(material)
                points = _accessor(document, binary, primitive["attributes"]["POSITION"])
                indices = [vertex(_transform(matrix, p)) for p in points]
                faces = _accessor(document, binary, primitive["indices"])
                reflected = _determinant(matrix) < 0
                for first in range(0, len(faces), 3):
                    face = [indices[faces[first + j][0]] for j in range(3)]
                    if reflected:
                        face.reverse()
                    triangles.append([*face, surface])
                meshes.append({"mesh": mesh_index, "node": node.get("name", ""),
                               "material": material, "triangles": len(faces) // 3})
        for child in node.get("children", ()):
            visit(child, matrix)

    for root in document["scenes"][document.get("scene", 0)]["nodes"]:
        visit(root, IDENTITY)
    source = {"version": SOURCE_FORMAT_VERSION, "source_sha256": digest,
              "title": SOURCE_TITLE, "author": SOURCE_AUTHOR, "source_url": SOURCE_URL,
              "license": SOURCE_LICENSE, "source_triangles": source_triangles,
              "units_per_game_unit": SOURCE_UNITS, "coordinate_system": "X across, Y up, Z forward; source-frame axles",
              "materials": MATERIAL_NAMES, "meshes": meshes,
              "vertices": vertices, "triangles": triangles}
    source["simplification"] = {"algorithm": "quadric error metrics", "package": "fast-simplification==0.2.0",
                                "preserve_material_boundaries": True, "aggressiveness": 5,
                                "target_triangles": QEM_TARGETS}
    source["lods"] = _simplified_lods(source)
    packed = json.dumps(source, separators=(",", ":"), ensure_ascii=True).encode("ascii")
    Path(output).write_bytes(gzip.compress(packed, compresslevel=9, mtime=0))
    return source


def load_source(path=SOURCE_PATH):
    source = json.loads(gzip.decompress(Path(path).read_bytes()))
    if source["version"] != SOURCE_FORMAT_VERSION or source["source_sha256"] != SOURCE_SHA256:
        raise ValueError("Invalid compact McMurtry source provenance")
    if source["units_per_game_unit"] != SOURCE_UNITS or tuple(source["materials"]) != MATERIAL_NAMES:
        raise ValueError("Invalid compact McMurtry source coordinate or material schema")
    return source


def _cross(points):
    a, b, c = points
    u = tuple(y - x for x, y in zip(a, b))
    v = tuple(y - x for x, y in zip(a, c))
    return (u[1]*v[2] - u[2]*v[1], u[2]*v[0] - u[0]*v[2], u[0]*v[1] - u[1]*v[0])


def _palette(surface, points, model):
    normal = _cross(points)
    magnitude = math.sqrt(sum(c*c for c in normal))
    up = normal[1] / magnitude if magnitude else 0
    if surface == PAINT:
        return model.PAINT_TOP if up > .35 else model.PAINT_EDGE if up < -.35 else model.PAINT_SIDE
    if surface == LAMP:
        return model.BRAKE_LIGHT if sum(p[2] for p in points) < 0 else model.WHITE
    if surface == RED:
        return model.BRAKE_LIGHT
    if surface == SILVER:
        return model.SILVER
    if surface == GREY:
        return model.GREY
    if surface in (GLASS, BLACK):
        return model.BLACK
    return model.CARBON if up > -.35 else model.BLACK


def _adapt_arch(point, name, model):
    """Open the source fenders around the game's wider steering envelope.

    The source wheel assemblies are discarded. Locally expand their body
    openings for the existing native tyre controls; blend back to the exact
    source outside the immediate wheel neighborhood, preserving the floor.
    """
    x, y, z = point
    weight = min(1., max(0., (abs(x) - ARCH_INNER_X) / (ARCH_OUTER_X - ARCH_INNER_X)))
    if not weight:
        return point
    _, cy, cz = min(model.WHEEL_CENTERS, key=lambda c: abs(c[2] - z))
    dy, dz = y - cy, z - cz
    radius = math.hypot(dy, dz)
    if radius == 0 or radius >= ARCH_BLEND_RADIUS:
        return point
    blend = min(1., (ARCH_BLEND_RADIUS - radius) / (ARCH_BLEND_RADIUS - ARCH_SOURCE_RADIUS))
    expanded = radius + (ARCH_TARGET_RADIUS[name] - ARCH_SOURCE_RADIUS) * weight * blend
    adapted_y = cy + dy * expanded / radius
    adapted_z = cz + dz * expanded / radius
    if adapted_y < model.CONTACT_Y:
        adapted_y = model.CONTACT_Y
        adapted_z = cz + math.copysign(math.sqrt(max(0., expanded*expanded -
                                                    (adapted_y - cy)**2)), dz)
    return x, adapted_y, adapted_z


@dataclass
class _DetailedShape:
    """Author geometry in native units; serialize body-only fixed-point XYZ."""
    scale: int
    coordinate_fraction_bits: int
    vertices: list = field(default_factory=list)
    primitives: list = field(default_factory=list)
    _lookup: dict = field(default_factory=dict)

    def vertex(self, point):
        if point not in self._lookup:
            self._lookup[point] = len(self.vertices)
            self.vertices.append(point)
        return self._lookup[point]

    def encode_hypervision(self):
        import mcmurtry_model as model
        if len(self.vertices) > model.HYPERVISION_LIMIT or len(self.primitives) > model.HYPERVISION_LIMIT:
            raise ValueError("Detailed shape exceeds H3D limits")
        if not 0 <= self.coordinate_fraction_bits <= MAX_FRACTION_BITS:
            raise ValueError("Invalid H3D body coordinate precision")
        controls_end = model.WHEEL_CONTROL_START + WHEEL_COUNT * model.WHEEL_CONTROL_COUNT
        factor = 1 << self.coordinate_fraction_bits
        index_format = "H" if len(self.vertices) > model.NATIVE_LIMIT else "B"
        data = bytearray(struct.pack("<4H", len(self.vertices), len(self.primitives),
                                     model.PAINT_COUNT, self.coordinate_fraction_bits))
        for index, point in enumerate(self.vertices):
            multiplier = 1 if index < controls_end else factor
            data.extend(struct.pack("<3h", *(round(c * multiplier) for c in point)))
        data.extend(struct.pack("<I", model.ALL_DIRECTIONS_MASK) * len(self.primitives))
        data.extend(struct.pack("<I", 0) * len(self.primitives))
        for kind, flags, materials, indices in self.primitives:
            data.extend((kind, flags, *materials))
            data.extend(struct.pack(f"<{len(indices)}{index_format}", *indices))
        return bytes(data)


def _fit_bounds(shape, model):
    controls_end = model.WHEEL_CONTROL_START + WHEEL_COUNT * model.WHEEL_CONTROL_COUNT
    rendered = shape.vertices[controls_end:]
    bounds = shape.vertices[:model.WHEEL_CONTROL_START]
    minimum = tuple(min(p[a] for p in bounds) for a in range(AXES))
    lower = tuple(math.floor(min(p[a] for p in rendered)) for a in range(AXES))
    upper = tuple(math.ceil(max(p[a] for p in rendered)) for a in range(AXES))
    shape.vertices[:model.WHEEL_CONTROL_START] = [
        tuple(min(p[a], lower[a]) if p[a] == minimum[a] else max(p[a], upper[a])
              for a in range(AXES)) for p in bounds]


def _showroom_shape(source, model):
    name, cluster = "car0", SHOWROOM_CLUSTER
    scale = model.SHOWROOM_SCALE if name == "car0" else 1
    # Copy compatible controls and tyres, rather than importing static wheels.
    classic = model._showroom_car() if name == "car0" else model._car(simple=name == "car2")
    shape = _DetailedShape(scale, FRACTION_BITS[name])
    controls_end = model.WHEEL_CONTROL_START + WHEEL_COUNT * model.WHEEL_CONTROL_COUNT
    shape.vertices = list(classic.vertices[:controls_end])
    # Body vertices must never alias mutable wheel-control vertices.
    shape._lookup = {}
    shape.primitives = [p for p in classic.primitives if p[0] == model.WHEEL_PRIMITIVE]
    shift = model.SHOWROOM_Y_OFFSET if name == "car0" else 0
    authored = []
    for raw in source["vertices"]:
        x, y, z = (c / SOURCE_UNITS for c in raw)
        z -= model.BODY_ORIGIN_Z
        point = _adapt_arch((x, y, z), name, model)
        authored.append(point)
    points = []
    factor = 1 << FRACTION_BITS[name]
    for point in authored:
        point = tuple(round(c / cluster) * cluster for c in point)
        points.append((round(point[0] * scale * factor) / factor,
                       round(point[1] * scale * factor) / factor + shift,
                       round(point[2] * scale * factor) / factor))
    seen = set()
    for a, b, c, surface in source["triangles"]:
        triangle = (points[a], points[b], points[c])
        quantized_normal = _cross(triangle)
        original = (authored[a], authored[b], authored[c])
        original_normal = _cross(original)
        if sum(x * y for x, y in zip(quantized_normal, original_normal)) <= 0:
            continue
        # Material seams share the same spatial grid. Duplicate coincident
        # faces are removed independent of their source winding/material.
        key = tuple(sorted(triangle))
        if key in seen:
            continue
        seen.add(key)
        material = _palette(surface, original, model)
        materials = (material,) * model.PAINT_COUNT if isinstance(material, int) else material
        indices = tuple(shape.vertex(p) for p in triangle)
        shape.primitives.append((TRIANGLE_VERTICES, model.TWO_SIDED_FLAG, materials, indices))
    _fit_bounds(shape, model)
    if len(shape.vertices) > model.HYPERVISION_LIMIT or len(shape.primitives) > model.HYPERVISION_LIMIT:
        raise ValueError(f"Detailed {name} exceeds the H3D word limits")
    return shape


def build_shapes(model=None):
    if model is None:
        import mcmurtry_model as model
    source = load_source()
    shapes = {"car0": _showroom_shape({**source, **source["lods"]["car0"]}, model)}
    cached = json.loads(gzip.decompress(RACING_SOURCE_PATH.read_bytes()))
    if (cached["version"] != RACING_CACHE_VERSION or
            set(cached["lods"]) != set(RACING_VERTEX_TARGETS)):
        raise ValueError("Invalid cached racing LOD schema")
    if cached["coordinate_fraction_bits"] != {name: FRACTION_BITS[name] for name in RACING_VERTEX_TARGETS}:
        raise ValueError("Racing LODs must be rebuilt at the current coordinate precision")
    digest = hashlib.sha256(shapes["car0"].encode_hypervision()).hexdigest()
    if cached["source_car0_sha256"] != digest:
        raise ValueError("Racing LODs must be rebuilt from the current car0")
    controls_end = model.WHEEL_CONTROL_START + WHEEL_COUNT * model.WHEEL_CONTROL_COUNT
    for name in RACING_VERTEX_TARGETS:
        shape = _DetailedShape(1, FRACTION_BITS[name])
        classic = model._car(simple=name == "car2")
        shape.vertices = list(classic.vertices[:controls_end])
        shape.primitives = [p for p in classic.primitives if p[0] == model.WHEEL_PRIMITIVE]
        factor = 1 << FRACTION_BITS[name]
        shape.vertices.extend(tuple(c / factor for c in point)
                              for point in cached["lods"][name]["vertices"])
        for indices, materials in cached["lods"][name]["triangles"]:
            shape.primitives.append((TRIANGLE_VERTICES, model.TWO_SIDED_FLAG,
                                     tuple(materials), tuple(i + controls_end for i in indices)))
        _fit_bounds(shape, model)
        lower, upper = RACING_VERTEX_LIMITS[name]
        if not lower <= len(shape.vertices) <= upper:
            raise ValueError(f"{name} exceeds its racing vertex budget")
        shapes[name] = shape
    return shapes


def build_container(model=None):
    if model is None:
        import mcmurtry_model as model
    shapes = build_shapes(model)
    resources = [(name, shape.encode_hypervision()) for name, shape in shapes.items()]
    offset = model.HYPERVISION_CONTAINER_HEADER_SIZE + len(resources) * model.HYPERVISION_CONTAINER_ENTRY_SIZE
    directory, payload = bytearray(), bytearray()
    for name, data in resources:
        directory.extend(struct.pack("<4sII", name.encode("ascii"), offset, len(data)))
        payload.extend(data)
        offset += len(data)
    return (struct.pack("<4sHH", model.HYPERVISION_CONTAINER_MAGIC,
                        CONTAINER_VERSION, len(resources)) + directory + payload)


def metadata():
    import mcmurtry_model as model
    source = load_source()
    cached = json.loads(gzip.decompress(RACING_SOURCE_PATH.read_bytes()))
    return {"source_sha256": source["source_sha256"], "source_triangles": source["source_triangles"],
            "retained_source_triangles": len(source["triangles"]), "retained_meshes": len(source["meshes"]),
            "compact_source": "tools/scripts/cars/mcmurtry_hypervision_source.json.gz",
            "author": source["author"], "source_url": source["source_url"], "license": source["license"],
            "container_version": CONTAINER_VERSION, "coordinate_fraction_bits": FRACTION_BITS,
            "simplification": source["simplification"],
            "lod_source_triangles": {"car0": len(source["lods"]["car0"]["triangles"])},
            "cluster_spacing_game_units": {"car0": SHOWROOM_CLUSTER},
            "cluster_spacing_mm": {"car0": SHOWROOM_CLUSTER * model.MM_PER_UNIT},
            "racing_vertex_targets_including_controls": RACING_VERTEX_TARGETS,
            "racing_vertex_limits_including_controls": RACING_VERTEX_LIMITS,
            "racing_authoring_source": "STMCMU.H3D/car0",
            "racing_source_car0_sha256": cached["source_car0_sha256"],
            "racing_compact_source": "tools/scripts/cars/mcmurtry_racing_lods.json.gz",
            "racing_surface_construction": cached["simplification"],
            "showroom_arch_target_radius_game_units": ARCH_TARGET_RADIUS["car0"],
            "showroom_arch_blend_radius_game_units": ARCH_BLEND_RADIUS,
            "adaptation": "Showroom retains imported source topology, boundary-preserving QEM and local fender-opening expansion. Both racing LODs derive from that exact showroom payload in racing coordinates, with source palette materials and seven paints. The near mesh has connected source-sampled front panels, joined canopy and rear shoulder seams, source-profile rear fenders and exact mirrored body geometry. Native animated wheels and integer controls are preserved; original textures, hidden cockpit and alpha decals are omitted."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, help="original supplied ZIP or GLB to import")
    parser.add_argument("--output", type=Path, default=SOURCE_PATH, help="compact source output")
    args = parser.parse_args()
    if args.source:
        source = import_source(args.source, args.output)
        print(f"Imported {len(source['triangles']):,} exterior triangles into {args.output}")
    for name, shape in build_shapes().items():
        print(f"{name}: {len(shape.vertices):,} vertices, {len(shape.primitives):,} primitives")


if __name__ == "__main__":
    main()
