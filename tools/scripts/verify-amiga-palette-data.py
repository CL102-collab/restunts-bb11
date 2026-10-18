#!/usr/bin/env python3
"""Verify numeric Amiga palette facts and their optional original-file provenance.

No commercial assets are required for the normal check. To verify against a
local extraction as well, pass --original-directory DIR, containing disk1/,
disk2/, and decoded/disk1/, decoded/disk2/ (decoded PPkc resources). An optional
--dos-directory verifies the canonical VGA palette against SDMAIN.PVS.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'src/restunts/tests/fixtures/amiga-palette-reference.json'
HEADER = ROOT / 'src/restunts/platform/amiga/palette_data.h'


def check(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def integers(text):
    return [int(word, 0) for word in re.findall(r'0x[0-9a-fA-F]+|\b[0-9]+\b', text)]


def dos_pixels(data, offset):
    width, height = struct.unpack_from('<HH', data, offset)
    raw = data[offset + 16:offset + 16 + width * height]
    check(len(raw) == width * height, 'Truncated DOS bitmap')
    layout = 0 if data[offset + 15] & 0xf0 else data[offset + 14] >> 4
    check(layout <= 3, 'Unknown DOS bitmap layout')
    if not layout:
        return width, height, raw
    pixels = bytearray(raw)
    even, odd = (height + 1) // 2, height // 2
    for y in range(height):
        for x in range(width):
            if layout == 1:
                source = x * height + y
            elif layout == 2:
                source = x * height + y // 2 + (even if y & 1 else 0)
            elif y & 1:
                source = width * even + x * odd + y // 2
            else:
                source = x * even + y // 2
            pixels[y * width + x] = raw[source]
    return width, height, pixels


def original_pixels(data, offset, compared_width, dashboard):
    rowbytes, height = struct.unpack_from('>HH', data, offset)
    # F0 replaces all four color planes; partial-plane FB sprites depend on
    # the existing destination and cannot provide independent color pairs.
    expected_mask = 0xf0 if dashboard else 0xe0
    check(data[offset + 12:offset + 14] == bytes((expected_mask, 0)),
          'Unverified or partial-plane bitmap used for color mapping')
    check(compared_width <= rowbytes * 8, 'Original bitmap narrower than compared image')
    pixels = bytearray(compared_width * height)
    for plane, weight in enumerate(data[offset + 14:offset + 20]):
        if not weight:
            break
        for y in range(height):
            for x in range(compared_width):
                source = offset + 20 + (plane * height + y) * rowbytes + x // 8
                if data[source] & (128 >> (x & 7)):
                    pixels[y * compared_width + x] |= weight
    return pixels


def verify_pixel_pairs(facts, original_directory, dos_directory, decoder):
    dos_cache, original_cache = {}, {}
    for mapping in facts['menu_maps']:
        dashboard = mapping.get('palette_entries_used') == 16
        sources = {source['asset']: source for source in mapping['sources']} if dashboard else None
        pairs = defaultdict(Counter)
        for chunk in mapping['paired_chunks']:
            source = sources[chunk['asset']] if dashboard else mapping
            dos_path = dos_directory / source['dos_source']
            if dos_path not in dos_cache:
                compressed = dos_path.read_bytes()
                check(digest(compressed) == source['dos_source_sha256'], 'DOS paired source differs')
                dos_cache[dos_path] = decoder.unpack(compressed)
            dos = dos_cache[dos_path]
            check(digest(dos) == source['dos_decoded_sha256'], 'DOS paired decoding differs')
            original_path = original_directory / 'decoded' / source['amiga_source']
            if original_path not in original_cache:
                original_cache[original_path] = original_path.read_bytes()
            original = original_cache[original_path]
            check(digest(original) == source['amiga_decoded_sha256'], 'Amiga paired decoding differs')
            width, height, dp = dos_pixels(dos, chunk['dos_decoded_header_offset'])
            check((width, height) == (chunk['width'], chunk['height']), 'Paired dimensions differ')
            ap = original_pixels(original, chunk['amiga_decoded_header_offset'], width, dashboard)
            check(len(ap) == len(dp), 'Paired bitmap sizes differ')
            original_hash = chunk['amiga_compared_pixels_sha256'] if dashboard else chunk['amiga_pixels_sha256']
            check(digest(dp) == chunk['dos_pixels_sha256'], 'Decoded DOS pixel hash differs')
            check(digest(ap) == original_hash, 'Decoded Amiga pixel hash differs')
            for source_index, target_index in zip(dp, ap):
                pairs[source_index][target_index] += 1
        expected = {int(source): {int(target): count for target, count in counts.items()}
                    for source, counts in mapping['pairs'].items()}
        check(dict(pairs) == expected, f'Observed pixel pairs differ: {mapping["resource"]}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original-directory', type=Path)
    parser.add_argument('--dos-directory', type=Path)
    args = parser.parse_args()
    facts = json.loads(FIXTURE.read_text())
    header = HEADER.read_text()
    palettes = re.findall(r'\{\s*"([A-Z0-9]+)",\s*"(![a-z0-9]+)",\s*\{(.*?)\}\s*\}', header, re.S)
    actual = {(resource, chunk): integers(body) for resource, chunk, body in palettes}
    check(len(actual) == len(facts['palettes']) == 31, 'Expected 31 distinct palette records')
    for palette in facts['palettes']:
        key = palette['resource'], palette['chunk']
        words = [int(word, 16) for word in palette['ecs_words']]
        check(len(words) == 32 and max(words) <= 0xfff, f'Invalid ECS palette: {key}')
        check(actual[key] == words, f'Header palette differs: {key}')
        packed = struct.pack('>32H', *words)
        check(digest(packed) == palette['palette_sha256'], f'Palette hash differs: {key}')
        if args.original_directory:
            original = (args.original_directory / palette['source']).read_bytes()
            decoded = (args.original_directory / palette['decoded_file']).read_bytes()
            check(digest(original) == palette['source_sha256'], f'Original source differs: {key}')
            check(digest(decoded) == palette['decoded_sha256'], f'Decoded source differs: {key}')
            offset = palette['decoded_palette_data_offset']
            check(decoded[offset:offset + 64] == packed, f'Original palette differs: {key}')
    canonical = facts['dos_reference_palette']
    block = re.search(r'amiga_dos_reference_palette\[256\]\[3\] = \{(.*?)\};', header, re.S)
    rgb = bytes(integers(block.group(1)))
    check(len(rgb) == 768 and max(rgb) <= 63, 'Invalid VGA DAC reference palette')
    check(list(rgb) == canonical['rgb6_bytes'], 'Header VGA reference differs')
    check(digest(rgb) == canonical['palette_sha256'], 'VGA reference hash differs')
    maps_block = header.split('amiga_original_color_maps[] = {', 1)[1].split('\n};', 1)[0]
    maps = {name: integers(body) for name, body in re.findall(r'\{\s*"([A-Z0-9]+)",\s*\{(.*?)\}\s*\}', maps_block, re.S)}
    for mapping in facts['menu_maps']:
        values = maps[mapping['resource']]
        check(values == mapping['indexes'], f'Header map differs: {mapping["resource"]}')
        check(len(values) == 256 and max(values) < 32, 'Invalid original index map')
        pairs = {int(source): {int(target): count for target, count in counts.items()}
                 for source, counts in mapping['pairs'].items()}
        majority = {source: min(counts, key=lambda target: (-counts[target], target))
                    for source, counts in pairs.items()}
        check(all(values[source] == target for source, target in majority.items()),
              f'Map disagrees with observed majority: {mapping["resource"]}')
        check(sum(sum(counts.values()) for counts in pairs.values()) == mapping['pixels'],
              'Paired pixel count differs')
        check(sum(pairs[source][target] for source, target in majority.items()) ==
              mapping['dominant_mapping_agreement'], 'Paired agreement count differs')
    source = (ROOT / 'src/restunts/c/full_data.c').read_text()
    material_block = source.split('material_color_list[SHAPE3D_MATERIAL_COUNT] = {', 1)[1]
    material_block = material_block.split('#ifdef RESTUNTS_AMIGA', 1)[1].split('#else', 1)[0]
    material_block = re.sub(r'/\*.*?\*/', '', material_block, flags=re.S)
    material = facts['material_colors']
    check(integers(material_block) == material['original_material_colors'], 'Original material indexes differ')
    check(len(material['original_material_colors']) == 129, 'Expected 129 original material colors')
    if args.original_directory:
        original = (args.original_directory / facts['original_executable']['source']).read_bytes()
        check(digest(original) == facts['original_executable']['sha256'], 'Original executable differs')
        offset = material['material_color_file_offset']
        check(list(struct.unpack_from('>129H', original, offset)) == material['original_material_colors'],
              'Original executable material table differs')
    if args.dos_directory:
        spec = importlib.util.spec_from_file_location('dos_unpack', ROOT / 'tools/scripts/generate-owoot-road-geometry.py')
        decoder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(decoder)
        original = (args.dos_directory / canonical['source']).read_bytes()
        decoded = decoder.unpack(original)
        check(digest(original) == canonical['source_sha256'], 'DOS SDMAIN source differs')
        check(digest(decoded) == canonical['decoded_sha256'], 'DOS SDMAIN decoding differs')
        offset = canonical['decoded_palette_data_offset']
        check(decoded[offset:offset + 768] == rgb, 'Original VGA palette differs')
        if args.original_directory:
            verify_pixel_pairs(facts, args.original_directory, args.dos_directory, decoder)
    print(f'Verified {len(actual)} ECS palettes, {len(maps)} index maps, 768 VGA bytes, and 129 original material colors.')


if __name__ == '__main__':
    main()
