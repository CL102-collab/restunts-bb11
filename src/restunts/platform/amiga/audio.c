/* Four-voice Paula adaptation of the original AdLib instruments. The shared
 * DOS sequencer still owns note allocation, timing, envelopes and engine RPM.
 * Short, looping waveforms replace OPL synthesis so the 68000 never has to mix
 * an audio stream. Timbres and four-note polyphony differ from the DOS driver.
 * All hardware channels are reserved through audio.device before touching DMA.
 */
#include <devices/audio.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <proto/exec.h>
#include <string.h>
#include "amiga.h"
#include "../../c/audio.h"
#include "../../c/audio_internal.h"
#include "../../c/platform.h"
#include "../../c/resource_bytes.h"
#include "../../c/timing.h"

#define PAULA_VOICES 4U
/* AD15 resource masks reserve bit zero for the original sampled sound path. */
#define PAULA_CONTEXTS (PAULA_VOICES + 1U)
#define PAULA_WAVE_BYTES 128U
#define PAULA_DMA_MASK 15U
#define PAULA_INTERRUPT_MASK 0x0780U
#define PAULA_MIN_PERIOD 124U
#define ADLIB_RESOURCE_BYTES 94U
#define ADLIB_MODULATOR 70U
#define ADLIB_CARRIER 82U
#define ADLIB_OPERATOR_MULTIPLIER 6U
#define ADLIB_OPERATOR_LEVEL 4U
#define ADLIB_OPERATOR_WAVEFORM 11U
#define ADLIB_TRANSPOSE_OFFSET 17U
#define ADLIB_BEND_RANGE_OFFSET 18U
#define ADLIB_VELOCITY_ENABLED_OFFSET 21U
#define ADLIB_MODULATION_CONTROL_OFFSET 22U
#define ADLIB_EXPRESSION_CONTROL_OFFSET 23U
#define ADLIB_PARAMETER_CONTROL_OFFSET 24U
#define ADLIB_LEVEL_CONTROL_OFFSET 25U
#define ADLIB_ADDITIVE_ENABLED_OFFSET 68U
#define ADLIB_FEEDBACK_OFFSET 69U
#define ADLIB_SELECTOR_MODULATOR_MULTIPLIER 0x81U
#define ADLIB_SELECTOR_CARRIER_MULTIPLIER 0x82U
#define ADLIB_SELECTOR_MODULATOR_GAIN 0x83U
#define ADLIB_SELECTOR_CARRIER_GAIN 0x84U
#define ADLIB_SELECTOR_FEEDBACK 0x85U
#define ADLIB_CONTROL_MODULATION 1U
#define ADLIB_CONTROL_VOLUME 7U
#define ADLIB_CONTROL_EXPRESSION 11U
#define ADLIB_CONTROL_PARAMETER 12U
#define PAULA_NTSC_REFRESH_HZ 60U
#define PAULA_NTSC_CLOCK_HZ 3579545UL
#define PAULA_PAL_CLOCK_HZ 3546895UL
#define ADLIB_SELECTOR_PITCH 0x90U
#define ADLIB_SELECTOR_NOTE 0x91U

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

struct PAULA_VOICE {
	const legacy_u8 *resource;
	struct AUDIO_CHANNEL *channel;
	legacy_u16 base_pitch;
	legacy_u16 last_pitch;
	legacy_u16 period;
	legacy_u16 wave_bytes;
	legacy_u8 note;
	legacy_u8 velocity;
	legacy_u8 modulator_multiplier;
	legacy_u8 carrier_multiplier;
	legacy_u8 modulator_gain;
	legacy_u8 carrier_gain;
	legacy_u8 feedback;
	legacy_u8 dirty;
	legacy_u8 active;
};

static volatile struct Custom *const paula = (volatile struct Custom *)0x00dff000;
static struct MsgPort *paula_port;
static struct IOAudio paula_request;
static struct PAULA_VOICE paula_voices[PAULA_VOICES];
static legacy_s8 *paula_waves;
static legacy_u32 paula_clock;
static legacy_u8 paula_ready;
static legacy_u8 paula_deferred;
static legacy_u8 paula_channels[] = {PAULA_DMA_MASK};

static const legacy_s8 paula_sine[32] = {
	0, 25,	49,	 71,  90,  106,	 117,  125,	 127,  125,	 117,  106,	 90,  71,  49,	25,
	0, -25, -49, -71, -90, -106, -117, -125, -127, -125, -117, -106, -90, -71, -49, -25};
static const legacy_u8 paula_multiplier[16] = {1,  2,  4,  6,  8,  10, 12, 14,
											   16, 18, 20, 20, 24, 24, 30, 30};
static const legacy_u16 adlib_frequencies[] = {
	21,	 23,  24,  25,	27,	 29,  30,  32,	34,	 36,  38,  40,	43,	 45,  48,
	51,	 54,  57,  61,	64,	 68,  72,  76,	81,	 86,  91,  96,	102, 108, 114,
	121, 128, 136, 144, 153, 162, 171, 182, 192, 204, 216, 229, 242, 257, 272,
	288, 306, 324, 343, 363, 385, 408, 432, 458, 485, 514, 544, 577, 611, 647};

static legacy_s16 paula_voice_index(legacy_s16 channel)
{
	return channel > 0 && (legacy_u16)channel <= PAULA_VOICES ? channel - 1 : -1;
}

static legacy_u16 paula_note_pitch(legacy_u16 note)
{
	note &= 255U;
	return (legacy_u16)(((note / 12U) & 7U) << 10U) | adlib_frequencies[24U + note % 12U];
}

static legacy_u16 paula_bend_target(legacy_u8 note, legacy_s16 semitones)
{
	legacy_s16 index = 24 + note % 12U + semitones;
	if (index < 0) {
		index = 0;
	}
	if ((legacy_u16)index >= sizeof(adlib_frequencies) / sizeof(adlib_frequencies[0])) {
		index = sizeof(adlib_frequencies) / sizeof(adlib_frequencies[0]) - 1;
	}
	return (legacy_u16)(((note / 12U) & 7U) << 10U) | adlib_frequencies[index];
}

static void paula_stop(legacy_u16 voice)
{
	if (paula_ready) {
		paula->aud[voice].ac_vol = 0;
		paula->dmacon = (legacy_u16)(1U << voice);
	}
	paula_voices[voice].active = 0;
}

/* DMA must see a disabled scan line before a voice is restarted. This takes
 * about 128 microseconds, only on note starts and waveform length changes. */
static void paula_dma_settle(void)
{
	for (legacy_u16 line = 0; line < 2U; ++line) {
		legacy_u16 previous = paula->vhposr & 0xff00U;
		while ((paula->vhposr & 0xff00U) == previous) {
		}
	}
}

static legacy_s16 paula_operator_sample(legacy_u16 phase, legacy_u8 waveform)
{
	legacy_s16 sample = paula_sine[phase & 31U];
	switch (waveform & 3U) {
		case 1:
			return sample < 0 ? 0 : sample;
		case 2:
			return sample < 0 ? -sample : sample;
		case 3:
			return (phase & 8U) ? 0 : (sample < 0 ? -sample : sample);
		default:
			return sample;
	}
}

static void paula_make_wave(legacy_u16 voice)
{
	struct PAULA_VOICE *state = &paula_voices[voice];
	const legacy_u8 *resource = state->resource;
	legacy_s8 *wave = paula_waves + voice * PAULA_WAVE_BYTES;
	legacy_u16 shift = 4U;
	legacy_s16 previous = 0;
	legacy_s32 sum = 0;
	while ((1U << shift) < state->wave_bytes) {
		++shift;
	}
	/* Two base cycles make the OPL half-frequency multiplier a full loop. */
	for (legacy_u16 index = 0; index < state->wave_bytes; ++index) {
		legacy_u16 mod_phase =
			(index * paula_multiplier[state->modulator_multiplier] * 32U) >> shift;
		legacy_s16 mod =
			paula_operator_sample(mod_phase, resource[ADLIB_MODULATOR + ADLIB_OPERATOR_WAVEFORM]);
		legacy_s16 phase_offset = (mod * state->modulator_gain) / 256;
		phase_offset += (previous * state->feedback) / 128;
		legacy_u16 carrier_phase =
			(index * paula_multiplier[state->carrier_multiplier] * 32U) >> shift;
		legacy_s16 sample =
			paula_operator_sample((legacy_u16)(carrier_phase + phase_offset),
								  resource[ADLIB_CARRIER + ADLIB_OPERATOR_WAVEFORM]);
		if (resource[ADLIB_ADDITIVE_ENABLED_OFFSET] != 0) {
			sample = (sample + mod * state->modulator_gain / 127) / 2;
		}
		previous = sample;
		wave[index] = (legacy_s8)sample;
		sum += sample;
	}
	/* Half-wave instruments contain DC; remove it before feeding Paula. */
	legacy_s16 mean = (legacy_s16)(sum / state->wave_bytes);
	for (legacy_u16 index = 0; index < state->wave_bytes; ++index) {
		legacy_s16 sample = wave[index] - mean;
		if (sample > 127) {
			sample = 127;
		}
		if (sample < -127) {
			sample = -127;
		}
		wave[index] = (legacy_s8)sample;
	}
	state->dirty = 0;
}

static void paula_volume(legacy_u16 voice)
{
	struct PAULA_VOICE *state = &paula_voices[voice];
	legacy_u32 volume = state->channel ? state->channel->volume : 0;
	if (volume > 127U) {
		volume = 127U;
	}
	volume = (volume * state->velocity * state->carrier_gain) >> 15U;
	if (volume > 64U) {
		volume = 64U;
	}
	if (paula_ready && !paula_deferred) {
		paula->aud[voice].ac_vol = state->active ? (UWORD)volume : 0;
	}
}

static void paula_commit_voice(legacy_u16 voice)
{
	struct PAULA_VOICE *state = &paula_voices[voice];
	if (!paula_ready || !state->resource) {
		return;
	}
	if (state->dirty) {
		legacy_u8 active = state->active;
		paula_stop(voice);
		paula_dma_settle();
		paula_make_wave(voice);
		paula->aud[voice].ac_ptr = (UWORD *)(paula_waves + voice * PAULA_WAVE_BYTES);
		paula->aud[voice].ac_len = state->wave_bytes / 2U;
		paula->aud[voice].ac_per = state->period;
		state->active = active;
	}
	paula->aud[voice].ac_per = state->period;
	if (state->active) {
		paula->dmacon = DMAF_SETCLR | DMAF_MASTER | (1U << voice);
	}
	paula_volume(voice);
}

/* Resource loads and slow frames leave several logical 100 Hz ticks pending.
 * Advance all sequencer/envelope state, then synthesize only the final audible
 * waveform. Rebuilding every historical waveform can outrun a 7 MHz 68000. */
void amiga_audio_begin_update(void)
{
	++paula_deferred;
}

void amiga_audio_end_update(void)
{
	if (!paula_deferred || --paula_deferred != 0) {
		return;
	}
	for (legacy_u16 voice = 0; voice < PAULA_VOICES; ++voice) {
		if (paula_voices[voice].active) {
			paula_commit_voice(voice);
		}
	}
}

static void paula_apply_pitch(legacy_u16 voice, legacy_u16 pitch)
{
	struct PAULA_VOICE *state = &paula_voices[voice];
	/* Envelope ticks commonly change only loudness. Avoid software 32-bit
	 * division when the Paula period and sample length stay the same. */
	if (state->period != 0 && pitch == state->last_pitch) {
		if (!paula_deferred) {
			paula_commit_voice(voice);
		}
		return;
	}
	state->last_pitch = pitch;
	legacy_u16 block = (pitch >> 10U) & 7U;
	legacy_u32 frequency16 = ((legacy_u32)(pitch & 1023U) * 50000UL) >> (16U - block);
	legacy_u16 bytes = PAULA_WAVE_BYTES;
	legacy_u32 period = 65535U;
	if (frequency16 != 0) {
		/* The waveform contains two cycles, hence clock * 16 * 2. */
		period = (paula_clock * 32UL) / (frequency16 * bytes);
		while (period < PAULA_MIN_PERIOD && bytes > 16U) {
			bytes >>= 1U;
			period = (paula_clock * 32UL) / (frequency16 * bytes);
		}
	}
	if (period < PAULA_MIN_PERIOD) {
		period = PAULA_MIN_PERIOD;
	}
	if (period > 65535U) {
		period = 65535U;
	}
	if (bytes != state->wave_bytes) {
		state->wave_bytes = bytes;
		state->dirty = 1;
	}
	state->period = (legacy_u16)period;
	if (!paula_deferred) {
		paula_commit_voice(voice);
	}
}

static void paula_control(legacy_u16 voice, legacy_u16 selector, legacy_u16 value)
{
	struct PAULA_VOICE *state = &paula_voices[voice];
	legacy_u8 *destination = NULL;
	switch (selector) {
		case ADLIB_SELECTOR_MODULATOR_MULTIPLIER:
			destination = &state->modulator_multiplier;
			value = (value >> 3U) & 7U;
			break;
		case ADLIB_SELECTOR_CARRIER_MULTIPLIER:
			destination = &state->carrier_multiplier;
			value = (value >> 3U) & 7U;
			break;
		case ADLIB_SELECTOR_MODULATOR_GAIN:
			destination = &state->modulator_gain;
			value &= 127U;
			break;
		case ADLIB_SELECTOR_CARRIER_GAIN:
			state->carrier_gain = (legacy_u8)(value & 127U);
			break;
		case ADLIB_SELECTOR_FEEDBACK:
			destination = &state->feedback;
			value = (value >> 4U) & 7U;
			break;
		default:
			break;
	}
	if (destination && *destination != value) {
		*destination = (legacy_u8)value;
		state->dirty = 1;
	}
}

legacy_u8 dos_audio_driver_initialize(void)
{
	if (paula_ready) {
		return PAULA_CONTEXTS;
	}
	paula_port = CreateMsgPort();
	if (!paula_port) {
		return 0;
	}
	memset(&paula_request, 0, sizeof(paula_request));
	paula_request.ioa_Request.io_Message.mn_ReplyPort = paula_port;
	paula_request.ioa_Request.io_Message.mn_Length = sizeof(paula_request);
	paula_request.ioa_Request.io_Message.mn_Node.ln_Pri = 127;
	paula_request.ioa_Request.io_Command = ADCMD_ALLOCATE;
	paula_request.ioa_Request.io_Flags = ADIOF_NOWAIT;
	paula_request.ioa_Data = paula_channels;
	paula_request.ioa_Length = sizeof(paula_channels);
	if (OpenDevice((CONST_STRPTR)AUDIONAME, 0, (struct IORequest *)&paula_request, 0) != 0) {
		DeleteMsgPort(paula_port);
		paula_port = NULL;
		return 0;
	}
	paula_waves = AllocMem(PAULA_VOICES * PAULA_WAVE_BYTES, MEMF_CHIP | MEMF_CLEAR);
	if (!paula_waves) {
		CloseDevice((struct IORequest *)&paula_request);
		DeleteMsgPort(paula_port);
		paula_port = NULL;
		return 0;
	}
	paula_clock = SysBase->VBlankFrequency == PAULA_NTSC_REFRESH_HZ ? PAULA_NTSC_CLOCK_HZ
																	: PAULA_PAL_CLOCK_HZ;
	paula->dmacon = PAULA_DMA_MASK;
	paula->adkcon = 0x00ffU;
	paula->intena = PAULA_INTERRUPT_MASK;
	paula->intreq = PAULA_INTERRUPT_MASK;
	memset(paula_voices, 0, sizeof(paula_voices));
	paula_ready = 1;
	return PAULA_CONTEXTS;
}

void dos_audio_driver_prepare_context(legacy_s16 driver_channel, struct AUDIO_CONTEXT *context,
									  legacy_u8 *timer, void *resource)
{
	legacy_s16 voice = paula_voice_index(driver_channel);
	(void)context;
	if (voice < 0 || !resource || resource_read_u32le(resource) < ADLIB_RESOURCE_BYTES) {
		return;
	}
	struct PAULA_VOICE *state = &paula_voices[voice];
	const legacy_u8 *bytes = resource;
	state->resource = bytes;
	state->channel = (struct AUDIO_CHANNEL *)timer;
	state->modulator_multiplier = bytes[ADLIB_MODULATOR + ADLIB_OPERATOR_MULTIPLIER] & 15U;
	state->carrier_multiplier = bytes[ADLIB_CARRIER + ADLIB_OPERATOR_MULTIPLIER] & 15U;
	state->modulator_gain = (63U - (bytes[ADLIB_MODULATOR + ADLIB_OPERATOR_LEVEL] & 63U)) * 2U;
	state->carrier_gain = (63U - (bytes[ADLIB_CARRIER + ADLIB_OPERATOR_LEVEL] & 63U)) * 2U;
	state->feedback = bytes[ADLIB_FEEDBACK_OFFSET] & 7U;
	state->dirty = 1;
	if (state->channel) {
		paula_control((legacy_u16)voice, bytes[ADLIB_MODULATION_CONTROL_OFFSET],
					  state->channel->driver_private_state[0]);
		paula_control((legacy_u16)voice, bytes[ADLIB_EXPRESSION_CONTROL_OFFSET],
					  state->channel->driver_private_state[2]);
		paula_control((legacy_u16)voice, bytes[ADLIB_PARAMETER_CONTROL_OFFSET],
					  state->channel->driver_private_state[3]);
	}
}

void dos_audio_driver_set_context_value(legacy_s16 driver_channel, struct AUDIO_CONTEXT *context,
										legacy_u16 value)
{
	legacy_s16 voice = paula_voice_index(driver_channel);
	legacy_u32 numerator = (legacy_u32)value << 16U;
	legacy_u16 block = 0;
	(void)context;
	if (voice < 0) {
		return;
	}
	while (numerator > 511UL * 50000UL && block < 7U) {
		numerator >>= 1U;
		++block;
	}
	paula_voices[voice].base_pitch = (legacy_u16)(numerator / 50000UL) | (block << 10U);
}

void dos_audio_driver_activate_context(legacy_s16 driver_channel, struct AUDIO_CONTEXT *context,
									   legacy_u8 *timer, legacy_s16 pitch, legacy_u16 parameter,
									   void *resource)
{
	legacy_s16 voice = paula_voice_index(driver_channel);
	if (voice < 0 || !paula_ready) {
		return;
	}
	struct PAULA_VOICE *state = &paula_voices[voice];
	if (state->resource != resource) {
		dos_audio_driver_prepare_context(driver_channel, context, timer, resource);
	}
	if (!resource || state->resource != resource) {
		return;
	}
	paula_stop((legacy_u16)voice);
	state->channel = (struct AUDIO_CHANNEL *)timer;
	state->note = (legacy_u8)pitch;
	state->velocity =
		state->resource[ADLIB_VELOCITY_ENABLED_OFFSET] ? (legacy_u8)(parameter & 127U) : 127U;
	if (state->note != AUDIO_NOTE_USE_DRIVER_VALUE) {
		state->base_pitch = paula_note_pitch(state->note);
	}
	state->dirty = 1;
	paula_apply_pitch(
		(legacy_u16)voice,
		(legacy_u16)(state->base_pitch + (legacy_s8)state->resource[ADLIB_TRANSPOSE_OFFSET]));
	state->active = 1;
	if (!paula_deferred) {
		paula->dmacon = DMAF_SETCLR | DMAF_MASTER | (1U << voice);
	}
	paula_volume((legacy_u16)voice);
}

void dos_audio_driver_suspend_context(legacy_s16 driver_channel, struct AUDIO_CONTEXT *context,
									  legacy_u16 value, void *resource)
{
	legacy_s16 voice = paula_voice_index(driver_channel);
	(void)value;
	if (voice < 0 || !context || !context->state || !resource) {
		return;
	}
	struct PAULA_VOICE *state = &paula_voices[voice];
	const legacy_u8 *bytes = resource;
	if (state->resource != bytes) {
		return;
	}
	legacy_u16 pitch = state->base_pitch;
	if (bytes[AUDIO_RESOURCE_SEQUENCE_ENABLED_OFFSET] == ADLIB_SELECTOR_NOTE) {
		pitch = paula_note_pitch(state->note + context->sequence_value);
	}
	if (bytes[AUDIO_RESOURCE_MODULATION_ENABLED_OFFSET] == ADLIB_SELECTOR_PITCH) {
		pitch = (legacy_u16)(pitch + context->modulation);
	}
	if (bytes[ADLIB_LEVEL_CONTROL_OFFSET] == ADLIB_SELECTOR_PITCH) {
		pitch = (legacy_u16)(pitch + context->level);
	}
	legacy_s16 bend = state->channel ? LEGACY_S16_FROM_BITS(state->channel->pitch) : 0;
	if (bend != 0) {
		legacy_s16 semitones =
			bend > 0 ? bytes[ADLIB_BEND_RANGE_OFFSET] : -(legacy_s16)bytes[ADLIB_BEND_RANGE_OFFSET];
		legacy_s32 difference = (legacy_s32)paula_bend_target(state->note, semitones) - pitch;
		legacy_s32 delta = LEGACY_S32_SAR(difference * bend, 13U);
		pitch = (legacy_u16)(pitch + (bend > 0 ? delta : -delta));
	}
	paula_control((legacy_u16)voice, bytes[AUDIO_RESOURCE_SEQUENCE_ENABLED_OFFSET],
				  context->sequence_value);
	paula_control((legacy_u16)voice, bytes[AUDIO_RESOURCE_MODULATION_ENABLED_OFFSET],
				  (legacy_u16)context->modulation);
	paula_control((legacy_u16)voice, bytes[ADLIB_LEVEL_CONTROL_OFFSET], (legacy_u16)context->level);
	paula_apply_pitch((legacy_u16)voice,
					  (legacy_u16)(pitch + (legacy_s8)bytes[ADLIB_TRANSPOSE_OFFSET]));
}

void dos_audio_driver_start_context(legacy_s16 driver_channel, struct AUDIO_CONTEXT *context)
{
	legacy_s16 voice = paula_voice_index(driver_channel);
	(void)context;
	if (voice >= 0) {
		paula_stop((legacy_u16)voice);
	}
}

void dos_audio_driver_end_context(legacy_s16 driver_channel, struct AUDIO_CONTEXT *context)
{
	dos_audio_driver_start_context(driver_channel, context);
}

void dos_audio_driver_release_channel(legacy_s16 driver_channel)
{
	legacy_s16 voice = paula_voice_index(driver_channel);
	if (voice < 0) {
		return;
	}
	paula_stop((legacy_u16)voice);
	memset(&paula_voices[voice], 0, sizeof(paula_voices[voice]));
}

void dos_audio_driver_start(void)
{
	for (legacy_s16 channel = 1; (legacy_u16)channel <= PAULA_VOICES; ++channel) {
		dos_audio_driver_release_channel(channel);
	}
}

void dos_audio_set_channel_volume(legacy_s16 channel, legacy_s16 volume)
{
	if (channel < 0 || (legacy_u16)channel >= AUDIO_CHANNEL_COUNT) {
		return;
	}
	audio_channels[channel].volume = (legacy_u8)volume;
	for (legacy_u16 voice = 0; voice < PAULA_VOICES; ++voice) {
		if (paula_voices[voice].channel == &audio_channels[channel]) {
			paula_volume(voice);
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
	state->driver_channel =
		(legacy_u8)((channel & AUDIO_DIRECT_CHANNEL_MASK) + AUDIO_DRIVER_CHANNEL_BASE);
	for (legacy_s16 index = 1; (legacy_u16)index < PAULA_CONTEXTS; ++index) {
		if (dos_audio_contexts[index].channel == channel) {
			dos_audio_driver_prepare_context(index, &dos_audio_contexts[index], (legacy_u8 *)state,
											 resource);
		}
	}
}

void dos_audio_set_context_pitch(legacy_s16 context_index, legacy_s16 pitch)
{
	if (context_index <= 0 || (legacy_u16)context_index >= PAULA_CONTEXTS) {
		return;
	}
	struct AUDIO_CONTEXT *context = &dos_audio_contexts[context_index];
	dos_audio_driver_set_context_value(context->driver_channel, context, (legacy_u16)pitch);
}

void dos_audio_driver_set_control(legacy_s16 driver_channel, struct AUDIO_CONTEXT *context,
								  legacy_u16 control, legacy_u16 value)
{
	legacy_s16 voice = paula_voice_index(driver_channel);
	if (voice < 0 || !context || !context->state || !paula_voices[voice].resource) {
		return;
	}
	const legacy_u8 *resource = paula_voices[voice].resource;
	if (control == ADLIB_CONTROL_VOLUME) {
		dos_audio_set_channel_volume(context->channel, (legacy_s16)value);
	} else if (control == ADLIB_CONTROL_MODULATION) {
		paula_control((legacy_u16)voice, resource[ADLIB_MODULATION_CONTROL_OFFSET], value);
	} else if (control == ADLIB_CONTROL_EXPRESSION) {
		paula_control((legacy_u16)voice, resource[ADLIB_EXPRESSION_CONTROL_OFFSET], value);
	} else if (control == ADLIB_CONTROL_PARAMETER) {
		paula_control((legacy_u16)voice, resource[ADLIB_PARAMETER_CONTROL_OFFSET], value);
	}
}

/* These are MT-32/driver transport operations. The selected AD instruments
 * do not use them; pitch/envelope changes arrive in suspend_context above. */
void dos_audio_driver_load_bank(void *bank)
{
	(void)bank;
}
void dos_audio_driver_send_data(legacy_u16 length, legacy_u8 *data)
{
	(void)length;
	(void)data;
}
void dos_audio_driver_set_master_state(legacy_s16 operation, void *state)
{
	(void)operation;
	(void)state;
}
void dos_audio_driver_set_pitch(legacy_u8 *timer, legacy_s16 pitch, legacy_s16 driver_channel)
{
	(void)timer;
	(void)pitch;
	(void)driver_channel;
}
void dos_audio_driver_suspend_all(struct AUDIO_CONTEXT *contexts)
{
	(void)contexts;
}
void dos_audio_driver_reset(void)
{
	amiga_timer_pump();
}

void dos_audio_shutdown(void)
{
	audio_update_lock = AUDIO_UPDATE_LOCKED;
	timer_remove_callback(audio_sequence_timer);
	dos_audio_driver_start();
	if (paula_ready) {
		paula_dma_settle();
		paula->intreq = PAULA_INTERRUPT_MASK;
		CloseDevice((struct IORequest *)&paula_request);
		FreeMem(paula_waves, PAULA_VOICES * PAULA_WAVE_BYTES);
		DeleteMsgPort(paula_port);
	}
	paula_ready = 0;
	paula_waves = NULL;
	paula_port = NULL;
	dos_audio_driver_binary = NULL;
	dos_audio_uses_direct_channels = 0;
	dos_audio_special_mode = 0;
	dos_audio_context_count = 0;
	audio_music_enabled = AUDIO_STATE_DISABLED;
	audio_effects_enabled = AUDIO_STATE_DISABLED;
	audio_update_lock = AUDIO_UPDATE_UNLOCKED;
}

#ifdef RESTUNTS_AMIGA_SMOKE
static legacy_s16 paula_selftest_check(legacy_s16 condition, const legacy_char *message)
{
	if (!condition) {
		dos_write_stderr((const legacy_s8 *)message, (legacy_u16)strlen(message));
	}
	return condition;
}

/* Called only by --amiga-selftest. The readable DMA/interrupt status proves
 * Paula actually fetched the Chip RAM loop, rather than merely checking our
 * cached register values. A second allocation exercises the failure path. */
legacy_s16 amiga_audio_selftest(void)
{
	legacy_u8 instrument[ADLIB_RESOURCE_BYTES];
	struct IOAudio blocker;
	struct MsgPort *port;
	legacy_s16 ok = 1;
	legacy_u32 started;
	if (!paula_selftest_check(dos_audio_driver_initialize() == PAULA_CONTEXTS,
							  "Amiga audio selftest: initial channel allocation failed\n")) {
		return 0;
	}
	memset(instrument, 0, sizeof(instrument));
	LEGACY_WRITE_U32_LE(instrument, ADLIB_RESOURCE_BYTES);
	instrument[ADLIB_MODULATOR + ADLIB_OPERATOR_LEVEL] = 63;
	instrument[ADLIB_CARRIER + ADLIB_OPERATOR_MULTIPLIER] = 1;
	audio_channels[0].volume = 127;
	dos_audio_contexts[1].state = AUDIO_CONTEXT_STATE_PLAYING;
	amiga_audio_begin_update();
	dos_audio_driver_activate_context(1, &dos_audio_contexts[1], (legacy_u8 *)&audio_channels[0],
									  60, 127, instrument);
	dos_audio_driver_activate_context(1, &dos_audio_contexts[1], (legacy_u8 *)&audio_channels[0],
									  64, 127, instrument);
	ok &= paula_selftest_check(
		(paula->dmaconr & 1U) == 0 && paula_voices[0].dirty,
		"Amiga audio selftest: historical waveform was committed inside timer batch\n");
	amiga_audio_end_update();
	ok &= paula_selftest_check(
		paula_voices[0].note == 64 && !paula_voices[0].dirty,
		"Amiga audio selftest: final timer batch waveform was not committed\n");
	ok &= paula_selftest_check((paula->dmaconr & 1U) != 0,
							   "Amiga audio selftest: channel DMA did not start\n");
	/* A stale interrupt at initialization would otherwise mask a broken loop. */
	paula->intreq = 0x0080U;
	started = amiga_clock_ticks();
	while (!(paula->intreqr & 0x0080U) && amiga_clock_ticks() - started < 10U) {
	}
	ok &= paula_selftest_check((paula->intreqr & 0x0080U) != 0,
							   "Amiga audio selftest: Paula did not finish a sample loop\n");
	dos_audio_driver_end_context(1, &dos_audio_contexts[1]);
	paula_dma_settle();
	ok &= paula_selftest_check((paula->dmaconr & 1U) == 0,
							   "Amiga audio selftest: channel DMA did not stop\n");
	dos_audio_shutdown();
	port = CreateMsgPort();
	if (!paula_selftest_check(port != NULL,
							  "Amiga audio selftest: allocation test port unavailable\n")) {
		return 0;
	}
	memset(&blocker, 0, sizeof(blocker));
	blocker.ioa_Request.io_Message.mn_ReplyPort = port;
	blocker.ioa_Request.io_Message.mn_Length = sizeof(blocker);
	blocker.ioa_Request.io_Message.mn_Node.ln_Pri = 127;
	blocker.ioa_Request.io_Command = ADCMD_ALLOCATE;
	blocker.ioa_Request.io_Flags = ADIOF_NOWAIT;
	blocker.ioa_Data = paula_channels;
	blocker.ioa_Length = sizeof(paula_channels);
	if (OpenDevice((CONST_STRPTR)AUDIONAME, 0, (struct IORequest *)&blocker, 0) != 0) {
		DeleteMsgPort(port);
		paula_selftest_check(0, "Amiga audio selftest: channel release failed\n");
		return 0;
	}
	ok &= paula_selftest_check(dos_audio_driver_initialize() == 0,
							   "Amiga audio selftest: allocated channels were not protected\n");
	/* A non-conforming device allowing equal-priority theft is cleaned up too. */
	if (paula_ready) {
		dos_audio_shutdown();
	}
	CloseDevice((struct IORequest *)&blocker);
	DeleteMsgPort(port);
	ok &= paula_selftest_check(
		dos_audio_driver_initialize() == PAULA_CONTEXTS,
		"Amiga audio selftest: reinitialization after allocation failure failed\n");
	dos_audio_shutdown();
	dos_audio_contexts[1].state = AUDIO_CONTEXT_STATE_FREE;
	return ok;
}

#endif /* RESTUNTS_AMIGA_SMOKE */
