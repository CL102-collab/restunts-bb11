#ifndef RESTUNTS_EXTENDED_RACE_STATS_H
#define RESTUNTS_EXTENDED_RACE_STATS_H

#include "extended_replay_runtime.h"

/* Evaluation data stays stable while the user inspects or rewinds a replay. */
extern struct EXTENDED_REPLAY_CLOCKS extended_race_stats;
void extended_race_stats_capture(void);

#endif
