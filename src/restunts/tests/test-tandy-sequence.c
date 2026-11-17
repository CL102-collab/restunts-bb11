/* Run the shipped TD instruments, game music and effects through the native
 * Tandy adapter and SDL dummy output. No DOS driver code is executed. */
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
#include "../platform/sdl3/tandy_audio.h"

#define TEST_EXPECTED_ARGUMENTS 3
#define TEST_EFFECT_CHANNEL (AUDIO_EFFECT_CHANNEL_FIRST + 1U)
#define TEST_PATH_BYTES 1024U
#define TEST_MILLISECONDS_PER_SECOND 1000U
#define TEST_TICK_MS (TEST_MILLISECONDS_PER_SECOND / DOS_TIMER_REALTIME_TICKS_PER_SECOND)
#define TEST_SONG_TICKS 400U
#define TEST_EFFECT_TICKS 100U
#define TEST_ENGINE_PITCH 3200U
#define TEST_ENGINE_PITCH_CHANGE 40U
#define TEST_MOVING_CAR_VOLUME (AUDIO_ENGINE_MAX_VOLUME / 2U)
#define TEST_REPEATED_SWITCHES 4U
#define TEST_PROBE_FRAMES 64U
#define TEST_SAMPLE_RATE 44100U
#define TEST_PCM_PREFIX_BYTES 16U
#define TEST_PCM_DATA_BYTES 1000U
#define TEST_PCM_NOTE 30U
#define TEST_PCM_INSTRUMENT_BYTES 93U
#define TEST_MUSIC_DIRECTORY "assets/music"
#define TEST_MUSIC_PATH TEST_MUSIC_DIRECTORY "/titl.ogg"

extern void audio_start_engine(legacy_s16 index);
extern void audio_play_paved_skid(legacy_s16 index);
extern void audio_stop_skid_sound(legacy_s16 index);
extern legacy_s16 audio_play_effect_at_rate(void *resource, legacy_s16 channel, legacy_u8 priority,
											legacy_u16 rate);

static legacy_u64 mock_time_ms = TEST_MILLISECONDS_PER_SECOND;
static legacy_u32 audible_ticks;
static legacy_u32 effect_ticks;
static legacy_u32 sequenced_effect_ticks;
static legacy_u32 period_changes;
static legacy_u16 last_periods[TANDY_CONTEXT_COUNT];

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

static legacy_u8 probe_audible(const struct TANDY_DRIVER *driver)
{
	/* Rendering a snapshot reads the owned sample bytes without advancing
	 * live PCM positions or oscillator phases in the audio owner. */
	struct TANDY_DRIVER snapshot = *driver;
	legacy_s16 samples[TEST_PROBE_FRAMES];
	tandy_driver_render(&snapshot, samples, TEST_PROBE_FRAMES, TEST_SAMPLE_RATE);
	for (size_t index = 0; index < TEST_PROBE_FRAMES; index++) {
		if (samples[index] != 0) {
			return 1;
		}
	}
	return 0;
}

static void advance(legacy_u32 ticks)
{
	for (legacy_u32 tick = 0; tick < ticks; tick++) {
		mock_time_ms += TEST_TICK_MS;
		sdl3_timer_pump();
		const struct TANDY_DRIVER *driver = tandy_audio_driver();
		legacy_u8 has_effect = 0;
		legacy_u8 has_sequenced_effect = 0;
		for (size_t index = 0; index < TANDY_CONTEXT_COUNT; index++) {
			const struct TANDY_VOICE *voice = &driver->voices[index];
			if (!voice->active || voice->level == 0 || voice->channel == NULL) {
				continue;
			}
			period_changes += voice->period != last_periods[index];
			last_periods[index] = voice->period;
			has_effect |= voice->channel->channel >= AUDIO_EFFECT_CHANNEL_FIRST;
			has_sequenced_effect |= voice->channel->channel == TEST_EFFECT_CHANNEL;
		}
		legacy_u8 audible = probe_audible(driver);
		audible_ticks += audible;
		effect_ticks += audible && has_effect;
		sequenced_effect_ticks += audible && has_sequenced_effect;
	}
}

static void copy_resources(const legacy_char *source_directory)
{
	/* Keeping only TD voice banks here catches accidental PC/Ad Lib fallback. */
	static const legacy_char *const files[] = {"TDSKIDMS.VCE", "TDENG1.VCE",   "GEENG.SFX",
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
	struct TANDY_VOICE voices[TANDY_CONTEXT_COUNT];
	memcpy(channels, audio_channels, sizeof(channels));
	memcpy(contexts, dos_audio_contexts, sizeof(contexts));
	struct TANDY_DRIVER *driver = tandy_audio_driver();
	memcpy(voices, driver->voices, sizeof(voices));
	legacy_u16 elapsed = audio_sequence_elapsed_ticks;
	legacy_u16 period = audio_sequence_tick_period;
	legacy_u8 muted = driver->music_muted;
	assert(sdl3_music_toggle());
	assert(memcmp(channels, audio_channels, sizeof(channels)) == 0);
	assert(memcmp(contexts, dos_audio_contexts, sizeof(contexts)) == 0);
	assert(memcmp(voices, driver->voices, sizeof(voices)) == 0);
	assert(audio_sequence_elapsed_ticks == elapsed && audio_sequence_tick_period == period);
	assert(driver->music_muted != muted);
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
	assert((legacy_u16)context_index < TANDY_CONTEXT_COUNT);
	struct TANDY_DRIVER *driver = tandy_audio_driver();
	audio_timers[engine].target_volume = AUDIO_ENGINE_MAX_VOLUME;
	audio_timers[engine].target_pitch = TEST_ENGINE_PITCH;
	legacy_u32 before = effect_ticks;
	advance(TEST_EFFECT_TICKS);
	assert(effect_ticks > before);
	assert(driver->voices[context_index].active);
	/* TDENG1 transposes the continuous engine sentinel down two octaves. */
	assert(LEGACY_S8_FROM_BITS(driver->voices[context_index].note) < 0);
	assert(driver->voices[context_index].base_period != 0);
	for (legacy_u32 index = 0; index < TEST_REPEATED_SWITCHES; index++) {
		check_ogg_switch();
	}
	check_ogg_switch();
	assert(driver->music_muted);
	before = effect_ticks;
	legacy_u16 old_period = driver->voices[context_index].base_period;
	audio_timers[engine].target_pitch += TEST_ENGINE_PITCH_CHANGE;
	advance(TEST_EFFECT_TICKS);
	assert(effect_ticks > before && driver->voices[context_index].base_period != old_period);
	check_ogg_switch();
	assert(!driver->music_muted);
	/* Gameplay effects share the original tone/noise destinations with menu
	 * music. Stop the menu song as the game does when entering a race. */
	audio_stop_music();
	advance(TEST_EFFECT_TICKS);
	/* SKID starts silent in GEENG.SFX; the car timer supplies its spatial
	 * volume. Use the gameplay entry point while that volume interpolates. */
	audio_timers[engine].target_volume = TEST_MOVING_CAR_VOLUME;
	before = sequenced_effect_ticks;
	audio_play_paved_skid(engine);
	assert(audio_timers[engine].secondary_effect_channel == TEST_EFFECT_CHANNEL);
	advance(TEST_EFFECT_TICKS);
	assert(sequenced_effect_ticks > before);
	audio_stop_skid_sound(engine);
	const legacy_s8 *const names[] = {"CRAS", "BUMP"};
	for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
		void *header = init_audio_resources(effects, voices, names[index]);
		assert(header != NULL);
		before = sequenced_effect_ticks;
		audio_stop_effect_channel(TEST_EFFECT_CHANNEL);
		assert(audio_play_effect_at_rate(header, TEST_EFFECT_CHANNEL, AUDIO_ENGINE_MAX_VOLUME,
										 audio_effect_rate) >= 0);
		advance(TEST_EFFECT_TICKS);
		assert(sequenced_effect_ticks > before);
	}
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
	const size_t sample_bytes = TANDY_SAMPLE_HEADER_BYTES + TEST_PCM_DATA_BYTES;
	legacy_u8 *allocation =
		mmgr_alloc_resbytes("TDPCM", (legacy_s32)(TEST_PCM_PREFIX_BYTES + sample_bytes));
	assert(allocation != NULL);
	legacy_u8 *sample = allocation + TEST_PCM_PREFIX_BYTES;
	memset(sample, 0, sample_bytes);
	LEGACY_WRITE_U32_LE(sample + TANDY_SAMPLE_LENGTH_OFFSET, TEST_PCM_DATA_BYTES);
	memset(sample + TANDY_SAMPLE_HEADER_BYTES, LEGACY_U8_MAX, TEST_PCM_DATA_BYTES);
	legacy_u8 instrument[TEST_PCM_INSTRUMENT_BYTES] = {0};
	audio_write_far_pointer(instrument + TANDY_SAMPLE_REFERENCE_OFFSET, sample);
	dos_audio_driver_activate_context(0, &dos_audio_contexts[0],
									  (legacy_u8 *)&audio_channels[TEST_EFFECT_CHANNEL],
									  TEST_PCM_NOTE, AUDIO_ENGINE_MAX_VOLUME, instrument);
	struct TANDY_DRIVER *driver = tandy_audio_driver();
	assert(driver->sample.sample_active && driver->sample.sample_length == TEST_PCM_DATA_BYTES);
	assert(driver->sample.sample_data != sample + TANDY_SAMPLE_HEADER_BYTES);
	/* Borrowed bank bytes must be copied before the command releases them. */
	memset(sample + TANDY_SAMPLE_HEADER_BYTES, 0, TEST_PCM_DATA_BYTES);
	assert(driver->sample.sample_data[0] == LEGACY_U8_MAX);
	/* A pointer inside an allocation still needs a complete sample header. */
	audio_write_far_pointer(instrument + TANDY_SAMPLE_REFERENCE_OFFSET,
							sample + mmgr_get_pointer_size_bytes(sample) - 1U);
	dos_audio_driver_activate_context(0, &dos_audio_contexts[0],
									  (legacy_u8 *)&audio_channels[TEST_EFFECT_CHANNEL],
									  TEST_PCM_NOTE, AUDIO_ENGINE_MAX_VOLUME, instrument);
	assert(!driver->sample.sample_active && driver->sample.sample_data == NULL);
	audio_write_far_pointer(instrument + TANDY_SAMPLE_REFERENCE_OFFSET, sample);
	LEGACY_WRITE_U32_LE(sample + TANDY_SAMPLE_LENGTH_OFFSET, mmgr_get_pointer_size_bytes(sample));
	dos_audio_driver_activate_context(0, &dos_audio_contexts[0],
									  (legacy_u8 *)&audio_channels[TEST_EFFECT_CHANNEL],
									  TEST_PCM_NOTE, AUDIO_ENGINE_MAX_VOLUME, instrument);
	assert(!driver->sample.sample_active && driver->sample.sample_data == NULL);
	mmgr_release(allocation);
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
	sdl3_audio_configure(STARTUP_SOUND_TANDY, NULL, NULL, 0);
	assert(audio_load_dos_driver("td15.drv", 0, 0) == 0);
	assert(sdl3_audio_uses_tandy());
	assert(!dos_audio_uses_direct_channels && dos_audio_context_count == TANDY_CONTEXT_COUNT);
	assert(strcmp(SDL_GetCurrentAudioDriver(), "dummy") == 0);
	check_songs(argv[2]);
	check_pcm_allocation_bounds();
	dos_audio_shutdown();
	assert(!tandy_audio_driver()->sample.sample_active);
	assert(tandy_audio_driver()->sample.sample_data == NULL);
	dos_timer_shutdown();
	SDL_Quit();
	puts("Tandy original game music, engine/effects and Ogg continuity checks passed.");
	return 0;
}
