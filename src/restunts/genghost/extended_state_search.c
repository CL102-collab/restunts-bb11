#include "extended_state_search.h"

void extended_genghost_state_policy(struct GENGHOST_POLICY *policy,
									const struct GENGHOST_ROUTE *route, legacy_uint variant,
									legacy_uint profile)
{
	legacy_uint barrel_count = genghost_barrel_seed_count(route);
	legacy_uint barrel_index = profile;
	legacy_int barrel_seed = profile < barrel_count;
	profile = barrel_seed ? 0U : profile - barrel_count;
	static const legacy_f64 speed[GENGHOST_STATE_PILOT_PROFILES] = {1.0, 0.75, 0.55, 1.2,
																	0.9, 1.5,  0.85, 1.05};
	genghost_policy_default(policy);
	if (barrel_seed) {
		genghost_barrel_seed_policy(route, policy, barrel_index);
	}
	for (legacy_uint n = 0; n < GENGHOST_POLICY_SEGMENTS; ++n) {
		struct GENGHOST_GENE *gene = &policy->segment[n];
		gene->line_mode = variant != 0U;
		gene->dualway_choice = variant == 2U   ? 1U
							   : variant == 3U ? 2U
							   : variant >= 8U ? 3U + (legacy_u64)variant - 8U
											   : 0U;
		/* Later mixed proposals combine independently selected branch pairs. */
		if (variant & UINT32_C(0x80000000)) {
			legacy_u64 choice =
				((legacy_u64)variant << 32U) ^ ((legacy_u64)n * UINT64_C(0x9e3779b97f4a7c15));
			choice ^= choice >> 30U;
			choice *= UINT64_C(0xbf58476d1ce4e5b9);
			choice ^= choice >> 27U;
			choice *= UINT64_C(0x94d049bb133111eb);
			gene->dualway_choice = 3U + ((choice ^ (choice >> 31U)) % (UINT64_MAX - 3U));
		}
		if (!barrel_seed) {
			gene->speed_scale = speed[profile % GENGHOST_STATE_PILOT_PROFILES];
			gene->stunt_mode = profile == 6U   ? GENGHOST_STUNT_HUMAN
							   : profile == 7U ? GENGHOST_STUNT_HUMAN_ALT
											   : GENGHOST_STUNT_CORKSCREW_CUT;
		}
		if (profile == 3U || profile == 5U) {
			gene->slide_brake = 0.0;
		}
		if (profile == 4U) {
			gene->lateral_offset = -0.35;
		}
		if (profile == 5U) {
			gene->powergear_bias = 1.0;
		}
		if (profile >= GENGHOST_STATE_PILOT_PROFILES) {
			legacy_u32 word = (profile * UINT32_C(2654435761)) ^ (n * UINT32_C(2246822519));
			word ^= word >> 16U;
			word *= UINT32_C(3266489917);
			word ^= word >> 13U;
			gene->speed_scale *= 0.70 + (word & 255U) / 425.0;
			gene->lookahead_scale = 0.75 + ((word >> 8U) & 255U) / 510.0;
			gene->lateral_offset = (((word >> 16U) & 255U) - 127.5) / 200.0;
			gene->shift_rpm_scale = 0.80 + ((word >> 24U) & 255U) / 1275.0;
			gene->powergear_bias = word & 1U ? 1.0 : 0.5;
			gene->stunt_mode =
				(word >> 2U) % 4U == 0U ? GENGHOST_STUNT_HUMAN : GENGHOST_STUNT_CORKSCREW_CUT;
		}
	}
}

legacy_uint extended_genghost_state_pilot_count(const struct GENGHOST_ROUTE *route)
{
	return GENGHOST_STATE_PILOT_PROFILES + genghost_barrel_seed_count(route);
}

legacy_int extended_genghost_state_profile_available(const struct GENGHOST_ROUTE *route,
													 legacy_uint profile)
{
	return profile < extended_genghost_state_pilot_count(route);
}
