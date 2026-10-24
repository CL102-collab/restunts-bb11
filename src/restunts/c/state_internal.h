#ifndef RESTUNTS_STATE_INTERNAL_H
#define RESTUNTS_STATE_INTERNAL_H

#include "legacy.h"

struct CARSTATE;
struct SIMD;

enum GRIP_BEHAVIOR { GRIP_BEHAVIOR_OPPONENT = 0, GRIP_BEHAVIOR_PLAYER = 1 };

/* Preserve the existing game and physics-dump caller contract. */
#define LEGACY_DEFAULT_PLAYER_TICK_SI 80

void update_player_tick_with_legacy_si(legacy_s8 input_flags, legacy_s16 caller_si);

extern legacy_s16 grassDecelDivTab[];

void update_grip(struct CARSTATE *carstate, struct SIMD *simd, legacy_s16 grip_behavior);
void update_legacy_grip_stack_words(struct CARSTATE *carstate, struct SIMD *simd,
									legacy_u16 speed_before_grip, legacy_u16 speed2_before_grip,
									legacy_s16 caller_si);
void update_player_state(struct CARSTATE *playerstate, struct SIMD *playersimd,
						 struct CARSTATE *opponentstate, struct SIMD *opponentsimd,
						 legacy_s16 car_index);

void update_player_steering_input(legacy_s8 steering_input);

#endif
