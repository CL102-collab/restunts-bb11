/* Appended to the modularized single-file audio runtime inside a Blob Worker.
 * Resource pointers belong to this module and are never shared with gameplay. */
(() => {
    let runtime = null;
    let output = null;
    let channels = 0;
    let tickFrames = 0;
    let failed = false;
    const encoder = new TextEncoder();

    function fail(error) {
        if (failed) return;
        failed = true;
        self.postMessage({ type: 'error', error: error && error.message || String(error) });
    }

    function render(count, recycled) {
        if (!runtime || failed) return;
        try {
            for (let index = 0; index < count; index++) {
                const size = tickFrames * channels;
                const buffer = recycled && recycled[index] && recycled[index].byteLength === size * 2 ?
                    recycled[index] : new ArrayBuffer(size * 2);
                const pcm = new Int16Array(buffer);
                const pointer = runtime._audio_render_tick();
                if (pointer) pcm.set(runtime.HEAP16.subarray(pointer >> 1, (pointer >> 1) + size));
                else pcm.fill(0);
                output.postMessage({ type: 'pcm', buffer, frames: tickFrames }, [buffer]);
                let finished;
                while ((finished = runtime._audio_pop_finish() >>> 0) !== 0) {
                    self.postMessage({ type: 'finished', event: finished });
                }
            }
        } catch (error) {
            fail(error);
        }
    }

    function command(message) {
        const allocations = [];
        let responsePointer = 0;
        try {
            const resources = (message.resources || []).map(resource => {
                const bytes = new Uint8Array(resource.bytes);
                const pointer = runtime._malloc(Math.max(bytes.length, 1));
                if (!pointer) throw new Error('Cannot allocate game audio data.');
                allocations.push(pointer);
                runtime.HEAPU8.set(bytes, pointer);
                const descriptor = { ...resource, pointer, length: bytes.length };
                delete descriptor.bytes;
                return descriptor;
            });
            const payload = encoder.encode(JSON.stringify({ id: message.id, op: message.op,
                args: message.args, text: message.text, resources }));
            const pointer = runtime._malloc(payload.length + 1);
            if (!pointer) throw new Error('Cannot allocate a game audio command.');
            allocations.push(pointer);
            runtime.HEAPU8.set(payload, pointer);
            runtime.HEAPU8[pointer + payload.length] = 0;
            responsePointer = runtime._audio_command(pointer, payload.length);
            const response = responsePointer ? JSON.parse(runtime.UTF8ToString(responsePointer)) : { ok: true };
            if (response.flush) output.postMessage({ type: 'clear' });
            self.postMessage({ ...response, type: 'response', id: message.id });
        } catch (error) {
            self.postMessage({ type: 'response', id: message.id, ok: false,
                error: error && error.message || String(error) });
        } finally {
            if (responsePointer) runtime._audio_response_free(responsePointer);
            for (const pointer of allocations) runtime._free(pointer);
        }
    }

    self.onmessage = async event => {
        const message = event.data;
        if (message.type === 'initialize') {
            try {
                channels = message.channels;
                tickFrames = message.tickFrames;
                output = message.port;
                runtime = await createRestuntsAudioRuntime({
                    noInitialRun: true,
                    print: text => self.postMessage({ type: 'log', message: text }),
                    printErr: text => self.postMessage({ type: 'log', message: text }),
                    onAbort: fail
                });
                output.onmessage = request => {
                    if (request.data.type === 'need') render(request.data.count, request.data.buffers);
                };
                self.postMessage({ type: 'ready' });
            } catch (error) {
                fail(error);
            }
        } else if (message.type === 'command') {
            if (!runtime || failed) {
                self.postMessage({ type: 'response', id: message.id, ok: false,
                    error: 'Game audio is unavailable.' });
            } else {
                command(message);
            }
        }
    };
})();
