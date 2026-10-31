#ifndef RESTUNTS_GENGHOST_EXTENDED_STATE_SEARCH_H
#define RESTUNTS_GENGHOST_EXTENDED_STATE_SEARCH_H

#include "state_search.h"

void extended_genghost_state_policy(struct GENGHOST_POLICY *policy,
									const struct GENGHOST_ROUTE *route, legacy_uint variant,
									legacy_uint profile);
legacy_uint extended_genghost_state_pilot_count(const struct GENGHOST_ROUTE *route);
legacy_int extended_genghost_state_profile_available(const struct GENGHOST_ROUTE *route,
													 legacy_uint profile);

#endif
