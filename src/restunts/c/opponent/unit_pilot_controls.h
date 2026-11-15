#ifndef RESTUNTS_UNIT_PILOT_CONTROLS_H
#define RESTUNTS_UNIT_PILOT_CONTROLS_H
#include "../legacy.h"
struct CARSTATE;
struct SIMD;
struct CSRPL_OPPONENT_COMMAND;
legacy_u8 unit_controls_manual_shift(const struct CARSTATE *car, const struct SIMD *simd);
/* One preview tick only. Mask selects complete ordinary steering/pedal groups. */
void unit_opponent_pilot_input_override(legacy_u8 mask, legacy_u8 value);
void unit_pilot_controls_apply(const struct CARSTATE *car, legacy_u8 steering_rate,
							   legacy_int collision, struct CSRPL_OPPONENT_COMMAND *command);
#endif
