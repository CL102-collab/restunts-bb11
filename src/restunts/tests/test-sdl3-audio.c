/* Exercises the native driver against a shipped AdLib instrument and real
 * YM3812 synthesis without requiring an audio device. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include "../platform/sdl3/audio.c"

void dos_show_startup_error(const legacy_s8 *message)
{
	assert(message != NULL && message[0] != 0);
}

legacy_s32 sdl3_batch_mode;
static legacy_u32 removed_callbacks;

#define TEST_MUTE_SETTLE_SAMPLES 256U
#define TEST_MUTE_NOTE 72
#define TEST_MUTE_VOLUME 96
#define TEST_MUTE_CONTROL_VOLUME 64
#define TEST_MUTE_CONTROL_LEVEL 32
#define TEST_MUTE_MIN_ENERGY 1000000U
#define TEST_MUTE_ENERGY_RATIO 100U

/* Optional music has its own decoder/device regression fixture. */
void sdl3_music_update(void)
{
}

void sdl3_music_shutdown(void)
{
}

void audio_sequence_timer(void)
{
}

/* This driver-only fixture has no running clock or sequencer. */
void sdl3_timer_pump(void)
{
}

void timer_remove_callback(void (*callback)(void))
{
	assert(callback == audio_sequence_timer);
	++removed_callbacks;
}

legacy_u32 resource_read_u32le(const legacy_u8 *source)
{
	return LEGACY_READ_U32_LE(source);
}

/* The driver calls this sequencer helper only when rebinding a resource;
 * the tests below prepare voices through the driver API directly. */
void audio_write_far_pointer(legacy_u8 *destination, const void *value)
{
	(void)destination;
	(void)value;
	assert(0 && "Unexpected sequencer resource rebinding");
}

static void load_first_instrument(const legacy_char *path, legacy_u8 *instrument)
{
	FILE *file = fopen(path, "rb");
	assert(file != NULL);
	legacy_u8 header[6];
	assert(fread(header, 1, sizeof(header), file) == sizeof(header));
	legacy_u32 count = LEGACY_READ_U16_LE(header + 4);
	assert(count > 0 && count < 256);
	assert(fseek(file, 6L + count * 4L, SEEK_SET) == 0);
	legacy_u8 offset[4];
	assert(fread(offset, 1, sizeof(offset), file) == sizeof(offset));
	assert(fseek(file, 6L + count * 8L + LEGACY_READ_U32_LE(offset), SEEK_SET) == 0);
	assert(fread(instrument, 1, ADLIB_RESOURCE_SIZE, file) == ADLIB_RESOURCE_SIZE);
	fclose(file);
}

static legacy_s16 generate_sample(void)
{
	legacy_s16 sample;
	adlib_generate_samples(&sample, 1);
	return sample;
}

static legacy_u64 pcm_energy(void)
{
	legacy_u64 energy = 0;
	for (legacy_u32 frame = 0; frame < ADLIB_SAMPLE_RATE / 4; ++frame) {
		legacy_s16 sample = generate_sample();
		energy += (legacy_u32)((legacy_s32)sample * sample);
	}
	return energy;
}

static void make_sine_instrument(legacy_u8 instrument[ADLIB_RESOURCE_SIZE])
{
	memset(instrument, 0, ADLIB_RESOURCE_SIZE);
	LEGACY_WRITE_U32_LE(instrument, ADLIB_RESOURCE_SIZE);
	/* An inactive modulator and a sustained sine carrier isolate the sample
	 * rate from the timbre, tremolo and envelopes of the shipped instruments. */
	instrument[74] = 63;
	instrument[76] = 1;
	instrument[78] = 1;
	instrument[82] = 15;
	instrument[85] = 15;
	instrument[88] = 1;
	instrument[90] = 1;
}

static void generate_samples(legacy_u32 count)
{
	while (count-- != 0) {
		(void)generate_sample();
	}
}

static void check_sample_rate_and_retrigger(struct AUDIO_CHANNEL *channel,
											struct AUDIO_CONTEXT *context)
{
	legacy_u8 instrument[ADLIB_RESOURCE_SIZE];
	make_sine_instrument(instrument);
	channel->pitch = 0;
	channel->volume = 127;
	dos_audio_driver_prepare_context(1, context, (legacy_u8 *)channel, instrument);
	dos_audio_driver_activate_context(1, context, (legacy_u8 *)channel, 72, 127, instrument);
	/* F-number 580/block 4 is approximately 440 Hz at the YM3812 clock. Calling
	 * the unresampled generator at 44100 Hz instead would produce about 390 Hz. */
	adlib_write_pitch(0, (4U << 10U) | 580U, 1);
	generate_samples(ADLIB_SAMPLE_RATE / 10U);
	legacy_u32 crossings = 0;
	legacy_u64 energy = 0;
	legacy_s16 previous = 0;
	for (legacy_u32 index = 0; index < ADLIB_SAMPLE_RATE; ++index) {
		legacy_s16 sample = generate_sample();
		if (previous <= 0 && sample > 0) {
			crossings++;
		}
		energy += (legacy_u32)((legacy_s32)sample * sample);
		previous = sample;
	}
	assert(energy > 1000000);
	assert(crossings >= 438U && crossings <= 442U);

	dos_audio_driver_activate_context(1, context, (legacy_u8 *)channel, 72, 127, instrument);
	generate_samples(1000);
	legacy_u32 previous_phase = adlib_chip->slot[3].pg_phase;
	assert(previous_phase > 1000U);
	/* Buffered writes must preserve both transitions and restart the carrier. */
	dos_audio_driver_activate_context(1, context, (legacy_u8 *)channel, 72, 127, instrument);
	legacy_s32 restarted = 0;
	for (legacy_u32 index = 0; index < 64U; ++index) {
		generate_samples(1);
		legacy_u32 phase = adlib_chip->slot[3].pg_phase;
		if (phase < previous_phase) {
			restarted = 1;
		}
		previous_phase = phase;
	}
	assert(restarted);

	dos_audio_driver_release_channel(1);
	printf("%s: %" LEGACY_PRIu32 "Hz sine, same-tick note retrigger passed\n", ADLIB_EMULATOR,
		   crossings);
}

static void check_octave_crossing_bends(struct AUDIO_CHANNEL *channel,
										struct AUDIO_CONTEXT *context)
{
	static const struct {
		legacy_s16 note;
		legacy_s16 bend;
		legacy_s16 modulation;
		legacy_u16 frequency;
	} cases[] = {{71, 4096, 0, 172}, {71, 8191, 0, 181},  {60, -4096, 0, 81},
				 {60, -8192, 0, 76}, {71, 4097, 30, 186}, {60, -4097, -20, 72}};
	legacy_u8 instrument[ADLIB_RESOURCE_SIZE];
	make_sine_instrument(instrument);
	instrument[18] = 2;
	instrument[40] = 0x90;
	context->level = 0;
	context->sequence_value = 0;
	for (legacy_u32 index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
		channel->pitch = (legacy_u16)cases[index].bend;
		context->modulation = cases[index].modulation;
		dos_audio_driver_prepare_context(1, context, (legacy_u8 *)channel, instrument);
		dos_audio_driver_activate_context(1, context, (legacy_u8 *)channel, cases[index].note, 127,
										  instrument);
		dos_audio_driver_suspend_context(1, context, 0, instrument);
		/* AD15 extends its F-number table into neighboring octaves while
		 * retaining block 5. Interpolating packed block+F-number values jumps.
		 * The last two cases bend back across a modulated target and require
		 * AD15's arithmetic-shift rounding for a negative intermediate. */
		legacy_u16 expected = (5U << 10U) | cases[index].frequency;
		assert(adlib_registers[0xa0] == (expected & 255U));
		assert((adlib_registers[0xb0] & 31U) == (expected >> 8U));
		assert((adlib_registers[0xb0] & 32U) != 0);
		generate_samples(64);
	}
	channel->pitch = 0;
	dos_audio_driver_release_channel(1);
}

static void make_continuous_instrument(legacy_u8 instrument[ADLIB_RESOURCE_SIZE])
{
	memset(instrument, 0, ADLIB_RESOURCE_SIZE);
	LEGACY_WRITE_U32_LE(instrument, ADLIB_RESOURCE_SIZE);
	instrument[17] = (legacy_u8)-5;
	instrument[40] = 0x90;
	instrument[69] = 7;
	for (legacy_u32 index = 0; index < 2; ++index) {
		legacy_u8 *op = instrument + ADLIB_OPERATOR_OFFSET + index * ADLIB_OPERATOR_SIZE;
		op[0] = 15;
		op[3] = 15;
		op[8] = 1;
	}
	instrument[71] = 7;
	instrument[74] = 22;
	instrument[76] = 3;
	instrument[93] = 3;
}

static legacy_u32 programmed_pitch(void)
{
	return adlib_registers[0xa0] | ((adlib_registers[0xb0] & 31U) << 8U);
}

static legacy_u32 half_rate_increment(legacy_u32 pitch)
{
	legacy_u32 frequency = (pitch & 1023U) << (pitch >> 10U);
	return frequency >> 2U;
}

static legacy_u32 carrier_phase(void)
{
	return adlib_chip->slot[3].pg_phase;
}

static legacy_u32 phase_mask(void)
{
	return (1U << 19U) - 1U;
}

static legacy_u32 relative_phase(legacy_u32 logical_pitch)
{
	legacy_u32 relative = adlib_chip->slot[0].pg_phase - 6U * carrier_phase();
	/* Nuked applies a channel update after its modulator and before its
	 * carrier. Account for that fixed one-sample pipeline difference. */
	relative += 6U * half_rate_increment(logical_pitch);
	return relative & phase_mask();
}

static void check_continuous_pitch_history(struct AUDIO_CHANNEL *channel,
										   struct AUDIO_CONTEXT *context)
{
	legacy_u8 instrument[ADLIB_RESOURCE_SIZE];
	make_continuous_instrument(instrument);
	channel->pitch = 0;
	channel->volume = 127;
	context->modulation = 0;
	context->level = 0;
	context->sequence_value = 0;
	dos_audio_driver_prepare_context(1, context, (legacy_u8 *)channel, instrument);
	dos_audio_driver_set_context_value(1, context, 100);
	dos_audio_driver_activate_context(1, context, (legacy_u8 *)channel, 255, 127, instrument);
	generate_samples(64);
	legacy_u32 reference_phase = 0;
	for (legacy_u32 direction = 0; direction < 2; ++direction) {
		for (legacy_u32 index = 0; index < 8192; ++index) {
			legacy_u32 pitch = direction == 0 ? index : 8191U - index;
			context->modulation = index % 3U == 0 ? 11 : 0;
			/* Cover every final packed pitch while independently exercising
			 * modulation, signed detune, block transitions, and word wrap. */
			adlib_voices[0].base_pitch = (legacy_u16)(pitch + 5U - (legacy_u32)context->modulation);
			dos_audio_driver_suspend_context(1, context, 0, instrument);
			assert((adlib_registers[0xb0] & 32U) != 0);
			assert((adlib_registers[0x20] & 15U) == 6U);
			assert((adlib_registers[0x23] & 15U) == 1U);
			legacy_u32 actual = programmed_pitch();
			/* With multiplier one, the carrier advances twice as fast as
			 * a half-rate carrier at this programmed base frequency. */
			legacy_u32 integer_increment = (actual & 1023U) << (actual >> 10U);
			integer_increment >>= 1U;
			assert(integer_increment == half_rate_increment(pitch));
			for (legacy_u32 sample = 0; sample < 16; ++sample) {
				legacy_u32 before = carrier_phase();
				generate_samples(1);
				/* One output sample advances at most two chip samples. A
				 * hidden key-off/key-on would reset phase instead. */
				assert(((carrier_phase() - before) & phase_mask()) <=
					   2U * half_rate_increment(8191U));
			}
			if (direction == 0 && index == 0) {
				reference_phase = relative_phase(pitch);
			}
			assert(relative_phase(pitch) == reference_phase);
		}
	}
	/* Rebinding an active instrument must never temporarily restore its old
	 * multipliers while retaining the halved base pitch. */
	context->modulation = 0;
	adlib_voices[0].base_pitch = 462;
	dos_audio_driver_suspend_context(1, context, 0, instrument);
	generate_samples(64);
	reference_phase = relative_phase(457);
	legacy_u32 continuous_pitch = programmed_pitch();
	dos_audio_driver_prepare_context(1, context, (legacy_u8 *)channel, instrument);
	assert(adlib_voices[0].doubled_multipliers);
	assert(programmed_pitch() == continuous_pitch);
	assert((adlib_registers[0x20] & 15U) == 6U);
	assert((adlib_registers[0x23] & 15U) == 1U);
	generate_samples(64);
	assert(relative_phase(457) == reference_phase);
	/* Changing eligibility during a rebind must restore the unhalved pitch
	 * immediately; the next sequencer update may be a full tick away. */
	instrument[79] = 1;
	dos_audio_driver_prepare_context(1, context, (legacy_u8 *)channel, instrument);
	assert(!adlib_voices[0].doubled_multipliers);
	assert(programmed_pitch() == 457U);
	assert((adlib_registers[0xb0] & 32U) != 0);
	assert((adlib_registers[0x20] & 15U) == 3U);
	assert((adlib_registers[0x23] & 15U) == 0U);
	generate_samples(64);
	instrument[79] = 0;
	dos_audio_driver_prepare_context(1, context, (legacy_u8 *)channel, instrument);
	assert(adlib_voices[0].doubled_multipliers);
	assert(programmed_pitch() == continuous_pitch);
	generate_samples(64);
	/* Reusing the same resource as a musical note must restore its original
	 * multipliers and normal note pitch without requiring another prepare. */
	dos_audio_driver_activate_context(1, context, (legacy_u8 *)channel, 72, 127, instrument);
	assert((adlib_registers[0x20] & 15U) == 3U);
	assert((adlib_registers[0x23] & 15U) == 0U);
	assert(programmed_pitch() == adlib_note_pitch(72) - 5U);
	assert(!adlib_voices[0].doubled_multipliers);
	generate_samples(64);
	dos_audio_driver_release_channel(1);
	puts("Continuous FM: exact carrier pitch and stable phase through both full-range sweeps");
}

static void check_continuous_eligibility(struct AUDIO_CHANNEL *channel,
										 struct AUDIO_CONTEXT *context)
{
	static const struct {
		legacy_u32 offset;
		legacy_u8 value;
	} rejected[] = {{68, 1},	{76, 0},	{76, 7},	{88, 1},	{70, 14},	{82, 14},
					{72, 1},	{84, 1},	{73, 14},	{85, 14},	{75, 1},	{87, 1},
					{77, 1},	{89, 1},	{78, 0},	{90, 0},	{79, 1},	{91, 1},
					{53, 0x91}, {22, 0x81}, {23, 0x81}, {24, 0x81}, {25, 0x81}, {40, 0x81},
					{53, 0x81}, {22, 0x82}, {23, 0x82}, {24, 0x82}, {25, 0x82}, {40, 0x82},
					{53, 0x82}};
	legacy_u8 instrument[ADLIB_RESOURCE_SIZE];
	for (legacy_u32 index = 0; index < sizeof(rejected) / sizeof(rejected[0]); ++index) {
		make_continuous_instrument(instrument);
		instrument[rejected[index].offset] = rejected[index].value;
		dos_audio_driver_prepare_context(1, context, (legacy_u8 *)channel, instrument);
		dos_audio_driver_set_context_value(1, context, 100);
		dos_audio_driver_activate_context(1, context, (legacy_u8 *)channel, 255, 127, instrument);
		adlib_write_pitch(0, 107U, 1);
		assert(programmed_pitch() == 107U);
		assert(!adlib_voices[0].doubled_multipliers);
		generate_samples(64);
		dos_audio_driver_release_channel(1);
	}
	static const legacy_u32 doubled[] = {2, 4, 6, 8, 10, 12};
	for (legacy_u32 multiplier = 1; multiplier <= 6; ++multiplier) {
		make_continuous_instrument(instrument);
		instrument[76] = (legacy_u8)multiplier;
		/* Tremolo changes amplitude only, so it remains compatible. */
		instrument[80] = 1;
		instrument[92] = 1;
		dos_audio_driver_prepare_context(1, context, (legacy_u8 *)channel, instrument);
		dos_audio_driver_set_context_value(1, context, 100);
		dos_audio_driver_activate_context(1, context, (legacy_u8 *)channel, 255, 127, instrument);
		assert((adlib_registers[0x20] & 15U) == doubled[multiplier - 1U]);
		assert((adlib_registers[0x23] & 15U) == 1U);
		assert((adlib_registers[0x20] & 128U) != 0);
		assert((adlib_registers[0x23] & 128U) != 0);
		generate_samples(64);
		dos_audio_driver_release_channel(1);
	}
	context->modulation = 0;
}

static void check_music_mute(void)
{
	legacy_u8 instrument[ADLIB_RESOURCE_SIZE];
	make_sine_instrument(instrument);
	instrument[ADLIB_RESOURCE_CONNECTION_OFFSET] = ADLIB_CONNECTION_ADDITIVE;
	struct AUDIO_CHANNEL *music = &audio_channels[AUDIO_MUSIC_CHANNEL_FIRST];
	struct AUDIO_CHANNEL *effect = &audio_channels[AUDIO_EFFECT_CHANNEL_FIRST];
	struct AUDIO_CONTEXT *context = &dos_audio_contexts[AUDIO_DRIVER_CHANNEL_BASE];
	struct AUDIO_CONTEXT *effect_context = &dos_audio_contexts[AUDIO_DRIVER_CHANNEL_BASE + 1U];
	music->channel = AUDIO_MUSIC_CHANNEL_FIRST;
	music->volume = AUDIO_ENGINE_MAX_VOLUME;
	effect->channel = AUDIO_EFFECT_CHANNEL_FIRST;
	effect->volume = AUDIO_ENGINE_MAX_VOLUME;
	context->channel = music->channel;
	context->state = AUDIO_CONTEXT_STATE_PLAYING;
	effect_context->channel = effect->channel;
	effect_context->state = AUDIO_CONTEXT_STATE_PLAYING;
	dos_audio_driver_prepare_context(AUDIO_DRIVER_CHANNEL_BASE, context, (legacy_u8 *)music,
									 instrument);
	dos_audio_driver_activate_context(AUDIO_DRIVER_CHANNEL_BASE, context, (legacy_u8 *)music,
									  TEST_MUTE_NOTE, AUDIO_ENGINE_MAX_VOLUME, instrument);
	dos_audio_driver_prepare_context(AUDIO_DRIVER_CHANNEL_BASE + 1U, effect_context,
									 (legacy_u8 *)effect, instrument);
	dos_audio_driver_activate_context(AUDIO_DRIVER_CHANNEL_BASE + 1U, effect_context,
									  (legacy_u8 *)effect, TEST_MUTE_NOTE, AUDIO_ENGINE_MAX_VOLUME,
									  instrument);
	generate_samples(TEST_MUTE_SETTLE_SAMPLES);
	legacy_u32 music_carrier =
		ADLIB_REGISTER_OPERATOR_LEVEL + adlib_slots[0] + ADLIB_CARRIER_SLOT_OFFSET;
	legacy_u32 effect_carrier =
		ADLIB_REGISTER_OPERATOR_LEVEL + adlib_slots[1] + ADLIB_CARRIER_SLOT_OFFSET;
	legacy_u8 music_level = adlib_registers[music_carrier];
	legacy_u8 effect_level = adlib_registers[effect_carrier];
	legacy_u8 key_state = adlib_registers[ADLIB_REGISTER_KEY_BLOCK];
	legacy_u32 phase = carrier_phase();
	assert(SDL_PauseAudioStreamDevice(adlib_stream));
	legacy_s16 queued_samples[ADLIB_TICK_SAMPLES] = {0};
	assert(SDL_PutAudioStreamData(adlib_stream, queued_samples, sizeof(queued_samples)));
	assert(SDL_GetAudioStreamQueued(adlib_stream) > 0);
	sdl3_audio_set_music_muted(1);
	assert(SDL_GetAudioStreamQueued(adlib_stream) == 0);
	assert(carrier_phase() == phase);
	assert(adlib_registers[ADLIB_REGISTER_KEY_BLOCK] == key_state);
	assert(adlib_registers[music_carrier] == music_level);
	assert((adlib_output_registers[music_carrier] & ADLIB_LEVEL_MAX) == ADLIB_LEVEL_MAX);
	assert(adlib_output_registers[effect_carrier] == effect_level);
	generate_samples(TEST_MUTE_SETTLE_SAMPLES);
	assert(pcm_energy() > TEST_MUTE_MIN_ENERGY);
	dos_audio_driver_release_channel(AUDIO_DRIVER_CHANNEL_BASE + 1U);
	generate_samples(TEST_MUTE_SETTLE_SAMPLES);
	legacy_u64 muted_energy = pcm_energy();
	/* Volume and controller writes still update their logical state while
	 * both operators stay attenuated, including the additive modulator. */
	dos_audio_set_channel_volume(AUDIO_MUSIC_CHANNEL_FIRST, TEST_MUTE_VOLUME);
	dos_audio_driver_set_control(AUDIO_DRIVER_CHANNEL_BASE, context, ADLIB_CONTROL_VOLUME,
								 TEST_MUTE_CONTROL_VOLUME);
	adlib_control(0, ADLIB_SELECTOR_MODULATOR_LEVEL, TEST_MUTE_CONTROL_LEVEL);
	assert(music->volume == TEST_MUTE_VOLUME);
	assert((adlib_registers[music_carrier] & ADLIB_LEVEL_MAX) < ADLIB_LEVEL_MAX);
	assert((adlib_registers[ADLIB_REGISTER_OPERATOR_LEVEL] & ADLIB_LEVEL_MAX) < ADLIB_LEVEL_MAX);
	assert((adlib_output_registers[music_carrier] & ADLIB_LEVEL_MAX) == ADLIB_LEVEL_MAX);
	assert((adlib_output_registers[ADLIB_REGISTER_OPERATOR_LEVEL] & ADLIB_LEVEL_MAX) ==
		   ADLIB_LEVEL_MAX);
	sdl3_audio_set_music_muted(0);
	assert(adlib_registers[ADLIB_REGISTER_KEY_BLOCK] == key_state);
	assert(adlib_output_registers[music_carrier] == adlib_registers[music_carrier]);
	assert(adlib_output_registers[ADLIB_REGISTER_OPERATOR_LEVEL] ==
		   adlib_registers[ADLIB_REGISTER_OPERATOR_LEVEL]);
	generate_samples(TEST_MUTE_SETTLE_SAMPLES);
	legacy_u64 restored_energy = pcm_energy();
	assert(restored_energy > TEST_MUTE_MIN_ENERGY);
	assert(restored_energy > muted_energy * TEST_MUTE_ENERGY_RATIO);

	/* Reusing a voice and its instrument for an effect must restore its
	 * output even when the global music-mute setting remains enabled. */
	sdl3_audio_set_music_muted(1);
	dos_audio_driver_activate_context(AUDIO_DRIVER_CHANNEL_BASE, context, (legacy_u8 *)effect,
									  TEST_MUTE_NOTE, AUDIO_ENGINE_MAX_VOLUME, instrument);
	assert(adlib_output_registers[music_carrier] == adlib_registers[music_carrier]);
	assert((adlib_output_registers[music_carrier] & ADLIB_LEVEL_MAX) < ADLIB_LEVEL_MAX);
	dos_audio_driver_activate_context(AUDIO_DRIVER_CHANNEL_BASE, context, (legacy_u8 *)music,
									  TEST_MUTE_NOTE, AUDIO_ENGINE_MAX_VOLUME, instrument);
	assert((adlib_output_registers[music_carrier] & ADLIB_LEVEL_MAX) == ADLIB_LEVEL_MAX);
	dos_audio_driver_release_channel(AUDIO_DRIVER_CHANNEL_BASE);
	sdl3_audio_set_music_muted(0);
	assert(SDL_ResumeAudioStreamDevice(adlib_stream));
	puts("Music mute: live notes and controllers preserved, effects audible, queued PCM cleared");
}

legacy_int main(legacy_int argc, legacy_char **argv)
{
	assert(argc == 2);
	legacy_u8 instrument[ADLIB_RESOURCE_SIZE];
	load_first_instrument(argv[1], instrument);
	SDL_SetMainReady();
	SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "restunts-missing-audio-driver",
							SDL_HINT_OVERRIDE);
	assert(dos_audio_driver_initialize() == ADLIB_CONTEXTS);
	assert(!adlib_ready && adlib_chip == NULL && adlib_stream == NULL);
	/* Batch tools never acquire a device, even when an audio driver exists. */
	SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE);
	sdl3_batch_mode = 1;
	assert(dos_audio_driver_initialize() == ADLIB_CONTEXTS);
	assert(!adlib_ready && adlib_chip == NULL && adlib_stream == NULL);
	sdl3_batch_mode = 0;
	assert(dos_audio_driver_initialize() == ADLIB_CONTEXTS);
	assert(adlib_ready && adlib_chip != NULL && adlib_stream != NULL);
	assert(pcm_energy() == 0);

	struct AUDIO_CHANNEL *channel = &audio_channels[0];
	struct AUDIO_CONTEXT *context = &dos_audio_contexts[1];
	channel->volume = 127;
	context->channel = 0;
	context->state = AUDIO_CONTEXT_STATE_PLAYING;
	context->driver_channel = 1;
	dos_audio_driver_prepare_context(1, context, (legacy_u8 *)channel, instrument);
	dos_audio_driver_activate_context(1, context, (legacy_u8 *)channel, 72, 127, instrument);
	assert(adlib_registers[0xa0] == 86);
	assert(adlib_registers[0xb0] == (32 | 24));
	assert(adlib_registers[0xe0] == (instrument[81] & 3));
	assert(adlib_registers[0xe3] == (instrument[93] & 3));
	assert(pcm_energy() > 1000000);

	dos_audio_set_channel_volume(0, 0);
	assert((adlib_registers[0x43] & 63) == 63);
	if (instrument[68] == 1) {
		assert((adlib_registers[0x40] & 63) == 63);
	}
	dos_audio_set_channel_volume(0, 127);
	assert((adlib_registers[0x43] & 63) < 63);

	/* Contexts retain the original numbering: final voice maps to OPL 8. */
	dos_audio_driver_prepare_context(9, &dos_audio_contexts[9], (legacy_u8 *)channel, instrument);
	dos_audio_driver_activate_context(9, &dos_audio_contexts[9], (legacy_u8 *)channel, 60, 127,
									  instrument);
	assert((adlib_registers[0xb8] & 32) != 0);
	assert(adlib_voice_index(0) == -1);
	assert(adlib_voice_index(10) == -1);

	/* Continuous engine values are frequencies, not MIDI note indices. */
	dos_audio_driver_set_context_value(1, context, 1000);
	assert(adlib_voices[0].base_pitch == (2U << 10U | 327U));
	dos_audio_driver_activate_context(1, context, (legacy_u8 *)channel, 255, 127, instrument);
	assert(adlib_registers[0xa0] == 71);
	assert((adlib_registers[0xb0] & 31) == 9);

	instrument[40] = 0x90;
	context->modulation = 9;
	dos_audio_driver_suspend_context(1, context, 0, instrument);
	assert(adlib_registers[0xa0] == 80);
	dos_audio_driver_start_context(1, context);
	assert((adlib_registers[0xb0] & 32) == 0);
	dos_audio_driver_release_channel(1);
	dos_audio_driver_release_channel(9);
	assert(adlib_voices[0].resource == NULL);
	assert(adlib_voices[8].resource == NULL);
	assert((adlib_registers[0x43] & 63) == 63);
	check_sample_rate_and_retrigger(channel, context);
	check_octave_crossing_bends(channel, context);
	check_continuous_pitch_history(channel, context);
	check_continuous_eligibility(channel, context);
	check_music_mute();
	sdl3_audio_update();
	sdl3_audio_set_music_muted(1);
	dos_audio_shutdown();
	assert(!adlib_music_muted);
	assert(!adlib_ready && adlib_chip == NULL && adlib_stream == NULL);
	assert(removed_callbacks == 1);
	SDL_Quit();
	puts("SDL3 AdLib: synthesis, octave bends, device fallback, batch mode, and cleanup passed");
	return 0;
}
