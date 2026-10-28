#include <assert.h>
#include <stdio.h>

#include "../c/stateply.c"
#include "../c/math.c"

struct GAMESTATE state;
struct MATRIX car_to_world_rotation;
legacy_s16 car_working_pitch, car_working_roll, car_working_yaw;
legacy_u16 framespersec;
legacy_s16 planindex;
struct PLANE *planptr;
struct PLANE *current_planptr;

#define TEST_CORNERING_GRIP 500
#define TEST_CORNERING_SURFACE_GRIP 256
#define TEST_CORNERING_SPEED 25600U
#define TEST_CORNERING_STEERING_LIMIT 240
#define TEST_CORNERING_SAFE_STEERING 55
#define TEST_CORNERING_SLIDING_STEERING 56
#define TEST_CORNERING_SAFE_DEMAND 936
#define TEST_CORNERING_LEGACY_LEFT_HEADING (-14)
#define TEST_CORNERING_LEGACY_RIGHT_HEADING 13
#define TEST_CORNERING_LEGACY_LEFT_SLIDING_HEADING (-9)
#define TEST_CORNERING_LEGACY_RIGHT_SLIDING_HEADING 8
#define TEST_CORNERING_LEGACY_LEFT_SLIDING_SPEED 25554U
#define TEST_CORNERING_LEGACY_RIGHT_SLIDING_SPEED 25552U
#define TEST_CORNERING_TRACK_ROWS 30

legacy_s16 grassDecelDivTab[CAR_SURFACE_GRASS + 1];
legacy_s16 terrainrows[TEST_CORNERING_TRACK_ROWS];
legacy_u8 far *track_element_map;

static void configure_cornering(legacy_s8 *option)
{
	legacy_s8 *argv[] = {(legacy_s8 *)"restunts", option};
	configure_left_corner_bias((legacy_s16)(sizeof(argv) / sizeof(argv[0])), argv);
}

static legacy_s16 cornering_heading(legacy_s16 steering_angle, legacy_u16 speed,
									struct CARSTATE *car, struct PLAYER_WHEEL_MOTION *motion)
{
	struct CARSTATE initial_car = {0};
	struct SIMD simd = {0};
	*car = initial_car;
	car->car_sumSurfAllWheels = PLAYER_PHYSICS_WHEEL_COUNT;
	car->car_steeringAngle = steering_angle;
	car->car_actual_speed = speed;
	car->car_rev_speed = speed;
	simd.grip = TEST_CORNERING_GRIP;
	simd.sliding = TEST_CORNERING_SURFACE_GRIP;
	for (legacy_u16 index = 0; index < SIMD_SURFACE_GRIP_COUNT; index++) {
		simd.surface_grip[index] = TEST_CORNERING_SURFACE_GRIP;
	}
	for (legacy_u16 index = 0; index < PLAYER_PHYSICS_WHEEL_COUNT; index++) {
		car->car_surfaceWhl[index] = CAR_SURFACE_PAVED;
	}
	update_grip(car, &simd, GRIP_BEHAVIOR_PLAYER);
	return prepare_wheel_travel(car, motion);
}

static void test_original_cornering_bias(void)
{
	struct CARSTATE left;
	struct CARSTATE right;
	struct PLAYER_WHEEL_MOTION motion = {0};
	framespersec = GAME_FRAME_RATE_NORMAL;
	player_motion_fraction = 0;
	configure_cornering((legacy_s8 *)"--lcb:on");
	/* Equal tire demand still produces a larger wheel angle for a left turn. */
	assert(cornering_heading(-TEST_CORNERING_SAFE_STEERING, TEST_CORNERING_SPEED, &left, &motion) ==
		   TEST_CORNERING_LEGACY_LEFT_HEADING);
	assert(cornering_heading(TEST_CORNERING_SAFE_STEERING, TEST_CORNERING_SPEED, &right, &motion) ==
		   TEST_CORNERING_LEGACY_RIGHT_HEADING);
	assert(left.car_demandedGrip == TEST_CORNERING_SAFE_DEMAND);
	assert(right.car_demandedGrip == left.car_demandedGrip);
	assert(left.car_slidingFlag == CAR_SLIDING_INACTIVE);
	assert(right.car_slidingFlag == CAR_SLIDING_INACTIVE);
	/* Once grip is exceeded, rounding also costs the left turn less speed. */
	assert(cornering_heading(-TEST_CORNERING_SLIDING_STEERING, TEST_CORNERING_SPEED, &left,
							 &motion) == TEST_CORNERING_LEGACY_LEFT_SLIDING_HEADING);
	assert(cornering_heading(TEST_CORNERING_SLIDING_STEERING, TEST_CORNERING_SPEED, &right,
							 &motion) == TEST_CORNERING_LEGACY_RIGHT_SLIDING_HEADING);
	assert(left.car_slidingFlag == CAR_SLIDING_ACTIVE);
	assert(right.car_slidingFlag == CAR_SLIDING_ACTIVE);
	assert(left.car_actual_speed == TEST_CORNERING_LEGACY_LEFT_SLIDING_SPEED);
	assert(right.car_actual_speed == TEST_CORNERING_LEGACY_RIGHT_SLIDING_SPEED);
}

static void test_symmetric_cornering(void)
{
	static const legacy_u16 rates[] = {GAME_FRAME_RATE_NORMAL, GAME_FRAME_RATE_LOW};
	static const legacy_u32 fractions[] = {0, PHANTOM_PHYSICS_ONE / 3, PHANTOM_PHYSICS_ONE};
	static const legacy_u16 speeds[] = {0, 4096U, 12800U, TEST_CORNERING_SPEED, 40000U};
	configure_cornering((legacy_s8 *)"--lcb:off");
	for (legacy_u16 rate = 0; rate < sizeof(rates) / sizeof(rates[0]); rate++) {
		framespersec = rates[rate];
		for (legacy_u16 fraction = 0; fraction < sizeof(fractions) / sizeof(fractions[0]);
			 fraction++) {
			player_motion_fraction = fractions[fraction];
			for (legacy_u16 speed = 0; speed < sizeof(speeds) / sizeof(speeds[0]); speed++) {
				for (legacy_s16 angle = 0; angle <= TEST_CORNERING_STEERING_LIMIT; angle++) {
					struct CARSTATE left;
					struct CARSTATE right;
					struct PLAYER_WHEEL_MOTION left_motion = {0};
					struct PLAYER_WHEEL_MOTION right_motion = {0};
					legacy_s16 left_heading =
						cornering_heading(-angle, speeds[speed], &left, &left_motion);
					legacy_s16 right_heading =
						cornering_heading(angle, speeds[speed], &right, &right_motion);
					assert(left_heading == -right_heading);
					assert(left.car_front_wheel_response_angle ==
						   -right.car_front_wheel_response_angle);
					assert(left.car_demandedGrip == right.car_demandedGrip);
					assert(left.car_slidingFlag == right.car_slidingFlag);
					assert(left.car_actual_speed == right.car_actual_speed);
					assert(left.car_rev_speed == right.car_rev_speed);
					assert(left.car_slide_yaw_delta == -right.car_slide_yaw_delta);
					assert(left.car_velocity_heading_offset == -right.car_velocity_heading_offset);
					assert(left_motion.travel == right_motion.travel);
					left.car_sumSurfAllWheels = CAR_WHEEL_CONTACT_NONE;
					assert(prepare_wheel_travel(&left, &left_motion) == 0);
				}
			}
		}
	}
	configure_cornering((legacy_s8 *)"--lcb:on");
	player_motion_fraction = 0;
}

static void test_inherited_contact_reconciliation(void)
{
	/* New impacts retain their whole correction, even for a very short step. */
	assert(retained_wheel_plane_residual(1, -100, 1) == 0);
	assert(retained_wheel_plane_residual(0, -100, 21845UL) == 0);
	assert(retained_wheel_plane_residual(-100, 1, 21845UL) == 0);

	/* Existing compression relaxes with time; extra penetration is removed now. */
	assert(retained_wheel_plane_residual(-12, -30, 0) == -12);
	assert(retained_wheel_plane_residual(-12, -30, 21845UL) == -8);
	assert(retained_wheel_plane_residual(-12, -30, PHANTOM_PHYSICS_ONE) == 0);
	assert(retained_wheel_plane_residual(-12, -2, 21845UL) == -2);

	for (legacy_s16 previous = -384; previous <= 32; previous++) {
		for (legacy_s16 current = -384; current <= 32; current++) {
			legacy_s16 residual = retained_wheel_plane_residual(previous, current, 21845UL);
			if (previous >= 0 || current >= 0) {
				assert(residual == 0);
			} else {
				/* Correcting a short step never makes an overlap deeper, and
				 * never retains penetration created after its starting pose. */
				assert(residual >= current);
				assert(residual >= previous);
				assert(residual <= 0);
			}
		}
	}
}

static void test_fractional_inverted_attachment(void)
{
	static const legacy_u32 fractions[] = {0, 1, 21845UL, PHANTOM_PHYSICS_ONE};
	static const legacy_u16 rates[] = {GAME_FRAME_RATE_NORMAL, GAME_FRAME_RATE_LOW};
	static const legacy_s16 expected[2][4] = {{192, 0, 64, 192}, {192, 0, 32, 96}};
	for (legacy_u16 rate = 0; rate < sizeof(rates) / sizeof(rates[0]); rate++) {
		struct CARSTATE car = {0};
		struct PLAYER_WHEEL_MOTION motion = {0};
		framespersec = rates[rate];
		car.car_sumSurfAllWheels = 4;
		car.car_actual_speed = PLAYER_PHYSICS_LOW_SPEED_LIMIT + 1;
		car_working_pitch = car_working_yaw = 0;
		car_working_roll = ANGLE_HALF_TURN;
		for (legacy_u16 i = 0; i < sizeof(fractions) / sizeof(fractions[0]); i++) {
			player_motion_fraction = fractions[i];
			prepare_wheel_rotation(&car, &motion);
			assert(motion.inverted_adjustment == expected[rate][i]);
			assert(motion.inverted_offset.x == 0);
			assert(motion.inverted_offset.y == expected[rate][i]);
			assert(motion.inverted_offset.z == 0);
		}
		car.car_actual_speed = PLAYER_PHYSICS_LOW_SPEED_LIMIT;
		player_motion_fraction = 21845UL;
		prepare_wheel_rotation(&car, &motion);
		assert(motion.inverted_adjustment == -expected[rate][2]);
		car_working_roll = 0;
		prepare_wheel_rotation(&car, &motion);
		assert(motion.inverted_adjustment == 0);
		car_working_roll = ANGLE_HALF_TURN;
		car.car_sumSurfAllWheels = CAR_WHEEL_CONTACT_NONE;
		prepare_wheel_rotation(&car, &motion);
		assert(motion.inverted_adjustment == 0);
	}
	player_motion_fraction = 0;
}

static void test_contact_residual_applies_to_wheel(void)
{
	struct PLANE plane = {0};
	struct PLAYER_WHEEL_MOTION motion = {0};
	mat_rot_y(&plane.plane_rotation, 0);
	planptr = &plane;
	current_planptr = &plane;
	planindex = 0;
	framespersec = GAME_FRAME_RATE_NORMAL;
	player_motion_fraction = 21845UL;
	motion.current[0].ly = 6400;
	motion.contact_distances[0] = -30;
	relax_wheel_plane_residual(&motion, 0, -12);
	assert(motion.current[0].ly == 6400 - 8 * 64);
	assert(motion.contact_distances[0] == -22);
	motion.current[0].ly = 6400;
	motion.contact_distances[0] = -30;
	relax_wheel_plane_residual(&motion, 0, 1);
	assert(motion.current[0].ly == 6400);
	assert(motion.contact_distances[0] == -30);
	motion.contact_distances[0] = 12;
	relax_wheel_plane_residual(&motion, 0, 12);
	assert(motion.current[0].ly == 6400);
	assert(motion.contact_distances[0] == 4);
	player_motion_fraction = 0;
}

legacy_int main(void)
{
	test_original_cornering_bias();
	test_symmetric_cornering();
	test_inherited_contact_reconciliation();
	test_fractional_inverted_attachment();
	test_contact_residual_applies_to_wheel();
	puts("phantom motion regression tests passed");
	return 0;
}
