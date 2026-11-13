/* Exercise device ownership and silent batch progression without hardware. */
#include <assert.h>
#include <string.h>
#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include "../platform/sdl3/pc_speaker_audio.h"
#include "../c/audio/audio_internal.h"

#define TEST_SAMPLE_BYTES 32U
#define TEST_SAMPLE_NOTE 0U
#define TEST_FINISH_TICKS 8U

legacy_s32 sdl3_batch_mode;

static void test_batch_progression(void)
{
	sdl3_batch_mode = 1;
	assert(pc_speaker_audio_initialize());
	assert(SDL_WasInit(SDL_INIT_AUDIO) == 0);
	legacy_u8 sample[PC_SPEAKER_SAMPLE_HEADER_BYTES + TEST_SAMPLE_BYTES] = {0};
	LEGACY_WRITE_U32_LE(sample + PC_SPEAKER_SAMPLE_LENGTH_OFFSET, TEST_SAMPLE_BYTES);
	struct AUDIO_CONTEXT context;
	struct AUDIO_CHANNEL source;
	memset(&context, 0, sizeof(context));
	memset(&source, 0, sizeof(source));
	source.channel = AUDIO_EFFECT_CHANNEL_FIRST;
	struct PC_SPEAKER_DRIVER *driver = pc_speaker_audio_driver();
	pc_speaker_driver_activate(driver, 0, &context, &source, TEST_SAMPLE_NOTE, sample,
							   sizeof(sample));
	pc_speaker_driver_mix(driver);
	assert(driver->sample_active);
	pc_speaker_audio_update();
	assert(driver->sample_position != 0);
	for (legacy_u32 tick = 0; tick < TEST_FINISH_TICKS; tick++) {
		pc_speaker_audio_update();
	}
	assert(!driver->sample_active);
	assert(SDL_WasInit(SDL_INIT_AUDIO) == 0);
	pc_speaker_audio_shutdown();
	assert(driver->sample_data == NULL);
}

static void test_device_lifetime(void)
{
	sdl3_batch_mode = 0;
	assert(SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
	assert(SDL_InitSubSystem(SDL_INIT_AUDIO));
	assert(pc_speaker_audio_initialize());
	pc_speaker_audio_update();
	pc_speaker_audio_set_music_muted(1);
	assert(pc_speaker_audio_initialize());
	pc_speaker_audio_update();
	pc_speaker_audio_shutdown();
	/* The backend releases its own SDL reference while preserving its caller's. */
	assert(SDL_WasInit(SDL_INIT_AUDIO) != 0);
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
	assert(SDL_WasInit(SDL_INIT_AUDIO) == 0);
	assert(SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "restunts-unavailable-audio-driver",
								   SDL_HINT_OVERRIDE));
	assert(!pc_speaker_audio_initialize());
	assert(pc_speaker_audio_error()[0] != 0);
	pc_speaker_audio_shutdown();
	assert(SDL_WasInit(SDL_INIT_AUDIO) == 0);
	assert(SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
	assert(pc_speaker_audio_initialize());
	pc_speaker_audio_shutdown();
	assert(SDL_WasInit(SDL_INIT_AUDIO) == 0);
}

legacy_int main(void)
{
	SDL_SetMainReady();
	test_batch_progression();
	test_device_lifetime();
	return 0;
}
