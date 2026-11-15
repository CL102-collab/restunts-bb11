#include "../legacy.h"
#include "../gamestate.h"
#include "extended_statecrs.h"

#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
#include "extended_replay_runtime.h"
#include "../externs.h"
#ifndef RESTUNTS_HEADLESS
#include "extended_race_stats.h"
#endif

void extended_crash_record_end(legacy_s16 car_index)
{
	if (extended_replay_is_active() && car_index == PLAYER_CAR_INDEX) {
		extended_replay_clocks.player_end = extended_replay_frame();
	}
}

void extended_crash_record_finish(legacy_s16 car_index)
{
	if (!extended_replay_is_active()) {
		return;
	}
	if (car_index == PLAYER_CAR_INDEX) {
		legacy_u32 frame = extended_replay_frame();
		legacy_u32 penalty = extended_replay_clocks.penalty;
		extended_replay_clocks.player_finish =
			frame > LEGACY_U32_MAX - penalty ? LEGACY_U32_MAX : frame + penalty;
	} else {
		legacy_u32 frame = extended_replay_frame();
		legacy_u32 penalty = (legacy_u8)gameconfig.game_opponenttype == CSRPL_UNIT_OPPONENT_TYPE
								 ? extended_replay_clocks.unit_penalty.frames
								 : 0;
		extended_replay_clocks.opponent_finish =
			frame > LEGACY_U32_MAX - penalty ? LEGACY_U32_MAX : frame + penalty;
	}
}

legacy_s16 extended_crash_opponent_finish_time(legacy_s16 finish)
{
	return extended_replay_is_active() &&
				   (legacy_u8)gameconfig.game_opponenttype == CSRPL_UNIT_OPPONENT_TYPE
			   ? LEGACY_S16_WRAP_ADD(finish, (legacy_u16)extended_replay_clocks.unit_penalty.frames)
			   : finish;
}

#ifndef RESTUNTS_HEADLESS
void extended_crash_capture_stats(void)
{
	if (extended_replay_is_active()) {
		extended_race_stats_capture();
	}
}
#endif
#endif
