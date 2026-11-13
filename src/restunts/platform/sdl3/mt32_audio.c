/* Audio-owner Munt/SDL bridge. MIDI writes and PCM rendering share one
 * timeline; SDL consumes the completed PCM queue. */
#include "mt32_audio.h"
#include "mt32_roms.h"
#include <SDL3/SDL.h>
#include "../../c/timing.h"
#include "../../c/platform.h"

#define MT32_AUDIO_SAMPLE_RATE 44100U
#define MT32_AUDIO_CHANNELS 2U
#define MT32_AUDIO_TICK_FRAMES (MT32_AUDIO_SAMPLE_RATE / DOS_TIMER_REALTIME_TICKS_PER_SECOND)
#define MT32_AUDIO_MAX_QUEUED_TICKS 10U
#define MT32_AUDIO_MIDI_QUEUE_SIZE 4096U
#define MT32_AUDIO_ERROR_SIZE 1024U

static mt32emu_context mt32_context;
static SDL_AudioStream *mt32_stream;
static struct MT32_DRIVER mt32_driver;
static legacy_u8 mt32_audio_subsystem;
static legacy_char mt32_error[MT32_AUDIO_ERROR_SIZE];

static void mt32_audio_midi(void *instance, const legacy_u8 *bytes, size_t length)
{
	mt32emu_context context = instance;
	if (context != NULL) {
		mt32emu_parse_stream(context, bytes, (mt32emu_bit32u)length);
	}
}

const legacy_char *mt32_audio_error(void)
{
	return mt32_error;
}

struct MT32_DRIVER *mt32_audio_driver(void)
{
	return &mt32_driver;
}

void mt32_audio_shutdown(void)
{
	SDL_DestroyAudioStream(mt32_stream);
	mt32_stream = NULL;
	if (mt32_context != NULL) {
		mt32emu_context released = mt32_context;
		mt32emu_free_context(mt32_context);
		mt32_context = NULL;
		/* SDL owns the UTF-8 loaded ROM data until the synth is destroyed. */
		mt32_rom_release(released);
	}
	SDL_zero(mt32_driver);
	if (mt32_audio_subsystem) {
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		mt32_audio_subsystem = 0;
	}
}

legacy_int mt32_audio_initialize(const legacy_char *rom_directory, const legacy_char *model)
{
	mt32_audio_shutdown();
	mt32_error[0] = 0;
	mt32emu_report_handler_i report_handler;
	SDL_zero(report_handler);
	mt32_context = mt32emu_create_context(report_handler, NULL);
	if (mt32_context == NULL) {
		SDL_strlcpy(mt32_error, "Cannot allocate MT-32 synthesizer.", sizeof(mt32_error));
		return 0;
	}
	if (!mt32_rom_load(mt32_context, rom_directory, model, mt32_error, sizeof(mt32_error))) {
		mt32_audio_shutdown();
		return 0;
	}
	mt32emu_set_stereo_output_samplerate(mt32_context, MT32_AUDIO_SAMPLE_RATE);
	mt32emu_set_midi_event_queue_size(mt32_context, MT32_AUDIO_MIDI_QUEUE_SIZE);
	if (mt32emu_open_synth(mt32_context) != MT32EMU_RC_OK) {
		SDL_strlcpy(mt32_error, "Cannot initialize MT-32 with the selected ROM set.",
					sizeof(mt32_error));
		mt32_audio_shutdown();
		return 0;
	}
	if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
		SDL_snprintf(mt32_error, sizeof(mt32_error), "Cannot initialize MT-32 audio: %s",
					 SDL_GetError());
		mt32_audio_shutdown();
		return 0;
	}
	mt32_audio_subsystem = 1;
	SDL_AudioSpec spec = {SDL_AUDIO_S16, MT32_AUDIO_CHANNELS, MT32_AUDIO_SAMPLE_RATE};
	mt32_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
	if (mt32_stream == NULL || !SDL_ResumeAudioStreamDevice(mt32_stream)) {
		SDL_snprintf(mt32_error, sizeof(mt32_error), "Cannot open MT-32 audio output: %s",
					 SDL_GetError());
		mt32_audio_shutdown();
		return 0;
	}
	mt32_driver_initialize(&mt32_driver, mt32_audio_midi, mt32_context);
	return 1;
}

void mt32_audio_finish_bank_upload(void)
{
	if (mt32_context != NULL) {
		/* Bank transfer is a startup/loading operation, before any song starts.
		 * Complete it before notes enter the timed MIDI queue. */
		mt32emu_flush_midi_queue(mt32_context);
	}
}

void mt32_audio_set_music_muted(legacy_u8 muted)
{
	mt32_driver_set_music_muted(&mt32_driver, muted);
	if (mt32_context != NULL) {
		mt32emu_flush_midi_queue(mt32_context);
	}
	if (mt32_stream != NULL) {
		SDL_ClearAudioStream(mt32_stream);
	}
}

#ifndef __DJGPP__
void mt32_audio_clear_queue(void)
{
	if (mt32_stream != NULL) {
		SDL_ClearAudioStream(mt32_stream);
	}
}

legacy_s32 mt32_audio_queued_frames(void)
{
	if (mt32_stream == NULL) {
		return -1;
	}
	legacy_s32 queued = SDL_GetAudioStreamQueued(mt32_stream);
	return queued < 0 ? -1 : queued / (legacy_s32)(sizeof(legacy_s16) * MT32_AUDIO_CHANNELS);
}
#endif

void mt32_audio_update(void)
{
	if (mt32_context == NULL || mt32_stream == NULL) {
		return;
	}
	const legacy_s32 max_queue = MT32_AUDIO_TICK_FRAMES * MT32_AUDIO_CHANNELS * sizeof(legacy_s16) *
								 MT32_AUDIO_MAX_QUEUED_TICKS;
	if (SDL_GetAudioStreamQueued(mt32_stream) > max_queue) {
		SDL_ClearAudioStream(mt32_stream);
	}
	legacy_s16 samples[MT32_AUDIO_TICK_FRAMES * MT32_AUDIO_CHANNELS];
	mt32emu_render_bit16s(mt32_context, samples, MT32_AUDIO_TICK_FRAMES);
	SDL_PutAudioStreamData(mt32_stream, samples, sizeof(samples));
}
