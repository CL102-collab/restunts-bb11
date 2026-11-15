#ifndef RESTUNTS_EXTENDED_BARREL_SEARCH_H
#define RESTUNTS_EXTENDED_BARREL_SEARCH_H

#include "../legacy.h"

#define EXTENDED_BARREL_POPULATION 32U
#define EXTENDED_BARREL_CHECKPOINTS 4U
#define EXTENDED_BARREL_PATTERN_UNITS 64U

enum EXTENDED_BARREL_FIELD {
	EXTENDED_BARREL_SPEED,
	EXTENDED_BARREL_LATERAL,
	EXTENDED_BARREL_SHIFT,
	EXTENDED_BARREL_BEGIN,
	EXTENDED_BARREL_DURATION,
	EXTENDED_BARREL_GAP,
	EXTENDED_BARREL_COUNTER,
	EXTENDED_BARREL_PEDAL_BEGIN,
	EXTENDED_BARREL_PEDAL_DURATION,
	EXTENDED_BARREL_CHECKPOINT,
	EXTENDED_BARREL_AIR_PEDAL,
	EXTENDED_BARREL_SIDE_PEDAL,
	EXTENDED_BARREL_FIELD_COUNT
};

#define EXTENDED_BARREL_FIELDS_ALL ((1U << EXTENDED_BARREL_FIELD_COUNT) - 1U)
#define EXTENDED_BARREL_FIELDS_PULSES                                                              \
	((1U << EXTENDED_BARREL_BEGIN) | (1U << EXTENDED_BARREL_DURATION) |                            \
	 (1U << EXTENDED_BARREL_GAP) | (1U << EXTENDED_BARREL_COUNTER) |                               \
	 (1U << EXTENDED_BARREL_PEDAL_BEGIN) | (1U << EXTENDED_BARREL_PEDAL_DURATION) |                \
	 (1U << EXTENDED_BARREL_SIDE_PEDAL))

struct EXTENDED_BARREL_PARAMETERS {
	legacy_f64 speed, lateral, shift;
	legacy_uint checkpoint, begin, duration, gap, counter, pedal_begin, pedal_duration;
	legacy_u8 side, pedal, air_brake;
};

/* Pure search state: no engine pointers, allocation, native stepping, or threads. */
struct EXTENDED_BARREL_SEARCH {
	struct EXTENDED_BARREL_PARAMETERS population[EXTENDED_BARREL_POPULATION];
	legacy_f64 fitness[EXTENDED_BARREL_POPULATION];
	legacy_uint count;
	legacy_u64 attempts;
	legacy_u32 random;
};

/* Ordinary input groups to replace; mask zero preserves the caller's guidance. */
struct EXTENDED_BARREL_INPUT {
	legacy_u8 mask, value;
};

void extended_barrel_search_init(struct EXTENDED_BARREL_SEARCH *search, legacy_u32 seed);
/* A new maneuver clears the population but preserves its random stream. */
void extended_barrel_search_clear(struct EXTENDED_BARREL_SEARCH *search);
/* A changed entry state retains ideas, never their old native fitness. */
void extended_barrel_search_invalidate(struct EXTENDED_BARREL_SEARCH *search);
/* Span and bank are measured native ticks from ramp entry. Disabled fields
 * retain baseline values. All fields with a NULL baseline preserve the ghost
 * generator's original candidate sequence and random-number consumption. */
struct EXTENDED_BARREL_PARAMETERS
extended_barrel_search_next(struct EXTENDED_BARREL_SEARCH *search, legacy_uint span,
							legacy_uint bank, legacy_u16 enabled_fields,
							const struct EXTENDED_BARREL_PARAMETERS *baseline);
void extended_barrel_search_observe(struct EXTENDED_BARREL_SEARCH *search,
									const struct EXTENDED_BARREL_PARAMETERS *parameters,
									legacy_f64 fitness);
struct EXTENDED_BARREL_INPUT
extended_barrel_search_input(const struct EXTENDED_BARREL_PARAMETERS *parameters, legacy_uint span,
							 legacy_uint tick);

#endif
