/* The editor is loaded with the browser game. The game and preview share
 * touch.c's geometry; JavaScript owns only the editable draft and its UI. */
(() => {
    const MIN_SCALE = 1;
    const MAX_SCALE = 3;
    const SCALE_STEP = 0.01;
    const DEFAULT_CENTER = -1;
    const CENTER_COMPONENTS = 2;
    const CIRCLE_COMPONENTS = 3;
    const DEFAULT_ASPECT = 4 / 3;
    const KEYBOARD_MOVE = 8;
    // The order matches enum TOUCH_CONTROL in touch.h.
    const CONTROLS = [
        ['Escape', 'ESC'], ['Steer left', '←'], ['Steer right', '→'],
        ['Shift up', '↑', 'SHIFT+'], ['Shift down', '↓', 'SHIFT−'], ['Rewind', 'Q'],
        ['Accelerate', '↑'], ['Brake', '↓'], ['Camera', 'C'], ['Follow opponent', 'T']
    ];
    const ARROW_MOVES = new Map([
        ['ArrowLeft', [-1, 0]], ['ArrowRight', [1, 0]],
        ['ArrowUp', [0, -1]], ['ArrowDown', [0, 1]]
    ]);
    const CENTER_COUNT = CONTROLS.length * CENTER_COMPONENTS;
    let editor = null;

    function validateLayout(layout) {
        if (!layout || !Number.isFinite(layout.scale) || layout.scale < MIN_SCALE || layout.scale > MAX_SCALE ||
            !Array.isArray(layout.centers) || layout.centers.length !== CENTER_COUNT) return null;
        const centers = Array.from(layout.centers);
        for (let offset = 0; offset < centers.length; offset += CENTER_COMPONENTS) {
            const x = centers[offset];
            const y = centers[offset + 1];
            if (x === DEFAULT_CENTER && y === DEFAULT_CENTER) continue;
            if (!Number.isFinite(x) || !Number.isFinite(y) || x < 0 || x > 1 || y < 0 || y > 1) return null;
        }
        return { scale: layout.scale, centers };
    }

    function defaultLayout() {
        return { scale: MIN_SCALE, centers: Array(CENTER_COUNT).fill(DEFAULT_CENTER) };
    }

    function copyCenters(centers) {
        const address = Module['_restunts_browser_touch_centers']();
        HEAPF32.set(centers, address / Float32Array.BYTES_PER_ELEMENT);
    }

    function preview(layout, width, height) {
        copyCenters(layout.centers);
        const address = Module['_restunts_browser_touch_preview'](layout.scale, width, height);
        const offset = address / Float32Array.BYTES_PER_ELEMENT;
        return HEAPF32.slice(offset, offset + CONTROLS.length * CIRCLE_COMPONENTS);
    }

    function previewAspect() {
        const display = window.screen;
        if (document.getElementById('start-fullscreen')?.checked && display?.width > 0 && display.height > 0) {
            // Fullscreen uses the full display, including the side areas around the game.
            return Module['mobileDevice'] ?
                Math.max(display.width, display.height) / Math.min(display.width, display.height) :
                display.width / display.height;
        }
        const game = document.getElementById('canvas')?.getBoundingClientRect();
        return game?.width && game.height ? game.width / game.height : DEFAULT_ASPECT;
    }

    function loadStyle() {
        if (document.getElementById('restunts-control-editor-style')) return;
        const style = document.createElement('style');
        style.id = 'restunts-control-editor-style';
        style.textContent = `
            .restunts-control-editor {
                box-sizing: border-box; width: min(1060px, calc(100% - 24px));
                height: min(850px, calc(100% - 24px)); max-width: none; max-height: none; padding: 16px;
                border: 1px solid #63748a; border-radius: 8px; background: #1b2635; color: #eef3fa;
            }
            .restunts-control-editor[open] { display: grid; grid-template-rows: auto minmax(0, 1fr); gap: 12px; }
            .restunts-control-editor::backdrop { background: rgb(0 0 0 / .65); }
            .restunts-control-editor header { display: flex; align-items: center; justify-content: space-between; gap: 8px; }
            .restunts-control-editor h2 { margin: 0; font-size: 1.2rem; }
            .restunts-control-editor .control-layout-actions { display: flex; gap: 6px; }
            .restunts-control-editor .control-layout-action { width: 44px; height: 44px; padding: 0; font-size: 1.4rem; }
            .restunts-control-editor .control-layout-save { color: #6ae289; }
            .restunts-control-editor .control-layout-cancel { color: #ff8181; }
            .restunts-control-editor .control-layout-viewport {
                display: grid; grid-template: minmax(0, 1fr) / minmax(0, 1fr); place-items: center;
            }
            .restunts-control-editor .control-layout-stage {
                position: relative; background: #171a1e; outline: 1px solid #45566a; border-radius: 6px;
                touch-action: none; user-select: none;
            }
            .restunts-control-editor .control-layout-circle {
                box-sizing: border-box; position: absolute; display: flex; flex-direction: column;
                justify-content: center; align-items: center; transform: translate(-50%, -50%);
                padding: 0; border: 1px solid rgb(255 255 255 / .82); border-radius: 50%;
                background: rgb(255 255 255 / .15); color: #fff; line-height: 1; cursor: grab;
            }
            .restunts-control-editor .control-layout-circle:focus-visible {
                z-index: 1; outline: 2px solid #edbd73; outline-offset: -3px;
            }
            .restunts-control-editor .control-layout-circle.dragging {
                z-index: 1; background: rgb(255 255 255 / .43); cursor: grabbing;
            }
            .restunts-control-editor .control-layout-shift { font-size: .5em; }
            .restunts-control-editor .control-layout-size {
                position: absolute; z-index: 2; top: 50%; left: 50%; transform: translate(-50%, -50%);
                display: grid; width: 30%; min-width: 90px; gap: 4px; text-align: center; font-size: .75rem;
            }
            .restunts-control-editor input[type="range"] { width: 100%; margin: 0; accent-color: #edbd73; }
        `;
        document.head.append(style);
    }

    function openEditor(layout, onSave) {
        if (editor) return;
        loadStyle();
        const draft = validateLayout(layout) || defaultLayout();
        const dialog = document.createElement('dialog');
        editor = dialog;
        dialog.className = 'restunts-control-editor';
        dialog.setAttribute('aria-labelledby', 'control-layout-title');
        dialog.innerHTML = `
            <header>
                <h2 id="control-layout-title">Edit Control Layout</h2>
                <div class="control-layout-actions">
                    <button type="button" class="control-layout-action control-layout-save" aria-label="Save control layout" title="Save control layout">✓</button>
                    <button type="button" class="control-layout-action control-layout-cancel" aria-label="Discard control layout changes" title="Discard control layout changes">✕</button>
                </div>
            </header>
            <div class="control-layout-viewport">
                <div class="control-layout-stage">
                    <label class="control-layout-size">Control circle size<input type="range"></label>
                </div>
            </div>
        `;
        const size = dialog.querySelector('input');
        size.min = MIN_SCALE;
        size.max = MAX_SCALE;
        size.step = SCALE_STEP;
        size.value = draft.scale;
        const viewport = dialog.querySelector('.control-layout-viewport');
        const stage = dialog.querySelector('.control-layout-stage');
        const circles = CONTROLS.map(([name, symbol, shift]) => {
            const circle = document.createElement('button');
            circle.type = 'button';
            circle.className = 'control-layout-circle';
            circle.setAttribute('aria-label', name);
            circle.title = name;
            circle.textContent = symbol;
            if (shift) {
                const text = document.createElement('span');
                text.className = 'control-layout-shift';
                text.textContent = shift;
                circle.append(text);
            }
            stage.insertBefore(circle, size.parentElement);
            return circle;
        });
        let width = 0;
        let height = 0;
        let positions = null;
        let drag = null;

        function draw() {
            if (!width || !height) return;
            positions = preview(draft, width, height);
            circles.forEach((circle, index) => {
                const offset = index * CIRCLE_COMPONENTS;
                const radius = positions[offset + 2];
                circle.style.left = positions[offset] + 'px';
                circle.style.top = positions[offset + 1] + 'px';
                circle.style.width = circle.style.height = 2 * radius + 'px';
                circle.style.fontSize = .72 * radius + 'px';
            });
        }

        function resize() {
            const bounds = viewport.getBoundingClientRect();
            const aspect = previewAspect();
            width = Math.floor(Math.min(bounds.width, bounds.height * aspect));
            height = Math.floor(width / aspect);
            stage.style.width = width + 'px';
            stage.style.height = height + 'px';
            draw();
        }

        function move(index, x, y) {
            const offset = index * CENTER_COMPONENTS;
            draft.centers[offset] = Math.min(1, Math.max(0, x / width));
            draft.centers[offset + 1] = Math.min(1, Math.max(0, y / height));
            draw();
        }

        function nearestCircle(x, y) {
            let selected = -1;
            let nearest = Infinity;
            // Pick the circle drawn on top when two centers coincide.
            for (let index = circles.length - 1; index >= 0; index--) {
                const offset = index * CIRCLE_COMPONENTS;
                const dx = x - positions[offset];
                const dy = y - positions[offset + 1];
                const radius = positions[offset + 2];
                const distance = dx * dx + dy * dy;
                if (distance <= radius * radius && distance < nearest) {
                    selected = index;
                    nearest = distance;
                }
            }
            return selected;
        }

        stage.addEventListener('pointerdown', event => {
            if (drag || !positions || event.button !== 0 || event.target.closest('.control-layout-size')) return;
            const bounds = stage.getBoundingClientRect();
            const x = event.clientX - bounds.left;
            const y = event.clientY - bounds.top;
            const index = nearestCircle(x, y);
            if (index < 0) return;
            const offset = index * CIRCLE_COMPONENTS;
            drag = { index, pointer: event.pointerId, lastX: event.clientX, lastY: event.clientY,
                offsetX: positions[offset] - x, offsetY: positions[offset + 1] - y };
            circles[index].focus({ preventScroll: true });
            circles[index].classList.add('dragging');
            stage.setPointerCapture(event.pointerId);
            event.preventDefault();
        });

        function dragCircle(event) {
            if (!drag || drag.pointer !== event.pointerId || !width || !height ||
                (event.clientX === drag.lastX && event.clientY === drag.lastY)) return;
            drag.lastX = event.clientX;
            drag.lastY = event.clientY;
            const bounds = stage.getBoundingClientRect();
            move(drag.index, event.clientX - bounds.left + drag.offsetX, event.clientY - bounds.top + drag.offsetY);
        }

        stage.addEventListener('pointermove', dragCircle);
        stage.addEventListener('pointerup', dragCircle);
        stage.addEventListener('lostpointercapture', event => {
            if (!drag || drag.pointer !== event.pointerId) return;
            circles[drag.index].classList.remove('dragging');
            drag = null;
        });
        circles.forEach((circle, index) => circle.addEventListener('keydown', event => {
            const direction = ARROW_MOVES.get(event.key);
            if (!direction || !positions || !width || !height) return;
            event.preventDefault();
            const offset = index * CIRCLE_COMPONENTS;
            move(index, positions[offset] + direction[0] * KEYBOARD_MOVE,
                positions[offset + 1] + direction[1] * KEYBOARD_MOVE);
        }));
        size.addEventListener('input', () => {
            draft.scale = size.valueAsNumber;
            draw();
        });
        dialog.querySelector('.control-layout-save').addEventListener('click', () => {
            const settings = validateLayout(draft);
            if (!settings) return;
            dialog.close();
            onSave(settings);
        });
        dialog.querySelector('.control-layout-cancel').addEventListener('click', () => dialog.close());
        const observer = new ResizeObserver(resize);
        dialog.addEventListener('close', () => {
            observer.disconnect();
            dialog.remove();
            editor = null;
        }, { once: true });
        document.body.append(dialog);
        dialog.showModal();
        observer.observe(viewport);
        resize();
    }

    Module['validateRestuntsControlLayout'] = validateLayout;
    Module['openRestuntsControlLayoutEditor'] = openEditor;
    Module['applyRestuntsControlLayout'] = layout => {
        const settings = validateLayout(layout) || defaultLayout();
        copyCenters(settings.centers);
        Module['_restunts_browser_touch_apply'](settings.scale);
    };
})();
