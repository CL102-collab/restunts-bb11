#ifndef RESTUNTS_UNIT_PLANNER_INTERNAL_H
#define RESTUNTS_UNIT_PLANNER_INTERNAL_H

#include "unit_planner.h"

#define UNIT_PLANNER_ROUTE_LIMIT 262144U
#define UNIT_PLANNER_CHUNK_POINTS 64U
#define UNIT_PLANNER_LOCAL_POINTS 96U
#define UNIT_PLANNER_LIVE_CANDIDATES 22U
#define UNIT_PLANNER_CRAWL_CANDIDATE (UNIT_PLANNER_LIVE_CANDIDATES - 1U)
#define UNIT_PLANNER_SYNC_CANDIDATES 3U
#define UNIT_PLANNER_MAX_WORKERS 256U
#define UNIT_PLANNER_WORKER_MEMORY (64U * 1024U * 1024U)
#define UNIT_PLANNER_WORLD_PER_MPH (22.0 / 3.0)
#define UNIT_PLANNER_MIN_DISTANCE 0.01
#define UNIT_PLANNER_MIN_SPEED 1.0
#define UNIT_PLANNER_MAX_LOOKAHEAD 850.0
#define UNIT_PLANNER_ROLLOUT_SECONDS 2.4
#define UNIT_PLANNER_COLLISION_MARGIN 24.0
#define UNIT_PLANNER_OVERTAKE_COLLISION_MARGIN 8.0
#define UNIT_PLANNER_COLLISION_HEIGHT 90.0
#define UNIT_PLANNER_WORLD_GRAVITY (18.0 * 20.0 * 20.0 / 64.0)
#define UNIT_PLANNER_LOCAL_SAMPLE_SCAN_LIMIT (UNIT_PLANNER_LOCAL_POINTS * 64U)
#define UNIT_NO_PLAYER_POSITION 1000000000.0
#define UNIT_PLANNER_PASS_SPEED_ADVANTAGE 12.0
#define UNIT_PLANNER_PASS_MINIMUM_SPEED 18.0
#define UNIT_PLANNER_PASS_EMERGENCY_SPEED 40.0
#define UNIT_PLANNER_PASS_EMERGENCY_SECONDS 0.25
#define UNIT_PLANNER_PASS_LOOKAHEAD_SCALE 0.6

struct UNIT_PROFILE {
	legacy_f64 lane, apex, lookahead;
	legacy_u8 clearance_passing;
	/* Live traffic intent is never exported, learned, or inherited. */
	legacy_f64 overtake_offset;
};

struct UNIT_EVALUATION {
	struct UNIT_PROFILE profile;
	legacy_f64 *speeds, score;
	legacy_u32 count, cursor, phase;
};

struct UNIT_REQUEST {
	struct UNIT_VEHICLE unit, player;
	legacy_u32 cursor, generation;
	legacy_u8 native_preview, overtaking;
	legacy_f64 overtake_offset;
};

struct UNIT_ROLLOUT {
	struct UNIT_PLAN plan;
	legacy_f64 score;
	legacy_u32 candidate;
};

struct UNIT_LOCAL_POINT {
	legacy_f64 x, y, z, width, speed, minimum_speed, distance;
	legacy_f64 road_x, road_z, ordinary_width;
	legacy_u32 source;
	legacy_u8 terminal;
	legacy_u8 height_unspecified;
};

legacy_f64 unit_planner_clamp(legacy_f64 value, legacy_f64 minimum, legacy_f64 maximum);
legacy_u8 unit_planner_clearance_corridor_blocked(const struct UNIT_ROUTE_POINT *points,
												  legacy_u32 count,
												  const struct UNIT_CAR_LIMITS *limits,
												  const struct UNIT_REQUEST *request);
void unit_planner_protected_target(const struct UNIT_ROUTE_POINT *points, legacy_u32 count,
								   const struct UNIT_REQUEST *request, struct UNIT_PLAN *plan);
void unit_planner_profile(legacy_u32 *seed, struct UNIT_PROFILE *profile);
void unit_planner_evaluation_begin(struct UNIT_EVALUATION *evaluation, legacy_u32 count,
								   const struct UNIT_PROFILE *profile);
/* At most UNIT_PLANNER_CHUNK_POINTS route operations. */
legacy_int unit_planner_evaluation_step(struct UNIT_EVALUATION *evaluation,
										const struct UNIT_ROUTE_POINT *points,
										const struct UNIT_CAR_LIMITS *limits, legacy_u8 owoot);
legacy_int unit_planner_rollout(const struct UNIT_ROUTE_POINT *points, legacy_u32 count,
								const struct UNIT_CAR_LIMITS *limits, legacy_u8 owoot,
								const struct UNIT_PROFILE *profile,
								const struct UNIT_REQUEST *request, legacy_u32 candidate,
								struct UNIT_ROLLOUT *result);

#endif
