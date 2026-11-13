/* Run the original MT voice banks and game sequences through the complete
 * native driver glue. A capture replaces Munt output, so this test needs game
 * data but never proprietary Roland ROMs or physical audio hardware. */
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

#define TEST_EXPECTED_ARGUMENTS 3
/* The original timer services effect channels 16..22; channel 16 is the engine. */
#define TEST_EFFECT_CHANNEL (AUDIO_EFFECT_CHANNEL_FIRST + 1U)
#define TEST_PATH_BYTES 1024U
#define TEST_MILLISECONDS_PER_SECOND 1000U
#define TEST_TICK_MS (TEST_MILLISECONDS_PER_SECOND / DOS_TIMER_REALTIME_TICKS_PER_SECOND)
#define TEST_SONG_TICKS 400U
#define TEST_EFFECT_TICKS 100U
#define TEST_REPEATED_SWITCHES 4U
#define TEST_SYSEX_BYTES 512U
#define TEST_MIDI_CHANNEL_MASK 15U
#define TEST_MIDI_COMMAND_MASK 0xf0U
#define TEST_MIDI_DATA_MASK 127U
#define TEST_RHYTHM_CHANNEL 9U
#define TEST_CONTEXT_PLAYING_NOTE_OFFSET 3U
#define TEST_MUSIC_DIRECTORY "assets/music"
#define TEST_MUSIC_PATH TEST_MUSIC_DIRECTORY "/titl.ogg"

extern void audio_start_engine(legacy_s16 index);
extern legacy_s16 audio_play_effect_at_rate(void *resource, legacy_s16 channel, legacy_u8 priority,
											legacy_u16 rate);

static legacy_u64 mock_time_ms = TEST_MILLISECONDS_PER_SECOND;

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

struct MIDI_CAPTURE {
	legacy_u32 notes[MT32_MIDI_CHANNEL_COUNT];
	legacy_u32 effect_notes[MT32_MIDI_CHANNEL_COUNT];
	legacy_u32 stops;
	legacy_u32 programs;
	legacy_u32 bends;
	legacy_u32 sysex;
	legacy_u32 patches;
	legacy_u32 timbres;
	legacy_u8 volume[MT32_MIDI_CHANNEL_COUNT];
	legacy_u8 status;
	legacy_u8 first;
	legacy_u8 have_first;
	legacy_u8 sysex_bytes[TEST_SYSEX_BYTES];
	size_t sysex_length;
};

static void capture_sysex(struct MIDI_CAPTURE *output)
{
	static const legacy_u8 header[] = {0xf0U, 0x41U, 0x10U, 0x16U, 0x12U};
	assert(output->sysex_length >= 10U);
	assert(memcmp(output->sysex_bytes, header, sizeof(header)) == 0);
	legacy_u32 checksum = 0;
	for (size_t index = sizeof(header); index + 1U < output->sysex_length; index++) {
		assert(output->sysex_bytes[index] <= TEST_MIDI_DATA_MASK);
		checksum += output->sysex_bytes[index];
	}
	assert((checksum & TEST_MIDI_DATA_MASK) == 0);
	output->sysex++;
	output->patches += output->sysex_bytes[5] == 5U;
	output->timbres += output->sysex_bytes[5] == 8U;
	output->sysex_length = 0;
}

static legacy_u8 is_sequenced_effect_note(legacy_u8 channel, legacy_u8 note)
{
	if (mt32_audio_driver()->music_channels[channel]) {
		return 0;
	}
	for (size_t index = 0; index < AUDIO_CONTEXT_COUNT; index++) {
		const struct AUDIO_CONTEXT *context = &dos_audio_contexts[index];
		if (context->state == AUDIO_CONTEXT_STATE_PLAYING &&
			context->channel == TEST_EFFECT_CHANNEL && context->driver_channel == channel &&
			context->driver_private_state[TEST_CONTEXT_PLAYING_NOTE_OFFSET] == note) {
			return 1;
		}
	}
	return 0;
}

static void capture(void *instance, const legacy_u8 *bytes, size_t length)
{
	struct MIDI_CAPTURE *output = instance;
	for (size_t index = 0; index < length; index++) {
		legacy_u8 byte = bytes[index];
		if (byte >= 0xf8U) {
			continue;
		}
		if (byte == 0xf0U || output->sysex_length != 0) {
			assert(output->sysex_length < sizeof(output->sysex_bytes));
			output->sysex_bytes[output->sysex_length++] = byte;
			output->status = 0;
			output->have_first = 0;
			if (byte == 0xf7U) {
				capture_sysex(output);
			}
			continue;
		}
		if (byte & 0x80U) {
			assert(byte < 0xf0U);
			output->status = byte;
			output->have_first = 0;
			continue;
		}
		assert(output->status != 0);
		legacy_u8 command = output->status & TEST_MIDI_COMMAND_MASK;
		legacy_u8 channel = output->status & TEST_MIDI_CHANNEL_MASK;
		if (command == 0xc0U || command == 0xd0U) {
			output->programs += command == 0xc0U;
		} else if (!output->have_first) {
			output->first = byte;
			output->have_first = 1;
		} else {
			output->notes[channel] += command == 0x90U && byte != 0;
			if (command == 0x90U && byte != 0 && is_sequenced_effect_note(channel, output->first)) {
				output->effect_notes[channel]++;
			}
			output->stops += command == 0x80U || (command == 0x90U && byte == 0);
			output->bends += command == 0xe0U;
			if (command == 0xb0U && output->first == 7U) {
				output->volume[channel] = byte;
			}
			output->have_first = 0;
		}
	}
}

static legacy_u32 note_count(const struct MIDI_CAPTURE *output)
{
	legacy_u32 count = 0;
	for (size_t index = 0; index < MT32_MIDI_CHANNEL_COUNT; index++) {
		count += output->notes[index];
	}
	return count;
}

static void advance(legacy_u32 ticks)
{
	for (legacy_u32 index = 0; index < ticks; index++) {
		mock_time_ms += TEST_TICK_MS;
		sdl3_timer_pump();
	}
}

static void copy_resources(const legacy_char *source_directory)
{
	static const legacy_char *const files[] = {"MT32.PLB",	   "MTSKIDMS.VCE", "MTENG1.VCE",
											   "GEENG.SFX",	   "SKIDTITL.KMS", "SKIDSLCT.KMS",
											   "SKIDOVER.KMS", "SKIDVICT.KMS"};
	for (size_t index = 0; index < sizeof(files) / sizeof(files[0]); index++) {
		legacy_char source[TEST_PATH_BYTES];
		legacy_int length =
			SDL_snprintf(source, sizeof(source), "%s/%s", source_directory, files[index]);
		assert(length > 0 && (size_t)length < sizeof(source));
		assert(SDL_CopyFile(source, files[index]));
	}
}

static void check_ogg_switch(struct MIDI_CAPTURE *output)
{
	struct AUDIO_CHANNEL channels[AUDIO_CHANNEL_COUNT];
	struct AUDIO_CONTEXT contexts[AUDIO_CONTEXT_COUNT];
	memcpy(channels, audio_channels, sizeof(channels));
	memcpy(contexts, dos_audio_contexts, sizeof(contexts));
	legacy_u16 elapsed = audio_sequence_elapsed_ticks;
	legacy_u16 period = audio_sequence_tick_period;
	assert(sdl3_music_toggle());
	assert(memcmp(channels, audio_channels, sizeof(channels)) == 0);
	assert(memcmp(contexts, dos_audio_contexts, sizeof(contexts)) == 0);
	assert(audio_sequence_elapsed_ticks == elapsed && audio_sequence_tick_period == period);
	struct MT32_DRIVER *driver = mt32_audio_driver();
	legacy_u32 musical_channels = 0;
	for (size_t index = 0; index < MT32_MIDI_CHANNEL_COUNT; index++) {
		if (driver->music_channels[index]) {
			musical_channels++;
			assert(output->volume[index] == (driver->music_muted ? 0 : driver->volumes[index]));
		}
	}
	assert(musical_channels != 0);
}

static void check_engine(struct MIDI_CAPTURE *output)
{
	void *voices = load_voice_file("eng1");
	void *effects = load_sfx_file("eng");
	assert(voices != NULL && effects != NULL);
	audio_add_driver_timer();
	legacy_s16 engine =
		audio_init_engine(PLAYER_ENGINE_LEGACY_TYPE, &player_engine_definition, voices, effects);
	assert(engine >= 0);
	legacy_u32 notes = note_count(output);
	legacy_u32 bends = output->bends;
	audio_start_engine(engine);
	assert(audio_timers[engine].engine_active);
	assert(audio_timers[engine].engine_context >= 0);
	audio_timers[engine].target_volume = AUDIO_ENGINE_MAX_VOLUME;
	advance(TEST_EFFECT_TICKS);
	assert(note_count(output) > notes && output->bends > bends);
	legacy_u8 driver_channel =
		dos_audio_contexts[audio_timers[engine].engine_context].driver_channel;
	assert(!mt32_audio_driver()->music_channels[driver_channel]);
	assert(output->volume[driver_channel] != 0);
	for (legacy_u32 index = 0; index < TEST_REPEATED_SWITCHES; index++) {
		check_ogg_switch(output);
		assert(output->volume[driver_channel] != 0);
	}
	/* A second engine pitch and the skid/crash sequences must remain audible
	 * while Ogg mutes music-owned channels. */
	check_ogg_switch(output);
	assert(mt32_audio_driver()->music_muted);
	audio_timers[engine].target_pitch += 40U;
	bends = output->bends;
	advance(TEST_EFFECT_TICKS);
	assert(output->bends > bends);
	const legacy_u8 effect_midi_channel =
		(TEST_EFFECT_CHANNEL & AUDIO_DIRECT_CHANNEL_MASK) + AUDIO_DRIVER_CHANNEL_BASE;
	assert(effect_midi_channel != driver_channel);
	const legacy_s8 *const names[] = {"SKID", "CRAS", "BUMP"};
	for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
		void *header = init_audio_resources(effects, voices, names[index]);
		assert(header != NULL);
		notes = output->effect_notes[effect_midi_channel];
		assert(audio_play_effect_at_rate(header, TEST_EFFECT_CHANNEL, AUDIO_ENGINE_MAX_VOLUME,
										 audio_effect_rate) >= 0);
		advance(TEST_EFFECT_TICKS);
		/* A note must belong to this effect's live context, on its own MIDI
		 * channel. Continuing music/engine notes cannot satisfy this assertion
		 * while Ogg owns music playback. */
		assert(output->effect_notes[effect_midi_channel] > notes);
	}
	check_ogg_switch(output);
	assert(!mt32_audio_driver()->music_muted);
	audio_stop_effect_channel(TEST_EFFECT_CHANNEL);
	audio_remove_driver_timer();
	mmgr_release(effects);
	mmgr_release(voices);
}

static void check_songs(struct MIDI_CAPTURE *output, const legacy_char *ogg_path)
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
		legacy_u32 notes = note_count(output);
		legacy_u32 programs = output->programs;
		advance(TEST_SONG_TICKS);
		assert(note_count(output) > notes && output->programs > programs);
		if (index == 0) {
			assert(output->notes[TEST_RHYTHM_CHANNEL] != 0);
			for (legacy_u32 toggle = 0; toggle < TEST_REPEATED_SWITCHES; toggle++) {
				check_ogg_switch(output);
			}
			check_engine(output);
		}
		audio_stop_music();
		mmgr_release(song);
	}
	mmgr_release(voices);
	assert(SDL_RemovePath(TEST_MUSIC_PATH));
}

legacy_int main(legacy_int argc, legacy_char **argv)
{
	assert(argc == TEST_EXPECTED_ARGUMENTS);
	SDL_SetMainReady();
	assert(SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
	copy_resources(argv[1]);
	mmgr_init_conventional_arena();
	dos_timer_setup_interrupt();
	sdl3_batch_mode = 1;
	sdl3_audio_configure(STARTUP_SOUND_MT32, NULL, NULL, 0);
	struct MIDI_CAPTURE output;
	memset(&output, 0, sizeof(output));
	mt32_driver_initialize(mt32_audio_driver(), capture, &output);
	assert(audio_load_dos_driver("mt15.drv", 0, 0) == 0);
	assert(dos_audio_uses_direct_channels && dos_audio_context_count == AUDIO_CONTEXT_COUNT);
	assert(output.patches != 0 && output.timbres != 0);
	/* Only synth initialization is bypassed. Exercise real replacement playback
	 * and normal main-thread timer dispatch after the capture is installed. */
	sdl3_batch_mode = 0;
	check_songs(&output, argv[2]);
	assert(output.stops != 0 && output.bends != 0 && output.sysex_length == 0);
	dos_audio_shutdown();
	dos_timer_shutdown();
	SDL_Quit();
	puts("MT-32 game-resource sequences, engine effects, and Ogg ownership checks passed.");
	return 0;
}
