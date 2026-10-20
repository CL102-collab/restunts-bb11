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
const OPTIONS = ['skip-intro', 'sound-enabled', 'hypervision', 'new-midi', 'show-fps', 'show-control-layout',
    'power-gear', 'legacy-collision', 'left-corner-bias', 'dual-way-switching', 'owoot', 'amiga-grass'];
const RULES = ['power-gear', 'legacy-collision', 'left-corner-bias', 'dual-way-switching', 'owoot', 'amiga-grass'];
const PRESETS = ['off', 'auto', 'full', 'high', 'medium', 'low'];
// Only the native runtime boundary is replaced. Browser form logic, file
// selection, settings storage, fullscreen, and CSS run unchanged from the shell.
const RUNTIME = `<script>
    const fixtureFiles = new Map();
    const fixtureDirectories = new Set(['/stunts']);
    Module.FS = {
        mkdirTree(path) {
            const parts = path.split('/').filter(Boolean);
            for (let count = 1; count <= parts.length; count++) {
                fixtureDirectories.add('/' + parts.slice(0, count).join('/'));
            }
        },
        readdir(path) {
            const prefix = path + '/';
            const children = [...fixtureDirectories, ...fixtureFiles.keys()]
                .filter(name => name.startsWith(prefix) && !name.slice(prefix.length).includes('/'))
                .map(name => name.slice(prefix.length));
            return ['.', '..', ...children];
        },
        stat(path) { return { mode: fixtureDirectories.has(path) ? 2 : 1 }; },
        isFile(mode) { return mode === 1; },
        isDir(mode) { return mode === 2; },
        writeFile(path, bytes) { fixtureFiles.set(path, Uint8Array.from(bytes)); },
        readFile(path) { return Uint8Array.from(fixtureFiles.get(path)); },
        unlink(path) { fixtureFiles.delete(path); },
        rmdir(path) { fixtureDirectories.delete(path); },
        lookupPath(path, options) {
            return { path: options?.parent ? path.slice(0, path.lastIndexOf('/')) : path };
        }
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

async function installLoadingFixture(page, specs, direct = true) {
    await page.evaluate(({ specs, direct }) => {
        window.testStatusObserver?.disconnect();
        const fixture = window.testLoadingFixture = {
            pending: { metadata: [], content: [] },
            active: { metadata: 0, content: 0 },
            peak: { metadata: 0, content: 0 },
            calls: { metadata: [], content: [] },
            automatic: { metadata: false, content: false },
            diskWrites: [],
            statuses: [],
            async gate(phase, path, operation) {
                this.calls[phase].push(path);
                this.active[phase]++;
                this.peak[phase] = Math.max(this.peak[phase], this.active[phase]);
                try {
                    await new Promise((resolve, reject) => {
                        const pending = { path, finish: error => {
                            this.pending[phase].splice(this.pending[phase].indexOf(pending), 1);
                            if (error) reject(new Error('Fixture ' + phase + ' failure'));
                            else resolve();
                        } };
                        this.pending[phase].push(pending);
                        if (this.automatic[phase]) pending.finish();
                    });
                    return await operation();
                } finally {
                    this.active[phase]--;
                }
            },
            release(phase, automatic = false) {
                this.automatic[phase] = automatic;
                // Resolve out of enumeration order to check deterministic casing and commits.
                for (const pending of this.pending[phase].slice().reverse()) pending.finish();
            }
        };
        window.testStatusObserver = new MutationObserver(() => fixture.statuses.push(status.textContent));
        window.testStatusObserver.observe(status, { childList: true });
        if (!direct) {
            const original = File.prototype.arrayBuffer;
            File.prototype.arrayBuffer = function () {
                return fixture.gate('content', this.webkitRelativePath || this.name, () => original.call(this));
            };
            return;
        }
        function fileHandle(spec) {
            const name = spec.path.slice(spec.path.lastIndexOf('/') + 1);
            return {
                kind: 'file', name,
                getFile() {
                    return fixture.gate('metadata', spec.path, () => ({
                        name, size: spec.bytes.length,
                        arrayBuffer: () => fixture.gate('content', spec.path,
                            () => Uint8Array.from(spec.bytes).buffer)
                    }));
                }
            };
        }
        function directoryHandle(name, prefix = '') {
            return {
                kind: 'directory', name,
                async *values() {
                    const children = new Set();
                    for (const spec of specs) {
                        if (!spec.path.startsWith(prefix)) continue;
                        const relative = spec.path.slice(prefix.length);
                        const child = relative.split('/')[0];
                        if (children.has(child)) continue;
                        children.add(child);
                        yield relative.includes('/') ? directoryHandle(child, prefix + child + '/') : fileHandle(spec);
                    }
                }
            };
        }
        fixture.root = directoryHandle('fixture-directory');
        fixture.root.getFileHandle = async name => ({
            async createWritable() {
                let bytes;
                return {
                    async write(contents) { bytes = Array.from(contents); },
                    async close() { fixture.diskWrites.push({ name, bytes }); },
                    async abort() {}
                };
            }
        });
    }, { specs, direct });
}

async function loadingState(page) {
    return page.evaluate(() => JSON.stringify({
        files: Array.from(fixtureFiles, ([name, bytes]) => [name, Array.from(bytes)]),
        directories: Array.from(fixtureDirectories),
        casing: Array.from(diskNames),
        baseline: Array.from(baseline, ([name, bytes]) => [name, Array.from(bytes)])
    }));
}

async function assertConcurrentPhase(page, phase) {
    await page.waitForFunction(phase => window.testLoadingFixture.pending[phase].length > 1, phase);
    const active = await page.evaluate(phase => window.testLoadingFixture.active[phase], phase);
    assert.ok(active > 1 && active <= 8, phase + ' reads must overlap with at most eight in flight');
}

async function finishLoading(page) {
    await page.evaluate(() => {
        testLoadingFixture.release('metadata', true);
        testLoadingFixture.release('content', true);
    });
    await page.waitForFunction(() => !document.getElementById('open-folder').disabled);
}

async function exerciseDirectoryLoading(browser, url) {
    const context = await browser.newContext();
    await context.addInitScript(() => {
        window.showDirectoryPicker = async () => window.testLoadingFixture.root;
    });
    const page = await context.newPage();
    const errors = [];
    page.on('pageerror', error => errors.push(String(error)));
    try {
        await page.goto(url);
        await ready(page);
        const specs = ['MaIn.ReS', 'FONTDEF.FNT', 'FONTN.FNT', 'MiXeD.HIG',
            ...Array.from({ length: 12 }, (_, index) => 'TRACK' + index + '.TRK'),
            'MAIN.RES', 'Opponents/Animations/Opp1Win.WebM',
            'ignored.exe', 'archive/MAIN.RES', 'opponents/animations/opp7win.webm']
            .map(path => ({ path, bytes: [1, 2, 3] }));
        const accepted = specs.filter(spec => !['ignored.exe', 'archive/MAIN.RES',
            'opponents/animations/opp7win.webm'].includes(spec.path));
        await installLoadingFixture(page, specs);
        await page.locator('#open-folder').click();
        await assertConcurrentPhase(page, 'metadata');
        assert.match(await page.locator('#status').textContent(), /^Checking game files… 0 of 18\./);
        assert.equal(await page.locator('#start').isDisabled(), true);
        await page.evaluate(() => testLoadingFixture.release('metadata', true));
        await assertConcurrentPhase(page, 'content');
        assert.match(await page.locator('#status').textContent(), /^Reading game files… 0 of 17\./);
        await page.evaluate(() => testLoadingFixture.pending.content.find(item => item.path === 'FONTDEF.FNT').finish());
        await page.waitForFunction(() => document.getElementById('status').textContent === 'Reading game files… 1 of 17.');
        await finishLoading(page);
        assert.match(await page.locator('#status').textContent(), /^Loaded 17 game files\./);
        const imported = await page.evaluate(() => ({
            files: Array.from(fixtureFiles.keys()),
            casing: Array.from(diskNames),
            baseline: Array.from(baseline.keys()),
            calls: testLoadingFixture.calls,
            peak: testLoadingFixture.peak,
            statuses: testLoadingFixture.statuses
        }));
        for (const phase of ['metadata', 'content']) {
            assert.deepEqual(imported.calls[phase].slice().sort(), accepted.map(spec => spec.path).sort(),
                phase + ' must process every supported file, including identical duplicates, exactly once');
            assert.ok(imported.peak[phase] > 1 && imported.peak[phase] <= 8);
        }
        assert.equal(new Map(imported.casing).get('main.res'), 'MaIn.ReS',
            'The first filename casing must survive out-of-order completion');
        assert.ok(imported.files.includes('/stunts/opponents/animations/opp1win.webm'));
        assert.equal(imported.files.length, 17);
        assert.ok(imported.baseline.includes('mixed.hig'), 'Existing saves must retain their export baseline');
        assert.ok(imported.statuses.some(value => value.startsWith('Scanning game folder ')),
            'Directory enumeration must report progress');
        const saved = await page.evaluate(async () => {
            Module.FS.writeFile('/stunts/mixed.hig', Uint8Array.from([4, 5, 6]));
            await Module.persistFile('/stunts/mixed.hig', false);
            return testLoadingFixture.diskWrites;
        });
        assert.deepEqual(saved, [{ name: 'MiXeD.HIG', bytes: [4, 5, 6] }],
            'Saving must preserve the original filename casing after concurrent loading');
        const original = await loadingState(page);
        await page.evaluate(() => { window.testOriginalDirectory = directoryHandle; });

        for (const phase of ['metadata', 'content']) {
            await installLoadingFixture(page, specs);
            if (phase === 'content') await page.evaluate(() => { testLoadingFixture.automatic.metadata = true; });
            await page.locator('#open-folder').click();
            await assertConcurrentPhase(page, phase);
            const inFlight = await page.evaluate(phase => {
                const count = testLoadingFixture.active[phase];
                testLoadingFixture.pending[phase][0].finish(true);
                return count;
            }, phase);
            await page.waitForFunction(({ phase, expected }) => testLoadingFixture.active[phase] === expected,
                { phase, expected: inFlight - 1 });
            assert.equal(await page.locator('#open-folder').isDisabled(), true,
                'A failed import must drain other in-flight reads before enabling another selection');
            assert.equal(await page.locator('#start').isDisabled(), true);
            assert.equal(await loadingState(page), original, 'Failed imports must preserve the complete previous folder');
            await finishLoading(page);
            assert.match(await page.locator('#status').textContent(), new RegExp('^Folder loading failed: Fixture ' + phase + ' failure'));
            const settled = await page.evaluate(() => ({
                calls: testLoadingFixture.calls,
                active: testLoadingFixture.active,
                sameDirectory: directoryHandle === window.testOriginalDirectory
            }));
            assert.equal(settled.calls[phase].length, inFlight,
                'Workers must stop scheduling files after a failure');
            if (phase === 'metadata') assert.equal(settled.calls.content.length, 0);
            assert.deepEqual(settled.active, { metadata: 0, content: 0 });
            assert.equal(settled.sameDirectory, true);
            assert.equal(await loadingState(page), original);
        }

        const rejected = await page.evaluate(async () => {
            const cases = [
                { names: ['MAIN.RES', 'FONTDEF.FNT'], error: 'Select the folder containing' },
                { names: ['MAIN.RES', 'FONTDEF.FNT', 'FONTN.FNT', 'main.res'],
                    sizes: [3, 3, 3, 4], error: 'Conflicting game filenames' },
                { names: ['MAIN.RES', 'FONTDEF.FNT', 'FONTN.FNT', 'LARGE.TRK'],
                    sizes: [3, 3, 3, 256 * 1024 * 1024], error: 'Game folder resources exceed 256 MiB' },
                { names: ['MAIN.RES', 'FONTDEF.FNT', 'FONTN.FNT', 'main.res'],
                    conflicting: true, error: 'Conflicting game filenames' }
            ];
            const results = [];
            for (const scenario of cases) {
                const reads = [];
                const files = scenario.names.map((name, index) => ({ path: name, file: {
                    name, size: scenario.sizes?.[index] || 3,
                    async arrayBuffer() {
                        reads.push(name);
                        return Uint8Array.from(scenario.conflicting && index === 3 ? [9, 9, 9] : [1, 2, 3]).buffer;
                    }
                } }));
                let message = '';
                try { await loadGameFiles(files, null); }
                catch (error) { message = error.message; }
                results.push({ message, expected: scenario.error, reads });
            }
            return results;
        });
        for (const [index, result] of rejected.entries()) {
            assert.ok(result.message.startsWith(result.expected), 'Invalid folder must report ' + result.expected);
            if (index < 3) assert.deepEqual(result.reads, [], 'Validate names and sizes before reading contents');
            else assert.deepEqual(result.reads.slice().sort(), ['FONTDEF.FNT', 'FONTN.FNT', 'MAIN.RES', 'main.res']);
        }
        assert.equal(await loadingState(page), original, 'Every validation failure must preserve the previous folder');
        assert.equal(await page.evaluate(() => directoryHandle === window.testOriginalDirectory), true);
        assert.deepEqual(errors, [], 'Directory loading must not produce unhandled browser errors');
    } finally {
        await context.close();
    }
}

async function exerciseFallbackLoading(browser, url, temporary) {
    const directory = path.join(temporary, 'many-game-files');
    await fs.mkdir(directory);
    const names = ['MAIN.RES', 'FONTDEF.FNT', 'FONTN.FNT',
        ...Array.from({ length: 12 }, (_, index) => 'TRACK' + index + '.TRK')];
    await Promise.all(names.concat('IGNORE.EXE').map(name => fs.writeFile(path.join(directory, name), 'fixture')));
    await fs.mkdir(path.join(directory, 'archive'));
    await fs.writeFile(path.join(directory, 'archive', 'OLD.TRK'), 'ignored');
    const context = await browser.newContext();
    await context.addInitScript(() => { window.showDirectoryPicker = undefined; });
    const page = await context.newPage();
    try {
        await page.goto(url);
        await ready(page);
        await installLoadingFixture(page, [], false);
        await page.locator('#game-files').setInputFiles(directory);
        await assertConcurrentPhase(page, 'content');
        assert.equal(await page.locator('#start').isDisabled(), true);
        await page.evaluate(() => testLoadingFixture.release('content', true));
        await page.waitForFunction(() => !document.getElementById('start').disabled);
        const actual = await page.evaluate(() => ({
            calls: testLoadingFixture.calls.content.map(name => name.split('/').pop()),
            peak: testLoadingFixture.peak.content,
            mode: document.getElementById('save-mode').textContent
        }));
        assert.deepEqual(actual.calls.slice().sort(), names.slice().sort(),
            'Read-only directory loading must concurrently read supported immediate files exactly once');
        assert.ok(actual.peak > 1 && actual.peak <= 8);
        assert.match(actual.mode, /read-only/);
    } finally {
        await context.close();
    }
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
        assert.equal(await page.locator('#sound-enabled').isChecked(), true,
            'Sound must retain its existing enabled default');
        assert.equal(await page.locator('#show-fps').isChecked(), false);
        assert.equal(await page.locator('#show-control-layout').isChecked(), false);
        assert.equal(await page.locator('#power-gear').isChecked(), true);
        assert.equal(await page.locator('#legacy-collision').isChecked(), true);
        assert.equal(await page.locator('#left-corner-bias').isChecked(), true);
        assert.equal(await page.locator('#dual-way-switching').isChecked(), true);
        assert.equal((await page.locator('#dual-way-switching').locator('..').innerText()).trim(), 'Dual-Way Switching');
        assert.equal(await page.locator('#owoot').isChecked(), false);
        assert.equal(await page.locator('#amiga-grass').isChecked(), false);
        await page.evaluate(() => setLoading(true));
        for (const id of OPTIONS) assert.equal(await page.locator('#' + id).isDisabled(), true,
            id + ' must lock while loading');
        await page.evaluate(() => setLoading(false));
        await loadFixture(page, fixture);
        await page.locator('#start').click();
        assert.deepEqual(await page.evaluate(() => window.launchedArguments), [
            '--data-dir', '/stunts', '--ogg:on', '--fps:off', '--hv:auto', '--control-layout:off',
            '--pg:on', '--lc:on', '--lcb:on', '--dws:on'
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
        for (const id of ['sound-enabled', 'new-midi', ...RULES.slice(0, 4)]) await page.locator('#' + id).uncheck();
        await page.locator('#hypervision').selectOption('high');
        const stored = await storedOptions(page);
        for (const id of OPTIONS.filter(id => id !== 'hypervision')) assert.equal(typeof stored[id], 'boolean');
        await page.reload();
        await ready(page);
        for (const id of ['skip-intro', 'show-fps', 'show-control-layout', 'owoot', 'amiga-grass']) {
            assert.equal(await page.locator('#' + id).isChecked(), true, id + ' must persist across reload');
        }
        for (const id of ['sound-enabled', 'new-midi', ...RULES.slice(0, 4)]) {
            assert.equal(await page.locator('#' + id).isChecked(), false, id + ' must persist across reload');
        }
        assert.equal(await page.locator('#hypervision').inputValue(), 'high');
        await loadFixture(page, fixture);
        await page.locator('#start').click();
        assert.deepEqual(await page.evaluate(() => window.launchedArguments), [
            '--data-dir', '/stunts', '--nointro', '/ns', '--ogg:off', '--fps:on', '--hv:high', '--control-layout:on',
            '--pg:off', '--lc:off', '--lcb:off', '--dws:off', '--owoot', '/amigagrass'
        ]);

        await page.reload();
        await ready(page);
        await page.locator('#sound-enabled').check();
        assert.equal((await storedOptions(page))['sound-enabled'], true);
        await page.reload();
        await ready(page);
        assert.equal(await page.locator('#sound-enabled').isChecked(), true,
            'An explicitly re-enabled sound choice must persist');
        await loadFixture(page, fixture);
        await page.locator('#start').click();
        assert.equal(await page.evaluate(() => window.launchedArguments.includes('/ns')), false,
            'Enabled sound must omit the sound-disable argument');

        for (const [legacy, expected] of [[true, 'auto'], [false, 'off'], ['invalid', 'auto']]) {
            await page.evaluate(value => browserStorage(LAUNCH_OPTIONS_STORE, 'readwrite',
                { hypervision: value, 'show-fps': 'true', 'new-midi': false, 'unknown-preference': true }), legacy);
            await page.reload();
            await ready(page);
            assert.equal(await page.locator('#hypervision').inputValue(), expected,
                'Legacy checkbox values must migrate; unknown presets must use the browser default');
            assert.equal(await page.locator('#show-fps').isChecked(), false, 'Malformed boolean values must be ignored');
            assert.equal(await page.locator('#new-midi').isChecked(), false, 'Other remembered choices must survive migration');
            assert.equal(await page.locator('#sound-enabled').isChecked(), true,
                'Preferences from earlier versions must leave sound enabled');
            assert.equal(await page.locator('#dual-way-switching').isChecked(), true,
                'Preferences from earlier versions must preserve original dual-way switching');
            await page.locator('#show-fps').check();
            const migrated = await storedOptions(page);
            assert.equal(migrated.hypervision, expected,
                'Saving settings must replace the legacy boolean with the migrated preset');
            assert.equal(Object.hasOwn(migrated, 'unknown-preference'), false,
                'Saving settings must discard unknown preferences');
            await loadFixture(page, fixture);
            await page.locator('#start').click();
            const args = await page.evaluate(() => window.launchedArguments);
            assert.deepEqual(args.filter(arg => arg.startsWith('--hv:')),
                expected === 'off' ? [] : ['--hv:' + expected]);
            assert.equal(args.includes('/ns'), false);
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
        const url = 'http://127.0.0.1:' + server.address().port + '/';
        await exerciseDirectoryLoading(browser, url);
        await exerciseFallbackLoading(browser, url, temporary);
        console.log('PASS: wrapping web settings, sound enable/disable, six HyperVision presets, FPS, driving rules, unknown preference filtering, typed persistence, legacy migration, fullscreen canvas, landscape lock lifecycle, touch-action CSS, bounded concurrent direct/fallback folder reads, progress, duplicate handling and failed-import draining');
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
