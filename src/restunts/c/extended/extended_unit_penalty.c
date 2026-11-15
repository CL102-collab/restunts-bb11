#include "extended_unit_penalty.h"

#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
#include "extended_replay_runtime.h"
#include "../externs.h"
#include "../crash_state.h"

#define UNIT_PENALTY_SECONDS_PER_PIECE 3U
#define UNIT_PENALTY_FIRST_UNSAFE_SKIP 3

void extended_unit_penalty_update(void)
{
	if (!extended_replay_is_active() ||
		(legacy_u8)gameconfig.game_opponenttype != CSRPL_UNIT_OPPONENT_TYPE) {
		return;
	}
	struct EXTENDED_UNIT_PENALTY *progress = &extended_replay_clocks.unit_penalty;
	if (!progress->initialized) {
		memset(progress, 0, sizeof(*progress));
		progress->route.minimum_column = progress->route.maximum_column = start_finish_column;
		progress->route.minimum_row = progress->route.maximum_row = start_finish_row;
		progress->initialized = 1;
	}
	if (state.opponentstate.car_crashBmpFlag == CRASH_EVENT_FINISH) {
		return;
	}
	legacy_s16 skipped = race_penalty_step(&progress->route, &state.opponentstate.car_position);
	race_penalty_recover_direction(&progress->route, &state.opponentstate);
	if (skipped > 0) {
		legacy_u32 added = (legacy_u32)skipped * framespersec * UNIT_PENALTY_SECONDS_PER_PIECE;
		progress->frames =
			progress->frames > LEGACY_U32_MAX - added ? LEGACY_U32_MAX : progress->frames + added;
	}
}

legacy_u32 extended_unit_penalty_total(void)
{
	return extended_replay_is_active() &&
				   (legacy_u8)gameconfig.game_opponenttype == CSRPL_UNIT_OPPONENT_TYPE
			   ? extended_replay_clocks.unit_penalty.frames
			   : 0;
}

legacy_int extended_unit_penalty_recovery_required(void)
{
	const struct EXTENDED_UNIT_PENALTY *progress = &extended_replay_clocks.unit_penalty;
	const struct RACE_PENALTY_STATE *route = &progress->route;
	/* Native confirmation counters survive returning to the same confirmed
	 * footprint. Keep that history for penalties, but let normal driving resume
	 * once both the observed piece and its distance match the confirmed route. */
	legacy_int unresolved =
		route->confirmations &&
		(route->previous_route != route->confirmed_route || route->last_skipped);
	return progress->initialized && (route->status != ROUTE_TRACKING_NORMAL || unresolved ||
									 route->last_skipped >= UNIT_PENALTY_FIRST_UNSAFE_SKIP);
}

legacy_int extended_unit_penalty_shortcuts_allowed(void)
{
	return !extended_unit_penalty_total() && !extended_unit_penalty_recovery_required();
}

#endif
