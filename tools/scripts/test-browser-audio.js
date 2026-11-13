#!/usr/bin/env node
'use strict';

// Exercise the independent browser audio transport without game data or emcc.
// Playwright setup matches test-wasm-shell.js; CHROMIUM_EXECUTABLE_PATH can
// select an existing Chromium installation.
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const http = require('node:http');
const os = require('node:os');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const { execFile } = require('node:child_process');
const { promisify } = require('node:util');
const { chromium } = require('playwright');

const ROOT = path.resolve(__dirname, '../..');
const SOURCE = path.join(ROOT, 'src/restunts/platform/sdl3');
const RUN = promisify(execFile);
const MOCK_RUNTIME = `
// Markup in a runtime string must stay inside the final script: </script><
async function createRestuntsAudioRuntime() {
    const memory = new ArrayBuffer(4 * 1024 * 1024);
    const heap = new Uint8Array(memory);
    const pcm = new Int16Array(memory);
    let next = 8192;
    let ticks = 0;
    let phase = 0;
    let gain = 5000;
    const files = [];
    const finishes = [];
    const runtime = {
        HEAPU8: heap,
        HEAP16: pcm,
        _malloc(bytes) { const pointer = next; next += bytes + 8; return pointer; },
        _free() {},
        _audio_response_free() {},
        _audio_pop_finish() { return finishes.shift() || 0; },
        UTF8ToString(pointer) {
            let end = pointer;
            while (heap[end]) end++;
            return new TextDecoder().decode(heap.subarray(pointer, end));
        },
        _audio_command(pointer, length) {
            const command = JSON.parse(new TextDecoder().decode(heap.subarray(pointer, pointer + length)));
            let result = { args: command.args, text: command.text };
            if (command.op === 'stats') result = { ticks, files };
            if (command.op === 37) files.push({ path: command.text,
                bytes: Array.from(heap.subarray(command.resources[0].pointer,
                    command.resources[0].pointer + command.resources[0].length)) });
            if (command.op === 'resource') result = command.resources.map(resource => ({
                id: resource.id, length: resource.length,
                sum: heap.subarray(resource.pointer, resource.pointer + resource.length)
                    .reduce((sum, value) => sum + value, 0)
            }));
            if (command.op === 'gain') gain = command.args;
            if (command.op === 'finish') finishes.push(command.args[0]);
            if (command.op === 'fatal') {
                setTimeout(() => self.postMessage({ type: 'error', error: 'Expected fatal error.' }), 0);
            }
            const response = JSON.stringify({ ok: command.op !== 'error', result,
                error: command.op === 'error' ? 'Expected command error.' : undefined,
                legacy_hihat: 12345, impact: 1, flush: command.op === 'gain' });
            const bytes = new TextEncoder().encode(response);
            const output = runtime._malloc(bytes.length + 1);
            heap.set(bytes, output);
            heap[output + bytes.length] = 0;
            return output;
        },
        _audio_render_tick() {
            ticks++;
            for (let frame = 0; frame < 441; frame++) {
                const value = Math.round(Math.sin(phase++ * 0.04) * gain);
                pcm[128 + frame * 2] = value;
                pcm[129 + frame * 2] = value;
            }
            return 256;
        }
    };
    return runtime;
}`;

async function fixture(directory) {
    const runtime = path.join(directory, 'mock-audio-runtime.js');
    const prelude = path.join(directory, 'audio-sources.js');
    await fs.writeFile(runtime, MOCK_RUNTIME);
    await RUN(process.env.PYTHON || 'python3', [
        path.join(ROOT, 'tools/scripts/generate-audio-sources.py'),
        '--runtime', runtime,
        '--worker', path.join(SOURCE, 'browser_audio_worker.js'),
        '--worklet', path.join(SOURCE, 'browser_audio_worklet.js'),
        '--output', prelude
    ]);
    const embedded = await fs.readFile(prelude, 'utf8');
    assert.ok(!embedded.includes('</script>'), 'the embedded runtime must be safe in inline HTML');
    const controller = await fs.readFile(path.join(SOURCE, 'browser_audio.js'), 'utf8');
    return `<!doctype html><button id="start">Start</button><script>
        var Module = { printErr: message => { (window.audioErrors ||= []).push(message); } };
        const files = new Map([
            ['/stunts/mT32.pLb', [1, 2]], ['/assets/music/TITL.ogg', [3, 4]],
            ['/assets/music/readme.txt', [5]], ['/selected/MT-é/CONTROL.ROM', [6]],
            ['/selected/a/PCM.ROM', [7]], ['/selected/b/PCM.ROM', [8]]
        ]);
        Module.FS = {
            cwd: () => '/stunts',
            readdir(directory) {
                const prefix = directory + '/';
                const names = [...new Set([...files.keys()].filter(path => path.startsWith(prefix))
                    .map(path => path.slice(prefix.length).split('/')[0]))];
                if (!names.length) throw { errno: 44 };
                return ['.', '..', ...names];
            },
            stat: path => ({ mode: files.has(path) ? 1 : 2 }),
            isFile: mode => mode === 1,
            isDir: mode => mode === 2,
            readFile: path => Uint8Array.from(files.get(path))
        };
        ${embedded}
        const NativeAudioContext = AudioContext;
        window.audioContexts = [];
        window.unhandled = [];
        addEventListener('unhandledrejection', event => unhandled.push(String(event.reason)));
        window.AudioContext = class extends NativeAudioContext {
            constructor(options) {
                super({ ...options, sampleRate: Number(new URL(location).searchParams.get('rate')) || 44100 });
                audioContexts.push(this);
                const addModule = this.audioWorklet.addModule.bind(this.audioWorklet);
                this.audioWorklet.addModule = async source => {
                    const result = await addModule(source);
                    if (window.delayAudioModule) await new Promise(resolve => setTimeout(resolve, 100));
                    return result;
                };
                if (window.rejectAudioResume) this.resume = () => Promise.reject(new Error('Expected resume error.'));
            }
        };
        const NativeAudioWorkletNode = AudioWorkletNode;
        window.AudioWorkletNode = class extends NativeAudioWorkletNode {
            constructor(...args) { super(...args); window.playbackNode = this; }
        };
        ${controller}
        window.bridge = Module.restuntsBrowserAudio;
        bridge.onError = error => { (window.audioErrors ||= []).push(error.message); };
        document.getElementById('start').onclick = async () => {
            try {
                await bridge.start();
                window.started = true;
                window.analyser = playbackNode.context.createAnalyser();
                analyser.fftSize = 2048;
                playbackNode.connect(analyser);
            } catch (error) {
                window.startError = error.message;
            }
        };
        window.peak = () => {
            const samples = new Float32Array(analyser.fftSize);
            analyser.getFloatTimeDomainData(samples);
            return Math.max(...samples.map(Math.abs));
        };
        window.playbackStats = () => new Promise(resolve => {
            const request = performance.now();
            const listener = event => {
                if (event.data.type !== 'stats' || event.data.request !== request) return;
                playbackNode.port.removeEventListener('message', listener);
                resolve(event.data);
            };
            playbackNode.port.addEventListener('message', listener);
            playbackNode.port.postMessage({ type: 'stats', request });
        });
    </script>`;
}

async function exercisePlayback(browser, origin, rate) {
    const page = await browser.newPage();
    const errors = [];
    page.on('pageerror', error => errors.push(String(error)));
    try {
        const url = new URL(origin);
        url.searchParams.set('rate', rate);
        await page.goto(url.href);
        await page.click('#start');
        await page.waitForFunction(() => window.started || window.startError);
        assert.equal(await page.evaluate(() => window.startError), undefined);
        await page.evaluate(() => bridge.command('finish', [0xff123405]));
        await page.waitForFunction(() => Module.restuntsAudioFinished.includes(0xff123405));
        await page.evaluate(() => bridge.prepareFiles('/selected'));
        await page.evaluate(() => bridge.prepareFiles('/selected'));
        assert.deepEqual(await page.evaluate(async () => (await bridge.command('stats')).files), [
            { path: '/MT32.PLB', bytes: [1, 2] },
            { path: '/assets/music/TITL.ogg', bytes: [3, 4] },
            { path: '/roms/MT-é/CONTROL.ROM', bytes: [6] },
            { path: '/roms/a/PCM.ROM', bytes: [7] },
            { path: '/roms/b/PCM.ROM', bytes: [8] }
        ]);
        assert.deepEqual(await page.evaluate(() => bridge.command({
            op: 'echo', args: [1, -2], text: 'bank/é'
        })), { args: [1, -2], text: 'bank/é' });
        const resource = await page.evaluate(async () => {
            const bytes = new Uint8Array([10, 20, 30]);
            const result = await bridge.command('resource', null, [{ id: 7, bytes }]);
            return { result, unchanged: bytes[2], metadata: Module.lastAudioResponse.legacy_hihat };
        });
        assert.deepEqual(resource, { result: [{ id: 7, length: 3, sum: 60 }], unchanged: 30, metadata: 12345 });
        assert.equal(await page.evaluate(async () => {
            try { await bridge.command('error'); return false; }
            catch (error) { return error.message; }
        }), 'Expected command error.');
        await page.waitForFunction(() => bridge.state.frames >= 44100);
        const before = await page.evaluate(() => bridge.command('stats'));
        const playbackBefore = await page.evaluate(() => playbackStats());
        await page.evaluate(() => {
            const end = performance.now() + 1500;
            while (performance.now() < end) {}
        });
        const after = await page.evaluate(() => bridge.command('stats'));
        const playbackAfter = await page.evaluate(() => playbackStats());
        assert.ok(after.ticks >= before.ticks + 100, 'synthesis must continue through a main-thread stall');
        const output = await page.evaluate(() => ({ ...bridge.state, peak: peak(),
            contextRate: playbackNode.context.sampleRate, errors: window.audioErrors || [] }));
        assert.equal(playbackAfter.underruns, playbackBefore.underruns);
        assert.ok(output.peak > 0.14);
        assert.equal(output.contextRate, rate);
        assert.deepEqual(output.errors, []);
        await page.evaluate(() => bridge.command('gain', 0));
        await page.waitForFunction(() => peak() === 0);
        await page.evaluate(() => bridge.shutdown());
        assert.equal(await page.evaluate(() => bridge.state.running), false);
        assert.deepEqual(errors, []);
        console.log(`PASS: independent ${rate} Hz playback, source ownership, commands, metadata, flush and main-thread stall.`);
    } finally {
        await page.close();
    }
}

async function exerciseLifecycle(browser, origin) {
    const page = await browser.newPage();
    const errors = [];
    page.on('pageerror', error => errors.push(String(error)));
    try {
        await page.goto(origin);
        await page.click('#start');
        await page.waitForFunction(() => window.started);
        await page.evaluate(() => bridge.shutdown());
        const cancellation = await page.evaluate(async () => {
            window.delayAudioModule = true;
            const previous = bridge.start().then(() => 'Unexpected startup.', error => error.message);
            await bridge.shutdown();
            window.delayAudioModule = false;
            await bridge.start();
            return { previous: await previous, result: await bridge.command('stats') };
        });
        assert.equal(cancellation.previous, 'Game audio closed.');
        assert.ok(cancellation.result.ticks > 0);
        await page.evaluate(() => bridge.command('fatal'));
        await page.waitForFunction(() => (window.audioErrors || []).includes('Expected fatal error.'));
        assert.equal(await page.evaluate(async () => Promise.race([
            bridge.command('stats').then(() => 'Unexpected success.', error => error.message),
            new Promise(resolve => setTimeout(() => resolve('Command hung.'), 500))
        ])), 'Expected fatal error.');
        await page.evaluate(() => bridge.shutdown());
        await page.waitForFunction(() => audioContexts.every(context => context.state === 'closed'));
        const resume = await page.evaluate(async () => {
            window.rejectAudioResume = true;
            try { await bridge.start(); return 'Unexpected startup.'; }
            catch (error) { return error.message; }
            finally { await bridge.shutdown(); }
        });
        assert.equal(resume, 'Expected resume error.');
        assert.deepEqual(await page.evaluate(() => unhandled), []);
        assert.deepEqual(errors, []);
        console.log('PASS: cancelled startup, restart, fatal-error rejection and cleanup, and handled resume failure.');
    } finally {
        await page.close();
    }
}

(async () => {
    const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'restunts-audio-browser-'));
    let server;
    let browser;
    try {
        const html = await fixture(directory);
        const offline = path.join(directory, 'audio.html');
        await fs.writeFile(offline, html);
        server = http.createServer((request, response) => {
            response.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
            response.end(html);
        });
        await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
        browser = await chromium.launch({ headless: true,
            executablePath: process.env.CHROMIUM_EXECUTABLE_PATH || undefined });
        const origin = 'http://127.0.0.1:' + server.address().port;
        for (const rate of [44100, 48000]) await exercisePlayback(browser, origin, rate);
        await exerciseLifecycle(browser, origin);
        try {
            await exercisePlayback(browser, pathToFileURL(offline).href, 44100);
        } catch (error) {
            // Managed development browsers can disallow all local navigation.
            // Unrestricted browsers must pass the complete offline playback test.
            if (!String(error).includes('ERR_BLOCKED_BY_ADMINISTRATOR')) throw error;
            console.log('SKIP: offline navigation is blocked by this browser\'s administrator policy.');
        }
    } finally {
        if (browser) await browser.close();
        if (server) await new Promise(resolve => server.close(resolve));
        await fs.rm(directory, { recursive: true, force: true });
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
