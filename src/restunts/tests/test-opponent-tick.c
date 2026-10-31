#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../c/externs.h"
#include "../c/state_internal.h"
#include "../c/car/car_speed.h"
#include "../c/crash_state.h"
#include "../c/game_input.h"
#include "../c/opponent/opponent.h"
#include "../c/track/track_objects.h"
#include "../c/track/trackdata_layout.h"

static legacy_s8 route_indices[9 * LEGACY_WORD_BYTES];
static legacy_s8 route_elements[8];
static legacy_s8 route_flags[8];
static legacy_s8 route_columns[8];
static legacy_s8 route_rows[8];
static legacy_u8 terrain[901];
static legacy_u16 trace[8];
static legacy_u32 trace_count;
static legacy_s8 last_input;
static legacy_u32 random_state = 1;

#ifdef OPPONENT_DIFFERENTIAL
void reference_update_opponent_tick(void);
#endif

void update_car_speed(legacy_s8 input, legacy_s16 index, struct CARSTATE *car, struct SIMD *simd)
{
	assert(index == OPPONENT_CAR_INDEX);
	assert(car == &state.opponentstate);
	assert(simd == &simd_opponent);
	trace[trace_count++] = 0x100U | (legacy_u8)input;
	last_input = input;
	car->car_lastrpm = car->car_currpm;
}

void update_grip(struct CARSTATE *car, struct SIMD *simd, legacy_s16 behavior)
{
	assert(car == &state.opponentstate);
	assert(simd == &simd_opponent);
	assert(behavior == GRIP_BEHAVIOR_OPPONENT);
	trace[trace_count++] = 0x200U;
	car->car_actual_speed = LEGACY_U16_WRAP_ADD(car->car_actual_speed, 1U);
}

void update_player_state(struct CARSTATE *car, struct SIMD *simd, struct CARSTATE *other,
						 struct SIMD *other_simd, legacy_s16 index)
{
	assert(car == &state.opponentstate);
	assert(simd == &simd_opponent);
	assert(other == &state.playerstate);
	assert(other_simd == &simd_player);
	assert(index == OPPONENT_CAR_INDEX);
	trace[trace_count++] = 0x300U;
	car->car_position.lx = LEGACY_S32_WRAP_ADD(car->car_position.lx, 64L);
	car->car_position.lz = LEGACY_S32_WRAP_ADD(car->car_position.lz, 128L);
	car->car_rotate.y = LEGACY_S16_WRAP_ADD(car->car_rotate.y, 1);
}

void update_crash_state(legacy_s16 event, legacy_s16 index)
{
	assert(index == OPPONENT_CAR_INDEX);
	assert(event == CRASH_EVENT_FINISH);
	trace[trace_count++] = 0x400U | (legacy_u16)event;
	state.opponentstate.car_crashBmpFlag = (legacy_s8)event;
}

static legacy_u16 random_word(void)
{
	random_state = random_state * 1664525UL + 1013904223UL;
	return (legacy_u16)(random_state >> 16);
}

static void reset_opponent(void)
{
	memset(&state, 0, sizeof(state));
	memset(terrain, 0, sizeof(terrain));
	memset(trace, 0, sizeof(trace));
	trace_count = 0;
	framespersec = GAME_FRAME_RATE_NORMAL;
	track_angle = 0;
	start_finish_column = 10;
	start_finish_row = 10;
	track_terrain_map = terrain;
	opponent_route_track_indices = route_indices;
	track_route_element_ids = route_elements;
	track_route_traversal_flags = route_flags;
	track_route_columns = route_columns;
	track_route_rows = route_rows;
	for (legacy_s32 index = 0; index < 30; index++) {
		terrainrows[index] = index * 30;
		track_row_centers[index] = index * 1024 + 512;
		track_row_positions[index] = index * 1024;
		track_column_centers[index] = index * 1024 + 512;
		track_column_positions[index] = index * 1024;
	}
	for (legacy_s32 index = 0; index < 8; index++) {
		LEGACY_WRITE_U16_LE(route_indices + index * LEGACY_WORD_BYTES, (legacy_u16)index);
		route_elements[index] = 4;
		route_flags[index] = 0;
		route_columns[index] = 10;
		route_rows[index] = (legacy_s8)(10 + index);
	}
	LEGACY_WRITE_U16_LE(route_indices + 8 * LEGACY_WORD_BYTES, 0U);
	state.opponentstate.car_position.lx = 10752L * 64;
	state.opponentstate.car_position.lz = 10752L * 64;
	state.playerstate.car_position.lx = 12000L * 64;
	state.playerstate.car_position.lz = 12000L * 64;
	state.opponentstate.car_route_target.x = 10752;
	state.opponentstate.car_route_target.y = -1;
	state.opponentstate.car_route_target.z = 11752;
	state.opponentstate.car_route_first_edge = state.opponentstate.car_route_target;
	state.opponentstate.car_route_second_edge = state.opponentstate.car_route_target;
	state.opponentstate.car_route_first_edge.x -= 120;
	state.opponentstate.car_route_second_edge.x += 120;
	state.opponentstate.car_sumSurfFrontWheels = 2;
	state.opponentstate.car_sumSurfRearWheels = 2;
	state.opponentstate.car_surfacegrip_sum = 1000;
	state.game_opponent_target_speed = 40;
}

static void run_opponent(void)
{
#ifdef OPPONENT_DIFFERENTIAL
	struct GAMESTATE initial = state;
	reference_update_opponent_tick();
	struct GAMESTATE expected = state;
	legacy_u16 expected_trace[8];
	memmove(expected_trace, trace, sizeof(trace));
	legacy_u32 expected_count = trace_count;
	state = initial;
	memset(trace, 0, sizeof(trace));
	trace_count = 0;
#endif
	update_opponent_tick();
#ifdef OPPONENT_DIFFERENTIAL
	assert(memcmp(&state, &expected, sizeof(state)) == 0);
	assert(trace_count == expected_count);
	assert(memcmp(trace, expected_trace, sizeof(trace)) == 0);
#endif
	assert(trace_count >= 3);
	assert((trace[0] & 0xff00U) == 0x100U);
	assert(trace[1] == 0x200U);
	assert(trace[2] == 0x300U);
}

static void test_pedal_boundaries(void)
{
	static const legacy_s8 inputs[] = {INPUT_ACCELERATE_FLAG, INPUT_NONE, INPUT_NONE,
									   INPUT_BRAKE_FLAG};

	static const legacy_u16 speeds[] = {9983, 9984, 11008, 11009};
	for (legacy_s32 index = 0; index < 4; index++) {
		reset_opponent();
		state.opponentstate.car_rev_speed = speeds[index];
		run_opponent();
		assert(last_input == inputs[index]);
	}
	reset_opponent();
	state.opponentstate.car_sumSurfRearWheels = 0;
	run_opponent();
	assert(last_input == INPUT_NONE);
	reset_opponent();
	state.opponentstate.car_crashBmpFlag = CRASH_EVENT_COLLISION;
	state.opponentstate.car_actual_speed = 0;
	run_opponent();
	assert(last_input == INPUT_BRAKE_FLAG);
	assert(state.opponentstate.car_sound_flags == CAR_SOUND_NONE);
}

static void test_steering_and_passing(void)
{
	reset_opponent();
	state.opponentstate.car_steeringAngle = 40;
	state.opponentstate.car_sumSurfFrontWheels = 0;
	run_opponent();
	assert(state.opponentstate.car_steeringAngle == 32);
	reset_opponent();
	framespersec = GAME_FRAME_RATE_LOW;
	state.opponentstate.car_steeringAngle = 40;
	state.opponentstate.car_sumSurfFrontWheels = 0;
	run_opponent();
	assert(state.opponentstate.car_steeringAngle == 24);
	reset_opponent();
	state.playerstate.car_position.lx = 10751L * 64;
	state.playerstate.car_position.lz = 11000L * 64;
	run_opponent();
	assert(state.game_opponent_route_indicator == ROUTE_INDICATOR_RIGHT);
	reset_opponent();
	state.playerstate.car_position.lx = 10753L * 64;
	state.playerstate.car_position.lz = 11000L * 64;
	run_opponent();
	assert(state.game_opponent_route_indicator == ROUTE_INDICATOR_LEFT);
}

static void test_tick_sweep(void)
{
	static const legacy_s16 angles[] = {-1024, -256, -65, -1, 0, 1, 65, 256, 1024};
	legacy_u32 hash = 2166136261UL;
	for (legacy_u32 sample = 0; sample < 200000; sample++) {
		reset_opponent();
		framespersec = sample % 2 ? GAME_FRAME_RATE_NORMAL : GAME_FRAME_RATE_LOW;
		state.game_inputmode = random_word() % 4;
		state.game_opponent_target_speed = LEGACY_S8_FROM_BITS((legacy_u8)random_word());
		state.opponentstate.car_route_index = random_word() % 8;
		state.opponentstate.car_route_point_index = random_word() % 2;
		state.opponentstate.car_lap_count = random_word() % 3;
		state.opponentstate.car_crashBmpFlag = random_word() % 3;
		state.playerstate.car_crashBmpFlag = random_word() % 3;
		state.opponentstate.car_steeringAngle = angles[random_word() % 9];
		state.opponentstate.car_rotate.x = angles[random_word() % 9];
		state.opponentstate.car_rotate.y = angles[random_word() % 9];
		state.opponentstate.car_rotate.z = angles[random_word() % 9];
		state.opponentstate.car_velocity_heading_offset = angles[random_word() % 9];
		state.opponentstate.car_actual_speed = random_word();
		state.opponentstate.car_rev_speed = random_word();
		state.opponentstate.car_sumSurfFrontWheels = random_word() % 3;
		state.opponentstate.car_sumSurfRearWheels = random_word() % 3;
		state.opponentstate.car_demandedGrip = LEGACY_S16_FROM_BITS(random_word());
		state.opponentstate.car_surfacegrip_sum = LEGACY_S16_FROM_BITS(random_word());
		state.opponentstate.car_slidingFlag = random_word() % 2;
		state.opponentstate.car_route_target.x += (legacy_s32)(random_word() % 2000) - 1000;
		state.opponentstate.car_route_target.y = random_word() % 2 ? -1 : 100;
		state.opponentstate.car_route_target.z += (legacy_s32)(random_word() % 2000) - 2000;
		state.playerstate.car_position.lx = (10752L + (legacy_s32)(random_word() % 800) - 400) * 64;
		state.playerstate.car_position.ly = ((legacy_s32)(random_word() % 400) - 200) * 64L;
		state.playerstate.car_position.lz =
			(10752L + (legacy_s32)(random_word() % 1400) - 700) * 64;
		for (legacy_u32 index = 0; index < 8; index++) {
			route_elements[index] = 4 + random_word() % 6;
			route_flags[index] = random_word() % 2 ? 16 : 0;
		}
		run_opponent();
		const legacy_u8 *bytes = (const legacy_u8 *)&state;
		for (legacy_u32 index = 0; index < sizeof(state); index++) {
			hash = (hash ^ bytes[index]) * 16777619UL;
		}
		for (legacy_u32 index = 0; index < trace_count; index++) {
			hash = (hash ^ trace[index]) * 16777619UL;
		}
	}
#ifdef OPPONENT_RECORD_BASELINE
	fprintf(stdout, "%08" LEGACY_PRIx32 "\n", hash);
#else
	/* Opponent state and dependency calls with one random draw per input field. */
	assert(hash == 0x20da6be0UL);
#endif
}

int main(void)
{
	test_pedal_boundaries();
	test_steering_and_passing();
	test_tick_sweep();
	return 0;
}
