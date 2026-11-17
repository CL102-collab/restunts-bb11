#ifndef RESTUNTS_UNIT_OVERTAKE_H
#define RESTUNTS_UNIT_OVERTAKE_H

#include "unit_planner.h"

enum UNIT_OVERTAKE_PHASE { UNIT_OVERTAKE_IDLE, UNIT_OVERTAKE_COMMITTED, UNIT_OVERTAKE_MERGING };

/* Main-thread traffic intent, absent from learned profiles and recordings.
 * Positive offsets follow the road's left normal (-dz, +dx). */
struct UNIT_OVERTAKE {
	legacy_f64 offset, previous_x, previous_z;
	legacy_u32 generation;
	legacy_s8 side;
	legacy_u8 phase, have_generation, started;
};

void unit_overtake_cancel(struct UNIT_OVERTAKE *intent);

struct UNIT_PROFILE;
struct UNIT_REQUEST;
void unit_overtake_request(struct UNIT_OVERTAKE *intent, const struct UNIT_ROUTE_POINT *points,
						   legacy_u32 count, const struct UNIT_CAR_LIMITS *limits, legacy_u8 owoot,
						   const struct UNIT_PROFILE *profile, struct UNIT_REQUEST *request);

#endif
