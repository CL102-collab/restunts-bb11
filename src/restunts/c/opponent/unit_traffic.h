#ifndef RESTUNTS_UNIT_TRAFFIC_H
#define RESTUNTS_UNIT_TRAFFIC_H

#include "../legacy.h"

#define UNIT_COLLISION_REACTION_SECONDS 0.25
#define UNIT_COLLISION_HEIGHT_CLEARANCE 90.0
#define UNIT_COLLISION_MARGIN 20.0
#define UNIT_MINIMUM_MOVING_SPEED 1.0
#define UNIT_TRAFFIC_LANDING_GRAVITY(position_scale) (18.0 * 20.0 * 20.0 / (position_scale))

struct UNIT_ROUTE;
struct UNIT_VEHICLE;
/* Native following and jump-landing admission use the current human snapshot. */
legacy_int unit_collision_brake(const struct UNIT_ROUTE *route, const struct UNIT_VEHICLE *unit,
								const struct UNIT_VEHICLE *player);
legacy_int unit_landing_brake(const struct UNIT_ROUTE *route, legacy_u32 cursor,
							  const struct UNIT_VEHICLE *unit, const struct UNIT_VEHICLE *player);

#endif
