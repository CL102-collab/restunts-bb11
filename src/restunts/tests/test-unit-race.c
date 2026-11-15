#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "../genghost/simulation.h"
#include "../c/externs.h"
#include "../c/game_input.h"
#include "../c/crash_state.h"
#include "../c/owoot/owoot.h"
#include "../c/state_internal.h"
#include "../c/race/race_route.h"
#include "../c/track/track_objects.h"
#include "../c/opponent/opponent.h"
#include "../c/opponent/unit_opponent.h"
#include "../c/opponent/unit_pilot.h"
#include "../c/opponent/unit_pilot_pulses.h"
#include "../c/opponent/unit_opponent_internal.h"
#include "../c/extended/extended_track.h"
#include "../c/extended/extended_owoot_internal.h"
#include "../c/extended/extended_replay_runtime.h"

#undef memcpy
#undef strlen
#undef printf

#define UNIT_BENCHMARK_DEFAULT_FRAMES 12000U
#define UNIT_BENCHMARK_MAXIMUM_TRACK_BYTES (16U * 1024U * 1024U)
#define UNIT_BENCHMARK_WORLD_SCALE 64.0
#define UNIT_BENCHMARK_SPEED_SCALE 256.0
#define UNIT_BENCHMARK_PARK_POSITION 128L
#define UNIT_BENCHMARK_PARK_SCALE 64L
#define UNIT_BENCHMARK_CORNER_ANGLE 8
#define UNIT_BENCHMARK_SPEED_MINIMUM 1U
#define UNIT_BENCHMARK_HASH_OFFSET UINT64_C(14695981039346656037)
#define UNIT_BENCHMARK_HASH_PRIME UINT64_C(1099511628211)
#define UNIT_BENCHMARK_DEBUG_POINTS 5000U
#define UNIT_BENCHMARK_FAILURE_TRACE_POINTS 128U
/* Serialized roll momentum word from extended_stateply.c. */
#define UNIT_BENCHMARK_ROLL_VELOCITY_WORD 0U
#define UNIT_BENCHMARK_HUMAN_DISTANCE 1024.0
#define UNIT_BENCHMARK_HUMAN_LATERAL 200.0
#define UNIT_BENCHMARK_HUMAN_START_SECONDS 2U
#define UNIT_BENCHMARK_GRID_DRIVE_FRAMES 60U
#define UNIT_BENCHMARK_PASS_DISTANCE 112.0
#define UNIT_BENCHMARK_NO_HUMAN_DISTANCE 1000000.0
#define UNIT_BENCHMARK_NS_PER_SECOND UINT64_C(1000000000)
#define UNIT_BENCHMARK_NS_PER_MICROSECOND 1000.0
#define UNIT_BENCHMARK_NS_PER_MILLISECOND 1000000.0
#define UNIT_BENCHMARK_MAX_PLANNER_YIELD_MS 50U

enum UNIT_BENCHMARK_HUMAN {
	UNIT_BENCHMARK_HUMAN_PARKED,
	UNIT_BENCHMARK_HUMAN_STATIONARY,
	UNIT_BENCHMARK_HUMAN_ACCELERATING,
	UNIT_BENCHMARK_HUMAN_CROSSING,
	UNIT_BENCHMARK_HUMAN_GRID,
	UNIT_BENCHMARK_HUMAN_GRID_THEN_PARK
};

static legacy_u64 state_hash(void)
{
	legacy_u8 bytes[GAMESTATE_SERIALIZED_SIZE];
	legacy_s8 seed[GAMESTATE_RANDOM_SEED_SIZE];
	assert(gamestate_serialize(bytes, &state) == sizeof(bytes));
	get_kevinrandom_seed(seed);
	legacy_u64 hash = UNIT_BENCHMARK_HASH_OFFSET;
	for (size_t index = 0; index < sizeof(bytes); index++) {
		hash = (hash ^ bytes[index]) * UNIT_BENCHMARK_HASH_PRIME;
	}
	for (size_t index = 0; index < sizeof(seed); index++) {
		hash = (hash ^ (legacy_u8)seed[index]) * UNIT_BENCHMARK_HASH_PRIME;
	}
	/* input_frame is the live writer cursor; silent playback has no writer. */
	const struct EXTENDED_REPLAY_CLOCKS *clocks = &extended_replay_clocks;
	const struct RACE_PENALTY_STATE *rules = &clocks->unit_penalty.route;
	const legacy_u32 fields[] = {clocks->frame,
								 clocks->player_finish,
								 clocks->opponent_finish,
								 clocks->player_end,
								 clocks->penalty,
								 clocks->unit_penalty.frames,
								 clocks->unit_penalty.initialized,
								 (legacy_u32)rules->confirmed_route,
								 (legacy_u32)rules->previous_route,
								 (legacy_u32)rules->last_skipped,
								 (legacy_u32)rules->minimum_column,
								 (legacy_u32)rules->maximum_column,
								 (legacy_u32)rules->minimum_row,
								 (legacy_u32)rules->maximum_row,
								 (legacy_u32)rules->status,
								 (legacy_u32)rules->confirmations,
								 (legacy_u32)rules->laps,
								 clocks->checkpoints.initialized};
	for (size_t index = 0; index < sizeof(fields) / sizeof(fields[0]); index++) {
		hash = (hash ^ fields[index]) * UNIT_BENCHMARK_HASH_PRIME;
	}
	for (legacy_u32 car = 0; car < EXTENDED_CHECKPOINT_CAR_COUNT; car++) {
		for (legacy_u32 index = 0; index < EXTENDED_CHECKPOINT_VISITED_BYTES; index++) {
			hash = (hash ^ clocks->checkpoints.visited[car][index]) * UNIT_BENCHMARK_HASH_PRIME;
		}
		for (legacy_u32 color = 0; color < EXTENDED_CHECKPOINT_COLOR_COUNT; color++) {
			hash = (hash ^ clocks->checkpoints.remaining[car][color]) * UNIT_BENCHMARK_HASH_PRIME;
		}
		hash =
			(hash ^ (legacy_u32)clocks->checkpoints.previous[car].lx) * UNIT_BENCHMARK_HASH_PRIME;
		hash =
			(hash ^ (legacy_u32)clocks->checkpoints.previous[car].ly) * UNIT_BENCHMARK_HASH_PRIME;
		hash =
			(hash ^ (legacy_u32)clocks->checkpoints.previous[car].lz) * UNIT_BENCHMARK_HASH_PRIME;
	}
	return hash;
}

/* Follow the opponent through the native packed player penalty path without
 * changing the live race. Compare this separate history with UNIT accounting. */
static void shadow_penalty_step(struct GAMESTATE *shadow, legacy_u32 *penalty_frames)
{
	struct GENGHOST_SNAPSHOT live;
	genghost_sim_capture(&live);
	state = *shadow;
	state.playerstate = live.state.opponentstate;
	state.game_frame = live.state.game_frame;
	extended_replay_clocks.penalty = *penalty_frames;
	legacy_s8 previous_status = state.game_player_route_status;
	update_player_route_penalty();
	update_player_route_guidance(previous_status);
	*shadow = state;
	*penalty_frames = extended_replay_is_active() ? extended_replay_clocks.penalty
												  : (legacy_u16)state.game_penalty;
	genghost_sim_restore(&live);
}

static void debug_pilot_pulse(void)
{
	static legacy_u32 previous_attempts;
	struct UNIT_PILOT_PULSE_DIAGNOSTIC diagnostic;
	if (!unit_pilot_last_pulse(&diagnostic) || !diagnostic.active) {
		previous_attempts = 0;
		return;
	}
	if (diagnostic.attempts == previous_attempts) {
		return;
	}
	previous_attempts = diagnostic.attempts;
	const struct EXTENDED_BARREL_PARAMETERS *parameters = &diagnostic.parameters;
	fprintf(stderr,
			"pilot_pulse=%u frame=%u span=%u bank=%u fitness=%.3f progress=%u "
			"begin=%u duration=%u gap=%u counter=%u pedal_begin=%u pedal_duration=%u "
			"side=%u pedal=%u air_brake=%u relative_speed=%.6f relative_lateral=%.6f "
			"profile_speed=%.6f profile_offset=%.6f profile_exit_fraction=%.6f "
			"profile_exit_speed=%.6f profile_secondary_speed=%.6f "
			"profile_secondary_ramp_approach=%u\n",
			diagnostic.attempts, genghost_sim_frame(), diagnostic.span, diagnostic.bank,
			diagnostic.fitness, diagnostic.progress, parameters->begin, parameters->duration,
			parameters->gap, parameters->counter, parameters->pedal_begin,
			parameters->pedal_duration, parameters->side, parameters->pedal, parameters->air_brake,
			parameters->speed, parameters->lateral, diagnostic.profile.speed,
			diagnostic.profile.barrel_offset, diagnostic.profile.exit_fraction,
			diagnostic.profile.exit_speed, diagnostic.profile.secondary_speed,
			diagnostic.profile.secondary_ramp_approach);
}

static void debug_pilot_failure(legacy_u32 *previous_failures, FILE *trace, FILE *entry_trace)
{
	debug_pilot_pulse();
	struct UNIT_PILOT_STATS stats;
	unit_pilot_stats(&stats);
	if (stats.failed_trials == *previous_failures) {
		return;
	}
	*previous_failures = stats.failed_trials;
	struct CARSTATE car;
	legacy_f64 target;
	legacy_s16 piece;
	if (unit_pilot_last_failure(&car, &target, &piece)) {
		fprintf(stderr,
				"pilot_failure=%u frame=%u piece=%d event=%d target=%.3f exit_variant=%u "
				"secondary_piece=%d secondary_speed=%.3f barrel_offset=%.3f "
				"exit_fraction=%.3f exit_speed=%.3f barrel_progress=%u ramp_approach=%u "
				"secondary_ramp_approach=%u "
				"speed=%.3f last_speed=%.3f "
				"x=%.3f y=%.3f z=%.3f route=%d.%d yaw=%d pitch=%d roll=%d contact=%d\n",
				stats.failed_trials, genghost_sim_frame(), piece, stats.last_failure, target,
				stats.last_failure_exit_variant, stats.last_failure_secondary_piece,
				stats.last_failure_secondary_speed, stats.last_failure_barrel_offset,
				stats.last_failure_exit_fraction, stats.last_failure_exit_speed,
				stats.last_failure_barrel_progress, stats.last_failure_ramp_approach,
				stats.last_failure_secondary_ramp_approach,
				car.car_actual_speed / UNIT_BENCHMARK_SPEED_SCALE,
				car.car_lastspeed / UNIT_BENCHMARK_SPEED_SCALE,
				car.car_position.lx / UNIT_BENCHMARK_WORLD_SCALE,
				car.car_position.ly / UNIT_BENCHMARK_WORLD_SCALE,
				car.car_position.lz / UNIT_BENCHMARK_WORLD_SCALE, car.car_route_index,
				car.car_route_point_index, car.car_rotate.x, car.car_rotate.y, car.car_rotate.z,
				car.car_sumSurfAllWheels);
		struct CARSTATE entry;
		if (unit_pilot_last_failure_entry(&entry)) {
			fprintf(stderr,
					"pilot_guide_entry=%u frame=%u piece=%d target=%.3f speed=%.3f "
					"x=%.3f y=%.3f z=%.3f yaw=%d pitch=%d roll=%d steering=%d "
					"velocity_heading_offset=%d contact=%d "
					"vx=%.3f vy=%.3f vz=%.3f\n",
					stats.failed_trials, genghost_sim_frame(), piece, target,
					entry.car_actual_speed / UNIT_BENCHMARK_SPEED_SCALE,
					entry.car_position.lx / UNIT_BENCHMARK_WORLD_SCALE,
					entry.car_position.ly / UNIT_BENCHMARK_WORLD_SCALE,
					entry.car_position.lz / UNIT_BENCHMARK_WORLD_SCALE, entry.car_rotate.x,
					entry.car_rotate.y, entry.car_rotate.z, entry.car_steeringAngle,
					entry.car_velocity_heading_offset, entry.car_sumSurfAllWheels,
					(entry.car_position.lx - entry.car_previous_position.lx) * framespersec /
						UNIT_BENCHMARK_WORLD_SCALE,
					(entry.car_position.ly - entry.car_previous_position.ly) * framespersec /
						UNIT_BENCHMARK_WORLD_SCALE,
					(entry.car_position.lz - entry.car_previous_position.lz) * framespersec /
						UNIT_BENCHMARK_WORLD_SCALE);
		}
		if (entry_trace != NULL) {
			for (legacy_u32 after = 0; after < UNIT_BENCHMARK_FAILURE_TRACE_POINTS; after++) {
				if (!unit_pilot_failure_entry_car(after, &entry)) {
					break;
				}
				fprintf(entry_trace,
						"%u,%u,%u,%d,%.3f,%u,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%d,%d,%"
						"d,%d,%d,%d,%u,%.3f,%d,%d,%.3f,%.3f,%u,%u\n",
						stats.failed_trials, genghost_sim_frame(), after, piece, target,
						stats.last_failure_exit_variant, stats.last_failure_secondary_piece,
						stats.last_failure_secondary_speed,
						entry.car_position.lx / UNIT_BENCHMARK_WORLD_SCALE,
						entry.car_position.ly / UNIT_BENCHMARK_WORLD_SCALE,
						entry.car_position.lz / UNIT_BENCHMARK_WORLD_SCALE,
						entry.car_actual_speed / UNIT_BENCHMARK_SPEED_SCALE, entry.car_route_index,
						entry.car_route_point_index, entry.car_rotate.x, entry.car_rotate.y,
						entry.car_rotate.z, entry.car_sumSurfAllWheels, entry.car_steeringAngle,
						entry.car_velocity_heading_offset, entry.car_is_accelerating,
						entry.car_is_braking,
						entry.car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_SOURCE],
						entry.car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_TARGET],
						(legacy_u16)entry.car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_LAUNCH],
						stats.last_failure_barrel_offset,
						entry.car_reserved_contact_state[UNIT_BENCHMARK_ROLL_VELOCITY_WORD],
						entry.car_sumSurfRearWheels, stats.last_failure_exit_fraction,
						stats.last_failure_exit_speed, stats.last_failure_ramp_approach,
						stats.last_failure_secondary_ramp_approach);
			}
		}
		if (trace != NULL) {
			for (legacy_s32 before = UNIT_BENCHMARK_FAILURE_TRACE_POINTS; before >= 0; before--) {
				if (!unit_pilot_failure_car((legacy_u32)before, &car)) {
					continue;
				}
				fprintf(trace,
						"%u,%u,%u,%d,%d,%.3f,%u,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%d,%"
						"d,%d,%d,%d,%u,%.3f,%d,%d,%.3f,%.3f,%u,%u\n",
						stats.failed_trials, genghost_sim_frame(),
						stats.last_failure_frame - (legacy_u32)before, before, piece, target,
						stats.last_failure_exit_variant, stats.last_failure_secondary_piece,
						stats.last_failure_secondary_speed,
						car.car_position.lx / UNIT_BENCHMARK_WORLD_SCALE,
						car.car_position.ly / UNIT_BENCHMARK_WORLD_SCALE,
						car.car_position.lz / UNIT_BENCHMARK_WORLD_SCALE,
						car.car_actual_speed / UNIT_BENCHMARK_SPEED_SCALE, car.car_route_index,
						car.car_route_point_index, car.car_rotate.x, car.car_rotate.y,
						car.car_rotate.z, car.car_sumSurfAllWheels, car.car_steeringAngle,
						car.car_is_accelerating, car.car_is_braking,
						car.car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_SOURCE],
						car.car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_TARGET],
						(legacy_u16)car.car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_LAUNCH],
						stats.last_failure_barrel_offset,
						car.car_reserved_contact_state[UNIT_BENCHMARK_ROLL_VELOCITY_WORD],
						car.car_sumSurfRearWheels, stats.last_failure_exit_fraction,
						stats.last_failure_exit_speed, stats.last_failure_ramp_approach,
						stats.last_failure_secondary_ramp_approach);
			}
		}
	}
}

static void debug_pilot_entries(void)
{
	const struct UNIT_ROUTE *route = unit_opponent_pilot_route();
	if (!route) {
		return;
	}
	for (legacy_u32 piece = 0; piece < route->verified_count; piece++) {
		struct CARSTATE car;
		legacy_f64 speed;
		if (!unit_pilot_expected_entry((legacy_s16)piece, &car, &speed)) {
			continue;
		}
		fprintf(stderr,
				"pilot_entry=%u target=%.3f speed=%.3f x=%.3f y=%.3f z=%.3f "
				"yaw=%d pitch=%d roll=%d steering=%d gear=%d rpm=%d ramp_approach=%u\n",
				piece, speed, car.car_actual_speed / UNIT_BENCHMARK_SPEED_SCALE,
				car.car_position.lx / UNIT_BENCHMARK_WORLD_SCALE,
				car.car_position.ly / UNIT_BENCHMARK_WORLD_SCALE,
				car.car_position.lz / UNIT_BENCHMARK_WORLD_SCALE, car.car_rotate.x,
				car.car_rotate.y, car.car_rotate.z, car.car_steeringAngle, car.car_current_gear,
				car.car_currpm,
				route->verified_ramp_approaches ? route->verified_ramp_approaches[piece] : 0);
	}
}

static legacy_u8 *read_track(const legacy_char *path, legacy_uint *size)
{
	FILE *source = fopen(path, "rb");
	assert(source != NULL);
	assert(fseek(source, 0, SEEK_END) == 0);
	legacy_s64 length = ftell(source);
	assert(length > 0 && (legacy_u64)length <= UNIT_BENCHMARK_MAXIMUM_TRACK_BYTES);
	assert(fseek(source, 0, SEEK_SET) == 0);
	legacy_u8 *data = malloc((size_t)length);
	assert(data != NULL);
	assert(fread(data, 1, (size_t)length, source) == (size_t)length);
	assert(fclose(source) == 0);
	*size = (legacy_uint)length;
	return data;
}

legacy_s16 stuntsmain(legacy_s16 argc, legacy_s8 **argv)
{
	if (argc < 3) {
		fprintf(
			stderr,
			"Usage: test-unit-race [--data-dir DIR] TRACK 6|7 "
			"[--prepare-ms N] [--prepare-until-plan] [--frames N] [--trace FILE] [--verify-replay] "
			"[--planner-yield-ms N] [--planner-sync] [--require-worker] "
			"[--pilot-debug] [--pilot-trace FILE] [--pilot-entry-trace FILE] [--car ID] "
			"[--manual] [--frame-rate 10|20] [--require-finish] "
			"[--require-clean] [--human "
			"stationary|accelerating|crossing|grid|grid-then-park] "
			"[--owoot:on]\n");
		return 1;
	}
	legacy_u8 opponent = (legacy_u8)strtoul((legacy_char *)argv[2], NULL, 10);
	assert(opponent == OPPONENT_LEGACY_LAST || opponent == OPPONENT_UNIT);
	legacy_u32 frame_limit = UNIT_BENCHMARK_DEFAULT_FRAMES;
	legacy_u16 frame_rate = GENGHOST_FRAME_RATE;
	legacy_u32 preparation_ms = 0;
	legacy_u32 planner_yield_ms = 0;
	legacy_int verify = 0;
	legacy_int route_debug = 0;
	legacy_int pilot_debug = 0;
	legacy_int require_finish = 0;
	legacy_int require_clean = 0;
	legacy_int require_worker = 0;
	legacy_int planner_sync = 0;
	legacy_int manual = 0;
	const legacy_char *car_id = "PMIN";
	legacy_int human = UNIT_BENCHMARK_HUMAN_PARKED;
	legacy_int prepare_until_plan = 0;
	FILE *trace = NULL;
	FILE *pilot_trace = NULL;
	FILE *pilot_entry_trace = NULL;
	for (legacy_s16 index = 3; index < argc; index++) {
		if (strcmp((legacy_char *)argv[index], "--frames") == 0 && index + 1 < argc) {
			frame_limit = (legacy_u32)strtoul((legacy_char *)argv[++index], NULL, 10);
		} else if (strcmp((legacy_char *)argv[index], "--frame-rate") == 0 && index + 1 < argc) {
			frame_rate = (legacy_u16)strtoul((legacy_char *)argv[++index], NULL, 10);
			assert(frame_rate == GAME_FRAME_RATE_LOW || frame_rate == GAME_FRAME_RATE_NORMAL);
		} else if (strcmp((legacy_char *)argv[index], "--planner-yield-ms") == 0 &&
				   index + 1 < argc) {
			planner_yield_ms = (legacy_u32)strtoul((legacy_char *)argv[++index], NULL, 10);
			assert(planner_yield_ms <= UNIT_BENCHMARK_MAX_PLANNER_YIELD_MS);
		} else if (strcmp((legacy_char *)argv[index], "--prepare-ms") == 0 && index + 1 < argc) {
			preparation_ms = (legacy_u32)strtoul((legacy_char *)argv[++index], NULL, 10);
		} else if (strcmp((legacy_char *)argv[index], "--pilot-entry-trace") == 0 &&
				   index + 1 < argc) {
			pilot_entry_trace = fopen((legacy_char *)argv[++index], "w");
			assert(pilot_entry_trace != NULL);
			pilot_debug = 1;
			fputs("failure,live_frame,after,piece,target,exit_variant,secondary_piece,secondary_"
				  "speed,x,y,z,speed,route,point,yaw,pitch,roll,contact,steering,velocity_heading_"
				  "offset,throttle,brake,jump_source,jump_target,jump_launch,barrel_offset,spin_"
				  "velocity,rear_contact,exit_fraction,exit_speed,ramp_approach,secondary_ramp_"
				  "approach\n",
				  pilot_entry_trace);
		} else if (strcmp((legacy_char *)argv[index], "--pilot-trace") == 0 && index + 1 < argc) {
			pilot_trace = fopen((legacy_char *)argv[++index], "w");
			assert(pilot_trace != NULL);
			pilot_debug = 1;
			fputs("failure,live_frame,native_frame,before,piece,target,exit_variant,secondary_"
				  "piece,secondary_speed,x,y,z,speed,"
				  "route,point,yaw,"
				  "pitch,roll,contact,steering,throttle,brake,jump_source,jump_target,jump_launch,"
				  "barrel_offset,spin_velocity,rear_contact,exit_fraction,exit_speed,ramp_approach,"
				  "secondary_ramp_approach\n",
				  pilot_trace);
		} else if (strcmp((legacy_char *)argv[index], "--trace") == 0 && index + 1 < argc) {
			trace = fopen((legacy_char *)argv[++index], "w");
			assert(trace != NULL);
		} else if (strcmp((legacy_char *)argv[index], "--verify-replay") == 0) {
			verify = 1;
		} else if (strcmp((legacy_char *)argv[index], "--human") == 0 && index + 1 < argc) {
			const legacy_char *mode = (legacy_char *)argv[++index];
			human = strcmp(mode, "stationary") == 0		  ? UNIT_BENCHMARK_HUMAN_STATIONARY
					: strcmp(mode, "accelerating") == 0	  ? UNIT_BENCHMARK_HUMAN_ACCELERATING
					: strcmp(mode, "crossing") == 0		  ? UNIT_BENCHMARK_HUMAN_CROSSING
					: strcmp(mode, "grid") == 0			  ? UNIT_BENCHMARK_HUMAN_GRID
					: strcmp(mode, "grid-then-park") == 0 ? UNIT_BENCHMARK_HUMAN_GRID_THEN_PARK
														  : UNIT_BENCHMARK_HUMAN_PARKED;
			assert(human != UNIT_BENCHMARK_HUMAN_PARKED);
		} else if (strcmp((legacy_char *)argv[index], "--car") == 0 && index + 1 < argc) {
			car_id = (legacy_char *)argv[++index];
			assert(strlen(car_id) == CSRPL_CAR_ID_SIZE);
		} else if (strcmp((legacy_char *)argv[index], "--manual") == 0) {
			manual = 1;
		} else if (strcmp((legacy_char *)argv[index], "--planner-sync") == 0) {
			planner_sync = 1;
		} else if (strcmp((legacy_char *)argv[index], "--require-worker") == 0) {
			require_worker = 1;
		} else if (strcmp((legacy_char *)argv[index], "--require-clean") == 0) {
			require_clean = 1;
		} else if (strcmp((legacy_char *)argv[index], "--require-finish") == 0) {
			require_finish = 1;
		} else if (strcmp((legacy_char *)argv[index], "--prepare-until-plan") == 0) {
			prepare_until_plan = 1;
		} else if (strcmp((legacy_char *)argv[index], "--pilot-debug") == 0) {
			pilot_debug = 1;
		} else if (strcmp((legacy_char *)argv[index], "--route-debug") == 0) {
			route_debug = 1;
		}
	}
	assert(frame_limit > 0 && frame_limit <= GENGHOST_MAX_FRAMES);
	legacy_uint track_size;
	legacy_u8 *track = read_track((legacy_char *)argv[1], &track_size);
	struct CSRPL_METADATA metadata = {0};
	metadata.version = CSRPL_VERSION;
	metadata.frames_per_second = frame_rate;
	metadata.player_transmission = TRANSMISSION_AUTOMATIC;
	metadata.opponent_transmission = manual ? TRANSMISSION_MANUAL : TRANSMISSION_AUTOMATIC;
	metadata.opponent_type = opponent;
	memcpy(metadata.player_car_id, car_id, CSRPL_CAR_ID_SIZE);
	memcpy(metadata.opponent_car_id, car_id, CSRPL_CAR_ID_SIZE);
	if (opponent == OPPONENT_UNIT) {
		memcpy(metadata.opponent_id, "UNIT", CSRPL_OPPONENT_ID_SIZE);
		metadata.flags = CSRPL_FLAG_OPPONENT_COMMANDS;
	}
	if (track_size == REPLAY_TRACK_SIZE) {
		metadata.flags |= CSRPL_FLAG_CLASSIC_TRACK;
	}
	genghost_sim_set_frame_rate(frame_rate);
	genghost_sim_set_replay_metadata(&metadata);
	assert(genghost_sim_init_track(track, track_size, car_id, TRANSMISSION_AUTOMATIC, argc,
								   (legacy_char **)argv) == 0);
	free(track);
	if (route_debug) {
		struct UNIT_ROUTE route = {0};
		assert(unit_route_build(&route));
		fprintf(stderr,
				"limits max=%f accel=%f brake=%f lateral=%f width=%f length=%f "
				"wheelbase=%f native_grip=%u count=%u\n",
				route.limits.max_speed, route.limits.acceleration, route.limits.braking,
				route.limits.lateral_acceleration, route.limits.half_width,
				route.limits.half_length, route.wheelbase, simd_opponent.grip, route.count);
		for (legacy_u32 index = 0; index < route.count && index < UNIT_BENCHMARK_DEBUG_POINTS;
			 index++) {
			const struct UNIT_ROUTE_POINT *point = &route.points[index];
			legacy_u8 id = (legacy_u8)track_route_element_ids[point->route_index];
			struct CARSTATE native_point = {0};
			get_track_route_point(point->route_index, &native_point.car_route_target,
								  (legacy_s16)point->route_point, NULL);
			fprintf(
				stderr,
				"point=%u piece=%d.%u x=%f y=%f z=%f width=%f surface=%u speed=%f id=%u model=%d "
				"native_y=%d unspecified=%u stunt=%u\n",
				index, point->route_index, point->route_point, point->x, point->y, point->z,
				point->half_width, point->surface, point->speed_limit, id,
				trkObjectList[id].ss_physicalModel, native_point.car_route_target.y,
				point->height_unspecified, point->stunt);
		}
		unit_route_free(&route);
	}
	if (opponent == OPPONENT_UNIT && !extended_replay_is_active()) {
		extended_replay_runtime_begin();
		extended_replay_set_classic_track(!extended_track_active());
	}
	game_replay_mode = REPLAY_MODE_LIVE;
	state.game_inputmode = GAME_INPUT_MODE_ACTIVE;
	/* Preserve the real starting grid for parallel-start checks; other
	 * scenarios place the human explicitly. UNIT keeps its native state. */
	legacy_int grid =
		human == UNIT_BENCHMARK_HUMAN_GRID || human == UNIT_BENCHMARK_HUMAN_GRID_THEN_PARK;
	if (!grid) {
		state.playerstate.car_position.lx =
			UNIT_BENCHMARK_PARK_POSITION * UNIT_BENCHMARK_PARK_SCALE;
		state.playerstate.car_position.lz =
			UNIT_BENCHMARK_PARK_POSITION * UNIT_BENCHMARK_PARK_SCALE;
		state.playerstate.car_previous_position = state.playerstate.car_position;
	}
	legacy_f64 forward_x = -sin(state.opponentstate.car_rotate.x * UNIT_ANGLE_RADIANS);
	legacy_f64 forward_z = cos(state.opponentstate.car_rotate.x * UNIT_ANGLE_RADIANS);
	legacy_f64 human_start_x = state.opponentstate.car_position.lx / UNIT_BENCHMARK_WORLD_SCALE +
							   forward_x * UNIT_BENCHMARK_HUMAN_DISTANCE;
	legacy_f64 human_start_z = state.opponentstate.car_position.lz / UNIT_BENCHMARK_WORLD_SCALE +
							   forward_z * UNIT_BENCHMARK_HUMAN_DISTANCE;
	if (grid) {
		human_start_x = state.playerstate.car_position.lx / UNIT_BENCHMARK_WORLD_SCALE;
		human_start_z = state.playerstate.car_position.lz / UNIT_BENCHMARK_WORLD_SCALE;
	} else if (human != UNIT_BENCHMARK_HUMAN_PARKED) {
		legacy_f64 lateral =
			human == UNIT_BENCHMARK_HUMAN_CROSSING ? UNIT_BENCHMARK_HUMAN_LATERAL : 0.0;
		state.playerstate.car_position.lx =
			(legacy_s32)((human_start_x + forward_z * lateral) * UNIT_BENCHMARK_WORLD_SCALE);
		state.playerstate.car_position.lz =
			(legacy_s32)((human_start_z - forward_x * lateral) * UNIT_BENCHMARK_WORLD_SCALE);
		state.playerstate.car_position.ly = state.opponentstate.car_position.ly;
		state.playerstate.car_previous_position = state.playerstate.car_position;
		state.playerstate.car_rotate = state.opponentstate.car_rotate;
		if (human == UNIT_BENCHMARK_HUMAN_CROSSING) {
			state.playerstate.car_rotate.x =
				LEGACY_S16_WRAP_ADD(state.playerstate.car_rotate.x, ANGLE_QUARTER_TURN);
		}
	}
	struct GENGHOST_SNAPSHOT initial;
	genghost_sim_capture(&initial);
	struct GAMESTATE shadow = state;
	legacy_u32 shadow_penalty_frames = 0;
	legacy_u64 preparation_elapsed = 0;
	legacy_u32 pilot_failures = 0;
	if (opponent == OPPONENT_UNIT && preparation_ms != 0) {
		legacy_u64 started = SDL_GetTicks();
		assert(unit_opponent_prepare_begin_with_workers(planner_sync ? -1 : 0));
		while (SDL_GetTicks() - started < preparation_ms) {
			unit_opponent_prepare_work();
			if (pilot_debug) {
				debug_pilot_failure(&pilot_failures, pilot_trace, pilot_entry_trace);
			}
			if (prepare_until_plan) {
				struct UNIT_PILOT_STATS preview;
				unit_pilot_stats(&preview);
				if (preview.completed_lap || preview.exhausted) {
					break;
				}
			}
			SDL_Delay(1);
		}
		assert(unit_opponent_prepare_end());
		preparation_elapsed = SDL_GetTicks() - started;
		if (pilot_debug) {
			debug_pilot_entries();
		}
	}
	legacy_u64 *hashes = verify ? calloc(frame_limit, sizeof(*hashes)) : NULL;
	assert(!verify || hashes != NULL);
	if (trace != NULL) {
		fputs("frame,time,x,y,z,speed,steering,route,point,event,grip,demanded,yaw,pitch,roll,"
			  "contact,penalty_frames,penalty_status,penalty_confirmations,penalty_last_skipped,"
			  "penalty_recovering,shortcuts_allowed,throttle,brake\n",
			  trace);
	}
	legacy_u32 frames = 0, corner_frames = 0;
	legacy_f64 corner_speed_sum = 0, speed_sum = 0, peak_speed = 0;
	legacy_f64 closest_human = UNIT_BENCHMARK_NO_HUMAN_DISTANCE;
	legacy_int passed_human = 0;
	legacy_u64 step_sum_ns = 0, step_max_ns = 0, pacing_ns = 0;
	legacy_u32 deadline_misses = 0;
	legacy_u64 started = SDL_GetTicksNS();
	for (; frames < frame_limit; frames++) {
		legacy_u8 human_input = INPUT_NONE;
		if (grid || ((human == UNIT_BENCHMARK_HUMAN_ACCELERATING ||
					  human == UNIT_BENCHMARK_HUMAN_CROSSING) &&
					 frames >= UNIT_BENCHMARK_HUMAN_START_SECONDS * frame_rate)) {
			human_input = INPUT_ACCELERATE_FLAG;
		}
		if (human == UNIT_BENCHMARK_HUMAN_GRID_THEN_PARK &&
			frames >= UNIT_BENCHMARK_GRID_DRIVE_FRAMES) {
			/* Park through ordinary recorded controls, so playback requires no
			 * external teleport or other scripted state mutation. */
			human_input = INPUT_BRAKE_FLAG;
		}
		legacy_u64 step_started = SDL_GetTicksNS();
		assert(genghost_sim_step(human_input) >= 0);
		legacy_u64 step_ns = SDL_GetTicksNS() - step_started;
		step_sum_ns += step_ns;
		if (step_ns > step_max_ns) {
			step_max_ns = step_ns;
		}
		if (step_ns > UNIT_BENCHMARK_NS_PER_SECOND / frame_rate) {
			deadline_misses++;
		}
		if (pilot_debug) {
			debug_pilot_failure(&pilot_failures, pilot_trace, pilot_entry_trace);
		}
		shadow_penalty_step(&shadow, &shadow_penalty_frames);
		if (opponent == OPPONENT_UNIT) {
			assert(extended_unit_penalty_total() == shadow_penalty_frames);
		}
		const struct CARSTATE *car = &state.opponentstate;
		legacy_f64 speed = car->car_actual_speed / UNIT_BENCHMARK_SPEED_SCALE;
		if (human != UNIT_BENCHMARK_HUMAN_PARKED) {
			legacy_f64 dx = (car->car_position.lx - state.playerstate.car_position.lx) /
							UNIT_BENCHMARK_WORLD_SCALE;
			legacy_f64 dz = (car->car_position.lz - state.playerstate.car_position.lz) /
							UNIT_BENCHMARK_WORLD_SCALE;
			closest_human = fmin(closest_human, sqrt(dx * dx + dz * dz));
			if ((car->car_position.lx / UNIT_BENCHMARK_WORLD_SCALE - human_start_x) * forward_x +
					(car->car_position.lz / UNIT_BENCHMARK_WORLD_SCALE - human_start_z) *
						forward_z >
				UNIT_BENCHMARK_PASS_DISTANCE) {
				passed_human = 1;
			}
		}
		speed_sum += speed;
		if (speed > peak_speed) {
			peak_speed = speed;
		}
		if (abs(car->car_steeringAngle) > UNIT_BENCHMARK_CORNER_ANGLE &&
			car->car_actual_speed > UNIT_BENCHMARK_SPEED_MINIMUM) {
			corner_speed_sum += speed;
			corner_frames++;
		}
		if (hashes != NULL) {
			hashes[frames] = state_hash();
		}
		if (trace != NULL) {
			fprintf(trace,
					"%u,%.2f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%d,%u,%u,%d,%d,%d,%d,%u,%d,%d,%d,%d,%d,%"
					"d,%d\n",
					frames + 1U, (frames + 1U) / (legacy_f64)frame_rate,
					car->car_position.lx / UNIT_BENCHMARK_WORLD_SCALE,
					car->car_position.ly / UNIT_BENCHMARK_WORLD_SCALE,
					car->car_position.lz / UNIT_BENCHMARK_WORLD_SCALE, speed,
					car->car_steeringAngle, car->car_route_index, car->car_route_point_index,
					car->car_crashBmpFlag, car->car_surfacegrip_sum, car->car_demandedGrip,
					car->car_rotate.x, car->car_rotate.y, car->car_rotate.z,
					car->car_sumSurfAllWheels, shadow_penalty_frames,
					extended_replay_clocks.unit_penalty.route.status,
					extended_replay_clocks.unit_penalty.route.confirmations,
					extended_replay_clocks.unit_penalty.route.last_skipped,
					extended_unit_penalty_recovery_required(),
					(!owoot_enabled && extended_unit_penalty_shortcuts_allowed()),
					car->car_is_accelerating, car->car_is_braking);
		}
		if (car->car_crashBmpFlag != CRASH_EVENT_NONE) {
			frames++;
			break;
		}
		if (planner_yield_ms != 0) {
			legacy_u64 pacing_started = SDL_GetTicksNS();
			SDL_Delay(planner_yield_ms);
			pacing_ns += SDL_GetTicksNS() - pacing_started;
		}
	}
	legacy_u64 elapsed_ns = SDL_GetTicksNS() - started;
	legacy_u8 event = state.opponentstate.car_crashBmpFlag;
	legacy_u8 human_event = state.playerstate.car_crashBmpFlag;
	struct UNIT_PLANNER_STATS planner_stats;
	unit_opponent_planner_stats(&planner_stats);
	assert(!planner_sync || planner_stats.workers == 0);
	legacy_u64 worker_commands = unit_opponent_worker_commands();
	struct UNIT_PILOT_STATS pilot_stats;
	unit_pilot_stats(&pilot_stats);
	printf("opponent=%u frames=%u seconds=%.2f frame_rate=%u owoot=%u event=%u finished=%u "
		   "mean_mph=%.3f corner_mph=%.3f peak_mph=%.3f simulation_ms=%.3f "
		   "penalty_frames=%u preparation_ms=%.0f mean_step_us=%.3f max_step_us=%.3f "
		   "deadline_misses=%u pacing_ms=%.3f\n",
		   opponent, frames, frames / (legacy_f64)frame_rate, frame_rate, owoot_enabled != 0, event,
		   event == CRASH_EVENT_FINISH, frames ? speed_sum / frames : 0,
		   corner_frames ? corner_speed_sum / corner_frames : 0, peak_speed,
		   (elapsed_ns - pacing_ns) / UNIT_BENCHMARK_NS_PER_MILLISECOND, shadow_penalty_frames,
		   (legacy_f64)preparation_elapsed,
		   frames ? step_sum_ns / UNIT_BENCHMARK_NS_PER_MICROSECOND / frames : 0,
		   step_max_ns / UNIT_BENCHMARK_NS_PER_MICROSECOND, deadline_misses,
		   pacing_ns / UNIT_BENCHMARK_NS_PER_MILLISECOND);
	if (opponent == OPPONENT_UNIT) {
		printf("planner_workers=%u opening_candidates=%.0f route_candidates=%.0f "
			   "live_candidates=%.0f worker_accepted=%.0f worker_commands_applied=%.0f\n",
			   planner_stats.workers, (legacy_f64)planner_stats.opening_candidates,
			   (legacy_f64)planner_stats.route_candidates,
			   (legacy_f64)planner_stats.live_candidates, (legacy_f64)planner_stats.worker_accepted,
			   (legacy_f64)worker_commands);
		printf("pilot_ticks=%u pilot_trials=%u pilot_accepted=%u pilot_failed=%u "
			   "pilot_piece=%d pilot_event=%d pilot_completed=%u pilot_exhausted=%u\n",
			   pilot_stats.native_ticks, pilot_stats.trials, pilot_stats.accepted,
			   pilot_stats.failed_trials, pilot_stats.last_piece, pilot_stats.last_failure,
			   pilot_stats.completed_lap, pilot_stats.exhausted);
		printf("pilot_matched=%u pilot_rejected=%u pilot_mismatch_frame=%u "
			   "pilot_mismatch_offset=%u pilot_mismatch_actual=%u pilot_mismatch_expected=%u\n",
			   pilot_stats.matched_commands, pilot_stats.rejected_matches,
			   pilot_stats.first_mismatch_frame, pilot_stats.first_mismatch_offset,
			   pilot_stats.mismatch_actual, pilot_stats.mismatch_expected);
		printf("penalty_status=%d penalty_confirmations=%d penalty_last_skipped=%d "
			   "penalty_recovering=%d penalty_confirmed_route=%d shortcuts_allowed=%d\n",
			   extended_replay_clocks.unit_penalty.route.status,
			   extended_replay_clocks.unit_penalty.route.confirmations,
			   extended_replay_clocks.unit_penalty.route.last_skipped,
			   extended_unit_penalty_recovery_required(),
			   extended_replay_clocks.unit_penalty.route.confirmed_route,
			   (!owoot_enabled && extended_unit_penalty_shortcuts_allowed()));
	}
	if (human != UNIT_BENCHMARK_HUMAN_PARKED) {
		printf("human=%d human_event=%u passed_human=%d closest_human=%.3f "
			   "human_speed_mph=%.3f\n",
			   human, human_event, passed_human, closest_human,
			   state.playerstate.car_actual_speed / UNIT_BENCHMARK_SPEED_SCALE);
	}
	if (trace != NULL) {
		assert(fclose(trace) == 0);
	}
	if (pilot_trace != NULL) {
		assert(fclose(pilot_trace) == 0);
	}
	if (pilot_entry_trace != NULL) {
		assert(fclose(pilot_entry_trace) == 0);
	}
	if (verify) {
		unit_opponent_stop();
		genghost_sim_restore(&initial);
		game_replay_mode = REPLAY_MODE_PLAYBACK;
		for (legacy_u32 frame = 0; frame < frames; frame++) {
			/* The saved inputs and UNIT command stream are replayed without a
			 * planner or a writer that might truncate the completed recording. */
			update_gamestate_silent();
			if (state_hash() != hashes[frame]) {
				fprintf(stderr, "Replay state mismatch at frame %u\n", frame + 1U);
				free(hashes);
				return 1;
			}
		}
		printf("replay_verified_frames=%u\n", frames);
	}
	free(hashes);
	unit_opponent_stop();
	legacy_int failed_human =
		human != UNIT_BENCHMARK_HUMAN_PARKED &&
		(!passed_human || human_event != CRASH_EVENT_NONE || shadow_penalty_frames != 0 ||
		 (event != CRASH_EVENT_NONE && event != CRASH_EVENT_FINISH));
	return (require_finish && event != CRASH_EVENT_FINISH) ||
				   (require_clean && shadow_penalty_frames != 0) ||
				   (require_worker && (!worker_commands || planner_stats.workers > 1U)) ||
				   failed_human
			   ? 1
			   : 0;
}
