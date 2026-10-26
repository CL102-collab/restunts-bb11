#include "state_internal.h"
#include "physics_internal.h"
#include "trackdata_layout.h"
#include "externs.h"
#include "residue.h"
#include "crash_state.h"

#define CAR_WHEEL_COUNT 4U
#define CAR_WHEEL_INDEX_FIRST 0
#define GRASS_WHEEL_COUNT_NONE 0
#define GRASS_WHEEL_COUNT_STEP 1
#define CAR_SPEED_INTEGER_SHIFT 8U
#define GRIP_FIXED_SCALE 256L
#define LEGACY_LONG_HIGH_WORD_SHIFT 16U
#define DEMANDED_GRIP_ANGLE_SHIFT 3U
#define DEMANDED_GRIP_SPEED_SQUARE_SHIFT 6U
#define COMBINED_GRIP_PRODUCT_SHIFT 10U
#define FRONT_WHEEL_ANGLE_SHIFT 2U
#define ROTATION_OFFSET_NONE 0
#define ROTATION_RECENTER_THRESHOLD 8
#define ROTATION_RECENTER_STEP 1
#define SLIDE_ANGLE_WEIGHT 3
#define SLIDE_ANGLE_BLEND_SHIFT 2U
#define SLIDE_ANGLE_DECAY_SHIFT 4U
#define SLIDE_ANGLE_FINAL_DECAY_SHIFT 1U
#define SLIDE_ANGLE_DECAY_THRESHOLD 16
#define BANK_EFFECT_ROTATION_THRESHOLD 4
#define BANKED_TRACK_FIRST 52U
#define BANKED_TRACK_LAST 55U
#define BANKED_TRACK_TILT_DIVISOR 5
#define SLIDE_CORRECTION_DIVISOR 14
#define SLIDE_GRIP_TOLERANCE 1000U
#define SLIDE_YAW_DAMPING_DIVISOR 2
#define SLIDE_SPEED_PENALTY_SHIFT 1U

enum LEFT_CORNER_BIAS_MODE { LEFT_CORNER_BIAS_OFF = 0, LEFT_CORNER_BIAS_ON = 1 };

static legacy_s16 left_corner_bias_enabled = LEFT_CORNER_BIAS_ON;

void configure_left_corner_bias(legacy_s16 argc, legacy_s8 *argv[])
{
	static const legacy_s8 off_option[] = "--lcb:off";
	static const legacy_s8 on_option[] = "--lcb:on";
	left_corner_bias_enabled = LEFT_CORNER_BIAS_ON;
	for (legacy_s16 index = 1; index < argc; index++) {
		if (stricmp(argv[index], off_option) == 0) {
			left_corner_bias_enabled = LEFT_CORNER_BIAS_OFF;
		} else if (stricmp(argv[index], on_option) == 0) {
			left_corner_bias_enabled = LEFT_CORNER_BIAS_ON;
		}
	}
}

/* Original grass coefficients: tests/fixtures/amiga-grass-reference.json.
 * Only grass is overridden; grass drag and skid formulas are shared. */
static const legacy_s8 amiga_grass_car_ids[][5] = {"ANSX", "AUDI", "COUN", "FGTO", "JAGU", "LANC",
												   "LM02", "P962", "PC04", "PMIN", "VETT"};
static const legacy_s16 amiga_grass_values[] = {128, 200, 128, 128, 40, 210, 230, 30, 180, 30, 128};
static legacy_s16 amiga_grass_enabled;

void configure_amiga_grass(legacy_s16 argc, legacy_s8 *argv[])
{
	amiga_grass_enabled = 0;
	for (legacy_s16 index = 1; index < argc; index++) {
		if (stricmp(argv[index], (const legacy_s8 *)"--amigagrass") == 0) {
			amiga_grass_enabled = 1;
		}
	}
}

void apply_amiga_grass(struct SIMD *simd, const legacy_s8 car_id[4])
{
	if (amiga_grass_enabled == 0) {
		return;
	}
	for (legacy_u16 index = 0; index < sizeof(amiga_grass_car_ids) / sizeof(amiga_grass_car_ids[0]);
		 index++) {
		legacy_u16 character;
		for (character = 0; character < 4U; character++) {
			legacy_s8 letter = car_id[character];
			if (letter >= 'a' && letter <= 'z') {
				letter = (legacy_s8)(letter - 'a' + 'A');
			}
			if (letter != amiga_grass_car_ids[index][character]) {
				break;
			}
		}
		if (character == 4U) {
			simd->surface_grip[CAR_SURFACE_GRASS - CAR_SURFACE_PAVED] = amiga_grass_values[index];
			return;
		}
	}
	/* Custom car IDs have no original Amiga counterpart: retain their data. */
}

legacy_s16 scale_cornering_angle(legacy_s16 angle, legacy_u8 shift)
{
	if (left_corner_bias_enabled == LEFT_CORNER_BIAS_OFF && angle < 0) {
		/* Shift the magnitude to round both steering directions toward zero.
		 * Widen before negation so the minimum signed word remains valid. */
		return (legacy_s16)-LEGACY_S32_SAR(-(legacy_s32)angle, shift);
	}
	/* The original SAR rounds negative (left) angles down, increasing their
	 * magnitude and turning farther for the same demanded grip. */
	return LEGACY_S16_SAR(angle, shift);
}

/*
 * The original player update reuses four words below update_player_tick's stack frame.
 * update_car_speed and update_grip write that same physical window before the
 * player physics reads it. Keep the window as explicit 16-bit execution state
 * so its behavior does not depend on a compiler's frame layout or ABI.
 */
void update_legacy_grip_stack_words(struct CARSTATE *carstate, struct SIMD *simd,
									legacy_u16 speed_before_grip, legacy_u16 speed2_before_grip,
									legacy_s16 caller_si)
{
	/* update_grip saves its caller's SI in the future fourth contact-distance slot. */
	legacy_execution_residue.grip_stack_words[LEGACY_RESIDUE_FOURTH_WORD] = caller_si;
	if (carstate->car_sumSurfAllWheels == CAR_WHEEL_CONTACT_NONE) {
		return;
	}

	/*
	 * Reproduce update_grip's first operands: twice the car's base grip and
	 * the sum of the four surface-specific sliding coefficients.
	 */
	legacy_s16 combined_grip_operand = LEGACY_S16_SHL(simd->grip, 1U);
	legacy_s16 *sliding_values = &simd->sliding;
	legacy_s16 sliding_sum = 0;
	for (legacy_u16 i = CAR_WHEEL_INDEX_FIRST; i < CAR_WHEEL_COUNT; i++) {
		sliding_sum = LEGACY_S16_WRAP_ADD(sliding_sum,
										  sliding_values[(legacy_u8)carstate->car_surfaceWhl[i]]);
	}

	/* Operand words left by update_grip's first signed long multiply. */
	legacy_execution_residue.grip_stack_words[LEGACY_RESIDUE_FIRST_WORD] = sliding_sum < 0 ? -1 : 0;
	legacy_execution_residue.grip_stack_words[LEGACY_RESIDUE_SECOND_WORD] = combined_grip_operand;
	legacy_execution_residue.grip_stack_words[LEGACY_RESIDUE_THIRD_WORD] =
		combined_grip_operand < 0 ? -1 : 0;

	if (carstate->car_demandedGrip <= carstate->car_surfacegrip_sum) {
		return;
	}

	/*
	 * Sliding grip uses the post-deceleration speed when any wheel is on
	 * grass, with the divisor selected by the number of grass wheels.
	 */
	legacy_s16 grass_wheels = GRASS_WHEEL_COUNT_NONE;
	for (legacy_u16 i = CAR_WHEEL_INDEX_FIRST; i < CAR_WHEEL_COUNT; i++) {
		if (carstate->car_surfaceWhl[i] == CAR_SURFACE_GRASS) {
			grass_wheels = LEGACY_S16_WRAP_ADD(grass_wheels, GRASS_WHEEL_COUNT_STEP);
		}
	}
	legacy_u16 grip_speed = speed_before_grip;
	if (grass_wheels != GRASS_WHEEL_COUNT_NONE) {
		speed2_before_grip = LEGACY_U16_WRAP_SUB(
			speed2_before_grip,
			LEGACY_U16_DIV_OR_ZERO(speed2_before_grip, grassDecelDivTab[grass_wheels]));
		grip_speed = speed2_before_grip;
	}

	/* Operand words left by the sliding-grip signed long division. */
	legacy_u16 speed_shr8 = grip_speed >> CAR_SPEED_INTEGER_SHIFT;
	legacy_u32 speed_squared = LEGACY_U32_WRAP_MUL((legacy_u32)speed_shr8, (legacy_u32)speed_shr8);
	legacy_s32 scaled_combined_grip =
		LEGACY_S32_WRAP_MUL((legacy_s32)carstate->car_surfacegrip_sum, GRIP_FIXED_SCALE);
	legacy_execution_residue.grip_stack_words[LEGACY_RESIDUE_FIRST_WORD] =
		(legacy_s16)((legacy_u32)scaled_combined_grip >> LEGACY_LONG_HIGH_WORD_SHIFT);
	legacy_execution_residue.grip_stack_words[LEGACY_RESIDUE_SECOND_WORD] =
		(legacy_s16)speed_squared;
	legacy_execution_residue.grip_stack_words[LEGACY_RESIDUE_THIRD_WORD] =
		(legacy_s16)(speed_squared >> LEGACY_LONG_HIGH_WORD_SHIFT);
}

static void grip_grass_drag(struct CARSTATE *carstate)
{
	legacy_u16 grass_wheels = GRASS_WHEEL_COUNT_NONE;
	for (legacy_u16 i = CAR_WHEEL_INDEX_FIRST; i < CAR_WHEEL_COUNT; i++) {
		if (carstate->car_surfaceWhl[i] == CAR_SURFACE_GRASS) {
			grass_wheels = LEGACY_U16_WRAP_ADD(grass_wheels, GRASS_WHEEL_COUNT_STEP);
		}
	}
	if (grass_wheels != GRASS_WHEEL_COUNT_NONE) {
		carstate->car_actual_speed = LEGACY_U16_WRAP_SUB(
			carstate->car_actual_speed,
			LEGACY_U16_DIV_OR_ZERO(carstate->car_actual_speed, grassDecelDivTab[grass_wheels]));
		carstate->car_rev_speed = carstate->car_actual_speed;
	}
}

static legacy_u16 grip_demanded(legacy_s16 initial_angle, legacy_u16 speed_shr8)
{
	legacy_s16 absolute_angle = absolute_word(initial_angle);
	legacy_s16 angle_factor = LEGACY_S16_SAR(absolute_angle, DEMANDED_GRIP_ANGLE_SHIFT);
	legacy_u16 square_low = LEGACY_U16_WRAP_MUL(speed_shr8, speed_shr8);
	square_low = (legacy_u16)(square_low >> DEMANDED_GRIP_SPEED_SQUARE_SHIFT);
	legacy_u16 demanded_grip = LEGACY_U16_WRAP_MUL(square_low, angle_factor);
	return demanded_grip;
}

static legacy_s16 grip_surface_sum(struct CARSTATE *carstate, struct SIMD *simd)
{
	legacy_s16 combined_grip = LEGACY_S16_SHL(simd->grip, 1U);
	legacy_s16 *sliding_values = &simd->sliding;
	legacy_s16 sliding_sum = 0;
	for (legacy_u16 i = CAR_WHEEL_INDEX_FIRST; i < CAR_WHEEL_COUNT; i++) {
		sliding_sum = LEGACY_S16_WRAP_ADD(sliding_sum,
										  sliding_values[(legacy_u8)carstate->car_surfaceWhl[i]]);
	}
	legacy_s32 product = LEGACY_S32_WRAP_MUL((legacy_s32)combined_grip, (legacy_s32)sliding_sum);
	combined_grip =
		LEGACY_S16_FROM_BITS((legacy_u16)LEGACY_S32_SAR(product, COMBINED_GRIP_PRODUCT_SHIFT));
	return combined_grip;
}

static void grip_recenter(struct CARSTATE *carstate)
{
	if (carstate->car_steeringAngle == CAR_STEERING_CENTERED) {
		legacy_s16 rotation_low = LEGACY_S8_FROM_BITS((legacy_u8)carstate->car_rotate.x);
		if (rotation_low != ROTATION_OFFSET_NONE) {
			legacy_s16 absolute_angle = rotation_low;
			if (absolute_angle < 0) {
				absolute_angle = LEGACY_S16_WRAP_NEGATE(absolute_angle);
			}
			if (absolute_angle < ROTATION_RECENTER_THRESHOLD) {
				if (rotation_low > 0) {
					carstate->car_rotate.x =
						LEGACY_S16_WRAP_SUB(carstate->car_rotate.x, ROTATION_RECENTER_STEP);
				} else {
					carstate->car_rotate.x =
						LEGACY_S16_WRAP_ADD(carstate->car_rotate.x, ROTATION_RECENTER_STEP);
				}
			}
		}
	}
}

static legacy_s16 grip_slip_angle(struct CARSTATE *carstate, legacy_s16 initial_angle,
								  legacy_s16 combined_grip, legacy_u16 demanded_grip,
								  legacy_u16 speed_shr8)
{
	legacy_s16 adjusted_angle = initial_angle;
	if (LEGACY_S16_FROM_BITS(demanded_grip) > combined_grip) {
		carstate->car_slidingFlag = CAR_SLIDING_ACTIVE;
		legacy_s32 numerator = LEGACY_S32_WRAP_MUL((legacy_s32)combined_grip, GRIP_FIXED_SCALE);
		legacy_s32 denominator =
			LEGACY_S32_WRAP_MUL((legacy_s32)speed_shr8, (legacy_s32)speed_shr8);
		adjusted_angle =
			LEGACY_S16_FROM_BITS((legacy_u16)LEGACY_S32_DIV_OR_ZERO(numerator, denominator));
		if (initial_angle < 0) {
			adjusted_angle = LEGACY_S16_WRAP_NEGATE(adjusted_angle);
		}
		adjusted_angle = scale_cornering_angle(
			LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_MUL(adjusted_angle, SLIDE_ANGLE_WEIGHT),
								initial_angle),
			SLIDE_ANGLE_BLEND_SHIFT);
		carstate->car_slip_angle = LEGACY_S16_WRAP_SUB(initial_angle, adjusted_angle);
	} else {
		carstate->car_slidingFlag = CAR_SLIDING_INACTIVE;
		if (carstate->car_slip_angle != 0) {
			carstate->car_slip_angle = LEGACY_S16_WRAP_SUB(
				carstate->car_slip_angle,
				LEGACY_S16_SAR(carstate->car_slip_angle, SLIDE_ANGLE_DECAY_SHIFT));
			legacy_s16 absolute_angle = carstate->car_slip_angle;
			if (absolute_angle < 0) {
				absolute_angle = LEGACY_S16_WRAP_NEGATE(absolute_angle);
			}
			if (absolute_angle < SLIDE_ANGLE_DECAY_THRESHOLD) {
				carstate->car_slip_angle =
					LEGACY_S16_SAR(carstate->car_slip_angle, SLIDE_ANGLE_FINAL_DECAY_SHIFT);
			}
		}
	}
	return adjusted_angle;
}

static void grip_banked_steering(struct CARSTATE *carstate)
{
	legacy_s16 absolute_angle = carstate->car_rotate.z;
	if (absolute_angle < 0) {
		absolute_angle = LEGACY_S16_WRAP_NEGATE(absolute_angle);
	}
	if (absolute_angle > BANK_EFFECT_ROTATION_THRESHOLD) {
		legacy_u8 tile_x =
			(legacy_u8)((legacy_u32)carstate->car_position.lx >> TRACK_WORLD_TILE_SHIFT);
		legacy_u8 tile_z =
			(legacy_u8)((legacy_u32)carstate->car_position.lz >> TRACK_WORLD_TILE_SHIFT);
		legacy_u8 track = track_element_map[LEGACY_U16_WRAP_ADD(terrainrows[tile_z], tile_x)];
		if (track == TRACK_TILE_CONTINUATION_SOUTHEAST) {
			tile_x = LEGACY_U8_WRAP_SUB(tile_x, TRACK_TILE_COORDINATE_STEP);
			tile_z = LEGACY_U8_WRAP_ADD(tile_z, TRACK_TILE_COORDINATE_STEP);
		} else if (track == TRACK_TILE_CONTINUATION_SOUTH) {
			tile_z = LEGACY_U8_WRAP_ADD(tile_z, TRACK_TILE_COORDINATE_STEP);
		} else if (track == TRACK_TILE_CONTINUATION_EAST) {
			tile_x = LEGACY_U8_WRAP_SUB(tile_x, TRACK_TILE_COORDINATE_STEP);
		}
		track = track_element_map[LEGACY_U16_WRAP_ADD(terrainrows[tile_z], tile_x)];
		if (track >= BANKED_TRACK_FIRST && track <= BANKED_TRACK_LAST) {
			carstate->car_front_wheel_response_angle = LEGACY_S16_WRAP_ADD(
				carstate->car_front_wheel_response_angle,
				LEGACY_S16_DIV_OR_ZERO(carstate->car_rotate.z, BANKED_TRACK_TILT_DIVISOR));
		}
	}
}

static void grip_player_yaw(struct CARSTATE *carstate, legacy_s16 initial_angle,
							legacy_s16 adjusted_angle, legacy_s16 combined_grip,
							legacy_u16 demanded_grip)
{
	legacy_s16 correction = LEGACY_S16_DIV_OR_ZERO(
		LEGACY_S16_WRAP_SUB(adjusted_angle, initial_angle), SLIDE_CORRECTION_DIVISOR);
	if (LEGACY_S16_FROM_BITS(LEGACY_U16_WRAP_ADD(combined_grip, SLIDE_GRIP_TOLERANCE)) <
		LEGACY_S16_FROM_BITS(demanded_grip)) {
		carstate->car_slide_yaw_delta =
			LEGACY_S16_WRAP_ADD(carstate->car_slide_yaw_delta, correction);
		carstate->car_slide_yaw_delta =
			LEGACY_S16_DIV_OR_ZERO(carstate->car_slide_yaw_delta, SLIDE_YAW_DAMPING_DIVISOR);
	} else if (carstate->car_slide_yaw_delta != 0) {
		carstate->car_slide_yaw_delta =
			LEGACY_S16_WRAP_ADD(carstate->car_slide_yaw_delta, correction);
		carstate->car_slide_yaw_delta =
			LEGACY_S16_DIV_OR_ZERO(carstate->car_slide_yaw_delta, SLIDE_YAW_DAMPING_DIVISOR);
		if (carstate->car_slide_yaw_delta == 0) {
			carstate->car_actual_speed = (legacy_u16)multiply_and_scale(
				cos_fast(carstate->car_velocity_heading_offset), carstate->car_actual_speed);
			if (cos_fast(carstate->car_velocity_heading_offset) < 0) {
				carstate->car_actual_speed = CAR_SPEED_STOPPED;
			}
			carstate->car_velocity_heading_offset = 0;
		}
	}
}

static void grip_heading_offset(struct CARSTATE *carstate)
{
	if (carstate->car_velocity_heading_offset != 0 && carstate->car_slide_yaw_delta == 0) {
		carstate->car_velocity_heading_offset =
			damp_collision_angle(carstate->car_velocity_heading_offset);
	}
	if (carstate->car_slide_yaw_delta != 0) {
		carstate->car_velocity_heading_offset = LEGACY_S16_WRAP_SUB(
			carstate->car_velocity_heading_offset, carstate->car_slide_yaw_delta);
	}
}

static void grip_sliding_speed(struct CARSTATE *carstate)
{
	legacy_u16 i;

	if (carstate->car_slidingFlag != CAR_SLIDING_INACTIVE) {
		legacy_s16 absolute_angle = carstate->car_slip_angle;
		if (absolute_angle < 0) {
			absolute_angle = LEGACY_S16_WRAP_NEGATE(absolute_angle);
		}
		legacy_s16 penalty = LEGACY_S16_SHL(absolute_angle, SLIDE_SPEED_PENALTY_SHIFT);
		if (carstate->car_rev_speed <= (legacy_u16)penalty) {
			carstate->car_rev_speed = CAR_SPEED_STOPPED;
			carstate->car_actual_speed = CAR_SPEED_STOPPED;
		} else if (carstate->car_actual_speed > (legacy_u16)penalty) {
			carstate->car_rev_speed = LEGACY_U16_WRAP_SUB(carstate->car_rev_speed, penalty);
			carstate->car_actual_speed = LEGACY_U16_WRAP_SUB(carstate->car_actual_speed, penalty);
		} else {
			carstate->car_rev_speed = CAR_SPEED_STOPPED;
			carstate->car_actual_speed = CAR_SPEED_STOPPED;
		}

		if (carstate->car_crashBmpFlag == CRASH_EVENT_NONE) {
			for (i = CAR_WHEEL_INDEX_FIRST; i < CAR_WHEEL_COUNT; i++) {
				if (carstate->car_surfaceWhl[i] == CAR_SURFACE_PAVED) {
					break;
				}
			}
			carstate->car_sound_flags =
				(legacy_u8)carstate->car_sound_flags |
				(i < CAR_WHEEL_COUNT ? CAR_SOUND_SKID_PAVED_FLAG : CAR_SOUND_SKID_OFFROAD_FLAG);
		}
	}
}

static void grip_player_response(struct CARSTATE *carstate, legacy_s16 initial_angle,
								 legacy_s16 combined_grip, legacy_u16 demanded_grip,
								 legacy_u16 speed_shr8)
{
	grip_recenter(carstate);
	legacy_s16 adjusted_angle =
		grip_slip_angle(carstate, initial_angle, combined_grip, demanded_grip, speed_shr8);
	if (carstate->car_slide_yaw_delta == 0 && carstate->car_crashBmpFlag != CRASH_EVENT_COLLISION) {
		carstate->car_front_wheel_response_angle = adjusted_angle;
	} else {
		carstate->car_front_wheel_response_angle = 0;
	}
	grip_banked_steering(carstate);
	grip_player_yaw(carstate, initial_angle, adjusted_angle, combined_grip, demanded_grip);
}

void update_grip(struct CARSTATE *carstate, struct SIMD *simd, legacy_s16 grip_behavior)
{
	if (carstate->car_sumSurfAllWheels == CAR_WHEEL_CONTACT_NONE) {
		carstate->car_front_wheel_response_angle = 0;
		carstate->car_slidingFlag = CAR_SLIDING_INACTIVE;
		return;
	}
	grip_grass_drag(carstate);
	legacy_s16 initial_angle =
		LEGACY_S16_WRAP_ADD(carstate->car_steeringAngle, carstate->car_velocity_heading_offset);
	legacy_u16 speed_shr8 = (legacy_u16)(carstate->car_rev_speed >> CAR_SPEED_INTEGER_SHIFT);
	legacy_u16 demanded_grip = grip_demanded(initial_angle, speed_shr8);
	legacy_s16 combined_grip = grip_surface_sum(carstate, simd);
	carstate->car_demandedGrip = LEGACY_S16_FROM_BITS(demanded_grip);
	carstate->car_surfacegrip_sum = combined_grip;
	if (grip_behavior == GRIP_BEHAVIOR_OPPONENT) {
		carstate->car_front_wheel_response_angle =
			LEGACY_S16_SHL(carstate->car_steeringAngle, FRONT_WHEEL_ANGLE_SHIFT);
		if (carstate->car_slide_yaw_delta != 0) {
			carstate->car_slide_yaw_delta = damp_collision_angle(carstate->car_slide_yaw_delta);
		}
	}
	if (grip_behavior == GRIP_BEHAVIOR_PLAYER) {
		grip_player_response(carstate, initial_angle, combined_grip, demanded_grip, speed_shr8);
	}
	grip_heading_offset(carstate);
	grip_sliding_speed(carstate);
	carstate->car_slip_angle = 0;
}
