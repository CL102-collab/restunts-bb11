/* A separate browser realm owns its sequencer, instruments and PCM streams.
 * Commands borrow input bytes only until their acknowledged execution ends. */
#include "../sdl3/audio_commands.h"
#include "../sdl3/audio_resources_worker.h"
#include "../sdl3/audio_worker.h"
#include "../../c/platform.h"
#include "../../c/memmgr.h"
#include <SDL3/SDL.h>
#include <emscripten/heap.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define RUNTIME_CHANNELS 2U
#define RUNTIME_STREAM_CAPACITY 8U
#define RUNTIME_RING_FRAMES 32768U
#define RUNTIME_MIN_RATE 8000
#define RUNTIME_MAX_RATE 192000
#define RUNTIME_JSON_BYTES 65536U
#define RUNTIME_JSON_DEPTH 16U
#define RUNTIME_JSON_KEY_BYTES 64U
#define RUNTIME_RESPONSE_BYTES 2048U
#define RUNTIME_ERROR_BYTES 256U
#define RUNTIME_ERROR_ESCAPE_BYTES (RUNTIME_ERROR_BYTES * 6U + 1U)
#define RUNTIME_UNICODE_DIGITS 4U
#define RUNTIME_UNICODE_BASE 16U
#define RUNTIME_UTF8_CONTINUATION 0x80U
#define RUNTIME_UTF8_MASK 0x3fU
#define RUNTIME_UNICODE_HIGH_FIRST 0xd800U
#define RUNTIME_UNICODE_HIGH_LAST 0xdbffU
#define RUNTIME_UNICODE_LOW_FIRST 0xdc00U
#define RUNTIME_UNICODE_LOW_LAST 0xdfffU
#define RUNTIME_UNICODE_SURROGATE_BITS 10U
#define RUNTIME_UNICODE_SUPPLEMENT 0x10000U

struct RUNTIME_JSON {
	const legacy_char *at;
	const legacy_char *end;
	legacy_u8 failed;
};

struct RUNTIME_STREAM {
	SDL_AudioSpec input;
	legacy_s16 *samples;
	legacy_u32 read;
	legacy_u32 queued;
	legacy_f64 phase;
	legacy_f32 gain;
	legacy_u8 paused;
};

static struct RUNTIME_STREAM *runtime_streams[RUNTIME_STREAM_CAPACITY];
static sdl3_audio_worker_callback runtime_callbacks[SDL3_AUDIO_WORKER_CALLBACK_CAPACITY];
static struct SDL3_AUDIO_WORKER_STATS runtime_statistics;
static legacy_s16 runtime_pcm[SDL3_AUDIO_WORKER_TICK_FRAMES * RUNTIME_CHANNELS];
static legacy_u8 runtime_configured;
static legacy_u8 runtime_suspended;
static legacy_u32 runtime_audio_references;
static legacy_u32 runtime_timer_origin;
static legacy_u32 runtime_timer_last;
static legacy_char runtime_startup_error[RUNTIME_ERROR_BYTES];
static legacy_char runtime_no_memory[] =
	"{\"ok\":false,\"error\":\"Cannot allocate audio command response\"}";
legacy_s32 sdl3_batch_mode;

extern void sdl3_audio_update(void);
extern bool SDLCALL __real_SDL_InitSubSystem(SDL_InitFlags flags);
extern void SDLCALL __real_SDL_QuitSubSystem(SDL_InitFlags flags);
extern SDL_InitFlags SDLCALL __real_SDL_WasInit(SDL_InitFlags flags);

static void runtime_json_space(struct RUNTIME_JSON *json)
{
	while (json->at < json->end &&
		   (*json->at == ' ' || *json->at == '\t' || *json->at == '\r' || *json->at == '\n')) {
		json->at++;
	}
}

static legacy_u8 runtime_json_take(struct RUNTIME_JSON *json, legacy_char expected)
{
	runtime_json_space(json);
	if (json->at == json->end || *json->at != expected) {
		json->failed = 1;
		return 0;
	}
	json->at++;
	return 1;
}

static void runtime_json_append(struct RUNTIME_JSON *json, legacy_char *text, size_t capacity,
								size_t *used, legacy_u32 value)
{
	if (text != NULL) {
		if (*used + 1U >= capacity) {
			json->failed = 1;
			return;
		}
		text[*used] = (legacy_char)value;
	}
	(*used)++;
}

static legacy_u32 runtime_json_hex(struct RUNTIME_JSON *json)
{
	legacy_u32 value = 0;
	for (legacy_u32 digit = 0; digit < RUNTIME_UNICODE_DIGITS; digit++) {
		if (json->at == json->end) {
			json->failed = 1;
			return 0;
		}
		legacy_u8 byte = (legacy_u8)*json->at++;
		legacy_u32 part;
		if (byte >= '0' && byte <= '9') {
			part = byte - '0';
		} else if (byte >= 'a' && byte <= 'f') {
			part = byte - 'a' + 10U;
		} else if (byte >= 'A' && byte <= 'F') {
			part = byte - 'A' + 10U;
		} else {
			json->failed = 1;
			return 0;
		}
		value = value * RUNTIME_UNICODE_BASE + part;
	}
	return value;
}

static void runtime_json_utf8(struct RUNTIME_JSON *json, legacy_char *text, size_t capacity,
							  size_t *used, legacy_u32 value)
{
	if (value < 0x80U) {
		runtime_json_append(json, text, capacity, used, value);
	} else if (value < 0x800U) {
		runtime_json_append(json, text, capacity, used, 0xc0U | (value >> 6U));
		runtime_json_append(json, text, capacity, used,
							RUNTIME_UTF8_CONTINUATION | (value & RUNTIME_UTF8_MASK));
	} else {
		if (value >= RUNTIME_UNICODE_SUPPLEMENT) {
			runtime_json_append(json, text, capacity, used, 0xf0U | (value >> 18U));
			runtime_json_append(json, text, capacity, used,
								RUNTIME_UTF8_CONTINUATION | ((value >> 12U) & RUNTIME_UTF8_MASK));
		} else {
			runtime_json_append(json, text, capacity, used, 0xe0U | (value >> 12U));
		}
		runtime_json_append(json, text, capacity, used,
							RUNTIME_UTF8_CONTINUATION | ((value >> 6U) & RUNTIME_UTF8_MASK));
		runtime_json_append(json, text, capacity, used,
							RUNTIME_UTF8_CONTINUATION | (value & RUNTIME_UTF8_MASK));
	}
}

static legacy_u8 runtime_json_string(struct RUNTIME_JSON *json, legacy_char *text, size_t capacity)
{
	if (!runtime_json_take(json, '"')) {
		return 0;
	}
	size_t used = 0;
	while (json->at < json->end && !json->failed) {
		legacy_u8 byte = (legacy_u8)*json->at++;
		if (byte == '"') {
			if (text != NULL) {
				text[used] = 0;
			}
			return 1;
		}
		if (byte < ' ') {
			break;
		}
		if (byte != '\\') {
			runtime_json_append(json, text, capacity, &used, byte);
			continue;
		}
		if (json->at == json->end) {
			break;
		}
		byte = (legacy_u8)*json->at++;
		if (byte == 'u') {
			legacy_u32 value = runtime_json_hex(json);
			if (value >= RUNTIME_UNICODE_HIGH_FIRST && value <= RUNTIME_UNICODE_HIGH_LAST) {
				if (json->end - json->at < 2 || json->at[0] != '\\' || json->at[1] != 'u') {
					break;
				}
				json->at += 2;
				legacy_u32 low = runtime_json_hex(json);
				if (low < RUNTIME_UNICODE_LOW_FIRST || low > RUNTIME_UNICODE_LOW_LAST) {
					break;
				}
				value = RUNTIME_UNICODE_SUPPLEMENT +
						((value - RUNTIME_UNICODE_HIGH_FIRST) << RUNTIME_UNICODE_SURROGATE_BITS) +
						low - RUNTIME_UNICODE_LOW_FIRST;
			} else if (value >= RUNTIME_UNICODE_LOW_FIRST && value <= RUNTIME_UNICODE_LOW_LAST) {
				break;
			}
			/* C-facing paths and resource names cannot contain embedded NULs. */
			if (value == 0) {
				break;
			}
			runtime_json_utf8(json, text, capacity, &used, value);
		} else {
			const legacy_char *escapes = "\"\\/bfnrt";
			const legacy_char values[] = {'"', '\\', '/', '\b', '\f', '\n', '\r', '\t'};
			const legacy_char *found = byte != 0 ? strchr(escapes, byte) : NULL;
			if (found == NULL) {
				break;
			}
			runtime_json_append(json, text, capacity, &used, values[found - escapes]);
		}
	}
	json->failed = 1;
	return 0;
}

static legacy_u64 runtime_json_integer(struct RUNTIME_JSON *json, legacy_u8 *negative)
{
	runtime_json_space(json);
	*negative = json->at < json->end && *json->at == '-';
	json->at += *negative;
	if (json->at == json->end || *json->at < '0' || *json->at > '9') {
		json->failed = 1;
		return 0;
	}
	legacy_u8 leading_zero = *json->at == '0';
	legacy_u64 value = 0;
	size_t digits = 0;
	while (json->at < json->end && *json->at >= '0' && *json->at <= '9') {
		legacy_u32 digit = (legacy_u32)(*json->at++ - '0');
		if ((leading_zero && digits != 0) || value > (~(legacy_u64)0 - digit) / 10U) {
			json->failed = 1;
			return 0;
		}
		value = value * 10U + digit;
		digits++;
	}
	return value;
}

static void runtime_json_skip(struct RUNTIME_JSON *json, legacy_u32 depth)
{
	runtime_json_space(json);
	if (depth == RUNTIME_JSON_DEPTH || json->at == json->end) {
		json->failed = 1;
		return;
	}
	legacy_char first = *json->at;
	if (first == '"') {
		runtime_json_string(json, NULL, 0);
	} else if (first == '{' || first == '[') {
		json->at++;
		legacy_char close = first == '{' ? '}' : ']';
		runtime_json_space(json);
		if (json->at < json->end && *json->at == close) {
			json->at++;
			return;
		}
		while (!json->failed) {
			if (first == '{') {
				runtime_json_string(json, NULL, 0);
				runtime_json_take(json, ':');
			}
			runtime_json_skip(json, depth + 1U);
			runtime_json_space(json);
			if (json->at < json->end && *json->at == close) {
				json->at++;
				return;
			}
			runtime_json_take(json, ',');
		}
	} else {
		/* Unknown metadata is ignored; its primitive token is still bounded. */
		const legacy_char *start = json->at;
		while (json->at < json->end && strchr(",}] \t\r\n", *json->at) == NULL) {
			json->at++;
		}
		json->failed |= json->at == start;
	}
}

static void runtime_json_comma(struct RUNTIME_JSON *json, legacy_char close)
{
	runtime_json_take(json, ',');
	runtime_json_space(json);
	if (json->at == json->end || *json->at == close) {
		json->failed = 1;
	}
}

static void runtime_json_args(struct RUNTIME_JSON *json, struct SDL3_AUDIO_COMMAND *command)
{
	runtime_json_take(json, '[');
	runtime_json_space(json);
	size_t count = 0;
	while (!json->failed && json->at < json->end && *json->at != ']') {
		legacy_u8 negative;
		legacy_u64 value = runtime_json_integer(json, &negative);
		if (count == SDL3_AUDIO_COMMAND_ARGUMENTS ||
			value > (negative ? LEGACY_U32_SIGN_BIT : LEGACY_S32_MAX)) {
			json->failed = 1;
			break;
		}
		command->args[count++] =
			negative ? LEGACY_S32_FROM_BITS((legacy_u32)(0U - value)) : (legacy_s32)value;
		runtime_json_space(json);
		if (json->at < json->end && *json->at != ']') {
			runtime_json_comma(json, ']');
		}
	}
	runtime_json_take(json, ']');
}

static void runtime_json_resources(struct RUNTIME_JSON *json, struct SDL3_AUDIO_COMMAND *command)
{
	runtime_json_take(json, '[');
	runtime_json_space(json);
	size_t count = 0;
	legacy_u8 slots = 0;
	while (!json->failed && json->at < json->end && *json->at != ']') {
		if (count == SDL3_AUDIO_COMMAND_RESOURCES || !runtime_json_take(json, '{')) {
			json->failed = 1;
			break;
		}
		legacy_u64 pointer = 0;
		legacy_u64 bytes = 0;
		legacy_u64 resource_key = 0;
		size_t slot = count;
		runtime_json_space(json);
		while (!json->failed && json->at < json->end && *json->at != '}') {
			legacy_char key[RUNTIME_JSON_KEY_BYTES];
			if (!runtime_json_string(json, key, sizeof(key)) || !runtime_json_take(json, ':')) {
				break;
			}
			if (!strcmp(key, "id") || !strcmp(key, "pointer") || !strcmp(key, "length") ||
				!strcmp(key, "slot")) {
				legacy_u8 negative;
				legacy_u64 value = runtime_json_integer(json, &negative);
				json->failed |= negative;
				if (!strcmp(key, "id")) {
					resource_key = value;
				} else if (!strcmp(key, "pointer")) {
					pointer = value;
				} else if (!strcmp(key, "length")) {
					bytes = value;
				} else {
					if (value >= SDL3_AUDIO_COMMAND_RESOURCES) {
						json->failed = 1;
						break;
					}
					slot = (size_t)value;
				}
			} else {
				runtime_json_skip(json, 0);
			}
			runtime_json_space(json);
			if (json->at < json->end && *json->at != '}') {
				runtime_json_comma(json, '}');
			}
		}
		runtime_json_take(json, '}');
		size_t heap = emscripten_get_heap_size();
		if (pointer > heap || bytes > heap - (size_t)pointer || (bytes != 0 && pointer == 0)) {
			json->failed = 1;
			break;
		}
		if ((slots & (1U << slot)) != 0) {
			json->failed = 1;
			break;
		}
		slots |= (legacy_u8)(1U << slot);
		command->keys[slot] = resource_key;
		command->data[slot] = (const void *)(size_t)pointer;
		command->bytes[slot] = (size_t)bytes;
		count++;
		runtime_json_space(json);
		if (json->at < json->end && *json->at != ']') {
			runtime_json_comma(json, ']');
		}
	}
	runtime_json_take(json, ']');
}

static legacy_u8 runtime_parse_command(const legacy_char *data, size_t bytes,
									   struct SDL3_AUDIO_COMMAND *command)
{
	struct RUNTIME_JSON json = {data, data + bytes, 0};
	legacy_u8 has_operation = 0;
	runtime_json_take(&json, '{');
	runtime_json_space(&json);
	while (!json.failed && json.at < json.end && *json.at != '}') {
		legacy_char key[RUNTIME_JSON_KEY_BYTES];
		if (!runtime_json_string(&json, key, sizeof(key)) || !runtime_json_take(&json, ':')) {
			break;
		}
		if (!strcmp(key, "op")) {
			legacy_u8 negative;
			legacy_u64 op = runtime_json_integer(&json, &negative);
			json.failed |=
				negative || has_operation || op > AUDIO_COMMAND_SET_FINISH_CALLBACK || op == 0;
			command->op = (legacy_u32)op;
			has_operation = 1;
		} else if (!strcmp(key, "args")) {
			runtime_json_args(&json, command);
		} else if (!strcmp(key, "resources")) {
			runtime_json_resources(&json, command);
		} else if (!strcmp(key, "text")) {
			runtime_json_string(&json, command->text, sizeof(command->text));
		} else {
			runtime_json_skip(&json, 0);
		}
		runtime_json_space(&json);
		if (json.at < json.end && *json.at != '}') {
			runtime_json_comma(&json, '}');
		}
	}
	runtime_json_take(&json, '}');
	runtime_json_space(&json);
	return !json.failed && has_operation && json.at == json.end;
}

static struct RUNTIME_STREAM *runtime_stream(SDL_AudioStream *stream)
{
	for (size_t index = 0; index < RUNTIME_STREAM_CAPACITY; index++) {
		if ((SDL_AudioStream *)runtime_streams[index] == stream && stream != NULL) {
			return runtime_streams[index];
		}
	}
	SDL_SetError("Unknown browser audio stream");
	return NULL;
}

static legacy_u8 runtime_format(const SDL_AudioSpec *spec)
{
	return spec != NULL && spec->format == SDL_AUDIO_S16 && spec->channels >= 1 &&
		   spec->channels <= (legacy_s32)RUNTIME_CHANNELS && spec->freq >= RUNTIME_MIN_RATE &&
		   spec->freq <= RUNTIME_MAX_RATE;
}

bool SDLCALL __wrap_SDL_InitSubSystem(SDL_InitFlags flags)
{
	if ((flags & ~SDL_INIT_AUDIO) != 0 && !__real_SDL_InitSubSystem(flags & ~SDL_INIT_AUDIO)) {
		return false;
	}
	if ((flags & SDL_INIT_AUDIO) != 0) {
		runtime_audio_references++;
	}
	return true;
}

void SDLCALL __wrap_SDL_QuitSubSystem(SDL_InitFlags flags)
{
	if ((flags & SDL_INIT_AUDIO) != 0 && runtime_audio_references != 0) {
		runtime_audio_references--;
	}
	if ((flags & ~SDL_INIT_AUDIO) != 0) {
		__real_SDL_QuitSubSystem(flags & ~SDL_INIT_AUDIO);
	}
}

SDL_InitFlags SDLCALL __wrap_SDL_WasInit(SDL_InitFlags flags)
{
	SDL_InitFlags others = flags & ~SDL_INIT_AUDIO;
	SDL_InitFlags initialized = flags != 0 && others == 0 ? 0 : __real_SDL_WasInit(others);
	if (runtime_audio_references != 0 && (flags == 0 || (flags & SDL_INIT_AUDIO) != 0)) {
		initialized |= SDL_INIT_AUDIO;
	}
	return initialized;
}

SDL_AudioStream *SDLCALL __wrap_SDL_OpenAudioDeviceStream(SDL_AudioDeviceID device,
														  const SDL_AudioSpec *spec,
														  SDL_AudioStreamCallback callback,
														  void *userdata)
{
	(void)device;
	(void)userdata;
	if (!runtime_format(spec) || callback != NULL) {
		SDL_SetError("Unsupported browser audio stream format or callback");
		return NULL;
	}
	for (size_t index = 0; index < RUNTIME_STREAM_CAPACITY; index++) {
		if (runtime_streams[index] != NULL) {
			continue;
		}
		struct RUNTIME_STREAM *stream = calloc(1, sizeof(*stream));
		if (stream != NULL) {
			stream->samples = malloc(RUNTIME_RING_FRAMES * RUNTIME_CHANNELS * sizeof(legacy_s16));
		}
		if (stream == NULL || stream->samples == NULL) {
			free(stream);
			SDL_SetError("Cannot allocate browser audio queue");
			return NULL;
		}
		stream->input = *spec;
		stream->gain = 1;
		stream->paused = 1;
		runtime_streams[index] = stream;
		return (SDL_AudioStream *)stream;
	}
	SDL_SetError("Browser audio stream limit reached");
	return NULL;
}

void SDLCALL __wrap_SDL_DestroyAudioStream(SDL_AudioStream *stream)
{
	if (stream == NULL) {
		return;
	}
	for (size_t index = 0; index < RUNTIME_STREAM_CAPACITY; index++) {
		if ((SDL_AudioStream *)runtime_streams[index] == stream) {
			free(runtime_streams[index]->samples);
			free(runtime_streams[index]);
			runtime_streams[index] = NULL;
			return;
		}
	}
}

bool SDLCALL __wrap_SDL_SetAudioStreamFormat(SDL_AudioStream *handle, const SDL_AudioSpec *source,
											 const SDL_AudioSpec *destination)
{
	struct RUNTIME_STREAM *stream = runtime_stream(handle);
	if (stream == NULL || (source != NULL && !runtime_format(source)) ||
		(destination != NULL && !runtime_format(destination))) {
		return false;
	}
	if (source != NULL) {
		if (stream->queued != 0) {
			return SDL_SetError("Cannot change a queued browser audio format");
		}
		stream->input = *source;
		stream->phase = 0;
	}
	return true;
}

bool SDLCALL __wrap_SDL_PutAudioStreamData(SDL_AudioStream *handle, const void *data,
										   legacy_int bytes)
{
	struct RUNTIME_STREAM *stream = runtime_stream(handle);
	if (stream == NULL || bytes < 0 || (data == NULL && bytes != 0)) {
		return false;
	}
	legacy_u32 frame_bytes = (legacy_u32)stream->input.channels * sizeof(legacy_s16);
	legacy_u32 frames = (legacy_u32)bytes / frame_bytes;
	if ((legacy_u32)bytes % frame_bytes != 0 || frames > RUNTIME_RING_FRAMES - stream->queued) {
		return SDL_SetError("Browser audio queue overflow or incomplete frame");
	}
	const legacy_s16 *samples = data;
	for (legacy_u32 frame = 0; frame < frames; frame++) {
		legacy_u32 write = (stream->read + stream->queued + frame) % RUNTIME_RING_FRAMES;
		for (legacy_u32 channel = 0; channel < (legacy_u32)stream->input.channels; channel++) {
			stream->samples[write * RUNTIME_CHANNELS + channel] =
				samples[frame * (legacy_u32)stream->input.channels + channel];
		}
	}
	stream->queued += frames;
	return true;
}

legacy_int SDLCALL __wrap_SDL_GetAudioStreamQueued(SDL_AudioStream *handle)
{
	struct RUNTIME_STREAM *stream = runtime_stream(handle);
	return stream == NULL ? -1
						  : (legacy_int)(stream->queued * (legacy_u32)stream->input.channels *
										 sizeof(legacy_s16));
}

bool SDLCALL __wrap_SDL_ClearAudioStream(SDL_AudioStream *handle)
{
	struct RUNTIME_STREAM *stream = runtime_stream(handle);
	if (stream == NULL) {
		return false;
	}
	stream->read = 0;
	stream->queued = 0;
	stream->phase = 0;
	return true;
}

bool SDLCALL __wrap_SDL_SetAudioStreamGain(SDL_AudioStream *handle, legacy_f32 gain)
{
	struct RUNTIME_STREAM *stream = runtime_stream(handle);
	if (stream == NULL || !isfinite(gain) || gain < 0) {
		return false;
	}
	stream->gain = gain;
	return true;
}

bool SDLCALL __wrap_SDL_ResumeAudioStreamDevice(SDL_AudioStream *handle)
{
	struct RUNTIME_STREAM *stream = runtime_stream(handle);
	if (stream == NULL) {
		return false;
	}
	stream->paused = 0;
	return true;
}

bool SDLCALL __wrap_SDL_PauseAudioStreamDevice(SDL_AudioStream *handle)
{
	struct RUNTIME_STREAM *stream = runtime_stream(handle);
	if (stream == NULL) {
		return false;
	}
	stream->paused = 1;
	return true;
}

static void runtime_mix(void)
{
	for (legacy_u32 frame = 0; frame < SDL3_AUDIO_WORKER_TICK_FRAMES; frame++) {
		legacy_f64 mixed[RUNTIME_CHANNELS] = {0};
		for (size_t index = 0; index < RUNTIME_STREAM_CAPACITY; index++) {
			struct RUNTIME_STREAM *stream = runtime_streams[index];
			if (stream == NULL || stream->paused || stream->queued == 0) {
				continue;
			}
			legacy_u32 next =
				stream->queued > 1 ? (stream->read + 1U) % RUNTIME_RING_FRAMES : stream->read;
			for (legacy_u32 channel = 0; channel < RUNTIME_CHANNELS; channel++) {
				legacy_u32 source = stream->input.channels == 1 ? 0 : channel;
				legacy_f64 first = stream->samples[stream->read * RUNTIME_CHANNELS + source];
				legacy_f64 second = stream->samples[next * RUNTIME_CHANNELS + source];
				mixed[channel] += (first + (second - first) * stream->phase) * stream->gain;
			}
			stream->phase += (legacy_f64)stream->input.freq / SDL3_AUDIO_WORKER_SAMPLE_RATE;
			legacy_u32 consume = (legacy_u32)stream->phase;
			stream->phase -= consume;
			if (consume > stream->queued) {
				consume = stream->queued;
				stream->phase = 0;
			}
			stream->read = (stream->read + consume) % RUNTIME_RING_FRAMES;
			stream->queued -= consume;
		}
		for (legacy_u32 channel = 0; channel < RUNTIME_CHANNELS; channel++) {
			legacy_f64 value = mixed[channel];
			if (value > LEGACY_S16_MAX) {
				value = LEGACY_S16_MAX;
			} else if (value < -(legacy_f64)LEGACY_U16_SIGN_BIT) {
				value = -(legacy_f64)LEGACY_U16_SIGN_BIT;
			}
			runtime_pcm[frame * RUNTIME_CHANNELS + channel] = (legacy_s16)value;
		}
	}
}

legacy_int sdl3_audio_worker_start(void)
{
	return 1;
}

void sdl3_audio_worker_stop(void)
{
	memset(runtime_callbacks, 0, sizeof(runtime_callbacks));
	runtime_configured = 0;
	runtime_suspended = 0;
}

legacy_u8 sdl3_audio_worker_is_active(void)
{
	return runtime_configured;
}

legacy_u8 sdl3_audio_worker_is_worker(void)
{
	return 1;
}

legacy_int sdl3_audio_worker_call(sdl3_audio_worker_command callback, void *argument)
{
	if (callback == NULL) {
		return 0;
	}
	callback(argument);
	return 1;
}

legacy_s16 sdl3_audio_worker_register_callback(sdl3_audio_worker_callback callback)
{
	if (callback != NULL) {
		for (size_t index = 0; index < SDL3_AUDIO_WORKER_CALLBACK_CAPACITY; index++) {
			if (runtime_callbacks[index] == callback) {
				return DOS_TIMER_CALLBACK_REGISTRATION_SUCCEEDED;
			}
		}
		for (size_t index = 0; index < SDL3_AUDIO_WORKER_CALLBACK_CAPACITY; index++) {
			if (runtime_callbacks[index] == NULL) {
				runtime_callbacks[index] = callback;
				return DOS_TIMER_CALLBACK_REGISTRATION_SUCCEEDED;
			}
		}
	}
	return DOS_TIMER_CALLBACK_REGISTRATION_FAILED;
}

void sdl3_audio_worker_unregister_callback(sdl3_audio_worker_callback callback)
{
	for (size_t index = 0; index < SDL3_AUDIO_WORKER_CALLBACK_CAPACITY; index++) {
		if (runtime_callbacks[index] == callback) {
			memmove(runtime_callbacks + index, runtime_callbacks + index + 1U,
					(SDL3_AUDIO_WORKER_CALLBACK_CAPACITY - index - 1U) * sizeof(callback));
			runtime_callbacks[SDL3_AUDIO_WORKER_CALLBACK_CAPACITY - 1U] = NULL;
			return;
		}
	}
}

void sdl3_audio_worker_set_suspended(legacy_u8 suspended)
{
	runtime_suspended = suspended != 0;
}

void sdl3_audio_worker_get_stats(struct SDL3_AUDIO_WORKER_STATS *statistics)
{
	if (statistics != NULL) {
		*statistics = runtime_statistics;
		statistics->active = runtime_configured;
		statistics->suspended = runtime_suspended;
	}
}

legacy_s16 dos_timer_register_callback(void (*callback)(void))
{
	return sdl3_audio_worker_register_callback(callback);
}

void dos_timer_unregister_callback(void (*callback)(void))
{
	sdl3_audio_worker_unregister_callback(callback);
}

legacy_s32 sdl3_timer_callbacks_suspended(void)
{
	return runtime_suspended;
}

void dos_timer_set_callbacks_suspended(legacy_s16 suspended)
{
	sdl3_audio_worker_set_suspended(suspended != 0);
}

void sdl3_timer_pump(void)
{
}

void *_memcpy(void *destination, const void *source, legacy_u16 length)
{
	return memcpy(destination, source, length);
}

void *__fmemcpy(void *destination, const void *source, legacy_u16 length)
{
	return memcpy(destination, source, length);
}

legacy_s16 dos_write_stdout(const legacy_s8 *text, legacy_u16 length)
{
	return fwrite(text, 1, length, stdout) == length ? (legacy_s16)length : -1;
}

legacy_s16 dos_write_stderr(const legacy_s8 *text, legacy_u16 length)
{
	return fwrite(text, 1, length, stderr) == length ? (legacy_s16)length : -1;
}

void dos_process_exit(legacy_s16 status)
{
	exit(status);
}

void headless_exit(legacy_s16 status)
{
	dos_process_exit(status);
}

legacy_s16 dos_data_stack_segments_match(void)
{
	return 1;
}

void dos_install_divide_error_handler(void)
{
}

void dos_interrupts_disable(void)
{
	/* Commands and rendering are serialized by this realm's event loop. */
}

void dos_interrupts_enable(void)
{
}

void dos_set_critical_error_handler(legacy_s16 (*callback)(void))
{
	(void)callback;
}

void sdl3_platform_delay(legacy_u32 milliseconds)
{
	/* Audio advances only on render requests. No gameplay stack needs yielding. */
	(void)milliseconds;
}

void sdl3_browser_yield_if_due(void)
{
}

legacy_u64 hires_presentation_now(void)
{
	return SDL_GetTicksNS();
}

void dos_show_startup_error(const legacy_s8 *message)
{
	SDL_strlcpy(runtime_startup_error, (const legacy_char *)message, sizeof(runtime_startup_error));
	fprintf(stderr, "%s\n", runtime_startup_error);
}

void sdl3_timer_rebase(void)
{
}

void dos_timer_setup_interrupt(void)
{
	memset(runtime_callbacks, 0, sizeof(runtime_callbacks));
	memset(&runtime_statistics, 0, sizeof(runtime_statistics));
	runtime_suspended = 0;
	runtime_timer_origin = 0;
	runtime_timer_last = 0;
}

void dos_timer_shutdown(void)
{
	memset(runtime_callbacks, 0, sizeof(runtime_callbacks));
}

legacy_u32 dos_timer_get_realtime_counter(void)
{
	return (legacy_u32)runtime_statistics.ticks;
}

legacy_u32 timer_get_counter(void)
{
	return dos_timer_get_realtime_counter() - runtime_timer_origin;
}

legacy_u32 timer_get_delta(void)
{
	legacy_u32 now = timer_get_counter();
	legacy_u32 delta = now - runtime_timer_last;
	runtime_timer_last = now;
	return delta;
}

legacy_u32 timer_get_slow_counter(void)
{
	return timer_get_counter() / DOS_TIMER_DEFAULT_DIVIDER_PERIOD;
}

void dos_timer_reset_counter(void)
{
	runtime_timer_origin = dos_timer_get_realtime_counter();
	runtime_timer_last = 0;
}

static legacy_u8 runtime_write_file(const struct SDL3_AUDIO_COMMAND *command)
{
	legacy_char path[SDL3_AUDIO_COMMAND_TEXT_BYTES];
	SDL_strlcpy(path, command->text, sizeof(path));
	if (path[0] != '/' || strstr(path, "/../") != NULL || strstr(path, "/./") != NULL ||
		!strcmp(path, "/..") || !strcmp(path, "/.") || strlen(path) < 2U) {
		return 0;
	}
	for (legacy_char *part = path + 1; *part != 0; part++) {
		if (*part == '/') {
			*part = 0;
			legacy_int result = mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR);
			*part = '/';
			if (result != 0 && errno != EEXIST) {
				return 0;
			}
		}
	}
	FILE *file = fopen(path, "wb");
	if (file == NULL) {
		return 0;
	}
	legacy_u8 success = command->bytes[0] == 0 ||
						fwrite(command->data[0], 1, command->bytes[0], file) == command->bytes[0];
	return fclose(file) == 0 && success;
}

static legacy_char *runtime_response(const legacy_char *error,
									 const struct SDL3_AUDIO_COMMAND *command)
{
	legacy_char *response = malloc(RUNTIME_RESPONSE_BYTES);
	if (response == NULL) {
		return runtime_no_memory;
	}
	if (error != NULL) {
		legacy_char escaped[RUNTIME_ERROR_ESCAPE_BYTES];
		size_t used = 0;
		for (size_t index = 0; error[index] != 0 && index < RUNTIME_ERROR_BYTES - 1U; index++) {
			legacy_u8 byte = (legacy_u8)error[index];
			if (byte == '"' || byte == '\\') {
				escaped[used++] = '\\';
				escaped[used++] = (legacy_char)byte;
			} else if (byte < ' ') {
				used += (size_t)snprintf(escaped + used, sizeof(escaped) - used, "\\u%04x",
										 (legacy_uint)byte);
			} else {
				escaped[used++] = (legacy_char)byte;
			}
		}
		escaped[used] = 0;
		snprintf(response, RUNTIME_RESPONSE_BYTES, "{\"ok\":false,\"error\":\"%s\"}", escaped);
	} else {
		legacy_s64 result =
			command->pointer_result != 0 ? command->pointer_result : (legacy_s64)command->result;
		snprintf(response, RUNTIME_RESPONSE_BYTES,
				 "{\"ok\":true,\"result\":%" LEGACY_PRId64 ",\"pointer_result\":%" LEGACY_PRIu32
				 ",\"legacy_hihat\":%u,"
				 "\"impact\":%s,\"flush\":%s}",
				 result, command->pointer_result, (legacy_uint)command->legacy_hihat,
				 command->impact ? "true" : "false", command->flush ? "true" : "false");
	}
	return response;
}

legacy_char *audio_command(const legacy_char *json, size_t bytes)
{
	struct SDL3_AUDIO_COMMAND command;
	memset(&command, 0, sizeof(command));
	size_t heap = emscripten_get_heap_size();
	size_t pointer = (size_t)json;
	if (json == NULL || bytes == 0 || bytes > RUNTIME_JSON_BYTES || pointer > heap ||
		bytes > heap - pointer || !runtime_parse_command(json, bytes, &command)) {
		return runtime_response("Invalid audio command", NULL);
	}
	if (command.op == AUDIO_COMMAND_FILE) {
		return runtime_write_file(&command) ? runtime_response(NULL, &command)
											: runtime_response("Cannot copy audio file", NULL);
	}
	if (command.op == AUDIO_COMMAND_CONFIGURE) {
		if (runtime_configured) {
			struct SDL3_AUDIO_COMMAND shutdown;
			memset(&shutdown, 0, sizeof(shutdown));
			shutdown.op = AUDIO_COMMAND_SHUTDOWN;
			sdl3_audio_resources_set_execution(1);
			sdl3_audio_command_execute(&shutdown);
		}
		runtime_configured = 0;
		sdl3_audio_resources_set_execution(0);
		mmgr_init_conventional_arena();
	} else if (!runtime_configured && command.op != AUDIO_COMMAND_SHUTDOWN) {
		return runtime_response("Audio is not configured", NULL);
	}
	runtime_startup_error[0] = 0;
	sdl3_audio_command_execute(&command);
	sdl3_audio_resources_set_execution(1);
	if (command.op == AUDIO_COMMAND_CONFIGURE) {
		runtime_configured = !command.failed;
	} else if (command.op == AUDIO_COMMAND_SHUTDOWN) {
		runtime_configured = 0;
	}
	return command.failed ? runtime_response(runtime_startup_error[0] != 0 ? runtime_startup_error
																		   : "Audio command failed",
											 NULL)
						  : runtime_response(NULL, &command);
}

void audio_response_free(void *response)
{
	if (response != runtime_no_memory) {
		free(response);
	}
}

const legacy_s16 *audio_render_tick(void)
{
	if (!runtime_configured) {
		return NULL;
	}
	sdl3_audio_resources_set_execution(1);
	sdl3_audio_worker_callback pending[SDL3_AUDIO_WORKER_CALLBACK_CAPACITY];
	memcpy(pending, runtime_callbacks, sizeof(pending));
	if (!runtime_suspended) {
		for (size_t index = 0; index < SDL3_AUDIO_WORKER_CALLBACK_CAPACITY; index++) {
			if (pending[index] != NULL) {
				pending[index]();
				runtime_statistics.callback_invocations++;
			}
		}
		runtime_statistics.callback_ticks++;
	}
	sdl3_audio_update();
	runtime_mix();
	runtime_statistics.ticks++;
	return runtime_pcm;
}
