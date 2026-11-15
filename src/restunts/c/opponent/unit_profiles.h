#ifndef RESTUNTS_UNIT_PROFILES_H
#define RESTUNTS_UNIT_PROFILES_H
#include "../legacy.h"
struct UNIT_ROUTE;
struct UNIT_LAUNCH_POLICY;
/* Controller-owned immutable geometry plus accepted native preview policies. */
struct UNIT_ROUTE *unit_opponent_pilot_route_mutable(void);
void unit_profiles_reset(void);
void unit_profiles_policy(legacy_int preview, legacy_s16 piece, legacy_f64 speed,
						  struct UNIT_LAUNCH_POLICY *policy);
#endif
