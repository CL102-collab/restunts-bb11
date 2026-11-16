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

struct UNIT_PROFILE {
	legacy_f64 lane, apex, lookahead;
	legacy_u8 clearance_passing;
};

struct UNIT_EVALUATION {
	struct UNIT_PROFILE profile;
	legacy_f64 *speeds, score;
	legacy_u32 count, cursor, phase;
};

struct UNIT_REQUEST {
	struct UNIT_VEHICLE unit, player;
	legacy_u32 cursor, generation;
	legacy_u8 native_preview;
};

struct UNIT_ROLLOUT {
	struct UNIT_PLAN plan;
	legacy_f64 score;
	legacy_u32 candidate;
};

legacy_f64 unit_planner_clamp(legacy_f64 value, legacy_f64 minimum, legacy_f64 maximum);
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
