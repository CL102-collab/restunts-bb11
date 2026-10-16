#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../c/externs.h"
#include "../c/state_internal.h"
#include "../c/physics_internal.h"
#include "../c/residue.h"
#include "../c/crash_state.h"

legacy_s16 grassDecelDivTab[5] = {255, 256, 192, 128, 64};
legacy_s16 terrainrows[30];
legacy_u8 far *track_element_map;

static struct CARSTATE car;
static struct SIMD simd;
static legacy_u8 elements[901];
static legacy_u32 random_state = 1;

#ifdef GRIP_DIFFERENTIAL
void reference_update_grip(struct CARSTATE *, struct SIMD *, legacy_s16);
void reference_update_legacy_grip_stack_words(struct CARSTATE *, struct SIMD *, legacy_u16,
											  legacy_u16);
#endif

static legacy_u16 random_word(void)
{
	random_state = random_state * 1664525UL + 1013904223UL;
	return (legacy_u16)(random_state >> 16);
}

static void reset_car(void)
{
	memset(&car, 0, sizeof(car));
	memset(&simd, 0, sizeof(simd));
	memset(&legacy_execution_residue, 0, sizeof(legacy_execution_residue));
	memset(elements, 0, sizeof(elements));
	for (legacy_s32 index = 0; index < 30; index++) {
		terrainrows[index] = index * 30;
	}
	track_element_map = elements;
	car.car_sumSurfAllWheels = 4;
	for (legacy_s32 index = 0; index < 4; index++) {
		car.car_surfaceWhl[index] = CAR_SURFACE_PAVED;
	}
	car.car_actual_speed = 16000;
	car.car_rev_speed = 16000;
	car.car_position.lx = 10L << 16;
	car.car_position.lz = 10L << 16;
	simd.grip = 500;
	simd.sliding = 256;
	for (legacy_s32 index = 0; index < SIMD_SURFACE_GRIP_COUNT; index++) {
		simd.surface_grip[index] = 256;
	}
}

static void run_grip(legacy_s16 behavior)
{
	legacy_u16 speed_before = car.car_rev_speed;
	legacy_u16 actual_before = car.car_actual_speed;
#ifdef GRIP_DIFFERENTIAL
	struct CARSTATE expected = car;
	struct LEGACY_EXECUTION_RESIDUE before = legacy_execution_residue;
	reference_update_grip(&expected, &simd, behavior);
	reference_update_legacy_grip_stack_words(&expected, &simd, speed_before, actual_before);
	struct LEGACY_EXECUTION_RESIDUE expected_residue = legacy_execution_residue;
	legacy_execution_residue = before;
#endif
	update_grip(&car, &simd, behavior);
	update_legacy_grip_stack_words(&car, &simd, speed_before, actual_before,
								   LEGACY_DEFAULT_PLAYER_TICK_SI);
#ifdef GRIP_DIFFERENTIAL
	assert(memcmp(&expected, &car, sizeof(car)) == 0);
	assert(memcmp(&expected_residue, &legacy_execution_residue, sizeof(expected_residue)) == 0);
#endif
}

static void test_contact_and_grass(void)
{
	reset_car();
	car.car_sumSurfAllWheels = 0;
	car.car_front_wheel_response_angle = 32;
	car.car_slidingFlag = CAR_SLIDING_ACTIVE;
	car.car_slip_angle = 100;
	run_grip(GRIP_BEHAVIOR_PLAYER);
	assert(car.car_front_wheel_response_angle == 0);
	assert(car.car_slidingFlag == CAR_SLIDING_INACTIVE);
	assert(car.car_actual_speed == 16000);
	assert(car.car_slip_angle == 100);
	assert(legacy_execution_residue.grip_stack_words[3] == 80);
	for (legacy_s32 grass_count = 1; grass_count <= 4; grass_count++) {
		reset_car();
		for (legacy_s32 index = 0; index < grass_count; index++) {
			car.car_surfaceWhl[index] = CAR_SURFACE_GRASS;
		}
		run_grip(GRIP_BEHAVIOR_PLAYER);
		assert(car.car_actual_speed == 16000 - 16000 / grassDecelDivTab[grass_count]);
		assert(car.car_rev_speed == car.car_actual_speed);
		assert(car.car_demandedGrip == 0);
		assert(car.car_surfacegrip_sum == 1000);
	}
}

static void test_recenter_and_banking(void)
{
	static const legacy_s16 expected[] = {-8, -6, 0, 0, 0, 6, 8};
	static const legacy_s16 input[] = {-8, -7, -1, 0, 1, 7, 8};
	for (legacy_s32 rotation = 0; rotation < 7; rotation++) {
		reset_car();
		car.car_rotate.x = input[rotation];
		run_grip(GRIP_BEHAVIOR_PLAYER);
		assert(car.car_rotate.x == expected[rotation]);
	}
	reset_car();
	car.car_rotate.z = 25;
	elements[terrainrows[10] + 10] = 253;
	elements[terrainrows[11] + 9] = 52;
	run_grip(GRIP_BEHAVIOR_PLAYER);
	assert(car.car_front_wheel_response_angle == 5);
	reset_car();
	car.car_steeringAngle = 12;
	car.car_slide_yaw_delta = 16;
	run_grip(GRIP_BEHAVIOR_OPPONENT);
	assert(car.car_front_wheel_response_angle == 48);
	assert(car.car_slide_yaw_delta == 15);
	assert(car.car_velocity_heading_offset == -15);
}

static void configure_collision_option(const char *option)
{
	legacy_s8 *argv[] = {(legacy_s8 *)"restunts", (legacy_s8 *)option};
	configure_legacy_collision(option ? 2 : 1, argv);
}

static void test_legacy_collision_recovery(void)
{
	static const char *options[] = {NULL, "--lc:on"};
	for (legacy_u32 index = 0; index < sizeof(options) / sizeof(options[0]); index++) {
		configure_collision_option(options[index]);
		reset_car();
		car.car_velocity_heading_offset = -46;
		for (legacy_s32 frame = 0; frame < 128; frame++) {
			run_grip(GRIP_BEHAVIOR_PLAYER);
		}
		assert(car.car_steeringAngle == CAR_STEERING_CENTERED);
		assert(car.car_velocity_heading_offset == -15);
		assert(car.car_front_wheel_response_angle == -15);
		reset_car();
		car.car_slide_yaw_delta = -15;
		run_grip(GRIP_BEHAVIOR_OPPONENT);
		assert(car.car_slide_yaw_delta == -15);
		assert(car.car_velocity_heading_offset == 15);
	}
}

static void assert_collision_recovery(legacy_s16 angle, legacy_s16 behavior, legacy_u16 speed)
{
	reset_car();
	car.car_actual_speed = speed;
	car.car_rev_speed = speed;
	if (behavior == GRIP_BEHAVIOR_PLAYER) {
		car.car_velocity_heading_offset = angle;
	} else {
		car.car_slide_yaw_delta = angle;
	}
	for (legacy_s32 frame = 0; frame < 512; frame++) {
		legacy_s16 before = behavior == GRIP_BEHAVIOR_PLAYER ? car.car_velocity_heading_offset
															 : car.car_slide_yaw_delta;
		update_grip(&car, &simd, behavior);
		legacy_s16 after = behavior == GRIP_BEHAVIOR_PLAYER ? car.car_velocity_heading_offset
															: car.car_slide_yaw_delta;
		if (before < 0) {
			assert(after > before && after <= 0);
		} else if (before > 0) {
			assert(after < before && after >= 0);
		} else {
			assert(after == 0);
		}
		assert(car.car_steeringAngle == CAR_STEERING_CENTERED);
	}
	assert(car.car_velocity_heading_offset == 0);
	assert(car.car_slide_yaw_delta == 0);
	assert(car.car_front_wheel_response_angle == 0);
	assert(car.car_actual_speed == speed);
	assert(car.car_rev_speed == speed);
}

static void test_corrected_collision_recovery(void)
{
	static const struct {
		legacy_s16 angle;
		legacy_s16 damped;
	} cases[] = {
		{-32768, -30720}, {-4096, -3840}, {-46, -43}, {-16, -15},	{-15, -14},		{0, 0},
		{15, 14},		  {16, 15},		  {46, 43},	  {4096, 3840}, {32767, 30719},
	};
	configure_collision_option("--lc:off");
	/* Include every formerly stuck negative remainder and the replay's -46 offset. */
	for (legacy_s16 angle = -64; angle <= 64; angle++) {
		assert_collision_recovery(angle, GRIP_BEHAVIOR_PLAYER, 16000);
		assert_collision_recovery(angle, GRIP_BEHAVIOR_OPPONENT, 16000);
		assert(damp_collision_angle(angle) == -damp_collision_angle(-angle));
	}
	for (legacy_u32 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		assert(damp_collision_angle(cases[index].angle) == cases[index].damped);
		/* Low speed keeps large heading offsets within the available tire grip. */
		assert_collision_recovery(cases[index].angle, GRIP_BEHAVIOR_PLAYER, 256);
		assert_collision_recovery(cases[index].angle, GRIP_BEHAVIOR_OPPONENT, 256);
	}
}

/* Inputs stay inside the track and use the six valid contact coefficients.
 * Signed edge values stress the explicitly wrapped 16-bit grip arithmetic. */
static void test_wrapped_grip_sweep(void)
{
	static const legacy_s16 angles[] = {-32768, -16384, -1024, -256, -16,	-1,	  0,
										1,		16,		256,   1024, 16384, 32767};
	static const legacy_s16 grips[] = {-32768, -1, 0, 1, 256, 500, 16384, 32767};
	static const legacy_u16 speeds[] = {0, 1, 255, 256, 257, 32767, 32768, 65535};
	legacy_u32 hash = 2166136261UL;
	for (legacy_u32 sample = 0; sample < 200000; sample++) {
		reset_car();
		car.car_steeringAngle = angles[random_word() % 13];
		car.car_velocity_heading_offset = angles[random_word() % 13];
		car.car_rotate.x = angles[random_word() % 13];
		car.car_rotate.z = angles[random_word() % 13];
		car.car_slide_yaw_delta = angles[random_word() % 13];
		car.car_slip_angle = angles[random_word() % 13];
		car.car_actual_speed = speeds[random_word() % 8];
		car.car_rev_speed = speeds[random_word() % 8];
		car.car_slidingFlag = random_word() % 2;
		car.car_crashBmpFlag = random_word() % 3;
		car.car_sumSurfAllWheels = random_word() % 5;
		for (legacy_u32 index = 0; index < 4; index++) {
			car.car_surfaceWhl[index] = random_word() % 6;
		}
		simd.grip = grips[random_word() % 8];
		simd.sliding = grips[random_word() % 8];
		for (legacy_u32 index = 0; index < SIMD_SURFACE_GRIP_COUNT; index++) {
			simd.surface_grip[index] = grips[random_word() % 8];
		}
		elements[terrainrows[10] + 10] = 51 + random_word() % 6;
		run_grip((legacy_s16)(sample % 3));
		const legacy_u8 *bytes = (const legacy_u8 *)&car;
		for (legacy_u32 index = 0; index < sizeof(car); index++) {
			hash = (hash ^ bytes[index]) * 16777619UL;
		}
		bytes = (const legacy_u8 *)&legacy_execution_residue;
		for (legacy_u32 index = 0; index < sizeof(legacy_execution_residue); index++) {
			hash = (hash ^ bytes[index]) * 16777619UL;
		}
	}
#ifdef GRIP_RECORD_BASELINE
	fprintf(stdout, "%08" LEGACY_PRIx32 "\n", hash);
#else
	/* Captured from the original update_grip and explicit stack-residue model. */
	assert(hash == 0x216e96a0UL);
#endif
}

enum CORNERING_TEST_SETUP {
	CORNERING_TEST_ANGLE = 67,
	CORNERING_TEST_SPEED = 16000,
	CORNERING_TEST_GRIP = 100,
	CORNERING_TEST_MAX_ANGLE = 128,
	CORNERING_TEST_WHEEL_COUNT = 4
};

static void configure_cornering_option(const legacy_s8 *option)
{
	legacy_s8 *argv[] = {(legacy_s8 *)"restunts", (legacy_s8 *)option};
	configure_left_corner_bias(option != NULL ? 2 : 1, argv);
}

static void test_cornering_options(void)
{
	enum CORNERING_OPTION_TEST_VALUES {
		CORNERING_OPTION_TEST_ANGLE = -7,
		CORNERING_OPTION_TEST_SHIFT = 2,
		CORNERING_OPTION_TEST_BIASED = -2,
		CORNERING_OPTION_TEST_SYMMETRIC = -1
	};
	static const struct {
		const legacy_s8 *first;
		const legacy_s8 *second;
		legacy_s16 expected_angle;
	} cases[] = {{NULL, NULL, CORNERING_OPTION_TEST_BIASED},
				 {(const legacy_s8 *)"--lcb:on", NULL, CORNERING_OPTION_TEST_BIASED},
				 {(const legacy_s8 *)"--lcb:off", NULL, CORNERING_OPTION_TEST_SYMMETRIC},
				 {(const legacy_s8 *)"--LCB:OFF", NULL, CORNERING_OPTION_TEST_SYMMETRIC},
				 {(const legacy_s8 *)"--LcB:OfF", NULL, CORNERING_OPTION_TEST_SYMMETRIC},
				 {(const legacy_s8 *)"lcb:off", NULL, CORNERING_OPTION_TEST_BIASED},
				 {(const legacy_s8 *)"/lcb:off", NULL, CORNERING_OPTION_TEST_BIASED},
				 {(const legacy_s8 *)"/LCB:OFF", NULL, CORNERING_OPTION_TEST_BIASED},
				 {(const legacy_s8 *)"-lcb:off", NULL, CORNERING_OPTION_TEST_BIASED},
				 {(const legacy_s8 *)"--lcb:offx", NULL, CORNERING_OPTION_TEST_BIASED},
				 {(const legacy_s8 *)"--lcb:off ", NULL, CORNERING_OPTION_TEST_BIASED},
				 {(const legacy_s8 *)"--lcb:", NULL, CORNERING_OPTION_TEST_BIASED},
				 {(const legacy_s8 *)"--lcb", NULL, CORNERING_OPTION_TEST_BIASED},
				 {(const legacy_s8 *)"--lcb:off", (const legacy_s8 *)"--LCB:ON",
				  CORNERING_OPTION_TEST_BIASED},
				 {(const legacy_s8 *)"--LCB:ON", (const legacy_s8 *)"--lcb:off",
				  CORNERING_OPTION_TEST_SYMMETRIC},
				 {(const legacy_s8 *)"--lcb:off", (const legacy_s8 *)"lcb:on",
				  CORNERING_OPTION_TEST_SYMMETRIC},
				 {(const legacy_s8 *)"--lcb:off", (const legacy_s8 *)"/lcb:on",
				  CORNERING_OPTION_TEST_SYMMETRIC},
				 {(const legacy_s8 *)"--lcb:off", (const legacy_s8 *)"--lc:on",
				  CORNERING_OPTION_TEST_SYMMETRIC}};
	for (legacy_u16 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		/* A new parse restores the default before applying its recognized switches. */
		configure_cornering_option((const legacy_s8 *)"--lcb:off");
		legacy_s8 *arguments[] = {(legacy_s8 *)"restunts", (legacy_s8 *)cases[index].first,
								  (legacy_s8 *)cases[index].second};
		legacy_s16 count = cases[index].second != NULL ? 3 : cases[index].first != NULL ? 2 : 1;
		configure_left_corner_bias(count, arguments);
		assert(scale_cornering_angle(CORNERING_OPTION_TEST_ANGLE, CORNERING_OPTION_TEST_SHIFT) ==
			   cases[index].expected_angle);
	}
	legacy_s8 *executable_only[] = {(legacy_s8 *)"--lcb:off"};
	configure_left_corner_bias(sizeof(executable_only) / sizeof(executable_only[0]),
							   executable_only);
	assert(scale_cornering_angle(CORNERING_OPTION_TEST_ANGLE, CORNERING_OPTION_TEST_SHIFT) ==
		   CORNERING_OPTION_TEST_BIASED);
}

static void test_cornering_skid_bias(void)
{
	static const struct {
		const legacy_s8 *option;
		legacy_s16 left_response;
		legacy_u16 left_speed;
	} cases[] = {{NULL, -27, 15920},
				 {(const legacy_s8 *)"--lcb:off", -26, 15918},
				 {(const legacy_s8 *)"--LCB:ON", -27, 15920}};
	static const legacy_s16 expected_right_response = 26;
	static const legacy_u16 expected_right_speed = 15918;
	configure_collision_option(NULL);
	for (legacy_u16 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		configure_cornering_option(cases[index].option);
		reset_car();
		simd.grip = CORNERING_TEST_GRIP;
		car.car_actual_speed = CORNERING_TEST_SPEED;
		car.car_rev_speed = CORNERING_TEST_SPEED;
		car.car_steeringAngle = CORNERING_TEST_ANGLE;
		update_grip(&car, &simd, GRIP_BEHAVIOR_PLAYER);
		assert(car.car_slidingFlag == CAR_SLIDING_ACTIVE);
		assert(car.car_front_wheel_response_angle == expected_right_response);
		assert(car.car_actual_speed == expected_right_speed);
		struct CARSTATE right = car;
		reset_car();
		simd.grip = CORNERING_TEST_GRIP;
		car.car_actual_speed = CORNERING_TEST_SPEED;
		car.car_rev_speed = CORNERING_TEST_SPEED;
		car.car_steeringAngle = -CORNERING_TEST_ANGLE;
		update_grip(&car, &simd, GRIP_BEHAVIOR_PLAYER);
		assert(car.car_slidingFlag == CAR_SLIDING_ACTIVE);
		assert(car.car_demandedGrip == right.car_demandedGrip);
		assert(car.car_front_wheel_response_angle == cases[index].left_response);
		assert(car.car_actual_speed == cases[index].left_speed);
	}
	configure_cornering_option(NULL);
}

static void test_corrected_cornering_symmetry(void)
{
	static const legacy_u16 speeds[] = {0, 256, 8000, CORNERING_TEST_SPEED, 32000, 64000};
	static const legacy_s16 grips[] = {0, CORNERING_TEST_GRIP, 500};
	configure_cornering_option((const legacy_s8 *)"--lcb:off");
	for (legacy_u16 speed = 0; speed < sizeof(speeds) / sizeof(speeds[0]); speed++) {
		for (legacy_u16 grip = 0; grip < sizeof(grips) / sizeof(grips[0]); grip++) {
			for (legacy_u16 surface = 0; surface <= SIMD_SURFACE_GRIP_COUNT; surface++) {
				for (legacy_s16 angle = 0; angle <= CORNERING_TEST_MAX_ANGLE; angle++) {
					reset_car();
					simd.grip = grips[grip];
					car.car_actual_speed = speeds[speed];
					car.car_rev_speed = speeds[speed];
					for (legacy_u16 wheel = 0; wheel < CORNERING_TEST_WHEEL_COUNT; wheel++) {
						car.car_surfaceWhl[wheel] = (legacy_s8)surface;
					}
					car.car_steeringAngle = angle;
					struct CARSTATE left = car;
					left.car_steeringAngle = -angle;
					update_grip(&car, &simd, GRIP_BEHAVIOR_PLAYER);
					update_grip(&left, &simd, GRIP_BEHAVIOR_PLAYER);
					assert(left.car_demandedGrip == car.car_demandedGrip);
					assert(left.car_surfacegrip_sum == car.car_surfacegrip_sum);
					assert(left.car_slidingFlag == car.car_slidingFlag);
					assert(left.car_front_wheel_response_angle ==
						   -car.car_front_wheel_response_angle);
					assert(left.car_slide_yaw_delta == -car.car_slide_yaw_delta);
					assert(left.car_velocity_heading_offset == -car.car_velocity_heading_offset);
					assert(left.car_actual_speed == car.car_actual_speed);
					assert(left.car_rev_speed == car.car_rev_speed);
				}
			}
		}
	}
	configure_cornering_option(NULL);
}

static void configure_grass_option(const legacy_s8 *option)
{
	legacy_s8 *arguments[] = {(legacy_s8 *)"restunts", (legacy_s8 *)option};
	configure_amiga_grass(option != NULL ? 2 : 1, arguments);
}

static void run_grass_grip(legacy_u8 enabled, const legacy_s8 car_id[4], legacy_s16 behavior)
{
	configure_grass_option(enabled != 0 ? (const legacy_s8 *)"/amigagrass" : NULL);
	apply_amiga_grass(&simd, car_id);
	run_grip(behavior);
}

static void test_amiga_grass_options(void)
{
	/* No startup configuration has run yet: the optional override starts off. */
	reset_car();
	apply_amiga_grass(&simd, (const legacy_s8 *)"PMIN");
	assert(simd.surface_grip[CAR_SURFACE_GRASS - CAR_SURFACE_PAVED] == 256);
	static const struct {
		const legacy_s8 *option;
		legacy_s16 expected_grass;
	} cases[] = {{NULL, 256},
				 {(const legacy_s8 *)"/amigagrass", 30},
				 {(const legacy_s8 *)"/AMIGAGRASS", 30},
				 {(const legacy_s8 *)"/AmIgAgRaSs", 30},
				 {(const legacy_s8 *)"", 256},
				 {(const legacy_s8 *)"amigagrass", 256},
				 {(const legacy_s8 *)"--amigagrass", 256},
				 {(const legacy_s8 *)"-amigagrass", 256},
				 {(const legacy_s8 *)"/amigagras", 256},
				 {(const legacy_s8 *)"/amigagrassx", 256},
				 {(const legacy_s8 *)"/amigagrass ", 256},
				 {(const legacy_s8 *)" /amigagrass", 256},
				 {(const legacy_s8 *)"/amigagrass:on", 256},
				 {(const legacy_s8 *)"/amigagrass:off", 256},
				 {(const legacy_s8 *)"/amigagrass=1", 256},
				 {(const legacy_s8 *)"--lc:on", 256}};
	for (legacy_u16 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		configure_grass_option((const legacy_s8 *)"/amigagrass");
		configure_grass_option(cases[index].option);
		reset_car();
		apply_amiga_grass(&simd, (const legacy_s8 *)"PMIN");
		assert(simd.surface_grip[CAR_SURFACE_GRASS - CAR_SURFACE_PAVED] ==
			   cases[index].expected_grass);
	}
	legacy_s8 *arguments[] = {(legacy_s8 *)"restunts", (legacy_s8 *)"--lc:off",
							  (legacy_s8 *)"/amigagrass", (legacy_s8 *)"/amigagrass:off"};
	configure_amiga_grass(sizeof(arguments) / sizeof(arguments[0]), arguments);
	reset_car();
	apply_amiga_grass(&simd, (const legacy_s8 *)"PMIN");
	assert(simd.surface_grip[CAR_SURFACE_GRASS - CAR_SURFACE_PAVED] == 30);

	legacy_s8 *executable_only[] = {(legacy_s8 *)"/amigagrass"};
	configure_amiga_grass(1, executable_only);
	reset_car();
	apply_amiga_grass(&simd, (const legacy_s8 *)"PMIN");
	assert(simd.surface_grip[CAR_SURFACE_GRASS - CAR_SURFACE_PAVED] == 256);
	configure_grass_option((const legacy_s8 *)"/amigagrass");
	configure_amiga_grass(0, NULL);
	apply_amiga_grass(&simd, (const legacy_s8 *)"PMIN");
	assert(simd.surface_grip[CAR_SURFACE_GRASS - CAR_SURFACE_PAVED] == 256);
}

static void test_amiga_grass_profiles(void)
{
	/* Numeric resource facts from fixtures/amiga-grass-reference.json. */
	static const struct {
		legacy_s8 car_id[4];
		legacy_s16 amiga_grass;
	} cases[] = {{"ANSX", 128}, {"AUDI", 200}, {"COUN", 128}, {"FGTO", 128}, {"JAGU", 40},
				 {"LANC", 210}, {"LM02", 230}, {"P962", 30},  {"PC04", 180}, {"PMIN", 30},
				 {"VETT", 128}, {"CSIL", 173}, {"DIA3", 173}};
	for (legacy_u16 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		for (legacy_u8 enabled = 0; enabled < 2; enabled++) {
			for (legacy_u8 lower_case = 0; lower_case < 2; lower_case++) {
				/* Four bytes intentionally have no terminator, like replay car IDs. */
				legacy_s8 car_id[4];
				for (legacy_u16 character = 0; character < sizeof(car_id); character++) {
					car_id[character] = cases[index].car_id[character];
					if (lower_case != 0 && car_id[character] >= 'A' && car_id[character] <= 'Z') {
						car_id[character] += 'a' - 'A';
					}
				}
				memset(&simd, 0xa5, sizeof(simd));
				simd.surface_grip[CAR_SURFACE_GRASS - CAR_SURFACE_PAVED] = 173;
				struct SIMD expected = simd;
				if (enabled != 0) {
					expected.surface_grip[CAR_SURFACE_GRASS - CAR_SURFACE_PAVED] =
						cases[index].amiga_grass;
				}
				configure_grass_option(enabled != 0 ? (const legacy_s8 *)"/amigagrass" : NULL);
				apply_amiga_grass(&simd, car_id);
				assert(memcmp(&expected, &simd, sizeof(simd)) == 0);
			}
		}
	}
	configure_grass_option(NULL);
}

static void reset_grass_reference_car(void)
{
	reset_car();
	car.car_actual_speed = 20480;
	car.car_rev_speed = 20480;
	simd.grip = 600;
	simd.sliding = 0;
	simd.surface_grip[CAR_SURFACE_PAVED - CAR_SURFACE_PAVED] = 256;
	simd.surface_grip[CAR_SURFACE_DIRT - CAR_SURFACE_PAVED] = 192;
	simd.surface_grip[CAR_SURFACE_ICE - CAR_SURFACE_PAVED] = 64;
	simd.surface_grip[CAR_SURFACE_GRASS - CAR_SURFACE_PAVED] = 128;
}

static void test_amiga_grass_non_grass_equivalence(void)
{
	static const legacy_s8 surfaces[] = {CAR_WHEEL_CONTACT_NONE, CAR_SURFACE_PAVED,
										 CAR_SURFACE_DIRT, CAR_SURFACE_ICE, CAR_SURFACE_WATER};
	static const legacy_s16 behaviors[] = {GRIP_BEHAVIOR_PLAYER, GRIP_BEHAVIOR_OPPONENT};
	for (legacy_u16 behavior = 0; behavior < sizeof(behaviors) / sizeof(behaviors[0]); behavior++) {
		for (legacy_u16 combination = 0; combination < 625; combination++) {
			reset_grass_reference_car();
			car.car_steeringAngle = combination % 2 != 0 ? 128 : -128;
			car.car_velocity_heading_offset = 7;
			car.car_slide_yaw_delta = -5;
			car.car_slip_angle = 9;
			car.car_slidingFlag = CAR_SLIDING_ACTIVE;
			car.car_sound_flags = 1;
			car.car_sumSurfAllWheels = 0;
			legacy_u16 remaining = combination;
			for (legacy_u16 wheel = 0; wheel < 4; wheel++) {
				car.car_surfaceWhl[wheel] = surfaces[remaining % 5];
				car.car_sumSurfAllWheels += car.car_surfaceWhl[wheel];
				remaining /= 5;
			}
			memset(&legacy_execution_residue, 0x5a, sizeof(legacy_execution_residue));
			struct CARSTATE before = car;
			struct SIMD original_simd = simd;
			struct LEGACY_EXECUTION_RESIDUE before_residue = legacy_execution_residue;
			run_grass_grip(0, (const legacy_s8 *)"PMIN", behaviors[behavior]);
			struct CARSTATE expected = car;
			struct LEGACY_EXECUTION_RESIDUE expected_residue = legacy_execution_residue;
			car = before;
			simd = original_simd;
			legacy_execution_residue = before_residue;
			run_grass_grip(1, (const legacy_s8 *)"PMIN", behaviors[behavior]);
			assert(memcmp(&expected, &car, sizeof(car)) == 0);
			assert(memcmp(&expected_residue, &legacy_execution_residue, sizeof(expected_residue)) ==
				   0);
		}
	}
	configure_grass_option(NULL);
}

static void test_amiga_grass_mixed_contacts(void)
{
	static const legacy_s16 expected_grip[2][4] = {{1050, 900, 750, 600}, {935, 670, 405, 140}};
	static const legacy_u16 expected_drag_speed[] = {20400, 20374, 20320, 20160};
	for (legacy_u16 mask = 1; mask < 16; mask++) {
		legacy_u16 grass_count = 0;
		for (legacy_u16 wheel = 0; wheel < 4; wheel++) {
			if ((mask & (1U << wheel)) != 0) {
				grass_count++;
			}
		}
		for (legacy_u16 turning = 0; turning < 2; turning++) {
			struct CARSTATE dos_result = {0};
			for (legacy_u8 enabled = 0; enabled < 2; enabled++) {
				reset_grass_reference_car();
				car.car_steeringAngle = turning != 0 ? 128 : 0;
				for (legacy_u16 wheel = 0; wheel < 4; wheel++) {
					if ((mask & (1U << wheel)) != 0) {
						car.car_surfaceWhl[wheel] = CAR_SURFACE_GRASS;
					}
				}
				run_grass_grip(enabled, (const legacy_s8 *)"PMIN", GRIP_BEHAVIOR_PLAYER);
				assert(car.car_surfacegrip_sum == expected_grip[enabled][grass_count - 1]);
				assert(car.car_actual_speed == car.car_rev_speed);
				if (turning == 0) {
					assert(car.car_actual_speed == expected_drag_speed[grass_count - 1]);
					assert(car.car_slidingFlag == CAR_SLIDING_INACTIVE);
					assert(car.car_sound_flags == 0);
				} else {
					assert(car.car_slidingFlag == CAR_SLIDING_ACTIVE);
					assert(car.car_sound_flags == (grass_count < 4 ? CAR_SOUND_SKID_PAVED_FLAG
																   : CAR_SOUND_SKID_OFFROAD_FLAG));
					if (enabled == 0) {
						dos_result = car;
					} else {
						assert(car.car_demandedGrip == dos_result.car_demandedGrip);
						assert(car.car_front_wheel_response_angle <
							   dos_result.car_front_wheel_response_angle);
						assert(car.car_actual_speed < dos_result.car_actual_speed);
					}
				}
			}
		}
	}
	configure_grass_option(NULL);
}

static void test_amiga_grass_original_vectors(void)
{
	/* Outputs of the original Amiga 68000 routine, including its DOS-coefficient
	 * controls, recorded in fixtures/amiga-grass-reference.json. */
	static const struct {
		legacy_s16 steering;
		legacy_s8 surface;
		legacy_u8 enabled;
		legacy_u16 speed;
		legacy_s16 demanded_grip;
		legacy_s16 available_grip;
		legacy_s16 wheel_response;
		legacy_s16 heading_offset;
		legacy_s16 yaw_delta;
		legacy_u8 sliding;
		legacy_u8 sound;
	} cases[] = {{32, CAR_SURFACE_PAVED, 1, 20480, 400, 1200, 32, 0, 0, 0, 0},
				 {32, CAR_SURFACE_PAVED, 0, 20480, 400, 1200, 32, 0, 0, 0, 0},
				 {32, CAR_SURFACE_GRASS, 1, 20118, 380, 140, 11, 0, 0, 1, 4},
				 {32, CAR_SURFACE_GRASS, 0, 20160, 380, 600, 32, 0, 0, 0, 0},
				 {128, CAR_SURFACE_PAVED, 1, 20360, 1600, 1200, 68, 0, 0, 1, 2},
				 {128, CAR_SURFACE_PAVED, 0, 20360, 1600, 1200, 68, 0, 0, 1, 2},
				 {128, CAR_SURFACE_GRASS, 1, 19974, 1520, 140, 35, 3, -3, 1, 4},
				 {128, CAR_SURFACE_GRASS, 0, 20004, 1520, 600, 50, 0, 0, 1, 4}};
	configure_collision_option(NULL);
	configure_cornering_option(NULL);
	for (legacy_u16 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		reset_grass_reference_car();
		car.car_steeringAngle = cases[index].steering;
		for (legacy_u16 wheel = 0; wheel < 4; wheel++) {
			car.car_surfaceWhl[wheel] = cases[index].surface;
		}
		run_grass_grip(cases[index].enabled, (const legacy_s8 *)"PMIN", GRIP_BEHAVIOR_PLAYER);
		assert(car.car_actual_speed == cases[index].speed);
		assert(car.car_rev_speed == cases[index].speed);
		assert(car.car_demandedGrip == cases[index].demanded_grip);
		assert(car.car_surfacegrip_sum == cases[index].available_grip);
		assert(car.car_front_wheel_response_angle == cases[index].wheel_response);
		assert(car.car_velocity_heading_offset == cases[index].heading_offset);
		assert(car.car_slide_yaw_delta == cases[index].yaw_delta);
		assert(car.car_slidingFlag == cases[index].sliding);
		assert(car.car_sound_flags == cases[index].sound);
		assert(car.car_slip_angle == 0);
	}
	configure_grass_option(NULL);
}

int main(void)
{
	test_contact_and_grass();
	test_recenter_and_banking();
	test_wrapped_grip_sweep();
	test_legacy_collision_recovery();
	test_corrected_collision_recovery();
	test_cornering_options();
	test_cornering_skid_bias();
	test_corrected_cornering_symmetry();
	test_amiga_grass_options();
	test_amiga_grass_profiles();
	test_amiga_grass_non_grass_equivalence();
	test_amiga_grass_mixed_contacts();
	test_amiga_grass_original_vectors();
	return 0;
}
