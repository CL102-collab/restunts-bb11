#ifndef RESTUNTS_EXTENDED_OPPONENT_H
#define RESTUNTS_EXTENDED_OPPONENT_H

#include "legacy.h"

/* Build a least-cost nonempty lap in the extended route table. Costs are the
 * original opponent's resource entries. Does not modify the table on failure. */
int extended_opponent_build_route(const legacy_u8 costs[256]);

#endif
