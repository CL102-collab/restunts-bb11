#ifndef RESTUNTS_UNIT_LEARNING_CACHE_H
#define RESTUNTS_UNIT_LEARNING_CACHE_H

#include "unit_planner.h"
#include "../extended/csrpl.h"

#define UNIT_LEARNING_ALGORITHM_VERSION 1U
#define UNIT_LEARNING_MAXIMUM_PIECES 32768U
#define UNIT_LEARNING_MAXIMUM_COMMANDS 65536U

struct UNIT_LEARNING_KEY {
	legacy_u64 first, second;
};

struct UNIT_LEARNING_PIECE {
	legacy_f64 speed, exit_fraction, exit_speed, barrel_offset;
	legacy_u8 ramp_approach;
};

/* Preferences and portable commands must undergo fresh native validation.
 * This data never contains native snapshots, pointers into game resources or
 * traffic-dependent permissions. The caller owns it independently of a pilot. */
struct UNIT_LEARNING_DATA {
	struct UNIT_PLANNER_LEARNING search;
	struct UNIT_PLANNER_PROFILE best_profile;
	struct UNIT_LEARNING_PIECE *pieces;
	struct CSRPL_OPPONENT_COMMAND *commands;
	legacy_u64 native_samples;
	legacy_u32 piece_count, best_frames, command_count;
};

/* Hash canonical content, never pointer values or compiler padding. Include the
 * driving geometry, car physics, route, transmission, rules and simulation version. */
void unit_learning_key_begin(struct UNIT_LEARNING_KEY *key);
void unit_learning_key_bytes(struct UNIT_LEARNING_KEY *key, const void *bytes, legacy_u32 size);
void unit_learning_key_u32(struct UNIT_LEARNING_KEY *key, legacy_u32 value);
void unit_learning_key_f64(struct UNIT_LEARNING_KEY *key, legacy_f64 value);

/* Nonempty existing directories opt headless tools into persistence. Empty
 * disables it; NULL restores the gameplay default (the game save directory).
 * Invalid overrides disable writes. This does not create a directory. */
legacy_int unit_learning_cache_set_directory(const legacy_s8 *directory);
legacy_int unit_learning_cache_enabled(void);

/* Missing, stale, corrupt or unwritable caches return zero without a fatal
 * error. Decode/load leave output untouched on failure. Success transfers new
 * allocations; release an existing value before replacing it. */
legacy_int unit_learning_cache_load(const struct UNIT_LEARNING_KEY *key, legacy_u32 count,
									struct UNIT_LEARNING_DATA *data);
legacy_int unit_learning_cache_save(const struct UNIT_LEARNING_KEY *key,
									const struct UNIT_LEARNING_DATA *data);
void unit_learning_cache_release(struct UNIT_LEARNING_DATA *data);

/* Portable bounded little-endian codec, also used by corruption regressions. */
legacy_int unit_learning_cache_encode(const struct UNIT_LEARNING_KEY *key,
									  const struct UNIT_LEARNING_DATA *data, legacy_u8 **bytes,
									  legacy_u32 *size);
legacy_int unit_learning_cache_decode(const struct UNIT_LEARNING_KEY *key, legacy_u32 count,
									  const legacy_u8 *bytes, legacy_u32 size,
									  struct UNIT_LEARNING_DATA *data);

#endif
