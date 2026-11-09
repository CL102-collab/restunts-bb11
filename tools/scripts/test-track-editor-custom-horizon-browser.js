#!/usr/bin/env node
'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const { deflateSync } = require('node:zlib');
const { chromium } = require('playwright');
const { serveHtml } = require('./browser-test-server.js');
const model = require('../../src/restunts/platform/wasm/track-editor-model.js');

const ROOT = path.resolve(__dirname, '../..');
const WIDTH = 4096, TIMEOUT_MS = 30000, PNG_RGB = 2, PNG_INDEXED = 3;
const UNICODE_FILENAME = 'Északi fény.PNG';
const CUSTOM_FILES = [UNICODE_FILENAME, 'Far fields.png', 'Boundary.png', 'Short.png'];

function options() {
    const result = {};
    for (let index = 2; index < process.argv.length; index += 2) {
        const option = process.argv[index];
        assert(['--data-dir', '--browser-executable', '--output'].includes(option), 'Unknown option: ' + option);
        assert(process.argv[index + 1], 'Missing value for ' + option);
        result[option.slice(2)] = path.resolve(process.argv[index + 1]);
    }
    assert(result['data-dir'], 'Use --data-dir to name your original Stunts folder.');
    return result;
}
function chunk(type, data) {
    const bytes = Buffer.alloc(data.length + 12); bytes.writeUInt32BE(data.length); bytes.write(type, 4); bytes.set(data, 8);
    let crc = 0xffffffff;
    for (let index = 4; index < data.length + 8; index++) {
        crc ^= bytes[index];
        for (let bit = 0; bit < 8; bit++) crc = crc & 1 ? 0xedb88320 ^ crc >>> 1 : crc >>> 1;
    }
    bytes.writeUInt32BE((crc ^ 0xffffffff) >>> 0, data.length + 8); return bytes;
}
function png(height, type = PNG_RGB) {
    const header = Buffer.alloc(13); header.writeUInt32BE(WIDTH); header.writeUInt32BE(height, 4); header[8] = 8; header[9] = type;
    const channels = type === PNG_INDEXED ? 1 : 3, stride = WIDTH * channels + 1, raw = Buffer.alloc(stride * height);
    for (let row = 0; row < height; row++) for (let column = 0; column < WIDTH; column++) {
        const ground = row >= height - 12, offset = row * stride + 1 + column * channels;
        if (type === PNG_INDEXED) raw[offset] = ground ? 1 : 0;
        else raw.set(ground ? [67, 91, 52] : [93, 255, 255], offset);
    }
    const chunks = [chunk('IHDR', header)];
    if (type === PNG_INDEXED) chunks.push(chunk('PLTE', Buffer.from([93, 255, 255, 67, 91, 52])));
    chunks.push(chunk('IDAT', deflateSync(raw)), chunk('IEND', Buffer.alloc(0)));
    return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), ...chunks]);
}
async function fixture(directory, original) {
    const files = new Map();
    for (const name of await fs.readdir(original)) if (/^(main|tedit)\.(res|pre)$/i.test(name) || /^(sdmain|sdtedit)\.(pvs|xvs|pes|esh|vsh)$/i.test(name))
        files.set(name, await fs.readFile(path.join(original, name)));
    for (const [name, bytes] of [
        ['horizons/' + UNICODE_FILENAME, png(65)], ['horizons/Far fields.png', png(119, PNG_INDEXED)],
        ['horizons/Boundary.png', png(120)], ['horizons/Short.png', png(64)],
        ['horizons/Too short.png', png(63)], ['horizons/Corrupt.png', Buffer.from([1, 2, 3])],
        ['horizons/nested/Hidden.png', png(64)], ['elsewhere/Hidden.png', png(64)], ['RootIgnored.png', png(64)]
    ]) files.set(name, bytes);
    for (const [name, bytes] of files) {
        const filename = path.join(directory, name); await fs.mkdir(path.dirname(filename), { recursive: true }); await fs.writeFile(filename, bytes);
    }
    return files;
}
async function observeModel(page) {
    await page.addInitScript(() => {
        let api;
        Object.defineProperty(window, 'TrackEditorModel', { configurable: true, get: () => api, set(value) {
            api = { ...value, decodeCstrk(...args) { const doc = value.decodeCstrk(...args); window.documentUnderTest = doc; return doc; } };
        } });
    });
}
async function openTrack(page, customHorizon = '', horizon = 4) {
    const doc = new model.Document(4, 4); doc.setHorizon(horizon, customHorizon);
    await page.locator('#track-file-input').setInputFiles({ name: 'CUSTOM.cstrk', mimeType: 'application/octet-stream', buffer: Buffer.from(model.encodeCstrk(doc)) });
    await page.waitForFunction(() => document.getElementById('document-name').textContent === 'CUSTOM');
}
async function stored(page) {
    return page.evaluate(() => ({ customHorizon: documentUnderTest.customHorizon, horizon: documentUnderTest.horizon }));
}
async function save(page, direct) {
    await page.locator('#save-track').click();
    if (direct) {
        await page.locator('#confirm-save').click();
        await page.waitForFunction(() => !document.getElementById('save-dialog').open);
        return page.evaluate(async () => Array.from(new Uint8Array(await (await (await directoryUnderTest.getFileHandle('CUSTOM.cstrk')).getFile()).arrayBuffer())));
    }
    const [download] = await Promise.all([page.waitForEvent('download'), page.locator('#confirm-save').click()]);
    const bytes = await fs.readFile(await download.path()); await download.delete(); return bytes;
}
async function checkSelection(page, direct, output) {
    const choices = await page.locator('#track-horizon option').evaluateAll(options => options.map(option => ({ value: option.value, label: option.textContent })));
    assert.deepEqual(choices.slice(0, 5).map(choice => choice.label), model.HORIZONS);
    assert.deepEqual(choices.slice(5).map(choice => choice.label).sort(), CUSTOM_FILES.map(name => name.replace(/\.png$/i, '')).sort());
    assert.match(await page.locator('#custom-horizon-status').textContent(), /Corrupt|Too short/);
    await openTrack(page);
    await page.locator('#track-horizon').selectOption('custom:' + UNICODE_FILENAME);
    assert.deepEqual(await stored(page), { customHorizon: UNICODE_FILENAME, horizon: 4 });
    assert.match(await page.locator('#document-name').textContent(), /•/);
    await page.locator('#undo').click(); assert.deepEqual(await stored(page), { customHorizon: '', horizon: 4 });
    await page.locator('#redo').click(); assert.deepEqual(await stored(page), { customHorizon: UNICODE_FILENAME, horizon: 4 });
    await page.locator('#track-horizon').selectOption('1'); assert.deepEqual(await stored(page), { customHorizon: '', horizon: 1 });
    await page.locator('#undo').click(); assert.deepEqual(await stored(page), { customHorizon: UNICODE_FILENAME, horizon: 4 });
    const saved = model.decodeCstrk(Uint8Array.from(await save(page, direct)));
    assert.equal(saved.customHorizon, UNICODE_FILENAME); assert.equal(saved.horizon, 4);
    if (output) {
        await page.locator('#track-horizon').scrollIntoViewIfNeeded();
        await page.screenshot({ path: path.join(output, direct ? 'custom-horizon-filesystem.png' : 'custom-horizon-readonly.png') });
    }
    for (const filename of ['Missing sky.png', 'Corrupt.png', 'északi fény.PNG']) {
        await openTrack(page, filename, 3);
        assert.equal(await page.locator('#track-horizon').inputValue(), 'custom:' + filename);
        assert.match(await page.locator('#track-horizon option:checked').textContent(), /unavailable/);
        assert.match(await page.locator('#custom-horizon-status').textContent(), /reference is preserved.*City/);
        const retained = model.decodeCstrk(Uint8Array.from(await save(page, direct)));
        assert.equal(retained.customHorizon, filename); assert.equal(retained.horizon, 3);
    }
}
async function main() {
    const settings = options(), temporary = await fs.mkdtemp(path.join(os.tmpdir(), 'restunts-horizon-browser-'));
    let browser, server;
    try {
        const folder = path.join(temporary, 'game'), files = await fixture(folder, settings['data-dir']);
        const baseOnly = path.join(temporary, 'game-without-horizons'); await fs.mkdir(baseOnly);
        for (const [name, bytes] of files) if (!name.includes('/')) await fs.writeFile(path.join(baseOnly, name), bytes);
        const html = path.join(temporary, 'tracked.html');
        execFileSync(process.env.PYTHON || 'python3', [path.join(ROOT, 'tools/scripts/build-track-editor.py'), '--output', html]);
        server = await serveHtml(html);
        browser = await chromium.launch({ headless: true, executablePath: settings['browser-executable'] || process.env.CHROMIUM_EXECUTABLE_PATH || undefined, args: ['--no-sandbox'] });
        if (settings.output) await fs.mkdir(settings.output, { recursive: true });
        for (const direct of [false, true]) {
            const page = await browser.newPage({ viewport: { width: 1600, height: 1000 } }), errors = [];
            page.setDefaultTimeout(TIMEOUT_MS); page.on('pageerror', error => errors.push(error.message)); page.on('dialog', dialog => dialog.accept());
            await observeModel(page);
            if (direct) await page.addInitScript(() => { window.showDirectoryPicker = async () => directoryUnderTest; });
            await page.goto(server.url);
            if (direct) {
                // Exercise actual FileSystemDirectoryHandle values/getFile/write APIs
                // in OPFS; only the OS picker is replaced for headless automation.
                await page.evaluate(async entries => {
                    window.directoryUnderTest = await (await navigator.storage.getDirectory()).getDirectoryHandle('horizon-fixture', { create: true });
                    for (const [relative, encoded] of entries) {
                        const parts = relative.split('/'), filename = parts.pop(); let directory = directoryUnderTest;
                        for (const part of parts) directory = await directory.getDirectoryHandle(part, { create: true });
                        const writable = await (await directory.getFileHandle(filename, { create: true })).createWritable();
                        await writable.write(Uint8Array.from(atob(encoded), character => character.charCodeAt(0))); await writable.close();
                    }
                }, Array.from(files, ([name, bytes]) => [name, bytes.toString('base64')]));
                await page.locator('#choose-folder').click();
            } else await page.locator('#folder-input').setInputFiles(folder);
            await page.waitForFunction(() => !document.getElementById('editor-controls').disabled);
            await checkSelection(page, direct, settings.output);
            await page.locator('#track-horizon').selectOption('custom:' + UNICODE_FILENAME);
            const retained = await stored(page);
            if (direct) await page.evaluate(() => directoryUnderTest.removeEntry('horizons', { recursive: true }));
            await page.locator('#folder-button').click();
            if (direct) await page.locator('#choose-folder').click(); else await page.locator('#folder-input').setInputFiles(baseOnly);
            await page.waitForFunction(() => !document.getElementById('folder-dialog').open &&
                document.getElementById('track-horizon').selectedOptions[0].textContent.includes('(unavailable)'));
            assert.deepEqual(await stored(page), retained, 'changing folders preserves the selected reference');
            const resaved = model.decodeCstrk(Uint8Array.from(await save(page, direct)));
            assert.equal(resaved.customHorizon, retained.customHorizon); assert.equal(resaved.horizon, retained.horizon);
            assert.deepEqual(errors, []);
            await page.close();
        }
        const duplicate = path.join(temporary, 'duplicate'); await fixture(duplicate, settings['data-dir']);
        await fs.writeFile(path.join(duplicate, 'horizons', 'duplicate.png'), png(64));
        await fs.writeFile(path.join(duplicate, 'horizons', 'DUPLICATE.PNG'), png(64));
        const page = await browser.newPage(); await observeModel(page); await page.goto(server.url); await page.locator('#folder-input').setInputFiles(duplicate);
        await page.waitForFunction(() => !document.getElementById('editor-controls').disabled);
        assert.match(await page.locator('#custom-horizon-status').textContent(), /Duplicate/i);
        assert.equal(await page.locator('#track-horizon option[value="custom:duplicate.png"]').count(), 0);
        assert.equal(await page.locator('#track-horizon option[value="custom:DUPLICATE.PNG"]').count(), 0);
        await openTrack(page, 'duplicate.png', 4);
        assert.match(await page.locator('#track-horizon option:checked').textContent(), /unavailable/);
        await page.locator('#track-horizon').selectOption('3');
        assert.deepEqual(await stored(page), { customHorizon: '', horizon: 3 });
        console.log('PASS browser custom horizons: read-only directory and actual FileSystem handles; Unicode labels; 64/65/119/120 heights; targeted discovery; invalid/duplicate rejection; undo/redo; direct/download saves; missing/corrupt reference preservation.');
    } finally {
        if (browser) await browser.close(); if (server) await server.close(); await fs.rm(temporary, { recursive: true, force: true });
    }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
