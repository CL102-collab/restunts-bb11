#ifndef RESTUNTS_OPPONENT_H
#define RESTUNTS_OPPONENT_H

#include "../legacy.h"

#define OPPONENT_FIRST 1U
#define OPPONENT_LAST 6U

void update_opponent(void);

void opponent_route_advance(legacy_s16 route_point);
extern void update_opponent_tick(void);

#endif
