#include "unit_planner_internal.h"
#include "unit_shortcut.h"

#include <math.h>
#include <string.h>

#define UNIT_DEFAULT_HALF_WIDTH 60.0
#define UNIT_BRAKING_RESERVE 0.80
#define UNIT_ACCELERATION_RESERVE 0.85
#define UNIT_GRIP_RESERVE 0.82
#define UNIT_MIN_LOOKAHEAD 110.0
#define UNIT_LOOKAHEAD_SECONDS 0.30
#define UNIT_ROLLOUT_STEP 0.05
#define UNIT_ROLLOUT_STEPS 48U
#define UNIT_CORRIDOR_COST 500.0
#define UNIT_LANE_COST 12.0
#define UNIT_SPEED_VARIANTS 5U
#define UNIT_RANDOM_MULTIPLIER 1664525U
#define UNIT_RANDOM_INCREMENT 1013904223U
#define UNIT_RANDOM_MASK 65535U
#define UNIT_SURFACE_COUNT 5U
#define UNIT_CREST_CONTACT_RESERVE 0.80
#define UNIT_DEFAULT_WHEELBASE 62.0
#define UNIT_DEFAULT_STEERING_RATE 0.2454369260617026
#define UNIT_MAXIMUM_FRONT_STEERING 0.3681553890925539
#define UNIT_GRASS_SURFACE 4U
#define UNIT_GRASS_DRAG_PER_SECOND (20.0 / 64.0)
#define UNIT_STEERING_RETURN_MULTIPLIER 4.0
#define UNIT_STEERING_CENTER_THRESHOLD 0.0015339807878856
#define UNIT_RECOVERY_SPEED 40.0
#define UNIT_RECOVERY_CRAWL_SPEED 10.0
#define UNIT_CURVATURE_WINDOW_MINIMUM 128.0
#define UNIT_CURVATURE_WINDOW_MAXIMUM 256.0
#define UNIT_CURVATURE_WINDOW_SAMPLES 64U
#define UNIT_LOCAL_SAMPLE_DISTANCE 64.0
#define UNIT_STUNT_ENTRY_DISTANCE 64.0
#define UNIT_PROFILE_LOOKAHEAD 1.0
#define UNIT_NATIVE_ANGLE_RADIANS (6.28318530717958647692 / 1024.0)
#define UNIT_NATIVE_FRONT_STEERING_SCALE 4.0
#define UNIT_NATIVE_GRIP_CURVATURE_SCALE (3.14159265358979323846 / 4.0)
#define UNIT_NATIVE_DEMAND_ANGLE_STEP 8.0
#define UNIT_NATIVE_DEMAND_SPEED_STEP 64.0
#define UNIT_NATIVE_SLIP_GRIP_SCALE 256.0
#define UNIT_NATIVE_SLIP_ANGLE_WEIGHT 3.0
#define UNIT_NATIVE_SLIP_ANGLE_DIVISOR 4.0
#define UNIT_NATIVE_SLIP_SPEED_DIVISOR 128.0
#define UNIT_NATIVE_SLIDE_GRIP_TOLERANCE 1000.0
#define UNIT_NATIVE_INTEGER_EPSILON 0.00000001
#define UNIT_SLIDE_COST 1000.0
#define UNIT_APPROACH_ACCELERATION_RESERVE 0.45
#define UNIT_STUNT_ENTRY_COST 100000.0
#define UNIT_STUNT_SPEED_TOLERANCE 2.0
/* Original banked bends support a thirty-degree tilt. Native player grip adds
 * bank/5 to the steering response after evaluating tire demand. */
#define UNIT_BANKED_ROAD_ANGLE 0.52359877559829887308
#define UNIT_NATIVE_BANK_TILT_DIVISOR 5.0

legacy_f64 unit_planner_clamp(legacy_f64 value, legacy_f64 minimum, legacy_f64 maximum)
{
	return value < minimum ? minimum : value > maximum ? maximum : value;
}

static legacy_f64 random_fraction(legacy_u32 *seed)
{
	*seed = *seed * UNIT_RANDOM_MULTIPLIER + UNIT_RANDOM_INCREMENT;
	return (*seed & UNIT_RANDOM_MASK) / (legacy_f64)UNIT_RANDOM_MASK;
}

void unit_planner_profile(legacy_u32 *seed, struct UNIT_PROFILE *profile)
{
	profile->lane = (random_fraction(seed) - 0.5) * 0.8;
	profile->apex = (random_fraction(seed) - 0.5) * 0.8;
	/* Startup evaluates line geometry and speed envelopes. Pursuit response
	 * is evaluated only by live rollouts, so it must not randomize an unscored
	 * actuator horizon and inadvertently change a validated approach. */
	profile->lookahead = UNIT_PROFILE_LOOKAHEAD;
	profile->clearance_passing = 0;
	profile->overtake_offset = 0.0;
}

static legacy_f64 distance(legacy_f64 x, legacy_f64 z)
{
	return sqrt(x * x + z * z);
}

static legacy_f64 road_grip(const struct UNIT_ROUTE_POINT *point,
							const struct UNIT_CAR_LIMITS *limits)
{
	static const legacy_f64 fallback[UNIT_SURFACE_COUNT] = {1.0, 1.0, 0.72, 0.40, 0.45};
	legacy_u32 surface = point->surface < UNIT_SURFACE_COUNT ? point->surface : 0U;
	return limits->surface_grip[surface] > 0.0 ? limits->surface_grip[surface] : fallback[surface];
}

static legacy_f64 available_width(const struct UNIT_ROUTE_POINT *point,
								  const struct UNIT_CAR_LIMITS *limits, legacy_u8 owoot)
{
	legacy_f64 car_width = limits->half_width > 0.0 ? limits->half_width : UNIT_DEFAULT_HALF_WIDTH;
	legacy_f64 width = fmax(0.0, point->half_width - car_width - UNIT_SHORTCUT_ROAD_CLEARANCE);
	/* The producer may only set shortcut_safe after validating its expanded
	 * corridor against the native penalty rule. Ordinary geometry never gets
	 * wider, including when OWOOT is disabled. */
	if (!owoot && point->shortcut_safe) {
		width += point->half_width * UNIT_SHORTCUT_EXTRA_WIDTH_FRACTION;
	}
	return width;
}

static legacy_int ordered_stunt(const struct UNIT_ROUTE_POINT *point)
{
	return point->stunt != UNIT_STUNT_NONE && !point->clearance_only;
}

static void route_window(const struct UNIT_ROUTE_POINT *points, legacy_u32 count, legacy_u32 index,
						 const struct UNIT_CAR_LIMITS *limits, legacy_u32 *before,
						 legacy_u32 *after)
{
	legacy_f64 span = unit_planner_clamp(limits->wheelbase * 2.0, UNIT_CURVATURE_WINDOW_MINIMUM,
										 UNIT_CURVATURE_WINDOW_MAXIMUM);
	legacy_f64 squared = span * span;
	*before = *after = index;
	for (legacy_u32 sample = 0U; *before > 0U && sample < UNIT_CURVATURE_WINDOW_SAMPLES; sample++) {
		(*before)--;
		legacy_f64 dx = points[index].x - points[*before].x;
		legacy_f64 dz = points[index].z - points[*before].z;
		if (dx * dx + dz * dz >= squared || ordered_stunt(&points[*before])) {
			break;
		}
	}
	for (legacy_u32 sample = 0U; *after + 1U < count && sample < UNIT_CURVATURE_WINDOW_SAMPLES;
		 sample++) {
		(*after)++;
		legacy_f64 dx = points[*after].x - points[index].x;
		legacy_f64 dz = points[*after].z - points[index].z;
		if (dx * dx + dz * dz >= squared || ordered_stunt(&points[*after])) {
			break;
		}
	}
}

static legacy_u8 route_alignment(const struct UNIT_ROUTE_POINT *point, legacy_u8 original_policy)
{
	legacy_u8 mask = UNIT_ALIGN_MANEUVER | UNIT_ALIGN_GATE | UNIT_ALIGN_CLEARANCE;
	if (!original_policy) {
		mask |= UNIT_ALIGN_FALLBACK;
	}
	return point->align_center & mask;
}

static void route_position(const struct UNIT_ROUTE_POINT *points, legacy_u32 count,
						   legacy_u32 index, const struct UNIT_CAR_LIMITS *limits, legacy_u8 owoot,
						   const struct UNIT_PROFILE *profile, legacy_u8 original_policy,
						   legacy_f64 *x, legacy_f64 *z)
{
	legacy_u32 before, after;
	route_window(points, count, index, limits, &before, &after);
	legacy_f64 dx = points[after].x - points[before].x;
	legacy_f64 dz = points[after].z - points[before].z;
	legacy_f64 length = distance(dx, dz);
	legacy_f64 ax = points[index].x - points[before].x;
	legacy_f64 az = points[index].z - points[before].z;
	legacy_f64 bx = points[after].x - points[index].x;
	legacy_f64 bz = points[after].z - points[index].z;
	legacy_f64 cross = ax * bz - az * bx;
	legacy_f64 apex = cross > UNIT_PLANNER_MIN_DISTANCE	   ? 1.0
					  : cross < -UNIT_PLANNER_MIN_DISTANCE ? -1.0
														   : 0.0;
	legacy_f64 lane =
		unit_planner_clamp(profile->lane + apex * profile->apex, -UNIT_SHORTCUT_MAX_LANE_FRACTION,
						   UNIT_SHORTCUT_MAX_LANE_FRACTION);
	legacy_f64 width = available_width(&points[index], limits, owoot);
	legacy_f64 offset = width * lane;
	if (ordered_stunt(&points[index]) || route_alignment(&points[index], original_policy)) {
		offset = 0.0;
	}
	*x = points[index].x + (length > UNIT_PLANNER_MIN_DISTANCE ? -dz * offset / length : 0.0);
	*z = points[index].z + (length > UNIT_PLANNER_MIN_DISTANCE ? dx * offset / length : 0.0);
}

static legacy_f64 line_grip(const struct UNIT_ROUTE_POINT *point,
							const struct UNIT_CAR_LIMITS *limits, legacy_u8 owoot, legacy_f64 x,
							legacy_f64 z)
{
	legacy_f64 grip = road_grip(point, limits);
	if (!owoot && point->shortcut_safe &&
		distance(x - point->x, z - point->z) > available_width(point, limits, 1U)) {
		struct UNIT_ROUTE_POINT grass = *point;
		grass.surface = UNIT_GRASS_SURFACE;
		grip = fmin(grip, road_grip(&grass, limits));
	}
	return grip;
}

static legacy_f64 route_limit(const struct UNIT_ROUTE_POINT *points, legacy_u32 count,
							  legacy_u32 index, const struct UNIT_CAR_LIMITS *limits,
							  legacy_u8 owoot, const struct UNIT_PROFILE *profile,
							  legacy_u8 original_policy)
{
	legacy_f64 result = limits->max_speed;
	legacy_u32 before, after;
	route_window(points, count, index, limits, &before, &after);
	if (points[index].speed_limit > 0.0) {
		result = fmin(result, points[index].speed_limit);
	}
	if (!original_policy && points[index].fallback_speed_limit > 0.0) {
		result = fmin(result, points[index].fallback_speed_limit);
	}
	if (before < index && after > index && !ordered_stunt(&points[before]) &&
		!ordered_stunt(&points[index]) && !ordered_stunt(&points[after])) {
		legacy_f64 ax, az, bx, bz, cx, cz;
		route_position(points, count, before, limits, owoot, profile, original_policy, &ax, &az);
		route_position(points, count, index, limits, owoot, profile, original_policy, &bx, &bz);
		route_position(points, count, after, limits, owoot, profile, original_policy, &cx, &cz);
		legacy_f64 first = distance(bx - ax, bz - az);
		legacy_f64 second = distance(cx - bx, cz - bz);
		legacy_f64 chord = distance(cx - ax, cz - az);
		legacy_f64 cross = fabs((bx - ax) * (cz - bz) - (bz - az) * (cx - bx));
		legacy_f64 denominator = first * second * chord;
		if (denominator > UNIT_PLANNER_MIN_DISTANCE && cross > UNIT_PLANNER_MIN_DISTANCE) {
			legacy_f64 curvature = 2.0 * cross / denominator;
			legacy_f64 grip =
				limits->lateral_acceleration * line_grip(&points[index], limits, owoot, bx, bz);
			if (points[index].banked_corner && original_policy) {
				legacy_f64 wheelbase =
					limits->wheelbase > 0.0 ? limits->wheelbase : UNIT_DEFAULT_WHEELBASE;
				legacy_f64 demand = atan(curvature * wheelbase);
				demand = fmax(UNIT_NATIVE_ANGLE_RADIANS,
							  demand - UNIT_BANKED_ROAD_ANGLE / (UNIT_NATIVE_BANK_TILT_DIVISOR *
																 UNIT_NATIVE_FRONT_STEERING_SCALE));
				curvature = tan(demand) / wheelbase;
			}
			result = fmin(
				result, sqrt(grip * UNIT_GRIP_RESERVE / (curvature * UNIT_PLANNER_WORLD_PER_MPH)));
		}
	}
	if (before < index && after > index && !ordered_stunt(&points[before]) &&
		!ordered_stunt(&points[index]) && !ordered_stunt(&points[after]) &&
		!points[before].height_unspecified && !points[index].height_unspecified &&
		!points[after].height_unspecified) {
		legacy_f64 first =
			distance(points[index].x - points[before].x, points[index].z - points[before].z);
		legacy_f64 second =
			distance(points[after].x - points[index].x, points[after].z - points[index].z);
		if (first > UNIT_PLANNER_MIN_DISTANCE && second > UNIT_PLANNER_MIN_DISTANCE) {
			legacy_f64 rise = points[index].y - points[before].y;
			legacy_f64 next_rise = points[after].y - points[index].y;
			legacy_f64 entry = atan2(rise, first), exit = atan2(next_rise, second);
			legacy_f64 arc = (hypot(first, rise) + hypot(second, next_rise)) * 0.5;
			legacy_f64 curvature = (entry - exit) / arc;
			if (curvature > UNIT_PLANNER_MIN_DISTANCE / UNIT_PLANNER_MAX_LOOKAHEAD) {
				legacy_f64 normal_gravity = UNIT_PLANNER_WORLD_GRAVITY * cos((entry + exit) * 0.5);
				result = fmin(result, sqrt(fmax(0.0, normal_gravity) * UNIT_CREST_CONTACT_RESERVE /
										   curvature) /
										  UNIT_PLANNER_WORLD_PER_MPH);
			}
		}
	}
	return fmin(limits->max_speed,
				fmax(fmax(UNIT_PLANNER_MIN_SPEED, points[index].speed_floor), result));
}

static legacy_f64 route_distance(const struct UNIT_ROUTE_POINT *points, legacy_u32 count,
								 legacy_u32 index, const struct UNIT_CAR_LIMITS *limits,
								 legacy_u8 owoot, const struct UNIT_PROFILE *profile)
{
	legacy_f64 ax, az, bx, bz;
	if (!index) {
		return 0.0;
	}
	route_position(points, count, index - 1U, limits, owoot, profile, 1U, &ax, &az);
	route_position(points, count, index, limits, owoot, profile, 1U, &bx, &bz);
	return distance(bx - ax, bz - az);
}

void unit_planner_evaluation_begin(struct UNIT_EVALUATION *evaluation, legacy_u32 count,
								   const struct UNIT_PROFILE *profile)
{
	evaluation->profile = *profile;
	evaluation->profile.clearance_passing = 0;
	evaluation->profile.overtake_offset = 0.0;
	evaluation->count = count;
	evaluation->cursor = 0U;
	evaluation->phase = 0U;
	evaluation->score = 0.0;
}

legacy_int unit_planner_evaluation_step(struct UNIT_EVALUATION *evaluation,
										const struct UNIT_ROUTE_POINT *points,
										const struct UNIT_CAR_LIMITS *limits, legacy_u8 owoot)
{
	for (legacy_u32 work = 0; work < UNIT_PLANNER_CHUNK_POINTS; work++) {
		legacy_u32 index = evaluation->cursor;
		if (evaluation->phase == 0U) {
			evaluation->speeds[index] = route_limit(points, evaluation->count, index, limits, owoot,
													&evaluation->profile, 1U);
			if (++evaluation->cursor == evaluation->count) {
				evaluation->phase = 1U;
				evaluation->cursor = evaluation->count - 1U;
			}
		} else if (evaluation->phase == 1U) {
			legacy_f64 length = route_distance(points, evaluation->count, index, limits, owoot,
											   &evaluation->profile);
			legacy_f64 next = evaluation->speeds[index];
			legacy_f64 limit = sqrt(next * next + 2.0 * limits->braking * UNIT_BRAKING_RESERVE *
													  length / UNIT_PLANNER_WORLD_PER_MPH);
			evaluation->speeds[index - 1U] = fmin(evaluation->speeds[index - 1U], limit);
			if (--evaluation->cursor == 0U) {
				evaluation->phase = 2U;
				evaluation->cursor = 1U;
				evaluation->speeds[0] = 0.0;
			}
		} else {
			legacy_f64 length = route_distance(points, evaluation->count, index, limits, owoot,
											   &evaluation->profile);
			legacy_f64 previous = evaluation->speeds[index - 1U];
			legacy_f64 limit =
				sqrt(previous * previous + 2.0 * limits->acceleration * UNIT_ACCELERATION_RESERVE *
											   length / UNIT_PLANNER_WORLD_PER_MPH);
			evaluation->speeds[index] = fmin(evaluation->speeds[index], limit);
			if (evaluation->speeds[index] + UNIT_STUNT_SPEED_TOLERANCE <
				points[index].speed_floor) {
				/* A quick line is not feasible if acceleration cannot provide its
				 * mandatory loop or launch entry speed. */
				evaluation->score += UNIT_STUNT_ENTRY_COST;
			}
			legacy_f64 mean_speed =
				fmax(UNIT_PLANNER_MIN_SPEED, (previous + evaluation->speeds[index]) * 0.5);
			evaluation->score += length / (mean_speed * UNIT_PLANNER_WORLD_PER_MPH);
			if (++evaluation->cursor == evaluation->count) {
				return 1;
			}
		}
	}
	return 0;
}

static legacy_u32 local_route(const struct UNIT_ROUTE_POINT *points, legacy_u32 count,
							  const struct UNIT_CAR_LIMITS *limits, legacy_u8 owoot,
							  const struct UNIT_PROFILE *profile, legacy_u32 cursor,
							  legacy_u8 original_policy,
							  struct UNIT_LOCAL_POINT local[UNIT_PLANNER_LOCAL_POINTS])
{
	legacy_u32 first = cursor ? cursor - 1U : 0U;
	legacy_u32 length = 0U, previous = first;
	for (legacy_u32 source = first; source < count && length < UNIT_PLANNER_LOCAL_POINTS &&
									source - first < UNIT_PLANNER_LOCAL_SAMPLE_SCAN_LIMIT;
		 source++) {
		legacy_f64 dx = points[source].x - points[previous].x;
		legacy_f64 dz = points[source].z - points[previous].z;
		legacy_u8 boundary =
			source == first || source == cursor || source + 1U == count ||
			points[source].stunt != UNIT_STUNT_NONE ||
			(!original_policy && source &&
			 points[source].fallback_speed_limit != points[source - 1U].fallback_speed_limit) ||
			(!original_policy && source + 1U < count &&
			 points[source].fallback_speed_limit != points[source + 1U].fallback_speed_limit) ||
			(source && points[source].route_index != points[source - 1U].route_index) ||
			(source + 1U < count && points[source].route_index != points[source + 1U].route_index);
		if (!boundary &&
			dx * dx + dz * dz < UNIT_LOCAL_SAMPLE_DISTANCE * UNIT_LOCAL_SAMPLE_DISTANCE) {
			continue;
		}
		legacy_u32 index = length++;
		route_position(points, count, source, limits, owoot, profile, original_policy,
					   &local[index].x, &local[index].z);
		local[index].y = points[source].y;
		local[index].road_x = points[source].x;
		local[index].road_z = points[source].z;
		local[index].source = source;
		local[index].terminal = source + 1U == count;
		local[index].height_unspecified = points[source].height_unspecified;
		local[index].width = available_width(&points[source], limits, owoot);
		local[index].ordinary_width = available_width(&points[source], limits, 1U);
		local[index].speed =
			route_limit(points, count, source, limits, owoot, profile, original_policy);
		local[index].minimum_speed = fmin(limits->max_speed, points[source].speed_floor);
		local[index].distance =
			index ? local[index - 1U].distance + distance(local[index].x - local[index - 1U].x,
														  local[index].z - local[index - 1U].z)
				  : 0.0;
		previous = source;
		if (index && ordered_stunt(&points[source])) {
			break;
		}
	}
	for (legacy_u32 index = length ? length - 1U : 0U; index > 0U; index--) {
		legacy_f64 span = local[index].distance - local[index - 1U].distance;
		legacy_f64 limit =
			sqrt(local[index].speed * local[index].speed +
				 2.0 * limits->braking * UNIT_BRAKING_RESERVE * span / UNIT_PLANNER_WORLD_PER_MPH);
		local[index - 1U].speed = fmin(local[index - 1U].speed, limit);
		legacy_f64 minimum =
			sqrt(fmax(0.0, local[index].minimum_speed * local[index].minimum_speed -
							   2.0 * limits->acceleration * UNIT_APPROACH_ACCELERATION_RESERVE *
								   span / UNIT_PLANNER_WORLD_PER_MPH));
		local[index - 1U].minimum_speed = fmax(local[index - 1U].minimum_speed, minimum);
	}
	return length;
}

static legacy_f64 project(const struct UNIT_LOCAL_POINT *a, const struct UNIT_LOCAL_POINT *b,
						  legacy_f64 x, legacy_f64 z, legacy_f64 *error)
{
	legacy_f64 dx = b->x - a->x, dz = b->z - a->z;
	legacy_f64 squared = dx * dx + dz * dz;
	legacy_f64 fraction = squared > UNIT_PLANNER_MIN_DISTANCE
							  ? fmax(0.0, ((x - a->x) * dx + (z - a->z) * dz) / squared)
							  : 0.0;
	if (!b->terminal) {
		fraction = fmin(1.0, fraction);
	}
	*error = distance(x - a->x - fraction * dx, z - a->z - fraction * dz);
	return fraction;
}

static legacy_u32 closest(const struct UNIT_LOCAL_POINT *local, legacy_u32 count, legacy_u32 cursor,
						  legacy_f64 x, legacy_f64 z, legacy_f64 *progress, legacy_f64 *error)
{
	legacy_u32 first = cursor > 1U ? cursor - 1U : 0U;
	legacy_u32 end = cursor + 5U < count ? cursor + 5U : count;
	legacy_u32 best = first;
	*error = HUGE_VAL;
	for (legacy_u32 index = first; index + 1U < end; index++) {
		legacy_f64 candidate_error;
		legacy_f64 fraction = project(&local[index], &local[index + 1U], x, z, &candidate_error);
		if (candidate_error < *error) {
			*error = candidate_error;
			*progress = local[index].distance +
						fraction * (local[index + 1U].distance - local[index].distance);
			best = index;
		}
	}
	return best;
}

static legacy_f64 road_violation(const struct UNIT_LOCAL_POINT *local, legacy_u32 count,
								 legacy_u32 cursor, legacy_f64 x, legacy_f64 z, legacy_u8 ordinary)
{
	legacy_u32 first = cursor > 1U ? cursor - 1U : 0U;
	legacy_u32 end = cursor + 5U < count ? cursor + 5U : count;
	legacy_f64 violation = HUGE_VAL;
	for (legacy_u32 index = first; index + 1U < end; index++) {
		struct UNIT_LOCAL_POINT a = local[index], b = local[index + 1U];
		a.x = a.road_x;
		a.z = a.road_z;
		b.x = b.road_x;
		b.z = b.road_z;
		legacy_f64 error;
		legacy_f64 fraction = project(&a, &b, x, z, &error);
		legacy_f64 first_width = ordinary ? a.ordinary_width : a.width;
		legacy_f64 second_width = ordinary ? b.ordinary_width : b.width;
		legacy_f64 width = first_width + (second_width - first_width) * fmin(1.0, fraction);
		violation = fmin(violation, fmax(0.0, error - width));
	}
	return violation;
}

static void target_at(const struct UNIT_LOCAL_POINT *local, legacy_u32 count, legacy_f64 progress,
					  struct UNIT_PLAN *plan)
{
	legacy_u32 index = 1U;
	while (index + 1U < count && local[index].distance < progress) {
		index++;
	}
	const struct UNIT_LOCAL_POINT *a = &local[index - 1U], *b = &local[index];
	legacy_f64 span = b->distance - a->distance;
	legacy_f64 fraction =
		span > UNIT_PLANNER_MIN_DISTANCE ? fmax(0.0, (progress - a->distance) / span) : 1.0;
	if (!b->terminal) {
		fraction = fmin(1.0, fraction);
	}
	plan->target_x = a->x + (b->x - a->x) * fraction;
	plan->target_y = a->y + (b->y - a->y) * fraction;
	plan->target_z = a->z + (b->z - a->z) * fraction;
	plan->target_speed = fmin(a->speed, b->speed);
	plan->route_cursor = b->source;
}

static legacy_f64 native_grip_response(const struct UNIT_CAR_LIMITS *limits, legacy_f64 wheelbase,
									   legacy_f64 grip_ratio, legacy_f64 steering,
									   legacy_f64 *speed, legacy_f64 *cost)
{
	legacy_f64 combined =
		floor(limits->lateral_acceleration * wheelbase * grip_ratio /
				  (UNIT_PLANNER_WORLD_PER_MPH * UNIT_NATIVE_GRIP_CURVATURE_SCALE) +
			  UNIT_NATIVE_INTEGER_EPSILON);
	legacy_f64 angle = copysign(
		floor(fabs(steering) * UNIT_NATIVE_FRONT_STEERING_SCALE / UNIT_NATIVE_ANGLE_RADIANS +
			  UNIT_NATIVE_INTEGER_EPSILON),
		steering);
	legacy_f64 integer_speed = floor(*speed);
	legacy_f64 squared = integer_speed * integer_speed;
	legacy_f64 demanded = floor(fabs(angle) / UNIT_NATIVE_DEMAND_ANGLE_STEP) *
						  floor(squared / UNIT_NATIVE_DEMAND_SPEED_STEP);
	if (demanded <= combined || squared < UNIT_PLANNER_MIN_DISTANCE) {
		return steering;
	}
	/* Native player grip deliberately uses 256 here, whereas its demand
	 * threshold is equivalent to 512. A lateral-acceleration clamp therefore
	 * overstates steering response as soon as the tires start sliding. */
	legacy_f64 saturated = copysign(floor(combined * UNIT_NATIVE_SLIP_GRIP_SCALE / squared), angle);
	legacy_f64 adjusted =
		trunc((UNIT_NATIVE_SLIP_ANGLE_WEIGHT * saturated + angle) / UNIT_NATIVE_SLIP_ANGLE_DIVISOR);
	*speed = fmax(0.0, *speed - fabs(angle - adjusted) / UNIT_NATIVE_SLIP_SPEED_DIVISOR);
	*cost += UNIT_SLIDE_COST;
	if (demanded > combined + UNIT_NATIVE_SLIDE_GRIP_TOLERANCE) {
		/* Beyond this threshold the native engine rotates the sliding body
		 * separately and suppresses front-wheel steering. Do not invent usable
		 * cornering authority from an unmodelled slide-yaw state. */
		return 0.0;
	}
	return adjusted * UNIT_NATIVE_ANGLE_RADIANS / UNIT_NATIVE_FRONT_STEERING_SCALE;
}

legacy_int unit_planner_rollout(const struct UNIT_ROUTE_POINT *points, legacy_u32 count,
								const struct UNIT_CAR_LIMITS *limits, legacy_u8 owoot,
								const struct UNIT_PROFILE *profile,
								const struct UNIT_REQUEST *request, legacy_u32 candidate,
								struct UNIT_ROLLOUT *result)
{
	const struct UNIT_ROUTE_POINT *current = &points[request->cursor];
	/* Unrestricted live guidance uses conservative fallback assumptions.
	 * Native previews prove their own trajectory; OWOOT retains its existing
	 * wheel/jump policy rather than adopting unvalidated launch changes. */
	legacy_u8 original_policy = request->native_preview || owoot;
	owoot = owoot || request->unit.shortcuts_forbidden;
	if (ordered_stunt(current)) {
		/* Loops and rolled launches overlap themselves in a ground projection.
		 * Preserve the ordered three-dimensional native guide rather than
		 * interpreting that projection as a hairpin or selecting a later floor. */
		memset(result, 0, sizeof(*result));
		result->candidate = candidate;
		result->plan.target_x = current->x;
		result->plan.target_y = current->y;
		result->plan.target_z = current->z;
		if (current->stunt == UNIT_STUNT_PROTECTED || current->stunt == UNIT_STUNT_RAMP) {
			unit_planner_protected_target(points, count, request, &result->plan);
		}
		result->plan.target_speed =
			fmin(limits->max_speed,
				 fmax(current->speed_floor,
					  current->speed_limit > 0.0 ? current->speed_limit : limits->max_speed));
		if (!original_policy && current->fallback_speed_limit > 0.0) {
			result->plan.target_speed =
				fmax(current->speed_floor,
					 fmin(result->plan.target_speed, current->fallback_speed_limit));
		}
		result->plan.route_cursor = request->cursor;
		result->plan.generation = request->generation;
		result->plan.valid = 1U;
		result->score = -result->plan.target_speed;
		return 1;
	}
	struct UNIT_PROFILE selected = *profile;
	selected.clearance_passing = 0;
	selected.overtake_offset = 0.0;
	legacy_f64 speed_scale = 1.0;
	legacy_u8 crawl = candidate == UNIT_PLANNER_CRAWL_CANDIDATE;
	if (crawl) {
		selected.lane = selected.apex = 0.0;
		selected.lookahead = 1.0;
	} else if (candidate) {
		legacy_u32 variant = (candidate - 1U) / UNIT_SPEED_VARIANTS;
		selected.lane = ((legacy_s32)((candidate - 1U) % UNIT_SPEED_VARIANTS) - 2) * 0.4;
		selected.apex *= 0.5;
		speed_scale = variant == 0U ? 1.0 : variant == 1U ? 0.8 : variant == 2U ? 0.5 : 0.0;
	}
	struct UNIT_LOCAL_POINT local[UNIT_PLANNER_LOCAL_POINTS];
	legacy_u8 alignment = route_alignment(current, original_policy);
	if (request->unit.recovering ||
		(alignment & (UNIT_ALIGN_MANEUVER | UNIT_ALIGN_FALLBACK | UNIT_ALIGN_CLEARANCE))) {
		selected.lane = 0.0;
		selected.apex = 0.0;
		selected.lookahead = 1.0;
	} else if (alignment) {
		/* Gate alignment belongs to each route sample. Maneuver approaches
		 * still need the complete centered horizon above, while a gate may
		 * preserve racing-line candidates on its open approach and exit. */
		selected.lookahead = 1.0;
	}
	legacy_u32 local_count = local_route(points, count, limits, owoot, &selected, request->cursor,
										 original_policy, local);
	if (local_count < 2U) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->candidate = candidate;
	legacy_f64 x = request->unit.x, z = request->unit.z, yaw = request->unit.yaw;
	legacy_f64 speed = fmax(0.0, request->unit.speed);
	legacy_f64 steering = request->unit.steering;
	legacy_f64 steering_rate = request->unit.steering_rate > 0.0 ? request->unit.steering_rate
																 : UNIT_DEFAULT_STEERING_RATE;
	legacy_f64 wheelbase = limits->wheelbase > 0.0 ? limits->wheelbase : UNIT_DEFAULT_WHEELBASE;
	legacy_f64 progress = 0.0, initial_error = 0.0;
	legacy_u32 cursor = closest(local, local_count, 0U, x, z, &progress, &initial_error);
	legacy_f64 initial_progress = progress;
	legacy_f64 cost = alignment ? 0.0 : fabs(selected.lane) * UNIT_LANE_COST;
	for (legacy_u32 step = 0U; step < UNIT_ROLLOUT_STEPS; step++) {
		legacy_f64 error;
		cursor = closest(local, local_count, cursor, x, z, &progress, &error);
		/* A chosen racing line does not move the physical road. Recovery from
		 * outside the corridor also does not enlarge the legal driving region. */
		cost += road_violation(local, local_count, cursor, x, z, 0U) * UNIT_CORRIDOR_COST;
		legacy_f64 lookahead = unit_planner_clamp(
			(UNIT_MIN_LOOKAHEAD + speed * UNIT_PLANNER_WORLD_PER_MPH * UNIT_LOOKAHEAD_SECONDS) *
				selected.lookahead,
			UNIT_MIN_LOOKAHEAD, UNIT_PLANNER_MAX_LOOKAHEAD);
		struct UNIT_PLAN target = {0};
		target_at(local, local_count, progress + lookahead, &target);
		legacy_f64 maximum_speed = fmin(target.target_speed, local[cursor].speed);
		target.target_speed = maximum_speed * speed_scale;
		if (crawl) {
			/* A recovery cap can collapse all proportional speed variants to
			 * the same value. Keep a slow, independently simulated maneuver
			 * that can regain the road without committing to the next stunt. */
			target.target_speed = fmin(target.target_speed, UNIT_RECOVERY_CRAWL_SPEED);
		} else if (speed_scale > 0.0) {
			/* Slow variants must still leave enough acceleration distance before
			 * a required launch. A full stop remains available for unsafe approaches. */
			target.target_speed =
				fmin(maximum_speed, fmax(target.target_speed, local[cursor].minimum_speed));
		}
		if (request->unit.recovering) {
			target.target_speed = fmin(target.target_speed, UNIT_RECOVERY_SPEED);
		}
		if (!step) {
			result->plan = target;
			result->plan.generation = request->generation;
			result->plan.valid = 1U;
		}
		legacy_f64 acceleration =
			target.target_speed > speed ? limits->acceleration : -limits->braking;
		legacy_f64 next_speed = speed + acceleration * UNIT_ROLLOUT_STEP;
		speed = acceleration > 0.0 ? fmin(target.target_speed, next_speed)
								   : fmax(target.target_speed, next_speed);
		const struct UNIT_ROUTE_POINT *source = &points[local[cursor].source];
		legacy_f64 grip_ratio = road_grip(source, limits);
		if (road_violation(local, local_count, cursor, x, z, 1U) > UNIT_PLANNER_MIN_DISTANCE) {
			struct UNIT_ROUTE_POINT grass = *source;
			grass.surface = UNIT_GRASS_SURFACE;
			grip_ratio = fmin(grip_ratio, road_grip(&grass, limits));
			/* Eligibility proves grassy terrain for this footprint. Elsewhere
			 * lower grip is conservative, but unknown terrain must not provide
			 * extra braking through an assumed grass drag coefficient. */
			if (!owoot && source->shortcut_safe) {
				speed *= 1.0 - UNIT_GRASS_DRAG_PER_SECOND * UNIT_ROLLOUT_STEP;
			}
		}
		legacy_f64 dx = target.target_x - x, dz = target.target_z - z;
		legacy_f64 squared = dx * dx + dz * dz;
		legacy_f64 lateral = dx * cos(yaw) - dz * sin(yaw);
		legacy_f64 wanted =
			squared > UNIT_PLANNER_MIN_DISTANCE ? atan(2.0 * wheelbase * lateral / squared) : 0.0;
		wanted =
			unit_planner_clamp(wanted, -UNIT_MAXIMUM_FRONT_STEERING, UNIT_MAXIMUM_FRONT_STEERING);
		legacy_f64 change = wanted - steering;
		legacy_f64 response_rate = steering_rate;
		if ((change > 0.0 && steering < -UNIT_STEERING_CENTER_THRESHOLD) ||
			(change < 0.0 && steering > UNIT_STEERING_CENTER_THRESHOLD)) {
			response_rate *= UNIT_STEERING_RETURN_MULTIPLIER;
		}
		steering += unit_planner_clamp(change, -response_rate * UNIT_ROLLOUT_STEP,
									   response_rate * UNIT_ROLLOUT_STEP);
		legacy_f64 response =
			native_grip_response(limits, wheelbase, grip_ratio, steering, &speed, &cost);
		if (source->banked_corner) {
			legacy_u32 bank_before, bank_after;
			route_window(points, count, local[cursor].source, limits, &bank_before, &bank_after);
			legacy_f64 ax = source->x - points[bank_before].x;
			legacy_f64 az = source->z - points[bank_before].z;
			legacy_f64 bx = points[bank_after].x - source->x;
			legacy_f64 bz = points[bank_after].z - source->z;
			legacy_f64 cross = ax * bz - az * bx;
			if (fabs(cross) > UNIT_PLANNER_MIN_DISTANCE) {
				response -= copysign(UNIT_BANKED_ROAD_ANGLE / (UNIT_NATIVE_BANK_TILT_DIVISOR *
															   UNIT_NATIVE_FRONT_STEERING_SCALE),
									 cross);
			}
		}
		legacy_f64 rate = speed * UNIT_PLANNER_WORLD_PER_MPH * tan(response) / wheelbase;
		yaw += rate * UNIT_ROLLOUT_STEP;
		x += sin(yaw) * speed * UNIT_PLANNER_WORLD_PER_MPH * UNIT_ROLLOUT_STEP;
		z += cos(yaw) * speed * UNIT_PLANNER_WORLD_PER_MPH * UNIT_ROLLOUT_STEP;
		legacy_f64 time = (step + 1U) * UNIT_ROLLOUT_STEP;
		if (ordered_stunt(&points[local[local_count - 1U].source]) &&
			progress >= local[local_count - 1U].distance - UNIT_STUNT_ENTRY_DISTANCE) {
			const struct UNIT_LOCAL_POINT *entry = &local[local_count - 1U];
			legacy_f64 remaining = fmax(0.0, entry->distance - progress);
			legacy_f64 attainable = sqrt(
				speed * speed + 2.0 * limits->acceleration * UNIT_APPROACH_ACCELERATION_RESERVE *
									remaining / UNIT_PLANNER_WORLD_PER_MPH);
			if (attainable + UNIT_STUNT_SPEED_TOLERANCE < entry->minimum_speed) {
				cost += UNIT_STUNT_ENTRY_COST;
			}
			/* A protected maneuver continues beyond this guide's endpoint. Its
			 * ordered 3D controller takes over there; stopping before the entry
			 * must not beat a safe approach merely because this rollout ends. */
			progress += speed * UNIT_PLANNER_WORLD_PER_MPH * (UNIT_PLANNER_ROLLOUT_SECONDS - time);
			break;
		}
	}
	result->score = cost - (progress - initial_progress);
	if (speed_scale == 0.0) {
		result->plan.brake = UNIT_PLAN_BRAKE_STOP;
		result->plan.target_speed = 0.0;
	}
	return 1;
}
