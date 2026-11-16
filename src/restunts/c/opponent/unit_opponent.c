#include "unit_opponent.h"

#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
#include <math.h>
#include <SDL3/SDL_timer.h>
#include <string.h>
#include "unit_opponent_internal.h"
#include "unit_pilot.h"
#include "unit_pilot_controls.h"
#include "unit_launch.h"
#include "unit_profiles.h"
#include "unit_collision.h"
#include "opponent.h"
#include "../externs.h"
#include "../game_input.h"
#include "../state_internal.h"
#include "../physics/physics_internal.h"
#include "../car/car_speed.h"
#include "../car/car_resources.h"
#include "../crash_state.h"
#include "../owoot/owoot.h"
#include "../extended/extended_replay_runtime.h"
#include "../extended/extended_geometry.h"
#include "../extended/extended_unit_penalty.h"
#include "../extended/extended_unit_checkpoint.h"
#include "../fatal.h"
#include "../residue.h"

#define UNIT_PREPARATION_WORK_UNITS 2U
#define UNIT_PREPARATION_NATIVE_TICKS 64U
#define UNIT_MINIMUM_LOOKAHEAD_SQUARED 1.0
#define UNIT_SPEED_HYSTERESIS 0.5
#define UNIT_STEERING_GRIP_RESERVE 0.96
#define UNIT_COLLISION_REACTION_SECONDS 0.25
#define UNIT_COLLISION_HEIGHT_CLEARANCE 90.0
#define UNIT_COLLISION_MARGIN 20.0
#define UNIT_MINIMUM_MOVING_SPEED 1.0
#define UNIT_HEADING_VECTOR_LENGTH 1024
#define UNIT_BARREL_MINIMUM_STEERING_AUTHORITY 0.25
#define UNIT_PLAYER_STYLE_GRIP_SCALE 512.0
#define UNIT_CHECKPOINT_RECOVERY_SPEED 10.0
#define UNIT_RECHECK_BRAKING_RESERVE 0.5
#define UNIT_RECHECK_STOP_MARGIN_TILES 0.5
#define UNIT_RECHECK_MAXIMUM_LEAD_TILES 16.0
#define UNIT_RECHECK_SETTLED_TICKS 4U
#define UNIT_RECHECK_RETRY_TICKS 20U
#define UNIT_RECHECK_CHUNKS 8U
#define UNIT_RECHECK_TIME_NS 30000000ULL
#define UNIT_FIRST_GEAR 1
#define UNIT_LANDING_GRAVITY (18.0 * 20.0 * 20.0 / UNIT_POSITION_SCALE)

enum UNIT_PREPARATION_PHASE {
	UNIT_PREPARATION_NONE,
	UNIT_PREPARATION_PLANNING,
	UNIT_PREPARATION_COMPLETE,
	UNIT_PREPARATION_FAILED
};

static struct UNIT_ROUTE unit_route;
static struct UNIT_PLANNER *unit_planner;
static legacy_u32 unit_last_frame;
static legacy_u8 unit_preparation;
static struct CSRPL_OPPONENT_COMMAND unit_last_preview_command;
static legacy_s16 unit_recheck_piece = ROUTE_INDEX_NONE;
static legacy_u8 unit_recheck_settled, unit_recheck_pending;
static legacy_u32 unit_recheck_retry;
static legacy_f64 unit_recheck_lead;
static legacy_u64 unit_applied_worker_commands;
static legacy_u8 unit_current_from_worker;

legacy_int unit_opponent_is_selected(void)
{
	return (legacy_u8)gameconfig.game_opponenttype == CSRPL_UNIT_OPPONENT_TYPE;
}

void unit_opponent_stop(void)
{
	unit_opponent_pilot_input_override(0, 0);
	unit_pilot_stop();
	unit_planner_destroy(unit_planner);
	unit_planner = NULL;
	unit_route_free(&unit_route);
	unit_last_frame = 0;
	unit_preparation = UNIT_PREPARATION_NONE;
	unit_recheck_piece = ROUTE_INDEX_NONE;
	unit_recheck_settled = unit_recheck_pending = 0;
	unit_recheck_retry = 0;
	unit_recheck_lead = 0.0;
	unit_applied_worker_commands = 0;
	unit_current_from_worker = 0;
	unit_profiles_reset();
}

legacy_int unit_opponent_prepare_begin_with_workers(legacy_s32 requested_workers)
{
	unit_opponent_stop();
	if (!unit_opponent_is_selected() || !unit_route_build(&unit_route)) {
		return 0;
	}
	owoot_load_opponent_wheels();
	unit_planner = unit_planner_create(unit_route.points, unit_route.count, &unit_route.limits,
									   owoot_enabled != 0, requested_workers);
	unit_preparation = unit_planner ? UNIT_PREPARATION_PLANNING : UNIT_PREPARATION_FAILED;
	return unit_planner != NULL;
}

legacy_int unit_opponent_prepare_begin(void)
{
	return unit_opponent_prepare_begin_with_workers(0);
}

void unit_opponent_prepare_work(void)
{
	if (!unit_planner || unit_preparation == UNIT_PREPARATION_FAILED) {
		return;
	}
	unit_planner_work(unit_planner, UNIT_PREPARATION_WORK_UNITS);
	if (unit_preparation != UNIT_PREPARATION_PLANNING) {
		unit_pilot_work(UNIT_PREPARATION_NATIVE_TICKS);
		return;
	}
	struct UNIT_PLANNER_STATS stats;
	unit_planner_stats(unit_planner, &stats);
	if (!stats.route_ready) {
		return;
	}
	/* The first search covers the complete checkpoint-aware lap. Native
	 * previews then refine the same route throughout the remaining wait. */
	if (unit_pilot_begin(unit_planner)) {
		unit_preparation = UNIT_PREPARATION_COMPLETE;
	} else {
		unit_preparation = UNIT_PREPARATION_FAILED;
	}
}

legacy_int unit_opponent_prepare_end(void)
{
	if (!unit_planner || unit_preparation != UNIT_PREPARATION_COMPLETE) {
		unit_opponent_stop();
		return 0;
	}
	struct UNIT_PLANNER *validated = unit_pilot_finish();
	if (validated) {
		(void)unit_planner_adopt_profiles(unit_planner, validated);
	}
	unit_planner_finish_startup(unit_planner);
	return 1;
}

static void unit_vehicle_snapshot(const struct CARSTATE *car, const struct SIMD *simd,
								  struct UNIT_VEHICLE *vehicle)
{
	memset(vehicle, 0, sizeof(*vehicle));
	vehicle->x = car->car_position.lx / UNIT_POSITION_SCALE;
	vehicle->y = car->car_position.ly / UNIT_POSITION_SCALE;
	vehicle->z = car->car_position.lz / UNIT_POSITION_SCALE;
	vehicle->speed = car->car_actual_speed / UNIT_SPEED_SCALE;
	vehicle->airborne = car->car_sumSurfAllWheels == CAR_WHEEL_CONTACT_NONE;
	vehicle->vy =
		(car->car_position.ly - car->car_previous_position.ly) / UNIT_POSITION_SCALE * framespersec;
	if (car == &state.opponentstate) {
		vehicle->recovering = extended_unit_penalty_recovery_required() != 0;
		vehicle->shortcuts_forbidden = !extended_unit_penalty_shortcuts_allowed();
	}
	legacy_u16 car_index = car == &state.opponentstate ? OPPONENT_CAR_INDEX : PLAYER_CAR_INDEX;
	vehicle->steering = car->car_steeringAngle * UNIT_ANGLE_RADIANS / UNIT_FRONT_STEERING_SCALE;
	legacy_s16 first = player_steering_next_angle(
		car, INPUT_STEER_RIGHT_FLAG >> INPUT_STEERING_SHIFT, car_steering_rate[car_index]);
	legacy_s16 second = player_steering_next_angle(
		car, INPUT_STEER_LEFT_FLAG >> INPUT_STEERING_SHIFT, car_steering_rate[car_index]);
	legacy_f64 first_delta = fabs((legacy_f64)first - car->car_steeringAngle);
	legacy_f64 second_delta = fabs((legacy_f64)second - car->car_steeringAngle);
	/* Native reversal recenters four times faster than steering farther into
	 * a corner. Publish the slower base rate, ignoring the clipped endpoint. */
	legacy_f64 response = first_delta > 0.0 && second_delta > 0.0
							  ? fmin(first_delta, second_delta)
							  : fmax(first_delta, second_delta) / 4.0;
	vehicle->steering_rate =
		response * framespersec * UNIT_ANGLE_RADIANS / UNIT_FRONT_STEERING_SCALE;
	vehicle->vx =
		(car->car_position.lx - car->car_previous_position.lx) / UNIT_POSITION_SCALE * framespersec;
	vehicle->vz =
		(car->car_position.lz - car->car_previous_position.lz) / UNIT_POSITION_SCALE * framespersec;
	struct VECTOR forward = {0, 0, UNIT_HEADING_VECTOR_LENGTH};
	struct VECTOR world;
	struct MATRIX rotation = *mat_rot_zxy(-car->car_rotate.z, -car->car_rotate.y,
										  -car->car_rotate.x, MATRIX_ROTATION_ORDER_ZXY);
	mat_mul_vector(&forward, &rotation, &world);
	vehicle->yaw = atan2(world.x, world.z);
	vehicle->half_width = fabs((legacy_f64)simd->collide_points[0].px);
	vehicle->half_length = fabs((legacy_f64)simd->collide_points[1].px);
	for (legacy_u16 index = 0; index < CARSTATE_WHEEL_COUNT; index++) {
		vehicle->half_width =
			fmax(vehicle->half_width, fabs(simd->wheel_coords[index].x / UNIT_POSITION_SCALE));
		vehicle->half_length =
			fmax(vehicle->half_length, fabs(simd->wheel_coords[index].z / UNIT_POSITION_SCALE));
	}
}

static legacy_s16 unit_steering(const struct UNIT_PLAN *plan, legacy_int axis_guidance)
{
	const struct CARSTATE *car = &state.opponentstate;
	struct VECTOR relative;
	relative.x =
		(legacy_s16)unit_bounded(plan->target_x - car->car_position.lx / UNIT_POSITION_SCALE,
								 -(legacy_f64)LEGACY_S16_MAX, LEGACY_S16_MAX);
	relative.y =
		(legacy_s16)unit_bounded(plan->target_y - car->car_position.ly / UNIT_POSITION_SCALE,
								 -(legacy_f64)LEGACY_S16_MAX, LEGACY_S16_MAX);
	relative.z =
		(legacy_s16)unit_bounded(plan->target_z - car->car_position.lz / UNIT_POSITION_SCALE,
								 -(legacy_f64)LEGACY_S16_MAX, LEGACY_S16_MAX);
	if (plan->route_cursor < unit_route.count &&
		unit_route.points[plan->route_cursor].height_unspecified) {
		relative.y = 0;
	}
	struct VECTOR local;
	struct MATRIX rotation = *mat_rot_zxy(car->car_rotate.z, car->car_rotate.y, car->car_rotate.x,
										  MATRIX_ROTATION_ORDER_YXZ);
	mat_mul_vector(&relative, &rotation, &local);
	legacy_f64 distance_squared = (legacy_f64)local.x * local.x + (legacy_f64)local.z * local.z;
	legacy_f64 curvature = 2.0 * local.x / fmax(distance_squared, UNIT_MINIMUM_LOOKAHEAD_SQUARED);
	if (axis_guidance) {
		struct VECTOR forward = {0, 0, UNIT_HEADING_VECTOR_LENGTH};
		struct VECTOR sideways = {UNIT_HEADING_VECTOR_LENGTH, 0, 0}, world_forward, world_side;
		rotation = *mat_rot_zxy(-car->car_rotate.z, -car->car_rotate.y, -car->car_rotate.x,
								MATRIX_ROTATION_ORDER_ZXY);
		mat_mul_vector(&forward, &rotation, &world_forward);
		mat_mul_vector(&sideways, &rotation, &world_side);
		legacy_f64 fx = world_forward.x / (legacy_f64)UNIT_HEADING_VECTOR_LENGTH;
		legacy_f64 fz = world_forward.z / (legacy_f64)UNIT_HEADING_VECTOR_LENGTH;
		legacy_f64 determinant =
			(fz * world_side.x - fx * world_side.z) / UNIT_HEADING_VECTOR_LENGTH;
		legacy_f64 horizontal = hypot(fx, fz);
		legacy_f64 authority = horizontal > 0.0 ? determinant / (horizontal * horizontal) : 0.0;
		curvature = 0.0;
		if (fabs(authority) >= UNIT_BARREL_MINIMUM_STEERING_AUTHORITY) {
			legacy_f64 lateral = (relative.x * fz - relative.z * fx) / horizontal;
			legacy_f64 distance =
				(legacy_f64)relative.x * relative.x + (legacy_f64)relative.z * relative.z;
			/* Wheel travel follows the banked body. Account for native yaw
			 * authority and horizontal travel per unit of body travel. */
			curvature = 2.0 * lateral / fmax(distance, UNIT_MINIMUM_LOOKAHEAD_SQUARED) *
						horizontal / authority;
		}
	}
	legacy_f64 wanted =
		atan(unit_route.wheelbase * curvature) * UNIT_FRONT_STEERING_SCALE / UNIT_ANGLE_RADIANS;
	/* Analog precision with the same maximum steering response as a player.
	 * This prevents a planner update from snapping the front wheels instantly. */
	legacy_s16 first = player_steering_next_angle(
		car, INPUT_STEER_RIGHT_FLAG >> INPUT_STEERING_SHIFT, car_steering_rate[OPPONENT_CAR_INDEX]);
	legacy_s16 second = player_steering_next_angle(
		car, INPUT_STEER_LEFT_FLAG >> INPUT_STEERING_SHIFT, car_steering_rate[OPPONENT_CAR_INDEX]);
	legacy_f64 minimum = fmin(first, second), maximum = fmax(first, second);
	return (legacy_s16)unit_bounded(wanted, fmax(minimum, -UNIT_STEERING_MAXIMUM),
									fmin(maximum, UNIT_STEERING_MAXIMUM));
}

static legacy_int unit_collision_brake(const struct UNIT_VEHICLE *unit,
									   const struct UNIT_VEHICLE *player)
{
	if (fabs(unit->y - player->y) > UNIT_COLLISION_HEIGHT_CLEARANCE) {
		return 0;
	}
	legacy_f64 dx = player->x - unit->x, dz = player->z - unit->z;
	legacy_f64 sine = sin(unit->yaw), cosine = cos(unit->yaw);
	legacy_f64 ahead = dx * sine + dz * cosine;
	legacy_f64 lateral = dx * cosine - dz * sine;
	legacy_f64 relative_yaw = player->yaw - unit->yaw;
	legacy_f64 player_length = fabs(cos(relative_yaw)) * player->half_length +
							   fabs(sin(relative_yaw)) * player->half_width;
	legacy_f64 player_width = fabs(cos(relative_yaw)) * player->half_width +
							  fabs(sin(relative_yaw)) * player->half_length;
	legacy_f64 extent = unit->half_length + player_length + UNIT_COLLISION_MARGIN;
	legacy_f64 width = unit->half_width + player_width + UNIT_COLLISION_MARGIN;
	legacy_f64 closing = (unit->vx - player->vx) * sine + (unit->vz - player->vz) * cosine;
	legacy_f64 stopping =
		closing > 0 ? closing * UNIT_COLLISION_REACTION_SECONDS +
						  closing * closing / (2.0 * unit_route.limits.braking * UNIT_WORLD_PER_MPH)
					: 0;
	return ahead > 0 && ahead < extent + stopping && fabs(lateral) < width;
}

static legacy_int unit_landing_brake(legacy_u32 cursor, const struct UNIT_VEHICLE *unit,
									 const struct UNIT_VEHICLE *player)
{
	if (unit->airborne || cursor >= unit_route.count) {
		return 0;
	}
	legacy_u32 first = cursor;
	while (first &&
		   unit_route.points[first - 1U].route_index == unit_route.points[first].route_index) {
		first--;
	}
	legacy_f64 approach = 0.0;
	for (; first + 1U < unit_route.count && approach < 2.0 * TRACK_TILE_SIZE; first++) {
		const struct UNIT_ROUTE_POINT *entry = &unit_route.points[first];
		if (entry->stunt != UNIT_STUNT_BARREL && entry->stunt != UNIT_STUNT_RAMP) {
			approach += hypot(unit_route.points[first + 1U].x - entry->x,
							  unit_route.points[first + 1U].z - entry->z);
			continue;
		}
		legacy_u32 last = first;
		while (last + 1U < unit_route.count &&
			   unit_route.points[last + 1U].route_index == entry->route_index) {
			last++;
		}
		if (last + 2U >= unit_route.count) {
			return 0;
		}
		const struct UNIT_ROUTE_POINT *lip = &unit_route.points[last];
		/* Once committed, the only physical path is the jump. The forecast is
		 * used while the car can still wait on its approach. */
		if ((unit->x - entry->x) * (lip->x - entry->x) +
				(unit->z - entry->z) * (lip->z - entry->z) >=
			0.0) {
			return 0;
		}
		const struct UNIT_ROUTE_POINT *landing = &unit_route.points[last + 1U];
		legacy_f64 speed = fmax(UNIT_MINIMUM_MOVING_SPEED,
								(unit->speed + fmax(entry->speed_floor, entry->speed_limit)) * 0.5);
		legacy_f64 time =
			hypot(landing->x - unit->x, landing->z - unit->z) / (speed * UNIT_WORLD_PER_MPH);
		legacy_f64 predicted_x = player->x + player->vx * time;
		legacy_f64 predicted_z = player->z + player->vz * time;
		legacy_f64 minimum_y = player->y, maximum_y = player->y;
		if (player->airborne) {
			legacy_f64 predicted_y =
				player->y + player->vy * time - 0.5 * UNIT_LANDING_GRAVITY * time * time;
			minimum_y = fmin(minimum_y, predicted_y);
			maximum_y = fmax(maximum_y, predicted_y);
			if (player->vy > 0.0) {
				maximum_y = fmax(maximum_y, player->y + player->vy * player->vy /
															(2.0 * UNIT_LANDING_GRAVITY));
			}
		}
		legacy_f64 clearance = hypot(unit->half_width, unit->half_length) +
							   hypot(player->half_width, player->half_length) +
							   UNIT_COLLISION_MARGIN;
		legacy_f64 corridor = 0.0;
		for (legacy_u32 point = last + 1U;
			 point + 1U < unit_route.count && corridor < 2.0 * TRACK_TILE_SIZE; point++) {
			const struct UNIT_ROUTE_POINT *a = &unit_route.points[point];
			const struct UNIT_ROUTE_POINT *b = &unit_route.points[point + 1U];
			legacy_f64 dx = b->x - a->x, dz = b->z - a->z;
			legacy_f64 squared = dx * dx + dz * dz;
			legacy_f64 fraction =
				squared > 1.0
					? unit_bounded(((predicted_x - a->x) * dx + (predicted_z - a->z) * dz) /
									   squared,
								   0.0, 1.0)
					: 0.0;
			legacy_f64 y = a->y + (b->y - a->y) * fraction;
			if (hypot(predicted_x - a->x - dx * fraction, predicted_z - a->z - dz * fraction) <
					clearance &&
				y > minimum_y - UNIT_COLLISION_HEIGHT_CLEARANCE &&
				y < maximum_y + UNIT_COLLISION_HEIGHT_CLEARANCE) {
				return 1;
			}
			corridor += sqrt(squared);
		}
		return 0;
	}
	return 0;
}

static legacy_int unit_make_command(struct UNIT_PLANNER *planner, legacy_u32 frame,
									legacy_s16 override_piece, legacy_f64 override_speed,
									struct CSRPL_OPPONENT_COMMAND *command)
{
	struct CARSTATE *car = &state.opponentstate;
	legacy_u32 cursor = unit_route_cursor(&unit_route, car);
	cursor = unit_route_advance(&unit_route, car, cursor);
	unit_route_store(&unit_route, cursor, car);
	struct UNIT_VEHICLE unit, player;
	unit_vehicle_snapshot(car, &simd_opponent, &unit);
	unit_vehicle_snapshot(&state.playerstate, &simd_player, &player);
	struct UNIT_PLAN plan;
	memset(&plan, 0, sizeof(plan));
	unit_planner_plan(planner, &unit, &player, cursor, frame, &plan);
	if (planner == unit_planner) {
		unit_current_from_worker = plan.from_worker;
	}
	command->flags = 0;
	command->input = INPUT_BRAKE_FLAG;
	command->route_index = car->car_route_index;
	command->route_point = (legacy_u8)car->car_route_point_index;
	command->steering = car->car_steeringAngle;
	if (!plan.valid || car->car_crashBmpFlag != CRASH_EVENT_NONE) {
		return 1;
	}
	if (unit_checkpoint_guide(&unit_route, cursor, car, &plan) &&
		plan.brake == UNIT_PLAN_BRAKE_STOP) {
		plan.brake = UNIT_PLAN_BRAKE_NONE;
		plan.target_speed = fmax(plan.target_speed, UNIT_CHECKPOINT_RECOVERY_SPEED);
	}
	struct UNIT_LAUNCH_POLICY policy;
	unit_profiles_policy(planner != unit_planner, override_piece, override_speed, &policy);
	policy.preserve_approach = hypot(unit.x - player.x, unit.z - player.z) < TRACK_TILE_SIZE &&
							   fabs(unit.y - player.y) < UNIT_COLLISION_HEIGHT_CLEARANCE;
	legacy_int launch = unit_launch_guidance_policy(&unit_route, cursor, &plan, &policy);
	command->steering =
		launch == UNIT_LAUNCH_NEUTRAL
			? player_steering_next_angle(car, INPUT_NONE, car_steering_rate[OPPONENT_CAR_INDEX])
			: unit_steering(&plan, launch == UNIT_LAUNCH_AXIS);
	legacy_f64 target_speed = plan.target_speed;
	legacy_f64 grip = car->car_surfacegrip_sum;
	legacy_f64 demanded_angle =
		fabs((legacy_f64)command->steering + car->car_velocity_heading_offset);
	if (grip > 0 && demanded_angle >= 8.0 && car->car_sumSurfAllWheels &&
		unit_route.points[cursor].stunt == UNIT_STUNT_NONE && launch == UNIT_LAUNCH_NONE) {
		/* Match ordinary-road grip before the next tick. A rolling stunt's
		 * steering angle is not planar curvature: its sustaining speed comes
		 * from the protected/native-validated maneuver instead. */
		legacy_f64 corner_speed =
			sqrt(grip * UNIT_PLAYER_STYLE_GRIP_SCALE / demanded_angle) * UNIT_STEERING_GRIP_RESERVE;
		target_speed = fmin(target_speed, corner_speed);
	}
	legacy_int collision = plan.brake == UNIT_PLAN_BRAKE_COLLISION ||
						   unit_collision_brake(&unit, &player) ||
						   unit_landing_brake(cursor, &unit, &player);
	command->input = INPUT_NONE;
	if (plan.brake || collision || unit.speed > target_speed + UNIT_SPEED_HYSTERESIS) {
		command->input = INPUT_BRAKE_FLAG;
	} else if (unit.speed < target_speed - UNIT_SPEED_HYSTERESIS ||
			   unit.speed < UNIT_MINIMUM_MOVING_SPEED) {
		command->input = INPUT_ACCELERATE_FLAG;
	}
	command->input |= unit_controls_manual_shift(car, &simd_opponent);
	return collision;
}

static legacy_int unit_apply_provenance(const struct CSRPL_OPPONENT_COMMAND *command)
{
	struct CARSTATE *car = &state.opponentstate;
	legacy_u32 cursor = unit_route_advance(&unit_route, car, unit_route_cursor(&unit_route, car));
	const struct UNIT_ROUTE_POINT *point = &unit_route.points[cursor];
	/* Recorded controls are choices, not authority to teleport route progress.
	 * Recompute physical gate crossing before accepting the recorded provenance. */
	if (point->route_index != command->route_index || point->route_point != command->route_point) {
		return 0;
	}
	unit_route_store(&unit_route, cursor, car);
	return 1;
}

static void unit_apply_physics(const struct CSRPL_OPPONENT_COMMAND *command)
{
	struct CARSTATE *car = &state.opponentstate;
	car->car_sound_flags = car->car_actual_speed || car->car_crashBmpFlag == CRASH_EVENT_NONE
							   ? CAR_SOUND_ENGINE_ACTIVE_FLAG
							   : CAR_SOUND_NONE;
	state.game_opponent_route_indicator = ROUTE_INDICATOR_NONE;
	car->car_steeringAngle = command->steering;
	legacy_s8 input = car->car_crashBmpFlag == CRASH_EVENT_NONE ? (legacy_s8)command->input
																: (legacy_s8)INPUT_BRAKE_FLAG;
	/* The speed routine uses this selector only for the old opponents' reduced
	 * engine torque and doubled brakes. UNIT uses the car's ordinary values. */
	update_car_speed(input, PLAYER_CAR_INDEX, car, &simd_opponent);
	legacy_execution_residue.grip_stack_words[LEGACY_RESIDUE_FIRST_WORD] = car->car_lastrpm;
	legacy_execution_residue.grip_stack_words[LEGACY_RESIDUE_SECOND_WORD] =
		(legacy_s16)car->car_rev_speed;
	legacy_execution_residue.grip_stack_words[LEGACY_RESIDUE_THIRD_WORD] =
		(legacy_s16)car->car_gearratio;
	legacy_u16 rev_speed = car->car_rev_speed, actual_speed = car->car_actual_speed;
	update_grip(car, &simd_opponent, GRIP_BEHAVIOR_PLAYER);
	update_legacy_grip_stack_words(car, &simd_opponent, rev_speed, actual_speed,
								   LEGACY_DEFAULT_PLAYER_TICK_SI);
	update_player_state(car, &simd_opponent, &state.playerstate, &simd_player, OPPONENT_CAR_INDEX);
	if (car->car_crashBmpFlag != CRASH_EVENT_FINISH) {
		extended_unit_penalty_update();
		if (unit_route_finished(&unit_route, car)) {
			update_crash_state(CRASH_EVENT_FINISH, OPPONENT_CAR_INDEX);
		}
	}
}

legacy_u64 unit_opponent_worker_commands(void)
{
	return unit_applied_worker_commands;
}

void unit_opponent_planner_stats(struct UNIT_PLANNER_STATS *stats)
{
	unit_planner_stats(unit_planner, stats);
}

const struct UNIT_ROUTE *unit_opponent_pilot_route(void)
{
	return unit_route.count && unit_route.finish_begin < unit_route.count ? &unit_route : NULL;
}

struct UNIT_ROUTE *unit_opponent_pilot_route_mutable(void)
{
	return &unit_route;
}

void unit_opponent_pilot_last_command(struct CSRPL_OPPONENT_COMMAND *command)
{
	if (command) {
		*command = unit_last_preview_command;
	}
}

static void unit_hold_command(struct CSRPL_OPPONENT_COMMAND *command)
{
	const struct CARSTATE *car = &state.opponentstate;
	legacy_u32 cursor = unit_route_advance(&unit_route, car, unit_route_cursor(&unit_route, car));
	command->flags = 0;
	command->input = INPUT_BRAKE_FLAG | unit_controls_manual_shift(car, &simd_opponent);
	command->steering = car->car_steeringAngle;
	command->route_index = unit_route.points[cursor].route_index;
	command->route_point = unit_route.points[cursor].route_point;
}

legacy_s16 unit_opponent_pilot_wait_step(legacy_u32 frame)
{
	unit_opponent_pilot_input_override(0, 0);
	(void)frame;
	if (!unit_opponent_pilot_route()) {
		return -1;
	}
	struct CSRPL_OPPONENT_COMMAND command;
	unit_hold_command(&command);
	unit_last_preview_command = command;
	if (!unit_apply_provenance(&command)) {
		return -1;
	}
	unit_apply_physics(&command);
	return state.opponentstate.car_crashBmpFlag;
}

legacy_s16 unit_opponent_pilot_step(struct UNIT_PLANNER *planner, legacy_u32 frame,
									legacy_s16 override_piece, legacy_f64 override_speed)
{
	if (!planner || !unit_opponent_pilot_route()) {
		unit_opponent_pilot_input_override(0, 0);
		return -1;
	}
	struct CSRPL_OPPONENT_COMMAND command;
	legacy_int collision =
		unit_make_command(planner, frame, override_piece, override_speed, &command);
	unit_pilot_controls_apply(&state.opponentstate, car_steering_rate[OPPONENT_CAR_INDEX],
							  collision, &command);
	unit_last_preview_command = command;
	if (!unit_apply_provenance(&command)) {
		return -1;
	}
	unit_apply_physics(&command);
	return state.opponentstate.car_crashBmpFlag;
}

/* Validate the cached native path against the current human, independently
 * of the different evasive trajectory a fresh planner proposal may choose. */
static legacy_int unit_cached_path_blocked(const struct UNIT_VEHICLE *unit,
										   const struct UNIT_VEHICLE *player)
{
	legacy_f64 horizon =
		UNIT_COLLISION_REACTION_SECONDS + unit->speed / fmax(unit_route.limits.braking, 1.0);
	legacy_u32 ticks = (legacy_u32)ceil(horizon * framespersec) + 1U;
	if (ticks > 160U) {
		ticks = 160U;
	}
	struct UNIT_VEHICLE previous = *unit, player_start = *player;
	legacy_f64 step = 1.0 / framespersec;
	for (legacy_u32 index = 1; index <= ticks; index++) {
		struct CARSTATE expected;
		if (!unit_pilot_future_car(index, &expected)) {
			struct UNIT_PILOT_STATS stats;
			unit_pilot_stats(&stats);
			/* A failed preview proves no safe continuation beyond this prefix.
			 * Replan while braking distance remains, before its last safe tick. */
			return index == 1U || (stats.exhausted && !stats.completed_lap);
		}
		struct UNIT_VEHICLE next;
		unit_vehicle_snapshot(&expected, &simd_opponent, &next);
		legacy_f64 time = index * step;
		legacy_f64 player_y = player->y + player->vy * time;
		if (player->airborne) {
			player_y -= UNIT_LANDING_GRAVITY * time * time * 0.5;
		}
		if (fmax(previous.y, next.y) >=
				fmin(player_start.y, player_y) - UNIT_COLLISION_HEIGHT_CLEARANCE &&
			fmin(previous.y, next.y) <=
				fmax(player_start.y, player_y) + UNIT_COLLISION_HEIGHT_CLEARANCE &&
			unit_collision_swept_boxes(&previous, &next, &player_start, step,
									   UNIT_COLLISION_MARGIN)) {
			return 1;
		}
		previous = next;
		player_start.x = player->x + player->vx * time;
		player_start.z = player->z + player->vz * time;
		player_start.y = player_y;
	}
	return 0;
}

static void unit_cancel_recheck(void)
{
	if (unit_recheck_piece != ROUTE_INDEX_NONE || unit_recheck_pending) {
		unit_pilot_recheck_cancel();
		unit_recheck_pending = unit_recheck_settled = 0;
		unit_recheck_piece = ROUTE_INDEX_NONE;
		unit_recheck_retry = 0;
		unit_recheck_lead = 0.0;
		(void)unit_planner_resume_live(unit_planner);
	}
}

/* Verified commands remain a plan, not replay authority: route progress is
 * rechecked below and the current human can invalidate a prepared maneuver. */
static legacy_int unit_verified_controls(legacy_u32 frame, struct CSRPL_OPPONENT_COMMAND *command)
{
	struct CARSTATE *car = &state.opponentstate;
	legacy_u32 cursor = unit_route_advance(&unit_route, car, unit_route_cursor(&unit_route, car));
	struct UNIT_VEHICLE unit, player;
	unit_vehicle_snapshot(car, &simd_opponent, &unit);
	unit_vehicle_snapshot(&state.playerstate, &simd_player, &player);
	legacy_int nearby = hypot(unit.x - player.x, unit.z - player.z) < TRACK_TILE_SIZE &&
						fabs(unit.y - player.y) < UNIT_COLLISION_HEIGHT_CLEARANCE;
	legacy_int continuing = unit_pilot_continuation_pending();
	if (unit_pilot_command(car, frame, command)) {
		/* The first exact live recheck handoff can arm continuation here. */
		continuing = unit_pilot_continuation_pending();
		unit_recheck_pending = unit_recheck_settled = 0;
		unit_recheck_piece = ROUTE_INDEX_NONE;
		unit_recheck_lead = 0.0;
		if (continuing) {
			(void)unit_planner_pause_live(unit_planner);
			unit_pilot_continue_work(UNIT_PILOT_CONTINUATION_TICKS);
			if (!unit_pilot_continuation_pending()) {
				(void)unit_planner_resume_live(unit_planner);
			}
		} else {
			(void)unit_planner_resume_live(unit_planner);
		}
		/* Collision forecasts stay fresh while verified controls are used.
		 * Native continuation and the background worker never run together. */
		struct UNIT_PLAN current = {0};
		unit_planner_plan(unit_planner, &unit, &player, cursor, frame, &current);
		if (current.brake == UNIT_PLAN_BRAKE_COLLISION || unit_collision_brake(&unit, &player) ||
			unit_landing_brake(cursor, &unit, &player) ||
			unit_cached_path_blocked(&unit, &player)) {
			unit_pilot_continuation_cancel();
			(void)unit_planner_resume_live(unit_planner);
			unit_make_command(unit_planner, frame, ROUTE_INDEX_NONE, 0.0, command);
		}
		return 1;
	}
	if (continuing) {
		unit_pilot_continuation_cancel();
		(void)unit_planner_resume_live(unit_planner);
	}
	/* Preserve the live evasive maneuver while the human is near. */
	if (nearby) {
		unit_cancel_recheck();
		return 0;
	}
	/* A pending exact hold forecast includes transient suspension contacts.
	 * Require road support to begin it; do not restart its fixed deadline when
	 * the predicted stopped car briefly puts one wheel beyond the road edge. */
	for (legacy_u16 wheel = 0; !unit_recheck_pending && wheel < CARSTATE_WHEEL_COUNT; wheel++) {
		if (car->car_surfaceWhl[wheel] < CAR_SURFACE_PAVED ||
			car->car_surfaceWhl[wheel] > CAR_SURFACE_ICE) {
			unit_cancel_recheck();
			return 0;
		}
	}
	legacy_s16 piece;
	legacy_f64 distance;
	legacy_f64 stopping =
		unit.speed * unit.speed * UNIT_WORLD_PER_MPH /
		(2.0 * fmax(unit_route.limits.braking * UNIT_RECHECK_BRAKING_RESERVE, 1.0));
	stopping = fmin(stopping + UNIT_RECHECK_STOP_MARGIN_TILES * TRACK_TILE_SIZE,
					UNIT_RECHECK_MAXIMUM_LEAD_TILES * TRACK_TILE_SIZE);
	/* Retain the original stopping allowance until this approach ends. Shrinking
	 * it as the car slows would discard the target before the fixed handoff. */
	legacy_f64 lead = fmax(stopping, unit_recheck_lead);
	if (!unit_pilot_next_stunt_with_lead(car, &piece, &distance, lead) ||
		distance <= 2.0 * unit.half_length || unit.airborne) {
		unit_cancel_recheck();
		return 0;
	}
	if (unit_recheck_piece != piece) {
		unit_recheck_piece = piece;
		unit_recheck_pending = unit_recheck_settled = 0;
		unit_recheck_retry = 0;
		unit_recheck_lead = stopping;
		/* Rebuild from this exact rolling state while ordinary road remains
		 * ahead. Its verified prefix gives native search time to check the
		 * stunt without stopping before an unrelated corner or checkpoint. */
		(void)unit_planner_pause_live(unit_planner);
		if (!owoot_enabled && unit_pilot_recheck_rolling_begin(unit_planner, frame, piece)) {
			unit_pilot_work(UNIT_PREPARATION_NATIVE_TICKS);
			if (unit_pilot_recheck_status(frame) > 0 && unit_pilot_command(car, frame, command)) {
				unit_recheck_piece = ROUTE_INDEX_NONE;
				unit_recheck_lead = 0.0;
				if (unit_pilot_continuation_pending()) {
					unit_pilot_continue_work(UNIT_PILOT_CONTINUATION_TICKS);
				}
				if (!unit_pilot_continuation_pending()) {
					(void)unit_planner_resume_live(unit_planner);
				}
				if (unit_collision_brake(&unit, &player) ||
					unit_landing_brake(cursor, &unit, &player) ||
					unit_cached_path_blocked(&unit, &player)) {
					unit_pilot_continuation_cancel();
					(void)unit_planner_resume_live(unit_planner);
					unit_make_command(unit_planner, frame, ROUTE_INDEX_NONE, 0.0, command);
				}
				return 1;
			}
			unit_pilot_recheck_cancel();
		}
		(void)unit_planner_resume_live(unit_planner);
	}
	if (unit_recheck_pending) {
		legacy_u64 begin = SDL_GetTicksNS();
		legacy_int result = 0;
		for (legacy_u32 chunk = 0; chunk < UNIT_RECHECK_CHUNKS; chunk++) {
			unit_pilot_work(UNIT_PREPARATION_NATIVE_TICKS);
			result = unit_pilot_recheck_status(frame);
			if (result != 0 || SDL_GetTicksNS() - begin >= UNIT_RECHECK_TIME_NS) {
				break;
			}
		}
		if (result > 0 && unit_pilot_command(car, frame, command)) {
			unit_recheck_pending = unit_recheck_settled = 0;
			if (unit_pilot_continuation_pending()) {
				(void)unit_planner_pause_live(unit_planner);
			} else {
				(void)unit_planner_resume_live(unit_planner);
			}
			if (unit_collision_brake(&unit, &player) ||
				unit_landing_brake(cursor, &unit, &player) ||
				unit_cached_path_blocked(&unit, &player)) {
				unit_pilot_continuation_cancel();
				(void)unit_planner_resume_live(unit_planner);
				unit_make_command(unit_planner, frame, ROUTE_INDEX_NONE, 0.0, command);
			}
			return 1;
		}
		if (result != 0) {
			unit_recheck_pending = unit_recheck_settled = 0;
			unit_recheck_retry = frame + UNIT_RECHECK_RETRY_TICKS;
		}
	} else {
		/* The exact future hold forecast includes native suspension/rounding
		 * drift. A stopped drivetrain need not have identical world positions. */
		legacy_int stable = car->car_actual_speed == CAR_SPEED_STOPPED &&
							car->car_rev_speed == CAR_SPEED_STOPPED && !car->car_changing_gear &&
							car->car_current_gear <= UNIT_FIRST_GEAR;
		unit_recheck_settled = stable ? unit_recheck_settled + 1U : 0;
		if (unit_recheck_settled >= UNIT_RECHECK_SETTLED_TICKS && frame >= unit_recheck_retry) {
			(void)unit_planner_pause_live(unit_planner);
			unit_recheck_pending = unit_pilot_recheck_begin(unit_planner, frame, piece) != 0;
			unit_recheck_settled = 0;
			if (!unit_recheck_pending) {
				unit_recheck_retry = frame + UNIT_RECHECK_RETRY_TICKS;
			}
		}
	}
	if (!unit_recheck_pending && unit.speed > 0.0) {
		unit_make_command(unit_planner, frame, ROUTE_INDEX_NONE, 0.0, command);
	} else {
		unit_hold_command(command);
	}
	command->input = INPUT_BRAKE_FLAG | unit_controls_manual_shift(car, &simd_opponent);
	return 1;
}

void unit_opponent_tick(void)
{
	legacy_u32 frame =
		extended_replay_is_active() ? extended_replay_frame() : (legacy_u16)state.game_frame;
	if (frame) {
		frame--;
	}
	/* Rewind retains the immutable geometry, while each command restores route
	 * provenance in CARSTATE. Plans from the abandoned future are discarded. */
	if (frame < unit_last_frame && unit_planner) {
		unit_pilot_stop();
		unit_recheck_pending = unit_recheck_settled = 0;
		unit_recheck_piece = ROUTE_INDEX_NONE;
		unit_recheck_lead = 0.0;
		unit_planner_destroy(unit_planner);
		unit_planner = NULL;
	}
	unit_last_frame = frame;
	if (!unit_route.count) {
		if (!unit_route_build(&unit_route)) {
			fatal_error("UNIT: could not construct a complete route");
			return;
		}
		owoot_load_opponent_wheels();
	}
	struct CSRPL_OPPONENT_COMMAND command;
	unit_current_from_worker = 0;
	if (game_replay_mode == REPLAY_MODE_LIVE) {
		if (!unit_planner) {
			/* Headless runs and a replay branch have no preparation UI. Start a
			 * bounded guide immediately; never perform ten seconds in a tick. */
			unit_planner = unit_planner_create(unit_route.points, unit_route.count,
											   &unit_route.limits, owoot_enabled != 0, -1);
			unit_planner_finish_startup(unit_planner);
		}
		if (!unit_verified_controls(frame, &command)) {
			unit_make_command(unit_planner, frame, -1, 0.0, &command);
		}
		if (!extended_replay_record_opponent_command(frame, &command)) {
			fatal_error("UNIT: could not record opponent controls");
			return;
		}
	} else if (!extended_replay_opponent_command_at(frame, &command)) {
		fatal_error("UNIT: replay is missing opponent controls");
		return;
	}
	if (!unit_apply_provenance(&command)) {
		fatal_error("UNIT: invalid opponent route in replay");
		return;
	}
	if (unit_current_from_worker && state.opponentstate.car_crashBmpFlag == CRASH_EVENT_NONE) {
		unit_applied_worker_commands++;
	}
	unit_apply_physics(&command);
}
#endif
