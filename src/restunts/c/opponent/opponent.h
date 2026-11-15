#ifndef RESTUNTS_OPPONENT_H
#define RESTUNTS_OPPONENT_H

#include "../legacy.h"

#define OPPONENT_FIRST 1U
#define OPPONENT_LEGACY_LAST 6U
#define OPPONENT_UNIT 7U
#ifdef RESTUNTS_SDL3
#define OPPONENT_LAST OPPONENT_UNIT
#else
#define OPPONENT_LAST OPPONENT_LEGACY_LAST
#endif

void update_opponent(void);

void opponent_route_advance(legacy_s16 route_point);
extern void update_opponent_tick(void);

#endif
