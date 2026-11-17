#include "unit_pilot.h"

#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
#include <stdlib.h>
#include <string.h>
#include "unit_pilot_internal.h"
#include "unit_opponent.h"
#include "unit_learning_cache.h"
#include "../externs.h"
#include "../replay/replay.h"

static struct {
	struct UNIT_LEARNING_DATA *data;
	struct UNIT_PILOT *best;
	struct UNIT_LEARNING_PIECE *best_pieces;
	legacy_u8 active, full_start;
} unit_pilot_learning;

static void unit_pilot_learning_recipes(const struct UNIT_LEARNING_DATA *data)
{
	const struct UNIT_ROUTE *route = unit_opponent_pilot_route();
	legacy_int available = data && data->pieces && data->piece_count == route->verified_count;
	for (legacy_u32 piece = 0; piece < route->verified_count; piece++) {
		const struct UNIT_LEARNING_PIECE empty = {0};
		const struct UNIT_LEARNING_PIECE *recipe = available ? &data->pieces[piece] : &empty;
		route->verified_speeds[piece] = recipe->speed;
		route->verified_exits[piece] =
			(struct UNIT_EXIT_PROFILE){recipe->exit_fraction, recipe->exit_speed};
		route->verified_barrel_offsets[piece] = recipe->barrel_offset;
		route->verified_ramp_approaches[piece] = recipe->ramp_approach;
	}
}

static legacy_int unit_pilot_learning_capture(legacy_int completed)
{
	if (completed && !unit_pilot_state->learning_full_start) {
		return 0;
	}
	struct UNIT_LEARNING_DATA *data = unit_pilot_learning.data;
	const struct UNIT_ROUTE *route = unit_pilot_state->route;
	struct UNIT_LEARNING_PIECE *pieces = calloc(route->verified_count, sizeof(*pieces));
	struct CSRPL_OPPONENT_COMMAND *commands = NULL;
	struct UNIT_PLANNER_PROFILE profile;
	if (completed) {
		commands = calloc(unit_pilot_state->step_count, sizeof(*commands));
	}
	if (!pieces || (completed && (!commands || !unit_planner_export_profile(unit_pilot_state->seed,
																			&profile)))) {
		free(pieces);
		free(commands);
		return 0;
	}
	for (legacy_u32 piece = 0; piece < route->verified_count; piece++) {
		pieces[piece] = (struct UNIT_LEARNING_PIECE){
			route->verified_speeds[piece], route->verified_exits[piece].fraction,
			route->verified_exits[piece].speed, route->verified_barrel_offsets[piece],
			route->verified_ramp_approaches[piece]};
		/* Finishing preparation removes unseen imported hints from live route
		 * authority. Keep that earlier knowledge when harvesting later progress. */
		if (!completed && data->pieces && data->piece_count == route->verified_count &&
			pieces[piece].speed == 0.0 && pieces[piece].exit_fraction == 0.0 &&
			pieces[piece].exit_speed == 0.0 && pieces[piece].barrel_offset == 0.0 &&
			!pieces[piece].ramp_approach) {
			pieces[piece] = data->pieces[piece];
		}
	}
	if (completed) {
		for (legacy_u32 frame = 0; frame < unit_pilot_state->step_count; frame++) {
			commands[frame] = unit_pilot_state->steps[frame].command;
		}
		free(data->commands);
		data->commands = commands;
		data->command_count = data->best_frames = unit_pilot_state->step_count;
		data->best_profile = profile;
	}
	free(data->pieces);
	data->pieces = pieces;
	data->piece_count = route->verified_count;
	return 1;
}

static legacy_int unit_pilot_learning_candidate(struct UNIT_PLANNER *source)
{
	struct UNIT_LEARNING_DATA *data = unit_pilot_learning.data;
	struct UNIT_PLANNER *candidate =
		data->native_samples ? unit_planner_clone_candidate(source, data->native_samples - 1U)
							 : unit_planner_clone_sync(source);
	if (!candidate) {
		return 0;
	}
	data->native_samples++;
	unit_pilot_learning_recipes(data);
	unit_opponent_pilot_input_override(0, 0);
	unit_opponent_pilot_exit_override(ROUTE_INDEX_NONE, 0.0, 0.0);
	unit_opponent_pilot_secondary_override(ROUTE_INDEX_NONE, 0.0);
	unit_opponent_pilot_barrel_override(ROUTE_INDEX_NONE, 0.0);
	unit_opponent_pilot_ramp_override(ROUTE_INDEX_NONE, 0);
	legacy_int started = unit_pilot_start(candidate);
	unit_planner_destroy(candidate);
	if (started) {
		unit_pilot_state->learning_full_start = unit_pilot_learning.full_start;
	}
	return started;
}

/* Bank-withheld commands and speculative native steps provide no progress. */
static legacy_int unit_pilot_learning_better(const struct UNIT_PILOT *candidate,
											 const struct UNIT_PILOT *incumbent)
{
	if (!candidate || (!candidate->stats.completed_lap && !candidate->committed_steps)) {
		return 0;
	}
	if (!incumbent) {
		return 1;
	}
	if (candidate->stats.completed_lap != incumbent->stats.completed_lap) {
		return candidate->stats.completed_lap;
	}
	if (candidate->stats.completed_lap) {
		return candidate->step_count < incumbent->step_count;
	}
	legacy_u32 candidate_cursor =
		unit_route_cursor(candidate->route, &candidate->steps[candidate->committed_steps - 1U].car);
	legacy_u32 incumbent_cursor =
		unit_route_cursor(incumbent->route, &incumbent->steps[incumbent->committed_steps - 1U].car);
	return candidate_cursor > incumbent_cursor ||
		   (candidate_cursor == incumbent_cursor &&
			candidate->committed_steps < incumbent->committed_steps);
}

static legacy_int unit_pilot_learning_retain(void)
{
	const struct UNIT_ROUTE *route = unit_pilot_state->route;
	struct UNIT_LEARNING_PIECE *pieces = calloc(route->verified_count, sizeof(*pieces));
	if (!pieces) {
		return 0;
	}
	for (legacy_u32 piece = 0; piece < route->verified_count; piece++) {
		pieces[piece] = (struct UNIT_LEARNING_PIECE){
			route->verified_speeds[piece], route->verified_exits[piece].fraction,
			route->verified_exits[piece].speed, route->verified_barrel_offsets[piece],
			route->verified_ramp_approaches[piece]};
	}
	unit_pilot_release(unit_pilot_learning.best);
	free(unit_pilot_learning.best_pieces);
	unit_pilot_learning.best = unit_pilot_state;
	unit_pilot_learning.best_pieces = pieces;
	unit_pilot_state = NULL;
	return 1;
}

void unit_pilot_learning_stop(void)
{
	unit_pilot_release(unit_pilot_learning.best);
	free(unit_pilot_learning.best_pieces);
	memset(&unit_pilot_learning, 0, sizeof(unit_pilot_learning));
}

legacy_int unit_pilot_ready(void)
{
	return (unit_pilot_learning.best && unit_pilot_learning.best->stats.completed_lap) ||
		   (unit_pilot_state && unit_pilot_state->stats.completed_lap);
}

legacy_int unit_pilot_learning_begin(struct UNIT_PLANNER *source, struct UNIT_LEARNING_DATA *data)
{
	unit_pilot_stop();
	const struct UNIT_ROUTE *route = unit_opponent_pilot_route();
	if (!source || !data || !route || !route->verified_count) {
		return 0;
	}
	unit_pilot_learning.data = data;
	unit_pilot_learning.active = 1;
	legacy_u32 frame =
		extended_replay_is_active() ? extended_replay_frame() : (legacy_u16)state.game_frame;
	unit_pilot_learning.full_start = frame == 0;
	if (unit_pilot_learning.full_start && data->commands && data->command_count &&
		data->command_count <= UNIT_PILOT_TRACE_MAXIMUM &&
		data->command_count == data->best_frames && data->pieces &&
		data->piece_count == route->verified_count) {
		struct UNIT_PLANNER *incumbent = unit_planner_clone_profile(source, &data->best_profile);
		legacy_int started = incumbent && unit_pilot_start(incumbent);
		unit_planner_destroy(incumbent);
		if (started) {
			unit_pilot_state->learning_full_start = 1;
			unit_pilot_state->learned_replay_count = data->command_count;
			for (legacy_u32 frame = 0; frame < data->command_count; frame++) {
				unit_pilot_state->steps[frame].command = data->commands[frame];
			}
			return 1;
		}
	}
	if (unit_pilot_learning.full_start) {
		free(data->commands);
		data->commands = NULL;
		data->command_count = data->best_frames = 0;
	}
	/* Portable recipes remain search hints until this candidate's actual
	 * native approach proves them. Disk persistence is the caller's choice. */
	return unit_pilot_learning_candidate(source);
}

void unit_pilot_learning_work(struct UNIT_PLANNER *source, legacy_u32 maximum_ticks)
{
	if (!unit_pilot_learning.active || !source || !maximum_ticks) {
		return;
	}
	unit_pilot_work(maximum_ticks);
	if (unit_pilot_state && unit_pilot_state->stats.active) {
		return;
	}
	struct UNIT_LEARNING_DATA *data = unit_pilot_learning.data;
	if (unit_pilot_state) {
		if (unit_pilot_state->learned_replay_count) {
			if (unit_pilot_state->stats.completed_lap) {
				unit_pilot_learning_recipes(data);
			} else {
				/* Imported commands earn live authority only after a complete
				 * clean native replay from the fresh starting state. */
				free(data->commands);
				data->commands = NULL;
				data->command_count = data->best_frames = 0;
			}
		} else if (unit_pilot_learning_better(unit_pilot_state, unit_pilot_learning.best) &&
				   unit_pilot_state->stats.completed_lap && unit_pilot_state->learning_full_start) {
			if (!unit_pilot_learning_capture(1)) {
				unit_pilot_learning.active = 0;
				return;
			}
		} else if ((!unit_pilot_learning.best || !unit_pilot_learning.best->stats.completed_lap) &&
				   !data->command_count) {
			unit_pilot_learning_capture(0);
		}
		if (unit_pilot_learning_better(unit_pilot_state, unit_pilot_learning.best) &&
			!unit_pilot_learning_retain()) {
			unit_pilot_learning.active = 0;
			return;
		}
		unit_pilot_release(unit_pilot_state);
		unit_pilot_state = NULL;
	}
	if (!unit_pilot_learning_candidate(source)) {
		unit_pilot_learning.active = 0;
	}
}

struct UNIT_PLANNER *unit_pilot_learning_finish(void)
{
	if (unit_pilot_learning.best &&
		!unit_pilot_learning_better(unit_pilot_state, unit_pilot_learning.best)) {
		unit_pilot_release(unit_pilot_state);
		unit_pilot_state = unit_pilot_learning.best;
		unit_pilot_learning.best = NULL;
		struct UNIT_LEARNING_DATA recipes = {0};
		recipes.pieces = unit_pilot_learning.best_pieces;
		recipes.piece_count = unit_pilot_state->route->verified_count;
		unit_pilot_learning_recipes(&recipes);
	}
	unit_pilot_release(unit_pilot_learning.best);
	unit_pilot_learning.best = NULL;
	free(unit_pilot_learning.best_pieces);
	unit_pilot_learning.best_pieces = NULL;
	if (unit_pilot_state && unit_pilot_state->learned_replay_count &&
		!unit_pilot_state->stats.completed_lap) {
		unit_pilot_state->learned_replay_count = 0;
		unit_pilot_state->step_count = unit_pilot_state->committed_steps = 0;
		unit_pilot_state->stats.active = 0;
		unit_pilot_learning_recipes(NULL);
	} else if (unit_pilot_state && !unit_pilot_state->stats.completed_lap) {
		if (unit_pilot_learning.data && !unit_pilot_learning.data->command_count) {
			unit_pilot_learning_capture(0);
		}
		/* Other candidates' hints are not this prefix's live authority. Keep
		 * only recipes its own native entries actually validated. */
		const struct UNIT_ROUTE *route = unit_pilot_state->route;
		for (legacy_u32 piece = 0; piece < route->verified_count; piece++) {
			if (!unit_pilot_state->entries[piece].valid) {
				route->verified_speeds[piece] = 0.0;
				route->verified_exits[piece] = (struct UNIT_EXIT_PROFILE){0};
				route->verified_barrel_offsets[piece] = 0.0;
				route->verified_ramp_approaches[piece] = 0;
			}
		}
	}
	unit_pilot_learning.active = 0;
	return unit_pilot_finish();
}

void unit_pilot_learning_save_progress(void)
{
	if (unit_pilot_state && unit_pilot_state->learning_full_start && unit_pilot_learning.data &&
		!unit_pilot_learning.data->best_frames && !unit_pilot_learning.data->command_count &&
		(!unit_pilot_learning.best || !unit_pilot_learning.best->stats.completed_lap)) {
		unit_pilot_learning_capture(0);
	}
}

void unit_pilot_learning_completed(void)
{
	if (!unit_pilot_learning.active && unit_pilot_learning.data &&
		unit_pilot_state->learning_full_start) {
		unit_pilot_learning_capture(1);
	}
}

#endif
