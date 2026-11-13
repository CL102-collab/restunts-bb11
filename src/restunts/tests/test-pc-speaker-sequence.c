/* Exercise shipped PC instrument/music/effect resources through the real
 * mapper, sequencer, original speaker arbitration, and SDL dummy output. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include "../c/audio/audio_control.h"
#include "../c/audio/audio_internal.h"
#include "../c/car/car_audio.h"
#include "../c/memmgr.h"
#include "../c/platform.h"
#include "../c/startup_internal.h"
#include "../platform/sdl3/sdl3.h"
#include "../platform/sdl3/music.h"
#include "../platform/sdl3/mt32_audio.h"
#include "../platform/sdl3/pc_speaker_audio.h"

#define TEST_EXPECTED_ARGUMENTS 3
/* The original effect timer services channels 16..22; channel 16 is the engine. */
#define TEST_EFFECT_CHANNEL (AUDIO_EFFECT_CHANNEL_FIRST + 1U)
#define TEST_PATH_BYTES 1024U
#define TEST_MILLISECONDS_PER_SECOND 1000U
#define TEST_TICK_MS (TEST_MILLISECONDS_PER_SECOND / DOS_TIMER_REALTIME_TICKS_PER_SECOND)
#define TEST_SONG_TICKS 400U
#define TEST_EFFECT_TICKS 100U
#define TEST_ENGINE_PITCH 1600U
#define TEST_REPEATED_SWITCHES 4U
#define TEST_PCM_PREFIX_BYTES 16U
#define TEST_PCM_DATA_BYTES 1000U
#define TEST_PCM_NOTE 30U
#define TEST_PCM_RENDER_RATE 44100U
#define TEST_PCM_INSTRUMENT_BYTES 93U
#define TEST_MUSIC_DIRECTORY "assets/music"
#define TEST_MUSIC_PATH TEST_MUSIC_DIRECTORY "/titl.ogg"

extern void audio_start_engine(legacy_s16 index);
extern legacy_s16 audio_play_effect_at_rate(void *resource, legacy_s16 channel, legacy_u8 priority,
											legacy_u16 rate);

static legacy_u64 mock_time_ms = TEST_MILLISECONDS_PER_SECOND;
static legacy_u32 audible_ticks;
static legacy_u32 effect_ticks;
static legacy_u32 sequenced_effect_ticks;
static legacy_u32 period_changes;
static legacy_u16 last_period;

static legacy_u64 test_get_ticks(void)
{
	return mock_time_ms;
}

static void test_delay(legacy_u32 milliseconds)
{
	assert(milliseconds == 1U);
}

static void test_platform_pump(void);
#define SDL_GetTicks test_get_ticks
#define SDL_Delay test_delay
#define sdl3_platform_pump test_platform_pump
#include "../platform/sdl3/timer.c"
#undef SDL_GetTicks
#undef SDL_Delay
#undef sdl3_platform_pump

static void test_platform_pump(void)
{
	sdl3_timer_pump();
}

static void advance(legacy_u32 ticks)
{
	for (legacy_u32 index = 0; index < ticks; index++) {
		mock_time_ms += TEST_TICK_MS;
		sdl3_timer_pump();
		struct PC_SPEAKER_DRIVER *driver = pc_speaker_audio_driver();
		if (driver->output_active) {
			assert(driver->selected_voice > 0 &&
				   (legacy_u8)driver->selected_voice < PC_SPEAKER_CONTEXT_COUNT);
			const struct PC_SPEAKER_VOICE *voice = &driver->voices[driver->selected_voice];
			/* Mixing precedes sequence commands. A later release in this same
			 * tick leaves the PIT output latched until the next mixer pass;
			 * count ownership only while its source context remains active. */
			if (voice->active && voice->volume != 0 && voice->channel != NULL) {
				assert(!driver->music_muted ||
					   voice->channel->channel >= AUDIO_EFFECT_CHANNEL_FIRST);
				audible_ticks++;
				effect_ticks += voice->channel->channel >= AUDIO_EFFECT_CHANNEL_FIRST;
				sequenced_effect_ticks += voice->channel->channel == TEST_EFFECT_CHANNEL;
				period_changes += last_period != driver->output_period;
				last_period = driver->output_period;
			}
		}
	}
}

static void copy_resources(const legacy_char *source_directory)
{
	static const legacy_char *const files[] = {"PCSKIDMS.VCE", "PCENG1.VCE",   "GEENG.SFX",
											   "SKIDTITL.KMS", "SKIDSLCT.KMS", "SKIDOVER.KMS",
											   "SKIDVICT.KMS"};
	for (size_t index = 0; index < sizeof(files) / sizeof(files[0]); index++) {
		legacy_char source[TEST_PATH_BYTES];
		legacy_int length =
			SDL_snprintf(source, sizeof(source), "%s/%s", source_directory, files[index]);
		assert(length > 0 && (size_t)length < sizeof(source));
		assert(SDL_CopyFile(source, files[index]));
	}
}

static void check_ogg_switch(void)
{
	struct AUDIO_CHANNEL channels[AUDIO_CHANNEL_COUNT];
	struct AUDIO_CONTEXT contexts[AUDIO_CONTEXT_COUNT];
	memcpy(channels, audio_channels, sizeof(channels));
	memcpy(contexts, dos_audio_contexts, sizeof(contexts));
	legacy_u16 elapsed = audio_sequence_elapsed_ticks;
	legacy_u16 period = audio_sequence_tick_period;
	struct PC_SPEAKER_DRIVER *driver = pc_speaker_audio_driver();
	legacy_u8 mix_phase = driver->mix_phase;
	legacy_u8 muted = driver->music_muted;
	assert(sdl3_music_toggle());
	assert(memcmp(channels, audio_channels, sizeof(channels)) == 0);
	assert(memcmp(contexts, dos_audio_contexts, sizeof(contexts)) == 0);
	assert(audio_sequence_elapsed_ticks == elapsed && audio_sequence_tick_period == period);
	assert(driver->mix_phase == mix_phase && driver->music_muted != muted);
	if (driver->music_muted && driver->output_active) {
		assert(driver->voices[driver->selected_voice].channel->channel >=
			   AUDIO_EFFECT_CHANNEL_FIRST);
	}
}

static void check_engine(void)
{
	void *voices = load_voice_file("eng1");
	void *effects = load_sfx_file("eng");
	assert(voices != NULL && effects != NULL);
	audio_add_driver_timer();
	legacy_s16 engine =
		audio_init_engine(PLAYER_ENGINE_LEGACY_TYPE, &player_engine_definition, voices, effects);
	assert(engine >= 0);
	audio_start_engine(engine);
	assert(audio_timers[engine].engine_active && audio_timers[engine].engine_context >= 0);
	legacy_s16 context_index = audio_timers[engine].engine_context;
	assert((legacy_u16)context_index < PC_SPEAKER_CONTEXT_COUNT);
	struct PC_SPEAKER_DRIVER *driver = pc_speaker_audio_driver();
	audio_timers[engine].target_volume = AUDIO_ENGINE_MAX_VOLUME;
	audio_timers[engine].target_pitch = TEST_ENGINE_PITCH;
	legacy_u32 before = effect_ticks;
	advance(TEST_EFFECT_TICKS);
	assert(effect_ticks > before);
	assert(driver->voices[context_index].active);
	assert(driver->voices[context_index].note == AUDIO_NOTE_USE_DRIVER_VALUE);
	assert(driver->voices[context_index].base_period != 0);
	for (legacy_u32 index = 0; index < TEST_REPEATED_SWITCHES; index++) {
		check_ogg_switch();
	}
	check_ogg_switch();
	assert(driver->music_muted);
	before = effect_ticks;
	legacy_u16 old_period = driver->voices[context_index].base_period;
	audio_timers[engine].target_pitch += 40U;
	advance(TEST_EFFECT_TICKS);
	assert(effect_ticks > before && driver->voices[context_index].base_period != old_period);
	const legacy_s8 *const names[] = {"SKID", "CRAS", "BUMP"};
	for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
		void *header = init_audio_resources(effects, voices, names[index]);
		assert(header != NULL);
		before = sequenced_effect_ticks;
		assert(audio_play_effect_at_rate(header, TEST_EFFECT_CHANNEL, AUDIO_ENGINE_MAX_VOLUME,
										 audio_effect_rate) >= 0);
		advance(TEST_EFFECT_TICKS);
		assert(sequenced_effect_ticks > before);
	}
	check_ogg_switch();
	assert(!driver->music_muted);
	audio_stop_effect_channel(TEST_EFFECT_CHANNEL);
	audio_remove_driver_timer();
	mmgr_release(effects);
	mmgr_release(voices);
}

static void check_songs(const legacy_char *ogg_path)
{
	static const legacy_s8 *const songs[] = {"skidtitl", "skidslct", "skidover", "skidvict",
											 "skidtitl"};
	static const legacy_s8 *const names[] = {"titl", "slct", "over", "vict", "titl"};
	void *voices = load_voice_file("skidms");
	assert(voices != NULL);
	assert(SDL_CreateDirectory(TEST_MUSIC_DIRECTORY));
	assert(SDL_CopyFile(ogg_path, TEST_MUSIC_PATH));
	for (size_t index = 0; index < sizeof(songs) / sizeof(songs[0]); index++) {
		void *song = load_song_file(songs[index]);
		assert(song != NULL);
		void *header = init_audio_resources(song, voices, names[index]);
		assert(header != NULL);
		load_audio_finalize(header);
		assert(audio_music_active && audio_music_channel_count != 0);
		legacy_u32 before = audible_ticks;
		legacy_u32 changes = period_changes;
		advance(TEST_SONG_TICKS);
		assert(audible_ticks > before && period_changes > changes);
		if (index == 0) {
			for (legacy_u32 toggle = 0; toggle < TEST_REPEATED_SWITCHES; toggle++) {
				check_ogg_switch();
			}
			check_engine();
		}
		audio_stop_music();
		mmgr_release(song);
	}
	mmgr_release(voices);
	assert(SDL_RemovePath(TEST_MUSIC_PATH));
}

static void check_pcm_allocation_bounds(void)
{
	const size_t sample_bytes = PC_SPEAKER_SAMPLE_HEADER_BYTES + TEST_PCM_DATA_BYTES;
	legacy_u8 *allocation =
		mmgr_alloc_resbytes("PCPCM", (legacy_s32)(TEST_PCM_PREFIX_BYTES + sample_bytes));
	assert(allocation != NULL);
	legacy_u8 *sample = allocation + TEST_PCM_PREFIX_BYTES;
	memset(sample, 0, sample_bytes);
	LEGACY_WRITE_U32_LE(sample + PC_SPEAKER_SAMPLE_LENGTH_OFFSET, TEST_PCM_DATA_BYTES);
	memset(sample + PC_SPEAKER_SAMPLE_HEADER_BYTES, 255, TEST_PCM_DATA_BYTES);
	assert(mmgr_get_pointer_size_bytes(sample) >= sample_bytes);
	legacy_u8 instrument[TEST_PCM_INSTRUMENT_BYTES];
	memset(instrument, 0, sizeof(instrument));
	assert(mmgr_get_pointer_size_bytes(instrument) == 0);
	audio_write_far_pointer(instrument + PC_SPEAKER_SAMPLE_REFERENCE_OFFSET, sample);
	dos_audio_driver_activate_context(0, &dos_audio_contexts[0],
									  (legacy_u8 *)&audio_channels[TEST_EFFECT_CHANNEL],
									  TEST_PCM_NOTE, 0, instrument);
	struct PC_SPEAKER_DRIVER *driver = pc_speaker_audio_driver();
	assert(driver->sample_active && driver->sample_length == TEST_PCM_DATA_BYTES);
	assert(driver->sample_data != sample + PC_SPEAKER_SAMPLE_HEADER_BYTES);
	/* Even the sample header must fit before any of its fields are read. */
	audio_write_far_pointer(instrument + PC_SPEAKER_SAMPLE_REFERENCE_OFFSET,
							sample + mmgr_get_pointer_size_bytes(sample) - 1U);
	dos_audio_driver_activate_context(0, &dos_audio_contexts[0],
									  (legacy_u8 *)&audio_channels[TEST_EFFECT_CHANNEL],
									  TEST_PCM_NOTE, 0, instrument);
	assert(!driver->sample_active && driver->sample_data == NULL);
	audio_write_far_pointer(instrument + PC_SPEAKER_SAMPLE_REFERENCE_OFFSET, sample);
	/* A length beyond the real allocation is rejected by the complete glue,
	 * rather than being trusted as the bound passed into the driver. */
	LEGACY_WRITE_U32_LE(sample + PC_SPEAKER_SAMPLE_LENGTH_OFFSET,
						mmgr_get_pointer_size_bytes(sample));
	dos_audio_driver_activate_context(0, &dos_audio_contexts[0],
									  (legacy_u8 *)&audio_channels[TEST_EFFECT_CHANNEL],
									  TEST_PCM_NOTE, 0, instrument);
	assert(!driver->sample_active && driver->sample_data == NULL);
	LEGACY_WRITE_U32_LE(sample + PC_SPEAKER_SAMPLE_LENGTH_OFFSET, TEST_PCM_DATA_BYTES);
	dos_audio_driver_activate_context(0, &dos_audio_contexts[0],
									  (legacy_u8 *)&audio_channels[TEST_EFFECT_CHANNEL],
									  TEST_PCM_NOTE, 0, instrument);
	mmgr_release(allocation);
	memset(sample, 0, sample_bytes);
	pc_speaker_driver_mix(driver);
	legacy_s16 output;
	pc_speaker_driver_render(driver, &output, 1, TEST_PCM_RENDER_RATE);
	assert(output > 0);
	dos_audio_driver_end_context(0, &dos_audio_contexts[0]);
	assert(!driver->sample_active && driver->sample_data == NULL);
}

legacy_int main(legacy_int argc, legacy_char **argv)
{
	assert(argc == TEST_EXPECTED_ARGUMENTS);
	SDL_SetMainReady();
	assert(SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
	copy_resources(argv[1]);
	mmgr_init_conventional_arena();
	dos_timer_setup_interrupt();
	sdl3_batch_mode = 0;
	sdl3_audio_configure(STARTUP_SOUND_PC_SPEAKER, NULL, NULL, 0);
	assert(audio_load_dos_driver("pc15.drv", 0, 0) == 0);
	assert(!dos_audio_uses_direct_channels && dos_audio_context_count == PC_SPEAKER_CONTEXT_COUNT);
	assert(strcmp(SDL_GetCurrentAudioDriver(), "dummy") == 0);
	check_songs(argv[2]);
	check_pcm_allocation_bounds();
	dos_audio_shutdown();
	dos_timer_shutdown();
	SDL_Quit();
	puts("PC speaker game music, continuous engine/effects, and Ogg ownership checks passed.");
	return 0;
}
