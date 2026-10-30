#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../c/externs.h"
#include "../c/car_speed.h"
#include "../c/physics_internal.h"
#include "../c/game_input.h"

#undef printf

struct GAMESTATE state;
legacy_u16 framespersec;
legacy_u8 oppnentSped[1];

static struct SIMD simd;
static struct CARSTATE car;
static legacy_s16 aerodynamic_drag[64];

static void reset_car(void)
{
	memset(&state, 0, sizeof(state));
	memset(&simd, 0, sizeof(simd));
	memset(&car, 0, sizeof(car));
	memset(aerodynamic_drag, 0, sizeof(aerodynamic_drag));
	framespersec = GAME_FRAME_RATE_NORMAL;
	oppnentSped[0] = 200;
	simd.num_gears = 5;
	simd.car_mass = 25;
	simd.braking_eff = 100;
	simd.idle_rpm = 1000;
	simd.downshift_rpm = 2000;
	simd.upshift_rpm = 6000;
	simd.max_rpm = 12000;
	simd.idle_torque = 32;
	simd.aerorestable = aerodynamic_drag;
	for (legacy_u32 index = 0; index < SIMD_GEAR_RATIO_COUNT; index++) {
		simd.gear_ratios[index] = 4096;
		simd.knob_points[index].px = (legacy_s16)(index * 12);
		simd.knob_points[index].py = 12;
	}
	simd.knob_points[0].py = 0;
	memset(simd.torque_curve, 32, sizeof(simd.torque_curve));
	car.car_transmission = TRANSMISSION_MANUAL;
	car.car_current_gear = 1;
	car.car_currpm = 1000;
	car.car_gearratio = 4096;
	car.car_gearratioshr8 = 16;
	car.car_sumSurfRearWheels = 2;
	car.car_sumSurfAllWheels = 4;
	car.car_rev_speed = 16000;
	car.car_actual_speed = 16000;
	car.car_knob_x = simd.knob_points[1].px;
	car.car_knob_y = simd.knob_points[1].py;
}

static void test_shift_precedence_and_limits(void)
{
	reset_car();
	update_car_speed(INPUT_SHIFT_UP_FLAG | INPUT_SHIFT_DOWN_FLAG, PLAYER_CAR_INDEX, &car, &simd);
	assert(car.car_current_gear == 2);
	assert(car.car_changing_gear == CAR_GEAR_CHANGE_ACTIVE);
	assert(car.car_gear_change_delay == 30);
	assert(car.car_knob_y == 6);

	reset_car();
	car.car_current_gear = simd.num_gears;
	update_car_speed(INPUT_SHIFT_UP_FLAG | INPUT_SHIFT_DOWN_FLAG, PLAYER_CAR_INDEX, &car, &simd);
	assert(car.car_current_gear == simd.num_gears);
	assert(car.car_changing_gear == CAR_GEAR_CHANGE_INACTIVE);

	reset_car();
	update_car_speed(INPUT_SHIFT_DOWN_FLAG, PLAYER_CAR_INDEX, &car, &simd);
	assert(car.car_current_gear == 1);
	assert(car.car_changing_gear == CAR_GEAR_CHANGE_INACTIVE);
}

static void test_automatic_shift_contact_and_threshold(void)
{
	reset_car();
	car.car_transmission = TRANSMISSION_AUTOMATIC;
	car.car_currpm = simd.upshift_rpm;
	update_car_speed(INPUT_NONE, PLAYER_CAR_INDEX, &car, &simd);
	assert(car.car_current_gear == 1);

	car.car_currpm = simd.upshift_rpm + 1;
	car.car_sumSurfRearWheels = 0;
	update_car_speed(INPUT_NONE, PLAYER_CAR_INDEX, &car, &simd);
	assert(car.car_current_gear == 1);

	car.car_currpm = simd.upshift_rpm + 1;
	car.car_sumSurfRearWheels = 2;
	update_car_speed(INPUT_NONE, PLAYER_CAR_INDEX, &car, &simd);
	assert(car.car_current_gear == 2);
}

static void test_shift_completion_and_delay(void)
{
	reset_car();
	car.car_current_gear = 2;
	car.car_changing_gear = CAR_GEAR_CHANGE_ACTIVE;
	car.car_knob_x2 = car.car_knob_x;
	car.car_knob_y2 = car.car_knob_y;
	car.car_gear_change_delay = 10;
	simd.gear_ratios[2] = 2048;
	update_car_speed(INPUT_NONE, PLAYER_CAR_INDEX, &car, &simd);
	assert(car.car_changing_gear == CAR_GEAR_CHANGE_INACTIVE);
	assert(car.car_gearratio == 2048);
	assert(car.car_gearratioshr8 == 8);
	assert(car.car_gear_change_delay == 10);
	update_car_speed(INPUT_NONE, PLAYER_CAR_INDEX, &car, &simd);
	assert(car.car_gear_change_delay == 9);
}

static void test_pedals_and_frame_rates(void)
{
	reset_car();
	update_car_speed(INPUT_ACCELERATE_FLAG, PLAYER_CAR_INDEX, &car, &simd);
	assert(car.car_actual_speed == 16016);
	assert(car.car_is_accelerating == CAR_PEDAL_PRESSED);
	assert(state.game_topSpeed == 16016);

	reset_car();
	framespersec = GAME_FRAME_RATE_LOW;
	update_car_speed(INPUT_ACCELERATE_FLAG, PLAYER_CAR_INDEX, &car, &simd);
	assert(car.car_actual_speed == 16032);

	reset_car();
	oppnentSped[0] = 100;
	update_car_speed(INPUT_ACCELERATE_FLAG, OPPONENT_CAR_INDEX, &car, &simd);
	assert(car.car_actual_speed == 16012);

	reset_car();
	update_car_speed(INPUT_BRAKE_FLAG, PLAYER_CAR_INDEX, &car, &simd);
	assert(car.car_actual_speed == 15900);
	assert(car.car_is_braking == CAR_PEDAL_PRESSED);

	reset_car();
	update_car_speed(INPUT_BRAKE_FLAG, OPPONENT_CAR_INDEX, &car, &simd);
	assert(car.car_actual_speed == 15800);

	reset_car();
	update_car_speed(INPUT_PEDAL_MASK, PLAYER_CAR_INDEX, &car, &simd);
	assert(car.car_actual_speed == 16000);
	assert(car.car_is_braking == CAR_PEDAL_RELEASED);
	assert(car.car_is_accelerating == CAR_PEDAL_RELEASED);
}

static void test_overrev_preserves_pedal_state(void)
{
	reset_car();
	car.car_currpm = simd.max_rpm + 1;
	car.car_is_accelerating = CAR_PEDAL_PRESSED;
	update_car_speed(INPUT_BRAKE_FLAG, PLAYER_CAR_INDEX, &car, &simd);
	assert(car.car_actual_speed == 15900);
	assert(car.car_is_accelerating == CAR_PEDAL_PRESSED);
	assert(car.car_is_braking == CAR_PEDAL_RELEASED);
}

static void test_airborne_and_wheel_synchronization(void)
{
	reset_car();
	car.car_sumSurfRearWheels = 0;
	update_car_speed(INPUT_ACCELERATE_FLAG, PLAYER_CAR_INDEX, &car, &simd);
	assert(car.car_rev_speed == 16768);
	assert(car.car_actual_speed == 16000);

	reset_car();
	car.car_rev_speed = 10000;
	update_car_speed(INPUT_NONE, PLAYER_CAR_INDEX, &car, &simd);
	assert(car.car_rev_speed == 13000);
	assert(car.car_actual_speed == 13000);
	assert(car.car_engineLimiterTimer == 5);

	reset_car();
	car.car_rev_speed = 10880;
	update_car_speed(INPUT_NONE, PLAYER_CAR_INDEX, &car, &simd);
	assert(car.car_rev_speed == 10880);
	assert(car.car_actual_speed == 10880);
}

static void test_speed_wrap_and_stop(void)
{
	reset_car();
	car.car_rev_speed = car.car_actual_speed = 50;
	update_car_speed(INPUT_BRAKE_FLAG, PLAYER_CAR_INDEX, &car, &simd);
	assert(car.car_actual_speed == CAR_SPEED_STOPPED);

	reset_car();
	car.car_sumSurfRearWheels = 0;
	car.car_rev_speed = 65000;
	car.car_pseudoGravity = 1000;
	update_car_speed(INPUT_NONE, PLAYER_CAR_INDEX, &car, &simd);
	assert(car.car_rev_speed == 62720);

	reset_car();
	car.car_sumSurfRearWheels = 0;
	car.car_rev_speed = 32760;
	car.car_pseudoGravity = 1000;
	update_car_speed(INPUT_NONE, PLAYER_CAR_INDEX, &car, &simd);
	assert(car.car_rev_speed == 33760);
}

static void configure_powergear_option(const char *option)
{
	legacy_s8 *argv[] = {(legacy_s8 *)"restunts", (legacy_s8 *)option};
	configure_powergear_bug(2, argv);
}

static void prepare_powergear_car(legacy_s16 mass, legacy_s16 drag, legacy_s16 gravity)
{
	reset_car();
	simd.car_mass = mass;
	car.car_rev_speed = car.car_actual_speed = 59000;
	aerodynamic_drag[59000 >> 10] = drag;
	car.car_pseudoGravity = gravity;
}

static void test_powergear_initial_default(void)
{
	prepare_powergear_car(15, 544, 0);
	update_car_speed(INPUT_ACCELERATE_FLAG, PLAYER_CAR_INDEX, &car, &simd);
	assert(car.car_actual_speed == 60757);
}

static void test_powergear_stock_mass_classes(void)
{
	static const struct {
		legacy_s16 mass;
		legacy_u16 legacy_speed;
		legacy_s16 corrected_delta;
		legacy_s16 positive_delta;
	} cases[] = {
		/* All stock mass classes, including flexible/rigid PG, anti-PG and no PG.
		 * Net force -512 gives -12800 / mass, truncated toward zero; the final
		 * arithmetic right shift rounds negative odd quotients down. */
		{15, 60757, -427, 26}, /* Indy */
		{20, 59000, -320, 20}, /* Porsche 962 */
		{21, 59000, -305, 19}, /* Jaguar */
		{25, 62720, -256, 16}, /* Audi and Lancia */
		{27, 62720, -237, 14}, /* Ferrari */
		{31, 60907, -206, 12}, /* Acura */
		{32, 58800, -200, 12}, /* Carrera */
		{33, 56820, -194, 12}, /* Countach */
		{35, 62720, -183, 11}, /* Corvette */
		{55, 59000, -116, 7},  /* LM002 */
	};
	static const legacy_u16 frame_rates[] = {GAME_FRAME_RATE_NORMAL, GAME_FRAME_RATE_LOW};

	for (legacy_u32 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		configure_powergear_option("--pg:on");
		prepare_powergear_car(cases[index].mass, 544, 0);
		update_car_speed(INPUT_ACCELERATE_FLAG, PLAYER_CAR_INDEX, &car, &simd);
		assert(car.car_actual_speed == cases[index].legacy_speed);

		configure_powergear_option("--pg:off");
		for (legacy_u32 rate = 0; rate < sizeof(frame_rates) / sizeof(frame_rates[0]); rate++) {
			for (legacy_s16 car_index = PLAYER_CAR_INDEX; car_index <= OPPONENT_CAR_INDEX;
				 car_index++) {
				for (legacy_u32 gravity_case = 0; gravity_case < 2; gravity_case++) {
					/* Drag and uphill pseudogravity must produce the same net force. */
					prepare_powergear_car(cases[index].mass, gravity_case ? 0 : 544,
										  gravity_case ? -544 : 0);
					framespersec = frame_rates[rate];
					oppnentSped[0] = 100;
					legacy_s16 expected_delta = cases[index].corrected_delta;
					if (car_index == OPPONENT_CAR_INDEX) {
						expected_delta -= expected_delta / 4;
					}
					if (framespersec == GAME_FRAME_RATE_LOW) {
						expected_delta *= 2;
					}
					update_car_speed(INPUT_ACCELERATE_FLAG, car_index, &car, &simd);
					assert(car.car_actual_speed == 59000 + expected_delta);
					assert(car.car_rev_speed == car.car_actual_speed);
					assert(car.car_engineLimiterTimer == 0);
				}
			}
		}

		/* Positive force keeps its original acceleration in either mode. */
		for (legacy_u32 mode = 0; mode < 2; mode++) {
			configure_powergear_option(mode ? "--pg:off" : "--pg:on");
			prepare_powergear_car(cases[index].mass, 0, 0);
			update_car_speed(INPUT_ACCELERATE_FLAG, PLAYER_CAR_INDEX, &car, &simd);
			assert(car.car_actual_speed == 59000 + cases[index].positive_delta);
		}
	}
}

static void test_powergear_division_rounding(void)
{
	static const struct {
		legacy_s16 mass;
		legacy_s16 force;
		legacy_s16 expected_delta;
	} cases[] = {
		{25, -3, -2}, {25, -1, -1}, {25, 0, 0}, {25, 1, 0},	  {25, 3, 1},
		{32, -1, 0},  {32, -3, -1}, {32, 3, 1}, {15, -1, -1},
	};

	configure_powergear_option("--pg:off");
	for (legacy_u32 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		prepare_powergear_car(cases[index].mass, 32 - cases[index].force, 0);
		update_car_speed(INPUT_ACCELERATE_FLAG, PLAYER_CAR_INDEX, &car, &simd);
		assert(car.car_actual_speed == 59000 + cases[index].expected_delta);
	}

	/* A power-of-two mass still differs by one unit for fractional negatives:
	 * unsigned division floors the negative equivalent; signed division does not. */
	configure_powergear_option("--pg:on");
	prepare_powergear_car(32, 33, 0);
	update_car_speed(INPUT_ACCELERATE_FLAG, PLAYER_CAR_INDEX, &car, &simd);
	assert(car.car_actual_speed == 58999);
}

static void test_powergear_options(void)
{
	static const struct {
		const legacy_char *first;
		const legacy_char *second;
		legacy_u16 expected_speed;
	} cases[] = {
		/* Removed spellings are ignored, including after a recognized option. */
		{"/pg:off", NULL, 60757},
		{"/PG:OFF", NULL, 60757},
		{"--pg:off", "/pg:on", 58573},
		{"--pg:off", "/PG:ON", 58573},
		{NULL, NULL, 60757},
		{"--pg:on", NULL, 60757},
		{"--pg:off", NULL, 58573},
		{"--PG:OFF", NULL, 58573},
		{"--Pg:OfF", NULL, 58573},
		{"--pg", NULL, 60757},
		{"pg:off", NULL, 60757},
		{"-pg:off", NULL, 60757},
		{"--pg:offx", NULL, 60757},
		{"--pg:off ", NULL, 60757},
		{"--pg:", NULL, 60757},
		{"--pg:off", "--pG:On", 60757},
		{"--PG:ON", "--pg:off", 58573},
		{"--pg:off", "--pg:onx", 58573},
		{"--pg:off", "--nointro", 58573},
		{"/ns", "--pg:off", 58573},
		{"--pg:off", "--pg:off", 58573},
	};

	for (legacy_u32 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		/* Each invocation must reset an earlier invocation's opt-in. */
		configure_powergear_option("--pg:off");
		legacy_s8 *argv[] = {(legacy_s8 *)"restunts", (legacy_s8 *)cases[index].first,
							 (legacy_s8 *)cases[index].second};
		legacy_s16 argc = cases[index].second ? 3 : cases[index].first ? 2 : 1;
		configure_powergear_bug(argc, argv);
		prepare_powergear_car(15, 544, 0);
		update_car_speed(INPUT_ACCELERATE_FLAG, PLAYER_CAR_INDEX, &car, &simd);
		assert(car.car_actual_speed == cases[index].expected_speed);
	}

	legacy_s8 *argv[] = {(legacy_s8 *)"--pg:off"};
	configure_powergear_bug(1, argv);
	prepare_powergear_car(15, 544, 0);
	update_car_speed(INPUT_ACCELERATE_FLAG, PLAYER_CAR_INDEX, &car, &simd);
	assert(car.car_actual_speed == 60757);
}

#ifndef PHYSICS_RECORD_BASELINE
static void test_fractional_held_pedals(void)
{
	static const legacy_u16 rates[] = {GAME_FRAME_RATE_NORMAL, GAME_FRAME_RATE_LOW};
	for (legacy_u16 rate = 0; rate < sizeof(rates) / sizeof(rates[0]); rate++) {
		reset_car();
		framespersec = rates[rate];
		state.game_topSpeed = 1234;
		car.car_gear_change_delay = 8;
		car.car_engineLimiterTimer = 7;
		struct CARSTATE before = car;
		struct GAMESTATE saved_state = state;
		update_car_speed_fraction(INPUT_ACCELERATE_FLAG | INPUT_SHIFT_UP_FLAG, PLAYER_CAR_INDEX,
								  &car, &simd, 0);
		assert(memcmp(&car, &before, sizeof(car)) == 0);
		for (legacy_u16 fractional_step = 1; fractional_step <= 2; fractional_step++) {
			update_car_speed_fraction(INPUT_ACCELERATE_FLAG | INPUT_SHIFT_UP_FLAG, PLAYER_CAR_INDEX,
									  &car, &simd, 21845UL);
			assert(car.car_actual_speed == 16000 + 5 * fractional_step);
			assert(car.car_rev_speed == car.car_actual_speed);
			assert(car.car_is_accelerating == CAR_PEDAL_PRESSED);
			assert(car.car_current_gear == before.car_current_gear);
			assert(car.car_changing_gear == before.car_changing_gear);
			assert(car.car_gear_change_delay == before.car_gear_change_delay);
			assert(car.car_engineLimiterTimer == before.car_engineLimiterTimer);
			assert(car.car_knob_x == before.car_knob_x);
			assert(car.car_knob_y == before.car_knob_y);
			assert(memcmp(&state, &saved_state, sizeof(state)) == 0);
		}

		reset_car();
		framespersec = rates[rate];
		update_car_speed_fraction(INPUT_BRAKE_FLAG, PLAYER_CAR_INDEX, &car, &simd, 21845UL);
		assert(car.car_actual_speed == 15967);
		update_car_speed_fraction(INPUT_BRAKE_FLAG, PLAYER_CAR_INDEX, &car, &simd, 21845UL);
		assert(car.car_actual_speed == 15934);
		assert(car.car_is_braking == CAR_PEDAL_PRESSED);
		assert(state.game_topSpeed == 0);
		car.car_rev_speed = car.car_actual_speed = 20;
		update_car_speed_fraction(INPUT_BRAKE_FLAG, PLAYER_CAR_INDEX, &car, &simd, 21845UL);
		assert(car.car_actual_speed == CAR_SPEED_STOPPED);

		reset_car();
		framespersec = rates[rate];
		car.car_sumSurfRearWheels = CAR_WHEEL_CONTACT_NONE;
		update_car_speed_fraction(INPUT_ACCELERATE_FLAG, PLAYER_CAR_INDEX, &car, &simd, 21845UL);
		assert(car.car_actual_speed == 16000);
		assert(car.car_rev_speed == 16255);
		update_car_speed_fraction(INPUT_ACCELERATE_FLAG, PLAYER_CAR_INDEX, &car, &simd, 21845UL);
		assert(car.car_actual_speed == 16000);
		assert(car.car_rev_speed == 16510);
		assert(state.game_topSpeed == 0);

		/* On landing, wheel and road speeds synchronize as one contact
		 * constraint. Later fractional steps must not overshoot that shared speed. */
		reset_car();
		framespersec = rates[rate];
		car.car_rev_speed = 10000;
		for (legacy_u16 fractional_step = 0; fractional_step < 2; fractional_step++) {
			update_car_speed_fraction(INPUT_NONE, PLAYER_CAR_INDEX, &car, &simd, 21845UL);
			assert(car.car_actual_speed == 13000);
			assert(car.car_rev_speed == 13000);
			assert(state.game_topSpeed == 0);
		}
	}
}
#endif

static legacy_u32 authoritative_speed_fingerprint(legacy_s16 include_fractional_steps)
{
	static const legacy_s16 masses[] = {15, 25, 32, 55};
	static const legacy_u16 rates[] = {GAME_FRAME_RATE_NORMAL, GAME_FRAME_RATE_LOW};
	static const legacy_s8 inputs[] = {INPUT_ACCELERATE_FLAG, INPUT_NONE, INPUT_BRAKE_FLAG,
									   INPUT_ACCELERATE_FLAG | INPUT_SHIFT_UP_FLAG,
									   INPUT_SHIFT_DOWN_FLAG};
	legacy_u32 hash = 2166136261UL;
	configure_powergear_option("--pg:on");
#ifdef PHYSICS_RECORD_BASELINE
	(void)include_fractional_steps;
#endif
	for (legacy_u16 rate = 0; rate < sizeof(rates) / sizeof(rates[0]); rate++) {
		for (legacy_u16 mass = 0; mass < sizeof(masses) / sizeof(masses[0]); mass++) {
			for (legacy_s16 car_index = PLAYER_CAR_INDEX; car_index <= OPPONENT_CAR_INDEX;
				 car_index++) {
				reset_car();
				framespersec = rates[rate];
				simd.car_mass = masses[mass];
				car.car_transmission = mass % 2 ? TRANSMISSION_AUTOMATIC : TRANSMISSION_MANUAL;
				for (legacy_u16 tick = 0; tick < 50; tick++) {
					legacy_s8 input = inputs[tick % (sizeof(inputs) / sizeof(inputs[0]))];
					car.car_sumSurfRearWheels = tick % 7 < 2 ? 0 : 2;
					car.car_sumSurfAllWheels = car.car_sumSurfRearWheels * 2;
#ifndef PHYSICS_RECORD_BASELINE
					if (include_fractional_steps != 0) {
						struct CARSTATE fractional_car = car;
						struct GAMESTATE saved_state = state;
						update_car_speed_fraction(input, car_index, &fractional_car, &simd,
												  21845UL);
						update_car_speed_fraction(input, car_index, &fractional_car, &simd,
												  21846UL);
						assert(memcmp(&state, &saved_state, sizeof(state)) == 0);
					}
#endif
					update_car_speed(input, car_index, &car, &simd);
					const legacy_u8 *bytes = (const legacy_u8 *)&car;
					for (legacy_u16 i = 0; i < sizeof(car); i++) {
						hash = (hash ^ bytes[i]) * 16777619UL;
					}
					hash = (hash ^ state.game_topSpeed) * 16777619UL;
				}
			}
		}
	}
	return hash;
}

int main(void)
{
	test_powergear_initial_default();
	test_shift_precedence_and_limits();
	test_automatic_shift_contact_and_threshold();
	test_shift_completion_and_delay();
	test_pedals_and_frame_rates();
	test_overrev_preserves_pedal_state();
	test_airborne_and_wheel_synchronization();
	test_speed_wrap_and_stop();
	test_powergear_stock_mass_classes();
	test_powergear_division_rounding();
	test_powergear_options();
#ifdef PHYSICS_RECORD_BASELINE
	printf("%08" LEGACY_PRIx32 "\n", authoritative_speed_fingerprint(0));
#else
	test_fractional_held_pedals();
	legacy_u32 fingerprint = authoritative_speed_fingerprint(0);
	/* Captured from the original authoritative implementation. */
	assert(fingerprint == 0x51ccdf3cUL);
	assert(authoritative_speed_fingerprint(1) == fingerprint);
#endif
	return 0;
}
