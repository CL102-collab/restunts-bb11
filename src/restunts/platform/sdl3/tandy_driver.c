/* Native TD15.DRV protocol. Tables and arithmetic follow the supplied driver;
 * the portable sequencer owns resource envelopes, priorities and allocation. */
#include "tandy_driver.h"
#include "../../c/audio/audio_internal.h"
#include <string.h>

#define TANDY_TONE_FIRST 1U
#define TANDY_TONE_LAST 3U
#define TANDY_NOISE_CHANNEL 4U
#define TANDY_SPEAKER_CHANNEL 5U
#define TANDY_SPEAKER_VOICE 1U
#define TANDY_NOTE_COUNT 120U
#define TANDY_DAC_NOTE_COUNT 96U
#define TANDY_NOISE_NOTE_MASK 7U
#define TANDY_NOISE_NOTE_COUNT (TANDY_NOISE_NOTE_MASK + 1U)
#define TANDY_PERIOD_MAX 1023U
#define TANDY_VOLUME_MAX 15U
#define TANDY_VELOCITY_COUNT (TANDY_VOLUME_MAX + 1U)
#define TANDY_VOLUME_SHIFT 3U
#define TANDY_VOLUME_ROUND 7U
#define TANDY_DAC_VOLUME_MAX 7U
#define TANDY_DAC_CLOCK_HZ 3584000U
#define TANDY_TONE_ENGINE_NUMERATOR 1790000U
#define TANDY_TONE_ENGINE_MINIMUM 1750U
#define TANDY_DAC_ENGINE_NUMERATOR 57344000U
#define TANDY_DAC_ENGINE_MINIMUM 880U
#define TANDY_PC_ENGINE_NUMERATOR 19090880U
#define TANDY_PC_ENGINE_MINIMUM 292U
#define TANDY_RESOURCE_DETUNE 17U
#define TANDY_RESOURCE_BEND_RANGE 18U
#define TANDY_RESOURCE_VELOCITY 21U
#define TANDY_RESOURCE_ENVELOPE 25U
#define TANDY_RESOURCE_MODULATION 40U
#define TANDY_RESOURCE_SEQUENCE 53U
#define TANDY_ROUTE_PERIOD 2U
#define TANDY_ROUTE_VOLUME 3U
#define TANDY_SEQUENCE_NOTE 1U
#define TANDY_BEND_FRACTION_BITS 13U
#define TANDY_BEND_POSITIVE_LIMIT 4096U
#define TANDY_BEND_POSITIVE_ADJUSTMENT 128U
#define TANDY_CONTEXT_NOTE 0U
#define TANDY_CONTEXT_BASE 1U
#define TANDY_CONTEXT_CURRENT 3U
#define TANDY_PSG_LATCH 128U
#define TANDY_PSG_NOISE_REGISTER 6U
#define TANDY_PSG_REGISTER_SHIFT 4U
#define TANDY_PSG_TONE_HIGH_SHIFT 4U
#define TANDY_PSG_TONE_LOW_MASK 15U
#define TANDY_RENDER_FRAMES 128U
#define TANDY_PCM_MINIMUM (-32768)
#define TANDY_PCM_MAXIMUM 32767

/* TD15.DRV tables at 032f, 041f, 04df and 04ef respectively. */
static const legacy_u16 tandy_tone_periods[TANDY_NOTE_COUNT] = {
	0,	 0,	  0,   0,	0,	 0,	  0,   0,	0,	 0,	  0,   0,	0,	 0,	  0,   0,	 0,	  0,
	0,	 0,	  0,   0,	0,	 0,	  0,   0,	0,	 0,	  0,   0,	0,	 0,	  0,   1017, 960, 906,
	855, 807, 762, 719, 679, 641, 605, 571, 539, 509, 480, 453, 428, 404, 381, 360,	 339, 320,
	302, 285, 269, 254, 240, 227, 214, 202, 190, 180, 170, 160, 151, 143, 135, 127,	 120, 113,
	107, 101, 95,  90,	85,	 80,  76,  71,	67,	 64,  60,  57,	53,	 50,  48,  45,	 42,  40,
	38,	 36,  34,  32,	30,	 28,  27,  25,	24,	 22,  21,  20,	19,	 18,  17,  16,	 15,  14,
	13,	 13,  12,  11,	11,	 10,  9,   9,	8,	 8,	  7,   7};

static const legacy_u16 tandy_dac_periods[TANDY_DAC_NOTE_COUNT] = {
	3425, 3233, 3051, 2880, 2718, 2566, 2422, 2286, 2157, 2036, 1922, 1814, 1712, 1616, 1526, 1440,
	1359, 1283, 1211, 1143, 1079, 1018, 961,  907,	856,  808,	763,  720,	680,  641,	605,  571,
	539,  509,	481,  454,	428,  404,	381,  360,	340,  321,	303,  286,	270,  255,	240,  227,
	214,  202,	191,  180,	170,  160,	151,  143,	135,  127,	120,  113,	107,  101,	95,	  90,
	85,	  80,	76,	  71,	67,	  64,	60,	  57,	54,	  51,	48,	  45,	42,	  40,	38,	  36,
	34,	  32,	30,	  28,	27,	  25,	24,	  22,	21,	  20,	19,	  18,	17,	  16,	15,	  14};
static const legacy_u16 tandy_noise_periods[TANDY_NOISE_NOTE_COUNT] = {2, 1, 0, 0, 6, 5, 4, 4};
static const legacy_u8 tandy_velocities[TANDY_VELOCITY_COUNT] = {1, 2, 3,  4,  5,  7,  8,  8,
																 9, 9, 10, 10, 11, 13, 14, 15};

static legacy_u8 tandy_valid_channel(legacy_s16 channel)
{
	return channel >= 0 && (legacy_u16)channel < TANDY_CONTEXT_COUNT;
}

legacy_u16 tandy_driver_note_period(legacy_s16 channel, legacy_u8 note)
{
	if (channel == TANDY_SAMPLE_CHANNEL) {
		return note < TANDY_DAC_NOTE_COUNT ? tandy_dac_periods[note] : 0;
	}
	if (channel == TANDY_NOISE_CHANNEL) {
		return tandy_noise_periods[note & TANDY_NOISE_NOTE_MASK];
	}
	if (channel == TANDY_SPEAKER_CHANNEL) {
		return pc_speaker_driver_note_period(note);
	}
	return channel >= (legacy_s16)TANDY_TONE_FIRST && channel <= (legacy_s16)TANDY_TONE_LAST &&
				   note < TANDY_NOTE_COUNT
			   ? tandy_tone_periods[note]
			   : 0;
}

static void tandy_store_period(struct AUDIO_CONTEXT *context, const struct TANDY_VOICE *voice)
{
	if (context != NULL) {
		context->driver_private_state[TANDY_CONTEXT_NOTE] = voice->note;
		LEGACY_WRITE_U16_LE(context->driver_private_state + TANDY_CONTEXT_BASE, voice->base_period);
		LEGACY_WRITE_U16_LE(context->driver_private_state + TANDY_CONTEXT_CURRENT, voice->period);
	}
}

static legacy_u8 tandy_combine_volume(legacy_u8 left, legacy_u8 right)
{
	legacy_u16 value = ((legacy_u16)left * right + TANDY_VOLUME_ROUND) / TANDY_VOLUME_MAX;
	return value > TANDY_VOLUME_MAX ? TANDY_VOLUME_MAX : (legacy_u8)value;
}

static legacy_u8 tandy_audible(const struct TANDY_DRIVER *driver, legacy_u16 channel)
{
	const struct TANDY_VOICE *voice = &driver->voices[channel];
	return voice->active && !(driver->music_muted && voice->channel != NULL &&
							  voice->channel->channel < AUDIO_EFFECT_CHANNEL_FIRST);
}

static void tandy_write_register(struct TANDY_DRIVER *driver, legacy_u8 reg, legacy_u16 value)
{
	if (driver->register_valid[reg] && driver->registers[reg] == value) {
		return;
	}
	driver->register_valid[reg] = 1;
	driver->registers[reg] = value;
	mame_psg_write(&driver->psg, (legacy_u8)(TANDY_PSG_LATCH | (reg << TANDY_PSG_REGISTER_SHIFT) |
											 (value & TANDY_PSG_TONE_LOW_MASK)));
	if ((reg & 1U) == 0 && reg < TANDY_PSG_NOISE_REGISTER) {
		mame_psg_write(&driver->psg, (legacy_u8)(value >> TANDY_PSG_TONE_HIGH_SHIFT));
	}
	/* Send only the noise latch. MAME accepts the redundant data write used
	 * by TD15, although its NCR hardware behavior is still unverified; that
	 * extra byte would incorrectly replace white noise with periodic zero. */
}

static void tandy_apply_voice(struct TANDY_DRIVER *driver, legacy_u16 channel)
{
	struct TANDY_VOICE *voice = &driver->voices[channel];
	legacy_u8 audible = tandy_audible(driver, channel);
	if (channel >= TANDY_TONE_FIRST && channel <= TANDY_NOISE_CHANNEL) {
		legacy_u8 reg = (legacy_u8)((channel - TANDY_TONE_FIRST) * 2U);
		legacy_u16 period = voice->period > TANDY_PERIOD_MAX ? TANDY_PERIOD_MAX : voice->period;
		tandy_write_register(driver, (legacy_u8)(reg + 1U),
							 audible ? TANDY_VOLUME_MAX - voice->level : TANDY_VOLUME_MAX);
		if (voice->active) {
			tandy_write_register(driver, reg, period);
		}
	} else if (channel == TANDY_SAMPLE_CHANNEL) {
		driver->sample.sample_rate = voice->period != 0 ? TANDY_DAC_CLOCK_HZ / voice->period : 0;
		driver->sample.voices[TANDY_SAMPLE_CHANNEL].volume = audible ? AUDIO_ENGINE_MAX_VOLUME : 0;
		pc_speaker_driver_set_music_muted(&driver->sample, driver->music_muted);
	} else if (channel == TANDY_SPEAKER_CHANNEL) {
		struct PC_SPEAKER_VOICE *speaker = &driver->speaker.voices[TANDY_SPEAKER_VOICE];
		speaker->channel = voice->channel;
		speaker->period = voice->period;
		speaker->active = audible;
		/* TD15 gates the PIT but has no physical amplitude control there.
		 * Its computed PSG/DAC attenuation does not scale this output. */
		speaker->volume = AUDIO_ENGINE_MAX_VOLUME;
		pc_speaker_driver_set_music_muted(&driver->speaker, driver->music_muted);
	}
}

void tandy_driver_initialize(struct TANDY_DRIVER *driver, enum MAME_PSG_PROFILE profile)
{
	memset(driver, 0, sizeof(*driver));
	mame_psg_initialize(&driver->psg, profile, TANDY_PSG_CLOCK_HZ, TANDY_SAMPLE_RATE);
	pc_speaker_driver_initialize(&driver->sample);
	pc_speaker_driver_initialize(&driver->speaker);
	for (legacy_u16 channel = 0; channel < TANDY_CONTEXT_COUNT; channel++) {
		driver->voices[channel].volume = TANDY_VOLUME_MAX;
		driver->voices[channel].velocity = TANDY_VOLUME_MAX;
		tandy_apply_voice(driver, channel);
	}
}

void tandy_driver_shutdown(struct TANDY_DRIVER *driver)
{
	pc_speaker_driver_shutdown(&driver->sample);
	pc_speaker_driver_shutdown(&driver->speaker);
	memset(driver, 0, sizeof(*driver));
}

void tandy_driver_prepare(struct TANDY_DRIVER *driver, legacy_s16 channel,
						  struct AUDIO_CHANNEL *source)
{
	if (tandy_valid_channel(channel)) {
		driver->voices[channel].channel = source;
		if (source != NULL) {
			tandy_driver_volume(driver, channel, source->volume);
		}
	}
}

void tandy_driver_engine_value(struct TANDY_DRIVER *driver, legacy_s16 channel,
							   struct AUDIO_CONTEXT *context, legacy_u16 value)
{
	if (!tandy_valid_channel(channel)) {
		return;
	}
	legacy_u32 numerator = TANDY_TONE_ENGINE_NUMERATOR;
	legacy_u16 minimum = TANDY_TONE_ENGINE_MINIMUM;
	legacy_u16 fallback = TANDY_PERIOD_MAX;
	if (channel == TANDY_SAMPLE_CHANNEL) {
		numerator = TANDY_DAC_ENGINE_NUMERATOR;
		minimum = TANDY_DAC_ENGINE_MINIMUM;
	} else if (channel == TANDY_SPEAKER_CHANNEL) {
		numerator = TANDY_PC_ENGINE_NUMERATOR;
		minimum = TANDY_PC_ENGINE_MINIMUM;
		fallback = 0;
	}
	struct TANDY_VOICE *voice = &driver->voices[channel];
	voice->base_period = value > minimum ? (legacy_u16)(numerator / value) : fallback;
	voice->period = voice->base_period;
	tandy_store_period(context, voice);
}

void tandy_driver_activate(struct TANDY_DRIVER *driver, legacy_s16 channel,
						   struct AUDIO_CONTEXT *context, struct AUDIO_CHANNEL *source,
						   legacy_s16 note, legacy_u16 parameter, const legacy_u8 *resource,
						   const legacy_u8 *sample, size_t sample_bytes)
{
	if (!tandy_valid_channel(channel)) {
		return;
	}
	struct TANDY_VOICE *voice = &driver->voices[channel];
	voice->channel = source;
	voice->note = (legacy_u8)note;
	if (voice->note != AUDIO_NOTE_USE_DRIVER_VALUE) {
		voice->base_period = tandy_driver_note_period(channel, voice->note);
		voice->period = voice->base_period;
	}
	legacy_u8 velocity = resource != NULL && resource[TANDY_RESOURCE_VELOCITY] != 0
							 ? (legacy_u8)(parameter & AUDIO_ENGINE_MAX_VOLUME)
							 : AUDIO_ENGINE_MAX_VOLUME;
	voice->velocity = tandy_velocities[velocity >> TANDY_VOLUME_SHIFT];
	voice->level = tandy_combine_volume(voice->volume, voice->velocity);
	voice->active = 1;
	if (channel == TANDY_SAMPLE_CHANNEL) {
		/* The shared helper bounds and owns a copy; TD uses its own divisor
		 * table and volume rather than PC15's fixed sample-note rate table. */
		pc_speaker_driver_activate(&driver->sample, 0, NULL, source, 0, sample, sample_bytes);
		voice->active = driver->sample.sample_active;
	}
	tandy_store_period(context, voice);
	tandy_apply_voice(driver, (legacy_u16)channel);
}

void tandy_driver_release(struct TANDY_DRIVER *driver, legacy_s16 channel)
{
	if (tandy_valid_channel(channel)) {
		driver->voices[channel].active = 0;
		if (channel == TANDY_SAMPLE_CHANNEL) {
			pc_speaker_driver_release(&driver->sample, 0);
		}
		tandy_apply_voice(driver, (legacy_u16)channel);
	}
}

void tandy_driver_start(struct TANDY_DRIVER *driver)
{
	for (legacy_u16 channel = 0; channel < TANDY_CONTEXT_COUNT; channel++) {
		tandy_driver_release(driver, (legacy_s16)channel);
	}
}

void tandy_driver_update_context(struct TANDY_DRIVER *driver, legacy_s16 channel,
								 struct AUDIO_CONTEXT *context, const legacy_u8 *resource)
{
	if (!tandy_valid_channel(channel) || context == NULL ||
		context->state == AUDIO_CONTEXT_STATE_FREE || resource == NULL) {
		return;
	}
	struct TANDY_VOICE *voice = &driver->voices[channel];
	legacy_u8 note = voice->note;
	legacy_u16 period = voice->base_period;
	if (resource[TANDY_RESOURCE_SEQUENCE] == TANDY_SEQUENCE_NOTE) {
		note = (legacy_u8)(note + context->sequence_value);
		period = tandy_driver_note_period(channel, note);
	}
	period = LEGACY_U16_WRAP_SUB(period, (legacy_s8)resource[TANDY_RESOURCE_DETUNE]);
	legacy_s16 pitch = voice->channel != NULL ? LEGACY_S16_FROM_BITS(voice->channel->pitch) : 0;
	if (pitch != 0) {
		legacy_u16 magnitude = pitch > 0 ? (legacy_u16)pitch : (legacy_u16)(-(legacy_s32)pitch);
		legacy_u16 difference;
		if (pitch > 0) {
			difference = LEGACY_U16_WRAP_SUB(
				period, tandy_driver_note_period(
							channel, (legacy_u8)(note + resource[TANDY_RESOURCE_BEND_RANGE])));
			if (magnitude >= TANDY_BEND_POSITIVE_LIMIT) {
				magnitude = LEGACY_U16_WRAP_ADD(magnitude, TANDY_BEND_POSITIVE_ADJUSTMENT);
			}
		} else {
			difference = LEGACY_U16_WRAP_SUB(
				tandy_driver_note_period(channel,
										 (legacy_u8)(note - resource[TANDY_RESOURCE_BEND_RANGE])),
				period);
		}
		legacy_s32 product = LEGACY_S32_FROM_BITS((legacy_u32)difference * magnitude);
		legacy_u16 adjustment = (legacy_u16)LEGACY_S32_SAR(product, TANDY_BEND_FRACTION_BITS);
		period = pitch > 0 ? LEGACY_U16_WRAP_SUB(period, adjustment)
						   : LEGACY_U16_WRAP_ADD(period, adjustment);
	}
	if (resource[TANDY_RESOURCE_MODULATION] == TANDY_ROUTE_PERIOD) {
		period = LEGACY_U16_WRAP_ADD(period, context->modulation);
	}
	if (resource[TANDY_RESOURCE_ENVELOPE] == TANDY_ROUTE_PERIOD) {
		period = LEGACY_U16_WRAP_ADD(period, context->level);
	}
	voice->period = period;
	voice->level = tandy_combine_volume(voice->volume, voice->velocity);
	if (resource[TANDY_RESOURCE_ENVELOPE] == TANDY_ROUTE_VOLUME) {
		voice->level = tandy_combine_volume(
			voice->level, (legacy_u8)((legacy_u16)context->level >> LEGACY_BYTE_BITS));
	}
	tandy_store_period(context, voice);
	tandy_apply_voice(driver, (legacy_u16)channel);
}

void tandy_driver_volume(struct TANDY_DRIVER *driver, legacy_s16 channel, legacy_u8 volume)
{
	if (tandy_valid_channel(channel)) {
		driver->voices[channel].volume = (volume & AUDIO_ENGINE_MAX_VOLUME) >> TANDY_VOLUME_SHIFT;
		/* The next sequencer update reapplies the current envelope. */
	}
}

void tandy_driver_set_music_muted(struct TANDY_DRIVER *driver, legacy_u8 muted)
{
	driver->music_muted = muted != 0;
	tandy_driver_mix(driver);
}

void tandy_driver_mix(struct TANDY_DRIVER *driver)
{
	for (legacy_u16 channel = 0; channel < TANDY_CONTEXT_COUNT; channel++) {
		tandy_apply_voice(driver, channel);
	}
}

void tandy_driver_render(struct TANDY_DRIVER *driver, legacy_s16 *samples, legacy_u32 frames,
						 legacy_u32 sample_rate)
{
	if (samples == NULL) {
		return;
	}
	if (sample_rate != TANDY_SAMPLE_RATE) {
		memset(samples, 0, (size_t)frames * sizeof(*samples));
		return;
	}
	legacy_s16 pcm[TANDY_RENDER_FRAMES];
	legacy_s16 speaker[TANDY_RENDER_FRAMES];
	for (legacy_u32 offset = 0; offset < frames;) {
		legacy_u32 count = frames - offset;
		if (count > TANDY_RENDER_FRAMES) {
			count = TANDY_RENDER_FRAMES;
		}
		mame_psg_render(&driver->psg, samples + offset, count);
		pc_speaker_driver_render(&driver->sample, pcm, count, sample_rate);
		pc_speaker_driver_render(&driver->speaker, speaker, count, sample_rate);
		legacy_u8 dac_volume = driver->voices[TANDY_SAMPLE_CHANNEL].level >> 1U;
		for (legacy_u32 frame = 0; frame < count; frame++) {
			legacy_s32 mixed = samples[offset + frame] + speaker[frame] +
							   (legacy_s32)pcm[frame] * dac_volume / TANDY_DAC_VOLUME_MAX;
			if (mixed > TANDY_PCM_MAXIMUM) {
				mixed = TANDY_PCM_MAXIMUM;
			} else if (mixed < TANDY_PCM_MINIMUM) {
				mixed = TANDY_PCM_MINIMUM;
			}
			samples[offset + frame] = (legacy_s16)mixed;
		}
		offset += count;
	}
	driver->voices[TANDY_SAMPLE_CHANNEL].active = driver->sample.sample_active;
}
