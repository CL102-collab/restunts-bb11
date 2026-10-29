#!/usr/bin/env node
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const model = require('../../src/restunts/platform/wasm/track-editor-model.js');
const resources = require('../../src/restunts/platform/wasm/track-editor-resources.js');

function test(name, callback) { callback(); console.log('PASS ' + name); }
test('library combines rotated alternatives while preserving distinct shapes and groups', () => {
    assert.deepEqual(model.groups.map(g => model.library.filter(p => p.group === g.id).length), [7, 8, 4, 2, 4, 9, 2, 10]);
    const families = new Set(model.library.map(p => p.group + ':' + p.physical));
    assert.equal(families.size + model.SURFACES.length - 1, model.library.length);
    assert.equal(new Set(model.library.map(piece => piece.key)).size, model.library.length);
    for (const entry of model.catalog.filter(p => p.legacySurface <= 0)) assert(families.has(entry.group + ':' + entry.physical));
    for (const id of [38, 58, 60, 62, 101, 103]) assert(model.library.some(p => p.id === id));
    assert.equal(model.library.filter(p => p.name === 'Highway transition').length, 1);
});
test('directions select native artwork and rotate remaining symmetric variations', () => {
    const piece = id => model.catalog.find(p => p.id === id);
    for (const [id, expected] of [[111, [111, 112, 113, 114]], [1, [1, 180, 179, 181]], [7, [7, 9, 8, 6]]]) {
        assert.deepEqual([0, 1, 2, 3].map(direction => model.pieceVariant(piece(id), direction).piece.id), expected);
    }
    const eastLoop = model.pieceVariant(piece(64), 1);
    assert.equal(eastLoop.piece.id, 65); assert.equal(eastLoop.rotation, 0);
    assert.deepEqual(model.rotatedFootprint(eastLoop.piece, eastLoop.rotation), { width: 2, height: 1, mask: 3 });
    const southLoop = model.pieceVariant(piece(64), 2);
    assert.equal(southLoop.piece.id, 64); assert.equal(southLoop.rotation, 2);
    const eastTurn = model.pieceVariant(piece(11), 1);
    assert.equal(eastTurn.piece.id, 13); assert.equal(eastTurn.rotation, 0);
    assert.deepEqual(model.rotatedFootprint(eastTurn.piece, eastTurn.rotation), { width: 2, height: 2, mask: 14 });
});
test('symmetric pieces share canonical artwork without changing saved placement', () => {
    const symmetricExamples = [4, 64, 66, 68, 83, 34, 103, 99, 101, 109, 115, 61, 60, 85];
    const symmetricModels = new Set(symmetricExamples.map(id => model.catalog.find(piece => piece.id === id).physical));
    const catalogBefore = JSON.parse(JSON.stringify(model.catalog));
    for (const entry of model.catalog.filter(piece => symmetricModels.has(piece.physical))) {
        for (let rotation = 0; rotation < 4; rotation++) {
            const display = model.pieceDisplayVariant(entry, rotation);
            const reversed = model.pieceDisplayVariant(entry, (rotation + 2) % 4);
            assert.equal(display.piece.id, reversed.piece.id, entry.name + ': opposite directions share artwork');
            assert.equal(display.rotation, reversed.rotation);
            assert.deepEqual(model.rotatedFootprint(display.piece, display.rotation), model.rotatedFootprint(entry, rotation));
            for (const surface of entry.surfaces) {
                const doc = new model.Document(4, 4);
                doc.place(entry.id, 1, 1, surface, rotation);
                const before = doc.snapshot(), saved = model.encodeCstrk(doc);
                model.pieceDisplayVariant(entry, rotation);
                assert.deepEqual(doc.snapshot(), before);
                assert.deepEqual(model.encodeCstrk(doc), saved);
            }
        }
    }
    assert.deepEqual(model.catalog, catalogBefore);
    // Cover every symmetric family, including multi-route and elevated artwork.
    const canonicalIds = [4, 34, 60, 62, 64, 66, 68, 83, 85, 99, 101, 103, 109, 115];
    assert.equal(new Set(canonicalIds.map(id => model.catalog.find(piece => piece.id === id).physical)).size, symmetricModels.size);
    for (const id of canonicalIds) {
        const canonical = model.catalog.find(piece => piece.id === id);
        const variants = model.catalog.filter(piece => piece.group === canonical.group && piece.physical === canonical.physical);
        for (const entry of variants) for (let rotation = 0; rotation < 4; rotation++) {
            const display = model.pieceDisplayVariant(entry, rotation);
            assert.equal(display.piece.id, id, entry.name + ': every native variant uses the canonical image');
            assert.equal(display.rotation, (entry.rotation + rotation) % 2);
        }
    }
});
test('display variants preserve absolute direction and projected connector positions', () => {
    const symmetricModels = new Set([1, 27, 28, 30, 31, 18, 19, 20, 22, 11, 34, 4, 5, 35]);
    const rotatedModels = new Set([0, 2, 3, 6, 7, 8, 9, 10, 29, 23, 24, 25, 26]);
    for (const entry of model.catalog) for (let rotation = 0; rotation < 4; rotation++) {
        const display = model.pieceDisplayVariant(entry, rotation);
        if (entry.physical === 12) {
            assert.equal(display.piece.id, 74);
            assert.equal(display.rotation, 0);
        } else if (!symmetricModels.has(entry.physical)) {
            const direction = (entry.rotation + rotation) % 4;
            assert.equal((display.piece.rotation + display.rotation) % 4, direction);
            if (rotatedModels.has(entry.physical)) {
                const canonical = model.library.find(piece => piece.group === entry.group && piece.physical === entry.physical);
                assert.equal(display.piece.id, canonical.id);
                assert.equal(display.rotation, direction);
            } else {
                const native = model.catalog.find(piece => piece.group === entry.group && piece.physical === entry.physical &&
                    piece.legacySurface <= 0 && piece.rotation === direction);
                if (native) {
                    assert.equal(display.piece.id, native.id);
                    assert.equal(display.rotation, 0);
                }
            }
            assert(display.piece.legacySurface <= 0, 'independent surface tint starts from neutral native artwork');
            assert.deepEqual(model.rotatedFootprint(display.piece, display.rotation), model.rotatedFootprint(entry, rotation));
        }
    }
    for (const [id, rotation, expectedId, expectedRotation] of [[38, 2, 39, 0], [58, 2, 59, 0], [97, 2, 98, 0], [106, 2, 107, 0],
        [117, 1, 120, 0], [121, 3, 122, 0], [17, 2, 7, 2], [134, 2, 1, 2], [54, 0, 53, 2], [55, 0, 53, 1]]) {
        const entry = model.catalog.find(piece => piece.id === id);
        assert.deepEqual(model.pieceDisplayVariant(entry, rotation), { piece: model.catalog.find(piece => piece.id === expectedId), rotation: expectedRotation });
        const doc = new model.Document(4, 4);
        doc.place(id, 1, 1, 2, rotation);
        const before = doc.snapshot(), saved = model.encodeCstrk(doc);
        model.pieceDisplayVariant(entry, rotation);
        assert.deepEqual(doc.snapshot(), before);
        assert.deepEqual(model.encodeCstrk(doc), saved);
    }
});
test('every library direction preserves the placement footprint and saved track data', () => {
    for (const entry of model.library) for (let direction = 0; direction < 4; direction++) {
        const { piece, rotation } = model.pieceVariant(entry, direction);
        assert.equal((piece.rotation + rotation) % 4, direction);
        const expected = model.rotatedFootprint(entry, direction);
        assert.deepEqual(model.rotatedFootprint(piece, rotation), expected);
        const doc = new model.Document(4, 4), placed = doc.place(piece.id, 1, 1, 0, rotation);
        assert.equal(placed.width, expected.width); assert.equal(placed.height, expected.height); assert.equal(placed.mask, expected.mask);
        assert.deepEqual(model.decodeCstrk(model.encodeCstrk(doc)).snapshot(), doc.snapshot());
    }
});
test('terrain reaches four levels and propagates all corner slopes', () => {
    const doc = new model.Document();
    for (let i = 0; i < 8; i++) doc.raise(45, 45);
    assert.equal(doc.vertices[45 * 91 + 45], 4);
    assert.equal(doc.vertices[45 * 91 + 44], 3);
    assert.equal(doc.vertices[43 * 91 + 43], 2);
    assert.deepEqual(doc.validate(), []);
    doc.raise(45, 45, -1);
    assert.equal(doc.vertices[45 * 91 + 45], 3);
});
test('original hill profiles preserve native slope and corner classifications at every level', () => {
    const profiles = [[7, [1, 1, 0, 0]], [8, [1, 0, 0, 1]], [9, [0, 0, 1, 1]], [10, [0, 1, 1, 0]],
        [11, [1, 0, 0, 0]], [12, [0, 0, 0, 1]], [13, [0, 0, 1, 0]], [14, [0, 1, 0, 0]],
        [15, [1, 1, 0, 1]], [16, [1, 0, 1, 1]], [17, [0, 1, 1, 1]], [18, [1, 1, 1, 0]]];
    const maximumLevel = 4, expected = new Map();
    for (let base = 0; base < maximumLevel; base++) for (const [terrain, heights] of profiles) {
        const translated = heights.map(height => height + base);
        expected.set(translated.join(','), { terrain, baseLevel: base });
    }
    for (let level = 0; level <= maximumLevel; level++)
        expected.set([level, level, level, level].join(','), { terrain: level ? 6 : 0, baseLevel: level });
    for (let nw = 0; nw <= maximumLevel; nw++) for (let ne = 0; ne <= maximumLevel; ne++) {
        for (let se = 0; se <= maximumLevel; se++) for (let sw = 0; sw <= maximumLevel; sw++) {
            const heights = [nw, ne, se, sw], profile = expected.get(heights.join(',')) || null;
            assert.deepEqual(model.terrainProfile(heights), profile);
            const doc = new model.Document(1, 1); doc.vertices.set([nw, ne, sw, se]);
            if (profile) assert.deepEqual(model.decodeCstrk(model.encodeCstrk(doc)).snapshot(), doc.snapshot());
            else assert.throws(() => model.encodeCstrk(doc), /Terrain/);
        }
    }
    assert.equal(expected.size, 53);
    for (const invalid of [null, [], [0, 0, 0], [0, .5, 0, 0], [0, 0, 0, 5]]) assert.equal(model.terrainProfile(invalid), null);
});
test('terrain edits produce only original profiles and resolve diagonal islands symmetrically', () => {
    const doc = new model.Document(8, 8);
    const assertProfiles = () => {
        for (let y = 0; y < doc.height; y++) for (let x = 0; x < doc.width; x++)
            assert(model.terrainProfile(doc.tileHeights(x, y)), 'every tile has an original hill profile');
        assert.deepEqual(doc.validate(), []);
    };
    doc.raise(3, 3); doc.raise(4, 4); assertProfiles();
    assert.deepEqual(doc.tileHeights(3, 3), [1, 1, 1, 1]);
    doc.raise(3, 4, -1); doc.raise(4, 3, -1); assertProfiles();
    assert.deepEqual(doc.tileHeights(3, 3), [0, 0, 0, 0]);
    for (const height of [4, 0, 3, 1, 2]) {
        doc.level(3, 3, height, 0); assertProfiles();
        assert.deepEqual(doc.tileHeights(3, 3), [height, height, height, height]);
    }
    const randomMultiplier = 1664525, randomIncrement = 1013904223;
    let random = 19;
    for (let edit = 0; edit < 100; edit++) {
        random = (Math.imul(random, randomMultiplier) + randomIncrement) >>> 0;
        const x = random % (doc.width + 1), y = (random >>> 8) % (doc.height + 1);
        doc.raise(x, y, random & 1 ? 1 : -1); assertProfiles();
    }
});
test('hill placement matches original aligned road and ramp rules in every direction and level', () => {
    const maximumLevel = 4, tileCount = 4, target = 1;
    for (let base = 0; base < maximumLevel; base++) for (let terrain = 7; terrain <= 10; terrain++) {
        const doc = new model.Document(tileCount, tileCount);
        for (let y = 0; y <= tileCount; y++) for (let x = 0; x <= tileCount; x++) {
            const raised = terrain === 7 ? y <= target : terrain === 8 ? x <= target : terrain === 9 ? y > target : x > target;
            doc.vertices[y * (tileCount + 1) + x] = base + raised;
        }
        assert.equal(model.terrainProfile(doc.tileHeights(target, target)).terrain, terrain);
        for (const entry of model.catalog) for (let rotation = 0; rotation < 4; rotation++) {
            const direction = (entry.rotation + rotation) % 4;
            const alignedRoad = entry.physical === 1 && direction % 2 === (terrain === 7 || terrain === 9 ? 0 : 1);
            const matchingRamp = (entry.physical === 16 || entry.physical === 17) && direction === [2, 1, 0, 3][terrain - 7];
            for (const surface of entry.surfaces) {
                if (alignedRoad || matchingRamp) {
                    const object = doc.place(entry.id, target, target, surface, rotation);
                    assert.deepEqual(doc.validate(), []);
                    assert.deepEqual(model.decodeCstrk(model.encodeCstrk(doc)).snapshot(), doc.snapshot());
                    assert.equal(object.baseHeight, doc.tileHeights(target, target)[0]);
                    doc.objects = [];
                } else assert.throws(() => doc.place(entry.id, target, target, surface, rotation), /original hill/);
            }
        }
    }
    for (const heights of [[1, 0, 0, 0], [0, 1, 1, 1]]) {
        const doc = new model.Document(1, 1); doc.vertices.set([heights[0], heights[1], heights[3], heights[2]]);
        for (const id of [4, 38, 66, 151]) assert.throws(() => doc.place(id, 0, 0), /original hill/);
    }
});
test('original hill propagation is identical when the entire edit sequence is rotated', () => {
    const width = 8, stride = width + 1, edits = 100;
    const first = new model.Document(width, width), rotated = new model.Document(width, width);
    const randomMultiplier = 1664525, randomIncrement = 1013904223;
    let random = 41;
    for (let edit = 0; edit < edits; edit++) {
        random = (Math.imul(random, randomMultiplier) + randomIncrement) >>> 0;
        if (edit % 3) {
            const x = random % stride, y = (random >>> 8) % stride, delta = random & 1 ? 1 : -1;
            first.raise(x, y, delta); rotated.raise(width - y, x, delta);
        } else {
            const x = random % width, y = (random >>> 8) % width, level = (random >>> 16) % 5;
            first.level(x, y, level, 0); rotated.level(width - 1 - y, x, level, 0);
        }
        for (let y = 0; y <= width; y++) for (let x = 0; x <= width; x++)
            assert.equal(first.vertices[y * stride + x], rotated.vertices[x * stride + width - y]);
    }
});
test('hill display corners turn through right angles consistently at every orientation and level', () => {
    const cornerCount = 4, profileCount = 1 << cornerCount, maximumLevel = 4;
    const pointKey = point => JSON.stringify(point.map(value => Number(value.toFixed(12))));
    function pathKey(points, closed) {
        const keys = points.map(pointKey), variants = [];
        for (const ordered of [keys, keys.slice().reverse()]) {
            for (let start = 0; start < (closed ? ordered.length : 1); start++)
                variants.push(ordered.slice(start).concat(ordered.slice(0, start)).join(';'));
        }
        return variants.sort()[0];
    }
    const shapeKey = layer => ({ polygons: layer.polygons.map(points => pathKey(points, true)).sort(),
        contours: layer.contours.map(points => pathKey(points, false)).sort() });
    const rotatePoint = ([x, y]) => [1 - y, x];
    for (let base = 0; base < maximumLevel; base++) for (let mask = 1; mask < profileCount - 1; mask++) {
        const heights = Array.from({ length: cornerCount }, (_, corner) => base + !!(mask & (1 << corner)));
        if (!model.terrainProfile(heights)) {
            assert.throws(() => model.terrainDisplayLayers(heights), /terrain display profile/);
            continue;
        }
        const before = heights.slice(), display = model.terrainDisplayLayers(heights);
        assert.equal(display.baseLevel, base); assert.equal(display.layers.length, 1);
        const layer = display.layers[0];
        assert.equal(layer.level, base + .5);
        const highCorners = heights.filter(height => height > base).length;
        for (const points of layer.polygons) for (const point of points)
            assert(point.every(coordinate => coordinate >= 0 && coordinate <= 1));
        if (highCorners === 1 || highCorners === cornerCount - 1) {
            assert.equal(layer.contours.length, 1);
            const [first, elbow, last] = layer.contours[0];
            assert.equal(layer.contours[0].length, 3);
            const firstHorizontal = first[1] === elbow[1], lastHorizontal = last[1] === elbow[1];
            assert(firstHorizontal !== lastHorizontal, 'a corner contains one horizontal and one vertical segment');
            assert(firstHorizontal ? first[0] !== elbow[0] && last[0] === elbow[0] : first[0] === elbow[0] && last[0] !== elbow[0]);
        }
        const rotated = model.terrainDisplayLayers([heights[3], heights[0], heights[1], heights[2]]);
        assert.deepEqual(shapeKey(rotated.layers[0]), shapeKey({ polygons: layer.polygons.map(points => points.map(rotatePoint)),
            contours: layer.contours.map(points => points.map(rotatePoint)) }));
        assert.deepEqual(heights, before);
    }
    assert.deepEqual(model.terrainDisplayLayers([1, 0, 0, 0]).layers[0].contours,
        [[[.5, 0], [.5, .5], [0, .5]]]);
    for (let level = 0; level <= maximumLevel; level++)
        assert.deepEqual(model.terrainDisplayLayers(new Uint8Array(cornerCount).fill(level)), { baseLevel: level, layers: [] });
});
test('neighboring hill display contours join without changing terrain or saved data', () => {
    const doc = new model.Document(8, 8);
    doc.level(3, 3, 4, 0); doc.paint(2, 2, 0, 2, 1);
    const before = doc.snapshot(), saved = model.encodeCstrk(doc);
    function crossings(x, y, axis, boundary) {
        return model.terrainDisplayLayers(doc.tileHeights(x, y)).layers.flatMap(layer => layer.contours.flatMap(points =>
            [points[0], points[points.length - 1]].filter(point => point[axis] === boundary)
                .map(point => JSON.stringify([layer.level, point[1 - axis]])))).sort();
    }
    for (let y = 0; y < doc.height; y++) for (let x = 0; x < doc.width; x++) {
        if (x + 1 < doc.width) assert.deepEqual(crossings(x, y, 0, 1), crossings(x + 1, y, 0, 0));
        if (y + 1 < doc.height) assert.deepEqual(crossings(x, y, 1, 1), crossings(x, y + 1, 1, 0));
    }
    assert.deepEqual(doc.snapshot(), before); assert.deepEqual(model.encodeCstrk(doc), saved);
});
test('hill display rejects unsupported profiles and keeps geometry independent of materials', () => {
    const doc = new model.Document(2, 2);
    doc.raise(1, 1);
    const expected = model.terrainDisplayLayers(doc.tileHeights(0, 0));
    for (let direction = 1; direction <= 4; direction++) {
        doc.paint(0, 0, 0, direction, 1);
        assert.deepEqual(model.terrainDisplayLayers(doc.tileHeights(0, 0)), expected);
    }
    const heights = [1, 0, 0, 0], first = model.terrainDisplayLayers(heights);
    first.layers[0].polygons[0][0][0] = 99;
    assert.deepEqual(model.terrainDisplayLayers(heights).layers[0].polygons[0][0], [0, 0]);
    for (const invalid of [null, [], [0, 0, 0], [0, 0, 0, 5], [0, .5, 0, 0],
        [1, 0, 1, 0], [0, 1, 0, 1], [0, 1, 2, 1], [0, 0, 2, 2]])
        assert.throws(() => model.terrainDisplayLayers(invalid), /terrain display/);
});
test('Level applies each selected surface to the plateau and its generated slopes', () => {
    for (const surface of [0, 1, 2, 3]) for (const height of [1, 2, 3, 4]) {
        const doc = new model.Document(12, 12);
        doc.paint(0, 0, (surface + 1) % 4, 2, (surface + 1) % 4 === model.DEFAULT_CORNER_SURFACE ? 0 : model.DEFAULT_CORNER_SURFACE);
        const before = doc.snapshot(), history = doc.history.length;
        doc.level(4, 4, height, surface);
        assert.deepEqual(doc.tileHeights(4, 4), [height, height, height, height]);
        for (let y = 0; y < doc.height; y++) for (let x = 0; x < doc.width; x++) {
            const corners = [y * 13 + x, y * 13 + x + 1, (y + 1) * 13 + x, (y + 1) * 13 + x + 1];
            const affected = (x === 4 && y === 4) || corners.some(i => doc.vertices[i] !== before.vertices[i]);
            assert.deepEqual(doc.cells[y * 12 + x], affected ? { surface, halfWaterCorner: 0 } : before.cells[y * 12 + x]);
        }
        assert.deepEqual(doc.validate(), []);
        assert.equal(doc.history.length, history + 1);
        const after = doc.snapshot();
        assert(doc.undo()); assert.deepEqual(doc.snapshot(), before);
        assert(doc.redo()); assert.deepEqual(doc.snapshot(), after);
        assert.deepEqual(model.decodeCstrk(model.encodeCstrk(doc)).snapshot(), after);
    }
});
test('Level recolors an existing height, clears Shore, and supports Ground without losing no-op redo', () => {
    const doc = new model.Document(8, 8);
    doc.vertices.fill(3); doc.paint(3, 3, 0, 2);
    const before = doc.snapshot();
    doc.level(3, 3, 3, 2);
    assert.deepEqual(doc.cells[27], { surface: 2, halfWaterCorner: 0 });
    assert.deepEqual(Array.from(doc.vertices), before.vertices);
    assert(doc.undo()); assert.deepEqual(doc.snapshot(), before);
    doc.redo(); doc.level(3, 3, 0, 1);
    assert.deepEqual(doc.tileHeights(3, 3), [0, 0, 0, 0]);
    assert.deepEqual(doc.cells[27], { surface: 1, halfWaterCorner: 0 });
    assert.deepEqual(doc.validate(), []);
    const flat = new model.Document(2, 2);
    flat.setNotes('future'); flat.undo();
    flat.level(0, 0, 0, 0);
    assert.equal(flat.history.length, 0); assert.equal(flat.canRedo, true);
    flat.level(0, 0, 0, 3);
    assert.deepEqual(flat.cells[0], { surface: 3, halfWaterCorner: 0 });
});
test('Level handles mixed slopes and map edges while Raise and Lower preserve surfaces', () => {
    for (const [x, y] of [[0, 0], [7, 7], [3, 3]]) for (const height of [0, 2, 4]) {
        const doc = new model.Document(8, 8);
        for (let vy = 0; vy <= 8; vy++) for (let vx = 0; vx <= 8; vx++) doc.vertices[vy * 9 + vx] = Math.min(4, Math.max(vx, vy));
        doc.level(x, y, height, 1);
        assert.deepEqual(doc.tileHeights(x, y), [height, height, height, height]);
        assert.deepEqual(doc.validate(), []);
        const cells = doc.cells.map(cell => ({ ...cell }));
        doc.raise(4, 4); doc.raise(4, 4, -1);
        assert.deepEqual(doc.cells, cells);
    }
});
test('blocked Level leaves heights and surfaces atomic inside and outside a drag', () => {
    const doc = new model.Document(12, 12);
    doc.place(4, 3, 3); doc.setNotes('future'); doc.undo();
    const before = doc.snapshot(), history = doc.history.length;
    assert.throws(() => doc.level(2, 2, 1, 2), /bend/);
    assert.deepEqual(doc.snapshot(), before);
    assert.equal(doc.history.length, history); assert.equal(doc.canRedo, true);
    doc.beginTransaction('Terrain stroke'); doc.level(8, 8, 1, 1);
    const earlierTile = doc.snapshot();
    assert.throws(() => doc.level(2, 2, 1, 2), /bend/);
    assert.deepEqual(doc.snapshot(), earlierTile);
    doc.endTransaction(); assert.equal(doc.history.length, history + 1);
    assert(doc.undo()); assert.deepEqual(doc.snapshot(), before);
    assert(doc.redo()); assert.deepEqual(doc.snapshot(), earlierTile);
    // Changing the slope beneath an existing native hill road is also atomic.
    const slope = new model.Document(4, 4);
    for (let y = 0; y <= 4; y++) for (let x = 0; x <= 4; x++) slope.vertices[y * 5 + x] = Math.min(x, 2);
    slope.place(5, 1, 1);
    const supported = slope.snapshot();
    assert.throws(() => slope.level(2, 1, 1, 2), /bend/);
    assert.deepEqual(slope.snapshot(), supported);
});
test('one live terrain stroke is one undo operation and undo restores notes', () => {
    const doc = new model.Document(8, 8), before = doc.snapshot();
    doc.beginTransaction('Terrain stroke'); doc.raise(3, 3); doc.raise(3, 3); doc.paint(3, 3, 1, 2); doc.endTransaction();
    assert.equal(doc.history.length, 1); assert(doc.undo()); assert.deepEqual(doc.snapshot(), before);
    assert(doc.redo()); assert.equal(doc.cells[27].surface, 1);
    doc.setNotes('Hello\nWorld'); doc.undo(); assert.equal(doc.notes, ''); doc.redo(); assert.equal(doc.notes, 'Hello\nWorld');
    doc.undo(); doc.paint(1, 1, 3); assert.equal(doc.canRedo, false);
});
test('failed edits roll back atomically, including cancelled drag strokes', () => {
    const doc = new model.Document(8, 8); doc.place(4, 3, 3); const before = doc.snapshot();
    assert.throws(() => doc.raise(3, 3), /bend/); assert.deepEqual(doc.snapshot(), before);
    doc.beginTransaction(); doc.paint(0, 0, 2); doc.cancelTransaction(); assert.deepEqual(doc.snapshot(), before);
});
test('decorations use empty corner cells of large turns, banked turns, chicanes and elevated turns', () => {
    for (const id of [10, 11, 12, 13, 52, 53, 54, 55, 60, 61, 62, 63, 105, 106, 107, 108]) {
        for (let rotation = 0; rotation < 4; rotation++) {
            const doc = new model.Document(4, 4), object = doc.place(id, 1, 1, 0, rotation);
            const occupied = new Set(model.occupied(object).map(([x, y]) => y * 4 + x));
            for (let y = 1; y <= 2; y++) for (let x = 1; x <= 2; x++) {
                if (occupied.has(y * 4 + x)) assert.throws(() => doc.place(151, x, y), /occupied/);
                else doc.place(151, x, y);
            }
            assert.deepEqual(doc.validate(), []);
        }
    }
});
test('large elements require a flat plateau across their entire footprint', () => {
    const doc = new model.Document(8, 8);
    doc.vertices.fill(2);
    const object = doc.place(11, 0, 2, 2); assert.equal(object.baseHeight, 2);
    assert.equal(object.slopeX, 0); assert.equal(object.slopeY, 0);
    assert.deepEqual(doc.validate(), []);
    doc.objects = [];
    for (let y = 0; y <= 8; y++) for (let x = 0; x <= 8; x++) doc.vertices[y * 9 + x] = Math.min(x, 4);
    assert.throws(() => doc.place(11, 0, 2, 2), /original hill/);
    assert.throws(() => doc.place(11, 3, 4), /original hill/);
});
test('unsupported diagonal planes and saddles are rejected when opening tracks', () => {
    const doc = new model.Document(2, 2);
    const encoded = model.encodeCstrk(doc), headerSize = 32;
    for (let y = 0; y <= 2; y++) for (let x = 0; x <= 2; x++) doc.vertices[y * 3 + x] = x + y;
    assert.throws(() => doc.place(4, 0, 0), /original hill/);
    assert.throws(() => model.encodeCstrk(doc), /original/);
    encoded.set(doc.vertices, headerSize);
    assert.throws(() => model.decodeCstrk(encoded), /original/);
    doc.vertices.fill(0); doc.vertices[1] = doc.vertices[3] = 1;
    assert.throws(() => model.encodeCstrk(doc), /original/);
});
test('slaloms and tunnels require flat plateaus instead of being tilted onto hills', () => {
    for (const id of [66, 115]) {
        const short = new model.Document(4, 4);
        for (let y = 0; y <= 4; y++) for (let x = 0; x <= 4; x++) short.vertices[y * 5 + x] = x ? 1 : 0;
        assert.throws(() => short.place(id, 0, 1), /original hill/);
        const long = new model.Document(4, 4);
        for (let y = 0; y <= 4; y++) for (let x = 0; x <= 4; x++) long.vertices[y * 5 + x] = Math.min(x, 2);
        assert.throws(() => long.place(id, 0, 1), /original hill/);
        assert.throws(() => long.place(id, 1, 1), /original hill/);
        for (let level = 0; level <= 4; level++) {
            long.vertices.fill(level); assert.doesNotThrow(() => long.place(id, 0, 1)); long.objects = [];
        }
    }
});
test('expanded road surfaces are available across road and stunt families', () => {
    for (const id of [40, 49, 53, 60, 64, 66, 68, 70, 75, 83, 85, 87, 99, 105, 109, 115, 117, 121]) {
        for (const surface of [0, 1, 2]) assert.doesNotThrow(() => new model.Document(4, 4).place(id, 0, 0, surface));
    }
});
test('all loop and pipe variations preserve dirt and ice through placement, validation and reload', () => {
    const loopAndPipeModels = new Set([27, 29, 30, 31]);
    const entries = model.catalog.filter(piece => loopAndPipeModels.has(piece.physical));
    assert.deepEqual(new Set(entries.map(piece => piece.physical)), loopAndPipeModels);
    for (const entry of entries) {
        assert.deepEqual(entry.surfaces, [0, 1, 2]);
        for (const surface of [1, 2]) for (let rotation = 0; rotation < 4; rotation++) for (const level of [0, 4]) {
            const doc = new model.Document(4, 4);
            doc.vertices.fill(level);
            doc.place(entry.id, 1, 1, surface, rotation);
            assert.deepEqual(doc.validate(), []);
            const loaded = model.decodeCstrk(model.encodeCstrk(doc));
            assert.equal(loaded.objects[0].surface, surface);
            assert.deepEqual(loaded.snapshot(), doc.snapshot());
        }
    }
});
test('overpass palette variants keep the lower road independent through rotations, history and reload', () => {
    const variants = model.library.filter(piece => piece.physical === model.OVERPASS_PHYSICAL_MODEL);
    assert.deepEqual(variants.map(piece => piece.name), ['Overpass over asphalt', 'Overpass over dirt', 'Overpass over ice']);
    assert.deepEqual(variants.map(piece => piece.lowerSurface), [0, 1, 2]);
    assert.equal(new Set(variants.map(piece => piece.key)).size, variants.length);
    for (const selected of variants) for (const surface of [0, 1, 2]) for (let direction = 0; direction < 4; direction++) {
        const doc = new model.Document(4, 4), variant = model.pieceVariant(selected, direction);
        assert.equal(variant.piece.key, selected.key);
        assert.equal(variant.piece.lowerSurface, selected.lowerSurface);
        assert.equal(variant.piece.name, selected.name);
        const placed = doc.place(variant.piece.id, 1, 1, surface, variant.rotation, selected.lowerSurface);
        assert.equal(placed.surface, surface); assert.equal(placed.lowerSurface, selected.lowerSurface);
        const expected = doc.snapshot();
        assert(doc.undo()); assert.equal(doc.objects.length, 0);
        assert(doc.redo()); assert.deepEqual(doc.snapshot(), expected);
        const encoded = model.encodeCstrk(doc), objectOffset = 32 + doc.vertices.length + doc.cells.length * 2;
        assert.equal(encoded[objectOffset + 12], selected.lowerSurface);
        assert.deepEqual(Array.from(encoded.subarray(objectOffset + 13, objectOffset + 16)), [0, 0, 0]);
        assert.deepEqual(model.decodeCstrk(encoded).snapshot(), expected);
    }
    assert.throws(() => new model.Document(4, 4).place(4, 1, 1, 0, 0, 1), /Only overpasses/);
    for (const lowerSurface of [-1, 3, 1.5])
        assert.throws(() => new model.Document(4, 4).place(101, 1, 1, 0, 0, lowerSurface), /lower road surface/);
});
test('all corkscrew variations preserve dirt and ice surfaces through save and reload', () => {
    for (const entry of model.catalog.filter(p => [32, 33, 35].includes(p.physical))) {
        for (const surface of [1, 2]) for (let rotation = 0; rotation < 4; rotation++) {
            const doc = new model.Document(4, 4);
            doc.place(entry.id, 1, 1, surface, rotation);
            const loaded = model.decodeCstrk(model.encodeCstrk(doc));
            assert.equal(loaded.objects[0].surface, surface);
            assert.deepEqual(loaded.snapshot(), doc.snapshot());
        }
    }
});
test('scenery accepts every road selection and new placements store the neutral surface', () => {
    for (const entry of model.catalog.filter(p => p.decoration)) for (const surface of [0, 1, 2]) {
        const doc = new model.Document(4, 4), placed = doc.place(entry.id, 1, 1, surface);
        assert.equal(placed.surface, 0);
        assert.deepEqual(doc.validate(), []);
        // Surface values already present in a file are accepted and ignored for scenery.
        placed.surface = surface;
        assert.deepEqual(model.decodeCstrk(model.encodeCstrk(doc)).snapshot(), doc.snapshot());
    }
});
test('strict ASCII notes enforce the 8000-character bound', () => {
    const doc = new model.Document(); doc.setNotes('A'.repeat(8000));
    assert.throws(() => doc.setNotes('A'.repeat(8001)), /8000/);
    for (const invalid of ['café', '\u0000', '\u007f', 'track 🏁']) assert.throws(() => doc.setNotes(invalid), /ASCII/);
});
test('horizons are validated edits with undo, redo, and no-op history preservation', () => {
    const doc = new model.Document(4, 4);
    assert.equal(doc.horizon, 0);
    assert.deepEqual(model.HORIZONS, ['Desert', 'Tropical', 'Alpine', 'City', 'Country']);
    doc.setNotes('Keep these notes');
    const original = doc.snapshot();
    for (let horizon = 1; horizon < model.HORIZONS.length; horizon++) {
        doc.setHorizon(horizon);
        assert.equal(doc.horizon, horizon);
        assert.equal(doc.notes, original.notes);
        assert.equal(doc.history.at(-1).label, 'Change horizon');
    }
    for (let horizon = model.HORIZONS.length - 2; horizon >= 0; horizon--) {
        assert(doc.undo()); assert.equal(doc.horizon, horizon);
    }
    assert.deepEqual(doc.snapshot(), original);
    doc.setHorizon(doc.horizon);
    assert.equal(doc.canRedo, true);
    assert(doc.redo()); assert.equal(doc.horizon, 1);
    const before = doc.snapshot(), history = doc.history.length;
    for (const invalid of [-1, model.HORIZONS.length, 1.5, '2', null])
        assert.throws(() => doc.setHorizon(invalid), /horizon/);
    assert.deepEqual(doc.snapshot(), before); assert.equal(doc.history.length, history);
});
test('Shore paints every distinct material pair and corner with exact storage and undo/redo', () => {
    const water = model.DEFAULT_CORNER_SURFACE;
    const headerSize = 32, cornerSurfaceShift = 3;
    for (let surface = 0; surface < model.TERRAIN_SURFACES.length; surface++) {
        for (let secondary = 0; secondary < model.TERRAIN_SURFACES.length; secondary++) {
            if (surface === secondary) continue;
            for (let direction = 1; direction <= 4; direction++) {
                const doc = new model.Document(2, 2); doc.setNotes('Mixed surfaces');
                const before = doc.snapshot();
                doc.paint(0, 0, surface, direction, secondary);
                const expected = { surface, halfWaterCorner: direction };
                if (secondary !== water) expected.cornerSurface = secondary;
                assert.deepEqual(doc.cells[0], expected);
                assert.equal(doc.version, model.VERSION);
                const after = doc.snapshot(), encoded = model.encodeCstrk(doc);
                const cornerByte = headerSize + doc.vertices.length + 1;
                assert.equal(encoded[cornerByte], direction | ((secondary ^ water) << cornerSurfaceShift));
                assert.deepEqual(model.decodeCstrk(encoded).snapshot(), after);
                assert(doc.undo()); assert.deepEqual(doc.snapshot(), before);
                assert(doc.redo()); assert.deepEqual(doc.snapshot(), after);
                doc.paint(0, 0, surface);
                assert.deepEqual(doc.cells[0], { surface, halfWaterCorner: 0 });
                doc.cells[0].cornerSurface = model.TERRAIN_SURFACES.length;
                const cleared = model.encodeCstrk(doc);
                assert.equal(cleared[cornerByte], 0);
                assert.deepEqual(model.decodeCstrk(cleared).cells[0], { surface, halfWaterCorner: 0 });
                assert(doc.undo()); assert.deepEqual(doc.cells[0], expected);
                doc.level(0, 0, 1, surface);
                assert.deepEqual(doc.cells[0], { surface, halfWaterCorner: 0 });
            }
        }
    }
});
test('lower surface paints every flat elevation and preserves uniform hills through all four transitions', () => {
    const water = model.DEFAULT_CORNER_SURFACE, maximumLevel = 4, corners = [[0, 0], [1, 0], [1, 1], [0, 1]];
    for (let target = 0; target < model.TERRAIN_SURFACES.length; target++) {
        for (let surface = 0; surface < model.TERRAIN_SURFACES.length; surface++) for (let base = 0; base <= maximumLevel; base++) {
            const flat = new model.Document(1, 1); flat.vertices.fill(base); flat.paint(0, 0, surface);
            const original = flat.snapshot(), history = flat.history.length;
            assert.equal(flat.paintLowerSurface(0, 0, target), surface !== target);
            assert.deepEqual(flat.cells[0], { surface: target, halfWaterCorner: 0 });
            assert.deepEqual(Array.from(flat.vertices), original.vertices);
            assert.equal(flat.history.length, history + (surface !== target));
            if (surface !== target) { assert(flat.undo()); assert.deepEqual(flat.snapshot(), original); assert(flat.redo()); }
            for (let secondary = 0; secondary < model.TERRAIN_SURFACES.length; secondary++) {
                if (secondary === surface) continue;
                for (let direction = 1; direction <= corners.length; direction++) {
                    const mixed = new model.Document(1, 1); mixed.vertices.fill(base); mixed.paint(0, 0, surface, direction, secondary);
                    const before = mixed.snapshot();
                    assert.equal(mixed.paintLowerSurface(0, 0, target), true);
                    assert.deepEqual(mixed.cells[0], { surface: target, halfWaterCorner: 0 });
                    assert.deepEqual(Array.from(mixed.vertices), before.vertices);
                    assert.deepEqual(model.decodeCstrk(model.encodeCstrk(mixed)).snapshot(), mixed.snapshot());
                    assert(mixed.undo()); assert.deepEqual(mixed.snapshot(), before);
                }
            }
            if (base === maximumLevel) continue;
            for (let high = 0; high < corners.length; high++) {
                const doc = new model.Document(1, 1), [x, y] = corners[high];
                doc.vertices.fill(base); doc.raise(x, y); doc.paint(0, 0, surface);
                const before = doc.snapshot(), steps = doc.history.length;
                assert.equal(doc.paintLowerSurface(0, 0, target), surface !== target);
                const expected = surface === target ? { surface, halfWaterCorner: 0 }
                    : { surface, halfWaterCorner: (high + 2) % corners.length + 1 };
                if (surface !== target && target !== water) expected.cornerSurface = target;
                assert.deepEqual(doc.cells[0], expected);
                assert.deepEqual(Array.from(doc.vertices), before.vertices);
                assert.deepEqual(doc.objects, before.objects);
                assert.equal(doc.history.length, steps + (surface !== target));
                const after = doc.snapshot();
                assert.deepEqual(model.decodeCstrk(model.encodeCstrk(doc)).snapshot(), after);
                if (surface !== target) {
                    assert(doc.undo()); assert.deepEqual(doc.snapshot(), before);
                    assert(doc.redo()); assert.deepEqual(doc.snapshot(), after);
                }
            }
        }
    }
});
test('lower surface preserves both aligned hill representations at every transition', () => {
    const water = model.DEFAULT_CORNER_SURFACE, maximumLevel = 4, corners = [[0, 0], [1, 0], [1, 1], [0, 1]];
    for (let target = 0; target < model.TERRAIN_SURFACES.length; target++) for (let base = 0; base < maximumLevel; base++) {
        for (let high = 0; high < corners.length; high++) {
            const low = (high + 2) % corners.length, [x, y] = corners[high];
            for (let hill = 0; hill < model.TERRAIN_SURFACES.length; hill++) {
                for (let ground = 0; ground < model.TERRAIN_SURFACES.length; ground++) {
                    if (hill === ground) continue;
                    for (const secondaryHill of [false, true]) {
                        const doc = new model.Document(1, 1); doc.vertices.fill(base); doc.raise(x, y);
                        doc.paint(0, 0, secondaryHill ? ground : hill, (secondaryHill ? high : low) + 1, secondaryHill ? hill : ground);
                        doc.setNotes('future'); doc.undo();
                        const before = doc.snapshot(), history = doc.history.length;
                        assert.equal(doc.paintLowerSurface(0, 0, target), ground !== target);
                        const expected = ground === target ? before.cells[0] : hill === target ? { surface: target, halfWaterCorner: 0 }
                            : { surface: hill, halfWaterCorner: low + 1 };
                        if (ground !== target && hill !== target && target !== water) expected.cornerSurface = target;
                        assert.deepEqual(doc.cells[0], expected);
                        assert.deepEqual(Array.from(doc.vertices), before.vertices);
                        assert.equal(doc.history.length, history + (ground !== target));
                        assert.equal(doc.canRedo, ground === target);
                        const after = doc.snapshot();
                        assert.deepEqual(model.decodeCstrk(model.encodeCstrk(doc)).snapshot(), after);
                        if (ground !== target) {
                            assert(doc.undo()); assert.deepEqual(doc.snapshot(), before);
                            assert(doc.redo()); assert.deepEqual(doc.snapshot(), after);
                        }
                    }
                }
            }
        }
    }
});
test('lower surface rejects crossing hill materials atomically rather than repainting either material', () => {
    const maximumLevel = 4, corners = [[0, 0], [1, 0], [1, 1], [0, 1]];
    for (let target = 0; target < model.TERRAIN_SURFACES.length; target++) for (let base = 0; base < maximumLevel; base++) {
        for (let high = 0; high < corners.length; high++) for (const offset of [1, 3]) {
            for (let primary = 0; primary < model.TERRAIN_SURFACES.length; primary++) {
                for (let secondary = 0; secondary < model.TERRAIN_SURFACES.length; secondary++) {
                    if (primary === secondary) continue;
                    const doc = new model.Document(1, 1), [x, y] = corners[high];
                    doc.vertices.fill(base); doc.raise(x, y); doc.paint(0, 0, primary, (high + offset) % corners.length + 1, secondary);
                    doc.setNotes('future'); doc.undo();
                    const before = doc.snapshot(), history = doc.history.length, saved = model.encodeCstrk(doc);
                    assert.throws(() => doc.paintLowerSurface(0, 0, target), /cannot preserve both hill materials/);
                    assert.deepEqual(doc.snapshot(), before); assert.equal(doc.history.length, history);
                    assert.equal(doc.canRedo, true); assert.equal(doc.pending, null);
                    assert.deepEqual(model.encodeCstrk(doc), saved);
                }
            }
        }
    }
});
test('lower surface validates materials and skips unsupported terrain without consuming redo', () => {
    const maximumLevel = 4, cornerCount = 4, profileCount = 1 << cornerCount;
    for (let target = 0; target < model.TERRAIN_SURFACES.length; target++) {
        for (let base = 0; base <= maximumLevel; base++) for (let mask = 0; mask < profileCount - 1; mask++) {
            const heights = Array.from({ length: cornerCount }, (_, corner) => base + !!(mask & (1 << corner)));
            if (!model.terrainProfile(heights) || (mask === 0 || heights.filter(height => height > base).length === 1)) continue;
            const doc = new model.Document(1, 1);
            doc.vertices.set([heights[0], heights[1], heights[3], heights[2]]);
            doc.paint(0, 0, 2, 2, 1); doc.setNotes('future'); doc.undo();
            const before = doc.snapshot(), history = doc.history.length, saved = model.encodeCstrk(doc);
            assert.equal(doc.paintLowerSurface(0, 0, target), false);
            assert.deepEqual(doc.snapshot(), before); assert.equal(doc.history.length, history);
            assert.equal(doc.canRedo, true); assert.equal(doc.pending, null);
            assert.deepEqual(model.encodeCstrk(doc), saved);
        }
        for (let level = 0; level <= maximumLevel; level++) {
            const flat = new model.Document(1, 1); flat.vertices.fill(level); flat.paintLowerSurface(0, 0, target);
            flat.setNotes('future'); flat.undo();
            const before = flat.snapshot(), history = flat.history.length;
            assert.equal(flat.paintLowerSurface(0, 0, target), false);
            assert.deepEqual(flat.snapshot(), before); assert.equal(flat.history.length, history);
            assert.equal(flat.canRedo, true);
            for (const [x, y] of [[-1, 0], [0, -1], [1, 0], [0, 1]]) assert.throws(() => flat.paintLowerSurface(x, y, target), /tile/);
            for (const invalid of [-1, model.TERRAIN_SURFACES.length, 1.5, null, undefined, '2', NaN]) {
                assert.throws(() => flat.paintLowerSurface(0, 0, invalid), /terrain surface/);
                flat.vertices.fill(maximumLevel);
                assert.throws(() => flat.paintLowerSurface(0, 0, invalid), /terrain surface/);
                flat.vertices.fill(level);
            }
            assert.deepEqual(flat.snapshot(), before); assert.equal(flat.canRedo, true);
        }
    }
});
test('lower surface drag edits undo together and preserve terrain geometry and track objects', () => {
    const maximumLevel = 4;
    for (let target = 0; target < model.TERRAIN_SURFACES.length; target++) for (let base = 0; base < maximumLevel; base++) {
        const doc = new model.Document(4, 4); doc.vertices.fill(base); doc.raise(2, 2); doc.place(4, 0, 0);
        const originalSurface = (target + 1) % model.TERRAIN_SURFACES.length;
        for (let y = 0; y < doc.height; y++) for (let x = 0; x < doc.width; x++) doc.paint(x, y, originalSurface);
        doc.setNotes('Keep the hills and roads'); doc.setHorizon(2);
        const before = doc.snapshot(), history = doc.history.length;
        doc.beginTransaction('Terrain stroke');
        for (const [x, y] of [[1, 1], [2, 1], [2, 2], [1, 2], [0, 0], [3, 3]]) {
            assert.equal(doc.paintLowerSurface(x, y, target), true);
            assert.equal(doc.paintLowerSurface(x, y, target), false);
        }
        assert.equal(doc.endTransaction(), true); assert.equal(doc.history.length, history + 1);
        const after = doc.snapshot();
        assert.deepEqual(after.vertices, before.vertices); assert.deepEqual(after.objects, before.objects);
        assert.equal(after.notes, before.notes); assert.equal(after.horizon, before.horizon);
        assert.deepEqual(model.decodeCstrk(model.encodeCstrk(doc)).snapshot(), after);
        assert(doc.undo()); assert.deepEqual(doc.snapshot(), before);
        assert(doc.redo()); assert.deepEqual(doc.snapshot(), after);
        doc.setNotes('future'); doc.undo();
        doc.beginTransaction('Terrain stroke'); doc.paintLowerSurface(0, 0, target); doc.paintLowerSurface(1, 1, target);
        assert.equal(doc.endTransaction(), false); assert.equal(doc.canRedo, true);
        doc.paint(2, 2, 2, 2, 1);
        const mixed = doc.snapshot(), mixedHistory = doc.history.length;
        doc.beginTransaction('Terrain stroke'); doc.paintLowerSurface(3, 0, target);
        const earlierTile = doc.snapshot();
        assert.throws(() => doc.paintLowerSurface(2, 2, target), /cannot preserve both hill materials/);
        assert.deepEqual(doc.snapshot(), earlierTile);
        doc.cancelTransaction(); assert.deepEqual(doc.snapshot(), mixed);
        assert.equal(doc.history.length, mixedHistory);
    }
});
test('Shore rejects matching or invalid materials atomically and in stored tracks', () => {
    const water = model.DEFAULT_CORNER_SURFACE;
    const headerSize = 32;
    const doc = new model.Document(2, 2);
    doc.setNotes('future'); doc.undo();
    const before = doc.snapshot();
    for (let surface = 0; surface < model.TERRAIN_SURFACES.length; surface++) {
        assert.throws(() => doc.paint(0, 0, surface, 1, surface), /different surfaces/);
        assert.deepEqual(doc.snapshot(), before); assert.equal(doc.canRedo, true);
    }
    for (const invalid of [-1, model.TERRAIN_SURFACES.length, 1.5, null]) {
        assert.throws(() => doc.paint(0, 0, 0, 1, invalid), /corner surface/);
        assert.deepEqual(doc.snapshot(), before); assert.equal(doc.canRedo, true);
    }
    for (let direction = 1; direction <= 4; direction++) {
        for (let surface = 0; surface < model.TERRAIN_SURFACES.length; surface++) {
            doc.cells[0] = { surface, halfWaterCorner: direction, cornerSurface: surface };
            assert.throws(() => model.encodeCstrk(doc), /terrain/);
        }
        doc.cells[0] = { surface: 0, halfWaterCorner: direction };
        const encoded = model.encodeCstrk(doc), cornerByte = headerSize + doc.vertices.length + 1;
        assert.equal(encoded[cornerByte], direction); assert.equal(encoded[8], model.VERSION);
        assert.deepEqual(model.encodeCstrk(model.decodeCstrk(encoded)), encoded);
        const matching = encoded.slice(); matching[cornerByte - 1] = water;
        assert.throws(() => model.decodeCstrk(matching), /terrain/);
        for (const badCorner of [5, 8, 32, 255]) {
            const corrupt = encoded.slice(); corrupt[cornerByte] = badCorner;
            assert.throws(() => model.decodeCstrk(corrupt), /terrain/);
        }
    }
    doc.cells[0] = { surface: water, halfWaterCorner: 1 };
    assert.throws(() => model.encodeCstrk(doc), /terrain/);
});
test('CSTRK horizons round-trip in the current format without changing terrain or notes', () => {
    const doc = new model.Document(4, 4); doc.place(4, 1, 1); doc.setNotes('Horizon metadata');
    const original = model.encodeCstrk(doc);
    assert.equal(model.VERSION, 4); assert.equal(doc.version, model.VERSION);
    assert.equal(model.decodeCstrk(original).horizon, 0);
    for (let horizon = 0; horizon < model.HORIZONS.length; horizon++) {
        doc.setHorizon(horizon);
        const bytes = model.encodeCstrk(doc), view = new DataView(bytes.buffer);
        assert.equal(view.getUint16(8, true), model.VERSION);
        assert.equal(view.getUint32(12, true), horizon);
        assert.equal(bytes.length, original.length);
        assert.deepEqual(bytes.subarray(32), original.subarray(32));
        assert.deepEqual(model.decodeCstrk(bytes).snapshot(), doc.snapshot());
    }
    const invalidHorizon = original.slice(), header = new DataView(invalidHorizon.buffer);
    for (const invalid of [model.HORIZONS.length, 256, 0xffffffff]) {
        header.setUint32(12, invalid, true);
        assert.throws(() => model.decodeCstrk(invalidHorizon), /horizon/);
    }
    doc.horizon = model.HORIZONS.length;
    assert.throws(() => model.encodeCstrk(doc), /horizon/);
});
test('v3 tracks upgrade with their original overpass surfaces and reserve the new field until v4', () => {
    const source = new model.Document(4, 4); source.place(101, 1, 1, 2);
    const bytes = model.encodeCstrk(source), objectOffset = 32 + source.vertices.length + source.cells.length * 2;
    new DataView(bytes.buffer).setUint16(8, 3, true);
    const loaded = model.decodeCstrk(bytes);
    assert.equal(loaded.version, model.VERSION);
    assert.equal(loaded.objects[0].surface, 2); assert.equal(loaded.objects[0].lowerSurface, 2);
    assert.equal(new DataView(model.encodeCstrk(loaded).buffer).getUint16(8, true), model.VERSION);
    loaded.objects[0].lowerSurface = 1;
    assert.deepEqual(model.decodeCstrk(model.encodeCstrk(loaded)).snapshot(), loaded.snapshot());
    loaded.version = 3;
    assert.throws(() => model.encodeCstrk(loaded), /require.*version v4/);
    loaded.objects[0].lowerSurface = loaded.objects[0].surface;
    const legacyExport = model.encodeCstrk(loaded);
    assert.equal(new DataView(legacyExport.buffer).getUint16(8, true), 3);
    assert.equal(legacyExport[objectOffset + 12], 0);
    assert.equal(model.decodeCstrk(legacyExport).objects[0].lowerSurface, loaded.objects[0].surface);
    loaded.objects[0].lowerSurface = 0;
    assert.throws(() => model.encodeCstrk(loaded), /require.*version v4/);
    for (const id of [101, 102]) for (const surface of [0, 1, 2]) {
        const doc = new model.Document(4, 4); doc.place(id, 1, 1, surface);
        const legacy = model.encodeCstrk(doc); new DataView(legacy.buffer).setUint16(8, 3, true);
        const upgraded = model.decodeCstrk(legacy);
        assert.equal(upgraded.version, model.VERSION);
        assert.equal(upgraded.objects[0].surface, surface);
        assert.equal(upgraded.objects[0].lowerSurface, surface);
        assert.deepEqual(model.decodeCstrk(model.encodeCstrk(upgraded)).snapshot(), upgraded.snapshot());
        doc.version = 3; doc.objects[0].lowerSurface = surface;
        const representable = model.encodeCstrk(doc), record = 32 + doc.vertices.length + doc.cells.length * 2;
        assert.equal(representable[record + 12], 0);
        assert.equal(model.decodeCstrk(representable).objects[0].lowerSurface, surface);
    }
    for (const offset of [12, 13, 14, 15]) {
        const invalid = bytes.slice(); invalid[objectOffset + offset] = 1;
        assert.throws(() => model.decodeCstrk(invalid), /element flags/);
    }
    for (const version of [model.VERSION, 15, 101]) {
        const valid = bytes.slice(); new DataView(valid.buffer).setUint16(8, version, true);
        valid[objectOffset + 12] = 2;
        const doc = model.decodeCstrk(valid);
        assert.equal(doc.version, version); assert.equal(doc.objects[0].lowerSurface, 2);
        assert.deepEqual(model.encodeCstrk(doc), valid);
        for (const offset of [13, 14, 15]) {
            const invalid = valid.slice(); invalid[objectOffset + offset] = 1;
            assert.throws(() => model.decodeCstrk(invalid), /element flags/);
        }
        const invalidSurface = valid.slice(); invalidSurface[objectOffset + 12] = 3;
        assert.throws(() => model.decodeCstrk(invalidSurface), /lower road surface/);
        const invalidPiece = valid.slice(); new DataView(invalidPiece.buffer).setUint16(objectOffset, 4, true);
        assert.throws(() => model.decodeCstrk(invalidPiece), /Only overpasses/);
    }
});
test('CSTRK version metadata preserves compatible newer revisions across loading and editing', () => {
    const doc = new model.Document(4, 4); doc.setHorizon(model.HORIZONS.indexOf('Country'));
    const original = model.encodeCstrk(doc);
    for (const version of [model.VERSION, 15, 101, 0xffff]) {
        const bytes = original.slice(); new DataView(bytes.buffer).setUint16(8, version, true);
        const loaded = model.decodeCstrk(bytes);
        assert.equal(loaded.version, version); assert.equal(loaded.horizon, doc.horizon);
        assert.deepEqual(model.encodeCstrk(loaded), bytes);
        loaded.setNotes('Saved with version ' + version);
        assert(loaded.undo()); assert.equal(loaded.version, version);
        assert.deepEqual(model.encodeCstrk(loaded), bytes);
        assert(loaded.redo()); assert.equal(loaded.version, version);
        const saved = model.encodeCstrk(loaded);
        assert.equal(new DataView(saved.buffer).getUint16(8, true), version);
        assert.equal(model.decodeCstrk(saved).notes, loaded.notes);
        loaded.paint(0, 0, 0, 1, 1);
        assert.equal(loaded.version, version);
        const painted = model.encodeCstrk(loaded);
        assert.equal(new DataView(painted.buffer).getUint16(8, true), version);
        assert.deepEqual(model.decodeCstrk(painted).snapshot(), loaded.snapshot());
    }
    for (const version of [1, 2]) {
        const bytes = original.slice(); new DataView(bytes.buffer).setUint16(8, version, true);
        assert.throws(() => model.decodeCstrk(bytes), /Unsupported older.*version v[12]/);
        doc.version = version;
        assert.throws(() => model.encodeCstrk(doc), /Unsupported older.*version v[12]/);
    }
    const zeroVersion = original.slice(); new DataView(zeroVersion.buffer).setUint16(8, 0, true);
    assert.throws(() => model.decodeCstrk(zeroVersion), /version/);
    for (const invalid of [0, -1, 1.5, 0x10000]) {
        doc.version = invalid; assert.throws(() => model.encodeCstrk(doc), /version/);
    }
    const wrongHeader = original.slice(); new DataView(wrongHeader.buffer).setUint16(10, 33, true);
    assert.throws(() => model.decodeCstrk(wrongHeader), /header size/);
});
test('CSTRK round-trip preserves 90x90 terrain, surfaces, overlaps and notes', () => {
    const doc = new model.Document(); doc.raise(60, 60); doc.paint(59, 59, 2, 3);
    doc.place(11, 0, 0, 1); doc.place(151, 0, 1); doc.setNotes('Chocolate Stunts\r\nASCII\tmetadata');
    const data = model.encodeCstrk(doc), loaded = model.decodeCstrk(data);
    assert.deepEqual(loaded.snapshot(), doc.snapshot()); assert.equal(loaded.canUndo, false);
    assert.equal(data[8], model.VERSION); assert.equal(data[16], 90); assert.equal(new DataView(data.buffer).getUint32(28, true), data.length - 32);
    assert.throws(() => model.decodeCstrk(data.subarray(0, data.length - 1)), /truncated/);
    assert.throws(() => model.decodeCstrk(Uint8Array.from([...data, 0])), /truncated/);
    const invalid = data.slice(); invalid[32] = 5; assert.throws(() => model.decodeCstrk(invalid), /four levels|one level/);
    const horizon = data.slice(); horizon[12] = model.HORIZONS.length; assert.throws(() => model.decodeCstrk(horizon), /horizon/);
    const forged = data.slice(), objectOffset = 32 + doc.vertices.length + doc.cells.length * 2;
    forged[objectOffset + 8] = 15; assert.throws(() => model.decodeCstrk(forged), /catalog geometry|overlap/);
    const internal = data.slice(); internal[objectOffset] = 182; assert.throws(() => model.decodeCstrk(internal), /Unknown track element/);
});
test('tile artwork keeps road width and connectors when fill images are cropped', () => {
    const tileSize = 16, interiorSize = tileSize - 1, roadColor = 8, clearMask = 15;
    const make = (width, height, maskWidth, maskHeight) => ({ width, height,
        pixels: new Uint8Array(width * height),
        mask: { width: maskWidth, height: maskHeight, pixels: new Uint8Array(maskWidth * maskHeight).fill(clearMask) } });
    const vertical = make(tileSize, interiorSize, tileSize, tileSize);
    const horizontal = make(tileSize, 10, tileSize, tileSize);
    for (let y = 0; y < interiorSize; y++) for (let x = 6; x <= 10; x++) vertical.pixels[y * tileSize + x] = roadColor;
    for (let y = 5; y <= 9; y++) for (let x = 1; x < tileSize; x++) horizontal.pixels[y * tileSize + x] = roadColor;
    const up = resources.composeTileBitmap(vertical), across = resources.composeTileBitmap(horizontal);
    assert.deepEqual([up.width, up.height, across.width, across.height], [interiorSize, interiorSize, interiorSize, interiorSize]);
    for (let y = 0; y < interiorSize; y++) for (let x = 0; x < interiorSize; x++) {
        const north = y * interiorSize + x, east = x * interiorSize + interiorSize - 1 - y;
        assert.equal(up.pixels[north], across.pixels[east]);
        assert.equal(up.alpha[north], across.alpha[east]);
    }
    // A tunnel mask can retain black pixels below its cropped fill image.
    horizontal.mask.pixels[14 * tileSize + 7] = 0;
    const padded = resources.composeTileBitmap(horizontal), index = 14 * interiorSize + 6;
    assert.equal(padded.pixels[index], 0); assert.equal(padded.alpha[index], 255);
    assert.equal(padded.alpha[0], 0);
});
test('multi-tile artwork removes internal grid separators without breaking the road', () => {
    const tileSize = 16, interiorSize = tileSize - 1, roadColor = 8;
    const bitmap = { width: tileSize * 2, height: 10, pixels: new Uint8Array(tileSize * 2 * 10),
        mask: { width: tileSize * 2, height: tileSize, pixels: new Uint8Array(tileSize * tileSize * 2).fill(15) } };
    for (let y = 5; y <= 9; y++) for (let x = 1; x < bitmap.width; x++)
        if (x % tileSize) bitmap.pixels[y * bitmap.width + x] = roadColor;
    const result = resources.composeTileBitmap(bitmap);
    assert.deepEqual([result.width, result.height], [interiorSize * 2, interiorSize]);
    for (let x = 0; x < result.width; x++) {
        assert.equal(result.pixels[7 * result.width + x], roadColor);
        assert.equal(result.alpha[7 * result.width + x], 255);
        assert.equal(result.alpha[x], 0);
    }
    assert.throws(() => resources.composeTileBitmap({ ...bitmap, mask: { ...bitmap.mask, width: 15 } }), /artwork/);
    assert.throws(() => resources.composeTileBitmap({ ...bitmap, pixels: new Uint8Array(1) }), /artwork/);
});
test('RLE byte runs and canonical VLE decode with bounds checks', () => {
    assert.deepEqual(resources.decompress(Uint8Array.from([1, 4, 0, 0, 0, 0, 0, 0, 129, 255, 255, 4, 65])), Uint8Array.from([65, 65, 65, 65]));
    assert.deepEqual(resources.decompress(Uint8Array.from([2, 4, 0, 0, 1, 2, 65, 66, 0b01100000])), Uint8Array.from([65, 66, 66, 65]));
    assert.throws(() => resources.decompress(Uint8Array.from([2, 4, 0, 0, 1, 2, 65, 66])), /Truncated/);
});
test('planar bitmap expansion preserves high bits and channel mapping', () => {
    const shape = new Uint8Array(18); shape[0] = 1; shape[2] = 2; shape[12] = 2; shape[16] = 128; shape[17] = 1;
    const bitmap = resources.decodeShape(shape, true); assert.equal(bitmap.width, 8); assert.equal(bitmap.height, 2);
    assert.equal(bitmap.pixels[0], 2); assert.equal(bitmap.pixels[15], 2); assert.equal(bitmap.pixels[1], 0);
});

(async () => {
    // Optional local smoke test. Original copyrighted game resources are never fixtures.
    const directory = path.resolve(__dirname, '../../stunts');
    if (fs.existsSync(path.join(directory, 'TEDIT.PRE')) || fs.existsSync(path.join(directory, 'TEDIT.RES'))) {
        const files = new Map(fs.readdirSync(directory).filter(name => fs.statSync(path.join(directory, name)).isFile()).map(name => [name, fs.readFileSync(path.join(directory, name))]));
        const decoded = await resources.load(files);
        assert.equal(decoded.icons.size, 186); assert.equal(decoded.terrain.size, 19); assert.equal(decoded.palette.length, 768);
        assert(decoded.groups.find(g => g.id === 7).tiles.includes(60));
        for (const entry of model.catalog) {
            const tile = resources.composeTileBitmap(decoded.icons.get(entry.id));
            assert.equal(tile.width, entry.width * 15); assert.equal(tile.height, entry.height * 15);
            assert.equal(tile.alpha.length, tile.pixels.length);
        }
        const road = resources.composeTileBitmap(decoded.icons.get(4));
        assert.deepEqual(Array.from(road.alpha.slice(0, road.width)), Array.from(road.alpha.slice(-road.width)));
        const roadAcross = resources.composeTileBitmap(decoded.icons.get(5));
        for (let y = 0; y < road.height; y++) for (let x = 0; x < road.width; x++) {
            const original = y * road.width + x, rotated = x * road.width + road.height - 1 - y;
            assert.equal(road.pixels[original], roadAcross.pixels[rotated]);
            assert.equal(road.alpha[original], roadAcross.alpha[rotated]);
        }
        // Route metadata supplies every connector, including both levels of an overpass.
        const routeData = require('../../src/restunts/platform/wasm/track-editor-route-data.js');
        const exitSteps = [[0, 0, 0], [0, -1, 0], [0, 1, 2], [1, 0, 1], [-1, 0, 3],
            [1, -1, 0], [-1, 1, 3], [1, 1, 1], [2, 0, 1], [2, 1, 1],
            [1, 1, 2], [0, 2, 2], [1, 2, 2]];
        const tileSize = road.width, connectors = [];
        const roadWidth = Array.from(road.alpha.slice(0, tileSize)).filter(Boolean).length;
        for (const entry of model.catalog.filter(piece => !piece.decoration && piece.legacySurface <= 0)) {
            const routeIndex = routeData.objects[entry.id].routeIndex;
            if (routeIndex === null) continue;
            const display = model.pieceDisplayVariant(entry);
            let tile = resources.composeTileBitmap(decoded.icons.get(display.piece.id));
            for (let turn = 0; turn < display.rotation; turn++) {
                const rotated = { width: tile.height, height: tile.width, alpha: new Uint8Array(tile.alpha.length) };
                for (let y = 0; y < tile.height; y++) for (let x = 0; x < tile.width; x++)
                    rotated.alpha[x * rotated.width + tile.height - 1 - y] = tile.alpha[y * tile.width + x];
                tile = rotated;
            }
            const count = routeData.blocks[routeIndex].count;
            for (const block of routeData.blocks.slice(routeIndex, routeIndex + count)) {
                for (const end of ['entry', 'exit']) {
                    const [dx, dy, side] = exitSteps[block[end]];
                    const cellX = dx + (side === 1 ? -1 : side === 3 ? 1 : 0);
                    const cellY = dy + (side === 0 ? 1 : side === 2 ? -1 : 0);
                    const alpha = Array.from({ length: tileSize }, (_, offset) => {
                        const x = side === 1 ? (cellX + 1) * tileSize - 1 :
                            side === 3 ? cellX * tileSize : cellX * tileSize + offset;
                        const y = side === 0 ? cellY * tileSize :
                            side === 2 ? (cellY + 1) * tileSize - 1 : cellY * tileSize + offset;
                        assert(x >= 0 && x < tile.width && y >= 0 && y < tile.height);
                        return tile.alpha[y * tile.width + x];
                    });
                    connectors.push({ id: entry.id, name: entry.name, side, type: block[end + 'Type'], alpha });
                }
            }
        }
        let pairCount = 0;
        for (const first of connectors) for (const second of connectors) {
            if (first.type !== second.type || (first.side + 2) % 4 !== second.side) continue;
            const overlap = first.alpha.filter((alpha, offset) => alpha && second.alpha[offset]).length;
            assert(overlap >= roadWidth, first.name + ' (' + first.id + ') and ' + second.name +
                ' (' + second.id + ') must share a complete connector');
            pairCount++;
        }
        assert(pairCount > 0);
        console.log('PASS all ' + pairCount + ' compatible native artwork connector pairs align');
        console.log('PASS original local editor resources decode without bundling game data');
    }
})().catch(error => { console.error(error); process.exitCode = 1; });

// Keep independent overpass preview colors covered by the standard editor suite.
require('./test-overpass-icons.js');
