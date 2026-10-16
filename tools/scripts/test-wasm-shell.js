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
const OPTIONS = ['skip-intro', 'hypervision', 'new-midi', 'show-fps', 'show-control-layout',
    'power-gear', 'legacy-collision', 'left-corner-bias', 'owoot', 'amiga-grass'];
const RULES = ['power-gear', 'legacy-collision', 'left-corner-bias', 'owoot', 'amiga-grass'];
const PRESETS = ['off', 'auto', 'full', 'high', 'medium', 'low'];
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

async function storedOptions(page) {
    // Reading the same object store waits behind pending change writes.
    return page.evaluate(() => browserStorage(LAUNCH_OPTIONS_STORE, 'readonly'));
}

async function assertSettingsLayout(page, narrow) {
    const layout = await page.evaluate(ids => {
        const graphics = document.getElementById('graphics-settings').getBoundingClientRect();
        const driving = document.getElementById('driving-rules').getBoundingClientRect();
        return {
            graphicsBottom: graphics.bottom,
            drivingTop: driving.top,
            graphicsControls: Array.from(document.querySelectorAll('#graphics-settings input, #graphics-settings select'))
                .map(option => option.id),
            rules: ids.map(id => {
                const input = document.getElementById(id);
                return { grouped: input.closest('fieldset').id === 'driving-rules',
                    y: input.parentElement.getBoundingClientRect().y };
            }),
            rows: Array.from(document.querySelectorAll('.controls, .settings-row')).map(row => {
                const style = getComputedStyle(row);
                const bounds = row.getBoundingClientRect();
                return { display: style.display, wrap: style.flexWrap, scrollWidth: row.scrollWidth,
                    clientWidth: row.clientWidth, childrenFit: Array.from(row.children).every(child => {
                        const item = child.getBoundingClientRect();
                        return item.left >= bounds.left - 1 && item.right <= bounds.right + 1;
                    }) };
            }),
            documentWidth: document.documentElement.scrollWidth,
            viewportWidth: innerWidth
        };
    }, RULES);
    assert.ok(layout.drivingTop >= layout.graphicsBottom, 'Driving rules must sit below graphics settings');
    assert.deepEqual(layout.graphicsControls, ['show-fps', 'show-control-layout', 'hypervision'],
        'Graphics must match the Android order with HyperVision last');
    assert.ok(layout.rules.every(rule => rule.grouped), 'Driving rules must stay in the same group');
    const ruleLines = new Set(layout.rules.map(rule => rule.y)).size;
    if (narrow !== undefined) {
        assert.ok(narrow ? ruleLines > 1 : ruleLines === 1,
            'Driving rules must wrap on phones and fit one line when space allows');
    }
    for (const row of layout.rows) {
        assert.equal(row.display, 'flex', 'Settings groups must use horizontal flex stacks');
        assert.equal(row.wrap, 'wrap', 'Every settings group must wrap');
        assert.ok(row.scrollWidth <= row.clientWidth + 1, 'Settings must fit without horizontal scrolling');
        assert.ok(row.childrenFit, 'Each control must stay inside its settings group');
    }
    assert.ok(layout.documentWidth <= layout.viewportWidth, 'Settings must not overflow the page');
}

// Keep actual fullscreen transitions while replacing only the device orientation
// API. A desktop browser cannot establish how a phone physically rotates.
async function stubOrientation(page, result) {
    await page.evaluate(result => {
        window.testOrientationObject ||= screen.orientation;
        window.testOrientationCalls = [];
        window.testOrientationActive = false;
        window.testOrientationUnhandled ||= [];
        if (!window.testOrientationRejectionListener) {
            window.addEventListener('unhandledrejection', event =>
                window.testOrientationUnhandled.push(String(event.reason)));
            window.testOrientationRejectionListener = true;
        }
        Object.defineProperty(screen, 'orientation', {
            configurable: true,
            value: result === 'missing-object' ? undefined : window.testOrientationObject
        });
        if (result === 'missing-object') return;
        Object.defineProperty(screen.orientation, 'lock', {
            configurable: true,
            value: result === 'missing-lock' ? undefined : mode => {
                window.testOrientationCalls.push({ type: 'lock', mode,
                    canvasFullscreen: document.fullscreenElement === document.getElementById('canvas') });
                if (result === 'reject') {
                    return Promise.reject(new DOMException('Orientation lock unavailable', 'NotSupportedError'));
                }
                if (result === 'delayed') {
                    return new Promise(resolve => {
                        window.testResolveOrientation = () => {
                            window.testOrientationActive = true;
                            window.testOrientationCalls.push({ type: 'resolved' });
                            resolve();
                        };
                    });
                }
                window.testOrientationActive = true;
                return Promise.resolve();
            }
        });
        Object.defineProperty(screen.orientation, 'unlock', {
            configurable: true,
            value: () => {
                window.testOrientationActive = false;
                window.testOrientationCalls.push({ type: 'unlock',
                    canvasFullscreen: document.fullscreenElement === document.getElementById('canvas') });
            }
        });
    }, result);
}

async function orientationCalls(page) {
    return page.evaluate(() => window.testOrientationCalls);
}

async function settleOrientation(page) {
    // Flush promises and fullscreen-change handlers without assuming a phone's
    // orientation or waiting for a fixed device rotation delay.
    await page.evaluate(() => new Promise(resolve =>
        requestAnimationFrame(() => requestAnimationFrame(resolve))));
}

async function enterCanvasFullscreen(page, direct) {
    if (direct) {
        // SDL's Alt+Enter route requests fullscreen on the same native canvas.
        await page.locator('#canvas').click();
        await page.locator('#canvas').evaluate(canvas => canvas.requestFullscreen());
    } else {
        await page.locator('#fullscreen').click();
    }
    await page.waitForFunction(() => document.fullscreenElement === document.getElementById('canvas'));
    await settleOrientation(page);
}

async function exitFullscreen(page) {
    await page.evaluate(() => document.exitFullscreen());
    await page.waitForFunction(() => document.fullscreenElement === null);
    await settleOrientation(page);
}

async function exerciseFullscreenOrientation(page) {
    await page.setViewportSize({ width: 390, height: 844 });
    await stubOrientation(page, 'resolve');
    assert.deepEqual(await orientationCalls(page), [], 'Orientation must remain free before fullscreen');
    for (const direct of [false, true]) {
        const before = await orientationCalls(page);
        if (!direct) {
            // Emscripten snapshots portrait dimensions into inline CSS before
            // entering fullscreen. A later landscape resize must still fill it.
            await page.locator('#canvas').evaluate(canvas => {
                canvas.style.width = '390px';
                canvas.style.height = '844px';
            });
        }
        await enterCanvasFullscreen(page, direct);
        const entered = await orientationCalls(page);
        assert.equal(entered.filter(call => call.type === 'lock').length,
            before.filter(call => call.type === 'lock').length + 1,
            'Every canvas entry must request landscape, including the native fullscreen route');
        assert.ok(entered.filter(call => call.type === 'lock').every(call =>
            call.mode === 'landscape' && call.canvasFullscreen),
        'Landscape must be requested only after the canvas has entered fullscreen');
        assert.equal(await page.evaluate(() => window.testOrientationActive), true);
        if (!direct) {
            await page.setViewportSize({ width: 844, height: 390 });
            await settleOrientation(page);
            const canvasSize = await page.locator('#canvas').evaluate(canvas => {
                const bounds = canvas.getBoundingClientRect();
                return { width: bounds.width, height: bounds.height,
                    viewportWidth: innerWidth, viewportHeight: innerHeight };
            });
            assert.equal(canvasSize.width, canvasSize.viewportWidth,
                'Landscape fullscreen width must override stale inline portrait CSS');
            assert.equal(canvasSize.height, canvasSize.viewportHeight,
                'Landscape fullscreen height must override stale inline portrait CSS');
        }
        await exitFullscreen(page);
        const exited = await orientationCalls(page);
        assert.ok(exited.filter(call => call.type === 'unlock').length >
            before.filter(call => call.type === 'unlock').length,
        'Leaving canvas fullscreen must release its orientation request');
        assert.equal(await page.evaluate(() => window.testOrientationActive), false);
        if (!direct) {
            await page.locator('#canvas').evaluate(canvas => {
                canvas.style.removeProperty('width');
                canvas.style.removeProperty('height');
            });
            await page.setViewportSize({ width: 390, height: 844 });
        }
    }

    for (const result of ['reject', 'missing-lock', 'missing-object']) {
        await stubOrientation(page, result);
        await enterCanvasFullscreen(page, result === 'reject');
        assert.equal(await page.evaluate(() => document.fullscreenElement.id), 'canvas',
            'Unsupported orientation lock must preserve fullscreen');
        assert.equal(await page.evaluate(() => window.testOrientationActive), false);
        const calls = await orientationCalls(page);
        assert.equal(calls.filter(call => call.type === 'lock').length, result === 'reject' ? 1 : 0);
        assert.doesNotMatch(await page.locator('#status').innerText(), /^Game error:/,
            'Unavailable orientation lock must not become a game error');
        await exitFullscreen(page);
    }

    await stubOrientation(page, 'delayed');
    await enterCanvasFullscreen(page, false);
    await exitFullscreen(page);
    await page.evaluate(() => window.testResolveOrientation());
    await settleOrientation(page);
    const delayed = await orientationCalls(page);
    const resolved = delayed.findIndex(call => call.type === 'resolved');
    assert.ok(delayed.slice(resolved + 1).some(call => call.type === 'unlock' && !call.canvasFullscreen),
        'A landscape lock that resolves after fullscreen exit must also be released');
    assert.equal(await page.evaluate(() => window.testOrientationActive), false);

    await stubOrientation(page, 'resolve');
    await page.evaluate(() => {
        const button = document.createElement('button');
        button.id = 'unrelated-fullscreen';
        button.textContent = 'Other fullscreen';
        button.addEventListener('click', () => button.requestFullscreen());
        document.body.appendChild(button);
    });
    await page.locator('#unrelated-fullscreen').click();
    await page.waitForFunction(() => document.fullscreenElement?.id === 'unrelated-fullscreen');
    await settleOrientation(page);
    await exitFullscreen(page);
    assert.deepEqual(await orientationCalls(page), [],
        'Unrelated fullscreen elements must not acquire or release the canvas orientation lock');
    assert.deepEqual(await page.evaluate(() => window.testOrientationUnhandled), [],
        'Orientation failures must not produce unhandled promise rejections');
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
        for (const width of [320, 390, 1000, 1920]) {
            await page.setViewportSize({ width, height: 844 });
            await assertSettingsLayout(page, width < 600);
        }
        await page.evaluate(() => { document.documentElement.style.fontSize = '150%'; });
        for (const width of [320, 390]) {
            await page.setViewportSize({ width, height: 844 });
            await assertSettingsLayout(page);
        }
        await page.evaluate(() => { document.documentElement.style.fontSize = ''; });
        await page.setViewportSize({ width: 390, height: 844 });
        assert.deepEqual(await page.locator('#hypervision option').evaluateAll(options =>
            options.map(option => ({ value: option.value, label: option.label }))),
        PRESETS.map(value => ({ value, label: value[0].toUpperCase() + value.slice(1) })));
        assert.equal(await page.locator('#hypervision').inputValue(), 'auto');
        assert.equal(await page.locator('#show-fps').isChecked(), false);
        assert.equal(await page.locator('#show-control-layout').isChecked(), false);
        assert.equal(await page.locator('#power-gear').isChecked(), true);
        assert.equal(await page.locator('#legacy-collision').isChecked(), true);
        assert.equal(await page.locator('#left-corner-bias').isChecked(), true);
        assert.equal(await page.locator('#owoot').isChecked(), false);
        assert.equal(await page.locator('#amiga-grass').isChecked(), false);
        await loadFixture(page, fixture);
        await page.locator('#start').click();
        assert.deepEqual(await page.evaluate(() => window.launchedArguments), [
            '--data-dir', '/stunts', '--ogg:on', '--fps:off', '--hv:auto', '--control-layout:off',
            '--pg:on', '--lc:on', '--lcb:on'
        ]);
        for (const id of OPTIONS) assert.equal(await page.locator('#' + id).isDisabled(), true);

        for (const [index, preset] of PRESETS.entries()) {
            await page.reload();
            await ready(page);
            await page.locator('#hypervision').selectOption(preset);
            const fps = index % 2 === 1;
            await page.locator('#show-fps').setChecked(fps);
            const stored = await storedOptions(page);
            assert.equal(stored.hypervision, preset, 'Preset must be stored as its string value');
            assert.equal(stored['show-fps'], fps, 'FPS must be stored as a boolean');
            await page.reload();
            await ready(page);
            assert.equal(await page.locator('#hypervision').inputValue(), preset, 'Preset must persist');
            assert.equal(await page.locator('#show-fps').isChecked(), fps, 'FPS must persist');
            await loadFixture(page, fixture);
            await page.locator('#start').click();
            const args = await page.evaluate(() => window.launchedArguments);
            assert.deepEqual(args.filter(arg => arg.startsWith('--hv:')),
                preset === 'off' ? [] : ['--hv:' + preset], 'Only the chosen preset may be passed');
            assert.deepEqual(args.filter(arg => arg.startsWith('--fps:')), [fps ? '--fps:on' : '--fps:off']);
        }

        await page.reload();
        await ready(page);
        for (const id of ['skip-intro', 'show-fps', 'show-control-layout', 'owoot', 'amiga-grass']) {
            await page.locator('#' + id).check();
        }
        for (const id of ['new-midi', ...RULES.slice(0, 3)]) await page.locator('#' + id).uncheck();
        await page.locator('#hypervision').selectOption('high');
        const stored = await storedOptions(page);
        for (const id of OPTIONS.filter(id => id !== 'hypervision')) assert.equal(typeof stored[id], 'boolean');
        await page.reload();
        await ready(page);
        for (const id of ['skip-intro', 'show-fps', 'show-control-layout', 'owoot', 'amiga-grass']) {
            assert.equal(await page.locator('#' + id).isChecked(), true, id + ' must persist across reload');
        }
        for (const id of ['new-midi', ...RULES.slice(0, 3)]) {
            assert.equal(await page.locator('#' + id).isChecked(), false, id + ' must persist across reload');
        }
        assert.equal(await page.locator('#hypervision').inputValue(), 'high');
        await loadFixture(page, fixture);
        await page.locator('#start').click();
        assert.deepEqual(await page.evaluate(() => window.launchedArguments), [
            '--data-dir', '/stunts', '--nointro', '--ogg:off', '--fps:on', '--hv:high', '--control-layout:on',
            '--pg:off', '--lc:off', '--lcb:off', '--owoot', '/amigagrass'
        ]);

        for (const [legacy, expected] of [[true, 'auto'], [false, 'off'], ['invalid', 'auto']]) {
            await page.evaluate(value => browserStorage(LAUNCH_OPTIONS_STORE, 'readwrite',
                { hypervision: value, 'show-fps': 'true', 'new-midi': false }), legacy);
            await page.reload();
            await ready(page);
            assert.equal(await page.locator('#hypervision').inputValue(), expected,
                'Legacy checkbox values must migrate; unknown presets must use the browser default');
            assert.equal(await page.locator('#show-fps').isChecked(), false, 'Malformed boolean values must be ignored');
            assert.equal(await page.locator('#new-midi').isChecked(), false, 'Other remembered choices must survive migration');
            await page.locator('#show-fps').check();
            assert.equal((await storedOptions(page)).hypervision, expected,
                'Saving settings must replace the legacy boolean with the migrated preset');
            await loadFixture(page, fixture);
            await page.locator('#start').click();
            const args = await page.evaluate(() => window.launchedArguments);
            assert.deepEqual(args.filter(arg => arg.startsWith('--hv:')),
                expected === 'off' ? [] : ['--hv:' + expected]);
        }
        await page.setViewportSize({ width: 1000, height: 500 });
        await assertSettingsLayout(page, false);
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
        await exitFullscreen(page);
        await exerciseFullscreenOrientation(page);
        assert.deepEqual(errors, [], 'Shell must not report browser errors');
        await context.close();
        console.log('PASS: wrapping web settings, six HyperVision presets, FPS, typed persistence, legacy migration, fullscreen canvas, landscape lock lifecycle and touch-action CSS');
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
