#include "driving_rules.h"
#include "car_speed.h"
#include "physics_internal.h"
#include "race_route.h"
#include "owoot.h"

legacy_u16 driving_rules_current(void)
{
	legacy_u16 rules = 0;
	if (powergear_bug_is_enabled()) {
		rules |= DRIVING_RULE_POWER_GEAR;
	}
	if (legacy_collision_is_enabled()) {
		rules |= DRIVING_RULE_LEGACY_COLLISION;
	}
	if (left_corner_bias_is_enabled()) {
		rules |= DRIVING_RULE_LEFT_CORNER_BIAS;
	}
	if (dual_way_switching_is_enabled()) {
		rules |= DRIVING_RULE_DUAL_WAY_SWITCHING;
	}
	if (chicane_fast_grass_is_enabled()) {
		rules |= DRIVING_RULE_CHICANE_FAST_GRASS;
	}
	if (amiga_grass_is_enabled()) {
		rules |= DRIVING_RULE_AMIGA_GRASS;
	}
	if (owoot_enabled != 0) {
		rules |= DRIVING_RULE_OWOOT;
	}
	return rules;
}
