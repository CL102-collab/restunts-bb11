/* Race-route checks mirror the SDL3 CSTRK path in track_setup.c. This checker
 * does not change the document or prevent saving unfinished tracks. */
(function (root, factory) {
    if (typeof module === 'object' && module.exports) module.exports = factory(require('./track-editor-route-data.js'), require('./track-editor-model.js'));
    else root.TrackEditorValidation = factory(root.TrackEditorRouteData, root.TrackEditorModel);
}(typeof globalThis !== 'undefined' ? globalThis : this, function (data, model) {
    'use strict';
    const CAPACITY = 16201;
    const STATUS = { OK: 0, NO_START_FINISH: 1, INTERNAL_ERROR: 2, MANY_START_FINISH: 3,
        ELEMENT_MISMATCH: 4, WRONG_WAY: 5, MANY_ELEMENTS: 6, NO_PATH: 7, MANY_PATHS: 8,
        NO_RUNWAY: 9, LONG_JUMP: 10, TERRAIN_MISMATCH: 11 };
    const STARTS = [[1, 134, 147], [136, 149, 180], [135, 148, 179], [137, 150, 181]];
    const ENTRY_OWNER = [2, 4, 1, 3], ENTRY_NORTH = [11, 6, 0, 7];
    const ENTRY_WEST = [10, 0, 5, 8], ENTRY_NORTHWEST = [12, 0, 0, 9];
    // [column step, row step, direction N/E/S/W], indexed by exit point.
    const STEPS = [[0, 0, 0], [0, -1, 0], [0, 1, 2], [1, 0, 1], [-1, 0, 3],
        [1, -1, 0], [-1, 1, 3], [1, 1, 1], [2, 0, 1], [2, 1, 1],
        [1, 1, 2], [0, 2, 2], [1, 2, 2]];
    const SYMMETRIC = new Set([1, 27, 28, 30, 31, 18, 19, 20, 22, 11, 34, 4, 5, 35]);
    const FLAT_TERRAIN = 0, RAISED_TERRAIN = 6;
    const HILL_TERRAIN_FIRST = 7, HILL_TERRAIN_END = 11, HILL_ROAD_FIRST = 182, HILL_SPAN_FIRST = 194;
    const HILL_DIRECTION_COUNT = 4, HILL_DIRECT_VARIANT_COUNT = 3;
    const SLOPED_STUNT_PHYSICAL_MODELS = new Set([28, 34]);
    const HILL_PIECES = [[4, 14, 24, 39, 59, 98], [5, 15, 25, 36, 56, 95],
        [4, 14, 24, 38, 58, 97], [5, 15, 25, 37, 57, 96]];
    const CAUSES = ['', 'No start / finish element.', 'The route could not be compiled.',
        'More than one start / finish.', 'Track pieces do not connect.',
        'A track piece faces the wrong way.', 'This track has too many elements.',
        'The track has no complete route.', 'This track has too many branches.',
        'A jump needs a longer runway.', 'The jump crosses too many tiles.', 'Terrain edges do not match.'];
    const HINTS = ['', 'Add exactly one start / finish.', 'Check the track and try again.',
        'Keep exactly one start / finish.', 'Check their directions and types.',
        'Rotate or replace that piece.', 'Reduce the number of road pieces.',
        'Connect back to start / finish.', 'Simplify the branching roads.',
        'Add straight road before the ramp.', 'Shorten the gap between ramps.', 'Check the terrain at this position.'];
    function message(status, column, row, pieces) {
        if (!status) return 'Race route is valid (' + pieces + ' elements).';
        const location = status !== STATUS.NO_START_FINISH && status !== STATUS.INTERNAL_ERROR
            ? ' Column ' + (column + 1) + ', row ' + (row + 1) + '.' : '';
        return (CAUSES[status] || 'Track validation error ' + status + '.') + location + ' ' +
            (HINTS[status] || 'Check the track and try again.');
    }
    function resolveOrientation(object) {
        const original = data.objects[object.id];
        if (!original || object.id < 1 || object.id >= 182) return -1;
        if (!object.rotation) return object.id;
        const angle = (original.rotation + object.rotation) & 3;
        for (let pass = 0; pass < 2; pass++) for (let id = 1; id < 182; id++) {
            const candidate = data.objects[id];
            if (candidate.physical !== original.physical || candidate.surface !== original.surface ||
                candidate.shape !== original.shape || candidate.lowShape !== original.lowShape) continue;
            if (1 + !!(candidate.flags & 2) !== object.width || 1 + !!(candidate.flags & 1) !== object.height) continue;
            if (candidate.rotation === angle || original.physical === 12) return id;
            if (pass && ((candidate.rotation ^ angle) === 2) && SYMMETRIC.has(original.physical)) return id;
        }
        if (original.routeIndex === null && object.width === 1 && object.height === 1) return object.id;
        return -1;
    }
    function resolveId(doc, object) {
        const id = resolveOrientation(object);
        if (id < 0) return id;
        const profile = model.terrainProfile(doc.tileHeights(object.x, object.y));
        if (!profile) return -1;
        if (profile.terrain === FLAT_TERRAIN || profile.terrain === RAISED_TERRAIN) return id;
        if (profile.terrain < HILL_TERRAIN_FIRST || profile.terrain >= HILL_TERRAIN_END) return -1;
        if (SLOPED_STUNT_PHYSICAL_MODELS.has(data.objects[id].physical)) return doc.supportsTerrain(object) ? id : -1;
        const direction = profile.terrain - HILL_TERRAIN_FIRST;
        const variant = HILL_PIECES[direction].indexOf(id);
        if (variant < 0) return -1;
        return variant < HILL_DIRECT_VARIANT_COUNT ? HILL_ROAD_FIRST + direction + object.surface * HILL_DIRECTION_COUNT :
            HILL_SPAN_FIRST + direction;
    }
    function validateRoute(doc) {
        if (!data || !data.objects || !data.blocks) throw new Error('Editor route data could not be loaded.');
        const width = doc.width, height = doc.height, occupied = new Array(width * height), route = [];
        const visited = new Uint8Array(width * height), firstVisit = new Map(), branches = [];
        let start = null;
        const walk = { x: 0, y: 0, direction: 0, id: 0, entry: 0, previousCode: 0,
            subtype: 0, connection: 0, previousSubtype: 0, previousConnection: -1,
            previousId: 0, previousX: 0, previousY: 0, previousPiece: -1,
            runway: 0, jump: 0, closed: false, matches: 0 };
        function result(status, column = walk.x, row = walk.y) {
            if (column === -1) column = 0; else if (column === width) column = width - 1;
            if (row === -1) row = 0; else if (row === height) row = height - 1;
            column &= 255; row &= 255;
            return { status, column, row, pieces: route.length, route, message: message(status, column, row, route.length) };
        }
        for (const object of doc.objects) {
            const id = resolveId(doc, object);
            if (id < 0) return result(STATUS.INTERNAL_ERROR, object.x, object.y);
            const entry = { object, id };
            for (let y = 0; y < object.height; y++) for (let x = 0; x < object.width; x++)
                if (object.mask & (1 << (y * object.width + x))) occupied[(object.y + y) * width + object.x + x] = entry;
        }
        for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
            const entry = occupied[y * width + x];
            const id = entry && entry.object.x === x && entry.object.y === y ? entry.id : 0;
            const direction = STARTS.findIndex(ids => ids.includes(id));
            if (direction < 0) continue;
            if (start) return result(STATUS.MANY_START_FINISH, x, y);
            start = { x, y, direction };
        }
        if (!start) return result(STATUS.NO_START_FINISH, width, height);
        Object.assign(walk, start);
        function link(source, destination) {
            if (source < 0) return;
            if (route[source].primary === -1) route[source].primary = destination;
            else route[source].alternate = destination;
        }
        function key(subtype, connection) { return walk.x + ':' + walk.y + ':' + walk.id + ':' + subtype + ':' + connection; }
        function revisit(subtype) {
            if (walk.connection === -1 || !visited[walk.y * width + walk.x]) return;
            const index = firstVisit.get(key(subtype, walk.connection));
            if (index === undefined) return;
            walk.connection = -1;
            link(walk.previousPiece, index);
            if (index === 0) walk.closed = true;
        }
        function pushBranch(subtype) {
            if (branches.length === CAPACITY) return STATUS.MANY_PATHS;
            branches.push({ x: walk.x, y: walk.y, id: walk.id, subtype,
                connection: walk.connection, previousCode: walk.previousCode,
                previousPiece: walk.previousPiece, runway: walk.runway,
                previousX: walk.previousX, previousY: walk.previousY, previousId: walk.previousId,
                previousSubtype: walk.previousSubtype, previousConnection: walk.previousConnection });
            return STATUS.OK;
        }
        function matchBlocks() {
            const offset = data.objects[walk.id].routeIndex;
            if (offset === null) return STATUS.OK;
            const count = data.blocks[offset].count & 255;
            let selected = -1;
            for (let index = 0; index < count; index++) {
                const block = data.blocks[offset + index];
                walk.connection = -1;
                if ((block.entry & 255) === walk.entry) {
                    if ((block.entryType & 255) !== walk.previousCode) return STATUS.ELEMENT_MISMATCH;
                    walk.connection = 0;
                } else if ((block.exit & 255) === walk.entry) {
                    if ((block.exitType & 255) !== walk.previousCode) return STATUS.ELEMENT_MISMATCH;
                    walk.connection = 1;
                }
                revisit(index);
                if (walk.connection !== -1) {
                    if (!walk.matches) { walk.subtype = index; selected = walk.connection; }
                    else { const status = pushBranch(index); if (status) return status; }
                    walk.matches = (walk.matches + 1) & 255;
                }
            }
            if (walk.matches) walk.connection = selected;
            return STATUS.OK;
        }
        function resolvePiece() {
            const entry = occupied[walk.y * width + walk.x];
            if (!entry) { walk.id = 0; return STATUS.OK; }
            const dx = walk.x - entry.object.x, dy = walk.y - entry.object.y;
            walk.id = entry.id; walk.x = entry.object.x; walk.y = entry.object.y;
            const entries = dy ? (dx ? ENTRY_NORTHWEST : ENTRY_NORTH) : (dx ? ENTRY_WEST : ENTRY_OWNER);
            walk.entry = entries[walk.direction];
            if (!walk.jump && !walk.entry) return STATUS.INTERNAL_ERROR;
            return matchBlocks();
        }
        function jump() {
            if (walk.runway < 2) return STATUS.NO_RUNWAY;
            walk.runway = (walk.runway + 1) & 255;
            walk.jump = (walk.jump + 1) & 255;
            const distance = walk.jump + 1;
            if (walk.direction === 0) { walk.x = walk.previousX; walk.y = walk.previousY - distance; }
            else if (walk.direction === 1) { walk.y = walk.previousY; walk.x = walk.previousX + distance; }
            else if (walk.direction === 2) { walk.x = walk.previousX; walk.y = walk.previousY + distance; }
            else if (walk.direction === 3) { walk.y = walk.previousY; walk.x = walk.previousX - distance; }
            return STATUS.OK;
        }
        function appendPiece() {
            walk.jump = 0;
            visited[walk.y * width + walk.x] = 1;
            if (walk.previousPiece !== -1) link(walk.previousPiece, route.length);
            walk.previousPiece = route.length;
            const visit = key(walk.subtype, walk.connection);
            if (!firstVisit.has(visit)) firstVisit.set(visit, route.length);
            route.push({ id: walk.id, x: walk.x, y: walk.y, flags: ((walk.connection << 4) + walk.subtype) & 255, primary: -1, alternate: -1 });
            const block = data.blocks[data.objects[walk.id].routeIndex + walk.subtype];
            walk.runway = block.sign === 0 ? (walk.runway + 1) & 255 : 0;
            if (route.length === CAPACITY) return STATUS.MANY_ELEMENTS;
            walk.entry = (walk.connection === 1 ? block.entry : block.exit) & 255;
            walk.previousCode = (walk.connection === 1 ? block.entryType : block.exitType) & 255;
            walk.previousX = walk.x; walk.previousY = walk.y;
            walk.previousConnection = walk.connection; walk.previousSubtype = walk.subtype; walk.previousId = walk.id;
            if (walk.entry >= 1 && walk.entry <= 12) {
                const step = STEPS[walk.entry]; walk.x += step[0]; walk.y += step[1]; walk.direction = step[2];
            }
            return STATUS.OK;
        }
        // Each append consumes route capacity; jump attempts consume at most two
        // empty cells before returning or backtracking. The walk is bounded by
        // the same piece/branch limits as the game rather than a heuristic cutoff.
        for (;;) {
            walk.matches = 0;
            let backtrack = walk.x < 0 || walk.y < 0 || walk.x >= width || walk.y >= height;
            if (!backtrack) {
                const status = resolvePiece(); if (status) return result(status);
                if (!walk.matches) {
                    if (walk.previousCode !== 1 || walk.jump >= 2) backtrack = true;
                    else { const status = jump(); if (status) return result(status); continue; }
                }
            }
            if (backtrack) {
                if (!branches.length) return walk.closed ? result(STATUS.OK, start.x, start.y) : result(STATUS.NO_PATH);
                Object.assign(walk, branches.pop());
                if (walk.jump > 1) return result(STATUS.LONG_JUMP);
            }
            const status = appendPiece(); if (status) return result(status);
        }
    }
    return { validateRoute, STATUS };
}));
