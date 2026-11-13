/* Portable PC15.DRV protocol implementation, based on the project's TempleOS
 * PC speaker backend. The existing sequencer retains its original context
 * masks, note allocation, envelopes, modulation, and effect priorities. */
#include "pc_speaker_driver.h"
#include "../../c/audio/audio_internal.h"
#include <stdlib.h>
#include <string.h>

#define PC_DEFAULT_VOLUME_COUNT 5U
#define PC_NOTE_COUNT 120U
#define PC_SAMPLE_NOTE_COUNT 60U
#define PC_ENGINE_PITCH_SCALE 16U
#define PC_ENGINE_PITCH_MINIMUM 304U
#define PC_PITCH_FRACTION_BITS 13U
#define PC_POSITIVE_BEND_LIMIT 4096U
#define PC_POSITIVE_BEND_ADJUSTMENT 128U
#define PC_MIX_PAIR_PHASE 2U
#define PC_VOICE_SAMPLE 0U
#define PC_VOICE_LOW_FIRST 1U
#define PC_VOICE_LOW_SECOND 2U
#define PC_VOICE_HIGH_FIRST 3U
#define PC_VOICE_HIGH_SECOND 4U
#define PC_VOICE_EFFECT_FIRST 5U
#define PC_VOICE_EFFECT_SECOND 6U
#define PC_RESOURCE_DETUNE_OFFSET 17U
#define PC_RESOURCE_BEND_RANGE_OFFSET 18U
#define PC_RESOURCE_ENVELOPE_MODE_OFFSET 25U
#define PC_RESOURCE_MODULATION_MODE_OFFSET 40U
#define PC_RESOURCE_SEQUENCE_MODE_OFFSET 53U
#define PC_RESOURCE_SEQUENCE_NOTE 1U
#define PC_RESOURCE_ROUTE_PERIOD 2U
#define PC_CONTEXT_NOTE_OFFSET 0U
#define PC_CONTEXT_BASE_OFFSET 1U
#define PC_CONTEXT_CURRENT_OFFSET 3U
#define PC_PCM_AMPLITUDE 8192
#define PC_TONE_HALF_PHASE 0x80000000U
#define PC_PIT_FULL_PERIOD 65536U

/* Exact PC15.DRV tables, shared with the TempleOS implementation. */
static const legacy_u16 pc_note_periods[PC_NOTE_COUNT] = {
	0,	   0,	  65009, 61360, 57917, 54666, 51598, 48702, 45968, 43388, 40953, 38655,
	36485, 34437, 32505, 30680, 28958, 27333, 25799, 24351, 22984, 21694, 20477, 19327,
	18243, 17219, 16252, 15340, 14479, 13666, 12899, 12175, 11492, 10847, 10238, 9664,
	9121,  8609,  8126,	 7670,	7240,  6833,  6450,	 6088,	5746,  5424,  5119,	 4832,
	4561,  4305,  4063,	 3835,	3620,  3417,  3225,	 3044,	2873,  2712,  2560,	 2416,
	2280,  2152,  2032,	 1918,	1810,  1708,  1612,	 1522,	1437,  1356,  1280,	 1208,
	1140,  1076,  1016,	 959,	905,   854,	  806,	 761,	718,   678,	  640,	 604,
	570,   538,	  508,	 479,	452,   427,	  403,	 380,	359,   339,	  320,	 302,
	285,   269,	  254,	 240,	226,   214,	  202,	 190,	180,   169,	  160,	 151,
	143,   135,	  127,	 120,	113,   107,	  101,	 95,	90,	   85,	  80,	 75};
static const legacy_u16 pc_sample_rates[PC_SAMPLE_NOTE_COUNT] = {
	1047,  1109,  1175,	 1245,	1319,  1397,  1480,	 1568,	1661,  1760,  1865,	 1976,
	2093,  2217,  2349,	 2489,	2637,  2794,  2960,	 3136,	3322,  3520,  3729,	 3951,
	4186,  4435,  4699,	 4978,	5274,  5588,  5920,	 6272,	6645,  7040,  7459,	 7902,
	8372,  8870,  9397,	 9956,	10548, 11175, 11840, 12544, 13290, 14080, 14917, 15804,
	16744, 17740, 18795, 19912, 21096, 22351, 23680, 25088, 26580, 28160, 29834, 31609};

static legacy_u8 pc_valid_channel(legacy_s16 channel)
{
	return channel >= 0 && (legacy_u16)channel < PC_SPEAKER_CONTEXT_COUNT;
}

legacy_u16 pc_speaker_driver_note_period(legacy_u8 note)
{
	return note < PC_NOTE_COUNT ? pc_note_periods[note] : 0;
}

static void pc_store_period(struct AUDIO_CONTEXT *context, const struct PC_SPEAKER_VOICE *voice)
{
	if (context != NULL) {
		context->driver_private_state[PC_CONTEXT_NOTE_OFFSET] = voice->note;
		LEGACY_WRITE_U16_LE(context->driver_private_state + PC_CONTEXT_BASE_OFFSET,
							voice->base_period);
		LEGACY_WRITE_U16_LE(context->driver_private_state + PC_CONTEXT_CURRENT_OFFSET,
							voice->period);
	}
}

static void pc_stop_sample(struct PC_SPEAKER_DRIVER *driver)
{
	free(driver->sample_data);
	driver->sample_data = NULL;
	driver->sample_length = 0;
	driver->sample_position = 0;
	driver->sample_active = 0;
	driver->voices[PC_VOICE_SAMPLE].active = 0;
}

void pc_speaker_driver_initialize(struct PC_SPEAKER_DRIVER *driver)
{
	memset(driver, 0, sizeof(*driver));
	driver->selected_voice = -1;
	for (size_t index = 0; index < PC_DEFAULT_VOLUME_COUNT; index++) {
		driver->voices[index].volume = AUDIO_ENGINE_MAX_VOLUME;
	}
}

void pc_speaker_driver_shutdown(struct PC_SPEAKER_DRIVER *driver)
{
	pc_stop_sample(driver);
	memset(driver, 0, sizeof(*driver));
	driver->selected_voice = -1;
}

void pc_speaker_driver_prepare(struct PC_SPEAKER_DRIVER *driver, legacy_s16 channel,
							   struct AUDIO_CHANNEL *source)
{
	if (pc_valid_channel(channel)) {
		driver->voices[channel].channel = source;
	}
}

void pc_speaker_driver_engine_value(struct PC_SPEAKER_DRIVER *driver, legacy_s16 channel,
									struct AUDIO_CONTEXT *context, legacy_u16 value)
{
	if (!pc_valid_channel(channel) || channel == PC_VOICE_SAMPLE) {
		return;
	}
	struct PC_SPEAKER_VOICE *voice = &driver->voices[channel];
	voice->base_period = value > PC_ENGINE_PITCH_MINIMUM
							 ? (legacy_u16)((PC_SPEAKER_PIT_HZ * PC_ENGINE_PITCH_SCALE) / value)
							 : 0;
	voice->period = voice->base_period;
	pc_store_period(context, voice);
}

static void pc_start_sample(struct PC_SPEAKER_DRIVER *driver, legacy_u8 note,
							const legacy_u8 *sample, size_t sample_bytes)
{
	pc_stop_sample(driver);
	if (note >= PC_SAMPLE_NOTE_COUNT || sample == NULL ||
		sample_bytes < PC_SPEAKER_SAMPLE_HEADER_BYTES) {
		return;
	}
	legacy_u32 length = LEGACY_READ_U32_LE(sample + PC_SPEAKER_SAMPLE_LENGTH_OFFSET);
	if (length == 0 || length > PC_SPEAKER_SAMPLE_MAX_BYTES ||
		length > sample_bytes - PC_SPEAKER_SAMPLE_HEADER_BYTES) {
		return;
	}
	driver->sample_data = malloc(length);
	if (driver->sample_data == NULL) {
		return;
	}
	memcpy(driver->sample_data, sample + PC_SPEAKER_SAMPLE_HEADER_BYTES, length);
	driver->sample_length = length;
	driver->sample_rate = pc_sample_rates[note];
	driver->sample_active = 1;
	driver->voices[PC_VOICE_SAMPLE].active = 1;
}

void pc_speaker_driver_activate(struct PC_SPEAKER_DRIVER *driver, legacy_s16 channel,
								struct AUDIO_CONTEXT *context, struct AUDIO_CHANNEL *source,
								legacy_s16 note, const legacy_u8 *sample, size_t sample_bytes)
{
	if (!pc_valid_channel(channel)) {
		return;
	}
	struct PC_SPEAKER_VOICE *voice = &driver->voices[channel];
	voice->channel = source;
	voice->note = (legacy_u8)note;
	if (channel == PC_VOICE_SAMPLE) {
		pc_start_sample(driver, voice->note, sample, sample_bytes);
	} else {
		if (voice->note != AUDIO_NOTE_USE_DRIVER_VALUE) {
			voice->base_period = pc_speaker_driver_note_period(voice->note);
			voice->period = voice->base_period;
		}
		voice->active = 1;
	}
	pc_store_period(context, voice);
}

void pc_speaker_driver_release(struct PC_SPEAKER_DRIVER *driver, legacy_s16 channel)
{
	if (pc_valid_channel(channel)) {
		if (channel == PC_VOICE_SAMPLE) {
			pc_stop_sample(driver);
		}
		driver->voices[channel].active = 0;
	}
}

void pc_speaker_driver_start(struct PC_SPEAKER_DRIVER *driver)
{
	pc_stop_sample(driver);
	for (size_t index = 0; index < PC_SPEAKER_CONTEXT_COUNT; index++) {
		driver->voices[index].active = 0;
	}
	driver->output_active = 0;
	driver->selected_voice = -1;
}

void pc_speaker_driver_update_context(struct PC_SPEAKER_DRIVER *driver, legacy_s16 channel,
									  struct AUDIO_CONTEXT *context, const legacy_u8 *resource)
{
	if (!pc_valid_channel(channel) || channel == PC_VOICE_SAMPLE || context == NULL ||
		context->state == AUDIO_CONTEXT_STATE_FREE || resource == NULL) {
		return;
	}
	struct PC_SPEAKER_VOICE *voice = &driver->voices[channel];
	legacy_u8 note = voice->note;
	legacy_u16 period = voice->base_period;
	if (resource[PC_RESOURCE_SEQUENCE_MODE_OFFSET] == PC_RESOURCE_SEQUENCE_NOTE) {
		note = (legacy_u8)(note + context->sequence_value);
		period = pc_speaker_driver_note_period(note);
	}
	period = LEGACY_U16_WRAP_SUB(period, (legacy_s8)resource[PC_RESOURCE_DETUNE_OFFSET]);
	legacy_s16 pitch = voice->channel != NULL ? LEGACY_S16_FROM_BITS(voice->channel->pitch) : 0;
	if (pitch != 0) {
		legacy_u16 magnitude = pitch > 0 ? (legacy_u16)pitch : (legacy_u16)(-(legacy_s32)pitch);
		legacy_u16 difference;
		if (pitch > 0) {
			difference = LEGACY_U16_WRAP_SUB(
				period, pc_speaker_driver_note_period(
							(legacy_u8)(note + resource[PC_RESOURCE_BEND_RANGE_OFFSET])));
			if (magnitude >= PC_POSITIVE_BEND_LIMIT) {
				magnitude = LEGACY_U16_WRAP_ADD(magnitude, PC_POSITIVE_BEND_ADJUSTMENT);
			}
		} else {
			difference =
				LEGACY_U16_WRAP_SUB(pc_speaker_driver_note_period((
										legacy_u8)(note - resource[PC_RESOURCE_BEND_RANGE_OFFSET])),
									period);
		}
		legacy_s32 product = LEGACY_S32_FROM_BITS((legacy_u32)difference * magnitude);
		legacy_u16 adjustment = (legacy_u16)LEGACY_S32_SAR(product, PC_PITCH_FRACTION_BITS);
		period = pitch > 0 ? LEGACY_U16_WRAP_SUB(period, adjustment)
						   : LEGACY_U16_WRAP_ADD(period, adjustment);
	}
	if (resource[PC_RESOURCE_MODULATION_MODE_OFFSET] == PC_RESOURCE_ROUTE_PERIOD) {
		period = LEGACY_U16_WRAP_ADD(period, context->modulation);
	}
	if (resource[PC_RESOURCE_ENVELOPE_MODE_OFFSET] == PC_RESOURCE_ROUTE_PERIOD) {
		period = LEGACY_U16_WRAP_ADD(period, context->level);
	}
	voice->period = period;
	pc_store_period(context, voice);
}

void pc_speaker_driver_volume(struct PC_SPEAKER_DRIVER *driver, legacy_s16 channel,
							  legacy_u8 volume)
{
	if (pc_valid_channel(channel)) {
		driver->voices[channel].volume = volume;
	}
}

static legacy_u8 pc_voice_audible(const struct PC_SPEAKER_DRIVER *driver, size_t index)
{
	const struct PC_SPEAKER_VOICE *voice = &driver->voices[index];
	return voice->active && voice->volume &&
		   !(driver->music_muted && voice->channel != NULL &&
			 voice->channel->channel < AUDIO_EFFECT_CHANNEL_FIRST);
}

static void pc_select_voice(struct PC_SPEAKER_DRIVER *driver)
{
	legacy_s8 selected = -1;
	if (driver->sample_active && pc_voice_audible(driver, PC_VOICE_SAMPLE)) {
		selected = PC_VOICE_SAMPLE;
	} else if (pc_voice_audible(driver, PC_VOICE_EFFECT_SECOND)) {
		selected = PC_VOICE_EFFECT_SECOND;
	} else if (pc_voice_audible(driver, PC_VOICE_EFFECT_FIRST)) {
		selected = PC_VOICE_EFFECT_FIRST;
	} else if ((pc_voice_audible(driver, PC_VOICE_HIGH_FIRST) ||
				pc_voice_audible(driver, PC_VOICE_HIGH_SECOND)) &&
			   (!(driver->mix_phase & PC_MIX_PAIR_PHASE) ||
				!(pc_voice_audible(driver, PC_VOICE_LOW_FIRST) ||
				  pc_voice_audible(driver, PC_VOICE_LOW_SECOND)))) {
		selected = pc_voice_audible(driver, PC_VOICE_HIGH_FIRST) ? PC_VOICE_HIGH_FIRST
																 : PC_VOICE_HIGH_SECOND;
	} else if (pc_voice_audible(driver, PC_VOICE_LOW_FIRST)) {
		selected = PC_VOICE_LOW_FIRST;
	} else if (pc_voice_audible(driver, PC_VOICE_LOW_SECOND)) {
		selected = PC_VOICE_LOW_SECOND;
	}
	legacy_u8 active = selected > (legacy_s8)PC_VOICE_SAMPLE;
	if (active) {
		legacy_u16 period = driver->voices[selected].period;
		if (!driver->output_active || period != driver->output_period) {
			/* Programming a new PIT divisor starts a new square-wave cycle. */
			driver->tone_phase = 0;
		}
		driver->output_period = period;
	}
	driver->selected_voice = selected;
	driver->output_active = active;
}

void pc_speaker_driver_mix(struct PC_SPEAKER_DRIVER *driver)
{
	pc_select_voice(driver);
	driver->mix_phase++;
}

void pc_speaker_driver_set_music_muted(struct PC_SPEAKER_DRIVER *driver, legacy_u8 muted)
{
	driver->music_muted = muted != 0;
	/* Source switches change audibility immediately, without advancing the
     * original arbitration phase or changing any sequencer contexts. */
	pc_select_voice(driver);
}

static legacy_u32 pc_tone_step(legacy_u16 divisor, legacy_u32 sample_rate)
{
	legacy_u32 period = divisor != 0 ? divisor : PC_PIT_FULL_PERIOD;
	return (legacy_u32)(((legacy_u64)PC_SPEAKER_PIT_HZ << PC_SPEAKER_PHASE_BITS) /
						((legacy_u64)period * sample_rate));
}

void pc_speaker_driver_render(struct PC_SPEAKER_DRIVER *driver, legacy_s16 *samples,
							  legacy_u32 frames, legacy_u32 sample_rate)
{
	if (sample_rate == 0 || samples == NULL) {
		return;
	}
	legacy_u32 tone_step = pc_tone_step(driver->output_period, sample_rate);
	legacy_u64 sample_step =
		((legacy_u64)driver->sample_rate << PC_SPEAKER_PHASE_BITS) / sample_rate;
	for (legacy_u32 frame = 0; frame < frames; frame++) {
		legacy_s16 sample = 0;
		if (driver->sample_active) {
			size_t index = (size_t)(driver->sample_position >> PC_SPEAKER_PHASE_BITS);
			if (index >= driver->sample_length) {
				driver->sample_active = 0;
				driver->voices[PC_VOICE_SAMPLE].active = 0;
				pc_select_voice(driver);
				tone_step = pc_tone_step(driver->output_period, sample_rate);
			} else {
				if (driver->selected_voice == PC_VOICE_SAMPLE) {
					sample =
						((legacy_s16)driver->sample_data[index] - PC_SPEAKER_PCM_UNSIGNED_CENTER) *
						PC_SPEAKER_PCM_SAMPLE_SCALE;
				}
				driver->sample_position += sample_step;
			}
		}
		if (driver->output_active) {
			sample = driver->tone_phase & PC_TONE_HALF_PHASE ? PC_PCM_AMPLITUDE : -PC_PCM_AMPLITUDE;
			driver->tone_phase += tone_step;
		}
		samples[frame] = sample;
	}
}
