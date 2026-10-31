#include "extended_search.h"

legacy_int extended_genghost_search_seed_policy(const struct GENGHOST_ROUTE *route,
												legacy_u64 trials, struct GENGHOST_POLICY *policy)
{
	if (trials >= genghost_barrel_seed_count(route)) {
		return 0;
	}
	/* Tire-contact timing makes neighboring launch speeds physically distinct.
	 * Fund these complete pilots before repairing a failed coarse parent. */
	genghost_barrel_seed_policy(route, policy, (legacy_uint)trials);
	return 1;
}

legacy_int extended_genghost_search_native_proposals(const struct GENGHOST_ROUTE *route,
													 legacy_u64 trials)
{
	return trials >= genghost_barrel_seed_count(route);
}
