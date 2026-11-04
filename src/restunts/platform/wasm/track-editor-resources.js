/* Original artwork is read from the selected game folder, never bundled. */
(function (root, factory) {
    const api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.TrackEditorResources = api;
}(typeof globalThis !== 'undefined' ? globalThis : this, function () {
    'use strict';
    const MAX_RESOURCE = 16 * 1024 * 1024;
    const EDITOR_TILE_SIZE = 16, EDITOR_TILE_INTERIOR = EDITOR_TILE_SIZE - 1;
    const OVERPASS_VERTICAL_ICON = 101, OVERPASS_HORIZONTAL_ICON = 102;
    const ROAD_SURFACE_DIRT = 1, ROAD_SURFACE_ICE = 2;
    const TUNNEL_SLALOM_ICONS = new Map([[115, { tunnel: 66, road: 4 }], [116, { tunnel: 67, road: 5 }]]);
    const ROLL_RAMP_VERTICAL_ICON = 85, ROLL_RAMP_HORIZONTAL_ICON = 86, ROLL_RAMP_TILE_COUNT = 2;
    const ROLL_RAMP_HALF_WIDTH = 4, ROLL_RAMP_LIP_HALF_WIDTH = 2, ROLL_RAMP_GRID_STEP = 3;
    const ROLL_RAMP_SUPPORT_WIDTH = 2, ROLL_RAMP_SUPPORT_DEPTH = 3;
    const ROLL_RAMP_DASH_LENGTH = 3, ROLL_RAMP_DASH_PERIOD = 6;
    const ROLL_RAMP_COLORS = { concrete: 7, shade: 8, edge: 15, grille: 0, direction: 14 };
    const PINE_ICON = 153, PINE_SMALL_COUNT = 6, PINE_GROUP_ICON_SCALE = 4, PINE_LAYOUT_SCALE = 256, PINE_TILE_WORLD_SIZE = 1024;
    const WHEAT_FIELD_ID = 215, TULIP_FIELD_ID = 216, VINE_FIELD_ID = 217;
    const CROP_ICON_SCALE = 4, CROP_ICON_SOIL = 200, CROP_ICON_EDGE_WIDTH = 1, CROP_ICON_GRAPE_COLOR = 171;
    const CROP_ICON_GRAPE_COUNT = 8;
    const CROP_ICON_GRAPE_STATIONS = Array.from({ length: CROP_ICON_GRAPE_COUNT }, (_, index) => (2 * index + 1) / (2 * CROP_ICON_GRAPE_COUNT));
    const CROP_FIELD_ICONS = new Map([
        [WHEAT_FIELD_ID, { name: 'wheat field', rows: 6, gap: 2, colors: [74] }],
        [TULIP_FIELD_ID, { name: 'tulip field', rows: 8, gap: 2, colors: [15, 10, 70, 54, 38, 167, 181, 9] }],
        [VINE_FIELD_ID, { name: 'vine field', rows: 12, scale: 8, gap: 6, colors: [90], edge: 92, grapes: true }]
    ]);
    // Keep layouts and unsigned hash arithmetic in sync with extended/extended_pine_trees.c.
    const PINE_TEN_LAYOUT = [[-330, 310, 128], [15, 360, 176], [312, 263, 144], [-197, 99, 112],
        [101, 128, 192], [348, -48, 152], [-365, -151, 168], [-61, -190, 120],
        [221, -311, 160], [-261, -367, 136]];
    const PINE_SIX_LAYOUT = [[-318, 274, 152], [-47, 355, 192], [305, 171, 136],
        [171, -143, 208], [-119, -326, 160], [-363, -89, 176]];
    const PINE_SCATTER = { columnSeed: 0x9e3779b1, rowSeed: 0x85ebca77, treeSeed: 0xc2b2ae3d,
        mixFirst: 0x7feb352d, mixSecond: 0x846ca68b, depthSeed: 0x68bc21eb, scaleSeed: 0x02e5be93,
        positionRange: 193, positionRadius: 96, largePositionRange: 129, largePositionRadius: 64,
        scaleRange: 33, scaleRadius: 16 };
    function pineScatterHash(value) {
        value = Math.imul(value ^ (value >>> 16), PINE_SCATTER.mixFirst);
        value = Math.imul(value ^ (value >>> 15), PINE_SCATTER.mixSecond);
        return (value ^ (value >>> 16)) >>> 0;
    }
    function pineTreeLayout(pineCount, column = 0, row = 0) {
        if (pineCount !== PINE_SMALL_COUNT && pineCount !== PINE_TEN_LAYOUT.length) fail('Invalid pine tree group.');
        const small = pineCount === PINE_SMALL_COUNT, layout = small ? PINE_SIX_LAYOUT : PINE_TEN_LAYOUT;
        const positionRange = small ? PINE_SCATTER.positionRange : PINE_SCATTER.largePositionRange;
        const positionRadius = small ? PINE_SCATTER.positionRadius : PINE_SCATTER.largePositionRadius;
        return layout.map(([x, z, scale], index) => {
            let hash = pineScatterHash(Math.imul(column, PINE_SCATTER.columnSeed) ^
                Math.imul(row, PINE_SCATTER.rowSeed) ^ Math.imul(index + 1, PINE_SCATTER.treeSeed));
            x += hash % positionRange - positionRadius;
            hash = pineScatterHash(hash ^ PINE_SCATTER.depthSeed);
            z += hash % positionRange - positionRadius;
            hash = pineScatterHash(hash ^ PINE_SCATTER.scaleSeed);
            scale += hash % PINE_SCATTER.scaleRange - PINE_SCATTER.scaleRadius;
            return [x, z, scale];
        });
    }
    const ROAD_TINT_NEUTRAL_RANGE = 24, ROAD_TINT_MIN_SHADE = 35, ROAD_TINT_MAX_SHADE = 225;
    const RGB_PALETTE_SIZE = 256 * 3, PALETTE_HEADER_SIZE = 16;
    const COMPRESSED_RESOURCE_EXTENSIONS = new Set(['pre', 'pvs', 'pes', 'xvs', 'p3s']);
    function bytes(value) { return value instanceof Uint8Array ? value : new Uint8Array(value); }
    function fail(message) { throw new Error(message); }
    function read24(a, p) { return a[p] | (a[p + 1] << 8) | (a[p + 2] << 16); }
    function decompress(input) {
        let a = bytes(input), passes = 1;
        if (a[0] & 128) { passes = a[0] & 127; a = a.subarray(4); }
        if (!passes || passes > 16) fail('Invalid resource compression passes.');
        while (passes--) {
            const length = read24(a, 1);
            if (a.length < 4 || !length || length > MAX_RESOURCE) fail('Invalid compressed resource size.');
            const out = new Uint8Array(length);
            if (a[0] === 1) {
                if (a.length < 9) fail('Truncated RLE resource.');
                const sourceLength = read24(a, 4), escapeCount = a[8] & 127;
                const lookup = new Uint8Array(256), escape = a.subarray(9, 9 + escapeCount);
                if (escape.length !== escapeCount) fail('Truncated RLE escape table.');
                escape.forEach((v, i) => { lookup[v] = i + 1; });
                let source = a.subarray(9 + escapeCount), p = 0;
                if (a[8] <= 128) {
                    const sequence = [], end = sourceLength;
                    if (source.length < end || escapeCount < 2) fail('Invalid RLE sequence stream.');
                    while (p < end) {
                        const v = source[p++];
                        if (v !== escape[1]) sequence.push(v);
                        else {
                            const run = [];
                            while (p < end && source[p] !== escape[1]) run.push(source[p++]);
                            if (p + 1 >= end) fail('Unterminated RLE sequence.');
                            p++;
                            const repeats = source[p++];
                            for (let r = 0; r < repeats; r++) {
                                if (sequence.length + run.length > MAX_RESOURCE) fail('RLE sequence is too large.');
                                for (const value of run) sequence.push(value);
                            }
                        }
                        if (sequence.length > MAX_RESOURCE) fail('RLE sequence is too large.');
                    }
                    source = Uint8Array.from(sequence); p = 0;
                }
                let q = 0;
                while (q < length) {
                    if (p >= source.length) fail('Truncated RLE pixels.');
                    let v = source[p++], count = lookup[v], n = 1;
                    if (count) {
                        if (count === 1) n = source[p++];
                        else if (count === 3) { n = source[p] | (source[p + 1] << 8); p += 2; }
                        else n = count - 1;
                        if (p >= source.length || !n) fail('Invalid RLE run.');
                        v = source[p++];
                    }
                    if (q + n > length) fail('RLE run exceeds declared size.');
                    out.fill(v, q, q + n); q += n;
                }
            } else if (a[0] === 2) {
                const widths = a[4] & 127, additive = !!(a[4] & 128);
                if (!widths || widths > 16 || a.length < 5 + widths) fail('Invalid VLE alphabet.');
                const counts = a.subarray(5, 5 + widths), limits = [], offsets = [];
                let total = 0, code = 0;
                counts.forEach((count, i) => { offsets[i] = total - code; code += count; limits[i] = code; total += count; code *= 2; });
                if (total > 256 || a.length < 5 + widths + total) fail('Invalid VLE symbols.');
                const alphabet = a.subarray(5 + widths, 5 + widths + total);
                let bit = (5 + widths + total) * 8, previous = 0;
                for (let i = 0; i < length; i++) {
                    let word = 0, found = false;
                    for (let width = 0; width < widths; width++) {
                        if (bit >= a.length * 8) fail('Truncated VLE stream.');
                        word = word * 2 + ((a[bit >>> 3] >>> (7 - (bit & 7))) & 1); bit++;
                        if (word < limits[width]) {
                            const index = word + offsets[width];
                            if (index < 0 || index >= alphabet.length) fail('Invalid VLE code.');
                            previous = additive ? (previous + alphabet[index]) & 255 : alphabet[index];
                            out[i] = previous; found = true; break;
                        }
                    }
                    if (!found) fail('Invalid VLE code width.');
                }
            } else fail('Unsupported resource compression.');
            a = out;
        }
        return a;
    }
    function resourceDirectory(input) {
        const a = bytes(input), view = new DataView(a.buffer, a.byteOffset, a.byteLength);
        if (a.length < 6) fail('Truncated game resource.');
        const size = view.getUint32(0, true), count = view.getUint16(4, true), base = 6 + count * 8;
        if (size < base || size > a.length || count > 4096) fail('Invalid game resource directory.');
        const entries = [], result = new Map();
        for (let i = 0; i < count; i++) {
            const id = String.fromCharCode(...a.subarray(6 + i * 4, 10 + i * 4));
            const start = base + view.getUint32(6 + count * 4 + i * 4, true);
            if (start > size || result.has(id)) fail('Invalid game resource entry.');
            entries.push({ id, start }); result.set(id, null);
        }
        for (const entry of entries) {
            let end = size;
            for (const other of entries) if (other.start > entry.start) end = Math.min(end, other.start);
            result.set(entry.id, a.subarray(entry.start, end));
        }
        return result;
    }
    function unflip(source, width, height, mode) {
        const out = new Uint8Array(width * height), even = Math.ceil(height / 2), odd = Math.floor(height / 2);
        for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
            let p = x + y * width;
            if (mode === 1) p = x * height + y;
            if (mode === 2) p = x * height + (y >>> 1) + ((y & 1) ? even : 0);
            if (mode === 3) p = (y & 1) ? width * even + x * odd + (y >>> 1) : x * even + (y >>> 1);
            out[x + y * width] = source[p];
        }
        return out;
    }
    function decodeShape(input, planar, palette) {
        const a = bytes(input);
        if (a.length < 16) fail('Truncated editor bitmap.');
        const v = new DataView(a.buffer, a.byteOffset, a.byteLength), w = v.getUint16(0, true), h = v.getUint16(2, true);
        if (!w || !h || w * h > 65536) fail('Invalid editor bitmap dimensions.');
        let pixels, width = w;
        if (planar) {
            width *= 8; pixels = new Uint8Array(width * h); pixels.fill(a[13] >>> 4);
            let offset = 16, transpose = !(a[15] & 240) ? a[14] >>> 4 : 0;
            for (let plane = 0; plane < 4; plane++) {
                const pattern = a[12 + plane] & 15;
                if (!pattern) break;
                if (offset + w * h > a.length) fail('Truncated planar editor bitmap.');
                let source = a.subarray(offset, offset + w * h); offset += w * h;
                if (transpose & (1 << plane)) source = unflip(source, w, h, 1);
                for (let p = 0; p < source.length; p++) for (let bit = 0; bit < 8; bit++)
                    if (source[p] & (128 >>> bit)) pixels[p * 8 + bit] |= pattern;
            }
            if (palette) pixels = pixels.map(p => palette[p]);
        } else {
            if (16 + w * h > a.length) fail('Truncated editor bitmap pixels.');
            pixels = a.slice(16, 16 + w * h);
            const mode = !(a[15] & 240) ? a[14] >>> 4 : 0;
            if (mode) pixels = unflip(pixels, w, h, mode);
        }
        return { width, height: h, pixels };
    }
    function composeTileBitmap(bitmap) {
        const mask = bitmap && bitmap.mask;
        if (!mask || !Number.isInteger(mask.width) || !Number.isInteger(mask.height) ||
            mask.width < EDITOR_TILE_SIZE || mask.height < EDITOR_TILE_SIZE ||
            mask.width % EDITOR_TILE_SIZE || mask.height % EDITOR_TILE_SIZE ||
            !mask.pixels || mask.pixels.length !== mask.width * mask.height ||
            !Number.isInteger(bitmap.width) || !Number.isInteger(bitmap.height) ||
            bitmap.width < 1 || bitmap.height < 1 || !bitmap.pixels ||
            bitmap.pixels.length !== bitmap.width * bitmap.height) fail('Invalid editor tile artwork.');
        const width = mask.width / EDITOR_TILE_SIZE * EDITOR_TILE_INTERIOR;
        const height = mask.height / EDITOR_TILE_SIZE * EDITOR_TILE_INTERIOR;
        const pixels = new Uint8Array(width * height), alpha = new Uint8Array(width * height);
        for (let y = 0; y < height; y++) {
            // Native icons reserve each tile's left column and bottom row for the editor grid.
            const sourceY = Math.floor(y / EDITOR_TILE_INTERIOR) * EDITOR_TILE_SIZE + y % EDITOR_TILE_INTERIOR;
            for (let x = 0; x < width; x++) {
                const sourceX = Math.floor(x / EDITOR_TILE_INTERIOR) * EDITOR_TILE_SIZE + x % EDITOR_TILE_INTERIOR + 1;
                const pixel = sourceX < bitmap.width && sourceY < bitmap.height
                    ? bitmap.pixels[sourceY * bitmap.width + sourceX] : 0;
                const index = y * width + x;
                pixels[index] = pixel;
                // The full AND mask includes opaque black pixels beyond the cropped OR image.
                alpha[index] = pixel !== 0 || mask.pixels[sourceY * mask.width + sourceX] === 0 ? 255 : 0;
            }
        }
        return { width, height, pixels, alpha };
    }
    function composeRollRampBitmap(id) {
        const width = EDITOR_TILE_INTERIOR, height = width * ROLL_RAMP_TILE_COUNT;
        const pixels = new Uint8Array(width * height), alpha = new Uint8Array(width * height);
        const center = Math.floor(width / 2), lipRow = height - ROLL_RAMP_SUPPORT_DEPTH - 1;
        function pixel(x, y, color) {
            if (x < 0 || x >= width || y < 0 || y >= height) return;
            const index = y * width + x;
            pixels[index] = color; alpha[index] = 255;
        }
        // Native yaw zero enters at +z (north on the map) and launches south.
        // Keep the concrete entry above the grille so artwork and routing agree.
        for (let y = 0; y < height; y++) {
            const concrete = y < EDITOR_TILE_INTERIOR;
            const half = concrete ? ROLL_RAMP_HALF_WIDTH : ROLL_RAMP_LIP_HALF_WIDTH +
                Math.floor((height - 1 - y) * (ROLL_RAMP_HALF_WIDTH - ROLL_RAMP_LIP_HALF_WIDTH) / (EDITOR_TILE_INTERIOR - 1));
            for (let x = center - half; x <= center + half; x++) {
                const edge = x === center - half || x === center + half;
                if (concrete || edge || x % ROLL_RAMP_GRID_STEP === 0 || y % ROLL_RAMP_GRID_STEP === 0)
                    pixel(x, y, edge ? (x < center ? ROLL_RAMP_COLORS.shade : ROLL_RAMP_COLORS.edge)
                        : concrete ? ROLL_RAMP_COLORS.concrete : ROLL_RAMP_COLORS.grille);
            }
        }
        // Small concrete supports sit beside the elevated grille. Its holes
        // remain transparent so the launch tile is visibly different.
        for (const y of [EDITOR_TILE_INTERIOR + ROLL_RAMP_SUPPORT_DEPTH, lipRow]) {
            for (const x of [0, width - ROLL_RAMP_SUPPORT_WIDTH]) {
                for (let dy = 0; dy < ROLL_RAMP_SUPPORT_DEPTH; dy++)
                    for (let dx = 0; dx < ROLL_RAMP_SUPPORT_WIDTH; dx++)
                        pixel(x + dx, y + dy, dx ? ROLL_RAMP_COLORS.concrete : ROLL_RAMP_COLORS.shade);
            }
        }
        // A diagonal lip shows the final bank; the entry arrow makes this
        // asymmetric stunt distinguishable after a half-turn.
        for (let dx = -ROLL_RAMP_LIP_HALF_WIDTH; dx <= ROLL_RAMP_LIP_HALF_WIDTH; dx++)
            pixel(center + dx, lipRow - dx, ROLL_RAMP_COLORS.direction);
        for (let y = 0; y < height; y++)
            if (y % ROLL_RAMP_DASH_PERIOD < ROLL_RAMP_DASH_LENGTH) pixel(center, y, ROLL_RAMP_COLORS.direction);
        const arrowTip = ROLL_RAMP_HALF_WIDTH;
        for (let offset = 0; offset <= ROLL_RAMP_LIP_HALF_WIDTH; offset++) {
            pixel(center - offset, arrowTip - offset, ROLL_RAMP_COLORS.direction);
            pixel(center + offset, arrowTip - offset, ROLL_RAMP_COLORS.direction);
        }
        for (let y = 0; y <= arrowTip; y++) pixel(center, y, ROLL_RAMP_COLORS.direction);
        if (id === ROLL_RAMP_VERTICAL_ICON) return { width, height, pixels, alpha };
        const rotated = new Uint8Array(pixels.length), rotatedAlpha = new Uint8Array(alpha.length);
        for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
            const source = y * width + x, destination = x * height + height - 1 - y;
            rotated[destination] = pixels[source]; rotatedAlpha[destination] = alpha[source];
        }
        return { width: height, height: width, pixels: rotated, alpha: rotatedAlpha };
    }
    function composePineTreesBitmap(icons, pineCount, column, row) {
        const source = composeTileBitmap(icons.get(PINE_ICON));
        const width = EDITOR_TILE_INTERIOR * PINE_GROUP_ICON_SCALE, height = width;
        const pixels = new Uint8Array(width * height), alpha = new Uint8Array(width * height);
        const trees = pineTreeLayout(pineCount, column, row);
        const topPadding = Math.ceil(source.height * Math.max(...trees.map(tree => tree[2])) / PINE_LAYOUT_SCALE);
        // Paint the northern trees first, keeping the individual silhouettes
        // and their trunks legible on the transparent terrain background.
        for (const [x, z, scale] of trees.sort((first, second) => second[1] - first[1])) {
            const treeWidth = Math.max(1, Math.round(source.width * scale / PINE_LAYOUT_SCALE));
            const treeHeight = Math.max(1, Math.round(source.height * scale / PINE_LAYOUT_SCALE));
            const left = Math.round((x / PINE_TILE_WORLD_SIZE + .5) * width - treeWidth / 2);
            const top = Math.round(topPadding + (.5 - z / PINE_TILE_WORLD_SIZE) * (height - topPadding) - treeHeight);
            for (let dy = 0; dy < treeHeight; dy++) for (let dx = 0; dx < treeWidth; dx++) {
                const sourceX = Math.floor(dx * source.width / treeWidth), sourceY = Math.floor(dy * source.height / treeHeight);
                const from = sourceY * source.width + sourceX, targetX = left + dx, targetY = top + dy;
                if (!source.alpha[from] || targetX < 0 || targetX >= width || targetY < 0 || targetY >= height) continue;
                const to = targetY * width + targetX;
                pixels[to] = source.pixels[from]; alpha[to] = source.alpha[from];
            }
        }
        return { width, height, pixels, alpha };
    }
    function composeCropFieldBitmap(field) {
        const width = EDITOR_TILE_INTERIOR * (field.scale || CROP_ICON_SCALE), height = width;
        const pixels = new Uint8Array(width * height).fill(CROP_ICON_SOIL);
        const alpha = new Uint8Array(pixels.length).fill(255);
        function pixel(x, y, color) {
            if (x >= 0 && x < width && y >= 0 && y < height) pixels[y * width + x] = color;
        }
        const rowWidth = Math.floor(width / field.rows) - field.gap;
        for (let row = 0; row < field.rows; row++) {
            const x = Math.floor((row + .5) * width / field.rows), halfWidth = Math.floor(rowWidth / 2);
            for (let y = 0; y < height; y++) for (let dx = -halfWidth; dx <= halfWidth; dx++)
                pixel(x + dx, y, field.edge !== undefined && Math.abs(dx) > halfWidth - CROP_ICON_EDGE_WIDTH
                    ? field.edge : field.colors[row % field.colors.length]);
            if (field.grapes) for (const station of CROP_ICON_GRAPE_STATIONS) for (const side of [-1, 1]) {
                const y = Math.floor(station * height), edge = x + side * halfWidth;
                for (const [dx, dy] of [[0, 0], [1, 0], [0, 1], [1, 1], [0, 2]]) pixel(edge + side * dx, y + dy, CROP_ICON_GRAPE_COLOR);
            }
        }
        return { width, height, pixels, alpha };
    }
    function composePieceTileBitmap(icons, id, tunnelSlalom = 0, rollRamp = 0, pineCount = 0, column = 0, row = 0) {
        const field = CROP_FIELD_ICONS.get(id);
        if (field) {
            if (tunnelSlalom || rollRamp || pineCount) fail('Invalid ' + field.name + ' artwork flags.');
            return composeCropFieldBitmap(field);
        }
        if (pineCount) {
            if (id !== PINE_ICON || tunnelSlalom || rollRamp || ![PINE_SMALL_COUNT, PINE_TEN_LAYOUT.length].includes(pineCount))
                fail('Invalid pine tree group artwork.');
            return composePineTreesBitmap(icons, pineCount, column, row);
        }
        if (rollRamp) {
            if (rollRamp !== 1 || tunnelSlalom ||
                (id !== ROLL_RAMP_VERTICAL_ICON && id !== ROLL_RAMP_HORIZONTAL_ICON)) fail('Invalid barrel roll ramp artwork.');
            return composeRollRampBitmap(id);
        }
        const slalom = composeTileBitmap(icons.get(id));
        if (!tunnelSlalom) return slalom;
        const parts = TUNNEL_SLALOM_ICONS.get(id);
        if (tunnelSlalom !== 1 || !parts) fail('Invalid tunnel with slalom artwork.');
        const tunnel = composeTileBitmap(icons.get(parts.tunnel));
        const road = composeTileBitmap(icons.get(parts.road));
        if (slalom.width !== tunnel.width || slalom.height !== tunnel.height ||
            road.width !== tunnel.width || road.height !== tunnel.height) fail('Incompatible stunt artwork.');
        const combined = { width: tunnel.width, height: tunnel.height,
            pixels: tunnel.pixels.slice(), alpha: tunnel.alpha.slice() };
        // Keep the tunnel shell and add only the slalom barriers; its opaque
        // road background would otherwise cover the walls of the tunnel.
        for (let index = 0; index < slalom.pixels.length; index++) {
            if (slalom.alpha[index] && (slalom.pixels[index] !== road.pixels[index] ||
                slalom.alpha[index] !== road.alpha[index])) {
                combined.pixels[index] = slalom.pixels[index];
                combined.alpha[index] = slalom.alpha[index];
            }
        }
        return combined;
    }
    function tintTileBitmap(bitmap, palette, surface = 0, lowerSurface = 0, overpassId = 0) {
        const rgba = new Uint8ClampedArray(bitmap.pixels.length * 4);
        for (let i = 0; i < bitmap.pixels.length; i++) {
            const index = bitmap.pixels[i];
            let r = palette[index * 3], g = palette[index * 3 + 1], b = palette[index * 3 + 2];
            // The native overpass artwork offsets its raised deck toward the top/left.
            // The visible lower road continues from the right/bottom half of the tile.
            const lowerRoad = overpassId === OVERPASS_VERTICAL_ICON && i % bitmap.width >= bitmap.width / 2 ||
                overpassId === OVERPASS_HORIZONTAL_ICON && Math.floor(i / bitmap.width) >= bitmap.height / 2;
            const paint = lowerRoad ? lowerSurface : surface;
            if (paint && Math.max(r, g, b) - Math.min(r, g, b) < ROAD_TINT_NEUTRAL_RANGE &&
                r > ROAD_TINT_MIN_SHADE && r < ROAD_TINT_MAX_SHADE) {
                if (paint === ROAD_SURFACE_DIRT) { g = r * .72; b = r * .45; }
                else if (paint === ROAD_SURFACE_ICE) { b = Math.min(255, r + 55); g = Math.min(255, r + 32); r = Math.min(255, r + 8); }
            }
            rgba[i * 4] = r; rgba[i * 4 + 1] = g; rgba[i * 4 + 2] = b;
            rgba[i * 4 + 3] = bitmap.alpha[i];
        }
        return rgba;
    }
    function normalizeFiles(files) {
        const normalized = new Map();
        for (const [name, value] of files) normalized.set(name.toLowerCase(), value);
        return normalized;
    }
    async function readResource(files, name, extensions) {
        for (const extension of extensions) {
            const filename = name + '.' + extension, value = files.get(filename);
            if (value) {
                try {
                    let data = bytes(value.arrayBuffer ? await value.arrayBuffer() : value);
                    if (COMPRESSED_RESOURCE_EXTENSIONS.has(extension)) data = decompress(data);
                    return { directory: resourceDirectory(data), extension };
                } catch (error) { fail('Cannot read ' + filename.toUpperCase() + ': ' + error.message); }
            }
        }
        fail('Select a Stunts folder containing ' + name.toUpperCase() + ' game resources (' +
            extensions.map(extension => name.toUpperCase() + '.' + extension.toUpperCase()).join(' or ') + ').');
    }
    async function loadPalette(files) {
        const main = await readResource(files, 'sdmain', ['pvs', 'xvs', 'pes', 'esh', 'vsh']);
        const colorShape = main.directory.get('!pal');
        if (!colorShape || colorShape.length < PALETTE_HEADER_SIZE + RGB_PALETTE_SIZE)
            fail('The selected folder is missing its VGA palette.');
        return colorShape.slice(PALETTE_HEADER_SIZE, PALETTE_HEADER_SIZE + RGB_PALETTE_SIZE)
            .map(n => ((n & 63) << 2) | ((n & 63) >>> 4));
    }
    async function load3D(files, palette) {
        const normalized = normalizeFiles(files), models = new Map();
        // The game searches GAME1 before GAME2; duplicate names such as zban
        // intentionally have different geometry in the two archives.
        for (const name of ['game1', 'game2']) {
            const resource = await readResource(normalized, name, ['3sh', 'p3s']);
            for (const [id, shape] of resource.directory) if (!models.has(id)) models.set(id, shape);
        }
        if (!models.size) fail('The selected folder has no 3D track models.');
        const rgbPalette = palette === undefined ? await loadPalette(normalized) : bytes(palette);
        if (rgbPalette.length !== RGB_PALETTE_SIZE) fail('The 3D preview requires a complete VGA palette.');
        return { models, palette: rgbPalette };
    }
    async function load(files) {
        const normalized = normalizeFiles(files);
        const read = (name, extensions) => readResource(normalized, name, extensions);
        const text = await read('tedit', ['res', 'pre']), shapes = await read('sdtedit', ['pvs', 'xvs', 'pes', 'esh', 'vsh']);
        const rgbPalette = await loadPalette(normalized);
        const shapeNames = text.directory.get('snam'), maskNames = text.directory.get('mnam'), pages = text.directory.get('pbox');
        if (!shapeNames || !maskNames || shapeNames.length < 186 * 4 || maskNames.length < 186 * 4 || !pages || pages.length < 396)
            fail('The selected folder has incomplete track editor resources.');
        const planar = ['pes', 'esh'].includes(shapes.extension), paletteShape = shapes.directory.get('!MGA');
        const palette = paletteShape && paletteShape.subarray(16, 32), icons = new Map(), terrain = new Map(), names = new Map();
        const getShape = id => { const a = shapes.directory.get(id); if (!a) fail('Missing editor bitmap ' + id + '.'); return decodeShape(a, planar, palette); };
        for (let id = 0; id < 186; id++) {
            const shape = getShape(String.fromCharCode(...shapeNames.subarray(id * 4, id * 4 + 4)));
            shape.mask = getShape(String.fromCharCode(...maskNames.subarray(id * 4, id * 4 + 4)));
            icons.set(id, shape);
        }
        const terrainNames = 'flatlakelak1lak2lak3lak4highgoungouwgousgouegou1gou2gou3gou4gou5gou6gou7gou8';
        for (let id = 0; id < 19; id++) terrain.set(id, getShape(terrainNames.slice(id * 4, id * 4 + 4)));
        const groups = [];
        for (let id = 1; id <= 10; id++) groups.push({ id, name: 'F' + id, tiles: [...new Set(pages.subarray(id * 36, (id + 1) * 36))].filter(n => n > 0 && n < 186) });
        return { icons, terrain, groups, names, palette: rgbPalette };
    }
    return { load, load3D, decompress, resourceDirectory, decodeShape, composeTileBitmap, composePieceTileBitmap, tintTileBitmap, pineTreeLayout };
}));
