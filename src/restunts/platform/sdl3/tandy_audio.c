/* The existing audio owner sequences TD15 resources and produces mono PCM.
 * SDL consumes the queued samples without touching the driver's state. */
#include "tandy_audio.h"
#include "../../c/platform.h"
#include <SDL3/SDL.h>

#define TANDY_AUDIO_SAMPLE_RATE TANDY_SAMPLE_RATE
#define TANDY_AUDIO_TICK_FRAMES (TANDY_AUDIO_SAMPLE_RATE / DOS_TIMER_REALTIME_TICKS_PER_SECOND)
#define TANDY_AUDIO_MAX_QUEUED_TICKS 10U
#define TANDY_AUDIO_ERROR_BYTES 256U

static struct TANDY_DRIVER tandy_driver;
static SDL_AudioStream *tandy_stream;
static legacy_u8 tandy_audio_subsystem;
static legacy_u8 tandy_ready;
static legacy_char tandy_error[TANDY_AUDIO_ERROR_BYTES];
extern legacy_s32 sdl3_batch_mode;

struct TANDY_DRIVER *tandy_audio_driver(void)
{
	return &tandy_driver;
}

const legacy_char *tandy_audio_error(void)
{
	return tandy_error;
}

void tandy_audio_shutdown(void)
{
	SDL_DestroyAudioStream(tandy_stream);
	tandy_stream = NULL;
	if (tandy_audio_subsystem) {
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		tandy_audio_subsystem = 0;
	}
	tandy_driver_shutdown(&tandy_driver);
	tandy_ready = 0;
}

legacy_int tandy_audio_initialize(void)
{
	tandy_audio_shutdown();
	tandy_error[0] = 0;
	/* TD15 also uses the later Tandy DAC. PSSJ-3 retains the requested NCR
	 * noise behavior and the non-inverted PSG output of the integrated chip. */
	tandy_driver_initialize(&tandy_driver, MAME_PSG_PSSJ3);
	if (sdl3_batch_mode) {
		return 1;
	}
	if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
		SDL_snprintf(tandy_error, sizeof(tandy_error), "Cannot initialize Tandy audio: %s",
					 SDL_GetError());
		tandy_audio_shutdown();
		return 0;
	}
	tandy_audio_subsystem = 1;
	SDL_AudioSpec spec = {SDL_AUDIO_S16, 1, TANDY_AUDIO_SAMPLE_RATE};
	tandy_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
	if (tandy_stream == NULL || !SDL_ResumeAudioStreamDevice(tandy_stream)) {
		SDL_snprintf(tandy_error, sizeof(tandy_error), "Cannot open Tandy audio: %s",
					 SDL_GetError());
		tandy_audio_shutdown();
		return 0;
	}
	tandy_ready = 1;
	return 1;
}

void tandy_audio_clear_queue(void)
{
	if (tandy_stream != NULL) {
		SDL_ClearAudioStream(tandy_stream);
	}
}

legacy_s32 tandy_audio_queued_frames(void)
{
	if (tandy_stream == NULL) {
		return -1;
	}
	legacy_s32 queued = SDL_GetAudioStreamQueued(tandy_stream);
	return queued < 0 ? -1 : queued / (legacy_s32)sizeof(legacy_s16);
}

void tandy_audio_set_music_muted(legacy_u8 muted)
{
	tandy_driver_set_music_muted(&tandy_driver, muted);
	tandy_audio_clear_queue();
}

void tandy_audio_update(void)
{
	if (!tandy_ready && !sdl3_batch_mode) {
		return;
	}
	legacy_s16 samples[TANDY_AUDIO_TICK_FRAMES];
	tandy_driver_render(&tandy_driver, samples, TANDY_AUDIO_TICK_FRAMES, TANDY_AUDIO_SAMPLE_RATE);
	if (!tandy_ready) {
		return;
	}
	const legacy_s32 max_queue =
		TANDY_AUDIO_TICK_FRAMES * sizeof(legacy_s16) * TANDY_AUDIO_MAX_QUEUED_TICKS;
	if (SDL_GetAudioStreamQueued(tandy_stream) > max_queue) {
		SDL_ClearAudioStream(tandy_stream);
	}
	SDL_PutAudioStreamData(tandy_stream, samples, sizeof(samples));
}
