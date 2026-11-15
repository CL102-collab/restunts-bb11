#include <assert.h>
#include <string.h>
#include "../c/gamestate.h"
#include "../c/game_input.h"
#include "../c/car/car_resources.h"
#include "../c/state_internal.h"
#include "../c/extended/csrpl.h"
#include "../c/opponent/unit_pilot_controls.h"

#define TEST_INITIAL_STEERING 73
#define TEST_NATIVE_STEERING (-39)
#define TEST_ROUTE_INDEX 42
#define TEST_ROUTE_POINT 17U
#define TEST_UNKNOWN_FLAG 128U
#define TEST_NULL_ARGUMENT_CASES 2U

static struct CARSTATE car;
static const struct CARSTATE *actuator_car;
static legacy_u32 actuator_calls;
static legacy_s8 actuator_direction;
static legacy_u8 actuator_rate;

legacy_s16 player_steering_next_angle(const struct CARSTATE *source, legacy_s8 direction,
									  legacy_u8 rate)
{
	actuator_car = source;
	actuator_direction = direction;
	actuator_rate = rate;
	actuator_calls++;
	return TEST_NATIVE_STEERING;
}

static struct CSRPL_OPPONENT_COMMAND baseline_command(void)
{
	const struct CSRPL_OPPONENT_COMMAND command = {
		INPUT_ACCELERATE_FLAG | INPUT_SHIFT_UP_FLAG | INPUT_SHIFT_DOWN_FLAG, 0,
		TEST_INITIAL_STEERING, TEST_ROUTE_INDEX, TEST_ROUTE_POINT};
	return command;
}

static void reset_fixture(void)
{
	memset(&car, 0, sizeof(car));
	car.car_steeringAngle = TEST_INITIAL_STEERING;
	actuator_car = NULL;
	actuator_calls = 0;
	actuator_direction = INPUT_NONE;
	actuator_rate = 0;
	unit_opponent_pilot_input_override(INPUT_NONE, INPUT_NONE);
}

static void assert_provenance(const struct CSRPL_OPPONENT_COMMAND *command)
{
	assert(command->flags == 0);
	assert(command->route_index == TEST_ROUTE_INDEX && command->route_point == TEST_ROUTE_POINT);
	assert((command->input & INPUT_ACTION_BUTTON_MASK) == INPUT_ACTION_BUTTON_MASK);
	assert(car.car_steeringAngle == TEST_INITIAL_STEERING);
}

static void check_native_steering(void)
{
	static const legacy_u8 values[] = {INPUT_STEER_RIGHT_FLAG, INPUT_STEER_LEFT_FLAG, INPUT_NONE,
									   INPUT_STEERING_MASK};
	static const legacy_s8 directions[] = {INPUT_STEER_RIGHT_FLAG >> INPUT_STEERING_SHIFT,
										   INPUT_STEER_LEFT_FLAG >> INPUT_STEERING_SHIFT,
										   INPUT_NONE, INPUT_NONE};
	for (legacy_u32 index = 0; index < sizeof(values) / sizeof(values[0]); index++) {
		reset_fixture();
		struct CSRPL_OPPONENT_COMMAND command = baseline_command();
		unit_opponent_pilot_input_override(INPUT_STEERING_MASK, values[index]);
		unit_pilot_controls_apply(&car, CAR_STEERING_RATE_MAX, 0, &command);
		/* Neutral also uses the native actuator's gradual recentering. */
		assert(actuator_calls == 1 && actuator_car == &car);
		assert(actuator_direction == directions[index] && actuator_rate == CAR_STEERING_RATE_MAX);
		assert(command.steering == TEST_NATIVE_STEERING);
		assert(command.input == baseline_command().input);
		assert_provenance(&command);
		command = baseline_command();
		unit_pilot_controls_apply(&car, CAR_STEERING_RATE_DEFAULT, 0, &command);
		assert(actuator_calls == 1 && command.steering == TEST_INITIAL_STEERING);
	}
}

static void check_pedal_groups(void)
{
	static const legacy_u8 values[] = {INPUT_NONE, INPUT_ACCELERATE_FLAG, INPUT_BRAKE_FLAG,
									   INPUT_PEDAL_MASK};
	static const legacy_u8 pedals[] = {INPUT_NONE, INPUT_ACCELERATE_FLAG, INPUT_BRAKE_FLAG,
									   INPUT_BRAKE_FLAG};
	for (legacy_u32 index = 0; index < sizeof(values) / sizeof(values[0]); index++) {
		reset_fixture();
		struct CSRPL_OPPONENT_COMMAND command = baseline_command();
		command.input = INPUT_BRAKE_FLAG | INPUT_ACTION_BUTTON_MASK;
		unit_opponent_pilot_input_override(INPUT_PEDAL_MASK, values[index]);
		unit_pilot_controls_apply(&car, CAR_STEERING_RATE_DEFAULT, 0, &command);
		assert((command.input & INPUT_PEDAL_MASK) == pedals[index]);
		assert(command.steering == TEST_INITIAL_STEERING && actuator_calls == 0);
		assert_provenance(&command);
		command = baseline_command();
		unit_pilot_controls_apply(&car, CAR_STEERING_RATE_DEFAULT, 0, &command);
		assert((command.input & INPUT_PEDAL_MASK) == INPUT_ACCELERATE_FLAG);
	}
}

static void check_collision_priority(void)
{
	reset_fixture();
	struct CSRPL_OPPONENT_COMMAND command = baseline_command();
	unit_opponent_pilot_input_override(INPUT_DRIVING_MASK,
									   INPUT_STEER_LEFT_FLAG | INPUT_ACCELERATE_FLAG);
	unit_pilot_controls_apply(&car, CAR_STEERING_RATE_MAX, 1, &command);
	assert((command.input & INPUT_PEDAL_MASK) == INPUT_BRAKE_FLAG);
	assert(command.steering == TEST_INITIAL_STEERING && actuator_calls == 0);
	assert_provenance(&command);
	command = baseline_command();
	unit_pilot_controls_apply(&car, CAR_STEERING_RATE_MAX, 0, &command);
	assert(command.input == baseline_command().input && actuator_calls == 0);
	assert(command.steering == TEST_INITIAL_STEERING);
	/* Collision protection also holds with no pending pulse. */
	unit_pilot_controls_apply(&car, CAR_STEERING_RATE_DEFAULT, 1, &command);
	assert((command.input & INPUT_PEDAL_MASK) == INPUT_BRAKE_FLAG);
}

static void check_combined_override(void)
{
	reset_fixture();
	struct CSRPL_OPPONENT_COMMAND command = baseline_command();
	command.input = INPUT_BRAKE_FLAG | INPUT_ACTION_BUTTON_MASK;
	unit_opponent_pilot_input_override(INPUT_DRIVING_MASK,
									   INPUT_STEER_LEFT_FLAG | INPUT_ACCELERATE_FLAG);
	unit_pilot_controls_apply(&car, CAR_STEERING_RATE_DEFAULT, 0, &command);
	assert((command.input & INPUT_PEDAL_MASK) == INPUT_ACCELERATE_FLAG);
	assert(command.steering == TEST_NATIVE_STEERING && actuator_calls == 1);
	assert(actuator_direction == INPUT_STEER_LEFT_FLAG >> INPUT_STEERING_SHIFT);
	assert_provenance(&command);
}

static void check_invalid_masks(void)
{
	static const legacy_u8 masks[] = {INPUT_STEER_RIGHT_FLAG,	INPUT_STEER_LEFT_FLAG,
									  INPUT_ACCELERATE_FLAG,	INPUT_BRAKE_FLAG,
									  INPUT_ACTION_BUTTON_MASK, TEST_UNKNOWN_FLAG};
	for (legacy_u32 index = 0; index < sizeof(masks) / sizeof(masks[0]); index++) {
		reset_fixture();
		struct CSRPL_OPPONENT_COMMAND command = baseline_command();
		unit_opponent_pilot_input_override(masks[index], INPUT_STEER_LEFT_FLAG | INPUT_BRAKE_FLAG);
		unit_pilot_controls_apply(&car, CAR_STEERING_RATE_DEFAULT, 0, &command);
		assert(command.input == baseline_command().input);
		assert(command.steering == TEST_INITIAL_STEERING && actuator_calls == 0);
		assert_provenance(&command);
	}
	/* A partial group must not disable the other complete group. */
	struct CSRPL_OPPONENT_COMMAND command = baseline_command();
	unit_opponent_pilot_input_override(INPUT_STEER_LEFT_FLAG | INPUT_PEDAL_MASK, INPUT_BRAKE_FLAG);
	unit_pilot_controls_apply(&car, CAR_STEERING_RATE_DEFAULT, 0, &command);
	assert((command.input & INPUT_PEDAL_MASK) == INPUT_BRAKE_FLAG && actuator_calls == 0);
	command = baseline_command();
	unit_opponent_pilot_input_override(INPUT_STEERING_MASK | INPUT_BRAKE_FLAG | TEST_UNKNOWN_FLAG,
									   INPUT_STEER_RIGHT_FLAG | INPUT_BRAKE_FLAG |
										   TEST_UNKNOWN_FLAG);
	unit_pilot_controls_apply(&car, CAR_STEERING_RATE_DEFAULT, 0, &command);
	assert(command.input == baseline_command().input);
	assert(actuator_calls == 1 &&
		   actuator_direction == INPUT_STEER_RIGHT_FLAG >> INPUT_STEERING_SHIFT);
	assert_provenance(&command);
}

static void check_consumption_and_replacement(void)
{
	reset_fixture();
	for (legacy_u8 missing = 0; missing < TEST_NULL_ARGUMENT_CASES; missing++) {
		struct CSRPL_OPPONENT_COMMAND command = baseline_command();
		unit_opponent_pilot_input_override(INPUT_DRIVING_MASK,
										   INPUT_STEER_LEFT_FLAG | INPUT_BRAKE_FLAG);
		unit_pilot_controls_apply(missing ? &car : NULL, CAR_STEERING_RATE_DEFAULT, 0,
								  missing ? NULL : &command);
		unit_pilot_controls_apply(&car, CAR_STEERING_RATE_DEFAULT, 0, &command);
		assert(command.input == baseline_command().input);
		assert(command.steering == TEST_INITIAL_STEERING && actuator_calls == 0);
	}
	struct CSRPL_OPPONENT_COMMAND command = baseline_command();
	unit_opponent_pilot_input_override(INPUT_DRIVING_MASK,
									   INPUT_STEER_LEFT_FLAG | INPUT_BRAKE_FLAG);
	unit_opponent_pilot_input_override(INPUT_NONE, INPUT_NONE);
	unit_pilot_controls_apply(&car, CAR_STEERING_RATE_DEFAULT, 0, &command);
	assert(command.input == baseline_command().input && actuator_calls == 0);
	unit_opponent_pilot_input_override(INPUT_STEERING_MASK, INPUT_STEER_LEFT_FLAG);
	unit_opponent_pilot_input_override(INPUT_PEDAL_MASK, INPUT_BRAKE_FLAG);
	unit_pilot_controls_apply(&car, CAR_STEERING_RATE_DEFAULT, 0, &command);
	assert((command.input & INPUT_PEDAL_MASK) == INPUT_BRAKE_FLAG);
	assert(command.steering == TEST_INITIAL_STEERING && actuator_calls == 0);
	assert_provenance(&command);
}

legacy_int main(void)
{
	check_native_steering();
	check_pedal_groups();
	check_collision_priority();
	check_combined_override();
	check_invalid_masks();
	check_consumption_and_replacement();
	return 0;
}
