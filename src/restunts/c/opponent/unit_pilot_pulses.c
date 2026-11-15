#include "unit_pilot_pulses.h"
#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include "unit_opponent.h"
#include "unit_opponent_internal.h"
#include "../crash_state.h"
#include "../extended/extended_barrel_search.h"
#include "../extended/extended_owoot_internal.h"
#include "../extended/extended_owoot_route.h"
#include "../math/math.h"
#define UNIT_PULSE_DEFAULT_SPAN_SECONDS 3U
#define UNIT_PULSE_RECEIVER_SAMPLES 128U
#define UNIT_PULSE_FULL_ROLL_SCORE 1000000.0
#define UNIT_PULSE_SETTLED_SCORE 100000000.0
#define UNIT_PULSE_LATERAL_WEIGHT 4.0
#define UNIT_PULSE_HEIGHT_WEIGHT 4.0
#define UNIT_PULSE_DISTANCE_WEIGHT 2.0
#define UNIT_PULSE_SURVIVAL_WEIGHT 20.0
#define UNIT_PULSE_ANGLE_WEIGHT 32.0
#define UNIT_PULSE_BODY_HEIGHT 8.0
#define UNIT_PULSE_MINIMUM_SPEED 20.0
#define UNIT_PULSE_LATERAL_LIMIT 1.0
#define UNIT_PULSE_SEARCH_FIELDS                                                                   \
	(EXTENDED_BARREL_FIELDS_PULSES | (1U << EXTENDED_BARREL_SPEED) |                               \
	 (1U << EXTENDED_BARREL_LATERAL))
struct UNIT_PILOT_PULSES {
	struct EXTENDED_BARREL_SEARCH search;
	struct UNIT_PILOT_PULSE_DIAGNOSTIC last_trial;
	struct EXTENDED_BARREL_PARAMETERS parameters, known_parameters;
	struct UNIT_PILOT_PULSE_PROFILE current, best, known_profile;
	struct UNIT_ROUTE_POINT entry, lip, receiver, receiver_end;
	legacy_f64 forward_x, forward_z, length, half_length, best_fitness, maximum_speed;
	legacy_s16 piece, previous_roll;
	legacy_s32 airborne_turn;
	legacy_u32 entry_frame, bank_frame, air_frame, landing_frame;
	legacy_u32 best_span, best_bank, span, bank, attempts, known_span, known_bank;
	legacy_u16 frame_rate, progress;
	legacy_u8 valid, active, entered, banked, airborne, landed, complete;
	legacy_u8 saw_source, best_valid, known_valid, known_pending, known_trial, has_last_trial;
};
struct UNIT_PILOT_PULSES *unit_pilot_pulses_create(legacy_u32 seed)
{
	struct UNIT_PILOT_PULSES *pulses = calloc(1U, sizeof(*pulses));
	if (pulses) {
		extended_barrel_search_init(&pulses->search, seed);
		pulses->piece = -1;
	}
	return pulses;
}
void unit_pilot_pulses_destroy(struct UNIT_PILOT_PULSES *pulses)
{
	free(pulses);
}
legacy_int unit_pilot_pulses_begin(struct UNIT_PILOT_PULSES *pulses, const struct UNIT_ROUTE *route,
								   legacy_u32 first, legacy_u32 last, legacy_u16 frame_rate)
{
	unit_opponent_pilot_input_override(0U, 0U);
	if (!pulses) {
		return 0;
	}
	pulses->valid = pulses->active = 0U;
	pulses->attempts = 0U;
	if (!route || !route->points || first >= route->count || last >= route->count ||
		last <= first + 1U || last + 1U >= route->count ||
		route->points[first].stunt != UNIT_STUNT_BARREL || route->points[first].route_index < 0 ||
		!frame_rate || !isfinite(route->limits.max_speed) || route->limits.max_speed <= 0.0) {
		return 0;
	}
	legacy_s16 piece = route->points[first].route_index;
	if (pulses->piece != piece) {
		extended_barrel_search_clear(&pulses->search);
		pulses->known_valid = pulses->best_valid = 0U;
	} else {
		/* Reuse ideas after a stopped recheck, never old numerical fitness. */
		extended_barrel_search_invalidate(&pulses->search);
	}
	pulses->known_pending = pulses->known_valid;
	pulses->piece = piece;
	pulses->entry = route->points[first];
	pulses->lip = route->points[last - 1U];
	pulses->receiver = route->points[last + 1U];
	legacy_u32 end = last + 1U;
	while (end + 1U < route->count && end - last < UNIT_PULSE_RECEIVER_SAMPLES &&
		   route->points[end + 1U].route_index == pulses->receiver.route_index) {
		end++;
	}
	pulses->receiver_end = route->points[end];
	pulses->forward_x = pulses->lip.x - pulses->entry.x;
	pulses->forward_z = pulses->lip.z - pulses->entry.z;
	pulses->length = hypot(pulses->forward_x, pulses->forward_z);
	if (!isfinite(pulses->length) || pulses->length < 1.0) {
		return 0;
	}
	pulses->forward_x /= pulses->length;
	pulses->forward_z /= pulses->length;
	pulses->half_length = route->limits.half_length;
	pulses->maximum_speed = route->limits.max_speed;
	pulses->frame_rate = frame_rate;
	pulses->best_fitness = -DBL_MAX;
	if (!pulses->best_valid) {
		pulses->best_span = frame_rate * UNIT_PULSE_DEFAULT_SPAN_SECONDS;
		pulses->best_bank = pulses->best_span / 2U;
	}
	pulses->valid = 1U;
	return 1;
}
void unit_pilot_pulses_start_trial(struct UNIT_PILOT_PULSES *pulses, legacy_u32 frame,
								   const struct UNIT_PILOT_PULSE_PROFILE *profile)
{
	unit_opponent_pilot_input_override(0U, 0U);
	if (!pulses || !pulses->valid || !profile) {
		return;
	}
	pulses->current = *profile;
	pulses->entered = pulses->banked = pulses->airborne = pulses->landed = 0U;
	pulses->complete = pulses->saw_source = 0U;
	pulses->entry_frame = pulses->bank_frame = pulses->air_frame = pulses->landing_frame = frame;
	pulses->previous_roll = 0;
	pulses->airborne_turn = 0;
	pulses->progress = 0U;
}
static legacy_int unit_pulses_receiving_road(const struct UNIT_PILOT_PULSES *pulses,
											 const struct CARSTATE *car)
{
	if (car->car_route_index != pulses->receiver.route_index) {
		return 0;
	}
	for (legacy_u16 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
		if (car->car_surfaceWhl[wheel] >= CAR_SURFACE_PAVED &&
			car->car_surfaceWhl[wheel] <= CAR_SURFACE_ICE) {
			return 1;
		}
	}
	return 0;
}

static void unit_pulses_observe(struct UNIT_PILOT_PULSES *pulses, const struct CARSTATE *car,
								legacy_u32 frame)
{
	legacy_f64 x = car->car_position.lx / UNIT_POSITION_SCALE;
	legacy_f64 z = car->car_position.lz / UNIT_POSITION_SCALE;
	legacy_f64 along =
		(x - pulses->entry.x) * pulses->forward_x + (z - pulses->entry.z) * pulses->forward_z;
	if (!pulses->entered) {
		if (car->car_route_index != pulses->piece || along < 0.0 ||
			along > pulses->length + pulses->half_length ||
			fabs((x - pulses->entry.x) * pulses->forward_z -
				 (z - pulses->entry.z) * pulses->forward_x) >
				pulses->entry.half_width + pulses->half_length) {
			return;
		}
		pulses->entered = 1U;
		pulses->entry_frame = frame;
		pulses->previous_roll = car->car_rotate.z;
	}
	legacy_u8 source =
		car->car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_SOURCE] == pulses->piece + 1;
	if (source) {
		pulses->saw_source = 1U;
		if (!pulses->banked) {
			pulses->banked = 1U;
			pulses->bank_frame = frame;
		}
	}
	legacy_s16 delta =
		(legacy_s16)(((legacy_u16)(car->car_rotate.z - pulses->previous_roll + ANGLE_HALF_TURN) &
					  ANGLE_MASK) -
					 ANGLE_HALF_TURN);
	pulses->previous_roll = car->car_rotate.z;
	if (!car->car_sumSurfAllWheels && along >= pulses->length - pulses->half_length) {
		if (!pulses->airborne) {
			pulses->airborne = 1U;
			pulses->air_frame = frame;
		} else if (!pulses->landed) {
			pulses->airborne_turn += delta;
		}
	}
	legacy_u16 progress = extended_owoot_barrel_progress(car, pulses->piece);
	if (!pulses->saw_source && pulses->airborne_turn > 0) {
		progress = (legacy_u16)fmin(pulses->airborne_turn, ANGLE_FULL_TURN);
	}
	if (progress > pulses->progress) {
		pulses->progress = progress;
	}
	pulses->complete |= progress >= ANGLE_FULL_TURN;
	if (pulses->airborne && !pulses->landed && car->car_sumSurfAllWheels &&
		unit_pulses_receiving_road(pulses, car) &&
		!car->car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_SOURCE] &&
		car->car_crashBmpFlag == CRASH_EVENT_NONE) {
		pulses->landed = 1U;
		pulses->landing_frame = frame;
	}
}
void unit_pilot_pulses_before_step(struct UNIT_PILOT_PULSES *pulses, const struct CARSTATE *car,
								   legacy_u32 frame)
{
	unit_opponent_pilot_input_override(0U, 0U);
	if (!pulses || !pulses->valid || !car) {
		return;
	}
	unit_pulses_observe(pulses, car, frame);
	if (pulses->active && pulses->entered && !pulses->landed) {
		struct EXTENDED_BARREL_INPUT input = extended_barrel_search_input(
			&pulses->parameters, pulses->span, frame - pulses->entry_frame);
		unit_opponent_pilot_input_override(input.mask, input.value);
	}
}
static legacy_f64 unit_pulses_fitness(const struct UNIT_PILOT_PULSES *pulses,
									  const struct CARSTATE *car, legacy_u32 frame,
									  legacy_u8 accepted)
{
	legacy_f64 x = car->car_position.lx / UNIT_POSITION_SCALE;
	legacy_f64 y = car->car_position.ly / UNIT_POSITION_SCALE;
	legacy_f64 z = car->car_position.lz / UNIT_POSITION_SCALE;
	legacy_f64 dx = pulses->receiver_end.x - pulses->receiver.x;
	legacy_f64 dz = pulses->receiver_end.z - pulses->receiver.z;
	legacy_f64 length = dx * dx + dz * dz;
	legacy_f64 fraction =
		length > 0.0 ? ((x - pulses->receiver.x) * dx + (z - pulses->receiver.z) * dz) / length
					 : 0.0;
	fraction = fmax(0.0, fmin(1.0, fraction));
	legacy_f64 road_y =
		pulses->receiver.y + fraction * (pulses->receiver_end.y - pulses->receiver.y);
	legacy_f64 lateral = fabs((x - pulses->receiver.x) * pulses->forward_z -
							  (z - pulses->receiver.z) * pulses->forward_x);
	legacy_f64 behind = fmax(0.0, -((x - pulses->receiver.x) * pulses->forward_x +
									(z - pulses->receiver.z) * pulses->forward_z));
	legacy_f64 survived = pulses->landed ? frame - pulses->landing_frame : 0U;
	/* Ranking never accepts a maneuver. Native rule, contact, penalty, and
	 * complete receiving-piece validation remain the manager's responsibility. */
	return (pulses->complete ? UNIT_PULSE_FULL_ROLL_SCORE
							 : pulses->progress * UNIT_PULSE_ANGLE_WEIGHT) +
		   (accepted ? UNIT_PULSE_SETTLED_SCORE : 0.0) + survived * UNIT_PULSE_SURVIVAL_WEIGHT -
		   lateral * UNIT_PULSE_LATERAL_WEIGHT -
		   fabs(y - road_y - UNIT_PULSE_BODY_HEIGHT) * UNIT_PULSE_HEIGHT_WEIGHT -
		   behind * UNIT_PULSE_DISTANCE_WEIGHT;
}
void unit_pilot_pulses_finish_trial(struct UNIT_PILOT_PULSES *pulses, const struct CARSTATE *car,
									legacy_u32 frame, legacy_u8 accepted)
{
	unit_opponent_pilot_input_override(0U, 0U);
	if (!pulses || !pulses->valid || !car) {
		return;
	}
	unit_pulses_observe(pulses, car, frame);
	if (!pulses->entered) {
		return;
	}
	legacy_f64 fitness = unit_pulses_fitness(pulses, car, frame, accepted);
	pulses->last_trial.parameters = pulses->parameters;
	pulses->last_trial.profile = pulses->current;
	pulses->last_trial.attempts = pulses->attempts;
	pulses->last_trial.span = pulses->span;
	pulses->last_trial.bank = pulses->bank;
	pulses->last_trial.fitness = fitness;
	pulses->last_trial.progress = pulses->progress;
	pulses->last_trial.active = pulses->active;
	pulses->has_last_trial = 1U;
	if (pulses->active) {
		/* An exact historical retry may use a different scalar baseline. Its
		 * score cannot rank relative genes interpreted against the new baseline. */
		if (!pulses->known_trial) {
			extended_barrel_search_observe(&pulses->search, &pulses->parameters, fitness);
		}
		if (accepted) {
			pulses->known_parameters = pulses->parameters;
			pulses->known_profile = pulses->current;
			pulses->known_valid = 1U;
			pulses->known_span = pulses->span;
			pulses->known_bank = pulses->bank;
		}
	} else if (!pulses->best_valid || fitness > pulses->best_fitness) {
		pulses->best = pulses->current;
		pulses->best_fitness = fitness;
		pulses->best_valid = 1U;
		pulses->best_span = pulses->airborne ? pulses->air_frame - pulses->entry_frame
											 : pulses->frame_rate * UNIT_PULSE_DEFAULT_SPAN_SECONDS;
		if (pulses->best_span < pulses->frame_rate) {
			pulses->best_span = pulses->frame_rate;
		}
		pulses->best_bank =
			pulses->banked ? pulses->bank_frame - pulses->entry_frame : pulses->best_span / 2U;
	}
}
legacy_int unit_pilot_pulses_next(struct UNIT_PILOT_PULSES *pulses,
								  struct UNIT_PILOT_PULSE_PROFILE *profile)
{
	if (!pulses || !pulses->valid || !pulses->best_valid || !profile ||
		pulses->attempts >= UNIT_PILOT_PULSE_ATTEMPT_LIMIT) {
		return 0;
	}
	pulses->span = pulses->best_span;
	pulses->bank = pulses->best_bank;
	pulses->known_trial = pulses->known_pending;
	if (pulses->known_pending) {
		pulses->parameters = pulses->known_parameters;
		pulses->span = pulses->known_span;
		pulses->bank = pulses->known_bank;
		*profile = pulses->known_profile;
		pulses->known_pending = 0U;
	} else {
		struct EXTENDED_BARREL_PARAMETERS baseline = {0};
		baseline.speed = 1.0;
		pulses->parameters = extended_barrel_search_next(
			&pulses->search, pulses->span, pulses->bank, UNIT_PULSE_SEARCH_FIELDS, &baseline);
		*profile = pulses->best;
		profile->speed =
			fmax(fmin(UNIT_PULSE_MINIMUM_SPEED, pulses->maximum_speed),
				 fmin(pulses->maximum_speed, pulses->best.speed * pulses->parameters.speed));
		profile->exit_speed =
			pulses->best.speed > 0.0
				? fmin(pulses->maximum_speed,
					   pulses->best.exit_speed * profile->speed / pulses->best.speed)
				: 0.0;
		profile->barrel_offset =
			fmax(-UNIT_PULSE_LATERAL_LIMIT,
				 fmin(UNIT_PULSE_LATERAL_LIMIT,
					  pulses->best.barrel_offset + pulses->parameters.lateral));
	}
	pulses->attempts++;
	pulses->active = 1U;
	return 1;
}
legacy_int unit_pilot_pulses_active(const struct UNIT_PILOT_PULSES *pulses)
{
	return pulses && pulses->valid && pulses->active;
}
legacy_int unit_pilot_pulses_last_trial(const struct UNIT_PILOT_PULSES *pulses,
										struct UNIT_PILOT_PULSE_DIAGNOSTIC *diagnostic)
{
	if (!pulses || !diagnostic || !pulses->has_last_trial) {
		return 0;
	}
	*diagnostic = pulses->last_trial;
	return 1;
}
#endif
