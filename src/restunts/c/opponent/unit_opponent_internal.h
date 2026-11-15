#ifndef RESTUNTS_UNIT_OPPONENT_INTERNAL_H
#define RESTUNTS_UNIT_OPPONENT_INTERNAL_H

#include "unit_planner.h"
#include "../gamestate.h"
#include "../extended/extended_track.h"

#define UNIT_POSITION_SCALE 64.0
#define UNIT_SPEED_SCALE 256.0
#define UNIT_ANGLE_RADIANS (6.28318530717958647692 / 1024.0)
#define UNIT_WORLD_PER_MPH (22.0 / 3.0)
#define UNIT_STEERING_MAXIMUM 240.0
#define UNIT_FRONT_STEERING_SCALE 4.0
#define UNIT_ROUTE_SUBTYPE_MASK 15U
#define UNIT_ROUTE_INITIAL_CAPACITY 128U
#define UNIT_ROUTE_MAXIMUM_POINTS (EXTENDED_TRACK_ROUTE_CAPACITY * 128U)

struct UNIT_EXIT_PROFILE {
	legacy_f64 fraction, speed;
};

struct UNIT_ROUTE {
	struct UNIT_ROUTE_POINT *points;
	legacy_u32 count;
	legacy_u32 capacity;
	legacy_u32 finish_begin;
	legacy_f64 *verified_speeds, *verified_barrel_offsets;
	struct UNIT_EXIT_PROFILE *verified_exits;
	legacy_u8 *verified_ramp_approaches;
	legacy_u32 verified_count;
	legacy_f64 wheelbase;
	struct UNIT_CAR_LIMITS limits;
};

legacy_int unit_route_build(struct UNIT_ROUTE *route);
legacy_int unit_route_build_opening(struct UNIT_ROUTE *route);
void unit_route_free(struct UNIT_ROUTE *route);
legacy_u32 unit_route_cursor(const struct UNIT_ROUTE *route, const struct CARSTATE *car);
legacy_u32 unit_route_advance(const struct UNIT_ROUTE *route, const struct CARSTATE *car,
							  legacy_u32 cursor);
void unit_route_store(const struct UNIT_ROUTE *route, legacy_u32 cursor, struct CARSTATE *car);
legacy_int unit_route_finished(const struct UNIT_ROUTE *route, const struct CARSTATE *car);
legacy_f64 unit_bounded(legacy_f64 value, legacy_f64 minimum, legacy_f64 maximum);

#endif
