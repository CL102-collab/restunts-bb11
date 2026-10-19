/* Native implementation of the AdLib driver interface. Resource offsets and
 * register arithmetic follow the supplied AD15.DRV; the portable sequencer
 * continues to own channel allocation, note timing, envelopes and modulation.
 * Only the main thread accesses this state. SDL consumes queued PCM separately. */
#include "sdl3.h"
#include "music.h"
#include <string.h>
#include <stdio.h>
#include "../../c/audio.h"
#include "../../c/audio_internal.h"
#include "../../c/platform.h"
#include "../../c/timing.h"
#include "../../c/resource_bytes.h"
#ifdef __ANDROID__
#include <jni.h>
#endif
#ifdef __DJGPP__
#include <inlines/pc.h>
#else
#include "audio_trace.h"
#include "opl2.h"
#define ADLIB_EMULATOR "nuked"
#endif

extern legacy_s32 sdl3_batch_mode;

#ifdef __ANDROID__
void android_car_impact_feedback(void)
{
	JNIEnv *env = SDL_GetAndroidJNIEnv();
	if (env == NULL) {
		return;
	}
	jobject activity = SDL_GetAndroidActivity();
	jclass activity_class = NULL;
	jmethodID vibrate = NULL;
	if (activity != NULL && !(*env)->ExceptionCheck(env)) {
		activity_class = (*env)->GetObjectClass(env, activity);
	}
	if (activity_class != NULL && !(*env)->ExceptionCheck(env)) {
		vibrate = (*env)->GetMethodID(env, activity_class, "vibrateCarImpact", "()V");
	}
	if (vibrate != NULL && !(*env)->ExceptionCheck(env)) {
		(*env)->CallVoidMethod(env, activity, vibrate);
	}
	if ((*env)->ExceptionCheck(env)) {
		(*env)->ExceptionClear(env);
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Cannot vibrate for car impact: Java exception");
	}
	if (activity_class != NULL) {
		(*env)->DeleteLocalRef(env, activity_class);
	}
	if (activity != NULL) {
		(*env)->DeleteLocalRef(env, activity);
	}
}
#endif

#define ADLIB_VOICES 9U
/* The original context mask reserves bit zero for its sampled-sound path.
 * FM contexts 1..9 correspond to OPL channels 0..8. */
#define ADLIB_CONTEXTS 10U
#define ADLIB_SAMPLE_RATE 44100U
#define ADLIB_TICK_SAMPLES (ADLIB_SAMPLE_RATE / DOS_TIMER_REALTIME_TICKS_PER_SECOND)
#define ADLIB_RESOURCE_SIZE 94U
#define ADLIB_OPERATOR_SIZE 12U
#define ADLIB_OPERATOR_OFFSET 70U

#define ADLIB_REGISTER_COUNT 256U
#define ADLIB_ADDRESS_PORT 0x388
#define ADLIB_DATA_PORT 0x389
#define ADLIB_ADDRESS_DELAY_READS 6
#define ADLIB_DATA_DELAY_READS 35
#define ADLIB_TIMER_TEST_DELAY_READS 400
#define ADLIB_SEMITONES_PER_OCTAVE 12U
#define ADLIB_NOTE_TABLE_BASE 24
#define ADLIB_OPERATORS_PER_VOICE 2U
#define ADLIB_CARRIER_SLOT_OFFSET 3U
#define ADLIB_ENVELOPE_RATE_MAX 15
#define ADLIB_CONNECTION_ADDITIVE 1
#define ADLIB_OPERATOR_CHARACTER_FLAGS_MASK 0xf0U
#define ADLIB_NIBBLE_BITS 4U
#define ADLIB_KEY_SCALE_LEVEL_SHIFT 6U
#define ADLIB_KEY_SCALE_RATE_SHIFT 4U
#define ADLIB_SUSTAIN_TYPE_SHIFT 5U
#define ADLIB_VIBRATO_SHIFT 6U
#define ADLIB_TREMOLO_SHIFT 7U
#define ADLIB_WAVEFORM_MASK 3U
#define ADLIB_LEVEL_MAX 63U
#define ADLIB_GAIN_FRACTION_BITS 6U
#define ADLIB_FEEDBACK_MASK 7U
#define ADLIB_FEEDBACK_SHIFT 1U
#define ADLIB_MULTIPLIER_CONTROL_SHIFT 3U
#define ADLIB_CONTROL_MULTIPLIER_MASK 7U
#define ADLIB_PITCH_MASK 0x1fffU
#define ADLIB_PITCH_BLOCK_MASK 0x1c00U
#define ADLIB_PITCH_BLOCK_STEP 0x400U
#define ADLIB_PITCH_BLOCK_SHIFT 10U
#define ADLIB_PITCH_BLOCK_MAX 7U
#define ADLIB_PITCH_HIGH_MASK 31U
#define ADLIB_KEY_ON_FLAG 32U
#define ADLIB_PITCH_BEND_FRACTION_BITS 13U
#define ADLIB_TIMER_MASK 0x60
#define ADLIB_TIMER_RESET 0x80
#define ADLIB_TIMER1_START 0x21
#define ADLIB_TIMER_STATUS_MASK 0xe0U
#define ADLIB_TIMER1_EXPIRED 0xc0U
#define ADLIB_WAVEFORM_ENABLE 0x20
#define ADLIB_NOTE_SELECT_FLAG 0x40
#define ADLIB_CLOCK_HZ 3579545
#define ADLIB_DRIVER_CLOCK_HZ 50000UL
#define ADLIB_DRIVER_FNUMBER_MAX 511UL
#define ADLIB_MAX_QUEUED_TICKS 10U
#define ADLIB_MAX_QUEUE_BYTES (ADLIB_TICK_SAMPLES * sizeof(legacy_s16) * ADLIB_MAX_QUEUED_TICKS)

enum ADLIB_REGISTER {
	ADLIB_REGISTER_TEST = 1,
	ADLIB_REGISTER_TIMER1 = 2,
	ADLIB_REGISTER_TIMER_CONTROL = 4,
	ADLIB_REGISTER_NOTE_SELECT = 8,
	ADLIB_REGISTER_OPERATOR_CHARACTER = 0x20,
	ADLIB_REGISTER_OPERATOR_LEVEL = 0x40,
	ADLIB_REGISTER_ATTACK_DECAY = 0x60,
	ADLIB_REGISTER_SUSTAIN_RELEASE = 0x80,
	ADLIB_REGISTER_FREQUENCY_LOW = 0xa0,
	ADLIB_REGISTER_KEY_BLOCK = 0xb0,
	ADLIB_REGISTER_FEEDBACK = 0xc0,
	ADLIB_REGISTER_WAVEFORM = 0xe0
};

enum ADLIB_OPERATOR_FIELD {
	ADLIB_OPERATOR_ATTACK,
	ADLIB_OPERATOR_DECAY,
	ADLIB_OPERATOR_SUSTAIN,
	ADLIB_OPERATOR_RELEASE,
	ADLIB_OPERATOR_LEVEL,
	ADLIB_OPERATOR_KEY_SCALE_LEVEL,
	ADLIB_OPERATOR_MULTIPLIER,
	ADLIB_OPERATOR_KEY_SCALE_RATE,
	ADLIB_OPERATOR_SUSTAIN_TYPE,
	ADLIB_OPERATOR_VIBRATO,
	ADLIB_OPERATOR_TREMOLO,
	ADLIB_OPERATOR_WAVEFORM
};

enum ADLIB_RESOURCE_OFFSET {
	ADLIB_RESOURCE_PITCH_BIAS_OFFSET = 17,
	ADLIB_RESOURCE_BEND_RANGE_OFFSET = 18,
	ADLIB_RESOURCE_VELOCITY_ENABLED_OFFSET = 21,
	ADLIB_RESOURCE_CONTROL_1_OFFSET = 22,
	ADLIB_RESOURCE_CONTROL_11_OFFSET = 23,
	ADLIB_RESOURCE_CONTROL_12_OFFSET = 24,
	ADLIB_RESOURCE_LEVEL_SELECTOR_OFFSET = 25,
	ADLIB_RESOURCE_CONNECTION_OFFSET = 68,
	ADLIB_RESOURCE_FEEDBACK_OFFSET = 69
};

enum ADLIB_SELECTOR {
	ADLIB_SELECTOR_MODULATOR_MULTIPLIER = 0x81,
	ADLIB_SELECTOR_CARRIER_MULTIPLIER = 0x82,
	ADLIB_SELECTOR_MODULATOR_LEVEL = 0x83,
	ADLIB_SELECTOR_FEEDBACK = 0x85,
	ADLIB_SELECTOR_PITCH = 0x90,
	ADLIB_SELECTOR_NOTE = 0x91
};

enum ADLIB_CONTROL {
	ADLIB_CONTROL_MODULATION = 1,
	ADLIB_CONTROL_VOLUME = 7,
	ADLIB_CONTROL_EXPRESSION = 11,
	ADLIB_CONTROL_EFFECT = 12
};

struct AUDIO_TIMER audio_timers[AUDIO_TIMER_COUNT];
struct AUDIO_CHANNEL audio_channels[AUDIO_CHANNEL_COUNT];
struct AUDIO_CHANNEL *audio_sfx_channels = audio_channels + AUDIO_EFFECT_CHANNEL_FIRST;
struct AUDIO_CONTEXT dos_audio_contexts[AUDIO_CONTEXT_COUNT];
legacy_u8 dos_audio_master_state[DOS_AUDIO_MASTER_STATE_SIZE] = DOS_AUDIO_MASTER_STATE_INITIALIZER;
legacy_u8 dos_audio_driver_data[DOS_AUDIO_DRIVER_DATA_SIZE];
void *dos_audio_driver_binary;
legacy_s16 audio_update_lock = AUDIO_UPDATE_LOCKED;
legacy_s8 audio_music_enabled = AUDIO_STATE_ENABLED;
legacy_s8 audio_effects_enabled = AUDIO_STATE_ENABLED;
legacy_u8 dos_audio_uses_direct_channels;
legacy_u8 dos_audio_special_mode;
legacy_u8 dos_audio_master_volume;
legacy_u8 dos_audio_context_count;

struct ADLIB_VOICE {
	const legacy_u8 *resource;
	struct AUDIO_CHANNEL *channel;
	legacy_u16 base_pitch;
	legacy_u16 current_pitch;
	legacy_u8 note;
	legacy_u8 velocity;
	legacy_u8 doubled_multipliers;
};

static struct ADLIB_VOICE adlib_voices[ADLIB_VOICES];
static legacy_u8 adlib_registers[ADLIB_REGISTER_COUNT];
static legacy_u8 adlib_output_registers[ADLIB_REGISTER_COUNT];
static legacy_s32 adlib_music_muted;
static legacy_u8 adlib_register_valid[ADLIB_REGISTER_COUNT];
static legacy_s32 adlib_ready;
static const legacy_u8 adlib_slots[ADLIB_VOICES] = {0, 1, 2, 8, 9, 10, 16, 17, 18};
static const legacy_u16 adlib_frequencies[] = {
	21,	 23,  24,  25,	27,	 29,  30,  32,	34,	 36,  38,  40,	43,	 45,  48,
	51,	 54,  57,  61,	64,	 68,  72,  76,	81,	 86,  91,  96,	102, 108, 114,
	121, 128, 136, 144, 153, 162, 171, 182, 192, 204, 216, 229, 242, 257, 272,
	288, 306, 324, 343, 363, 385, 408, 432, 458, 485, 514, 544, 577, 611, 647};
#ifndef __DJGPP__
static opl2_chip *adlib_chip;
static legacy_s32 adlib_initializing;
static SDL_AudioStream *adlib_stream;

static void adlib_delete_chip(void)
{
	free(adlib_chip);
	adlib_chip = NULL;
}

static void adlib_generate_samples(legacy_s16 *samples, legacy_u32 count)
{
	OPL2_GenerateStream(adlib_chip, samples, count);
	adlib_trace_advance(count);
}
#endif

static legacy_u8 adlib_output_value(legacy_u32 reg, legacy_u8 value)
{
	if (!adlib_music_muted || reg < ADLIB_REGISTER_OPERATOR_LEVEL ||
		reg > ADLIB_REGISTER_OPERATOR_LEVEL + adlib_slots[ADLIB_VOICES - 1U] +
				  ADLIB_CARRIER_SLOT_OFFSET) {
		return value;
	}
	for (legacy_u32 voice = 0; voice < ADLIB_VOICES; ++voice) {
		const struct AUDIO_CHANNEL *channel = adlib_voices[voice].channel;
		legacy_u32 operator_reg = ADLIB_REGISTER_OPERATOR_LEVEL + adlib_slots[voice];
		if (channel != NULL && channel->channel < AUDIO_EFFECT_CHANNEL_FIRST &&
			(reg == operator_reg || reg == operator_reg + ADLIB_CARRIER_SLOT_OFFSET)) {
			return value | ADLIB_LEVEL_MAX;
		}
	}
	return value;
}

static void adlib_write(legacy_u32 reg, legacy_u32 value)
{
	legacy_u8 byte = (legacy_u8)value;
	if (!adlib_ready || reg >= sizeof(adlib_registers)) {
		return;
	}
	/* Keep the original register state current while attenuating only music
	 * voices at the output. Notes, envelopes and controllers keep advancing. */
	adlib_registers[reg] = byte;
	byte = adlib_output_value(reg, byte);
	if (adlib_register_valid[reg] && adlib_output_registers[reg] == byte) {
		return;
	}
	adlib_output_registers[reg] = byte;
	adlib_register_valid[reg] = 1;
#ifdef __DJGPP__
	outportb(ADLIB_ADDRESS_PORT, reg);
	for (legacy_s32 delay = 0; delay < ADLIB_ADDRESS_DELAY_READS; ++delay) {
		(void)inportb(ADLIB_ADDRESS_PORT);
	}
	outportb(ADLIB_DATA_PORT, byte);
	for (legacy_s32 delay = 0; delay < ADLIB_DATA_DELAY_READS; ++delay) {
		(void)inportb(ADLIB_ADDRESS_PORT);
	}
#else
	/* Buffer runtime writes so same-tick key-off/key-on transitions survive. */
	legacy_s32 buffered = !adlib_initializing;
	if (buffered) {
		OPL2_WriteRegBuffered(adlib_chip, (legacy_u8)reg, byte);
	} else {
		OPL2_WriteReg(adlib_chip, (legacy_u8)reg, byte);
	}
	adlib_trace_write(reg, byte, buffered);
#endif
}

void sdl3_audio_set_music_muted(legacy_s32 muted)
{
	muted = muted != 0;
	if (adlib_music_muted == muted) {
		return;
	}
	adlib_music_muted = muted;
	for (legacy_u32 voice = 0; voice < ADLIB_VOICES; ++voice) {
		for (legacy_u32 operator_index = 0; operator_index < ADLIB_OPERATORS_PER_VOICE;
			 ++operator_index) {
			legacy_u32 reg = ADLIB_REGISTER_OPERATOR_LEVEL + adlib_slots[voice] +
							 operator_index * ADLIB_CARRIER_SLOT_OFFSET;
			adlib_write(reg, adlib_registers[reg]);
		}
	}
#ifndef __DJGPP__
	if (adlib_stream != NULL) {
		/* Discard already queued music so the switch takes effect immediately;
		 * the chip and all currently sounding effects retain their state. */
		adlib_trace_clear();
		SDL_ClearAudioStream(adlib_stream);
	}
#endif
}

static legacy_s32 adlib_voice_index(legacy_s16 driver_channel)
{
	return driver_channel > 0 && (legacy_u16)driver_channel <= ADLIB_VOICES ? driver_channel - 1
																			: -1;
}

static legacy_u16 adlib_note_pitch(legacy_u32 note)
{
	note &= LEGACY_U8_MAX;
	return (
		legacy_u16)((((note / ADLIB_SEMITONES_PER_OCTAVE) & ADLIB_PITCH_BLOCK_MAX)
					 << ADLIB_PITCH_BLOCK_SHIFT) |
					adlib_frequencies[ADLIB_NOTE_TABLE_BASE + note % ADLIB_SEMITONES_PER_OCTAVE]);
}

/* AD15 bends within an extended F-number table while retaining the note's
 * block. Interpolating packed pitches across octaves also changes the block. */
static legacy_u16 adlib_bend_target(legacy_u32 note, legacy_s32 semitones)
{
	note &= LEGACY_U8_MAX;
	legacy_s32 index =
		ADLIB_NOTE_TABLE_BASE + (legacy_s32)(note % ADLIB_SEMITONES_PER_OCTAVE) + semitones;
	if (index < 0) {
		index = 0;
	} else if (index >= (legacy_s32)(sizeof(adlib_frequencies) / sizeof(adlib_frequencies[0]))) {
		index = (legacy_s32)(sizeof(adlib_frequencies) / sizeof(adlib_frequencies[0])) - 1;
	}
	return (legacy_u16)((((note / ADLIB_SEMITONES_PER_OCTAVE) & ADLIB_PITCH_BLOCK_MAX)
						 << ADLIB_PITCH_BLOCK_SHIFT) |
						adlib_frequencies[index]);
}

/* A half-rate carrier rounds its phase increment independently from the
 * modulator. Low pitches can therefore leave a different relative phase
 * after returning to the same RPM. Integer multipliers retain the ratio.
 * Only normalize sustained, continuously pitched FM voices whose steady
 * level and key scaling permit lowering the pitch block. */
static legacy_u32 adlib_continuous_multiplier(const struct ADLIB_VOICE *state)
{
	static const legacy_u8 doubled[] = {0, 2, 4, 6, 8, 10, 12};
	static const legacy_u8 selectors[] = {
		ADLIB_RESOURCE_CONTROL_1_OFFSET,		  ADLIB_RESOURCE_CONTROL_11_OFFSET,
		ADLIB_RESOURCE_CONTROL_12_OFFSET,		  ADLIB_RESOURCE_LEVEL_SELECTOR_OFFSET,
		AUDIO_RESOURCE_MODULATION_ENABLED_OFFSET, AUDIO_RESOURCE_SEQUENCE_ENABLED_OFFSET};
	const legacy_u8 *resource = state->resource;
	if (state->note != AUDIO_NOTE_USE_DRIVER_VALUE || !resource ||
		resource[ADLIB_RESOURCE_CONNECTION_OFFSET] != 0 ||
		resource[AUDIO_RESOURCE_SEQUENCE_ENABLED_OFFSET] == ADLIB_SELECTOR_NOTE) {
		return 0;
	}
	const legacy_u8 *modulator = resource + ADLIB_OPERATOR_OFFSET;
	const legacy_u8 *carrier = modulator + ADLIB_OPERATOR_SIZE;
	if (carrier[ADLIB_OPERATOR_MULTIPLIER] != 0 || modulator[ADLIB_OPERATOR_MULTIPLIER] == 0 ||
		modulator[ADLIB_OPERATOR_MULTIPLIER] >= sizeof(doubled)) {
		return 0;
	}
	for (legacy_u32 index = 0; index < sizeof(selectors); ++index) {
		legacy_u32 selector = resource[selectors[index]];
		if (selector == ADLIB_SELECTOR_MODULATOR_MULTIPLIER ||
			selector == ADLIB_SELECTOR_CARRIER_MULTIPLIER) {
			return 0;
		}
	}
	for (legacy_u32 index = 0; index < ADLIB_OPERATORS_PER_VOICE; ++index) {
		const legacy_u8 *op = modulator + index * ADLIB_OPERATOR_SIZE;
		if (op[ADLIB_OPERATOR_ATTACK] != ADLIB_ENVELOPE_RATE_MAX ||
			op[ADLIB_OPERATOR_SUSTAIN] != 0 ||
			op[ADLIB_OPERATOR_RELEASE] != ADLIB_ENVELOPE_RATE_MAX ||
			op[ADLIB_OPERATOR_KEY_SCALE_LEVEL] != 0 || op[ADLIB_OPERATOR_KEY_SCALE_RATE] != 0 ||
			op[ADLIB_OPERATOR_SUSTAIN_TYPE] != 1 || op[ADLIB_OPERATOR_VIBRATO] != 0) {
			return 0;
		}
	}
	return doubled[modulator[ADLIB_OPERATOR_MULTIPLIER]];
}

static void adlib_write_pitch(legacy_s32 voice, legacy_u16 pitch, legacy_s32 key_on)
{
	struct ADLIB_VOICE *state = &adlib_voices[voice];
	state->current_pitch = pitch;
	legacy_u32 multiplier = adlib_continuous_multiplier(state);
	if (multiplier != 0 || state->doubled_multipliers) {
		legacy_u32 slot = adlib_slots[voice];
		const legacy_u8 *modulator = state->resource + ADLIB_OPERATOR_OFFSET;
		const legacy_u8 *carrier = modulator + ADLIB_OPERATOR_SIZE;
		adlib_write(ADLIB_REGISTER_OPERATOR_CHARACTER + slot,
					(adlib_registers[ADLIB_REGISTER_OPERATOR_CHARACTER + slot] &
					 ADLIB_OPERATOR_CHARACTER_FLAGS_MASK) |
						(multiplier != 0 ? multiplier : modulator[ADLIB_OPERATOR_MULTIPLIER]));
		adlib_write(
			(ADLIB_REGISTER_OPERATOR_CHARACTER + ADLIB_CARRIER_SLOT_OFFSET) + slot,
			(adlib_registers[(ADLIB_REGISTER_OPERATOR_CHARACTER + ADLIB_CARRIER_SLOT_OFFSET) +
							 slot] &
			 ADLIB_OPERATOR_CHARACTER_FLAGS_MASK) |
				(multiplier != 0 ? 1U : carrier[ADLIB_OPERATOR_MULTIPLIER]));
	}
	state->doubled_multipliers = multiplier != 0;
	if (multiplier != 0) {
		/* Doubling both multipliers and halving the base pitch preserves the
		 * carrier's exact phase increment, including its low-pitch rounding.
		 * Every intermediate A0/B0 write also retains the integer ratio. */
		pitch &= ADLIB_PITCH_MASK;
		pitch =
			(pitch & ADLIB_PITCH_BLOCK_MASK) != 0 ? pitch - ADLIB_PITCH_BLOCK_STEP : pitch >> 1U;
	}
	adlib_write(ADLIB_REGISTER_FREQUENCY_LOW + voice, pitch & LEGACY_U8_MAX);
	adlib_write(ADLIB_REGISTER_KEY_BLOCK + voice,
				((pitch >> LEGACY_BYTE_BITS) & ADLIB_PITCH_HIGH_MASK) |
					(key_on ? ADLIB_KEY_ON_FLAG : 0U));
}

static void adlib_program_operator(legacy_u32 slot, const legacy_u8 *op, legacy_u32 multiplier)
{
	adlib_write(ADLIB_REGISTER_OPERATOR_CHARACTER + slot,
				(op[ADLIB_OPERATOR_TREMOLO] << ADLIB_TREMOLO_SHIFT) |
					(op[ADLIB_OPERATOR_VIBRATO] << ADLIB_VIBRATO_SHIFT) |
					(op[ADLIB_OPERATOR_SUSTAIN_TYPE] << ADLIB_SUSTAIN_TYPE_SHIFT) |
					(op[ADLIB_OPERATOR_KEY_SCALE_RATE] << ADLIB_KEY_SCALE_RATE_SHIFT) | multiplier);
	adlib_write(ADLIB_REGISTER_OPERATOR_LEVEL + slot,
				(op[ADLIB_OPERATOR_KEY_SCALE_LEVEL] << ADLIB_KEY_SCALE_LEVEL_SHIFT) |
					op[ADLIB_OPERATOR_LEVEL]);
	adlib_write(ADLIB_REGISTER_ATTACK_DECAY + slot,
				(op[ADLIB_OPERATOR_ATTACK] << ADLIB_NIBBLE_BITS) | op[ADLIB_OPERATOR_DECAY]);
	adlib_write(ADLIB_REGISTER_SUSTAIN_RELEASE + slot,
				(op[ADLIB_OPERATOR_SUSTAIN] << ADLIB_NIBBLE_BITS) | op[ADLIB_OPERATOR_RELEASE]);
	adlib_write(ADLIB_REGISTER_WAVEFORM + slot, op[ADLIB_OPERATOR_WAVEFORM] & ADLIB_WAVEFORM_MASK);
}

static void adlib_volume(legacy_s32 voice, legacy_u32 volume)
{
	const struct ADLIB_VOICE *state = &adlib_voices[voice];
	const legacy_u8 *resource = state->resource;
	if (!resource) {
		return;
	}
	if (volume > AUDIO_ENGINE_MAX_VOLUME) {
		volume = AUDIO_ENGINE_MAX_VOLUME;
	}
	/* AD15 rounds two products independently, with a denominator of 128. */
	legacy_u32 scale = (((volume * state->velocity) >> ADLIB_GAIN_FRACTION_BITS) + 1U) >> 1U;
	for (legacy_u32 op_index = 0; op_index < ADLIB_OPERATORS_PER_VOICE; ++op_index) {
		const legacy_u8 *op = resource + ADLIB_OPERATOR_OFFSET + op_index * ADLIB_OPERATOR_SIZE;
		legacy_u32 level = op[ADLIB_OPERATOR_LEVEL] & ADLIB_LEVEL_MAX;
		if (op_index != 0 ||
			resource[ADLIB_RESOURCE_CONNECTION_OFFSET] == ADLIB_CONNECTION_ADDITIVE) {
			legacy_u32 gain =
				((((ADLIB_LEVEL_MAX - level) * scale) >> ADLIB_GAIN_FRACTION_BITS) + 1U) >> 1U;
			level = ADLIB_LEVEL_MAX - (gain & ADLIB_LEVEL_MAX);
		}
		adlib_write(ADLIB_REGISTER_OPERATOR_LEVEL + adlib_slots[voice] +
						op_index * ADLIB_CARRIER_SLOT_OFFSET,
					(op[ADLIB_OPERATOR_KEY_SCALE_LEVEL] << ADLIB_KEY_SCALE_LEVEL_SHIFT) | level);
	}
}

/* The voice resource selects which OPL property a controller/envelope drives. */
static void adlib_control(legacy_s32 voice, legacy_u32 selector, legacy_u32 value)
{
	const legacy_u8 *resource = adlib_voices[voice].resource;
	if (!resource || selector < ADLIB_SELECTOR_MODULATOR_MULTIPLIER ||
		selector > ADLIB_SELECTOR_FEEDBACK) {
		return;
	}
	if (selector == ADLIB_SELECTOR_FEEDBACK) {
		adlib_write(ADLIB_REGISTER_FEEDBACK + voice,
					(((value >> ADLIB_NIBBLE_BITS) & ADLIB_FEEDBACK_MASK) << ADLIB_FEEDBACK_SHIFT) |
						resource[ADLIB_RESOURCE_CONNECTION_OFFSET]);
		return;
	}
	legacy_u32 operator_index = (selector - ADLIB_SELECTOR_MODULATOR_MULTIPLIER) & 1U;
	legacy_u32 slot = adlib_slots[voice] + operator_index * ADLIB_CARRIER_SLOT_OFFSET;
	const legacy_u8 *op = resource + ADLIB_OPERATOR_OFFSET + operator_index * ADLIB_OPERATOR_SIZE;
	if (selector < ADLIB_SELECTOR_MODULATOR_LEVEL) {
		adlib_write(
			ADLIB_REGISTER_OPERATOR_CHARACTER + slot,
			(op[ADLIB_OPERATOR_TREMOLO] << ADLIB_TREMOLO_SHIFT) |
				(op[ADLIB_OPERATOR_VIBRATO] << ADLIB_VIBRATO_SHIFT) |
				(op[ADLIB_OPERATOR_SUSTAIN_TYPE] << ADLIB_SUSTAIN_TYPE_SHIFT) |
				(op[ADLIB_OPERATOR_KEY_SCALE_RATE] << ADLIB_KEY_SCALE_RATE_SHIFT) |
				((value >> ADLIB_MULTIPLIER_CONTROL_SHIFT) & ADLIB_CONTROL_MULTIPLIER_MASK));
	} else {
		adlib_write(ADLIB_REGISTER_OPERATOR_LEVEL + slot,
					(op[ADLIB_OPERATOR_KEY_SCALE_LEVEL] << ADLIB_KEY_SCALE_LEVEL_SHIFT) |
						(((AUDIO_ENGINE_MAX_VOLUME - value) >> 1U) & ADLIB_LEVEL_MAX));
	}
}

legacy_u8 dos_audio_driver_initialize(void)
{
	adlib_ready = 0;
	adlib_music_muted = 0;
	memset(adlib_register_valid, 0, sizeof(adlib_register_valid));
	memset(adlib_voices, 0, sizeof(adlib_voices));
	if (sdl3_batch_mode) {
		/* Dump tools still resolve instruments and maintain sequencer state,
		 * but do not need an audio device or access to DOS hardware ports. */
		adlib_ready = 0;
		return ADLIB_CONTEXTS;
	}
#ifdef __DJGPP__
	adlib_ready = 1;
	adlib_write(ADLIB_REGISTER_TIMER_CONTROL, ADLIB_TIMER_MASK);
	adlib_write(ADLIB_REGISTER_TIMER_CONTROL, ADLIB_TIMER_RESET);
	legacy_u32 initial = inportb(ADLIB_ADDRESS_PORT);
	adlib_write(ADLIB_REGISTER_TIMER1, LEGACY_U8_MAX);
	adlib_write(ADLIB_REGISTER_TIMER_CONTROL, ADLIB_TIMER1_START);
	for (legacy_s32 delay = 0; delay < ADLIB_TIMER_TEST_DELAY_READS; ++delay) {
		(void)inportb(ADLIB_ADDRESS_PORT);
	}
	legacy_u32 running = inportb(ADLIB_ADDRESS_PORT);
	adlib_write(ADLIB_REGISTER_TIMER_CONTROL, ADLIB_TIMER_MASK);
	adlib_write(ADLIB_REGISTER_TIMER_CONTROL, ADLIB_TIMER_RESET);
	if ((initial & ADLIB_TIMER_STATUS_MASK) != 0 ||
		(running & ADLIB_TIMER_STATUS_MASK) != ADLIB_TIMER1_EXPIRED) {
		adlib_ready = 0;
		fputs("Audio unavailable: no AdLib-compatible chip at port 388h; continuing silently.\n",
			  stderr);
		return ADLIB_CONTEXTS;
	}
#else
	adlib_chip = calloc(1, sizeof(*adlib_chip));
	if (!adlib_chip) {
		fputs("Audio unavailable: cannot allocate OPL synthesizer; continuing silently.\n", stderr);
		return ADLIB_CONTEXTS;
	}
	OPL2_Reset(adlib_chip, ADLIB_SAMPLE_RATE);
	if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
		fprintf(stderr, "Audio unavailable: %s; continuing silently.\n", SDL_GetError());
		adlib_delete_chip();
		return ADLIB_CONTEXTS;
	}
	SDL_AudioSpec spec = {SDL_AUDIO_S16, 1, ADLIB_SAMPLE_RATE};
	adlib_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
	if (!adlib_stream || !SDL_ResumeAudioStreamDevice(adlib_stream)) {
		fprintf(stderr, "Audio unavailable: %s; continuing silently.\n", SDL_GetError());
		SDL_DestroyAudioStream(adlib_stream);
		adlib_stream = NULL;
		adlib_delete_chip();
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return ADLIB_CONTEXTS;
	}
	adlib_ready = 1;
	adlib_initializing = 1;
	adlib_trace_open(ADLIB_EMULATOR, ADLIB_CLOCK_HZ, ADLIB_SAMPLE_RATE);
#endif
	for (legacy_u32 reg = 0; reg < ADLIB_REGISTER_COUNT; ++reg) {
		adlib_write(reg, 0);
	}
	adlib_write(ADLIB_REGISTER_TEST, ADLIB_WAVEFORM_ENABLE);
	adlib_write(ADLIB_REGISTER_NOTE_SELECT, ADLIB_NOTE_SELECT_FLAG);
#ifndef __DJGPP__
	adlib_initializing = 0;
#endif
	return ADLIB_CONTEXTS;
}

/* Called once after each 100 Hz main-thread timer tick. Keeping the synthesizer
 * here avoids racing the sequencer or resource frees in SDL's audio thread. */
void sdl3_audio_update(void)
{
	sdl3_music_update();
#ifndef __DJGPP__
	if (!adlib_ready || !adlib_stream) {
		return;
	}
	/* Bound latency after a debugger stop or a catch-up burst. */
	if (SDL_GetAudioStreamQueued(adlib_stream) > (legacy_s32)ADLIB_MAX_QUEUE_BYTES) {
		adlib_trace_clear();
		SDL_ClearAudioStream(adlib_stream);
	}
	legacy_s16 samples[ADLIB_TICK_SAMPLES];
	adlib_generate_samples(samples, ADLIB_TICK_SAMPLES);
	SDL_PutAudioStreamData(adlib_stream, samples, sizeof(samples));
#endif
}

void dos_audio_driver_prepare_context(legacy_s16 driver_channel, struct AUDIO_CONTEXT *context,
									  legacy_u8 *timer, void *resource)
{
	(void)context;
	legacy_s32 voice = adlib_voice_index(driver_channel);
	if (voice < 0 || !resource || resource_read_u32le(resource) < ADLIB_RESOURCE_SIZE) {
		return;
	}
	struct ADLIB_VOICE *state = &adlib_voices[voice];
	legacy_s32 was_doubled = state->doubled_multipliers;
	state->resource = resource;
	state->channel = (struct AUDIO_CHANNEL *)timer;
	legacy_u32 multiplier = adlib_continuous_multiplier(state);
	state->doubled_multipliers = multiplier != 0;
	const legacy_u8 *bytes = resource;
	const legacy_u8 *modulator = bytes + ADLIB_OPERATOR_OFFSET;
	const legacy_u8 *carrier = modulator + ADLIB_OPERATOR_SIZE;
	adlib_write(ADLIB_REGISTER_FEEDBACK + voice,
				(bytes[ADLIB_RESOURCE_FEEDBACK_OFFSET] << ADLIB_FEEDBACK_SHIFT) |
					bytes[ADLIB_RESOURCE_CONNECTION_OFFSET]);
	adlib_program_operator(adlib_slots[voice], modulator,
						   multiplier != 0 ? multiplier : modulator[ADLIB_OPERATOR_MULTIPLIER]);
	adlib_program_operator(adlib_slots[voice] + ADLIB_CARRIER_SLOT_OFFSET, carrier,
						   multiplier != 0 ? 1U : carrier[ADLIB_OPERATOR_MULTIPLIER]);
	if (was_doubled != state->doubled_multipliers) {
		/* Resource rebinding may happen while a voice is sounding. Keep its
		 * last pitch in the same units as the newly programmed multipliers. */
		adlib_write_pitch(voice, state->current_pitch,
						  adlib_registers[ADLIB_REGISTER_KEY_BLOCK + voice] & ADLIB_KEY_ON_FLAG);
	}
	if (state->channel) {
		adlib_control(voice, bytes[ADLIB_RESOURCE_CONTROL_1_OFFSET],
					  state->channel->driver_private_state[0]);
		adlib_control(voice, bytes[ADLIB_RESOURCE_CONTROL_11_OFFSET],
					  state->channel->driver_private_state[2]);
		adlib_control(voice, bytes[ADLIB_RESOURCE_CONTROL_12_OFFSET],
					  state->channel->driver_private_state[3]);
	}
}

void dos_audio_driver_set_context_value(legacy_s16 driver_channel, struct AUDIO_CONTEXT *context,
										legacy_u16 value)
{
	(void)context;
	legacy_s32 voice = adlib_voice_index(driver_channel);
	if (voice < 0) {
		return;
	}
	/* The engine supplies Hz. AD15 normalizes to its 50 kHz OPL clock. */
	legacy_u32 numerator = (legacy_u32)value << LEGACY_WORD_BITS;
	legacy_u32 block = 0;
	while (numerator > ADLIB_DRIVER_FNUMBER_MAX * ADLIB_DRIVER_CLOCK_HZ &&
		   block < ADLIB_PITCH_BLOCK_MAX) {
		numerator >>= 1U;
		++block;
	}
	adlib_voices[voice].base_pitch =
		(legacy_u16)((numerator / ADLIB_DRIVER_CLOCK_HZ) | (block << ADLIB_PITCH_BLOCK_SHIFT));
}

void dos_audio_driver_activate_context(legacy_s16 driver_channel, struct AUDIO_CONTEXT *context,
									   legacy_u8 *timer, legacy_s16 pitch, legacy_u16 parameter,
									   void *resource)
{
	legacy_s32 voice = adlib_voice_index(driver_channel);
	if (voice < 0 || !resource) {
		return;
	}
	struct ADLIB_VOICE *state = &adlib_voices[voice];
	if (state->resource != resource) {
		dos_audio_driver_prepare_context(driver_channel, context, timer, resource);
	}
	if (state->resource != resource) {
		return;
	}
	state->channel = (struct AUDIO_CHANNEL *)timer;
	state->note = (legacy_u8)pitch;
	if (state->note != AUDIO_NOTE_USE_DRIVER_VALUE) {
		state->base_pitch = adlib_note_pitch(state->note);
	}
	state->velocity = state->resource[ADLIB_RESOURCE_VELOCITY_ENABLED_OFFSET]
						  ? (legacy_u8)(parameter & AUDIO_ENGINE_MAX_VOLUME)
						  : AUDIO_ENGINE_MAX_VOLUME;
	adlib_write_pitch(
		voice, state->base_pitch + (legacy_s8)state->resource[ADLIB_RESOURCE_PITCH_BIAS_OFFSET], 0);
	adlib_write_pitch(
		voice, state->base_pitch + (legacy_s8)state->resource[ADLIB_RESOURCE_PITCH_BIAS_OFFSET], 1);
	adlib_volume(voice, state->channel->volume);
}

void dos_audio_driver_start_context(legacy_s16 driver_channel, struct AUDIO_CONTEXT *context)
{
	(void)context;
	legacy_s32 voice = adlib_voice_index(driver_channel);
	if (voice >= 0) {
		adlib_write(ADLIB_REGISTER_KEY_BLOCK + voice,
					adlib_registers[ADLIB_REGISTER_KEY_BLOCK + voice] & ~ADLIB_KEY_ON_FLAG);
	}
}

void dos_audio_driver_end_context(legacy_s16 driver_channel, struct AUDIO_CONTEXT *context)
{
	dos_audio_driver_start_context(driver_channel, context);
	legacy_s32 voice = adlib_voice_index(driver_channel);
	if (voice >= 0) {
		adlib_write(ADLIB_REGISTER_OPERATOR_LEVEL + adlib_slots[voice], ADLIB_LEVEL_MAX);
		adlib_write((ADLIB_REGISTER_OPERATOR_LEVEL + ADLIB_CARRIER_SLOT_OFFSET) +
						adlib_slots[voice],
					ADLIB_LEVEL_MAX);
	}
}

void dos_audio_driver_release_channel(legacy_s16 driver_channel)
{
	dos_audio_driver_end_context(driver_channel, NULL);
	legacy_s32 voice = adlib_voice_index(driver_channel);
	if (voice >= 0) {
		memset(&adlib_voices[voice], 0, sizeof(adlib_voices[voice]));
	}
}

void dos_audio_driver_start(void)
{
	for (legacy_s16 channel = 1; (legacy_u16)channel <= ADLIB_VOICES; ++channel) {
		dos_audio_driver_release_channel(channel);
	}
}

void dos_audio_driver_suspend_context(legacy_s16 driver_channel, struct AUDIO_CONTEXT *context,
									  legacy_u16 value, void *resource)
{
	(void)value;
	legacy_s32 voice = adlib_voice_index(driver_channel);
	if (voice < 0 || !context || !context->state || !resource) {
		return;
	}
	struct ADLIB_VOICE *state = &adlib_voices[voice];
	const legacy_u8 *bytes = resource;
	if (state->resource != bytes) {
		return;
	}
	legacy_u16 pitch = state->base_pitch;
	if (bytes[AUDIO_RESOURCE_SEQUENCE_ENABLED_OFFSET] == ADLIB_SELECTOR_NOTE) {
		pitch = adlib_note_pitch(state->note + context->sequence_value);
	}
	if (bytes[AUDIO_RESOURCE_MODULATION_ENABLED_OFFSET] == ADLIB_SELECTOR_PITCH) {
		pitch = LEGACY_U16_WRAP_ADD(pitch, context->modulation);
	}
	if (bytes[ADLIB_RESOURCE_LEVEL_SELECTOR_OFFSET] == ADLIB_SELECTOR_PITCH) {
		pitch = LEGACY_U16_WRAP_ADD(pitch, context->level);
	}
	legacy_s16 bend = state->channel ? LEGACY_S16_FROM_BITS(state->channel->pitch) : 0;
	if (bend != 0) {
		legacy_s32 semitones = bend > 0 ? bytes[ADLIB_RESOURCE_BEND_RANGE_OFFSET]
										: -(legacy_s32)bytes[ADLIB_RESOURCE_BEND_RANGE_OFFSET];
		legacy_s32 difference = (legacy_s32)adlib_bend_target(state->note, semitones) - pitch;
		/* AD15 shifts signed products, then subtracts for downward bends. */
		legacy_s32 delta = LEGACY_S32_SAR(difference * bend, ADLIB_PITCH_BEND_FRACTION_BITS);
		pitch = LEGACY_U16_WRAP_ADD(pitch, bend > 0 ? delta : -delta);
	}
	pitch = LEGACY_U16_WRAP_ADD(pitch, (legacy_s8)bytes[ADLIB_RESOURCE_PITCH_BIAS_OFFSET]);
	adlib_write_pitch(voice, pitch, context->state == AUDIO_CONTEXT_STATE_PLAYING);
	adlib_control(voice, bytes[AUDIO_RESOURCE_SEQUENCE_ENABLED_OFFSET], context->sequence_value);
	adlib_control(voice, bytes[AUDIO_RESOURCE_MODULATION_ENABLED_OFFSET],
				  (legacy_u16)context->modulation);
	adlib_control(voice, bytes[ADLIB_RESOURCE_LEVEL_SELECTOR_OFFSET], (legacy_u16)context->level);
}

void dos_audio_set_channel_volume(legacy_s16 channel, legacy_s16 volume)
{
	if (channel < 0 || (legacy_u16)channel >= AUDIO_CHANNEL_COUNT) {
		return;
	}
	audio_channels[channel].volume = (legacy_u8)volume;
	for (legacy_u32 voice = 0; voice < ADLIB_VOICES; ++voice) {
		if (dos_audio_contexts[voice + AUDIO_DRIVER_CHANNEL_BASE].channel == channel) {
			adlib_volume(voice, (legacy_u8)volume);
		}
	}
}

void dos_audio_bind_channel_context(legacy_s16 channel, void *resource)
{
	if (channel < 0 || (legacy_u16)channel >= AUDIO_CHANNEL_COUNT || !resource) {
		return;
	}
	struct AUDIO_CHANNEL *state = &audio_channels[channel];
	audio_write_far_pointer((legacy_u8 *)&state->resource, resource);
	const legacy_u8 *bytes = resource;
	state->driver_channel =
		bytes[AUDIO_INSTRUMENT_DIRECT_CHANNEL_OFFSET] < AUDIO_DIRECT_CHANNEL_COUNT
			? bytes[AUDIO_INSTRUMENT_DIRECT_CHANNEL_OFFSET]
			: (legacy_u8)((channel & AUDIO_DIRECT_CHANNEL_MASK) + AUDIO_DRIVER_CHANNEL_BASE);
	for (legacy_s16 index = 1; (legacy_u16)index < ADLIB_CONTEXTS; ++index) {
		if (dos_audio_contexts[index].channel == channel) {
			dos_audio_driver_prepare_context(index, &dos_audio_contexts[index], (legacy_u8 *)state,
											 resource);
		}
	}
}

void dos_audio_set_context_pitch(legacy_s16 context_index, legacy_s16 pitch)
{
	if (context_index < 0 || (legacy_u16)context_index >= ADLIB_CONTEXTS) {
		return;
	}
	struct AUDIO_CONTEXT *context = &dos_audio_contexts[context_index];
	dos_audio_driver_set_context_value(context->driver_channel, context, (legacy_u16)pitch);
}

void dos_audio_driver_set_control(legacy_s16 driver_channel, struct AUDIO_CONTEXT *context,
								  legacy_u16 control, legacy_u16 value)
{
	legacy_s32 voice = adlib_voice_index(driver_channel);
	if (voice < 0 || !context || !context->state || !adlib_voices[voice].resource) {
		return;
	}
	const legacy_u8 *resource = adlib_voices[voice].resource;
	if (control == ADLIB_CONTROL_VOLUME) {
		adlib_volume(voice, value);
	} else if (control == ADLIB_CONTROL_MODULATION || control == ADLIB_CONTROL_EXPRESSION ||
			   control == ADLIB_CONTROL_EFFECT) {
		legacy_u32 offset = control == ADLIB_CONTROL_MODULATION ? ADLIB_RESOURCE_CONTROL_1_OFFSET
							: control == ADLIB_CONTROL_EXPRESSION
								? ADLIB_RESOURCE_CONTROL_11_OFFSET
								: ADLIB_RESOURCE_CONTROL_12_OFFSET;
		adlib_control(voice, resource[offset], value);
	}
}

void dos_audio_driver_reset(void)
{
	/* Account for loading time while the old song is stopped, before the
	 * sequencer activates its replacement. Otherwise the next pump applies
	 * that elapsed time to the new song and rushes its opening notes on OPL. */
	sdl3_timer_pump();
}

/* These operations are also no-ops in AD15: banks/master SysEx belong to
 * MT-32, and pitch changes are applied by the per-context 100 Hz update. */
void dos_audio_driver_load_bank(void *bank)
{
	(void)bank;
}

void dos_audio_driver_set_pitch(legacy_u8 *timer, legacy_s16 pitch, legacy_s16 driver_channel)
{
	(void)timer;
	(void)pitch;
	(void)driver_channel;
}

void dos_audio_driver_send_data(legacy_u16 length, legacy_u8 *data)
{
	(void)length;
	(void)data;
}

void dos_audio_driver_suspend_all(struct AUDIO_CONTEXT *contexts)
{
	(void)contexts;
}

void dos_audio_driver_set_master_state(legacy_s16 operation, void *state)
{
	(void)operation;
	(void)state;
}

void dos_audio_shutdown(void)
{
	sdl3_music_shutdown();
	audio_update_lock = AUDIO_UPDATE_LOCKED;
	timer_remove_callback(audio_sequence_timer);
	dos_audio_driver_start();
	adlib_ready = 0;
	adlib_music_muted = 0;
#ifndef __DJGPP__
	adlib_trace_close();
	if (adlib_stream) {
		SDL_DestroyAudioStream(adlib_stream);
		adlib_stream = NULL;
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
	}
	if (adlib_chip) {
		adlib_delete_chip();
	}
#endif
	dos_audio_driver_binary = NULL;
	dos_audio_uses_direct_channels = 0;
	dos_audio_special_mode = 0;
	dos_audio_context_count = 0;
	audio_music_enabled = AUDIO_STATE_DISABLED;
	audio_effects_enabled = AUDIO_STATE_DISABLED;
	audio_update_lock = AUDIO_UPDATE_UNLOCKED;
}
