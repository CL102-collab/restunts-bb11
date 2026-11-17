// SPDX-License-Identifier: BSD-3-Clause
// copyright-holders:Nicola Salmoria
// Contributions by others; see the upstream history listed in README.md.
// Adapted from MAME sn76496.cpp/.h; see README.md and LICENSE.
#include <string.h>
#include "mame_psg.h"

#define PSG_DATA_LATCH 0x80U
#define PSG_REGISTER_MASK 0x70U
#define PSG_REGISTER_SHIFT 4U
#define PSG_DATA_LOW_MASK 0x0fU
#define PSG_DATA_HIGH_MASK 0x3fU
#define PSG_REGISTER_HIGH_MASK 0x3f0U
#define PSG_NOISE_REGISTER 6U
#define PSG_LAST_TONE_REGISTER 4U
#define PSG_NOISE_CHANNEL 3U
#define PSG_LAST_TONE_CHANNEL 2U
#define PSG_NOISE_WHITE_MODE 0x04U
#define PSG_NOISE_RATE_MASK 0x03U
#define PSG_NOISE_TONE_RATE 0x03U
#define PSG_NOISE_BASE_PERIOD 32U
#define PSG_ZERO_TONE_PERIOD 0x400U
#define PSG_NCR_FEEDBACK_MASK 0x8000U
#define PSG_NCR_NOISE_TAP_FIRST 0x02U
#define PSG_NCR_NOISE_TAP_SECOND 0x20U
#define PSG_INPUT_CLOCK_DIVIDER 16U
#define PSG_MAME_STREAM_CLOCK_DIVIDER 2U

/* MAME gain=0: truncate (32767 / 4) / 1.258925412^attenuation; last level is mute. */
static const legacy_s16 volume_levels[MAME_PSG_VOLUME_LEVELS] = {
	8191, 6506, 5168, 4105, 3260, 2590, 2057, 1634, 1298, 1031, 819, 650, 516, 410, 326, 0};

legacy_int mame_psg_initialize(struct MAME_PSG *chip, enum MAME_PSG_PROFILE profile,
							   legacy_u32 clock_hz, legacy_u32 sample_rate)
{
	if (chip == NULL) {
		return 0;
	}
	memset(chip, 0, sizeof(*chip));
	if ((profile != MAME_PSG_NCR8496 && profile != MAME_PSG_PSSJ3) || clock_hz == 0 ||
		sample_rate == 0) {
		return 0;
	}
	chip->profile = profile;
	chip->clock_hz = clock_hz;
	chip->sample_rate = sample_rate;
	mame_psg_reset(chip);
	return 1;
}

void mame_psg_reset(struct MAME_PSG *chip)
{
	memset(chip->registers, 0, sizeof(chip->registers));
	for (legacy_u32 channel = 0; channel < MAME_PSG_CHANNELS; channel++) {
		chip->period[channel] = channel == PSG_NOISE_CHANNEL ? 0 : PSG_ZERO_TONE_PERIOD;
		chip->counter[channel] = 0;
		chip->volume[channel] = volume_levels[0];
		chip->output[channel] = 0;
	}
	chip->last_register = 0;
	chip->lfsr = PSG_NCR_FEEDBACK_MASK;
	/* MAME's clock/2 stream starts its /8 divider at 7: first transition is at 14. */
	chip->tick_remaining =
		(legacy_u64)(PSG_INPUT_CLOCK_DIVIDER - PSG_MAME_STREAM_CLOCK_DIVIDER) * chip->sample_rate;
}

void mame_psg_write(struct MAME_PSG *chip, legacy_u8 data)
{
	legacy_u8 reg = chip->last_register;
	if (data & PSG_DATA_LATCH) {
		reg = (data & PSG_REGISTER_MASK) >> PSG_REGISTER_SHIFT;
		chip->last_register = reg;
		if (reg == PSG_NOISE_REGISTER &&
			((data ^ chip->registers[PSG_NOISE_REGISTER]) & PSG_NOISE_WHITE_MODE)) {
			/* NCR/PSSJ reset the shift register only when a latched mode bit changes. */
			chip->lfsr = PSG_NCR_FEEDBACK_MASK;
		}
		chip->registers[reg] =
			(chip->registers[reg] & PSG_REGISTER_HIGH_MASK) | (data & PSG_DATA_LOW_MASK);
	}

	legacy_u8 channel = reg >> 1;
	if (reg & 1U) {
		chip->volume[channel] = volume_levels[data & PSG_DATA_LOW_MASK];
		if (!(data & PSG_DATA_LATCH)) {
			chip->registers[reg] =
				(chip->registers[reg] & PSG_REGISTER_HIGH_MASK) | (data & PSG_DATA_LOW_MASK);
		}
	} else if (reg != PSG_NOISE_REGISTER) {
		if (!(data & PSG_DATA_LATCH)) {
			chip->registers[reg] = (chip->registers[reg] & PSG_DATA_LOW_MASK) |
								   ((data & PSG_DATA_HIGH_MASK) << PSG_REGISTER_SHIFT);
		}
		chip->period[channel] = chip->registers[reg] ? chip->registers[reg] : PSG_ZERO_TONE_PERIOD;
		if (reg == PSG_LAST_TONE_REGISTER &&
			(chip->registers[PSG_NOISE_REGISTER] & PSG_NOISE_RATE_MASK) == PSG_NOISE_TONE_RATE) {
			chip->period[PSG_NOISE_CHANNEL] = chip->period[PSG_LAST_TONE_CHANNEL] << 1;
		}
	} else {
		if (!(data & PSG_DATA_LATCH)) {
			chip->registers[reg] =
				(chip->registers[reg] & PSG_REGISTER_HIGH_MASK) | (data & PSG_DATA_LOW_MASK);
		}
		legacy_u8 rate = chip->registers[reg] & PSG_NOISE_RATE_MASK;
		chip->period[PSG_NOISE_CHANNEL] = rate == PSG_NOISE_TONE_RATE
											  ? (legacy_u32)chip->period[PSG_LAST_TONE_CHANNEL] << 1
											  : PSG_NOISE_BASE_PERIOD << rate;
	}
}

static void clock_divided_chip(struct MAME_PSG *chip)
{
	for (legacy_u32 channel = 0; channel < MAME_PSG_TONE_CHANNELS; channel++) {
		chip->counter[channel]--;
		if (chip->counter[channel] <= 0) {
			chip->output[channel] ^= 1U;
			chip->counter[channel] = chip->period[channel];
		}
	}
	chip->counter[PSG_NOISE_CHANNEL]--;
	if (chip->counter[PSG_NOISE_CHANNEL] <= 0) {
		legacy_u8 first = (chip->lfsr & PSG_NCR_NOISE_TAP_FIRST) != 0;
		legacy_u8 second = (chip->lfsr & PSG_NCR_NOISE_TAP_SECOND) != PSG_NCR_NOISE_TAP_SECOND;
		legacy_u8 white = (chip->registers[PSG_NOISE_REGISTER] & PSG_NOISE_WHITE_MODE) != 0;
		legacy_u8 feedback = first != (second && white);
		chip->lfsr >>= 1;
		if (feedback) {
			chip->lfsr |= PSG_NCR_FEEDBACK_MASK;
		}
		chip->output[PSG_NOISE_CHANNEL] = chip->lfsr & 1U;
		chip->counter[PSG_NOISE_CHANNEL] = chip->period[PSG_NOISE_CHANNEL];
	}
}

static legacy_s32 mixed_output(const struct MAME_PSG *chip)
{
	legacy_s32 mixed = 0;
	for (legacy_u32 channel = 0; channel < MAME_PSG_CHANNELS; channel++) {
		/* Bipolar adaptation removes each oscillator's half-volume DC component. */
		mixed += chip->output[channel] ? chip->volume[channel] : -chip->volume[channel];
	}
	return chip->profile == MAME_PSG_NCR8496 ? -mixed : mixed;
}

void mame_psg_render(struct MAME_PSG *chip, legacy_s16 *buffer, size_t frames)
{
	if (chip->clock_hz == 0 || chip->sample_rate == 0) {
		memset(buffer, 0, frames * sizeof(*buffer));
		return;
	}
	legacy_u64 tick_duration = (legacy_u64)PSG_INPUT_CLOCK_DIVIDER * chip->sample_rate;
	for (size_t frame = 0; frame < frames; frame++) {
		legacy_u64 remaining = chip->clock_hz;
		legacy_s64 area = 0;
		while (remaining != 0) {
			if (chip->tick_remaining == 0) {
				clock_divided_chip(chip);
				chip->tick_remaining = tick_duration;
			}
			legacy_u64 span = remaining < chip->tick_remaining ? remaining : chip->tick_remaining;
			area += (legacy_s64)mixed_output(chip) * (legacy_s64)span;
			chip->tick_remaining -= span;
			remaining -= span;
		}
		buffer[frame] = (legacy_s16)(area / chip->clock_hz);
	}
}
