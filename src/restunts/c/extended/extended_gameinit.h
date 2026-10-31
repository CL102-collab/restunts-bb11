#ifndef RESTUNTS_EXTENDED_GAMEINIT_H
#define RESTUNTS_EXTENDED_GAMEINIT_H
#include "../legacy.h"
struct CARSTATE;
struct SIMD;
legacy_s16 extended_gameinit_car_start(struct CARSTATE *carstate, struct SIMD *simd,
									   legacy_s8 transmission, legacy_s16 column_offset,
									   legacy_s16 row_offset);
void extended_gameinit_follow_camera(legacy_s16 initial_height);
void extended_gameinit_reset(legacy_s16 initialization_mode);
legacy_s16 extended_gameinit_restore(legacy_u16 frame);
#endif
