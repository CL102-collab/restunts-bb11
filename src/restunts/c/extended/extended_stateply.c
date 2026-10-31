#include "extended_stateply.h"

#ifdef RESTUNTS_SDL3
#include "../math.h"
#include "../physics_internal.h"
#include "../crash_state.h"
#include "../track/track_collision.h"
#include "../externs.h"
#include "extended_track.h"
#include "extended_geometry.h"
#include "roll_ramp.h"
#if !defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY)
#include "extended_replay_runtime.h"
#endif

#define PLAYER_PHYSICS_WHEEL_COUNT CARSTATE_WHEEL_COUNT
#define PLAYER_PHYSICS_FRONT_WHEEL_COUNT 2
#define PLAYER_PHYSICS_POSITION_SCALE_SHIFT 6U
#define PLAYER_PHYSICS_POSITION_SCALE (1L << PLAYER_PHYSICS_POSITION_SCALE_SHIFT)
#define PLAYER_PHYSICS_PLANE_NORMAL_SCALE (TRIG_FIXED_ONE / 2)
#define PLAYER_PHYSICS_FRACTIONAL_CONTACT_LIMIT 1
#define PLAYER_PHYSICS_WORLD_MIN_POSITION 3840L
#define EXTENDED_PLAYER_TILE_POSITION_SCALE (CSTRK_TILE_WORLD_SIZE * PLAYER_PHYSICS_POSITION_SCALE)
#define EXTENDED_PLAYER_OTHER_CAR_RADIUS (4L * EXTENDED_PLAYER_TILE_POSITION_SCALE)

/* These serialized words were reserved for contact bookkeeping. Keep the
 * legacy car image unchanged while retaining launch momentum across seeks. */
enum PLAYER_ROLL_MOTION_WORD {
	PLAYER_ROLL_VELOCITY,
	PLAYER_ROLL_FRACTION,
	PLAYER_ROLL_PHASE,
	PLAYER_ROLL_LAST_SUPPORTED_ANGLE,
	PLAYER_ROLL_WORD_COUNT
};

enum PLAYER_ROLL_MOTION_PHASE { PLAYER_ROLL_IDLE, PLAYER_ROLL_SUPPORTED, PLAYER_ROLL_AIRBORNE };

#define PLAYER_ROLL_VELOCITY_SCALE 256L
#define PLAYER_ROLL_RATE_FILTER_TICKS 8L
#define PLAYER_ROLL_REAR_WHEEL_MASK (3U << PLAYER_PHYSICS_FRONT_WHEEL_COUNT)

static legacy_u32 player_roll_tick_fraction(legacy_u32 motion_fraction)
{
	if (motion_fraction) {
		return motion_fraction;
	}
	return framespersec == GAME_FRAME_RATE_LOW ? PHYSICS_LOW_RATE_TICK_FRACTION
											   : PHYSICS_FRACTION_ONE;
}

static legacy_s16 player_roll_angle_delta(legacy_s16 first, legacy_s16 second)
{
	return (legacy_s16)(((legacy_u16)(first - second + ANGLE_HALF_TURN) & ANGLE_MASK) -
						ANGLE_HALF_TURN);
}

static legacy_u8 player_roll_motion_active(const struct CARSTATE *carstate)
{
	legacy_s16 phase = carstate->car_reserved_contact_state[PLAYER_ROLL_PHASE];
	return phase == PLAYER_ROLL_SUPPORTED || phase == PLAYER_ROLL_AIRBORNE;
}

static void clear_player_roll_motion(struct CARSTATE *carstate)
{
	if (extended_track_active() && player_roll_motion_active(carstate)) {
		for (legacy_u8 word = 0; word < PLAYER_ROLL_WORD_COUNT; word++) {
			carstate->car_reserved_contact_state[word] = 0;
		}
	}
}

static void prepare_player_roll_motion(struct CARSTATE *carstate,
									   const struct GAMESTATE *motion_state,
									   legacy_u32 motion_fraction)
{
	if (!extended_track_active() || !player_roll_motion_active(carstate)) {
		return;
	}
	if (carstate->car_crashBmpFlag != CRASH_EVENT_NONE ||
		motion_state->game_inputmode == GAME_INPUT_MODE_INTRO) {
		clear_player_roll_motion(carstate);
		return;
	}
	if (carstate->car_reserved_contact_state[PLAYER_ROLL_PHASE] != PLAYER_ROLL_AIRBORNE) {
		return;
	}
	legacy_s64 displacement =
		(legacy_s64)carstate->car_reserved_contact_state[PLAYER_ROLL_VELOCITY] *
		(legacy_s64)player_roll_tick_fraction(motion_fraction) / (legacy_s64)PHYSICS_FRACTION_ONE;
	displacement += carstate->car_reserved_contact_state[PLAYER_ROLL_FRACTION];
	legacy_s16 angle = (legacy_s16)(displacement / PLAYER_ROLL_VELOCITY_SCALE);
	carstate->car_reserved_contact_state[PLAYER_ROLL_FRACTION] =
		(legacy_s16)(displacement % PLAYER_ROLL_VELOCITY_SCALE);
	car_working_roll = LEGACY_S16_WRAP_ADD(car_working_roll, angle);
}

static struct VECTOR player_roll_contact_normal(const struct CSTRK_OBJECT *object, legacy_s32 x,
												legacy_s32 z)
{
	struct VECTOR relative = {
		(legacy_s16)(x - object->x * CSTRK_TILE_WORLD_SIZE -
					 object->width * CSTRK_TILE_WORLD_SIZE / 2),
		0,
		(legacy_s16)(z - (extended_track_height() - object->y) * CSTRK_TILE_WORLD_SIZE +
					 object->height * CSTRK_TILE_WORLD_SIZE / 2)};
	legacy_s16 orientation = roll_ramp_orientation(object);
	struct MATRIX rotation;
	struct VECTOR local, point, normal, world;
	mat_rot_y(&rotation, LEGACY_S16_WRAP_NEGATE(orientation));
	mat_mul_vector(&relative, &rotation, &local);
	legacy_s16 distance = ROLL_RAMP_HALF_LENGTH - local.z;
	legacy_s16 cosine = cos_fast(roll_ramp_bank_angle(distance));
	legacy_s16 lateral = cosine ? (legacy_s16)((legacy_s32)local.x * TRIG_FIXED_ONE / cosine) : 0;
	/* The finite collision query proves contact. Sample the shared smooth
	 * support normal there so tessellation seams cannot change launch torque. */
	roll_ramp_sample(distance, lateral, &point, &normal);
	mat_rot_y(&rotation, orientation);
	mat_mul_vector(&normal, &rotation, &world);
	extended_geometry_grade_normal(object, &world);
	return world;
}

static legacy_u8 player_roll_ramp_contact(const struct CARSTATE *carstate, legacy_s16 *bank)
{
	legacy_u8 mask = 0, rear_count = 0;
	legacy_s32 normal_x = 0, normal_y = 0, normal_z = 0;
	for (legacy_u8 wheel = 0; wheel < PLAYER_PHYSICS_WHEEL_COUNT; wheel++) {
		if (carstate->car_surfaceWhl[wheel] < CAR_SURFACE_PAVED ||
			carstate->car_surfaceWhl[wheel] > CAR_SURFACE_ICE) {
			continue;
		}
		legacy_s32 x, z;
		extended_geometry_world_position(&carstate->car_wheel_contact_positions[wheel], &x, &z);
		if (x < 0 || z < 0) {
			continue;
		}
		const struct CSTRK_OBJECT *object = extended_track_object_at(
			(legacy_int)(x / CSTRK_TILE_WORLD_SIZE),
			extended_track_height() - 1 - (legacy_int)(z / CSTRK_TILE_WORLD_SIZE));
		if (object && object->roll_ramp) {
			/* Terrain below the open grille can share its dirt/ice contact code.
			 * Require the generated finite road plane at this wheel. */
			struct TRACK_COLLISION_SNAPSHOT saved;
			track_collision_capture(&saved);
			struct VECTOR point = carstate->car_wheel_contact_positions[wheel];
			build_track_object(&point, &point);
			legacy_s16 distance = plane_signed_distance(planindex, point.x, point.y, point.z);
			legacy_u8 supported = planindex > EXTENDED_GEOMETRY_PLANE_INDEX &&
								  distance >= -PHYSICS_PLANE_CONTACT_TOLERANCE &&
								  distance <= PHYSICS_PLANE_CONTACT_TOLERANCE;
			if (supported) {
				mask |= 1U << wheel;
				if (wheel >= PLAYER_PHYSICS_FRONT_WHEEL_COUNT) {
					struct VECTOR normal = player_roll_contact_normal(object, x, z);
					normal_x += normal.x;
					normal_y += normal.y;
					normal_z += normal.z;
					rear_count++;
				}
			}
			track_collision_restore(&saved);
		}
	}
	*bank = 0;
	if (rear_count) {
		/* Keep one consistent axle as the front and rear leave in turn.
		 * Express its actual support normal in the unrolled heading frame. */
		struct VECTOR normal = {(legacy_s16)(normal_x / rear_count),
								(legacy_s16)(normal_y / rear_count),
								(legacy_s16)(normal_z / rear_count)};
		struct VECTOR heading, level;
		struct MATRIX rotation;
		mat_rot_y(&rotation, car_working_yaw);
		mat_mul_vector(&normal, &rotation, &heading);
		mat_rot_x(&rotation, car_working_pitch);
		mat_mul_vector(&heading, &rotation, &level);
		*bank = polarAngle(level.x, level.y);
	}
	return mask;
}

static legacy_s16 player_roll_filter_rate(legacy_s16 previous, legacy_s64 measured,
										  legacy_u32 motion_fraction)
{
	legacy_u32 remaining = player_roll_tick_fraction(motion_fraction);
	while (remaining) {
		legacy_u32 step = remaining > PHYSICS_FRACTION_ONE ? PHYSICS_FRACTION_ONE : remaining;
		legacy_s64 change = (measured - previous) * (legacy_s64)step;
		change /= PLAYER_ROLL_RATE_FILTER_TICKS * (legacy_s64)PHYSICS_FRACTION_ONE;
		previous += (legacy_s16)change;
		remaining -= step;
	}
	return previous;
}

static void finish_player_roll_motion(struct CARSTATE *carstate,
									  const struct GAMESTATE *motion_state,
									  legacy_u32 motion_fraction)
{
	if (!extended_track_active() || motion_state->game_inputmode == GAME_INPUT_MODE_INTRO) {
		return;
	}
	if (carstate->car_crashBmpFlag != CRASH_EVENT_NONE) {
		clear_player_roll_motion(carstate);
		return;
	}
	legacy_s16 *roll = carstate->car_reserved_contact_state;
	legacy_s16 bank;
	legacy_u8 contact_mask = player_roll_ramp_contact(carstate, &bank);
	if (contact_mask) {
		legacy_u8 first_supported = roll[PLAYER_ROLL_PHASE] != PLAYER_ROLL_SUPPORTED;
		if (first_supported) {
			roll[PLAYER_ROLL_LAST_SUPPORTED_ANGLE] = bank;
			roll[PLAYER_ROLL_VELOCITY] = 0;
			roll[PLAYER_ROLL_FRACTION] = 0;
		}
		legacy_s16 delta = player_roll_angle_delta(bank, roll[PLAYER_ROLL_LAST_SUPPORTED_ANGLE]);
		legacy_s64 velocity = (legacy_s64)delta * PLAYER_ROLL_VELOCITY_SCALE *
							  (legacy_s64)PHYSICS_FRACTION_ONE /
							  (legacy_s64)player_roll_tick_fraction(motion_fraction);
		legacy_s32 maximum = LEGACY_S16_MAX;
		if (velocity > maximum) {
			velocity = maximum;
		} else if (velocity < -maximum) {
			velocity = -maximum;
		}
		/* Contact normals impart the rate independently of the suspension's
		 * quantized pose corrections. Filter in physical 20 Hz time units. */
		if ((contact_mask & PLAYER_ROLL_REAR_WHEEL_MASK) == PLAYER_ROLL_REAR_WHEEL_MASK) {
			roll[PLAYER_ROLL_VELOCITY] =
				player_roll_filter_rate(roll[PLAYER_ROLL_VELOCITY], velocity, motion_fraction);
			roll[PLAYER_ROLL_LAST_SUPPORTED_ANGLE] = bank;
		}
		roll[PLAYER_ROLL_PHASE] = PLAYER_ROLL_SUPPORTED;
	} else if (carstate->car_sumSurfFrontWheels || carstate->car_sumSurfRearWheels) {
		clear_player_roll_motion(carstate);
	} else if (roll[PLAYER_ROLL_PHASE] == PLAYER_ROLL_SUPPORTED) {
		roll[PLAYER_ROLL_PHASE] = PLAYER_ROLL_AIRBORNE;
		/* Gravity moves the airborne rigid body as a whole. Keep the mean
		 * launch velocity without counting old per-wheel impulses twice. */
		legacy_s32 total = 0;
		for (legacy_u8 wheel = 0; wheel < PLAYER_PHYSICS_WHEEL_COUNT; wheel++) {
			total += carstate->car_wheel_vertical_speed[wheel];
		}
		legacy_s16 mean = (legacy_s16)(total / PLAYER_PHYSICS_WHEEL_COUNT);
		for (legacy_u8 wheel = 0; wheel < PLAYER_PHYSICS_WHEEL_COUNT; wheel++) {
			carstate->car_wheel_vertical_speed[wheel] = mean;
		}
	}
}
struct PLAYER_GROUND_CONTACT {
	legacy_s32 height;
	legacy_s16 distance;
};

static legacy_s32 ground_plane_coordinate(legacy_s32 position, legacy_s16 origin)
{
	legacy_s32 whole = LEGACY_S16_WRAP_SUB(position_to_word(position), origin);
	legacy_s32 fraction = (legacy_s32)((legacy_u32)position & (PLAYER_PHYSICS_POSITION_SCALE - 1));
	return whole * PLAYER_PHYSICS_POSITION_SCALE + fraction;
}

static legacy_s16 extended_ground_contact(const struct VECTORLONG *position,
										  struct PLAYER_GROUND_CONTACT *contact,
										  legacy_s16 input_mode)
{
	if (!extended_track_active() || input_mode == GAME_INPUT_MODE_INTRO) {
		return 0;
	}
	if (planindex != EXTENDED_GEOMETRY_PLANE_INDEX) {
		if (legacy_collision_is_enabled()) {
			return 0;
		}
#if !defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY)
		if (extended_replay_is_active() &&
			extended_replay_version() < CSRPL_FIXED_POINT_SOLID_VERSION) {
			return 0;
		}
#endif
		return track_selected_plane_contact_fixed(position, &contact->height, &contact->distance);
	}
	const struct PLANE *plane = current_planptr;
	const struct VECTOR *normal = &plane->plane_normal;
	if (normal->y <= 0 || (normal->x == 0 && normal->z == 0)) {
		return 0;
	}
	/* Native slope rotations and normals differ slightly. Whole-unit wheel
	 * coordinates also turn a continuous incline into steps. Resolve hillside
	 * contacts against one plane using the wheel's original 1/64-unit position. */
	legacy_s32 x = ground_plane_coordinate(
		position->lx, LEGACY_S16_WRAP_ADD(elem_xCenter, plane->plane_origin.x));
	legacy_s32 z = ground_plane_coordinate(
		position->lz, LEGACY_S16_WRAP_ADD(elem_zCenter, plane->plane_origin.z));
	legacy_s32 origin_y =
		((legacy_s32)terrainHeight + plane->plane_origin.y) * PLAYER_PHYSICS_POSITION_SCALE;
	legacy_s64 horizontal = (legacy_s64)x * normal->x + (legacy_s64)z * normal->z;
	legacy_s64 rise = -horizontal;
	legacy_s64 height = rise / normal->y;
	/* Round the surface upward by at most one position unit so a corrected
	 * wheel never remains just below the plane. */
	if (rise > 0 && rise % normal->y != 0) {
		height++;
	}
	contact->height = origin_y + (legacy_s32)height;
	legacy_s64 distance = horizontal + ((legacy_s64)position->ly - origin_y) * normal->y;
	contact->distance = (legacy_s16)(distance / (PLAYER_PHYSICS_PLANE_NORMAL_SCALE *
												 PLAYER_PHYSICS_POSITION_SCALE));
	return 1;
}

void extended_player_begin(struct CARSTATE *carstate, const struct GAMESTATE *motion_state,
						   legacy_u32 motion_fraction)
{
	if (extended_track_active()) {
		extended_geometry_set_context(carstate->car_position.lx, carstate->car_position.lz);
	}
	prepare_player_roll_motion(carstate, motion_state, motion_fraction);
}

void extended_player_finish(struct CARSTATE *carstate, const struct GAMESTATE *motion_state,
							legacy_u32 motion_fraction)
{
	finish_player_roll_motion(carstate, motion_state, motion_fraction);
}

void extended_player_clear_motion(struct CARSTATE *carstate)
{
	clear_player_roll_motion(carstate);
}

void extended_player_clear_crashed_motion(struct CARSTATE *carstate)
{
	if (carstate->car_crashBmpFlag != CRASH_EVENT_NONE) {
		clear_player_roll_motion(carstate);
	}
}

void extended_player_capture_suspension(const struct CARSTATE *carstate,
										struct EXTENDED_PLAYER_SUSPENSION_STATE *saved)
{
	saved->retain = extended_track_active() && player_roll_motion_active(carstate);
	if (saved->retain) {
		for (legacy_u8 word = 0; word < PLAYER_ROLL_WORD_COUNT; word++) {
			saved->words[word] = carstate->car_reserved_contact_state[word];
		}
	}
}

void extended_player_restore_suspension(struct CARSTATE *carstate,
										const struct EXTENDED_PLAYER_SUSPENSION_STATE *saved)
{
	if (saved->retain) {
		for (legacy_u8 word = 0; word < PLAYER_ROLL_WORD_COUNT; word++) {
			carstate->car_reserved_contact_state[word] = saved->words[word];
		}
	}
}

legacy_s16 extended_player_measure_plane(const struct VECTORLONG *position, legacy_s16 input_mode)
{
	struct PLAYER_GROUND_CONTACT contact;
	if (!extended_ground_contact(position, &contact, input_mode)) {
		return 0;
	}
	nextPosAndNormalIP = contact.distance;
	return 1;
}

void extended_player_contact_distance(const struct VECTORLONG *position, legacy_s16 input_mode,
									  legacy_s16 *distance)
{
	struct PLAYER_GROUND_CONTACT contact;
	if (extended_ground_contact(position, &contact, input_mode)) {
		*distance = contact.distance;
	}
}

legacy_s16 extended_player_wheel_gravity(const struct CARSTATE *carstate, legacy_s16 gravity)
{
	if (extended_track_active() &&
		carstate->car_reserved_contact_state[PLAYER_ROLL_PHASE] == PLAYER_ROLL_AIRBORNE) {
		legacy_s32 total = 0;
		for (legacy_u8 wheel = 0; wheel < PLAYER_PHYSICS_WHEEL_COUNT; wheel++) {
			total += wheel_gravity_steps[wheel];
		}
		gravity = (legacy_s16)(total / PLAYER_PHYSICS_WHEEL_COUNT);
	}
	return gravity;
}

legacy_s16 extended_player_use_ground_plane(const struct VECTORLONG *position)
{
	if (!extended_track_active()) {
		return 0;
	}
	struct VECTOR wheel = {position_to_word(position->lx), position_to_word(position->ly),
						   position_to_word(position->lz)};
	build_extended_track_terrain(&wheel);
	return 1;
}

legacy_s16 extended_player_correct_wheel_contact(struct VECTORLONG *current,
												 const struct VECTORLONG *previous,
												 legacy_s16 input_mode)
{
	struct PLAYER_GROUND_CONTACT contact;
	struct PLAYER_GROUND_CONTACT previous_contact;
	if (!extended_ground_contact(current, &contact, input_mode) ||
		!extended_ground_contact(previous, &previous_contact, input_mode) ||
		previous_contact.distance > 0 ||
		(planindex != EXTENDED_GEOMETRY_PLANE_INDEX &&
		 (previous_contact.distance < -PLAYER_PHYSICS_FRACTIONAL_CONTACT_LIMIT ||
		  contact.distance < -PLAYER_PHYSICS_FRACTIONAL_CONTACT_LIMIT))) {
		return 0;
	}
	/* Correct sub-unit penetration without pulling an airborne wheel down onto
	 * an incline or changing its horizontal travel. New impacts retain the
	 * original split between air and road travel. */
	if (current->ly < contact.height) {
		current->ly = contact.height;
	}
	return 1;
}

void extended_player_finish_wheel_contact(struct VECTORLONG *position, legacy_s16 input_mode,
										  legacy_s16 resolved)
{
	/* New landings can retain a fraction below their exact mesh after the native
	 * rotated response. Keep impact accounting, then remove only that residual. */
	struct PLAYER_GROUND_CONTACT contact;
	if (resolved && planindex != EXTENDED_GEOMETRY_PLANE_INDEX &&
		extended_ground_contact(position, &contact, input_mode) &&
		contact.distance >= -PLAYER_PHYSICS_FRACTIONAL_CONTACT_LIMIT &&
		position->ly < contact.height) {
		position->ly = contact.height;
	}
}

static legacy_s32 extended_player_clamp_coordinate(legacy_s32 position, legacy_s16 dimension)
{
	legacy_s32 maximum = (legacy_s32)dimension * EXTENDED_PLAYER_TILE_POSITION_SCALE -
						 PLAYER_PHYSICS_WORLD_MIN_POSITION;
	if (position > maximum) {
		return maximum - 1L;
	}
	if (position < PLAYER_PHYSICS_WORLD_MIN_POSITION) {
		return PLAYER_PHYSICS_WORLD_MIN_POSITION;
	}
	return position;
}

legacy_s16 extended_player_clamp_world_pose(legacy_s32 *x, legacy_s32 *z)
{
	if (!extended_track_active()) {
		return 0;
	}
	*x = extended_player_clamp_coordinate(*x, extended_track_width());
	*z = extended_player_clamp_coordinate(*z, extended_track_height());
	return 1;
}

legacy_s16 extended_player_body_height_only(legacy_s16 height_only)
{
	/* Translated or tilted ground planes retain the original solid-ground body
	 * test. Initial wheel contacts can start just below the two-unit bias. */
	if (extended_track_active() && (planindex == EXTENDED_GEOMETRY_PLANE_INDEX ||
									planindex == EXTENDED_GEOMETRY_PLANE_INDEX + 1 ||
									planindex == EXTENDED_GEOMETRY_PLANE_INDEX + 2 ||
									planindex == EXTENDED_GEOMETRY_PLANE_INDEX + 3)) {
		return 1;
	}
	return height_only;
}

legacy_s16 extended_player_other_car_distant(const struct CARSTATE *other_carstate)
{
	if (extended_track_active()) {
		legacy_s32 dx = car_working_x - other_carstate->car_position.lx;
		legacy_s32 dz = car_working_z - other_carstate->car_position.lz;
		if (dx < -EXTENDED_PLAYER_OTHER_CAR_RADIUS || dx > EXTENDED_PLAYER_OTHER_CAR_RADIUS ||
			dz < -EXTENDED_PLAYER_OTHER_CAR_RADIUS || dz > EXTENDED_PLAYER_OTHER_CAR_RADIUS) {
			return 1;
		}
	}
	return 0;
}

void extended_player_start_finish_pole(struct VECTOR *obstacle_pose, legacy_s16 angle,
									   legacy_s16 offset)
{
	if (!extended_track_active()) {
		return;
	}
	const struct CSTRK_OBJECT *object =
		extended_track_object_at(start_finish_column, start_finish_row);
	if (object) {
		struct VECTOR origin;
		extended_geometry_object_origin(extended_track_model(), object, &origin);
		obstacle_pose[0] = (struct VECTOR){
			LEGACY_S16_WRAP_ADD(origin.x, multiply_and_scale(sin_fast(angle), offset)), origin.y,
			LEGACY_S16_WRAP_ADD(origin.z, multiply_and_scale(cos_fast(angle), offset))};
		obstacle_pose[1] = (struct VECTOR){0, 0, 0};
	}
}

void extended_player_scenery_tile(const struct VECTOR *position, legacy_s16 *column,
								  legacy_s16 *row, legacy_s16 *valid)
{
	if (!extended_track_active()) {
		return;
	}
	legacy_s32 world_x, world_z;
	extended_geometry_world_position(position, &world_x, &world_z);
	*column = (legacy_s16)(world_x / CSTRK_TILE_WORLD_SIZE);
	*row = (legacy_s16)(extended_track_height() - 1 - world_z / CSTRK_TILE_WORLD_SIZE);
	*valid = *column >= 0 && *column < extended_track_width() && *row >= 0 &&
			 *row < extended_track_height();
}
#endif
