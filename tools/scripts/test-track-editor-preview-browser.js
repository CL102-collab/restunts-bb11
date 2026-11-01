'use strict';

// Real-browser smoke test for the standalone editor. Requires Playwright and
// user-supplied Stunts data; the generated HTML embeds no original game assets.
// CHROMIUM_EXECUTABLE_PATH may select an existing Chromium installation.
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const { chromium } = require('playwright');
const { serveHtml } = require('./browser-test-server.js');
const model = require('../../src/restunts/platform/wasm/track-editor-model.js');

const ROOT = path.resolve(__dirname, '../..');
const TIMEOUT_MS = 30000;
const MOVE_DISTANCE = .08;
const POSITION_TOLERANCE = .000001;
const FRAME_SAMPLE_STEP = 40;
const MIN_FRAME_COLORS = 4;
const FIXTURE_SIZE = 12;

function options() {
    const result = {};
    const args = process.argv.slice(2);
    for (let index = 0; index < args.length; index++) {
        if (args[index] === '--help') {
            console.log('Usage: node tools/scripts/test-track-editor-preview-browser.js --data-dir DIR ' +
                '[--html FILE] [--output DIR] [--browser-executable FILE]');
            process.exit(0);
        }
        assert.ok(['--data-dir', '--html', '--output', '--browser-executable'].includes(args[index]),
            'Unknown option ' + args[index]);
        assert.ok(args[index + 1], 'Missing value for ' + args[index]);
        result[args[index].slice(2)] = path.resolve(args[++index]);
    }
    assert.ok(result['data-dir'], '--data-dir must name your own Stunts data folder');
    return result;
}

function fixture() {
    const doc = new model.Document(FIXTURE_SIZE, FIXTURE_SIZE);
    for (let y = 0; y <= doc.height; y++) for (let x = 0; x <= doc.width; x++)
        doc.vertices[doc.vertexIndex(x, y)] = x < 7 ? 0 : x === 7 ? 1 : 2;
    doc.place(1, 2, 9);
    doc.place(4, 2, 3);
    doc.place(11, 3, 3);
    doc.place(115, 2, 6, 1, 0, 0, 1);
    doc.place(85, 4, 7, 2, 0, 0, 0, 1);
    doc.place(153, 6, 3, 0, 1, 0, 0, 0, 10);
    doc.place(153, 8, 5, 0, 3, 0, 0, 0, 5);
    doc.place(153, 8, 3);
    doc.place(101, 8, 8, 2, 0, 1);
    doc.place(155, 2, 10);
    // Cover generated scenery in its 2D map and actual WebGL preview, including
    // adjacent fields and a continuous slope through the wheat rows.
    for (let y = 7; y < 9; y++) for (let x = 6; x < 8; x++) doc.place(model.WHEAT_FIELD_ID, x, y);
    for (let y = 1; y < 5; y++) for (let x = 9; x < doc.width; x++) doc.paint(x, y, 3);
    doc.paint(8, 1, 0, 2, 3);
    assert.deepEqual(doc.validate(), []);
    return Buffer.from(model.encodeCstrk(doc));
}

async function observePreview(page) {
    // Preserve the production renderer/controller. Capture its instance only to
    // observe actual camera state, rather than infer movement from screenshots.
    await page.addInitScript(() => {
        let api;
        Object.defineProperty(window, 'TrackEditorPreview', {
            configurable: true,
            get: () => api,
            set(value) {
                api = { ...value, Preview: new Proxy(value.Preview, {
                    construct(target, args, receiver) {
                        const preview = Reflect.construct(target, args, receiver);
                        window.previewUnderTest = preview;
                        return preview;
                    }
                }) };
            }
        });
    });
}

async function loadEditor(page, url, directory, bytes) {
    await page.goto(url);
    await page.locator('#folder-input').setInputFiles(directory);
    await page.waitForFunction(() => !document.getElementById('editor-controls').disabled,
        null, { timeout: TIMEOUT_MS });
    await page.locator('#track-file-input').setInputFiles({
        name: 'PREVIEW.cstrk', mimeType: 'application/octet-stream', buffer: bytes
    });
    await page.waitForFunction(() => document.getElementById('document-name').textContent === 'PREVIEW');
}

async function camera(page) {
    return page.evaluate(() => ({
        position: Array.from(previewUnderTest.camera.position),
        yaw: previewUnderTest.camera.yaw, pitch: previewUnderTest.camera.pitch
    }));
}

async function settleFrames(page) {
    await page.evaluate(() => new Promise(resolve =>
        requestAnimationFrame(() => requestAnimationFrame(resolve))));
}

async function move(page, key) {
    const before = await camera(page);
    await page.keyboard.down(key);
    try {
        await page.waitForFunction(({ before, minimum }) => {
            const position = previewUnderTest.camera.position;
            return Math.hypot(...position.map((value, index) => value - before[index])) >= minimum;
        }, { before: before.position, minimum: MOVE_DISTANCE });
    } finally {
        await page.keyboard.up(key);
    }
    const after = await camera(page);
    return after.position.map((value, index) => value - before.position[index]);
}

async function checkFrame(page) {
    const result = await page.evaluate(step => {
        const preview = previewUnderTest, gl = preview.gl;
        preview.draw();
        const pixels = new Uint8Array(gl.drawingBufferWidth * gl.drawingBufferHeight * 4);
        gl.readPixels(0, 0, gl.drawingBufferWidth, gl.drawingBufferHeight,
            gl.RGBA, gl.UNSIGNED_BYTE, pixels);
        const colors = new Set();
        for (let y = 0; y < gl.drawingBufferHeight; y += step)
            for (let x = 0; x < gl.drawingBufferWidth; x += step) {
                const p = (y * gl.drawingBufferWidth + x) * 4;
                colors.add(pixels[p] + ':' + pixels[p + 1] + ':' + pixels[p + 2]);
            }
        return { colors: colors.size, error: gl.getError(),
            vertices: preview.scene.positions.length / 3,
            width: gl.drawingBufferWidth, height: gl.drawingBufferHeight };
    }, FRAME_SAMPLE_STEP);
    assert.equal(result.error, 0, 'The actual WebGL render must have no GL errors');
    assert.ok(result.vertices > FIXTURE_SIZE * FIXTURE_SIZE * 6,
        'The scene must include actual object geometry beyond its terrain');
    assert.ok(result.width > 100 && result.height > 100, 'The preview drawing buffer must fill its view');
    assert.ok(result.colors >= MIN_FRAME_COLORS, 'The preview must draw a visible scene, not a blank canvas');
    return result;
}

async function checkMovement(page) {
    const forward = await move(page, 'w'), backward = await move(page, 's');
    const left = await move(page, 'a'), right = await move(page, 'd');
    assert.ok(forward[0] * backward[0] + forward[2] * backward[2] < 0, 'W and S move in opposite directions');
    assert.ok(left[0] * right[0] + left[2] * right[2] < 0, 'A and D strafe in opposite directions');
    assert.ok(Math.abs(forward[0] * right[0] + forward[2] * right[2]) < POSITION_TOLERANCE,
        'Forward movement and strafing must be perpendicular');
    assert.equal(await page.locator('[data-tool="select"]').getAttribute('aria-pressed'), 'false',
        'S in the preview must not switch the 2D selection tool');
    const up = await move(page, 'Space'), down = await move(page, 'Shift');
    assert.ok(up[1] >= MOVE_DISTANCE && down[1] <= -MOVE_DISTANCE,
        'Space and Shift move vertically without collision or gravity');
    assert.ok(Math.abs(up[0]) < POSITION_TOLERANCE && Math.abs(up[2]) < POSITION_TOLERANCE,
        'Vertical movement must not alter horizontal coordinates');
    await page.keyboard.down('Alt');
    assert.equal(await page.evaluate(() => previewUnderTest.status.speedMultiplier), 3, 'Alt enables exactly 3× speed');
    await page.keyboard.up('Alt');
    assert.equal(await page.evaluate(() => previewUnderTest.status.speedMultiplier), 1, 'Releasing Alt restores normal speed');
    // Lock state belongs to the OS. Use the browser's actual getModifierState
    // implementation with explicit Caps Lock state for portable headless tests.
    await page.locator('#preview-canvas').dispatchEvent('keydown', {
        key: 'CapsLock', code: 'CapsLock', modifierCapsLock: true, bubbles: true
    });
    assert.equal(await page.evaluate(() => previewUnderTest.status.running), true, 'Caps Lock on enables running');
    await page.locator('#preview-canvas').dispatchEvent('keydown', {
        key: 'Alt', code: 'AltLeft', altKey: true, modifierCapsLock: true, bubbles: true
    });
    assert.equal(await page.evaluate(() => previewUnderTest.status.speedMultiplier), 3,
        'Alt and Caps Lock must not stack to 9×');
    await page.locator('#preview-canvas').dispatchEvent('keyup', {
        key: 'Alt', code: 'AltLeft', modifierCapsLock: false, bubbles: true
    });
    assert.equal(await page.evaluate(() => previewUnderTest.status.running), false, 'Caps Lock off disables running');
    assert.equal(await page.evaluate(() => previewUnderTest.status.speedMultiplier), 1);
}

async function checkCompassHandedness(page, originalBytes) {
    // Native extended_geometry_object_origin increases x with eastward columns
    // and z with northward rows. These map compass expectations are independent
    // of the preview's camera/projection implementation.
    const cases = [
        { name: 'North', yaw: 0, position: [5.5, .7, 1.5], start: [3, 5], pine: [7, 5], forward: [0, 0, 1], right: [1, 0, 0] },
        { name: 'East', yaw: Math.PI / 2, position: [1.5, .7, 5.5], start: [5, 3], pine: [5, 7], forward: [1, 0, 0], right: [0, 0, -1] },
        { name: 'South', yaw: Math.PI, position: [5.5, .7, 9.5], start: [7, 5], pine: [3, 5], forward: [0, 0, -1], right: [-1, 0, 0] },
        { name: 'West', yaw: Math.PI * 3 / 2, position: [9.5, .7, 5.5], start: [5, 7], pine: [5, 3], forward: [-1, 0, 0], right: [0, 0, 1] }
    ];
    const map = new model.Document(11, 11);
    map.place(1, ...cases[0].start); map.place(153, ...cases[0].pine);
    await page.locator('#view-2d').click();
    await page.locator('#track-file-input').setInputFiles({ name: 'COMPASS.cstrk',
        mimeType: 'application/octet-stream', buffer: Buffer.from(model.encodeCstrk(map)) });
    await page.waitForFunction(() => document.getElementById('document-name').textContent === 'COMPASS');
    await page.locator('#fit-map').click();
    await settleFrames(page);
    const mapMarkers = await page.evaluate(() => {
        const canvas = document.getElementById('track-canvas'), rect = canvas.getBoundingClientRect();
        const size = 32 * Math.max(.12, Math.min(4, (rect.width - 64) / (11 * 32), (rect.height - 64) / (11 * 32)));
        const left = (rect.width - 11 * size) / 2;
        return { start: left + 3.5 * size, pine: left + 7.5 * size };
    });
    assert.ok(mapMarkers.start < mapMarkers.pine, 'The asymmetric fixture places Start west/left of Pine on the 2D map');
    await page.locator('#view-3d').click();
    await page.waitForFunction(() => previewUnderTest.active && previewUnderTest.available);
    await page.evaluate(async headings => {
        const files = new Map(Array.from(document.getElementById('folder-input').files, file => [file.name.toLowerCase(), file]));
        const resources = await TrackEditorResources.load(files);
        const assets = await TrackEditorResources.load3D(files, resources.palette);
        window.previewCompassFixtures = headings.map(heading => {
            const empty = new TrackEditorModel.Document(11, 11);
            const start = new TrackEditorModel.Document(11, 11); start.place(1, ...heading.start);
            const pine = new TrackEditorModel.Document(11, 11); pine.place(153, ...heading.pine);
            return { ...heading, empty: TrackEditorScene.buildScene(empty, assets),
                startScene: TrackEditorScene.buildScene(start, assets), pineScene: TrackEditorScene.buildScene(pine, assets) };
        });
        window.previewCompassPixels = index => {
            const preview = previewUnderTest, gl = preview.gl, fixture = previewCompassFixtures[index];
            const render = scene => {
                preview.setScene(scene, { resetCamera: false }); preview.draw();
                const pixels = new Uint8Array(gl.drawingBufferWidth * gl.drawingBufferHeight * 4);
                gl.readPixels(0, 0, gl.drawingBufferWidth, gl.drawingBufferHeight, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
                return pixels;
            };
            const background = render(fixture.empty);
            const marker = scene => {
                const pixels = render(scene), width = gl.drawingBufferWidth;
                let count = 0, sumX = 0;
                for (let p = 0; p < pixels.length; p += 4) {
                    if (Math.abs(pixels[p] - background[p]) + Math.abs(pixels[p + 1] - background[p + 1]) +
                        Math.abs(pixels[p + 2] - background[p + 2]) < 16) continue;
                    count++; sumX += (p / 4) % width;
                }
                return { count, x: count ? sumX / count : null };
            };
            return { width: gl.drawingBufferWidth, start: marker(fixture.startScene), pine: marker(fixture.pineScene) };
        };
    }, cases);
    const report = [];
    for (let index = 0; index < cases.length; index++) {
        const heading = cases[index];
        const reset = () => page.evaluate(index => {
            const fixture = previewCompassFixtures[index];
            previewUnderTest.camera = { position: Array.from(fixture.position), yaw: fixture.yaw, pitch: -.1 };
            previewUnderTest.clearInput();
        }, index);
        const sample = () => page.evaluate(index => previewCompassPixels(index), index);
        await reset(); await page.locator('#preview-canvas').focus();
        const initial = await sample();
        assert.ok(initial.start.count > 20 && initial.pine.count > 20,
            heading.name + ' camera must see both actual native mesh markers');
        assert.ok(initial.start.x < initial.width / 2 && initial.pine.x > initial.width / 2,
            heading.name + ' camera must show the native Start marker on the compass-left and Pine on the compass-right');
        const strafe = await move(page, 'd');
        assert.ok(strafe.reduce((sum, value, axis) => sum + value * heading.right[axis], 0) >= MOVE_DISTANCE,
            heading.name + ' D must strafe toward the native compass-right direction');
        const afterStrafe = await sample();
        assert.ok(afterStrafe.start.x < initial.start.x,
            heading.name + ' right strafe must shift the fixed native Start mesh left on screen');
        await reset();
        const forward = await move(page, 'w');
        assert.ok(forward.reduce((sum, value, axis) => sum + value * heading.forward[axis], 0) >= MOVE_DISTANCE,
            heading.name + ' W must move toward its native compass heading');
        await reset();
        const beforeLook = await sample(), beforeCamera = await camera(page);
        const bounds = await page.locator('#preview-canvas').boundingBox();
        await page.mouse.move(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2);
        await page.mouse.down();
        await page.mouse.move(bounds.x + bounds.width / 2 + 30, bounds.y + bounds.height / 2);
        await page.mouse.up();
        const afterCamera = await camera(page);
        const turn = Math.atan2(Math.sin(afterCamera.yaw - beforeCamera.yaw), Math.cos(afterCamera.yaw - beforeCamera.yaw));
        assert.ok(turn > 0, heading.name + ' right mouselook turns clockwise toward compass-right');
        const afterLook = await sample();
        assert.ok(afterLook.start.x < beforeLook.start.x,
            heading.name + ' right mouselook must shift the fixed native Start mesh left on screen');
        await page.keyboard.press('Escape');
        if (await page.evaluate(() => document.pointerLockElement !== null)) await page.evaluate(() => document.exitPointerLock());
        report.push({ heading: heading.name, left: Math.round(initial.start.x), right: Math.round(initial.pine.x) });
    }
    await page.locator('#view-2d').click();
    await page.locator('#track-file-input').setInputFiles({ name: 'PREVIEW.cstrk',
        mimeType: 'application/octet-stream', buffer: originalBytes });
    await page.waitForFunction(() => document.getElementById('document-name').textContent === 'PREVIEW');
    await page.locator('#view-3d').click();
    await page.waitForFunction(() => previewUnderTest.active && previewUnderTest.available);
    await page.locator('#preview-canvas').focus();
    console.log('Native compass marker pixels: ' + JSON.stringify(report));
}

async function checkMouseLook(page) {
    const before = await camera(page);
    const bounds = await page.locator('#preview-canvas').boundingBox();
    await page.mouse.move(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2);
    await page.mouse.down();
    await page.mouse.move(bounds.x + bounds.width / 2 + 60, bounds.y + bounds.height / 2 - 35);
    await page.mouse.up();
    const dragged = await camera(page);
    assert.notEqual(dragged.yaw, before.yaw, 'Mouse drag turns the camera');
    assert.notEqual(dragged.pitch, before.pitch, 'Mouse drag changes the camera pitch');
    await page.locator('#preview-canvas').click();
    await settleFrames(page);
    const locked = await page.evaluate(() => document.pointerLockElement === document.getElementById('preview-canvas'));
    if (locked) {
        const first = await camera(page);
        await page.mouse.move(20, 10);
        assert.notEqual((await camera(page)).yaw, first.yaw, 'Captured mouse movement turns the camera');
        await page.keyboard.press('Escape');
        await page.waitForFunction(() => document.pointerLockElement === null);
    }
    await page.locator('#preview-canvas').focus();
}

async function checkLoadingExit(page) {
    await page.evaluate(() => {
        const original = TrackEditorResources.load3D;
        window.restorePreviewLoader = () => { TrackEditorResources.load3D = original; };
        TrackEditorResources.load3D = (...args) => new Promise((resolve, reject) => {
            window.releasePreviewLoad = () => {
                window.previewLoadFinished = original(...args).then(resolve, reject).then(() => true);
            };
        });
    });
    await page.locator('#view-3d').click();
    await page.waitForFunction(() => typeof window.releasePreviewLoad === 'function');
    await page.locator('#view-2d').click();
    await page.evaluate(() => releasePreviewLoad());
    await page.waitForFunction(async () => await window.previewLoadFinished);
    await settleFrames(page);
    assert.equal(await page.evaluate(() => previewUnderTest.active), false,
        'Finishing an old model load must not restart a preview after returning to 2D');
    assert.equal(await page.locator('#preview-view').isHidden(), true);
    await page.evaluate(() => restorePreviewLoader());
}

async function checkDialogInput(page) {
    const before = await camera(page);
    await page.locator('#notes-button').click();
    await page.locator('#track-notes').pressSequentially('wasd WASD ');
    await settleFrames(page);
    assert.equal(await page.evaluate(() => previewUnderTest.active), false,
        'An editor dialog must pause the preview');
    assert.deepEqual(await camera(page), before, 'Typing WASD in track notes must not move the camera');
    await page.locator('#notes-dialog [data-close]').first().click();
    await page.waitForFunction(() => previewUnderTest.active);
    await settleFrames(page);
    assert.deepEqual(await camera(page), before, 'Closing a dialog must resume without stuck movement');
}

async function checkDocumentShortcuts(page) {
    for (const modifier of ['Control', 'Meta']) {
        await page.locator('#preview-canvas').focus();
        const before = await camera(page);
        await page.keyboard.press(modifier + '+s');
        await page.locator('#save-dialog').waitFor({ state: 'visible' });
        await settleFrames(page);
        assert.equal(await page.evaluate(() => previewUnderTest.active), false,
            modifier + '+S must open the editor Save dialog and pause the preview');
        assert.deepEqual(await camera(page), before,
            modifier + '+S must not move the preview camera backward');
        await page.locator('#save-dialog [data-close]').first().click();
        await page.waitForFunction(() => previewUnderTest.active);
        await settleFrames(page);
        assert.deepEqual(await camera(page), before,
            'Closing Save must resume without a held movement key');
        await page.locator('#preview-canvas').focus();
        const [chooser] = await Promise.all([
            page.waitForEvent('filechooser'), page.keyboard.press(modifier + '+o')
        ]);
        assert.equal(await chooser.element().getAttribute('id'), 'track-file-input',
            modifier + '+O must open the editor track chooser');
        await chooser.setFiles([]);
        await settleFrames(page);
        assert.deepEqual(await camera(page), before, 'Opening a track chooser must not move the camera');
    }
}

async function checkFocusAndExit(page) {
    await page.keyboard.down('w');
    await page.locator('#preview-reset').focus();
    await page.keyboard.up('w');
    await settleFrames(page);
    const blurred = await camera(page);
    await settleFrames(page);
    assert.deepEqual(await camera(page), blurred, 'Leaving the preview canvas must clear movement keys');
    await page.locator('#preview-canvas').focus();
    await page.keyboard.down('w');
    await page.locator('#view-2d').click();
    await page.keyboard.up('w');
    assert.equal(await page.evaluate(() => previewUnderTest.active), false, '2D mode must stop the preview loop');
    assert.equal(await page.locator('#preview-view').isHidden(), true);
    await settleFrames(page);
    const stopped = await camera(page);
    await page.keyboard.press('w');
    await page.keyboard.press('Space');
    await page.keyboard.up('Space');
    await settleFrames(page);
    assert.deepEqual(await camera(page), stopped, '2D editor input must not move the hidden 3D camera');
    await page.locator('#view-3d').click();
    await page.waitForFunction(() => previewUnderTest.active && previewUnderTest.available);
    await settleFrames(page);
    await page.locator('#preview-canvas').focus();
    const resumed = await camera(page);
    await settleFrames(page);
    assert.deepEqual(await camera(page), resumed, 'Re-entering preview must not retain held movement');
}

async function checkContextRecovery(page) {
    const available = await page.evaluate(() => {
        window.previewLoseContext = previewUnderTest.gl.getExtension('WEBGL_lose_context');
        return !!window.previewLoseContext;
    });
    if (!available) return;
    await page.evaluate(() => previewLoseContext.loseContext());
    await page.waitForFunction(() => !previewUnderTest.available);
    assert.match(await page.locator('#preview-message').textContent(), /WebGL|recover|paused/i);
    await page.evaluate(() => previewLoseContext.restoreContext());
    await page.waitForFunction(() => previewUnderTest.available, null, { timeout: TIMEOUT_MS });
    await page.locator('#preview-reset').click();
    await settleFrames(page);
    await checkFrame(page);
}

async function checkContextRecoveryAfterDocumentChange(page, bytes) {
    const oldVertices = await page.evaluate(() => previewUnderTest.scene.positions.length);
    const available = await page.evaluate(() => {
        window.previewLoseContext = previewUnderTest.gl.getExtension('WEBGL_lose_context');
        window.sceneBeforeContextRecovery = previewUnderTest.scene;
        return !!window.previewLoseContext;
    });
    if (!available) return;
    const replacement = model.decodeCstrk(new Uint8Array(bytes));
    replacement.place(153, 4, 10, 0, 0, 0, 0, 0, 10);
    replacement.place(153, 8, 10, 0, 1, 0, 0, 0, 5);
    await page.evaluate(() => previewLoseContext.loseContext());
    await page.waitForFunction(() => !previewUnderTest.available);
    await page.locator('#view-2d').click();
    await page.locator('#track-file-input').setInputFiles({
        name: 'RECOVERED.cstrk', mimeType: 'application/octet-stream',
        buffer: Buffer.from(model.encodeCstrk(replacement))
    });
    await page.waitForFunction(() => document.getElementById('document-name').textContent === 'RECOVERED');
    await page.locator('#view-3d').click();
    assert.equal(await page.evaluate(() => previewUnderTest.available), false,
        'The recovery fixture must re-enter 3D while its context is still lost');
    await page.evaluate(() => previewLoseContext.restoreContext());
    await page.waitForFunction(previousVertices => previewUnderTest.available && previewUnderTest.active &&
        previewUnderTest.scene !== sceneBeforeContextRecovery &&
        previewUnderTest.scene.positions.length > previousVertices,
    oldVertices, { timeout: TIMEOUT_MS });
    assert.equal(await page.locator('#preview-view').isVisible(), true,
        'Context recovery must resume the requested 3D view without another toggle');
    await settleFrames(page);
    await checkFrame(page);
}

async function checkUnavailable(browser, url, directory, bytes) {
    const page = await browser.newPage();
    try {
        await page.addInitScript(() => {
            const getContext = HTMLCanvasElement.prototype.getContext;
            HTMLCanvasElement.prototype.getContext = function (kind, ...args) {
                return kind.includes('webgl') ? null : getContext.call(this, kind, ...args);
            };
        });
        await loadEditor(page, url, directory, bytes);
        await page.locator('#view-3d').click();
        await page.waitForFunction(() => /WebGL/i.test(document.getElementById('preview-message').textContent));
        await page.locator('#view-2d').click();
        assert.equal(await page.locator('#track-canvas').isVisible(), true, '2D editing remains available without WebGL');
    } finally {
        await page.close();
    }
}

async function main() {
    const settings = options(), temporary = await fs.mkdtemp(path.join(os.tmpdir(), 'restunts-preview-browser-'));
    let browser, server;
    try {
        const html = settings.html || path.join(temporary, 'tracked.html');
        if (!settings.html) execFileSync(process.env.PYTHON || 'python3', [
            path.join(ROOT, 'tools/scripts/build-track-editor.py'), '--output', html
        ]);
        server = await serveHtml(html);
        browser = await chromium.launch({
            headless: true,
            executablePath: settings['browser-executable'] || process.env.CHROMIUM_EXECUTABLE_PATH || undefined,
            args: ['--no-sandbox', '--use-angle=swiftshader', '--enable-unsafe-swiftshader']
        });
        const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
        page.setDefaultTimeout(TIMEOUT_MS);
        const errors = [];
        page.on('pageerror', error => errors.push(error.message));
        const bytes = fixture();
        await observePreview(page);
        await loadEditor(page, server.url, settings['data-dir'], bytes);
        await checkLoadingExit(page);
        await page.locator('#view-3d').click();
        await page.waitForFunction(() => window.previewUnderTest?.active && previewUnderTest.available && previewUnderTest.scene,
            null, { timeout: TIMEOUT_MS });
        await page.locator('#preview-canvas').focus();
        const frame = await checkFrame(page);
        if (settings.output) {
            await fs.mkdir(settings.output, { recursive: true });
            await page.screenshot({ path: path.join(settings.output, 'preview.png') });
        }
        await checkCompassHandedness(page, bytes);
        await checkMovement(page);
        await checkMouseLook(page);
        await checkDialogInput(page);
        await checkDocumentShortcuts(page);
        await checkFocusAndExit(page);
        await checkContextRecovery(page);
        await checkContextRecoveryAfterDocumentChange(page, bytes);
        await checkUnavailable(browser, server.url, settings['data-dir'], bytes);
        assert.deepEqual(errors, [], 'Browser preview must not throw uncaught errors');
        console.log('WebGL editor browser smoke passed (' + frame.vertices + ' vertices, ' + frame.colors + ' sampled colors).');
    } finally {
        if (browser) await browser.close();
        if (server) await server.close();
        await fs.rm(temporary, { recursive: true, force: true });
    }
}

main().catch(error => { console.error(error); process.exitCode = 1; });
