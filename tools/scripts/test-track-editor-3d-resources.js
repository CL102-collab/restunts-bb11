#!/usr/bin/env node
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { test } = require('node:test');
const resources = require('../../src/restunts/platform/wasm/track-editor-resources.js');
const model = require('../../src/restunts/platform/wasm/track-editor-model.js');

const PALETTE_SIZE = 256 * 3, SHAPE_HEADER_SIZE = 16;
const palette = Uint8Array.from({ length: PALETTE_SIZE }, (_, index) => index % 256);
function archive(entries) {
    const headerSize = 6 + entries.length * 8;
    const data = new Uint8Array(headerSize + entries.reduce((size, entry) => size + entry[1].length, 0));
    const view = new DataView(data.buffer);
    view.setUint32(0, data.length, true); view.setUint16(4, entries.length, true);
    let offset = 0;
    for (let index = 0; index < entries.length; index++) {
        const [id, shape] = entries[index];
        assert.equal(id.length, 4);
        data.set(Buffer.from(id), 6 + index * 4);
        view.setUint32(6 + entries.length * 4 + index * 4, offset, true);
        data.set(shape, headerSize + offset); offset += shape.length;
    }
    return data;
}
function compressed(data) {
    const escaped = [];
    for (const value of data) escaped.push(...value === 255 ? [255, 1, value] : [value]);
    return Uint8Array.from([1, data.length & 255, data.length >>> 8 & 255, data.length >>> 16,
        escaped.length & 255, escaped.length >>> 8 & 255, escaped.length >>> 16, 0, 129, 255, ...escaped]);
}
function files() {
    return new Map([
        ['GAME1.3SH', archive([['pine', Uint8Array.from([1, 2, 3, 4])], ['zban', Uint8Array.from([5, 6])]])],
        ['GaMe2.3sH', archive([['zban', Uint8Array.from([7, 8])], ['road', Uint8Array.from([9, 10])]])]
    ]);
}

test('wheat field artwork draws golden crop rows without a native bitmap', () => {
    const bitmap = resources.composePieceTileBitmap(new Map(), model.WHEAT_FIELD_ID);
    assert.equal(bitmap.width, 60); assert.equal(bitmap.height, 60);
    assert.equal(bitmap.pixels.length, bitmap.width * bitmap.height);
    assert.equal(bitmap.alpha.length, bitmap.pixels.length);
    assert(bitmap.alpha.every(value => value === 255));
    assert.deepEqual([...new Set(bitmap.pixels)].sort((first, second) => first - second), [74, 200], 'only soil and golden rows remain');
    for (let row = 1; row < bitmap.height; row++)
        assert.deepEqual(bitmap.pixels.subarray(row * bitmap.width, (row + 1) * bitmap.width), bitmap.pixels.subarray(0, bitmap.width), 'crop rows have no stalk decorations');
    const next = resources.composePieceTileBitmap(new Map(), model.WHEAT_FIELD_ID);
    assert.deepEqual(next, bitmap); assert.notEqual(next.pixels, bitmap.pixels);
    for (const flags of [[1, 0, 0], [0, 1, 0], [0, 0, 6]])
        assert.throws(() => resources.composePieceTileBitmap(new Map(), model.WHEAT_FIELD_ID, ...flags), /wheat field artwork flags/);
});

test('3D loader accepts case-insensitive File inputs and preserves native archive precedence', async () => {
    const selected = files(), first = selected.get('GAME1.3SH');
    selected.set('GAME1.3SH', { arrayBuffer: async () => first.buffer });
    const assets = await resources.load3D(selected, palette);
    assert(assets.models instanceof Map);
    assert.equal(assets.models.size, 3);
    assert.deepEqual([...assets.models.get('pine')], [1, 2, 3, 4]);
    assert.deepEqual([...assets.models.get('zban')], [5, 6]);
    assert.deepEqual([...assets.models.get('road')], [9, 10]);
    assert.deepEqual(assets.palette, palette);
});

test('3D loader decompresses P3S archives and prefers uncompressed overrides', async () => {
    const selected = new Map([...files()].map(([name, data]) => [name.replace(/3sh/i, 'P3S'), compressed(data)]));
    const assets = await resources.load3D(selected, palette);
    assert.deepEqual([...assets.models.get('pine')], [1, 2, 3, 4]);
    selected.set('GAME1.3SH', archive([['pine', Uint8Array.from([30, 40])]]));
    selected.set('GAME1.P3S', { arrayBuffer: async () => { throw new Error('Unused compressed archive'); } });
    assert.deepEqual([...(await resources.load3D(selected, palette)).models.get('pine')], [30, 40]);
});

test('3D loader names missing and corrupt archives without loading any models over the network', async () => {
    await assert.rejects(resources.load3D(new Map(), palette), /GAME1\.3SH or GAME1\.P3S/);
    const missingSecond = files(); missingSecond.delete('GaMe2.3sH');
    await assert.rejects(resources.load3D(missingSecond, palette), /GAME2\.3SH or GAME2\.P3S/);
    const corrupt = files(); corrupt.set('GAME1.3SH', Uint8Array.from([0]));
    await assert.rejects(resources.load3D(corrupt, palette), /Cannot read GAME1\.3SH: Truncated game resource/);
    corrupt.delete('GAME1.3SH'); corrupt.set('GAME1.P3S', Uint8Array.from([1]));
    await assert.rejects(resources.load3D(corrupt, palette), /Cannot read GAME1\.P3S:/);
    const unreadable = files(); unreadable.set('GAME1.3SH', { arrayBuffer: async () => { throw new Error('Access revoked'); } });
    await assert.rejects(resources.load3D(unreadable, palette), /GAME1\.3SH: Access revoked/);
});

test('3D loader can read the selected folder VGA palette when one is not supplied', async () => {
    const selected = files(), shape = new Uint8Array(SHAPE_HEADER_SIZE + PALETTE_SIZE);
    shape.set([0, 1, 31, 63, 64, 255], SHAPE_HEADER_SIZE);
    selected.set('SDMAIN.VSH', archive([['!pal', shape]]));
    const assets = await resources.load3D(selected);
    assert.equal(assets.palette.length, PALETTE_SIZE);
    assert.deepEqual([...assets.palette.slice(0, 6)], [0, 4, 125, 255, 0, 255]);
    selected.set('SDMAIN.VSH', archive([['!pal', new Uint8Array(SHAPE_HEADER_SIZE)]]));
    await assert.rejects(resources.load3D(selected), /missing its VGA palette/);
    selected.delete('SDMAIN.VSH');
    await assert.rejects(resources.load3D(selected), /SDMAIN/);
    await assert.rejects(resources.load3D(files(), new Uint8Array(PALETTE_SIZE - 1)), /complete VGA palette/);
});

test('3D loader rejects empty archives instead of silently opening an empty preview', async () => {
    const empty = archive([]);
    await assert.rejects(resources.load3D(new Map([['game1.3sh', empty], ['game2.3sh', empty]]), palette), /no 3D track models/);
});

test('local Stunts 3D assets load lazily alongside the ordinary 2D resources', async context => {
    const directory = path.resolve(__dirname, '../../stunts');
    if (!fs.existsSync(path.join(directory, 'GAME1.P3S'))) return context.skip('Original game assets are optional local inputs.');
    const selected = new Map(fs.readdirSync(directory).filter(name => fs.statSync(path.join(directory, name)).isFile())
        .map(name => [name, fs.readFileSync(path.join(directory, name))]));
    const only2D = new Map([...selected].filter(([name]) => !/^GAME[12]\./i.test(name)));
    const editor = await resources.load(only2D);
    assert.equal(editor.icons.size, 186);
    const assets = await resources.load3D(selected, editor.palette);
    assert.equal(assets.models.size, 116);
    assert.equal(assets.models.get('zban').length, 84);
    assert.equal(assets.palette.length, PALETTE_SIZE);
    assert.deepEqual(assets.palette, editor.palette);
});
