#ifndef RESTUNTS_EXTENDED_UNIT_PENALTY_H
#define RESTUNTS_EXTENDED_UNIT_PENALTY_H

#include "../race/race_route.h"

struct EXTENDED_UNIT_PENALTY {
	struct RACE_PENALTY_STATE route;
	legacy_u32 frames;
	legacy_u8 initialized;
};

/* Main-thread observation of the actual post-physics opponent position. This
 * state belongs to replay clocks, so restoring a snapshot restores rule history. */
void extended_unit_penalty_update(void);
legacy_u32 extended_unit_penalty_total(void);
legacy_int extended_unit_penalty_recovery_required(void);
legacy_int extended_unit_penalty_shortcuts_allowed(void);

#endif
