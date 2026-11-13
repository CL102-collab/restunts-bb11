#!/usr/bin/env node
'use strict';

// Exercise the real isolated C runtime; transport mocks are tested separately.
// Usage: node test-browser-audio-runtime.js [runtime.js] [original-game-folder]
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const http = require('node:http');
const path = require('node:path');
const { chromium } = require('playwright');

const ROOT = path.resolve(__dirname, '../..');

async function workerTests(input) {
    const checks = [];
    const logs = [];
    function check(value, message) {
        if (!value) throw new Error(message);
        checks.push(message);
    }
    importScripts(input.runtimeURL);
    const runtime = await createRestuntsAudioRuntime({ noInitialRun: true,
        print: message => logs.push(String(message)),
        printErr: message => logs.push(String(message)) });
    const encoder = new TextEncoder();
    function raw(text) {
        const bytes = encoder.encode(text);
        const pointer = runtime._malloc(bytes.length + 1);
        let response = 0;
        try {
            runtime.HEAPU8.set(bytes, pointer);
            runtime.HEAPU8[pointer + bytes.length] = 0;
            response = runtime._audio_command(pointer, bytes.length);
            check(response !== 0, 'C command returns an explicit response');
            return JSON.parse(runtime.UTF8ToString(response));
        } finally {
            if (response) runtime._audio_response_free(response);
            runtime._free(pointer);
        }
    }
    function command(op, args = [], resources = [], text = '') {
        const allocations = [];
        try {
            const descriptors = resources.map(resource => {
                const bytes = new Uint8Array(resource.bytes || []);
                const pointer = runtime._malloc(Math.max(bytes.length, 1));
                if (!pointer) throw new Error('Cannot allocate runtime test payload');
                allocations.push({ pointer, bytes: bytes.length });
                runtime.HEAPU8.set(bytes, pointer);
                return { id: resource.id, slot: resource.slot || 0,
                    pointer, length: bytes.length };
            });
            return raw(JSON.stringify({ op, args, resources: descriptors, text }));
        } finally {
            // The C executor must have cloned banks before acknowledging them.
            for (const allocation of allocations) {
                runtime.HEAPU8.fill(0xa5, allocation.pointer,
                    allocation.pointer + allocation.bytes);
                runtime._free(allocation.pointer);
            }
        }
    }
    function accepted(response, name) {
        check(response.ok === true, name + ': ' + (response.error || 'accepted'));
        return response;
    }
    function render(ticks) {
        let peak = 0;
        let energy = 0;
        let checksum = 0;
        let stereoMismatch = 0;
        for (let tick = 0; tick < ticks; tick++) {
            const pointer = runtime._audio_render_tick();
            check(pointer !== 0, 'configured runtime returns a PCM tick');
            const pcm = runtime.HEAP16.subarray(pointer >> 1, (pointer >> 1) + 441 * 2);
            for (let frame = 0; frame < 441; frame++) {
                const left = pcm[frame * 2];
                const right = pcm[frame * 2 + 1];
                peak = Math.max(peak, Math.abs(left), Math.abs(right));
                energy += left * left + right * right;
                checksum = (Math.imul(checksum, 33) + left + right) >>> 0;
                stereoMismatch += left !== right;
            }
        }
        return { peak, energy, checksum, stereoMismatch };
    }
    function prepare(song, voices, name) {
        const response = accepted(command(2, [], [
            { id: 1001, slot: 0, bytes: song },
            { id: 1002, slot: 1, bytes: voices }
        ], name), 'borrowed song/instrument import');
        check(response.pointer_result > 0 && response.result === response.pointer_result,
            'resource preparation returns its separate opaque far pointer');
        accepted(command(3, [response.pointer_result | 0]), 'opaque resource finalization');
    }

    check(runtime._audio_render_tick() === 0, 'unconfigured runtime returns silence');
    const invalid = ['', '{', '{}', '[1]', '{"op":1,}', '{"op":-1}', '{"op":999}',
        '{"op":1,"args":[2147483648]}', '{"op":1,"args":[-2147483649]}',
        '{"op":1,"text":"\\uD800"}',
        JSON.stringify({ op: 1, args: Array(13).fill(0) }),
        JSON.stringify({ op: 1, resources: Array(4).fill({ id: 0, pointer: 0, length: 0 }) }),
        JSON.stringify({ op: 37, text: '/outside.bin', resources: [
            { id: 1, pointer: runtime.HEAPU8.length - 1, length: 2 }
        ] })];
    for (const value of invalid) check(raw(value).ok === false, 'malformed/bounded input is rejected');
    const outside = runtime._audio_command(runtime.HEAPU8.length - 1, 2);
    check(JSON.parse(runtime.UTF8ToString(outside)).ok === false, 'JSON heap extent is checked');
    runtime._audio_response_free(outside);

    accepted(command(37, [], [{ id: 0, slot: 0, bytes: [11, 22, 33] }],
        '/assets/music/nested/fixture.bin'), 'FILE creates directories and copies bytes');
    check(Array.from(runtime.FS.readFile('/assets/music/nested/fixture.bin')).join(',') === '11,22,33',
        'FILE bytes survive temporary payload overwrite/free');
    accepted(raw('{"op":37,"text":"/assets/music/nested/\\uD83D\\uDE00.bin"}'),
        'FILE decodes paired Unicode escapes');
    check(runtime.FS.readFile('/assets/music/nested/😀.bin').length === 0,
        'Unicode file names reach the independent filesystem');
    check(command(37, [], [], '../outside.bin').ok === false, 'FILE requires an absolute path');
    accepted(command(37, [], [{ id: 0, slot: 0, bytes: input.ogg }], '/assets/music/titl.ogg'),
        'FILE installs the tracked OGG fixture');

    accepted(command(1, [2, 0]), 'PC Speaker configure');
    prepare(input.titl, input.pc, 'titl');
    check(runtime._audio_pop_finish() === 0, 'completion export starts empty');
    accepted(command(38, [16, 1, 1]), 'finish registration carries values instead of game pointers');
    const pc = render(100);
    check(pc.peak > 0 && pc.energy > 0, 'copied PC banks produce actual PCM');
    check(pc.stereoMismatch === 0, 'mono PC output expands to stereo');

    accepted(command(30, [1]), 'enable OGG replacement');
    render(10);
    const ogg = render(100);
    check(ogg.peak > 0 && ogg.stereoMismatch === 0, 'OGG source resampling and bounded refill work');
    accepted(command(34, [0]), 'mute OGG stream gain');
    render(10);
    check(render(10).peak === 0, 'zero stream gain produces exact silence');
    accepted(command(34, [127]), 'restore OGG stream gain');
    check(render(10).peak > 0, 'restoring stream gain restores PCM');

    // Same source IDs now name different bytes. A configure operation must
    // quiesce and discard already-mapped private banks before importing again.
    accepted(command(1, [2, 0]), 'reconfigure PC without an explicit shutdown');
    prepare(input.slct, input.pc, 'slct');
    const changed = render(100);
    check(changed.peak > 0 && changed.checksum !== pc.checksum,
        'reconfiguration imports changed bytes under reused source IDs');
    accepted(command(6, [], [{ id: 1001, slot: 0, bytes: [] }]), 'empty resource descriptor releases a key');

    accepted(command(1, [0, 0]), 'reconfigure Ad Lib with changed instrument bytes');
    prepare(input.titl, input.ad, 'titl');
    check(render(100).peak > 0, 'Ad Lib emulator produces actual PCM');
    accepted(command(36), 'shutdown');
    check(runtime._audio_render_tick() === 0, 'shutdown stops PCM ticks');
    accepted(command(36), 'repeated shutdown is safe');

    check(command(1, [1, 0]).ok === false, 'MT-32 without user ROMs returns a command error');
    accepted(command(1, [2, 0]), 'PC Speaker recovers after failed MT-32 configure');
    prepare(input.titl, input.pc, 'titl');
    check(render(20).peak > 0, 'recovered runtime produces PCM');
    accepted(command(36), 'final shutdown');
    return { checks: checks.length, pc, ogg, changed, logs };
}

(async () => {
    const runtimePath = path.resolve(process.argv[2] || path.join(ROOT, 'out/mt32-wasm/restunts_audio_runtime.js'));
    const game = path.resolve(process.argv[3] || path.join(ROOT, 'stunts'));
    const source = await fs.readFile(runtimePath);
    const read = async name => Array.from(await fs.readFile(path.join(game, name)));
    const resources = { titl: await read('SKIDTITL.KMS'), slct: await read('SKIDSLCT.KMS'),
        pc: await read('PCSKIDMS.VCE'), ad: await read('ADSKIDMS.VCE'),
        ogg: Array.from(await fs.readFile(path.join(ROOT, 'src/restunts/tests/fixtures/music/tone-mono.ogg'))) };
    const server = http.createServer((request, response) => {
        response.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
        response.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
        response.setHeader('Content-Type', request.url === '/runtime.js' ? 'text/javascript' : 'text/html');
        response.end(request.url === '/runtime.js' ? source : '<!doctype html><title>Audio C runtime test</title>');
    });
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    let browser;
    try {
        browser = await chromium.launch({ headless: true,
            executablePath: process.env.CHROMIUM_EXECUTABLE_PATH || undefined });
        const page = await browser.newPage();
        page.setDefaultTimeout(120000);
        const origin = 'http://127.0.0.1:' + server.address().port;
        await page.goto(origin);
        const result = await page.evaluate(({ tests, input }) => new Promise((resolve, reject) => {
            const script = 'self.onmessage=async event=>{try{const result=await (' + tests +
                ')(event.data);self.postMessage({ok:true,result});}catch(error){self.postMessage({ok:false,error:String(error.stack||error)});}}';
            const url = URL.createObjectURL(new Blob([script], { type: 'text/javascript' }));
            const worker = new Worker(url);
            const timer = setTimeout(() => { worker.terminate(); reject(new Error('C audio runtime timed out')); }, 120000);
            worker.onerror = event => { clearTimeout(timer); worker.terminate(); reject(new Error(event.message)); };
            worker.onmessage = event => {
                clearTimeout(timer); worker.terminate(); URL.revokeObjectURL(url);
                if (event.data.ok) resolve(event.data.result);
                else reject(new Error(event.data.error));
            };
            worker.postMessage(input);
        }), { tests: workerTests.toString(), input: { ...resources, runtimeURL: origin + '/runtime.js' } });
        assert.ok(result.checks > 100);
        console.log('PASS actual browser audio C runtime:', JSON.stringify(result));
    } finally {
        if (browser) await browser.close();
        await new Promise(resolve => server.close(resolve));
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
