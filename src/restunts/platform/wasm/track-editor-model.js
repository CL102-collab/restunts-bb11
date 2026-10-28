/* Chocolate Stunts editor document and CSTRK v1 binary codec. */
(function (root, factory) {
    const api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.TrackEditorModel = api;
}(typeof globalThis !== 'undefined' ? globalThis : this, function () {
    'use strict';
    const SURFACES = ['Paved', 'Dirt', 'Ice'];
    const TERRAIN_SURFACES = ['Grass', 'Ice', 'Dirt', 'Water'];
    const MAGIC = [67, 83, 84, 82, 75, 26, 13, 10];
    const groups = [{ id: 1, name: 'F1 · Roads' }, { id: 4, name: 'F4 · Stunts' }, { id: 5, name: 'F5 · Banked roads' },
        { id: 6, name: 'F6 · Large branches' }, { id: 7, name: 'F7 · Highway & chicanes' },
        { id: 8, name: 'F8 · Elevated roads' }, { id: 9, name: 'F9 · Corkscrews' }, { id: 10, name: 'F10 · Scenery' }];
    // [original rotation, surface, multi-tile flag, physical model], from trkObjectList.
    const LEGACY = [[0,0,0,64],[0,0,0,0],[0,0,0,64],[0,0,0,64],[0,0,0,1],[256,0,0,1],[768,0,0,2],[0,0,0,2],[512,0,0,2],[256,0,0,2],[768,0,3,3],[0,0,3,3],[512,0,3,3],[256,0,3,3],[0,1,0,1],[256,1,0,1],[768,1,0,2],[0,1,0,2],[512,1,0,2],[256,1,0,2],[768,1,3,3],[0,1,3,3],[512,1,3,3],[256,1,3,3],[0,2,0,1],[256,2,0,1],[768,2,0,2],[0,2,0,2],[512,2,0,2],[256,2,0,2],[768,2,3,3],[0,2,3,3],[512,2,3,3],[256,2,3,3],[0,0,0,18],[768,0,0,18],[256,0,0,16],[768,0,0,16],[0,0,0,16],[512,0,0,16],[0,0,0,24],[256,0,0,24],[512,0,0,24],[768,0,0,24],[0,0,0,23],[256,0,0,23],[512,0,0,23],[768,0,0,23],[512,0,0,25],[0,0,0,25],[768,0,0,25],[256,0,0,25],[768,0,3,26],[0,0,3,26],[512,0,3,26],[256,0,3,26],[256,0,0,16],[768,0,0,16],[0,0,0,16],[512,0,0,16],[0,0,3,5],[256,0,3,4],[0,0,3,4],[256,0,3,5],[0,0,1,27],[256,0,2,27],[0,0,0,28],[768,0,0,28],[0,0,0,30],[256,0,0,30],[0,0,0,29],[512,0,0,29],[768,0,0,29],[256,0,0,29],[0,0,0,12],[0,0,0,6],[768,0,0,6],[512,0,0,6],[256,0,0,6],[0,0,0,7],[768,0,0,7],[512,0,0,7],[256,0,0,7],[0,0,0,31],[256,0,0,31],[0,0,1,35],[256,0,2,35],[0,0,3,8],[768,0,3,8],[512,0,3,8],[256,0,3,8],[0,0,3,9],[768,0,3,9],[512,0,3,9],[256,0,3,9],[256,0,0,17],[768,0,0,17],[0,0,0,17],[512,0,0,17],[0,0,0,20],[768,0,0,20],[0,0,0,22],[768,0,0,22],[0,0,0,19],[768,0,0,19],[768,0,3,21],[0,0,3,21],[512,0,3,21],[256,0,3,21],[0,0,0,11],[768,0,0,11],[0,-1,0,10],[256,-1,0,10],[512,-1,0,10],[768,-1,0,10],[0,0,0,34],[768,0,0,34],[0,0,3,32],[768,0,3,32],[512,0,3,32],[256,0,3,32],[0,0,3,33],[768,0,3,33],[512,0,3,33],[256,0,3,33],[0,1,0,12],[0,1,0,6],[768,1,0,6],[512,1,0,6],[256,1,0,6],[0,1,0,7],[768,1,0,7],[512,1,0,7],[256,1,0,7],[0,1,0,0],[512,1,0,0],[256,1,0,0],[768,1,0,0],[0,2,0,12],[0,2,0,6],[768,2,0,6],[512,2,0,6],[256,2,0,6],[0,2,0,7],[768,2,0,7],[512,2,0,7],[256,2,0,7],[0,2,0,0],[512,2,0,0],[256,2,0,0],[768,2,0,0],[0,0,0,74],[0,0,0,72],[0,0,0,71],[0,0,0,73],[0,0,0,66],[512,0,0,66],[768,0,0,66],[256,0,0,66],[0,0,0,65],[512,0,0,65],[768,0,0,65],[256,0,0,65],[0,0,0,68],[512,0,0,68],[768,0,0,68],[256,0,0,68],[0,-1,0,69],[512,-1,0,69],[768,-1,0,69],[256,-1,0,69],[0,0,0,70],[512,0,0,70],[768,0,0,70],[256,0,0,70],[0,-1,0,67],[512,-1,0,67],[768,-1,0,67],[256,-1,0,67],[512,0,0,0],[256,0,0,0],[768,0,0,0],[0,0,0,1],[768,0,0,1],[512,0,0,1],[256,0,0,1]];
    const physicalNames = { 0: 'Start / finish', 1: 'Road', 2: 'Sharp turn', 3: 'Big turn', 4: 'Chicane right-left',
        5: 'Chicane left-right', 6: 'Branch left', 7: 'Branch right', 8: 'Large branch left', 9: 'Large branch right',
        10: 'Highway transition', 11: 'Divided highway', 12: 'Crossroad', 16: 'Ramp', 17: 'Solid ramp',
        18: 'Elevated road', 19: 'Elevated span', 20: 'Solid elevated road', 21: 'Elevated turn', 22: 'Overpass',
        23: 'Bank entrance', 24: 'Bank exit', 25: 'Banked road', 26: 'Banked turn', 27: 'Loop', 28: 'Tunnel',
        29: 'Pipe entrance', 30: 'Pipe', 31: 'Half pipe', 32: 'Corkscrew up', 33: 'Corkscrew down',
        34: 'Slalom', 35: 'Horizontal corkscrew', 65: 'Barn', 66: 'Gas station', 67: 'Joe’s', 68: 'Office',
        69: 'Windmill', 70: 'Ship', 71: 'Palm tree', 72: 'Cactus', 73: 'Tennis court', 74: 'Tree', 64: 'Scenery' };
    function groupFor(id, physical) {
        if (id >= 151 && id <= 178) return 10;
        if (id >= 117 && id <= 124) return 9;
        if ([34, 35, 36, 37, 38, 39].includes(id) || (id >= 95 && id <= 108)) return 8;
        if ((id >= 60 && id <= 63) || (id >= 109 && id <= 114)) return 7;
        if (id >= 87 && id <= 94) return 6;
        if (id >= 40 && id <= 55) return 5;
        if ((id >= 56 && id <= 73) || id === 83 || id === 84 || id === 85 || id === 86 || id === 115 || id === 116) return 4;
        return 1;
    }
    const catalog = LEGACY.map((a, id) => {
        const width = (a[2] & 2) ? 2 : 1, height = (a[2] & 1) ? 2 : 1, physical = a[3];
        let mask = (1 << (width * height)) - 1;
        if ([3, 21, 26].includes(physical)) mask = ({ 0: 11, 256: 14, 512: 13, 768: 7 })[a[0]];
        if (id === 60 || id === 61) mask = 6;
        if (id === 62 || id === 63) mask = 9;
        const decoration = physical >= 64;
        return { id, name: physicalNames[physical] || 'Element ' + id, group: groupFor(id, physical), width, height, mask,
            rotation: a[0] / 256, physical, decoration, legacySurface: a[1],
            surfaces: [27, 29, 30, 31].includes(physical) ? [0] : [0, 1, 2] };
    }).filter(a => a.id > 0 && a.id < 182 && a.id !== 2 && a.id !== 3);
    const byId = new Map(catalog.map(a => [a.id, a]));
    // A family keeps distinct shapes and editor groups, while combining native rotations.
    const libraryFamilies = new Map();
    for (const entry of catalog.filter(p => p.legacySurface <= 0)) {
        const key = entry.group + ':' + entry.physical;
        if (!libraryFamilies.has(key)) libraryFamilies.set(key, []);
        libraryFamilies.get(key).push(entry);
    }
    const library = Array.from(libraryFamilies.values(), variants => variants.find(p => p.rotation === 0) || variants[0]);
    function integer(n, min, max, label) { if (!Number.isInteger(n) || n < min || n > max) throw new Error('Invalid ' + label + '.'); return n; }
    function pieceVariant(entry, direction) {
        integer(direction, 0, 3, 'piece direction');
        const variants = libraryFamilies.get(entry.group + ':' + entry.physical);
        // Use original directional artwork when present. Symmetric pieces may have only
        // one or two native variants; explicit rotation supplies the remaining directions.
        const piece = variants.find(p => p.rotation === direction) || variants.find(p => p.rotation === 0) || variants[0];
        return { piece, rotation: (direction - piece.rotation + 4) % 4 };
    }
    function checkNotes(notes) {
        if (typeof notes !== 'string' || notes.length > 8000 || /[^\x09\x0a\x0d\x20-\x7e]/.test(notes))
            throw new Error('Notes must contain at most 8000 ASCII characters (text, tabs and line breaks).');
    }
    function rotatedFootprint(entry, rotation) {
        let { width, height, mask } = entry;
        for (let r = 0; r < rotation; r++) {
            let next = 0;
            for (let y = 0; y < height; y++) for (let x = 0; x < width; x++)
                if (mask & (1 << (y * width + x))) next |= 1 << (x * height + height - 1 - y);
            [width, height, mask] = [height, width, next];
        }
        return { width, height, mask };
    }
    function occupied(object) {
        const cells = [];
        for (let y = 0; y < object.height; y++) for (let x = 0; x < object.width; x++)
            if (object.mask & (1 << (y * object.width + x))) cells.push([object.x + x, object.y + y]);
        return cells;
    }
    class Document {
        constructor(width = 90, height = 90) {
            this.width = integer(width, 1, 90, 'track width'); this.height = integer(height, 1, 90, 'track height');
            this.vertices = new Uint8Array((width + 1) * (height + 1));
            this.cells = Array.from({ length: width * height }, () => ({ surface: 0, halfWaterCorner: 0 }));
            this.objects = []; this.notes = ''; this.history = []; this.future = []; this.pending = null;
        }
        get canUndo() { return this.history.length > 0; }
        get canRedo() { return this.future.length > 0; }
        snapshot() { return { width: this.width, height: this.height, vertices: Array.from(this.vertices), cells: this.cells.map(a => ({ ...a })), objects: this.objects.map(a => ({ ...a })), notes: this.notes }; }
        restore(state) { this.width = state.width; this.height = state.height; this.vertices = Uint8Array.from(state.vertices); this.cells = state.cells.map(a => ({ ...a })); this.objects = state.objects.map(a => ({ ...a })); this.notes = state.notes; }
        beginTransaction(label = 'Edit') { if (this.pending) throw new Error('An editor transaction is already active.'); this.pending = { label, before: this.snapshot() }; }
        endTransaction() {
            if (!this.pending) return false;
            const step = this.pending; this.pending = null; step.after = this.snapshot();
            if (JSON.stringify(step.before) === JSON.stringify(step.after)) return false;
            this.history.push(step); if (this.history.length > 100) this.history.shift(); this.future = []; return true;
        }
        cancelTransaction() { if (this.pending) { const before = this.pending.before; this.pending = null; this.restore(before); } }
        transaction(label, callback) {
            if (this.pending) return callback(this);
            this.beginTransaction(label);
            try { const result = callback(this); this.endTransaction(); return result; }
            catch (error) { this.cancelTransaction(); throw error; }
        }
        undo() { if (this.pending) this.endTransaction(); const step = this.history.pop(); if (!step) return false; this.restore(step.before); this.future.push(step); return true; }
        redo() { if (this.pending) this.endTransaction(); const step = this.future.pop(); if (!step) return false; this.restore(step.after); this.history.push(step); return true; }
        vertexIndex(x, y) { integer(x, 0, this.width, 'corner column'); integer(y, 0, this.height, 'corner row'); return y * (this.width + 1) + x; }
        cellIndex(x, y) { integer(x, 0, this.width - 1, 'tile column'); integer(y, 0, this.height - 1, 'tile row'); return y * this.width + x; }
        tileHeights(x, y) { this.cellIndex(x, y); const p = this.vertexIndex(x, y), stride = this.width + 1; return [this.vertices[p], this.vertices[p + 1], this.vertices[p + stride + 1], this.vertices[p + stride]]; }
        raise(x, y, delta = 1) {
            integer(delta, -1, 1, 'terrain change');
            return this.transaction(delta > 0 ? 'Raise terrain' : 'Lower terrain', () => {
                const p = this.vertexIndex(x, y), previous = this.vertices.slice(), target = Math.max(0, Math.min(4, this.vertices[p] + delta));
                this.vertices[p] = target;
                const queue = [[x, y]];
                for (let q = 0; q < queue.length; q++) {
                    const [cx, cy] = queue[q], level = this.vertices[cy * (this.width + 1) + cx];
                    for (let ny = Math.max(0, cy - 1); ny <= Math.min(this.height, cy + 1); ny++)
                        for (let nx = Math.max(0, cx - 1); nx <= Math.min(this.width, cx + 1); nx++) {
                            if (Math.abs(nx - cx) + Math.abs(ny - cy) !== 1) continue;
                            const index = ny * (this.width + 1) + nx, value = this.vertices[index];
                            if (delta > 0 && value < level - 1) { this.vertices[index] = level - 1; queue.push([nx, ny]); }
                            if (delta < 0 && value > level + 1) { this.vertices[index] = level + 1; queue.push([nx, ny]); }
                        }
                }
                const invalid = this.objects.find(object => !this.matchesPlane(object) || !this.hasLongSlope(object));
                if (invalid) { this.vertices = previous; throw new Error('Terrain would bend element ' + invalid.id + '. Remove or move it first.'); }
                return target;
            });
        }
        paint(x, y, surface, halfWaterCorner = 0) {
            integer(surface, 0, 3, 'terrain surface'); integer(halfWaterCorner, 0, 4, 'water corner');
            return this.transaction('Paint terrain', () => { this.cells[this.cellIndex(x, y)] = { surface, halfWaterCorner }; });
        }
        level(x, y, height, surface) {
            const cell = this.cellIndex(x, y);
            integer(height, 0, 4, 'terrain level'); integer(surface, 0, 3, 'terrain surface');
            return this.transaction('Level terrain', () => {
                const vertices = this.vertices.slice(), affected = new Set([cell]), stride = this.width + 1;
                for (let vy = 0; vy <= this.height; vy++) for (let vx = 0; vx <= this.width; vx++) {
                    // Clamp to the legal slope envelope around the four target corners.
                    // Staging the whole plateau avoids partial edits if an object blocks it.
                    const distance = Math.max(x - vx, 0, vx - x - 1) + Math.max(y - vy, 0, vy - y - 1);
                    const index = vy * stride + vx;
                    vertices[index] = Math.max(height - distance, Math.min(height + distance, this.vertices[index]));
                    if (vertices[index] === this.vertices[index]) continue;
                    for (let cy = Math.max(0, vy - 1); cy <= Math.min(this.height - 1, vy); cy++)
                        for (let cx = Math.max(0, vx - 1); cx <= Math.min(this.width - 1, vx); cx++) affected.add(cy * this.width + cx);
                }
                const invalid = this.objects.find(object => !this.matchesPlane(object, vertices) || !this.hasLongSlope(object, vertices));
                if (invalid) throw new Error('Terrain would bend element ' + invalid.id + '. Remove or move it first.');
                const cells = this.cells.slice();
                for (const index of affected) cells[index] = { surface, halfWaterCorner: 0 };
                this.vertices = vertices; this.cells = cells;
            });
        }
        matchesPlane(object, vertices = this.vertices) {
            for (let y = 0; y <= object.height; y++) for (let x = 0; x <= object.width; x++)
                if (vertices[(object.y + y) * (this.width + 1) + object.x + x] !== object.baseHeight + x * object.slopeX + y * object.slopeY) return false;
            return true;
        }
        hasLongSlope(object, vertices = this.vertices) {
            const entry = byId.get(object.id);
            if (!entry || !([8, 9, 10, 11].includes(entry.physical) || (entry.physical >= 16 && entry.physical <= 35)) || (!object.slopeX && !object.slopeY)) return true;
            const width = object.slopeX ? Math.max(2, object.width) : object.width;
            const height = object.slopeY ? Math.max(2, object.height) : object.height;
            for (let oy = object.height - height; oy <= 0; oy++) for (let ox = object.width - width; ox <= 0; ox++) {
                const left = object.x + ox, top = object.y + oy;
                if (left < 0 || top < 0 || left + width > this.width || top + height > this.height) continue;
                let matches = true;
                for (let y = 0; y <= height; y++) for (let x = 0; x <= width; x++)
                    if (vertices[(top + y) * (this.width + 1) + left + x] !== object.baseHeight + (ox + x) * object.slopeX + (oy + y) * object.slopeY) matches = false;
                if (matches) return true;
            }
            return false;
        }
        placement(id, x, y, surface = 0, rotation = 0) {
            const entry = byId.get(id); if (!entry) throw new Error('Unknown track element.');
            integer(surface, 0, 2, 'road surface'); integer(rotation, 0, 3, 'rotation');
            if (!entry.surfaces.includes(surface)) throw new Error(entry.name + ' does not support ' + SURFACES[surface].toLowerCase() + '.');
            const footprint = rotatedFootprint(entry, rotation);
            integer(x, 0, this.width - footprint.width, 'element column'); integer(y, 0, this.height - footprint.height, 'element row');
            const h = this.tileHeights(x, y), object = { id, x, y, rotation, surface: entry.decoration ? 0 : surface, ...footprint, baseHeight: h[0], slopeX: h[1] - h[0], slopeY: h[3] - h[0] };
            if (!this.matchesPlane(object)) throw new Error('The entire element needs a continuous planar slope.');
            if (!this.hasLongSlope(object)) throw new Error('This stunt needs a continuous slope at least two tiles long in each sloping direction.');
            const cells = new Set(occupied(object).map(([cx, cy]) => cy * this.width + cx));
            for (const existing of this.objects) if (occupied(existing).some(([cx, cy]) => cells.has(cy * this.width + cx)))
                throw new Error('This space is occupied. Remove the existing element first.');
            return object;
        }
        place(id, x, y, surface = 0, rotation = 0) { return this.transaction('Place element', () => { const object = this.placement(id, x, y, surface, rotation); this.objects.push(object); return object; }); }
        erase(x, y) { return this.transaction('Remove element', () => { this.cellIndex(x, y); const index = this.objects.findIndex(object => occupied(object).some(([cx, cy]) => cx === x && cy === y)); if (index < 0) return false; this.objects.splice(index, 1); return true; }); }
        setNotes(notes) { checkNotes(notes); return this.transaction('Edit notes', () => { this.notes = notes; }); }
        validate() {
            const errors = [], occupiedCells = new Set();
            try { integer(this.width, 1, 90, 'track width'); integer(this.height, 1, 90, 'track height'); checkNotes(this.notes); } catch (error) { errors.push(error.message); }
            if (this.vertices.length !== (this.width + 1) * (this.height + 1) || this.cells.length !== this.width * this.height) errors.push('Track dimensions do not match its terrain.');
            for (let y = 0; y <= this.height; y++) for (let x = 0; x <= this.width; x++) {
                const level = this.vertices[y * (this.width + 1) + x];
                if (level > 4) errors.push('Terrain exceeds four levels.');
                if (x < this.width && Math.abs(level - this.vertices[y * (this.width + 1) + x + 1]) > 1) errors.push('Terrain rises more than one level per edge.');
                if (y < this.height && Math.abs(level - this.vertices[(y + 1) * (this.width + 1) + x]) > 1) errors.push('Terrain rises more than one level per edge.');
            }
            for (const cell of this.cells) if (!Number.isInteger(cell.surface) || cell.surface < 0 || cell.surface > 3 || !Number.isInteger(cell.halfWaterCorner) || cell.halfWaterCorner < 0 || cell.halfWaterCorner > 4) errors.push('Invalid terrain surface.');
            if (this.objects.length > 8100) errors.push('Too many track elements.');
            for (const object of this.objects) {
                try {
                    integer(object.id, 1, 214, 'element ID'); integer(object.x, 0, this.width - object.width, 'element column'); integer(object.y, 0, this.height - object.height, 'element row');
                    integer(object.width, 1, 2, 'element width'); integer(object.height, 1, 2, 'element height');
                    integer(object.mask, 1, (1 << (object.width * object.height)) - 1, 'element footprint');
                    integer(object.rotation, 0, 3, 'element rotation'); integer(object.surface, 0, 2, 'element surface');
                    integer(object.baseHeight, 0, 4, 'element height'); integer(object.slopeX, -1, 1, 'element slope'); integer(object.slopeY, -1, 1, 'element slope');
                    const entry = byId.get(object.id);
                    if (!entry) throw new Error('Unknown track element.');
                    const footprint = rotatedFootprint(entry, object.rotation);
                    if (object.width !== footprint.width || object.height !== footprint.height || object.mask !== footprint.mask) throw new Error('Element footprint does not match its catalog geometry.');
                    if (!entry.surfaces.includes(object.surface)) throw new Error('Element does not support the selected road surface.');
                    if (!this.matchesPlane(object)) throw new Error('Element does not match its terrain plane.');
                    if (!this.hasLongSlope(object)) throw new Error('This stunt needs a continuous slope at least two tiles long in each sloping direction.');
                    for (const [x, y] of occupied(object)) { const key = y * this.width + x; if (occupiedCells.has(key)) throw new Error('Track elements overlap.'); occupiedCells.add(key); }
                } catch (error) { errors.push(error.message); }
            }
            return [...new Set(errors)];
        }
    }
    function encodeCstrk(doc) {
        const errors = doc.validate(); if (errors.length) throw new Error(errors.join(' '));
        const payload = doc.vertices.length + doc.cells.length * 2 + doc.objects.length * 16 + doc.notes.length;
        const a = new Uint8Array(32 + payload), v = new DataView(a.buffer); a.set(MAGIC);
        v.setUint16(8, 1, true); v.setUint16(10, 32, true); v.setUint16(16, doc.width, true); v.setUint16(18, doc.height, true);
        v.setUint32(20, doc.notes.length, true); v.setUint32(24, doc.objects.length, true); v.setUint32(28, payload, true);
        let p = 32; a.set(doc.vertices, p); p += doc.vertices.length;
        for (const cell of doc.cells) { a[p++] = cell.surface; a[p++] = cell.halfWaterCorner; }
        for (const o of doc.objects) {
            v.setUint16(p, o.id, true); a.set([o.x, o.y, o.rotation, o.surface, o.width, o.height, o.mask, o.baseHeight], p + 2);
            v.setInt8(p + 10, o.slopeX); v.setInt8(p + 11, o.slopeY); p += 16;
        }
        for (let i = 0; i < doc.notes.length; i++) a[p++] = doc.notes.charCodeAt(i);
        return a;
    }
    function decodeCstrk(input) {
        const a = input instanceof Uint8Array ? input : new Uint8Array(input);
        if (a.length < 32 || !MAGIC.every((n, i) => a[i] === n)) throw new Error('This is not a Chocolate Stunts track.');
        const v = new DataView(a.buffer, a.byteOffset, a.byteLength);
        if (v.getUint16(8, true) !== 1 || v.getUint16(10, true) !== 32 || v.getUint32(12, true)) throw new Error('Unsupported Chocolate Stunts track version.');
        const width = v.getUint16(16, true), height = v.getUint16(18, true), notesLength = v.getUint32(20, true), count = v.getUint32(24, true), payload = v.getUint32(28, true);
        const doc = new Document(width, height), expected = doc.vertices.length + doc.cells.length * 2 + count * 16 + notesLength;
        if (notesLength > 8000 || count > 8100 || payload !== expected || a.length !== 32 + expected) throw new Error('Invalid or truncated Chocolate Stunts track.');
        let p = 32; doc.vertices.set(a.subarray(p, p + doc.vertices.length)); p += doc.vertices.length;
        for (const cell of doc.cells) { cell.surface = a[p++]; cell.halfWaterCorner = a[p++]; }
        for (let i = 0; i < count; i++, p += 16) {
            if (v.getUint32(p + 12, true)) throw new Error('Unsupported element flags.');
            doc.objects.push({ id: v.getUint16(p, true), x: a[p + 2], y: a[p + 3], rotation: a[p + 4], surface: a[p + 5], width: a[p + 6], height: a[p + 7], mask: a[p + 8], baseHeight: a[p + 9], slopeX: v.getInt8(p + 10), slopeY: v.getInt8(p + 11) });
        }
        doc.notes = String.fromCharCode(...a.subarray(p));
        const errors = doc.validate(); if (errors.length) throw new Error(errors.join(' '));
        return doc;
    }
    return { Document, catalog, library, pieceVariant, groups, SURFACES, TERRAIN_SURFACES, encodeCstrk, decodeCstrk, occupied, rotatedFootprint };
}));
