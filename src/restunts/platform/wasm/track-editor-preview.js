/* WebGL track preview and collision-free, frame-rate-independent camera controls. */
(function (root, factory) {
    const api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.TrackEditorPreview = api;
}(typeof globalThis !== 'undefined' ? globalThis : this, function () {
    'use strict';
    const MOVE_SPEED = 1, RUN_MULTIPLIER = 3, MAX_FRAME_SECONDS = .1;
    const LOOK_SENSITIVITY = .0025, PITCH_LIMIT = Math.PI / 2 - .01;
    const FIELD_OF_VIEW = 70 * Math.PI / 180, NEAR_PLANE = .02, MIN_FAR_PLANE = 128;
    const MAX_PIXEL_RATIO = 2, DEFAULT_EYE_HEIGHT = .18, DEFAULT_CAMERA_DISTANCE = 2;
    const SKY_COLOR = [.36, .68, .87], VECTOR_SIZE = 3, TRIANGLE_VALUES = 9, LINE_VALUES = 6;
    const MOVEMENT_CODES = new Set(['KeyW', 'KeyA', 'KeyS', 'KeyD', 'Space', 'ShiftLeft', 'ShiftRight']);
    const MODIFIER_CODES = new Set(['AltLeft', 'AltRight', 'CapsLock']);
    const VERTEX_SHADER = `
        attribute vec3 aPosition;
        attribute vec3 aColor;
        uniform mat4 uViewProjection;
        uniform vec3 uEye;
        varying vec3 vColor;
        varying float vDistance;
        void main() {
            gl_Position = uViewProjection * vec4(aPosition, 1.0);
            vColor = aColor;
            vDistance = distance(aPosition, uEye);
        }
    `;
    const FRAGMENT_SHADER = `
        precision mediump float;
        uniform vec3 uSky;
        uniform vec2 uFog;
        varying vec3 vColor;
        varying float vDistance;
        void main() {
            float fog = smoothstep(uFog.x, uFog.y, vDistance);
            gl_FragColor = vec4(mix(vColor, uSky, fog), 1.0);
        }
    `;
    function clamp(value, low, high) { return Math.max(low, Math.min(high, value)); }
    function finiteVector(vector) {
        return vector && vector.length === VECTOR_SIZE && Array.from(vector).every(Number.isFinite);
    }
    function movement(camera, codes, seconds, fast) {
        const forward = Number(codes.has('KeyW')) - Number(codes.has('KeyS'));
        const right = Number(codes.has('KeyD')) - Number(codes.has('KeyA'));
        const up = Number(codes.has('Space')) - Number(codes.has('ShiftLeft') || codes.has('ShiftRight'));
        const length = Math.hypot(forward, right, up);
        if (!length) return;
        const distance = MOVE_SPEED * (fast ? RUN_MULTIPLIER : 1) * seconds / length;
        const sine = Math.sin(camera.yaw), cosine = Math.cos(camera.yaw);
        camera.position[0] += (forward * sine + right * cosine) * distance;
        camera.position[1] += up * distance;
        camera.position[2] += (-forward * cosine + right * sine) * distance;
    }
    function look(camera, dx, dy) {
        if (!Number.isFinite(dx) || !Number.isFinite(dy)) return;
        camera.yaw = (camera.yaw + dx * LOOK_SENSITIVITY) % (Math.PI * 2);
        camera.pitch = clamp(camera.pitch - dy * LOOK_SENSITIVITY, -PITCH_LIMIT, PITCH_LIMIT);
    }
    function viewProjection(camera, aspect, far) {
        const sine = Math.sin(camera.yaw), cosine = Math.cos(camera.yaw);
        const pitchSine = Math.sin(camera.pitch), pitchCosine = Math.cos(camera.pitch);
        const right = [cosine, 0, sine];
        const up = [-sine * pitchSine, pitchCosine, cosine * pitchSine];
        const back = [-sine * pitchCosine, -pitchSine, cosine * pitchCosine];
        const eye = camera.position;
        const dot = vector => vector[0] * eye[0] + vector[1] * eye[1] + vector[2] * eye[2];
        const view = [right[0], up[0], back[0], 0, right[1], up[1], back[1], 0,
            right[2], up[2], back[2], 0, -dot(right), -dot(up), -dot(back), 1];
        const focal = 1 / Math.tan(FIELD_OF_VIEW / 2), depth = 1 / (NEAR_PLANE - far);
        const projection = [focal / aspect, 0, 0, 0, 0, focal, 0, 0,
            0, 0, (far + NEAR_PLANE) * depth, -1, 0, 0, 2 * far * NEAR_PLANE * depth, 0];
        const matrix = new Float32Array(16);
        for (let column = 0; column < 4; column++) for (let row = 0; row < 4; row++) {
            for (let k = 0; k < 4; k++) matrix[column * 4 + row] += projection[k * 4 + row] * view[column * 4 + k];
        }
        return matrix;
    }
    function normalizeScene(scene) {
        if (!scene || !finiteVector(scene.bounds?.min) || !finiteVector(scene.bounds?.max) ||
            scene.bounds.min.some((value, index) => value > scene.bounds.max[index])) throw new Error('Invalid 3D preview bounds.');
        const result = { ...scene };
        for (const [positions, colors, primitive] of [['positions', 'colors', TRIANGLE_VALUES], ['linePositions', 'lineColors', LINE_VALUES]]) {
            const points = scene[positions] || new Float32Array();
            const paints = scene[colors] || new Float32Array();
            if (!(points instanceof Float32Array) || !(paints instanceof Float32Array) ||
                points.length % primitive || paints.length !== points.length ||
                !points.every(Number.isFinite) || !paints.every(Number.isFinite)) throw new Error('Invalid 3D preview geometry.');
            result[positions] = points; result[colors] = paints;
        }
        if (scene.spawn && (!finiteVector(scene.spawn.position) ||
            (scene.spawn.yaw !== undefined && !Number.isFinite(scene.spawn.yaw)) ||
            (scene.spawn.pitch !== undefined && !Number.isFinite(scene.spawn.pitch)))) throw new Error('Invalid 3D preview camera.');
        return result;
    }
    class Preview {
        constructor(canvas, options = {}) {
            this.canvas = canvas;
            this.document = canvas.ownerDocument;
            this.window = this.document.defaultView;
            this.onStatus = typeof options.onStatus === 'function' ? options.onStatus : () => {};
            this.active = false; this.available = false; this.disposed = false;
            this.camera = { position: [0, DEFAULT_EYE_HEIGHT, DEFAULT_CAMERA_DISTANCE], yaw: 0, pitch: 0 };
            this.codes = new Set(); this.alt = false; this.capsLock = false;
            this.dragging = false; this.pointerLocked = false; this.contextLost = false;
            this.scene = null; this.frame = null; this.lastTime = null; this.lastStatus = '';
            this.focused = true; this.listeners = []; this.batches = []; this.program = null;
            this.message = '';
            this.listen(canvas, 'click', event => this.requestLook(event));
            this.listen(canvas, 'mousedown', event => this.mouseDown(event));
            this.listen(this.document, 'mousemove', event => this.mouseMove(event));
            this.listen(this.document, 'mouseup', () => { this.dragging = false; });
            this.listen(this.document, 'keydown', event => this.key(event, true), true);
            this.listen(this.document, 'keyup', event => this.key(event, false), true);
            this.listen(canvas, 'blur', () => { if (!this.hasFocus()) this.clearInput(); });
            this.listen(this.document, 'pointerlockchange', () => this.pointerLockChanged());
            this.listen(this.document, 'pointerlockerror', () => {
                this.message = 'Mouselook could not lock the pointer. Hold the left mouse button and drag to look around.';
                this.report();
            });
            this.listen(this.window, 'blur', () => { this.focused = false; this.clearInput(); this.cancelFrame(); });
            this.listen(this.window, 'focus', () => { this.focused = true; this.lastTime = null; this.schedule(); });
            this.listen(this.document, 'visibilitychange', () => {
                this.clearInput(); this.cancelFrame(); this.lastTime = null; this.schedule();
            });
            this.listen(canvas, 'webglcontextlost', event => {
                event.preventDefault(); this.contextLost = true; this.available = false;
                this.clearInput(); this.releaseLook(); this.cancelFrame();
                this.message = 'The 3D preview is paused while WebGL recovers.'; this.report();
            });
            this.listen(canvas, 'webglcontextrestored', () => {
                if (this.disposed) return;
                this.contextLost = false; this.program = null; this.batches = [];
                this.initialize(); this.upload(); this.lastTime = null; this.report(); this.schedule();
            });
            this.initialize(); this.report();
        }
        listen(target, event, callback, capture = false) {
            target.addEventListener(event, callback, capture);
            this.listeners.push(() => target.removeEventListener(event, callback, capture));
        }
        initialize() {
            try {
                this.gl = this.canvas.getContext('webgl', { alpha: false, antialias: true, depth: true, powerPreference: 'high-performance' });
                if (!this.gl) throw new Error('WebGL is unavailable.');
                const gl = this.gl;
                const compile = (type, source) => {
                    const shader = gl.createShader(type);
                    if (!shader) throw new Error('Could not allocate a WebGL shader.');
                    gl.shaderSource(shader, source); gl.compileShader(shader);
                    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
                        gl.deleteShader(shader); throw new Error('Could not compile the 3D preview shader.');
                    }
                    return shader;
                };
                const shaders = [];
                try {
                    shaders.push(compile(gl.VERTEX_SHADER, VERTEX_SHADER));
                    shaders.push(compile(gl.FRAGMENT_SHADER, FRAGMENT_SHADER));
                    this.program = gl.createProgram();
                    if (!this.program) throw new Error('Could not allocate a WebGL program.');
                    shaders.forEach(shader => gl.attachShader(this.program, shader));
                    gl.linkProgram(this.program);
                    if (!gl.getProgramParameter(this.program, gl.LINK_STATUS)) throw new Error('Could not link the 3D preview shader.');
                } finally { shaders.forEach(shader => gl.deleteShader(shader)); }
                this.locations = {
                    position: gl.getAttribLocation(this.program, 'aPosition'), color: gl.getAttribLocation(this.program, 'aColor'),
                    viewProjection: gl.getUniformLocation(this.program, 'uViewProjection'), eye: gl.getUniformLocation(this.program, 'uEye'),
                    sky: gl.getUniformLocation(this.program, 'uSky'), fog: gl.getUniformLocation(this.program, 'uFog')
                };
                gl.enable(gl.DEPTH_TEST); gl.depthFunc(gl.LEQUAL);
                gl.disable(gl.CULL_FACE);
                gl.enable(gl.POLYGON_OFFSET_FILL); gl.polygonOffset(1, 1);
                this.available = true;
                this.message = 'Click the view for mouselook. Press Esc to release the pointer.';
            } catch (error) {
                if (this.program && this.gl) this.gl.deleteProgram(this.program);
                this.program = null; this.available = false;
                this.message = 'The 3D preview requires WebGL. ' + error.message;
            }
        }
        hasFocus() { return this.document.activeElement === this.canvas || this.document.pointerLockElement === this.canvas; }
        syncModifiers(event) {
            this.alt = !!event.altKey;
            if (typeof event.getModifierState === 'function') this.capsLock = !!event.getModifierState('CapsLock');
        }
        key(event, down) {
            if (!this.active || !this.hasFocus()) return;
            // Leave document and browser shortcuts available while the view has focus.
            // Dropping held motion also prevents a shortcut from continuing an earlier walk.
            if (event.ctrlKey || event.metaKey) { this.clearInput(); return; }
            const relevant = MOVEMENT_CODES.has(event.code) || MODIFIER_CODES.has(event.code) || event.code === 'Escape';
            if (!relevant || (!down && MOVEMENT_CODES.has(event.code) && !this.codes.has(event.code))) return;
            this.syncModifiers(event);
            if (event.code === 'AltLeft' || event.code === 'AltRight') this.alt = down || !!event.altKey;
            if (MOVEMENT_CODES.has(event.code)) {
                if (down) this.codes.add(event.code); else this.codes.delete(event.code);
            }
            if (event.code === 'Escape' && down) { this.clearInput(); this.releaseLook(); }
            event.preventDefault(); event.stopPropagation(); this.report();
        }
        mouseDown(event) {
            if (!this.active || event.button !== 0) return;
            this.canvas.focus({ preventScroll: true }); this.syncModifiers(event);
            this.dragging = true; this.mouseX = event.clientX; this.mouseY = event.clientY;
            event.preventDefault(); this.report();
        }
        mouseMove(event) {
            if (!this.active || (!this.pointerLocked && !this.dragging)) return;
            this.syncModifiers(event);
            if (this.pointerLocked) look(this.camera, event.movementX, event.movementY);
            else {
                look(this.camera, event.clientX - this.mouseX, event.clientY - this.mouseY);
                this.mouseX = event.clientX; this.mouseY = event.clientY;
            }
            event.preventDefault(); this.report();
        }
        requestLook(event) {
            if (!this.active || !this.available || this.pointerLocked) return;
            this.canvas.focus({ preventScroll: true });
            if (event) this.syncModifiers(event);
            if (typeof this.canvas.requestPointerLock !== 'function') {
                this.message = 'Hold the left mouse button and drag to look around.'; this.report(); return;
            }
            try {
                const pending = this.canvas.requestPointerLock();
                if (pending && typeof pending.then === 'function') pending.then(() => {
                    if (!this.active || this.disposed) this.releaseLook();
                }, () => {
                    if (!this.active || this.disposed) return;
                    this.message = 'Hold the left mouse button and drag to look around.'; this.report();
                });
            } catch (_) { this.message = 'Hold the left mouse button and drag to look around.'; this.report(); }
        }
        pointerLockChanged() {
            const wasLocked = this.pointerLocked;
            this.pointerLocked = this.document.pointerLockElement === this.canvas;
            if (this.pointerLocked && (!this.active || this.disposed)) { this.releaseLook(); return; }
            if (wasLocked && !this.pointerLocked) this.clearInput();
            if (this.available) this.message = this.pointerLocked ? 'Mouselook active. Press Esc to release the pointer.' :
                'Click the view for mouselook. Press Esc to release the pointer.';
            this.report();
        }
        releaseLook() {
            this.dragging = false;
            if (this.document.pointerLockElement === this.canvas && typeof this.document.exitPointerLock === 'function') this.document.exitPointerLock();
        }
        clearInput() {
            this.codes.clear(); this.alt = false; this.capsLock = false; this.dragging = false;
            this.lastTime = null; this.report();
        }
        setScene(scene, options = {}) {
            if (this.disposed) throw new Error('The 3D preview has been disposed.');
            const next = normalizeScene(scene);
            this.scene = next;
            this.upload();
            if (options.resetCamera !== false) this.resetCamera();
            this.schedule();
        }
        upload() {
            if (!this.available || !this.scene || this.contextLost || this.disposed) return;
            const gl = this.gl;
            this.batches.forEach(batch => { gl.deleteBuffer(batch.positions); gl.deleteBuffer(batch.colors); });
            this.batches = [];
            try {
                for (const [positions, colors, mode] of [['positions', 'colors', gl.TRIANGLES], ['linePositions', 'lineColors', gl.LINES]]) {
                    const points = this.scene[positions], paints = this.scene[colors];
                    if (!points.length) continue;
                    const batch = { positions: gl.createBuffer(), colors: gl.createBuffer(), count: points.length / VECTOR_SIZE, mode };
                    this.batches.push(batch);
                    if (!batch.positions || !batch.colors) throw new Error('Could not allocate the 3D preview geometry.');
                    gl.bindBuffer(gl.ARRAY_BUFFER, batch.positions); gl.bufferData(gl.ARRAY_BUFFER, points, gl.STATIC_DRAW);
                    gl.bindBuffer(gl.ARRAY_BUFFER, batch.colors); gl.bufferData(gl.ARRAY_BUFFER, paints, gl.STATIC_DRAW);
                }
                const error = gl.getError();
                if (error !== gl.NO_ERROR) throw new Error(error === gl.OUT_OF_MEMORY
                    ? 'This track is too large for the available graphics memory.'
                    : 'The 3D preview could not load this track into graphics memory.');
            } catch (error) {
                this.batches.forEach(batch => { gl.deleteBuffer(batch.positions); gl.deleteBuffer(batch.colors); });
                this.batches = [];
                this.available = false; this.message = error.message; this.cancelFrame(); this.report();
            }
        }
        resetCamera() {
            if (!this.scene) return;
            const { bounds, spawn } = this.scene;
            this.camera.position = spawn ? Array.from(spawn.position) :
                [(bounds.min[0] + bounds.max[0]) / 2, bounds.max[1] + DEFAULT_EYE_HEIGHT, bounds.max[2] + DEFAULT_CAMERA_DISTANCE];
            this.camera.yaw = spawn?.yaw || 0;
            this.camera.pitch = clamp(spawn?.pitch || 0, -PITCH_LIMIT, PITCH_LIMIT);
            this.clearInput(); this.schedule();
        }
        start() {
            if (this.disposed || !this.available) return false;
            this.active = true; this.focused = true; this.lastTime = null;
            this.canvas.focus({ preventScroll: true }); this.report(); this.schedule();
            return this.available;
        }
        stop() {
            this.active = false; this.cancelFrame(); this.clearInput(); this.releaseLook(); this.report();
        }
        cancelFrame() {
            if (this.frame !== null) this.window.cancelAnimationFrame(this.frame);
            this.frame = null;
        }
        schedule() {
            if (!this.active || !this.available || this.disposed || this.contextLost || this.document.hidden || !this.focused || this.frame !== null) return;
            this.frame = this.window.requestAnimationFrame(time => this.tick(time));
        }
        tick(time) {
            this.frame = null;
            if (!this.active || !this.available || this.disposed || this.contextLost || this.document.hidden || !this.focused) return;
            const seconds = this.lastTime === null ? 0 : clamp((time - this.lastTime) / 1000, 0, MAX_FRAME_SECONDS);
            this.lastTime = time;
            if (this.hasFocus()) movement(this.camera, this.codes, seconds, this.alt || this.capsLock);
            this.draw(); this.schedule();
        }
        draw() {
            if (!this.available || !this.scene || this.contextLost || this.disposed) return;
            const gl = this.gl, ratio = Math.min(this.window.devicePixelRatio || 1, MAX_PIXEL_RATIO);
            const width = Math.max(1, Math.round(this.canvas.clientWidth * ratio));
            const height = Math.max(1, Math.round(this.canvas.clientHeight * ratio));
            if (this.canvas.width !== width || this.canvas.height !== height) { this.canvas.width = width; this.canvas.height = height; }
            const bounds = this.scene.bounds;
            const extent = Math.hypot(...bounds.max.map((value, index) => value - bounds.min[index]));
            const centerDistance = Math.hypot(...this.camera.position.map((value, index) => value - (bounds.min[index] + bounds.max[index]) / 2));
            const far = Math.max(MIN_FAR_PLANE, extent * 2 + centerDistance);
            const sky = finiteVector(this.scene.skyColor) ? this.scene.skyColor : SKY_COLOR;
            gl.viewport(0, 0, width, height); gl.clearColor(...sky, 1); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
            gl.useProgram(this.program);
            gl.uniformMatrix4fv(this.locations.viewProjection, false, viewProjection(this.camera, width / height, far));
            gl.uniform3fv(this.locations.eye, this.camera.position); gl.uniform3fv(this.locations.sky, sky);
            gl.uniform2f(this.locations.fog, far * .6, far);
            for (const batch of this.batches) {
                gl.bindBuffer(gl.ARRAY_BUFFER, batch.positions); gl.enableVertexAttribArray(this.locations.position);
                gl.vertexAttribPointer(this.locations.position, VECTOR_SIZE, gl.FLOAT, false, 0, 0);
                gl.bindBuffer(gl.ARRAY_BUFFER, batch.colors); gl.enableVertexAttribArray(this.locations.color);
                gl.vertexAttribPointer(this.locations.color, VECTOR_SIZE, gl.FLOAT, false, 0, 0);
                gl.drawArrays(batch.mode, 0, batch.count);
            }
        }
        report() {
            const status = { available: this.available, active: this.active, looking: this.pointerLocked,
                running: this.capsLock, speedMultiplier: this.alt || this.capsLock ? RUN_MULTIPLIER : 1, message: this.message };
            this.status = status;
            const signature = JSON.stringify(status);
            if (signature !== this.lastStatus) { this.lastStatus = signature; this.onStatus(status); }
        }
        dispose() {
            if (this.disposed) return;
            this.stop(); this.disposed = true;
            this.listeners.forEach(remove => remove()); this.listeners = [];
            if (this.gl && !this.contextLost) {
                this.batches.forEach(batch => { this.gl.deleteBuffer(batch.positions); this.gl.deleteBuffer(batch.colors); });
                if (this.program) this.gl.deleteProgram(this.program);
            }
            this.batches = []; this.program = null; this.scene = null;
        }
    }
    return { Preview, MOVE_SPEED, RUN_MULTIPLIER, movement, look, viewProjection };
}));
