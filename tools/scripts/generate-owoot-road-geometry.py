#!/usr/bin/env python3
"""Extract OWOOT road triangles from the original GAME1/2.P3S resources.

Run from any directory; optional --game-directory and --output override the
repository defaults. Only road paint materials (19, 22, 23) are extracted.
Paint variants share vertices, so these also cover dirt and ice. Kerbs (127,
128), railings, buildings, lane markings, and highway medians are excluded.
The output is checked in so DOS and headless builds do not load render shapes.
"""

import argparse
import hashlib
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BYTE_BITS = 8
BYTE_MAX = (1 << BYTE_BITS) - 1
WORD_BYTES = struct.calcsize("<H")
PACKED_SIZE_OFFSET = 1
PACKED_SIZE_BYTES = 3
PACKED_HEADER_SIZE = PACKED_SIZE_OFFSET + PACKED_SIZE_BYTES
PACKED_MULTI_PASS_FLAG = 0x80
PACKED_PASS_COUNT_MASK = 0x7F
COMPRESSION_RLE = 1
COMPRESSION_VLE = 2
VLE_FLAGS_OFFSET = PACKED_HEADER_SIZE
VLE_HEADER_SIZE = VLE_FLAGS_OFFSET + 1
VLE_DEPTH_MASK = 0x7F
VLE_DELTA_FLAG = 0x80
RLE_SOURCE_SIZE_OFFSET = PACKED_HEADER_SIZE
RLE_FLAGS_OFFSET = 8
RLE_HEADER_SIZE = RLE_FLAGS_OFFSET + 1
RLE_ESCAPE_COUNT_MASK = 0x7F
RLE_NO_SEQUENCE_FLAG = 0x80
RLE_SEQUENCE_ESCAPE_INDEX = 1
RLE_BYTE_COUNT_KIND = 1
RLE_WORD_COUNT_KIND = 3
RESOURCE_COUNT_OFFSET = struct.calcsize("<I")
RESOURCE_HEADER_SIZE = struct.calcsize("<IH")
RESOURCE_ID_SIZE = 4
RESOURCE_OFFSET_SIZE = struct.calcsize("<I")
SHAPE_HEADER_SIZE = 4
SHAPE_COUNT_FIELDS = 3
SHAPE_VERTEX_FORMAT = "<hhh"
SHAPE_VERTEX_SIZE = struct.calcsize(SHAPE_VERTEX_FORMAT)
SHAPE_PRIMITIVE_MASK_BYTES = struct.calcsize("<II")
SHAPE_PRIMITIVE_HEADER_SIZE = 2
SHAPE_POLYGON_MIN_VERTICES = 3
SHAPE_POLYGON_MAX_VERTICES = 10
SHAPE_PRIMITIVE_SPHERE = 11
SHAPE_SPHERE_VERTEX_COUNT = 2
SHAPE_WHEEL_VERTEX_COUNT = 6
MODEL_COUNT = 37
OVERPASS_MODEL = 22

# Scene overlays are included with their base model. Finish/slalom have a road
# overlay; the overpass also has the perpendicular lower road. Model 1's hill
# variants use separate model 36 so their sloping surface heights are retained.
MODEL_SHAPES = {
    0: ("road",), 1: ("road",), 2: ("turn",), 3: ("stur",),
    4: ("chi1",), 5: ("chi2",), 6: ("offl",), 7: ("offr",),
    8: ("sofl",), 9: ("sofr",), 10: ("gwro",), 11: ("wroa",),
    12: ("inte",), 16: ("ramp",), 17: ("sram",), 18: ("elrd",),
    19: ("elsp",), 20: ("selr",), 21: ("sest",), OVERPASS_MODEL: ("elsp",),
    23: ("lban",), 24: ("rban",), 25: ("bank",), 26: ("btur",),
    27: ("loop", "loo1"), 28: ("tun2",), 29: ("spip",),
    30: ("pipe", "pip2"), 31: ("hpip", "pip2"),
    32: ("lco0", "lco1"), 33: ("rco0", "rco1"),
    34: ("road",), 35: ("vcor",), 36: ("rdup",),
}
ROAD_MATERIALS = {19, 22, 23}


def vle(data):
    size = int.from_bytes(data[PACKED_SIZE_OFFSET:PACKED_HEADER_SIZE], "little")
    depth = data[VLE_FLAGS_OFFSET] & VLE_DEPTH_MASK
    counts = data[VLE_HEADER_SIZE:VLE_HEADER_SIZE + depth]
    alphabet = iter(data[VLE_HEADER_SIZE + depth:VLE_HEADER_SIZE + depth + sum(counts)])
    codes = {}
    code = 0
    for width, count in enumerate(counts, 1):
        for _ in range(count):
            codes[width, code] = next(alphabet)
            code += 1
        code *= 2
    stream = data[VLE_HEADER_SIZE + depth + sum(counts):]
    output = bytearray()
    code = width = previous = 0
    for byte in stream:
        for bit in range(BYTE_BITS - 1, -1, -1):
            code = code * 2 + ((byte >> bit) & 1)
            width += 1
            if (width, code) not in codes:
                continue
            value = codes[width, code]
            if data[VLE_FLAGS_OFFSET] & VLE_DELTA_FLAG:
                value = (value + previous) & BYTE_MAX
            output.append(value)
            previous = value
            code = width = 0
            if len(output) == size:
                return bytes(output)
    raise ValueError("truncated VLE resource")


def rle(data):
    size = int.from_bytes(data[PACKED_SIZE_OFFSET:PACKED_HEADER_SIZE], "little")
    source_size = int.from_bytes(
        data[RLE_SOURCE_SIZE_OFFSET:RLE_SOURCE_SIZE_OFFSET + PACKED_SIZE_BYTES], "little")
    escape_count = data[RLE_FLAGS_OFFSET] & RLE_ESCAPE_COUNT_MASK
    escapes = data[RLE_HEADER_SIZE:RLE_HEADER_SIZE + escape_count]
    source = data[RLE_HEADER_SIZE + escape_count:]
    if data[RLE_FLAGS_OFFSET] <= RLE_NO_SEQUENCE_FLAG:
        sequence = bytearray()
        cursor = 0
        while cursor < source_size:
            value = source[cursor]
            cursor += 1
            if value == escapes[RLE_SEQUENCE_ESCAPE_INDEX]:
                end = source.index(value, cursor)
                sequence.extend(source[cursor:end] * source[end + 1])
                cursor = end + 2
            else:
                sequence.append(value)
        source = sequence
    lookup = {value: index + 1 for index, value in enumerate(escapes)}
    output = bytearray()
    cursor = 0
    while len(output) < size:
        value = source[cursor]
        cursor += 1
        kind = lookup.get(value, 0)
        if kind:
            if kind == RLE_BYTE_COUNT_KIND:
                count = source[cursor]
                cursor += 1
            elif kind == RLE_WORD_COUNT_KIND:
                count = int.from_bytes(source[cursor:cursor + WORD_BYTES], "little")
                cursor += WORD_BYTES
            else:
                count = kind - 1
            value = source[cursor]
            cursor += 1
            output.extend(bytes((value,)) * count)
        else:
            output.append(value)
    return bytes(output[:size])


def unpack(data):
    passes = data[0] & PACKED_PASS_COUNT_MASK if data[0] & PACKED_MULTI_PASS_FLAG else 1
    if data[0] & PACKED_MULTI_PASS_FLAG:
        data = data[PACKED_HEADER_SIZE:]
    for _ in range(passes):
        if data[0] == COMPRESSION_RLE:
            data = rle(data)
        elif data[0] == COMPRESSION_VLE:
            data = vle(data)
        else:
            raise ValueError("unsupported resource compression")
    return data


def shapes(data):
    count = struct.unpack_from("<H", data, RESOURCE_COUNT_OFFSET)[0]
    base = RESOURCE_HEADER_SIZE + count * (RESOURCE_ID_SIZE + RESOURCE_OFFSET_SIZE)
    result = {}
    for index in range(count):
        name_start = RESOURCE_HEADER_SIZE + RESOURCE_ID_SIZE * index
        name = data[name_start:name_start + RESOURCE_ID_SIZE].decode("ascii")
        offset_start = RESOURCE_HEADER_SIZE + RESOURCE_ID_SIZE * count + RESOURCE_OFFSET_SIZE * index
        start = base + struct.unpack_from("<I", data, offset_start)[0]
        vertex_count, primitive_count, paint_count = data[start:start + SHAPE_COUNT_FIELDS]
        vertices = [struct.unpack_from(
            SHAPE_VERTEX_FORMAT, data, start + SHAPE_HEADER_SIZE + SHAPE_VERTEX_SIZE * i)
            for i in range(vertex_count)]
        cursor = (start + SHAPE_HEADER_SIZE + SHAPE_VERTEX_SIZE * vertex_count
                  + SHAPE_PRIMITIVE_MASK_BYTES * primitive_count)
        polygons = []
        for _ in range(primitive_count):
            kind = data[cursor]
            material = data[cursor + SHAPE_PRIMITIVE_HEADER_SIZE]
            length = (kind if kind <= SHAPE_POLYGON_MAX_VERTICES else
                      SHAPE_SPHERE_VERTEX_COUNT if kind == SHAPE_PRIMITIVE_SPHERE else
                      SHAPE_WHEEL_VERTEX_COUNT)
            indices_start = cursor + SHAPE_PRIMITIVE_HEADER_SIZE + paint_count
            indices = data[indices_start:indices_start + length]
            if (SHAPE_POLYGON_MIN_VERTICES <= kind <= SHAPE_POLYGON_MAX_VERTICES
                    and material in ROAD_MATERIALS):
                polygons.append([vertices[i] for i in indices])
            cursor += SHAPE_PRIMITIVE_HEADER_SIZE + paint_count + length
        result[name] = polygons
    return result


def cross(a, b, c):
    return (b[0] - a[0]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[0] - a[0])


def triangles(polygon):
    # Triangulate concave split-road polygons without adding their grassy bays.
    points = polygon[:]
    area = sum(a[0] * b[2] - a[2] * b[0]
               for a, b in zip(points, points[1:] + points[:1]))
    if area == 0:
        return [(points[0], points[i], points[i + 1]) for i in range(1, len(points) - 1)]
    sign = 1 if area > 0 else -1
    result = []
    while len(points) > 3:
        for i in range(len(points)):
            a, b, c = points[i - 1], points[i], points[(i + 1) % len(points)]
            if cross(a, b, c) * sign <= 0:
                continue
            others = [v for j, v in enumerate(points)
                      if j not in ((i - 1) % len(points), i, (i + 1) % len(points))]
            if any(all(cross(x, y, v) * sign >= 0
                       for x, y in ((a, b), (b, c), (c, a))) for v in others):
                continue
            result.append((a, b, c))
            del points[i]
            break
        else:
            raise ValueError(f"cannot triangulate road polygon: {polygon}")
    return result + [tuple(points)]


def generate(directory):
    bank = {}
    hashes = []
    for name in ("GAME1.P3S", "GAME2.P3S"):
        data = (directory / name).read_bytes()
        hashes.append(f" * {name} SHA256 {hashlib.sha256(data).hexdigest()}")
        bank.update(shapes(unpack(data)))
    all_triangles = []
    model_ranges = []
    for model in range(MODEL_COUNT):
        start = len(all_triangles)
        polygons = [p for name in MODEL_SHAPES.get(model, ()) for p in bank[name]]
        if model == OVERPASS_MODEL:
            polygons += [[(-z, y, x) for x, y, z in p] for p in bank["road"]]
        for polygon in polygons:
            all_triangles.extend(triangles(polygon))
        model_ranges.append((start, len(all_triangles) - start))
    lines = ["/* Generated by tools/scripts/generate-owoot-road-geometry.py.",
             " * Road surface triangles retain resource coordinates and heights.",
             *hashes, " * Do not edit the numeric table by hand. */", "",
             "static const struct OWOOT_ROAD_TRIANGLE far owoot_road_triangles[] = {"]
    for model, (start, count) in enumerate(model_ranges):
        if count:
            lines.append(f"\t/* Physical model {model}: {', '.join(MODEL_SHAPES[model])}. */")
        for triangle in all_triangles[start:start + count]:
            vertices = ", ".join("{" + ", ".join(map(str, vertex)) + "}" for vertex in triangle)
            lines.append("\t{{" + vertices + "}},")
    lines += ["};", "", "static const struct OWOOT_ROAD_MODEL owoot_road_models[] = {"]
    for start, count in model_ranges:
        lines.append(f"\t{{{start}, {count}}},")
    lines += ["};", ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-directory", type=Path, default=ROOT / "stunts")
    parser.add_argument("--output", type=Path, default=ROOT / "src/restunts/c/owoot/owoot_road_data.h")
    parser.add_argument("--clang-format", default="clang-format")
    args = parser.parse_args()
    args.output.write_bytes(generate(args.game_directory).replace("\n", "\r\n").encode())
    subprocess.run([args.clang_format, f"--style=file:{ROOT / '.clang-format'}",
                    "-i", str(args.output)], check=True, cwd=ROOT)


if __name__ == "__main__":
    main()
