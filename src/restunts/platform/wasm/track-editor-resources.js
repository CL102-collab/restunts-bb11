/* Original artwork is read from the selected game folder, never bundled. */
(function (root, factory) {
    const api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.TrackEditorResources = api;
}(typeof globalThis !== 'undefined' ? globalThis : this, function () {
    'use strict';
    const MAX_RESOURCE = 16 * 1024 * 1024;
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
    async function load(files) {
        const normalized = new Map();
        for (const [name, value] of files) normalized.set(name.toLowerCase(), value);
        async function read(name, extensions) {
            for (const extension of extensions) {
                const filename = name + '.' + extension, value = normalized.get(filename);
                if (value) {
                    let data = bytes(value.arrayBuffer ? await value.arrayBuffer() : value);
                    if (['pre', 'pvs', 'pes', 'xvs'].includes(extension)) data = decompress(data);
                    return { directory: resourceDirectory(data), extension };
                }
            }
            fail('Select a Stunts folder containing ' + name.toUpperCase() + ' game resources.');
        }
        const text = await read('tedit', ['res', 'pre']), shapes = await read('sdtedit', ['pvs', 'xvs', 'pes', 'esh', 'vsh']);
        const main = await read('sdmain', ['pvs', 'xvs', 'pes', 'esh', 'vsh']);
        const colorShape = main.directory.get('!pal');
        if (!colorShape || colorShape.length < 784) fail('The selected folder is missing its VGA palette.');
        const rgbPalette = colorShape.slice(16, 784).map(n => ((n & 63) << 2) | ((n & 63) >>> 4));
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
    return { load, decompress, resourceDirectory, decodeShape };
}));
