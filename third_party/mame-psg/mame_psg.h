// SPDX-License-Identifier: BSD-3-Clause
// copyright-holders:Nicola Salmoria
// Contributions by others; see the upstream history listed in README.md.
// Adapted from MAME sn76496.cpp/.h; see README.md and LICENSE.
#ifndef RESTUNTS_MAME_PSG_H
#define RESTUNTS_MAME_PSG_H

#include <stddef.h>
#include "../../src/restunts/c/legacy.h"

#define MAME_PSG_TONE_CHANNELS 3U
#define MAME_PSG_CHANNELS 4U
#define MAME_PSG_REGISTERS 8U
#define MAME_PSG_VOLUME_LEVELS 16U

enum MAME_PSG_PROFILE { MAME_PSG_NCR8496 = 0, MAME_PSG_PSSJ3 };

/* Public storage permits allocation without a heap. Fields are private to the core. */
struct MAME_PSG {
	enum MAME_PSG_PROFILE profile;
	legacy_u32 clock_hz;
	legacy_u32 sample_rate;
	legacy_u16 registers[MAME_PSG_REGISTERS];
	legacy_u16 period[MAME_PSG_CHANNELS];
	legacy_s32 counter[MAME_PSG_CHANNELS];
	legacy_s16 volume[MAME_PSG_CHANNELS];
	legacy_u8 output[MAME_PSG_CHANNELS];
	legacy_u8 last_register;
	legacy_u32 lfsr;
	/* Rational time units are input-clock periods multiplied by sample_rate. */
	legacy_u64 tick_remaining;
};

/* Nonzero means success. Both rates must be nonzero; no SDL or allocation is needed. */
legacy_int mame_psg_initialize(struct MAME_PSG *chip, enum MAME_PSG_PROFILE profile,
							   legacy_u32 clock_hz, legacy_u32 sample_rate);
/* Like MAME power-up: maximum volumes, zero tone registers, and initialized LFSR. */
void mame_psg_reset(struct MAME_PSG *chip);
/* Applies an ordinary SN764xx data-port byte at the current rendering boundary. */
void mame_psg_write(struct MAME_PSG *chip, legacy_u8 data);
/* Replaces frames mono signed-16 samples. Partial clock intervals survive each call. */
void mame_psg_render(struct MAME_PSG *chip, legacy_s16 *buffer, size_t frames);

#endif
