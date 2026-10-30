#!/usr/bin/env node
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const { spawnSync } = require('node:child_process');
const model = require('../../src/restunts/platform/wasm/track-editor-model.js');
const validation = require('../../src/restunts/platform/wasm/track-editor-validation.js');
const fixtures = [];
const SHORE_CORNER_COUNT = 4;
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
function putImported(doc, ...args) {
    const flat = new model.Document(doc.width, doc.height), object = flat.place(...args);
    const heights = doc.tileHeights(object.x, object.y);
    object.baseHeight = heights[0]; object.slopeX = heights[1] - heights[0]; object.slopeY = heights[3] - heights[0];
    doc.objects.push(object);
}
function replace(doc, x, y, id, rotation = 0, surface = 0, lowerSurface = 0, tunnelSlalom = 0) {
    doc.erase(x, y);
    if (id) doc.place(id, x, y, surface, rotation, lowerSurface, tunnelSlalom);
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
    doc.horizon = source.horizon; doc.version = source.version;
    for (let y = 0; y <= source.height; y++) for (let x = 0; x <= source.width; x++)
        doc.vertices[x * (doc.width + 1) + source.height - y] = source.vertices[y * (source.width + 1) + x];
    for (let y = 0; y < source.height; y++) for (let x = 0; x < source.width; x++) {
        const cell = source.cells[y * source.width + x];
        doc.cells[x * doc.width + source.height - y - 1] = { ...cell, halfWaterCorner: cell.halfWaterCorner ? cell.halfWaterCorner % SHORE_CORNER_COUNT + 1 : 0 };
    }
    for (const object of source.objects) put(doc, object.id, source.height - object.y - object.height, object.x, object.surface, (object.rotation + 1) % 4, object.lowerSurface ?? 0, object.tunnelSlalom ?? 0);
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
for (let primary = 0; primary < model.TERRAIN_SURFACES.length; primary++) {
    for (let secondary = 0; secondary < model.TERRAIN_SURFACES.length; secondary++) {
        if (primary === secondary) continue;
        for (let corner = 1; corner <= SHORE_CORNER_COUNT; corner++) {
            const doc = rectangle(); doc.paint(0, 0, primary, corner, secondary);
            check(model.TERRAIN_SURFACES[primary] + '/' + model.TERRAIN_SURFACES[secondary] +
                ' corner ' + corner + ' retains the route', clearHistory(doc), 0);
        }
    }
}
for (let horizon = 0; horizon < model.HORIZONS.length; horizon++) {
    const doc = rectangle();
    doc.setHorizon(horizon);
    check(model.HORIZONS[horizon] + ' horizon retains the route', clearHistory(doc), 0);
}
const futureTrackVersions = [15, 101];
for (const version of futureTrackVersions) {
    const doc = rectangle();
    doc.version = version;
    doc.setHorizon(model.HORIZONS.indexOf('Country'));
    check('future version ' + version + ' with a chosen horizon retains the route', clearHistory(doc), 0);
}
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
const HILL_ROUTE_MAX_LEVEL = 4, HILL_ROUTE_START_COLUMN = 12, HILL_ROUTE_END_COLUMN = 24;
const HILL_ROUTE_ALIAS_COLUMN = 13, HILL_ROUTE_RETURN_COLUMN = 16, HILL_ROUTE_ROW = 4;
const HILL_ROUTE_DIRECTION_COUNT = 4, HILL_ROUTE_ROAD_FIRST = 182, HILL_ROUTE_SPAN_FIRST = 194, HILL_ROUTE_SPAN_END = 198;
function terrainRectangle(heightAtVertex, surface = 0) {
    const doc = rectangle();
    for (let y = 0; y <= doc.height; y++) for (let x = 0; x <= doc.width; x++)
        doc.vertices[y * (doc.width + 1) + x] = heightAtVertex(x, y);
    for (const object of doc.objects) {
        const heights = doc.tileHeights(object.x, object.y);
        object.surface = surface;
        object.baseHeight = heights[0]; object.slopeX = heights[1] - heights[0]; object.slopeY = heights[3] - heights[0];
    }
    return clearHistory(doc);
}
for (let base = 0; base <= HILL_ROUTE_MAX_LEVEL; base++) {
    for (let direction = 0, doc = terrainRectangle(() => base); direction < 4; direction++, doc = rotate(doc))
        check('flat plateau level ' + base + ' direction ' + direction, doc, 0);
    for (let rise = 1; rise <= HILL_ROUTE_MAX_LEVEL - base; rise++) for (let surface = 0; surface < model.SURFACES.length; surface++) {
        const hill = terrainRectangle(x => base + Math.max(0, Math.min(rise, x - HILL_ROUTE_START_COLUMN,
            HILL_ROUTE_END_COLUMN - x)), surface);
        for (let direction = 0, doc = hill; direction < 4; direction++, doc = rotate(doc)) {
            const result = check('native hill roads base ' + base + ' rise ' + rise + ' surface ' + surface + ' direction ' + direction, doc, 0);
            const hillPieces = result.route.filter(piece => piece.id >= HILL_ROUTE_ROAD_FIRST && piece.id < HILL_ROUTE_SPAN_FIRST);
            assert.equal(hillPieces.length, rise * HILL_ROUTE_DIRECTION_COUNT);
            assert(hillPieces.every(piece => Math.floor((piece.id - HILL_ROUTE_ROAD_FIRST) / HILL_ROUTE_DIRECTION_COUNT) === surface));
        }
    }
}
// Sloped tunnels and slaloms keep their native stunt routes while their floor
// follows the same incline as neighboring hill roads, including both grades.
const SLOPED_STUNT_MODELS = new Set([28, 34]);
const SLOPED_STUNT_VARIANTS = [[66, 1], [66, 3], [67, 0], [67, 2], [115, 1], [115, 3], [116, 0], [116, 2]];
for (let base = 0; base < HILL_ROUTE_MAX_LEVEL; base++) for (const surface of [0, 1, 2]) {
    for (const [id, rotation] of SLOPED_STUNT_VARIANTS) for (const tunnelSlalom of id === 115 || id === 116 ? [0, 1] : [0]) {
        const rise = Math.min(2, HILL_ROUTE_MAX_LEVEL - base);
        const hill = terrainRectangle(x => base + Math.max(0, Math.min(rise, x - HILL_ROUTE_START_COLUMN,
            HILL_ROUTE_END_COLUMN - x)), surface);
        for (const y of [HILL_ROUTE_ROW, hill.height - 4]) for (const x of [HILL_ROUTE_START_COLUMN, HILL_ROUTE_END_COLUMN - 1])
            replace(hill, x, y, id, rotation, surface, 0, tunnelSlalom);
        for (let direction = 0, doc = clearHistory(hill); direction < 4; direction++, doc = rotate(doc)) {
            const result = check('sloped stunt ' + id + ' tunnel ' + tunnelSlalom + ' rotation ' + rotation + ' base ' + base +
                ' surface ' + surface + ' direction ' + direction, doc, 0);
            assert.equal(result.route.filter(piece => SLOPED_STUNT_MODELS.has(model.catalog.find(entry => entry.id === piece.id)?.physical)).length, 4);
            assert.deepEqual(model.decodeCstrk(model.encodeCstrk(doc)).snapshot(), doc.snapshot());
        }
    }
}
// A ramp pointing away from the high plateau becomes the original flat hill
// span. Its lower-terrain side meets an elevated deck, not another ground road.
for (let base = 0; base < HILL_ROUTE_MAX_LEVEL; base++) for (let surface = 0; surface < model.SURFACES.length; surface++) {
    for (const alias of [36, 56, 95]) {
        const span = terrainRectangle(x => base + (x <= HILL_ROUTE_ALIAS_COLUMN ? 1 : 0), surface);
        replace(span, HILL_ROUTE_ALIAS_COLUMN, HILL_ROUTE_ROW, alias, 0, surface);
        for (let x = HILL_ROUTE_ALIAS_COLUMN + 1; x < HILL_ROUTE_RETURN_COLUMN; x++)
            replace(span, x, HILL_ROUTE_ROW, 35, 0, surface);
        replace(span, HILL_ROUTE_RETURN_COLUMN, HILL_ROUTE_ROW, 37, 0, surface);
        for (let direction = 0, doc = clearHistory(span); direction < 4; direction++, doc = rotate(doc)) {
            const result = check('native hill span alias ' + alias + ' base ' + base + ' surface ' + surface + ' direction ' + direction, doc, 0);
            assert.equal(result.route.filter(piece => piece.id >= HILL_ROUTE_SPAN_FIRST && piece.id < HILL_ROUTE_SPAN_END).length, 1);
        }
    }
}
// The corners and vertical return road lie crosswise on the same straight
// hillside as the original aligned hill roads along the top and bottom edges.
const GENERAL_ROUTE_SLOPE_FIRST_COLUMN = 35, GENERAL_ROUTE_SLOPE_RISE = 2;
for (const grade of [-1, 1]) {
    const doc = terrainRectangle(x => {
        const rise = Math.max(0, Math.min(GENERAL_ROUTE_SLOPE_RISE, x - GENERAL_ROUTE_SLOPE_FIRST_COLUMN));
        return grade > 0 ? rise : GENERAL_ROUTE_SLOPE_RISE - rise;
    });
    for (let direction = 0, current = doc; direction < HILL_ROUTE_DIRECTION_COUNT; direction++, current = rotate(current))
        check('connected crosswise roads and sharp turns slope ' + grade + ' direction ' + direction, current, 0);
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
        doc.place(piece.id, 12, 4, 0, rotation, entry.lowerSurface ?? 0, entry.tunnelSlalom ?? 0);
        check(entry.name + ' direction ' + direction, clearHistory(doc));
    }
}
// Material combinations do not change either route through an overpass.
for (let direction = 0; direction < 4; direction++) {
    const reference = rectangle(); replace(reference, 12, 4, 101, direction);
    const status = validation.validateRoute(reference).status;
    for (const surface of [0, 1, 2]) for (const lowerSurface of [0, 1, 2]) {
        const doc = rectangle(); replace(doc, 12, 4, 101, direction, surface, lowerSurface);
        check('overpass upper ' + surface + ' lower ' + lowerSurface + ' direction ' + direction, clearHistory(doc), status);
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
// Raised and lowered terrain in both free corners must not change the
// connected turn's route, including a hill at its unclaimed origin vertex.
for (const delta of [-1, 1]) {
    const hill = model.decodeCstrk(model.encodeCstrk(sparse)), supportLevel = 2;
    hill.vertices.fill(supportLevel);
    for (const object of hill.objects) object.baseHeight = supportLevel;
    const turn = hill.objects[0];
    for (let y = 0; y < turn.height; y++) for (let x = 0; x < turn.width; x++) {
        if (!(turn.mask & (1 << (y * turn.width + x)))) hill.raise(turn.x + x * turn.width, turn.y + y * turn.height, delta);
    }
    for (let direction = 0, doc = clearHistory(hill); direction < 4; direction++, doc = rotate(doc))
        check('connected sparse turn hill corners ' + delta + ' direction ' + direction, doc, 0);
}
sparse.place(4, 40, 40); sparse.place(4, 41, 41);
check('roads share both sparse turn corner cells', clearHistory(sparse), 0);
for (let direction = 0, doc = sparse; direction < 4; direction++, doc = rotate(doc)) check('sparse shared anchor direction ' + direction, doc, 0);

// Even disconnected stunts are admitted before the native route walk. Cover
// every affected family with hills in both free cells and each orientation.
const SPARSE_HILL_MODELS = new Set([3, 4, 5, 21, 26]);
for (const entry of model.library.filter(piece => SPARSE_HILL_MODELS.has(piece.physical))) {
    for (let direction = 0; direction < 4; direction++) for (const delta of [-1, 1]) {
        const doc = terrainRectangle(() => 2), { piece, rotation } = model.pieceVariant(entry, direction);
        const object = doc.place(piece.id, 15, 8, 0, rotation);
        for (let y = 0; y < object.height; y++) for (let x = 0; x < object.width; x++) {
            if (!(object.mask & (1 << (y * object.width + x)))) doc.raise(object.x + x * object.width, object.y + y * object.height, delta);
        }
        const loaded = model.decodeCstrk(model.encodeCstrk(doc));
        check(entry.name + ' hill corners ' + delta + ' direction ' + direction, clearHistory(loaded), 0);
    }
}

// Exercise actual connected multi-tile geometry on a two-level hillside,
// including roads independently sharing both unclaimed corner cells.
const GENERAL_BIG_TURN_FIRST_COLUMN = 40, GENERAL_BIG_TURN_RISE = 2;
for (const grade of [-1, 1]) {
    const doc = new model.Document(sparse.width, sparse.height);
    for (let y = 0; y <= doc.height; y++) for (let x = 0; x <= doc.width; x++) {
        const rise = Math.max(0, Math.min(GENERAL_BIG_TURN_RISE, x - GENERAL_BIG_TURN_FIRST_COLUMN));
        doc.vertices[y * (doc.width + 1) + x] = grade > 0 ? rise : GENERAL_BIG_TURN_RISE - rise;
    }
    for (const object of sparse.objects)
        put(doc, object.id, object.x, object.y, object.surface, object.rotation, object.lowerSurface ?? 0, object.tunnelSlalom ?? 0);
    for (let direction = 0, current = clearHistory(doc); direction < HILL_ROUTE_DIRECTION_COUNT; direction++, current = rotate(current))
        check('connected large turn on straight slope ' + grade + ' direction ' + direction, current, 0);
}
// Admission happens before the native route walk. Every library family and
// orientation must load on all four slope directions, including scenery and
// highways that the editor no longer permits users to build on slopes.
const GENERAL_SLOPE_MAP_SIZE = 4, GENERAL_SLOPE_ORIGIN = 1;
const GENERAL_SLOPE_DIRECTIONS = [[0, 1], [1, 0], [0, -1], [-1, 0]];
for (const entry of model.library) for (let direction = 0; direction < GENERAL_SLOPE_DIRECTIONS.length; direction++) {
    const { piece, rotation } = model.pieceVariant(entry, direction), footprint = model.rotatedFootprint(piece, rotation);
    for (const [slopeX, slopeY] of GENERAL_SLOPE_DIRECTIONS) {
        const doc = new model.Document(GENERAL_SLOPE_MAP_SIZE, GENERAL_SLOPE_MAP_SIZE);
        const span = slopeX ? footprint.width : footprint.height, grade = slopeX || slopeY;
        for (let y = 0; y <= doc.height; y++) for (let x = 0; x <= doc.width; x++) {
            const rise = Math.max(0, Math.min(span, (slopeX ? x : y) - GENERAL_SLOPE_ORIGIN));
            doc.vertices[y * (doc.width + 1) + x] = grade > 0 ? rise : span - rise;
        }
        putImported(doc, piece.id, GENERAL_SLOPE_ORIGIN, GENERAL_SLOPE_ORIGIN, 1, rotation, entry.lowerSurface ?? 0, entry.tunnelSlalom ?? 0);
        check(entry.name + ' straight slope ' + slopeX + ',' + slopeY + ' direction ' + direction, doc);
    }
}

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
    assert.ifError(native.error);
    assert.equal(native.status, 0, native.stderr || 'Native route oracle status: ' + native.status + ', signal: ' + native.signal);
    const results = native.stdout.trim().split('\n').map(line => JSON.parse(line));
    assert.equal(results.length, fixtures.length, 'native oracle response count');
    for (let i = 0; i < fixtures.length; i++) {
        const { name, result } = fixtures[i], actual = results[i];
        assert(!actual.loadError, name + ': ' + actual.loadError);
        for (const field of ['status', 'column', 'row', 'pieces', 'route']) assert.deepEqual(result[field], actual[field], name + ': native ' + field);
    }
    console.log('PASS ' + fixtures.length + ' editor routes match native game status, location, pieces and links');
} else console.log('PASS ' + fixtures.length + ' editor route checks (pass the native oracle for differential checks)');
