#include "extended_search.h"
#include "search.h"
#include "../c/externs.h"
#include "../c/game_input.h"
#include "../c/track/track_objects.h"
#include "../c/extended/extended_owoot_internal.h"
#include "../c/extended/extended_owoot_route.h"
#include "../c/extended/roll_ramp.h"
#include "../c/extended/extended_barrel_search.h"
#include <stdlib.h>
#include <string.h>

#undef memcpy

legacy_int extended_genghost_search_seed_policy(const struct GENGHOST_ROUTE *route,
												legacy_u64 trials, struct GENGHOST_POLICY *policy)
{
	if (trials >= genghost_barrel_seed_count(route)) {
		return 0;
	}
	/* Tire-contact timing makes neighboring launch speeds physically distinct.
	 * Fund these complete pilots before repairing a failed coarse parent. */
	genghost_barrel_seed_policy(route, policy, (legacy_uint)trials);
	return 1;
}

legacy_int extended_genghost_search_native_proposals(const struct GENGHOST_ROUTE *route,
													 legacy_u64 trials)
{
	return trials >= genghost_barrel_seed_count(route);
}

#define BARREL_REPAIR_STALL_TICKS 400U
#define BARREL_REPAIR_POLL_TICKS 64U
#define BARREL_REPAIR_SURVIVAL_SECONDS 1U
#define BARREL_REPAIR_FULL_ROLL_SCORE 1000000.0
#define BARREL_REPAIR_SETTLED_SCORE 100000000.0
#define BARREL_REPAIR_LATERAL_WEIGHT 4.0
#define BARREL_REPAIR_HEIGHT_WEIGHT 4.0
#define BARREL_REPAIR_DISTANCE_WEIGHT 2.0
#define BARREL_REPAIR_SURVIVAL_WEIGHT 20.0
#define BARREL_REPAIR_PROGRESS_WEIGHT 0.01
#define BARREL_REPAIR_BODY_HEIGHT 8.0
#define BARREL_REPAIR_ANGLE_WEIGHT 32.0

extern legacy_f64 fabs(legacy_f64);

struct GENGHOST_BARREL_REPAIR {
	struct GENGHOST_SNAPSHOT snapshots[EXTENDED_BARREL_CHECKPOINTS];
	struct GENGHOST_DRIVER drivers[EXTENDED_BARREL_CHECKPOINTS];
	legacy_uint frames[EXTENDED_BARREL_CHECKPOINTS], span, bank;
	struct EXTENDED_BARREL_SEARCH search;
	legacy_u64 revision;
	legacy_int source, ready;
	size_t first, last;
};

static legacy_f64 barrel_clamp(legacy_f64 value, legacy_f64 minimum, legacy_f64 maximum)
{
	return value < minimum ? minimum : value > maximum ? maximum : value;
}

static legacy_int barrel_stopped(genghost_barrel_stop_function stop, void *opaque,
								 legacy_uint frame)
{
	return frame % BARREL_REPAIR_POLL_TICKS == 0U && stop && stop(opaque);
}

static legacy_int barrel_at_frontier(const struct GENGHOST_ROUTE *route, size_t first, size_t last,
									 legacy_f64 progress)
{
	legacy_f64 approach = 3.0 * CSTRK_TILE_WORLD_SIZE;
	legacy_f64 runout = (ROLL_RAMP_JUMP_MAX_EMPTY_TILES + 3.0) * CSTRK_TILE_WORLD_SIZE;
	return route->points[first].roll_ramp && last + 1U < route->point_count &&
		   route->points[first].distance <= progress + approach &&
		   progress <= route->points[last].distance + runout;
}

legacy_int extended_genghost_search_barrel_available(const struct GENGHOST_ROUTE *route,
													 legacy_f64 progress,
													 legacy_f64 protected_progress)
{
	for (size_t first = 0U; first < route->point_count;) {
		size_t last = first;
		while (last + 1U < route->point_count &&
			   route->points[last + 1U].piece == route->points[first].piece) {
			++last;
		}
		if (barrel_at_frontier(route, first, last, progress) &&
			route->points[last + 1U].distance > protected_progress) {
			return 1;
		}
		first = last + 1U;
	}
	return 0;
}

static legacy_int barrel_prepare(struct GENGHOST_BARREL_REPAIR *repair,
								 const struct GENGHOST_ROUTE *route,
								 const struct GENGHOST_OPTIONS *options, const legacy_u8 *parent,
								 legacy_uint length, const struct GENGHOST_POLICY *policy,
								 legacy_u64 revision, struct GENGHOST_SEARCH_RESULT *totals,
								 genghost_barrel_stop_function stop, void *opaque)
{
	if (repair->revision == revision) {
		return repair->ready;
	}
	repair->revision = revision;
	repair->ready = 0;
	struct GENGHOST_DRIVER driver;
	genghost_driver_reset(&driver);
	genghost_sim_reset();
	for (legacy_uint frame = 0U; frame < length; ++frame) {
		if (barrel_stopped(stop, opaque, frame)) {
			return 0;
		}
		genghost_driver_step(route, policy, &driver, options->powergear, options->collision,
							 options->owoot);
		legacy_int event = genghost_sim_step(parent[frame]);
		++totals->simulated_frames;
		genghost_driver_observe(route, &driver);
		if (event) {
			break;
		}
	}
	/* Repair only the barrel at the failed frontier. A completed early barrel
	 * must not attract every later candidate on a long multi-stunt course. */
	size_t selected = route->point_count;
	for (size_t first = 0U; first < route->point_count;) {
		size_t last = first;
		while (last + 1U < route->point_count &&
			   route->points[last + 1U].piece == route->points[first].piece) {
			++last;
		}
		if (barrel_at_frontier(route, first, last, driver.progress)) {
			selected = first;
			repair->last = last;
		}
		first = last + 1U;
	}
	if (selected == route->point_count) {
		return 0;
	}
	repair->first = selected;
	legacy_int source = route->points[selected].piece;
	if (repair->source != source) {
		repair->source = source;
		extended_barrel_search_clear(&repair->search);
	}
	/* Parameter ideas survive a better prefix; their old numerical fitness
	 * does not. Every new entry state is measured by native playback again. */
	extended_barrel_search_invalidate(&repair->search);
	genghost_driver_reset(&driver);
	genghost_sim_reset();
	legacy_uint checkpoint = 0U, entry_frame = 0U, airborne_frame = 0U, bank_frame = 0U;
	for (legacy_uint frame = 0U; frame < length; ++frame) {
		if (barrel_stopped(stop, opaque, frame)) {
			return 0;
		}
		while (checkpoint < EXTENDED_BARREL_CHECKPOINTS) {
			legacy_f64 lead = (legacy_f64)(EXTENDED_BARREL_CHECKPOINTS - checkpoint) *
							  CSTRK_TILE_WORLD_SIZE / EXTENDED_BARREL_CHECKPOINTS;
			if (driver.progress + lead < route->points[selected].distance) {
				break;
			}
			repair->frames[checkpoint] = frame;
			repair->drivers[checkpoint] = driver;
			genghost_sim_capture(&repair->snapshots[checkpoint++]);
		}
		genghost_driver_step(route, policy, &driver, options->powergear, options->collision,
							 options->owoot);
		if (!entry_frame && driver.barrel_waypoint == selected + 1U && driver.barrel_active >= 2U) {
			entry_frame = frame;
		}
		legacy_int event = genghost_sim_step(parent[frame]);
		++totals->simulated_frames;
		genghost_driver_observe(route, &driver);
		if (entry_frame && !bank_frame &&
			state.playerstate.car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_SOURCE] == source + 1) {
			bank_frame = frame + 1U;
		}
		if (entry_frame && !airborne_frame &&
			state.playerstate.car_sumSurfAllWheels == CAR_WHEEL_CONTACT_NONE &&
			driver.progress >= route->points[repair->last].distance) {
			airborne_frame = frame + 1U;
		}
		if (event) {
			break;
		}
	}
	if (!checkpoint || !entry_frame) {
		return 0;
	}
	while (checkpoint < EXTENDED_BARREL_CHECKPOINTS) {
		repair->frames[checkpoint] = repair->frames[checkpoint - 1U];
		repair->drivers[checkpoint] = repair->drivers[checkpoint - 1U];
		repair->snapshots[checkpoint] = repair->snapshots[checkpoint - 1U];
		++checkpoint;
	}
	legacy_uint minimum = genghost_sim_frame_rate();
	repair->span = airborne_frame > entry_frame ? airborne_frame - entry_frame : minimum * 3U;
	if (repair->span < minimum) {
		repair->span = minimum;
	}
	repair->bank = bank_frame > entry_frame ? bank_frame - entry_frame : repair->span / 2U;
	repair->ready = 1;
	return 1;
}

static legacy_int barrel_matches_source(const struct GENGHOST_BARREL_REPAIR *repair,
										const struct CARSTATE *car)
{
	legacy_s16 source = car->car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_SOURCE] - 1;
	if (source < 0 || source >= track_pieces_counter) {
		return 0;
	}
	/* Different compiled route nodes can refer to one placed object. Match
	 * that placement, rather than borrowing a neighboring ramp's milestones. */
	const struct CSTRK_OBJECT *actual = route_extended_object(source);
	return actual && actual == route_extended_object((legacy_s16)repair->source);
}

legacy_int extended_genghost_search_barrel_repair(
	struct GENGHOST_BARREL_REPAIR **repair_state, const struct GENGHOST_ROUTE *route,
	const struct GENGHOST_OPTIONS *options, const legacy_u8 *parent, legacy_uint parent_length,
	const struct GENGHOST_POLICY *parent_policy, legacy_u64 revision, legacy_u8 *inputs,
	struct GENGHOST_DRIVER *driver, struct GENGHOST_RESULT *race,
	struct GENGHOST_SEARCH_RESULT *totals, genghost_barrel_stop_function stop, void *opaque,
	legacy_uint *length, legacy_uint *protected_prefix, legacy_f64 *protected_progress)
{
	if (!*repair_state) {
		*repair_state = calloc(1U, sizeof(**repair_state));
		if (!*repair_state) {
			return -1;
		}
		(*repair_state)->source = -1;
		(*repair_state)->revision = ~(legacy_u64)0U;
		extended_barrel_search_init(&(*repair_state)->search, options->seed);
	}
	struct GENGHOST_BARREL_REPAIR *repair = *repair_state;
	if (!barrel_prepare(repair, route, options, parent, parent_length, parent_policy, revision,
						totals, stop, opaque)) {
		return 0;
	}
	struct EXTENDED_BARREL_PARAMETERS parameters = extended_barrel_search_next(
		&repair->search, repair->span, repair->bank, EXTENDED_BARREL_FIELDS_ALL, NULL);
	legacy_uint frame = repair->frames[parameters.checkpoint];
	memcpy(inputs, parent, frame);
	genghost_sim_restore(&repair->snapshots[parameters.checkpoint]);
	*driver = repair->drivers[parameters.checkpoint];
	struct GENGHOST_POLICY policy = *parent_policy;
	for (legacy_uint segment = 0U; segment < GENGHOST_POLICY_SEGMENTS; ++segment) {
		policy.segment[segment].speed_scale = parameters.speed;
		policy.segment[segment].lateral_offset = parameters.lateral;
		policy.segment[segment].shift_rpm_scale = parameters.shift;
	}
	const struct GENGHOST_WAYPOINT *entry = &route->points[repair->first];
	const struct GENGHOST_WAYPOINT *lip = &route->points[repair->last];
	const struct GENGHOST_WAYPOINT *receiver = &route->points[repair->last + 1U];
	legacy_f64 forward_x = (lip->x - entry->x) / ROLL_RAMP_LENGTH;
	legacy_f64 forward_z = (lip->z - entry->z) / ROLL_RAMP_LENGTH;
	legacy_f64 flight_distance =
		(receiver->x - lip->x) * forward_x + (receiver->z - lip->z) * forward_z;
	legacy_uint ticks = 0U, landing_frame = 0U, survived = 0U;
	legacy_int complete = 0, settled = 0;
	legacy_f64 landing_x = 0.0, landing_y = 0.0, landing_z = 0.0;
	legacy_s16 previous_roll = state.playerstate.car_rotate.z;
	legacy_s32 airborne_turn = 0, maximum_turn = 0;
	legacy_f64 furthest = driver->progress;
	legacy_uint last_progress_frame = frame;
	while (frame < genghost_sim_frame_limit() - genghost_sim_frame_rate()) {
		if (barrel_stopped(stop, opaque, frame)) {
			break;
		}
		const struct GENGHOST_POLICY *active_policy = landing_frame ? parent_policy : &policy;
		legacy_u8 input = genghost_driver_step(route, active_policy, driver, options->powergear,
											   options->collision, options->owoot);
		if (!landing_frame && driver->barrel_waypoint == repair->first + 1U &&
			driver->barrel_active >= 2U) {
			struct EXTENDED_BARREL_INPUT pattern =
				extended_barrel_search_input(&parameters, repair->span, ticks);
			input = (legacy_u8)((input & ~pattern.mask) | pattern.value);
			++ticks;
		}
		if (!landing_frame && parameters.air_brake && driver->barrel_active == 3U &&
			driver->barrel_waypoint == repair->first + 1U &&
			state.playerstate.car_sumSurfAllWheels == CAR_WHEEL_CONTACT_NONE) {
			legacy_f64 along = (state.playerstate.car_position.lx / 64.0 - lip->x) * forward_x +
							   (state.playerstate.car_position.lz / 64.0 - lip->z) * forward_z;
			legacy_f64 onset = parameters.air_brake == 1U	? 0.0
							   : parameters.air_brake == 2U ? flight_distance / 2.0
															: flight_distance * 2.0 / 3.0;
			if (along >= onset) {
				/* Native air braking leaves the ballistic trajectory intact but
				 * controls wheel/rev speed and its later touchdown response. */
				input = (legacy_u8)((input & ~INPUT_ACCELERATE_FLAG) | INPUT_BRAKE_FLAG);
			}
		}
		inputs[frame++] = input;
		legacy_int event = genghost_sim_step(input);
		++totals->simulated_frames;
		genghost_driver_observe(route, driver);
		const struct CARSTATE *car = &state.playerstate;
		legacy_s16 delta =
			(legacy_s16)(((legacy_u16)(car->car_rotate.z - previous_roll + ANGLE_HALF_TURN) &
						  ANGLE_MASK) -
						 ANGLE_HALF_TURN);
		previous_roll = car->car_rotate.z;
		if (!landing_frame && car->car_sumSurfAllWheels == CAR_WHEEL_CONTACT_NONE) {
			airborne_turn += delta;
			if (airborne_turn > maximum_turn) {
				maximum_turn = airborne_turn;
			}
		}
		if (driver->barrel_waypoint == repair->first + 1U && barrel_matches_source(repair, car) &&
			extended_owoot_barrel_roll_complete(car)) {
			complete = 1;
		}
		/* Only native road contact can end the launch. A roof or transient
		 * first touch does not outrank a landing that survives its runout. */
		if (complete && !landing_frame && !event &&
			!car->car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_SOURCE] &&
			car->car_sumSurfAllWheels != CAR_WHEEL_CONTACT_NONE) {
			landing_frame = frame;
			landing_x = car->car_position.lx / 64.0;
			landing_y = car->car_position.ly / 64.0;
			landing_z = car->car_position.lz / 64.0;
		}
		if (landing_frame && !event) {
			survived = frame - landing_frame;
			settled |= survived >= genghost_sim_frame_rate() * BARREL_REPAIR_SURVIVAL_SECONDS;
		}
		if (driver->progress > furthest + 8.0) {
			furthest = driver->progress;
			last_progress_frame = frame;
		}
		if (event || frame - last_progress_frame > BARREL_REPAIR_STALL_TICKS) {
			break;
		}
	}
	genghost_sim_result(race);
	*length = frame;
	*protected_prefix = settled ? landing_frame : 0U;
	*protected_progress = settled ? receiver->distance : 0.0;
	legacy_f64 x = landing_frame ? landing_x : state.playerstate.car_position.lx / 64.0;
	legacy_f64 y = landing_frame ? landing_y : state.playerstate.car_position.ly / 64.0;
	legacy_f64 z = landing_frame ? landing_z : state.playerstate.car_position.lz / 64.0;
	legacy_f64 lateral = fabs((x - receiver->x) * forward_z - (z - receiver->z) * forward_x);
	legacy_f64 behind = -((x - receiver->x) * forward_x + (z - receiver->z) * forward_z);
	if (behind < 0.0) {
		behind = 0.0;
	}
	legacy_f64 height =
		fabs(y - genghost_route_piece_height(receiver->piece, x, z) - BARREL_REPAIR_BODY_HEIGHT);
	legacy_f64 fitness =
		(complete ? BARREL_REPAIR_FULL_ROLL_SCORE
				  : barrel_clamp(maximum_turn, 0.0, ANGLE_FULL_TURN) * BARREL_REPAIR_ANGLE_WEIGHT) +
		(settled ? BARREL_REPAIR_SETTLED_SCORE : 0.0) +
		driver->progress * BARREL_REPAIR_PROGRESS_WEIGHT - lateral * BARREL_REPAIR_LATERAL_WEIGHT -
		height * BARREL_REPAIR_HEIGHT_WEIGHT - behind * BARREL_REPAIR_DISTANCE_WEIGHT +
		survived * BARREL_REPAIR_SURVIVAL_WEIGHT;
	extended_barrel_search_observe(&repair->search, &parameters, fitness);
	return 1;
}

void extended_genghost_search_barrel_free(struct GENGHOST_BARREL_REPAIR *repair)
{
	free(repair);
}
