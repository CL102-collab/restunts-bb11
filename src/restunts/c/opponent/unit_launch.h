#ifndef RESTUNTS_UNIT_LAUNCH_H
#define RESTUNTS_UNIT_LAUNCH_H
#include "../legacy.h"
#define UNIT_RAMP_APPROACH_MINIMUM 2U
#define UNIT_RAMP_APPROACH_MAXIMUM 4U
#define UNIT_RAMP_APPROACH_OVERRIDES 2U
struct UNIT_ROUTE;
struct UNIT_PLAN;
struct UNIT_EXIT_PROFILE;
struct UNIT_RAMP_APPROACH {
	legacy_s16 piece;
	legacy_u8 tiles;
};
struct UNIT_LAUNCH_POLICY {
	legacy_s16 primary_piece, secondary_piece;
	legacy_f64 primary_speed, secondary_speed;
	const struct UNIT_EXIT_PROFILE *exit;
	const legacy_f64 *barrel_offset;
	struct UNIT_RAMP_APPROACH ramps[UNIT_RAMP_APPROACH_OVERRIDES];
};

legacy_int unit_launch_guidance_policy(const struct UNIT_ROUTE *route, legacy_u32 cursor,
									   struct UNIT_PLAN *plan,
									   const struct UNIT_LAUNCH_POLICY *policy);

enum UNIT_LAUNCH_GUIDANCE {
	UNIT_LAUNCH_NONE,
	UNIT_LAUNCH_GUIDED,
	UNIT_LAUNCH_NEUTRAL,
	UNIT_LAUNCH_AXIS
};
legacy_int unit_launch_guidance(const struct UNIT_ROUTE *route, legacy_u32 cursor,
								struct UNIT_PLAN *plan, legacy_s16 override_piece,
								legacy_f64 override_speed);
legacy_int unit_launch_guidance_profile(const struct UNIT_ROUTE *route, legacy_u32 cursor,
										struct UNIT_PLAN *plan, legacy_s16 override_piece,
										legacy_f64 override_speed,
										const struct UNIT_EXIT_PROFILE *exit_override);
#endif
