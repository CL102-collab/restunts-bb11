#ifndef RESTUNTS_FRAME_RENDER_INTERNAL_H
#define RESTUNTS_FRAME_RENDER_INTERNAL_H

#include "frame_internal.h"

#define TRACK_OBJECT_COUNT 215U
#define FRAME_SUPERSIGHT_TILE_COUNT 110
#define FRAME_DEFAULT_TRANSFORM_DISTANCE 1024
#define FRAME_TRANSFORM_FLAGS_DEFAULT 4
#define FRAME_TRANSFORM_FLAGS_NO_DEPTH_SORT 5
#define FRAME_SCENERY_PHYSICAL_MODEL_FIRST 64
#define FRAME_SINGLE_TILE_TRANSFORM_DISTANCE 2048
#define FRAME_START_FLAG_SHAPE_INDEX 111U
#define FRAME_START_FLAG_RADIUS 36
#define FRAME_START_FLAG_FAR_OFFSET 438
#define FRAME_START_FLAG_ANIMATION_SHIFT 6U
#define FRAME_START_FLAG_MAX_MATERIAL 3
#define FRAME_DETAIL_FULL 0
#define FRAME_TILE_DETAIL_FULL 0
#define FRAME_FENCE_EDGE_CLASS_COUNT 3
#define FRAME_FENCE_NONE (-1)
#define FRAME_CAMERA_TILE_SHIFT 10U
#if defined(RESTUNTS_SDL3)
#define FRAME_MAXIMUM_TILE_COUNT (90 * 90)
#else
#define FRAME_MAXIMUM_TILE_COUNT FRAME_SUPERSIGHT_TILE_COUNT
#endif
enum FRAME_FENCE_EDGE_CLASS {
	FRAME_FENCE_EDGE_LOW = 0,
	FRAME_FENCE_EDGE_HIGH = 1,
	FRAME_FENCE_EDGE_INTERIOR = 2
};

enum FRAME_TILE_PASS { FRAME_TILE_GROUND_PASS, FRAME_TILE_OBJECT_PASS };

/* Per-frame scratch data stays on the stack; none of it survives a frame. */
struct FRAME_LOOKAHEAD_TILE {
	legacy_s8 east;
	legacy_s8 south;
	legacy_s8 detail;
};

struct FRAME_CAMERA {
	struct VECTOR position;
	struct MATRIX pitch_roll_rotation;
	legacy_s16 pitch;
	legacy_s16 yaw;
	legacy_s16 roll;
	legacy_s16 skybox_parameter;
	legacy_s16 view_heading;
};

struct FRAME_TILE_SELECTION {
	const struct FRAME_LOOKAHEAD_TILE *lookahead;
	struct FRAME_LOOKAHEAD_TILE extended_lookahead[FRAME_MAXIMUM_TILE_COUNT];
	legacy_s16 count, first;
	legacy_s8 detail_threshold;
	legacy_s8 camera_east, camera_south;
	legacy_s8 player_east, player_south;
	legacy_s8 markers[FRAME_MAXIMUM_TILE_COUNT];
	legacy_s8 east[FRAME_MAXIMUM_TILE_COUNT];
	legacy_s8 south[FRAME_MAXIMUM_TILE_COUNT];
	legacy_s8 detail[FRAME_MAXIMUM_TILE_COUNT];
	legacy_u8 elements[FRAME_MAXIMUM_TILE_COUNT];
	legacy_u8 terrain[FRAME_MAXIMUM_TILE_COUNT];
#if defined(RESTUNTS_SDL3)
	legacy_s16 index_by_world_tile[FRAME_MAXIMUM_TILE_COUNT];
	legacy_s32 complete_tile_lookup;
#endif
};

struct FRAME_TILE {
#if defined(RESTUNTS_SDL3)
	legacy_u8 adaptive_flags;
#endif
	legacy_s8 east, south;
	legacy_s8 last_east, last_south;
	legacy_s8 detail;
	legacy_u8 element, terrain;
	legacy_s16 height;
	legacy_s16 depth_mask;
	struct VECTOR position;
};

extern const legacy_s8 fence_by_edge[FRAME_FENCE_EDGE_CLASS_COUNT][FRAME_FENCE_EDGE_CLASS_COUNT];
void frame_prepare_flat_track_shape(struct TRANSFORMEDSHAPE3D *shape, legacy_s8 tile_east,
									legacy_s8 tile_south, const struct VECTOR *camera_position,
									legacy_s16 flags, legacy_s16 rotation);
#ifdef RESTUNTS_SDL3
struct SHAPE3D *frame_track_shape(const struct TRACKOBJECT *object, legacy_s8 detail);
#endif

#endif
