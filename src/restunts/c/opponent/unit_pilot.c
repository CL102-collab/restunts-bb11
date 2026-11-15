#include "unit_pilot.h"

#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL_timer.h>
#include "unit_opponent.h"
#include "unit_opponent_internal.h"
#include "unit_pilot_internal.h"
#include "../externs.h"
#include "../crash_state.h"
#include "../replay/replay.h"
#include "../race/race_stats.h"
#include "../track/trackdata_layout.h"
#include "../owoot/owoot.h"
#include "../extended/extended_physics_snapshot.h"
#include "../extended/extended_owoot_internal.h"
#include "../extended/extended_owoot_route.h"
#ifndef RESTUNTS_HEADLESS
#include "../extended/extended_race_stats.h"
#endif

#define UNIT_PILOT_CHUNK_TICKS 64U
#define UNIT_PILOT_CHUNK_NS 4000000ULL
#define UNIT_PILOT_CONTINUATION_CHUNKS 2U
#define UNIT_PILOT_CONTINUATION_NS 8000000ULL
#define UNIT_PILOT_APPROACH_TILES 3.0
#define UNIT_PILOT_RAMP_APPROACH_TILES 4.0
#define UNIT_PILOT_STABLE_TICKS 4U
#define UNIT_PILOT_TRIAL_SECONDS 120U
#define UNIT_PILOT_LAP_SECONDS 1800U
#define UNIT_PILOT_PARK_HEIGHT (-30000L)
#define UNIT_PILOT_PARK_OFFSET (-4L * TRACK_TILE_SIZE)
#define UNIT_PILOT_FAILURE_PENALTY (-2)
#define UNIT_PILOT_FAILURE_TIMEOUT (-3)
#define UNIT_PILOT_TRACE_MAXIMUM 65536U
#define UNIT_PILOT_WAIT_TICKS 32U
#define UNIT_PILOT_PULSE_SEED 0x554e4954U

struct UNIT_PILOT *unit_pilot_state;

static void unit_pilot_save_live(void)
{
	struct UNIT_PILOT_ENVELOPE *saved = &unit_pilot_state->envelope;
	extended_physics_capture(&unit_pilot_state->live);
#ifndef RESTUNTS_HEADLESS
	saved->distance = gState_travDist;
	saved->frame = gState_frame;
	saved->impact = gState_impactSpeed;
	saved->jumps = gState_jumpCount;
	saved->opponent_end = gState_oEndFrame;
	saved->opponent_finish = gState_opponent_finish_time;
	saved->player_end = gState_pEndFrame;
	saved->penalty = gState_penalty;
	saved->top_speed = gState_topSpeed;
	saved->total_finish = gState_total_finish_time;
	saved->recording_flags = replay_recording_flags;
	saved->race_stats = extended_race_stats;
	replay_recording_flags |= REPLAY_RECORDING_RESTARTABLE_FLAG;
#endif
	saved->replay = is_in_replay;
	saved->replay_mode = game_replay_mode;
	saved->recorded_frames = gameconfig.game_recordedframes;
	is_in_replay = 1;
	game_replay_mode = REPLAY_MODE_LIVE;
}

static void unit_pilot_restore_live(void)
{
	const struct UNIT_PILOT_ENVELOPE *saved = &unit_pilot_state->envelope;
	extended_physics_restore(&unit_pilot_state->live);
#ifndef RESTUNTS_HEADLESS
	gState_travDist = saved->distance;
	gState_frame = saved->frame;
	gState_impactSpeed = saved->impact;
	gState_jumpCount = saved->jumps;
	gState_oEndFrame = saved->opponent_end;
	gState_opponent_finish_time = saved->opponent_finish;
	gState_pEndFrame = saved->player_end;
	gState_penalty = saved->penalty;
	gState_topSpeed = saved->top_speed;
	gState_total_finish_time = saved->total_finish;
	replay_recording_flags = saved->recording_flags;
	extended_race_stats = saved->race_stats;
#endif
	is_in_replay = saved->replay;
	game_replay_mode = saved->replay_mode;
	gameconfig.game_recordedframes = saved->recorded_frames;
}

void unit_pilot_stop(void)
{
	unit_opponent_pilot_input_override(0, 0);
	if (!unit_pilot_state) {
		return;
	}
	unit_opponent_pilot_exit_override(ROUTE_INDEX_NONE, 0.0, 0.0);
	unit_opponent_pilot_secondary_override(ROUTE_INDEX_NONE, 0.0);
	unit_opponent_pilot_barrel_override(ROUTE_INDEX_NONE, 0.0);
	unit_opponent_pilot_ramp_override(ROUTE_INDEX_NONE, 0);
	unit_planner_destroy(unit_pilot_state->planner);
	unit_planner_destroy(unit_pilot_state->approach_planner);
	unit_planner_destroy(unit_pilot_state->seed);
	unit_pilot_pulses_destroy(unit_pilot_state->pulses);
	free(unit_pilot_state->entries);
	free(unit_pilot_state->steps);
	free(unit_pilot_state);
	unit_pilot_state = NULL;
}

legacy_int unit_pilot_begin(struct UNIT_PLANNER *source)
{
	unit_pilot_stop();
	const struct UNIT_ROUTE *route = unit_opponent_pilot_route();
	if (!source || !route || !route->verified_count) {
		return 0;
	}
	unit_pilot_state = calloc(1, sizeof(*unit_pilot_state));
	if (!unit_pilot_state) {
		return 0;
	}
	unit_pilot_state->route = route;
	unit_pilot_state->pulses = unit_pilot_pulses_create(UNIT_PILOT_PULSE_SEED);
	unit_pilot_state->entries = calloc(route->verified_count, sizeof(*unit_pilot_state->entries));
	unit_pilot_state->steps = calloc(UNIT_PILOT_TRACE_MAXIMUM, sizeof(*unit_pilot_state->steps));
	unit_pilot_state->step_capacity = UNIT_PILOT_TRACE_MAXIMUM;
	unit_pilot_state->seed = unit_planner_clone_sync(source);
	if (unit_pilot_state->seed) {
		unit_pilot_state->planner = unit_planner_clone_sync(unit_pilot_state->seed);
		unit_pilot_state->approach_planner = unit_planner_clone_sync(unit_pilot_state->seed);
	}
	if (!unit_pilot_state->pulses || !unit_pilot_state->entries || !unit_pilot_state->steps ||
		!unit_pilot_state->planner || !unit_pilot_state->approach_planner) {
		unit_pilot_stop();
		return 0;
	}
	unit_pilot_state->first = route->count;
	unit_pilot_state->stats.last_piece = ROUTE_INDEX_NONE;
	unit_pilot_state->stats.first_mismatch_offset = (legacy_u16)sizeof(struct CARSTATE);
	unit_pilot_state->stats.active = 1;
	unit_pilot_save_live();
	state.game_inputmode = GAME_INPUT_MODE_ACTIVE;
	/* Keep the native human body and avoidance snapshot outside track bounds;
	 * valid custom tracks can have roads near the maximum native altitude. */
	state.playerstate.car_position.lx = UNIT_PILOT_PARK_OFFSET * (legacy_s32)UNIT_POSITION_SCALE;
	state.playerstate.car_position.lz = UNIT_PILOT_PARK_OFFSET * (legacy_s32)UNIT_POSITION_SCALE;
	state.playerstate.car_position.ly = UNIT_PILOT_PARK_HEIGHT * (legacy_s32)UNIT_POSITION_SCALE;
	state.playerstate.car_previous_position = state.playerstate.car_position;
	state.playerstate.car_actual_speed = CAR_SPEED_STOPPED;
	state.playerstate.car_rev_speed = CAR_SPEED_STOPPED;
	unit_pilot_state->frame =
		extended_replay_is_active() ? extended_replay_frame() : (legacy_u16)state.game_frame;
	unit_pilot_state->run_start_frame = unit_pilot_state->frame;
	extended_physics_capture(&unit_pilot_state->run);
	unit_pilot_restore_live();
	return 1;
}

static legacy_f64 unit_pilot_approach_distance(legacy_u8 stunt)
{
	return (stunt == UNIT_STUNT_RAMP ? UNIT_PILOT_RAMP_APPROACH_TILES : UNIT_PILOT_APPROACH_TILES) *
		   TRACK_TILE_SIZE;
}

/* Certify overlapping approaches together, keeping the preview window bounded. */
static legacy_u32 unit_pilot_group_end(legacy_u32 last)
{
	const struct UNIT_ROUTE *route = unit_pilot_state->route;
	legacy_f64 gap = 0.0;
	for (legacy_u32 next = last + 1U; next < route->finish_begin; next++) {
		const struct UNIT_ROUTE_POINT *a = &route->points[next - 1U];
		const struct UNIT_ROUTE_POINT *b = &route->points[next];
		gap += hypot(hypot(a->x - b->x, a->z - b->z), a->y - b->y);
		if (gap > UNIT_PILOT_RAMP_APPROACH_TILES * TRACK_TILE_SIZE) {
			break;
		}
		if (b->stunt == UNIT_STUNT_NONE || gap > unit_pilot_approach_distance(b->stunt)) {
			continue;
		}
		while (next + 1U < route->count && route->points[next + 1U].route_index == b->route_index) {
			next++;
		}
		last = next;
		gap = 0.0;
	}
	return last;
}

static void unit_pilot_find_stunt(void)
{
	const struct UNIT_ROUTE *route = unit_pilot_state->route;
	legacy_u32 first = unit_pilot_state->next_search;
	while (first < route->finish_begin && route->points[first].stunt == UNIT_STUNT_NONE) {
		first++;
	}
	unit_pilot_state->first = first;
	if (first >= route->finish_begin) {
		return;
	}
	legacy_u32 last = first;
	while (last + 1U < route->count &&
		   route->points[last + 1U].route_index == route->points[first].route_index) {
		last++;
	}
	unit_pilot_state->first_last = last;
	unit_pilot_state->last = unit_pilot_group_end(last);
	unit_pilot_state->receiver_last = unit_pilot_state->last + 1U;
	while (unit_pilot_state->receiver_last + 1U < route->count &&
		   route->points[unit_pilot_state->receiver_last + 1U].route_index ==
			   route->points[unit_pilot_state->last + 1U].route_index) {
		unit_pilot_state->receiver_last++;
	}
	unit_pilot_state->exit_speed = 0.0;
	unit_pilot_state->secondary_piece = ROUTE_INDEX_NONE;
	unit_pilot_state->secondary_first = route->count;
	for (legacy_u32 next = last + 1U; next <= unit_pilot_state->last; next++) {
		if (route->points[next].stunt == UNIT_STUNT_PROTECTED ||
			route->points[next].stunt == UNIT_STUNT_RAMP) {
			unit_pilot_state->secondary_piece = route->points[next].route_index;
			unit_pilot_state->secondary_first = next;
			break;
		}
	}
	for (legacy_u32 next = last + 1U; next <= unit_pilot_state->last; next++) {
		const struct UNIT_ROUTE_POINT *point = &route->points[next];
		if (point->stunt != UNIT_STUNT_NONE) {
			unit_pilot_state->exit_speed =
				fmin(route->limits.max_speed,
					 point->speed_floor > 0.0 ? point->speed_floor : point->speed_limit);
			break;
		}
	}
	legacy_f64 required = unit_pilot_approach_distance(route->points[first].stunt);
	legacy_f64 ahead = 0.0;
	/* A later ramp can need run-up before the first member's approach.
	 * Capture that common beginning before validating any committed exit. */
	for (legacy_u32 next = first + 1U; next <= unit_pilot_state->last; next++) {
		const struct UNIT_ROUTE_POINT *a = &route->points[next - 1U];
		const struct UNIT_ROUTE_POINT *b = &route->points[next];
		ahead += hypot(hypot(a->x - b->x, a->z - b->z), a->y - b->y);
		if (ahead >= UNIT_PILOT_RAMP_APPROACH_TILES * TRACK_TILE_SIZE) {
			break;
		}
		if (b->stunt != UNIT_STUNT_NONE && a->route_index != b->route_index) {
			required = fmax(required, unit_pilot_approach_distance(b->stunt) - ahead);
		}
	}
	legacy_f64 distance = 0.0;
	legacy_u32 approach = first;
	while (approach > 0 && distance < required) {
		const struct UNIT_ROUTE_POINT *a = &route->points[approach];
		const struct UNIT_ROUTE_POINT *b = &route->points[approach - 1U];
		distance += sqrt((a->x - b->x) * (a->x - b->x) + (a->y - b->y) * (a->y - b->y) +
						 (a->z - b->z) * (a->z - b->z));
		approach--;
	}
	unit_pilot_state->approach_cursor = approach;
}

void unit_pilot_exhaust(legacy_s16 failure)
{
	unit_pilot_state->stats.active = 0;
	unit_pilot_state->stats.exhausted = 1;
	unit_pilot_state->stats.last_failure = failure;
	unit_pilot_state->recheck_status = -1;
}

/* Guide cursors advance before a ramp lip. Prove passage onto the receiving
 * road with actual position and the independent native footprint observer. */
static legacy_int unit_pilot_reached_receiver(legacy_u32 cursor)
{
	const struct UNIT_ROUTE *route = unit_pilot_state->route;
	if (unit_pilot_state->last + 1U >= route->count || cursor < unit_pilot_state->receiver_last) {
		return 0;
	}
	const struct UNIT_ROUTE_POINT *receiver = &route->points[unit_pilot_state->receiver_last];
	const struct UNIT_ROUTE_POINT *lip = &route->points[unit_pilot_state->receiver_last - 1U];
	legacy_f64 dx = receiver->x - lip->x, dz = receiver->z - lip->z;
	legacy_f64 x = state.opponentstate.car_position.lx / UNIT_POSITION_SCALE;
	legacy_f64 z = state.opponentstate.car_position.lz / UNIT_POSITION_SCALE;
	if ((x - receiver->x) * dx + (z - receiver->z) * dz < 0.0) {
		return 0;
	}
	const struct EXTENDED_UNIT_PENALTY *observed = &extended_replay_clocks.unit_penalty;
	if (!observed->initialized) {
		return 1;
	}
	if (extended_unit_penalty_recovery_required()) {
		return 0;
	}
	for (legacy_u32 point = unit_pilot_state->last + 1U; point <= cursor && point < route->count;
		 point++) {
		if (route->points[point].stunt != UNIT_STUNT_NONE) {
			return 0;
		}
		if (route->points[point].route_index == observed->route.previous_route) {
			return 1;
		}
	}
	return 0;
}

static legacy_int unit_pilot_grounded(void)
{
	const struct CARSTATE *car = &state.opponentstate;
	for (legacy_u16 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
		if (car->car_surfaceWhl[wheel] < CAR_SURFACE_PAVED ||
			car->car_surfaceWhl[wheel] > CAR_SURFACE_ICE) {
			return 0;
		}
	}
	/* Sloped landings legitimately have nonzero pitch. Native road contacts
	 * and cleared OWOOT jump state validate support without a flat-pose rule. */
	return !owoot_enabled || car->car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_SOURCE] == 0;
}

static void unit_pilot_accept_trial(void)
{
	legacy_s16 piece = unit_pilot_state->stats.last_piece;
	unit_pilot_pulses_finish_trial(unit_pilot_state->pulses, &state.opponentstate,
								   unit_pilot_state->frame, 1);
	struct UNIT_EXIT_PROFILE exit = unit_pilot_exit_profile();
	unit_opponent_pilot_accept(piece, unit_pilot_state->speed);
	unit_opponent_pilot_accept_exit(piece, exit.fraction, exit.speed);
	if (unit_pilot_state->route->points[unit_pilot_state->first].stunt == UNIT_STUNT_RAMP) {
		unit_opponent_pilot_accept_ramp(piece, unit_pilot_state->ramp_approach);
	}
	if (unit_pilot_state->route->points[unit_pilot_state->first].stunt == UNIT_STUNT_BARREL) {
		unit_opponent_pilot_accept_barrel(piece, unit_pilot_state->barrel_offset);
	}
	if (unit_pilot_state->secondary_ramp_approach) {
		unit_opponent_pilot_accept_ramp(unit_pilot_state->secondary_piece,
										unit_pilot_state->secondary_ramp_approach);
	}
	if (unit_pilot_state->secondary_speed > 0.0) {
		unit_opponent_pilot_accept(unit_pilot_state->secondary_piece,
								   unit_pilot_state->secondary_speed);
		if (unit_pilot_state->has_secondary_entry) {
			struct UNIT_PILOT_ENTRY *entry =
				&unit_pilot_state->entries[unit_pilot_state->secondary_piece];
			entry->car = unit_pilot_state->secondary_entry;
			entry->speed = unit_pilot_state->secondary_speed;
			entry->valid = 1;
		}
	}
	unit_opponent_pilot_exit_override(ROUTE_INDEX_NONE, 0.0, 0.0);
	unit_opponent_pilot_secondary_override(ROUTE_INDEX_NONE, 0.0);
	unit_opponent_pilot_barrel_override(ROUTE_INDEX_NONE, 0.0);
	unit_opponent_pilot_ramp_override(ROUTE_INDEX_NONE, 0);
	unit_opponent_pilot_input_override(0, 0);
	if (piece >= 0 && (legacy_u32)piece < unit_pilot_state->route->verified_count &&
		unit_pilot_state->has_entry) {
		unit_pilot_state->entries[piece].car = unit_pilot_state->entry;
		unit_pilot_state->entries[piece].speed = unit_pilot_state->speed;
		unit_pilot_state->entries[piece].valid = 1;
	}
	unit_pilot_state->stats.accepted++;
	unit_pilot_state->committed_steps = unit_pilot_state->step_count;
	if (unit_pilot_state->recheck) {
		unit_pilot_state->stats.active = 0;
		unit_pilot_state->recheck_status = 1;
	}
	unit_pilot_state->trial = 0;
	unit_pilot_state->next_search = unit_pilot_state->last + 1U;
	unit_pilot_state->first = unit_pilot_state->route->count;
}

static legacy_int unit_pilot_reserve_step(void)
{
	return unit_pilot_state->step_count < unit_pilot_state->step_capacity;
}

static void unit_pilot_wait_tick(void)
{
	extended_checkpoint_begin_step();
	state.game_frame = LEGACY_S16_WRAP_ADD(state.game_frame, 1);
	if (extended_replay_is_active()) {
		extended_replay_after_step();
	}
	legacy_s16 event = unit_opponent_pilot_wait_step(unit_pilot_state->frame);
	unit_pilot_state->frame++;
	unit_pilot_state->stats.native_ticks++;
	extended_checkpoint_end_step();
	if (event >= 0) {
		event = state.opponentstate.car_crashBmpFlag;
	}
	if (event || extended_unit_penalty_total() > unit_pilot_state->penalty_baseline) {
		unit_pilot_fail_trial(event ? event : UNIT_PILOT_FAILURE_PENALTY);
		return;
	}
	unit_pilot_state->wait_remaining--;
}

static void unit_pilot_tick(void)
{
	if (unit_pilot_state->wait_remaining) {
		unit_pilot_wait_tick();
		return;
	}
	legacy_u32 cursor = unit_route_cursor(unit_pilot_state->route, &state.opponentstate);
	if (!unit_pilot_state->trial && unit_pilot_state->first == unit_pilot_state->route->count) {
		unit_pilot_find_stunt();
	}
	if (!unit_pilot_state->trial &&
		unit_pilot_state->first < unit_pilot_state->route->finish_begin &&
		cursor >= unit_pilot_state->approach_cursor) {
		extended_physics_capture(&unit_pilot_state->approach);
		unit_pilot_state->approach_frame = unit_pilot_state->frame;
		unit_pilot_state->approach_steps = unit_pilot_state->step_count;
		if (!unit_planner_restore_continuation(unit_pilot_state->approach_planner,
											   unit_pilot_state->planner)) {
			unit_pilot_exhaust(UNIT_PILOT_FAILURE_MEMORY);
			return;
		}
		legacy_s16 piece = unit_pilot_state->route->points[unit_pilot_state->first].route_index;
		unit_pilot_state->exit_variant = 0;
		if (piece >= 0 && (legacy_u32)piece < unit_pilot_state->route->verified_count &&
			unit_pilot_state->route->verified_exits) {
			const struct UNIT_EXIT_PROFILE *known = &unit_pilot_state->route->verified_exits[piece];
			legacy_f64 entry = unit_pilot_state->route->verified_speeds[piece];
			if (known->fraction > 0.0 && known->speed > entry &&
				unit_pilot_state->exit_speed > entry) {
				legacy_u8 phase =
					(legacy_u8)unit_bounded(known->fraction * (UNIT_PILOT_EXIT_PHASES + 1U) + 0.5,
											1, UNIT_PILOT_EXIT_PHASES);
				legacy_u8 level =
					(legacy_u8)unit_bounded((known->speed - entry) * UNIT_PILOT_EXIT_LEVELS /
													(unit_pilot_state->exit_speed - entry) +
												0.5,
											1, UNIT_PILOT_EXIT_LEVELS);
				unit_pilot_state->exit_variant = (level - 1U) * UNIT_PILOT_EXIT_PHASES + phase;
			}
		}
		unit_pilot_state->candidate = 0;
		unit_pilot_state->secondary_candidate = 0;
		unit_pilot_state->secondary_ramp_variant = 0;
		unit_pilot_state->barrel_variant = 0;
		unit_pilot_state->ramp_variant = 0;
		unit_pilot_state->barrel_reached = 0;
		unit_pilot_state->barrel_best_progress = 0;
		unit_pilot_state->barrel_best_speed = 0.0;
		if (unit_pilot_state->recheck && unit_pilot_state->resume_pending) {
			unit_pilot_state->candidate = unit_pilot_state->resume_candidate;
			unit_pilot_state->exit_variant = unit_pilot_state->resume_exit_variant;
			unit_pilot_state->secondary_candidate = unit_pilot_state->resume_secondary_candidate;
			unit_pilot_state->secondary_ramp_variant =
				unit_pilot_state->resume_secondary_ramp_variant;
			unit_pilot_state->barrel_variant = unit_pilot_state->resume_barrel_variant;
			unit_pilot_state->ramp_variant = unit_pilot_state->resume_ramp_variant;
			unit_pilot_state->barrel_reached = unit_pilot_state->resume_barrel_reached;
			unit_pilot_state->barrel_best_progress = unit_pilot_state->resume_barrel_best_progress;
			unit_pilot_state->barrel_best_speed = unit_pilot_state->resume_barrel_best_speed;
		}
		unit_pilot_pulses_begin(unit_pilot_state->pulses, unit_pilot_state->route,
								unit_pilot_state->first, unit_pilot_state->first_last,
								framespersec);
		if (unit_pilot_state->recheck && unit_pilot_state->resume_pending &&
			unit_pilot_state->resume_pulses &&
			!unit_pilot_pulses_next(unit_pilot_state->pulses, &unit_pilot_state->pulse_profile)) {
			unit_pilot_exhaust(UNIT_PILOT_FAILURE_TIMEOUT);
			return;
		}
		unit_pilot_state->resume_pending = 0;
		unit_pilot_start_trial();
	}
	if (unit_pilot_state->trial && cursor >= unit_pilot_state->first &&
		!unit_pilot_state->has_entry) {
		unit_pilot_state->entry = state.opponentstate;
		unit_pilot_state->entry_step = unit_pilot_state->step_count;
		unit_pilot_state->has_entry = 1;
	}
	if (unit_pilot_state->trial && cursor >= unit_pilot_state->secondary_first &&
		!unit_pilot_state->has_secondary_entry) {
		unit_pilot_state->secondary_entry = state.opponentstate;
		unit_pilot_state->has_secondary_entry = 1;
	}
	if (!unit_pilot_reserve_step()) {
		unit_pilot_exhaust(UNIT_PILOT_FAILURE_MEMORY);
		return;
	}
	struct UNIT_PILOT_STEP *record = &unit_pilot_state->steps[unit_pilot_state->step_count];
	if (unit_pilot_state->trial &&
		unit_pilot_state->route->points[unit_pilot_state->first].stunt == UNIT_STUNT_BARREL) {
		legacy_s16 progress = extended_owoot_barrel_progress(&state.opponentstate,
															 unit_pilot_state->stats.last_piece);
		if (progress > unit_pilot_state->barrel_trial_progress) {
			unit_pilot_state->barrel_trial_progress = (legacy_u16)progress;
		}
	}
	if (unit_pilot_state->trial) {
		unit_pilot_pulses_before_step(unit_pilot_state->pulses, &state.opponentstate,
									  unit_pilot_state->frame);
	}
	record->car = state.opponentstate;
	extended_checkpoint_begin_step();
	state.game_frame = LEGACY_S16_WRAP_ADD(state.game_frame, 1);
	if (extended_replay_is_active()) {
		extended_replay_after_step();
	}
	legacy_s16 event = unit_opponent_pilot_step(
		unit_pilot_state->planner, unit_pilot_state->frame,
		unit_pilot_state->trial ? unit_pilot_state->stats.last_piece : ROUTE_INDEX_NONE,
		unit_pilot_state->trial ? unit_pilot_state->speed : 0.0);
	unit_opponent_pilot_last_command(&record->command);
	unit_pilot_state->step_count++;
	unit_pilot_state->frame++;
	unit_pilot_state->stats.native_ticks++;
	extended_checkpoint_end_step();
	if (event >= CRASH_EVENT_NONE) {
		event = state.opponentstate.car_crashBmpFlag;
	}
	if (extended_unit_penalty_total() > unit_pilot_state->penalty_baseline) {
		unit_pilot_fail_trial(UNIT_PILOT_FAILURE_PENALTY);
		return;
	}
	if (event == CRASH_EVENT_FINISH) {
		if (unit_pilot_state->trial && unit_pilot_state->has_entry) {
			unit_pilot_accept_trial();
		}
		unit_pilot_state->stats.completed_lap = 1;
		unit_pilot_state->committed_steps = unit_pilot_state->step_count;
		if (unit_pilot_state->recheck) {
			unit_pilot_state->recheck_status = 1;
		}
		unit_pilot_state->stats.active = 0;
		return;
	}
	if (event != CRASH_EVENT_NONE) {
		unit_pilot_fail_trial(event);
		return;
	}
	cursor = unit_route_cursor(unit_pilot_state->route, &state.opponentstate);
	if (unit_pilot_state->trial && cursor > unit_pilot_state->last &&
		cursor < unit_pilot_state->route->count &&
		unit_pilot_state->route->points[cursor].stunt == UNIT_STUNT_NONE && unit_pilot_grounded() &&
		unit_pilot_reached_receiver(cursor)) {
		if (++unit_pilot_state->stable_ticks >= UNIT_PILOT_STABLE_TICKS) {
			unit_pilot_accept_trial();
		}
	} else {
		unit_pilot_state->stable_ticks = 0;
	}
	legacy_u32 trial_ticks = UNIT_PILOT_TRIAL_SECONDS * (legacy_u16)framespersec;
	legacy_u32 lap_ticks = UNIT_PILOT_LAP_SECONDS * (legacy_u16)framespersec;
	if ((unit_pilot_state->trial &&
		 unit_pilot_state->frame - unit_pilot_state->approach_frame >= trial_ticks) ||
		unit_pilot_state->frame - unit_pilot_state->run_start_frame >= lap_ticks) {
		unit_pilot_fail_trial(UNIT_PILOT_FAILURE_TIMEOUT);
	}
}

void unit_pilot_work(legacy_u32 maximum_ticks)
{
	if (!unit_pilot_state || !unit_pilot_state->stats.active || !maximum_ticks) {
		return;
	}
	if (maximum_ticks > UNIT_PILOT_CHUNK_TICKS) {
		maximum_ticks = UNIT_PILOT_CHUNK_TICKS;
	}
	legacy_u64 start = SDL_GetTicksNS();
	unit_pilot_save_live();
	extended_physics_restore(&unit_pilot_state->run);
	unit_pilot_restore_overrides();
	for (legacy_u32 tick = 0; tick < maximum_ticks && unit_pilot_state->stats.active; tick++) {
		unit_pilot_tick();
		if (SDL_GetTicksNS() - start >= UNIT_PILOT_CHUNK_NS) {
			break;
		}
	}
	extended_physics_capture(&unit_pilot_state->run);
	unit_opponent_pilot_input_override(0, 0);
	unit_pilot_restore_live();
}

struct UNIT_PLANNER *unit_pilot_finish(void)
{
	unit_opponent_pilot_input_override(0, 0);
	if (!unit_pilot_state) {
		return NULL;
	}
	unit_pilot_state->continuation = unit_pilot_state->stats.active && !unit_pilot_state->recheck &&
									 unit_pilot_state->committed_steps > 0;
	unit_pilot_state->continuation_matched = unit_pilot_state->continuation_has_frame = 0;
	unit_pilot_state->stats.active = 0;
	unit_opponent_pilot_exit_override(ROUTE_INDEX_NONE, 0.0, 0.0);
	unit_opponent_pilot_secondary_override(ROUTE_INDEX_NONE, 0.0);
	unit_opponent_pilot_barrel_override(ROUTE_INDEX_NONE, 0.0);
	unit_opponent_pilot_ramp_override(ROUTE_INDEX_NONE, 0);
	return unit_pilot_state->seed;
}

legacy_int unit_pilot_continuation_pending(void)
{
	return unit_pilot_state && unit_pilot_state->continuation && !unit_pilot_state->recheck &&
		   !unit_pilot_state->stats.exhausted && !unit_pilot_state->stats.completed_lap;
}

legacy_int unit_pilot_continue_work(legacy_u32 maximum_ticks)
{
	if (!unit_pilot_continuation_pending() || !unit_pilot_state->continuation_matched ||
		!maximum_ticks) {
		return unit_pilot_continuation_pending();
	}
	unit_pilot_state->continuation_matched = 0;
	unit_pilot_state->stats.active = 1;
	if (maximum_ticks > UNIT_PILOT_CONTINUATION_TICKS) {
		maximum_ticks = UNIT_PILOT_CONTINUATION_TICKS;
	}
	legacy_u64 start = SDL_GetTicksNS();
	for (legacy_u32 chunk = 0;
		 chunk < UNIT_PILOT_CONTINUATION_CHUNKS && maximum_ticks && unit_pilot_state->stats.active;
		 chunk++) {
		if (chunk && SDL_GetTicksNS() - start >= UNIT_PILOT_CONTINUATION_NS) {
			break;
		}
		legacy_u32 ticks =
			maximum_ticks < UNIT_PILOT_CHUNK_TICKS ? maximum_ticks : UNIT_PILOT_CHUNK_TICKS;
		unit_pilot_work(ticks);
		maximum_ticks -= ticks;
	}
	if (!unit_pilot_state->stats.active) {
		unit_pilot_state->continuation = 0;
	}
	return unit_pilot_continuation_pending();
}

void unit_pilot_continuation_cancel(void)
{
	unit_opponent_pilot_input_override(0, 0);
	if (!unit_pilot_state) {
		return;
	}
	unit_pilot_state->stats.active = 0;
	unit_pilot_state->continuation = unit_pilot_state->continuation_matched = 0;
	unit_pilot_state->recheck = unit_pilot_state->resume_pending = 0;
	unit_pilot_state->recheck_status = -1;
	unit_pilot_state->committed_steps = unit_pilot_state->next_step;
	unit_opponent_pilot_exit_override(ROUTE_INDEX_NONE, 0.0, 0.0);
	unit_opponent_pilot_secondary_override(ROUTE_INDEX_NONE, 0.0);
	unit_opponent_pilot_barrel_override(ROUTE_INDEX_NONE, 0.0);
	unit_opponent_pilot_ramp_override(ROUTE_INDEX_NONE, 0);
}

legacy_int unit_pilot_next_stunt_with_lead(const struct CARSTATE *car, legacy_s16 *piece,
										   legacy_f64 *distance, legacy_f64 lead)
{
	const struct UNIT_ROUTE *route = unit_opponent_pilot_route();
	if (!route || !car) {
		return 0;
	}
	if (!isfinite(lead) || lead < 0.0) {
		return 0;
	}
	legacy_u32 cursor = unit_route_advance(route, car, unit_route_cursor(route, car));
	legacy_f64 approach = 0.0;
	if (cursor > 0 && cursor < route->count &&
		(route->points[cursor].stunt == UNIT_STUNT_NONE ||
		 route->points[cursor - 1U].route_index != route->points[cursor].route_index)) {
		const struct UNIT_ROUTE_POINT *point = &route->points[cursor];
		legacy_f64 dx = point->x - route->points[cursor - 1U].x;
		legacy_f64 dz = point->z - route->points[cursor - 1U].z;
		legacy_f64 length = hypot(dx, dz);
		if (length > 0.0) {
			approach = fmax(0.0, ((point->x - car->car_position.lx / UNIT_POSITION_SCALE) * dx +
								  (point->z - car->car_position.lz / UNIT_POSITION_SCALE) * dz) /
									 length);
		}
	}
	/* The controller only rechecks the nearby approach. Do not scan an entire
	 * large track when a cached command no longer matches and no stunt is near. */
	for (legacy_u32 index = cursor;
		 index < route->finish_begin &&
		 approach <= UNIT_PILOT_RAMP_APPROACH_TILES * TRACK_TILE_SIZE + lead;
		 index++) {
		if (route->points[index].stunt != UNIT_STUNT_NONE) {
			if (approach > unit_pilot_approach_distance(route->points[index].stunt) + lead) {
				return 0;
			}
			if (piece) {
				*piece = route->points[index].route_index;
			}
			if (distance) {
				*distance = approach;
			}
			return 1;
		}
		if (index + 1U < route->count) {
			approach += hypot(route->points[index + 1U].x - route->points[index].x,
							  route->points[index + 1U].z - route->points[index].z);
		}
	}
	return 0;
}

legacy_int unit_pilot_next_stunt(const struct CARSTATE *car, legacy_s16 *piece,
								 legacy_f64 *distance)
{
	return unit_pilot_next_stunt_with_lead(car, piece, distance, 0.0);
}

legacy_int unit_pilot_recheck_begin(struct UNIT_PLANNER *source, legacy_u32 frame, legacy_s16 piece)
{
	if (!source || (!unit_pilot_state && !unit_pilot_begin(source))) {
		return 0;
	}
	if (unit_pilot_state->stats.last_piece != piece) {
		unit_pilot_state->resume_pending = 0;
	}
	legacy_u32 first = 0;
	while (first < unit_pilot_state->route->finish_begin &&
		   (unit_pilot_state->route->points[first].route_index != piece ||
			unit_pilot_state->route->points[first].stunt == UNIT_STUNT_NONE)) {
		first++;
	}
	if (first >= unit_pilot_state->route->finish_begin) {
		return 0;
	}
	if (!unit_planner_restore_continuation(unit_pilot_state->planner, source)) {
		return 0;
	}
	unit_pilot_state->continuation = unit_pilot_state->continuation_matched = 0;
	unit_pilot_state->recheck = 1;
	unit_pilot_state->recheck_status = 0;
	unit_pilot_state->stats.active = 1;
	unit_pilot_state->stats.exhausted = 0;
	unit_pilot_state->stats.completed_lap = 0;
	unit_pilot_state->trial = 0;
	unit_pilot_state->next_search = first;
	unit_pilot_find_stunt();
	unit_pilot_state->approach_cursor =
		unit_route_cursor(unit_pilot_state->route, &state.opponentstate);
	unit_pilot_state->step_count = unit_pilot_state->committed_steps = unit_pilot_state->next_step =
		0;
	unit_pilot_state->last_live_frame = frame;
	unit_pilot_state->wait_remaining = UNIT_PILOT_WAIT_TICKS;
	unit_pilot_state->handoff_frame = frame + UNIT_PILOT_WAIT_TICKS;
	unit_pilot_state->penalty_baseline = extended_unit_penalty_total();
	unit_pilot_save_live();
	state.game_inputmode = GAME_INPUT_MODE_ACTIVE;
	state.game_frame = (legacy_s16)(legacy_u16)frame;
	if (extended_replay_is_active()) {
		extended_replay_clocks.frame = frame;
	}
	unit_pilot_state->frame = frame;
	unit_pilot_state->run_start_frame = unit_pilot_state->frame;
	extended_physics_capture(&unit_pilot_state->run);
	unit_pilot_restore_live();
	return 1;
}

void unit_pilot_recheck_cancel(void)
{
	unit_opponent_pilot_input_override(0, 0);
	if (!unit_pilot_state || !unit_pilot_state->recheck) {
		return;
	}
	unit_pilot_state->stats.active = 0;
	unit_pilot_state->recheck = 0;
	unit_pilot_state->recheck_status = -1;
	unit_pilot_state->committed_steps = 0;
	unit_pilot_state->resume_pending = 0;
	unit_opponent_pilot_exit_override(ROUTE_INDEX_NONE, 0.0, 0.0);
	unit_opponent_pilot_secondary_override(ROUTE_INDEX_NONE, 0.0);
	unit_opponent_pilot_barrel_override(ROUTE_INDEX_NONE, 0.0);
	unit_opponent_pilot_ramp_override(ROUTE_INDEX_NONE, 0);
}

legacy_int unit_pilot_recheck_status(legacy_u32 frame)
{
	if (!unit_pilot_state || !unit_pilot_state->recheck) {
		return -1;
	}
	if (frame > unit_pilot_state->handoff_frame && !unit_pilot_state->next_step) {
		unit_pilot_state->resume_pulses = unit_pilot_pulses_active(unit_pilot_state->pulses);
		unit_pilot_state->resume_pending =
			(unit_pilot_state->recheck_status <= 0 || unit_pilot_state->resume_pulses) &&
			(unit_pilot_state->resume_pulses ||
			 unit_pilot_state->candidate < unit_pilot_candidate_limit());
		unit_pilot_state->resume_candidate = unit_pilot_state->candidate;
		unit_pilot_state->resume_exit_variant = unit_pilot_state->exit_variant;
		unit_pilot_state->resume_secondary_candidate = unit_pilot_state->secondary_candidate;
		unit_pilot_state->resume_secondary_ramp_variant = unit_pilot_state->secondary_ramp_variant;
		unit_pilot_state->resume_barrel_variant = unit_pilot_state->barrel_variant;
		unit_pilot_state->resume_ramp_variant = unit_pilot_state->ramp_variant;
		unit_pilot_state->resume_barrel_reached = unit_pilot_state->barrel_reached;
		unit_pilot_state->resume_barrel_best_progress = unit_pilot_state->barrel_best_progress;
		unit_pilot_state->resume_barrel_best_speed = unit_pilot_state->barrel_best_speed;
		unit_pilot_state->stats.active = 0;
		unit_pilot_state->recheck_status = -1;
		return -1;
	}
	if (unit_pilot_state->recheck_status > 0 && frame < unit_pilot_state->handoff_frame) {
		return 0;
	}
	return unit_pilot_state->recheck_status;
}
#endif
