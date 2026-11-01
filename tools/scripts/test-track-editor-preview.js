#!/usr/bin/env node
'use strict';
const assert = require('node:assert/strict');
const { Preview, MOVE_SPEED, RUN_MULTIPLIER, movement, look, viewProjection } = require('../../src/restunts/platform/wasm/track-editor-preview.js');

function test(name, callback) { callback(); console.log('PASS ' + name); }
function close(actual, expected, message) { assert(Math.abs(actual - expected) < 1e-6, message || `${actual} != ${expected}`); }
function camera() { return { position: [0, 0, 0], yaw: 0, pitch: 0 }; }
class Target {
    constructor() { this.listeners = new Map(); }
    addEventListener(type, callback) {
        if (!this.listeners.has(type)) this.listeners.set(type, new Set());
        this.listeners.get(type).add(callback);
    }
    removeEventListener(type, callback) { this.listeners.get(type)?.delete(callback); }
    emit(type, values = {}) {
        const event = { type, prevented: false, stopped: false, altKey: false, ...values,
            preventDefault() { this.prevented = true; }, stopPropagation() { this.stopped = true; } };
        for (const callback of this.listeners.get(type) || []) callback(event);
        return event;
    }
}
function fakeGL() {
    let id = 0;
    const calls = [], gl = { calls };
    ['VERTEX_SHADER', 'FRAGMENT_SHADER', 'COMPILE_STATUS', 'LINK_STATUS', 'DEPTH_TEST', 'LEQUAL', 'CULL_FACE',
        'POLYGON_OFFSET_FILL', 'ARRAY_BUFFER', 'STATIC_DRAW', 'TRIANGLES', 'LINES', 'FLOAT', 'COLOR_BUFFER_BIT', 'DEPTH_BUFFER_BIT']
        .forEach((name, index) => { gl[name] = index + 1; });
    for (const name of ['createShader', 'createProgram', 'createBuffer']) gl[name] = () => ({ id: ++id });
    for (const name of ['shaderSource', 'compileShader', 'attachShader', 'linkProgram', 'enable', 'depthFunc', 'disable',
        'polygonOffset', 'bindBuffer', 'bufferData', 'deleteBuffer', 'deleteProgram', 'deleteShader', 'viewport', 'clearColor',
        'clear', 'useProgram', 'uniformMatrix4fv', 'uniform3fv', 'uniform2f', 'enableVertexAttribArray', 'vertexAttribPointer', 'drawArrays'])
        gl[name] = (...args) => { calls.push([name, ...args]); };
    gl.NO_ERROR = 0; gl.OUT_OF_MEMORY = 1285; gl.getError = () => gl.NO_ERROR;
    gl.getShaderParameter = gl.getProgramParameter = () => true;
    gl.getAttribLocation = (_, name) => name === 'aPosition' ? 0 : 1;
    gl.getUniformLocation = (_, name) => name;
    return gl;
}
function fixture({ unavailable = false, noPointerLock = false } = {}) {
    const win = new Target(), doc = new Target(), canvas = new Target(), gl = fakeGL();
    let nextFrame = 0;
    const frames = new Map(), statuses = [];
    win.requestAnimationFrame = callback => { const id = ++nextFrame; frames.set(id, callback); return id; };
    win.cancelAnimationFrame = id => frames.delete(id);
    win.devicePixelRatio = 3;
    doc.defaultView = win; doc.hidden = false; doc.activeElement = null; doc.pointerLockElement = null;
    doc.exitPointerLock = () => { doc.pointerLockElement = null; doc.emit('pointerlockchange'); };
    canvas.ownerDocument = doc; canvas.width = 0; canvas.height = 0; canvas.clientWidth = 300; canvas.clientHeight = 200;
    canvas.getContext = () => unavailable ? null : gl;
    canvas.focus = () => { doc.activeElement = canvas; };
    if (!noPointerLock) canvas.requestPointerLock = () => { doc.pointerLockElement = canvas; doc.emit('pointerlockchange'); };
    const preview = new Preview(canvas, { onStatus: status => statuses.push(status) });
    const frame = time => {
        const callbacks = [...frames.values()]; frames.clear();
        for (const callback of callbacks) callback(time);
    };
    const key = (code, down = true, { caps = false, alt = false, ctrl = false, meta = false } = {}) => doc.emit(down ? 'keydown' : 'keyup',
        { code, altKey: alt, ctrlKey: ctrl, metaKey: meta, getModifierState: modifier => modifier === 'CapsLock' && caps });
    return { win, doc, canvas, gl, preview, frames, statuses, frame, key };
}
function scene() {
    return {
        positions: new Float32Array([0, 0, 0, 1, 0, 0, 0, 0, 1]),
        colors: new Float32Array([0, 1, 0, 0, 1, 0, 0, 1, 0]),
        linePositions: new Float32Array([0, 0, 0, 0, 1, 0]),
        lineColors: new Float32Array([1, 1, 1, 1, 1, 1]),
        bounds: { min: [0, 0, 0], max: [1, 1, 1] }, spawn: { position: [.5, .18, .5], yaw: 0 }
    };
}

test('WASD follows heading while Space and either Shift move in world elevation', () => {
    const pose = camera();
    movement(pose, new Set(['KeyW']), 1, false);
    close(pose.position[0], 0); close(pose.position[2], -MOVE_SPEED);
    pose.yaw = Math.PI / 2; pose.pitch = Math.PI / 4;
    movement(pose, new Set(['KeyW']), 1, false);
    close(pose.position[0], MOVE_SPEED); close(pose.position[1], 0);
    movement(pose, new Set(['Space']), 1, false);
    close(pose.position[1], MOVE_SPEED);
    movement(pose, new Set(['ShiftRight']), .25, false);
    movement(pose, new Set(['ShiftLeft']), .25, false);
    close(pose.position[1], MOVE_SPEED / 2);
});
test('diagonal flight is normalized and opposite keys cancel', () => {
    const pose = camera();
    movement(pose, new Set(['KeyW', 'KeyD', 'Space']), 1, false);
    close(Math.hypot(...pose.position), MOVE_SPEED);
    const before = pose.position.slice();
    movement(pose, new Set(['KeyW', 'KeyS', 'KeyA', 'KeyD', 'Space', 'ShiftLeft']), 1, true);
    assert.deepEqual(pose.position, before);
});
test('mouse look clamps pitch and the view matrix points along the same yaw', () => {
    const pose = camera();
    look(pose, 100, -100000);
    assert(pose.yaw > 0); assert(pose.pitch < Math.PI / 2); assert(pose.pitch > 1.5);
    look(pose, Infinity, 0); assert(Number.isFinite(pose.yaw));
    pose.pitch = 0; pose.yaw = Math.PI / 2;
    const matrix = viewProjection(pose, 1, 100);
    const ahead = [1, 0, 0, 1], clip = [0, 0, 0, 0];
    for (let row = 0; row < 4; row++) for (let column = 0; column < 4; column++) clip[row] += matrix[column * 4 + row] * ahead[column];
    close(clip[0], 0); close(clip[1], 0); assert(clip[3] > 0); assert(Math.abs(clip[2]) < clip[3]);
});
test('Alt and actual Caps Lock state give exactly 3x speed, never a stacked multiplier', () => {
    for (const modifiers of [{}, { alt: true }, { caps: true }, { alt: true, caps: true }]) {
        const f = fixture(); f.preview.setScene(scene()); f.preview.start();
        f.key('KeyW', true, modifiers); f.frame(0); f.frame(100);
        const multiplier = modifiers.alt || modifiers.caps ? RUN_MULTIPLIER : 1;
        close(.5 - f.preview.camera.position[2], MOVE_SPEED * .1 * multiplier);
        assert.equal(f.preview.status.speedMultiplier, multiplier);
        assert.equal(f.preview.status.running, !!modifiers.caps);
        f.key('KeyW', false, modifiers);
        f.preview.dispose();
    }
    const f = fixture(); f.preview.setScene(scene()); f.preview.start();
    f.key('CapsLock', true, { caps: true }); assert.equal(f.preview.status.running, true);
    f.key('CapsLock', true, { caps: false }); assert.equal(f.preview.status.running, false);
    f.key('AltLeft', true, { alt: true }); assert.equal(f.preview.status.speedMultiplier, 3);
    f.key('AltLeft', false); assert.equal(f.preview.status.speedMultiplier, 1);
    f.preview.dispose();
});
test('input is scoped to the focused preview and releases on blur, pointer unlock and stop', () => {
    const f = fixture(); f.preview.setScene(scene());
    assert.equal(f.key('Space').prevented, false);
    f.preview.start(); f.doc.activeElement = {};
    assert.equal(f.key('KeyS').prevented, false); assert.equal(f.preview.codes.size, 0);
    f.canvas.focus(); assert.equal(f.key('Space').prevented, true); assert.equal(f.key('KeyW').stopped, true);
    f.win.emit('blur'); assert.equal(f.preview.codes.size, 0); assert.equal(f.frames.size, 0);
    f.win.emit('focus'); assert.equal(f.frames.size, 1);
    f.canvas.emit('click'); assert.equal(f.preview.status.looking, true);
    f.key('KeyW'); f.doc.exitPointerLock(); assert.equal(f.preview.codes.size, 0);
    f.canvas.emit('click'); f.key('KeyD'); f.key('Escape');
    assert.equal(f.doc.pointerLockElement, null); assert.equal(f.preview.codes.size, 0);
    f.key('KeyW'); f.preview.stop(); assert.equal(f.preview.codes.size, 0); assert.equal(f.frames.size, 0);
    assert.equal(f.key('Space').prevented, false); f.preview.dispose();
});
test('Ctrl and Command shortcuts never move the camera or consume application key events', () => {
    for (const modifiers of [{ ctrl: true }, { meta: true }]) {
        const f = fixture(); f.preview.setScene(scene()); f.preview.start();
        f.key('KeyW'); f.frame(0); f.frame(100);
        const position = f.preview.camera.position.slice();
        for (const code of ['KeyS', 'KeyW']) {
            const pressed = f.key(code, true, modifiers);
            assert.equal(pressed.prevented, false); assert.equal(pressed.stopped, false);
            assert.equal(f.preview.codes.size, 0);
            f.frame(200); f.frame(300);
            assert.deepEqual(f.preview.camera.position, position);
            // Some platforms release Command before delivering the letter's keyup.
            const released = f.key(code, false);
            assert.equal(released.prevented, false); assert.equal(released.stopped, false);
        }
        f.key('KeyW', true, { alt: true }); f.frame(400); f.frame(500);
        close(position[2] - f.preview.camera.position[2], MOVE_SPEED * .2 * RUN_MULTIPLIER);
        f.preview.dispose();
    }
});
test('mouselook supports pointer lock and drag fallback', () => {
    const f = fixture(); f.preview.setScene(scene()); f.preview.start(); f.canvas.emit('click');
    f.doc.emit('mousemove', { movementX: 40, movementY: -20 });
    assert(f.preview.camera.yaw > 0); assert(f.preview.camera.pitch > 0);
    f.preview.dispose();
    const drag = fixture({ noPointerLock: true }); drag.preview.setScene(scene()); drag.preview.start();
    drag.canvas.emit('click'); assert.match(drag.preview.status.message, /drag/);
    drag.canvas.emit('mousedown', { button: 0, clientX: 20, clientY: 30 });
    drag.doc.emit('mousemove', { clientX: 40, clientY: 20 });
    assert(drag.preview.camera.yaw > 0); assert(drag.preview.camera.pitch > 0);
    drag.doc.emit('mouseup'); const yaw = drag.preview.camera.yaw;
    drag.doc.emit('mousemove', { clientX: 100, clientY: 100 }); assert.equal(drag.preview.camera.yaw, yaw);
    drag.preview.dispose();
});
test('WebGL draws batched triangles and lines, caps display density and preserves camera on scene refresh', () => {
    const f = fixture(); const data = scene(); f.preview.setScene(data); f.preview.start(); f.frame(0);
    assert.equal(f.canvas.width, 600); assert.equal(f.canvas.height, 400);
    assert.deepEqual(f.gl.calls.filter(call => call[0] === 'drawArrays').map(call => call.slice(1)),
        [[f.gl.TRIANGLES, 0, 3], [f.gl.LINES, 0, 2]]);
    assert(f.gl.calls.some(call => call[0] === 'enable' && call[1] === f.gl.DEPTH_TEST));
    f.preview.camera.position = [8, 2, 9];
    f.preview.setScene(scene(), { resetCamera: false }); assert.deepEqual(f.preview.camera.position, [8, 2, 9]);
    f.preview.resetCamera(); assert.deepEqual(f.preview.camera.position, data.spawn.position);
    f.preview.dispose(); assert.equal(f.frames.size, 0);
    assert([...f.canvas.listeners.values()].every(listeners => listeners.size === 0));
    assert.equal(f.preview.start(), false);
    assert.throws(() => f.preview.setScene(scene()), /disposed/);
});
test('hidden windows and WebGL context loss suspend rendering and clear motion, restoration reloads buffers', () => {
    const f = fixture(); f.preview.setScene(scene()); f.preview.start(); f.key('KeyW'); f.frame(0);
    f.doc.hidden = true; f.doc.emit('visibilitychange'); assert.equal(f.frames.size, 0); assert.equal(f.preview.codes.size, 0);
    f.doc.hidden = false; f.doc.emit('visibilitychange'); assert.equal(f.frames.size, 1);
    const position = f.preview.camera.position.slice(); f.frame(10000); assert.deepEqual(f.preview.camera.position, position);
    f.canvas.emit('click'); f.key('KeyW');
    const lost = f.canvas.emit('webglcontextlost');
    assert.equal(lost.prevented, true); assert.equal(f.frames.size, 0); assert.equal(f.preview.available, false);
    assert.match(f.preview.status.message, /WebGL recovers/);
    const uploads = f.gl.calls.filter(call => call[0] === 'bufferData').length;
    f.canvas.emit('webglcontextrestored'); assert.equal(f.preview.available, true); assert.equal(f.frames.size, 1);
    assert.equal(f.gl.calls.filter(call => call[0] === 'bufferData').length, uploads + 4);
    f.preview.dispose();
});
test('WebGL absence is reported without failing the editor and malformed geometry is rejected before upload', () => {
    const absent = fixture({ unavailable: true }); assert.equal(absent.preview.available, false);
    assert.match(absent.preview.status.message, /requires WebGL/); assert.equal(absent.preview.start(), false);
    assert.equal(absent.preview.active, false); assert.equal(absent.frames.size, 0);
    absent.preview.dispose();
    const f = fixture(); f.preview.setScene(scene()); const previous = f.preview.scene;
    const invalid = scene(); invalid.positions[0] = NaN;
    assert.throws(() => f.preview.setScene(invalid), /geometry/); assert.equal(f.preview.scene, previous);
    const wrongBounds = scene(); wrongBounds.bounds.max[0] = -1;
    assert.throws(() => f.preview.setScene(wrongBounds), /bounds/);
    f.preview.dispose();
});
test('a pending pointer-lock grant is released after the preview is disposed', () => {
    const f = fixture(); let grant;
    f.canvas.requestPointerLock = () => ({ then(resolve) { grant = resolve; } });
    f.preview.setScene(scene()); f.preview.start(); f.canvas.emit('click');
    f.preview.dispose();
    f.doc.pointerLockElement = f.canvas; grant();
    assert.equal(f.doc.pointerLockElement, null);
});
test('graphics memory exhaustion is reported and partial GPU allocations are freed', () => {
    const f = fixture(); f.gl.getError = () => f.gl.OUT_OF_MEMORY;
    f.preview.setScene(scene());
    assert.equal(f.preview.available, false); assert.match(f.preview.status.message, /graphics memory/);
    assert.equal(f.preview.batches.length, 0); assert.equal(f.preview.start(), false);
    assert.equal(f.gl.calls.filter(call => call[0] === 'deleteBuffer').length, 4);
    f.preview.dispose();
});
