#include "extended_race_stats.h"

struct EXTENDED_REPLAY_CLOCKS extended_race_stats;

void extended_race_stats_capture(void)
{
	extended_race_stats = extended_replay_clocks;
}
