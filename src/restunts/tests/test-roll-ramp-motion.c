#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../c/stateply.c"

static legacy_u8 fixture_extended = 1;
static legacy_u8 fixture_road_contact = 1;
static struct CSTRK_OBJECT fixture_object;
static struct PLANE fixture_plane;
static legacy_s16 fixture_bank;

legacy_s16 roll_ramp_orientation(const struct CSTRK_OBJECT *object)
{
	(void)object;
	return 0;
}

legacy_s16 roll_ramp_bank_angle(legacy_s16 distance)
{
	(void)distance;
	return fixture_bank;
}

void roll_ramp_sample(legacy_s16 distance, legacy_s16 lateral, struct VECTOR *point,
					  struct VECTOR *normal)
{
	(void)distance;
	(void)lateral;
	*point = (struct VECTOR){0, 0, 0};
	*normal = (struct VECTOR){-sin_fast(fixture_bank) / 2, cos_fast(fixture_bank) / 2, 0};
}

void extended_geometry_grade_normal(const struct CSTRK_OBJECT *object, struct VECTOR *normal)
{
	(void)object;
	(void)normal;
}

legacy_int extended_track_active(void)
{
	return fixture_extended;
}

legacy_int extended_track_height(void)
{
	return 1;
}

void extended_geometry_world_position(const struct VECTOR *point, legacy_s32 *x, legacy_s32 *z)
{
	*x = point->x;
	*z = point->z;
}

const struct CSTRK_OBJECT *extended_track_object_at(legacy_int column, legacy_int row)
{
	assert(column == 0 && row == 0);
	return &fixture_object;
}

void track_collision_capture(struct TRACK_COLLISION_SNAPSHOT *saved)
{
	memset(saved, 0, sizeof(*saved));
	saved->plane_index = planindex;
}

void track_collision_restore(const struct TRACK_COLLISION_SNAPSHOT *saved)
{
	planindex = saved->plane_index;
}

void build_track_object(struct VECTOR *point, struct VECTOR *next)
{
	(void)point;
	(void)next;
	planindex = EXTENDED_GEOMETRY_PLANE_INDEX + fixture_road_contact;
	fixture_plane.plane_normal =
		(struct VECTOR){-sin_fast(fixture_bank) / 2, cos_fast(fixture_bank) / 2, 0};
	current_planptr = &fixture_plane;
}

#define TEST_ROLL_STEP 16
#define TEST_ROLL_REPEAT_COUNT 64U
#define TEST_ROLL_Q8_FRACTION 1
#define TEST_ROLL_FRONT_SPEED 200
#define TEST_ROLL_REAR_SPEED (-100)
#define TEST_ROLL_MEAN_SPEED ((TEST_ROLL_FRONT_SPEED + TEST_ROLL_REAR_SPEED) / 2)

static struct CARSTATE supported_car(legacy_u16 rate, legacy_s16 angle)
{
	struct CARSTATE car = {0};
	framespersec = rate;
	player_motion_fraction = 0;
	fixture_object.roll_ramp = 1;
	for (legacy_u8 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
		car.car_surfaceWhl[wheel] = CAR_SURFACE_PAVED;
	}
	car.car_sumSurfFrontWheels = car.car_sumSurfRearWheels = PLAYER_PHYSICS_FRONT_WHEEL_COUNT;
	car_initial_roll = 0;
	car_working_roll = 0;
	car_working_yaw = ANGLE_HALF_TURN;
	car_working_pitch = 0;
	fixture_bank = 0;
	finish_player_roll_motion(&car);
	fixture_bank = angle;
	car_working_roll = angle;
	finish_player_roll_motion(&car);
	return car;
}

static void leave_ramp(struct CARSTATE *car)
{
	car->car_sumSurfFrontWheels = car->car_sumSurfRearWheels = CAR_WHEEL_CONTACT_NONE;
	for (legacy_u8 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
		car->car_surfaceWhl[wheel] = CAR_WHEEL_CONTACT_NONE;
	}
	car->car_wheel_vertical_speed[0] = TEST_ROLL_FRONT_SPEED - TEST_ROLL_STEP;
	car->car_wheel_vertical_speed[1] = TEST_ROLL_FRONT_SPEED + TEST_ROLL_STEP;
	car->car_wheel_vertical_speed[2] = TEST_ROLL_REAR_SPEED - TEST_ROLL_STEP;
	car->car_wheel_vertical_speed[3] = TEST_ROLL_REAR_SPEED + TEST_ROLL_STEP;
	finish_player_roll_motion(car);
	assert(car->car_reserved_contact_state[PLAYER_ROLL_PHASE] == PLAYER_ROLL_AIRBORNE);
	for (legacy_u8 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
		assert(car->car_wheel_vertical_speed[wheel] == TEST_ROLL_MEAN_SPEED);
	}
}

static void test_contact_derived_rate_and_free_rotation(void)
{
	struct CARSTATE car = supported_car(GAME_FRAME_RATE_NORMAL, TEST_ROLL_STEP);
	assert(car.car_reserved_contact_state[PLAYER_ROLL_VELOCITY] > 0);
	car.car_reserved_contact_state[PLAYER_ROLL_VELOCITY] =
		TEST_ROLL_STEP * PLAYER_ROLL_VELOCITY_SCALE;
	leave_ramp(&car);
	car_working_roll = 0;
	for (legacy_u16 step = 0; step < TEST_ROLL_REPEAT_COUNT; step++) {
		prepare_player_roll_motion(&car);
		assert(car_working_roll == (legacy_s16)((step + 1U) * TEST_ROLL_STEP));
	}
	assert(car_working_roll == ANGLE_FULL_TURN);
	/* The rate remains physical state: reaching upright does not stop it. */
	prepare_player_roll_motion(&car);
	assert(car_working_roll == ANGLE_FULL_TURN + TEST_ROLL_STEP);
}

static void test_rates_fractions_and_checkpoint_copy(void)
{
	struct CARSTATE normal = supported_car(GAME_FRAME_RATE_NORMAL, TEST_ROLL_STEP);
	struct CARSTATE low = supported_car(GAME_FRAME_RATE_LOW, TEST_ROLL_STEP * 2);
	legacy_s16 normal_rate = 0;
	framespersec = GAME_FRAME_RATE_NORMAL;
	normal_rate = player_roll_filter_rate(normal_rate, TEST_ROLL_STEP * PLAYER_ROLL_VELOCITY_SCALE);
	normal_rate = player_roll_filter_rate(normal_rate, TEST_ROLL_STEP * PLAYER_ROLL_VELOCITY_SCALE);
	framespersec = GAME_FRAME_RATE_LOW;
	assert(player_roll_filter_rate(0, TEST_ROLL_STEP * PLAYER_ROLL_VELOCITY_SCALE) == normal_rate);
	normal.car_reserved_contact_state[PLAYER_ROLL_VELOCITY] =
		TEST_ROLL_STEP * PLAYER_ROLL_VELOCITY_SCALE;
	low.car_reserved_contact_state[PLAYER_ROLL_VELOCITY] =
		TEST_ROLL_STEP * PLAYER_ROLL_VELOCITY_SCALE;
	leave_ramp(&low);
	car_working_roll = 0;
	prepare_player_roll_motion(&low);
	assert(car_working_roll == TEST_ROLL_STEP * 2);
	framespersec = GAME_FRAME_RATE_NORMAL;
	leave_ramp(&normal);
	player_motion_fraction = PHYSICS_FRACTION_ONE / 2;
	car_working_roll = 0;
	prepare_player_roll_motion(&normal);
	assert(car_working_roll == TEST_ROLL_STEP / 2);
	prepare_player_roll_motion(&normal);
	assert(car_working_roll == TEST_ROLL_STEP);
	player_motion_fraction = 0;
	normal.car_reserved_contact_state[PLAYER_ROLL_VELOCITY] = TEST_ROLL_Q8_FRACTION;
	normal.car_reserved_contact_state[PLAYER_ROLL_FRACTION] = 0;
	car_working_roll = 0;
	prepare_player_roll_motion(&normal);
	struct CARSTATE checkpoint = normal;
	for (legacy_u16 step = 1; step < PLAYER_ROLL_VELOCITY_SCALE; step++) {
		prepare_player_roll_motion(&normal);
	}
	assert(car_working_roll == 1);
	assert(normal.car_reserved_contact_state[PLAYER_ROLL_FRACTION] == 0);
	car_working_roll = 0;
	for (legacy_u16 step = 1; step < PLAYER_ROLL_VELOCITY_SCALE; step++) {
		prepare_player_roll_motion(&checkpoint);
	}
	assert(car_working_roll == 1);
	assert(memcmp(&normal, &checkpoint, sizeof(normal)) == 0);
}

static void test_contact_crash_and_legacy_gates(void)
{
	struct CARSTATE car = supported_car(GAME_FRAME_RATE_NORMAL, TEST_ROLL_STEP);
	leave_ramp(&car);
	fixture_object.roll_ramp = 0;
	car.car_surfaceWhl[0] = CAR_SURFACE_PAVED;
	car.car_sumSurfFrontWheels = 1;
	finish_player_roll_motion(&car);
	for (legacy_u8 word = 0; word < PLAYER_ROLL_WORD_COUNT; word++) {
		assert(car.car_reserved_contact_state[word] == 0);
	}
	car = supported_car(GAME_FRAME_RATE_NORMAL, -TEST_ROLL_STEP);
	assert(car.car_reserved_contact_state[PLAYER_ROLL_VELOCITY] < 0);
	car.car_reserved_contact_state[PLAYER_ROLL_VELOCITY] =
		-TEST_ROLL_STEP * PLAYER_ROLL_VELOCITY_SCALE;
	leave_ramp(&car);
	car_working_roll = 0;
	prepare_player_roll_motion(&car);
	assert(car_working_roll == -TEST_ROLL_STEP);
	fixture_extended = 0;
	struct CARSTATE legacy = car;
	prepare_player_roll_motion(&car);
	finish_player_roll_motion(&car);
	assert(car_working_roll == -TEST_ROLL_STEP);
	assert(memcmp(&legacy, &car, sizeof(car)) == 0);
	fixture_extended = 1;
	car.car_crashBmpFlag = CRASH_EVENT_COLLISION;
	prepare_player_roll_motion(&car);
	assert(car.car_reserved_contact_state[PLAYER_ROLL_PHASE] == PLAYER_ROLL_IDLE);
	/* Tile ownership and terrain material cannot arm a car under the grille. */
	fixture_road_contact = 0;
	car = supported_car(GAME_FRAME_RATE_NORMAL, TEST_ROLL_STEP);
	assert(car.car_reserved_contact_state[PLAYER_ROLL_PHASE] == PLAYER_ROLL_IDLE);
	fixture_road_contact = 1;
}

legacy_int main(void)
{
	test_contact_derived_rate_and_free_rotation();
	test_rates_fractions_and_checkpoint_copy();
	test_contact_crash_and_legacy_gates();
	puts("Roll-ramp contact momentum, unsupported rotation, time steps, and checkpoint state "
		 "passed.");
	return 0;
}
