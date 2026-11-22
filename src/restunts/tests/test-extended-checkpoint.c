#include <assert.h>
#include <string.h>
#include "../c/externs.h"
#include "../c/gamestate.h"
#include "../c/crash_state.h"
#include "../c/extended/extended_track.h"
#include "../c/extended/extended_replay_runtime.h"
#include "../c/extended/extended_checkpoint.h"
#include "../c/extended/checkpoint_shape.h"
#undef memcpy

#define TEST_OBJECT_CAPACITY 5U
#define TEST_ROTATION_COUNT 4U
#define TEST_TRACK_SIDE 90U
#define TEST_HIGH_COLUMN 70U
#define TEST_GATE_ROW 7U
#define TEST_FINISH_ROW 4U
#define TEST_GATE_APPROACH 200
#define TEST_OUTSIDE_TILE (CSTRK_TILE_WORLD_SIZE / 2 + TEST_GATE_APPROACH)
#define TEST_CORNER_OFFSET (TEST_GATE_APPROACH / 2)
#define TEST_PASSED_FEEDBACK_SECONDS 2U
#define TEST_FAILED_FEEDBACK_SECONDS 3U
#define TEST_CAR_HEIGHT 20
#define TEST_UPPER_OPENING_HEIGHT 60
#define TEST_UPPER_OPENING_LATERAL 123

struct GAMESTATE state;
struct GAMEINFO gameconfig;
struct EXTENDED_REPLAY_CLOCKS extended_replay_clocks;
legacy_s8 start_finish_column = TEST_HIGH_COLUMN, start_finish_row = TEST_FINISH_ROW;
legacy_s16 track_angle;
legacy_u16 framespersec;
static struct CSTRK_OBJECT objects[TEST_OBJECT_CAPACITY];
static struct CSTRK_TRACK track;
static legacy_s16 track_active = 1, replay_active = 1;

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
	framespersec = GAME_FRAME_RATE_NORMAL;
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

static legacy_u16 add_combined_gate(legacy_u8 rotation, legacy_u8 surface)
{
	legacy_u16 index = (legacy_u16)track.object_count;
	assert(index < TEST_OBJECT_CAPACITY);
	objects[index] = test_object(CSTRK_COMBINED_CHECKPOINT_ID, (legacy_u8)(TEST_GATE_ROW + index));
	objects[index].rotation = rotation;
	objects[index].surface = surface;
	track.object_count++;
	return index;
}

static void test_combined_checkpoints(void)
{
	for (legacy_u8 rotation = 0; rotation < 4U; rotation++) {
		for (legacy_u8 surface = CSTRK_ROAD_PAVED; surface <= CSTRK_ROAD_ICE; surface++) {
			for (legacy_u16 chosen = 1U; chosen <= 2U; chosen++) {
				reset_fixture(1U, 1U);
				legacy_u16 shared = add_combined_gate(rotation, surface);
				sweep_gate(chosen, 0, TEST_CAR_HEIGHT, 0);
				assert(!finish_attempt());
				sweep_gate(shared, 0, TEST_CAR_HEIGHT, rotation & 1U);
				assert(extended_checkpoint_passed(&objects[shared], PLAYER_CAR_INDEX));
				assert(finish_attempt());
			}
		}
	}

	/* A shared gate itself introduces both colors. The other color's set
	 * can therefore consist entirely of shared gates. */
	reset_fixture(1U, 0U);
	legacy_u16 shared = add_combined_gate(0U, CSTRK_ROAD_PAVED);
	sweep_gate(shared, 0, TEST_CAR_HEIGHT, 0);
	assert(!extended_checkpoint_passed(&objects[1], PLAYER_CAR_INDEX));
	assert(finish_attempt());
	reset_fixture(0U, 1U);
	shared = add_combined_gate(0U, CSTRK_ROAD_ICE);
	sweep_gate(shared, 0, TEST_CAR_HEIGHT, 1);
	assert(!extended_checkpoint_passed(&objects[1], PLAYER_CAR_INDEX));
	assert(finish_attempt());

	reset_fixture(0U, 0U);
	legacy_u16 first = add_combined_gate(0U, CSTRK_ROAD_PAVED);
	legacy_u16 second = add_combined_gate(0U, CSTRK_ROAD_DIRT);
	assert(!finish_attempt());
	sweep_gate(first, 0, TEST_CAR_HEIGHT, 0);
	sweep_gate(first, 0, TEST_CAR_HEIGHT, 1);
	assert(!finish_attempt());
	struct EXTENDED_REPLAY_CLOCKS snapshot = extended_replay_clocks;
	sweep_gate(second, 0, TEST_CAR_HEIGHT, 1);
	assert(finish_attempt());
	extended_replay_clocks = snapshot;
	state.playerstate.car_crashBmpFlag = CRASH_EVENT_NONE;
	assert(!finish_attempt());
	sweep_gate(second, 0, TEST_CAR_HEIGHT, 0);
	assert(finish_attempt());
}

static void test_slalom_checkpoint_colors(void)
{
	for (legacy_u8 rotation = 0; rotation < TEST_ROTATION_COUNT; rotation++) {
		for (legacy_u16 chosen = 1U; chosen <= 2U; chosen++) {
			reset_fixture(1U, 1U);
			objects[1].legacy_id = CSTRK_SLALOM_BLUE_CHECKPOINT_ID;
			objects[2].legacy_id = CSTRK_SLALOM_RED_CHECKPOINT_ID;
			legacy_u16 shared = add_combined_gate(rotation, CSTRK_ROAD_ICE);
			objects[shared].legacy_id = CSTRK_SLALOM_COMBINED_CHECKPOINT_ID;
			objects[shared].base_height = CSTRK_MAX_HEIGHT - 1U;
			if (rotation & 1U) {
				objects[shared].slope_x = 1;
			} else {
				objects[shared].slope_y = 1;
			}
			sweep_gate(chosen, 0, TEST_CAR_HEIGHT, 0);
			assert(!finish_attempt());
			sweep_gate(shared, CHECKPOINT_GATE_INNER_HALF_WIDTH, TEST_CAR_HEIGHT, 0);
			assert(!extended_checkpoint_passed(&objects[shared], PLAYER_CAR_INDEX));
			sweep_gate(shared, 0, TEST_CAR_HEIGHT, 1);
			assert(extended_checkpoint_passed(&objects[shared], PLAYER_CAR_INDEX));
			assert(finish_attempt());
		}
	}
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

static enum EXTENDED_CHECKPOINT_FEEDBACK feedback_tick(struct VECTORLONG position)
{
	extended_checkpoint_begin_step();
	state.playerstate.car_position = position;
	extended_replay_clocks.frame++;
	extended_checkpoint_end_step();
	return extended_checkpoint_feedback();
}

static void assert_feedback_duration(enum EXTENDED_CHECKPOINT_FEEDBACK feedback, legacy_u16 seconds)
{
	assert(extended_checkpoint_feedback() == feedback);
	legacy_u16 duration = framespersec * seconds;
	for (legacy_u16 elapsed = 1U; elapsed < duration; elapsed++) {
		assert(feedback_tick(state.playerstate.car_position) == feedback);
	}
	assert(feedback_tick(state.playerstate.car_position) == EXTENDED_CHECKPOINT_FEEDBACK_NONE);
}

static void test_checkpoint_feedback_visit(void)
{
	static const legacy_u16 rates[] = {GAME_FRAME_RATE_LOW, GAME_FRAME_RATE_NORMAL};
	for (legacy_u16 rate = 0; rate < sizeof(rates) / sizeof(rates[0]); rate++) {
		reset_fixture(1U, 0U);
		framespersec = rates[rate];
		const struct CSTRK_OBJECT *gate = &objects[1];
		struct VECTORLONG outside = test_position(gate, 0, TEST_CAR_HEIGHT, -TEST_OUTSIDE_TILE);
		struct VECTORLONG inside = test_position(gate, 0, TEST_CAR_HEIGHT, -TEST_GATE_APPROACH);
		state.playerstate.car_position = outside;
		assert(feedback_tick(inside) == EXTENDED_CHECKPOINT_FEEDBACK_NONE);
		struct EXTENDED_REPLAY_CLOCKS snapshot = extended_replay_clocks;
		assert(feedback_tick(inside) == EXTENDED_CHECKPOINT_FEEDBACK_NONE);
		assert(feedback_tick(outside) == EXTENDED_CHECKPOINT_FEEDBACK_FAILED);
		assert_feedback_duration(EXTENDED_CHECKPOINT_FEEDBACK_FAILED, TEST_FAILED_FEEDBACK_SECONDS);

		extended_replay_clocks = snapshot;
		state.playerstate.car_position = inside;
		assert(feedback_tick(outside) == EXTENDED_CHECKPOINT_FEEDBACK_FAILED);
		assert(feedback_tick(inside) == EXTENDED_CHECKPOINT_FEEDBACK_FAILED);
		assert(feedback_tick(test_position(gate, 0, TEST_CAR_HEIGHT, 0)) ==
			   EXTENDED_CHECKPOINT_FEEDBACK_PASSED);
		assert_feedback_duration(EXTENDED_CHECKPOINT_FEEDBACK_PASSED, TEST_PASSED_FEEDBACK_SECONDS);

		struct VECTORLONG beyond = test_position(gate, 0, TEST_CAR_HEIGHT, TEST_OUTSIDE_TILE);
		assert(feedback_tick(beyond) == EXTENDED_CHECKPOINT_FEEDBACK_NONE);
		assert(feedback_tick(outside) == EXTENDED_CHECKPOINT_FEEDBACK_PASSED);
		assert(extended_checkpoint_passed(gate, PLAYER_CAR_INDEX));
		assert_feedback_duration(EXTENDED_CHECKPOINT_FEEDBACK_PASSED, TEST_PASSED_FEEDBACK_SECONDS);
		assert(feedback_tick(inside) == EXTENDED_CHECKPOINT_FEEDBACK_NONE);
		assert(feedback_tick(outside) == EXTENDED_CHECKPOINT_FEEDBACK_FAILED);
	}
}

static void test_checkpoint_feedback_swept_tile(void)
{
	reset_fixture(1U, 0U);
	const struct CSTRK_OBJECT *gate = &objects[1];
	legacy_s16 corner = -(CSTRK_TILE_WORLD_SIZE / 2);
	state.playerstate.car_position = test_position(gate, corner - TEST_CORNER_OFFSET,
												   TEST_CAR_HEIGHT, corner + TEST_CORNER_OFFSET);
	assert(feedback_tick(test_position(gate, corner + TEST_CORNER_OFFSET, TEST_CAR_HEIGHT,
									   corner - TEST_CORNER_OFFSET)) ==
		   EXTENDED_CHECKPOINT_FEEDBACK_NONE);

	/* A frame boundary at the exact corner must not turn the touch into a visit. */
	state.playerstate.car_position = test_position(gate, corner - TEST_CORNER_OFFSET,
												   TEST_CAR_HEIGHT, corner + TEST_CORNER_OFFSET);
	assert(feedback_tick(test_position(gate, corner, TEST_CAR_HEIGHT, corner)) ==
		   EXTENDED_CHECKPOINT_FEEDBACK_NONE);
	assert(feedback_tick(test_position(gate, corner + TEST_CORNER_OFFSET, TEST_CAR_HEIGHT,
									   corner - TEST_CORNER_OFFSET)) ==
		   EXTENDED_CHECKPOINT_FEEDBACK_NONE);

	state.playerstate.car_position =
		test_position(gate, 0, CHECKPOINT_GATE_SIGN_BOTTOM, -TEST_OUTSIDE_TILE);
	assert(feedback_tick(test_position(gate, 0, CHECKPOINT_GATE_SIGN_BOTTOM, TEST_OUTSIDE_TILE)) ==
		   EXTENDED_CHECKPOINT_FEEDBACK_FAILED);
	assert(!extended_checkpoint_passed(gate, PLAYER_CAR_INDEX));
	state.playerstate.car_position = test_position(gate, 0, TEST_CAR_HEIGHT, TEST_OUTSIDE_TILE);
	assert(feedback_tick(test_position(gate, 0, TEST_CAR_HEIGHT, -TEST_OUTSIDE_TILE)) ==
		   EXTENDED_CHECKPOINT_FEEDBACK_PASSED);
}

static void test_checkpoint_feedback_priority(void)
{
	reset_fixture(1U, 1U);
	/* One tick clears the red gate and then leaves the blue tile sideways. */
	objects[1].rotation = 1U;
	state.playerstate.car_position =
		test_position(&objects[2], 0, TEST_CAR_HEIGHT, -TEST_OUTSIDE_TILE);
	assert(feedback_tick(test_position(&objects[2], 0, TEST_CAR_HEIGHT,
									   CSTRK_TILE_WORLD_SIZE + TEST_OUTSIDE_TILE)) ==
		   EXTENDED_CHECKPOINT_FEEDBACK_PASSED);
	assert(extended_checkpoint_passed(&objects[2], PLAYER_CAR_INDEX));
	assert(!extended_checkpoint_passed(&objects[1], PLAYER_CAR_INDEX));

	reset_fixture(1U, 0U);
	gameconfig.game_opponenttype = 1;
	const struct CSTRK_OBJECT *gate = &objects[1];
	state.opponentstate.car_position = test_position(gate, 0, TEST_CAR_HEIGHT, -TEST_OUTSIDE_TILE);
	extended_checkpoint_begin_step();
	state.opponentstate.car_position = test_position(gate, 0, TEST_CAR_HEIGHT, TEST_OUTSIDE_TILE);
	extended_replay_clocks.frame++;
	extended_checkpoint_end_step();
	assert(extended_checkpoint_passed(gate, OPPONENT_CAR_INDEX));
	assert(extended_checkpoint_feedback() == EXTENDED_CHECKPOINT_FEEDBACK_NONE);
}

legacy_int main(void)
{
	test_aperture();
	test_color_alternatives();
	test_combined_checkpoints();
	test_slalom_checkpoint_colors();
	test_finish_rotation_and_opponent();
	test_real_crossings_and_snapshot();
	test_finish_order_in_fast_tick();
	test_checkpoint_feedback_visit();
	test_checkpoint_feedback_swept_tile();
	test_checkpoint_feedback_priority();
	return 0;
}
