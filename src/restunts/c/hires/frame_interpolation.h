#ifndef RESTUNTS_FRAME_INTERPOLATION_H
#define RESTUNTS_FRAME_INTERPOLATION_H

#include "../gamestate.h"

#define FRAME_INTERPOLATION_ONE 65536UL

/* Blend consecutive authoritative snapshots for presentation. A Q16 fraction
 * of zero selects the previous pose; ONE selects the current pose. Fractions
 * above ONE clamp to ONE. Discrete state and discontinuities use current.
 * No input, timers, physics, or global state are accessed. Callers reset
 * history after timeline discontinuities and supply a separate result. */
void frame_hires_interpolate_vector(struct VECTOR *result, const struct VECTOR *current,
									const struct VECTOR *previous, legacy_u32 fraction);
void frame_hires_interpolate_car(struct CARSTATE *result, const struct CARSTATE *current,
								 const struct CARSTATE *previous, legacy_u32 fraction);
void frame_hires_interpolate_state(struct GAMESTATE *result, const struct GAMESTATE *current,
								   const struct GAMESTATE *previous, legacy_u32 fraction);

#endif
