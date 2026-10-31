#ifndef RESTUNTS_GENGHOST_MANEUVERS_INTERNAL_H
#define RESTUNTS_GENGHOST_MANEUVERS_INTERNAL_H

#include "maneuvers.h"

/* Generic action-catalog services shared by native and extended proposals. */
size_t genghost_maneuver_feature_ahead(const struct GENGHOST_ROUTE *route,
									   const struct GENGHOST_DRIVER *driver, size_t begin,
									   size_t *end);
void genghost_maneuver_add_action(struct GENGHOST_MANEUVER *actions, size_t *count, size_t capacity,
								  const struct GENGHOST_MANEUVER *action);
struct GENGHOST_MANEUVER genghost_maneuver_feature_action(const struct GENGHOST_MANEUVER *ordinary,
														  const struct GENGHOST_ROUTE *route,
														  const struct GENGHOST_DRIVER *driver,
														  size_t begin, size_t end);

#endif
