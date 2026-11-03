#!/usr/bin/env node
'use strict';

// Install the test dependency outside the source tree's tracked files:
//   npm install --prefix out/wasm-browser-test playwright
//   out/wasm-browser-test/node_modules/.bin/playwright install chromium firefox
//   NODE_PATH=out/wasm-browser-test/node_modules node tools/scripts/test-wasm.js \
//       --data-dir /path/to/your/stunts --html out/sdl3-wasm/restunts.html
// The game directory is supplied only through the browser's folder picker.

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs/promises');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const { chromium, firefox } = require('playwright');
const { serveHtml } = require('./browser-test-server.js');

const ROOT = path.resolve(__dirname, '../..');
const DEFAULT_HTML = path.join(ROOT, 'out/sdl3-wasm/restunts.html');
const DEFAULT_OUTPUT = path.join(ROOT, 'out/wasm-browser-test/results');
const READY_TIMEOUT_MS = 120000;
const FRAME_SETTLE_MS = 1200;
const RACE_START_MS = 6000;
const IDLE_DEMO_TIMEOUT_MS = 90000;
const DRIVE_MS = 3000;
const KEY_HOLD_MS = 100;
const SAMPLE_WIDTH = 80;
const SAMPLE_HEIGHT = 50;
const MIN_COLORS = 16;
const MIN_SCREEN_CHANGE = 0.03;
const SAVE_NAME = 'WEB!$1.HIG';
const SAVE_BYTES = Buffer.from([0, 255, 1, 128, 13, 10, 32, 127]);
const SCREENSHOT_NAME = 'SHOT0001.PNG';
const SCREENSHOT_BYTES = Buffer.from(
    'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aWQAAAABJRU5ErkJggg==', 'base64');
const SAVE_FORMAT = 'restunts-saves';
const SAVE_VERSION = 1;
const DATA_DIRECTORY = '/stunts';
const FOLDER_DATABASE = 'restunts-folders';
const FOLDER_STORE = 'folders';
const FIRST_FOLDER = 'remembered-first';
const SECOND_FOLDER = 'remembered-second';
// Native resource loaders accept expanded shapes and compressed songs, voices and effects.
const RESOURCE_IMPORT_NAMES = ['SDGAME2.ESH', 'ALPINE.XvS', 'SKIDSLCT.PKM', 'ADENG1.PVC', 'GEENG.PSF'];
const ANIMATION_IMPORT_NAMES = ['Opponents/Animations/Opp1Win.WebM', 'opponents/animations/opp2lose.webm'];
const IGNORED_IMPORT_NAMES = ['SETUP.EXE', 'restunts.html', 'opp1win.webm',
    'opponents/animations/opp7win.webm', 'opponents/animations/opp1win.mp4',
    'opponents/other/opp1win.webm', 'unrelated/MAIN.RES'];

function options() {
    const result = { html: DEFAULT_HTML, output: DEFAULT_OUTPUT, browser: 'chromium', headed: false };
    const args = process.argv.slice(2);
    for (let index = 0; index < args.length; index++) {
        const arg = args[index];
        if (arg === '--headed') {
            result.headed = true;
        } else if (arg === '--no-isolation') {
            result.isolated = false;
        } else if (arg === '--multi-core') {
            result.multiCore = true;
        } else if (arg === '--serve') {
            result.serve = true;
        } else if (arg === '--direct-only') {
            result.directOnly = true;
        } else if (arg === '--idle-only') {
            result.idleOnly = true;
        } else if (arg === '--saves-only') {
            result.savesOnly = true;
        } else if (arg === '--help') {
            console.log('Usage: node tools/scripts/test-wasm.js --data-dir DIR ' +
                '[--html FILE] [--output DIR] [--browser chromium|firefox] [--headed] ' +
                '[--browser-executable FILE] [--serve] [--no-isolation] [--multi-core] [--track FILE.cstrk] ' +
                '[--direct-only|--saves-only|--idle-only]');
            process.exit(0);
        } else if (arg === '--browser') {
            result.browser = args[++index];
            assert.ok(['chromium', 'firefox'].includes(result.browser), 'Unsupported browser');
        } else if (['--html', '--data-dir', '--output', '--browser-executable', '--track'].includes(arg)) {
            assert.ok(args[index + 1], 'Missing value for ' + arg);
            result[arg.slice(2)] = path.resolve(args[++index]);
        } else {
            throw new Error('Unknown argument: ' + arg);
        }
    }
    assert.ok(result.directOnly || result['data-dir'], '--data-dir must name your own game data folder');
    return result;
}

function bundle(files) {
    return Buffer.from(JSON.stringify({ format: SAVE_FORMAT, version: SAVE_VERSION, files }));
}

async function importSave(page, bytes, name = 'test-saves.json') {
    await page.locator('#import').setInputFiles({ name, mimeType: 'application/json', buffer: bytes });
    await page.waitForFunction(() => document.getElementById('import').value === '' &&
        !document.getElementById('import').disabled);
    return page.locator('#status').innerText();
}

async function readSave(page) {
    return page.evaluate(({ directory, name }) => {
        const names = Module.FS.readdir(directory).filter(item => item.toLowerCase() === name.toLowerCase());
        return names.map(item => ({ name: item, bytes: Array.from(Module.FS.readFile(directory + '/' + item)) }));
    }, { directory: DATA_DIRECTORY, name: SAVE_NAME });
}

async function loadOwnData(page, settings, report) {
    await page.goto(settings.url, { timeout: READY_TIMEOUT_MS });
    assert.deepEqual(report.errors, [], 'Browser startup failed');
    await page.locator('#game-files').waitFor();
    await page.waitForFunction(() => !document.getElementById('game-files').disabled,
        null, { timeout: READY_TIMEOUT_MS });
    assert.equal(await page.locator('#start').isDisabled(), true,
        'Game must require a user-supplied data folder');
    const bundledFiles = await page.evaluate(directory => {
        if (!Module.FS.analyzePath(directory).exists) {
            return [];
        }
        return Module.FS.readdir(directory).filter(name => name !== '.' && name !== '..');
    }, DATA_DIRECTORY);
    assert.deepEqual(bundledFiles, [], 'Original game data must not be embedded in the HTML');
    await page.locator('#game-files').setInputFiles(settings['data-dir']);
    await page.waitForFunction(() => !document.getElementById('start').disabled,
        null, { timeout: READY_TIMEOUT_MS });
    const loadedFiles = await page.evaluate(directory => Module.FS.readdir(directory), DATA_DIRECTORY);
    assert.ok(loadedFiles.some(name => ['main.res', 'main.pre'].includes(name.toLowerCase())),
        'Folder picker did not load MAIN.RES/PRE into the game filesystem');
    report.dataFolderLoads++;
}

async function exerciseSaves(page, settings, report) {
    const entry = { name: SAVE_NAME, data: SAVE_BYTES.toString('base64') };
    assert.match(await importSave(page, bundle([entry])), /^Imported 1 file/);
    assert.deepEqual((await readSave(page))[0].bytes, Array.from(SAVE_BYTES));

    const changed = Buffer.from(SAVE_BYTES).reverse();
    assert.match(await importSave(page, bundle([{ name: SAVE_NAME.toLowerCase(),
        data: changed.toString('base64') }])), /^Imported 1 file/);
    const changedFiles = await readSave(page);
    assert.equal(changedFiles.length, 1, 'Case-insensitive imports must replace the same DOS file');
    assert.deepEqual(changedFiles[0].bytes, Array.from(changed));

    for (const unsafeName of ['../WEBTEST.HIG', '/WEBTEST.HIG', 'TOO-LONG-NAME.HIG', 'GAME.RES']) {
        const invalid = bundle([entry, { name: unsafeName, data: entry.data }]);
        assert.match(await importSave(page, invalid), /^Import failed:/);
        assert.deepEqual(await readSave(page), changedFiles,
            'Rejected save bundles must not partially overwrite valid entries');
    }
    assert.match(await importSave(page, bundle([entry, { name: 'BAD.HIG', data: '!' }])), /^Import failed:/);
    assert.deepEqual(await readSave(page), changedFiles, 'Invalid base64 must reject the complete bundle');

    const downloadPromise = page.waitForEvent('download');
    await page.locator('#export').click();
    const download = await downloadPromise;
    assert.equal(download.suggestedFilename(), 'restunts-saves.json');
    const downloadedPath = path.join(settings.output, 'roundtrip-saves.json');
    await download.saveAs(downloadedPath);
    const exported = await fs.readFile(downloadedPath);
    const parsed = JSON.parse(exported);
    assert.equal(parsed.format, SAVE_FORMAT);
    assert.equal(parsed.version, SAVE_VERSION);
    assert.equal(parsed.files.length, 1, 'Unchanged game files must not be included in the save export');
    assert.equal(parsed.files[0].name.toLowerCase(), SAVE_NAME.toLowerCase());
    assert.deepEqual(Buffer.from(parsed.files[0].data, 'base64'), changed);

    await loadOwnData(page, settings, report);
    assert.match(await importSave(page, exported), /^Imported 1 file/);
    assert.deepEqual((await readSave(page))[0].bytes, Array.from(changed));
    report.saveRoundtrip = true;
    report.atomicImportRejections = true;
}

async function exerciseExitFeedback(context, settings, report) {
    const page = await context.newPage();
    page.on('pageerror', error => report.errors.push(String(error)));
    try {
        await page.goto(settings.url, { timeout: READY_TIMEOUT_MS });
        await page.waitForFunction(() => !document.getElementById('open-folder').disabled ||
            !document.getElementById('game-files').disabled, null, { timeout: READY_TIMEOUT_MS });
        await page.evaluate(() => Module.onExit(1));
        assert.match(await page.locator('#status').innerText(), /^Game exited with code 1\./,
            'Failed exits without stderr must retain their exit code');

        const message = 'Broderbund Stunts 1.1 (Feb 12 1991) game data is required. ' +
            'MISC.RES/MISC.PRE has an unexpected gver.';
        await page.evaluate(message => {
            Module.printErr(message);
            Module.printErr('');
            Module.print('Closing the game');
            Module.onExit(1);
        }, message);
        assert.equal(await page.locator('#status').isVisible(), true);
        assert.equal(await page.locator('#status').innerText(),
            'Game error: ' + message + ' Reload the page to try again.',
            'Failed startup must display the stderr diagnostic outside the collapsed game log');
        assert.equal(await page.locator('#fullscreen').isDisabled(), true);

        await page.evaluate(() => Module.onExit(0));
        assert.match(await page.locator('#status').innerText(), /^Game closed\./,
            'Successful exits must not display previous stderr warnings as failures');
        report.exitErrorFeedback = true;
    } finally {
        await page.close();
    }
}

async function checkScreenshotDownload(page, settings, filename, label) {
    const downloadPending = page.waitForEvent('download');
    await page.evaluate(async name => Module.saveScreenshot(name), filename);
    const download = await downloadPending;
    assert.equal(download.suggestedFilename(), SCREENSHOT_NAME);
    const output = path.join(settings.output, label + '.png');
    await download.saveAs(output);
    assert.deepEqual(await fs.readFile(output), SCREENSHOT_BYTES,
        'Screenshot download must preserve the encoded PNG');
}

async function exerciseReadOnlyScreenshot(context, settings, report) {
    const page = await context.newPage();
    page.on('pageerror', error => report.errors.push(String(error)));
    await page.addInitScript(() => { window.showDirectoryPicker = undefined; });
    try {
        await page.goto(settings.url, { timeout: READY_TIMEOUT_MS });
        await page.waitForFunction(() => !document.getElementById('game-files').disabled,
            null, { timeout: READY_TIMEOUT_MS });
        await page.evaluate(({ directory, name, bytes }) => {
            Module.FS.chdir(directory);
            Module.FS.writeFile(name, Uint8Array.from(bytes));
        }, { directory: DATA_DIRECTORY, name: SCREENSHOT_NAME, bytes: Array.from(SCREENSHOT_BYTES) });
        await checkScreenshotDownload(page, settings, SCREENSHOT_NAME, 'screenshot-read-only');
        assert.match(await page.locator('#status').innerText(), /^Downloaded screenshot SHOT0001\.PNG\.$/);
        report.screenshotReadOnlyDownload = true;
    } finally {
        await page.close();
    }
}

async function exerciseDirectSaving(context, settings, report) {
    const page = await context.newPage();
    page.on('pageerror', error => report.errors.push(String(error)));
    await page.addInitScript(({ resourceNames, animationNames, ignoredNames, fixtureBytes }) => {
        // Model the granted directory in memory. Never write into the user's data folder.
        const initialBytes = [1, 2, 3];
        const files = new Map(['MAIN.RES', 'FONTDEF.FNT', 'FONTN.FNT', 'MiXeD.HIG']
            .map(name => [name, Uint8Array.from(initialBytes)]));
        for (const name of resourceNames.concat(animationNames, ignoredNames)) {
            files.set(name, Uint8Array.from(fixtureBytes));
        }
        files.set('SHOT0001.PNG', Uint8Array.from(initialBytes));
        files.set('sHoT0002.PnG', Uint8Array.from(initialBytes));
        files.set('SHOT0003.PNG/keep.txt', Uint8Array.from(initialBytes));
        window.testDirectory = files;
        window.testWriteFailure = false;
        window.testAbortedWrites = 0;
        window.testDiskOperations = [];
        function fileHandle(name, prefix = '') {
            const key = prefix + name;
            return {
                kind: 'file', name,
                async getFile() { return new File([files.get(key)], name); },
                async createWritable() {
                    let pending;
                    return {
                        async write(bytes) {
                            if (window.testWriteFailure) {
                                throw new DOMException('Test permission revoked', 'NotAllowedError');
                            }
                            pending = Uint8Array.from(bytes);
                        },
                        async close() { files.set(key, pending); },
                        async abort() { window.testAbortedWrites++; }
                    };
                }
            };
        }
        function directoryHandle(name, prefix = '') {
            return {
                kind: 'directory', name,
                async *values() {
                    const children = new Set();
                    for (const key of files.keys()) {
                        if (!key.startsWith(prefix)) continue;
                        const relative = key.slice(prefix.length);
                        const separator = relative.indexOf('/');
                        const child = separator < 0 ? relative : relative.slice(0, separator);
                        if (children.has(child)) continue;
                        children.add(child);
                        yield separator < 0 ? fileHandle(child, prefix) :
                            directoryHandle(child, prefix + child + '/');
                    }
                },
                async getFileHandle(name) {
                    window.testDiskOperations.push('open:' + name);
                    return fileHandle(name);
                },
                async removeEntry(name) {
                    window.testDiskOperations.push('remove:' + name);
                    if (!files.delete(name)) { throw new DOMException('Missing', 'NotFoundError'); }
                }
            };
        }
        window.showDirectoryPicker = async options => {
            window.testPickerOptions = options;
            return directoryHandle('test-directory');
        };
    }, { resourceNames: RESOURCE_IMPORT_NAMES, animationNames: ANIMATION_IMPORT_NAMES,
        ignoredNames: IGNORED_IMPORT_NAMES, fixtureBytes: Array.from(SAVE_BYTES) });
    try {
        await page.goto(settings.url, { timeout: READY_TIMEOUT_MS });
        assert.deepEqual(report.errors, [], 'Browser startup failed');
        await page.waitForFunction(() => !document.getElementById('open-folder').disabled,
            null, { timeout: READY_TIMEOUT_MS });
        assert.equal(await page.locator('#open-folder').isVisible(), true);
        assert.equal(await page.locator('#export').isVisible(), false);
        assert.equal(await page.locator('#import').isVisible(), false);
        await page.locator('#open-folder').click();
        await page.waitForFunction(() => !document.getElementById('start').disabled);
        assert.deepEqual(await page.evaluate(() => window.testPickerOptions), { mode: 'readwrite' });
        assert.match(await page.locator('#log').textContent(), /Folder could not be remembered for next time:/,
            'Non-cloneable picker fixtures must exercise recoverable IndexedDB storage failures');
        report.rememberedFolderCloneFailureRecovery = true;

        const importedResources = await page.evaluate(({ directory, resourceNames, ignoredNames }) => ({
            resources: resourceNames.map(name => {
                const filename = directory + '/' + name.toLowerCase();
                return { name, bytes: Module.FS.analyzePath(filename).exists ?
                    Array.from(Module.FS.readFile(filename)) : null };
            }),
            ignored: ignoredNames.filter(name => Module.FS.analyzePath(directory + '/' + name.toLowerCase()).exists)
        }), { directory: DATA_DIRECTORY, resourceNames: RESOURCE_IMPORT_NAMES.concat(ANIMATION_IMPORT_NAMES),
            ignoredNames: IGNORED_IMPORT_NAMES });
        for (const resource of importedResources.resources) {
            assert.deepEqual(resource.bytes, Array.from(SAVE_BYTES),
                'Folder import must preserve supported resource ' + resource.name);
        }
        assert.deepEqual(importedResources.ignored, [], 'Folder import must exclude executables and HTML');
        report.resourceFormatImports = true;

        const screenshotResults = await page.evaluate(async ({ directory, name, bytes }) => {
            Module.FS.chdir(directory);
            Module.FS.writeFile(name, Uint8Array.from(bytes));
            await Module.saveScreenshot(name);
            const status = document.getElementById('status').textContent;
            // Repeat with the same MEMFS source to model numbering restarting next session.
            await Module.saveScreenshot(name);
            const operations = window.testDiskOperations.length;
            let unsafeRejected = false;
            try { await Module.saveScreenshot('../' + name); }
            catch (_) { unsafeRejected = true; }
            return { status, unsafeRejected, noUnsafeWrites: operations === window.testDiskOperations.length,
                existing: Array.from(window.testDirectory.get('SHOT0001.PNG')),
                mixedCase: Array.from(window.testDirectory.get('sHoT0002.PnG')),
                first: Array.from(window.testDirectory.get('SHOT0004.PNG')),
                second: Array.from(window.testDirectory.get('SHOT0005.PNG')) };
        }, { directory: DATA_DIRECTORY, name: SCREENSHOT_NAME, bytes: Array.from(SCREENSHOT_BYTES) });
        assert.equal(screenshotResults.status, 'Saved SHOT0004.PNG to your game folder.');
        assert.deepEqual(screenshotResults.existing, [1, 2, 3]);
        assert.deepEqual(screenshotResults.mixedCase, [1, 2, 3]);
        assert.deepEqual(screenshotResults.first, Array.from(SCREENSHOT_BYTES));
        assert.deepEqual(screenshotResults.second, Array.from(SCREENSHOT_BYTES));
        assert.equal(screenshotResults.unsafeRejected, true);
        assert.equal(screenshotResults.noUnsafeWrites, true);
        report.screenshotDirectSaving = true;
        report.screenshotExistingNamesPreserved = true;

        const directResults = await page.evaluate(async ({ directory, bytes }) => {
            const modified = Uint8Array.from(bytes);
            Module.FS.writeFile(directory + '/mixed.hig', modified);
            await Module.persistFile(directory + '/mixed.hig', false);
            const namesAfterOverwrite = Array.from(window.testDirectory.keys());
            const originalCaseBytes = Array.from(window.testDirectory.get('MiXeD.HIG'));
            Module.FS.chdir(directory);
            await Module.persistFile('./mixed.hig', false);
            Module.FS.writeFile('fresh!$.hig', modified);
            await Module.persistFile('fresh!$.hig', false);
            const createdBytes = Array.from(window.testDirectory.get('fresh!$.hig'));
            Module.FS.unlink('fresh!$.hig');
            await Module.persistFile('fresh!$.hig', true);
            const removed = !window.testDirectory.has('fresh!$.hig');
            await Module.persistFile('fresh!$.hig', true); // Removing an absent file is harmless.

            const operationsBeforeScratch = window.testDiskOperations.length;
            Module.FS.writeFile('scratch.tmp', modified);
            await Module.persistFile('./scratch.tmp', false);
            Module.FS.unlink('scratch.tmp');
            await Module.persistFile('./scratch.tmp', true);
            const scratchIgnored = !window.testDirectory.has('scratch.tmp') &&
                operationsBeforeScratch === window.testDiskOperations.length;

            let unsafeRejected = false;
            try { await Module.persistFile('../outside.hig', false); }
            catch (_) { unsafeRejected = true; }
            window.testWriteFailure = true;
            Module.FS.writeFile('mixed.hig', Uint8Array.from([9, 8, 7]));
            let failedWriteRejected = false;
            try { await Module.persistFile('mixed.hig', false); }
            catch (_) { failedWriteRejected = true; }
            return { namesAfterOverwrite, originalCaseBytes, createdBytes, removed, scratchIgnored, unsafeRejected,
                failedWriteRejected, aborted: window.testAbortedWrites,
                bytesAfterFailure: Array.from(window.testDirectory.get('MiXeD.HIG')),
                inMemoryAfterFailure: Array.from(Module.FS.readFile('mixed.hig')) };
        }, { directory: DATA_DIRECTORY, bytes: Array.from(SAVE_BYTES) });
        assert.ok(directResults.namesAfterOverwrite.includes('MiXeD.HIG'));
        assert.ok(!directResults.namesAfterOverwrite.includes('mixed.hig'));
        assert.deepEqual(directResults.originalCaseBytes, Array.from(SAVE_BYTES));
        assert.deepEqual(directResults.createdBytes, Array.from(SAVE_BYTES));
        assert.equal(directResults.removed, true);
        assert.equal(directResults.scratchIgnored, true);
        assert.equal(directResults.unsafeRejected, true);
        assert.equal(directResults.failedWriteRejected, true);
        assert.equal(directResults.aborted, 1);
        assert.deepEqual(directResults.bytesAfterFailure, Array.from(SAVE_BYTES));
        assert.deepEqual(directResults.inMemoryAfterFailure, [9, 8, 7]);
        assert.equal(await page.locator('#export').isVisible(), true);
        assert.equal(await page.locator('#export').isEnabled(), true);
        assert.match(await page.locator('#status').innerText(), /^Direct saving failed:/);
        report.directSaving = true;
        report.directSavingCasePreserved = true;
        report.directSavingFailureRecovery = true;
        await checkScreenshotDownload(page, settings, SCREENSHOT_NAME, 'screenshot-write-failed');
        assert.equal(await page.evaluate(() => window.testAbortedWrites), 2);
        assert.match(await page.locator('#status').innerText(), /^Screenshot folder write failed\./);
        assert.equal(await page.evaluate(() => window.testDirectory.has('SHOT0006.PNG')), false);
        report.screenshotWriteFailureDownload = true;
        // Changing folders must remove optional assets from the previous selection.
        await page.evaluate(names => names.forEach(name => window.testDirectory.delete(name)), ANIMATION_IMPORT_NAMES);
        await page.locator('#open-folder').click();
        await page.waitForFunction(() => !document.getElementById('open-folder').disabled);
        assert.equal(await page.evaluate(directory => Module.FS.analyzePath(directory).exists,
            DATA_DIRECTORY + '/opponents'), false, 'Folder changes must clear previous animation directories');
        report.animationDirectImports = true;
    } finally {
        await page.close();
    }
}

async function exerciseReadOnlyAnimationImports(context, settings, report) {
    const page = await context.newPage();
    page.on('pageerror', error => report.errors.push(String(error)));
    await page.addInitScript(() => { window.showDirectoryPicker = undefined; });
    async function selectFiles(extra) {
        await page.evaluate(({ extra, bytes }) => {
            const entries = ['MAIN.RES', 'FONTDEF.FNT', 'FONTN.FNT']
                .map(path => ({ path, bytes })).concat(extra);
            const files = entries.map(entry => {
                const file = new File([Uint8Array.from(entry.bytes)], entry.path.split('/').pop());
                Object.defineProperty(file, 'webkitRelativePath', { value: 'selected/' + entry.path });
                return file;
            });
            const input = document.getElementById('game-files');
            Object.defineProperty(input, 'files', { configurable: true, value: files });
            input.dispatchEvent(new Event('change'));
        }, { extra, bytes: Array.from(SAVE_BYTES) });
        await page.waitForFunction(() => !document.getElementById('game-files').disabled);
    }
    try {
        await page.goto(settings.url, { timeout: READY_TIMEOUT_MS });
        await page.waitForFunction(() => !document.getElementById('game-files').disabled,
            null, { timeout: READY_TIMEOUT_MS });
        const animationPath = ANIMATION_IMPORT_NAMES[0].toLowerCase();
        const animation = { path: ANIMATION_IMPORT_NAMES[0], bytes: Array.from(SAVE_BYTES) };
        await selectFiles([animation, { ...animation, path: animationPath },
            ...IGNORED_IMPORT_NAMES.map(path => ({ path, bytes: animation.bytes }))]);
        assert.match(await page.locator('#status').innerText(), /^Loaded 4 game files/);
        const imported = await page.evaluate(({ directory, name, ignored }) => ({
            bytes: Array.from(Module.FS.readFile(directory + '/' + name)),
            ignored: ignored.filter(path => Module.FS.analyzePath(directory + '/' + path.toLowerCase()).exists)
        }), { directory: DATA_DIRECTORY, name: animationPath, ignored: IGNORED_IMPORT_NAMES });
        assert.deepEqual(imported.bytes, animation.bytes);
        assert.deepEqual(imported.ignored, [], 'Read-only folders must ignore unsupported nested assets');
        await selectFiles([animation, { path: animationPath, bytes: [1] }]);
        assert.match(await page.locator('#status').innerText(), /^Folder loading failed: Conflicting game filenames/);
        assert.deepEqual(await page.evaluate(path => Array.from(Module.FS.readFile(path)),
            DATA_DIRECTORY + '/' + animationPath), animation.bytes,
            'Conflicting animation filenames must preserve the previous complete folder');
        await selectFiles([]);
        assert.match(await page.locator('#status').innerText(), /^Loaded 3 game files/);
        assert.equal(await page.evaluate(directory => Module.FS.analyzePath(directory).exists,
            DATA_DIRECTORY + '/opponents'), false);
        report.animationReadOnlyImports = true;
    } finally {
        await page.close();
    }
}

async function installRememberedFolderFixture(page, disableDatabase = false) {
    await page.addInitScript(({ disableDatabase, initialFolder }) => {
        const initialBytes = [1, 2, 3];
        window.testPermissionState = sessionStorage.getItem('folder-permission') || 'granted';
        window.testPermissionResult = 'granted';
        window.testPermissionRequests = [];
        window.testPermissionQueries = [];
        window.testPickerCalls = 0;
        window.testPickerCancelled = false;
        window.testPickFolder = initialFolder;
        const permissionMethods = {
            async queryPermission(options) {
                window.testPermissionQueries.push(options);
                return window.testPermissionState;
            },
            async requestPermission(options) {
                window.testPermissionRequests.push({ options, active: navigator.userActivation.isActive });
                return window.testPermissionResult;
            }
        };
        if (typeof FileSystemHandle !== 'undefined') {
            for (const [name, method] of Object.entries(permissionMethods)) {
                Object.defineProperty(FileSystemHandle.prototype, name, { configurable: true, value: method });
            }
        }
        // Firefox exposes OPFS on file://, so its test uses real browser handles. Chromium
        // rejects OPFS there. Only the marked synthetic fixture is rehydrated on IDB reads;
        // IDB serialization, origin scoping and persistence remain the browser's own work.
        const fixturePrototype = {
            ...permissionMethods,
            async *values() {
                for (const name of Object.keys(this.files)) { yield await this.getFileHandle(name); }
            },
            async getFileHandle(name) {
                const files = this.files;
                return {
                    kind: 'file', name,
                    async getFile() { return new File([Uint8Array.from(files[name])], name); },
                    async createWritable() {
                        let pending;
                        return {
                            async write(bytes) { pending = Array.from(bytes); },
                            async close() { files[name] = pending; },
                            async abort() {}
                        };
                    }
                };
            }
        };
        const resultDescriptor = Object.getOwnPropertyDescriptor(IDBRequest.prototype, 'result');
        Object.defineProperty(IDBRequest.prototype, 'result', {
            ...resultDescriptor,
            get() {
                const result = resultDescriptor.get.call(this);
                if (result && result.testFolderFixture === true) Object.setPrototypeOf(result, fixturePrototype);
                return result;
            }
        });
        window.showDirectoryPicker = async () => {
            window.testPickerCalls++;
            if (window.testPickerCancelled) throw new DOMException('Cancelled by test', 'AbortError');
            let selected;
            try {
                const root = await navigator.storage.getDirectory();
                selected = await root.getDirectoryHandle(window.testPickFolder, { create: true });
                for (const name of ['MAIN.RES', 'FONTDEF.FNT', 'FONTN.FNT']) {
                    const handle = await selected.getFileHandle(name, { create: true });
                    const writable = await handle.createWritable();
                    await writable.write(Uint8Array.from(initialBytes));
                    await writable.close();
                }
                window.testFolderHandleType = 'native FileSystemDirectoryHandle';
            } catch (error) {
                if (error.name !== 'SecurityError') throw error;
                selected = Object.assign(Object.create(fixturePrototype), {
                    kind: 'directory', name: window.testPickFolder, testFolderFixture: true,
                    files: Object.fromEntries(['MAIN.RES', 'FONTDEF.FNT', 'FONTN.FNT']
                        .map(name => [name, initialBytes.slice()]))
                });
                window.testFolderHandleType = 'serializable directory fixture';
            }
            window.testPickedHandle = selected;
            return selected;
        };
        if (disableDatabase) Object.defineProperty(window, 'indexedDB', { value: undefined });
    }, { disableDatabase, initialFolder: FIRST_FOLDER });
}

async function storedFolderName(page) {
    return page.evaluate(({ database, store }) => new Promise((resolve, reject) => {
        const request = indexedDB.open(database);
        request.onerror = () => reject(request.error);
        request.onsuccess = () => {
            const db = request.result;
            const transaction = db.transaction(store, 'readonly');
            const read = transaction.objectStore(store).get(location.href);
            let name;
            read.onsuccess = () => { name = read.result && read.result.name; };
            transaction.oncomplete = () => { db.close(); resolve(name); };
            transaction.onabort = () => { db.close(); reject(transaction.error); };
        };
    }), { database: FOLDER_DATABASE, store: FOLDER_STORE });
}

async function waitForFolderControls(page) {
    // Firefox can restore enabled state before runtime initialization; the picker stays hidden then.
    await page.waitForFunction(() => {
        const picker = document.getElementById('open-folder');
        return !picker.hidden && !picker.disabled;
    }, null, { timeout: READY_TIMEOUT_MS });
}

async function exerciseRememberedFolders(context, settings, report) {
    const page = await context.newPage();
    page.setDefaultTimeout(READY_TIMEOUT_MS);
    page.on('pageerror', error => report.errors.push(String(error)));
    await installRememberedFolderFixture(page);
    try {
        await page.goto(settings.url, { timeout: READY_TIMEOUT_MS });
        await waitForFolderControls(page);
        assert.equal(await page.locator('#open-folder').innerText(), 'Choose game folder');
        await page.locator('#open-folder').click();
        await waitForFolderControls(page);
        assert.equal(await page.locator('#start').isEnabled(), true);
        assert.equal(await page.locator('#open-folder').innerText(), 'Change game folder');
        assert.equal(await storedFolderName(page), FIRST_FOLDER);
        report.rememberedFolderHandleType = await page.evaluate(() => window.testFolderHandleType);
        if (settings.browser === 'firefox') {
            assert.equal(report.rememberedFolderHandleType, 'native FileSystemDirectoryHandle',
                'Firefox must exercise native handle structured cloning, not the Chromium fixture');
        }

        await page.reload({ timeout: READY_TIMEOUT_MS });
        await page.waitForFunction(() => !document.getElementById('start').disabled,
            null, { timeout: READY_TIMEOUT_MS });
        await waitForFolderControls(page);
        assert.equal(await page.locator('#start').isEnabled(), true, 'Granted folder must load after reload');
        assert.equal(await page.locator('#open-folder').innerText(), 'Change game folder');
        assert.equal(await page.locator('#restore-folder').isVisible(), false);
        assert.equal(await page.evaluate(() => window.testPickerCalls), 0, 'Reload must not open a folder picker');
        assert.deepEqual(await page.evaluate(() => window.testPermissionRequests), [],
            'Granted restoration must not prompt for permission');
        assert.deepEqual(await page.evaluate(() => window.testPermissionQueries), [{ mode: 'readwrite' }]);

        await page.evaluate(() => sessionStorage.setItem('folder-permission', 'prompt'));
        await page.reload({ timeout: READY_TIMEOUT_MS });
        await page.locator('#restore-folder').waitFor({ state: 'visible' });
        await waitForFolderControls(page);
        assert.equal(await page.locator('#start').isEnabled(), false);
        assert.equal(await page.locator('#open-folder').innerText(), 'Change game folder');
        assert.equal(await page.locator('#restore-folder').isVisible(), true);
        assert.equal(await page.locator('#restore-folder').innerText(), 'Allow folder access');
        assert.deepEqual(await page.evaluate(() => window.testPermissionRequests), [],
            'Loading a remembered handle must wait for a click before requesting permission');
        await page.evaluate(() => { window.testPermissionResult = 'denied'; });
        await page.locator('#restore-folder').click();
        await waitForFolderControls(page);
        assert.equal(await page.locator('#start').isEnabled(), false);
        assert.equal(await page.locator('#restore-folder').isVisible(), true, 'Denied access must allow retry');
        assert.equal(await storedFolderName(page), FIRST_FOLDER, 'Denied access must retain the remembered handle');
        await page.evaluate(() => { window.testPermissionResult = 'granted'; });
        await page.locator('#restore-folder').click();
        await waitForFolderControls(page);
        assert.equal(await page.locator('#start').isEnabled(), true);
        assert.equal(await page.evaluate(() => window.testPickerCalls), 0);
        assert.deepEqual(await page.evaluate(() => window.testPermissionRequests), [
            { options: { mode: 'readwrite' }, active: true },
            { options: { mode: 'readwrite' }, active: true }
        ], 'Permission requests must run while the Allow button click has user activation');

        await page.evaluate(() => { window.testPickerCancelled = true; });
        await page.locator('#open-folder').click();
        await waitForFolderControls(page);
        assert.equal(await page.locator('#start').isEnabled(), true, 'Cancelling Change must preserve loaded data');
        assert.equal(await storedFolderName(page), FIRST_FOLDER);
        await page.evaluate(name => {
            window.testPickerCancelled = false;
            window.testPickFolder = name;
        }, SECOND_FOLDER);
        await page.locator('#open-folder').click();
        await waitForFolderControls(page);
        assert.equal(await storedFolderName(page), SECOND_FOLDER, 'Change must replace the remembered folder');
        await page.evaluate(() => sessionStorage.setItem('folder-permission', 'granted'));
        await page.reload({ timeout: READY_TIMEOUT_MS });
        await page.waitForFunction(() => !document.getElementById('start').disabled,
            null, { timeout: READY_TIMEOUT_MS });
        await waitForFolderControls(page);
        assert.equal(await page.locator('#start').isEnabled(), true);
        assert.equal(await storedFolderName(page), SECOND_FOLDER);
        assert.equal(await page.evaluate(() => window.testPickerCalls), 0);
        report.rememberedFolderRestore = true;
        report.rememberedFolderPermissionRetry = true;
        report.rememberedFolderChangeAndCancel = true;
    } catch (error) {
        report.rememberedFolderStatus = await page.locator('#status').innerText();
        report.rememberedFolderLog = await page.locator('#log').textContent();
        await page.screenshot({ path: path.join(settings.output, 'remembered-folder-failure.png'), fullPage: true });
        throw error;
    } finally {
        await page.close();
    }

    const unavailable = await context.newPage();
    unavailable.setDefaultTimeout(READY_TIMEOUT_MS);
    unavailable.on('pageerror', error => report.errors.push(String(error)));
    await installRememberedFolderFixture(unavailable, true);
    try {
        await unavailable.goto(settings.url, { timeout: READY_TIMEOUT_MS });
        await waitForFolderControls(unavailable);
        assert.equal(await unavailable.locator('#open-folder').innerText(), 'Choose game folder');
        await unavailable.locator('#open-folder').click();
        await waitForFolderControls(unavailable);
        assert.equal(await unavailable.locator('#start').isEnabled(), true,
            'Unavailable IndexedDB must not prevent selecting and loading a folder');
        const savedBytes = await unavailable.evaluate(async ({ directory, name, bytes }) => {
            Module.FS.writeFile(directory + '/' + name, Uint8Array.from(bytes));
            await Module.persistFile(directory + '/' + name, false);
            const handle = await window.testPickedHandle.getFileHandle(name);
            return Array.from(new Uint8Array(await (await handle.getFile()).arrayBuffer()));
        }, { directory: DATA_DIRECTORY, name: SAVE_NAME.toLowerCase(), bytes: Array.from(SAVE_BYTES) });
        assert.deepEqual(savedBytes, Array.from(SAVE_BYTES), 'Direct saving must survive unavailable IndexedDB');
        report.rememberedFolderStorageUnavailableRecovery = true;
    } finally {
        await unavailable.close();
    }
}

async function capture(page, settings, name) {
    const screenshot = await page.locator('#canvas').screenshot({ path: path.join(settings.output, name + '.png') });
    const samples = await page.evaluate(async ({ encoded, width, height }) => {
        const bytes = Uint8Array.from(atob(encoded), item => item.charCodeAt(0));
        const bitmap = await createImageBitmap(new Blob([bytes], { type: 'image/png' }));
        const sample = new OffscreenCanvas(width, height);
        const context = sample.getContext('2d');
        context.drawImage(bitmap, 0, 0, width, height);
        bitmap.close();
        return Array.from(context.getImageData(0, 0, width, height).data);
    }, { encoded: screenshot.toString('base64'), width: SAMPLE_WIDTH, height: SAMPLE_HEIGHT });
    const colors = new Set();
    for (let index = 0; index < samples.length; index += 4) {
        colors.add(samples.slice(index, index + 3).join(','));
    }
    assert.ok(colors.size >= MIN_COLORS, name + ': game canvas appears blank or failed to render');
    return { samples, colors: colors.size, sha256: crypto.createHash('sha256').update(screenshot).digest('hex') };
}

function assertChanged(before, after, name) {
    let different = 0;
    for (let index = 0; index < before.samples.length; index++) {
        if (before.samples[index] !== after.samples[index]) {
            different++;
        }
    }
    assert.ok(different / before.samples.length >= MIN_SCREEN_CHANGE,
        name + ': canvas did not change enough to show the expected game transition');
}

async function key(page, name) {
    await page.keyboard.press(name, { delay: KEY_HOLD_MS });
    await page.waitForTimeout(FRAME_SETTLE_MS);
}

async function exerciseIdleDemo(page, settings, report) {
    await page.evaluate(() => {
        window.testDemoOpened = false;
        const open = Module.FS.open;
        Module.FS.open = function(filename, ...args) {
            const result = open.call(this, filename, ...args);
            if (typeof filename === 'string' && filename.toLowerCase().endsWith('/default.rpl')) {
                window.testDemoOpened = true;
            }
            return result;
        };
    });
    await page.locator('#skip-intro').check();
    await page.locator('#start').click();
    await page.mouse.move(0, 0);
    await page.waitForTimeout(FRAME_SETTLE_MS);
    const menu = await capture(page, settings, 'idle-menu');
    await page.waitForFunction(() => window.testDemoOpened, null, { timeout: IDLE_DEMO_TIMEOUT_MS });
    await page.waitForTimeout(RACE_START_MS);
    const demo = await capture(page, settings, 'idle-demo');
    assertChanged(menu, demo, 'Main-menu idle demo starts');
    await page.waitForTimeout(DRIVE_MS);
    const playing = await capture(page, settings, 'idle-demo-playing');
    assertChanged(demo, playing, 'Idle replay continues playing without a disk-error dialog');
    assert.doesNotMatch(await page.locator('#log').textContent(), /Cannot load game resource:/);
    report.idleDemo = true;
}

async function exerciseGame(page, settings, report) {
    if (settings.track) {
        const bytes = await fs.readFile(settings.track);
        await page.evaluate(({ directory, bytes }) => {
            // Substitute only the browser's in-memory startup track; keep the
            // supplied game directory and the original track file unchanged.
            for (const name of Module.FS.readdir(directory)) {
                if (name.toLowerCase() === 'default.trk') Module.FS.unlink(directory + '/' + name);
            }
            Module.FS.writeFile(directory + '/default.cstrk', Uint8Array.from(bytes));
        }, { directory: DATA_DIRECTORY, bytes: Array.from(bytes) });
        report.track = settings.track;
    }
    if (settings.multiCore) {
        await page.locator('#multi-core').check();
        await page.locator('#hypervision').selectOption('full');
    }
    await page.locator('#skip-intro').check();
    await page.locator('#start').click();
    await page.waitForTimeout(FRAME_SETTLE_MS);
    assert.equal(await page.locator('#import').isDisabled(), true);
    assert.equal(await page.locator('#start').isDisabled(), true);
    await page.locator('#canvas').focus();
    await page.mouse.move(0, 0);
    const menu = await capture(page, settings, 'menu');
    await key(page, 'ArrowLeft');
    await key(page, 'Enter');
    const carMenu = await capture(page, settings, 'car-menu');
    assertChanged(menu, carMenu, 'Keyboard navigation to car menu');
    await key(page, 'Escape');
    const returned = await capture(page, settings, 'menu-returned');
    assertChanged(carMenu, returned, 'Keyboard return to main menu');
    await key(page, 'Enter');
    await page.waitForTimeout(RACE_START_MS);
    const race = await capture(page, settings, 'race-start');
    if (settings.multiCore) {
        const workerCount = await page.evaluate(() =>
            typeof PThread === 'object' ? Object.keys(PThread.pthreads).length : 0);
        assert.ok(workerCount > 0, 'Multi-core rendering must create native browser render threads');
        report.renderWorkers = workerCount;
    }
    assertChanged(returned, race, 'Starting a race');
    const initialFrames = await page.evaluate(() => window.testAnimationFrames);
    await page.keyboard.down('ArrowUp');
    await page.waitForTimeout(DRIVE_MS);
    await page.keyboard.up('ArrowUp');
    const driving = await capture(page, settings, 'driving');
    assertChanged(race, driving, 'Race animation while accelerating');
    await key(page, 'F12');
    const supersight = await capture(page, settings, 'supersight');
    assert.ok(await page.evaluate(() => window.testAnimationFrames) > initialFrames,
        'Game must yield to browser animation frames');
    assert.doesNotMatch(await page.locator('#status').innerText(), /error|exited|closed/i);
    report.keyboardNavigation = true;
    report.driving = true;
    report.screenshots = Object.fromEntries(Object.entries({ menu, carMenu, returned, race, driving, supersight })
        .map(([name, value]) => [name, { colors: value.colors, sha256: value.sha256 }]));
}

async function main() {
    const settings = options();
    assert.equal((await fs.stat(settings.html)).isFile(), true);
    if (!settings.directOnly) {
        assert.equal((await fs.stat(settings['data-dir'])).isDirectory(), true);
    }
    await fs.mkdir(settings.output, { recursive: true });
    const report = { html: settings.html, browser: settings.browser, dataFolderLoads: 0,
        errors: [], requests: [], console: [] };
    const browserType = settings.browser === 'firefox' ? firefox : chromium;
    const browser = await browserType.launch({
        headless: !settings.headed, executablePath: settings['browser-executable'],
    });
    const server = settings.serve ? await serveHtml(settings.html, { isolated: settings.isolated !== false }) : undefined;
    settings.url = server ? server.url : pathToFileURL(settings.html).href;
    report.transport = settings.serve ? 'http-loopback' : 'file';
    const context = await browser.newContext({ acceptDownloads: true, offline: !settings.serve,
        viewport: { width: 1100, height: 1000 } });
    const page = await context.newPage();
    page.setDefaultTimeout(READY_TIMEOUT_MS);
    await page.addInitScript(() => {
        // Exercise the read-only folder-picker fallback independently of browser support.
        window.showDirectoryPicker = undefined;
        window.testAnimationFrames = 0;
        function heartbeat() {
            window.testAnimationFrames++;
            requestAnimationFrame(heartbeat);
        }
        requestAnimationFrame(heartbeat);
    });
    context.on('request', request => {
        const url = request.url();
        report.requests.push(url);
        const ownNavigation = request.isNavigationRequest() &&
            request.frame().parentFrame() === null && url === settings.url;
        if (!ownNavigation && !url.startsWith('data:') && !url.startsWith('blob:')) {
            report.errors.push('External request: ' + url);
        }
    });
    page.on('pageerror', error => report.errors.push(error.stack || String(error)));
    page.on('console', message => {
        report.console.push({ type: message.type(), text: message.text() });
        if (message.type() === 'error') {
            report.errors.push(message.text());
        }
    });
    try {
        if (!settings.directOnly) {
            await loadOwnData(page, settings, report);
            if (settings.idleOnly) {
                await exerciseIdleDemo(page, settings, report);
            } else {
                await exerciseSaves(page, settings, report);
                if (!settings.savesOnly) {
                    await exerciseGame(page, settings, report);
                }
            }
        }
        await exerciseExitFeedback(context, settings, report);
        await exerciseDirectSaving(context, settings, report);
        await exerciseReadOnlyScreenshot(context, settings, report);
        await exerciseReadOnlyAnimationImports(context, settings, report);
        await exerciseRememberedFolders(context, settings, report);
        assert.deepEqual(report.errors, [], 'Browser errors or external resource requests occurred');
        report.passed = true;
        const passedChecks = settings.directOnly ? 'exit feedback, resource imports, direct saves, screenshot saves/downloads, remembered folders, permission retry and recovery' :
            (settings.serve ? 'HTTP loopback startup' : 'file:// offline startup') +
            ', exit feedback, supplied data, resource imports, remembered folders and direct-write recovery' +
            (settings.idleOnly ? ', idle demo playback' : settings.savesOnly ? ', saves' : ', saves, keyboard and driving');
        console.log('PASS: ' + passedChecks);
    } catch (error) {
        report.passed = false;
        report.failure = error.stack || String(error);
        await page.screenshot({ path: path.join(settings.output, 'failure.png'), fullPage: true }).catch(() => {});
        throw error;
    } finally {
        if (page.url() !== 'about:blank') {
            report.status = await page.locator('#status').innerText().catch(() => 'unavailable');
            report.gameLog = await page.locator('#log').textContent().catch(() => 'unavailable');
        }
        await fs.writeFile(path.join(settings.output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
        await browser.close();
        if (server) await server.close();
        console.log('Diagnostics: ' + settings.output);
    }
}

main().catch(error => {
    console.error(error.stack || error);
    process.exitCode = 1;
});
