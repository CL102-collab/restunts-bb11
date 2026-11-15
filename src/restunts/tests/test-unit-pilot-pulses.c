#include <assert.h>
#include <math.h>
#include <string.h>
#include "../c/opponent/unit_pilot_pulses.h"
#include "../c/opponent/unit_opponent_internal.h"
#include "../c/extended/extended_owoot_internal.h"
#include "../c/game_input.h"
#include "../c/math/math.h"

#define TEST_SEED 0x12345678U
#define TEST_FRAME_RATE 20U
#define TEST_POINT_COUNT 6U
#define TEST_BARREL_LAST 3U
#define TEST_SOURCE_PIECE 4
#define TEST_LIP_DISTANCE 800.0
#define TEST_RECEIVER_DISTANCE 1200.0
#define TEST_RECEIVER_HEIGHT 100.0
#define TEST_BODY_HEIGHT 8.0
#define TEST_ENTRY_FRAME 110U
#define TEST_BANK_DELAY 7U
#define TEST_AIR_DELAY 32U
#define TEST_FINISH_DELAY 50U
#define TEST_ATTEMPT_LIMIT 2048U
#define TEST_TIMING_WINDOW 160U
#define TEST_NO_CONTACT 0U
#define TEST_MAXIMUM_SPEED 200.0
#define TEST_MINIMUM_SPEED 20.0
#define TEST_MAPPING_ATTEMPTS 256U
#define TEST_FLOAT_TOLERANCE 0.000000001

static struct UNIT_ROUTE_POINT points[TEST_POINT_COUNT];
static struct UNIT_ROUTE route;
static struct CARSTATE car;
static legacy_u16 native_progress;
static legacy_u8 override_mask, override_value;
static const struct UNIT_PILOT_PULSE_PROFILE preferred = {91.0, -0.25, 0.875, 82.0, 87.0, 3U};
static const struct UNIT_PILOT_PULSE_PROFILE higher_roll = {96.0, 0.75, 0.75, 87.0, 93.0, 4U};
static const struct UNIT_PILOT_PULSE_PROFILE last_scalar = {88.0, -0.75, 0.9375, 79.0, 75.0, 4U};

legacy_u16 extended_owoot_barrel_progress(const struct CARSTATE *observed, legacy_s16 source)
{
	assert(observed == &car);
	assert(source == points[0].route_index);
	return native_progress;
}

void unit_opponent_pilot_input_override(legacy_u8 mask, legacy_u8 value)
{
	override_mask = mask;
	override_value = value;
}

static void assert_clear(void)
{
	assert(override_mask == 0U);
	assert(override_value == 0U);
}

static void assert_profile(const struct UNIT_PILOT_PULSE_PROFILE *actual,
						   const struct UNIT_PILOT_PULSE_PROFILE *expected)
{
	assert(actual->speed == expected->speed);
	assert(actual->barrel_offset == expected->barrel_offset);
	assert(actual->exit_fraction == expected->exit_fraction);
	assert(actual->exit_speed == expected->exit_speed);
	assert(actual->secondary_speed == expected->secondary_speed);
	assert(actual->secondary_ramp_approach == expected->secondary_ramp_approach);
}

static void assert_mapping(const struct UNIT_PILOT_PULSE_DIAGNOSTIC *trial,
						   const struct UNIT_PILOT_PULSE_PROFILE *baseline)
{
	legacy_f64 minimum = fmin(TEST_MINIMUM_SPEED, route.limits.max_speed);
	legacy_f64 speed = baseline->speed * trial->parameters.speed;
	speed = speed < minimum					 ? minimum
			: speed > route.limits.max_speed ? route.limits.max_speed
											 : speed;
	legacy_f64 offset = baseline->barrel_offset + trial->parameters.lateral;
	offset = offset < -1.0 ? -1.0 : offset > 1.0 ? 1.0 : offset;
	legacy_f64 exit_speed = baseline->exit_speed * speed / baseline->speed;
	assert(fabs(trial->profile.speed - speed) < TEST_FLOAT_TOLERANCE);
	assert(fabs(trial->profile.barrel_offset - offset) < TEST_FLOAT_TOLERANCE);
	assert(fabs(trial->profile.exit_speed - fmin(route.limits.max_speed, exit_speed)) <
		   TEST_FLOAT_TOLERANCE);
	assert(trial->profile.secondary_speed == baseline->secondary_speed);
	assert(trial->profile.secondary_ramp_approach == baseline->secondary_ramp_approach);
	assert(trial->profile.exit_fraction == baseline->exit_fraction);
	assert(trial->parameters.shift == 0.0);
	assert(trial->parameters.checkpoint == 0U);
	assert(trial->parameters.air_brake == 0U);
}

static void assert_parameters(const struct EXTENDED_BARREL_PARAMETERS *actual,
							  const struct EXTENDED_BARREL_PARAMETERS *expected)
{
	assert(actual->speed == expected->speed);
	assert(actual->lateral == expected->lateral);
	assert(actual->shift == expected->shift);
	assert(actual->checkpoint == expected->checkpoint);
	assert(actual->begin == expected->begin);
	assert(actual->duration == expected->duration);
	assert(actual->gap == expected->gap);
	assert(actual->counter == expected->counter);
	assert(actual->pedal_begin == expected->pedal_begin);
	assert(actual->pedal_duration == expected->pedal_duration);
	assert(actual->side == expected->side);
	assert(actual->pedal == expected->pedal);
	assert(actual->air_brake == expected->air_brake);
}

static void place_car(legacy_f64 along, legacy_f64 height_error, legacy_u8 grounded,
					  legacy_u8 source)
{
	car = (struct CARSTATE){0};
	car.car_position.lx = (legacy_s32)(along * UNIT_POSITION_SCALE);
	car.car_position.ly = (legacy_s32)((TEST_RECEIVER_HEIGHT + TEST_BODY_HEIGHT + height_error) *
									   UNIT_POSITION_SCALE);
	car.car_sumSurfAllWheels = grounded;
	car.car_route_index = points[0].route_index + (along >= TEST_RECEIVER_DISTANCE);
	for (legacy_u32 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
		car.car_surfaceWhl[wheel] = grounded ? CAR_SURFACE_PAVED : TEST_NO_CONTACT;
	}
	car.car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_SOURCE] =
		source ? points[0].route_index + 1 : 0;
}

static struct UNIT_PILOT_PULSES *reset_fixture(void)
{
	memset(points, 0, sizeof(points));
	route = (struct UNIT_ROUTE){0};
	route.points = points;
	route.count = TEST_POINT_COUNT;
	route.limits.half_length = 56.0;
	route.limits.max_speed = TEST_MAXIMUM_SPEED;
	for (legacy_u32 index = 0; index < TEST_POINT_COUNT; index++) {
		points[index].x = index * 400.0;
		points[index].y = TEST_RECEIVER_HEIGHT;
		points[index].half_width = 120.0;
		points[index].route_index = TEST_SOURCE_PIECE + (index > TEST_BARREL_LAST);
		points[index].stunt = index <= TEST_BARREL_LAST ? UNIT_STUNT_BARREL : UNIT_STUNT_NONE;
	}
	points[TEST_BARREL_LAST].x = 1024.0;
	points[TEST_BARREL_LAST + 1U].x = TEST_RECEIVER_DISTANCE;
	native_progress = 0U;
	unit_opponent_pilot_input_override(0U, 0U);
	struct UNIT_PILOT_PULSES *pulses = unit_pilot_pulses_create(TEST_SEED);
	assert(pulses);
	assert(unit_pilot_pulses_begin(pulses, &route, 0U, TEST_BARREL_LAST, TEST_FRAME_RATE));
	return pulses;
}

static struct UNIT_PILOT_PULSE_DIAGNOSTIC
measure_trial(struct UNIT_PILOT_PULSES *pulses, const struct UNIT_PILOT_PULSE_PROFILE *profile,
			  legacy_u16 progress, legacy_f64 height_error, legacy_u32 air_delay,
			  legacy_u8 accepted)
{
	unit_pilot_pulses_start_trial(pulses, TEST_ENTRY_FRAME - 1U, profile);
	native_progress = 0U;
	place_car(-10.0, 0.0, 1U, 0U);
	unit_pilot_pulses_before_step(pulses, &car, TEST_ENTRY_FRAME - 1U);
	assert_clear();
	place_car(0.0, 0.0, 1U, 0U);
	unit_pilot_pulses_before_step(pulses, &car, TEST_ENTRY_FRAME);
	place_car(300.0, 0.0, 1U, 1U);
	unit_pilot_pulses_before_step(pulses, &car, TEST_ENTRY_FRAME + TEST_BANK_DELAY);
	place_car(TEST_LIP_DISTANCE, 0.0, 0U, 1U);
	unit_pilot_pulses_before_step(pulses, &car, TEST_ENTRY_FRAME + air_delay);
	native_progress = progress;
	place_car(TEST_RECEIVER_DISTANCE, height_error, 1U, 0U);
	unit_pilot_pulses_finish_trial(pulses, &car, TEST_ENTRY_FRAME + air_delay + TEST_FINISH_DELAY,
								   accepted);
	assert_clear();
	struct UNIT_PILOT_PULSE_DIAGNOSTIC diagnostic;
	assert(unit_pilot_pulses_last_trial(pulses, &diagnostic));
	assert(diagnostic.progress == progress);
	assert_profile(&diagnostic.profile, profile);
	return diagnostic;
}

static void test_fitness_freezes_complete_profile(void)
{
	struct UNIT_PILOT_PULSES *pulses = reset_fixture();
	struct UNIT_PILOT_PULSE_DIAGNOSTIC good =
		measure_trial(pulses, &preferred, 1000U, 0.0, TEST_AIR_DELAY, 0U);
	struct UNIT_PILOT_PULSE_DIAGNOSTIC high =
		measure_trial(pulses, &higher_roll, 1023U, 1000.0, TEST_AIR_DELAY + 10U, 0U);
	struct UNIT_PILOT_PULSE_DIAGNOSTIC last =
		measure_trial(pulses, &last_scalar, 800U, 0.0, TEST_AIR_DELAY + 20U, 0U);
	assert(good.fitness > high.fitness && good.fitness > last.fitness);
	struct UNIT_PILOT_PULSE_PROFILE selected;
	assert(unit_pilot_pulses_next(pulses, &selected));
	assert(unit_pilot_pulses_active(pulses));
	struct UNIT_PILOT_PULSE_DIAGNOSTIC pulse =
		measure_trial(pulses, &selected, 1000U, 0.0, TEST_AIR_DELAY, 0U);
	assert(pulse.active && unit_pilot_pulses_active(pulses));
	assert(pulse.span == TEST_AIR_DELAY && pulse.bank == TEST_BANK_DELAY);
	assert(pulse.attempts == 1U);
	assert_mapping(&pulse, &preferred);
	unit_pilot_pulses_destroy(pulses);
}

static void test_measured_timing_and_cleanup(void)
{
	struct UNIT_PILOT_PULSES *pulses = reset_fixture();
	(void)measure_trial(pulses, &preferred, 1000U, 0.0, TEST_AIR_DELAY, 0U);
	struct UNIT_PILOT_PULSE_PROFILE selected;
	assert(unit_pilot_pulses_next(pulses, &selected));
	struct UNIT_PILOT_PULSE_DIAGNOSTIC pulse =
		measure_trial(pulses, &selected, 1000U, 0.0, TEST_AIR_DELAY, 0U);
	unit_pilot_pulses_start_trial(pulses, TEST_ENTRY_FRAME, &selected);
	native_progress = 0U;
	place_car(-10.0, 0.0, 1U, 0U);
	unit_pilot_pulses_before_step(pulses, &car, TEST_ENTRY_FRAME);
	assert_clear();
	legacy_u8 observed_masks = 0U;
	for (legacy_u32 tick = 0; tick < TEST_TIMING_WINDOW; tick++) {
		place_car(300.0, 0.0, 1U, 1U);
		unit_pilot_pulses_before_step(pulses, &car, TEST_ENTRY_FRAME + 1U + tick);
		struct EXTENDED_BARREL_INPUT expected =
			extended_barrel_search_input(&pulse.parameters, pulse.span, tick);
		assert(override_mask == expected.mask && override_value == expected.value);
		observed_masks |= override_mask;
	}
	assert(observed_masks != 0U);
	/* Once landed, every later scheduled pulse must remain disabled. */
	unit_pilot_pulses_start_trial(pulses, TEST_ENTRY_FRAME, &selected);
	place_car(TEST_LIP_DISTANCE, 0.0, 0U, 1U);
	unit_pilot_pulses_before_step(pulses, &car, TEST_ENTRY_FRAME);
	place_car(TEST_RECEIVER_DISTANCE, 0.0, 1U, 0U);
	for (legacy_u32 tick = 1U; tick < TEST_TIMING_WINDOW; tick++) {
		unit_pilot_pulses_before_step(pulses, &car, TEST_ENTRY_FRAME + tick);
		assert_clear();
	}
	unit_opponent_pilot_input_override(INPUT_STEERING_MASK, INPUT_STEER_LEFT_FLAG);
	unit_pilot_pulses_before_step(pulses, NULL, TEST_ENTRY_FRAME);
	assert_clear();
	unit_opponent_pilot_input_override(INPUT_PEDAL_MASK, INPUT_BRAKE_FLAG);
	unit_pilot_pulses_finish_trial(pulses, NULL, TEST_ENTRY_FRAME, 0U);
	assert_clear();
	unit_pilot_pulses_destroy(pulses);
}

static void test_reapproach_and_exact_known_retry(void)
{
	struct UNIT_PILOT_PULSES *pulses = reset_fixture();
	(void)measure_trial(pulses, &preferred, 1000U, 0.0, TEST_AIR_DELAY, 0U);
	struct UNIT_PILOT_PULSE_PROFILE selected;
	assert(unit_pilot_pulses_next(pulses, &selected));
	struct UNIT_PILOT_PULSE_DIAGNOSTIC accepted =
		measure_trial(pulses, &selected, ANGLE_FULL_TURN, 0.0, TEST_AIR_DELAY, 1U);
	assert(unit_pilot_pulses_begin(pulses, &route, 0U, TEST_BARREL_LAST, TEST_FRAME_RATE));
	assert(!unit_pilot_pulses_active(pulses));
	assert_clear();
	/* A lower-scoring changed approach must replace stale scalar fitness.
	 * The accepted pulse still receives one exact, independently checked retry. */
	(void)measure_trial(pulses, &last_scalar, 100U, 1000.0, TEST_AIR_DELAY * 2U, 0U);
	assert(unit_pilot_pulses_next(pulses, &selected));
	assert_profile(&selected, &accepted.profile);
	struct UNIT_PILOT_PULSE_DIAGNOSTIC retried =
		measure_trial(pulses, &selected, 100U, 0.0, TEST_AIR_DELAY * 2U, 0U);
	assert_parameters(&retried.parameters, &accepted.parameters);
	assert(retried.span == accepted.span && retried.bank == accepted.bank);
	assert(retried.attempts == 1U);
	assert(unit_pilot_pulses_next(pulses, &selected));
	struct UNIT_PILOT_PULSE_DIAGNOSTIC changed =
		measure_trial(pulses, &selected, 100U, 0.0, TEST_AIR_DELAY * 2U, 0U);
	assert(changed.span == TEST_AIR_DELAY * 2U);
	assert_mapping(&changed, &last_scalar);
	/* A deadline restart can continue immediately from retained ideas. */
	assert(unit_pilot_pulses_begin(pulses, &route, 0U, TEST_BARREL_LAST, TEST_FRAME_RATE));
	assert(unit_pilot_pulses_next(pulses, &selected));
	assert_profile(&selected, &accepted.profile);
	for (legacy_u32 point = 0; point < TEST_POINT_COUNT; point++) {
		points[point].route_index++;
	}
	assert(unit_pilot_pulses_begin(pulses, &route, 0U, TEST_BARREL_LAST, TEST_FRAME_RATE));
	assert(!unit_pilot_pulses_next(pulses, &selected));
	assert(!unit_pilot_pulses_active(pulses));
	unit_pilot_pulses_destroy(pulses);
}

static void test_disabled_paths_and_bound(void)
{
	struct UNIT_PILOT_PULSES *pulses = reset_fixture();
	struct UNIT_PILOT_PULSE_PROFILE selected;
	assert(!unit_pilot_pulses_next(pulses, &selected));
	(void)measure_trial(pulses, &preferred, 1000U, 0.0, TEST_AIR_DELAY, 0U);
	for (legacy_u32 attempt = 0; attempt < TEST_ATTEMPT_LIMIT; attempt++) {
		assert(unit_pilot_pulses_next(pulses, &selected));
		(void)measure_trial(pulses, &selected, 1000U, 0.0, TEST_AIR_DELAY, 0U);
	}
	assert(!unit_pilot_pulses_next(pulses, &selected));
	struct UNIT_PILOT_PULSE_DIAGNOSTIC last;
	assert(unit_pilot_pulses_last_trial(pulses, &last));
	assert(last.attempts == TEST_ATTEMPT_LIMIT);
	points[0].stunt = UNIT_STUNT_PROTECTED;
	unit_opponent_pilot_input_override(INPUT_PEDAL_MASK, INPUT_BRAKE_FLAG);
	assert(!unit_pilot_pulses_begin(pulses, &route, 0U, TEST_BARREL_LAST, TEST_FRAME_RATE));
	assert_clear();
	assert(!unit_pilot_pulses_active(pulses));
	assert(!unit_pilot_pulses_next(pulses, &selected));
	unit_pilot_pulses_start_trial(pulses, TEST_ENTRY_FRAME, &preferred);
	place_car(0.0, 0.0, 1U, 0U);
	unit_pilot_pulses_before_step(pulses, &car, TEST_ENTRY_FRAME);
	assert_clear();
	points[0].stunt = UNIT_STUNT_BARREL;
	assert(!unit_pilot_pulses_begin(pulses, &route, 0U, 1U, TEST_FRAME_RATE));
	assert(!unit_pilot_pulses_begin(pulses, &route, 0U, route.count, TEST_FRAME_RATE));
	assert(!unit_pilot_pulses_begin(pulses, &route, 0U, TEST_BARREL_LAST, 0U));
	assert(!unit_pilot_pulses_begin(pulses, NULL, 0U, TEST_BARREL_LAST, TEST_FRAME_RATE));
	assert(!unit_pilot_pulses_active(NULL));
	route.limits.max_speed = 0.0;
	assert(!unit_pilot_pulses_begin(pulses, &route, 0U, TEST_BARREL_LAST, TEST_FRAME_RATE));
	route.limits.max_speed = NAN;
	assert(!unit_pilot_pulses_begin(pulses, &route, 0U, TEST_BARREL_LAST, TEST_FRAME_RATE));
	unit_pilot_pulses_destroy(pulses);
}

static void test_entry_requires_actual_barrel_approach(void)
{
	struct UNIT_PILOT_PULSES *pulses = reset_fixture();
	(void)measure_trial(pulses, &preferred, 1000U, 0.0, TEST_AIR_DELAY, 0U);
	struct UNIT_PILOT_PULSE_PROFILE selected;
	assert(unit_pilot_pulses_next(pulses, &selected));
	struct UNIT_PILOT_PULSE_DIAGNOSTIC pulse =
		measure_trial(pulses, &selected, 1000U, 0.0, TEST_AIR_DELAY, 0U);
	for (legacy_u8 approach = 0; approach < 3U; approach++) {
		unit_pilot_pulses_start_trial(pulses, TEST_ENTRY_FRAME, &selected);
		native_progress = 0U;
		place_car(300.0, 0.0, 1U, 0U);
		if (approach == 0U) {
			/* A preceding curve may project ahead of the ramp's entry plane. */
			car.car_route_index = points[0].route_index - 1;
		} else if (approach == 1U) {
			car.car_position.lz = (legacy_s32)(1000.0 * UNIT_POSITION_SCALE);
		} else {
			car.car_position.lx = (legacy_s32)(2000.0 * UNIT_POSITION_SCALE);
		}
		for (legacy_u32 tick = 0; tick < TEST_TIMING_WINDOW; tick++) {
			unit_pilot_pulses_before_step(pulses, &car, TEST_ENTRY_FRAME + tick);
			assert_clear();
		}
		/* The ignored geometry must not move the eventual entry clock. */
		for (legacy_u32 tick = 0; tick < TEST_TIMING_WINDOW; tick++) {
			place_car(300.0, 0.0, 1U, 1U);
			unit_pilot_pulses_before_step(pulses, &car,
										  TEST_ENTRY_FRAME + TEST_TIMING_WINDOW + tick);
			struct EXTENDED_BARREL_INPUT expected =
				extended_barrel_search_input(&pulse.parameters, pulse.span, tick);
			assert(override_mask == expected.mask && override_value == expected.value);
		}
	}
	unit_pilot_pulses_destroy(pulses);
}

static void test_landing_requires_receiving_road(void)
{
	struct UNIT_PILOT_PULSES *pulses = reset_fixture();
	(void)measure_trial(pulses, &preferred, 1000U, 0.0, TEST_AIR_DELAY, 0U);
	struct UNIT_PILOT_PULSE_PROFILE selected;
	assert(unit_pilot_pulses_next(pulses, &selected));
	struct UNIT_PILOT_PULSE_DIAGNOSTIC pulse =
		measure_trial(pulses, &selected, 1000U, 0.0, TEST_AIR_DELAY, 0U);
	for (legacy_u8 contact = 0; contact < 2U; contact++) {
		unit_pilot_pulses_start_trial(pulses, TEST_ENTRY_FRAME, &selected);
		native_progress = 0U;
		place_car(TEST_LIP_DISTANCE, 0.0, 0U, 1U);
		unit_pilot_pulses_before_step(pulses, &car, TEST_ENTRY_FRAME);
		place_car(TEST_RECEIVER_DISTANCE, 0.0, 1U, 0U);
		if (contact == 0U) {
			for (legacy_u32 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
				car.car_surfaceWhl[wheel] = CAR_SURFACE_GRASS;
			}
		} else {
			car.car_route_index = points[0].route_index;
		}
		legacy_u8 expected_masks = 0U;
		for (legacy_u32 tick = 1U; tick < TEST_TIMING_WINDOW; tick++) {
			unit_pilot_pulses_before_step(pulses, &car, TEST_ENTRY_FRAME + tick);
			struct EXTENDED_BARREL_INPUT expected =
				extended_barrel_search_input(&pulse.parameters, pulse.span, tick);
			assert(override_mask == expected.mask && override_value == expected.value);
			expected_masks |= expected.mask;
		}
		assert(expected_masks != 0U);
	}
	unit_pilot_pulses_destroy(pulses);
}

static void test_joint_profile_mapping_and_clamps(void)
{
	static const struct UNIT_PILOT_PULSE_PROFILE baselines[] = {
		{20.0, -1.0, 0.75, 200.0, 87.0, 3U},
		{200.0, 1.0, 0.875, 180.0, 93.0, 4U},
		{12.0, 0.0, 0.0, 0.0, 9.0, 0U}};
	legacy_u8 lower = 0U, upper = 0U, left = 0U, right = 0U, exit_clamped = 0U;
	legacy_u8 speed_mutated = 0U, lateral_mutated = 0U;
	for (legacy_u32 index = 0U; index < sizeof(baselines) / sizeof(baselines[0]); index++) {
		struct UNIT_PILOT_PULSES *pulses = reset_fixture();
		if (index == 2U) {
			route.limits.max_speed = baselines[index].speed;
			assert(unit_pilot_pulses_begin(pulses, &route, 0U, TEST_BARREL_LAST, TEST_FRAME_RATE));
		}
		(void)measure_trial(pulses, &baselines[index], 1000U, 0.0, TEST_AIR_DELAY, 0U);
		for (legacy_u32 attempt = 0U; attempt < TEST_MAPPING_ATTEMPTS; attempt++) {
			struct UNIT_PILOT_PULSE_PROFILE selected;
			assert(unit_pilot_pulses_next(pulses, &selected));
			struct UNIT_PILOT_PULSE_DIAGNOSTIC trial =
				measure_trial(pulses, &selected, 1000U, 0.0, TEST_AIR_DELAY, 0U);
			assert_mapping(&trial, &baselines[index]);
			speed_mutated |= trial.parameters.speed != 1.0;
			lateral_mutated |= trial.parameters.lateral != 0.0;
			legacy_f64 raw_speed = baselines[index].speed * trial.parameters.speed;
			if (index == 0U && raw_speed < TEST_MINIMUM_SPEED) {
				assert(selected.speed == TEST_MINIMUM_SPEED);
				lower = 1U;
			}
			if (index == 1U && raw_speed > route.limits.max_speed) {
				assert(selected.speed == route.limits.max_speed);
				upper = 1U;
			}
			if (baselines[index].barrel_offset + trial.parameters.lateral < -1.0) {
				assert(selected.barrel_offset == -1.0);
				left = 1U;
			}
			if (baselines[index].barrel_offset + trial.parameters.lateral > 1.0) {
				assert(selected.barrel_offset == 1.0);
				right = 1U;
			}
			if (baselines[index].exit_speed * selected.speed / baselines[index].speed >
				route.limits.max_speed) {
				assert(selected.exit_speed == route.limits.max_speed);
				exit_clamped = 1U;
			}
			if (index == 2U) {
				assert(selected.speed == route.limits.max_speed);
				assert(selected.exit_speed == 0.0 && selected.exit_fraction == 0.0);
			}
		}
		unit_pilot_pulses_destroy(pulses);
	}
	assert(lower && upper && left && right && exit_clamped);
	assert(speed_mutated && lateral_mutated);
}

static void test_historical_fitness_does_not_rank_relative_genes(void)
{
	struct UNIT_PILOT_PULSES *a = reset_fixture();
	struct UNIT_PILOT_PULSES *b = reset_fixture();
	struct UNIT_PILOT_PULSES *pilots[] = {a, b};
	for (legacy_u32 index = 0U; index < sizeof(pilots) / sizeof(pilots[0]); index++) {
		struct UNIT_PILOT_PULSES *pulses = pilots[index];
		(void)measure_trial(pulses, &preferred, 1000U, 0.0, TEST_AIR_DELAY, 0U);
		struct UNIT_PILOT_PULSE_PROFILE selected;
		assert(unit_pilot_pulses_next(pulses, &selected));
		(void)measure_trial(pulses, &selected, ANGLE_FULL_TURN, 0.0, TEST_AIR_DELAY, 1U);
		assert(unit_pilot_pulses_begin(pulses, &route, 0U, TEST_BARREL_LAST, TEST_FRAME_RATE));
		(void)measure_trial(pulses, &last_scalar, 100U, 1000.0, TEST_AIR_DELAY * 2U, 0U);
		assert(unit_pilot_pulses_next(pulses, &selected));
		/* Different historical outcomes must not rank genes under the new baseline. */
		(void)measure_trial(pulses, &selected, index ? 0U : ANGLE_FULL_TURN, 0.0,
							TEST_AIR_DELAY * 2U, 0U);
	}
	for (legacy_u32 attempt = 0U; attempt < TEST_MAPPING_ATTEMPTS; attempt++) {
		struct UNIT_PILOT_PULSE_PROFILE first, second;
		assert(unit_pilot_pulses_next(a, &first));
		assert(unit_pilot_pulses_next(b, &second));
		assert_profile(&first, &second);
		struct UNIT_PILOT_PULSE_DIAGNOSTIC one =
			measure_trial(a, &first, 1000U, 0.0, TEST_AIR_DELAY * 2U, 0U);
		struct UNIT_PILOT_PULSE_DIAGNOSTIC two =
			measure_trial(b, &second, 1000U, 0.0, TEST_AIR_DELAY * 2U, 0U);
		assert_parameters(&one.parameters, &two.parameters);
		assert_mapping(&one, &last_scalar);
	}
	unit_pilot_pulses_destroy(a);
	unit_pilot_pulses_destroy(b);
}

legacy_int main(void)
{
	test_joint_profile_mapping_and_clamps();
	test_historical_fitness_does_not_rank_relative_genes();
	test_fitness_freezes_complete_profile();
	test_measured_timing_and_cleanup();
	test_reapproach_and_exact_known_retry();
	test_disabled_paths_and_bound();
	test_entry_requires_actual_barrel_approach();
	test_landing_requires_receiving_road();
	return 0;
}
