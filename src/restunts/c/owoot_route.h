#ifndef RESTUNTS_OWOOT_ROUTE_H
#define RESTUNTS_OWOOT_ROUTE_H

#include "legacy.h"

struct CARSTATE;

/* A failed mandatory stunt must not fall back to ordinary road contact. */
#define OWOOT_JUMP_FAILED (-1)

/* Call both checks every player tick, including ticks with a wheel over road.
 * Their progress lives in the car's reserved words, so checkpoints rewind it.
 * A jump returns one while permitted, zero outside its exception, or
 * OWOOT_JUMP_FAILED when a mandatory stunt has been attempted incorrectly. */
legacy_s16 owoot_route_is_valid(struct CARSTATE *carstate, legacy_s16 allowed_jump);
legacy_s16 owoot_jump_is_valid(struct CARSTATE *carstate);

#endif
