#include <assert.h>
#include <math.h>
#include <string.h>
#include "../c/externs.h"
#include "../c/gamestate.h"
#include "../c/crash_state.h"
#include "../c/opponent/unit_opponent_internal.h"
#include "../c/extended/extended_checkpoint.h"
#include "../c/extended/extended_replay_runtime.h"
#include "../c/extended/extended_unit_checkpoint.h"
#include "../c/extended/checkpoint_shape.h"

#undef memset
#undef memcpy

#define TEST_TRACK_SIZE 90U
#define TEST_GATE_COLUMN 70U
#define TEST_GATE_ROW 7U
#define TEST_POINT_COUNT 4U
#define TEST_PIECE_COUNT 3U
#define TEST_BODY_HEIGHT 20
#define TEST_SPEED 100U
#define TEST_APPROACH 200
#define TEST_MISSED_LATERAL 250
#define TEST_REPLAY_OPPONENT 7
#define TEST_NORMAL_SPEED_SCALE 256U
#define TEST_ROAD_HALF_WIDTH 256.0
#define TEST_ALIGN_APPROACH 800
#define TEST_OBLIQUE_HALF_WIDTH 40.0
#define TEST_OBLIQUE_HALF_LENGTH 90.0

struct GAMESTATE state;
struct GAMEINFO gameconfig;
struct SIMD simd_opponent;
struct EXTENDED_REPLAY_CLOCKS extended_replay_clocks;
legacy_s16 track_pieces_counter = TEST_PIECE_COUNT;
legacy_s16 track_angle;
legacy_u16 framespersec = GAME_FRAME_RATE_NORMAL;
legacy_s8 start_finish_column = TEST_GATE_COLUMN, start_finish_row = TEST_GATE_ROW + 3U;
static legacy_s8 route_columns[TEST_PIECE_COUNT], route_rows[TEST_PIECE_COUNT];
static legacy_s8 route_ids[TEST_PIECE_COUNT];
legacy_s8 *track_route_columns = route_columns;
legacy_s8 *track_route_rows = route_rows;
legacy_s8 *track_route_element_ids = route_ids;
static struct CSTRK_OBJECT objects[2];
static struct CSTRK_TRACK track;
static struct UNIT_ROUTE_POINT points[TEST_POINT_COUNT];
static struct UNIT_ROUTE route;
static legacy_int track_active = 1;

legacy_int extended_track_active(void)
{
	return track_active;
}

legacy_int extended_replay_is_active(void)
{
	return 1;
}

const struct CSTRK_TRACK *extended_track_model(void)
{
	return &track;
}

const struct CSTRK_OBJECT *extended_track_object_at(legacy_int column, legacy_int row)
{
	for (legacy_u32 index = 0; index < track.object_count; index++) {
		if (objects[index].x == column && objects[index].y == row) {
			return &objects[index];
		}
	}
	return NULL;
}

const struct CSTRK_OBJECT *extended_track_object_for_id(legacy_int column, legacy_int row,
														legacy_uint id)
{
	const struct CSTRK_OBJECT *object = extended_track_object_at(column, row);
	return object && object->legacy_id == id ? object : NULL;
}

void update_crash_state(legacy_s16 event, legacy_s16 car_index)
{
	assert(event == CRASH_EVENT_FINISH);
	assert(extended_checkpoint_finish_allowed(car_index));
}

static struct VECTORLONG position(legacy_s32 lateral, legacy_s32 depth)
{
	const struct CSTRK_OBJECT *object = &objects[1];
	legacy_s32 dx = lateral, dz = depth;
	switch (object->rotation) {
		case 1U:
			dx = depth;
			dz = -lateral;
			break;
		case 2U:
			dx = -lateral;
			dz = -depth;
			break;
		case 3U:
			dx = -depth;
			dz = lateral;
			break;
		default:
			break;
	}
	legacy_s32 support = object->base_height * CSTRK_LEVEL_WORLD_HEIGHT +
						 ((object->slope_x + object->slope_y) * CSTRK_LEVEL_WORLD_HEIGHT) / 2 +
						 (object->slope_x * dx - object->slope_y * dz) * CSTRK_LEVEL_WORLD_HEIGHT /
							 CSTRK_TILE_WORLD_SIZE;
	struct VECTORLONG result = {
		((legacy_s32)object->x * CSTRK_TILE_WORLD_SIZE + CSTRK_TILE_WORLD_SIZE / 2 + dx) *
			EXTENDED_CHECKPOINT_POSITION_SCALE,
		(support + TEST_BODY_HEIGHT) * EXTENDED_CHECKPOINT_POSITION_SCALE,
		(((legacy_s32)track.height - object->y) * CSTRK_TILE_WORLD_SIZE -
		 CSTRK_TILE_WORLD_SIZE / 2 + dz) *
			EXTENDED_CHECKPOINT_POSITION_SCALE};
	return result;
}

static void reset(legacy_u8 rotation, legacy_u16 gate_id)
{
	memset(&state, 0, sizeof(state));
	memset(&gameconfig, 0, sizeof(gameconfig));
	memset(&simd_opponent, 0, sizeof(simd_opponent));
	memset(&extended_replay_clocks, 0, sizeof(extended_replay_clocks));
	memset(&track, 0, sizeof(track));
	memset(objects, 0, sizeof(objects));
	memset(points, 0, sizeof(points));
	memset(&route, 0, sizeof(route));
	track_active = 1;
	track.width = track.height = TEST_TRACK_SIZE;
	track.objects = objects;
	track.object_count = 2;
	objects[0].legacy_id = CHECKPOINT_START_FINISH_ID;
	objects[0].x = TEST_GATE_COLUMN;
	objects[0].y = TEST_GATE_ROW + 3U;
	objects[0].width = objects[0].height = 1;
	objects[1].legacy_id = gate_id;
	objects[1].x = TEST_GATE_COLUMN;
	objects[1].y = TEST_GATE_ROW;
	objects[1].width = objects[1].height = 1;
	objects[1].rotation = rotation;
	objects[1].base_height = 20;
	objects[1].slope_x = 1;
	objects[1].slope_y = -1;
	gameconfig.game_opponenttype = TEST_REPLAY_OPPONENT;
	route.points = points;
	route.count = route.finish_begin = TEST_POINT_COUNT;
	route.wheelbase = 80;
	route.limits.half_width = 20;
	route.limits.half_length = 50;
	route.limits.max_speed = 200;
	simd_opponent.collide_points[0].py = 40;
	for (legacy_u32 index = 0; index < TEST_PIECE_COUNT; index++) {
		route_columns[index] = TEST_GATE_COLUMN;
		route_rows[index] = TEST_GATE_ROW + 1U - index;
		route_ids[index] = index == 1U ? LEGACY_S8_FROM_BITS((legacy_u8)gate_id) : 0;
	}
	for (legacy_u32 index = 0; index < TEST_POINT_COUNT; index++) {
		struct VECTORLONG sample =
			position(0, (legacy_s32)index * CSTRK_TILE_WORLD_SIZE - CSTRK_TILE_WORLD_SIZE * 3 / 2);
		points[index].x = sample.lx / UNIT_POSITION_SCALE;
		points[index].y = sample.ly / UNIT_POSITION_SCALE - TEST_BODY_HEIGHT;
		points[index].z = sample.lz / UNIT_POSITION_SCALE;
		points[index].half_width = TEST_ROAD_HALF_WIDTH;
		points[index].route_index = index == 0 ? 0 : index == TEST_POINT_COUNT - 1U ? 2 : 1;
		points[index].route_point = index == 2U ? 1 : 0;
	}
	state.opponentstate.car_position = position(0, -TEST_APPROACH);
	state.opponentstate.car_previous_position = state.opponentstate.car_position;
	state.opponentstate.car_actual_speed = TEST_SPEED * TEST_NORMAL_SPEED_SCALE;
	state.opponentstate.car_rotate.x = -(legacy_s16)(rotation * ANGLE_QUARTER_TURN);
}

static struct UNIT_PLAN plan(void)
{
	struct UNIT_PLAN result = {0};
	result.valid = 1;
	result.target_speed = 150;
	return result;
}

static struct VECTORLONG target_position(const struct UNIT_PLAN *guide)
{
	struct VECTORLONG result = {
		(legacy_s32)(guide->target_x * UNIT_POSITION_SCALE),
		(legacy_s32)((guide->target_y + TEST_BODY_HEIGHT) * UNIT_POSITION_SCALE),
		(legacy_s32)(guide->target_z * UNIT_POSITION_SCALE)};
	return result;
}

static void test_physical_crossing(void)
{
	const legacy_u16 colors[] = {CSTRK_BLUE_CHECKPOINT_ID, CSTRK_RED_CHECKPOINT_ID,
								 CSTRK_COMBINED_CHECKPOINT_ID};
	for (legacy_u8 color = 0; color < sizeof(colors) / sizeof(colors[0]); color++) {
		for (legacy_u8 rotation = 0; rotation < 4U; rotation++) {
			reset(rotation, colors[color]);
			assert(unit_checkpoint_can_advance(&route, 0));
			assert(unit_checkpoint_can_advance(&route, 1));
			assert(!unit_checkpoint_can_advance(&route, 2));
			struct UNIT_PLAN guidance = plan();
			assert(unit_checkpoint_guide(&route, 1, &state.opponentstate, &guidance));
			assert(guidance.target_speed == 150);
			struct VECTORLONG target = target_position(&guidance);
			assert(extended_checkpoint_crossed(&track, &objects[1],
											   &state.opponentstate.car_position, &target));
			struct EXTENDED_REPLAY_CLOCKS before = extended_replay_clocks;
			extended_checkpoint_begin_step();
			state.opponentstate.car_position = target;
			extended_checkpoint_end_step();
			assert(extended_checkpoint_passed(&objects[1], OPPONENT_CAR_INDEX));
			assert(!extended_checkpoint_passed(&objects[1], PLAYER_CAR_INDEX));
			assert(unit_checkpoint_can_advance(&route, 2));
			assert(!unit_checkpoint_guide(&route, 2, &state.opponentstate, &guidance));
			/* Existing replay state alone restores the frontier. */
			extended_replay_clocks = before;
			assert(!unit_checkpoint_can_advance(&route, 2));
		}
	}
}

static void test_missed_gate_recovery(void)
{
	for (legacy_u8 rotation = 0; rotation < 4U; rotation++) {
		reset(rotation, CSTRK_BLUE_CHECKPOINT_ID);
		state.opponentstate.car_position = position(TEST_MISSED_LATERAL, TEST_APPROACH);
		state.opponentstate.car_previous_position = position(TEST_MISSED_LATERAL, -TEST_APPROACH);
		assert(!extended_checkpoint_crossed(&track, &objects[1],
											&state.opponentstate.car_previous_position,
											&state.opponentstate.car_position));
		struct UNIT_PLAN first = plan(), second = plan();
		assert(unit_checkpoint_guide(&route, 2, &state.opponentstate, &first));
		assert(unit_checkpoint_guide(&route, 2, &state.opponentstate, &second));
		assert(first.target_x == second.target_x && first.target_z == second.target_z);
		assert(first.target_speed <= 20);
		legacy_f64 yaw = -state.opponentstate.car_rotate.x * UNIT_ANGLE_RADIANS;
		legacy_f64 dx = first.target_x - state.opponentstate.car_position.lx / UNIT_POSITION_SCALE;
		legacy_f64 dz = first.target_z - state.opponentstate.car_position.lz / UNIT_POSITION_SCALE;
		assert(fabs(dx * cos(yaw) - dz * sin(yaw)) > 1.0);
		assert(!unit_checkpoint_can_advance(&route, 2));
		/* Facing the gate from the far side must produce a real crossing. */
		state.opponentstate.car_position = position(0, TEST_APPROACH);
		state.opponentstate.car_rotate.x =
			LEGACY_S16_WRAP_ADD(state.opponentstate.car_rotate.x, ANGLE_HALF_TURN);
		first = plan();
		assert(unit_checkpoint_guide(&route, 2, &state.opponentstate, &first));
		struct VECTORLONG target = target_position(&first);
		assert(extended_checkpoint_crossed(&track, &objects[1], &state.opponentstate.car_position,
										   &target));
	}
}

static void test_approach_and_fallback(void)
{
	reset(0, CSTRK_COMBINED_CHECKPOINT_ID);
	state.opponentstate.car_position = position(0, -CSTRK_TILE_WORLD_SIZE);
	state.opponentstate.car_actual_speed = 10U * TEST_NORMAL_SPEED_SCALE;
	struct UNIT_PLAN guidance = plan();
	assert(!unit_checkpoint_guide(&route, 0, &state.opponentstate, &guidance));
	state.opponentstate.car_actual_speed = 200U * TEST_NORMAL_SPEED_SCALE;
	assert(unit_checkpoint_guide(&route, 0, &state.opponentstate, &guidance));
	guidance.brake = 1;
	state.opponentstate.car_position = position(TEST_MISSED_LATERAL, -TEST_APPROACH);
	assert(unit_checkpoint_guide(&route, 1, &state.opponentstate, &guidance));
	assert(guidance.brake && guidance.target_speed <= 40);
	struct VECTORLONG target = target_position(&guidance);
	assert(!extended_checkpoint_crossed(&track, &objects[1], &state.opponentstate.car_position,
										&target));
	track_active = 0;
	assert(unit_checkpoint_can_advance(&route, 2));
	assert(!unit_checkpoint_guide(&route, 2, &state.opponentstate, &guidance));
	assert(unit_checkpoint_can_advance(NULL, 0));
	assert(!unit_checkpoint_guide(NULL, 0, &state.opponentstate, &guidance));
}

static void test_offset_passage_preserved(void)
{
	for (legacy_u8 rotation = 0; rotation < 4U; rotation++) {
		reset(rotation, CSTRK_BLUE_CHECKPOINT_ID);
		state.playerstate.car_position = position(0, 0);
		state.opponentstate.car_position = position(60, -TEST_APPROACH);
		struct VECTORLONG destination = position(60, TEST_APPROACH);
		struct UNIT_PLAN guidance = plan();
		guidance.target_x = destination.lx / UNIT_POSITION_SCALE;
		guidance.target_y = destination.ly / UNIT_POSITION_SCALE - TEST_BODY_HEIGHT;
		guidance.target_z = destination.lz / UNIT_POSITION_SCALE;
		struct UNIT_PLAN saved = guidance;
		assert(extended_checkpoint_crossed(&track, &objects[1], &state.opponentstate.car_position,
										   &destination));
		assert(!unit_checkpoint_guide(&route, 1, &state.opponentstate, &guidance));
		assert(guidance.target_x == saved.target_x && guidance.target_z == saved.target_z &&
			   guidance.target_speed == saved.target_speed);
		/* The center alone can fit while the body would clip a pole. */
		state.opponentstate.car_position = position(110, -TEST_APPROACH);
		destination = position(110, TEST_APPROACH);
		guidance.target_x = destination.lx / UNIT_POSITION_SCALE;
		guidance.target_y = destination.ly / UNIT_POSITION_SCALE - TEST_BODY_HEIGHT;
		guidance.target_z = destination.lz / UNIT_POSITION_SCALE;
		assert(extended_checkpoint_crossed(&track, &objects[1], &state.opponentstate.car_position,
										   &destination));
		assert(unit_checkpoint_guide(&route, 1, &state.opponentstate, &guidance));
		assert(guidance.target_speed <= 40);
	}
}

static void test_gate_route_alignment(void)
{
	reset(0, CSTRK_BLUE_CHECKPOINT_ID);
	unit_checkpoint_align_route(&route);
	assert(!points[0].align_center);
	assert(points[0].half_width == TEST_ROAD_HALF_WIDTH);
	for (legacy_u32 index = 1; index < TEST_POINT_COUNT - 1U; index++) {
		assert(points[index].align_center & UNIT_ALIGN_GATE);
		assert(points[index].half_width == CHECKPOINT_GATE_INNER_HALF_WIDTH);
	}
	assert(!points[TEST_POINT_COUNT - 1U].align_center);
	track_active = 0;
	memset(points, 0, sizeof(points));
	unit_checkpoint_align_route(&route);
	assert(!points[0].align_center && !points[1].align_center);
	unit_checkpoint_align_route(NULL);
}

static void test_alignment_keeps_moving_forward(void)
{
	for (legacy_u8 rotation = 0; rotation < 4U; rotation++) {
		reset(rotation, CSTRK_BLUE_CHECKPOINT_ID);
		state.opponentstate.car_position = position(TEST_MISSED_LATERAL, -TEST_ALIGN_APPROACH);
		struct VECTORLONG destination = position(TEST_MISSED_LATERAL, -TEST_ALIGN_APPROACH / 2);
		struct UNIT_PLAN guidance = plan();
		guidance.target_x = destination.lx / UNIT_POSITION_SCALE;
		guidance.target_y = destination.ly / UNIT_POSITION_SCALE - TEST_BODY_HEIGHT;
		guidance.target_z = destination.lz / UNIT_POSITION_SCALE;
		assert(unit_checkpoint_guide(&route, 1, &state.opponentstate, &guidance));
		legacy_f64 yaw = -state.opponentstate.car_rotate.x * UNIT_ANGLE_RADIANS;
		legacy_f64 dx =
			guidance.target_x - state.opponentstate.car_position.lx / UNIT_POSITION_SCALE;
		legacy_f64 dz =
			guidance.target_z - state.opponentstate.car_position.lz / UNIT_POSITION_SCALE;
		assert(dx * sin(yaw) + dz * cos(yaw) > 0.0);
		assert(guidance.target_speed <= 40);
	}
}

static void test_safe_approach_preserved(void)
{
	for (legacy_u8 rotation = 0; rotation < 4U; rotation++) {
		reset(rotation, CSTRK_BLUE_CHECKPOINT_ID);
		state.opponentstate.car_position = position(60, -TEST_ALIGN_APPROACH);
		struct VECTORLONG destination = position(60, -TEST_ALIGN_APPROACH / 2);
		struct UNIT_PLAN guidance = plan();
		guidance.target_x = destination.lx / UNIT_POSITION_SCALE;
		guidance.target_y = destination.ly / UNIT_POSITION_SCALE - TEST_BODY_HEIGHT;
		guidance.target_z = destination.lz / UNIT_POSITION_SCALE;
		struct UNIT_PLAN saved = guidance;
		assert(!unit_checkpoint_guide(&route, 1, &state.opponentstate, &guidance));
		assert(guidance.target_x == saved.target_x && guidance.target_z == saved.target_z);
		/* A long car lining up with the gate is bounded by its corner radius,
		 * even if a linear angular-error estimate exceeds the whole opening. */
		route.limits.half_width = TEST_OBLIQUE_HALF_WIDTH;
		route.limits.half_length = TEST_OBLIQUE_HALF_LENGTH;
		state.opponentstate.car_position = position(0, -TEST_ALIGN_APPROACH);
		state.opponentstate.car_rotate.x =
			LEGACY_S16_WRAP_ADD(state.opponentstate.car_rotate.x, ANGLE_QUARTER_TURN / 3);
		destination = position(0, TEST_ALIGN_APPROACH);
		guidance.target_x = destination.lx / UNIT_POSITION_SCALE;
		guidance.target_y = destination.ly / UNIT_POSITION_SCALE - TEST_BODY_HEIGHT;
		guidance.target_z = destination.lz / UNIT_POSITION_SCALE;
		assert(!unit_checkpoint_guide(&route, 1, &state.opponentstate, &guidance));
	}
}

static void test_centered_reverse_approach_turns(void)
{
	for (legacy_u8 rotation = 0; rotation < 4U; rotation++) {
		reset(rotation, CSTRK_BLUE_CHECKPOINT_ID);
		state.opponentstate.car_position = position(0, TEST_APPROACH);
		struct VECTORLONG destination = position(0, -TEST_APPROACH);
		struct UNIT_PLAN guidance = plan();
		guidance.target_x = destination.lx / UNIT_POSITION_SCALE;
		guidance.target_y = destination.ly / UNIT_POSITION_SCALE - TEST_BODY_HEIGHT;
		guidance.target_z = destination.lz / UNIT_POSITION_SCALE;
		assert(unit_checkpoint_guide(&route, 2, &state.opponentstate, &guidance));
		legacy_f64 yaw = -state.opponentstate.car_rotate.x * UNIT_ANGLE_RADIANS;
		legacy_f64 dx =
			guidance.target_x - state.opponentstate.car_position.lx / UNIT_POSITION_SCALE;
		legacy_f64 dz =
			guidance.target_z - state.opponentstate.car_position.lz / UNIT_POSITION_SCALE;
		assert(fabs(dx * cos(yaw) - dz * sin(yaw)) > 1.0);
	}
}

legacy_int main(void)
{
	test_physical_crossing();
	test_missed_gate_recovery();
	test_approach_and_fallback();
	test_offset_passage_preserved();
	test_gate_route_alignment();
	test_alignment_keeps_moving_forward();
	test_safe_approach_preserved();
	test_centered_reverse_approach_turns();
	return 0;
}
