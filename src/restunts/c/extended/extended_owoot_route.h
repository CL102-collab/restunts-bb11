#ifndef RESTUNTS_EXTENDED_OWOOT_ROUTE_H
#define RESTUNTS_EXTENDED_OWOOT_ROUTE_H

#include "../legacy.h"

struct CARSTATE;

/* Shared native/extended traversal uses one gate and jump implementation. */
legacy_s16 extended_owoot_route_is_valid(struct CARSTATE *carstate, legacy_s16 allowed_jump);
legacy_s16 extended_owoot_jump_is_valid(struct CARSTATE *carstate);

#endif
