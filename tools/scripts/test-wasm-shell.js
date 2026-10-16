#!/usr/bin/env node
'use strict';

// Exercise the browser launch form without an Emscripten SDK or original game data.
// Install Playwright as described in docs/wasm.md. CHROMIUM_EXECUTABLE_PATH can
// select an existing Chromium installation instead of Playwright's downloaded one.
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const http = require('node:http');
const path = require('node:path');
const { chromium } = require('playwright');

const ROOT = path.resolve(__dirname, '../..');
const SHELL = path.join(ROOT, 'src/restunts/platform/wasm/shell.html');
const OPTIONS = ['skip-intro', 'hypervision', 'new-midi', 'show-control-layout',
    'power-gear', 'legacy-collision', 'left-corner-bias', 'owoot', 'amiga-grass'];
const RULES = ['power-gear', 'legacy-collision', 'left-corner-bias', 'owoot', 'amiga-grass'];
// Only the native runtime boundary is replaced. Browser form logic, file
// selection, settings storage, fullscreen, and CSS run unchanged from the shell.
const RUNTIME = `<script>
    const fixtureFiles = new Map();
    Module.FS = {
        mkdirTree() {},
        readdir() { return ['.', '..', ...fixtureFiles.keys()]; },
        stat() { return { mode: 1 }; },
        isFile() { return true; },
        isDir() { return false; },
        writeFile(path, bytes) { fixtureFiles.set(path.slice(path.lastIndexOf('/') + 1), bytes); },
        readFile(path) { return fixtureFiles.get(path.slice(path.lastIndexOf('/') + 1)); },
        unlink(path) { fixtureFiles.delete(path.slice(path.lastIndexOf('/') + 1)); }
    };
    Module.callMain = args => { window.launchedArguments = args; };
    // Pinned SDL overrides this entry point and requests fullscreen on #canvas
    // through emscripten_request_fullscreen_strategy (SDL_emscriptenvideo.c).
    Module.requestFullscreen = () => Module.canvas.requestFullscreen();
    Module.onRuntimeInitialized();
</script>`;

async function ready(page) {
    await page.waitForFunction(() => !document.getElementById('game-files').disabled);
}

async function loadFixture(page, directory) {
    await page.locator('#game-files').setInputFiles(directory);
    await page.waitForFunction(() => !document.getElementById('start').disabled);
}

async function assertRulesRow(page) {
    const layout = await page.evaluate(ids => {
        const row = document.querySelector('#driving-rules .settings-row');
        const graphics = document.getElementById('graphics-settings').getBoundingClientRect();
        const driving = document.getElementById('driving-rules').getBoundingClientRect();
        return {
            graphicsBottom: graphics.bottom,
            drivingTop: driving.top,
            rules: ids.map(id => {
                const input = document.getElementById(id);
                const bounds = input.parentElement.getBoundingClientRect();
                return { grouped: input.closest('fieldset').id === 'driving-rules', y: bounds.y };
            }),
            scrollable: getComputedStyle(row).overflowX === 'auto',
            documentWidth: document.documentElement.scrollWidth,
            viewportWidth: innerWidth
        };
    }, RULES);
    assert.ok(layout.drivingTop >= layout.graphicsBottom, 'Driving rules must sit below graphics settings');
    assert.ok(layout.rules.every(rule => rule.grouped), 'Driving rules must stay in the same group');
    assert.ok(layout.rules.every(rule => rule.y === layout.rules[0].y), 'Driving rules must stay on one line');
    assert.ok(layout.scrollable, 'A narrow screen must be able to scroll the rules');
    assert.ok(layout.documentWidth <= layout.viewportWidth, 'Settings must not overflow the mobile page');
    assert.equal(await page.locator('#graphics-settings #show-control-layout').count(), 1,
        'Show Control Layout must be part of graphics settings');
}

async function main() {
    const temporary = await fs.mkdtemp(path.join(os.tmpdir(), 'restunts-web-shell-'));
    const fixture = path.join(temporary, 'game-files');
    await fs.mkdir(fixture);
    for (const name of ['MAIN.RES', 'FONTDEF.FNT', 'FONTN.FNT']) {
        await fs.writeFile(path.join(fixture, name), 'fixture');
    }
    const source = await fs.readFile(SHELL, 'utf8');
    assert.ok(source.includes('{{{ SCRIPT }}}'), 'Shell runtime placeholder is missing');
    const html = source.replace('{{{ SCRIPT }}}', RUNTIME);
    const server = http.createServer((request, response) => {
        response.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
        response.end(html);
    });
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    let browser;
    try {
        browser = await chromium.launch({
            headless: true,
            ...(process.env.CHROMIUM_EXECUTABLE_PATH ? { executablePath: process.env.CHROMIUM_EXECUTABLE_PATH } : {})
        });
        const context = await browser.newContext({ viewport: { width: 390, height: 844 }, hasTouch: true });
        await context.addInitScript(() => { window.showDirectoryPicker = undefined; });
        const page = await context.newPage();
        const errors = [];
        page.on('pageerror', error => errors.push(String(error)));
        await page.goto('http://127.0.0.1:' + server.address().port + '/');
        await ready(page);
        await assertRulesRow(page);
        assert.equal(await page.locator('#show-control-layout').isChecked(), false);
        assert.equal(await page.locator('#power-gear').isChecked(), true);
        assert.equal(await page.locator('#legacy-collision').isChecked(), true);
        assert.equal(await page.locator('#left-corner-bias').isChecked(), true);
        assert.equal(await page.locator('#owoot').isChecked(), false);
        assert.equal(await page.locator('#amiga-grass').isChecked(), false);
        await loadFixture(page, fixture);
        await page.locator('#start').click();
        assert.deepEqual(await page.evaluate(() => window.launchedArguments), [
            '--data-dir', '/stunts', '--hv:auto', '--ogg:on', '--control-layout:off', '--pg:on', '--lc:on', '--lcb:on'
        ]);
        for (const id of OPTIONS) assert.equal(await page.locator('#' + id).isDisabled(), true);
        await page.reload();
        await ready(page);
        for (const id of ['skip-intro', 'show-control-layout', 'owoot', 'amiga-grass']) await page.locator('#' + id).check();
        for (const id of ['hypervision', 'new-midi', ...RULES.slice(0, 3)]) await page.locator('#' + id).uncheck();
        // Reading the same object store waits behind all pending change writes.
        await page.evaluate(() => browserStorage(LAUNCH_OPTIONS_STORE, 'readonly'));
        await page.reload();
        await ready(page);
        for (const id of ['skip-intro', 'show-control-layout', 'owoot', 'amiga-grass']) {
            assert.equal(await page.locator('#' + id).isChecked(), true, id + ' must persist across reload');
        }
        for (const id of ['hypervision', 'new-midi', ...RULES.slice(0, 3)]) {
            assert.equal(await page.locator('#' + id).isChecked(), false, id + ' must persist across reload');
        }
        await loadFixture(page, fixture);
        await page.locator('#start').click();
        assert.deepEqual(await page.evaluate(() => window.launchedArguments), [
            '--data-dir', '/stunts', '--nointro', '--ogg:off', '--control-layout:on', '--pg:off', '--lc:off', '--lcb:off', '--owoot', '/amigagrass'
        ]);
        await page.setViewportSize({ width: 1000, height: 500 });
        await assertRulesRow(page);
        await page.locator('#fullscreen').click();
        await page.waitForFunction(() => document.fullscreenElement === document.getElementById('canvas'));
        const fullscreen = await page.locator('#canvas').evaluate(canvas => {
            const bounds = canvas.getBoundingClientRect();
            return { width: bounds.width, height: bounds.height, viewportWidth: innerWidth,
                viewportHeight: innerHeight, touchAction: getComputedStyle(canvas).touchAction,
                objectFit: getComputedStyle(canvas).objectFit };
        });
        assert.equal(fullscreen.width, fullscreen.viewportWidth, 'Fullscreen canvas must include the side areas');
        assert.equal(fullscreen.height, fullscreen.viewportHeight, 'Fullscreen canvas must fill the viewport height');
        assert.equal(fullscreen.objectFit, 'fill', 'Canvas pixels must cover the fullscreen touch surface');
        assert.equal(fullscreen.touchAction, 'none', 'Driving gestures must not pan or zoom the page');
        await page.evaluate(() => document.exitFullscreen());
        assert.deepEqual(errors, [], 'Shell must not report browser errors');
        await context.close();
        console.log('PASS: web settings grouping, mobile row, defaults, launch flags, persistence, fullscreen canvas and touch-action CSS');
    } finally {
        if (browser) await browser.close();
        await new Promise(resolve => server.close(resolve));
        await fs.rm(temporary, { recursive: true, force: true });
    }
}

main().catch(error => {
    console.error(error);
    process.exitCode = 1;
});
