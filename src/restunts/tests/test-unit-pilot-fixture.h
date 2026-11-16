#ifndef RESTUNTS_TEST_UNIT_PILOT_FIXTURE_H
#define RESTUNTS_TEST_UNIT_PILOT_FIXTURE_H

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL_timer.h>
#include "../c/opponent/unit_pilot.h"
#include "../c/opponent/unit_learning_cache.h"
#include "../c/opponent/unit_opponent.h"
#include "../c/opponent/unit_opponent_internal.h"
#include "../c/extended/extended_physics_snapshot.h"
#include "../c/extended/extended_race_stats.h"
#include "../c/externs.h"
#include "../c/crash_state.h"
#include "../c/replay/replay.h"
#include "../c/race/race_stats.h"
#include "../c/track/trackdata_layout.h"
#include "../c/owoot/owoot.h"
#include "../c/owoot/owoot_road.h"

/* Trial scheduling uses a deterministic native-step fixture. Full-engine
 * replay tests separately exercise real physics/audio isolation. */
struct UNIT_PLANNER {
	legacy_u32 calls;
	struct UNIT_PLANNER_PROFILE profile;
};
struct GAMESTATE state;
struct GAMEINFO gameconfig;
struct EXTENDED_REPLAY_CLOCKS extended_replay_clocks, extended_race_stats;
legacy_s16 gState_impactSpeed, gState_jumpCount, gState_oEndFrame;
legacy_s16 gState_opponent_finish_time, gState_pEndFrame, gState_penalty;
legacy_s16 gState_topSpeed, gState_total_finish_time, owoot_enabled;
legacy_s32 gState_travDist;
legacy_u16 gState_frame, framespersec;
legacy_s8 is_in_replay, replay_recording_flags;
legacy_u8 game_replay_mode;
static struct UNIT_ROUTE route;
static struct UNIT_ROUTE_POINT points[128];
static legacy_f64 speeds[4], preview_exit_fraction, preview_secondary_speed;
static struct UNIT_EXIT_PROFILE exits[4];
static legacy_f64 barrel_offsets[4], preview_barrel_offset, last_native_speed;
static legacy_int reject_barrel_offset;
static legacy_f64 required_barrel_offset, preview_exit_speed;
static legacy_f64 required_exit_fraction, required_exit_ratio;
static legacy_int reject_barrel_phase, reject_barrel_combination, reject_barrel_refinement;
static legacy_f64 required_refined_speed, required_local_trim, required_local_phase;
static legacy_int reject_barrel_local;
static legacy_u8 preview_input_mask, preview_input_value;
static legacy_u8 ramp_approaches[4], preview_ramp_approaches[4], required_ramp_approach;
static legacy_int reject_ramp_approach, reject_secondary_ramp;
static legacy_u32 waited_frames, fail_ordinary_at;
static legacy_u64 fake_time, clock_step;
static legacy_u32 injected_penalty, accepts, resets;
static legacy_int reject_fast, reject_everything, finish_event, reject_group, hold_launch;
static legacy_int reject_secondary, reject_receiver;
static legacy_s16 observed_owner;
static legacy_int native_road_support;
static struct CSRPL_OPPONENT_COMMAND last_command;
static legacy_int learning_motion;
static legacy_u32 replayed_commands, rejected_replay_frame;

Uint64 SDLCALL SDL_GetTicksNS(void)
{
	fake_time += clock_step;
	return fake_time;
}
void extended_physics_capture(struct EXTENDED_PHYSICS_SNAPSHOT *s)
{
	memset(s, 0, sizeof(*s));
	s->state = state;
	s->clocks = extended_replay_clocks;
}
void extended_physics_restore(const struct EXTENDED_PHYSICS_SNAPSHOT *s)
{
	state = s->state;
	extended_replay_clocks = s->clocks;
}
legacy_int extended_replay_is_active(void)
{
	return 1;
}
legacy_u32 extended_replay_frame(void)
{
	return extended_replay_clocks.frame;
}
void extended_replay_after_step(void)
{
	extended_replay_clocks.frame++;
	gameconfig.game_recordedframes++;
}
void extended_checkpoint_begin_step(void)
{
}
void extended_checkpoint_end_step(void)
{
	if (observed_owner >= 0) {
		extended_replay_clocks.unit_penalty.initialized = 1;
		extended_replay_clocks.unit_penalty.route.previous_route = observed_owner;
	}
}
legacy_u16 owoot_wheel_footprint(const struct CARSTATE *car, legacy_u16 wheel,
								 struct VECTOR output[OWOOT_WHEEL_VERTEX_COUNT])
{
	(void)car;
	memset(output, 0, sizeof(struct VECTOR) * OWOOT_WHEEL_VERTEX_COUNT);
	output[0].x = (legacy_s16)wheel;
	return OWOOT_WHEEL_VERTEX_COUNT;
}
legacy_s16 track_road_overlaps_wheel(const struct VECTOR *vertices, legacy_u16 ring_count,
									 legacy_s16 road_contact)
{
	assert(ring_count == OWOOT_WHEEL_RING_COUNT && road_contact);
	return (legacy_s16)((native_road_support & (1U << vertices[0].x)) != 0);
}
legacy_u16 extended_owoot_barrel_progress(const struct CARSTATE *car, legacy_s16 source)
{
	if ((!reject_barrel_refinement && !reject_barrel_local) || source != 1 ||
		car->car_reserved_route_word1 < 45) {
		return 0;
	}
	if (fabs(last_native_speed - 99.5) < 0.000001) {
		return 1005;
	}
	return fabs(last_native_speed - 100.0) < 0.000001 ? 1001 : 900;
}
legacy_int extended_unit_penalty_recovery_required(void)
{
	return 0;
}
legacy_u32 extended_unit_penalty_total(void)
{
	return injected_penalty;
}
const struct UNIT_ROUTE *unit_opponent_pilot_route(void)
{
	return &route;
}
struct UNIT_PLANNER *unit_planner_clone_sync(struct UNIT_PLANNER *source)
{
	struct UNIT_PLANNER *clone = source ? calloc(1, sizeof(*source)) : NULL;
	if (clone) {
		clone->profile = source->profile;
	}
	return clone;
}
legacy_int unit_planner_export_profile(struct UNIT_PLANNER *source,
									   struct UNIT_PLANNER_PROFILE *profile)
{
	*profile = source->profile;
	profile->lookahead = 1.0;
	return 1;
}
struct UNIT_PLANNER *unit_planner_clone_profile(struct UNIT_PLANNER *source,
												const struct UNIT_PLANNER_PROFILE *profile)
{
	struct UNIT_PLANNER *clone = unit_planner_clone_sync(source);
	if (clone) {
		clone->profile = *profile;
	}
	return clone;
}
struct UNIT_PLANNER *unit_planner_clone_candidate(struct UNIT_PLANNER *source, legacy_u64 sample)
{
	struct UNIT_PLANNER *clone = unit_planner_clone_sync(source);
	if (clone) {
		clone->profile.lane = sample == 0 ? 1.0 : (sample == 1 ? -1.0 : 0.0);
	}
	return clone;
}
struct UNIT_PLANNER *unit_planner_clone_continuation(struct UNIT_PLANNER *source)
{
	struct UNIT_PLANNER *clone = unit_planner_clone_sync(source);
	if (clone) {
		*clone = *source;
		resets++;
	}
	return clone;
}
legacy_int unit_planner_restore_continuation(struct UNIT_PLANNER *destination,
											 struct UNIT_PLANNER *source)
{
	assert(destination && source);
	*destination = *source;
	resets++;
	return 1;
}
void unit_planner_destroy(struct UNIT_PLANNER *p)
{
	free(p);
}
legacy_f64 unit_bounded(legacy_f64 v, legacy_f64 lo, legacy_f64 hi)
{
	return fmax(lo, fmin(v, hi));
}
legacy_u32 unit_route_cursor(const struct UNIT_ROUTE *unused, const struct CARSTATE *car)
{
	(void)unused;
	return (legacy_u16)car->car_reserved_route_word1;
}
legacy_u32 unit_route_advance(const struct UNIT_ROUTE *unused, const struct CARSTATE *car,
							  legacy_u32 cursor)
{
	(void)unused;
	(void)car;
	return cursor;
}
void unit_opponent_pilot_last_command(struct CSRPL_OPPONENT_COMMAND *command)
{
	*command = last_command;
}
void unit_opponent_pilot_input_override(legacy_u8 mask, legacy_u8 value)
{
	preview_input_mask = mask;
	preview_input_value = value;
}
void unit_opponent_pilot_ramp_override(legacy_s16 piece, legacy_u8 tiles)
{
	if (piece < 0) {
		memset(preview_ramp_approaches, 0, sizeof(preview_ramp_approaches));
	} else {
		assert(piece < 4);
		preview_ramp_approaches[piece] = tiles;
	}
}
void unit_opponent_pilot_accept_ramp(legacy_s16 piece, legacy_u8 tiles)
{
	assert(piece >= 0 && piece < 4);
	ramp_approaches[piece] = tiles;
}
void unit_opponent_pilot_barrel_override(legacy_s16 piece, legacy_f64 fraction)
{
	preview_barrel_offset = piece >= 0 ? fraction : 0.0;
}
void unit_opponent_pilot_accept_barrel(legacy_s16 piece, legacy_f64 fraction)
{
	barrel_offsets[piece] = fraction;
}
void unit_opponent_pilot_secondary_override(legacy_s16 piece, legacy_f64 speed)
{
	preview_secondary_speed = piece >= 0 ? speed : 0.0;
}
void unit_opponent_pilot_exit_override(legacy_s16 piece, legacy_f64 fraction, legacy_f64 speed)
{
	preview_exit_speed = piece >= 0 ? speed : 0.0;
	preview_exit_fraction = piece >= 0 ? fraction : 0.0;
}
void unit_opponent_pilot_accept_exit(legacy_s16 piece, legacy_f64 fraction, legacy_f64 speed)
{
	exits[piece].fraction = fraction;
	exits[piece].speed = speed;
}
legacy_s16 unit_opponent_pilot_wait_step(legacy_u32 frame)
{
	assert(is_in_replay && extended_replay_clocks.frame == frame + 1U);
	state.opponentstate.car_previous_position = state.opponentstate.car_position;
	state.opponentstate.car_position.lx--;
	waited_frames++;
	return 0;
}
void unit_opponent_pilot_accept(legacy_s16 piece, legacy_f64 speed)
{
	assert(piece >= 0 && piece < 4);
	speeds[piece] = speed;
	accepts++;
}
legacy_s16 unit_opponent_pilot_step(struct UNIT_PLANNER *p, legacy_u32 frame, legacy_s16 piece,
									legacy_f64 speed)
{
	assert(is_in_replay == 1 && (replay_recording_flags & REPLAY_RECORDING_RESTARTABLE_FLAG));
	assert(game_replay_mode != REPLAY_MODE_PAUSED);
	assert(state.game_inputmode == GAME_INPUT_MODE_ACTIVE);
	assert(state.playerstate.car_position.lx < 0 && state.playerstate.car_position.lz < 0);
	assert(frame == p->calls++ + waited_frames && extended_replay_clocks.frame == frame + 1U);
	last_native_speed = speed;
	memset(&last_command, 0, sizeof(last_command));
	last_command.route_index = 1;
	last_command.route_point = (legacy_u16)frame;
	state.opponentstate.car_reserved_route_word1++;
	if (learning_motion && p->profile.lane > 0.0) {
		state.opponentstate.car_reserved_route_word1++;
		last_command.input = 1;
	}
	legacy_u32 cursor = (legacy_u16)state.opponentstate.car_reserved_route_word1;
	state.opponentstate.car_route_index =
		points[cursor < route.count ? cursor : route.count - 1U].route_index;
	state.opponentstate.car_position.lz = state.opponentstate.car_reserved_route_word1 * 100 * 64;
	if (hold_launch && state.opponentstate.car_reserved_route_word1 >= 42) {
		state.opponentstate.car_position.lz = 4090 * 64;
	}
	for (legacy_u16 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
		state.opponentstate.car_surfaceWhl[wheel] = CAR_SURFACE_PAVED;
	}
	gState_frame = 123;
	extended_race_stats.frame = 123;
	if ((learning_motion && p->profile.lane < 0.0) ||
		(fail_ordinary_at && piece < 0 && cursor >= fail_ordinary_at) ||
		(reject_everything && piece >= 0) || (reject_fast && piece >= 0 && speed > 99.0) ||
		(reject_ramp_approach && piece >= 0 &&
		 (preview_ramp_approaches[piece] != required_ramp_approach || speed > 99.0)) ||
		(reject_secondary_ramp && state.opponentstate.car_reserved_route_word1 >= 50 &&
		 piece >= 0 &&
		 (preview_ramp_approaches[3] != required_ramp_approach || preview_secondary_speed <= 0.0 ||
		  preview_secondary_speed > 99.0)) ||
		(reject_barrel_local && state.opponentstate.car_reserved_route_word1 >= 46 && piece >= 0 &&
		 (speed != 100.0 || preview_barrel_offset != required_local_trim ||
		  preview_exit_fraction != required_local_phase)) ||
		(reject_barrel_refinement && state.opponentstate.car_reserved_route_word1 >= 46 &&
		 piece >= 0 && fabs(speed - required_refined_speed) > 0.000001) ||
		(reject_barrel_combination && state.opponentstate.car_reserved_route_word1 >= 46 &&
		 piece >= 0 &&
		 (preview_barrel_offset != 0.25 || preview_exit_fraction != 0.921875 ||
		  fabs(preview_exit_speed - speed * 0.85) > 0.000001 || speed > 99.0)) ||
		(reject_barrel_phase && state.opponentstate.car_reserved_route_word1 >= 46 && piece >= 0 &&
		 (preview_exit_fraction != required_exit_fraction ||
		  fabs(preview_exit_speed - speed * required_exit_ratio) > 0.000001 || speed > 99.0)) ||
		(reject_barrel_offset && state.opponentstate.car_reserved_route_word1 >= 46 && piece >= 0 &&
		 (preview_exit_fraction > 0.0 || preview_barrel_offset != required_barrel_offset ||
		  speed > 99.0)) ||
		(reject_secondary && state.opponentstate.car_reserved_route_word1 >= 50 && piece >= 0 &&
		 (preview_secondary_speed <= 0.0 || preview_secondary_speed > 99.0)) ||
		(reject_receiver && state.opponentstate.car_reserved_route_word1 >= 46 && piece >= 0 &&
		 speed > 99.0) ||
		(reject_group && state.opponentstate.car_reserved_route_word1 >= 50 && piece >= 0 &&
		 speed > 99.0 && preview_exit_fraction <= 0.0)) {
		state.opponentstate.car_crashBmpFlag = CRASH_EVENT_COLLISION;
	} else if (finish_event &&
			   (legacy_u16)state.opponentstate.car_reserved_route_word1 >= route.finish_begin) {
		state.opponentstate.car_crashBmpFlag = CRASH_EVENT_FINISH;
	}
	return state.opponentstate.car_crashBmpFlag;
}
legacy_s16 unit_opponent_pilot_replay_step(legacy_u32 frame,
										   const struct CSRPL_OPPONENT_COMMAND *command)
{
	struct UNIT_PLANNER planner = {.calls = frame};
	planner.profile.lane = command->input ? 1.0 : 0.0;
	replayed_commands++;
	legacy_s16 event = unit_opponent_pilot_step(&planner, frame, ROUTE_INDEX_NONE, 0.0);
	if (rejected_replay_frame && replayed_commands == rejected_replay_frame) {
		state.opponentstate.car_crashBmpFlag = CRASH_EVENT_COLLISION;
		return CRASH_EVENT_COLLISION;
	}
	return event;
}
static void reset_fixture(legacy_int stunt)
{
	unit_pilot_stop();
	memset(&state, 0, sizeof(state));
	memset(&gameconfig, 0, sizeof(gameconfig));
	memset(&extended_replay_clocks, 0, sizeof(extended_replay_clocks));
	memset(&extended_race_stats, 0, sizeof(extended_race_stats));
	memset(points, 0, sizeof(points));
	memset(speeds, 0, sizeof(speeds));
	memset(exits, 0, sizeof(exits));
	memset(barrel_offsets, 0, sizeof(barrel_offsets));
	memset(ramp_approaches, 0, sizeof(ramp_approaches));
	memset(preview_ramp_approaches, 0, sizeof(preview_ramp_approaches));
	required_ramp_approach = 3;
	reject_ramp_approach = reject_secondary_ramp = 0;
	preview_barrel_offset = last_native_speed = 0.0;
	reject_barrel_offset = 0;
	required_barrel_offset = 0.25;
	preview_exit_speed = 0.0;
	reject_barrel_phase = reject_barrel_combination = reject_barrel_refinement = 0;
	required_refined_speed = 99.5;
	reject_barrel_local = 0;
	required_exit_fraction = 0.875;
	required_exit_ratio = 0.90;
	waited_frames = fail_ordinary_at = 0;
	preview_input_mask = preview_input_value = 0;
	preview_exit_fraction = preview_secondary_speed = 0.0;
	memset(&route, 0, sizeof(route));
	route.points = points;
	route.count = 128;
	route.finish_begin = stunt ? 70 : 127;
	route.verified_speeds = speeds;
	route.verified_exits = exits;
	route.verified_barrel_offsets = barrel_offsets;
	route.verified_ramp_approaches = ramp_approaches;
	route.verified_count = 4;
	route.limits.max_speed = 200;
	for (legacy_u32 i = 0; i < route.count; i++) {
		points[i].z = i * 100.0;
		points[i].speed_limit = 100.0;
		points[i].route_index = i < 40 ? 0 : (i < 42 ? 1 : (i < 48 ? 2 : 3));
	}
	if (stunt) {
		points[40].stunt = points[41].stunt = UNIT_STUNT_BARREL;
	}
	gState_frame = 7;
	is_in_replay = 0;
	replay_recording_flags = 0;
	game_replay_mode = REPLAY_MODE_PAUSED;
	framespersec = GAME_FRAME_RATE_NORMAL;
	owoot_enabled = 1;
	fake_time = clock_step = injected_penalty = accepts = resets = 0;
	reject_fast = reject_everything = finish_event = reject_group = hold_launch = 0;
	reject_secondary = reject_receiver = 0;
	observed_owner = -1;
	native_road_support = (1U << CARSTATE_WHEEL_COUNT) - 1U;
	learning_motion = 0;
	replayed_commands = rejected_replay_frame = 0;
}
static void assert_live_unchanged(const struct GAMESTATE *before)
{
	assert(memcmp(&state, before, sizeof(state)) == 0);
	assert(extended_replay_clocks.frame == 0 && gameconfig.game_recordedframes == 0);
	assert(gState_frame == 7 && extended_race_stats.frame == 0);
	assert(is_in_replay == 0 && replay_recording_flags == 0 &&
		   game_replay_mode == REPLAY_MODE_PAUSED);
}

#endif
