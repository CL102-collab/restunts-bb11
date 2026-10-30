#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../c/externs.h"
#include "../c/opponent.h"
#include "../c/game_input.h"
#include "../c/car_audio.h"
#include "../c/fileio.h"
#include "../c/resource.h"
#include "../c/ui_text.h"
#include "../c/track_objects.h"
#include "../c/car_speed.h"
#include "../c/state_internal.h"
#include "../c/owoot.h"
#include "../c/physics_internal.h"

extern void update_follow_cameras(void);

#undef memcpy
#undef printf

static legacy_u64 trace_hash = UINT64_C(1469598103934665603);
static struct GAMESTATE checkpoints[GAMESTATE_CHECKPOINT_COUNT];
static struct VECTOR cameras[4] = {{0, 0, 0}, {500, 0, 500}, {-500, 0, -500}, {500, 0, 500}};
static legacy_s8 inputs[64];
#define TEST_OPPONENT_RESOURCE_HEADER_SIZE 14U
static legacy_u8 opponent_resource[TEST_OPPONENT_RESOURCE_HEADER_SIZE + OPPONENT_SPEED_COUNT];
static legacy_u8 *speed_data = opponent_resource + TEST_OPPONENT_RESOURCE_HEADER_SIZE;
static legacy_s8 restored_random_seed[GAMESTATE_RANDOM_SEED_SIZE];
static legacy_u32 random_restore_count;
static legacy_u32 loop_reset_count;

static void trace_bytes(const void *source, legacy_u32 count)
{
	const legacy_u8 *bytes = source;
	for (legacy_u32 index = 0; index < count; index++) {
		trace_hash = (trace_hash ^ bytes[index]) * UINT64_C(1099511628211);
	}
}

static void trace_event(legacy_u16 event)
{
	legacy_u8 bytes[2];
	bytes[0] = (legacy_u8)event;
	bytes[1] = (legacy_u8)(event >> 8);
	trace_bytes(bytes, 2);
}

void reset_race_loop_state(void)
{
	loop_reset_count++;
	trace_event(1);
}

legacy_s16 get_track_route_point(legacy_s16 route, struct VECTOR *target, legacy_s16 point,
								 legacy_s8 *speed)
{
	assert(speed == 0);
	trace_event(2);
	trace_event(route);
	trace_event(point);
	target->x = 900;
	target->y = 20;
	target->z = -800;
	return 1;
}

void opponent_route_advance(legacy_s16 point)
{
	trace_event(3);
	trace_event(point);
	state.opponentstate.car_route_target.x = -400;
	state.opponentstate.car_route_target.y = 30;
	state.opponentstate.car_route_target.z = 600;
}

void init_kevinrandom(const legacy_s8 *seed)
{
	memcpy(restored_random_seed, seed, sizeof(restored_random_seed));
	random_restore_count++;
}

void get_kevinrandom_seed(legacy_s8 *seed)
{
	trace_event(4);
	memset(seed, 23, GAMESTATE_RANDOM_SEED_SIZE);
}

void far *__fmemcpy(void far *destination, const void far *source, legacy_u16 count)
{
	trace_event(5);
	return memcpy(destination, source, count);
}

void update_player_tick_with_legacy_si(legacy_s8 input, legacy_s16 caller_si)
{
	(void)caller_si;
	trace_event(6);
	trace_event(input);
	trace_event(state.game_frame);
}

void update_opponent_tick(void)
{
	trace_event(7);
}

void update_crash_particles(void)
{
	trace_event(8);
}

void audio_carstate(void)
{
	trace_event(9);
	trace_event(start_flag_animation);
}

void far *file_load_resfile(const legacy_s8 *filename)
{
	trace_event(10);
	trace_bytes(filename, 4);
	LEGACY_WRITE_U32_LE(opponent_resource + RESOURCE_FILE_SIZE_OFFSET, sizeof(opponent_resource));
	LEGACY_WRITE_U16_LE(opponent_resource + RESOURCE_FILE_COUNT_OFFSET, 1);
	memcpy(opponent_resource + RESOURCE_FILE_DIRECTORY_OFFSET, "sped", 4);
	LEGACY_WRITE_U32_LE(opponent_resource + RESOURCE_FILE_DIRECTORY_OFFSET + 4, 0);
	return opponent_resource;
}

void far *file_load_resfile_with_tail(const legacy_s8 *filename, legacy_u16 tail_bytes)
{
	assert(tail_bytes == 256U);
	return file_load_resfile(filename);
}

void unload_resource(void far *resource)
{
	assert(resource == opponent_resource);
	trace_event(11);
}

void mmgr_release(void far *resource)
{
	unload_resource(resource);
}

legacy_s8 far *locate_shape_alt(legacy_s8 far *resource, const legacy_s8 *name)
{
	assert(resource == (legacy_s8 *)opponent_resource);
	trace_bytes(name, 4);
	return (legacy_s8 *)speed_data;
}

legacy_s8 far *locate_text_res(void far *resource, const legacy_s8 *name)
{
	assert(resource == opponent_resource);
	trace_bytes(name, 3);
	return (legacy_s8 *)"OP";
}

void copy_string(legacy_s8 *destination, legacy_s8 far *source)
{
	do {
		*destination++ = *source;
	} while (*source++ != 0);
}

static void configure_simulation(legacy_u32 scenario)
{
	memset(&state, 0x25, sizeof(state));
	memset(&gameconfig, 0, sizeof(gameconfig));
	memset(&simd_player, 0, sizeof(simd_player));
	memset(&simd_opponent, 0, sizeof(simd_opponent));
	memset(checkpoints, 0x57, sizeof(checkpoints));
	cvxptr = checkpoints;
	elapsed_time1 = 11;
	framespersec = scenario % 3U == 0 ? 0 : (scenario % 3U == 1 ? 10 : 20);
	track_angle = (legacy_s16)(scenario * 117U);
	start_finish_column = 2;
	start_finish_row = 3;
	hillFlag = scenario % 2U;
	track_column_centers[2] = scenario % 2U == 0 ? 2800 : -32760;
	track_row_centers[3] = 3600;
	track_row_positions[3] = 3100;
	gameconfig.game_opponenttype = scenario % 2U;
	gameconfig.game_playertransmission = scenario % 2U;
	simd_player.idle_rpm = 800;
	simd_opponent.idle_rpm = 1000;
	for (legacy_u32 index = 0; index < SIMD_GEAR_RATIO_COUNT; index++) {
		simd_player.gear_ratios[index] = (legacy_u16)(index * 1093U);
		simd_opponent.gear_ratios[index] = (legacy_u16)(index * 2087U);
	}
	simd_player.knob_points[1].px = -30;
	simd_player.knob_points[1].py = 15;
}

static void test_initialization(void)
{
	for (legacy_u32 scenario = 0; scenario < 48U; scenario++) {
		configure_simulation(scenario);
		init_game_state((legacy_s16)(scenario % 4U));
		trace_bytes(&state, sizeof(state));
		trace_event(checkpoints[0].game_checkpoint_valid);
		trace_event(checkpoints[GAMESTATE_CHECKPOINT_COUNT - 1].game_checkpoint_valid);
		trace_event(elapsed_time1);
		trace_event(checkpoint_frame_interval);
		trace_event(timer_ticks_per_frame);
		trace_event(steerWhlRespTable_ptr == steerWhlRespTable_10fps);
	}
}

static void test_restore_initial_checkpoint(void)
{
	configure_simulation(2);
	init_game_state(GAMESTATE_INIT_RESET_CHECKPOINTS);
	memset(inputs, 0, sizeof(inputs));
	replay_input_buffer = inputs;
	inputs[0] = INPUT_ACCELERATE_FLAG;
	game_replay_mode = REPLAY_MODE_LIVE;
	race_start_sequence_state = RACE_START_SEQUENCE_INACTIVE;
	update_gamestate();
	struct GAMESTATE initial_state = checkpoints[0];
	legacy_u32 resets_before_restore = loop_reset_count;

	for (legacy_u32 scenario = 0; scenario < 3U; scenario++) {
		memset(&state, 0x45, sizeof(state));
		state.game_frame = scenario == 1U ? 0 : 29;
		elapsed_time1 = scenario == 2U ? 600 : 0;
		elapsed_time2 = 31;
		memset(restored_random_seed, 0, sizeof(restored_random_seed));
		legacy_u32 restores_before = random_restore_count;

		restore_gamestate(0);

		assert(memcmp(&state, &initial_state, sizeof(state)) == 0);
		assert(memcmp(restored_random_seed, initial_state.kevinseed,
					  sizeof(restored_random_seed)) == 0);
		assert(random_restore_count == restores_before + 1U);
		assert(loop_reset_count == resets_before_restore);
		assert(elapsed_time2 == 0);
		assert(elapsed_time1 == (scenario == 2U ? 600 : 0));
	}
}

static void test_restore_without_initial_checkpoint(void)
{
	configure_simulation(2);
	init_game_state(GAMESTATE_INIT_RESET_CHECKPOINTS);
	state.game_frame = 29;
	state.game_inputmode = GAME_INPUT_MODE_ACTIVE;
	state.game_end_event = 1;
	state.playerstate.car_position.lx = -12345;
	legacy_u32 restores_before = random_restore_count;
	legacy_u32 resets_before = loop_reset_count;

	restore_gamestate(0);

	assert(state.game_frame == 0);
	assert(state.game_inputmode == GAME_INPUT_MODE_WAITING);
	assert(state.game_end_event == 0);
	assert(state.playerstate.car_position.lx != -12345);
	assert(checkpoints[0].game_checkpoint_valid == 0);
	assert(random_restore_count == restores_before);
	assert(loop_reset_count == resets_before + 1U);
}

static void test_owoot_checkpoint_progress(void)
{
	owoot_enabled = 1;
	configure_simulation(2);
	init_game_state(GAMESTATE_INIT_RESET_CHECKPOINTS);
	assert(state.playerstate.car_reserved_route_word1 == 0);
	assert(state.playerstate.car_reserved_route_word2 == 0);
	for (legacy_u32 index = 0; index < CARSTATE_WHEEL_COUNT; index++) {
		assert(state.playerstate.car_reserved_wheel_state[index] == 0);
		state.playerstate.car_reserved_wheel_state[index] = (legacy_s16)(index + 7);
	}
	state.playerstate.car_reserved_route_word1 = 12;
	state.playerstate.car_reserved_route_word2 = 4;
	memset(inputs, 0, sizeof(inputs));
	replay_input_buffer = inputs;
	game_replay_mode = REPLAY_MODE_LIVE;
	race_start_sequence_state = RACE_START_SEQUENCE_INACTIVE;
	update_gamestate();
	assert(checkpoints[0].game_checkpoint_valid);
	state.game_frame = 29;
	state.playerstate.car_reserved_route_word1 = 24;
	state.playerstate.car_reserved_route_word2 = 1;
	memset(state.playerstate.car_reserved_wheel_state, 0,
		   sizeof(state.playerstate.car_reserved_wheel_state));
	restore_gamestate(0);
	assert(state.playerstate.car_reserved_route_word1 == 12);
	assert(state.playerstate.car_reserved_route_word2 == 4);
	for (legacy_u32 index = 0; index < CARSTATE_WHEEL_COUNT; index++) {
		assert(state.playerstate.car_reserved_wheel_state[index] == (legacy_s16)(index + 7));
	}
	init_game_state(GAMESTATE_INIT_RESET_CHECKPOINTS);
	assert(state.playerstate.car_reserved_route_word1 == 0);
	assert(state.playerstate.car_reserved_route_word2 == 0);
	owoot_enabled = 0;
}

static void configure_cameras(legacy_u32 scenario)
{
	memset(&state, 0, sizeof(state));
	gameconfig.game_opponenttype = scenario % 2U;
	framespersec = scenario % 3U == 0 ? 0 : (scenario % 3U == 1 ? 10 : 20);
	state.game_frame = scenario % 11U;
	state.game_player_route_status = scenario % 7U == 0;
	state.game_route_confirmation_count = scenario % 7U == 1;
	trackside_camera_positions = cameras;
	trackside_camera_count = scenario % 13U == 0 ? 128U : 4U;
	static const legacy_s16 errors[] = {0, 128, 129, 895, 896};
	struct CARSTATE *car;
	for (legacy_u32 index = 0; index < 2U; index++) {
		car = index == 0 ? &state.playerstate : &state.opponentstate;
		car->car_position.lx = ((legacy_s32)scenario * 17 - 1200) * 64;
		car->car_position.ly = ((legacy_s32)scenario - 50) * 64;
		car->car_position.lz = ((legacy_s32)scenario * 13 - 1400) * 64;
		car->car_route_target.x = 400;
		car->car_route_target.z = -500;
		car->car_route_has_reverse_path = scenario % 7U == 2;
		car->car_crashBmpFlag = scenario % 7U == 3;
		car->car_route_index = scenario % 7U == 4 ? ROUTE_INDEX_NONE : 0;
		car->car_route_heading_error = errors[scenario % 5U];
		state.game_follow_camera_position[index].x = index * 300;
		state.game_follow_camera_position[index].y = (scenario % 5U) * 120;
		state.game_follow_camera_position[index].z = (legacy_s16)(-400 + index * 200);
		state.game_trackside_camera_index[index] = 3;
	}
}

static void test_frame_updates(void)
{
	for (legacy_u32 scenario = 0; scenario < 120U; scenario++) {
		configure_cameras(scenario);
		update_follow_cameras();
		trace_bytes(&state, sizeof(state));
		memset(checkpoints, 0, sizeof(checkpoints));
		cvxptr = checkpoints;
		memset(inputs, 0, sizeof(inputs));
		replay_input_buffer = inputs;
		inputs[state.game_frame] = scenario % 3U == 0 ? INPUT_ACCELERATE_FLAG : INPUT_NONE;
		checkpoint_frame_interval = scenario % 5U;
		state.game_end_event = scenario % 2U;
		state.game_frames_per_sec = 2;
		state.game_frame_in_sec = scenario % 3U;
		state.game_inputmode =
			scenario % 2U == 0 ? GAME_INPUT_MODE_WAITING : GAME_INPUT_MODE_ACTIVE;
		state.game_particles_active = scenario % 3U;
		state.playerstate.car_actual_speed = scenario % 2U;
		state.playerstate.car_rev_speed = (scenario % 3U) * 1280U;
		game_replay_mode = scenario % 3U;
		race_exit_request = 0;
		race_start_sequence_state = scenario % 3U;
		start_flag_animation = scenario % 2U == 0 ? 384 : 449;
		track_angle = 0;
		track_row_centers[start_finish_row] = scenario % 2U == 0 ? -3000 : 3000;
		update_gamestate();
		trace_bytes(&state, sizeof(state));
		trace_bytes(checkpoints, sizeof(checkpoints));
		trace_event(race_exit_request);
		trace_event(race_start_sequence_state);
		trace_event(start_flag_animation);
	}
}

static void test_opponent_routes(void)
{
	static legacy_s16 primary[] = {1, 2, 0, 4, 0, 5};
	track_primary_route_links = primary;
	static legacy_s16 alternate[] = {3, -1, -1, -1, -1, -1};
	track_alternate_route_links = alternate;
	static legacy_s8 elements[] = {0, 1, 2, 3, 4, 5};
	track_route_element_ids = elements;
	static legacy_s8 route[64];
	opponent_route_track_indices = route;
	for (legacy_u32 scenario = 0; scenario < 8U; scenario++) {
		memset(route, 0x66, sizeof(route));
		gameconfig.game_opponenttype = scenario;
		primary[4] = scenario % 3U == 0 ? -1 : (scenario % 3U == 1 ? 3 : 0);
		for (legacy_u32 index = 0; index < OPPONENT_SPEED_COUNT; index++) {
			speed_data[index] = (legacy_u8)(index * (scenario + 1U));
		}
		load_opponent_data();
		trace_bytes(route, sizeof(route));
		trace_bytes(oppnentSped, OPPONENT_SPEED_COUNT);
	}
}

static void test_fractional_follow_cameras(void)
{
	memset(&state, 0x5a, sizeof(state));
	struct GAMESTATE real_state = state;
	struct GAMESTATE branch;
	memset(&branch, 0, sizeof(branch));
	gameconfig.game_opponenttype = 1;
	framespersec = GAME_FRAME_RATE_NORMAL;
	branch.playerstate.car_route_index = ROUTE_INDEX_NONE;
	branch.opponentstate.car_route_index = ROUTE_INDEX_NONE;
	branch.playerstate.car_position.lz = 1000L * 64;
	branch.opponentstate.car_position.lx = 1000L * 64;
	branch.game_trackside_camera_index[0] = 2;
	branch.game_trackside_camera_index[1] = 3;

	update_follow_cameras_fraction(&branch, PHYSICS_FRACTION_ONE / 2);
	assert(branch.game_follow_camera_position[0].y == 15);
	assert(branch.game_follow_camera_position[0].z == 60);
	assert(branch.game_follow_camera_position[1].y == 15);
	assert(branch.game_follow_camera_position[1].x == 60);
	assert(branch.game_trackside_camera_index[0] == 2);
	assert(branch.game_trackside_camera_index[1] == 3);

	/* A landing changes the camera target during the disposable branch. */
	branch.playerstate.car_position.ly = 400L * 64;
	branch.game_follow_camera_position[0].y = 500;
	update_follow_cameras_fraction(&branch, PHYSICS_FRACTION_ONE / 2);
	assert(branch.game_follow_camera_position[0].y == 515);
	branch.playerstate.car_position.ly = 0;
	update_follow_cameras_fraction(&branch, PHYSICS_FRACTION_ONE / 2);
	assert(branch.game_follow_camera_position[0].y == 500);
	assert(branch.game_player_camera_previous.y == 515);

	/* The same duration is half a 10 Hz update; inactive opponents stay fixed. */
	gameconfig.game_opponenttype = 0;
	framespersec = GAME_FRAME_RATE_LOW;
	branch.game_follow_camera_position[0] = (struct VECTOR){0, 0, 0};
	struct VECTOR opponent_position = branch.game_follow_camera_position[1];
	update_follow_cameras_fraction(&branch, PHYSICS_FRACTION_ONE);
	assert(branch.game_follow_camera_position[0].y == 15);
	assert(branch.game_follow_camera_position[0].z == 120);
	assert(memcmp(&branch.game_follow_camera_position[1], &opponent_position,
				  sizeof(opponent_position)) == 0);
	struct GAMESTATE before = branch;
	update_follow_cameras_fraction(&branch, 0);
	assert(memcmp(&branch, &before, sizeof(branch)) == 0);
	assert(memcmp(&state, &real_state, sizeof(state)) == 0);

	/* A camera already following a car must cover its small displacement
	 * during each fractional step. */
	for (legacy_u16 rate = GAME_FRAME_RATE_LOW; rate <= GAME_FRAME_RATE_NORMAL; rate += 10) {
		framespersec = rate;
		memset(&branch, 0, sizeof(branch));
		branch.playerstate.car_route_index = ROUTE_INDEX_NONE;
		branch.playerstate.car_position.lz = 450L * 64;
		branch.game_follow_camera_position[0].y = 270;
		for (legacy_s16 step = 1; step <= 60 / rate; step++) {
			branch.playerstate.car_position.lz += 15L * 64;
			branch.playerstate.car_position.ly += 2L * 64;
			update_follow_cameras_fraction(&branch, PHYSICS_FRACTION_ONE / 3);
			assert(branch.game_follow_camera_position[0].z == 15 * step);
			assert(branch.game_follow_camera_position[0].y == 270 + 2 * step);
		}
	}
	assert(memcmp(&state, &real_state, sizeof(state)) == 0);
}

int main(void)
{
	test_initialization();
	test_frame_updates();
	test_opponent_routes();
	/* Captured before extraction: initialization modes, camera boundaries, frame callbacks,
	 * checkpoints, and opponent routes with branches, dead ends and cycles. */
	assert(trace_hash == UINT64_C(0x88796a1f55e594e4));
	test_restore_initial_checkpoint();
	test_restore_without_initial_checkpoint();
	test_owoot_checkpoint_progress();
	test_fractional_follow_cameras();
	puts("Simulation setup snapshots and initial checkpoint restoration passed (180 scenarios).");
	return 0;
}
