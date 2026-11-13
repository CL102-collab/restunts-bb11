/* Audio owners report channel completions without entering game callbacks.
 * The game thread validates registrations before invoking its local pointers. */
#include "audio_completions.h"

#if defined(RESTUNTS_AUDIO_WORKER) && !defined(__DJGPP__)
#include "audio_commands.h"
#include "../../c/audio/audio.h"
#include <SDL3/SDL.h>
#include <stddef.h>
#if defined(__EMSCRIPTEN__) && !defined(RESTUNTS_BROWSER_AUDIO_RUNTIME)
#include <emscripten.h>
#endif

#define AUDIO_COMPLETION_CHANNEL_BITS LEGACY_BYTE_BITS
#define AUDIO_COMPLETION_CHANNEL_MASK LEGACY_U8_MAX
#define AUDIO_COMPLETION_GENERATION_MASK 0x00ffffffU
#define AUDIO_COMPLETION_ARGUMENT_COUNT 3U

typedef void (*audio_completion_callback)(legacy_s16 channel);
static audio_completion_callback completion_callbacks[AUDIO_CHANNEL_COUNT];
static legacy_u32 completion_generations[AUDIO_CHANNEL_COUNT];
static legacy_u32 owner_generations[AUDIO_CHANNEL_COUNT];
static legacy_u32 pending_generations[AUDIO_CHANNEL_COUNT];
static legacy_u16 completion_cursor;
#ifndef __EMSCRIPTEN__
static SDL_SpinLock completion_lock;
#endif

extern void audio_set_finish_callback(legacy_s16 channel, void *callback);

static void completions_lock(void)
{
#ifndef __EMSCRIPTEN__
	SDL_LockSpinlock(&completion_lock);
#endif
}

static void completions_unlock(void)
{
#ifndef __EMSCRIPTEN__
	SDL_UnlockSpinlock(&completion_lock);
#endif
}

static legacy_u8 completion_channel_valid(legacy_s16 channel)
{
	return channel >= 0 && (legacy_u16)channel < AUDIO_CHANNEL_COUNT;
}

static legacy_u32 completion_next_generation(legacy_u32 generation)
{
	generation = (generation + 1U) & AUDIO_COMPLETION_GENERATION_MASK;
	return generation != 0 ? generation : 1U;
}

static void completion_record(legacy_s16 channel)
{
	if (!completion_channel_valid(channel)) {
		return;
	}
	completions_lock();
	pending_generations[channel] = owner_generations[channel];
	completions_unlock();
}

void sdl3_audio_completion_set(legacy_s16 channel, void *callback)
{
	if (!completion_channel_valid(channel)) {
		return;
	}
	legacy_u32 generation = completion_next_generation(completion_generations[channel]);
	completion_generations[channel] = generation;
	completion_callbacks[channel] = (audio_completion_callback)callback;
	const legacy_s32 arguments[AUDIO_COMPLETION_ARGUMENT_COUNT] = {channel, callback != NULL,
																   (legacy_s32)generation};
	sdl3_audio_command_scalars(AUDIO_COMMAND_SET_FINISH_CALLBACK, arguments,
							   AUDIO_COMPLETION_ARGUMENT_COUNT);
}

void sdl3_audio_completion_register(legacy_s16 channel, legacy_s32 enabled, legacy_u32 generation)
{
	if (!completion_channel_valid(channel)) {
		return;
	}
	generation &= AUDIO_COMPLETION_GENERATION_MASK;
	completions_lock();
	owner_generations[channel] = enabled ? generation : 0;
	completions_unlock();
	audio_set_finish_callback(channel,
							  enabled && generation != 0 ? (void *)completion_record : NULL);
}

legacy_u32 audio_pop_finish(void)
{
	legacy_u32 event = 0;
	completions_lock();
	for (legacy_u16 count = 0; count < AUDIO_CHANNEL_COUNT; count++) {
		legacy_u16 channel = completion_cursor;
		completion_cursor = (completion_cursor + 1U) % AUDIO_CHANNEL_COUNT;
		legacy_u32 generation = pending_generations[channel];
		if (generation != 0) {
			pending_generations[channel] = 0;
			event = (generation << AUDIO_COMPLETION_CHANNEL_BITS) | (channel + 1U);
			break;
		}
	}
	completions_unlock();
	return event;
}

#if defined(__EMSCRIPTEN__) && !defined(RESTUNTS_BROWSER_AUDIO_RUNTIME)
EM_JS(legacy_u32, completion_pop_browser, (), {
	const queue = Module['restuntsAudioFinished'];
	return queue && queue.length ? queue.shift() >>> 0 : 0;
});

EM_JS(void, completion_clear_browser, (), { Module['restuntsAudioFinished'] = []; });
#endif

void sdl3_audio_completions_pump(void)
{
#ifndef RESTUNTS_BROWSER_AUDIO_RUNTIME
	/* A callback may start another sequence. Bound each pump so completions
	 * created by those commands cannot monopolize the game thread. */
	for (legacy_u16 count = 0; count < AUDIO_CHANNEL_COUNT; count++) {
#if defined(__EMSCRIPTEN__)
		legacy_u32 event = completion_pop_browser();
#else
		legacy_u32 event = audio_pop_finish();
#endif
		if (event == 0) {
			break;
		}
		legacy_u32 channel = event & AUDIO_COMPLETION_CHANNEL_MASK;
		legacy_u32 generation = event >> AUDIO_COMPLETION_CHANNEL_BITS;
		if (channel == 0 || channel > AUDIO_CHANNEL_COUNT) {
			continue;
		}
		channel--;
		audio_completion_callback callback = completion_callbacks[channel];
		if (callback != NULL && generation == completion_generations[channel]) {
			callback((legacy_s16)channel);
		}
	}
#endif
}

void sdl3_audio_completions_clear(void)
{
	for (legacy_u16 channel = 0; channel < AUDIO_CHANNEL_COUNT; channel++) {
		completion_callbacks[channel] = NULL;
		completion_generations[channel] =
			completion_next_generation(completion_generations[channel]);
	}
	completions_lock();
	for (legacy_u16 channel = 0; channel < AUDIO_CHANNEL_COUNT; channel++) {
		owner_generations[channel] = 0;
		pending_generations[channel] = 0;
	}
	completion_cursor = 0;
	completions_unlock();
#if defined(__EMSCRIPTEN__) && !defined(RESTUNTS_BROWSER_AUDIO_RUNTIME)
	completion_clear_browser();
#endif
}
#endif
