#include "extended_gamestep.h"
#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
#include "extended_replay_runtime.h"
#include "extended_checkpoint.h"
#endif
legacy_s16 extended_gamestep_input(legacy_s8 *input, legacy_s16 store_checkpoints)
{
#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
	if (extended_replay_is_active()) {
		extended_checkpoint_begin_step();
		*input = (legacy_s8)extended_replay_input_at(extended_replay_frame());
		extended_replay_before_step(store_checkpoints);
		return 1;
	}
#else
	(void)input;
	(void)store_checkpoints;
#endif
	return 0;
}
legacy_s16 extended_gamestep_legacy_checkpoints(void)
{
#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
	return !extended_replay_is_active();
#else
	return 1;
#endif
}
void extended_gamestep_after_step(void)
{
#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
	if (extended_replay_is_active()) {
		extended_replay_after_step();
	}
#endif
}
void extended_gamestep_finish_step(void)
{
#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
	extended_checkpoint_end_step();
#endif
}
