#include "unit_learning_key.h"

#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
#include "unit_opponent_internal.h"
#include "../externs.h"
#include "../car/car_resources.h"
#include "../driving_rules.h"
#include "../state_internal.h"
#include "../track/trackdata_layout.h"
#include "../extended/extended_replay_runtime.h"

#define UNIT_LEARNING_AERO_WORDS (TRACKDATA_AERO_TABLE_SIZE / SIMD_RESOURCE_WORD_SIZE)

#define UNIT_LEARNING_FIELD(key, data, field) unit_learning_key_u32(key, (legacy_u32)(data)->field)

static void unit_learning_car_key(struct UNIT_LEARNING_KEY *key)
{
	const struct SIMD *car = &simd_opponent;
	unit_learning_key_bytes(key, gameconfig.game_opponentcarid, REPLAY_CAR_ID_SIZE);
	UNIT_LEARNING_FIELD(key, car, num_gears);
	UNIT_LEARNING_FIELD(key, car, car_mass);
	UNIT_LEARNING_FIELD(key, car, braking_eff);
	UNIT_LEARNING_FIELD(key, car, idle_rpm);
	UNIT_LEARNING_FIELD(key, car, downshift_rpm);
	UNIT_LEARNING_FIELD(key, car, upshift_rpm);
	UNIT_LEARNING_FIELD(key, car, max_rpm);
	UNIT_LEARNING_FIELD(key, car, aero_resistance);
	UNIT_LEARNING_FIELD(key, car, idle_torque);
	UNIT_LEARNING_FIELD(key, car, grip);
	UNIT_LEARNING_FIELD(key, car, sliding);
	UNIT_LEARNING_FIELD(key, car, car_height);
	unit_learning_key_bytes(key, car->torque_curve, SIMD_TORQUE_CURVE_SIZE);
	unit_learning_key_bytes(key, car->reserved_grip_bytes, SIMD_RESERVED_GRIP_SIZE);
	for (legacy_u32 index = 0; index < SIMD_GEAR_RATIO_COUNT; index++) {
		unit_learning_key_u32(key, car->gear_ratios[index]);
	}
	for (legacy_u32 index = 0; index < SIMD_KNOB_POINT_COUNT; index++) {
		unit_learning_key_u32(key, (legacy_u32)car->knob_points[index].px);
		unit_learning_key_u32(key, (legacy_u32)car->knob_points[index].py);
	}
	for (legacy_u32 index = 0; index < SIMD_RESERVED_HANDLING_COUNT; index++) {
		unit_learning_key_u32(key, (legacy_u32)car->reserved_handling_words[index]);
	}
	for (legacy_u32 index = 0; index < SIMD_SURFACE_GRIP_COUNT; index++) {
		unit_learning_key_u32(key, (legacy_u32)car->surface_grip[index]);
	}
	for (legacy_u32 index = 0; index < SIMD_COLLISION_POINT_COUNT; index++) {
		unit_learning_key_u32(key, (legacy_u32)car->collide_points[index].px);
		unit_learning_key_u32(key, (legacy_u32)car->collide_points[index].py);
	}
	for (legacy_u32 index = 0; index < SIMD_WHEEL_COORDINATE_COUNT; index++) {
		unit_learning_key_u32(key, (legacy_u32)car->wheel_coords[index].x);
		unit_learning_key_u32(key, (legacy_u32)car->wheel_coords[index].y);
		unit_learning_key_u32(key, (legacy_u32)car->wheel_coords[index].z);
	}
	if (car->aerorestable) {
		for (legacy_u32 index = 0; index < UNIT_LEARNING_AERO_WORDS; index++) {
			unit_learning_key_u32(key, (legacy_u32)car->aerorestable[index]);
		}
	}
	unit_learning_key_u32(key, car_steering_rate[OPPONENT_CAR_INDEX]);
	unit_learning_key_u32(key, car_surface_adhesion[OPPONENT_CAR_INDEX]);
	const struct CAR_SURFACE_ADHESION_SETTINGS *adhesion =
		&car_surface_adhesion_settings[OPPONENT_CAR_INDEX];
	UNIT_LEARNING_FIELD(key, adhesion, max_distance);
	UNIT_LEARNING_FIELD(key, adhesion, min_alignment);
	UNIT_LEARNING_FIELD(key, adhesion, surface_mask);
}

static void unit_learning_track_key(struct UNIT_LEARNING_KEY *key)
{
	const struct CSTRK_TRACK *track = extended_track_model();
	if (!extended_track_active() || !track) {
		unit_learning_key_bytes(key, track_element_map, TRACK_GRID_SIZE * TRACK_GRID_SIZE);
		unit_learning_key_bytes(key, track_terrain_map, TRACK_GRID_SIZE * TRACK_GRID_SIZE);
		return;
	}
	UNIT_LEARNING_FIELD(key, track, version);
	UNIT_LEARNING_FIELD(key, track, width);
	UNIT_LEARNING_FIELD(key, track, height);
	unit_learning_key_bytes(key, track->corner_heights, (track->width + 1U) * (track->height + 1U));
	for (legacy_u32 index = 0; index < (legacy_u32)track->width * track->height; index++) {
		const struct CSTRK_CELL *cell = &track->cells[index];
		UNIT_LEARNING_FIELD(key, cell, surface);
		UNIT_LEARNING_FIELD(key, cell, half_water_corner);
		UNIT_LEARNING_FIELD(key, cell, corner_surface);
	}
	UNIT_LEARNING_FIELD(key, track, object_count);
	for (legacy_u32 index = 0; index < track->object_count; index++) {
		const struct CSTRK_OBJECT *object = &track->objects[index];
		UNIT_LEARNING_FIELD(key, object, legacy_id);
		UNIT_LEARNING_FIELD(key, object, x);
		UNIT_LEARNING_FIELD(key, object, y);
		UNIT_LEARNING_FIELD(key, object, rotation);
		UNIT_LEARNING_FIELD(key, object, surface);
		UNIT_LEARNING_FIELD(key, object, width);
		UNIT_LEARNING_FIELD(key, object, height);
		UNIT_LEARNING_FIELD(key, object, occupied_mask);
		UNIT_LEARNING_FIELD(key, object, base_height);
		UNIT_LEARNING_FIELD(key, object, slope_x);
		UNIT_LEARNING_FIELD(key, object, slope_y);
		UNIT_LEARNING_FIELD(key, object, lower_surface);
		UNIT_LEARNING_FIELD(key, object, tunnel_slalom);
		UNIT_LEARNING_FIELD(key, object, roll_ramp);
		UNIT_LEARNING_FIELD(key, object, pine_count);
	}
}

legacy_int unit_learning_key_current(const struct UNIT_ROUTE *route, struct UNIT_LEARNING_KEY *key)
{
	if (!route || !route->count || !key || !track_element_map || !track_terrain_map) {
		return 0;
	}
	unit_learning_key_begin(key);
	unit_learning_key_u32(key, UNIT_LEARNING_ALGORITHM_VERSION);
	unit_learning_key_u32(key, CSRPL_VERSION);
	unit_learning_key_u32(key, extended_replay_version());
	unit_learning_key_u32(key, driving_rules_current());
	unit_learning_key_u32(key, (legacy_u16)framespersec);
	unit_learning_key_u32(key, (legacy_u8)gameconfig.game_opponenttransmission);
	unit_learning_key_u32(key, route->classic_track);
	unit_learning_car_key(key);
	unit_learning_track_key(key);
	unit_learning_key_u32(key, route->count);
	unit_learning_key_u32(key, route->finish_begin);
	unit_learning_key_u32(key, route->verified_count);
	for (legacy_u32 index = 0; index < route->count; index++) {
		const struct UNIT_ROUTE_POINT *point = &route->points[index];
		unit_learning_key_f64(key, point->x);
		unit_learning_key_f64(key, point->y);
		unit_learning_key_f64(key, point->z);
		unit_learning_key_f64(key, point->half_width);
		unit_learning_key_f64(key, point->speed_limit);
		unit_learning_key_f64(key, point->speed_floor);
		unit_learning_key_f64(key, point->fallback_speed_limit);
		UNIT_LEARNING_FIELD(key, point, route_index);
		UNIT_LEARNING_FIELD(key, point, route_point);
		UNIT_LEARNING_FIELD(key, point, surface);
		UNIT_LEARNING_FIELD(key, point, shortcut_safe);
		UNIT_LEARNING_FIELD(key, point, height_unspecified);
		UNIT_LEARNING_FIELD(key, point, stunt);
		UNIT_LEARNING_FIELD(key, point, banked_corner);
		if (point->bank_support) {
			unit_learning_key_u32(key, index);
			UNIT_LEARNING_FIELD(key, point, bank_support);
		}
		UNIT_LEARNING_FIELD(key, point, clearance_only);
		UNIT_LEARNING_FIELD(key, point, align_center);
	}
	return 1;
}
#endif
