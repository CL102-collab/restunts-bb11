/* Browser UI only. Geometry, validation, format and history live in the model. */
(function () {
    'use strict';
    const $ = id => document.getElementById(id);
    const canvas = $('track-canvas'), ctx = canvas.getContext('2d');
    const minimap = $('minimap'), mini = minimap.getContext('2d');
    const baseTile = 32, maximumTrackBytes = 4 * 1024 * 1024;
    const view = { x: 24, y: 24, zoom: 1, width: 0, height: 0 };
    const state = {
        ready: false, loading: false, doc: null, resources: null, folder: null, remembered: null,
        files: new Map(), diskNames: new Map(), name: 'Untitled', saved: '', tool: 'place',
        rotation: 0, selected: null, group: 1, panel: 'track', terrainTool: 'paint', hover: null, cursor: { x: 0, y: 0 }, space: false,
        drag: null, dirty: false, frame: 0, iconCache: new Map(), lastMessage: ''
    };
    let model;
    const groupIcons = { 1: 'road', 4: 'stunts', 5: 'banked', 6: 'branch', 7: 'highway', 8: 'elevated', 9: 'corkscrew', 10: 'tree' };
    const groupLabels = { 1: 'Roads', 4: 'Stunts', 5: 'Banked', 6: 'Branches', 7: 'Highway', 8: 'Elevated', 9: 'Corkscrews', 10: 'Scenery' };
    function makeIcon(name) {
        const icon = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
        icon.setAttribute('class', 'icon'); icon.setAttribute('aria-hidden', 'true'); icon.setAttribute('focusable', 'false');
        const use = document.createElementNS('http://www.w3.org/2000/svg', 'use');
        use.setAttribute('href', '#i-' + name); icon.append(use); return icon;
    }
    function syncVisualControls() {
        document.querySelectorAll('[data-surface-for]').forEach(button => {
            button.setAttribute('aria-pressed', String($(button.dataset.surfaceFor).value === button.dataset.value));
        });
        document.querySelectorAll('[data-level]').forEach(button => {
            button.setAttribute('aria-pressed', String($('terrain-level').value === button.dataset.level));
        });
        document.querySelectorAll('[data-group]').forEach(button => {
            button.setAttribute('aria-pressed', String(Number(button.dataset.group) === state.group));
        });
        document.querySelectorAll('[data-piece-direction]').forEach(button => {
            button.setAttribute('aria-pressed', String(Number(button.dataset.pieceDirection) === state.rotation));
        });
    }
    function renderSelection() {
        const variant = state.selected && model.pieceVariant(state.selected, state.rotation);
        const selected = variant && variant.piece, preview = $('selected-preview'), c = preview.getContext('2d');
        c.clearRect(0, 0, preview.width, preview.height);
        if (selected) {
            c.fillStyle = '#365333'; c.fillRect(0, 0, preview.width, preview.height);
            drawPiece(c, selected, 0, 0, preview.width, preview.height, Number($('road-surface').value), variant.rotation, true);
        }
        $('selected-piece').textContent = selected ? selected.name : 'No compatible pieces';
        document.querySelector('.selection-label').textContent = selected ? 'READY TO BUILD' : 'TRY ANOTHER SURFACE';
        const width = selected && (variant.rotation % 2 ? selected.height : selected.width);
        const height = selected && (variant.rotation % 2 ? selected.width : selected.height);
        $('selected-piece-meta').textContent = selected ? (selected.decoration ? 'Scenery' : model.SURFACES[Number($('road-surface').value)]) + ' · ' + width + ' × ' + height + ' tiles' : 'Choose another road surface';
    }

    function message(text, error = false) {
        $('status').textContent = text;
        $('status').dataset.error = String(error);
        state.lastMessage = text;
    }
    function folderMessage(text) { $('folder-status').textContent = text; }
    function showDialog(id) {
        const d = $(id);
        if (!d.open) d.showModal();
    }
    function closeDialog(id) { $(id).close(); canvas.focus({ preventScroll: true }); }
    function editableTarget(target) { return target && /^(INPUT|TEXTAREA|SELECT)$/.test(target.tagName); }
    function signature() {
        if (!state.doc) return '';
        return JSON.stringify([state.doc.width, state.doc.height, Array.from(state.doc.vertices), state.doc.cells, state.doc.objects, state.doc.notes]);
    }
    function refreshDocument() {
        if (!state.doc) return;
        state.dirty = signature() !== state.saved;
        $('document-name').textContent = state.name + (state.dirty ? ' •' : '');
        $('document-state').textContent = state.dirty ? 'Unsaved changes · .cstrk' : '.cstrk · ' + (state.folder ? 'Saving to ' + state.folder.name : 'Save downloads to your device');
        $('map-size').textContent = state.doc.width + ' × ' + state.doc.height;
        $('minimap-caption').textContent = state.doc.width + ' × ' + state.doc.height + ' tiles';
        $('object-count').textContent = state.doc.objects.length + (state.doc.objects.length === 1 ? ' element' : ' elements');
        $('notes-count').textContent = state.doc.notes.length + ' / 8000';
        $('undo').disabled = !state.doc.canUndo;
        $('redo').disabled = !state.doc.canRedo;
        scheduleDraw();
    }
    function setDocument(doc, name, clean = true) {
        state.doc = doc;
        state.name = name || 'Untitled';
        state.saved = clean ? signature() : '';
        state.cursor = { x: 0, y: 0 };
        state.hover = null;
        refreshDocument();
        fitMap();
    }
    function canDiscard() { return !state.dirty || window.confirm('Discard the unsaved changes to ' + state.name + '?'); }
    function showPanel(panel) {
        state.panel = panel;
        $('terrain-panel').hidden = panel !== 'terrain';
        $('track-panel').hidden = panel !== 'track';
        document.querySelectorAll('[data-editor-panel]').forEach(button => {
            const active = button.dataset.editorPanel === panel;
            button.setAttribute('aria-selected', String(active));
            button.tabIndex = active ? 0 : -1;
        });
    }
    function selectPanel(panel) { updateTool(panel === 'terrain' ? state.terrainTool : 'place'); }
    function updateTool(tool) {
        state.tool = tool;
        if (['paint', 'raise', 'lower', 'level', 'water-corner'].includes(tool)) {
            state.terrainTool = tool;
            showPanel('terrain');
        } else if (tool === 'place') showPanel('track');
        $('shore-direction-row').hidden = tool !== 'water-corner';
        document.querySelectorAll('[data-tool]').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.tool === tool)));
        canvas.style.cursor = tool === 'pan' ? 'grab' : 'crosshair';
        const labels = { place: 'Build track', erase: 'Erase track', pan: 'Pan view', paint: 'Paint terrain', raise: 'Raise terrain corner', lower: 'Lower terrain corner', level: 'Level terrain', 'water-corner': 'Water + hill corner' };
        $('canvas-mode').textContent = labels[tool].toUpperCase() + ' · 2D';
        const glyphs = { place: 'build', erase: 'erase', pan: 'hand', paint: 'paint', raise: 'raise', lower: 'lower', level: 'level', 'water-corner': 'shore' };
        document.querySelector('.canvas-tool-indicator use').setAttribute('href', '#i-' + glyphs[tool]);
        scheduleDraw();
    }
    function rotate(amount) {
        setDirection((state.rotation + amount + 4) % 4);
    }
    function setDirection(direction) {
        if (!state.ready) return;
        state.rotation = direction;
        renderPalette();
    }
    function catalogEntry(id) { return model.catalog.find(p => p.id === id); }
    function groupEntries() {
        return model.library.filter(p => Number(p.group) === Number(state.group));
    }
    function renderPalette() {
        const container = $('piece-palette');
        container.replaceChildren();
        const entries = groupEntries();
        const roadSurface = Number($('road-surface').value);
        if (!state.selected || !state.selected.surfaces.includes(roadSurface) || !entries.some(p => p.id === state.selected.id)) state.selected = entries.find(p => p.surfaces.includes(roadSurface)) || null;
        for (const piece of entries) {
            const variant = model.pieceVariant(piece, state.rotation);
            const footprint = model.rotatedFootprint(variant.piece, variant.rotation);
            const button = document.createElement('button');
            button.type = 'button'; button.className = 'piece-button';
            button.disabled = !piece.surfaces.includes(roadSurface);
            button.title = piece.name + (button.disabled ? ' — unavailable on ' + model.SURFACES[roadSurface] : ' · ' + footprint.width + ' × ' + footprint.height);
            button.setAttribute('aria-label', button.title);
            button.setAttribute('aria-pressed', String(state.selected && piece.id === state.selected.id));
            const thumb = document.createElement('canvas'); thumb.width = 64; thumb.height = 64;
            const t = thumb.getContext('2d');
            t.fillStyle = '#365333'; t.fillRect(0, 0, 64, 64);
            drawPiece(t, variant.piece, 0, 0, 64, 64, roadSurface, variant.rotation, true);
            const label = document.createElement('span'); label.textContent = piece.name;
            button.append(thumb, label);
            button.addEventListener('click', () => { state.selected = piece; updateTool('place'); renderPalette(); });
            container.append(button);
        }
        $('active-group-name').textContent = groupLabels[state.group] || 'Track pieces';
        $('palette-count').textContent = entries.length + ' pieces';
        renderSelection();
        syncVisualControls();
        scheduleDraw();
    }
    function buildGroups() {
        $('piece-group').replaceChildren();
        $('piece-groups').replaceChildren();
        for (const group of model.groups) {
            // Dirt and ice are surface choices rather than separate palettes.
            if (group.id === 2 || group.id === 3) continue;
            const name = group.name.replace(/^F\d+\s*[·:—-]?\s*/, '');
            const option = document.createElement('option');
            option.value = group.id; option.textContent = 'F' + group.id + ' · ' + name;
            $('piece-group').append(option);
            const button = document.createElement('button');
            button.type = 'button'; button.className = 'group-choice'; button.dataset.group = group.id;
            button.title = name + ' (F' + group.id + ')'; button.setAttribute('aria-label', button.title);
            const label = document.createElement('span'); label.className = 'group-label'; label.textContent = groupLabels[group.id] || name;
            const key = document.createElement('kbd'); key.textContent = 'F' + group.id;
            button.append(makeIcon(groupIcons[group.id] || 'road'), label, key);
            button.addEventListener('click', () => {
                $('piece-group').value = String(group.id);
                $('piece-group').dispatchEvent(new Event('change', { bubbles: true }));
            });
            $('piece-groups').append(button);
        }
        state.group = Number($('piece-group').value) || 1;
        renderPalette();
    }

    async function rememberFolder(value) {
        if (!window.indexedDB) return null;
        return new Promise((resolve, reject) => {
            const open = indexedDB.open('chocolate-stunts-editor', 1);
            open.onupgradeneeded = () => open.result.createObjectStore('folders');
            open.onerror = () => reject(open.error);
            open.onsuccess = () => {
                const database = open.result;
                const tx = database.transaction('folders', value ? 'readwrite' : 'readonly');
                const store = tx.objectStore('folders');
                const request = value ? store.put(value, 'game') : store.get('game');
                let result = null;
                request.onsuccess = () => { result = request.result; };
                tx.oncomplete = () => { database.close(); resolve(value || result); };
                tx.onerror = () => { database.close(); reject(tx.error); };
            };
        });
    }
    function enableFallback() {
        $('choose-folder').hidden = true;
        $('fallback-folder').hidden = false;
        $('folder-fallback-help').hidden = true;
        folderMessage('Select your game folder. Tracks will be saved as downloads.');
    }
    async function loadFolder(files, directory, name) {
        if (state.loading) return;
        state.loading = true;
        $('choose-folder').disabled = true; $('restore-folder').disabled = true; $('folder-input').disabled = true;
        folderMessage('Reading original track editor resources…');
        try {
            const normalized = new Map(), diskNames = new Map();
            for (const [filename, value] of files) {
                const lower = filename.toLowerCase();
                if (normalized.has(lower)) throw new Error('The selected folder contains conflicting names: ' + filename + '.');
                normalized.set(lower, value); diskNames.set(lower, filename);
            }
            if (!normalized.has('main.res') && !normalized.has('main.pre')) throw new Error('Choose the game folder containing MAIN.RES or MAIN.PRE.');
            const resources = await window.TrackEditorResources.load(normalized);
            state.resources = resources; state.files = normalized; state.diskNames = diskNames; state.folder = directory;
            state.iconCache.clear(); state.ready = true;
            $('editor-controls').disabled = false;
            $('folder-button-label').textContent = name;
            $('folder-button').title = 'Change Stunts folder (' + (directory ? 'direct saving' : 'read-only selection') + ')';
            $('canvas-empty').hidden = true;
            $('folder-dialog').close();
            if (!state.doc) setDocument(new model.Document(90, 90), 'Untitled');
            buildGroups();
            refreshDocument();
            message('Ready. ' + (directory ? 'Tracks save directly to ' + name + '.' : 'Tracks save as downloads.'));
            canvas.focus({ preventScroll: true });
            if (directory) {
                state.remembered = directory;
                try { await rememberFolder(directory); } catch (_) { /* Storage is optional. */ }
            }
        } catch (error) {
            folderMessage(error.message);
        } finally {
            state.loading = false;
            $('choose-folder').disabled = false; $('restore-folder').disabled = false; $('folder-input').disabled = false;
        }
    }
    async function chooseFolder() {
        if (!window.showDirectoryPicker) { enableFallback(); $('folder-input').click(); return; }
        try {
            const directory = await window.showDirectoryPicker({ mode: 'readwrite', id: 'chocolate-stunts' });
            const files = new Map();
            for await (const entry of directory.values()) if (entry.kind === 'file') files.set(entry.name, await entry.getFile());
            await loadFolder(files, directory, directory.name);
        } catch (error) {
            if (error.name === 'AbortError') folderMessage('Folder selection cancelled. Choose a Stunts folder to begin.');
            else { folderMessage(error.message + ' You can also use read-only folder selection.'); $('folder-fallback-help').open = true; }
        }
    }
    async function reconnectFolder() {
        if (!state.remembered) return;
        try {
            if (await state.remembered.requestPermission({ mode: 'readwrite' }) !== 'granted') throw new Error('Folder access was not granted.');
            const files = new Map();
            for await (const entry of state.remembered.values()) if (entry.kind === 'file') files.set(entry.name, await entry.getFile());
            await loadFolder(files, state.remembered, state.remembered.name);
        } catch (error) { folderMessage(error.message); }
    }
    async function loadTrack(file) {
        if (!state.ready || !file) return;
        if (!/\.cstrk$/i.test(file.name)) { message('Choose a .cstrk track.', true); return; }
        if (file.size > maximumTrackBytes) { message('This track is larger than the maximum .cstrk file size.', true); return; }
        if (!canDiscard()) return;
        try {
            const doc = model.decodeCstrk(new Uint8Array(await file.arrayBuffer()));
            setDocument(doc, file.name.replace(/\.cstrk$/i, ''));
            message('Opened ' + file.name + '.');
        } catch (error) { message('Could not open track: ' + error.message, true); }
    }
    function openSave() {
        if (!state.ready) return;
        $('save-name').value = state.name;
        $('save-destination').textContent = state.folder ? 'Save in your selected Stunts folder: ' + state.folder.name + '.' : 'Your browser will download the .cstrk file.';
        $('save-error').textContent = '';
        showDialog('save-dialog');
        $('save-name').select();
    }
    async function saveTrack(event) {
        event.preventDefault();
        if (!state.ready || state.loading) return;
        const name = $('save-name').value.trim();
        if (!name || name.length > 8 || /[^\x21-\x7e]|[/\\:*?"<>|.]/.test(name)) { $('save-error').textContent = 'Use 1–8 ASCII characters without spaces, dots, or reserved characters.'; return; }
        const filename = name + '.cstrk';
        $('confirm-save').disabled = true;
        try {
            const bytes = model.encodeCstrk(state.doc);
            if (state.folder) {
                const existingName = state.diskNames.get(filename.toLowerCase());
                if (existingName && name.toLowerCase() !== state.name.toLowerCase() && !window.confirm('Replace ' + existingName + ' in ' + state.folder.name + '?')) return;
                const handle = await state.folder.getFileHandle(existingName || filename, { create: true });
                const writer = await handle.createWritable();
                try { await writer.write(bytes); await writer.close(); }
                catch (error) { try { await writer.abort(); } catch (_) {} throw error; }
                state.diskNames.set(filename.toLowerCase(), existingName || filename);
            } else {
                const blob = new Blob([bytes], { type: 'application/octet-stream' });
                const url = URL.createObjectURL(blob), link = document.createElement('a');
                link.href = url; link.download = filename; document.body.append(link); link.click(); link.remove();
                window.setTimeout(() => URL.revokeObjectURL(url), 10000);
            }
            state.name = name; state.saved = signature(); refreshDocument(); closeDialog('save-dialog');
            message(state.folder ? 'Saved ' + filename + ' to ' + state.folder.name + '.' : 'Downloaded ' + filename + '.');
        } catch (error) { $('save-error').textContent = 'Track was not saved: ' + error.message; }
        finally { $('confirm-save').disabled = false; }
    }

    function terrainColor(surface, level, shade = 0) {
        const light = Math.round(30 + level * 4 + shade);
        if (surface === 1) return 'hsl(192 39% ' + (58 + level * 3 + shade) + '%)';
        if (surface === 2) return 'hsl(31 34% ' + (35 + level * 4 + shade) + '%)';
        if (surface === 3) return 'hsl(204 48% ' + (34 + level * 3 + shade) + '%)';
        return 'hsl(103 25% ' + light + '%)';
    }
    function drawTerrain(c, doc, x, y, left, top, size, showHeight = false) {
        const h = doc.tileHeights(x, y), cell = doc.cells[y * doc.width + x];
        const surface = cell.surface, average = (h[0] + h[1] + h[2] + h[3]) / 4;
        c.fillStyle = terrainColor(surface, average); c.fillRect(left, top, size + .4, size + .4);
        if (h.some(v => v !== h[0])) {
            const corners = [[0, 0], [1, 0], [1, 1], [0, 1]];
            // Match the game's planar triangles, including the half-water shoreline split.
            const triangles = cell.halfWaterCorner === 1 || cell.halfWaterCorner === 3
                ? [[0, 1, 3], [1, 2, 3]] : [[0, 1, 2], [0, 2, 3]];
            for (const [a, b, d] of triangles) {
                const ab = [corners[b][0] - corners[a][0], corners[b][1] - corners[a][1]];
                const ad = [corners[d][0] - corners[a][0], corners[d][1] - corners[a][1]];
                const slopeX = (h[b] - h[a]) * ad[1] - (h[d] - h[a]) * ab[1];
                const slopeY = ab[0] * (h[d] - h[a]) - ad[0] * (h[b] - h[a]);
                c.beginPath(); c.moveTo(left + corners[a][0] * size, top + corners[a][1] * size);
                c.lineTo(left + corners[b][0] * size, top + corners[b][1] * size);
                c.lineTo(left + corners[d][0] * size, top + corners[d][1] * size); c.closePath();
                c.fillStyle = terrainColor(surface, average, (slopeX + slopeY) * 3); c.fill();
            }
            if (size >= 12) {
                c.strokeStyle = '#deedb930'; c.lineWidth = 1;
                c.beginPath();
                // Half-level intersections cannot land on integer-height corners. Adjacent
                // triangles and tiles therefore join into continuous contours around hills.
                for (let level = Math.min(...h) + .5; level < Math.max(...h); level++) {
                    for (const triangle of triangles) {
                        const crossings = [];
                        for (let edge = 0; edge < 3; edge++) {
                            const a = triangle[edge], b = triangle[(edge + 1) % 3];
                            if ((h[a] < level) === (h[b] < level)) continue;
                            const t = (level - h[a]) / (h[b] - h[a]);
                            crossings.push([
                                left + (corners[a][0] + t * (corners[b][0] - corners[a][0])) * size,
                                top + (corners[a][1] + t * (corners[b][1] - corners[a][1])) * size
                            ]);
                        }
                        if (crossings.length === 2) { c.moveTo(...crossings[0]); c.lineTo(...crossings[1]); }
                    }
                }
                c.stroke();
            }
        }
        if (cell.halfWaterCorner) {
            c.save(); c.translate(left + size / 2, top + size / 2); c.rotate((cell.halfWaterCorner - 1) * Math.PI / 2);
            c.beginPath(); c.moveTo(-size / 2, -size / 2); c.lineTo(size / 2, -size / 2); c.lineTo(-size / 2, size / 2); c.closePath();
            c.fillStyle = terrainColor(3, Math.min(...h)); c.fill();
            c.strokeStyle = '#94cfdb99'; c.lineWidth = Math.max(1, size / 32); c.beginPath(); c.moveTo(size / 2, -size / 2); c.lineTo(-size / 2, size / 2); c.stroke(); c.restore();
        }
        if (showHeight && size >= 18) {
            c.fillStyle = '#eff6dbbd'; c.font = Math.max(8, Math.min(12, size * .29)) + 'px ui-monospace, monospace'; c.textAlign = 'center'; c.textBaseline = 'middle';
            c.fillText(h.every(v => v === h[0]) ? String(h[0]) : Math.min(...h) + '–' + Math.max(...h), left + size / 2, top + size / 2);
        }
    }
    function originalIcon(id, surface) {
        if (!state.resources || !state.resources.palette) return null;
        const cacheKey = id + ':' + surface;
        if (state.iconCache.has(cacheKey)) return state.iconCache.get(cacheKey);
        const bitmap = state.resources.icons.get(id);
        if (!bitmap) return null;
        const icon = document.createElement('canvas'); icon.width = bitmap.width; icon.height = bitmap.height;
        const context = icon.getContext('2d'), image = context.createImageData(bitmap.width, bitmap.height), palette = state.resources.palette;
        for (let i = 0; i < bitmap.pixels.length; i++) {
            const index = bitmap.pixels[i], mask = bitmap.mask && bitmap.mask.pixels[i];
            let r = palette[index * 3], g = palette[index * 3 + 1], b = palette[index * 3 + 2];
            // Tint the original neutral road pixels for the independently selected surface.
            if (surface && Math.max(r, g, b) - Math.min(r, g, b) < 24 && r > 35 && r < 225) {
                if (surface === 1) { g = r * .72; b = r * .45; }
                else { b = Math.min(255, r + 55); g = Math.min(255, r + 32); r = Math.min(255, r + 8); }
            }
            image.data[i * 4] = r; image.data[i * 4 + 1] = g; image.data[i * 4 + 2] = b;
            image.data[i * 4 + 3] = mask && index === 0 ? 0 : 255;
        }
        context.putImageData(image, 0, 0); state.iconCache.set(cacheKey, icon); return icon;
    }
    function drawPiece(c, piece, left, top, width, height, surface, rotation, thumbnail = false) {
        if (piece.decoration) surface = 0;
        const icon = originalIcon(piece.id, surface);
        c.save(); c.translate(left + width / 2, top + height / 2); c.rotate(rotation * Math.PI / 2);
        if (icon) {
            c.imageSmoothingEnabled = false;
            const rotated = rotation % 2, w = rotated ? height : width, h = rotated ? width : height;
            c.drawImage(icon, -w / 2, -h / 2, w, h);
        } else {
            // Only used while artwork is not loaded; the editor remains locked then.
            const s = Math.min(width, height); c.scale(s / 32, s / 32);
            c.lineCap = 'butt'; c.lineJoin = 'round'; c.lineWidth = 12;
            const road = surface === 1 ? '#c69b6a' : surface === 2 ? '#bbdfeb' : '#9da49d';
            c.strokeStyle = '#303c30'; c.beginPath(); c.moveTo(0, -16); c.lineTo(0, 16); c.stroke();
            c.strokeStyle = road; c.lineWidth = 9; c.beginPath();
            if (piece.decoration) { c.fillStyle = '#e2c8a3'; c.fillRect(-8, -8, 16, 16); }
            else if (/turn|corner/i.test(piece.name)) { c.moveTo(0, -16); c.quadraticCurveTo(0, 0, 16, 0); }
            else if (/chicane/i.test(piece.name)) { c.moveTo(0, -16); c.bezierCurveTo(-16, -6, 16, 6, 0, 16); }
            else { c.moveTo(0, -16); c.lineTo(0, 16); }
            c.stroke();
            if (!piece.decoration) { c.strokeStyle = '#eee7b1'; c.lineWidth = .7; c.setLineDash([3, 3]); c.stroke(); }
        }
        c.restore();
    }
    function scheduleDraw() { if (!state.frame) state.frame = requestAnimationFrame(draw); }
    function draw() {
        state.frame = 0;
        const ratio = window.devicePixelRatio || 1;
        if (canvas.width !== Math.round(view.width * ratio) || canvas.height !== Math.round(view.height * ratio)) {
            canvas.width = Math.round(view.width * ratio); canvas.height = Math.round(view.height * ratio);
        }
        ctx.setTransform(ratio, 0, 0, ratio, 0, 0); ctx.fillStyle = '#171c22'; ctx.fillRect(0, 0, view.width, view.height);
        const doc = state.doc, size = baseTile * view.zoom;
        if (!doc) {
            ctx.strokeStyle = '#8ca3b214'; ctx.lineWidth = 1;
            for (let x = view.x % size; x < view.width; x += size) { ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, view.height); ctx.stroke(); }
            for (let y = view.y % size; y < view.height; y += size) { ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(view.width, y); ctx.stroke(); }
            return;
        }
        const x0 = Math.max(0, Math.floor(-view.x / size)), y0 = Math.max(0, Math.floor(-view.y / size));
        const x1 = Math.min(doc.width, Math.ceil((view.width - view.x) / size)), y1 = Math.min(doc.height, Math.ceil((view.height - view.y) / size));
        for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) drawTerrain(ctx, doc, x, y, view.x + x * size, view.y + y * size, size, $('show-heights').checked);
        for (const object of doc.objects) {
            if (object.x + object.width < x0 || object.y + object.height < y0 || object.x > x1 || object.y > y1) continue;
            const piece = catalogEntry(object.id);
            if (piece) drawPiece(ctx, piece, view.x + object.x * size, view.y + object.y * size, object.width * size, object.height * size, object.surface, object.rotation);
        }
        if ($('show-grid').checked && size >= 7) {
            ctx.strokeStyle = '#13210f58'; ctx.lineWidth = 1;
            for (let x = x0; x <= x1; x++) { const px = Math.round(view.x + x * size) + .5; ctx.beginPath(); ctx.moveTo(px, view.y + y0 * size); ctx.lineTo(px, view.y + y1 * size); ctx.stroke(); }
            for (let y = y0; y <= y1; y++) { const py = Math.round(view.y + y * size) + .5; ctx.beginPath(); ctx.moveTo(view.x + x0 * size, py); ctx.lineTo(view.x + x1 * size, py); ctx.stroke(); }
        }
        ctx.strokeStyle = '#a5b58b99'; ctx.lineWidth = 1; ctx.strokeRect(view.x - .5, view.y - .5, doc.width * size + 1, doc.height * size + 1);
        if (state.hover && state.tool !== 'pan') drawCursor(size);
        if (size >= 20) drawCoordinates(size, x0, y0, x1, y1);
        drawMinimap();
    }
    function drawCoordinates(size, x0, y0, x1, y1) {
        ctx.fillStyle = '#d1dfb69c'; ctx.font = '9px ui-monospace, monospace'; ctx.textAlign = 'center'; ctx.textBaseline = 'top';
        const top = Math.max(3, view.y - 15);
        for (let x = x0; x < x1; x++) if (x % 5 === 0) ctx.fillText(x + 1, view.x + (x + .5) * size, top);
        ctx.textAlign = 'left'; ctx.textBaseline = 'middle';
        const left = Math.max(3, view.x - 22);
        for (let y = y0; y < y1; y++) if (y % 5 === 0) ctx.fillText(y + 1, left, view.y + (y + .5) * size);
    }
    function drawCursor(size) {
        const h = state.hover, doc = state.doc;
        if (h.x < 0 || h.y < 0 || h.x >= doc.width || h.y >= doc.height) return;
        let width = 1, height = 1, valid = true, preview = null;
        const variant = state.selected && model.pieceVariant(state.selected, state.rotation);
        if (state.tool === 'place' && state.selected) {
            try { preview = doc.placement(variant.piece.id, h.x, h.y, Number($('road-surface').value), variant.rotation); width = preview.width; height = preview.height; }
            catch (_) { valid = false; const footprint = model.rotatedFootprint(variant.piece, variant.rotation); width = footprint.width; height = footprint.height; }
        }
        const left = view.x + h.x * size, top = view.y + h.y * size;
        ctx.fillStyle = valid ? '#e6d69f22' : '#f080703b'; ctx.strokeStyle = valid ? '#ffdf8b' : '#ff9c80'; ctx.lineWidth = 2;
        ctx.fillRect(left, top, width * size, height * size);
        if (preview && variant) { ctx.globalAlpha = .6; drawPiece(ctx, variant.piece, left, top, width * size, height * size, Number($('road-surface').value), variant.rotation); ctx.globalAlpha = 1; }
        ctx.strokeRect(left + 1, top + 1, width * size - 2, height * size - 2);
        if (state.tool === 'raise' || state.tool === 'lower') {
            const cx = view.x + h.cornerX * size, cy = view.y + h.cornerY * size;
            ctx.fillStyle = '#ffde90'; ctx.beginPath(); ctx.arc(cx, cy, Math.max(3, Math.min(6, size / 7)), 0, Math.PI * 2); ctx.fill();
        }
    }
    function drawMinimap() {
        const doc = state.doc, width = minimap.width, height = minimap.height;
        mini.fillStyle = '#18231b'; mini.fillRect(0, 0, width, height);
        const scale = Math.min(width / doc.width, height / doc.height), ox = (width - doc.width * scale) / 2, oy = (height - doc.height * scale) / 2;
        for (let y = 0; y < doc.height; y++) for (let x = 0; x < doc.width; x++) {
            const h = doc.tileHeights(x, y), cell = doc.cells[y * doc.width + x];
            mini.fillStyle = terrainColor(cell.surface, (h[0] + h[1] + h[2] + h[3]) / 4); mini.fillRect(ox + x * scale, oy + y * scale, scale + .4, scale + .4);
        }
        for (const object of doc.objects) {
            mini.fillStyle = catalogEntry(object.id)?.decoration ? '#ddc895' : object.surface === 1 ? '#d6b083' : object.surface === 2 ? '#d2f0f3' : '#d7d5bf';
            for (let y = 0; y < object.height; y++) for (let x = 0; x < object.width; x++) if (object.mask & (1 << (y * object.width + x))) mini.fillRect(ox + (object.x + x) * scale, oy + (object.y + y) * scale, Math.max(1, scale), Math.max(1, scale));
        }
        const tile = baseTile * view.zoom;
        mini.strokeStyle = '#ffdd8e'; mini.lineWidth = 1.5;
        mini.strokeRect(ox - view.x / tile * scale, oy - view.y / tile * scale, view.width / tile * scale, view.height / tile * scale);
    }
    function resizeCanvas() {
        const bounds = $('canvas-container').getBoundingClientRect();
        if (view.width && view.height) {
            view.x += (bounds.width - view.width) / 2;
            view.y += (bounds.height - view.height) / 2;
        }
        view.width = bounds.width; view.height = bounds.height; scheduleDraw();
    }
    function fitMap() {
        if (!state.doc) return;
        view.zoom = Math.max(.12, Math.min(4, (view.width - 64) / (state.doc.width * baseTile), (view.height - 64) / (state.doc.height * baseTile)));
        view.x = (view.width - state.doc.width * baseTile * view.zoom) / 2;
        view.y = (view.height - state.doc.height * baseTile * view.zoom) / 2;
        updateZoom();
    }
    function updateZoom() { $('zoom-value').textContent = Math.round(view.zoom * 100) + '%'; scheduleDraw(); }
    function zoom(factor, px = view.width / 2, py = view.height / 2) {
        const old = view.zoom; view.zoom = Math.max(.12, Math.min(6, old * factor));
        view.x = px - (px - view.x) * view.zoom / old; view.y = py - (py - view.y) * view.zoom / old; updateZoom();
    }
    function pointAt(event) {
        const bounds = canvas.getBoundingClientRect(), px = event.clientX - bounds.left, py = event.clientY - bounds.top, size = baseTile * view.zoom;
        const fx = (px - view.x) / size, fy = (py - view.y) / size;
        return { x: Math.floor(fx), y: Math.floor(fy), cornerX: Math.round(fx), cornerY: Math.round(fy), px, py };
    }
    function hoverAt(point) {
        state.hover = point;
        const doc = state.doc;
        if (doc && point.x >= 0 && point.y >= 0 && point.x < doc.width && point.y < doc.height) {
            state.cursor = { x: point.x, y: point.y };
            const h = doc.tileHeights(point.x, point.y);
            $('cursor-position').textContent = (point.x + 1) + ', ' + (point.y + 1) + ' · L' + Math.min(...h) + (Math.max(...h) !== Math.min(...h) ? '–' + Math.max(...h) : '');
        } else $('cursor-position').textContent = '—';
        scheduleDraw();
    }
    function applyAt(point, tool = state.tool) {
        const doc = state.doc;
        if (!doc || point.x < 0 || point.y < 0 || point.x >= doc.width || point.y >= doc.height) return;
        const key = tool + ':' + ((tool === 'raise' || tool === 'lower') ? point.cornerX + ':' + point.cornerY : point.x + ':' + point.y);
        if (state.drag && state.drag.visited.has(key)) return;
        if (state.drag) state.drag.visited.add(key);
        try {
            if (tool === 'place' && state.selected) {
                const variant = model.pieceVariant(state.selected, state.rotation);
                doc.place(variant.piece.id, point.x, point.y, Number($('road-surface').value), variant.rotation);
            }
            else if (tool === 'erase') doc.erase(point.x, point.y);
            else if (tool === 'raise' || tool === 'lower') doc.raise(point.cornerX, point.cornerY, tool === 'raise' ? 1 : -1);
            else if (tool === 'paint') doc.paint(point.x, point.y, Number($('terrain-surface').value), 0);
            else if (tool === 'water-corner') doc.paint(point.x, point.y, Number($('terrain-surface').value), state.rotation + 1);
            else if (tool === 'level') {
                const level = Number($('terrain-level').value);
                for (const [x, y] of [[point.x, point.y], [point.x + 1, point.y], [point.x + 1, point.y + 1], [point.x, point.y + 1]]) {
                    let old = doc.vertices[y * (doc.width + 1) + x];
                    while (old !== level) {
                        doc.raise(x, y, level > old ? 1 : -1);
                        old = doc.vertices[y * (doc.width + 1) + x];
                    }
                }
            }
            scheduleDraw();
        } catch (error) { message(error.message, true); }
    }
    function finishDrag(cancel = false) {
        if (!state.drag) return;
        const drag = state.drag; state.drag = null;
        if (drag.editing) {
            if (cancel) state.doc.cancelTransaction(); else state.doc.endTransaction();
            refreshDocument();
        }
        canvas.style.cursor = state.tool === 'pan' ? 'grab' : 'crosshair';
    }
    function updateNotesCount() {
        const text = $('track-notes').value;
        const invalid = [...text].some(c => { const n = c.charCodeAt(0); return n !== 9 && n !== 10 && n !== 13 && (n < 32 || n > 126); });
        $('notes-length').textContent = text.length + ' / 8000';
        $('notes-error').textContent = invalid ? 'Use ASCII letters, numbers, punctuation, tabs, and line breaks.' : text.length > 8000 ? 'Notes may contain at most 8000 characters.' : '';
        $('apply-notes').disabled = invalid || text.length > 8000;
    }
    function checkTrack() {
        const issues = state.doc.validate(), container = $('check-results'); container.replaceChildren();
        const intro = document.createElement('p'); intro.textContent = 'Checks terrain, piece placement, surfaces, and track data. Route connectivity is checked by the game.'; container.append(intro);
        if (!issues.length) { const item = document.createElement('div'); item.className = 'check-item'; item.textContent = 'Track data is valid. ' + state.doc.objects.length + ' elements across ' + state.doc.width + ' × ' + state.doc.height + ' tiles.'; container.append(item); }
        for (const issue of issues) { const item = document.createElement('div'); item.className = 'check-item error'; item.textContent = issue; container.append(item); }
        const start = state.doc.objects.filter(o => catalogEntry(o.id)?.physical === 0);
        if (start.length !== 1) { const item = document.createElement('div'); item.className = 'check-item warning'; item.textContent = start.length ? 'The track has more than one start / finish element.' : 'Add a start / finish element before racing.'; container.append(item); }
        showDialog('check-dialog');
    }

    function installEvents() {
        document.querySelectorAll('[data-surface-for]').forEach(button => button.addEventListener('click', () => {
            const control = $(button.dataset.surfaceFor);
            control.value = button.dataset.value;
            control.dispatchEvent(new Event('change', { bubbles: true }));
        }));
        document.querySelectorAll('[data-level]').forEach(button => button.addEventListener('click', () => {
            $('terrain-level').value = button.dataset.level;
            $('terrain-level').dispatchEvent(new Event('input', { bubbles: true }));
        }));
        $('choose-folder').addEventListener('click', chooseFolder);
        $('folder-button').addEventListener('click', () => showDialog('folder-dialog'));
        $('use-fallback').addEventListener('click', enableFallback);
        $('restore-folder').addEventListener('click', reconnectFolder);
        $('folder-input').addEventListener('change', event => {
            const files = new Map();
            for (const file of event.target.files) {
                const relative = file.webkitRelativePath || file.name;
                if (relative.split('/').length <= 2) files.set(file.name, file);
            }
            const first = event.target.files[0], name = first ? (first.webkitRelativePath || '').split('/')[0] || 'selected folder' : 'selected folder';
            if (files.size) loadFolder(files, null, name);
        });
        $('folder-dialog').addEventListener('cancel', event => { if (!state.ready) event.preventDefault(); });
        document.querySelectorAll('[data-close]').forEach(button => button.addEventListener('click', () => closeDialog(button.dataset.close)));
        document.querySelectorAll('[data-tool]').forEach(button => button.addEventListener('click', () => updateTool(button.dataset.tool)));
        const panelButtons = Array.from(document.querySelectorAll('[data-editor-panel]'));
        panelButtons.forEach(button => {
            button.addEventListener('click', () => selectPanel(button.dataset.editorPanel));
            button.addEventListener('keydown', event => {
                if (!['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(event.key)) return;
                event.preventDefault(); event.stopPropagation();
                const index = event.key === 'Home' ? 0 : event.key === 'End' ? panelButtons.length - 1
                    : (panelButtons.indexOf(button) + (event.key === 'ArrowRight' ? 1 : -1) + panelButtons.length) % panelButtons.length;
                panelButtons[index].focus(); selectPanel(panelButtons[index].dataset.editorPanel);
            });
        });
        document.querySelectorAll('[data-piece-direction]').forEach(button => button.addEventListener('click', () => setDirection(Number(button.dataset.pieceDirection))));
        $('terrain-level').addEventListener('input', () => { $('level-output').textContent = $('terrain-level').value + ' / 4'; syncVisualControls(); });
        $('terrain-surface').addEventListener('change', () => { syncVisualControls(); if (!['paint', 'water-corner'].includes(state.tool)) updateTool('paint'); });
        $('road-surface').addEventListener('change', () => { renderPalette(); updateTool('place'); });
        $('piece-group').addEventListener('change', () => { state.group = Number($('piece-group').value); renderPalette(); updateTool('place'); });
        $('undo').addEventListener('click', () => { finishDrag(); state.doc.undo(); refreshDocument(); message('Undid the last edit.'); });
        $('redo').addEventListener('click', () => { finishDrag(); state.doc.redo(); refreshDocument(); message('Redid the last edit.'); });
        $('new-track').addEventListener('click', () => { if (canDiscard()) { showDialog('new-dialog'); $('new-name').select(); } });
        $('new-form').addEventListener('submit', event => { event.preventDefault(); try { const doc = new model.Document(Number($('new-width').value), Number($('new-height').value)); setDocument(doc, $('new-name').value.trim() || 'Untitled'); closeDialog('new-dialog'); message('New track ready.'); } catch (error) { message(error.message, true); } });
        $('load-track').addEventListener('click', () => $('track-file-input').click());
        $('track-file-input').addEventListener('change', event => { loadTrack(event.target.files[0]); event.target.value = ''; });
        $('save-track').addEventListener('click', openSave); $('save-form').addEventListener('submit', saveTrack);
        $('notes-button').addEventListener('click', () => { $('track-notes').value = state.doc.notes; updateNotesCount(); showDialog('notes-dialog'); });
        $('track-notes').addEventListener('input', updateNotesCount);
        $('notes-form').addEventListener('submit', event => { event.preventDefault(); try { state.doc.setNotes($('track-notes').value); refreshDocument(); closeDialog('notes-dialog'); message('Updated track notes.'); } catch (error) { $('notes-error').textContent = error.message; } });
        $('check-track').addEventListener('click', checkTrack);
        $('fit-map').addEventListener('click', fitMap);
        $('zoom-in').addEventListener('click', () => zoom(1.25)); $('zoom-out').addEventListener('click', () => zoom(.8));
        $('show-grid').addEventListener('change', scheduleDraw); $('show-heights').addEventListener('change', scheduleDraw);
        canvas.addEventListener('contextmenu', event => event.preventDefault());
        canvas.addEventListener('wheel', event => { event.preventDefault(); const p = pointAt(event); zoom(Math.exp(-event.deltaY * .0015), p.px, p.py); }, { passive: false });
        canvas.addEventListener('pointerdown', event => {
            if (!state.ready || state.loading) return;
            event.preventDefault(); canvas.focus({ preventScroll: true });
            const p = pointAt(event), panning = event.button === 1 || state.space || state.tool === 'pan';
            finishDrag();
            state.drag = { id: event.pointerId, panning, x: event.clientX, y: event.clientY, visited: new Set(), editing: !panning, tool: event.button === 2 ? 'erase' : state.tool };
            canvas.setPointerCapture(event.pointerId);
            if (panning) canvas.style.cursor = 'grabbing';
            else { state.doc.beginTransaction(state.drag.tool === 'place' ? 'Place track pieces' : 'Edit ' + state.drag.tool); applyAt(p, state.drag.tool); }
            hoverAt(p);
        });
        canvas.addEventListener('pointermove', event => {
            const drag = state.drag;
            if (drag && drag.id === event.pointerId) {
                if (drag.panning) { view.x += event.clientX - drag.x; view.y += event.clientY - drag.y; drag.x = event.clientX; drag.y = event.clientY; }
                else applyAt(pointAt(event), drag.tool);
            }
            hoverAt(pointAt(event));
        });
        canvas.addEventListener('pointerup', () => finishDrag());
        canvas.addEventListener('pointercancel', () => finishDrag(true));
        canvas.addEventListener('lostpointercapture', () => finishDrag());
        canvas.addEventListener('pointerleave', () => { if (!state.drag) { state.hover = null; scheduleDraw(); } });
        minimap.addEventListener('pointerdown', event => {
            if (!state.doc) return;
            const rect = minimap.getBoundingClientRect(), px = (event.clientX - rect.left) / rect.width * minimap.width, py = (event.clientY - rect.top) / rect.height * minimap.height;
            const scale = Math.min(minimap.width / state.doc.width, minimap.height / state.doc.height), ox = (minimap.width - state.doc.width * scale) / 2, oy = (minimap.height - state.doc.height * scale) / 2;
            view.x = view.width / 2 - (px - ox) / scale * baseTile * view.zoom; view.y = view.height / 2 - (py - oy) / scale * baseTile * view.zoom; scheduleDraw();
        });
        document.addEventListener('keydown', event => {
            if (!state.ready || editableTarget(event.target) || document.querySelector('dialog[open]')) return;
            if (event.target.closest('button') && (event.key === 'Enter' || event.code === 'Space')) return;
            const ctrl = event.ctrlKey || event.metaKey, key = event.key.toLowerCase();
            if (ctrl && key === 's') { event.preventDefault(); finishDrag(); openSave(); }
            else if (ctrl && key === 'o') { event.preventDefault(); $('track-file-input').click(); }
            else if (ctrl && (key === 'z' || key === 'y')) { event.preventDefault(); finishDrag(); if (key === 'y' || event.shiftKey) state.doc.redo(); else state.doc.undo(); refreshDocument(); }
            else if (event.code === 'Space') { event.preventDefault(); state.space = true; canvas.style.cursor = 'grab'; }
            else if (key === 'r' || key === 'q') { event.preventDefault(); rotate(key === 'r' ? 1 : -1); }
            else if (key === 'b') updateTool('place'); else if (key === 'e') updateTool('erase'); else if (key === 'h') updateTool('pan');
            else if (/^F([1-9]|10)$/.test(event.key)) {
                const group = Number(event.key.slice(1));
                if (model.groups.some(g => g.id === group) && group !== 2 && group !== 3) { event.preventDefault(); state.group = group; $('piece-group').value = group; renderPalette(); updateTool('place'); }
            } else if (key === '+' || key === '=') { event.preventDefault(); zoom(1.25); } else if (key === '-') { event.preventDefault(); zoom(.8); }
            else if (event.key.startsWith('Arrow')) {
                event.preventDefault();
                state.cursor.x = Math.max(0, Math.min(state.doc.width - 1, state.cursor.x + (event.key === 'ArrowRight' ? 1 : event.key === 'ArrowLeft' ? -1 : 0)));
                state.cursor.y = Math.max(0, Math.min(state.doc.height - 1, state.cursor.y + (event.key === 'ArrowDown' ? 1 : event.key === 'ArrowUp' ? -1 : 0)));
                const size = baseTile * view.zoom, x = view.x + state.cursor.x * size, y = view.y + state.cursor.y * size;
                if (x < 0 || x + size > view.width) view.x = view.width / 2 - (state.cursor.x + .5) * size;
                if (y < 0 || y + size > view.height) view.y = view.height / 2 - (state.cursor.y + .5) * size;
                hoverAt({ ...state.cursor, cornerX: state.cursor.x, cornerY: state.cursor.y });
            } else if (key === 'enter' || key === 'delete' || key === 'backspace') {
                event.preventDefault(); state.doc.transaction('Edit tile', () => applyAt({ ...state.cursor, cornerX: state.cursor.x, cornerY: state.cursor.y }, key === 'enter' ? state.tool : 'erase')); refreshDocument();
            } else if (key === 'escape') { finishDrag(true); state.hover = null; scheduleDraw(); }
        });
        document.addEventListener('keyup', event => { if (event.code === 'Space') { state.space = false; canvas.style.cursor = state.tool === 'pan' ? 'grab' : 'crosshair'; } });
        window.addEventListener('blur', () => { state.space = false; finishDrag(); });
        window.addEventListener('beforeunload', event => { if (state.dirty) { event.preventDefault(); event.returnValue = ''; } });
        const container = $('canvas-container');
        container.addEventListener('dragover', event => { if (state.ready) { event.preventDefault(); event.dataTransfer.dropEffect = 'copy'; } });
        container.addEventListener('drop', event => { event.preventDefault(); if (state.ready) loadTrack(event.dataTransfer.files[0]); });
        if (window.ResizeObserver) new ResizeObserver(resizeCanvas).observe(container); else window.addEventListener('resize', resizeCanvas);
    }
    function start() {
        installEvents(); resizeCanvas(); showDialog('folder-dialog');
        model = window.TrackEditorModel;
        if (!model || !window.TrackEditorResources) {
            folderMessage('Editor files could not be loaded. Keep the HTML, CSS, and JavaScript editor files together.');
            $('choose-folder').disabled = true; $('folder-input').disabled = true;
            return;
        }
        buildGroups(); syncVisualControls(); showPanel(state.panel);
        if (!window.showDirectoryPicker) enableFallback();
        if (window.showDirectoryPicker) rememberFolder().then(folder => {
            if (!folder || !folder.requestPermission) return;
            state.remembered = folder; $('restore-folder').hidden = false; $('restore-folder-label').textContent = 'Reconnect ' + folder.name;
        }).catch(() => {});
    }
    start();
}());
