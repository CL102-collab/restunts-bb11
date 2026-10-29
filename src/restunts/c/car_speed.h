#ifndef RESTUNTS_CAR_SPEED_H
#define RESTUNTS_CAR_SPEED_H

#include "legacy.h"

struct CARSTATE;
struct SIMD;
extern legacy_u8 oppnentSped[];

void configure_powergear_bug(legacy_s16 argc, legacy_s8 *argv[]);
legacy_u16 powergear_bug_is_enabled(void);

void update_car_speed(legacy_s8 input, legacy_s16 car_index, struct CARSTATE *carstate,
					  struct SIMD *simd);

#endif
