/* This processor only consumes owned PCM buffers. It never accesses game state,
 * allocates WASM memory, decodes music, or synthesizes instruments. */
class RestuntsAudioProcessor extends AudioWorkletProcessor {
    constructor(options) {
        super();
        const settings = options.processorOptions;
        this.channels = settings.channels;
        this.tickFrames = settings.tickFrames;
        this.queuedTicks = settings.queuedTicks;
        this.step = settings.sourceRate / sampleRate;
        this.queue = [];
        this.offset = 0;
        this.fraction = 0;
        this.queuedFrames = 0;
        this.frames = 0;
        this.underruns = 0;
        this.reportFrames = 0;
        this.started = false;
        this.closed = false;
        this.source = null;
        this.port.onmessage = event => {
            if (event.data.type === 'connect') {
                this.source = event.data.port;
                this.source.onmessage = message => this.receive(message.data);
                this.source.postMessage({ type: 'need', count: this.queuedTicks });
            } else if (event.data.type === 'close') {
                this.closed = true;
                this.queue.length = 0;
                if (this.source) this.source.close();
            } else if (event.data.type === 'stats') {
                this.report(event.data.request);
            }
        };
    }

    receive(message) {
        if (message.type === 'clear') {
            const buffers = this.queue.map(chunk => chunk.samples.buffer);
            const count = buffers.length;
            this.queue.length = 0;
            this.offset = 0;
            this.fraction = 0;
            this.queuedFrames = 0;
            this.started = false;
            if (count) this.source.postMessage({ type: 'need', count, buffers }, buffers);
        } else if (message.type === 'pcm') {
            if (message.frames !== this.tickFrames ||
                message.buffer.byteLength !== this.tickFrames * this.channels * 2) {
                this.port.postMessage({ type: 'error', error: 'Invalid game audio buffer.' });
                this.closed = true;
                return;
            }
            this.queue.push({ samples: new Int16Array(message.buffer), frames: message.frames });
            this.queuedFrames += message.frames;
        }
    }

    sample(channel, next) {
        let frame = this.offset + next;
        let chunk = this.queue[0];
        if (frame >= chunk.frames) {
            frame -= chunk.frames;
            chunk = this.queue[1];
        }
        return chunk.samples[frame * this.channels + channel];
    }

    consume(frames) {
        this.offset += frames;
        this.queuedFrames -= frames;
        while (this.queue.length && this.offset >= this.queue[0].frames) {
            const chunk = this.queue.shift();
            this.offset -= chunk.frames;
            const buffer = chunk.samples.buffer;
            this.source.postMessage({ type: 'need', count: 1, buffers: [buffer] }, [buffer]);
        }
    }

    report(request) {
        this.port.postMessage({ type: 'stats', frames: this.frames,
            underruns: this.underruns, queuedFrames: this.queuedFrames, request });
    }

    process(inputs, outputs) {
        if (this.closed) return false;
        const channels = outputs[0];
        if (!channels || !channels.length) return true;
        const frames = channels[0].length;
        // Initial buffering and a source change use the same bounded lead.
        if (!this.started && this.queuedFrames >= this.tickFrames * this.queuedTicks) {
            this.started = true;
        }
        if (this.started) {
            for (let frame = 0; frame < frames; frame++) {
                if (this.queuedFrames < 2) {
                    this.underruns++;
                    continue;
                }
                for (let channel = 0; channel < channels.length; channel++) {
                    const sourceChannel = Math.min(channel, this.channels - 1);
                    const first = this.sample(sourceChannel, 0);
                    const second = this.sample(sourceChannel, 1);
                    channels[channel][frame] = (first + (second - first) * this.fraction) / 32768;
                }
                this.fraction += this.step;
                const consumed = Math.min(Math.floor(this.fraction), this.queuedFrames);
                this.fraction -= consumed;
                this.consume(consumed);
            }
        }
        this.frames += frames;
        this.reportFrames += frames;
        if (this.reportFrames >= sampleRate) {
            this.reportFrames -= sampleRate;
            this.report();
        }
        return true;
    }
}

registerProcessor('restunts-audio', RestuntsAudioProcessor);
