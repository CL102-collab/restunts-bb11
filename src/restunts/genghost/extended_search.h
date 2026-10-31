#ifndef RESTUNTS_GENGHOST_EXTENDED_SEARCH_H
#define RESTUNTS_GENGHOST_EXTENDED_SEARCH_H

#include "routes.h"

/* Mandatory stunt pilots precede generic catalog, mutation and repair work. */
legacy_int extended_genghost_search_seed_policy(const struct GENGHOST_ROUTE *route,
												legacy_u64 trials, struct GENGHOST_POLICY *policy);
legacy_int extended_genghost_search_native_proposals(const struct GENGHOST_ROUTE *route,
													 legacy_u64 trials);

#endif
