/* The browser audio runtime has its own WASM memory. Its worklet and worker
 * exchange PCM directly, so the page never relays time-sensitive audio. */
function createRestuntsBrowserAudio(module, sources) {
    const SAMPLE_RATE = 44100;
    const CHANNELS = 2;
    const TICK_FRAMES = 441;
    const QUEUED_TICKS = 4;
    const FILE_COMMAND = 37;
    const FILE_NOT_FOUND = 44; // Emscripten's virtual filesystem errno.
    let session = null;
    let terminalFailure = null;
    let sequence = 0;
    const state = { running: false, frames: 0, underruns: 0, queuedFrames: 0 };

    function objectUrl(source) {
        return URL.createObjectURL(new Blob([source], { type: 'text/javascript' }));
    }

    function rejectRequests(current, error) {
        if (current.readyReject) current.readyReject(error);
        current.readyResolve = null;
        current.readyReject = null;
        for (const request of current.requests.values()) request.reject(error);
        current.requests.clear();
    }

    async function close(current, error) {
        if (current.closed) return;
        current.closed = true;
        current.error = error;
        rejectRequests(current, error);
        if (current.node) {
            current.node.port.postMessage({ type: 'close' });
            current.node.disconnect();
            current.node.port.close();
        }
        if (current.worker) current.worker.terminate();
        if (session === current) state.running = false;
        if (current.context && current.context.state !== 'closed') await current.context.close();
    }

    function fail(current, error) {
        if (session !== current || current.closed) return;
        const failure = error instanceof Error ? error : new Error(String(error));
        terminalFailure = failure;
        close(current, failure).catch(() => {});
        if (typeof api.onError === 'function') api.onError(failure);
        else if (typeof module.printErr === 'function') module.printErr(failure.message);
    }

    function checkCurrent(current) {
        if (session !== current || current.closed) throw new Error('Game audio closed.');
    }

    async function initialize(current) {
        if (!sources || !sources.runtime || !sources.worker || !sources.worklet) {
            throw new Error('The game audio files are missing.');
        }
        const AudioContextClass = globalThis.AudioContext || globalThis.webkitAudioContext;
        if (!AudioContextClass) throw new Error('This browser cannot play game audio.');
        const context = new AudioContextClass({ sampleRate: SAMPLE_RATE, latencyHint: 'interactive' });
        current.context = context;
        if (!context.audioWorklet) throw new Error('This browser cannot play game audio.');
        context.onstatechange = () => {
            if (session === current && !current.closed) state.running = context.state === 'running';
        };
        // Start inside the user's click before awaiting module compilation.
        const resumed = context.resume();
        resumed.catch(() => {});
        const workletUrl = objectUrl(sources.worklet);
        try {
            await context.audioWorklet.addModule(workletUrl);
        } finally {
            URL.revokeObjectURL(workletUrl);
        }
        checkCurrent(current);
        const node = new AudioWorkletNode(context, 'restunts-audio', {
            numberOfInputs: 0,
            numberOfOutputs: 1,
            outputChannelCount: [CHANNELS],
            processorOptions: { sourceRate: SAMPLE_RATE, channels: CHANNELS,
                tickFrames: TICK_FRAMES, queuedTicks: QUEUED_TICKS }
        });
        current.node = node;
        node.port.onmessage = event => {
            if (session !== current || current.closed) return;
            if (event.data.type === 'stats') Object.assign(state, {
                frames: event.data.frames, underruns: event.data.underruns,
                queuedFrames: event.data.queuedFrames
            });
            else if (event.data.type === 'error') fail(current, event.data.error);
        };
        node.onprocessorerror = () => fail(current, 'Game audio playback stopped.');
        const ready = new Promise((resolve, reject) => {
            current.readyResolve = resolve;
            current.readyReject = reject;
        });
        // A constructor failure below can reject this before we await it.
        ready.catch(() => {});
        const workerUrl = objectUrl(sources.runtime + '\n' + sources.worker);
        let worker;
        try {
            worker = new Worker(workerUrl);
            current.worker = worker;
        } finally {
            URL.revokeObjectURL(workerUrl);
        }
        worker.onerror = event => fail(current, event.message || 'Game audio stopped.');
        worker.onmessageerror = () => fail(current, 'Cannot receive game audio.');
        worker.onmessage = event => {
            if (session !== current || current.closed) return;
            const message = event.data;
            if (message.type === 'ready') {
                if (current.readyResolve) current.readyResolve();
                current.readyResolve = null;
                current.readyReject = null;
            } else if (message.type === 'response') {
                const request = current.requests.get(message.id);
                if (!request) return;
                current.requests.delete(message.id);
                module['lastAudioResponse'] = message;
                if (message.ok) request.resolve(message.result);
                else request.reject(new Error(message.error || 'Game audio command failed.'));
            } else if (message.type === 'error') {
                fail(current, message.error);
            } else if (message.type === 'finished') {
                module['restuntsAudioFinished'].push(message.event);
            } else if (message.type === 'log' && typeof module.printErr === 'function') {
                module.printErr(message.message);
            }
        };
        const connection = new MessageChannel();
        worker.postMessage({ type: 'initialize', port: connection.port1,
            sampleRate: SAMPLE_RATE, channels: CHANNELS, tickFrames: TICK_FRAMES },
            [connection.port1]);
        node.port.postMessage({ type: 'connect', port: connection.port2 }, [connection.port2]);
        node.connect(context.destination);
        await Promise.all([ready, resumed]);
        checkCurrent(current);
        state.running = context.state === 'running';
    }

    function start() {
        if (terminalFailure) return Promise.reject(terminalFailure);
        if (session) return session.starting;
        const current = { context: null, node: null, worker: null, closed: false,
            readyResolve: null, readyReject: null, requests: new Map(), files: new Set(), starting: null };
        session = current;
        module['restuntsAudioFinished'] = [];
        Object.assign(state, { running: false, frames: 0, underruns: 0, queuedFrames: 0 });
        current.starting = initialize(current).catch(async error => {
            const failure = current.closed && current.error ? current.error : error;
            if (session === current && !current.closed) terminalFailure = failure;
            await close(current, failure);
            throw failure;
        });
        return current.starting;
    }

    async function command(op, args, resources, text) {
        if (op && typeof op === 'object') ({ op, args, resources, text } = op);
        await start();
        if (terminalFailure) throw terminalFailure;
        const current = session;
        if (!current) throw new Error('Game audio closed.');
        checkCurrent(current);
        const id = ++sequence;
        const transfers = [];
        const ownedResources = (resources || []).map(resource => {
            const original = resource.bytes instanceof ArrayBuffer ?
                new Uint8Array(resource.bytes) : resource.bytes;
            const bytes = Uint8Array.from(original || []);
            transfers.push(bytes.buffer);
            return { ...resource, bytes: bytes.buffer };
        });
        return new Promise((resolve, reject) => {
            current.requests.set(id, { resolve, reject });
            try {
                current.worker.postMessage({ type: 'command', id, op, args, text,
                    resources: ownedResources }, transfers);
            } catch (error) {
                current.requests.delete(id);
                reject(error);
            }
        });
    }

    async function prepareFiles(romDirectory) {
        await start();
        const current = session;
        checkCurrent(current);
        const filesystem = module.FS;
        if (!filesystem) throw new Error('Cannot read game audio files.');

        function entries(directory) {
            try {
                return filesystem.readdir(directory).filter(name => name !== '.' && name !== '..').sort();
            } catch (error) {
                // Optional replacement music and MT-32 banks may be absent.
                if (error.code === 'ENOENT' || error.errno === FILE_NOT_FOUND) return [];
                throw error;
            }
        }

        function child(directory, name) {
            return directory.replace(/\/$/, '') + '/' + name;
        }

        async function copy(source, target) {
            checkCurrent(current);
            if (current.files.has(source)) return;
            const bytes = filesystem.readFile(source);
            await command(FILE_COMMAND, [], [{ id: 0, slot: 0, bytes }], target);
            current.files.add(source);
        }

        const directory = filesystem.cwd();
        const bank = entries(directory).find(name => name.toLowerCase() === 'mt32.plb');
        if (bank && filesystem.isFile(filesystem.stat(child(directory, bank)).mode)) {
            await copy(child(directory, bank), '/MT32.PLB');
        }
        for (const name of entries('/assets/music')) {
            const source = child('/assets/music', name);
            if (name.toLowerCase().endsWith('.ogg') && filesystem.isFile(filesystem.stat(source).mode)) {
                await copy(source, '/assets/music/' + name);
            }
        }
        async function copyRoms(source, relative) {
            for (const name of entries(source)) {
                const path = child(source, name);
                const target = relative ? relative + '/' + name : name;
                if (filesystem.isDir(filesystem.stat(path).mode)) await copyRoms(path, target);
                else if (filesystem.isFile(filesystem.stat(path).mode)) await copy(path, '/roms/' + target);
            }
        }
        if (romDirectory) await copyRoms(romDirectory, '');
    }

    async function shutdown() {
        const previous = session;
        session = null;
        terminalFailure = null;
        module['restuntsAudioFinished'] = [];
        state.running = false;
        if (previous) await close(previous, new Error('Game audio closed.'));
    }

    const api = { start, command, prepareFiles, shutdown, state, onError: null };
    return api;
}

if (typeof Module !== 'undefined') {
    Module['restuntsBrowserAudio'] = createRestuntsBrowserAudio(Module, Module['restuntsAudioSources']);
}
