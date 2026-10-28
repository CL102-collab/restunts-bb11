#!/usr/bin/env node
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const model = require('../../src/restunts/platform/wasm/track-editor-model.js');
const resources = require('../../src/restunts/platform/wasm/track-editor-resources.js');

function test(name, callback) { callback(); console.log('PASS ' + name); }
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
    for (const id of [40, 49, 53, 60, 66, 75, 87, 99, 105, 109, 115]) {
        for (const surface of [0, 1, 2]) assert.doesNotThrow(() => new model.Document(4, 4).place(id, 0, 0, surface));
    }
    assert.throws(() => new model.Document().place(64, 0, 0, 1), /does not support/);
});
test('strict ASCII notes enforce the 8000-character bound', () => {
    const doc = new model.Document(); doc.setNotes('A'.repeat(8000));
    assert.throws(() => doc.setNotes('A'.repeat(8001)), /8000/);
    for (const invalid of ['café', '\u0000', '\u007f', 'track 🏁']) assert.throws(() => doc.setNotes(invalid), /ASCII/);
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
    const flags = data.slice(); flags[12] = 1; assert.throws(() => model.decodeCstrk(flags), /version/);
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
