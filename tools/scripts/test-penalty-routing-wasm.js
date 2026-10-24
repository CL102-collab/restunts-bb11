#!/usr/bin/env node
'use strict';

// Execute the actual C penalty regression as WebAssembly in Node and Chromium.
// Activate the pinned Emscripten SDK, install Playwright outside tracked files,
// then run:
//   node tools/scripts/test-penalty-routing-wasm.js --emcc /path/to/emcc
// CHROMIUM_EXECUTABLE_PATH selects an existing browser when needed.
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const http = require('node:http');
const { chromium } = require('playwright');

const ROOT = path.resolve(__dirname, '../..');
const MARKER = 'Dual-Way Switching penalty routing tests passed';

function options() {
    const result = {
        emcc: process.env.EMCC || 'emcc',
        output: path.join(ROOT, 'out/penalty-routing-wasm')
    };
    const args = process.argv.slice(2);
    for (let index = 0; index < args.length; index++) {
        const argument = args[index];
        if (argument === '--help') {
            console.log('Usage: node tools/scripts/test-penalty-routing-wasm.js ' +
                '[--emcc /path/to/emcc] [--output DIR]');
            process.exit(0);
        }
        assert.ok(['--emcc', '--output'].includes(argument), 'Unknown argument: ' + argument);
        assert.ok(args[index + 1], 'Missing value for ' + argument);
        const value = args[++index];
        result[argument.slice(2)] = argument === '--output' ? path.resolve(value) : value;
    }
    return result;
}

function command(executable, arguments_) {
    const result = spawnSync(executable, arguments_, {
        cwd: ROOT, encoding: 'utf8', timeout: 180000, maxBuffer: 8 * 1024 * 1024
    });
    if (result.error) throw result.error;
    assert.equal(result.status, 0,
        executable + ' failed:\n' + result.stdout + result.stderr);
    return result.stdout + result.stderr;
}

async function main() {
    const settings = options();
    await fs.mkdir(settings.output, { recursive: true });
    const javascript = path.join(settings.output, 'penalty-routing.js');
    const compiler = command(settings.emcc, ['--version']);
    const compileLog = command(settings.emcc, [
        '-std=c99', '-O2', '-UNDEBUG', '-ffunction-sections', '-fdata-sections',
        '-Wno-sign-compare',
        'src/restunts/tests/test-penalty-routing.c',
        'src/restunts/c/race_route.c', 'src/restunts/c/strlib.c',
        '-Wl,--gc-sections', '-sASSERTIONS=1', '-sEXIT_RUNTIME=1',
        '-sSINGLE_FILE=1', '-sENVIRONMENT=web,node', '-o', javascript
    ]);
    await fs.writeFile(path.join(settings.output, 'compile.log'), compileLog);
    const nodeLog = command(process.execPath, [javascript]);
    await fs.writeFile(path.join(settings.output, 'node.log'), nodeLog);
    assert.ok(nodeLog.includes(MARKER), 'Node must finish all C regression assertions');

    const html = path.join(settings.output, 'penalty-routing.html');
    await fs.writeFile(html, '<!doctype html><meta charset="utf-8"><title>Penalty routing</title>' +
        '<script>window.penaltyOutput=[];window.penaltyErrors=[];' +
        'var Module={print:value=>penaltyOutput.push(String(value)),' +
        'printErr:value=>penaltyErrors.push(String(value)),' +
        'onExit:code=>window.penaltyExit=code,' +
        'onAbort:reason=>window.penaltyAbort=String(reason)};</script>' +
        '<script src="penalty-routing.js"></script>');
    const server = http.createServer(async (request, response) => {
        const filename = request.url === '/penalty-routing.js' ? javascript : html;
        response.setHeader('Content-Type', filename === javascript ? 'text/javascript' : 'text/html');
        response.end(await fs.readFile(filename));
    });
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    let browser;
    try {
        browser = await chromium.launch({
            headless: true,
            ...(process.env.CHROMIUM_EXECUTABLE_PATH ?
                { executablePath: process.env.CHROMIUM_EXECUTABLE_PATH } : {})
        });
        const page = await browser.newPage();
        const errors = [];
        page.on('pageerror', error => errors.push(String(error)));
        await page.goto('http://127.0.0.1:' + server.address().port + '/');
        await page.waitForFunction(() => window.penaltyExit !== undefined ||
            window.penaltyAbort !== undefined, null, { timeout: 30000 });
        const report = await page.evaluate(() => ({
            exit: window.penaltyExit, abort: window.penaltyAbort,
            output: window.penaltyOutput, stderr: window.penaltyErrors
        }));
        await fs.writeFile(path.join(settings.output, 'browser.json'),
            JSON.stringify({ compiler, browser: browser.version(), ...report, errors }, null, 2) + '\n');
        assert.equal(report.abort, undefined, 'WebAssembly must not abort');
        assert.equal(report.exit, 0, 'WebAssembly C regression must exit successfully');
        assert.deepEqual(errors, [], 'Browser must report no uncaught errors');
        assert.deepEqual(report.stderr, [], 'WebAssembly must report no runtime errors');
        assert.ok(report.output.some(line => line.includes(MARKER)),
            'Chromium must finish all C regression assertions');
        console.log(nodeLog.trim());
        console.log('PASS: compiled penalty-routing WebAssembly assertions in Node and Chromium ' + browser.version());
    } finally {
        if (browser) await browser.close();
        await new Promise(resolve => server.close(resolve));
    }
}

main().catch(error => {
    console.error(error);
    process.exitCode = 1;
});
