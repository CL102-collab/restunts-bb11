#!/usr/bin/env node
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const { spawnSync } = require('node:child_process');
const model = require('../../src/restunts/platform/wasm/track-editor-model.js');
const validation = require('../../src/restunts/platform/wasm/track-editor-validation.js');
const fixtures = [];
function check(name, doc, status) {
    assert.deepEqual(doc.validate(), [], name + ': storage validation');
    const before = doc.snapshot(), result = validation.validateRoute(doc);
    assert.deepEqual(doc.snapshot(), before, name + ': checking must not edit the track');
    assert.equal(doc.history.length, 0, name + ': checking must not create undo steps');
    if (status !== undefined) assert.equal(result.status, status, name + ': ' + result.message);
    fixtures.push({ name, doc, result });
    return result;
}
function clearHistory(doc) { doc.history = []; doc.future = []; return doc; }
function put(doc, ...args) { doc.objects.push(doc.placement(...args)); }
function replace(doc, x, y, id, rotation = 0, surface = 0) {
    doc.erase(x, y);
    if (id) doc.place(id, x, y, surface, rotation);
}
function rectangle(width = 40, height = 18) {
    const doc = new model.Document(width, height), left = 2, right = width - 4, top = 4, bottom = height - 4;
    for (const [id, x, y] of [[6, left, top], [7, right, top], [8, left, bottom], [9, right, bottom]]) put(doc, id, x, y);
    for (let x = left + 1; x < right; x++) {
        put(doc, x === right - 6 ? 181 : 5, x, top);
        put(doc, 5, x, bottom);
    }
    for (let y = top + 1; y < bottom; y++) { put(doc, 4, left, y); put(doc, 4, right, y); }
    return clearHistory(doc);
}
function rotate(source) {
    const doc = new model.Document(source.height, source.width);
    for (let y = 0; y <= source.height; y++) for (let x = 0; x <= source.width; x++)
        doc.vertices[x * (doc.width + 1) + source.height - y] = source.vertices[y * (source.width + 1) + x];
    for (let y = 0; y < source.height; y++) for (let x = 0; x < source.width; x++) {
        const cell = source.cells[y * source.width + x];
        doc.cells[x * doc.width + source.height - y - 1] = { surface: cell.surface, halfWaterCorner: cell.halfWaterCorner ? cell.halfWaterCorner % 4 + 1 : 0 };
    }
    for (const object of source.objects) put(doc, object.id, source.height - object.y - object.height, object.x, object.surface, (object.rotation + 1) % 4);
    return clearHistory(doc);
}
function chicaneJump(bridge = false, runway = false) {
    const doc = rectangle();
    const offset = runway ? 1 : 0;
    for (let x = 19 + offset; x <= 22 + offset; x++) doc.erase(x, 4);
    doc.place(61, 21 + offset, 4, 2); doc.place(63, 19 + offset, 4, 2);
    replace(doc, 18, 4, 57, 0, 1); replace(doc, 16, 4, 56, 0, 1);
    replace(doc, 17, 4, bridge ? 35 : 157, 0, bridge ? 1 : 0);
    return clearHistory(doc);
}

check('empty map', new model.Document(4, 3), 1);
check('smallest map without start', new model.Document(1, 1), 1);
check('closed rectangular route', rectangle(), 0);
const missing = rectangle(); replace(missing, 25, 4, 0); check('disconnected road', clearHistory(missing), 7);
const starts = rectangle(); starts.place(1, 1, 1); check('multiple start elements', clearHistory(starts), 3);
const mismatch = rectangle(); replace(mismatch, 25, 4, 35); check('ground road meets elevated deck', clearHistory(mismatch), 4);
const noRunway = chicaneJump(), jumpError = check('chicane immediately before jump', noRunway, 9);
assert.equal(jumpError.column, 17); assert.equal(jumpError.row, 4);
assert.match(jumpError.message, /runway/i); assert.match(jumpError.message, /18/); assert.match(jumpError.message, /5/);
check('elevated road bridges the ramp gap', chicaneJump(true), 0);
check('one straight plus takeoff ramp provides the original two-piece runway', chicaneJump(false, true), 0);
// An unfinished but structurally sound track must still save and reopen.
assert.deepEqual(model.decodeCstrk(model.encodeCstrk(noRunway)).snapshot(), noRunway.snapshot());
for (let direction = 0, doc = rectangle(); direction < 4; direction++, doc = rotate(doc)) check('closed route direction ' + direction, doc, 0);
for (let direction = 0, doc = chicaneJump(); direction < 4; direction++, doc = rotate(doc)) check('short runway direction ' + direction, doc, 9);
for (let direction = 0, doc = chicaneJump(true); direction < 4; direction++, doc = rotate(doc)) check('bridged ramps direction ' + direction, doc, 0);
for (const bridged of [false, true]) {
    const doc = chicaneJump(bridged);
    for (let y = 0; y <= doc.height; y++) for (let x = 0; x <= doc.width; x++) doc.vertices[y * (doc.width + 1) + x] = Math.max(0, Math.min(4, x - 19));
    for (const object of doc.objects) {
        const heights = doc.tileHeights(object.x, object.y);
        object.baseHeight = heights[0]; object.slopeX = heights[1] - heights[0]; object.slopeY = heights[3] - heights[0];
    }
    check('chicanes tilted through four hill levels, bridge ' + bridged, doc, bridged ? 0 : 9);
}
const branch = new model.Document(20, 20);
for (let x = 6; x <= 11; x++) { put(branch, 5, x, 5); put(branch, 5, x, 12); }
for (let y = 6; y <= 11; y++) { put(branch, 4, 5, y); put(branch, 4, 12, y); }
for (const [id, x, y] of [[6, 5, 5], [7, 12, 5], [9, 12, 12], [8, 5, 12]]) put(branch, id, x, y);
replace(branch, 5, 6, 1); replace(branch, 5, 8, 75); clearHistory(branch);
for (let direction = 0, doc = branch; direction < 4; direction++, doc = rotate(doc)) {
    const result = check('closed main route with deferred branch ' + direction, doc, 0);
    assert.equal(result.pieces, 29);
    assert(result.route.some(piece => piece.alternate >= 0));
}

// Place every library family/direction in the route. This covers native-ID
// resolution, handed shapes, two-cell entries, connection types and branches.
for (const entry of model.library.filter(piece => !piece.decoration && piece.physical !== 0)) {
    for (let direction = 0; direction < 4; direction++) {
        const doc = rectangle(), { piece, rotation } = model.pieceVariant(entry, direction);
        for (let x = 12; x <= 13; x++) doc.erase(x, 4);
        doc.place(piece.id, 12, 4, 0, rotation);
        check(entry.name + ' direction ' + direction, clearHistory(doc));
    }
}
// Sparse large turns retain their route even when another road or scenery
// shares the anchor in the unclaimed quadrant.
const sparse = new model.Document(90, 90);
sparse.place(13, 40, 40);
for (const [id, x, y] of [[6, 37, 37], [7, 41, 37], [8, 37, 41]]) sparse.place(id, x, y);
for (let x = 38; x <= 40; x++) sparse.place(5, x, 37);
for (let x = 38; x <= 39; x++) sparse.place(5, x, 41);
for (let y = 38; y <= 40; y++) sparse.place(y === 39 ? 1 : 4, 37, y);
sparse.place(4, 41, 38); sparse.place(4, 41, 39);
check('sparse large turn baseline', clearHistory(sparse), 0);
sparse.place(4, 40, 40); check('road shares a sparse turn anchor', clearHistory(sparse), 0);
for (let direction = 0, doc = sparse; direction < 4; direction++, doc = rotate(doc)) check('sparse shared anchor direction ' + direction, doc, 0);

// Deterministic mixtures exercise deferred branches, unmatched entries and
// early-error precedence without treating disconnected scenery as road.
let seed = 0xc57a19;
function random(maximum) { seed ^= seed << 13; seed ^= seed >>> 17; seed ^= seed << 5; return (seed >>> 0) % maximum; }
const single = model.catalog.filter(piece => piece.width === 1 && piece.height === 1);
for (let fixture = 0; fixture < 250; fixture++) {
    const doc = rectangle();
    for (let edit = 0, count = 1 + random(5); edit < count; edit++) {
        const x = 3 + random(30), piece = single[random(single.length)];
        if (x === 30) continue;
        replace(doc, x, 4, piece.id, random(4), piece.surfaces[random(piece.surfaces.length)]);
    }
    check('mixed route ' + fixture, clearHistory(doc));
}
const long = new model.Document(90, 90), path = [[0, 1], [1, 1]];
for (let row = 0; row < 44; row++) {
    const y = 1 + row * 2;
    if (row % 2 === 0) for (let x = 2; x <= 88; x++) path.push([x, y]);
    else for (let x = 88; x >= 2; x--) path.push([x, y]);
    if (row < 43) path.push([row % 2 === 0 ? 88 : 2, y + 1]);
}
path.push([1, 87], [0, 87]); for (let y = 86; y >= 2; y--) path.push([0, y]);
function side(origin, neighbor) { return neighbor[0] > origin[0] ? 1 : neighbor[1] > origin[1] ? 2 : neighbor[0] < origin[0] ? 4 : 8; }
for (let i = 0; i < path.length; i++) {
    const [x, y] = path[i], sides = side(path[i], path[(i + path.length - 1) % path.length]) | side(path[i], path[(i + 1) % path.length]);
    long.objects.push(long.placement(x === 0 && y === 40 ? 1 : ({ 3: 6, 6: 7, 9: 8, 12: 9, 5: 5 })[sides] || 4, x, y));
}
const longResult = check('90 by 90 route beyond the old route limit', clearHistory(long), 0);
assert.equal(longResult.pieces, path.length);

for (const filename of process.argv.slice(3)) check('uploaded ' + filename, clearHistory(model.decodeCstrk(fs.readFileSync(filename))));
if (process.argv[2]) {
    const input = fixtures.map(({ doc }) => Buffer.from(model.encodeCstrk(doc)).toString('hex')).join('\n') + '\n';
    const native = spawnSync(process.argv[2], [], { input, encoding: 'utf8', maxBuffer: 32 * 1024 * 1024 });
    assert.equal(native.status, 0, native.stderr);
    const results = native.stdout.trim().split('\n').map(line => JSON.parse(line));
    assert.equal(results.length, fixtures.length, 'native oracle response count');
    for (let i = 0; i < fixtures.length; i++) {
        const { name, result } = fixtures[i], actual = results[i];
        assert(!actual.loadError, name + ': ' + actual.loadError);
        for (const field of ['status', 'column', 'row', 'pieces', 'route']) assert.deepEqual(result[field], actual[field], name + ': native ' + field);
    }
    console.log('PASS ' + fixtures.length + ' editor routes match native game status, location, pieces and links');
} else console.log('PASS ' + fixtures.length + ' editor route checks (pass the native oracle for differential checks)');
