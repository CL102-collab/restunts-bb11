/* Exercise the WebGL scene compiler without a GPU or browser dependency. */
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const model = require('../../src/restunts/platform/wasm/track-editor-model.js');
const resources = require('../../src/restunts/platform/wasm/track-editor-resources.js');
const scene = require('../../src/restunts/platform/wasm/track-editor-scene.js');
const data = require('../../src/restunts/platform/wasm/track-editor-scene-data.js');
const ROOT = path.resolve(__dirname, '../..');
const TILE = 1024, LEVEL = 450, EPSILON = 1e-6;
let checks = 0;
function test(name, body) { body(); checks++; console.log('PASS ' + name); }
function near(actual, expected, message) { assert(Math.abs(actual - expected) <= EPSILON, (message || '') + ': ' + actual + ' != ' + expected); }
function primitiveShape(vertices, indices, material = 19) {
    const bytes = new Uint8Array(4 + vertices.length * 6 + 8 + 3 + indices.length);
    bytes[0] = vertices.length; bytes[1] = 1; bytes[2] = 1;
    const view = new DataView(bytes.buffer);
    vertices.forEach((point, index) => point.forEach((value, axis) => view.setInt16(4 + index * 6 + axis * 2, value, true)));
    bytes.set([indices.length, 1, material, ...indices], 4 + vertices.length * 6 + 8);
    return bytes;
}
function palette() {
    const result = new Uint8Array(256 * 3);
    for (let index = 0; index < 256; index++) result.set([index, (index * 3) % 256, (index * 7) % 256], index * 3);
    return result;
}
function syntheticAssets() {
    const shape = primitiveShape([[-120, 0, -512], [120, 0, -512], [120, 0, 512], [-120, 0, 512]], [0, 1, 2, 3]);
    return { models: new Map(data.shapeNames.map(name => [name, shape])), palette: palette() };
}
function object(id, changes = {}) {
    const entry = data.objects[id];
    return { id, x: 0, y: 0, rotation: 0, surface: 0, lowerSurface: 0, width: entry.width,
        height: entry.height, mask: (1 << (entry.width * entry.height)) - 1, baseHeight: 0, slopeX: 0, slopeY: 0, ...changes };
}
function vertices(buffer, first = 0) {
    return Array.from({ length: (buffer.length - first) / 3 }, (_, index) => Array.from(buffer.subarray(first + index * 3, first + index * 3 + 3)));
}
function heightPlane(doc, slopeX, slopeY, base = 1) {
    for (let y = 0; y <= doc.height; y++) for (let x = 0; x <= doc.width; x++)
        doc.vertices[y * (doc.width + 1) + x] = base + slopeX * x + slopeY * y;
}

test('native decoder rejects truncated and invalid primitive records', () => {
    const source = primitiveShape([[0, 0, 0], [128, 0, 0], [0, 128, 0]], [0, 1, 2]);
    const decoded = scene.decodeShape(source);
    assert.deepEqual(decoded.vertices, [[0, 0, 0], [128, 0, 0], [0, 128, 0]]);
    assert.deepEqual(decoded.primitives[0].indices, [0, 1, 2]);
    for (let length = 0; length < source.length; length++) assert.throws(() => scene.decodeShape(source.slice(0, length)), /3D model/);
    const invalid = source.slice(); invalid[invalid.length - 1] = 3;
    assert.throws(() => scene.decodeShape(invalid), /invalid vertex/);
});

test('concave native faces triangulate without filling their missing corner', () => {
    const points = [[0, 0, 0], [2, 0, 0], [2, 0, 1], [1, 0, 1], [1, 0, 2], [0, 0, 2]];
    const triangles = scene.triangulate(points);
    const area = triangles.reduce((total, [a, b, c]) => total + Math.abs((b[0] - a[0]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[0] - a[0])) / 2, 0);
    near(area, 3); assert.equal(triangles.length, points.length - 2);
});

test('terrain heights and all four material corners retain area and their support plane', () => {
    const assets = syntheticAssets();
    for (let corner = 1; corner <= 4; corner++) {
        const doc = new model.Document(1, 1);
        doc.vertices.set([1, 0, 0, 0]); doc.cells[0] = { surface: 0, halfWaterCorner: corner, cornerSurface: 3 };
        const result = scene.buildScene(doc, assets), points = vertices(result.positions);
        let area = 0;
        for (let index = 0; index < points.length; index += 3) {
            const [a, b, c] = points.slice(index, index + 3);
            area += Math.abs((b[0] - a[0]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[0] - a[0])) / 2;
        }
        near(area, 1); near(result.bounds.max[1], LEVEL / TILE);
        const colorSets = new Set(vertices(result.colors).map(color => color.map(channel => channel.toFixed(4)).join(',')));
        assert(colorSets.size >= 2, 'both terrain materials remain visible');
    }
});

test('overpass lower road keeps its perpendicular native direction', () => {
    const assets = syntheticAssets(), doc = new model.Document(1, 1);
    doc.objects.push(object(101));
    const result = scene.buildScene(doc, assets), points = vertices(result.positions, 18);
    assert.equal(result.instances, 2);
    const top = points.slice(0, 6), lower = points.slice(6);
    const extent = (list, axis) => Math.max(...list.map(point => point[axis])) - Math.min(...list.map(point => point[axis]));
    assert(extent(top, 0) < extent(top, 2)); assert(extent(lower, 0) > extent(lower, 2));
});

test('native hill-road substitutions use authored road geometry rather than an extra grade', () => {
    const assets = syntheticAssets(), doc = new model.Document(1, 1);
    heightPlane(doc, 0, -1); doc.objects.push(object(4, { baseHeight: 1, slopeY: -1 }));
    const hill = data.objects[data.hillRoads[0].shapes[0]];
    assets.models.set(hill.shape, primitiveShape([[-120, 0, -512], [120, 0, -512], [120, 450, 512], [-120, 450, 512]], [0, 1, 2, 3]));
    const points = vertices(scene.buildScene(doc, assets).positions, 18);
    near(Math.min(...points.map(point => point[1])), .0005);
    near(Math.max(...points.map(point => point[1])), LEVEL / TILE + .0005);
});

test('start camera keeps the map position and native north-east-south-west heading', () => {
    const assets = syntheticAssets(), cardinal = [[0, 1], [1, 0], [0, -1], [-1, 0]];
    const starts = model.catalog.filter(piece => piece.physical === 0);
    for (const entry of starts) for (let rotation = 0; rotation < cardinal.length; rotation++) {
        const doc = new model.Document(9, 6);
        doc.vertices.fill(2);
        const start = doc.place(entry.id, 1, 3, 0, rotation);
        const { spawn } = scene.buildScene(doc, assets);
        near(spawn.position[0], 1.5, 'a start placed on the west side keeps its x coordinate');
        near(spawn.position[2], 2.5, 'the map row maps to the original north-positive z coordinate');
        near(spawn.position[1], 2 * LEVEL / TILE + .18);
        const direction = cardinal[(entry.rotation + start.rotation) % cardinal.length];
        near(Math.sin(spawn.yaw), direction[0], 'native start east/west heading');
        near(Math.cos(spawn.yaw), direction[1], 'native start north/south heading');
    }
});

test('large maps compile into finite typed buffers without changing the document', () => {
    const doc = new model.Document(90, 90), before = doc.snapshot();
    const result = scene.buildScene(doc, syntheticAssets());
    assert.equal(result.triangles, 90 * 90 * 2); assert(result.positions instanceof Float32Array);
    assert.equal(result.positions.length, result.colors.length); assert(result.positions.every(Number.isFinite));
    assert.deepEqual(result.bounds.min, [0, 0, 0]); assert.deepEqual(result.bounds.max, [90, 0, 90]);
    assert.deepEqual(doc.snapshot(), before);
});

(async () => {
    const directory = process.argv[2] || path.join(ROOT, 'stunts');
    if (!fs.existsSync(path.join(directory, 'GAME1.P3S')) && !fs.existsSync(path.join(directory, 'GAME1.3SH'))) {
        console.log('SKIP native-resource scene checks: original game files are unavailable.'); return;
    }
    const files = new Map(fs.readdirSync(directory).filter(name => fs.statSync(path.join(directory, name)).isFile())
        .map(name => [name, new Uint8Array(fs.readFileSync(path.join(directory, name)))]));
    const assets = await resources.load3D(files);
    test('every native track shape decodes and every editor family renders in all directions and surfaces', () => {
        assert(assets.models.size >= data.shapeNames.length);
        for (const name of data.shapeNames) assert(scene.decodeShape(assets.models.get(name)).vertices.length > 0);
        for (const piece of model.library) for (let direction = 0; direction < 4; direction++) for (let surface = 0; surface < 3; surface++) {
            const doc = new model.Document(4, 4), variant = model.pieceVariant(piece, direction);
            doc.place(variant.piece.id, 1, 1, surface, variant.rotation, variant.piece.lowerSurface || 0,
                variant.piece.tunnelSlalom || 0, variant.piece.rollRamp || 0, variant.piece.pineCount || 0);
            const before = doc.snapshot(), result = scene.buildScene(doc, assets);
            assert(result.instances > 0, piece.name); assert(result.triangles > 32, piece.name);
            assert(result.positions.every(Number.isFinite)); assert(result.colors.every(channel => channel >= 0 && channel <= 1));
            assert.equal(result.positions.length, result.colors.length); assert.equal(result.linePositions.length, result.lineColors.length);
            assert.deepEqual(doc.snapshot(), before);
        }
    });
    test('six and ten pines keep independent upright crowns and slope-following roots', () => {
        const source = scene.decodeShape(assets.models.get('tree'));
        const nativeTrees = source.primitives.reduce((sum, primitive) => sum + primitive.indices.length - 2, 0);
        for (const count of [6, 10]) for (const [slopeX, slopeY] of [[0, 0], [1, 0], [-1, 0], [0, 1], [0, -1]]) for (let rotation = 0; rotation < 4; rotation++) {
            const doc = new model.Document(1, 1); heightPlane(doc, slopeX, slopeY, 1);
            doc.objects.push(object(153, { pineCount: count, rotation, baseHeight: 1, slopeX, slopeY }));
            const result = scene.buildScene(doc, assets);
            assert.equal(result.instances, count); assert.equal(result.triangles, 2 + nativeTrees * count);
            const points = vertices(result.positions, 18), perTree = nativeTrees * 3;
            const minimums = [], maximums = [];
            for (let tree = 0; tree < count; tree++) {
                const treePoints = points.slice(tree * perTree, (tree + 1) * perTree);
                const roots = treePoints.filter(point => Math.abs(point[1] - (1 + slopeX * point[0] + slopeY * (1 - point[2])) * LEVEL / TILE) < EPSILON);
                assert(roots.length >= 3, 'roots follow their terrain plane');
                minimums.push(Math.min(...treePoints.map(point => point[1])));
                maximums.push(Math.max(...treePoints.map(point => point[1])));
                const trunkTop = treePoints.filter(point => point[1] > Math.max(...roots.map(root => root[1])) + .01);
                assert(trunkTop.length > 0, 'each tree has foliage above its roots');
            }
            // Each crown receives its own uphill lift, based on this pine's
            // scaled native footprint, independent of the other trees.
            const [x, z, scale] = resources.pineTreeLayout(count)[0];
            const firstOffset = [[x, z], [z, -x], [-x, -z], [-z, x]][rotation];
            const radius = Math.trunc(60 * scale / 256), crownHeight = Math.trunc(300 * scale / 256);
            const anchorHeight = (1 + slopeX * (.5 + firstOffset[0] / TILE) +
                slopeY * (.5 - firstOffset[1] / TILE) + (Math.abs(slopeX) + Math.abs(slopeY)) * radius / TILE) * LEVEL;
            near(maximums[0], (crownHeight + anchorHeight) / TILE, 'first crown retains one common independent uphill lift');
            assert(new Set(maximums.map(value => value.toFixed(4))).size > 1, 'trees have different sizes and lifts');
            if (slopeX || slopeY) assert(new Set(minimums.map(value => value.toFixed(4))).size > 1, 'trees have independent support heights');
        }
    });
    test('pine forests vary neighboring groves and reproduce the same mesh after saving older or current tracks', () => {
        for (const count of [6, 10]) {
            const doc = new model.Document(8, 8);
            doc.placePineBrush(0, 0, 8, count);
            // Existing ten-tree tracks use the same scatter as newly created tracks.
            if (count === 10) doc.version = model.PINE_GROUP_VERSION;
            const layouts = doc.objects.map(object => resources.pineTreeLayout(object.pineCount, object.x, object.y));
            assert.equal(new Set(layouts.map(layout => JSON.stringify(layout))).size, 64, 'neighboring groves have distinct arrangements');
            const before = scene.buildScene(doc, assets);
            assert.equal(before.instances, 64 * count);
            const reloaded = model.decodeCstrk(model.encodeCstrk(doc));
            const after = scene.buildScene(reloaded, assets);
            assert.deepEqual(after.positions, before.positions, 'saving and reloading preserves all tree geometry');
            assert.deepEqual(after.colors, before.colors);
        }
    });
    test('barrel ramps show concrete approach, twisted grille, markings and supports', () => {
        const doc = new model.Document(2, 2); doc.objects.push(object(85, { rollRamp: 1 }));
        const result = scene.buildScene(doc, assets);
        assert(result.linePositions.length > 0, 'open grille uses depth-tested mesh lines');
        assert(result.triangles > 100, 'approach sides, dashes and all support boxes are present');
        assert(result.bounds.max[1] > .7, 'ramp rises above ground');
    });
    test('missing original geometry is reported instead of replacing track pieces with boxes', () => {
        const doc = new model.Document(1, 1); doc.objects.push(object(153));
        assert.throws(() => scene.buildScene(doc, { models: new Map(), palette: assets.palette }), /missing 3D model/);
    });
    console.log(checks + ' scene compiler checks passed.');
})().catch(error => { console.error(error); process.exitCode = 1; });
