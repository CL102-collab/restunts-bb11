#include <assert.h>
#include <string.h>
#include "../c/externs.h"
#include "../c/crash_state.h"
#include "../c/extended/extended_track.h"
#include "../c/extended/extended_replay_runtime.h"
#include "../c/extended/extended_checkpoint.h"
#include "../c/extended/checkpoint_shape.h"
#undef memcpy

#define TEST_OBJECT_CAPACITY 5U
#define TEST_TRACK_SIDE 90U
#define TEST_HIGH_COLUMN 70U
#define TEST_GATE_ROW 7U
#define TEST_FINISH_ROW 4U
#define TEST_GATE_APPROACH 200
#define TEST_CAR_HEIGHT 20
#define TEST_UPPER_OPENING_HEIGHT 60
#define TEST_UPPER_OPENING_LATERAL 123

struct GAMESTATE state;
struct GAMEINFO gameconfig;
struct EXTENDED_REPLAY_CLOCKS extended_replay_clocks;
legacy_s8 start_finish_column = TEST_HIGH_COLUMN, start_finish_row = TEST_FINISH_ROW;
legacy_s16 track_angle;
static struct CSTRK_OBJECT objects[TEST_OBJECT_CAPACITY];
static struct CSTRK_TRACK track;
static legacy_s16 track_active = 1, replay_active = 1;

legacy_int cstrk_is_checkpoint(legacy_u16 id)
{
	return id == CSTRK_BLUE_CHECKPOINT_ID || id == CSTRK_RED_CHECKPOINT_ID;
}

legacy_int extended_track_active(void)
{
	return track_active;
}
legacy_int extended_replay_is_active(void)
{
	return replay_active;
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

void update_crash_state(legacy_s16 event, legacy_s16 car_index)
{
	assert(event == CRASH_EVENT_FINISH);
	assert(extended_checkpoint_finish_allowed(car_index));
	struct CARSTATE *car =
		car_index == PLAYER_CAR_INDEX ? &state.playerstate : &state.opponentstate;
	car->car_crashBmpFlag = (legacy_s8)event;
}

static struct CSTRK_OBJECT test_object(legacy_u16 id, legacy_u8 row)
{
	struct CSTRK_OBJECT object = {0};
	object.legacy_id = id;
	object.x = TEST_HIGH_COLUMN;
	object.y = row;
	object.width = 1U;
	object.height = 1U;
	object.occupied_mask = 1U;
	return object;
}

static struct VECTORLONG test_position(const struct CSTRK_OBJECT *object, legacy_s32 lateral,
									   legacy_s32 height, legacy_s32 depth)
{
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
		(support + height) * EXTENDED_CHECKPOINT_POSITION_SCALE,
		(((legacy_s32)track.height - object->y) * CSTRK_TILE_WORLD_SIZE -
		 CSTRK_TILE_WORLD_SIZE / 2 + dz) *
			EXTENDED_CHECKPOINT_POSITION_SCALE};
	return result;
}

static void reset_fixture(legacy_u16 blue, legacy_u16 red)
{
	memset(&state, 0, sizeof(state));
	memset(&gameconfig, 0, sizeof(gameconfig));
	memset(&extended_replay_clocks, 0, sizeof(extended_replay_clocks));
	memset(&track, 0, sizeof(track));
	memset(objects, 0, sizeof(objects));
	track.width = TEST_TRACK_SIDE;
	track.height = TEST_TRACK_SIDE;
	track.objects = objects;
	objects[0] = test_object(CHECKPOINT_START_FINISH_ID, TEST_FINISH_ROW);
	track.object_count = 1U;
	for (legacy_u16 index = 0; index < blue + red; index++) {
		objects[track.object_count++] =
			test_object(index < blue ? CSTRK_BLUE_CHECKPOINT_ID : CSTRK_RED_CHECKPOINT_ID,
						(legacy_u8)(TEST_GATE_ROW + index));
	}
	state.playerstate.car_position =
		test_position(&objects[0], 0, TEST_CAR_HEIGHT, -TEST_GATE_APPROACH);
	state.opponentstate.car_position = state.playerstate.car_position;
	state.playerstate.car_lap_count = 1;
	state.opponentstate.car_lap_count = 1;
	track_angle = 0;
	track_active = replay_active = 1;
}

static void sweep_gate(legacy_u16 index, legacy_s32 lateral, legacy_s32 height, legacy_s16 reverse)
{
	const struct CSTRK_OBJECT *object = &objects[index];
	legacy_s32 depth = reverse ? TEST_GATE_APPROACH : -TEST_GATE_APPROACH;
	state.playerstate.car_position = test_position(object, lateral, height, depth);
	extended_checkpoint_begin_step();
	state.playerstate.car_position = test_position(object, lateral, height, -depth);
	extended_checkpoint_end_step();
}

static legacy_s16 finish_attempt(void)
{
	sweep_gate(0U, 0, TEST_CAR_HEIGHT, 0);
	return state.playerstate.car_crashBmpFlag == CRASH_EVENT_FINISH;
}

static void test_aperture(void)
{
	reset_fixture(1U, 0U);
	struct CSTRK_OBJECT object = objects[1];
	for (legacy_u16 rotation = 0U; rotation < 4U; rotation++) {
		object.rotation = (legacy_u8)rotation;
		for (legacy_u16 surface = CSTRK_ROAD_PAVED; surface <= CSTRK_ROAD_ICE; surface++) {
			object.surface = (legacy_u8)surface;
			struct VECTORLONG before =
				test_position(&object, 0, TEST_CAR_HEIGHT, -TEST_GATE_APPROACH);
			struct VECTORLONG after =
				test_position(&object, 0, TEST_CAR_HEIGHT, TEST_GATE_APPROACH);
			assert(extended_checkpoint_crossed(&track, &object, &before, &after));
			assert(extended_checkpoint_crossed(&track, &object, &after, &before));
			assert(!extended_checkpoint_crossed(&track, &object, &before, &before));
			for (legacy_s16 side = -1; side <= 1; side += 2) {
				before = test_position(&object, side * CHECKPOINT_GATE_INNER_HALF_WIDTH,
									   TEST_CAR_HEIGHT, -TEST_GATE_APPROACH);
				after = test_position(&object, side * CHECKPOINT_GATE_INNER_HALF_WIDTH,
									  TEST_CAR_HEIGHT, TEST_GATE_APPROACH);
				assert(!extended_checkpoint_crossed(&track, &object, &before, &after));
			}
			before = test_position(&object, TEST_UPPER_OPENING_LATERAL, TEST_UPPER_OPENING_HEIGHT,
								   -TEST_GATE_APPROACH);
			after = test_position(&object, TEST_UPPER_OPENING_LATERAL, TEST_UPPER_OPENING_HEIGHT,
								  TEST_GATE_APPROACH);
			assert(extended_checkpoint_crossed(&track, &object, &before, &after));
			before = test_position(&object, TEST_UPPER_OPENING_LATERAL, TEST_CAR_HEIGHT,
								   -TEST_GATE_APPROACH);
			after = test_position(&object, TEST_UPPER_OPENING_LATERAL, TEST_CAR_HEIGHT,
								  TEST_GATE_APPROACH);
			assert(!extended_checkpoint_crossed(&track, &object, &before, &after));
			before = test_position(&object, CHECKPOINT_GATE_UPPER_INNER_HALF_WIDTH,
								   TEST_UPPER_OPENING_HEIGHT, -TEST_GATE_APPROACH);
			after = test_position(&object, CHECKPOINT_GATE_UPPER_INNER_HALF_WIDTH,
								  TEST_UPPER_OPENING_HEIGHT, TEST_GATE_APPROACH);
			assert(!extended_checkpoint_crossed(&track, &object, &before, &after));
			before = test_position(&object, 0, CHECKPOINT_GATE_SIGN_BOTTOM, -TEST_GATE_APPROACH);
			after = test_position(&object, 0, CHECKPOINT_GATE_SIGN_BOTTOM, TEST_GATE_APPROACH);
			assert(!extended_checkpoint_crossed(&track, &object, &before, &after));
			before = test_position(&object, 0, -1, -TEST_GATE_APPROACH);
			after = test_position(&object, 0, -1, TEST_GATE_APPROACH);
			assert(!extended_checkpoint_crossed(&track, &object, &before, &after));
			before = test_position(&object, -TEST_GATE_APPROACH, TEST_CAR_HEIGHT,
								   -CSTRK_TILE_WORLD_SIZE);
			after =
				test_position(&object, TEST_GATE_APPROACH, TEST_CAR_HEIGHT, CSTRK_TILE_WORLD_SIZE);
			assert(extended_checkpoint_crossed(&track, &object, &before, &after));
		}
	}
	object.rotation = 0U;
	object.base_height = CSTRK_MAX_HEIGHT;
	object.slope_x = 1;
	object.slope_y = -1;
	struct VECTORLONG before = test_position(&object, 0, TEST_CAR_HEIGHT, -CSTRK_TILE_WORLD_SIZE);
	struct VECTORLONG after = test_position(&object, 0, TEST_CAR_HEIGHT, CSTRK_TILE_WORLD_SIZE);
	assert(extended_checkpoint_crossed(&track, &object, &before, &after));
}

static void test_color_alternatives(void)
{
	reset_fixture(1U, 0U);
	assert(!finish_attempt());
	sweep_gate(1U, 0, TEST_CAR_HEIGHT, 1);
	assert(extended_checkpoint_passed(&objects[1], PLAYER_CAR_INDEX));
	assert(finish_attempt());

	reset_fixture(0U, 1U);
	assert(!finish_attempt());
	sweep_gate(1U, 0, TEST_CAR_HEIGHT, 0);
	assert(finish_attempt());

	reset_fixture(2U, 1U);
	sweep_gate(1U, 0, TEST_CAR_HEIGHT, 0);
	assert(!finish_attempt());
	sweep_gate(1U, 0, TEST_CAR_HEIGHT, 1);
	assert(!finish_attempt());
	sweep_gate(3U, 0, TEST_CAR_HEIGHT, 1);
	assert(finish_attempt());

	reset_fixture(2U, 1U);
	sweep_gate(1U, 0, TEST_CAR_HEIGHT, 0);
	sweep_gate(2U, 0, TEST_CAR_HEIGHT, 1);
	assert(finish_attempt());

	reset_fixture(0U, 0U);
	assert(extended_checkpoint_finish_allowed(PLAYER_CAR_INDEX));
	track_active = 0;
	assert(extended_checkpoint_finish_allowed(PLAYER_CAR_INDEX));
	reset_fixture(1U, 0U);
	replay_active = 0;
	assert(extended_checkpoint_finish_allowed(PLAYER_CAR_INDEX));
}

static void test_finish_rotation_and_opponent(void)
{
	for (legacy_u16 rotation = 0U; rotation < 4U; rotation++) {
		reset_fixture(1U, 0U);
		objects[0].rotation = (legacy_u8)rotation;
		track_angle = (legacy_s16)(rotation * ANGLE_QUARTER_TURN);
		sweep_gate(1U, 0, TEST_CAR_HEIGHT, 0);
		sweep_gate(0U, 0, TEST_CAR_HEIGHT, 1);
		assert(state.playerstate.car_crashBmpFlag == CRASH_EVENT_NONE);
		assert(finish_attempt());
	}

	reset_fixture(1U, 0U);
	gameconfig.game_opponenttype = 1;
	state.opponentstate.car_position =
		test_position(&objects[1], 0, TEST_CAR_HEIGHT, -TEST_GATE_APPROACH);
	extended_checkpoint_begin_step();
	state.opponentstate.car_position =
		test_position(&objects[1], 0, TEST_CAR_HEIGHT, TEST_GATE_APPROACH);
	extended_checkpoint_end_step();
	assert(extended_checkpoint_passed(&objects[1], OPPONENT_CAR_INDEX));
	assert(!extended_checkpoint_passed(&objects[1], PLAYER_CAR_INDEX));
	state.opponentstate.car_position =
		test_position(&objects[0], 0, TEST_CAR_HEIGHT, -TEST_GATE_APPROACH);
	extended_checkpoint_begin_step();
	state.opponentstate.car_position =
		test_position(&objects[0], 0, TEST_CAR_HEIGHT, TEST_GATE_APPROACH);
	extended_checkpoint_end_step();
	assert(state.opponentstate.car_crashBmpFlag == CRASH_EVENT_FINISH);
	assert(!finish_attempt());

	reset_fixture(2U, 2U);
	sweep_gate(1U, 0, TEST_CAR_HEIGHT, 0);
	sweep_gate(3U, 0, TEST_CAR_HEIGHT, 0);
	assert(!finish_attempt());
	sweep_gate(4U, 0, TEST_CAR_HEIGHT, 0);
	assert(finish_attempt());
}

static void test_real_crossings_and_snapshot(void)
{
	reset_fixture(1U, 0U);
	sweep_gate(1U, CHECKPOINT_GATE_OUTER_HALF_WIDTH, TEST_CAR_HEIGHT, 0);
	assert(!extended_checkpoint_passed(&objects[1], PLAYER_CAR_INDEX));
	sweep_gate(1U, 0, CHECKPOINT_GATE_SIGN_TOP, 0);
	assert(!extended_checkpoint_passed(&objects[1], PLAYER_CAR_INDEX));
	assert(!finish_attempt());
	struct EXTENDED_REPLAY_CLOCKS snapshot = extended_replay_clocks;
	sweep_gate(1U, 0, TEST_CAR_HEIGHT, 0);
	assert(extended_checkpoint_passed(&objects[1], PLAYER_CAR_INDEX));
	assert(!extended_checkpoint_passed(&objects[1], OPPONENT_CAR_INDEX));
	assert(finish_attempt());
	extended_replay_clocks = snapshot;
	state.playerstate.car_crashBmpFlag = CRASH_EVENT_NONE;
	assert(!extended_checkpoint_passed(&objects[1], PLAYER_CAR_INDEX));
	assert(!finish_attempt());
	sweep_gate(1U, 0, TEST_CAR_HEIGHT, 0);
	sweep_gate(0U, CHECKPOINT_GATE_OUTER_HALF_WIDTH, TEST_CAR_HEIGHT, 0);
	assert(state.playerstate.car_crashBmpFlag == CRASH_EVENT_NONE);
	sweep_gate(0U, 0, CHECKPOINT_GATE_SIGN_TOP, 0);
	assert(state.playerstate.car_crashBmpFlag == CRASH_EVENT_NONE);
	assert(finish_attempt());
}

static void test_finish_order_in_fast_tick(void)
{
	reset_fixture(1U, 0U);
	objects[1].y = TEST_FINISH_ROW + 1U; /* Gate is south of finish: crossed first. */
	state.playerstate.car_position =
		test_position(&objects[1], 0, TEST_CAR_HEIGHT, -TEST_GATE_APPROACH);
	extended_checkpoint_begin_step();
	state.playerstate.car_position =
		test_position(&objects[0], 0, TEST_CAR_HEIGHT, TEST_GATE_APPROACH);
	extended_checkpoint_end_step();
	assert(state.playerstate.car_crashBmpFlag == CRASH_EVENT_FINISH);

	reset_fixture(1U, 0U);
	objects[1].y = TEST_FINISH_ROW - 1U;
	state.playerstate.car_position =
		test_position(&objects[0], 0, TEST_CAR_HEIGHT, -TEST_GATE_APPROACH);
	extended_checkpoint_begin_step();
	state.playerstate.car_position =
		test_position(&objects[1], 0, TEST_CAR_HEIGHT, TEST_GATE_APPROACH);
	extended_checkpoint_end_step();
	assert(state.playerstate.car_crashBmpFlag == CRASH_EVENT_NONE);
	assert(extended_checkpoint_passed(&objects[1], PLAYER_CAR_INDEX));
	assert(finish_attempt());
}

legacy_int main(void)
{
	test_aperture();
	test_color_alternatives();
	test_finish_rotation_and_opponent();
	test_real_crossings_and_snapshot();
	test_finish_order_in_fast_tick();
	return 0;
}
