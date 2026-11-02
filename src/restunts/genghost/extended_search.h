#ifndef RESTUNTS_GENGHOST_EXTENDED_SEARCH_H
#define RESTUNTS_GENGHOST_EXTENDED_SEARCH_H

#include "routes.h"

/* Mandatory stunt pilots precede generic catalog, mutation and repair work. */
legacy_int extended_genghost_search_seed_policy(const struct GENGHOST_ROUTE *route,
												legacy_u64 trials, struct GENGHOST_POLICY *policy);
legacy_int extended_genghost_search_native_proposals(const struct GENGHOST_ROUTE *route,
													 legacy_u64 trials);

/* A bounded native-input population repairs the barrel at an unfinished
 * replay's frontier. Replays are proposed only; the normal canonical finish
 * validation remains mandatory before publishing a ghost. */
/* Restrict the extra family to a barrel's approach, permitted flight budget,
 * and adjacent landing turn; ordinary unfinished sectors keep their schedule. */
legacy_int extended_genghost_search_barrel_available(const struct GENGHOST_ROUTE *route,
													 legacy_f64 progress,
													 legacy_f64 protected_progress);
struct GENGHOST_BARREL_REPAIR;
struct GENGHOST_OPTIONS;
struct GENGHOST_SEARCH_RESULT;
struct GENGHOST_RESULT;
typedef legacy_int (*genghost_barrel_stop_function)(void *opaque);
legacy_int extended_genghost_search_barrel_repair(
	struct GENGHOST_BARREL_REPAIR **repair_state, const struct GENGHOST_ROUTE *route,
	const struct GENGHOST_OPTIONS *options, const legacy_u8 *parent, legacy_uint parent_length,
	const struct GENGHOST_POLICY *parent_policy, legacy_u64 revision, legacy_u8 *inputs,
	struct GENGHOST_DRIVER *driver, struct GENGHOST_RESULT *race,
	struct GENGHOST_SEARCH_RESULT *totals, genghost_barrel_stop_function stop, void *opaque,
	legacy_uint *length, legacy_uint *protected_prefix, legacy_f64 *protected_progress);
void extended_genghost_search_barrel_free(struct GENGHOST_BARREL_REPAIR *state);

#endif
