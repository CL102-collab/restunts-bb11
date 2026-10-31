/* Select curved stunt surfaces and rectangular scenery collision walls. */
#include "../restunts.h"
#include "../legacy.h"
#include "../math.h"
#include "trackdata_layout.h"
#include "track_types.h"
#include "track_objects.h"
#include "track_collision.h"
#include "trackobj_internal.h"
#include "../physics_internal.h"
#include "../owoot/owoot_road.h"
#include "../externs.h"
#ifdef RESTUNTS_SDL3
#include "../extended/extended_trackobj.h"
#endif

#include "trackobj_model_internal.h"

static void track_object_loop_upper(struct TRACK_OBJECT_SAMPLE *sample, legacy_s16 value,
									legacy_s16 effective_x, legacy_s16 value2, legacy_u16 index)
{
	index = (legacy_u16)(LOOP_SURFACE_LAST_INDEX - index);
	if (effective_x < loopSurface_XBounds0[index] ||
		effective_x > LEGACY_S16_WRAP_ADD(loopSurface_XBounds1[index], LOOP_LANE_SEPARATION)) {
		return;
	}
	if (effective_x <= loopSurface_XBounds1[index] ||
		effective_x >= LEGACY_S16_WRAP_ADD(loopSurface_XBounds0[index], LOOP_LANE_SEPARATION)) {
		legacy_s16 value3 =
			track_interpolate(value2, loopSurface_ZBounds0[index], loopSurface_ZBounds1[index],
							  loopSurface_XBounds0[index], loopSurface_XBounds1[index]);
		if (effective_x <= value3 ||
			effective_x >= LEGACY_S16_WRAP_ADD(value3, LOOP_LANE_SEPARATION)) {
			return;
		}
	}
	planindex = LEGACY_S16_WRAP_ADD(value, (legacy_s16)index);
	current_surf_type = (legacy_u8)sample->surface_type;
	track_wall_collision_enabled = 0;
	return;
}

static legacy_s16 track_object_loop_lower(struct TRACK_OBJECT_SAMPLE *sample, legacy_s16 value,
										  legacy_s16 effective_x, legacy_s16 value2,
										  legacy_u16 index)
{
	if (!((index > LOOP_LOW_CLEARANCE_LAST_SEGMENT && sample->height < LOOP_LOW_CLEARANCE) ||
		  effective_x < loopSurface_XBounds0[index] ||
		  effective_x > LEGACY_S16_WRAP_ADD(loopSurface_XBounds1[index], LOOP_LANE_SEPARATION))) {
		if (effective_x > loopSurface_XBounds1[index] &&
			effective_x < LEGACY_S16_WRAP_ADD(loopSurface_XBounds0[index], LOOP_LANE_SEPARATION)) {
			planindex = LEGACY_S16_WRAP_ADD(value, (legacy_s16)index);
			current_surf_type = (legacy_u8)sample->surface_type;
			track_wall_collision_enabled = 0;
			return 1;
		}
		if (loopSurface_XBounds0[index] != loopSurface_XBounds1[index]) {
			legacy_s16 value3 =
				track_interpolate(value2, loopSurface_ZBounds0[index], loopSurface_ZBounds1[index],
								  loopSurface_XBounds0[index], loopSurface_XBounds1[index]);
			if (effective_x > value3 &&
				effective_x < LEGACY_S16_WRAP_ADD(value3, LOOP_LANE_SEPARATION)) {
				planindex = LEGACY_S16_WRAP_ADD(value, (legacy_s16)index);
				current_surf_type = (legacy_u8)sample->surface_type;
				track_wall_collision_enabled = 0;
				return 1;
			}
		}
	}
	return 0;
}

void track_object_loop(struct TRACK_OBJECT_SAMPLE *sample)
{
	legacy_s16 effective_z;
	legacy_s16 value;
	legacy_s16 effective_x;
	if (sample->position.z < 0) {
		value = LOOP_REAR_PLAN_BASE;
		effective_x = LEGACY_S16_WRAP_NEGATE(sample->position.x);
		effective_z = LEGACY_S16_WRAP_NEGATE(sample->position.z);
	} else {
		value = LOOP_FRONT_PLAN_BASE;
		effective_x = sample->position.x;
		effective_z = sample->position.z;
	}
	legacy_u16 index;
	legacy_s16 value2;
	if (effective_z <= LEGACY_S16_WRAP_ADD(loopSurface_maxZ, LOOP_SURFACE_END_PADDING)) {
		if (effective_z > LEGACY_S16_WRAP_SUB(loopSurface_maxZ, 1)) {
			value2 = LEGACY_S16_WRAP_SUB(loopSurface_maxZ, 1);
		} else {
			value2 = effective_z;
		}
		index = 0;
		while (loopSurface_ZBounds1[index] < value2) {
			index++;
		}
		if (sample->height > LOOP_UPPER_HEIGHT_THRESHOLD) {
			track_object_loop_upper(sample, value, effective_x, value2, index);
			return;
		}
		if (track_object_loop_lower(sample, value, effective_x, value2, index)) {
			return;
		}
	}

	index = 0;
	while (loopBase_ZBounds1[index] < effective_z) {
		index++;
	}
	value2 = track_interpolate(effective_z, loopBase_ZBounds0[index], loopBase_ZBounds1[index],
							   loopBae_InnXBounds0[index], loopBase_InnXBounds1[index]);
	legacy_s16 value3 =
		track_interpolate(effective_z, loopBase_ZBounds0[index], loopBase_ZBounds1[index],
						  loopBase_OutXBounds0[index], loopBase_OutXBounds1[index]);
	if (effective_x >= value2 && effective_x <= value3) {
		current_surf_type = (legacy_u8)sample->surface_type;
	}
}

void track_object_tunnel(struct TRACK_OBJECT_SAMPLE *sample)
{
	if (sample->height >= TUNNEL_HEIGHT || sample->next_height >= TUNNEL_HEIGHT) {
		if (sample->absolute_x < TUNNEL_OUTER_HALF_WIDTH) {
			current_surf_type = (legacy_u8)sample->surface_type;
			planindex = TUNNEL_ROOF_PLANE_INDEX;
		}
		return;
	}
	if (sample->absolute_x < ROAD_HALF_WIDTH) {
		current_surf_type = (legacy_u8)sample->surface_type;
	}
	if (sample->position.x >= ROAD_HALF_WIDTH && sample->position.x <= TUNNEL_OUTER_HALF_WIDTH) {
		track_object_building_wall(&sample->next_position, TUNNEL_HEIGHT, ROAD_HALF_WIDTH,
								   TUNNEL_OUTER_HALF_WIDTH, -TUNNEL_END_Z, TUNNEL_END_Z,
								   TUNNEL_REAR_WALL_INDEX, TUNNEL_FORWARD_WALL_INDEX,
								   TUNNEL_RIGHT_INNER_WALL_INDEX, TUNNEL_RIGHT_OUTER_WALL_INDEX);
	} else if (sample->position.x <= -ROAD_HALF_WIDTH &&
			   sample->position.x >= -TUNNEL_OUTER_HALF_WIDTH) {
		track_object_building_wall(&sample->next_position, TUNNEL_HEIGHT, -TUNNEL_OUTER_HALF_WIDTH,
								   -ROAD_HALF_WIDTH, -TUNNEL_END_Z, TUNNEL_END_Z,
								   TUNNEL_REAR_WALL_INDEX, TUNNEL_FORWARD_WALL_INDEX,
								   TUNNEL_LEFT_OUTER_WALL_INDEX, TUNNEL_LEFT_INNER_WALL_INDEX);
	}
}

void track_object_pipe_entrance(struct TRACK_OBJECT_SAMPLE *sample)
{
	if (absolute_word(sample->next_position.x) >= PIPE_ENTRANCE_WALL_INNER_X &&
		sample->absolute_x <= PIPE_HALF_WIDTH) {
		wallHeight = PIPE_WALL_HEIGHT;
		wallindex = sample->next_position.x <= 0 ? PIPE_ENTRANCE_LEFT_WALL_INDEX
												 : PIPE_ENTRANCE_RIGHT_WALL_INDEX;
		return;
	}
	if (sample->absolute_x >= PIPE_ENTRANCE_WALL_INNER_X ||
		sample->height >= PIPE_ENTRANCE_MAX_HEIGHT) {
		return;
	}
	current_surf_type = (legacy_u8)sample->surface_type;
	if (sample->absolute_x < PIPE_ENTRANCE_CENTER_HALF_WIDTH) {
		planindex = PIPE_ENTRANCE_CENTER_PLAN_INDEX;
		return;
	}
	legacy_s16 terrain_angle;
	legacy_s16 value;
	if (sample->position.x < -PIPE_ENTRANCE_SIDE_SPLIT_X) {
		planindex = PIPE_ENTRANCE_LEFT_OUTER_PLAN_BASE;
		value = -PIPE_ENTRANCE_OUTER_CENTER_X;
		terrain_angle = -PIPE_ENTRANCE_OUTER_ANGLE;
	} else if (sample->position.x < 0) {
		planindex = PIPE_ENTRANCE_LEFT_INNER_PLAN_BASE;
		value = -PIPE_ENTRANCE_INNER_CENTER_X;
		terrain_angle = -PIPE_ENTRANCE_INNER_ANGLE;
	} else if (sample->position.x > PIPE_ENTRANCE_SIDE_SPLIT_X) {
		planindex = PIPE_ENTRANCE_RIGHT_OUTER_PLAN_BASE;
		value = PIPE_ENTRANCE_OUTER_CENTER_X;
		terrain_angle = PIPE_ENTRANCE_OUTER_ANGLE;
	} else {
		planindex = PIPE_ENTRANCE_RIGHT_INNER_PLAN_BASE;
		value = PIPE_ENTRANCE_INNER_CENTER_X;
		terrain_angle = PIPE_ENTRANCE_INNER_ANGLE;
	}
	legacy_s16 value2 = LEGACY_S16_WRAP_ADD(
		multiply_and_scale(sin_fast((legacy_u16)terrain_angle), sample->position.z),
		multiply_and_scale(cos_fast((legacy_u16)terrain_angle),
						   LEGACY_S16_WRAP_SUB(sample->position.x, value)));
	if (value2 < 0) {
		planindex = LEGACY_S16_WRAP_ADD(planindex, 1);
	}
}

enum TRACK_PIPE_SECTION {
	TRACK_PIPE_LEFT_SIDE,
	TRACK_PIPE_RIGHT_SIDE,
	TRACK_PIPE_CENTER,
	TRACK_PIPE_LEFT_OUTER,
	TRACK_PIPE_LEFT_INNER,
	TRACK_PIPE_RIGHT_OUTER,
	TRACK_PIPE_RIGHT_INNER
};

/* The pipe and left/right corkscrew use the same cross section. The height
 * split applies before the x regions, including the centre of the pipe. */
static legacy_u16 track_object_pipe_section(const struct TRACK_OBJECT_SAMPLE *sample)
{
	if (sample->height > PIPE_SIDE_HEIGHT_SPLIT && sample->height <= PIPE_WALL_HEIGHT) {
		return sample->position.x < 0 ? TRACK_PIPE_LEFT_SIDE : TRACK_PIPE_RIGHT_SIDE;
	}
	if (sample->absolute_x < PIPE_ENTRANCE_CENTER_HALF_WIDTH) {
		return TRACK_PIPE_CENTER;
	}
	if (sample->position.x < -PIPE_ENTRANCE_SIDE_SPLIT_X) {
		return TRACK_PIPE_LEFT_OUTER;
	}
	if (sample->position.x < 0) {
		return TRACK_PIPE_LEFT_INNER;
	}
	if (sample->position.x > PIPE_ENTRANCE_SIDE_SPLIT_X) {
		return TRACK_PIPE_RIGHT_OUTER;
	}
	return TRACK_PIPE_RIGHT_INNER;
}

static legacy_s16 track_object_half_pipe_floor(struct TRACK_OBJECT_SAMPLE *sample, legacy_s16 value,
											   legacy_s16 value2)
{
	if (value == PIPE_HALF && value2 == PIPE_LOWER_HALF &&
		sample->absolute_x <= HALF_PIPE_FLOOR_HALF_WIDTH &&
		sample->absolute_z <= HALF_PIPE_FLOOR_HALF_LENGTH) {
		planindex = HALF_PIPE_FLOOR_PLAN_INDEX;
		if (sample->next_position.z <= -HALF_PIPE_FLOOR_HALF_LENGTH) {
			wallindex = HALF_PIPE_REAR_WALL_INDEX;
		} else if (sample->next_position.z >= HALF_PIPE_FLOOR_HALF_LENGTH) {
			wallindex = HALF_PIPE_FORWARD_WALL_INDEX;
		}
		return 1;
	}
	return 0;
}

void track_object_pipe(struct TRACK_OBJECT_SAMPLE *sample)
{
	static const legacy_s16 planes[2][7] = {
		{PIPE_UPPER_LEFT_SIDE_PLAN_INDEX, PIPE_UPPER_RIGHT_SIDE_PLAN_INDEX,
		 PIPE_LOWER_CENTER_PLAN_INDEX, PIPE_LOWER_LEFT_OUTER_PLAN_INDEX,
		 PIPE_LOWER_LEFT_INNER_PLAN_INDEX, PIPE_LOWER_RIGHT_OUTER_PLAN_INDEX,
		 PIPE_LOWER_RIGHT_INNER_PLAN_INDEX},
		{PIPE_UPPER_LEFT_SIDE_PLAN_INDEX, PIPE_UPPER_RIGHT_SIDE_PLAN_INDEX,
		 PIPE_UPPER_CENTER_PLAN_INDEX, PIPE_UPPER_LEFT_OUTER_PLAN_INDEX,
		 PIPE_UPPER_LEFT_INNER_PLAN_INDEX, PIPE_UPPER_RIGHT_OUTER_PLAN_INDEX,
		 PIPE_UPPER_RIGHT_INNER_PLAN_INDEX}};

	legacy_s16 value = sample->physical_model == PHYSICAL_MODEL_PIPE ? PIPE_FULL : PIPE_HALF;
	if (absolute_word(sample->next_position.x) >= PIPE_HALF_WIDTH &&
		sample->absolute_x <= PIPE_HALF_WIDTH) {
		wallHeight = PIPE_WALL_HEIGHT;
		wallindex = sample->next_position.x <= 0 ? PIPE_LEFT_WALL_INDEX : PIPE_RIGHT_WALL_INDEX;
		return;
	}
	if (sample->absolute_x >= PIPE_HALF_WIDTH || sample->height >= PIPE_MAX_HEIGHT) {
		return;
	}
	if (sample->absolute_x < PIPE_SURFACE_HALF_WIDTH) {
		current_surf_type = (legacy_u8)sample->surface_type;
	}
	legacy_s16 value2 = sample->height > PIPE_WALL_HEIGHT ? PIPE_UPPER_HALF : PIPE_LOWER_HALF;
	if (track_object_half_pipe_floor(sample, value, value2)) {
		return;
	}
	planindex = planes[value2][track_object_pipe_section(sample)];
}

void track_object_corkscrew_left_right(struct TRACK_OBJECT_SAMPLE *sample)
{
	static const legacy_s16 segments[2][7] = {
		{CORK_LR_LOWER_LEFT_SIDE_SEGMENT, CORK_LR_LOWER_RIGHT_SIDE_SEGMENT, CORK_LR_NO_SEGMENT,
		 CORK_LR_LOWER_LEFT_OUTER_SEGMENT, CORK_LR_LOWER_LEFT_INNER_SEGMENT,
		 CORK_LR_LOWER_RIGHT_OUTER_SEGMENT, CORK_LR_LOWER_RIGHT_INNER_SEGMENT},
		{CORK_LR_LOWER_LEFT_SIDE_SEGMENT, CORK_LR_LOWER_RIGHT_SIDE_SEGMENT,
		 CORK_LR_UPPER_CENTER_SEGMENT, CORK_LR_UPPER_LEFT_OUTER_SEGMENT,
		 CORK_LR_UPPER_LEFT_INNER_SEGMENT, CORK_LR_UPPER_RIGHT_OUTER_SEGMENT,
		 CORK_LR_UPPER_RIGHT_INNER_SEGMENT}};

	if (sample->absolute_x >= CORK_LR_HALF_WIDTH || sample->height >= CORK_LR_MAX_HEIGHT) {
		return;
	}
	current_surf_type = (legacy_u8)sample->surface_type;
	legacy_s16 value2 =
		sample->height > CORK_LR_UPPER_HEIGHT ? CORK_LR_UPPER_HALF : CORK_LR_LOWER_HALF;
	legacy_s16 value3 = segments[value2][track_object_pipe_section(sample)];
	if (value3 != CORK_LR_NO_SEGMENT && sample->position.z > corkLR_negZBound[value3] &&
		sample->position.z < corkLR_posZBound[value3]) {
		planindex = LEGACY_S16_WRAP_ADD(value3, CORK_LR_PLAN_BASE);
	}
	if (planindex == NO_PLANE_INDEX && sample->absolute_z < CORK_LR_END_Z) {
		wallindex = CORK_LR_WALL_INDEX;
		corkFlag = CORKSCREW_ACTIVE;
		wallHeight = CORK_LR_WALL_HEIGHT;
	}
}

static void track_object_corkscrew_arc(struct TRACK_OBJECT_SAMPLE *sample, legacy_s16 value,
									   legacy_s16 value2, legacy_s16 value3,
									   legacy_s16 terrain_angle)
{
	legacy_s16 radius = (legacy_s16)polarRadius2D(value, sample->position.z);
	if (radius <= CORK_UD_INNER_RADIUS || radius >= CORK_UD_OUTER_RADIUS) {
		return;
	}
	legacy_s16 angle_step =
		(legacy_s16)((((legacy_u16)LEGACY_S16_WRAP_NEGATE(LEGACY_S16_WRAP_SUB(
						   (legacy_s16)polarAngle(value, sample->position.z), ANGLE_QUARTER_TURN)) &
					   ANGLE_MASK) *
					  CORK_UD_ARC_SEGMENT_COUNT) >>
					 CORK_UD_ANGLE_SCALE_SHIFT);
	planindex =
		LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_ADD(value2, angle_step), CORK_UD_FIRST_ARC_PLAN_OFFSET);
	current_surf_type = (legacy_u8)sample->surface_type;
	track_wall_collision_enabled = 0;
	wallHeight = CORK_UD_WALL_HEIGHT;
	elRdWallRelated = ELEVATED_WALL_VERTICAL_OFFSET;
	value = LEGACY_S16_WRAP_SUB(radius, CORK_UD_CENTER_RADIUS);
	if (value > CORK_UD_WALL_RADIUS_OFFSET) {
		wallindex = LEGACY_S16_WRAP_ADD(value3, angle_step);
	} else if (value < -CORK_UD_WALL_RADIUS_OFFSET) {
		wallindex = LEGACY_S16_WRAP_ADD(terrain_angle, angle_step);
	}
}

void track_object_corkscrew_up_down(struct TRACK_OBJECT_SAMPLE *sample)
{
	legacy_s16 value3;
	legacy_s16 terrain_angle;
	legacy_s16 value;
	legacy_s16 value2;
	if (sample->physical_model == PHYSICAL_MODEL_CORKSCREW_UP_DOWN_A) {
		value = LEGACY_S16_WRAP_NEGATE(sample->position.x);
		value2 = CORK_UD_A_PLAN_BASE;
		value3 = CORK_UD_A_OUTER_WALL_BASE;
		terrain_angle = CORK_UD_A_INNER_WALL_BASE;
	} else {
		value = sample->position.x;
		value2 = CORK_UD_B_PLAN_BASE;
		value3 = CORK_UD_B_OUTER_WALL_BASE;
		terrain_angle = CORK_UD_B_INNER_WALL_BASE;
	}
	corkFlag = CORKSCREW_ACTIVE;
	if (sample->position.z < 0 && sample->height < CORK_UD_LOW_HEIGHT && value > 0) {
		if (value >= CORK_UD_LOWER_OUTER_RADIUS || value <= CORK_UD_LOWER_INNER_RADIUS) {
			return;
		}
		current_surf_type = (legacy_u8)sample->surface_type;
		planindex = value2;
		return;
	}
	if (sample->position.z > 0 && sample->height > CORK_UD_UPPER_HEIGHT &&
		value < CORK_UD_OUTER_RADIUS && value > CORK_UD_INNER_RADIUS) {
		wallHeight = CORK_UD_WALL_HEIGHT;
		elRdWallRelated = ELEVATED_WALL_VERTICAL_OFFSET;
		wallindex = LEGACY_S16_WRAP_ADD(value > CORK_UD_CENTER_RADIUS ? value3 : terrain_angle,
										CORK_UD_WALL_INDEX_OFFSET);
		current_surf_type = (legacy_u8)sample->surface_type;
		planindex = LEGACY_S16_WRAP_ADD(value2, CORK_UD_UPPER_PLAN_OFFSET);
		track_wall_collision_enabled = 0;
		return;
	}
	track_object_corkscrew_arc(sample, value, value2, value3, terrain_angle);
}

static void track_object_slalom_negative_wall(struct TRACK_OBJECT_SAMPLE *sample)
{
	wallHeight = SLALOM_POLE_WALL_HEIGHT;
	if (sample->next_position.z < -SLALOM_POLE_FAR_Z) {
		wallindex = SLALOM_NEGATIVE_Z_FAR_WALL_INDEX;
	} else if (sample->next_position.z > -SLALOM_POLE_NEAR_Z) {
		wallindex = SLALOM_NEGATIVE_Z_NEAR_WALL_INDEX;
	} else if (sample->next_position.x < SLALOM_POLE_INNER_X) {
		wallindex = SLALOM_NEGATIVE_Z_INNER_WALL_INDEX;
	} else if (sample->next_position.x > SLALOM_POLE_OUTER_X) {
		wallindex = SLALOM_NEGATIVE_Z_OUTER_WALL_INDEX;
	}
}

static void track_object_slalom_positive_wall(struct TRACK_OBJECT_SAMPLE *sample)
{
	wallHeight = SLALOM_POLE_WALL_HEIGHT;
	if (sample->next_position.z > SLALOM_POLE_FAR_Z) {
		wallindex = SLALOM_POSITIVE_Z_FAR_WALL_INDEX;
	} else if (sample->next_position.z < SLALOM_POLE_NEAR_Z) {
		wallindex = SLALOM_POSITIVE_Z_NEAR_WALL_INDEX;
	} else if (sample->next_position.x > -SLALOM_POLE_INNER_X) {
		wallindex = SLALOM_POSITIVE_Z_INNER_WALL_INDEX;
	} else if (sample->next_position.x < -SLALOM_POLE_OUTER_X) {
		wallindex = SLALOM_POSITIVE_Z_OUTER_WALL_INDEX;
	}
}

void track_object_slalom(struct TRACK_OBJECT_SAMPLE *sample)
{
	if (sample->absolute_x < ROAD_HALF_WIDTH) {
		current_surf_type = (legacy_u8)sample->surface_type;
	}
	if (sample->position.x >= SLALOM_POLE_INNER_X && sample->position.x <= SLALOM_POLE_OUTER_X &&
		sample->position.z > -SLALOM_POLE_FAR_Z && sample->position.z < -SLALOM_POLE_NEAR_Z) {
		track_object_slalom_negative_wall(sample);
	} else if (sample->position.x <= -SLALOM_POLE_INNER_X &&
			   sample->position.x >= -SLALOM_POLE_OUTER_X &&
			   sample->position.z < SLALOM_POLE_FAR_Z && sample->position.z > SLALOM_POLE_NEAR_Z) {
		track_object_slalom_positive_wall(sample);
	}
}

void track_object_barn(struct TRACK_OBJECT_SAMPLE *sample)
{
	if (sample->absolute_x <= BARN_HALF_WIDTH && sample->absolute_z <= BARN_HALF_WIDTH) {
		track_object_building_wall(&sample->next_position, BARN_HEIGHT, -BARN_HALF_WIDTH,
								   BARN_HALF_WIDTH, -BARN_HALF_WIDTH, BARN_HALF_WIDTH,
								   BARN_REAR_WALL_INDEX, BARN_FORWARD_WALL_INDEX,
								   BARN_LEFT_WALL_INDEX, BARN_RIGHT_WALL_INDEX);
	}
}

void track_object_gas_station(struct TRACK_OBJECT_SAMPLE *sample)
{
	if (sample->position.x >= GAS_STATION_LEFT_X && sample->position.x <= GAS_STATION_RIGHT_X &&
		sample->absolute_z <= GAS_STATION_HALF_LENGTH) {
		track_object_building_wall(&sample->next_position, GAS_STATION_HEIGHT, GAS_STATION_LEFT_X,
								   GAS_STATION_RIGHT_X, -GAS_STATION_HALF_LENGTH,
								   GAS_STATION_HALF_LENGTH, GAS_STATION_REAR_WALL_INDEX,
								   GAS_STATION_FORWARD_WALL_INDEX, GAS_STATION_LEFT_WALL_INDEX,
								   GAS_STATION_RIGHT_WALL_INDEX);
	}
}

void track_object_joes(struct TRACK_OBJECT_SAMPLE *sample)
{
	if (sample->absolute_x <= JOES_HALF_WIDTH && sample->absolute_z <= JOES_HALF_LENGTH) {
		track_object_building_wall(&sample->next_position, JOES_HEIGHT, -JOES_HALF_WIDTH,
								   JOES_HALF_WIDTH, -JOES_HALF_LENGTH, JOES_HALF_LENGTH,
								   JOES_REAR_WALL_INDEX, JOES_FORWARD_WALL_INDEX,
								   JOES_LEFT_WALL_INDEX, JOES_RIGHT_WALL_INDEX);
	}
}

void track_object_office(struct TRACK_OBJECT_SAMPLE *sample)
{
	if (sample->absolute_x <= OFFICE_HALF_WIDTH && sample->absolute_z <= OFFICE_HALF_WIDTH) {
		track_object_building_wall(&sample->next_position, OFFICE_HEIGHT, -OFFICE_HALF_WIDTH,
								   OFFICE_HALF_WIDTH, -OFFICE_HALF_WIDTH, OFFICE_HALF_WIDTH,
								   OFFICE_REAR_WALL_INDEX, OFFICE_FORWARD_WALL_INDEX,
								   OFFICE_LEFT_WALL_INDEX, OFFICE_RIGHT_WALL_INDEX);
	}
}

void track_object_windmill(struct TRACK_OBJECT_SAMPLE *sample)
{
	if (sample->absolute_x <= WINDMILL_HALF_WIDTH && sample->absolute_z <= WINDMILL_HALF_WIDTH) {
		track_object_building_wall(&sample->next_position, WINDMILL_HEIGHT, -WINDMILL_HALF_WIDTH,
								   WINDMILL_HALF_WIDTH, -WINDMILL_HALF_WIDTH, WINDMILL_HALF_WIDTH,
								   WINDMILL_REAR_WALL_INDEX, WINDMILL_FORWARD_WALL_INDEX,
								   WINDMILL_LEFT_WALL_INDEX, WINDMILL_RIGHT_WALL_INDEX);
	}
}

void track_object_ship(struct TRACK_OBJECT_SAMPLE *sample)
{
	if (sample->position.x >= SHIP_LEFT_X && sample->position.x <= SHIP_RIGHT_X &&
		sample->absolute_z <= SHIP_HALF_LENGTH) {
		track_object_building_wall(&sample->next_position, SHIP_HEIGHT, SHIP_LEFT_X, SHIP_RIGHT_X,
								   -SHIP_HALF_LENGTH, SHIP_HALF_LENGTH, SHIP_REAR_WALL_INDEX,
								   SHIP_FORWARD_WALL_INDEX, SHIP_LEFT_WALL_INDEX,
								   SHIP_RIGHT_WALL_INDEX);
	}
}
