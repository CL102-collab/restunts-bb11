/* Completion pointers belong to the game thread; workers report only tokens. */
#include <assert.h>
#include <stddef.h>
#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include "../platform/sdl3/audio_commands.h"
#include "../platform/sdl3/audio_completions.h"
#include "../c/audio/audio.h"

#define TEST_CHANNEL 3
#define TEST_ARGUMENTS 3U
#define TEST_CHANNEL_MASK 255U

typedef void (*test_finish_callback)(legacy_s16 channel);
static test_finish_callback owner_callbacks[AUDIO_CHANNEL_COUNT];
static SDL_ThreadID game_thread;
static legacy_u32 command_count, first_count, second_count;
static legacy_u32 registered_generation;
static legacy_u8 chain_completion;

void audio_set_finish_callback(legacy_s16 channel, void *callback)
{
	assert(channel >= 0 && (legacy_u16)channel < AUDIO_CHANNEL_COUNT);
	owner_callbacks[channel] = (test_finish_callback)callback;
}

legacy_s32 sdl3_audio_command_scalars(legacy_u32 op, const legacy_s32 *arguments, size_t count)
{
	assert(op == AUDIO_COMMAND_SET_FINISH_CALLBACK && count == TEST_ARGUMENTS);
	assert(SDL_GetCurrentThreadID() == game_thread);
	command_count++;
	registered_generation = (legacy_u32)arguments[2];
	sdl3_audio_completion_register((legacy_s16)arguments[0], arguments[1], registered_generation);
	return 0;
}

static void first_finished(legacy_s16 channel)
{
	assert(SDL_GetCurrentThreadID() == game_thread && channel == TEST_CHANNEL);
	first_count++;
	if (chain_completion) {
		owner_callbacks[channel](channel);
	}
}

static void second_finished(legacy_s16 channel)
{
	assert(SDL_GetCurrentThreadID() == game_thread && channel == TEST_CHANNEL);
	second_count++;
}

static legacy_int finish_on_worker(void *argument)
{
	(void)argument;
	assert(SDL_GetCurrentThreadID() != game_thread);
	assert(owner_callbacks[TEST_CHANNEL] != NULL);
	owner_callbacks[TEST_CHANNEL](TEST_CHANNEL);
	return 0;
}

static void report_completion(void)
{
	SDL_Thread *worker = SDL_CreateThread(finish_on_worker, "audio-completion-test", NULL);
	assert(worker != NULL);
	legacy_int result;
	SDL_WaitThread(worker, &result);
	assert(result == 0);
}

legacy_int main(void)
{
	SDL_SetMainReady();
	game_thread = SDL_GetCurrentThreadID();
	sdl3_audio_completion_set(-1, (void *)first_finished);
	sdl3_audio_completion_set(AUDIO_CHANNEL_COUNT, (void *)first_finished);
	assert(command_count == 0 && audio_pop_finish() == 0);
	sdl3_audio_completion_set(TEST_CHANNEL, (void *)first_finished);
	assert(command_count == 1 && registered_generation != 0);
	report_completion();
	assert(first_count == 0);
	sdl3_audio_completions_pump();
	assert(first_count == 1 && audio_pop_finish() == 0);

	/* A completion already queued for the previous registration must not
     * invoke the replacement callback at the same channel. */
	report_completion();
	sdl3_audio_completion_set(TEST_CHANNEL, (void *)second_finished);
	sdl3_audio_completions_pump();
	assert(first_count == 1 && second_count == 0);
	report_completion();
	sdl3_audio_completions_pump();
	assert(second_count == 1);

	report_completion();
	sdl3_audio_completion_set(TEST_CHANNEL, NULL);
	sdl3_audio_completions_pump();
	assert(owner_callbacks[TEST_CHANNEL] == NULL && second_count == 1);

	/* This is also the browser worker export: no game function is invoked
     * while a numeric completion token is removed from the owner queue. */
	sdl3_audio_completion_set(TEST_CHANNEL, (void *)first_finished);
	report_completion();
	legacy_u32 event = audio_pop_finish();
	assert((event & TEST_CHANNEL_MASK) == TEST_CHANNEL + 1U);
	assert((event >> LEGACY_BYTE_BITS) == registered_generation);
	assert(first_count == 1 && audio_pop_finish() == 0);

	report_completion();
	sdl3_audio_completions_clear();
	sdl3_audio_completions_pump();
	assert(first_count == 1 && second_count == 1);
	/* A late worker callback from before shutdown is harmless. */
	report_completion();
	assert(audio_pop_finish() == 0);

	sdl3_audio_completion_set(TEST_CHANNEL, (void *)first_finished);
	chain_completion = 1;
	report_completion();
	sdl3_audio_completions_pump();
	assert(first_count == 1U + AUDIO_CHANNEL_COUNT);
	chain_completion = 0;
	sdl3_audio_completions_clear();
	SDL_Quit();
	return 0;
}
