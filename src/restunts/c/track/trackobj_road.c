/* Select native road, ramp, elevated and banked driving surfaces. */
#include "../restunts.h"
#include "../legacy.h"
#include "../math/math.h"
#include "trackdata_layout.h"
#include "track_types.h"
#include "track_objects.h"
#include "track_collision.h"
#include "trackobj_internal.h"
#include "../physics/physics_internal.h"
#include "../owoot/owoot_road.h"
#include "../externs.h"
#ifdef RESTUNTS_SDL3
#include "../extended/extended_trackobj.h"
#endif

#include "trackobj_model_internal.h"

legacy_s16 track_interpolate(legacy_s16 position, legacy_s16 position0, legacy_s16 position1,
							 legacy_s16 value0, legacy_s16 value1)
{
	legacy_s32 numerator = (legacy_s32)LEGACY_S16_WRAP_SUB(position, position0) *
						   (legacy_s32)LEGACY_S16_WRAP_SUB(value1, value0);
	legacy_s16 denominator = LEGACY_S16_WRAP_SUB(position1, position0);
	legacy_s32 quotient = LEGACY_S32_DIV_OR_ZERO(numerator, (legacy_s32)denominator);
	return LEGACY_S16_WRAP_ADD(value0, LEGACY_S16_FROM_BITS((legacy_u16)quotient));
}

void track_rotate_local(struct VECTOR *position, legacy_s16 orientation)
{
	legacy_s16 old_x = position->x;
	switch ((legacy_u16)orientation) {
		case ANGLE_QUARTER_TURN:
			position->x = LEGACY_S16_WRAP_NEGATE(position->z);
			position->z = old_x;
			break;
		case ANGLE_HALF_TURN:
			position->x = LEGACY_S16_WRAP_NEGATE(position->x);
			position->z = LEGACY_S16_WRAP_NEGATE(position->z);
			break;
		case ANGLE_THREE_QUARTER_TURN:
			position->x = position->z;
			position->z = LEGACY_S16_WRAP_NEGATE(old_x);
			break;
	}
}

/* A multi-tile element keeps its origin on the shared tile edge, so the
 * flags decide whether the centre comes from the tile border or its middle. */
void track_object_tile_center(legacy_u8 track_tile, legacy_s16 row_index, legacy_s16 column_index)
{
	legacy_u8 multi_tile = (legacy_u8)trkObjectList[track_tile].ss_multiTileFlag;
	if ((multi_tile & MULTI_TILE_ROW_EDGE_FLAG) != 0) {
		elem_zCenter = (legacy_s16)terrainpos[row_index];
	}
	if ((multi_tile & MULTI_TILE_COLUMN_EDGE_FLAG) != 0) {
		elem_xCenter = (legacy_s16)track_column_position((legacy_u16)column_index);
	}
}

/* Rectangular buildings share one wall test: the wall the car meets is the
 * side of the footprint its next position crosses. The two x tests and the
 * two z tests are mutually exclusive, so their order does not matter. */
void track_object_building_wall(const struct VECTOR *next_position, legacy_s16 height,
								legacy_s16 x_min, legacy_s16 x_max, legacy_s16 z_min,
								legacy_s16 z_max, legacy_s16 wall_z_min, legacy_s16 wall_z_max,
								legacy_s16 wall_x_min, legacy_s16 wall_x_max)
{
	wallHeight = height;
	if (next_position->z <= z_min) {
		wallindex = wall_z_min;
	} else if (next_position->z >= z_max) {
		wallindex = wall_z_max;
	} else if (next_position->x <= x_min) {
		wallindex = wall_x_min;
	} else if (next_position->x >= x_max) {
		wallindex = wall_x_max;
	}
}

/* A corner surface is the ring between two radii around the arc centre. */
legacy_s16 track_radius_in_band(legacy_s16 x, legacy_s16 z, legacy_s16 low, legacy_s16 high)
{
	legacy_s16 radius = (legacy_s16)polarRadius2D(x, z);
	return radius > low && radius < high;
}

/* Elevated and banked corners share one arc, centred on the tile corner:
 * the radius is measured against the 1536-unit centre line, and the shape index
 * comes from the same angle quantised into the 18 segments of the arc. */
legacy_s16 track_arc_radius(const struct VECTOR *position)
{
	return LEGACY_S16_WRAP_SUB(
		(legacy_s16)polarRadius2D(LEGACY_S16_WRAP_ADD(position->x, TRACK_ARC_CENTER_OFFSET),
								  LEGACY_S16_WRAP_ADD(position->z, TRACK_ARC_CENTER_OFFSET)),
		TRACK_ARC_CENTER_RADIUS);
}

static legacy_s16 track_arc_segment(const struct VECTOR *position)
{
	legacy_s16 value =
		(legacy_s16)((((legacy_u16)polarAngle(
						   LEGACY_S16_WRAP_ADD(position->x, TRACK_ARC_CENTER_OFFSET),
						   LEGACY_S16_WRAP_ADD(position->z, TRACK_ARC_CENTER_OFFSET)) &
					   ANGLE_QUARTER_MASK) *
					  TRACK_ARC_SEGMENT_COUNT) >>
					 TRACK_ARC_ANGLE_SCALE_SHIFT);
	return LEGACY_S16_WRAP_NEGATE(LEGACY_S16_WRAP_SUB(value, TRACK_ARC_LAST_SEGMENT));
}

enum CHICANE_FAST_GRASS_MODE { CHICANE_FAST_GRASS_OFF = 0, CHICANE_FAST_GRASS_ON = 1 };

static legacy_s16 chicane_fast_grass_enabled = CHICANE_FAST_GRASS_ON;

legacy_u16 chicane_fast_grass_is_enabled(void)
{
	return chicane_fast_grass_enabled != 0;
}

void configure_chicane_fast_grass(legacy_s16 argc, legacy_s8 *argv[])
{
	static const legacy_s8 off_option[] = "--cfg:off";
	static const legacy_s8 on_option[] = "--cfg:on";
	chicane_fast_grass_enabled = CHICANE_FAST_GRASS_ON;
	for (legacy_s16 index = 1; index < argc; index++) {
		if (stricmp(argv[index], off_option) == 0) {
			chicane_fast_grass_enabled = CHICANE_FAST_GRASS_OFF;
		} else if (stricmp(argv[index], on_option) == 0) {
			chicane_fast_grass_enabled = CHICANE_FAST_GRASS_ON;
		}
	}
}

/* One collision query in the element's local coordinates. Keep this on the
 * caller's stack: handlers only share the query and the existing collision
 * outputs, so simultaneous calls do not acquire a new static work area. */

void track_object_straight(struct TRACK_OBJECT_SAMPLE *sample)
{
	if (sample->physical_model == PHYSICAL_MODEL_START_FINISH &&
		state.game_inputmode == GAME_INPUT_MODE_WAITING && sample->position.x > 0) {
		if (sample->position.z < START_FINISH_FAR_Z) {
			planindex = START_FINISH_FAR_PLANE_INDEX;
		} else if (sample->position.z < START_FINISH_NEAR_Z) {
			planindex = START_FINISH_NEAR_PLANE_INDEX;
		}
	}
	if (sample->absolute_x < ROAD_HALF_WIDTH) {
		current_surf_type = (legacy_u8)sample->surface_type;
	}
}

void track_object_crossroad(struct TRACK_OBJECT_SAMPLE *sample)
{
	if (sample->absolute_x < ROAD_HALF_WIDTH || sample->absolute_z < ROAD_HALF_WIDTH) {
		current_surf_type = (legacy_u8)sample->surface_type;
	}
}

void track_object_large_corner(struct TRACK_OBJECT_SAMPLE *sample)
{
	if (track_radius_in_band(LEGACY_S16_WRAP_ADD(sample->position.x, TRACK_ARC_CENTER_OFFSET),
							 LEGACY_S16_WRAP_ADD(sample->position.z, TRACK_ARC_CENTER_OFFSET),
							 LARGE_CORNER_INNER_RADIUS, LARGE_CORNER_OUTER_RADIUS)) {
		current_surf_type = (legacy_u8)sample->surface_type;
	}
}

void track_object_chicane(struct TRACK_OBJECT_SAMPLE *sample)
{
	if (chicane_fast_grass_enabled == CHICANE_FAST_GRASS_OFF) {
		/* Use the visible road footprint: the old circular approximation
		 * cuts across the chicane's rendered curves and their edges. Preserve
		 * the sampled background terrain outside the road. */
		if (track_road_contains_local_point(sample->physical_model, &sample->position)) {
			current_surf_type = (legacy_u8)sample->surface_type;
		}
		return;
	}
	if (sample->physical_model == PHYSICAL_MODEL_CHICANE_LEFT_RIGHT) {
		sample->position.x = LEGACY_S16_WRAP_NEGATE(sample->position.x);
	}
	current_surf_type = (legacy_u8)sample->surface_type;
	if (sample->position.x > 0) {
		sample->position.z = LEGACY_S16_WRAP_NEGATE(sample->position.z);
		sample->position.x = LEGACY_S16_WRAP_NEGATE(sample->position.x);
	}
	track_object_large_corner(sample);
}

void track_object_sharp_corner(struct TRACK_OBJECT_SAMPLE *sample)
{
	if (sample->physical_model != PHYSICAL_MODEL_SHARP_CORNER &&
		sample->absolute_x < ROAD_HALF_WIDTH) {
		current_surf_type = (legacy_u8)sample->surface_type;
		return;
	}
	legacy_s16 x = sample->physical_model == PHYSICAL_MODEL_SHARP_SPLIT_B
					   ? LEGACY_S16_WRAP_SUB(SHARP_CORNER_CENTER_OFFSET, sample->position.x)
					   : LEGACY_S16_WRAP_ADD(sample->position.x, SHARP_CORNER_CENTER_OFFSET);
	if (track_radius_in_band(x, LEGACY_S16_WRAP_ADD(sample->position.z, SHARP_CORNER_CENTER_OFFSET),
							 SHARP_CORNER_INNER_RADIUS, SHARP_CORNER_OUTER_RADIUS)) {
		current_surf_type = (legacy_u8)sample->surface_type;
	}
}

void track_object_large_split(struct TRACK_OBJECT_SAMPLE *sample)
{
	legacy_s16 x;

	if (sample->physical_model == PHYSICAL_MODEL_LARGE_SPLIT_A) {
		if (sample->position.x >= SPLIT_STRAIGHT_LANE_INNER_X &&
			sample->position.x <= SPLIT_STRAIGHT_LANE_OUTER_X) {
			current_surf_type = (legacy_u8)sample->surface_type;
			return;
		}
		x = LEGACY_S16_WRAP_ADD(sample->position.x, TRACK_ARC_CENTER_OFFSET);
	} else {
		if (sample->position.x >= -SPLIT_STRAIGHT_LANE_OUTER_X &&
			sample->position.x <= -SPLIT_STRAIGHT_LANE_INNER_X) {
			current_surf_type = (legacy_u8)sample->surface_type;
			return;
		}
		x = LEGACY_S16_WRAP_SUB(TRACK_ARC_CENTER_OFFSET, sample->position.x);
	}
	if (track_radius_in_band(x, LEGACY_S16_WRAP_ADD(sample->position.z, TRACK_ARC_CENTER_OFFSET),
							 LARGE_CORNER_INNER_RADIUS, LARGE_CORNER_OUTER_RADIUS)) {
		current_surf_type = (legacy_u8)sample->surface_type;
	}
}

void track_object_highway_entrance(struct TRACK_OBJECT_SAMPLE *sample)
{
	legacy_s16 value = absolute_word(sample->position.x);
	legacy_u16 index = 0;
	while (highEntrZBounds1[index] < sample->position.z) {
		index++;
	}
	legacy_s16 value2 = highEntrXInnBounds0[index];
	if (highEntrXInnBounds1[index] != value2) {
		value2 =
			track_interpolate(sample->position.z, highEntrZBounds0[index], highEntrZBounds1[index],
							  highEntrXInnBounds0[index], highEntrXInnBounds1[index]);
	}
	legacy_s16 value3 = highEntrXOutBounds0[index];
	if (highEntrXOutBounds1[index] != value3) {
		value3 =
			track_interpolate(sample->position.z, highEntrZBounds0[index], highEntrZBounds1[index],
							  highEntrXOutBounds0[index], highEntrXOutBounds1[index]);
	}
	if (value > value2 && value < value3) {
		current_surf_type = (legacy_u8)sample->surface_type;
		return;
	}
	if (sample->position.z >= 0 && value <= ROAD_HALF_WIDTH) {
		planindex = HIGHWAY_PLANE_INDEX;
		if (sample->position.z >= HIGHWAY_MERGE_END_Z) {
			if (sample->next_position.x <= -ROAD_HALF_WIDTH) {
				wallindex = HIGHWAY_FAR_LEFT_WALL_INDEX;
			} else if (sample->next_position.x >= ROAD_HALF_WIDTH) {
				wallindex = HIGHWAY_FAR_RIGHT_WALL_INDEX;
			}
		} else {
			wallindex = sample->next_position.x < 0 ? HIGHWAY_NEAR_LEFT_WALL_INDEX
													: HIGHWAY_NEAR_RIGHT_WALL_INDEX;
		}
	}
}

void track_object_highway(struct TRACK_OBJECT_SAMPLE *sample)
{
	if (sample->absolute_x <= HIGHWAY_OUTER_HALF_WIDTH) {
		if (sample->absolute_x > ROAD_HALF_WIDTH) {
			current_surf_type = (legacy_u8)sample->surface_type;
		} else {
			planindex = HIGHWAY_PLANE_INDEX;
			if (sample->next_position.x <= -ROAD_HALF_WIDTH) {
				wallindex = HIGHWAY_FAR_LEFT_WALL_INDEX;
			} else if (sample->next_position.x >= ROAD_HALF_WIDTH) {
				wallindex = HIGHWAY_FAR_RIGHT_WALL_INDEX;
			}
		}
	}
}

static void track_object_ramp_end_wall(struct TRACK_OBJECT_SAMPLE *sample)
{
	if (sample->physical_model == PHYSICAL_MODEL_RAMP) {
		if (sample->position.z > 0) {
			track_wall_collision_enabled = 0;
		} else if (sample->next_position.z >= 0) {
			wallindex = RAMP_ENTRY_WALL_INDEX;
		}
	}
	if (sample->physical_model == PHYSICAL_MODEL_SOLID_RAMP &&
		sample->next_position.z >= ELEVATED_ROAD_END_Z) {
		wallindex = ELEVATED_FORWARD_WALL_INDEX;
	}
}

void track_object_ramp(struct TRACK_OBJECT_SAMPLE *sample)
{
	track_object_ramp_end_wall(sample);

	if (absolute_word(sample->next_position.x) < ROAD_HALF_WIDTH) {
		planindex = RAMP_PLANE_INDEX;
		current_surf_type = (legacy_u8)sample->surface_type;
		if (wallindex < 0 && sample->position.z >= 0 && sample->absolute_x >= ROAD_HALF_WIDTH) {
			wallHeight = ELEVATED_SIDE_WALL_HEIGHT;
			elRdWallRelated = ELEVATED_WALL_VERTICAL_OFFSET;
			wallindex =
				sample->position.x < 0 ? ELEVATED_LEFT_WALL_INDEX : ELEVATED_RIGHT_WALL_INDEX;
		}
	} else if (track_wall_collision_enabled != 0 && sample->absolute_x <= ROAD_HALF_WIDTH) {
		planindex = RAMP_PLANE_INDEX;
		if (wallindex < 0) {
			sample->wall_orientation_modifier = ANGLE_HALF_TURN;
			wallindex =
				sample->position.x < 0 ? ELEVATED_LEFT_WALL_INDEX : ELEVATED_RIGHT_WALL_INDEX;
		}
	}
}

static legacy_s16 track_object_below_elevated_road(struct TRACK_OBJECT_SAMPLE *sample)
{
	if (sample->physical_model == PHYSICAL_MODEL_OVERPASS) {
		if (sample->height <= ELEVATED_DECK_CLEARANCE) {
			if (sample->absolute_z <= ROAD_HALF_WIDTH) {
				current_surf_type = (legacy_u8)sample->surface_type;
			}
			return 1;
		}
		track_wall_collision_enabled = 0;
	} else if (sample->physical_model != PHYSICAL_MODEL_SOLID_ROAD) {
		if (sample->height <= ELEVATED_DECK_CLEARANCE) {
			return 1;
		}
		track_wall_collision_enabled = 0;
	}
	return 0;
}

void track_object_elevated_road(struct TRACK_OBJECT_SAMPLE *sample)
{
	if (track_object_below_elevated_road(sample)) {
		return;
	}

	if (absolute_word(sample->next_position.x) <= ROAD_HALF_WIDTH) {
		planindex = ELEVATED_PLANE_INDEX;
		current_surf_type = (legacy_u8)sample->surface_type;
		if (track_wall_collision_enabled != 0) {
			if (sample->next_position.z >= ELEVATED_ROAD_END_Z) {
				wallindex = ELEVATED_FORWARD_WALL_INDEX;
			} else if (sample->next_position.z <= -ELEVATED_ROAD_END_Z) {
				wallindex = ELEVATED_REAR_WALL_INDEX;
			}
		}
		if (sample->absolute_x >= ROAD_HALF_WIDTH) {
			wallHeight = ELEVATED_SIDE_WALL_HEIGHT;
			wallindex =
				sample->position.x < 0 ? ELEVATED_LEFT_WALL_INDEX : ELEVATED_RIGHT_WALL_INDEX;
		}
	} else if (track_wall_collision_enabled != 0 && sample->absolute_x <= ROAD_HALF_WIDTH) {
		planindex = ELEVATED_PLANE_INDEX;
		wallHeight = ELEVATED_SIDE_WALL_HEIGHT;
		sample->wall_orientation_modifier = ANGLE_HALF_TURN;
		wallindex =
			sample->next_position.x < 0 ? ELEVATED_LEFT_WALL_INDEX : ELEVATED_RIGHT_WALL_INDEX;
	}
}

void track_object_elevated_corner(struct TRACK_OBJECT_SAMPLE *sample)
{
	if (sample->height <= ELEVATED_DECK_CLEARANCE) {
		return;
	}
	legacy_s16 radius = track_arc_radius(&sample->position);
	if (radius <= -ELEVATED_CORNER_OUTER_OFFSET || radius >= ELEVATED_CORNER_OUTER_OFFSET) {
		return;
	}
	current_surf_type = (legacy_u8)sample->surface_type;
	planindex = ELEVATED_PLANE_INDEX;
	track_wall_collision_enabled = 0;
	if (radius >= -ELEVATED_CORNER_INNER_OFFSET && radius <= ELEVATED_CORNER_INNER_OFFSET) {
		return;
	}
	legacy_s16 value = track_arc_segment(&sample->position);
	wallHeight = ELEVATED_SIDE_WALL_HEIGHT;
	elRdWallRelated = ELEVATED_WALL_VERTICAL_OFFSET;
	wallindex = LEGACY_S16_WRAP_ADD(value, radius < 0 ? ELEVATED_CORNER_INNER_WALL_BASE
													  : ELEVATED_CORNER_OUTER_WALL_BASE);
}

void track_object_banked_entrance(struct TRACK_OBJECT_SAMPLE *sample)
{
	legacy_s16 terrain_angle;
	legacy_s16 value;
	legacy_s16 value2;
	if (sample->physical_model == PHYSICAL_MODEL_BANKED_ENTRANCE_A) {
		value = BANKED_ENTRANCE_A_PLAN_BASE;
		value2 = BANKED_ENTRANCE_A_VARIANT;
		terrain_angle = BANKED_ENTRANCE_A_ANGLE;
	} else {
		value = BANKED_ENTRANCE_B_PLAN_BASE;
		value2 = BANKED_ENTRANCE_B_VARIANT;
		terrain_angle = BANKED_ENTRANCE_B_ANGLE;
	}
	if (sample->absolute_x > ROAD_HALF_WIDTH) {
		return;
	}
	if (value2 == BANKED_ENTRANCE_A_VARIANT && sample->next_position.x <= -ROAD_HALF_WIDTH) {
		sample->wall_orientation_modifier = ANGLE_HALF_TURN;
		wallindex = ELEVATED_LEFT_WALL_INDEX;
	} else if (value2 == BANKED_ENTRANCE_B_VARIANT && sample->next_position.x >= ROAD_HALF_WIDTH) {
		sample->wall_orientation_modifier = ANGLE_HALF_TURN;
		wallindex = ELEVATED_RIGHT_WALL_INDEX;
	}
	current_surf_type = (legacy_u8)sample->surface_type;
	if (sample->position.z < -BANKED_ENTRANCE_END_Z) {
		planindex = value;
		return;
	}
	if (sample->position.z >= BANKED_ENTRANCE_END_Z) {
		planindex = LEGACY_S16_WRAP_ADD(value, BANKED_ENTRANCE_POSITIVE_END_PLAN_OFFSET);
		return;
	}
	legacy_u16 index;
	if (sample->position.z < -BANKED_ENTRANCE_INNER_Z) {
		planindex = LEGACY_S16_WRAP_ADD(value, BANKED_ENTRANCE_NEGATIVE_OUTER_PLAN_OFFSET);
		index = BANKED_ENTRANCE_NEGATIVE_OUTER_ADJUST_INDEX;
	} else if (sample->position.z < 0) {
		planindex = LEGACY_S16_WRAP_ADD(value, BANKED_ENTRANCE_NEGATIVE_INNER_PLAN_OFFSET);
		index = BANKED_ENTRANCE_NEGATIVE_INNER_ADJUST_INDEX;
	} else if (sample->position.z < BANKED_ENTRANCE_INNER_Z) {
		planindex = LEGACY_S16_WRAP_ADD(value, BANKED_ENTRANCE_POSITIVE_INNER_PLAN_OFFSET);
		index = BANKED_ENTRANCE_POSITIVE_INNER_ADJUST_INDEX;
	} else {
		planindex = LEGACY_S16_WRAP_ADD(value, BANKED_ENTRANCE_POSITIVE_OUTER_PLAN_OFFSET);
		index = BANKED_ENTRANCE_POSITIVE_OUTER_ADJUST_INDEX;
	}
	legacy_s16 value3 = LEGACY_S16_WRAP_SUB(sample->position.z, bkRdEntr_triang_zAdjust[index]);
	value3 = LEGACY_S16_WRAP_ADD(
		multiply_and_scale(sin_fast((legacy_u16)terrain_angle), value3),
		multiply_and_scale(cos_fast((legacy_u16)terrain_angle), sample->position.x));
	if (value3 > 0) {
		planindex = LEGACY_S16_WRAP_ADD(planindex, BANKED_ENTRANCE_SECOND_TRIANGLE_PLAN_OFFSET);
	}
}

void track_object_banked_road(struct TRACK_OBJECT_SAMPLE *sample)
{
	if (sample->absolute_x <= ROAD_HALF_WIDTH) {
		current_surf_type = (legacy_u8)sample->surface_type;
		planindex = BANKED_ROAD_PLANE_INDEX;
		if (sample->next_position.x >= ROAD_HALF_WIDTH) {
			sample->wall_orientation_modifier = ANGLE_HALF_TURN;
			wallindex = ELEVATED_RIGHT_WALL_INDEX;
		}
	}
}

void track_object_banked_corner(struct TRACK_OBJECT_SAMPLE *sample)
{
	legacy_s16 radius = track_arc_radius(&sample->position);
	if (radius <= -BANKED_CORNER_INNER_OFFSET || radius >= BANKED_CORNER_OUTER_OFFSET) {
		return;
	}
	legacy_s16 value = track_arc_segment(&sample->position);
	planindex = LEGACY_S16_WRAP_ADD(value, BANKED_CORNER_PLAN_BASE);
	current_surf_type = (legacy_u8)sample->surface_type;
	if (radius > BANKED_CORNER_WALL_THRESHOLD) {
		sample->wall_orientation_modifier = ANGLE_HALF_TURN;
		wallindex = LEGACY_S16_WRAP_ADD(value, BANKED_CORNER_WALL_BASE);
		track_wall_collision_enabled = 0;
	}
}
