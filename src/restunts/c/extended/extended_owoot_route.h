#ifndef RESTUNTS_EXTENDED_OWOOT_ROUTE_H
#define RESTUNTS_EXTENDED_OWOOT_ROUTE_H

#include "../legacy.h"

struct CARSTATE;

/* Shared native/extended traversal uses one gate and jump implementation. */
legacy_s16 extended_owoot_route_is_valid(struct CARSTATE *carstate, legacy_s16 allowed_jump);
legacy_s16 extended_owoot_jump_is_valid(struct CARSTATE *carstate);

/* Read the serialized airborne milestone before a road landing clears it. */
legacy_s16 extended_owoot_barrel_roll_complete(const struct CARSTATE *carstate);

/* Read-only search feedback: zero before airborne progress or for another source,
 * otherwise native angle units, saturated at one full turn after completion. */
legacy_u16 extended_owoot_barrel_progress(const struct CARSTATE *carstate, legacy_s16 source);

#endif
