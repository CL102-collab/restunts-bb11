#include "restunts.h"
#include "legacy.h"
#include "math.h"
#include "trackdata_layout.h"
#include "track_types.h"
#include "track_objects.h"
#include "track_collision.h"
#include "physics_internal.h"
#include "owoot_road.h"
#include "externs.h"
#ifdef RESTUNTS_SDL3
#include "extended_track.h"
#include "extended_geometry.h"
#include "roll_ramp.h"
#endif

#define TRACK_ARC_CENTER_OFFSET 1024
#define TRACK_ARC_CENTER_RADIUS 1536
#define TRACK_ARC_SEGMENT_COUNT 18U
#define TRACK_ARC_LAST_SEGMENT 17
#define TRACK_ARC_ANGLE_SCALE_SHIFT 8U
#define TERRAIN_TILE_NONE 0U

enum WATER_TERRAIN_TILE {
	TERRAIN_WATER_TILE = 1,
	TERRAIN_WATER_SLOPE_2 = 2,
	TERRAIN_WATER_SLOPE_3 = 3,
	TERRAIN_WATER_SLOPE_4 = 4,
	TERRAIN_WATER_SLOPE_5 = 5
};

#define TERRAIN_SLOPE_2_ANGLE 128
#define TERRAIN_SLOPE_3_ANGLE -640
#define TERRAIN_SLOPE_4_ANGLE -384
#define TERRAIN_SLOPE_5_ANGLE -128
#define TRACK_PHYSICAL_MODEL_MAXIMUM 74
#define START_FINISH_FAR_Z -380
#define START_FINISH_NEAR_Z -300
#define START_FINISH_FAR_PLANE_INDEX 131
#define START_FINISH_NEAR_PLANE_INDEX 132
#define LARGE_CORNER_INNER_RADIUS 1416
#define LARGE_CORNER_OUTER_RADIUS 1656
#define SHARP_CORNER_CENTER_OFFSET 512
#define SHARP_CORNER_INNER_RADIUS 392
#define SHARP_CORNER_OUTER_RADIUS 632
#define SPLIT_STRAIGHT_LANE_INNER_X 392
#define SPLIT_STRAIGHT_LANE_OUTER_X 632
#define HIGHWAY_PLANE_INDEX 1
#define HIGHWAY_MERGE_END_Z 334
#define HIGHWAY_OUTER_HALF_WIDTH 360
#define HIGHWAY_FAR_LEFT_WALL_INDEX 188
#define HIGHWAY_FAR_RIGHT_WALL_INDEX 186
#define HIGHWAY_NEAR_LEFT_WALL_INDEX 189
#define HIGHWAY_NEAR_RIGHT_WALL_INDEX 187
#define RAMP_PLANE_INDEX 3
#define RAMP_ENTRY_WALL_INDEX 102
#define ELEVATED_PLANE_INDEX 2
#define ELEVATED_ROAD_END_Z 476
#define ELEVATED_FORWARD_WALL_INDEX 103
#define ELEVATED_REAR_WALL_INDEX 104
#define ELEVATED_SIDE_WALL_HEIGHT 42
#define ELEVATED_LEFT_WALL_INDEX 100
#define ELEVATED_RIGHT_WALL_INDEX 101
#define ELEVATED_CORNER_OUTER_OFFSET 150
#define ELEVATED_CORNER_INNER_OFFSET 108
#define ELEVATED_CORNER_INNER_WALL_BASE 105
#define ELEVATED_CORNER_OUTER_WALL_BASE 123
#define BANKED_ENTRANCE_A_PLAN_BASE 35
#define BANKED_ENTRANCE_A_ANGLE -672
#define BANKED_ENTRANCE_B_PLAN_BASE 25
#define BANKED_ENTRANCE_B_ANGLE 160
#define BANKED_ENTRANCE_END_Z 334
#define BANKED_ENTRANCE_INNER_Z 168

enum BANKED_ENTRANCE_VARIANT { BANKED_ENTRANCE_A_VARIANT = 0, BANKED_ENTRANCE_B_VARIANT = 1 };

#define BANKED_ENTRANCE_NEGATIVE_OUTER_PLAN_OFFSET 1
#define BANKED_ENTRANCE_NEGATIVE_INNER_PLAN_OFFSET 3
#define BANKED_ENTRANCE_POSITIVE_INNER_PLAN_OFFSET 5
#define BANKED_ENTRANCE_POSITIVE_OUTER_PLAN_OFFSET 7
#define BANKED_ENTRANCE_POSITIVE_END_PLAN_OFFSET 9

enum BANKED_ENTRANCE_ADJUST_INDEX {
	BANKED_ENTRANCE_NEGATIVE_OUTER_ADJUST_INDEX = 0,
	BANKED_ENTRANCE_NEGATIVE_INNER_ADJUST_INDEX = 1,
	BANKED_ENTRANCE_POSITIVE_INNER_ADJUST_INDEX = 2,
	BANKED_ENTRANCE_POSITIVE_OUTER_ADJUST_INDEX = 3
};

#define BANKED_ENTRANCE_SECOND_TRIANGLE_PLAN_OFFSET 1
#define BANKED_ROAD_PLANE_INDEX 6
#define BANKED_CORNER_INNER_OFFSET 120
#define BANKED_CORNER_OUTER_OFFSET 126
#define BANKED_CORNER_WALL_THRESHOLD 102
#define BANKED_CORNER_PLAN_BASE 7
#define BANKED_CORNER_WALL_BASE 123
#define LOOP_REAR_PLAN_BASE 51
#define LOOP_FRONT_PLAN_BASE 45
#define LOOP_SURFACE_END_PADDING 100
#define LOOP_SURFACE_LAST_INDEX 5U
#define LOOP_UPPER_HEIGHT_THRESHOLD 524
#define LOOP_LANE_SEPARATION 400
#define LOOP_LOW_CLEARANCE 100
#define LOOP_LOW_CLEARANCE_LAST_SEGMENT 1U
#define TUNNEL_OUTER_HALF_WIDTH 270
#define TUNNEL_ROOF_PLANE_INDEX 133
#define TUNNEL_END_Z 512
#define TUNNEL_REAR_WALL_INDEX 154
#define TUNNEL_FORWARD_WALL_INDEX 153
#define TUNNEL_RIGHT_INNER_WALL_INDEX 152
#define TUNNEL_RIGHT_OUTER_WALL_INDEX 150
#define TUNNEL_LEFT_INNER_WALL_INDEX 151
#define TUNNEL_LEFT_OUTER_WALL_INDEX 149
#define PIPE_HALF_WIDTH 164
#define PIPE_WALL_HEIGHT 151
#define PIPE_ENTRANCE_WALL_INNER_X 115
#define PIPE_ENTRANCE_LEFT_WALL_INDEX 160
#define PIPE_ENTRANCE_RIGHT_WALL_INDEX 159
#define PIPE_ENTRANCE_MAX_HEIGHT 171
#define PIPE_ENTRANCE_CENTER_HALF_WIDTH 31
#define PIPE_ENTRANCE_CENTER_PLAN_INDEX 70
#define PIPE_ENTRANCE_SIDE_SPLIT_X 84
#define PIPE_ENTRANCE_LEFT_OUTER_PLAN_BASE 73
#define PIPE_ENTRANCE_LEFT_INNER_PLAN_BASE 71
#define PIPE_ENTRANCE_RIGHT_OUTER_PLAN_BASE 77
#define PIPE_ENTRANCE_RIGHT_INNER_PLAN_BASE 75
#define PIPE_ENTRANCE_OUTER_CENTER_X 100
#define PIPE_ENTRANCE_INNER_CENTER_X 57
#define PIPE_ENTRANCE_OUTER_ANGLE 5
#define PIPE_ENTRANCE_INNER_ANGLE 8
#define PIPE_LEFT_WALL_INDEX 156
#define PIPE_RIGHT_WALL_INDEX 155
#define PIPE_MAX_HEIGHT 265
#define PIPE_SURFACE_HALF_WIDTH 130
#define HALF_PIPE_FLOOR_HALF_WIDTH 84
#define HALF_PIPE_FLOOR_HALF_LENGTH 75
#define HALF_PIPE_FLOOR_PLAN_INDEX 69
#define HALF_PIPE_REAR_WALL_INDEX 157
#define HALF_PIPE_FORWARD_WALL_INDEX 158
#define PIPE_SIDE_HEIGHT_SPLIT 88
#define PIPE_UPPER_LEFT_SIDE_PLAN_INDEX 60
#define PIPE_UPPER_RIGHT_SIDE_PLAN_INDEX 66
#define PIPE_UPPER_CENTER_PLAN_INDEX 63
#define PIPE_UPPER_LEFT_OUTER_PLAN_INDEX 61
#define PIPE_UPPER_LEFT_INNER_PLAN_INDEX 62
#define PIPE_UPPER_RIGHT_OUTER_PLAN_INDEX 65
#define PIPE_UPPER_RIGHT_INNER_PLAN_INDEX 64
#define PIPE_LOWER_CENTER_PLAN_INDEX 57
#define PIPE_LOWER_LEFT_OUTER_PLAN_INDEX 59
#define PIPE_LOWER_LEFT_INNER_PLAN_INDEX 58
#define PIPE_LOWER_RIGHT_OUTER_PLAN_INDEX 67
#define PIPE_LOWER_RIGHT_INNER_PLAN_INDEX 68

enum PIPE_TYPE { PIPE_FULL = 0, PIPE_HALF = 1 };

enum PIPE_VERTICAL_HALF { PIPE_LOWER_HALF = 0, PIPE_UPPER_HALF = 1 };

#define NO_PLANE_INDEX 0

enum CORKSCREW_STATE { CORKSCREW_INACTIVE = 0, CORKSCREW_ACTIVE = 1 };

#define CORK_LR_HALF_WIDTH 150
#define CORK_LR_MAX_HEIGHT 265
#define CORK_LR_UPPER_HEIGHT 151

enum CORK_LR_VERTICAL_HALF { CORK_LR_LOWER_HALF = 0, CORK_LR_UPPER_HALF = 1 };

enum CORK_LR_SEGMENT {
	CORK_LR_NO_SEGMENT = 0,
	CORK_LR_LOWER_LEFT_INNER_SEGMENT = 1,
	CORK_LR_LOWER_LEFT_OUTER_SEGMENT = 2,
	CORK_LR_LOWER_LEFT_SIDE_SEGMENT = 3,
	CORK_LR_UPPER_LEFT_OUTER_SEGMENT = 4,
	CORK_LR_UPPER_LEFT_INNER_SEGMENT = 5,
	CORK_LR_UPPER_CENTER_SEGMENT = 6,
	CORK_LR_UPPER_RIGHT_INNER_SEGMENT = 7,
	CORK_LR_UPPER_RIGHT_OUTER_SEGMENT = 8,
	CORK_LR_LOWER_RIGHT_SIDE_SEGMENT = 9,
	CORK_LR_LOWER_RIGHT_OUTER_SEGMENT = 10,
	CORK_LR_LOWER_RIGHT_INNER_SEGMENT = 11
};

#define CORK_LR_PLAN_BASE 57
#define CORK_LR_END_Z 512
#define CORK_LR_WALL_INDEX 185
#define CORK_LR_WALL_HEIGHT 117
#define CORK_UD_A_PLAN_BASE 79
#define CORK_UD_A_OUTER_WALL_BASE 50
#define CORK_UD_A_INNER_WALL_BASE 75
#define CORK_UD_B_PLAN_BASE 105
#define CORK_UD_B_OUTER_WALL_BASE 0
#define CORK_UD_B_INNER_WALL_BASE 25
#define CORK_UD_LOW_HEIGHT 100
#define CORK_UD_UPPER_HEIGHT 350
#define CORK_UD_LOWER_INNER_RADIUS 392
#define CORK_UD_LOWER_OUTER_RADIUS 632
#define CORK_UD_INNER_RADIUS 332
#define CORK_UD_OUTER_RADIUS 692
#define CORK_UD_CENTER_RADIUS 512
#define CORK_UD_WALL_RADIUS_OFFSET 90
#define CORK_UD_WALL_HEIGHT 42
#define CORK_UD_WALL_INDEX_OFFSET 24
#define CORK_UD_UPPER_PLAN_OFFSET 25
#define CORK_UD_ARC_SEGMENT_COUNT 24U
#define CORK_UD_ANGLE_SCALE_SHIFT 10U
#define CORK_UD_FIRST_ARC_PLAN_OFFSET 1
#define ELEVATED_WALL_VERTICAL_OFFSET -12
#define TRACK_WALL_NONE -1
#define TRACK_WALL_DEFAULT_HEIGHT ELEVATED_WALL_VERTICAL_OFFSET
#define ELEVATED_WALL_LOWER_BOUND_DEFAULT -1000
#define SLALOM_POLE_WALL_HEIGHT 42
#define SLALOM_NEGATIVE_Z_FAR_WALL_INDEX 145
#define SLALOM_NEGATIVE_Z_NEAR_WALL_INDEX 146
#define SLALOM_NEGATIVE_Z_INNER_WALL_INDEX 148
#define SLALOM_NEGATIVE_Z_OUTER_WALL_INDEX 147
#define SLALOM_POSITIVE_Z_FAR_WALL_INDEX 141
#define SLALOM_POSITIVE_Z_NEAR_WALL_INDEX 142
#define SLALOM_POSITIVE_Z_INNER_WALL_INDEX 143
#define SLALOM_POSITIVE_Z_OUTER_WALL_INDEX 144
#define BARN_HALF_WIDTH 150
#define BARN_HEIGHT 425
#define BARN_REAR_WALL_INDEX 161
#define BARN_FORWARD_WALL_INDEX 162
#define BARN_LEFT_WALL_INDEX 164
#define BARN_RIGHT_WALL_INDEX 163
#define GAS_STATION_LEFT_X -200
#define GAS_STATION_RIGHT_X 260
#define GAS_STATION_HALF_LENGTH 80
#define GAS_STATION_HEIGHT 230
#define GAS_STATION_REAR_WALL_INDEX 165
#define GAS_STATION_FORWARD_WALL_INDEX 168
#define GAS_STATION_LEFT_WALL_INDEX 166
#define GAS_STATION_RIGHT_WALL_INDEX 167
#define JOES_HALF_WIDTH 180
#define JOES_HALF_LENGTH 100
#define JOES_HEIGHT 248
#define JOES_REAR_WALL_INDEX 169
#define JOES_FORWARD_WALL_INDEX 172
#define JOES_LEFT_WALL_INDEX 171
#define JOES_RIGHT_WALL_INDEX 170
#define OFFICE_HALF_WIDTH 200
#define OFFICE_HEIGHT 550
#define OFFICE_REAR_WALL_INDEX 173
#define OFFICE_FORWARD_WALL_INDEX 174
#define OFFICE_LEFT_WALL_INDEX 175
#define OFFICE_RIGHT_WALL_INDEX 176
#define WINDMILL_HALF_WIDTH 114
#define WINDMILL_HEIGHT 495
#define WINDMILL_REAR_WALL_INDEX 180
#define WINDMILL_FORWARD_WALL_INDEX 178
#define WINDMILL_LEFT_WALL_INDEX 177
#define WINDMILL_RIGHT_WALL_INDEX 179
#define SHIP_LEFT_X -170
#define SHIP_RIGHT_X 260
#define SHIP_HALF_LENGTH 110
#define SHIP_HEIGHT 230
#define SHIP_REAR_WALL_INDEX 181
#define SHIP_FORWARD_WALL_INDEX 184
#define SHIP_LEFT_WALL_INDEX 183
#define SHIP_RIGHT_WALL_INDEX 182
#define TRACK_TILE_EMPTY 0U
#define TRACK_SURFACE_TYPE_OFFSET 1U
#define TRACK_SURFACE_TYPE_MINIMUM 1
#define PHYSICAL_MODEL_MINIMUM 0
#define TRACK_COLLISION_ENABLED 1
#define TERRAIN_ORIENTED_LAST_TILE 18U
#define TERRAIN_ORIENTATION_MASK 3U

#define TERRAIN_SLOPE_DOWN_LAST_TILE 14U

enum TERRAIN_PLANE_INDEX {
	TERRAIN_FLAT_PLANE_INDEX = 3,
	TERRAIN_SLOPE_DOWN_PLANE_INDEX = 4,
	TERRAIN_SLOPE_UP_PLANE_INDEX = 5
};

#define TERRAIN_UPPER_HEIGHT 450
#define PLANE_ORIENTATION_COUNT 4

enum PLANE_ORIENTATION_OFFSET {
	PLANE_DEFAULT_ORIENTATION_OFFSET = 0,
	PLANE_THREE_QUARTER_TURN_OFFSET = 1,
	PLANE_HALF_TURN_OFFSET = 2,
	PLANE_QUARTER_TURN_OFFSET = 3
};

#define GRASS_HEIGHT_HASH_SHIFT 8U
#define GRASS_HEIGHT_VARIATION_MASK 1U
#define NON_GRASS_HEIGHT_OFFSET 2

extern legacy_u8 corkFlag;

extern legacy_s16 loopSurface_ZBounds0[];
extern legacy_s16 loopSurface_ZBounds1[];
extern legacy_s16 loopSurface_maxZ;
extern legacy_s16 loopSurface_XBounds0[];
extern legacy_s16 loopSurface_XBounds1[];
extern legacy_s16 loopBase_ZBounds0[];
extern legacy_s16 loopBase_ZBounds1[];
extern legacy_s16 loopBae_InnXBounds0[];
extern legacy_s16 loopBase_InnXBounds1[];
extern legacy_s16 loopBase_OutXBounds0[];
extern legacy_s16 loopBase_OutXBounds1[];
extern legacy_s16 bkRdEntr_triang_zAdjust[];
extern legacy_s16 corkLR_negZBound[];
extern legacy_s16 corkLR_posZBound[];
extern legacy_s16 highEntrZBounds0[];
extern legacy_s16 highEntrZBounds1[];
extern legacy_s16 highEntrXInnBounds0[];
extern legacy_s16 highEntrXInnBounds1[];
extern legacy_s16 highEntrXOutBounds0[];
extern legacy_s16 highEntrXOutBounds1[];

static legacy_s16 track_interpolate(legacy_s16 position, legacy_s16 position0, legacy_s16 position1,
									legacy_s16 value0, legacy_s16 value1)
{
	legacy_s32 numerator = (legacy_s32)LEGACY_S16_WRAP_SUB(position, position0) *
						   (legacy_s32)LEGACY_S16_WRAP_SUB(value1, value0);
	legacy_s16 denominator = LEGACY_S16_WRAP_SUB(position1, position0);
	legacy_s32 quotient = LEGACY_S32_DIV_OR_ZERO(numerator, (legacy_s32)denominator);
	return LEGACY_S16_WRAP_ADD(value0, LEGACY_S16_FROM_BITS((legacy_u16)quotient));
}

static void track_rotate_local(struct VECTOR *position, legacy_s16 orientation)
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
static void track_object_tile_center(legacy_u8 track_tile, legacy_s16 row_index,
									 legacy_s16 column_index)
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
static void track_object_building_wall(const struct VECTOR *next_position, legacy_s16 height,
									   legacy_s16 x_min, legacy_s16 x_max, legacy_s16 z_min,
									   legacy_s16 z_max, legacy_s16 wall_z_min,
									   legacy_s16 wall_z_max, legacy_s16 wall_x_min,
									   legacy_s16 wall_x_max)
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
static legacy_s16 track_radius_in_band(legacy_s16 x, legacy_s16 z, legacy_s16 low, legacy_s16 high)
{
	legacy_s16 radius = (legacy_s16)polarRadius2D(x, z);
	return radius > low && radius < high;
}

/* Elevated and banked corners share one arc, centred on the tile corner:
 * the radius is measured against the 1536-unit centre line, and the shape index
 * comes from the same angle quantised into the 18 segments of the arc. */
static legacy_s16 track_arc_radius(const struct VECTOR *position)
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
struct TRACK_OBJECT_SAMPLE {
	struct VECTOR position;
	struct VECTOR next_position;
	legacy_s16 height;
	legacy_s16 next_height;
	legacy_s16 physical_model;
	legacy_s16 surface_type;
	legacy_s16 absolute_x;
	legacy_s16 absolute_z;
	legacy_s16 wall_orientation_modifier;
};

static void track_object_straight(struct TRACK_OBJECT_SAMPLE *sample)
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

static void track_object_crossroad(struct TRACK_OBJECT_SAMPLE *sample)
{
	if (sample->absolute_x < ROAD_HALF_WIDTH || sample->absolute_z < ROAD_HALF_WIDTH) {
		current_surf_type = (legacy_u8)sample->surface_type;
	}
}

static void track_object_large_corner(struct TRACK_OBJECT_SAMPLE *sample)
{
	if (track_radius_in_band(LEGACY_S16_WRAP_ADD(sample->position.x, TRACK_ARC_CENTER_OFFSET),
							 LEGACY_S16_WRAP_ADD(sample->position.z, TRACK_ARC_CENTER_OFFSET),
							 LARGE_CORNER_INNER_RADIUS, LARGE_CORNER_OUTER_RADIUS)) {
		current_surf_type = (legacy_u8)sample->surface_type;
	}
}

static void track_object_chicane(struct TRACK_OBJECT_SAMPLE *sample)
{
	if (chicane_fast_grass_enabled == CHICANE_FAST_GRASS_OFF) {
		/* Use the visible road footprint: the old circular approximation
		 * cuts across the chicane's rendered curves and their edges. */
		current_surf_type =
			track_road_contains_local_point(sample->physical_model, &sample->position)
				? (legacy_u8)sample->surface_type
				: CAR_SURFACE_GRASS;
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

static void track_object_sharp_corner(struct TRACK_OBJECT_SAMPLE *sample)
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

static void track_object_large_split(struct TRACK_OBJECT_SAMPLE *sample)
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

static void track_object_highway_entrance(struct TRACK_OBJECT_SAMPLE *sample)
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

static void track_object_highway(struct TRACK_OBJECT_SAMPLE *sample)
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

static void track_object_ramp(struct TRACK_OBJECT_SAMPLE *sample)
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

static void track_object_elevated_road(struct TRACK_OBJECT_SAMPLE *sample)
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

static void track_object_elevated_corner(struct TRACK_OBJECT_SAMPLE *sample)
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

static void track_object_banked_entrance(struct TRACK_OBJECT_SAMPLE *sample)
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

static void track_object_banked_road(struct TRACK_OBJECT_SAMPLE *sample)
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

static void track_object_banked_corner(struct TRACK_OBJECT_SAMPLE *sample)
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

static void track_object_loop(struct TRACK_OBJECT_SAMPLE *sample)
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

static void track_object_tunnel(struct TRACK_OBJECT_SAMPLE *sample)
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

static void track_object_pipe_entrance(struct TRACK_OBJECT_SAMPLE *sample)
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

static void track_object_pipe(struct TRACK_OBJECT_SAMPLE *sample)
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

static void track_object_corkscrew_left_right(struct TRACK_OBJECT_SAMPLE *sample)
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

static void track_object_corkscrew_up_down(struct TRACK_OBJECT_SAMPLE *sample)
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

static void track_object_slalom(struct TRACK_OBJECT_SAMPLE *sample)
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

static void track_object_barn(struct TRACK_OBJECT_SAMPLE *sample)
{
	if (sample->absolute_x <= BARN_HALF_WIDTH && sample->absolute_z <= BARN_HALF_WIDTH) {
		track_object_building_wall(&sample->next_position, BARN_HEIGHT, -BARN_HALF_WIDTH,
								   BARN_HALF_WIDTH, -BARN_HALF_WIDTH, BARN_HALF_WIDTH,
								   BARN_REAR_WALL_INDEX, BARN_FORWARD_WALL_INDEX,
								   BARN_LEFT_WALL_INDEX, BARN_RIGHT_WALL_INDEX);
	}
}

static void track_object_gas_station(struct TRACK_OBJECT_SAMPLE *sample)
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

static void track_object_joes(struct TRACK_OBJECT_SAMPLE *sample)
{
	if (sample->absolute_x <= JOES_HALF_WIDTH && sample->absolute_z <= JOES_HALF_LENGTH) {
		track_object_building_wall(&sample->next_position, JOES_HEIGHT, -JOES_HALF_WIDTH,
								   JOES_HALF_WIDTH, -JOES_HALF_LENGTH, JOES_HALF_LENGTH,
								   JOES_REAR_WALL_INDEX, JOES_FORWARD_WALL_INDEX,
								   JOES_LEFT_WALL_INDEX, JOES_RIGHT_WALL_INDEX);
	}
}

static void track_object_office(struct TRACK_OBJECT_SAMPLE *sample)
{
	if (sample->absolute_x <= OFFICE_HALF_WIDTH && sample->absolute_z <= OFFICE_HALF_WIDTH) {
		track_object_building_wall(&sample->next_position, OFFICE_HEIGHT, -OFFICE_HALF_WIDTH,
								   OFFICE_HALF_WIDTH, -OFFICE_HALF_WIDTH, OFFICE_HALF_WIDTH,
								   OFFICE_REAR_WALL_INDEX, OFFICE_FORWARD_WALL_INDEX,
								   OFFICE_LEFT_WALL_INDEX, OFFICE_RIGHT_WALL_INDEX);
	}
}

static void track_object_windmill(struct TRACK_OBJECT_SAMPLE *sample)
{
	if (sample->absolute_x <= WINDMILL_HALF_WIDTH && sample->absolute_z <= WINDMILL_HALF_WIDTH) {
		track_object_building_wall(&sample->next_position, WINDMILL_HEIGHT, -WINDMILL_HALF_WIDTH,
								   WINDMILL_HALF_WIDTH, -WINDMILL_HALF_WIDTH, WINDMILL_HALF_WIDTH,
								   WINDMILL_REAR_WALL_INDEX, WINDMILL_FORWARD_WALL_INDEX,
								   WINDMILL_LEFT_WALL_INDEX, WINDMILL_RIGHT_WALL_INDEX);
	}
}

static void track_object_ship(struct TRACK_OBJECT_SAMPLE *sample)
{
	if (sample->position.x >= SHIP_LEFT_X && sample->position.x <= SHIP_RIGHT_X &&
		sample->absolute_z <= SHIP_HALF_LENGTH) {
		track_object_building_wall(&sample->next_position, SHIP_HEIGHT, SHIP_LEFT_X, SHIP_RIGHT_X,
								   -SHIP_HALF_LENGTH, SHIP_HALF_LENGTH, SHIP_REAR_WALL_INDEX,
								   SHIP_FORWARD_WALL_INDEX, SHIP_LEFT_WALL_INDEX,
								   SHIP_RIGHT_WALL_INDEX);
	}
}

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

static void track_object_model(struct TRACK_OBJECT_SAMPLE *sample)
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

static void track_object_world_collision(const struct VECTOR *world_position,
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

#ifdef RESTUNTS_SDL3
static struct PLANE extended_selected_plane;
static void extended_select_terrain(const struct EXTENDED_TERRAIN_SAMPLE *terrain,
									const struct VECTOR *position)
{
	elem_xCenter = terrain->origin.x;
	elem_zCenter = terrain->origin.z;
	terrainHeight = terrain->surface == CAR_SURFACE_GRASS
						? (legacy_s16)(((legacy_u16)(position->x ^ position->z) >> 8U) & 1U)
						: 2;
	current_surf_type = (legacy_s8)terrain->surface;
	extended_selected_plane = terrain->plane;
	current_planptr = &extended_selected_plane;
	planindex = EXTENDED_GEOMETRY_PLANE_INDEX;
}

void build_extended_track_terrain(struct VECTOR *position)
{
	legacy_s32 world_x, world_z;
	struct EXTENDED_TERRAIN_SAMPLE sample;
	extended_geometry_world_position(position, &world_x, &world_z);
	if (extended_geometry_terrain(extended_track_model(), world_x, world_z, &sample)) {
		extended_select_terrain(&sample, position);
		wallindex = TRACK_WALL_NONE;
		track_wall_collision_enabled = 1;
	}
}

/* Sparse occupancy frees editor cells, while the authored road and guardrails
 * can still cross a small part of their corners. Resolve that geometry before
 * falling back to the independently placed piece that owns the cell. */
static legacy_s16 extended_object_road_at(const struct CSTRK_TRACK *track,
										  const struct CSTRK_OBJECT *object,
										  const struct VECTOR *position)
{
	legacy_u32 id = extended_track_object_id(object);
	const struct TRACKOBJECT *definition = &trkObjectList[id];
	struct VECTOR origin, local;
	extended_geometry_object_origin(track, object, &origin);
	local.x = LEGACY_S16_WRAP_SUB(position->x, origin.x);
	local.y = LEGACY_S16_WRAP_SUB(position->y, origin.y);
	local.z = LEGACY_S16_WRAP_SUB(position->z, origin.z);
	if (extended_geometry_uses_grade(object)) {
		local.y =
			LEGACY_S16_WRAP_SUB(local.y, extended_geometry_object_grade(object, local.x, local.z));
	}
	track_rotate_local(&local, definition->ss_rotY);
	legacy_s16 model = definition->ss_physicalModel;
	if (model == PHYSICAL_MODEL_LARGE_CORNER) {
		return track_radius_in_band(LEGACY_S16_WRAP_ADD(local.x, TRACK_ARC_CENTER_OFFSET),
									LEGACY_S16_WRAP_ADD(local.z, TRACK_ARC_CENTER_OFFSET),
									LARGE_CORNER_INNER_RADIUS, LARGE_CORNER_OUTER_RADIUS);
	}
	if (model == PHYSICAL_MODEL_BANKED_CORNER) {
		legacy_s16 radius = track_arc_radius(&local);
		return radius > -BANKED_CORNER_INNER_OFFSET && radius < BANKED_CORNER_OUTER_OFFSET;
	}
	if (model == PHYSICAL_MODEL_ELEVATED_CORNER) {
		legacy_s16 radius = track_arc_radius(&local);
		return local.y > ELEVATED_DECK_CLEARANCE && radius > -ELEVATED_CORNER_OUTER_OFFSET &&
			   radius < ELEVATED_CORNER_OUTER_OFFSET;
	}
	return track_road_contains_local_point(model, &local);
}

static const struct CSTRK_OBJECT *
extended_collision_object(const struct CSTRK_TRACK *track,
						  const struct EXTENDED_TERRAIN_SAMPLE *terrain,
						  const struct VECTOR *position)
{
	const struct CSTRK_OBJECT *owner =
		extended_track_object_at(terrain->column, terrain->north_row);
	const struct CSTRK_OBJECT *candidates[EXTENDED_TRACK_CELL_OBJECT_MAX];
	legacy_u16 count = extended_track_objects_in_cell(terrain->column, terrain->north_row,
													  candidates, EXTENDED_TRACK_CELL_OBJECT_MAX);
	if (owner && (count == 1 || extended_object_road_at(track, owner, position))) {
		return owner;
	}
	for (legacy_u16 index = 0; index < count; index++) {
		const struct CSTRK_OBJECT *candidate = candidates[index];
		legacy_u16 full_mask = (1U << (candidate->width * candidate->height)) - 1U;
		if (candidate != owner && candidate->occupied_mask != full_mask &&
			extended_object_road_at(track, candidate, position)) {
			return candidate;
		}
	}
	return owner;
}

#define ROLL_RAMP_COLLISION_NORMAL_SCALE (TRIG_FIXED_ONE / 2)
#define ROLL_RAMP_GRILLE_CONTACT_DEPTH 16

static legacy_s64 roll_ramp_triangle_side(const struct VECTOR *a, const struct VECTOR *b,
										  const struct VECTOR *point)
{
	return (legacy_s64)(b->x - a->x) * (point->z - a->z) -
		   (legacy_s64)(b->z - a->z) * (point->x - a->x);
}

static legacy_s16 roll_ramp_triangle_contains(const struct VECTOR triangle[3],
											  const struct VECTOR *point)
{
	legacy_u8 negative = 0, positive = 0;
	for (legacy_u8 edge = 0; edge < 3U; edge++) {
		legacy_s64 side =
			roll_ramp_triangle_side(&triangle[edge], &triangle[(edge + 1U) % 3U], point);
		negative |= side < 0;
		positive |= side > 0;
	}
	return !(negative && positive);
}

static void roll_ramp_triangle_plane(const struct VECTOR triangle[3], struct PLANE *plane)
{
	const struct VECTOR *a = &triangle[0], *b = &triangle[1], *c = &triangle[2];
	legacy_s32 normal[3] = {
		(legacy_s32)(b->y - a->y) * (c->z - a->z) - (legacy_s32)(b->z - a->z) * (c->y - a->y),
		(legacy_s32)(b->z - a->z) * (c->x - a->x) - (legacy_s32)(b->x - a->x) * (c->z - a->z),
		(legacy_s32)(b->x - a->x) * (c->y - a->y) - (legacy_s32)(b->y - a->y) * (c->x - a->x)};
	if (normal[1] < 0) {
		for (legacy_u8 axis = 0; axis < 3U; axis++) {
			normal[axis] = -normal[axis];
		}
	}
	legacy_s32 maximum = 0;
	for (legacy_u8 axis = 0; axis < 3U; axis++) {
		legacy_s32 magnitude = normal[axis] < 0 ? -normal[axis] : normal[axis];
		if (magnitude > maximum) {
			maximum = magnitude;
		}
	}
	while (maximum > ROLL_RAMP_COLLISION_NORMAL_SCALE) {
		for (legacy_u8 axis = 0; axis < 3U; axis++) {
			normal[axis] /= 2;
		}
		maximum /= 2;
	}
	plane->plane_origin = *a;
	plane->plane_normal =
		(struct VECTOR){(legacy_s16)normal[0], (legacy_s16)normal[1], (legacy_s16)normal[2]};
	legacy_s16 length = polarRadius3D(&plane->plane_normal);
	if (length > 0) {
		plane->plane_normal.x = (legacy_s16)(normal[0] * ROLL_RAMP_COLLISION_NORMAL_SCALE / length);
		plane->plane_normal.y = (legacy_s16)(normal[1] * ROLL_RAMP_COLLISION_NORMAL_SCALE / length);
		plane->plane_normal.z = (legacy_s16)(normal[2] * ROLL_RAMP_COLLISION_NORMAL_SCALE / length);
	}
	legacy_s16 across = polarRadius2D(plane->plane_normal.y, plane->plane_normal.z);
	plane->plane_yz = -polarAngle(plane->plane_normal.x, across);
	plane->plane_xy = polarAngle(plane->plane_normal.z, plane->plane_normal.y);
	struct MATRIX roll, pitch;
	mat_rot_z(&roll, plane->plane_yz);
	mat_rot_x(&pitch, plane->plane_xy);
	mat_multiply(&roll, &pitch, &plane->plane_rotation);
}

static legacy_s16 extended_select_roll_ramp(const struct CSTRK_OBJECT *object,
											const struct VECTOR *origin,
											const struct VECTOR *position)
{
	struct VECTOR relative = {LEGACY_S16_WRAP_SUB(position->x, origin->x),
							  LEGACY_S16_WRAP_SUB(position->y, origin->y),
							  LEGACY_S16_WRAP_SUB(position->z, origin->z)};
	legacy_s16 orientation = roll_ramp_orientation(object);
	legacy_s16 inverse = LEGACY_S16_WRAP_NEGATE(orientation) & ANGLE_MASK;
	for (legacy_u16 index = 0; index < roll_ramp_road_triangle_count(); index++) {
		struct VECTOR triangle[3];
		roll_ramp_road_triangle(index, triangle);
		for (legacy_u8 vertex = 0; vertex < 3U; vertex++) {
			track_rotate_local(&triangle[vertex], inverse);
			triangle[vertex].y = LEGACY_S16_WRAP_ADD(
				triangle[vertex].y,
				extended_geometry_object_grade(object, triangle[vertex].x, triangle[vertex].z));
		}
		if (!roll_ramp_triangle_contains(triangle, &relative)) {
			continue;
		}
		struct PLANE plane;
		roll_ramp_triangle_plane(triangle, &plane);
		legacy_s64 distance =
			(legacy_s64)(relative.x - plane.plane_origin.x) * plane.plane_normal.x +
			(legacy_s64)(relative.y - plane.plane_origin.y) * plane.plane_normal.y +
			(legacy_s64)(relative.z - plane.plane_origin.z) * plane.plane_normal.z;
		/* The second tile is an elevated strip, not a solid wedge. A car
		 * underneath it retains contact with the independent terrain. */
		if (index >= roll_ramp_road_triangle_count() / 2U &&
			distance <
				-(legacy_s64)ROLL_RAMP_GRILLE_CONTACT_DEPTH * ROLL_RAMP_COLLISION_NORMAL_SCALE) {
			return 0;
		}
		extended_selected_plane = plane;
		current_planptr = &extended_selected_plane;
		planindex = EXTENDED_GEOMETRY_PLANE_INDEX + 1U + index;
		elem_xCenter = origin->x;
		elem_zCenter = origin->z;
		terrainHeight = origin->y;
		current_surf_type = (legacy_s8)(object->surface + CAR_SURFACE_PAVED);
		track_wall_collision_enabled = 0;
		return 1;
	}
	return 0;
}

static void build_extended_track_object(struct VECTOR *position, struct VECTOR *next)
{
	const struct CSTRK_TRACK *track = extended_track_model();
	legacy_s32 world_x, world_z;
	extended_geometry_world_position(position, &world_x, &world_z);
	struct EXTENDED_TERRAIN_SAMPLE terrain;
	if (!extended_geometry_terrain(track, world_x, world_z, &terrain)) {
		current_planptr = &planptr[0];
		return;
	}
	const struct CSTRK_OBJECT *object = extended_collision_object(track, &terrain, position);
	if (object == 0) {
		extended_select_terrain(&terrain, position);
		return;
	}
	unsigned id = extended_track_object_id(object);
	if (id >= sizeof(trkObjectList) / sizeof(trkObjectList[0])) {
		extended_select_terrain(&terrain, position);
		return;
	}
	const struct TRACKOBJECT *definition = &trkObjectList[id];
	struct VECTOR origin, local, next_local;
	extended_geometry_object_origin(track, object, &origin);
	if (object->roll_ramp) {
		if (!extended_select_roll_ramp(object, &origin, position)) {
			extended_select_terrain(&terrain, position);
		}
		return;
	}
	local.x = LEGACY_S16_WRAP_SUB(position->x, origin.x);
	local.y = LEGACY_S16_WRAP_SUB(position->y, origin.y);
	local.z = LEGACY_S16_WRAP_SUB(position->z, origin.z);
	next_local.x = LEGACY_S16_WRAP_SUB(next->x, origin.x);
	next_local.y = LEGACY_S16_WRAP_SUB(next->y, origin.y);
	next_local.z = LEGACY_S16_WRAP_SUB(next->z, origin.z);
	legacy_s16 graded = extended_geometry_uses_grade(object);
	if (graded) {
		local.y =
			LEGACY_S16_WRAP_SUB(local.y, extended_geometry_object_grade(object, local.x, local.z));
		next_local.y = LEGACY_S16_WRAP_SUB(
			next_local.y, extended_geometry_object_grade(object, next_local.x, next_local.z));
	}
	legacy_s16 orientation = definition->ss_rotY;
	track_rotate_local(&local, orientation);
	track_rotate_local(&next_local, orientation);
	struct TRACK_OBJECT_SAMPLE sample;
	sample.position = local;
	sample.next_position = next_local;
	sample.height = local.y;
	sample.next_height = next_local.y;
	sample.physical_model = definition->ss_physicalModel;
	legacy_u8 surface = object->surface;
	if (sample.physical_model == PHYSICAL_MODEL_OVERPASS && local.y <= ELEVATED_DECK_CLEARANCE) {
		surface = object->lower_surface;
	}
	sample.surface_type = (legacy_s16)(surface + CAR_SURFACE_PAVED);
	sample.absolute_x = absolute_word(local.x);
	sample.absolute_z = absolute_word(local.z);
	sample.wall_orientation_modifier = 0;
	elem_xCenter = origin.x;
	elem_zCenter = origin.z;
	terrainHeight = origin.y;
	current_surf_type = (legacy_s8)terrain.surface;
	if (object->tunnel_slalom) {
		/* The shared slalom route keeps its finite barriers and steering line.
		 * Select the tunnel shell first so a pole cannot replace its roof or
		 * outside wall with a road-level collision plane. */
		track_object_tunnel(&sample);
		if (sample.height < TUNNEL_HEIGHT && sample.next_height < TUNNEL_HEIGHT &&
			wallindex == TRACK_WALL_NONE) {
			track_object_slalom(&sample);
		}
	} else {
		track_object_model(&sample);
	}
	/* Graded driving floors use the same authored flat plane as elevated
	 * decks and roofs. Applying one transform keeps their relative heights
	 * and normals exact; terrain outside the piece retains its native plane. */
	if (planindex == NO_PLANE_INDEX && wallindex == TRACK_WALL_NONE &&
		(!graded || current_surf_type != sample.surface_type)) {
		legacy_s8 surface = current_surf_type;
		extended_select_terrain(&terrain, position);
		current_surf_type = surface;
		terrainHeight = surface == CAR_SURFACE_GRASS
							? (legacy_s16)(((legacy_u16)(position->x ^ position->z) >>
											GRASS_HEIGHT_HASH_SHIFT) &
										   GRASS_HEIGHT_VARIATION_MASK)
							: NON_GRASS_HEIGHT_OFFSET;
		return;
	}
	track_object_world_collision(position, orientation, sample.wall_orientation_modifier);
	legacy_s16 source_index = planindex;
	extended_selected_plane = *current_planptr;
	if (graded) {
		legacy_s16 original_vertical_normal = extended_selected_plane.plane_normal.y;
		extended_geometry_grade_plane(object, &extended_selected_plane);
		if (wallindex != TRACK_WALL_NONE && original_vertical_normal != 0) {
			/* Wall bounds are signed distances to the selected driving plane.
			 * The inverse-transpose normal changes their distance scale while
			 * the sheared wall keeps its original vertical clearance. */
			wallHeight =
				(legacy_s16)((legacy_s32)wallHeight * extended_selected_plane.plane_normal.y /
							 original_vertical_normal);
			elRdWallRelated =
				(legacy_s16)((legacy_s32)elRdWallRelated * extended_selected_plane.plane_normal.y /
							 original_vertical_normal);
		}
	}
	current_planptr = &extended_selected_plane;
	planindex = EXTENDED_GEOMETRY_PLANE_INDEX + source_index;
}
#endif

void build_track_object(struct VECTOR *world_position, struct VECTOR *next_world_position)
{
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
	if (extended_track_active()) {
		build_extended_track_object(world_position, next_world_position);
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
	saved->extended_plane_valid =
		extended_track_active() && planindex >= EXTENDED_GEOMETRY_PLANE_INDEX;
	if (saved->extended_plane_valid) {
		saved->extended_plane = *current_planptr;
	}
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
	if (saved->extended_plane_valid) {
		extended_selected_plane = saved->extended_plane;
		current_planptr = &extended_selected_plane;
	}
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

static legacy_s16 wall_intersection_coordinate(legacy_s16 first, legacy_s16 second,
											   legacy_s16 first_depth, legacy_s16 second_depth)
{
	legacy_s32 difference = (legacy_s32)second - first;
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		difference = LEGACY_S16_WRAP_SUB(second, first);
	}
#endif
	legacy_s32 numerator = difference * -(legacy_s32)first_depth;
	legacy_s32 denominator = (legacy_s32)second_depth - first_depth;
	return LEGACY_S16_FROM_BITS((legacy_u16)((legacy_s32)first + numerator / denominator));
}

static legacy_s16 selected_wall_matches(const struct TRACK_COLLISION_SNAPSHOT *candidate)
{
	return wallindex == candidate->wall_index && wallStartX == candidate->wall_x &&
		   wallStartZ == candidate->wall_z && wallOrientation == candidate->wall_orientation &&
		   planindex == candidate->plane_index && terrainHeight == candidate->terrain_height &&
		   elem_xCenter == candidate->element_x && elem_zCenter == candidate->element_z &&
		   wallHeight == candidate->wall_height && elRdWallRelated == candidate->wall_lower_bound;
}

#define WALL_CONTACT_PROBE_DISTANCE 2

static legacy_s16 selected_wall_contains_point(struct VECTOR *point, struct MATRIX *rotation)
{
	struct TRACK_COLLISION_SNAPSHOT candidate;
	track_collision_capture(&candidate);

	/* Requery across the wall at the intersection, not at a segment endpoint.
	 * This retains the finite extent and distinguishes a rail from the solid
	 * side below it. Two world units keep the probes on opposite grid sides. */
	legacy_s16 offset_x =
		(legacy_s16)((legacy_s32)rotation->m._31 * WALL_CONTACT_PROBE_DISTANCE / TRIG_FIXED_ONE);
	legacy_s16 offset_z =
		(legacy_s16)((legacy_s32)rotation->m._33 * WALL_CONTACT_PROBE_DISTANCE / TRIG_FIXED_ONE);
	struct VECTOR probe = {offset_x, 0, offset_z};
	struct VECTOR first = *point;
	struct VECTOR second = *point;
	first.x = LEGACY_S16_WRAP_ADD(first.x, probe.x);
	first.y = LEGACY_S16_WRAP_ADD(first.y, probe.y);
	first.z = LEGACY_S16_WRAP_ADD(first.z, probe.z);
	second.x = LEGACY_S16_WRAP_SUB(second.x, probe.x);
	second.y = LEGACY_S16_WRAP_SUB(second.y, probe.y);
	second.z = LEGACY_S16_WRAP_SUB(second.z, probe.z);
	build_track_object(&first, &second);
	legacy_s16 contains = selected_wall_matches(&candidate);
	if (!contains) {
		build_track_object(&second, &first);
		contains = selected_wall_matches(&candidate);
	}
	track_collision_restore(&candidate);
	return contains;
}

static legacy_s16 selected_wall_intersects_segment(struct VECTOR *first, struct VECTOR *second)
{
	if (wallindex == TRACK_WALL_NONE) {
		return 0;
	}
	struct MATRIX rotation;
	mat_rot_y(&rotation,
			  LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_NEGATE(wallOrientation), ANGLE_QUARTER_TURN));
	struct VECTOR relative;
	relative.x = LEGACY_S16_WRAP_SUB(first->x, wallStartX);
	relative.y = 0;
	relative.z = LEGACY_S16_WRAP_SUB(first->z, wallStartZ);
	struct VECTOR start;
	mat_mul_vector(&relative, &rotation, &start);
	relative.x = LEGACY_S16_WRAP_SUB(second->x, wallStartX);
	relative.z = LEGACY_S16_WRAP_SUB(second->z, wallStartZ);
	struct VECTOR end;
	mat_mul_vector(&relative, &rotation, &end);
	/* Endpoint touches belong to the existing wheel collision response. This
	 * test only adds walls that pierce the car between two wheel paths. */
	if ((start.z >= 0 && end.z >= 0) || (start.z <= 0 && end.z <= 0)) {
		return 0;
	}

	struct VECTOR intersection;
	intersection.x = wall_intersection_coordinate(first->x, second->x, start.z, end.z);
	intersection.y = wall_intersection_coordinate(first->y, second->y, start.z, end.z);
	intersection.z = wall_intersection_coordinate(first->z, second->z, start.z, end.z);
	/* Interpolate signed distances before rounding the world intersection. On
	 * an inclined plane, rounding its coordinates can hide a shallow impact.
	 * Compare the ratio directly to retain the strict wall height limits. */
	legacy_s16 first_height = plane_signed_distance(planindex, first->x, first->y, first->z);
	legacy_s16 second_height = plane_signed_distance(planindex, second->x, second->y, second->z);
	legacy_s32 numerator = (legacy_s32)first_height * end.z - (legacy_s32)second_height * start.z;
	legacy_s32 denominator = (legacy_s32)end.z - start.z;
	if (denominator < 0) {
		numerator = -numerator;
		denominator = -denominator;
	}
	return numerator > (legacy_s32)elRdWallRelated * denominator &&
		   numerator < (legacy_s32)wallHeight * denominator &&
		   selected_wall_contains_point(&intersection, &rotation);
}

struct TRACK_SOLID_BOX {
	struct VECTOR minimum;
	struct VECTOR maximum;
};

struct TRACK_CONTACT_RATIO {
	legacy_u32 numerator;
	legacy_u32 denominator;
};

struct TRACK_SOLID_INTERVAL {
	struct TRACK_CONTACT_RATIO entry;
	struct TRACK_CONTACT_RATIO exit;
};

static legacy_s16 solid_ratio_less(const struct TRACK_CONTACT_RATIO *first,
								   const struct TRACK_CONTACT_RATIO *second)
{
	/* Word-coordinate differences are at most 65535. Unsigned products retain
	 * the full comparison without overflow or premature fraction rounding. */
	return first->numerator * second->denominator < second->numerator * first->denominator;
}

static legacy_s16 clip_solid_axis(legacy_s16 first, legacy_s16 second, legacy_s16 minimum,
								  legacy_s16 maximum, struct TRACK_SOLID_INTERVAL *interval)
{
	legacy_s32 denominator = (legacy_s32)second - first;
	if (denominator == 0) {
		return first > minimum && first < maximum;
	}
	legacy_s32 entry = (legacy_s32)minimum - first;
	legacy_s32 exit = (legacy_s32)maximum - first;
	if (denominator < 0) {
		legacy_s32 temporary = entry;
		entry = -exit;
		exit = -temporary;
		denominator = -denominator;
	}
	if (entry >= denominator || exit <= 0) {
		return 0;
	}
	struct TRACK_CONTACT_RATIO ratio;
	ratio.denominator = (legacy_u32)denominator;
	if (entry > 0) {
		ratio.numerator = (legacy_u32)entry;
		if (solid_ratio_less(&interval->entry, &ratio)) {
			interval->entry = ratio;
		}
	}
	if (exit < denominator) {
		ratio.numerator = (legacy_u32)exit;
		if (solid_ratio_less(&ratio, &interval->exit)) {
			interval->exit = ratio;
		}
	}
	return solid_ratio_less(&interval->entry, &interval->exit);
}

static legacy_s16 solid_box_contact(const struct VECTOR *first, const struct VECTOR *second,
									const struct TRACK_SOLID_BOX *box, legacy_s16 *fraction)
{
	struct TRACK_SOLID_INTERVAL interval = {{0, 1}, {1, 1}};
	if (!clip_solid_axis(first->x, second->x, box->minimum.x, box->maximum.x, &interval) ||
		!clip_solid_axis(first->y, second->y, box->minimum.y, box->maximum.y, &interval) ||
		!clip_solid_axis(first->z, second->z, box->minimum.z, box->maximum.z, &interval)) {
		return 0;
	}
	*fraction =
		(legacy_s16)(interval.entry.numerator * TRIG_FIXED_ONE / interval.entry.denominator);
	return 1;
}

static void solid_world_bounds(struct TRACK_SOLID_BOX *world, const struct TRACK_SOLID_BOX *local,
							   legacy_s16 orientation, legacy_s16 column, legacy_s16 row,
							   legacy_s16 elevation)
{
	*world = *local;
	legacy_s16 inverse_orientation =
		LEGACY_S16_FROM_BITS((legacy_u16)LEGACY_S16_WRAP_NEGATE(orientation) & ANGLE_MASK);
	track_rotate_local(&world->minimum, inverse_orientation);
	track_rotate_local(&world->maximum, inverse_orientation);
	if (world->minimum.x > world->maximum.x) {
		legacy_s16 temporary = world->minimum.x;
		world->minimum.x = world->maximum.x;
		world->maximum.x = temporary;
	}
	if (world->minimum.z > world->maximum.z) {
		legacy_s16 temporary = world->minimum.z;
		world->minimum.z = world->maximum.z;
		world->maximum.z = temporary;
	}
	world->minimum.x = LEGACY_S16_WRAP_ADD(world->minimum.x, track_column_centers[column]);
	world->maximum.x = LEGACY_S16_WRAP_ADD(world->maximum.x, track_column_centers[column]);
	world->minimum.y = LEGACY_S16_WRAP_ADD(world->minimum.y, elevation);
	world->maximum.y = LEGACY_S16_WRAP_ADD(world->maximum.y, elevation);
	world->minimum.z = LEGACY_S16_WRAP_ADD(world->minimum.z, terraincenterpos[row]);
	world->maximum.z = LEGACY_S16_WRAP_ADD(world->maximum.z, terraincenterpos[row]);
}

#ifdef RESTUNTS_SDL3
static legacy_s16 roll_ramp_clip_solid(legacy_s64 first, legacy_s64 second, legacy_s16 *entry,
									   legacy_s16 *exit)
{
	if (first >= 0 && second >= 0) {
		return 0;
	}
	if (first <= 0 && second <= 0) {
		return 1;
	}
	legacy_s16 crossing = (legacy_s16)(first * TRIG_FIXED_ONE / (first - second));
	if (first >= 0) {
		if (crossing > *entry) {
			*entry = crossing;
		}
	} else if (crossing < *exit) {
		*exit = crossing;
	}
	return *entry < *exit;
}

static legacy_s16 roll_ramp_clip_solid_cap(legacy_s64 first, legacy_s64 second, legacy_u8 shared,
										   legacy_s16 *entry, legacy_s16 *exit)
{
	/* Adjacent concrete segments share a face. A sweep along that face still
	 * enters their union, while the outside faces retain strict clipping. */
	if (shared && first == 0 && second == 0) {
		return 1;
	}
	return roll_ramp_clip_solid(first, second, entry, exit);
}

static legacy_s16 roll_ramp_wedge_contact(const struct VECTOR *first, const struct VECTOR *second,
										  legacy_s16 *fraction)
{
	const legacy_s16 span = ROLL_RAMP_LENGTH / ROLL_RAMP_SEGMENT_COUNT;
	legacy_s16 hit = 0, earliest = TRIG_FIXED_ONE;
	/* Each concrete segment is the same linear top used by the road mesh.
	 * Clip the motion against its finite convex volume instead of extending
	 * that top plane below the open grille or beyond the launch edge. */
	for (legacy_u16 segment = 0; segment < ROLL_RAMP_SEGMENT_COUNT / 2U; segment++) {
		legacy_s16 start = segment * span, end = start + span;
		legacy_s16 height = roll_ramp_center_height(start);
		legacy_s16 rise = roll_ramp_center_height(end) - height;
		legacy_s16 entry = 0, exit = TRIG_FIXED_ONE;
		if (!roll_ramp_clip_solid(-ROLL_RAMP_HALF_WIDTH - (legacy_s32)first->x,
								  -ROLL_RAMP_HALF_WIDTH - (legacy_s32)second->x, &entry, &exit) ||
			!roll_ramp_clip_solid((legacy_s32)first->x - ROLL_RAMP_HALF_WIDTH,
								  (legacy_s32)second->x - ROLL_RAMP_HALF_WIDTH, &entry, &exit) ||
			!roll_ramp_clip_solid(-(legacy_s32)first->y, -(legacy_s32)second->y, &entry, &exit) ||
			!roll_ramp_clip_solid_cap(ROLL_RAMP_HALF_LENGTH - end - (legacy_s32)first->z,
									  ROLL_RAMP_HALF_LENGTH - end - (legacy_s32)second->z,
									  end < CSTRK_TILE_WORLD_SIZE, &entry, &exit) ||
			!roll_ramp_clip_solid_cap((legacy_s32)first->z - ROLL_RAMP_HALF_LENGTH + start,
									  (legacy_s32)second->z - ROLL_RAMP_HALF_LENGTH + start,
									  start > 0, &entry, &exit) ||
			!roll_ramp_clip_solid(((legacy_s64)first->y - height) * span -
									  (legacy_s64)rise * (ROLL_RAMP_HALF_LENGTH - first->z - start),
								  ((legacy_s64)second->y - height) * span -
									  (legacy_s64)rise *
										  (ROLL_RAMP_HALF_LENGTH - second->z - start),
								  &entry, &exit)) {
			continue;
		}
		if (!hit || entry < earliest) {
			earliest = entry;
		}
		hit = 1;
	}
	if (hit) {
		*fraction = earliest;
	}
	return hit;
}

static legacy_s16 roll_ramp_solid_contact(const struct VECTOR *first, const struct VECTOR *second,
										  legacy_s16 *fraction)
{
	legacy_s16 earliest = TRIG_FIXED_ONE;
	legacy_s16 hit = roll_ramp_wedge_contact(first, second, &earliest);
	for (legacy_u16 index = 0; index < roll_ramp_support_box_count(); index++) {
		struct ROLL_RAMP_BOX support;
		roll_ramp_support_box(index, &support);
		struct TRACK_SOLID_BOX box = {support.minimum, support.maximum};
		legacy_s16 contact;
		if (solid_box_contact(first, second, &box, &contact)) {
			if (!hit || contact < earliest) {
				earliest = contact;
			}
			hit = 1;
		}
	}
	if (hit) {
		*fraction = earliest;
	}
	return hit;
}

static legacy_s16 extended_solid_obstacle_contact(struct VECTOR *first, struct VECTOR *second,
												  legacy_s16 *fraction,
												  const struct TRACK_SOLID_BOX *boxes,
												  unsigned box_count)
{
	const struct CSTRK_TRACK *track = extended_track_model();
	legacy_s16 hit = 0, earliest = TRIG_FIXED_ONE;
	for (uint32_t i = 0; i < track->object_count; i++) {
		const struct CSTRK_OBJECT *object = &track->objects[i];
		unsigned id = extended_track_object_id(object);
		if (id >= sizeof(trkObjectList) / sizeof(trkObjectList[0]) ||
			(!object->roll_ramp && trkObjectList[id].ss_physicalModel != PHYSICAL_MODEL_SLALOM)) {
			continue;
		}
		struct VECTOR origin, local_first, local_second;
		extended_geometry_object_origin(track, object, &origin);
		local_first = (struct VECTOR){LEGACY_S16_WRAP_SUB(first->x, origin.x),
									  LEGACY_S16_WRAP_SUB(first->y, origin.y),
									  LEGACY_S16_WRAP_SUB(first->z, origin.z)};
		local_second = (struct VECTOR){LEGACY_S16_WRAP_SUB(second->x, origin.x),
									   LEGACY_S16_WRAP_SUB(second->y, origin.y),
									   LEGACY_S16_WRAP_SUB(second->z, origin.z)};
		if (extended_geometry_uses_grade(object)) {
			local_first.y = LEGACY_S16_WRAP_SUB(
				local_first.y,
				extended_geometry_object_grade(object, local_first.x, local_first.z));
			local_second.y = LEGACY_S16_WRAP_SUB(
				local_second.y,
				extended_geometry_object_grade(object, local_second.x, local_second.z));
		}
		/* Exclude another 65536-unit page with identical wrapped coordinates. */
		legacy_s32 absolute_x, absolute_z;
		extended_geometry_world_position(first, &absolute_x, &absolute_z);
		legacy_s32 center_x = object->x * 1024 + object->width * 512;
		legacy_s32 center_z = track->height * 1024 - object->y * 1024 - object->height * 512;
		if (absolute_x - center_x < -3072 || absolute_x - center_x > 3072 ||
			absolute_z - center_z < -3072 || absolute_z - center_z > 3072) {
			continue;
		}
		legacy_s16 orientation =
			object->roll_ramp ? roll_ramp_orientation(object) : trkObjectList[id].ss_rotY;
		track_rotate_local(&local_first, orientation);
		track_rotate_local(&local_second, orientation);
		if (object->roll_ramp) {
			legacy_s16 contact;
			if (roll_ramp_solid_contact(&local_first, &local_second, &contact)) {
				if (!hit || contact < earliest) {
					earliest = contact;
				}
				hit = 1;
			}
			continue;
		}
		for (unsigned box = 0; box < box_count; box++) {
			legacy_s16 contact;
			if (solid_box_contact(&local_first, &local_second, &boxes[box], &contact)) {
				if (!hit || contact < earliest) {
					earliest = contact;
				}
				hit = 1;
			}
		}
	}
	if (hit) {
		*fraction = earliest;
	}
	return hit;
}
#endif

legacy_s16 track_solid_obstacle_contact(struct VECTOR *first, struct VECTOR *second,
										legacy_s16 *fraction)
{
	/* These finite solids use the existing wall clearance above the road, but
	 * stop at their physical base rather than extending below a raised tile. */
	static const struct TRACK_SOLID_BOX slalom_boxes[] = {
		{{SLALOM_POLE_INNER_X, 0, -SLALOM_POLE_FAR_Z},
		 {SLALOM_POLE_OUTER_X, NON_GRASS_HEIGHT_OFFSET + SLALOM_POLE_WALL_HEIGHT,
		  -SLALOM_POLE_NEAR_Z}},
		{{-SLALOM_POLE_OUTER_X, 0, SLALOM_POLE_NEAR_Z},
		 {-SLALOM_POLE_INNER_X, NON_GRASS_HEIGHT_OFFSET + SLALOM_POLE_WALL_HEIGHT,
		  SLALOM_POLE_FAR_Z}}};
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		return extended_solid_obstacle_contact(first, second, fraction, slalom_boxes,
											   sizeof(slalom_boxes) / sizeof(slalom_boxes[0]));
	}
#endif
	legacy_s16 minimum_x = first->x < second->x ? first->x : second->x;
	legacy_s16 maximum_x = first->x > second->x ? first->x : second->x;
	legacy_s16 minimum_z = first->z < second->z ? first->z : second->z;
	legacy_s16 maximum_z = first->z > second->z ? first->z : second->z;
	legacy_s16 first_column = LEGACY_S16_SAR(minimum_x, TRACK_TILE_POSITION_SHIFT);
	legacy_s16 last_column = LEGACY_S16_SAR(maximum_x, TRACK_TILE_POSITION_SHIFT);
	legacy_s16 first_row = LEGACY_S16_SAR(minimum_z, TRACK_TILE_POSITION_SHIFT);
	legacy_s16 last_row = LEGACY_S16_SAR(maximum_z, TRACK_TILE_POSITION_SHIFT);
	if (first_column < 0) {
		first_column = 0;
	}
	if (last_column > TRACK_GRID_LAST_INDEX) {
		last_column = TRACK_GRID_LAST_INDEX;
	}
	if (first_row < 0) {
		first_row = 0;
	}
	if (last_row > TRACK_GRID_LAST_INDEX) {
		last_row = TRACK_GRID_LAST_INDEX;
	}

	/* Inspect every candidate tile, including solids between two outside
	 * endpoints. This lookup does not alter the selected collision globals. */
	legacy_s16 first_contact = TRIG_FIXED_ONE;
	legacy_s16 hit = 0;
	for (legacy_s16 row = first_row; row <= last_row; row++) {
		for (legacy_s16 column = first_column; column <= last_column; column++) {
			legacy_u8 tile = track_element_map[terrainrows[row] + column];
			if (tile >= sizeof(trkObjectList) / sizeof(trkObjectList[0]) ||
				trkObjectList[tile].ss_physicalModel != PHYSICAL_MODEL_SLALOM) {
				continue;
			}
			legacy_s16 elevation = track_terrain_map[trackrows[row] + column] == TERRAIN_RAISED_TILE
									   ? hillHeightConsts[TERRAIN_RAISED_HEIGHT_INDEX]
									   : 0;
			for (legacy_u16 index = 0; index < sizeof(slalom_boxes) / sizeof(slalom_boxes[0]);
				 index++) {
				struct TRACK_SOLID_BOX box;
				solid_world_bounds(&box, &slalom_boxes[index], trkObjectList[tile].ss_rotY, column,
								   row, elevation);
				legacy_s16 contact;
				if (solid_box_contact(first, second, &box, &contact)) {
					if (!hit || contact < first_contact) {
						first_contact = contact;
					}
					hit = 1;
				}
			}
		}
	}
	if (hit) {
		*fraction = first_contact;
	}
	return hit;
}

legacy_s16 track_wall_intersects_segment(struct VECTOR *first, struct VECTOR *second)
{
	struct TRACK_COLLISION_SNAPSHOT saved;
	track_collision_capture(&saved);
	build_track_object(first, second);
	legacy_s16 hit = selected_wall_intersects_segment(first, second);
	if (!hit) {
		build_track_object(second, first);
		hit = selected_wall_intersects_segment(first, second);
	}
	track_collision_restore(&saved);
	if (!hit) {
		legacy_s16 fraction;
		hit = track_solid_obstacle_contact(first, second, &fraction);
	}
	return hit;
}

legacy_s16 track_surface_contains_point(struct VECTOR *point)
{
	/* A footprint query must not replace the wheel's selected collision state. */
	struct TRACK_COLLISION_SNAPSHOT saved;
	track_collision_capture(&saved);
	build_track_object(point, point);
	legacy_s16 contains = planindex == saved.plane_index && elem_xCenter == saved.element_x &&
						  elem_zCenter == saved.element_z &&
						  terrainHeight == saved.terrain_height &&
						  track_wall_collision_enabled == 0;
	track_collision_restore(&saved);
	return contains;
}

legacy_s16 sweep_track_surface_candidates(struct VECTOR *previous, struct VECTOR *current,
										  TRACK_SURFACE_SWEEP_TEST test, legacy_s16 *fraction)
{
	struct TRACK_COLLISION_SNAPSHOT saved;
	track_collision_capture(&saved);
	legacy_s16 first_fraction = 0;
	legacy_s16 hit = test(previous, current, &first_fraction);

	/* A fast wheel can leave the surface footprint before the next lookup. */
	build_track_object(previous, current);
	legacy_s16 previous_fraction = 0;
	if (test(previous, current, &previous_fraction)) {
		if (!hit || previous_fraction < first_fraction) {
			first_fraction = previous_fraction;
		}
		hit = 1;
	}
	track_collision_restore(&saved);
	if (hit) {
		*fraction = first_fraction;
	}
	return hit;
}
