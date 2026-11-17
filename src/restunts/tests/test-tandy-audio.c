/* Verify SDL reference ownership, device failures and silent batch advancement. */
#include <assert.h>
#include <string.h>
#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include "../platform/sdl3/tandy_audio.h"
#include "../c/audio/audio_internal.h"

#define TEST_SAMPLE_BYTES 1000U
#define TEST_SAMPLE_NOTE 30U
#define TEST_FINISH_TICKS 32U
#define TEST_INSTRUMENT_BYTES 93U

legacy_s32 sdl3_batch_mode;

static void test_batch_progression(void)
{
	sdl3_batch_mode = 1;
	assert(tandy_audio_initialize());
	assert(SDL_WasInit(SDL_INIT_AUDIO) == 0);
	legacy_u8 sample[TANDY_SAMPLE_HEADER_BYTES + TEST_SAMPLE_BYTES];
	memset(sample, LEGACY_U8_MAX, sizeof(sample));
	LEGACY_WRITE_U32_LE(sample + TANDY_SAMPLE_LENGTH_OFFSET, TEST_SAMPLE_BYTES);
	legacy_u8 resource[TEST_INSTRUMENT_BYTES] = {0};
	struct AUDIO_CONTEXT context;
	struct AUDIO_CHANNEL source;
	memset(&context, 0, sizeof(context));
	memset(&source, 0, sizeof(source));
	context.state = AUDIO_CONTEXT_STATE_PLAYING;
	source.channel = AUDIO_EFFECT_CHANNEL_FIRST;
	source.volume = AUDIO_ENGINE_MAX_VOLUME;
	struct TANDY_DRIVER *driver = tandy_audio_driver();
	tandy_driver_activate(driver, 0, &context, &source, TEST_SAMPLE_NOTE, AUDIO_ENGINE_MAX_VOLUME,
						  resource, sample, sizeof(sample));
	tandy_driver_mix(driver);
	assert(driver->sample.sample_active);
	tandy_audio_update();
	assert(driver->sample.sample_position != 0);
	for (legacy_u32 tick = 0; tick < TEST_FINISH_TICKS; tick++) {
		tandy_audio_update();
	}
	assert(!driver->sample.sample_active);
	assert(SDL_WasInit(SDL_INIT_AUDIO) == 0);
	tandy_audio_shutdown();
	assert(driver->sample.sample_data == NULL);
}

static void test_device_lifetime(void)
{
	sdl3_batch_mode = 0;
	assert(SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
	assert(SDL_InitSubSystem(SDL_INIT_AUDIO));
	assert(tandy_audio_initialize());
	tandy_audio_update();
	assert(tandy_audio_queued_frames() >= 0);
	tandy_audio_set_music_muted(1);
	tandy_audio_clear_queue();
	assert(tandy_audio_initialize());
	tandy_audio_update();
	tandy_audio_shutdown();
	/* A caller's SDL reference survives release of the backend's reference. */
	assert(SDL_WasInit(SDL_INIT_AUDIO) != 0);
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
	assert(SDL_WasInit(SDL_INIT_AUDIO) == 0);
	assert(SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "restunts-unavailable-audio-driver",
								   SDL_HINT_OVERRIDE));
	assert(!tandy_audio_initialize());
	assert(tandy_audio_error()[0] != 0);
	tandy_audio_shutdown();
	assert(SDL_WasInit(SDL_INIT_AUDIO) == 0);
	assert(SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
	assert(tandy_audio_initialize());
	tandy_audio_shutdown();
	assert(SDL_WasInit(SDL_INIT_AUDIO) == 0);
}

legacy_int main(void)
{
	SDL_SetMainReady();
	test_batch_progression();
	test_device_lifetime();
	return 0;
}
