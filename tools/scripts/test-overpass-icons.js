#!/usr/bin/env node
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const resources = require('../../src/restunts/platform/wasm/track-editor-resources.js');

const TILE_SIZE = 15, PALETTE_SIZE = 256 * 3;
const ASPHALT_INDEX = 8, CURB_INDEX = 7, WHITE_INDEX = 15;
const ROAD_RGBA = [[85, 85, 85, 255], [85, 61, 38, 255], [93, 117, 140, 255]];
function rgbaAt(rgba, width, x, y) { return Array.from(rgba.slice((y * width + x) * 4, (y * width + x + 1) * 4)); }
function syntheticOverpass(horizontal) {
    const pixels = new Uint8Array(TILE_SIZE * TILE_SIZE), alpha = new Uint8Array(pixels.length);
    function paint(x, y, index) {
        const location = horizontal ? x * TILE_SIZE + y : y * TILE_SIZE + x;
        pixels[location] = index; alpha[location] = 255;
    }
    for (let y = 0; y < TILE_SIZE; y++) {
        for (let x = 1; x <= 5; x++) paint(x, y, ASPHALT_INDEX);
        paint(0, y, CURB_INDEX); paint(6, y, CURB_INDEX); paint(7, y, WHITE_INDEX);
    }
    for (let y = 5; y <= 9; y++) for (let x = 8; x < TILE_SIZE; x++) paint(x, y, ASPHALT_INDEX);
    return { width: TILE_SIZE, height: TILE_SIZE, pixels, alpha };
}
function checkOverpass(bitmap, palette, id) {
    const horizontal = id === 102;
    const upper = horizontal ? [7, 3] : [3, 7], lower = horizontal ? [7, 11] : [11, 7];
    const white = [7, 7], empty = horizontal ? [0, 12] : [12, 0];
    for (let surface = 0; surface < ROAD_RGBA.length; surface++) for (let lowerSurface = 0; lowerSurface < ROAD_RGBA.length; lowerSurface++) {
        const rgba = resources.tintTileBitmap(bitmap, palette, surface, lowerSurface, id);
        assert.deepEqual(rgbaAt(rgba, bitmap.width, ...upper), ROAD_RGBA[surface], id + ': upper deck follows the road selector');
        assert.deepEqual(rgbaAt(rgba, bitmap.width, ...lower), ROAD_RGBA[lowerSurface], id + ': lower road follows the palette variant');
        assert.deepEqual(rgbaAt(rgba, bitmap.width, ...white), [255, 255, 255, 255], id + ': white structural edge stays intact');
        assert.equal(rgbaAt(rgba, bitmap.width, ...empty)[3], 0, id + ': transparent terrain stays transparent');
    }
    for (let surface = 0; surface < ROAD_RGBA.length; surface++) {
        assert.deepEqual(resources.tintTileBitmap(bitmap, palette, surface, surface, id),
            resources.tintTileBitmap(bitmap, palette, surface), id + ': equal decks preserve the existing preview tint');
    }
}
const palette = new Uint8Array(PALETTE_SIZE);
palette.set([85, 85, 85], ASPHALT_INDEX * 3); palette.set([170, 170, 170], CURB_INDEX * 3); palette.set([255, 255, 255], WHITE_INDEX * 3);
checkOverpass(syntheticOverpass(false), palette, 101);
checkOverpass(syntheticOverpass(true), palette, 102);
console.log('PASS all nine overpass preview combinations keep upper and lower road colors independent');
(async () => {
    const directory = path.resolve(__dirname, '../../stunts');
    if (!fs.existsSync(path.join(directory, 'TEDIT.PRE')) && !fs.existsSync(path.join(directory, 'TEDIT.RES'))) return;
    const files = new Map(fs.readdirSync(directory).filter(name => fs.statSync(path.join(directory, name)).isFile()).map(name => [name, fs.readFileSync(path.join(directory, name))]));
    const decoded = await resources.load(files);
    for (const id of [101, 102]) checkOverpass(resources.composeTileBitmap(decoded.icons.get(id)), decoded.palette, id);
    console.log('PASS original overpass artwork matches both independent preview layers');
})();
