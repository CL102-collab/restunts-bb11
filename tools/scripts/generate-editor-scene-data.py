#!/usr/bin/env python3
"""Keep the offline WebGL preview catalog identical to the native game metadata."""
import argparse
import ast
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / 'src/restunts/c'
OUTPUT = ROOT / 'src/restunts/platform/wasm/track-editor-scene-data.js'
OBJECT_COUNT = 215
TERRAIN_COUNT = 19
MATERIAL_COUNT = 129
SHAPE_NAME_STRIDE = 5
ROTATION_UNIT = 256
ROTATIONS = {'ANGLE_QUARTER_TURN': 256, 'ANGLE_HALF_TURN': 512,
             'ANGLE_THREE_QUARTER_TURN': 768}


def array_body(source, name):
    match = re.search(r'\b' + re.escape(name) + r'(?:\[[^\]]+\])+\s*=\s*\{', source)
    if not match:
        raise ValueError('Cannot find array ' + name)
    begin, depth = match.end(), 1
    for end in range(begin, len(source)):
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
            if not depth:
                return source[begin:end]
    raise ValueError('Unterminated array ' + name)


def integer(value):
    return ROTATIONS[value] if value in ROTATIONS else int(value)


def objects(source, name, count, names):
    rows = re.findall(r'\{([^{}]*)\}', array_body(source, name))
    if len(rows) != count:
        raise ValueError('Unexpected object count in ' + name)
    result = []
    for row in rows:
        fields = [field.strip() for field in row.split(',')]
        if len(fields) != 10:
            raise ValueError('Unexpected object record in ' + name)
        shape_names = []
        for field in fields[2:4]:
            shape = re.fullmatch(r'(?:TRACK_SHAPE\((\d+)\)|&game3dshapes\[(\d+)\])', field)
            if not shape and field != '0':
                raise ValueError('Unexpected shape pointer: ' + field)
            index = int(shape[1] or shape[2]) if shape else None
            # Car/explosion slots beyond the native game name table are not track art.
            shape_names.append(names[index] if index is not None and index < len(names) else None)
        flags = integer(fields[7])
        result.append(dict(shape=shape_names[0], lowShape=shape_names[1],
                           rotation=integer(fields[1]) // ROTATION_UNIT,
                           overlay=integer(fields[4]), surface=integer(fields[5]),
                           ignoreZBias=integer(fields[6]), multiTile=flags,
                           width=2 if flags & 2 else 1, height=2 if flags & 1 else 1,
                           physical=integer(fields[8])))
    return result


def numeric_array(source, name, count):
    body = array_body(source, name)
    if '#else' in body:
        body = body.split('#else', 1)[1].split('#endif', 1)[0]
    body = re.sub(r'/\*.*?\*/|//[^\n]*', '', body, flags=re.S)
    values = [int(value.strip()) for value in body.split(',') if value.strip()]
    if len(values) != count:
        raise ValueError('Unexpected table length in ' + name)
    return values


def generate():
    strings = (CORE / 'data/full_strings.c').read_text()
    full_data = (CORE / 'data/full_data.c').read_text()
    track_data = (CORE / 'headless/headless_trackdata.c').read_text()
    trkutil = (CORE / 'trkutil.c').read_text()
    frame_header = (CORE / 'extended/extended_frame_internal.h').read_text()
    raw_names = ast.literal_eval('[' + array_body(strings, 'game_shape_names') + ']')
    if len(raw_names) % SHAPE_NAME_STRIDE:
        raise ValueError('Unexpected shape-name table length')
    names = []
    for index in range(0, len(raw_names), SHAPE_NAME_STRIDE):
        if raw_names[index + SHAPE_NAME_STRIDE - 1] != 0:
            raise ValueError('Missing shape-name terminator')
        names.append(''.join(raw_names[index:index + SHAPE_NAME_STRIDE - 1]))
    colors = numeric_array(full_data, 'material_color_list', MATERIAL_COUNT)
    patterns = numeric_array(full_data, 'material_pattern_list', MATERIAL_COUNT)
    masks = numeric_array(full_data, 'material_pattern2_list', MATERIAL_COUNT)
    pieces = [[int(value.strip()) for value in row.split(',')]
              for row in re.findall(r'\{([^{}]*)\}', array_body(trkutil, 'hillroad_pieces'))]
    shapes = [[int(value.strip()) for value in row.split(',')]
              for row in re.findall(r'\{([^{}]*)\}', array_body(trkutil, 'hillroad_shapes'))]
    if len(pieces) != 4 or len(shapes) != 4 or any(len(row) != 6 for row in pieces) \
            or any(len(row) != 4 for row in shapes):
        raise ValueError('Unexpected hill-road substitution table dimensions')
    ground_materials = []
    for suffix in ('GROUND_GRASS', 'ICE', 'DIRT', 'WATER', 'HILL_GRASS'):
        match = re.search(r'\bFRAME_EXTENDED_' + suffix + r'_MATERIAL\s*=\s*(\d+)', frame_header)
        if not match:
            raise ValueError('Missing terrain material ' + suffix)
        ground_materials.append(int(match[1]))
    tables = dict(objects=objects(track_data, 'trkObjectList', OBJECT_COUNT, names),
                  terrain=objects(full_data, 'terrain_scene_objects', TERRAIN_COUNT, names),
                  materials=[dict(color=color, pattern=pattern, pattern2=mask)
                             for color, pattern, mask in zip(colors, patterns, masks)],
                  shapeNames=names,
                  hillRoads=[dict(pieces=pieces, shapes=shapes)
                             for pieces, shapes in zip(pieces, shapes)],
                  groundMaterials=ground_materials)
    def rows(values):
        return ',\n'.join('        ' + json.dumps(row, separators=(',', ':')) for row in values)
    return '''/* Generated by tools/scripts/generate-editor-scene-data.py from native C
 * catalogs. Resource names and material metadata only; no game artwork is included. */
(function (root, factory) {
    const api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.TrackEditorSceneData = api;
}(typeof globalThis !== 'undefined' ? globalThis : this, function () {
    'use strict';
    return {
''' + ',\n'.join('    ' + key + ': [\n' + rows(value) + '\n    ]'
                   for key, value in tables.items()) + '''
    };
}));
'''


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='Reject out-of-date generated metadata')
    args = parser.parse_args()
    content = generate()
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text() != content:
            parser.error('Editor scene data is out of date; run this script without --check')
        print('Editor scene data matches the native game metadata.')
    else:
        OUTPUT.write_text(content)
