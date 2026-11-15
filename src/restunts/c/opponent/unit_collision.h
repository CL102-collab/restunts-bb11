#ifndef RESTUNTS_UNIT_COLLISION_H
#define RESTUNTS_UNIT_COLLISION_H
#include <math.h>
#include "unit_planner.h"
/* Projection shared by rollout and cached-path safety checks. */
static inline legacy_f64 unit_collision_projected_extent(legacy_f64 along, legacy_f64 across,
														 legacy_f64 angle)
{
	return fabs(cos(angle)) * along + fabs(sin(angle)) * across;
}
/* Horizontal continuous overlap during one interval. Player translates with
 * its current velocity; UNIT follows the segment and shortest yaw arc. */
legacy_int unit_collision_swept_boxes(const struct UNIT_VEHICLE *first,
									  const struct UNIT_VEHICLE *last,
									  const struct UNIT_VEHICLE *player, legacy_f64 time,
									  legacy_f64 margin);
#endif
