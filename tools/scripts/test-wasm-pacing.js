#!/usr/bin/env node
'use strict';

// Uses the same Playwright dependency as test-wasm.js, without original game data:
//   NODE_PATH=out/wasm-test-tools/node_modules node tools/scripts/test-wasm-pacing.js \
//       --sdl-include out/sdl3-wasm/_deps/sdl3-src/include
// Activate the Emscripten SDK first, or supply --emcc /path/to/emcc.

const assert = require('node:assert/strict');
const { spawnSync } = require('node:child_process');
const fs = require('node:fs/promises');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const { setTimeout: pause } = require('node:timers/promises');
const { chromium, firefox } = require('playwright');
const { serveHtml } = require('./browser-test-server.js');

const ROOT = path.resolve(__dirname, '../..');
const TIMEOUT_MS = 30000;
const RESULT_POLL_MS = 50;
const ASYNCIFY_STACK_BYTES = 65536;

function options() {
    const result = {
        emcc: 'emcc', browser: 'chromium',
        output: path.join(ROOT, 'out/wasm-pacing-test'),
        'sdl-include': path.join(ROOT, 'out/sdl3-wasm/_deps/sdl3-src/include'),
    };
    const args = process.argv.slice(2);
    for (let index = 0; index < args.length; index++) {
        const name = args[index].replace(/^--/, '');
        if (name === 'help') {
            console.log('Usage: node tools/scripts/test-wasm-pacing.js ' +
                '[--emcc COMMAND] [--sdl-include DIR] [--output DIR] ' +
                '[--browser chromium|firefox] [--browser-executable FILE] [--serve]');
            process.exit(0);
        }
        if (name === 'serve') {
            result.serve = true;
            continue;
        }
        assert.ok(['emcc', 'sdl-include', 'output', 'browser', 'browser-executable'].includes(name),
            'Unknown option: ' + args[index]);
        assert.ok(args[index + 1], 'Missing value for ' + args[index]);
        result[name] = args[++index];
    }
    assert.ok(['chromium', 'firefox'].includes(result.browser), 'Unsupported browser');
    result.output = path.resolve(result.output);
    return result;
}

async function exercise(browser, url, hidden) {
    const page = await browser.newPage();
    const errors = [];
    page.on('pageerror', error => errors.push(String(error)));
    await page.addInitScript(hiddenDocument => {
        const nativeTimeout = window.setTimeout.bind(window);
        const NativeMessageChannel = window.MessageChannel;
        let task = 'startup';
        let observation;
        globalThis.resetPacingObservation = () => {
            observation = globalThis.pacingObservation = {
                channels: 0, resumes: 0, timers: 0, nestedTimers: 0,
                unfinishedTimers: 0, maxUnfinishedTimers: 0, finished: false,
            };
        };
        window.setTimeout = (callback, delay, ...args) => {
            const measured = Boolean(observation && !observation.finished);
            if (measured) {
                observation.timers++;
                observation.nestedTimers += task === 'timer' ? 1 : 0;
                observation.unfinishedTimers++;
                observation.maxUnfinishedTimers = Math.max(
                    observation.maxUnfinishedTimers, observation.unfinishedTimers);
            }
            return nativeTimeout(() => {
                task = 'timer';
                if (measured) {
                    observation.unfinishedTimers--;
                }
                callback(...args);
            }, delay);
        };
        window.MessageChannel = class extends NativeMessageChannel {
            constructor() {
                super();
                const measured = Boolean(observation && !observation.finished);
                if (measured) {
                    observation.channels++;
                }
                // Register before the production onmessage listener. Keep the task
                // origin through all Promise/Asyncify microtasks that follow it.
                this.port1.addEventListener('message', () => {
                    task = 'message';
                    if (measured) {
                        observation.resumes++;
                    }
                });
            }
        };
        if (hiddenDocument) {
            Object.defineProperty(document, 'hidden', { get: () => true });
            Object.defineProperty(document, 'visibilityState', { get: () => 'hidden' });
        }
        // A background tab need not receive animation callbacks. Suppressing
        // them verifies that both delay paths finish without display refresh.
        window.requestAnimationFrame = () => 0;
    }, hidden);
    try {
        await page.goto(url, { timeout: TIMEOUT_MS });
        const started = Date.now();
        let report;
        do {
            // Poll from Node so the observer sees only the fixture's browser timers.
            report = await page.evaluate(() => globalThis.pacingObservation);
            assert.deepEqual(errors, [], 'WebAssembly pacing fixture raised browser errors');
            assert.ok(Date.now() - started < TIMEOUT_MS, 'WebAssembly pacing fixture timed out');
            if (!report?.finished) {
                await pause(RESULT_POLL_MS);
            }
        } while (!report?.finished);
        assert.deepEqual(errors, [], 'WebAssembly pacing fixture raised browser errors');
        assert.equal(report.channels, 1, 'Sequential waits must reuse one message channel');
        assert.equal(report.resumes, report.expectedResumes,
            'Input polling must not stack another yield after an explicit delay');
        assert.equal(report.timers, report.expectedTimers,
            'Only nonzero waits should schedule a timer');
        assert.equal(report.nestedTimers, 0,
            'Resuming from a timer callback would trigger the browser nesting clamp');
        assert.equal(report.unfinishedTimers, 0, 'No timers should remain after the last wait');
        assert.equal(report.maxUnfinishedTimers, 1, 'Each wait should have one pending timer');
        return { hidden, ...report };
    } finally {
        await page.close();
    }
}

async function main() {
    const settings = options();
    await fs.mkdir(settings.output, { recursive: true });
    const fixture = path.join(settings.output, 'test-wasm-pacing.html');
    const build = spawnSync(settings.emcc, [
        '-O2', '-Wno-pointer-sign', '-DRESTUNTS_SDL3', '-I' + settings['sdl-include'],
        '-sASYNCIFY', '-sASYNCIFY_STACK_SIZE=' + ASYNCIFY_STACK_BYTES,
        '-sENVIRONMENT=web', '-sSINGLE_FILE=1', '-sASSERTIONS=1',
        path.join(ROOT, 'src/restunts/tests/test-wasm-pacing.c'), '-o', fixture,
    ], { cwd: ROOT, encoding: 'utf8' });
    assert.ifError(build.error);
    assert.equal(build.status, 0, build.stdout + build.stderr);
    const browserType = settings.browser === 'firefox' ? firefox : chromium;
    const browser = await browserType.launch({
        headless: true, executablePath: settings['browser-executable'],
    });
    const server = settings.serve ? await serveHtml(fixture) : undefined;
    try {
        const url = server ? server.url : pathToFileURL(fixture).href;
        const reports = [];
        for (const hidden of [false, true]) {
            reports.push(await exercise(browser, url, hidden));
        }
        await fs.writeFile(path.join(settings.output, 'results.json'),
            JSON.stringify(reports, null, 2) + '\n');
        console.log('WebAssembly pacing: visible and hidden-document regressions passed (' +
            (settings.serve ? 'HTTP loopback' : 'file://') + ')');
    } finally {
        await browser.close();
        if (server) await server.close();
    }
}

main().catch(error => {
    console.error(error);
    process.exitCode = 1;
});
