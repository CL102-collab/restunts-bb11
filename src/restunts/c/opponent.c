#include "state_internal.h"
#include "phantom_physics.h"
#include "game_input.h"
#include "trackdata_layout.h"
#include "crash_state.h"
#include "car_speed.h"
#include "track_types.h"
#include "track_objects.h"
#include "opponent.h"
#include "externs.h"
#include "extended_track.h"
#ifdef RESTUNTS_SDL3
#include "extended_geometry.h"
#endif

#define LEGACY_NULL_TRACK_VECTOR_CAPACITY 256
#define LEGACY_NULL_TRACK_TEXT_SPACE 32
#define LEGACY_NULL_TRACK_TEXT_R 82
#define LEGACY_NULL_TRACK_TEXT_U 117
#define LEGACY_NULL_TRACK_TEXT_N 110

#define OPPONENT_NORMAL_STEERING_STEP 8
#define OPPONENT_LOW_RATE_STEERING_STEP 16
#define OPPONENT_NORMAL_SPEED_STEP 1
#define OPPONENT_LOW_RATE_SPEED_STEP 2
#define OPPONENT_ROUTE_REACHED_DISTANCE 200
#define OPPONENT_PLAYER_HEIGHT_RANGE 90
#define OPPONENT_PLAYER_LATERAL_RANGE 180
#define OPPONENT_PLAYER_FORWARD_RANGE 600
#define OPPONENT_PLAYER_REAR_RANGE (-180)
#define OPPONENT_PASSING_FORWARD_LIMIT (-78)
#define OPPONENT_ROUTE_ANGLE_LIMIT ANGLE_QUARTER_TURN
#define OPPONENT_STEERING_TARGET_LIMIT 65

#define OPPONENT_EMERGENCY_TARGET_SPEED 16384U
#define OPPONENT_ACCELERATE_MARGIN 256U
#define OPPONENT_BRAKE_MARGIN 768U
#define OPPONENT_SPEED_TO_TARGET_SHIFT LEGACY_BYTE_BITS
#define OPPONENT_COAST_SPEED_SHIFT 9U

#define STEERING_RESPONSE_SPEED_SHIFT 10U
#define STEERING_RESPONSE_INDEX_MASK 252U
#define STEERING_LOW_RATE_RESPONSE_LIMIT 160
#define STEERING_NORMAL_RESPONSE_LIMIT 80
#define STEERING_ANGLE_LIMIT 240
#define STEERING_CENTER_DEADZONE 8
#define STEERING_AWAY_FROM_CENTER_THRESHOLD 1
#define STEERING_AWAY_RESPONSE_SHIFT 2U
#define STEERING_CENTERING_SAMPLE_OFFSET (INPUT_STEER_RIGHT_FLAG >> INPUT_STEERING_SHIFT)
#define STEERING_CENTERING_LIMIT_SHIFT 1U

#define OPPONENT_AVERAGE_SHIFT 1U

#define TRACK_ROUTE_SUBTYPE_MASK 15U
#define TRACK_ROUTE_REVERSED_FLAG 16U
#define TRACK_ROUTE_FLAG_OFFSET 18U
#define TRACK_ROUTE_VECTORS_PER_SEGMENT 2U
#define TRACK_ROUTE_SECOND_VECTOR_OFFSET 1U

enum TRACK_ROUTE_RESULT_INDEX {
	TRACK_ROUTE_RESULT_CENTER_INDEX = 0,
	TRACK_ROUTE_RESULT_FIRST_POINT_INDEX = 1,
	TRACK_ROUTE_RESULT_SECOND_POINT_INDEX = 2
};

#define TRACK_ROUTE_LAST_INDEX_OFFSET 1U
#define OPPONENT_ROUTE_POINT_NOT_LAST 0
#define OPPONENT_ROUTE_LIST_END 0
#define OPPONENT_ROUTE_INDEX_FIRST 0
#define OPPONENT_ROUTE_INDEX_STEP 1

enum OPPONENT_ROUTE_FORCE_STATE { OPPONENT_ROUTE_NOT_FORCED = 0, OPPONENT_ROUTE_FORCED = 1 };

#define OPPONENT_LAP_NONE 0
#define OPPONENT_LAP_STEP 1

/*
 * Track object zero has no info record.  In the original executable its null
 * near pointer aliases the fixed DGROUP prefix.  The camera pointer found
 * there reaches an unrelated 256-vector data window; the tail of that window
 * is zero-filled.  Preserve the exact legacy alias explicitly so changing the
 * C-only data layout cannot change route physics.
 */
static struct VECTOR legacy_null_track_vectors[LEGACY_NULL_TRACK_VECTOR_CAPACITY] = {
	{0, 0, 0},
	{-8960, -137, -1},
	{-1, -1, -1},
	{-1, -1, -1},
	{-1, -1, -1},
	{255, 0, 0},
	{0, 0, 0},
	{0, 0, 0},
	{0, 0, 0},
	{0, 13056, -13108},
	{27955, -27978, 27977},
	{-27978, 27977, -27978},
	{73, 0, -27648},
	{-27568, -27056, -26543},
	{28754, 12643, 53},
	{25715, 24941, 28265},
	{8448, 24944, 108},
	{28019, 30063, 29440},
	{28004, 26977, 110},
	{28019, 30063, 27904},
	{28525, 117, 8192},
	{768, 0, 17152},
	{28783, 29305, 26473},
	{29800, 10272, 10563},
	{21792, 27758, 28009},
	{29801, 25701, 21280},
	{26223, 30580, 29281},
	{8293, 28233, 11875},
	{12576, 14393, 11321},
	{14641, 12345, 8238},
	{16672, 27756, 29216},
	{26473, 29800, 8307},
	{25970, 25971, 30322},
	{25701, 46, 0},
	{0, 19712, 18243},
	{8257, 18775, 17486},
	{22351, 30464, 28265},
	{28516, 25719, 26213},
	{11552, 20256, 21589},
	{20256, 8262, 20306},
	{8279, 16724, 19522},
	{8261, 20563, 17217},
	{3397, 11776, 22096},
	{83, 22574, 21334},
	{11776, 21334, 72},
	{20526, 21317, 11776},
	{21317, 72, 19456},
	{20820, 22100, 23380},
	{24660, 25940, 84},
	{513, 1027, 1541},
	{2055, 2569, 3083},
	{3597, 11791, 22096},
	{83, 20053, 19526},
	{20553, 11776, 22104},
	{83, 20526, 21317},
	{21760, 17998, 18764},
	{80, 17710, 18515},
	{8448, 18253, 65},
	{0, 0, 0},
	{0, 0, 0},
	{0, 0, -26624},
	{22789, 15367, 20028},
	{21325, 15943, 62},
	{20992, 12342, 12336},
	{2573, 8237, 29811},
	{25441, 8299, 30319},
	{29285, 27750, 30575},
	{2573, 768, 20992},
	{12342, 13104, 2573},
	{8237, 28265, 25972},
	{25959, 8306, 26980},
	{26998, 25956, 25120},
	{8313, 3376, 10},
	{9, 13906, 12336},
	{3385, 11530, 28192},
	{29807, 25888, 28526},
	{26485, 8296, 28787},
	{25441, 8293, 28518},
	{8306, 28261, 26998},
	{28530, 28014, 28261},
	{3444, 10, 252},
	{2573, -256, 29184},
	{28277, 29741, 28009},
	{8293, 29285, 28530},
	{8306, 512, 20992},
	{12342, 12848, 2573},
	{8237, 27750, 24943},
	{26996, 26478, 28704},
	{26991, 29806, 28192},
	{29807, 27680, 24943},
	{25956, 3428, 10},
	{1, 13906, 12336},
	{3377, 11530, 28192},
	{27765, 8300, 28528},
	{28265, 25972, 8306},
	{29537, 26995, 28263},
	{25965, 29806, 2573},
	{2560, 2560, 25153},
	{28526, 28018, 27745},
	{28704, 28530, 29287},
	{28001, 29728, 29285},
	{26989, 24942, 26996},
	{28271, 10, 0},
	{0, 0, 0},
	{0, 0, 0},
	{0, 0, -256}};

/* Bytes 8-13 of the legacy DGROUP prefix read as "MS Run". */
static struct TRKOBJINFO legacy_null_track_info = {0,
												   0,
												   0,
												   0,
												   0,
												   0,
												   0,
												   legacy_null_track_vectors,
												   LEGACY_NULL_TRACK_TEXT_SPACE,
												   LEGACY_NULL_TRACK_TEXT_R,
												   LEGACY_NULL_TRACK_TEXT_U,
												   LEGACY_NULL_TRACK_TEXT_N};

extern struct VECTOR *headless_track_vector_from_legacy_offset(legacy_u16 offset);

struct VECTOR *track_vector_from_legacy_offset(legacy_u16 offset)
{
	return headless_track_vector_from_legacy_offset(offset);
}

static legacy_s16 opponent_route_word(legacy_s16 index)
{
	legacy_u16 offset = LEGACY_U16_WRAP_MUL(index, LEGACY_WORD_BYTES);
	return LEGACY_READ_S16_LE((const legacy_u8 far *)opponent_route_track_indices + offset);
}

static void opponent_advance_route(void)
{
	legacy_u8 route_point = (legacy_u8)state.opponentstate.car_route_point_index;
	state.opponentstate.car_route_point_index = LEGACY_S8_WRAP_ADD(route_point, ROUTE_POINT_STEP);
	if (get_track_route_point(opponent_route_word(state.opponentstate.car_route_index),
							  &state.opponentstate.car_route_target, route_point,
							  &state.game_opponent_target_speed) == OPPONENT_ROUTE_POINT_NOT_LAST) {
		return;
	}
	state.opponentstate.car_route_index =
		LEGACY_S16_WRAP_ADD(state.opponentstate.car_route_index, OPPONENT_ROUTE_INDEX_STEP);
	if (opponent_route_word(state.opponentstate.car_route_index) == OPPONENT_ROUTE_LIST_END) {
		state.opponentstate.car_lap_count =
			LEGACY_S8_WRAP_ADD(state.opponentstate.car_lap_count, OPPONENT_LAP_STEP);
		state.opponentstate.car_route_index = OPPONENT_ROUTE_INDEX_FIRST;
	}
	state.opponentstate.car_route_point_index = ROUTE_POINT_FIRST;
}

static legacy_s16 opponent_average(legacy_s16 first, legacy_s16 second)
{
	if (extended_track_active()) {
		return LEGACY_S16_WRAP_ADD(
			first, LEGACY_S16_SAR(LEGACY_S16_WRAP_SUB(second, first), OPPONENT_AVERAGE_SHIFT));
	}
	legacy_s32 sum = (legacy_s32)first + (legacy_s32)second;
	return LEGACY_S16_FROM_BITS((legacy_u16)LEGACY_S32_SAR(sum, OPPONENT_AVERAGE_SHIFT));
}

/* Positions and rotation are sampled before steering and kept until the
 * physics calls update the cars at the end of this tick. */
struct OPPONENT_GUIDANCE {
	struct VECTOR opponent;
	struct VECTOR player;
	struct MATRIX *rotation;
	legacy_u8 forced_route;
};

static void opponent_check_route_distance(struct OPPONENT_GUIDANCE *guidance)
{
	struct VECTOR route_target = state.opponentstate.car_route_target;
	legacy_s16 route_distance;
	struct VECTOR relative;
	if (route_target.y != ROUTE_POINT_HEIGHT_UNSPECIFIED) {
		relative.x = LEGACY_S16_WRAP_SUB(route_target.x, guidance->opponent.x);
		relative.y = LEGACY_S16_WRAP_SUB(route_target.y, guidance->opponent.y);
		relative.z = LEGACY_S16_WRAP_SUB(route_target.z, guidance->opponent.z);
		route_distance = (legacy_s16)polarRadius3D(&relative);
	} else {
		route_distance =
			(legacy_s16)polarRadius2D(LEGACY_S16_WRAP_SUB(route_target.x, guidance->opponent.x),
									  LEGACY_S16_WRAP_SUB(route_target.z, guidance->opponent.z));
	}
	if (route_distance < OPPONENT_ROUTE_REACHED_DISTANCE) {
		opponent_advance_route();
	}
}

static void opponent_passing_target(struct OPPONENT_GUIDANCE *guidance, struct VECTOR *route_target)
{
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		legacy_s32 dx = state.playerstate.car_position.lx - state.opponentstate.car_position.lx;
		legacy_s32 dz = state.playerstate.car_position.lz - state.opponentstate.car_position.lz;
		/* Cars a 65536-unit page apart must not appear in a nearby passing lane. */
		if (dx > 4096L * 64L || dx < -4096L * 64L || dz > 4096L * 64L || dz < -4096L * 64L) {
			return;
		}
	}
#endif
	const struct VECTOR *edge;
	struct VECTOR relative;
	struct VECTOR transformed;
	if (state.game_inputmode != GAME_INPUT_MODE_INTRO) {
		relative.x = LEGACY_S16_WRAP_SUB(guidance->player.x, guidance->opponent.x);
		relative.y = LEGACY_S16_WRAP_SUB(guidance->player.y, guidance->opponent.y);
		relative.z = LEGACY_S16_WRAP_SUB(guidance->player.z, guidance->opponent.z);
		mat_mul_vector(&relative, guidance->rotation, &transformed);
		legacy_s16 player_forward = transformed.z;
		legacy_s16 absolute_value = transformed.x;
		if (absolute_value < 0) {
			absolute_value = LEGACY_S16_WRAP_NEGATE(absolute_value);
		}
		if (transformed.y <= OPPONENT_PLAYER_HEIGHT_RANGE &&
			absolute_value <= OPPONENT_PLAYER_LATERAL_RANGE &&
			transformed.z <= OPPONENT_PLAYER_FORWARD_RANGE &&
			transformed.z >= OPPONENT_PLAYER_REAR_RANGE) {
			relative.x =
				LEGACY_S16_WRAP_SUB(guidance->player.x, state.opponentstate.car_route_target.x);
			relative.y = state.opponentstate.car_route_target.y == ROUTE_POINT_HEIGHT_UNSPECIFIED
							 ? 0
							 : LEGACY_S16_WRAP_SUB(guidance->player.y,
												   state.opponentstate.car_route_target.y);
			relative.z =
				LEGACY_S16_WRAP_SUB(guidance->player.z, state.opponentstate.car_route_target.z);
			mat_mul_vector(&relative, guidance->rotation, &transformed);
			legacy_s8 indicator;
			if (transformed.x < 0) {
				edge = &state.opponentstate.car_route_second_edge;
				indicator = ROUTE_INDICATOR_RIGHT;
			} else {
				edge = &state.opponentstate.car_route_first_edge;
				indicator = ROUTE_INDICATOR_LEFT;
			}
			route_target->x = opponent_average(state.opponentstate.car_route_target.x, edge->x);
			route_target->y =
				state.opponentstate.car_route_target.y == ROUTE_POINT_HEIGHT_UNSPECIFIED
					? ROUTE_POINT_HEIGHT_UNSPECIFIED
					: opponent_average(state.opponentstate.car_route_target.y, edge->y);
			route_target->z = opponent_average(state.opponentstate.car_route_target.z, edge->z);
			if (player_forward > OPPONENT_PASSING_FORWARD_LIMIT &&
				state.playerstate.car_crashBmpFlag == CRASH_EVENT_NONE) {
				state.game_opponent_route_indicator = indicator;
			}
		}
	}
}

static legacy_s16 opponent_steering_target(struct OPPONENT_GUIDANCE *guidance)
{
	struct VECTOR transformed;
	struct VECTOR route_target;
	struct VECTOR relative;
	for (;;) {
		route_target = state.opponentstate.car_route_target;
		opponent_passing_target(guidance, &route_target);

		relative.x = LEGACY_S16_WRAP_SUB(route_target.x, guidance->opponent.x);
		relative.y = route_target.y == ROUTE_POINT_HEIGHT_UNSPECIFIED
						 ? 0
						 : LEGACY_S16_WRAP_SUB(route_target.y, guidance->opponent.y);
		relative.z = LEGACY_S16_WRAP_SUB(route_target.z, guidance->opponent.z);
		mat_mul_vector(&relative, guidance->rotation, &transformed);
		legacy_s16 steering_target = (legacy_s16)polarAngle(transformed.x, transformed.z);
		if (state.opponentstate.car_slidingFlag == CAR_SLIDING_INACTIVE) {
			legacy_s16 absolute_value = steering_target;
			if (absolute_value < 0) {
				absolute_value = LEGACY_S16_WRAP_NEGATE(absolute_value);
			}
			if (absolute_value > OPPONENT_ROUTE_ANGLE_LIMIT) {
				opponent_advance_route();
			}
		}
		if (steering_target > OPPONENT_STEERING_TARGET_LIMIT) {
			if (guidance->forced_route == OPPONENT_ROUTE_NOT_FORCED) {
				guidance->forced_route = OPPONENT_ROUTE_FORCED;
				opponent_advance_route();
				continue;
			}
			steering_target = OPPONENT_STEERING_TARGET_LIMIT;
		} else if (steering_target < -OPPONENT_STEERING_TARGET_LIMIT) {
			if (guidance->forced_route == OPPONENT_ROUTE_NOT_FORCED) {
				guidance->forced_route = OPPONENT_ROUTE_FORCED;
				opponent_advance_route();
				continue;
			}
			steering_target = -OPPONENT_STEERING_TARGET_LIMIT;
		}
		return steering_target;
	}
}

static void opponent_apply_steering(legacy_s16 steering_target, legacy_s16 steering_step)
{
	if (state.opponentstate.car_sumSurfFrontWheels == CAR_WHEEL_CONTACT_NONE) {
		steering_target = CAR_STEERING_CENTERED;
	}
	legacy_s16 steering_delta =
		LEGACY_S16_WRAP_SUB(steering_target, state.opponentstate.car_steeringAngle);
	legacy_s16 absolute_value = steering_delta;
	if (absolute_value < 0) {
		absolute_value = LEGACY_S16_WRAP_NEGATE(absolute_value);
	}
	if (absolute_value > steering_step) {
		if (steering_target < state.opponentstate.car_steeringAngle) {
			state.opponentstate.car_steeringAngle =
				LEGACY_S16_WRAP_SUB(state.opponentstate.car_steeringAngle, steering_step);
		} else {
			state.opponentstate.car_steeringAngle =
				LEGACY_S16_WRAP_ADD(state.opponentstate.car_steeringAngle, steering_step);
		}
	} else {
		state.opponentstate.car_steeringAngle = steering_target;
	}
}

static legacy_u8 opponent_speed_input(legacy_s16 speed_step)
{
	legacy_u8 input = INPUT_NONE;
	if (state.opponentstate.car_sumSurfRearWheels != CAR_WHEEL_CONTACT_NONE) {
		if (state.opponentstate.car_crashBmpFlag != CRASH_EVENT_NONE) {
			input = INPUT_BRAKE_FLAG;
		} else if (state.opponentstate.car_velocity_heading_offset != 0) {
			legacy_u16 speed_threshold = LEGACY_U16_SHL(speed_step, OPPONENT_COAST_SPEED_SHIFT);
			if (speed_threshold > state.opponentstate.car_actual_speed) {
				state.opponentstate.car_actual_speed = CAR_SPEED_STOPPED;
				state.opponentstate.car_velocity_heading_offset = 0;
			} else {
				state.opponentstate.car_actual_speed =
					LEGACY_U16_WRAP_SUB(state.opponentstate.car_actual_speed, speed_threshold);
			}
		} else if (state.opponentstate.car_demandedGrip <=
				   state.opponentstate.car_surfacegrip_sum) {
			legacy_u16 target_speed =
				state.game_inputmode == GAME_INPUT_MODE_INTRO
					? OPPONENT_EMERGENCY_TARGET_SPEED
					: LEGACY_U16_SHL((legacy_u8)state.game_opponent_target_speed,
									 OPPONENT_SPEED_TO_TARGET_SHIFT);
			if (LEGACY_U16_WRAP_SUB(target_speed, OPPONENT_ACCELERATE_MARGIN) >
				state.opponentstate.car_rev_speed) {
				input = INPUT_ACCELERATE_FLAG;
			} else if (LEGACY_U16_WRAP_ADD(target_speed, OPPONENT_BRAKE_MARGIN) <
					   state.opponentstate.car_rev_speed) {
				input = INPUT_BRAKE_FLAG;
			}
		} else {
			input = INPUT_BRAKE_FLAG;
		}
	}
	return input;
}

static void opponent_heading_error(void)
{
	struct VECTOR transformed;
	struct MATRIX *rotation;
	struct VECTOR relative;
	if (state.opponentstate.car_crashBmpFlag == CRASH_EVENT_NONE) {
		relative.x =
			LEGACY_S16_WRAP_SUB(state.opponentstate.car_route_target.x,
								position_to_word((legacy_s32)state.opponentstate.car_position.lx));
		relative.y =
			LEGACY_S16_WRAP_SUB(state.opponentstate.car_route_target.y,
								position_to_word((legacy_s32)state.opponentstate.car_position.ly));
		relative.z =
			LEGACY_S16_WRAP_SUB(state.opponentstate.car_route_target.z,
								position_to_word((legacy_s32)state.opponentstate.car_position.lz));
		rotation = mat_rot_zxy(state.opponentstate.car_rotate.z, state.opponentstate.car_rotate.y,
							   state.opponentstate.car_rotate.x, MATRIX_ROTATION_ORDER_YXZ);
		mat_mul_vector(&relative, rotation, &transformed);
		state.opponentstate.car_route_heading_error = LEGACY_S16_FROM_BITS(
			(legacy_u16)polarAngle(LEGACY_S16_WRAP_NEGATE(transformed.x), transformed.z) &
			ANGLE_MASK);
	}
}

static void opponent_check_finish(void)
{
	if (state.opponentstate.car_lap_count != OPPONENT_LAP_NONE) {
		legacy_s16 finish_distance = multiply_and_scale(
			cos_fast(track_angle),
			LEGACY_S16_WRAP_SUB(track_row_centers[start_finish_row],
								position_to_word((legacy_s32)state.opponentstate.car_position.lz)));
		finish_distance = LEGACY_S16_WRAP_ADD(
			finish_distance,
			multiply_and_scale(
				sin_fast(track_angle),
				LEGACY_S16_WRAP_SUB(
					track_column_centers[start_finish_column],
					position_to_word((legacy_s32)state.opponentstate.car_position.lx))));
		if (finish_distance < 0) {
			update_crash_state(CRASH_EVENT_FINISH, OPPONENT_CAR_INDEX);
		}
	}
}

void update_opponent_tick(void)
{
	legacy_s16 speed_step;
	legacy_s16 steering_step;
	if (framespersec == GAME_FRAME_RATE_NORMAL) {
		steering_step = OPPONENT_NORMAL_STEERING_STEP;
		speed_step = OPPONENT_NORMAL_SPEED_STEP;
	} else {
		steering_step = OPPONENT_LOW_RATE_STEERING_STEP;
		speed_step = OPPONENT_LOW_RATE_SPEED_STEP;
	}
	struct OPPONENT_GUIDANCE guidance;
	guidance.forced_route = state.opponentstate.car_velocity_heading_offset != 0 ||
							state.game_inputmode == GAME_INPUT_MODE_INTRO;
	guidance.opponent.x = position_to_word((legacy_s32)state.opponentstate.car_position.lx);
	guidance.opponent.y = position_to_word((legacy_s32)state.opponentstate.car_position.ly);
	guidance.opponent.z = position_to_word((legacy_s32)state.opponentstate.car_position.lz);
	guidance.player.x = position_to_word((legacy_s32)state.playerstate.car_position.lx);
	guidance.player.y = position_to_word((legacy_s32)state.playerstate.car_position.ly);
	guidance.player.z = position_to_word((legacy_s32)state.playerstate.car_position.lz);
	state.opponentstate.car_sound_flags = CAR_SOUND_NONE;
	state.game_opponent_route_indicator = ROUTE_INDICATOR_NONE;
	guidance.rotation =
		mat_rot_zxy(state.opponentstate.car_rotate.z, state.opponentstate.car_rotate.y,
					state.opponentstate.car_rotate.x, MATRIX_ROTATION_ORDER_YXZ);
	state.opponentstate.car_sound_flags = CAR_SOUND_ENGINE_ACTIVE_FLAG;
	if (state.opponentstate.car_crashBmpFlag != CRASH_EVENT_NONE) {
		if (state.opponentstate.car_actual_speed == CAR_SPEED_STOPPED) {
			state.opponentstate.car_sound_flags = CAR_SOUND_NONE;
		}
	} else {
		opponent_check_route_distance(&guidance);
		legacy_s16 steering_target = opponent_steering_target(&guidance);
		opponent_apply_steering(steering_target, steering_step);
	}
	legacy_u8 input = opponent_speed_input(speed_step);
	update_car_speed(input, OPPONENT_CAR_INDEX, &state.opponentstate, &simd_opponent);
	update_grip(&state.opponentstate, &simd_opponent, GRIP_BEHAVIOR_OPPONENT);
	update_player_state(&state.opponentstate, &simd_opponent, &state.playerstate, &simd_player,
						OPPONENT_CAR_INDEX);
	opponent_heading_error();
	opponent_check_finish();
}

static legacy_s16 player_steering_response(const struct CARSTATE *carstate,
										   legacy_s16 steering_angle, legacy_s16 response,
										   legacy_u8 speed_index, const legacy_s8 *response_table)
{
	/* Turning farther from center gets the original fourfold response. */
	if ((response > 0 && steering_angle < -STEERING_AWAY_FROM_CENTER_THRESHOLD) ||
		(response < 0 && steering_angle > STEERING_AWAY_FROM_CENTER_THRESHOLD)) {
		response = LEGACY_S16_SHL(response, STEERING_AWAY_RESPONSE_SHIFT);
	}

	/* With no steering input, bring a moving car back toward center. */
	if (response == 0 && carstate->car_actual_speed != CAR_SPEED_STOPPED &&
		steering_angle != CAR_STEERING_CENTERED) {
		legacy_s16 centering_limit = LEGACY_S16_SHL(
			(legacy_s16)response_table[speed_index + STEERING_CENTERING_SAMPLE_OFFSET],
			STEERING_CENTERING_LIMIT_SHIFT);
		if (steering_angle < 0) {
			if (LEGACY_S16_WRAP_NEGATE(steering_angle) > centering_limit) {
				response = centering_limit;
			} else {
				response = LEGACY_S16_WRAP_NEGATE(steering_angle);
			}
		} else {
			if (steering_angle > centering_limit) {
				response = LEGACY_S16_WRAP_NEGATE(centering_limit);
			} else {
				response = LEGACY_S16_WRAP_NEGATE(steering_angle);
			}
		}
	}

	return response;
}

static void update_player_steering(struct CARSTATE *carstate, legacy_s8 steering_input,
								   legacy_u32 fraction20)
{
	legacy_s8 *response_table = steerWhlRespTable_ptr;
	legacy_s16 steering_angle = carstate->car_steeringAngle;
	legacy_u8 speed_index =
		(legacy_u8)((carstate->car_actual_speed >> STEERING_RESPONSE_SPEED_SHIFT) &
					STEERING_RESPONSE_INDEX_MASK);
	legacy_s16 response_index =
		LEGACY_S16_WRAP_ADD((legacy_s16)speed_index, (legacy_s16)steering_input);
	legacy_s16 response = response_table[response_index];

	response =
		player_steering_response(carstate, steering_angle, response, speed_index, response_table);

	if (framespersec == GAME_FRAME_RATE_LOW) {
		if (response > STEERING_LOW_RATE_RESPONSE_LIMIT) {
			response = STEERING_LOW_RATE_RESPONSE_LIMIT;
		}
		if (response < -STEERING_LOW_RATE_RESPONSE_LIMIT) {
			response = -STEERING_LOW_RATE_RESPONSE_LIMIT;
		}
	} else {
		if (response > STEERING_NORMAL_RESPONSE_LIMIT) {
			response = STEERING_NORMAL_RESPONSE_LIMIT;
		}
		if (response < -STEERING_NORMAL_RESPONSE_LIMIT) {
			response = -STEERING_NORMAL_RESPONSE_LIMIT;
		}
	}

	legacy_u32 tick_fraction =
		framespersec == GAME_FRAME_RATE_LOW ? PHANTOM_PHYSICS_LOW_RATE_TICK : PHANTOM_PHYSICS_ONE;
	if (fraction20 != tick_fraction) {
		response =
			(legacy_s16)((legacy_s32)response * (legacy_s32)fraction20 / (legacy_s32)tick_fraction);
	}

	steering_angle = LEGACY_S16_WRAP_ADD(steering_angle, response);
	if (steering_angle > STEERING_ANGLE_LIMIT) {
		steering_angle = STEERING_ANGLE_LIMIT;
	}
	if (steering_angle < -STEERING_ANGLE_LIMIT) {
		steering_angle = -STEERING_ANGLE_LIMIT;
	}

	if (response_table[response_index] == 0 && steering_angle > -STEERING_CENTER_DEADZONE &&
		steering_angle < STEERING_CENTER_DEADZONE) {
		steering_angle = CAR_STEERING_CENTERED;
	}

	carstate->car_steeringAngle = steering_angle;
}

void update_player_steering_input(legacy_s8 steering_input)
{
	legacy_u32 tick_fraction =
		framespersec == GAME_FRAME_RATE_LOW ? PHANTOM_PHYSICS_LOW_RATE_TICK : PHANTOM_PHYSICS_ONE;
	update_player_steering(&state.playerstate, steering_input, tick_fraction);
}

void update_player_steering_fraction(struct CARSTATE *carstate, legacy_s8 steering_input,
									 legacy_u32 fraction20)
{
	if (fraction20 != 0) {
		update_player_steering(carstate, steering_input, fraction20);
	}
}

static legacy_s16 route_average(legacy_s16 first, legacy_s16 second)
{
	if (extended_track_active()) {
		return LEGACY_S16_WRAP_ADD(
			first, LEGACY_S16_SAR(LEGACY_S16_WRAP_SUB(second, first), OPPONENT_AVERAGE_SHIFT));
	}
	legacy_s32 sum = LEGACY_S32_WRAP_ADD(first, second);
	return LEGACY_S16_FROM_BITS((legacy_u16)LEGACY_S32_SAR(sum, OPPONENT_AVERAGE_SHIFT));
}

static legacy_u8 opponent_speed_at(legacy_u16 index)
{
	if (index < OPPONENT_SPEED_COUNT) {
		return oppnentSped[index];
	}

	/*
	 * The assembly adds two zero-extended bytes as a 16-bit address; it does
	 * not wrap the sum to a byte.  The built-in surface -1 track objects use
	 * offset 268, in the original zero-filled rectangle workspace after the
	 * speed table.  Null track-info aliases similarly reach an unused zero
	 * byte in the path workspace.  Keep those reads deterministic instead of
	 * indexing beyond the C array.
	 */
	return 0;
}

static void rotate_route_points(struct VECTOR *first_point, struct VECTOR *second_point,
								legacy_s16 orientation)
{
	legacy_s16 base_position;

	if (orientation == ANGLE_QUARTER_TURN) {
		base_position = first_point->x;
		first_point->x = first_point->z;
		first_point->z = LEGACY_S16_WRAP_NEGATE(base_position);
		base_position = second_point->x;
		second_point->x = second_point->z;
		second_point->z = LEGACY_S16_WRAP_NEGATE(base_position);
	} else if (orientation == ANGLE_HALF_TURN) {
		first_point->x = LEGACY_S16_WRAP_NEGATE(first_point->x);
		first_point->z = LEGACY_S16_WRAP_NEGATE(first_point->z);
		second_point->x = LEGACY_S16_WRAP_NEGATE(second_point->x);
		second_point->z = LEGACY_S16_WRAP_NEGATE(second_point->z);
	} else if (orientation == ANGLE_THREE_QUARTER_TURN) {
		base_position = first_point->x;
		first_point->x = LEGACY_S16_WRAP_NEGATE(first_point->z);
		first_point->z = base_position;
		base_position = second_point->x;
		second_point->x = LEGACY_S16_WRAP_NEGATE(second_point->z);
		second_point->z = base_position;
	}
}

static void store_route_points(legacy_s16 track_index, struct TRACKOBJECT *track_object,
							   struct VECTOR *first_point, struct VECTOR *second_point,
							   struct VECTOR *output, legacy_u8 has_opponent_path)
{
	legacy_u8 column = (legacy_u8)track_route_columns[track_index];
	legacy_u8 row = (legacy_u8)track_route_rows[track_index];
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		const struct CSTRK_OBJECT *object = extended_track_object_for_id(
			column, row, (legacy_u8)track_route_element_ids[track_index]);
		if (object) {
			struct EXTENDED_GEOMETRY_BASIS basis;
			struct VECTOR origin, transformed;
			extended_geometry_object_basis(object, &basis);
			extended_geometry_object_origin(extended_track_model(), object, &origin);
			if (first_point->y == ROUTE_POINT_HEIGHT_UNSPECIFIED) {
				first_point->y = 0;
			}
			if (second_point->y == ROUTE_POINT_HEIGHT_UNSPECIFIED) {
				second_point->y = 0;
			}
			extended_geometry_forward(&basis, first_point, &transformed);
			first_point->x = LEGACY_S16_WRAP_ADD(origin.x, transformed.x);
			first_point->y = LEGACY_S16_WRAP_ADD(origin.y, transformed.y);
			first_point->z = LEGACY_S16_WRAP_ADD(origin.z, transformed.z);
			extended_geometry_forward(&basis, second_point, &transformed);
			second_point->x = LEGACY_S16_WRAP_ADD(origin.x, transformed.x);
			second_point->y = LEGACY_S16_WRAP_ADD(origin.y, transformed.y);
			second_point->z = LEGACY_S16_WRAP_ADD(origin.z, transformed.z);
			goto store_points;
		}
	}
#endif
	if (first_point->y != ROUTE_POINT_HEIGHT_UNSPECIFIED &&
		track_terrain_map[terrainrows[row] + column] == TERRAIN_RAISED_TILE) {
		first_point->y =
			LEGACY_S16_WRAP_ADD(first_point->y, hillHeightConsts[TERRAIN_RAISED_HEIGHT_INDEX]);
		second_point->y =
			LEGACY_S16_WRAP_ADD(second_point->y, hillHeightConsts[TERRAIN_RAISED_HEIGHT_INDEX]);
	}

	legacy_s16 base_position = track_object_base_z(track_object, row);
	first_point->z = LEGACY_S16_WRAP_ADD(first_point->z, base_position);
	second_point->z = LEGACY_S16_WRAP_ADD(second_point->z, base_position);

	base_position = track_object_base_x(track_object, column);
	first_point->x = LEGACY_S16_WRAP_ADD(first_point->x, base_position);
	second_point->x = LEGACY_S16_WRAP_ADD(second_point->x, base_position);

#ifdef RESTUNTS_SDL3
store_points:
#endif
	output[TRACK_ROUTE_RESULT_CENTER_INDEX].x = route_average(first_point->x, second_point->x);
	if (first_point->y == ROUTE_POINT_HEIGHT_UNSPECIFIED) {
		output[TRACK_ROUTE_RESULT_CENTER_INDEX].y = ROUTE_POINT_HEIGHT_UNSPECIFIED;
	} else {
		output[TRACK_ROUTE_RESULT_CENTER_INDEX].y = route_average(first_point->y, second_point->y);
	}
	output[TRACK_ROUTE_RESULT_CENTER_INDEX].z = route_average(first_point->z, second_point->z);
	output[TRACK_ROUTE_RESULT_FIRST_POINT_INDEX] = *first_point;
	output[TRACK_ROUTE_RESULT_SECOND_POINT_INDEX] = *second_point;
	/* This flag follows three native VECTORs in CARSTATE, not a file record. */
	legacy_s16 native_flag = has_opponent_path;
	((legacy_u8 *)output)[TRACK_ROUTE_FLAG_OFFSET] = ((const legacy_u8 *)&native_flag)[0];
	((legacy_u8 *)output)[TRACK_ROUTE_FLAG_OFFSET + 1U] = ((const legacy_u8 *)&native_flag)[1];
}

static void store_route_speed(const struct TRKOBJINFO *track_info,
							  const struct TRACKOBJECT *track_object, legacy_s8 *optional_speed)
{
	if (optional_speed != 0) {
		legacy_u16 speed_index = (legacy_u8)track_info->opponent_speed_code;
		speed_index = LEGACY_U16_WRAP_ADD(speed_index, (legacy_u8)track_object->ss_surfaceType);
		*optional_speed = LEGACY_S8_FROM_BITS(opponent_speed_at(speed_index));
	}
}

legacy_s16 get_track_route_point(legacy_s16 track_index_arg, struct VECTOR *output,
								 legacy_s16 route_index_arg, legacy_s8 *optional_speed)
{
	legacy_s16 track_index = (legacy_s16)track_index_arg;
	legacy_u8 tile_element = (legacy_u8)track_route_element_ids[track_index];
	legacy_u8 track_subtype =
		(legacy_u8)track_route_traversal_flags[track_index] & TRACK_ROUTE_SUBTYPE_MASK;
	legacy_u8 connection_status =
		(legacy_u8)track_route_traversal_flags[track_index] & TRACK_ROUTE_REVERSED_FLAG;
	struct TRACKOBJECT *track_object = &trkObjectList[tile_element];
	struct TRKOBJINFO *track_info;
	if (track_object->ss_trkObjInfoPtr == 0) {
		track_info = &legacy_null_track_info;
	} else {
		track_info = &track_object->ss_trkObjInfoPtr[track_subtype];
	}
	legacy_u8 route_point_count = (legacy_u8)track_info->route_point_count;
	legacy_u8 route_index = (legacy_u8)route_index_arg;

	legacy_u8 vector_index;
	if (connection_status == 0) {
		vector_index = LEGACY_U8_WRAP_MUL(route_index, TRACK_ROUTE_VECTORS_PER_SEGMENT);
	} else {
		vector_index = LEGACY_U8_WRAP_SUB(route_point_count, route_index);
		vector_index = LEGACY_U8_WRAP_MUL(vector_index, TRACK_ROUTE_VECTORS_PER_SEGMENT);
		vector_index = LEGACY_U8_WRAP_SUB(vector_index, TRACK_ROUTE_VECTORS_PER_SEGMENT);
	}

	store_route_speed(track_info, track_object, optional_speed);
#ifdef RESTUNTS_SDL3
	if (extended_track_active() && optional_speed) {
		const struct CSTRK_OBJECT *placed =
			extended_track_object_for_id((legacy_u8)track_route_columns[track_index],
										 (legacy_u8)track_route_rows[track_index], tile_element);
		if (placed) {
			*optional_speed = LEGACY_S8_FROM_BITS(
				opponent_speed_at((legacy_u8)track_info->opponent_speed_code + placed->surface));
		}
	}
#endif

	legacy_u16 packed_opponent_offset =
		(legacy_u16)((legacy_u8)track_info->reverse_path_offset_low |
					 LEGACY_U16_SHL((legacy_u8)track_info->reverse_path_offset_high,
									LEGACY_BYTE_BITS));
	legacy_u8 has_opponent_path = packed_opponent_offset != 0;
	struct VECTOR *route_vectors;
	if (connection_status != 0 && has_opponent_path != 0) {
		route_vectors = track_vector_from_legacy_offset(packed_opponent_offset);
	} else {
		route_vectors = track_info->route_vectors;
	}

	struct VECTOR second_point;
	struct VECTOR first_point;
	if (connection_status != 0 && has_opponent_path == 0) {
		first_point = route_vectors[vector_index + TRACK_ROUTE_SECOND_VECTOR_OFFSET];
		second_point = route_vectors[vector_index];
	} else {
		first_point = route_vectors[vector_index];
		second_point = route_vectors[vector_index + TRACK_ROUTE_SECOND_VECTOR_OFFSET];
	}

	rotate_route_points(&first_point, &second_point, track_info->route_orientation);

	store_route_points(track_index, track_object, &first_point, &second_point, output,
					   has_opponent_path);

	legacy_u16 route_index_word = route_index;
	if ((route_index & LEGACY_U8_SIGN_BIT) != 0) {
		route_index_word |= LEGACY_U16_HIGH_BYTE_MASK;
	}
	return LEGACY_U16_WRAP_SUB(route_point_count, TRACK_ROUTE_LAST_INDEX_OFFSET) ==
		   route_index_word;
}
