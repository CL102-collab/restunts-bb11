#!/usr/bin/env node
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const model = require('../../src/restunts/platform/wasm/track-editor-model.js');
const resources = require('../../src/restunts/platform/wasm/track-editor-resources.js');

function test(name, callback) { callback(); console.log('PASS ' + name); }
test('library combines rotated alternatives while preserving distinct shapes and groups', () => {
    assert.deepEqual(model.groups.map(g => model.library.filter(p => p.group === g.id).length), [7, 8, 4, 2, 4, 7, 2, 10]);
    const families = new Set(model.library.map(p => p.group + ':' + p.physical));
    assert.equal(families.size, model.library.length);
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
    assert.equal(doc.vertices[43 * 91 + 43], 0);
    assert.deepEqual(doc.validate(), []);
    doc.raise(45, 45, -1);
    assert.equal(doc.vertices[45 * 91 + 45], 3);
});
test('Level applies each selected surface to the plateau and its generated slopes', () => {
    for (const surface of [0, 1, 2, 3]) for (const height of [1, 2, 3, 4]) {
        const doc = new model.Document(12, 12);
        doc.paint(0, 0, (surface + 1) % 4, 2);
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
        for (let vy = 0; vy <= 8; vy++) for (let vx = 0; vx <= 8; vx++) doc.vertices[vy * 9 + vx] = Math.min(4, vx + vy);
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
    // The footprint can stay planar while the required extra slope run is lost.
    const slope = new model.Document(4, 4);
    for (let y = 0; y <= 4; y++) for (let x = 0; x <= 4; x++) slope.vertices[y * 5 + x] = Math.min(x, 2);
    slope.place(115, 0, 1);
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
test('large elements require one rigid plane across their entire footprint', () => {
    const doc = new model.Document(8, 8);
    // A two-tile slope reaches the second hill level in the east direction.
    for (let y = 0; y <= 8; y++) for (let x = 0; x <= 8; x++) doc.vertices[y * 9 + x] = Math.min(x, 4);
    const object = doc.place(11, 0, 2, 2); assert.equal(object.slopeX, 1); assert.equal(object.slopeY, 0);
    assert.throws(() => doc.place(11, 3, 4), /planar/);
    assert.deepEqual(doc.validate(), []);
});
test('rigid diagonal planes allow one level per horizontal and vertical edge', () => {
    const doc = new model.Document(2, 2);
    for (let y = 0; y <= 2; y++) for (let x = 0; x <= 2; x++) doc.vertices[y * 3 + x] = x + y;
    const object = doc.place(11, 0, 0);
    assert.equal(object.slopeX, 1); assert.equal(object.slopeY, 1);
    assert.deepEqual(model.decodeCstrk(model.encodeCstrk(doc)).snapshot(), doc.snapshot());
});
test('sloped slaloms and tunnels require two continuous tiles in every sloping direction', () => {
    for (const id of [66, 115]) {
        const short = new model.Document(4, 4);
        for (let y = 0; y <= 4; y++) for (let x = 0; x <= 4; x++) short.vertices[y * 5 + x] = x ? 1 : 0;
        assert.throws(() => short.place(id, 0, 1), /two tiles/);
        const long = new model.Document(4, 4);
        for (let y = 0; y <= 4; y++) for (let x = 0; x <= 4; x++) long.vertices[y * 5 + x] = Math.min(x, 2);
        assert.doesNotThrow(() => long.place(id, 0, 1));
        assert.doesNotThrow(() => long.place(id, 1, 1)); // Support may extend behind the piece.
    }
});
test('expanded road surfaces and incompatible stunt surfaces', () => {
    for (const id of [40, 49, 53, 60, 66, 75, 85, 87, 99, 105, 109, 115, 117, 121]) {
        for (const surface of [0, 1, 2]) assert.doesNotThrow(() => new model.Document(4, 4).place(id, 0, 0, surface));
    }
    assert.throws(() => new model.Document().place(64, 0, 0, 1), /does not support/);
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
test('CSTRK horizons round-trip in the current format without changing terrain or notes', () => {
    const doc = new model.Document(4, 4); doc.place(4, 1, 1); doc.setNotes('Horizon metadata');
    const original = model.encodeCstrk(doc);
    assert.equal(model.VERSION, 1); assert.equal(doc.version, model.VERSION);
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
    assert.equal(data[8], 1); assert.equal(data[16], 90); assert.equal(new DataView(data.buffer).getUint32(28, true), data.length - 32);
    assert.throws(() => model.decodeCstrk(data.subarray(0, data.length - 1)), /truncated/);
    assert.throws(() => model.decodeCstrk(Uint8Array.from([...data, 0])), /truncated/);
    const invalid = data.slice(); invalid[32] = 5; assert.throws(() => model.decodeCstrk(invalid), /four levels|one level/);
    const horizon = data.slice(); horizon[12] = model.HORIZONS.length; assert.throws(() => model.decodeCstrk(horizon), /horizon/);
    const forged = data.slice(), objectOffset = 32 + doc.vertices.length + doc.cells.length * 2;
    forged[objectOffset + 8] = 15; assert.throws(() => model.decodeCstrk(forged), /catalog geometry|overlap/);
    const internal = data.slice(); internal[objectOffset] = 182; assert.throws(() => model.decodeCstrk(internal), /Unknown track element/);
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
        console.log('PASS original local editor resources decode without bundling game data');
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
