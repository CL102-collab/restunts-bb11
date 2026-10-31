#ifndef RESTUNTS_GENGHOST_EXTENDED_MANEUVERS_H
#define RESTUNTS_GENGHOST_EXTENDED_MANEUVERS_H

#include "maneuvers.h"
struct CARSTATE;

const legacy_char *extended_genghost_maneuver_name(enum GENGHOST_MANEUVER_KIND kind);
legacy_int extended_genghost_slalom_flight_allowed(const struct GENGHOST_MANEUVER *action);
legacy_int extended_genghost_maneuver_skip_feature(const struct GENGHOST_MANEUVER *feature);
legacy_int extended_genghost_maneuver_preserves_inputs(const struct GENGHOST_DRIVER *driver,
													   const struct GENGHOST_MANEUVER *action);
legacy_s16 extended_genghost_maneuver_target_height(const struct GENGHOST_WAYPOINT *target,
													const struct CARSTATE *car);
void extended_genghost_add_barrel_maneuvers(const struct GENGHOST_ROUTE *route,
											const struct GENGHOST_DRIVER *driver,
											const struct GENGHOST_MANEUVER *ordinary, size_t begin,
											size_t end, struct GENGHOST_MANEUVER *actions,
											size_t *count, size_t capacity);

#endif
