#include "../restunts.h"
#include "../legacy.h"
#include "../math/math.h"
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

struct TRACK_OBJECT_HANDLER {
	legacy_s16 physical_model;
	void (*handle)(struct TRACK_OBJECT_SAMPLE *sample);
};

static const struct TRACK_OBJECT_HANDLER track_object_handlers[] = {
	{PHYSICAL_MODEL_START_FINISH, track_object_straight},
	{PHYSICAL_MODEL_ROAD, track_object_straight},
	{PHYSICAL_MODEL_CROSSROAD, track_object_crossroad},
	{PHYSICAL_MODEL_CHICANE_LEFT_RIGHT, track_object_chicane},
	{PHYSICAL_MODEL_CHICANE_RIGHT_LEFT, track_object_chicane},
	{PHYSICAL_MODEL_LARGE_CORNER, track_object_large_corner},
	{PHYSICAL_MODEL_SHARP_SPLIT_A, track_object_sharp_corner},
	{PHYSICAL_MODEL_SHARP_CORNER, track_object_sharp_corner},
	{PHYSICAL_MODEL_SHARP_SPLIT_B, track_object_sharp_corner},
	{PHYSICAL_MODEL_LARGE_SPLIT_A, track_object_large_split},
	{PHYSICAL_MODEL_LARGE_SPLIT_B, track_object_large_split},
	{PHYSICAL_MODEL_HIGHWAY_ENTRANCE, track_object_highway_entrance},
	{PHYSICAL_MODEL_HIGHWAY, track_object_highway},
	{PHYSICAL_MODEL_RAMP, track_object_ramp},
	{PHYSICAL_MODEL_SOLID_RAMP, track_object_ramp},
	{PHYSICAL_MODEL_ELEVATED_ROAD, track_object_elevated_road},
	{PHYSICAL_MODEL_ELEVATED_SPAN, track_object_elevated_road},
	{PHYSICAL_MODEL_SOLID_ROAD, track_object_elevated_road},
	{PHYSICAL_MODEL_OVERPASS, track_object_elevated_road},
	{PHYSICAL_MODEL_ELEVATED_CORNER, track_object_elevated_corner},
	{PHYSICAL_MODEL_BANKED_ENTRANCE_A, track_object_banked_entrance},
	{PHYSICAL_MODEL_BANKED_ENTRANCE_B, track_object_banked_entrance},
	{PHYSICAL_MODEL_BANKED_ROAD, track_object_banked_road},
	{PHYSICAL_MODEL_BANKED_CORNER, track_object_banked_corner},
	{PHYSICAL_MODEL_LOOP, track_object_loop},
	{PHYSICAL_MODEL_TUNNEL, track_object_tunnel},
	{PHYSICAL_MODEL_PIPE_ENTRANCE, track_object_pipe_entrance},
	{PHYSICAL_MODEL_HALF_PIPE, track_object_pipe},
	{PHYSICAL_MODEL_PIPE, track_object_pipe},
	{PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT, track_object_corkscrew_left_right},
	{PHYSICAL_MODEL_CORKSCREW_UP_DOWN_A, track_object_corkscrew_up_down},
	{PHYSICAL_MODEL_CORKSCREW_UP_DOWN_B, track_object_corkscrew_up_down},
	{PHYSICAL_MODEL_SLALOM, track_object_slalom},
	{PHYSICAL_MODEL_BARN, track_object_barn},
	{PHYSICAL_MODEL_GAS_STATION, track_object_gas_station},
	{PHYSICAL_MODEL_JOES, track_object_joes},
	{PHYSICAL_MODEL_OFFICE, track_object_office},
	{PHYSICAL_MODEL_WINDMILL, track_object_windmill},
	{PHYSICAL_MODEL_SHIP, track_object_ship}};

void track_object_model(struct TRACK_OBJECT_SAMPLE *sample)
{
	for (legacy_u16 index = 0;
		 index < sizeof(track_object_handlers) / sizeof(track_object_handlers[0]); index++) {
		if (track_object_handlers[index].physical_model == sample->physical_model) {
			track_object_handlers[index].handle(sample);
			return;
		}
	}
}

static void track_object_terrain(const struct VECTOR *world_position, legacy_u8 terrain_tile)
{
	static const legacy_s16 water_angles[4] = {TERRAIN_SLOPE_2_ANGLE, TERRAIN_SLOPE_3_ANGLE,
											   TERRAIN_SLOPE_4_ANGLE, TERRAIN_SLOPE_5_ANGLE};

	struct VECTOR position;
	if (terrain_tile == TERRAIN_WATER_TILE) {
		current_surf_type = CAR_SURFACE_WATER;
	} else if (terrain_tile >= TERRAIN_WATER_SLOPE_2 && terrain_tile <= TERRAIN_WATER_SLOPE_5) {
		legacy_s16 terrain_angle = water_angles[terrain_tile - TERRAIN_WATER_SLOPE_2];
		position.x = LEGACY_S16_WRAP_SUB(world_position->x, elem_xCenter);
		position.z = LEGACY_S16_WRAP_SUB(world_position->z, elem_zCenter);
		legacy_s16 value = LEGACY_S16_WRAP_ADD(
			multiply_and_scale(sin_fast((legacy_u16)terrain_angle), position.z),
			multiply_and_scale(cos_fast((legacy_u16)terrain_angle), position.x));
		if (value < 0) {
			current_surf_type = CAR_SURFACE_WATER;
		}
	} else if (terrain_tile == TERRAIN_RAISED_TILE) {
		terrainHeight = (legacy_s16)hillHeightConsts[TERRAIN_RAISED_HEIGHT_INDEX];
	}
}

static legacy_s16 track_object_hill(const struct VECTOR *world_position, legacy_u8 terrain_tile,
									legacy_s16 track_row, legacy_s16 track_column,
									legacy_s16 element_orientation)
{
	static const legacy_s16 orientations[4] = {0, ANGLE_THREE_QUARTER_TURN, ANGLE_HALF_TURN,
											   ANGLE_QUARTER_TURN};

	struct VECTOR position;
	position.x =
		LEGACY_S16_WRAP_SUB(world_position->x, (legacy_s16)track_column_centers[track_column]);
	position.z = LEGACY_S16_WRAP_SUB(world_position->z, (legacy_s16)terraincenterpos[track_row]);
	if (terrain_tile <= TERRAIN_ORIENTED_LAST_TILE) {
		element_orientation =
			orientations[(terrain_tile - HILL_TERRAIN_FIRST) & TERRAIN_ORIENTATION_MASK];
		track_rotate_local(&position, element_orientation);
	}
	if (terrain_tile < HILL_TERRAIN_END) {
		if (planindex == NO_PLANE_INDEX) {
			planindex = TERRAIN_FLAT_PLANE_INDEX;
		}
	} else {
		legacy_s16 value = LEGACY_S16_WRAP_ADD(
			multiply_and_scale(sin_fast((legacy_u16)TERRAIN_SLOPE_5_ANGLE), position.z),
			multiply_and_scale(cos_fast((legacy_u16)TERRAIN_SLOPE_5_ANGLE), position.x));
		if (terrain_tile <= TERRAIN_SLOPE_DOWN_LAST_TILE) {
			if (value < 0) {
				planindex = TERRAIN_SLOPE_DOWN_PLANE_INDEX;
			}
		} else if (terrain_tile <= TERRAIN_ORIENTED_LAST_TILE) {
			if (value > 0) {
				planindex = TERRAIN_SLOPE_UP_PLANE_INDEX;
			} else {
				terrainHeight = TERRAIN_UPPER_HEIGHT;
			}
		}
	}
	return element_orientation;
}

void track_object_world_collision(const struct VECTOR *world_position,
								  legacy_s16 element_orientation,
								  legacy_s16 wall_orientation_modifier)
{
	if (planindex > NO_PLANE_INDEX) {
		planindex = LEGACY_S16_WRAP_MUL(planindex, PLANE_ORIENTATION_COUNT);
		switch ((legacy_u16)element_orientation) {
			case ANGLE_QUARTER_TURN:
				planindex = LEGACY_S16_WRAP_ADD(planindex, PLANE_QUARTER_TURN_OFFSET);
				break;
			case ANGLE_HALF_TURN:
				planindex = LEGACY_S16_WRAP_ADD(planindex, PLANE_HALF_TURN_OFFSET);
				break;
			case ANGLE_THREE_QUARTER_TURN:
				planindex = LEGACY_S16_WRAP_ADD(planindex, PLANE_THREE_QUARTER_TURN_OFFSET);
				break;
		}
	}
	current_planptr = &planptr[planindex];
	if (current_surf_type == CAR_SURFACE_GRASS) {
		legacy_s16 value =
			LEGACY_S16_FROM_BITS((legacy_u16)(world_position->z ^ world_position->x));
		terrainHeight = LEGACY_S16_WRAP_ADD(
			terrainHeight, (legacy_s16)(LEGACY_U16_SAR(value, GRASS_HEIGHT_HASH_SHIFT) &
										GRASS_HEIGHT_VARIATION_MASK));
	} else {
		terrainHeight = LEGACY_S16_WRAP_ADD(terrainHeight, NON_GRASS_HEIGHT_OFFSET);
	}

	if (wallindex < 0) {
		return;
	}
	struct TRACK_WALL far *wall = &wallptr[wallindex];
	wallOrientation =
		(legacy_s16)((LEGACY_S16_WRAP_ADD(
						 LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_NEGATE(wall->orientation),
											 element_orientation),
						 wall_orientation_modifier)) &
					 ANGLE_MASK);
	switch ((legacy_u16)element_orientation) {
		case ANGLE_QUARTER_TURN:
			wallStartX = wall->z;
			wallStartZ = LEGACY_S16_WRAP_NEGATE(wall->x);
			break;
		case ANGLE_HALF_TURN:
			wallStartX = LEGACY_S16_WRAP_NEGATE(wall->x);
			wallStartZ = LEGACY_S16_WRAP_NEGATE(wall->z);
			break;
		case ANGLE_THREE_QUARTER_TURN:
			wallStartX = LEGACY_S16_WRAP_NEGATE(wall->z);
			wallStartZ = wall->x;
			break;
		default:
			wallStartX = wall->x;
			wallStartZ = wall->z;
			break;
	}
	wallStartX = LEGACY_S16_WRAP_ADD(wallStartX, elem_xCenter);
	wallStartZ = LEGACY_S16_WRAP_ADD(wallStartZ, elem_zCenter);
}

static legacy_s16 track_object_sample(struct TRACK_OBJECT_SAMPLE *sample,
									  const struct VECTOR *world_position,
									  const struct VECTOR *next_world_position,
									  legacy_s16 track_row, legacy_s16 track_column,
									  legacy_u8 terrain_tile)
{
	legacy_u8 track_tile = track_element_map[terrainrows[track_row] + track_column];
	legacy_s16 element_orientation = 0;
	if (track_tile == TRACK_TILE_EMPTY) {
		return element_orientation;
	}
	if (track_tile == TRACK_TILE_CONTINUATION_SOUTHEAST) {
		track_tile = track_element_map[terrainrows[track_row + 1] + track_column - 1];
		track_object_tile_center(track_tile, track_row + 1, track_column);
	} else if (track_tile == TRACK_TILE_CONTINUATION_SOUTH) {
		track_tile = track_element_map[terrainrows[track_row + 1] + track_column];
		track_object_tile_center(track_tile, track_row + 1, track_column + 1);
	} else if (track_tile == TRACK_TILE_CONTINUATION_EAST) {
		track_tile = track_element_map[terrainrows[track_row] + track_column - 1];
		track_object_tile_center(track_tile, track_row, track_column);
	} else {
		track_object_tile_center(track_tile, track_row, track_column + 1);
	}

	sample->position.x = LEGACY_S16_WRAP_SUB(world_position->x, elem_xCenter);
	sample->position.z = LEGACY_S16_WRAP_SUB(world_position->z, elem_zCenter);
	sample->next_position.x = LEGACY_S16_WRAP_SUB(next_world_position->x, elem_xCenter);
	sample->next_position.z = LEGACY_S16_WRAP_SUB(next_world_position->z, elem_zCenter);
	if (track_tile != TRACK_TILE_EMPTY && terrain_tile >= HILL_TERRAIN_FIRST &&
		terrain_tile < HILL_TERRAIN_END) {
		track_tile = subst_hillroad_track(terrain_tile, track_tile);
	}

	struct TRACKOBJECT *track_object = &trkObjectList[track_tile];
	sample->physical_model = (legacy_s8)track_object->ss_physicalModel;
	element_orientation = (legacy_s16)track_object->ss_rotY;
	track_rotate_local(&sample->position, element_orientation);
	track_rotate_local(&sample->next_position, element_orientation);
	sample->surface_type =
		(legacy_s8)((legacy_u8)track_object->ss_surfaceType + TRACK_SURFACE_TYPE_OFFSET);
	if (sample->surface_type < TRACK_SURFACE_TYPE_MINIMUM) {
		sample->surface_type = TRACK_SURFACE_TYPE_MINIMUM;
	}
	sample->absolute_x = absolute_word(sample->position.x);
	sample->absolute_z = absolute_word(sample->position.z);
	if (sample->physical_model < PHYSICAL_MODEL_MINIMUM ||
		sample->physical_model > TRACK_PHYSICAL_MODEL_MAXIMUM) {
		return element_orientation;
	}

	sample->height = LEGACY_S16_WRAP_SUB(world_position->y, terrainHeight);
	sample->next_height = LEGACY_S16_WRAP_SUB(next_world_position->y, terrainHeight);
	track_object_model(sample);
	return element_orientation;
}

void build_track_object(struct VECTOR *world_position, struct VECTOR *next_world_position)
{
#ifdef RESTUNTS_SDL3
	extended_trackobj_reset();
#endif
	planindex = NO_PLANE_INDEX;
	wallindex = TRACK_WALL_NONE;
	wallHeight = TRACK_WALL_DEFAULT_HEIGHT;
	elRdWallRelated = ELEVATED_WALL_LOWER_BOUND_DEFAULT;
	corkFlag = CORKSCREW_INACTIVE;
	current_surf_type = CAR_SURFACE_GRASS;
	track_wall_collision_enabled = TRACK_COLLISION_ENABLED;
	struct TRACK_OBJECT_SAMPLE sample;
	sample.wall_orientation_modifier = 0;
	terrainHeight = 0;
	legacy_u8 terrain_tile = TERRAIN_TILE_NONE;

#ifdef RESTUNTS_SDL3
	if (extended_trackobj_build(world_position, next_world_position)) {
		return;
	}
#endif

	legacy_s16 track_column = LEGACY_S16_SAR(world_position->x, TRACK_TILE_POSITION_SHIFT);
	legacy_s16 track_row = LEGACY_S16_SAR(world_position->z, TRACK_TILE_POSITION_SHIFT);
	sample.physical_model = PHYSICAL_MODEL_NONE;
	legacy_s16 element_orientation = 0;
	if (track_column >= 0 && track_column <= TRACK_GRID_LAST_INDEX && track_row >= 0 &&
		track_row <= TRACK_GRID_LAST_INDEX) {
		elem_xCenter = (legacy_s16)track_column_centers[track_column];
		elem_zCenter = (legacy_s16)terraincenterpos[track_row];
		terrain_tile = track_terrain_map[trackrows[track_row] + track_column];
		track_object_terrain(world_position, terrain_tile);

		element_orientation = track_object_sample(&sample, world_position, next_world_position,
												  track_row, track_column, terrain_tile);

		if (terrain_tile >= HILL_TERRAIN_FIRST) {
			element_orientation = track_object_hill(world_position, terrain_tile, track_row,
													track_column, element_orientation);
		}
	}

	track_object_world_collision(world_position, element_orientation,
								 sample.wall_orientation_modifier);
}

void track_collision_capture(struct TRACK_COLLISION_SNAPSHOT *saved)
{
	saved->plane_index = planindex;
	saved->plane = current_planptr;
#ifdef RESTUNTS_SDL3
	extended_trackobj_capture(saved);
#endif
	saved->wall_index = wallindex;
	saved->wall_height = wallHeight;
	saved->wall_lower_bound = elRdWallRelated;
	saved->corkscrew = corkFlag;
	saved->surface_type = current_surf_type;
	saved->wall_collision_enabled = track_wall_collision_enabled;
	saved->terrain_height = terrainHeight;
	saved->element_x = elem_xCenter;
	saved->element_z = elem_zCenter;
	saved->wall_x = wallStartX;
	saved->wall_z = wallStartZ;
	saved->wall_orientation = wallOrientation;
}

void track_collision_restore(const struct TRACK_COLLISION_SNAPSHOT *saved)
{
	planindex = saved->plane_index;
	current_planptr = saved->plane;
#ifdef RESTUNTS_SDL3
	extended_trackobj_restore(saved);
#endif
	wallindex = saved->wall_index;
	wallHeight = saved->wall_height;
	elRdWallRelated = saved->wall_lower_bound;
	corkFlag = saved->corkscrew;
	current_surf_type = saved->surface_type;
	track_wall_collision_enabled = saved->wall_collision_enabled;
	terrainHeight = saved->terrain_height;
	elem_xCenter = saved->element_x;
	elem_zCenter = saved->element_z;
	wallStartX = saved->wall_x;
	wallStartZ = saved->wall_z;
	wallOrientation = saved->wall_orientation;
}
