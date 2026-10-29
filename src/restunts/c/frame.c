#include "frame_internal.h"
#include "game_input.h"
#include "trackdata_layout.h"
#include "ui_text.h"
#include "shape3d.h"
#include "track_types.h"
#include "track_objects.h"
#include "track_collision.h"
#include "wheel_transform.h"
#include "camera.h"
#include "video_frame.h"
#include "shape2d.h"
#ifdef RESTUNTS_AMIGA
#include "shape2d_internal.h"
#endif
#include "car_model.h"
#include "scene_resources.h"
#include "projection.h"
#include "skybox.h"
#include "race_graphics.h"
#include "platform.h"
#include "math.h"
#include "externs.h"
#include "crash_state.h"
#include "ghost.h"
#include "residue.h"

#if defined(RESTUNTS_SDL3)
#include "shape3d_hires.h"
#include "frame_adaptive.h"
#include "extended_track.h"
#include "owoot_road.h"
#include "extended_geometry.h"
#ifndef RESTUNTS_HEADLESS
#include "extended_replay_runtime.h"
#endif
#endif

/* Presentations read their own immutable pose. Timer callbacks continue to see
 * the authoritative state, even when a rendering helper pumps platform events. */
static const struct GAMESTATE *frame_state = &state;
static const struct CARSTATE *frame_ghost;
static const struct GHOST_CAMERA_STATE *frame_ghost_camera;
static legacy_u8 frame_uses_snapshot;

#define TRACK_OBJECT_COUNT 215U
#if defined(RESTUNTS_SDL3)
#define FRAME_TRACK_LAST_X (extended_track_width() - 1)
#define TRACK_GRID_LAST_COORDINATE (extended_track_height() - 1)
#else
#define FRAME_TRACK_LAST_X 29
#define TRACK_GRID_LAST_COORDINATE 29
#endif
#define TRACK_WORLD_TILE_SHIFT 16U
#define FRAME_CAR_WHEEL_COUNT 4
#define FRAME_LOOKAHEAD_TILE_COUNT 23
#define FRAME_SUPERSIGHT_TILE_COUNT 110
#define FRAME_LOOKAHEAD_DEPTH_TILES 4
#define FRAME_LOOKAHEAD_WIDTH_TILES 2
#if defined(RESTUNTS_SDL3)
#define FRAME_MAXIMUM_TILE_COUNT (90 * 90)
/* Match the collision height allowances in trackobj.c. */
#define FRAME_NON_GRASS_HEIGHT_OFFSET 2
#define FRAME_GRASS_HEIGHT_HASH_SHIFT 8U
#define FRAME_GRASS_HEIGHT_VARIATION_MASK 1U
#else
#define FRAME_MAXIMUM_TILE_COUNT FRAME_SUPERSIGHT_TILE_COUNT
#endif
#define FRAME_SUPERSIGHT_DISCARD_BATCH 20
#define FRAME_SUPERSIGHT_MINIMUM_TILES 4
#define FRAME_SUPERSIGHT_PROBE_INTERVAL 16
#define FRAME_SUPERSIGHT_VIEW_KEY_COUNT 20
#define FRAME_CAR_UP_VECTOR_LENGTH 30000
#define FRAME_CAR_NEAR_SORT_ADJUSTMENT 2048
#define FRAME_DEFAULT_TRANSFORM_DISTANCE 1024
#define FRAME_CAR_TRANSFORM_DISTANCE 300
#define FRAME_SCREEN_WIDTH 320
#define FRAME_DIRTY_RECT_COUNT 15
#define FRAME_FONT_HEIGHT_OFFSET 14U
#define FRAME_DEBRIS_SLOT_COUNT 24
#define FRAME_TRANSFORM_FLAGS_DEFAULT 4
#define FRAME_TRANSFORM_FLAGS_NO_DEPTH_SORT 5
#define FRAME_TRANSFORM_FLAGS_CLIPPED 12
#define FRAME_COCKPIT_HEIGHT_CLEARANCE 6
#define FRAME_CAMERA_DIRECTION_LENGTH 16384
#define FRAME_TRACK_CAMERA_HEIGHT_OFFSET 90
#define FRAME_PLANE_CLEARANCE 12
#define FRAME_CAMERA_TARGET_HEIGHT_OFFSET 50
#define FRAME_ANIMATION_PHASE_MASK 15U
#define FRAME_LOOKAHEAD_HEADING_SHIFT 7U
#define FRAME_SKYBOX_TEST_DISTANCE 1000
#define FRAME_DISTANT_SHAPE_FLAGS (7 | SHAPE3D_BACKGROUND_FLAG)
#define FRAME_DISTANT_SHAPE_COUNT 8
#define FRAME_DISTANT_SHAPE_MIN_ANGLE 135
#define FRAME_DISTANT_SHAPE_MAX_ANGLE 889
#define FRAME_DISTANT_SHAPE_HEIGHT 2790
#define FRAME_DISTANT_SHAPE_DISTANCE 15000
#define FRAME_DISTANT_SHAPE_MIN_DEPTH 200
#define FRAME_WATER_TERRAIN_FIRST 1U
#define FRAME_WATER_TERRAIN_LAST 5U
#define FRAME_HILL_ROAD_TERRAIN_FIRST 7U
#define FRAME_HILL_ROAD_TERRAIN_END 11U
#define FRAME_SCENERY_PHYSICAL_MODEL_FIRST 64
#define FRAME_MULTITILE_ROW 1
#define FRAME_MULTITILE_COLUMN 2
#define FRAME_MULTITILE_BOTH 3
#define FRAME_MULTITILE_NONE 0
#define FRAME_ELEVATED_CORNER_FIRST 105U
#define FRAME_ELEVATED_CORNER_LAST 108U
#define FRAME_ELEVATED_CORNER_COUNT 4
#define FRAME_HILL_FILL_COUNT_SINGLE 1
#define FRAME_HILL_FILL_COUNT_ROW 2
#define FRAME_HILL_FILL_COUNT_COLUMN 2
#define FRAME_HILL_FILL_COUNT_BOTH 4
#define FRAME_HILL_FILL_SHAPE_INDEX 43U
#define FRAME_SINGLE_TILE_TRANSFORM_DISTANCE 2048
#define FRAME_NO_DEPTH_SORT_FLAG 1U
#define FRAME_WHEEL_SORT_ADJUSTMENT 1024
#define FRAME_CHECKPOINT_NONE 255U
#define FRAME_CHECKPOINT_TRACK_OBJECT_BASE 212U
#define FRAME_CHECKPOINT_OWNER_OFFSET 2
#define FRAME_CHECKPOINT_TRANSFORM_DISTANCE 100
#define FRAME_START_FLAG_SHAPE_INDEX 111U
#define FRAME_START_FLAG_RADIUS 36
#define FRAME_START_FLAG_CENTER_Z 56
#define FRAME_START_FLAG_FAR_OFFSET 438
#define FRAME_START_FLAG_VERTEX_COUNT 4U
#define FRAME_START_FLAG_FIRST_VERTEX 8U
#define FRAME_START_FLAG_ANIMATION_SHIFT 6U
#define FRAME_START_FLAG_MAX_MATERIAL 3
#define FRAME_EXPLOSION_CAR_COUNT 2
#define FRAME_EXPLOSION_FRAME_SHIFT 2U
#define FRAME_EXPLOSION_VARIANT_COUNT 3
#define FRAME_EXPLOSION_FIXED_SCALE 256L
#define FRAME_ELAPSED_TIME_X 140
#define FRAME_ELAPSED_TIME_Y_OFFSET 2
#define FRAME_DIRTY_RECT_CHANGED 3
#define FRAME_SKYBOX_RECT_INDEX 5
#define FRAME_DETAIL_FULL 0
#define FRAME_DETAIL_FASTEST 4
#define FRAME_CAR_LOW_DETAIL_FIRST 3
#define FRAME_TILE_DETAIL_FULL 0
#define FRAME_STEERED_WHEEL_FIRST_VERTEX 8U
#define FRAME_FENCE_EDGE_CLASS_COUNT 3
#define FRAME_FENCE_NONE (-1)
#define FRAME_SLOW_VIDEO_TRANSFORM_FLAG 8
#define FRAME_CAMERA_TILE_SHIFT 10U
#define FRAME_FENCE_POSITION_STRIDE 2
#define FRAME_FENCE_SECOND_COORDINATE 1
#define FRAME_FENCE_POSITION_COUNT_SINGLE 1
#define FRAME_FENCE_POSITION_COUNT_ROW 2
#define FRAME_FENCE_POSITION_COUNT_COLUMN 3
#define FRAME_FENCE_POSITION_COUNT_BOTH 4
#define FRAME_SORT_MINIMUM_SHAPE_COUNT 2
#define FRAME_RECT_CENTER_SHIFT 1U

enum FRAME_TILE_MARKER {
	FRAME_TILE_DRAW_MARKER = 0,
	FRAME_TILE_MULTITILE_COVERED_MARKER = 1,
	FRAME_TILE_UNAVAILABLE_MARKER = 2
};

enum FRAME_SORT_ID { FRAME_PLAYER_SORT_ID = 2, FRAME_OPPONENT_SORT_ID = 3 };

enum FRAME_FENCE_EDGE_CLASS {
	FRAME_FENCE_EDGE_LOW = 0,
	FRAME_FENCE_EDGE_HIGH = 1,
	FRAME_FENCE_EDGE_INTERIOR = 2
};

enum FRAME_CORNER {
	FRAME_CORNER_NORTHWEST = 0,
	FRAME_CORNER_NORTHEAST = 1,
	FRAME_CORNER_SOUTHWEST = 2,
	FRAME_CORNER_SOUTHEAST = 3
};

enum FRAME_START_FLAG_VERTEX_INDEX {
	FRAME_START_FLAG_VERTEX_LEFT_NEAR = 0,
	FRAME_START_FLAG_VERTEX_LEFT_FAR = 1,
	FRAME_START_FLAG_VERTEX_RIGHT_NEAR = 2,
	FRAME_START_FLAG_VERTEX_RIGHT_FAR = 3
};

/*
 * In the original dseg, terrain_scene_objects immediately follows trkObjectList.
 * Some track objects store overlay indices into that combined legacy table,
 * so indices beyond trkObjectList intentionally address terrain_scene_objects.
 */
struct TRACKOBJECT *frame_track_object_from_legacy_index(legacy_u8 index)
{
	if (index < TRACK_OBJECT_COUNT) {
		return &trkObjectList[index];
	}
	return &terrain_scene_objects[(legacy_u16)index - TRACK_OBJECT_COUNT];
}

#if defined(RESTUNTS_SDL3)
static legacy_u8 frame_adaptive_active;
#endif

void transformed_shape_add_for_sort(legacy_s16 z_adjust, legacy_s16 type)
{
	legacy_s16 index = LEGACY_S8_FROM_BITS((legacy_u8)transformedshape_counter);
#if defined(RESTUNTS_SDL3)
	if (supersight_enabled == 0)
#endif
	{
		struct VECTOR transformed_position;
		mat_mul_vector(&curtransshape_ptr->pos, &mat_temp, &transformed_position);
		transformedshape_zarray[index] = LEGACY_S16_WRAP_ADD(transformed_position.z, z_adjust);
	}
	transformed_shape_sort_types[index] = (legacy_s8)(legacy_u8)type;
	transformedshape_indices[index] = index;
	transformedshape_counter = LEGACY_S8_WRAP_ADD(transformedshape_counter, 1);
	curtransshape_ptr++;
}

/* Each lookahead table is a run of three-byte records: the tile offset from
 * the camera tile, and the detail level to draw that tile at. */
struct FRAME_LOOKAHEAD_TILE {
	legacy_s8 east;
	legacy_s8 south;
	legacy_s8 detail;
};

#if !defined(RESTUNTS_SDL3)
/* Camera-relative candidates from Alberto Marnetto's SuperSight. Keep the
 * far-to-near painter order; priority governs which models can lose detail. */
struct FRAME_SUPERSIGHT_TILE {
	legacy_s8 width, depth, priority;
};

static const struct FRAME_SUPERSIGHT_TILE supersight_tiles[FRAME_SUPERSIGHT_TILE_COUNT] = {
	{-6, 4, 26}, {0, 10, 20}, {5, 6, 27},  {-5, 6, 27}, {6, 3, 24},	 {-6, 3, 24}, {2, 9, 24},
	{-2, 9, 24}, {6, 2, 22},  {-6, 2, 22}, {4, 7, 26},	{-4, 7, 26}, {3, 8, 25},  {-3, 8, 25},
	{5, 5, 25},	 {-5, 5, 25}, {1, 9, 21},  {-1, 9, 21}, {6, 1, 20},	 {-6, 1, 20}, {0, 9, 18},
	{5, 4, 23},	 {-5, 4, 23}, {4, 6, 24},  {-4, 6, 24}, {2, 8, 22},	 {-2, 8, 22}, {3, 7, 23},
	{-3, 7, 23}, {5, 3, 21},  {-5, 3, 21}, {1, 8, 19},	{-1, 8, 19}, {0, 8, 16},  {4, 5, 22},
	{-4, 5, 22}, {5, 2, 19},  {-5, 2, 19}, {2, 7, 20},	{-2, 7, 20}, {5, 1, 17},  {-5, 1, 17},
	{3, 6, 21},	 {-3, 6, 21}, {4, 4, 20},  {-4, 4, 20}, {1, 7, 17},	 {-1, 7, 17}, {0, 7, 14},
	{3, 5, 19},	 {-3, 5, 19}, {4, 3, 18},  {-4, 3, 18}, {2, 6, 18},	 {-2, 6, 18}, {4, 2, 16},
	{-4, 2, 16}, {1, 6, 15},  {-1, 6, 15}, {3, 4, 17},	{-3, 4, 17}, {4, 1, 14},  {-4, 1, 14},
	{0, 6, 12},	 {4, 0, 12},  {-4, 0, 12}, {2, 5, 16},	{-2, 5, 16}, {3, 3, 15},  {-3, 3, 15},
	{1, 5, 13},	 {-1, 5, 13}, {2, 4, 14},  {-2, 4, 14}, {0, 5, 10},	 {3, 2, 13},  {-3, 2, 13},
	{3, 1, 11},	 {-3, 1, 11}, {3, 0, 9},   {-3, 0, 9},	{1, 4, 11},	 {-1, 4, 11}, {2, 3, 12},
	{-2, 3, 12}, {0, 4, 8},	  {2, 2, 10},  {-2, 2, 10}, {1, 3, 9},	 {-1, 3, 9},  {2, 1, 8},
	{-2, 1, 8},	 {0, 3, 6},	  {2, -1, 8},  {-2, -1, 8}, {2, 0, 6},	 {-2, 0, 6},  {1, 2, 7},
	{-1, 2, 7},	 {0, 2, 4},	  {1, 1, 5},   {-1, 1, 5},	{1, -1, 5},	 {-1, -1, 5}, {1, 0, 3},
	{-1, 0, 3},	 {0, -2, 4},  {0, 1, 2},   {0, -1, 2},	{0, 0, 0},
};

static const legacy_s8 supersight_detail_thresholds[] = {99, 20, 18, 16, 14, 12, 10};
#endif

static legacy_s16 frame_relative_position(legacy_s32 position, legacy_s16 camera_position)
{
	return LEGACY_S16_WRAP_SUB(position_to_word(position), camera_position);
}

static legacy_s16 frame_relative_position_sum(legacy_s32 first, legacy_s32 second,
											  legacy_s16 camera_position)
{
	return frame_relative_position(LEGACY_S32_WRAP_ADD(first, second), camera_position);
}

static legacy_s16 frame_relative_track_position(legacy_s32 offset, legacy_s16 track_position,
												legacy_s16 camera_position)
{
	return LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_ADD(position_to_word(offset), track_position),
							   camera_position);
}

static legacy_s8 frame_tile_from_world(legacy_s32 position)
{
	return LEGACY_S8_FROM_BITS((legacy_u8)((legacy_u32)position >> TRACK_WORLD_TILE_SHIFT));
}

static legacy_s8 frame_south_tile_from_world(legacy_s32 position)
{
	return LEGACY_S8_WRAP_SUB(TRACK_GRID_LAST_COORDINATE, frame_tile_from_world(position));
}

static legacy_s8 frame_tile_from_world_offset(legacy_s32 position, legacy_s16 offset)
{
	return frame_tile_from_world(LEGACY_S32_WRAP_ADD_S16(position, offset));
}

static legacy_s8 frame_south_tile_from_world_offset(legacy_s32 position, legacy_s16 offset)
{
	return LEGACY_S8_WRAP_SUB(TRACK_GRID_LAST_COORDINATE,
							  frame_tile_from_world_offset(position, offset));
}

static legacy_s16 frame_car_z_adjust(const legacy_s8 *wheel_surfaces, struct MATRIX *rotation)
{
	if (wheel_surfaces[0] == CAR_SURFACE_GRASS && wheel_surfaces[1] == CAR_SURFACE_GRASS &&
		wheel_surfaces[2] == CAR_SURFACE_GRASS && wheel_surfaces[3] == CAR_SURFACE_GRASS) {
		return 0;
	}
	struct VECTOR offset_vector;
	offset_vector.x = 0;
	offset_vector.z = 0;
	offset_vector.y = FRAME_CAR_UP_VECTOR_LENGTH;
	struct VECTOR rotated_vector;
	mat_mul_vector(&offset_vector, rotation, &rotated_vector);
	mat_mul_vector(&rotated_vector, &mat_temp, &offset_vector);
	if (offset_vector.z <= 0) {
		return -FRAME_CAR_NEAR_SORT_ADJUSTMENT;
	}
	return FRAME_CAR_NEAR_SORT_ADJUSTMENT;
}

#if defined(RESTUNTS_SDL3)
static legacy_s32 frame_adaptive_world_tile(legacy_s32 position, legacy_s16 offset)
{
	/* Positions and SIMD wheel offsets both have six fractional bits. Add
	 * before converting to tiles, retaining fractional boundary crossings.
	 * Widen the sum and floor negative values to keep off-map tiles signed. */
	legacy_s64 combined = (legacy_s64)position + offset;
	const legacy_s64 tile_size = (legacy_s64)1 << TRACK_WORLD_TILE_SHIFT;
	if (combined < 0) {
		combined -= tile_size - 1;
	}
	return (legacy_s32)(combined / tile_size);
}

static legacy_u8 frame_adaptive_position_flags(const struct VECTOR *relative,
											   const struct VECTOR *camera)
{
	if (frame_adaptive_active == 0) {
		return FRAME_ADAPTIVE_FULL;
	}
	legacy_s16 east = ((legacy_s32)relative->x + camera->x) >> FRAME_CAMERA_TILE_SHIFT;
	legacy_s16 south = TRACK_GRID_LAST_COORDINATE -
					   (((legacy_s32)relative->z + camera->z) >> FRAME_CAMERA_TILE_SHIFT);
	return frame_adaptive_flags(&frame_adaptive, east, south);
}

static legacy_u8 frame_adaptive_car_flags(const struct CARSTATE *carstate, const struct SIMD *simd)
{
	if (frame_adaptive_active == 0) {
		return FRAME_ADAPTIVE_FULL;
	}
	legacy_s32 first_east = frame_adaptive_world_tile(carstate->car_position.lx, 0);
	legacy_s32 last_east = first_east;
	legacy_s32 first_south =
		TRACK_GRID_LAST_COORDINATE - frame_adaptive_world_tile(carstate->car_position.lz, 0);
	legacy_s32 last_south = first_south;
	struct MATRIX rotation =
		*mat_rot_zxy(LEGACY_S16_WRAP_NEGATE(carstate->car_rotate.z),
					 LEGACY_S16_WRAP_NEGATE(carstate->car_rotate.y),
					 LEGACY_S16_WRAP_NEGATE(carstate->car_rotate.x), MATRIX_ROTATION_ORDER_ZXY);
	for (legacy_s16 wheel = 0; wheel < FRAME_CAR_WHEEL_COUNT; wheel++) {
		struct VECTOR offset;
		struct VECTOR wheel_position = simd->wheel_coords[wheel];
		mat_mul_vector(&wheel_position, &rotation, &offset);
		legacy_s32 east = frame_adaptive_world_tile(carstate->car_position.lx, offset.x);
		legacy_s32 south = TRACK_GRID_LAST_COORDINATE -
						   frame_adaptive_world_tile(carstate->car_position.lz, offset.z);
		if (east < first_east) {
			first_east = east;
		}
		if (east > last_east) {
			last_east = east;
		}
		if (south < first_south) {
			first_south = south;
		}
		if (south > last_south) {
			last_south = south;
		}
	}
	/* Off-map cells contribute HIDE to the footprint intersection. Clipping
	 * the bounds also keeps malformed or far-outside positions inexpensive. */
	if (first_east < 0) {
		first_east = 0;
	}
	if (first_south < 0) {
		first_south = 0;
	}
	if (last_east > FRAME_TRACK_LAST_X) {
		last_east = FRAME_TRACK_LAST_X;
	}
	if (last_south > TRACK_GRID_LAST_COORDINATE) {
		last_south = TRACK_GRID_LAST_COORDINATE;
	}
	legacy_u8 flags = FRAME_ADAPTIVE_HIDE;
	for (legacy_s32 south = first_south; south <= last_south; south++) {
		for (legacy_s32 east = first_east; east <= last_east; east++) {
			flags &= frame_adaptive_flags(&frame_adaptive, east, south);
		}
	}
	return flags;
}
#endif

static void frame_add_dynamic_shape(struct TRACKOBJECT *track_object, legacy_s16 state_index,
									legacy_s16 flags, legacy_s16 material, legacy_s16 z_adjust,
									const struct VECTOR *camera_position)
{
#if defined(RESTUNTS_SDL3)
	legacy_u8 adaptive_flags =
		frame_adaptive_position_flags(&curtransshape_ptr->pos, camera_position);
	if (adaptive_flags == FRAME_ADAPTIVE_HIDE) {
		return;
	}
#else
	(void)camera_position;
#endif
	curtransshape_ptr->shapeptr = track_object->ss_shapePtr;
#if defined(RESTUNTS_SDL3)
	if (adaptive_flags == FRAME_ADAPTIVE_LOW_GEOMETRY && track_object->ss_loShapePtr != 0) {
		curtransshape_ptr->shapeptr = track_object->ss_loShapePtr;
	}
#endif
	curtransshape_ptr->rectptr = &frame_sorted_shapes_rect;
	curtransshape_ptr->ts_flags = flags;
	curtransshape_ptr->rotvec.x =
		LEGACY_S16_WRAP_NEGATE(frame_state->game_particle_rotation_x[state_index]);
	curtransshape_ptr->rotvec.y =
		LEGACY_S16_WRAP_NEGATE(frame_state->game_particle_rotation_y[state_index]);
	curtransshape_ptr->rotvec.z =
		LEGACY_S16_WRAP_NEGATE(frame_state->game_particle_heading[state_index]);
	curtransshape_ptr->culling_distance = FRAME_DEFAULT_TRANSFORM_DISTANCE;
	curtransshape_ptr->material = material;
	transformed_shape_add_for_sort(z_adjust, 0);
}

static void frame_prepare_flat_track_shape(struct TRANSFORMEDSHAPE3D *shape, legacy_s8 tile_east,
										   legacy_s8 tile_south,
										   const struct VECTOR *camera_position, legacy_s16 flags,
										   legacy_s16 rotation)
{
	shape->pos.x = LEGACY_S16_WRAP_SUB(track_column_centers[tile_east], camera_position->x);
	shape->pos.y = LEGACY_S16_WRAP_NEGATE(camera_position->y);
	shape->pos.z = LEGACY_S16_WRAP_SUB(track_row_centers[tile_south], camera_position->z);
	shape->rectptr = &frame_unsorted_shapes_rect;
	shape->ts_flags = flags;
	shape->rotvec.x = 0;
	shape->rotvec.y = 0;
	shape->rotvec.z = rotation;
	shape->culling_distance = FRAME_DEFAULT_TRANSFORM_DISTANCE;
	shape->material = 0;
}

void init_rect_arrays(void)
{
	if (slow_video_mgmt_copy == 0) {
		return;
	}

	frame_rects_page0[0] = full_screen_rect;
	frame_rects_page1[0] = full_screen_rect;
	for (legacy_s16 i = 1; i < FRAME_DIRTY_RECT_COUNT; i++) {
		frame_rects_page0[i] = empty_rect;
		frame_rects_page1[i] = empty_rect;
	}
}

void font_set_fontdef2(void far *data)
{
	set_fontdefseg(data);
	font_glyph_height =
		LEGACY_S16_FROM_BITS(LEGACY_READ_U16_LE((legacy_u8 far *)data + FRAME_FONT_HEIGHT_OFFSET));
}

void font_set_fontdef(void)
{
	font_set_fontdef2(fontdefptr);
}

static void frame_mark_changed_rects(void)
{
	for (legacy_s16 i = 0; i < FRAME_DIRTY_RECT_COUNT; i++) {
		frame_rect_change_flags[i] = FRAME_DIRTY_RECT_CHANGED;
	}
	if (detail_level == FRAME_DETAIL_FASTEST) {
		frame_buffer_camera_headings[1] = last_rendered_camera_heading;
	}
	if (frame_buffer_camera_headings[1] == last_rendered_camera_heading &&
		frame_rects_page0[FRAME_SKYBOX_RECT_INDEX].left ==
			frame_rects_page1[FRAME_SKYBOX_RECT_INDEX].left &&
		frame_rects_page0[FRAME_SKYBOX_RECT_INDEX].right ==
			frame_rects_page1[FRAME_SKYBOX_RECT_INDEX].right &&
		frame_rects_page0[FRAME_SKYBOX_RECT_INDEX].top ==
			frame_rects_page1[FRAME_SKYBOX_RECT_INDEX].top &&
		frame_rects_page0[FRAME_SKYBOX_RECT_INDEX].bottom ==
			frame_rects_page1[FRAME_SKYBOX_RECT_INDEX].bottom) {
		frame_rect_change_flags[FRAME_SKYBOX_RECT_INDEX] = 0;
	}
}

void frame_present(struct RECTANGLE *cliprect)
{
#ifdef RESTUNTS_AMIGA
	if (render_window_sprite == &screen_sprite) {
		/* Native races draw into the indexed screen, already hidden behind
		 * the displayed planar page. Presentation follows dashboard updates. */
		sprite_select_screen_compat();
		mouse_draw_transparent_check();
		if (slow_video_mgmt_copy != 0) {
			frame_buffer_camera_headings[1] = last_rendered_camera_heading;
			for (legacy_s16 i = 0; i < FRAME_DIRTY_RECT_COUNT; i++) {
				frame_rects_page1[i] = frame_rects_page0[i];
			}
		}
		return;
	}
#endif
	if (video_uses_page_flipping != 0) {
		return;
	}

	sprite_select_screen_compat();
	struct RECTANGLE *dirty_rect;
	if (full_redraw_frames_remaining != 0) {
		mouse_draw_opaque_check();
		sprite_putimage(render_window_sprite->sprite_bitmapptr);
	} else if (slow_video_mgmt_copy == 0) {
		sprite_set_target_clip_bounds(cliprect->left, cliprect->right, cliprect->top,
									  cliprect->bottom);
		mouse_draw_opaque_check();
		sprite_putimage(render_window_sprite->sprite_bitmapptr);
	} else {
		frame_mark_changed_rects();

		redraw_rect_count = 0;
		rectlist_add_rects(FRAME_DIRTY_RECT_COUNT, frame_rect_change_flags, frame_rects_page0,
						   frame_rects_page1, cliprect, &redraw_rect_count, merged_redraw_rects);
		if (redraw_rect_count != 0) {
			rect_array_sort_by_top(redraw_rect_count, merged_redraw_rects,
								   redraw_rect_sort_indices);
			mouse_draw_opaque_check();
			for (legacy_s16 i = 0; i < redraw_rect_count; i++) {
				dirty_rect = &merged_redraw_rects[redraw_rect_sort_indices[i]];
				sprite_set_target_clip_bounds(dirty_rect->left, dirty_rect->right, dirty_rect->top,
											  dirty_rect->bottom);
				sprite_putimage(render_window_sprite->sprite_bitmapptr);
			}
		} else {
			sprite_set_target_clip_bounds(0, FRAME_SCREEN_WIDTH, cliprect->top, cliprect->bottom);
			mouse_draw_opaque_check();
			sprite_putimage(render_window_sprite->sprite_bitmapptr);
		}
	}

	mouse_draw_transparent_check();
	if (slow_video_mgmt_copy != 0) {
		frame_buffer_camera_headings[1] = last_rendered_camera_heading;
		for (legacy_s16 i = 0; i < FRAME_DIRTY_RECT_COUNT; i++) {
			frame_rects_page1[i] = frame_rects_page0[i];
		}
	}
}

#if defined(RESTUNTS_SDL3)
static struct VECTOR frame_car_render_offset(const struct CARSTATE *carstate,
											 const struct SHAPE3D *wheel_shape)
{
	struct VECTOR local = {0, -shape3d_car_ground_offset(wheel_shape), 0};
	struct VECTOR offset;
	struct MATRIX *rotation =
		mat_rot_zxy(LEGACY_S16_WRAP_NEGATE(carstate->car_rotate.z),
					LEGACY_S16_WRAP_NEGATE(carstate->car_rotate.y),
					LEGACY_S16_WRAP_NEGATE(carstate->car_rotate.x), MATRIX_ROTATION_ORDER_ZXY);
	mat_mul_vector(&local, rotation, &offset);
	/* Collision surfaces are raised two world units above the road artwork.
	 * Grass uses a zero/one-unit height variation instead. Remove the lowest
	 * supported-wheel bias to avoid sinking tires at mixed surface edges.
	 * Keep the road allowance in flight, without snapping to a surface. */
	legacy_s16 surface_bias = FRAME_NON_GRASS_HEIGHT_OFFSET;
	for (legacy_s16 wheel = 0; wheel < FRAME_CAR_WHEEL_COUNT; wheel++) {
		if (carstate->car_surfaceWhl[wheel] == CAR_SURFACE_GRASS) {
			const struct VECTOR *contact = &carstate->car_wheel_contact_positions[wheel];
			legacy_s16 grass_bias =
				((legacy_u16)(contact->x ^ contact->z) >> FRAME_GRASS_HEIGHT_HASH_SHIFT) &
				FRAME_GRASS_HEIGHT_VARIATION_MASK;
			if (grass_bias < surface_bias) {
				surface_bias = grass_bias;
			}
		}
	}
	offset.y = LEGACY_S16_WRAP_SUB(offset.y, surface_bias);
	return offset;
}

static void frame_offset_car_shape(const struct VECTOR *offset)
{
	curtransshape_ptr->pos.x = LEGACY_S16_WRAP_ADD(curtransshape_ptr->pos.x, offset->x);
	curtransshape_ptr->pos.y = LEGACY_S16_WRAP_ADD(curtransshape_ptr->pos.y, offset->y);
	curtransshape_ptr->pos.z = LEGACY_S16_WRAP_ADD(curtransshape_ptr->pos.z, offset->z);
}
#endif

/* The player and the opponent are drawn identically: first the debris
 * attached to that car, then the car body itself with its wheels, clip
 * rectangle and rotation. Only the shapes, buffers and material differ. */
static void frame_add_car(const struct CARSTATE *carstate, legacy_s8 debris_owner,
						  legacy_u16 car_object, struct SHAPE3D *wheel_shape,
						  legacy_s16 *wheel_angles, struct VECTOR *wheel_vectors,
						  struct VECTOR *wheel_vector, struct RECTANGLE *slow_rect,
						  struct RECTANGLE *crash_rect, const struct VECTOR *camera_position,
						  legacy_s8 tile_detail, legacy_s8 flags, legacy_s16 material,
						  legacy_s16 z_adjust)
{
#if defined(RESTUNTS_SDL3)
	if (frame_adaptive_active != 0) {
		const struct SIMD *simd =
			debris_owner == PLAYER_CAR_INDEX
				? &simd_player
				: (gameconfig.game_opponenttype != 0 ? &simd_opponent : ghost_car_simd());
		legacy_u8 adaptive_flags = frame_adaptive_car_flags(carstate, simd);
		if (adaptive_flags == FRAME_ADAPTIVE_HIDE) {
			return;
		}
		tile_detail = adaptive_flags == FRAME_ADAPTIVE_LOW_GEOMETRY;
	}
	struct VECTOR render_offset = {0, 0, 0};
	if (supersight_enabled != 0) {
		flags |= SHAPE3D_NO_SHADOW_RECEIVE_FLAG;
		render_offset = frame_car_render_offset(carstate, wheel_shape);
	}
#endif
	struct TRACKOBJECT *track_object;
	if (frame_state->game_particles_active != 0 && (flags & SHAPE3D_GHOST_FLAG) == 0U
#if defined(RESTUNTS_SDL3)
		&& frame_adaptive_active == 0
#endif
	) {
		for (legacy_s16 index = 0; index < FRAME_DEBRIS_SLOT_COUNT; index++) {
			if (frame_state->game_particle_forward_speed[index] != 0 &&
				frame_state->game_particle_owner[index] == debris_owner) {
				track_object =
					&particle_scene_objects[frame_state->game_particle_shape_index[index]];
				curtransshape_ptr->pos.x =
					frame_relative_position_sum(frame_state->game_particle_x[index],
												carstate->car_position.lx, camera_position->x);
				curtransshape_ptr->pos.y =
					frame_relative_position_sum(frame_state->game_particle_y[index],
												carstate->car_position.ly, camera_position->y);
				curtransshape_ptr->pos.z =
					frame_relative_position_sum(frame_state->game_particle_z[index],
												carstate->car_position.lz, camera_position->z);
#if defined(RESTUNTS_SDL3)
				frame_offset_car_shape(&render_offset);
#endif
				frame_add_dynamic_shape(track_object, index,
										flags | FRAME_TRANSFORM_FLAGS_NO_DEPTH_SORT, material,
										z_adjust, camera_position);
			}
		}
	}

	track_object = &trkObjectList[car_object];
	curtransshape_ptr->pos.x =
		frame_relative_position(carstate->car_position.lx, camera_position->x);
	curtransshape_ptr->pos.y =
		frame_relative_position(carstate->car_position.ly, camera_position->y);
	curtransshape_ptr->pos.z =
		frame_relative_position(carstate->car_position.lz, camera_position->z);
#if defined(RESTUNTS_SDL3)
	frame_offset_car_shape(&render_offset);
#endif

	if ((tile_detail != FRAME_TILE_DETAIL_FULL ||
		 (supersight_enabled == 0 && detail_level >= FRAME_CAR_LOW_DETAIL_FIRST))
#if defined(RESTUNTS_SDL3)
		&& (supersight_enabled == 0 || track_object->ss_loShapePtr != 0)
#endif
	) {
		curtransshape_ptr->shapeptr = track_object->ss_loShapePtr;
	} else {
		curtransshape_ptr->shapeptr = track_object->ss_shapePtr;
		shape3d_update_car_wheel_vertices(
			wheel_shape, FRAME_STEERED_WHEEL_FIRST_VERTEX, carstate->car_steeringAngle,
			carstate->car_suspension_deflection, wheel_angles, wheel_vectors, wheel_vector);
	}

	if (slow_video_mgmt_copy != 0) {
		curtransshape_ptr->rectptr = slow_rect;
		curtransshape_ptr->ts_flags = FRAME_TRANSFORM_FLAGS_CLIPPED;
	} else if (carstate->car_crashBmpFlag != CRASH_EVENT_COLLISION) {
		curtransshape_ptr->ts_flags = FRAME_TRANSFORM_FLAGS_DEFAULT;
	} else {
		*crash_rect = empty_rect;
		curtransshape_ptr->rectptr = crash_rect;
		curtransshape_ptr->ts_flags = FRAME_TRANSFORM_FLAGS_CLIPPED;
	}

	curtransshape_ptr->ts_flags |= flags & SHAPE3D_GHOST_FLAG;
#if defined(RESTUNTS_SDL3)
	curtransshape_ptr->ts_flags |= flags & SHAPE3D_NO_SHADOW_RECEIVE_FLAG;
#endif
	curtransshape_ptr->rotvec.x = LEGACY_S16_WRAP_NEGATE(carstate->car_rotate.z);
	curtransshape_ptr->rotvec.y = LEGACY_S16_WRAP_NEGATE(carstate->car_rotate.y);
	curtransshape_ptr->rotvec.z = LEGACY_S16_WRAP_NEGATE(carstate->car_rotate.x);
	curtransshape_ptr->culling_distance = FRAME_CAR_TRANSFORM_DISTANCE;
	curtransshape_ptr->material = material;
	/* The sort slot carries the same id as the track object: 2 for the
	   player, 3 for the opponent. */
	transformed_shape_add_for_sort(z_adjust, (legacy_s16)car_object);
}

/* Border fences: a tile sits on the low edge (0), the high edge (1) or in
 * between (2) along each axis, and the pair picks the fence piece. -1 means
 * the tile is not on the border at all. */
static const legacy_s8 fence_by_edge[FRAME_FENCE_EDGE_CLASS_COUNT][FRAME_FENCE_EDGE_CLASS_COUNT] = {
	{7, 5, 6}, {1, 3, 2}, {0, 4, FRAME_FENCE_NONE}};

static legacy_s16 frame_border_index(legacy_s8 offset)
{
	if (offset == 0) {
		return FRAME_FENCE_EDGE_LOW;
	}
	if (offset == TRACK_GRID_LAST_COORDINATE) {
		return FRAME_FENCE_EDGE_HIGH;
	}
	return FRAME_FENCE_EDGE_INTERIOR;
}

/* The camera looks out of the car, so it uses the car's rotation inverted. */
static struct MATRIX *frame_car_rotation(legacy_s16 rot_x, legacy_s16 rot_y, legacy_s16 rot_z)
{
	return mat_rot_zxy(LEGACY_S16_WRAP_NEGATE(rot_z), LEGACY_S16_WRAP_NEGATE(rot_y),
					   LEGACY_S16_WRAP_NEGATE(rot_x), MATRIX_ROTATION_ORDER_ZXY);
}

/* Per-frame scratch data stays on the stack; none of it survives a frame. */
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

struct FRAME_CAR_RENDER {
	legacy_s8 east, south;
	legacy_s16 depth_adjustment;
	struct RECTANGLE crash_rect;
	legacy_s8 explosion_visible;
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

#if defined(RESTUNTS_SDL3)
/* Geometry decisions use one cached byte per tile. Intersecting the masks for
 * an object's complete footprint keeps it whenever any covered tile needs it. */
static legacy_u8 frame_adaptive_tile_flags(const struct FRAME_TILE *tile)
{
	legacy_u8 flags = frame_adaptive_flags(&frame_adaptive, tile->east, tile->south);
	if (tile->element != 0) {
		legacy_u8 footprint = trkObjectList[tile->element].ss_multiTileFlag;
		legacy_s16 last_east = tile->east + ((footprint & FRAME_MULTITILE_COLUMN) != 0);
		legacy_s16 last_south = tile->south + ((footprint & FRAME_MULTITILE_ROW) != 0);
		for (legacy_s16 south = tile->south; south <= last_south; south++) {
			for (legacy_s16 east = tile->east; east <= last_east; east++) {
				flags &= frame_adaptive_flags(&frame_adaptive, east, south);
			}
		}
	}
	return flags;
}

static struct SHAPE3D *frame_track_shape(const struct TRACKOBJECT *object, legacy_s8 detail)
{
	/* A missing alternative must never turn a detail reduction into removal. */
	return detail != FRAME_TILE_DETAIL_FULL &&
				   (supersight_enabled == 0 || object->ss_loShapePtr != 0)
			   ? object->ss_loShapePtr
			   : object->ss_shapePtr;
}

static legacy_s32 frame_has_complete_tile_lookup(const struct FRAME_TILE_SELECTION *tiles)
{
	return tiles->complete_tile_lookup && tiles->lookahead == tiles->extended_lookahead;
}

static legacy_s16 frame_lookup_world_tile(const struct FRAME_TILE_SELECTION *tiles, legacy_s16 east,
										  legacy_s16 south)
{
	if (east < 0 || east > FRAME_TRACK_LAST_X || south < 0 || south > TRACK_GRID_LAST_COORDINATE) {
		return -1;
	}
	return tiles->index_by_world_tile[south * extended_track_width() + east];
}
#endif

static legacy_s16 frame_find_car_wheel(const struct CARSTATE *carstate, const struct SIMD *simd,
									   const struct FRAME_TILE_SELECTION *tiles,
									   legacy_s8 *result_tile_east, legacy_s8 *result_tile_south)
{
	struct MATRIX *rotation =
		mat_rot_zxy(LEGACY_S16_WRAP_NEGATE(carstate->car_rotate.z),
					LEGACY_S16_WRAP_NEGATE(carstate->car_rotate.y),
					LEGACY_S16_WRAP_NEGATE(carstate->car_rotate.x), MATRIX_ROTATION_ORDER_ZXY);
	struct VECTOR rotated_vector;
	legacy_s16 best_tile_index = -1;
	legacy_s16 matched_wheel = -1;
	struct VECTOR offset_vector;
	for (legacy_s16 wheel = 0; wheel < FRAME_CAR_WHEEL_COUNT; wheel++) {
		offset_vector = simd->wheel_coords[wheel];
		mat_mul_vector(&offset_vector, rotation, &rotated_vector);
		legacy_s8 tile_east, tile_south;
#if defined(RESTUNTS_SDL3)
		if (frame_adaptive_active != 0) {
			legacy_s32 east =
				frame_adaptive_world_tile(carstate->car_position.lx, rotated_vector.x);
			legacy_s32 south =
				TRACK_GRID_LAST_COORDINATE -
				frame_adaptive_world_tile(carstate->car_position.lz, rotated_vector.z);
			if (east < 0 || east > FRAME_TRACK_LAST_X || south < 0 ||
				south > TRACK_GRID_LAST_COORDINATE) {
				continue;
			}
			tile_east = east;
			tile_south = south;
		} else
#endif
		{
			tile_east = frame_tile_from_world_offset(carstate->car_position.lx, rotated_vector.x);
			tile_south =
				frame_south_tile_from_world_offset(carstate->car_position.lz, rotated_vector.z);
		}
#if defined(RESTUNTS_SDL3)
		if (frame_has_complete_tile_lookup(tiles)) {
			legacy_s16 tile_index = frame_lookup_world_tile(tiles, tile_east, tile_south);
			if (tile_index > best_tile_index &&
				tiles->markers[tile_index] != FRAME_TILE_UNAVAILABLE_MARKER) {
				*result_tile_east = tile_east;
				*result_tile_south = tile_south;
				best_tile_index = tile_index;
				matched_wheel = wheel;
			}
			continue;
		}
#endif
		for (legacy_s16 tile_index = tiles->count - 1; tile_index > best_tile_index; tile_index--) {
			if (tiles->markers[tile_index] != FRAME_TILE_UNAVAILABLE_MARKER &&
				tiles->lookahead[tile_index].east + tiles->camera_east == tile_east &&
				tiles->lookahead[tile_index].south + tiles->camera_south == tile_south) {
				*result_tile_east = tile_east;
				*result_tile_south = tile_south;
				best_tile_index = tile_index;
				matched_wheel = wheel;
			}
		}
	}
	if (matched_wheel != -1) {
		return frame_car_z_adjust(carstate->car_surfaceWhl, rotation);
	}
	return 0;
}

static legacy_s8 frame_begin(legacy_s8 buffer_index)
{
	if (video_uses_page_flipping == 0 || buffer_index == 0) {
		active_frame_rects = frame_rects_page0;
		alternate_frame_rects = frame_rects_page1;
	} else {
		alternate_frame_rects = frame_rects_page0;
		active_frame_rects = frame_rects_page1;
	}

	struct RECTANGLE *redraw_rect;
	legacy_s8 redraw_transform_flags;
	if (slow_video_mgmt_copy != 0) {
		redraw_transform_flags = FRAME_SLOW_VIDEO_TRANSFORM_FLAG;
		redraw_rect = frame_layer_rects;
		for (legacy_s16 rect_index = 0; rect_index < FRAME_DIRTY_RECT_COUNT; rect_index++) {
			*redraw_rect = empty_rect;
			redraw_rect++;
		}
	} else {
		redraw_transform_flags = 0;
	}

	return redraw_transform_flags;
}

/* Ghosts share the opponent drawing and camera slot without adding a
 * simulated car, collision body or sound source. */
static const struct CARSTATE *frame_second_car_state(void)
{
	return gameconfig.game_opponenttype != 0
			   ? &frame_state->opponentstate
			   : (frame_uses_snapshot != 0 ? frame_ghost : ghost_car_state());
}

static const struct CARSTATE *frame_viewed_car_state(void)
{
	const struct CARSTATE *second_car = followOpponentFlag != 0 ? frame_second_car_state() : 0;
	return second_car != 0 ? second_car : &frame_state->playerstate;
}

static const struct GHOST_CAMERA_STATE *frame_viewed_ghost_camera(void)
{
	return followOpponentFlag != 0 && gameconfig.game_opponenttype == 0
			   ? (frame_uses_snapshot != 0 ? frame_ghost_camera : ghost_camera_state())
			   : 0;
}

static legacy_s16 frame_position_camera(struct FRAME_CAMERA *camera, const struct VECTOR *car_pos,
										legacy_s16 car_rot_x, legacy_s16 car_rot_y,
										legacy_s16 car_rot_z,
										const struct GHOST_CAMERA_STATE *ghost_camera)
{
	// Set camera position, based on the car position and the camera mode
	struct MATRIX *car_rot_matrix;
	struct VECTOR car_to_cam_rotated;
	legacy_s16 camera_roll = 0;
	struct VECTOR offset_vector;
	legacy_u8 car_index =
		followOpponentFlag != 0 && (gameconfig.game_opponenttype != 0 || ghost_camera != 0)
			? OPPONENT_CAR_INDEX
			: PLAYER_CAR_INDEX;
	if (cameramode == CAMERA_MODE_COCKPIT) {
		camera->yaw = car_rot_x & ANGLE_MASK;
		camera->pitch = car_rot_y & ANGLE_MASK;
		camera_roll = car_rot_z & ANGLE_MASK;
		car_rot_matrix = frame_car_rotation(car_rot_x, car_rot_y, car_rot_z);
		offset_vector.x = 0;
		offset_vector.z = 0;
		offset_vector.y = LEGACY_S16_WRAP_SUB(ghost_camera != 0 ? ghost_car_simd()->car_height
																: simd_player.car_height,
											  FRAME_COCKPIT_HEIGHT_CLEARANCE);

		mat_mul_vector(&offset_vector, car_rot_matrix, &car_to_cam_rotated);
		camera->position.x = LEGACY_S16_WRAP_ADD(car_pos->x, car_to_cam_rotated.x);
		camera->position.y = LEGACY_S16_WRAP_ADD(car_pos->y, car_to_cam_rotated.y);
		camera->position.z = LEGACY_S16_WRAP_ADD(car_pos->z, car_to_cam_rotated.z);
	} else if (cameramode == CAMERA_MODE_FOLLOW) {
		camera->position = ghost_camera != 0 ? ghost_camera->follow_position
											 : frame_state->game_follow_camera_position[car_index];
	} else if (cameramode == CAMERA_MODE_CUSTOM) {
		offset_vector.x = 0;
		offset_vector.y = 0;
		offset_vector.z = FRAME_CAMERA_DIRECTION_LENGTH;
		car_rot_matrix = frame_car_rotation(car_rot_x, car_rot_y, car_rot_z);
		mat_mul_vector(&offset_vector, car_rot_matrix, &car_to_cam_rotated);

		offset_vector.x = 0;
		offset_vector.y = 0;
		offset_vector.z = custom_camera.distance;
		car_rot_matrix =
			mat_rot_zxy(0, LEGACY_S16_WRAP_NEGATE(custom_camera.elevation_angle),
						LEGACY_S16_WRAP_SUB(polarAngle(car_to_cam_rotated.x, car_to_cam_rotated.z),
											custom_camera.azimuth_angle),
						MATRIX_ROTATION_ORDER_ZXY);

		mat_mul_vector(&offset_vector, car_rot_matrix, &car_to_cam_rotated);
		camera->position.x = LEGACY_S16_WRAP_ADD(car_pos->x, car_to_cam_rotated.x);
		camera->position.y = LEGACY_S16_WRAP_ADD(car_pos->y, car_to_cam_rotated.y);
		camera->position.z = LEGACY_S16_WRAP_ADD(car_pos->z, car_to_cam_rotated.z);
	} else if (cameramode == CAMERA_MODE_TRACKSIDE) {
		legacy_s16 track_index = ghost_camera != 0
									 ? ghost_camera->trackside_index
									 : frame_state->game_trackside_camera_index[car_index];
		camera->position.x = trackside_camera_positions[track_index].x;
		camera->position.y =
			LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_ADD(trackside_camera_positions[track_index].y,
													camera_track_height_offset),
								FRAME_TRACK_CAMERA_HEIGHT_OFFSET);
		camera->position.z = trackside_camera_positions[track_index].z;
	}

	return camera_roll;
}

static void frame_aim_external_camera(struct FRAME_CAMERA *camera, const struct VECTOR *car_pos)
{
	build_track_object(&camera->position, &camera->position);
	if (camera->position.y < terrainHeight) {
		camera->position.y = terrainHeight;
	}

	if (track_wall_collision_enabled != 0) {
		legacy_s16 plane_distance = plane_signed_distance(planindex, camera->position.x,
														  camera->position.y, camera->position.z);
		if (plane_distance < FRAME_PLANE_CLEARANCE) {
			wheel_forward_travel.x = 0;
			wheel_forward_travel.y = LEGACY_S16_WRAP_SUB(FRAME_PLANE_CLEARANCE, plane_distance);
			wheel_forward_travel.z = 0;
			planindex_copy = planindex;
			wheel_heading_offset = 0;
			car_initial_pitch = 0;
			car_initial_roll = 0;
			car_initial_yaw = 0;
			transform_wheel_travel_to_world();
			camera->position.x = LEGACY_S16_WRAP_ADD(camera->position.x, wheel_world_travel.x);
			camera->position.y = LEGACY_S16_WRAP_ADD(camera->position.y, wheel_world_travel.y);
			camera->position.z = LEGACY_S16_WRAP_ADD(camera->position.z, wheel_world_travel.z);
		}
	}

	camera->yaw = LEGACY_S16_FROM_BITS((legacy_u16)LEGACY_S16_WRAP_NEGATE(polarAngle(
										   LEGACY_S16_WRAP_SUB(car_pos->x, camera->position.x),
										   LEGACY_S16_WRAP_SUB(car_pos->z, camera->position.z))) &
									   ANGLE_MASK);
	legacy_s16 camera_horizontal_distance =
		polarRadius2D(LEGACY_S16_WRAP_SUB(car_pos->x, camera->position.x),
					  LEGACY_S16_WRAP_SUB(car_pos->z, camera->position.z));
	camera->pitch = LEGACY_S16_FROM_BITS(
		(legacy_u16)polarAngle(
			LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_SUB(car_pos->y, camera->position.y),
								FRAME_CAMERA_TARGET_HEIGHT_OFFSET),
			camera_horizontal_distance) &
		ANGLE_MASK);
}

static void frame_setup_camera(struct FRAME_CAMERA *camera)
{
	const struct CARSTATE *viewed_car = frame_viewed_car_state();
	const struct GHOST_CAMERA_STATE *ghost_camera = frame_viewed_ghost_camera();
	struct VECTOR car_pos;
	car_pos.x = position_to_word(viewed_car->car_position.lx);
	car_pos.y = position_to_word(viewed_car->car_position.ly);
	car_pos.z = position_to_word(viewed_car->car_position.lz);
	camera->yaw = -1;
	legacy_s16 camera_roll =
		frame_position_camera(camera, &car_pos, viewed_car->car_rotate.x, viewed_car->car_rotate.y,
							  viewed_car->car_rotate.z, ghost_camera);

	// Keep external cameras above the track and aim them at the followed car.
	if (camera->yaw == -1) {
		frame_aim_external_camera(camera, &car_pos);
	}

	if (camera_roll > 1 && (legacy_u16)camera_roll < ANGLE_MASK) {
		camera->roll = camera_roll;
	} else {
		camera->roll = 0;
	}
}

static legacy_s8 frame_animated_material(void)
{
	legacy_s8 animated_material;

	if (frame_state->game_frame == 0) {
		animated_material =
			track_material_animation[frame_callback_count & FRAME_ANIMATION_PHASE_MASK];
	} else {
		animated_material =
			track_material_animation[frame_state->game_frame & FRAME_ANIMATION_PHASE_MASK];
	}

	return animated_material;
}

static const struct FRAME_LOOKAHEAD_TILE *frame_setup_projection(struct FRAME_CAMERA *camera,
																 struct RECTANGLE *cliprect)
{
	// Select the vector specifying the 23 tiles to draw. The vector contains
	// 24 elements, each 3 bytes long, in format (east_offset, south_offset,
	// detail threshold). A tile is drawn only if its detail threshold is lower
	// enough (0 = draw always, 1 = only if graphic detail is MEDIUM or FULL,
	// 2 = only if graphic detail is FULL).
	// There are 8 possible vectors, but they are all rotations/reflections of a
	// basic schema. Which is chosen depends on the heading of the car. For a
	// car heading north ($), the schema is the following:
	//
	// OOOOO
	// OOOOO
	// OOOOO
	// OOOOO
	//  O$O
	//
	// Also, note that the tiles appear in the vector in drawing order
	// (farthest tiles first). If a car is heading north but slightly west, the
	// algo will draw the NW tile before the NE, and vice-versa

	legacy_s16 heading =
		select_cliprect_rotate(camera->roll, camera->pitch, camera->yaw, cliprect, 0);
	camera->view_heading = heading;
	const struct FRAME_LOOKAHEAD_TILE *lookahead_tiles = (const struct FRAME_LOOKAHEAD_TILE *)
		lookahead_tiles_tables[(heading & ANGLE_MASK) >> FRAME_LOOKAHEAD_HEADING_SHIFT];

	camera->pitch_roll_rotation =
		*mat_rot_zxy(camera->roll, camera->pitch, 0, MATRIX_ROTATION_ORDER_YXZ);
	struct VECTOR offset_vector;
	offset_vector.x = 0;
	offset_vector.y = 0;
	offset_vector.z = FRAME_SKYBOX_TEST_DISTANCE;
	struct VECTOR shape_relative_position;
	mat_mul_vector(&offset_vector, &camera->pitch_roll_rotation, &shape_relative_position);
	if (shape_relative_position.z > 0) {
		camera->skybox_parameter = 1;
	} else {
		camera->skybox_parameter = -1;
	}

	return lookahead_tiles;
}

#if defined(RESTUNTS_SDL3)
static void frame_add_car_shadow(const struct CARSTATE *carstate, const struct SIMD *simd,
								 const struct VECTOR *camera_position, const struct SHAPE3D *shape)
{
	if (extended_track_active()) {
		legacy_s32 reference_x, reference_z;
		extended_geometry_get_context(&reference_x, &reference_z);
		legacy_s32 dx = LEGACY_S32_SAR(carstate->car_position.lx, 6U) - reference_x;
		legacy_s32 dz = LEGACY_S32_SAR(carstate->car_position.lz, 6U) - reference_z;
		if (dx < -28672L || dx > 28672L || dz < -28672L || dz > 28672L) {
			return;
		}
	}

	if (frame_adaptive_car_flags(carstate, simd) == FRAME_ADAPTIVE_HIDE ||
		carstate->car_crashBmpFlag == CRASH_EVENT_WATER ||
		(cameramode == CAMERA_MODE_COCKPIT && carstate == frame_viewed_car_state())) {
		return;
	}
	struct VECTOR relative_position;
	relative_position.x = frame_relative_position(carstate->car_position.lx, camera_position->x);
	relative_position.y = frame_relative_position(carstate->car_position.ly, camera_position->y);
	relative_position.z = frame_relative_position(carstate->car_position.lz, camera_position->z);
	/* The collision box stores half-width and half-length in renderer units. */
	shape3d_hires_shadow_car(&relative_position, LEGACY_S16_WRAP_NEGATE(carstate->car_rotate.x),
							 simd->collide_points[0].px, simd->collide_points[1].px);
	shape3d_hires_shadow_model(shape);
}
#endif

static void frame_draw_clouds(struct FRAME_CAMERA *camera, legacy_s8 redraw_transform_flags)
{
	// Draw the eight cloud shapes at full detail.
	struct VECTOR rotated_position;
	struct MATRIX cloud_heading_rotation;
	struct VECTOR offset_vector;
	if (detail_level == FRAME_DETAIL_FULL) {
		currenttransshape->rectptr = &frame_cloud_rect;
		currenttransshape->ts_flags = redraw_transform_flags | FRAME_DISTANT_SHAPE_FLAGS;
		currenttransshape->rotvec.x = 0;
		currenttransshape->rotvec.y = 0;
		currenttransshape->culling_distance = FRAME_DEFAULT_TRANSFORM_DISTANCE;
		currenttransshape->material = 0;

		for (legacy_s16 cloud_index = 0; cloud_index < FRAME_DISTANT_SHAPE_COUNT; cloud_index++) {
			legacy_s16 cloud_angle = LEGACY_S16_FROM_BITS(
				(legacy_u16)LEGACY_S16_WRAP_ADD(
					LEGACY_S16_WRAP_ADD(cloud_heading_offsets[cloud_index], camera->yaw),
					run_game_random) &
				ANGLE_MASK);
			if (cloud_angle < FRAME_DISTANT_SHAPE_MIN_ANGLE ||
				cloud_angle > FRAME_DISTANT_SHAPE_MAX_ANGLE) {
				mat_rot_y(&cloud_heading_rotation, cloud_angle);
				offset_vector.x = 0;
				offset_vector.y =
					LEGACY_S16_WRAP_SUB(FRAME_DISTANT_SHAPE_HEIGHT, camera->position.y);
				offset_vector.z = FRAME_DISTANT_SHAPE_DISTANCE;
				mat_mul_vector(&offset_vector, &cloud_heading_rotation, &rotated_position);
				rotated_position.z = FRAME_DISTANT_SHAPE_DISTANCE;
				mat_mul_vector(&rotated_position, &camera->pitch_roll_rotation,
							   &currenttransshape->pos);
				if (currenttransshape->pos.z > FRAME_DISTANT_SHAPE_MIN_DEPTH) {
					currenttransshape->shapeptr = cloud_shapes[cloud_index];
					currenttransshape->rotvec.z = LEGACY_S16_WRAP_NEGATE(camera->yaw);
					legacy_s16 transform_result =
						shape3d_transform_and_queue(&currenttransshape[0]);
					// we cannot be out of memory as we are just starting to process
					(void)transform_result;
				}
			}
		}
	}
}

static void frame_resolve_track_tile(struct FRAME_TILE *tile)
{
	if (tile->element != 0) {
		if (tile->terrain >= FRAME_HILL_ROAD_TERRAIN_FIRST &&
			tile->terrain < FRAME_HILL_ROAD_TERRAIN_END) {
			tile->element = subst_hillroad_track(tile->terrain, tile->element);
			tile->terrain = 0;
		}

		// Found a filler tile (non-main tile of a multitile component)
		// Process the main tile of the component instead (the NW one)
		if (tile->element == TRACK_TILE_CONTINUATION_SOUTHEAST) {
			tile->east = LEGACY_S8_WRAP_SUB(tile->east, 1);
			tile->south = LEGACY_S8_WRAP_SUB(tile->south, 1);
			tile->element = track_element_map[tile->east + trackrows[tile->south]];
			tile->terrain = track_terrain_map[tile->east + terrainrows[tile->south]];
		} else if (tile->element == TRACK_TILE_CONTINUATION_SOUTH) {
			tile->south = LEGACY_S8_WRAP_SUB(tile->south, 1);
			tile->element = track_element_map[tile->east + trackrows[tile->south]];
			tile->terrain = track_terrain_map[tile->east + terrainrows[tile->south]];
		} else if (tile->element == TRACK_TILE_CONTINUATION_EAST) {
			tile->east = LEGACY_S8_WRAP_SUB(tile->east, 1);
			tile->element = track_element_map[tile->east + trackrows[tile->south]];
			tile->terrain = track_terrain_map[tile->east + terrainrows[tile->south]];
		}
	}
}

static legacy_s16 frame_lookahead_is_covered(const struct FRAME_LOOKAHEAD_TILE *lookahead,
											 legacy_s16 multitile_flag, legacy_s8 east_offset,
											 legacy_s8 south_offset)
{
	if (multitile_flag == FRAME_MULTITILE_ROW) {
		return lookahead->east == east_offset &&
			   (lookahead->south == south_offset || lookahead->south == south_offset + 1);
	}
	if (multitile_flag == FRAME_MULTITILE_COLUMN) {
		return lookahead->south == south_offset &&
			   (lookahead->east == east_offset || lookahead->east == east_offset + 1);
	}
	if (multitile_flag == FRAME_MULTITILE_BOTH) {
		return (lookahead->east == east_offset || lookahead->east == east_offset + 1) &&
			   (lookahead->south == south_offset || lookahead->south == south_offset + 1);
	}
	return 0;
}

static void frame_mark_covered_tiles(struct FRAME_TILE_SELECTION *tiles,
									 const struct FRAME_TILE *tile, legacy_s16 tile_index)
{
#if defined(RESTUNTS_SDL3)
	if (extended_track_active()) {
		return;
	}
#endif
	if (tile->element != 0) {
		legacy_s16 multitile_flag = trkObjectList[tile->element].ss_multiTileFlag;
		if (multitile_flag != FRAME_MULTITILE_NONE) {
#if defined(RESTUNTS_SDL3)
			if (frame_has_complete_tile_lookup(tiles)) {
				if (multitile_flag < FRAME_MULTITILE_ROW || multitile_flag > FRAME_MULTITILE_BOTH) {
					return;
				}
				legacy_s16 last_east = tile->east + (multitile_flag != FRAME_MULTITILE_ROW);
				legacy_s16 last_south = tile->south + (multitile_flag != FRAME_MULTITILE_COLUMN);
				for (legacy_s16 south = tile->south; south <= last_south; south++) {
					for (legacy_s16 east = tile->east; east <= last_east; east++) {
						legacy_s16 covered_index = frame_lookup_world_tile(tiles, east, south);
						if (covered_index >= 0 && covered_index < tile_index) {
							tiles->markers[covered_index] = FRAME_TILE_MULTITILE_COVERED_MARKER;
						}
					}
				}
				return;
			}
#endif
			/* Recalculate after resolving filler tiles. Lower indices are visited later. */
			legacy_s8 east_offset = LEGACY_S8_WRAP_SUB(tile->east, tiles->camera_east);
			legacy_s8 south_offset = LEGACY_S8_WRAP_SUB(tile->south, tiles->camera_south);
			for (legacy_s16 covered_index = 0; covered_index < tile_index; covered_index++) {
				if (frame_lookahead_is_covered(&tiles->lookahead[covered_index], multitile_flag,
											   east_offset, south_offset)) {
					tiles->markers[covered_index] = FRAME_TILE_MULTITILE_COVERED_MARKER;
				}
			}
		}
	}
}

static void frame_select_track_tile(struct FRAME_TILE_SELECTION *tiles, struct FRAME_TILE *tile,
									legacy_s16 tile_index)
{
	tile->element = track_element_map[tile->east + trackrows[tile->south]];
	tile->terrain = track_terrain_map[tile->east + terrainrows[tile->south]];

#if defined(RESTUNTS_SDL3)
	if (!extended_track_active())
#endif
		frame_resolve_track_tile(tile);
	tiles->terrain[tile_index] = tile->terrain;
	tiles->detail[tile_index] = tiles->lookahead[tile_index].detail;

	if (tile->element != 0 && detail_level != FRAME_DETAIL_FULL &&
		trkObjectList[tile->element].ss_physicalModel >= FRAME_SCENERY_PHYSICAL_MODEL_FIRST &&
		(tile->east != tiles->player_east || tile->south != tiles->player_south)) {
		tile->element = 0;
	}

	tiles->east[tile_index] = tile->east;
	tiles->south[tile_index] = tile->south;
#if defined(RESTUNTS_SDL3)
	if (extended_track_active() && tile->element >= 253) {
		tile->element = 0;
	}
#endif
	tiles->elements[tile_index] = tile->element;

	frame_mark_covered_tiles(tiles, tile, tile_index);
}

#if defined(RESTUNTS_SDL3)
static void frame_extend_lookahead(struct FRAME_TILE_SELECTION *tiles,
								   const struct FRAME_CAMERA *camera)
{
	(void)camera;
	/* HyperVision uses screen depth, so enumerate each world tile once in
	 * stable grid order. Continuations still resolve to their complete object;
	 * the lookup identifies the same submission tile for its cars and wheels. */
	legacy_s16 index = 0;
	memset(tiles->index_by_world_tile, -1, sizeof(tiles->index_by_world_tile));
	for (legacy_s16 south = 0; south <= TRACK_GRID_LAST_COORDINATE; south++) {
		for (legacy_s16 east = 0; east <= FRAME_TRACK_LAST_X; east++) {
			if (extended_track_active() &&
				(abs(east - tiles->camera_east) > 28 || abs(south - tiles->camera_south) > 28)) {
				continue;
			}
			if (frame_adaptive_active != 0 &&
				frame_adaptive_flags(&frame_adaptive, east, south) == FRAME_ADAPTIVE_HIDE) {
				continue;
			}
			struct FRAME_LOOKAHEAD_TILE *tile = &tiles->extended_lookahead[index++];
			tile->east = east - tiles->camera_east;
			tile->south = south - tiles->camera_south;
			tile->detail = FRAME_TILE_DETAIL_FULL;
		}
	}
	tiles->count = index;
	tiles->complete_tile_lookup = 1;
	for (index = 0; index < tiles->count; index++) {
		legacy_s16 east = tiles->extended_lookahead[index].east + tiles->camera_east;
		legacy_s16 south = tiles->extended_lookahead[index].south + tiles->camera_south;
		/* Wrapped signed-byte offsets need the original comparison semantics. */
		if (east < 0 || east > FRAME_TRACK_LAST_X || south < 0 ||
			south > TRACK_GRID_LAST_COORDINATE) {
			tiles->complete_tile_lookup = 0;
		} else {
			tiles->index_by_world_tile[south * extended_track_width() + east] = index;
		}
	}
	tiles->lookahead = tiles->extended_lookahead;
}
#else
static void frame_extend_lookahead(struct FRAME_TILE_SELECTION *tiles,
								   const struct FRAME_CAMERA *camera)
{
	(void)camera;
	legacy_s8 east = tiles->lookahead[0].east;
	legacy_s8 south = tiles->lookahead[0].south;
	legacy_s8 depth_east = east == FRAME_LOOKAHEAD_DEPTH_TILES	  ? 1
						   : east == -FRAME_LOOKAHEAD_DEPTH_TILES ? -1
																  : 0;
	legacy_s8 depth_south = south == FRAME_LOOKAHEAD_DEPTH_TILES	? 1
							: south == -FRAME_LOOKAHEAD_DEPTH_TILES ? -1
																	: 0;
	legacy_s8 width_east = east == FRAME_LOOKAHEAD_WIDTH_TILES	  ? 1
						   : east == -FRAME_LOOKAHEAD_WIDTH_TILES ? -1
																  : 0;
	legacy_s8 width_south = south == FRAME_LOOKAHEAD_WIDTH_TILES	? 1
							: south == -FRAME_LOOKAHEAD_WIDTH_TILES ? -1
																	: 0;
	for (legacy_s16 index = 0; index < FRAME_SUPERSIGHT_TILE_COUNT; index++) {
		const struct FRAME_SUPERSIGHT_TILE *source = &supersight_tiles[index];
		struct FRAME_LOOKAHEAD_TILE *target = &tiles->extended_lookahead[index];
		target->east = source->depth * depth_east + source->width * width_east;
		target->south = source->depth * depth_south + source->width * width_south;
		target->detail = source->priority;
	}
	tiles->lookahead = tiles->extended_lookahead;
	tiles->count = FRAME_SUPERSIGHT_TILE_COUNT;
}

#endif

static void frame_select_tiles(struct FRAME_TILE_SELECTION *tiles,
							   const struct FRAME_CAMERA *camera)
{
#if defined(RESTUNTS_SDL3)
	tiles->complete_tile_lookup = 0;
#endif
	tiles->count = FRAME_LOOKAHEAD_TILE_COUNT;
	tiles->first = 0;
	tiles->camera_east =
		LEGACY_S8_FROM_BITS((legacy_u8)LEGACY_S16_SAR(camera->position.x, FRAME_CAMERA_TILE_SHIFT));
	tiles->camera_south = LEGACY_S8_WRAP_SUB(
		TRACK_GRID_LAST_COORDINATE, LEGACY_S16_SAR(camera->position.z, FRAME_CAMERA_TILE_SHIFT));
#if defined(RESTUNTS_SDL3)
	if (frame_adaptive_active != 0) {
		frame_adaptive_prepare(&frame_adaptive, tiles->camera_east, tiles->camera_south,
							   cos_fast(camera->view_heading), sin_fast(camera->view_heading));
	}
#endif
	if (supersight_enabled != 0) {
		frame_extend_lookahead(tiles, camera);
	}
#if defined(RESTUNTS_SDL3)
	if (extended_track_active()) {
		legacy_s32 x, z;
		extended_geometry_world_position(&camera->position, &x, &z);
		tiles->camera_east = (legacy_s8)LEGACY_S32_SAR(x, FRAME_CAMERA_TILE_SHIFT);
		tiles->camera_south =
			(legacy_s8)(extended_track_height() - 1 - LEGACY_S32_SAR(z, FRAME_CAMERA_TILE_SHIFT));
		/* The classic painter must retain the original far-to-near tile
		 * order. Row-major enumeration is only safe with HyperVision depth. */
		if (supersight_enabled != 0) {
			frame_extend_lookahead(tiles, camera);
		}
	}
#endif
	if (detail_level != FRAME_DETAIL_FULL) {
		tiles->player_east = frame_tile_from_world(frame_state->playerstate.car_position.lx);
		tiles->player_south = frame_south_tile_from_world(frame_state->playerstate.car_position.lz);
	}

	for (legacy_s16 tile_index = 0; tile_index < tiles->count; tile_index++) {
		tiles->markers[tile_index] = FRAME_TILE_DRAW_MARKER;
	}

	// Select the detail level (FULL if 1st or 2nd option in the graphics menu
	// were chosen, MEDIUM if the 3rd, FASTEST if 4th or 5th)
	legacy_s8 detail_threshold = detail_threshold_by_level[detail_level];

	/* Resolve each object's last submission tile first, suppressing earlier
	 * copies. The classic lookahead also retains its nearest-to-farthest order. */
	struct FRAME_TILE tile;
	for (legacy_s16 tile_index = tiles->count - 1; tile_index >= 0; tile_index--) {
		// Skip if a previous iteration determined this tile is not needed
		// (happens for multi-tile elements)
		if (tiles->markers[tile_index] != FRAME_TILE_DRAW_MARKER) {
			continue;
		}

		// Skip if detail threshold not met (e.g. far tiles in FASTEST detail)
		if (supersight_enabled != 0 || tiles->lookahead[tile_index].detail <= detail_threshold) {
			tile.east = LEGACY_S8_WRAP_ADD(tiles->lookahead[tile_index].east, tiles->camera_east);
			tile.south =
				LEGACY_S8_WRAP_ADD(tiles->lookahead[tile_index].south, tiles->camera_south);

			// Skip if tile is out of bounds
			if (tile.east >= 0 && tile.east <= FRAME_TRACK_LAST_X && tile.south >= 0 &&
				tile.south <= TRACK_GRID_LAST_COORDINATE) {
				frame_select_track_tile(tiles, &tile, tile_index);

			} else {
				tiles->markers[tile_index] = FRAME_TILE_UNAVAILABLE_MARKER;
			}
		} else {
			tiles->markers[tile_index] = FRAME_TILE_UNAVAILABLE_MARKER;
		}
	}
}

static void frame_place_cars(const struct FRAME_TILE_SELECTION *tiles,
							 struct FRAME_CAR_RENDER *cars)
{
	// Locate the player in the visible tile list using its wheels.
	cars[PLAYER_CAR_INDEX].east = -1;
	cars[PLAYER_CAR_INDEX].depth_adjustment = 0;
	if (cameramode != CAMERA_MODE_COCKPIT || followOpponentFlag != 0) {
		if (frame_state->playerstate.car_crashBmpFlag != CRASH_EVENT_WATER) {
			cars[PLAYER_CAR_INDEX].depth_adjustment =
				frame_find_car_wheel(&frame_state->playerstate, &simd_player, tiles,
									 &cars[PLAYER_CAR_INDEX].east, &cars[PLAYER_CAR_INDEX].south);
		}
	}

	// Locate the opponent in the same draw order.
	cars[OPPONENT_CAR_INDEX].east = -1;
	cars[OPPONENT_CAR_INDEX].depth_adjustment = 0;
	const struct CARSTATE *second_car = frame_second_car_state();
	if (second_car != 0) {
		if (cameramode != CAMERA_MODE_COCKPIT || followOpponentFlag == 0) {
			if (second_car->car_crashBmpFlag != CRASH_EVENT_WATER) {
				const struct SIMD *second_simd =
					gameconfig.game_opponenttype != 0 ? &simd_opponent : ghost_car_simd();
				cars[OPPONENT_CAR_INDEX].depth_adjustment = frame_find_car_wheel(
					second_car, second_simd, tiles, &cars[OPPONENT_CAR_INDEX].east,
					&cars[OPPONENT_CAR_INDEX].south);
			}
		}
	}
}

/* Report exhaustion to SuperSight; the legacy caller stops only this pass. */
static legacy_s16 frame_draw_fences(const struct FRAME_TILE *tile,
									const struct FRAME_TILE_SELECTION *tiles,
									const struct FRAME_CAMERA *camera,
									legacy_s8 redraw_transform_flags)
{
	legacy_s8 *fence_tile_offsets;
	legacy_s16 fence_position_count;
	struct TRACKOBJECT *track_object;
	if (tile->element == 0) {
		fence_position_count = 1;
		fence_tile_offsets = fence_tile_offsets_column;
	} else {
		track_object = &trkObjectList[tile->element];
		if (track_object->ss_multiTileFlag == FRAME_MULTITILE_NONE) {
			fence_position_count = FRAME_FENCE_POSITION_COUNT_SINGLE;
			fence_tile_offsets = fence_tile_offsets_single;
		} else if (track_object->ss_multiTileFlag == FRAME_MULTITILE_ROW) {
			fence_position_count = FRAME_FENCE_POSITION_COUNT_ROW;
			fence_tile_offsets = fence_tile_offsets_row;
		} else if (track_object->ss_multiTileFlag == FRAME_MULTITILE_COLUMN) {
			fence_position_count = FRAME_FENCE_POSITION_COUNT_COLUMN;
			fence_tile_offsets = fence_tile_offsets_column;
		} else if (track_object->ss_multiTileFlag == FRAME_MULTITILE_BOTH) {
			fence_position_count = FRAME_FENCE_POSITION_COUNT_BOTH;
			fence_tile_offsets = fence_tile_offsets_both;
		} else {
			return 0;
		}
	}

	// Draw the fence
	struct TRACKOBJECT *fence_object;
	for (legacy_s16 position_index = 0; position_index < fence_position_count; position_index++) {
		legacy_s8 tile_to_draw_east_offset = LEGACY_S8_WRAP_ADD(
			fence_tile_offsets[position_index * FRAME_FENCE_POSITION_STRIDE], tile->east);
		legacy_s8 tile_to_draw_south_offset =
			LEGACY_S8_WRAP_ADD(fence_tile_offsets[position_index * FRAME_FENCE_POSITION_STRIDE +
												  FRAME_FENCE_SECOND_COORDINATE],
							   tile->south);

		if (detail_level == FRAME_DETAIL_FULL ||
			(tile_to_draw_east_offset == tiles->player_east &&
			 tile_to_draw_south_offset == tiles->player_south)) {
			legacy_s16 fence_index = fence_by_edge[frame_border_index(tile_to_draw_east_offset)]
												  [frame_border_index(tile_to_draw_south_offset)];

			if (fence_index != FRAME_FENCE_NONE) {
#if defined(RESTUNTS_SDL3)
				if (frame_adaptive_active != 0 &&
					frame_adaptive_flags(&frame_adaptive, tile_to_draw_east_offset,
										 tile_to_draw_south_offset) == FRAME_ADAPTIVE_HIDE) {
					continue;
				}
#endif
				fence_object = frame_track_object_from_legacy_index(fence_TrkObjCodes[fence_index]);
#if defined(RESTUNTS_SDL3)
				currenttransshape->shapeptr = frame_track_shape(fence_object, tile->detail);
#else
				if (tile->detail == FRAME_TILE_DETAIL_FULL) {
					currenttransshape->shapeptr = fence_object->ss_shapePtr;
				} else {
					currenttransshape->shapeptr = fence_object->ss_loShapePtr;
				}
#endif

				frame_prepare_flat_track_shape(
					currenttransshape, tile_to_draw_east_offset, tile_to_draw_south_offset,
					&camera->position,
					(legacy_s16)(redraw_transform_flags | FRAME_TRANSFORM_FLAGS_NO_DEPTH_SORT),
					fence_rotations[fence_index]);
				legacy_s16 transform_result = shape3d_transform_and_queue(&currenttransshape[0]);
				if (transform_result > 0) {
					// if the return value is > 0, we are out of memory
					// for the polygons, so the rendering is interrupted.
					// Note that (since we start from afar) this means that
					// if the scene is too complex only the far objects
					// will be drawn, while our car and its immediate
					// surroundings will be invisible. Luckily, it does not
					// happen often
					return 1;
				}
			}
		}
	}
	return 0;
}

/* The legacy caller ignores corner exhaustion and continues ordinary terrain. */
static legacy_s16 frame_draw_elevated_corners(struct FRAME_TILE *tile,
											  const struct FRAME_CAMERA *camera,
											  legacy_s8 redraw_transform_flags)
{
	struct TRACKOBJECT *track_object;

	for (legacy_s16 corner_index = 0; corner_index < FRAME_ELEVATED_CORNER_COUNT; corner_index++) {
		if (corner_index == FRAME_CORNER_NORTHWEST) {
			tile->last_east = tile->east;
			tile->last_south = tile->south;
		} else if (corner_index == FRAME_CORNER_NORTHEAST) {
			tile->last_east = LEGACY_S8_WRAP_ADD(tile->east, 1);
			tile->last_south = tile->south;
		} else if (corner_index == FRAME_CORNER_SOUTHWEST) {
			tile->last_east = tile->east;
			tile->last_south = LEGACY_S8_WRAP_ADD(tile->south, 1);
		} else if (corner_index == FRAME_CORNER_SOUTHEAST) {
			tile->last_east = LEGACY_S8_WRAP_ADD(tile->east, 1);
			tile->last_south = LEGACY_S8_WRAP_ADD(tile->south, 1);
		}
		tile->terrain = track_terrain_map[tile->last_east + terrainrows[tile->last_south]];
#if defined(RESTUNTS_SDL3)
		if (frame_adaptive_active != 0 &&
			frame_adaptive_flags(&frame_adaptive, tile->last_east, tile->last_south) ==
				FRAME_ADAPTIVE_HIDE) {
			continue;
		}
#endif
		if (tile->terrain != 0) {
			track_object = &terrain_scene_objects[tile->terrain];
#if defined(RESTUNTS_SDL3)
			currenttransshape->shapeptr = supersight_enabled != 0
											  ? frame_track_shape(track_object, tile->detail)
											  : track_object->ss_shapePtr;
#else
			currenttransshape->shapeptr = track_object->ss_shapePtr;
#endif
			frame_prepare_flat_track_shape(
				currenttransshape, tile->last_east, tile->last_south, &camera->position,
				(legacy_s16)(redraw_transform_flags | FRAME_TRANSFORM_FLAGS_NO_DEPTH_SORT),
				track_object->ss_rotY);
			legacy_s16 transform_result = shape3d_transform_and_queue(&currenttransshape[0]);
			if (transform_result > 0) {
				return 1;
			}
		}
	}
	return 0;
}

/* Multi-tile selection suppresses continuation cells, but physics still samples
 * their own terrain. Restore water beneath the complete visible track object. */
static legacy_s16 frame_draw_covered_water(const struct FRAME_TILE *tile,
										   const struct FRAME_CAMERA *camera,
										   legacy_s8 redraw_transform_flags)
{
	if (tile->element == 0 ||
		(tile->element >= FRAME_ELEVATED_CORNER_FIRST &&
		 tile->element <= FRAME_ELEVATED_CORNER_LAST && tile->terrain != TERRAIN_RAISED_TILE)) {
		/* Elevated corners already draw every cell's terrain. */
		return 0;
	}
	legacy_s16 footprint = trkObjectList[tile->element].ss_multiTileFlag;
	if (footprint < FRAME_MULTITILE_ROW || footprint > FRAME_MULTITILE_BOTH) {
		return 0;
	}
	legacy_s16 last_east = tile->east + (footprint != FRAME_MULTITILE_ROW);
	legacy_s16 last_south = tile->south + (footprint != FRAME_MULTITILE_COLUMN);
	for (legacy_s16 south = tile->south; south <= last_south && south < TRACK_GRID_SIZE; south++) {
		for (legacy_s16 east = tile->east; east <= last_east && east < TRACK_GRID_SIZE; east++) {
			if (east == tile->east && south == tile->south) {
				continue;
			}
			legacy_u8 terrain = track_terrain_map[east + terrainrows[south]];
			if (terrain < FRAME_WATER_TERRAIN_FIRST || terrain > FRAME_WATER_TERRAIN_LAST) {
				continue;
			}
			struct TRACKOBJECT *object = &terrain_scene_objects[terrain];
#if defined(RESTUNTS_SDL3)
			currenttransshape->shapeptr = frame_track_shape(object, tile->detail);
#else
			currenttransshape->shapeptr = object->ss_shapePtr;
#endif
			frame_prepare_flat_track_shape(
				currenttransshape, east, south, &camera->position,
				(legacy_s16)(redraw_transform_flags | FRAME_TRANSFORM_FLAGS_NO_DEPTH_SORT),
				object->ss_rotY);
			legacy_s16 transform_result = shape3d_transform_and_queue(currenttransshape);
			if (transform_result > 0) {
				return 1;
			}
		}
	}
	return 0;
}

#ifdef RESTUNTS_SDL3
enum FRAME_EXTENDED_TERRAIN_MATERIAL {
	FRAME_EXTENDED_GROUND_GRASS_MATERIAL = 16,
	FRAME_EXTENDED_ICE_MATERIAL = 28,
	FRAME_EXTENDED_DIRT_MATERIAL = 25,
	FRAME_EXTENDED_WATER_MATERIAL = 100,
	FRAME_EXTENDED_HILL_GRASS_MATERIAL = 101,
	FRAME_EXTENDED_HILL_GRASS_MATERIAL_LAST = 105
};

#define FRAME_EXTENDED_TERRAIN_HALF_TILE (CSTRK_TILE_WORLD_SIZE / 2)
#define FRAME_EXTENDED_TERRAIN_ALWAYS_VISIBLE 1U
#define FRAME_EXTENDED_OUTER_CORNER_FIRST 11U
#define FRAME_EXTENDED_OUTER_CORNER_LAST 14U

static void frame_extended_rotate_vertex(struct VECTOR *point, legacy_s16 yaw)
{
	legacy_s16 x = point->x, z = point->z;
	switch (yaw & ANGLE_MASK) {
		case ANGLE_QUARTER_TURN:
			point->x = z;
			point->z = LEGACY_S16_WRAP_NEGATE(x);
			break;
		case ANGLE_HALF_TURN:
			point->x = LEGACY_S16_WRAP_NEGATE(x);
			point->z = LEGACY_S16_WRAP_NEGATE(z);
			break;
		case ANGLE_THREE_QUARTER_TURN:
			point->x = LEGACY_S16_WRAP_NEGATE(z);
			point->z = x;
			break;
	}
}

static legacy_s32 frame_extended_shore_distance(const struct VECTOR *point, legacy_u8 corner)
{
	switch (corner) {
		case CSTRK_WATER_NORTHWEST:
			return (legacy_s32)point->z - point->x;
		case CSTRK_WATER_NORTHEAST:
			return (legacy_s32)point->x + point->z;
		case CSTRK_WATER_SOUTHEAST:
			return (legacy_s32)point->x - point->z;
		default:
			return -(legacy_s32)point->x - point->z;
	}
}

static void frame_extended_append_terrain_vertex(struct VECTOR *output, legacy_u16 *count,
												 const struct VECTOR *point)
{
	if (*count == 0 || output[*count - 1U].x != point->x || output[*count - 1U].y != point->y ||
		output[*count - 1U].z != point->z) {
		output[(*count)++] = *point;
	}
}

/* Clip paint boundaries after choosing the native hill mesh. The same vertices
 * and collision surface are retained regardless of the selected Shore corner. */
static legacy_u16 frame_extended_clip_terrain(const struct VECTOR *source, legacy_u16 count,
											  struct VECTOR *output, legacy_u8 corner,
											  legacy_u8 secondary)
{
	legacy_u16 written = 0;
	struct VECTOR previous = source[count - 1U];
	legacy_s32 previous_distance = frame_extended_shore_distance(&previous, corner);
	if (!secondary) {
		previous_distance = -previous_distance;
	}
	for (legacy_u16 index = 0; index < count; index++) {
		struct VECTOR current = source[index];
		legacy_s32 distance = frame_extended_shore_distance(&current, corner);
		if (!secondary) {
			distance = -distance;
		}
		if ((distance >= 0) != (previous_distance >= 0)) {
			legacy_s32 divisor = previous_distance - distance;
			/* A symmetric rational expression rounds both painted halves to
			 * exactly the same word-sized point, even on a half-unit crossing. */
			struct VECTOR crossing = {(legacy_s16)(((legacy_s32)current.x * previous_distance -
													(legacy_s32)previous.x * distance) /
												   divisor),
									  (legacy_s16)(((legacy_s32)current.y * previous_distance -
													(legacy_s32)previous.y * distance) /
												   divisor),
									  (legacy_s16)(((legacy_s32)current.z * previous_distance -
													(legacy_s32)previous.z * distance) /
												   divisor)};
			frame_extended_append_terrain_vertex(output, &written, &crossing);
		}
		if (distance >= 0) {
			frame_extended_append_terrain_vertex(output, &written, &current);
		}
		previous = current;
		previous_distance = distance;
	}
	if (written > 1U && output[0].x == output[written - 1U].x &&
		output[0].y == output[written - 1U].y && output[0].z == output[written - 1U].z) {
		written--;
	}
	return written;
}

static legacy_s16 frame_extended_queue_terrain_polygon(const struct VECTOR *points,
													   legacy_u16 count, legacy_u8 primitive_flags,
													   legacy_u8 material,
													   const struct VECTOR *position,
													   legacy_s8 flags)
{
	if (count < SHAPE3D_PRIMITIVE_POLYGON_FIRST) {
		return 0;
	}
	if (count > SHAPE3D_POLYGON_MAX_VERTICES) {
		for (legacy_u16 index = 1; index + 1U < count; index++) {
			struct VECTOR triangle[] = {points[0], points[index], points[index + 1U]};
			if (frame_extended_queue_terrain_polygon(triangle, SHAPE3D_PRIMITIVE_POLYGON_FIRST,
													 primitive_flags, material, position, flags)) {
				return 1;
			}
		}
		return 0;
	}
	legacy_u8 bytes[SHAPE3D_POLYGON_MAX_VERTICES * SHAPE3D_VERTEX_SIZE];
	legacy_u8 primitive[SHAPE3D_POLYGON_MAX_VERTICES + 5U] = {0};
	legacy_u8 masks[SHAPE3D_VISIBILITY_MASK_SIZE] = {255, 255, 255, 255};
	legacy_u8 front_masks[SHAPE3D_VISIBILITY_MASK_SIZE] = {0};
	struct SHAPE3D shape = {count, bytes, 1, 1, primitive, masks, front_masks};
	primitive[0] = (legacy_u8)count;
	primitive[1] = primitive_flags;
	primitive[2] = material;
	for (legacy_u16 index = 0; index < count; index++) {
		shape3d_vertex_write(&shape, index, &points[index]);
		primitive[index + 3U] = (legacy_u8)index;
	}
	primitive[count + 3U] = 0;
	struct TRANSFORMEDSHAPE3D instance = {0};
	instance.pos = *position;
	instance.shapeptr = &shape;
	instance.rectptr = &frame_sorted_shapes_rect;
	instance.culling_distance = FRAME_DEFAULT_TRANSFORM_DISTANCE;
	instance.ts_flags = flags | FRAME_TRANSFORM_FLAGS_DEFAULT;
	return (legacy_s16)shape3d_transform_and_queue(&instance) > 0 && supersight_enabled != 0;
}

static legacy_s16 frame_extended_paint_terrain_polygon(const struct VECTOR *points,
													   legacy_u16 count, legacy_u8 primitive_flags,
													   legacy_u8 grass_material,
													   const struct CSTRK_CELL *cell,
													   const struct VECTOR *position,
													   legacy_s8 flags)
{
	static const legacy_u8 materials[] = {FRAME_EXTENDED_GROUND_GRASS_MATERIAL,
										  FRAME_EXTENDED_ICE_MATERIAL, FRAME_EXTENDED_DIRT_MATERIAL,
										  FRAME_EXTENDED_WATER_MATERIAL};
	for (legacy_u8 half = 0; half <= (cell->half_water_corner != CSTRK_WATER_NONE); half++) {
		struct VECTOR clipped[SHAPE3D_POLYGON_MAX_VERTICES + 1U];
		const struct VECTOR *polygon = points;
		legacy_u16 polygon_count = count;
		legacy_u8 surface = half ? cell->corner_surface : cell->surface;
		legacy_u8 material = surface == CSTRK_TERRAIN_GRASS ? grass_material : materials[surface];
		if (cell->half_water_corner != CSTRK_WATER_NONE) {
			polygon_count =
				frame_extended_clip_terrain(points, count, clipped, cell->half_water_corner, half);
			polygon = clipped;
		}
		if (frame_extended_queue_terrain_polygon(polygon, polygon_count, primitive_flags, material,
												 position, flags)) {
			return 1;
		}
	}
	return 0;
}

/* Original hill meshes retain their full tile span, corner diagonals, and
 * authored shading. Higher levels translate those meshes without tilting them. */
static legacy_s16 frame_draw_extended_terrain(const struct FRAME_TILE *tile,
											  const struct FRAME_CAMERA *camera, legacy_s8 flags)
{
	const struct CSTRK_TRACK *track = extended_track_model();
	struct CSTRK_TERRAIN_PROFILE profile;
	if (!cstrk_terrain_profile(track, (legacy_u16)tile->east, (legacy_u16)tile->south, &profile)) {
		return 0;
	}
	const struct CSTRK_CELL *cell = &track->cells[(size_t)tile->south * track->width + tile->east];
	struct VECTOR position = {
		LEGACY_S16_WRAP_SUB(track_column_centers[tile->east], camera->position.x),
		LEGACY_S16_WRAP_SUB((legacy_s16)(profile.base_height * CSTRK_LEVEL_WORLD_HEIGHT),
							camera->position.y),
		LEGACY_S16_WRAP_SUB(track_row_centers[tile->south], camera->position.z)};
	const struct CSTRK_OBJECT *object = extended_track_object_at(tile->east, tile->south);
	if (object && (object->slope_x || object->slope_y) && cell->surface == CSTRK_TERRAIN_GRASS &&
		cell->half_water_corner == CSTRK_WATER_NONE) {
		/* Native hill roads include their grass sides; native spans include
		 * the original hill overlay. Submit these complete resources once. */
		return 0;
	}
	if ((profile.terrain_id == 0 || (profile.terrain_id >= FRAME_EXTENDED_OUTER_CORNER_FIRST &&
									 profile.terrain_id <= FRAME_EXTENDED_OUTER_CORNER_LAST)) &&
		!(profile.base_height == 0 && cell->surface == CSTRK_TERRAIN_GRASS &&
		  cell->half_water_corner == CSTRK_WATER_NONE)) {
		/* Only the low half of an outer corner needs a supporting fill.
		 * A full quad beneath a slope can overpaint it in the classic painter. */
		const struct VECTOR flat[] = {
			{-FRAME_EXTENDED_TERRAIN_HALF_TILE, 0, FRAME_EXTENDED_TERRAIN_HALF_TILE},
			{FRAME_EXTENDED_TERRAIN_HALF_TILE, 0, FRAME_EXTENDED_TERRAIN_HALF_TILE},
			{FRAME_EXTENDED_TERRAIN_HALF_TILE, 0, -FRAME_EXTENDED_TERRAIN_HALF_TILE},
			{-FRAME_EXTENDED_TERRAIN_HALF_TILE, 0, -FRAME_EXTENDED_TERRAIN_HALF_TILE}};
		struct VECTOR low_half[3];
		const struct VECTOR *points = flat;
		legacy_u16 count = sizeof(flat) / sizeof(flat[0]);
		if (profile.terrain_id != 0) {
			legacy_u8 high_corner = 0;
			while (!(profile.high_mask & (1U << high_corner))) {
				high_corner++;
			}
			count = sizeof(low_half) / sizeof(low_half[0]);
			for (legacy_u16 vertex = 0; vertex < count; vertex++) {
				low_half[vertex] = flat[(high_corner + vertex + 1U) % 4U];
			}
			points = low_half;
		}
		legacy_u8 material = profile.base_height == 0 ? FRAME_EXTENDED_GROUND_GRASS_MATERIAL
													  : FRAME_EXTENDED_HILL_GRASS_MATERIAL;
		if (frame_extended_paint_terrain_polygon(points, count,
												 FRAME_EXTENDED_TERRAIN_ALWAYS_VISIBLE, material,
												 cell, &position, flags)) {
			return 1;
		}
	}
	if (profile.terrain_id == 0) {
		return 0;
	}
	const struct TRACKOBJECT *definition = &terrain_scene_objects[profile.terrain_id];
	const struct SHAPE3D *source =
		supersight_enabled != 0 ? frame_track_shape((struct TRACKOBJECT *)definition, tile->detail)
								: definition->ss_shapePtr;
	if (cell->surface == CSTRK_TERRAIN_GRASS && cell->half_water_corner == CSTRK_WATER_NONE) {
		struct TRANSFORMEDSHAPE3D instance = {0};
		instance.pos = position;
		instance.shapeptr = (struct SHAPE3D *)source;
		instance.rectptr =
			profile.base_height == 0 ? &frame_unsorted_shapes_rect : &frame_sorted_shapes_rect;
		instance.rotvec.z = definition->ss_rotY;
		instance.culling_distance = FRAME_DEFAULT_TRANSFORM_DISTANCE;
		instance.ts_flags = flags | FRAME_TRANSFORM_FLAGS_NO_DEPTH_SORT;
		return (legacy_s16)shape3d_transform_and_queue(&instance) > 0 && supersight_enabled != 0;
	}
	const legacy_u8 *primitive = source->shape3d_primitives;
	for (legacy_u16 index = 0; index < source->shape3d_numprimitives; index++) {
		legacy_u16 count = primitive[0];
		if (count < SHAPE3D_PRIMITIVE_POLYGON_FIRST || count > SHAPE3D_PRIMITIVE_POLYGON_LAST) {
			break;
		}
		struct VECTOR points[SHAPE3D_POLYGON_MAX_VERTICES];
		for (legacy_u16 vertex = 0; vertex < count; vertex++) {
			shape3d_vertex_read(source, primitive[2U + source->shape3d_numpaints + vertex],
								&points[vertex]);
			frame_extended_rotate_vertex(&points[vertex], definition->ss_rotY);
		}
		if (frame_extended_paint_terrain_polygon(points, count, primitive[1], primitive[2], cell,
												 &position, flags)) {
			return 1;
		}
		primitive += 2U + source->shape3d_numpaints + count;
	}
	return 0;
}

/* Hill-road resources contain their original grass embankments. The terrain
 * pass supplies that same native surface with this track's independent material
 * and Shore paint, so retaining these faces would paint grass over dirt or ice. */
static void frame_extended_remove_hill_grass(struct SHAPE3D *shape)
{
	legacy_u8 *read = shape->shape3d_primitives, *write = read;
	legacy_u16 kept = 0;
	for (legacy_u16 index = 0; index < shape->shape3d_numprimitives; index++) {
		legacy_u16 size = 2U + shape->shape3d_numpaints + read[0];
		legacy_u8 material = read[2];
		if (material < FRAME_EXTENDED_HILL_GRASS_MATERIAL ||
			material > FRAME_EXTENDED_HILL_GRASS_MATERIAL_LAST) {
			memmove(write, read, size);
			memmove(shape->shape3d_visibility_masks + kept * SHAPE3D_VISIBILITY_MASK_SIZE,
					shape->shape3d_visibility_masks + index * SHAPE3D_VISIBILITY_MASK_SIZE,
					SHAPE3D_VISIBILITY_MASK_SIZE);
			memmove(shape->shape3d_front_facing_masks + kept * SHAPE3D_VISIBILITY_MASK_SIZE,
					shape->shape3d_front_facing_masks + index * SHAPE3D_VISIBILITY_MASK_SIZE,
					SHAPE3D_VISIBILITY_MASK_SIZE);
			write += size;
			kept++;
		}
		read += size;
	}
	write[0] = 0;
	write[1] = 0;
	shape->shape3d_numprimitives = kept;
}

static legacy_s16 frame_draw_extended_objects(struct FRAME_TILE *tile,
											  const struct FRAME_CAMERA *camera, legacy_s8 flags)
{
	const struct CSTRK_TRACK *track = extended_track_model();
	tile->last_east = tile->east;
	tile->last_south = tile->south;
	for (uint32_t i = 0; i < track->object_count; i++) {
		const struct CSTRK_OBJECT *object = &track->objects[i];
		if (object->x != tile->east || object->y != tile->south) {
			continue;
		}
		unsigned id = extended_track_object_id(object);
		if (id >= TRACK_OBJECT_COUNT) {
			continue;
		}
		const struct TRACKOBJECT *definition = &trkObjectList[id];
		const struct CSTRK_CELL *cell = &track->cells[(size_t)object->y * track->width + object->x];
		legacy_u8 native_grass =
			cell->surface == CSTRK_TERRAIN_GRASS && cell->half_water_corner == CSTRK_WATER_NONE;
		struct VECTOR origin;
		extended_geometry_object_origin(track, object, &origin);
		legacy_u8 hill_overlay = (object->slope_x || object->slope_y) && definition->ss_ssOvelay;
		for (legacy_u8 part = 0; part < 2U; part++) {
			legacy_u8 overlay = hill_overlay ? part == 0 : part;
			const struct TRACKOBJECT *render_definition = definition;
			if (overlay) {
				if (!definition->ss_ssOvelay) {
					break;
				}
				render_definition = frame_track_object_from_legacy_index(definition->ss_ssOvelay);
			}
			const struct SHAPE3D *source =
				frame_track_shape((struct TRACKOBJECT *)render_definition, tile->detail);
			if ((object->slope_x || object->slope_y) && native_grass &&
				(overlay || object->surface < source->shape3d_numpaints)) {
				struct TRANSFORMEDSHAPE3D instance = {0};
				instance.pos = (struct VECTOR){LEGACY_S16_WRAP_SUB(origin.x, camera->position.x),
											   LEGACY_S16_WRAP_SUB(origin.y, camera->position.y),
											   LEGACY_S16_WRAP_SUB(origin.z, camera->position.z)};
				instance.shapeptr = (struct SHAPE3D *)source;
				instance.rectptr = &frame_sorted_shapes_rect;
				instance.rotvec.z = render_definition->ss_rotY;
				instance.culling_distance = FRAME_SINGLE_TILE_TRANSFORM_DISTANCE;
				instance.ts_flags =
					flags | FRAME_TRANSFORM_FLAGS_DEFAULT | render_definition->ss_ignoreZBias;
				instance.material = overlay ? 0 : object->surface;
				if ((legacy_s16)shape3d_transform_and_queue(&instance) > 0 &&
					supersight_enabled != 0) {
					return 1;
				}
				continue;
			}
			struct SHAPE3D mesh;
			if (!extended_geometry_transform_shape(source, &mesh, render_definition->ss_rotY,
												   object->surface, definition->ss_physicalModel)) {
				continue;
			}
			if ((object->slope_x || object->slope_y) && !native_grass) {
				frame_extended_remove_hill_grass(&mesh);
			}
			if (!mesh.shape3d_numprimitives) {
				extended_geometry_free_shape(&mesh);
				continue;
			}
			struct TRANSFORMEDSHAPE3D instance = {0};
			instance.pos.x = LEGACY_S16_WRAP_SUB(origin.x, camera->position.x);
			instance.pos.y = LEGACY_S16_WRAP_SUB(origin.y, camera->position.y);
			instance.pos.z = LEGACY_S16_WRAP_SUB(origin.z, camera->position.z);
			instance.shapeptr = &mesh;
			instance.rectptr = &frame_sorted_shapes_rect;
			instance.culling_distance = 2048;
			instance.ts_flags = flags | FRAME_TRANSFORM_FLAGS_DEFAULT;
			legacy_s16 result = shape3d_transform_and_queue(&instance);
			extended_geometry_free_shape(&mesh);
			if (result > 0 && supersight_enabled != 0) {
				return 1;
			}
		}
	}
	return 0;
}
#endif

static legacy_s16 frame_draw_terrain(struct FRAME_TILE *tile, const struct FRAME_CAMERA *camera,
									 legacy_s8 redraw_transform_flags)
{
#if defined(RESTUNTS_SDL3)
	if (extended_track_active()) {
		return frame_draw_extended_terrain(tile, camera, redraw_transform_flags);
	}
#endif
	if (supersight_enabled != 0 &&
		frame_draw_covered_water(tile, camera, redraw_transform_flags) != 0) {
		return 1;
	}

	// Elevated terrain is a flat piece of land at an elevated level.
	if (tile->terrain != TERRAIN_RAISED_TILE) {
		tile->height = 0;

		// Special treatment of elevated corners
		if (tile->element >= FRAME_ELEVATED_CORNER_FIRST &&
			tile->element <= FRAME_ELEVATED_CORNER_LAST) {
			if (frame_draw_elevated_corners(tile, camera, redraw_transform_flags) != 0 &&
				supersight_enabled != 0) {
				return 1;
			}

			tile->terrain = 0;
		}
	} else {
		tile->height = hillHeightConsts[TERRAIN_RAISED_HEIGHT_INDEX];
		if (tile->element != 0) {
			tile->terrain = 0;
		}
	}

	// The rest of the rendering loop still needs to be analyzed in detail.
	// Anyway, the gist is that every tile is associated with various shape,
	// each of which is rendered via a call to `shape3d_transform_and_queue`. The
	// result of such fn is checked each time, since a return value of 1
	// means we ran out of memory

	struct TRACKOBJECT *track_object;
	if (tile->terrain != 0) {
		track_object = &terrain_scene_objects[tile->terrain];
#if defined(RESTUNTS_SDL3)
		currenttransshape->shapeptr = supersight_enabled != 0
										  ? frame_track_shape(track_object, tile->detail)
										  : track_object->ss_shapePtr;
#else
		currenttransshape->shapeptr = track_object->ss_shapePtr;
#endif
		currenttransshape->pos.x =
			LEGACY_S16_WRAP_SUB(track_column_centers[tile->east], camera->position.x);
		currenttransshape->pos.y = LEGACY_S16_WRAP_SUB(tile->height, camera->position.y);
		currenttransshape->pos.z =
			LEGACY_S16_WRAP_SUB(track_row_centers[tile->south], camera->position.z);
		if (tile->height == 0) {
			currenttransshape->rectptr = &frame_unsorted_shapes_rect;
		} else {
			currenttransshape->rectptr = &frame_sorted_shapes_rect;
		}

		currenttransshape->ts_flags = redraw_transform_flags | FRAME_TRANSFORM_FLAGS_NO_DEPTH_SORT;
		currenttransshape->rotvec.x = 0;
		currenttransshape->rotvec.y = 0;
		currenttransshape->rotvec.z = track_object->ss_rotY;
		currenttransshape->culling_distance = FRAME_DEFAULT_TRANSFORM_DISTANCE;
		currenttransshape->material = 0;
		legacy_s16 transform_result = shape3d_transform_and_queue(&currenttransshape[0]);
		if (transform_result > 0) {
			return 1;
		}
	}

	return 0;
}

/* Hill-fill exhaustion stops this pass; the track element still follows. */
static legacy_s16 frame_draw_hill_fill(const struct FRAME_TILE *tile,
									   const struct TRACKOBJECT *track_object,
									   legacy_s8 redraw_transform_flags)
{
	legacy_s16 fill_count;
	legacy_s16 *hill_fill_offsets;
	if (tile->height != 0) {
		if (track_object->ss_multiTileFlag == FRAME_MULTITILE_NONE) {
			fill_count = FRAME_HILL_FILL_COUNT_SINGLE;
			hill_fill_offsets = hill_fill_offsets_single;
		} else if (track_object->ss_multiTileFlag == FRAME_MULTITILE_ROW) {
			fill_count = FRAME_HILL_FILL_COUNT_ROW;
			hill_fill_offsets = hill_fill_offsets_row;
		} else if (track_object->ss_multiTileFlag == FRAME_MULTITILE_COLUMN) {
			fill_count = FRAME_HILL_FILL_COUNT_COLUMN;
			hill_fill_offsets = hill_fill_offsets_column;
		} else if (track_object->ss_multiTileFlag == FRAME_MULTITILE_BOTH) {
			fill_count = FRAME_HILL_FILL_COUNT_BOTH;
			hill_fill_offsets = hill_fill_offsets_both;
		} else {
			return 0;
		}

		for (legacy_s16 fill_index = 0; fill_index < fill_count; fill_index++) {
			currenttransshape->pos.x = LEGACY_S16_WRAP_ADD(*hill_fill_offsets, tile->position.x);
			hill_fill_offsets++;
			currenttransshape->pos.y = tile->position.y;
			currenttransshape->pos.z = LEGACY_S16_WRAP_ADD(*hill_fill_offsets, tile->position.z);
			hill_fill_offsets++;
			currenttransshape->shapeptr = &game3dshapes[FRAME_HILL_FILL_SHAPE_INDEX];
			currenttransshape->rectptr = &frame_sorted_shapes_rect;
			currenttransshape->ts_flags =
				redraw_transform_flags | FRAME_TRANSFORM_FLAGS_NO_DEPTH_SORT;
			currenttransshape->rotvec.x = 0;
			currenttransshape->rotvec.y = 0;
			currenttransshape->rotvec.z = 0;
			currenttransshape->culling_distance = FRAME_SINGLE_TILE_TRANSFORM_DISTANCE;
			currenttransshape->material = 0;
			legacy_s16 transform_result = shape3d_transform_and_queue(&currenttransshape[0]);
			if (transform_result > 0) {
				return 1;
			}
		}
	}
	return 0;
}

static legacy_s16 frame_prepare_overlay(const struct FRAME_TILE *tile,
										const struct TRACKOBJECT *track_object,
										legacy_s8 redraw_transform_flags,
										legacy_s8 animated_material,
										legacy_s8 *overlay_needs_depth_sort)
{
	struct TRACKOBJECT *overlay_track_object;

	if (track_object->ss_ssOvelay != 0) {
		overlay_track_object = frame_track_object_from_legacy_index(track_object->ss_ssOvelay);
#if defined(RESTUNTS_SDL3)
		currenttransshape[1].shapeptr = frame_track_shape(overlay_track_object, tile->detail);
#else
		if (tile->detail != FRAME_TILE_DETAIL_FULL) {
			currenttransshape[1].shapeptr = overlay_track_object->ss_loShapePtr;
		} else {
			currenttransshape[1].shapeptr = overlay_track_object->ss_shapePtr;
		}
#endif

		if (currenttransshape[1].shapeptr != 0) {
			currenttransshape[1].pos = tile->position;
			currenttransshape[1].rotvec.x = 0;
			currenttransshape[1].rotvec.y = 0;
			currenttransshape[1].rotvec.z = overlay_track_object->ss_rotY;
			if (overlay_track_object->ss_multiTileFlag != FRAME_MULTITILE_NONE) {
				currenttransshape[1].culling_distance = FRAME_DEFAULT_TRANSFORM_DISTANCE;
			} else {
				currenttransshape[1].culling_distance = FRAME_SINGLE_TILE_TRANSFORM_DISTANCE;
			}

			if (overlay_track_object->ss_surfaceType >= 0) {
				currenttransshape[1].material = overlay_track_object->ss_surfaceType;
			} else {
				currenttransshape[1].material = animated_material;
			}

			currenttransshape[1].ts_flags = overlay_track_object->ss_ignoreZBias |
											redraw_transform_flags | FRAME_TRANSFORM_FLAGS_DEFAULT;
			if ((currenttransshape[1].ts_flags & FRAME_NO_DEPTH_SORT_FLAG) != 0) {
				currenttransshape[1].rectptr = &frame_unsorted_shapes_rect;
				legacy_s16 transform_result = shape3d_transform_and_queue(&currenttransshape[1]);
				if (transform_result > 0) {
					return 1;
				}
			} else {
				currenttransshape[1].rectptr = &frame_sorted_shapes_rect;
				*overlay_needs_depth_sort = 1;
			}
		}
	}

	return 0;
}

static void frame_add_roadside_sign(const struct FRAME_TILE *tile,
									const struct FRAME_CAMERA *camera,
									legacy_s8 redraw_transform_flags)
{
	legacy_u8 breakable_object_index =
		roadside_sign_indices_by_tile[tile->east + trackrows[tile->south]];
	struct TRACKOBJECT *track_object;
	if (breakable_object_index != FRAME_CHECKPOINT_NONE) {
		if (frame_state->game_object_destroyed[breakable_object_index] == 0) {
			track_object = &trkObjectList[FRAME_CHECKPOINT_TRACK_OBJECT_BASE +
										  roadside_sign_shape_indices[breakable_object_index]];
			curtransshape_ptr->pos.x = LEGACY_S16_WRAP_SUB(
				roadside_sign_positions[breakable_object_index].x, camera->position.x);
			curtransshape_ptr->pos.y = LEGACY_S16_WRAP_SUB(
				roadside_sign_positions[breakable_object_index].y, camera->position.y);
			curtransshape_ptr->pos.z = LEGACY_S16_WRAP_SUB(
				roadside_sign_positions[breakable_object_index].z, camera->position.z);
#if defined(RESTUNTS_SDL3)
			curtransshape_ptr->shapeptr = supersight_enabled != 0
											  ? frame_track_shape(track_object, tile->detail)
											  : track_object->ss_shapePtr;
#else
			curtransshape_ptr->shapeptr = track_object->ss_shapePtr;
#endif
			curtransshape_ptr->rectptr = &frame_sorted_shapes_rect;
			curtransshape_ptr->ts_flags = redraw_transform_flags | FRAME_TRANSFORM_FLAGS_DEFAULT;
			curtransshape_ptr->rotvec.x = 0;
			curtransshape_ptr->rotvec.y = 0;
			curtransshape_ptr->rotvec.z = roadside_sign_headings[breakable_object_index];
			curtransshape_ptr->culling_distance = FRAME_CHECKPOINT_TRANSFORM_DISTANCE;
			curtransshape_ptr->material = 0;
			transformed_shape_add_for_sort(0, 0);
		} else if (frame_state->game_particles_active != 0
#if defined(RESTUNTS_SDL3)
				   && frame_adaptive_active == 0
#endif
		) {
			for (legacy_s16 particle_index = 0; particle_index < FRAME_DEBRIS_SLOT_COUNT;
				 particle_index++) {
				if (frame_state->game_particle_forward_speed[particle_index] != 0 &&
					breakable_object_index + FRAME_CHECKPOINT_OWNER_OFFSET ==
						frame_state->game_particle_owner[particle_index]) {
					track_object =
						&particle_scene_objects[frame_state
													->game_particle_shape_index[particle_index]];
					curtransshape_ptr->pos.x = frame_relative_track_position(
						frame_state->game_particle_x[particle_index],
						roadside_sign_positions[breakable_object_index].x, camera->position.x);
					curtransshape_ptr->pos.y = frame_relative_track_position(
						frame_state->game_particle_y[particle_index],
						roadside_sign_positions[breakable_object_index].y, camera->position.y);
					curtransshape_ptr->pos.z = frame_relative_track_position(
						frame_state->game_particle_z[particle_index],
						roadside_sign_positions[breakable_object_index].z, camera->position.z);
					frame_add_dynamic_shape(track_object, particle_index,
											redraw_transform_flags |
												FRAME_TRANSFORM_FLAGS_NO_DEPTH_SORT,
											0, 0, &camera->position);
				}
			}
		}
	}
}

static void frame_animate_start_flag(void)
{
	legacy_s16 flag_x = multiply_and_scale(cos_fast(start_flag_animation), FRAME_START_FLAG_RADIUS);
	legacy_s16 flag_z = LEGACY_S16_WRAP_ADD(
		multiply_and_scale(sin_fast(start_flag_animation), FRAME_START_FLAG_RADIUS),
		FRAME_START_FLAG_CENTER_Z);

	struct VECTOR start_flag_vertices[FRAME_START_FLAG_VERTEX_COUNT];
	for (legacy_u16 vertex_index = 0; vertex_index < FRAME_START_FLAG_VERTEX_COUNT;
		 vertex_index++) {
		shape3d_vertex_read(&game3dshapes[FRAME_START_FLAG_SHAPE_INDEX],
							LEGACY_U16_WRAP_ADD(FRAME_START_FLAG_FIRST_VERTEX, vertex_index),
							&start_flag_vertices[vertex_index]);
	}
	start_flag_vertices[FRAME_START_FLAG_VERTEX_LEFT_NEAR].x =
		LEGACY_S16_WRAP_SUB(flag_x, FRAME_START_FLAG_RADIUS);
	start_flag_vertices[FRAME_START_FLAG_VERTEX_LEFT_FAR].x =
		LEGACY_S16_WRAP_SUB(flag_x, FRAME_START_FLAG_RADIUS);
	start_flag_vertices[FRAME_START_FLAG_VERTEX_RIGHT_NEAR].x =
		LEGACY_S16_WRAP_SUB(FRAME_START_FLAG_RADIUS, flag_x);
	start_flag_vertices[FRAME_START_FLAG_VERTEX_RIGHT_FAR].x =
		LEGACY_S16_WRAP_SUB(FRAME_START_FLAG_RADIUS, flag_x);

	start_flag_vertices[FRAME_START_FLAG_VERTEX_LEFT_NEAR].z = flag_z;
	start_flag_vertices[FRAME_START_FLAG_VERTEX_LEFT_FAR].z = flag_z;
	start_flag_vertices[FRAME_START_FLAG_VERTEX_RIGHT_NEAR].z = flag_z;
	start_flag_vertices[FRAME_START_FLAG_VERTEX_RIGHT_FAR].z = flag_z;
	for (legacy_u16 vertex_index = 0; vertex_index < FRAME_START_FLAG_VERTEX_COUNT;
		 vertex_index++) {
		shape3d_vertex_write(&game3dshapes[FRAME_START_FLAG_SHAPE_INDEX],
							 LEGACY_U16_WRAP_ADD(FRAME_START_FLAG_FIRST_VERTEX, vertex_index),
							 &start_flag_vertices[vertex_index]);
	}
}

static void frame_add_start_flag(const struct FRAME_TILE *tile, const struct FRAME_CAMERA *camera,
								 legacy_s8 redraw_transform_flags)
{
	if (frame_state->game_inputmode == GAME_INPUT_MODE_WAITING) {
		if ((tile->east == start_finish_column || tile->last_east == start_finish_column) &&
			(tile->south == start_finish_row || tile->last_south == start_finish_row)) {
			frame_animate_start_flag();
#if defined(RESTUNTS_SDL3)
			if (extended_track_active()) {
				const struct CSTRK_OBJECT *object =
					extended_track_object_at(start_finish_column, start_finish_row);
				if (!object) {
					return;
				}
				struct VECTOR origin, offset;
				extended_geometry_object_origin(extended_track_model(), object, &origin);
				offset = (struct VECTOR){
					LEGACY_S16_WRAP_ADD(
						multiply_and_scale(sin_fast(track_angle + ANGLE_QUARTER_TURN),
										   FRAME_START_FLAG_RADIUS),
						multiply_and_scale(sin_fast(track_angle + ANGLE_HALF_TURN),
										   FRAME_START_FLAG_FAR_OFFSET)),
					0,
					LEGACY_S16_WRAP_ADD(
						multiply_and_scale(cos_fast(track_angle + ANGLE_QUARTER_TURN),
										   FRAME_START_FLAG_RADIUS),
						multiply_and_scale(cos_fast(track_angle + ANGLE_HALF_TURN),
										   FRAME_START_FLAG_FAR_OFFSET))};
				unsigned paint =
					(legacy_u16)start_flag_animation >> FRAME_START_FLAG_ANIMATION_SHIFT;
				if (paint > FRAME_START_FLAG_MAX_MATERIAL) {
					paint = FRAME_START_FLAG_MAX_MATERIAL;
				}
				struct SHAPE3D mesh;
				if (extended_geometry_transform_shape(&game3dshapes[FRAME_START_FLAG_SHAPE_INDEX],
													  &mesh, track_angle, paint, -1)) {
					struct TRANSFORMEDSHAPE3D instance = {0};
					instance.pos =
						(struct VECTOR){LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_ADD(origin.x, offset.x),
															camera->position.x),
										LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_ADD(origin.y, offset.y),
															camera->position.y),
										LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_ADD(origin.z, offset.z),
															camera->position.z)};
					instance.shapeptr = &mesh;
					instance.rectptr = &frame_sorted_shapes_rect;
					instance.culling_distance = FRAME_DEFAULT_TRANSFORM_DISTANCE;
					instance.ts_flags = redraw_transform_flags | FRAME_TRANSFORM_FLAGS_DEFAULT;
					shape3d_transform_and_queue(&instance);
					extended_geometry_free_shape(&mesh);
				}
				return;
			}
#endif

			curtransshape_ptr->pos.x = LEGACY_S16_WRAP_SUB(
				LEGACY_S16_WRAP_ADD(
					LEGACY_S16_WRAP_ADD(multiply_and_scale(sin_fast(LEGACY_S16_WRAP_ADD(
															   track_angle, ANGLE_QUARTER_TURN)),
														   FRAME_START_FLAG_RADIUS),
										multiply_and_scale(sin_fast(LEGACY_S16_WRAP_ADD(
															   track_angle, ANGLE_HALF_TURN)),
														   FRAME_START_FLAG_FAR_OFFSET)),
					track_column_centers[start_finish_column]),
				camera->position.x);
			curtransshape_ptr->pos.y =
				LEGACY_S16_WRAP_SUB(hillHeightConsts[hillFlag], camera->position.y);
			curtransshape_ptr->pos.z = LEGACY_S16_WRAP_SUB(
				LEGACY_S16_WRAP_ADD(
					LEGACY_S16_WRAP_ADD(multiply_and_scale(cos_fast(LEGACY_S16_WRAP_ADD(
															   track_angle, ANGLE_QUARTER_TURN)),
														   FRAME_START_FLAG_RADIUS),
										multiply_and_scale(cos_fast(LEGACY_S16_WRAP_ADD(
															   track_angle, ANGLE_HALF_TURN)),
														   FRAME_START_FLAG_FAR_OFFSET)),
					track_row_centers[start_finish_row]),
				camera->position.z);

			curtransshape_ptr->shapeptr = &game3dshapes[FRAME_START_FLAG_SHAPE_INDEX];
			curtransshape_ptr->rectptr = &frame_sorted_shapes_rect;
			curtransshape_ptr->ts_flags = redraw_transform_flags | FRAME_TRANSFORM_FLAGS_DEFAULT;
			curtransshape_ptr->rotvec.x = 0;
			curtransshape_ptr->rotvec.y = 0;
			curtransshape_ptr->rotvec.z = track_angle;
			curtransshape_ptr->culling_distance = FRAME_DEFAULT_TRANSFORM_DISTANCE;
			legacy_s16 flag_material =
				LEGACY_S16_SAR(start_flag_animation, FRAME_START_FLAG_ANIMATION_SHIFT);
			if (flag_material > FRAME_START_FLAG_MAX_MATERIAL) {
				flag_material = FRAME_START_FLAG_MAX_MATERIAL;
			}

			curtransshape_ptr->material = flag_material;
			transformed_shape_add_for_sort(tile->depth_mask & -FRAME_SINGLE_TILE_TRANSFORM_DISTANCE,
										   0);
		}
	}
}

/* Exhaustion stops the current tile's sorted shapes, then advances the tile. */
static void frame_select_brake_paint(legacy_s16 shape_index)
{
	if (transformed_shape_sort_types[shape_index] == FRAME_PLAYER_SORT_ID) {
		if (frame_state->playerstate.car_is_braking != 0) {
			backlights_paint_override = BACKLIGHT_PAINT_BRAKING;
		} else {
			backlights_paint_override = BACKLIGHT_PAINT_NORMAL;
		}
	} else if (transformed_shape_sort_types[shape_index] == FRAME_OPPONENT_SORT_ID) {
		if (frame_state->opponentstate.car_is_braking == 0) {
			backlights_paint_override = BACKLIGHT_PAINT_NORMAL;
		} else {
			backlights_paint_override = BACKLIGHT_PAINT_BRAKING;
		}
	}
}

/* Resource models begin with eight enclosing-box vertices. Keep those in each
 * batch so a clipped first segment cannot hide later segments in the same tile. */
#define FRAME_RACING_LINE_BATCH_SIZE 60U
#define FRAME_RACING_LINE_BOUNDS_VERTICES 8U
#define FRAME_RACING_LINE_SEGMENT_VERTICES 4U
#define FRAME_RACING_LINE_PRIMITIVE_SIZE 7U

#if !defined(RESTUNTS_SDL3)
extern legacy_u16 polygon_buffer_full;
static legacy_u8 frame_racing_line_enabled = 1;
static legacy_u8 frame_racing_line_submitted;

static legacy_s16 frame_retry_without_racing_line(void)
{
	if (frame_racing_line_enabled != 0 && frame_racing_line_submitted != 0 &&
		polygon_buffer_full != 0) {
		/* Guidance must never spend the fixed queue budget needed by the world.
		 * Retry the complete scene, including batches queued before exhaustion. */
		frame_racing_line_enabled = 0;
		return 1;
	}
	return 0;
}
#endif

static legacy_s16 frame_has_racing_line(const struct FRAME_TILE *tile)
{
#if !defined(RESTUNTS_SDL3)
	if (frame_racing_line_enabled == 0) {
		return 0;
	}
#endif
	if (tile == 0 || !ghost_is_active()) {
		return 0;
	}
	for (legacy_s16 south = tile->south; south <= tile->last_south; south++) {
		for (legacy_s16 east = tile->east; east <= tile->last_east; east++) {
			if (ghost_racing_line_first(east, south) != 0) {
				return 1;
			}
		}
	}
	return 0;
}

static struct VECTOR frame_racing_line_origin(const struct FRAME_TILE *tile,
											  const struct FRAME_CAMERA *camera)
{
	if (tile->element != 0) {
		return tile->position;
	}
	struct VECTOR origin;
	origin.x = LEGACY_S16_WRAP_SUB(track_column_centers[tile->east], camera->position.x);
	origin.y = LEGACY_S16_WRAP_SUB(tile->height, camera->position.y);
	origin.z = LEGACY_S16_WRAP_SUB(track_row_centers[tile->south], camera->position.z);
	return origin;
}

static legacy_s16 frame_draw_racing_line(const struct FRAME_TILE *tile,
										 const struct FRAME_CAMERA *camera,
										 legacy_s8 redraw_transform_flags)
{
	static const legacy_u8 paints[] = {12, 14, 10}; /* Bright red, yellow, green. */
	legacy_u8 vertices[(FRAME_RACING_LINE_BOUNDS_VERTICES +
						FRAME_RACING_LINE_BATCH_SIZE * FRAME_RACING_LINE_SEGMENT_VERTICES) *
					   SHAPE3D_VERTEX_SIZE];
	legacy_u8 primitives[FRAME_RACING_LINE_BATCH_SIZE * FRAME_RACING_LINE_PRIMITIVE_SIZE +
						 SHAPE3D_PRIMITIVE_HEADER_SIZE];
	legacy_u8 visibility[FRAME_RACING_LINE_BATCH_SIZE * SHAPE3D_VISIBILITY_MASK_SIZE];
	struct SHAPE3D shape = {0};
	struct TRANSFORMEDSHAPE3D instance = {0};
	shape.shape3d_vertex_bytes = vertices;
	shape.shape3d_primitives = primitives;
	shape.shape3d_numpaints = 1;
	shape.shape3d_visibility_masks = visibility;
	shape.shape3d_front_facing_masks = visibility;
	for (legacy_u16 byte = 0; byte < sizeof(visibility); byte++) {
		visibility[byte] = 255;
	}
	instance.shapeptr = &shape;
	instance.rectptr = &frame_sorted_shapes_rect;
	instance.pos = frame_racing_line_origin(tile, camera);
	instance.ts_flags =
		redraw_transform_flags | FRAME_TRANSFORM_FLAGS_DEFAULT | SHAPE3D_NO_SHADOW_RECEIVE_FLAG;
	instance.culling_distance = FRAME_DEFAULT_TRANSFORM_DISTANCE;
	struct VECTOR origin;
	origin.x = LEGACY_S16_WRAP_ADD(instance.pos.x, camera->position.x);
	origin.y = LEGACY_S16_WRAP_ADD(instance.pos.y, camera->position.y);
	origin.z = LEGACY_S16_WRAP_ADD(instance.pos.z, camera->position.z);

	for (legacy_s16 south = tile->south; south <= tile->last_south; south++) {
		for (legacy_s16 east = tile->east; east <= tile->last_east; east++) {
			const struct GHOST_RACING_LINE_SEGMENT *segment = ghost_racing_line_first(east, south);
			while (segment != 0) {
				struct VECTOR minimum = {LEGACY_S16_MAX, LEGACY_S16_MAX, LEGACY_S16_MAX};
				struct VECTOR maximum = {-32768, -32768, -32768};
				legacy_u16 count = 0;
				while (segment != 0 && count < FRAME_RACING_LINE_BATCH_SIZE) {
					const struct VECTORLONG *corners[FRAME_RACING_LINE_SEGMENT_VERTICES];
					corners[0] = &segment->start_left;
					corners[1] = &segment->start_right;
					corners[2] = &segment->end_right;
					corners[3] = &segment->end_left;
					legacy_u16 first = FRAME_RACING_LINE_BOUNDS_VERTICES +
									   count * FRAME_RACING_LINE_SEGMENT_VERTICES;
					legacy_u8 *primitive = primitives + count * FRAME_RACING_LINE_PRIMITIVE_SIZE;
					primitive[0] = FRAME_RACING_LINE_SEGMENT_VERTICES;
					/* A ribbon is visible from either side, including inside a loop.
					 * Its surface lift supplies depth separation from road artwork. */
					primitive[1] = 1;
					primitive[2] = paints[ghost_racing_line_color(
						frame_state->playerstate.car_actual_speed, segment->speed)];
					for (legacy_u16 corner = 0; corner < FRAME_RACING_LINE_SEGMENT_VERTICES;
						 corner++) {
						struct VECTOR vertex;
						vertex.x =
							LEGACY_S16_WRAP_SUB(position_to_word(corners[corner]->lx), origin.x);
						vertex.y =
							LEGACY_S16_WRAP_SUB(position_to_word(corners[corner]->ly), origin.y);
						vertex.z =
							LEGACY_S16_WRAP_SUB(position_to_word(corners[corner]->lz), origin.z);
						shape3d_vertex_write(&shape, first + corner, &vertex);
						primitive[3 + corner] = (legacy_u8)(first + corner);
						if (vertex.x < minimum.x) {
							minimum.x = vertex.x;
						}
						if (vertex.y < minimum.y) {
							minimum.y = vertex.y;
						}
						if (vertex.z < minimum.z) {
							minimum.z = vertex.z;
						}
						if (vertex.x > maximum.x) {
							maximum.x = vertex.x;
						}
						if (vertex.y > maximum.y) {
							maximum.y = vertex.y;
						}
						if (vertex.z > maximum.z) {
							maximum.z = vertex.z;
						}
					}
					count++;
					segment = ghost_racing_line_next(segment);
				}
				for (legacy_u16 corner = 0; corner < FRAME_RACING_LINE_BOUNDS_VERTICES; corner++) {
					struct VECTOR vertex;
					vertex.x = (corner & 1U) != 0 ? maximum.x : minimum.x;
					vertex.y = (corner & 4U) != 0 ? maximum.y : minimum.y;
					vertex.z = (corner & 2U) != 0 ? maximum.z : minimum.z;
					shape3d_vertex_write(&shape, corner, &vertex);
				}
				shape.shape3d_numverts =
					FRAME_RACING_LINE_BOUNDS_VERTICES + count * FRAME_RACING_LINE_SEGMENT_VERTICES;
				shape.shape3d_numprimitives = count;
				primitives[count * FRAME_RACING_LINE_PRIMITIVE_SIZE] = SHAPE3D_PRIMITIVE_EMPTY;
				/* Classic clipping inspects the next primitive's flags before its
				 * type, including when the last polygon in a full batch is hidden. */
				primitives[count * FRAME_RACING_LINE_PRIMITIVE_SIZE + 1] = 0;
#if !defined(RESTUNTS_SDL3)
				frame_racing_line_submitted = 1;
#endif
				if ((legacy_s16)shape3d_transform_and_queue(&instance) > 0) {
					return 1;
				}
			}
		}
	}
	return 0;
}

static legacy_s16 frame_draw_sorted_shapes(struct FRAME_CAR_RENDER *cars,
										   const struct FRAME_TILE *tile,
										   const struct FRAME_CAMERA *camera,
										   legacy_s8 redraw_transform_flags)
{
	legacy_s16 racing_line_pending = frame_has_racing_line(tile);
	legacy_s16 racing_line_depth = 0;
	legacy_s16 depth_sort_racing_line = 1;
#if defined(RESTUNTS_SDL3)
	depth_sort_racing_line = supersight_enabled == 0;
#endif
	if (racing_line_pending != 0 && depth_sort_racing_line != 0) {
		struct VECTOR origin = frame_racing_line_origin(tile, camera);
		struct VECTOR view;
		mat_mul_vector(&origin, &mat_temp, &view);
		/* Follow the road's own tile sort point, immediately above its paint.
		 * Merge a virtual entry instead of using a slot needed by car/debris shapes. */
		racing_line_depth = LEGACY_S16_WRAP_SUB(view.z, 1);
	}
	if (transformedshape_counter != 0) {
		if (transformedshape_counter >= FRAME_SORT_MINIMUM_SHAPE_COUNT
#if defined(RESTUNTS_SDL3)
			&& supersight_enabled == 0
#endif
		) {
			heapsort_by_order(transformedshape_counter, transformedshape_zarray,
							  transformedshape_indices);
		}

		// Draw red overlights on the brake lights on own and opponent's car
		for (legacy_s16 sort_index = 0; sort_index < transformedshape_counter; sort_index++) {
			legacy_s16 shape_index = transformedshape_indices[sort_index];
			if (racing_line_pending != 0 &&
				(depth_sort_racing_line == 0 ||
				 racing_line_depth > transformedshape_zarray[shape_index])) {
				racing_line_pending = 0;
				/* A full optional line never suppresses the remaining world shapes. */
				(void)frame_draw_racing_line(tile, camera, redraw_transform_flags);
			}
			frame_select_brake_paint(shape_index);

			legacy_s16 transform_result =
				shape3d_transform_and_queue(&currenttransshape[shape_index]);
			if (transform_result > 0) {
				return 1;
			}

			if (transform_result == 0) {
				if (transformed_shape_sort_types[shape_index] == FRAME_PLAYER_SORT_ID) {
					if (frame_state->playerstate.car_crashBmpFlag == CRASH_EVENT_COLLISION) {
						cars[PLAYER_CAR_INDEX].explosion_visible = 1;
					}
				} else if (transformed_shape_sort_types[shape_index] == FRAME_OPPONENT_SORT_ID) {
					if ((currenttransshape[shape_index].ts_flags & SHAPE3D_GHOST_FLAG) == 0U &&
						frame_state->opponentstate.car_crashBmpFlag == CRASH_EVENT_COLLISION) {
						cars[OPPONENT_CAR_INDEX].explosion_visible = 1;
					}
				}
			}
		}
	}
	if (racing_line_pending != 0) {
		(void)frame_draw_racing_line(tile, camera, redraw_transform_flags);
	}
	return 0;
}

#if defined(RESTUNTS_SDL3)
static legacy_s16 frame_draw_adaptive_particles(const struct FRAME_CAMERA *camera,
												struct FRAME_CAR_RENDER *cars,
												legacy_s8 redraw_transform_flags)
{
	if (frame_adaptive_active == 0 || frame_state->game_particles_active == 0) {
		return 0;
	}
	/* Debris can cross the mask boundary independently of its hidden owner.
	 * Gather the active slots once; their complete group fits the existing
	 * transformed-shape array and uses the normal depth-sorted submission. */
	struct VECTOR car_offsets[GAME_CAR_COUNT];
	legacy_u8 offset_ready[GAME_CAR_COUNT] = {0};
	transformedshape_counter = 0;
	curtransshape_ptr = currenttransshape;
	for (legacy_s16 index = 0; index < FRAME_DEBRIS_SLOT_COUNT; index++) {
		if (frame_state->game_particle_forward_speed[index] == 0) {
			continue;
		}
		legacy_s16 owner = frame_state->game_particle_owner[index];
		legacy_s16 flags = redraw_transform_flags | FRAME_TRANSFORM_FLAGS_NO_DEPTH_SORT;
		legacy_s16 material = 0;
		if (owner == PLAYER_CAR_INDEX || owner == OPPONENT_CAR_INDEX) {
			/* Ghosts have no physical debris and do not use opponent particles. */
			if (owner == OPPONENT_CAR_INDEX && gameconfig.game_opponenttype == 0) {
				continue;
			}
			const struct CARSTATE *carstate =
				owner == PLAYER_CAR_INDEX ? &frame_state->playerstate : &frame_state->opponentstate;
			curtransshape_ptr->pos.x = frame_relative_position_sum(
				frame_state->game_particle_x[index], carstate->car_position.lx, camera->position.x);
			curtransshape_ptr->pos.y = frame_relative_position_sum(
				frame_state->game_particle_y[index], carstate->car_position.ly, camera->position.y);
			curtransshape_ptr->pos.z = frame_relative_position_sum(
				frame_state->game_particle_z[index], carstate->car_position.lz, camera->position.z);
			if (offset_ready[owner] == 0) {
				const struct SHAPE3D *wheel_shape =
					&game3dshapes[owner == PLAYER_CAR_INDEX ? PLAYER_CAR_WHEEL_SHAPE
															: OPPONENT_CAR_WHEEL_SHAPE];
				car_offsets[owner] = frame_car_render_offset(carstate, wheel_shape);
				offset_ready[owner] = 1;
			}
			frame_offset_car_shape(&car_offsets[owner]);
			flags |= SHAPE3D_NO_SHADOW_RECEIVE_FLAG;
			material = owner == PLAYER_CAR_INDEX ? gameconfig.game_playermaterial
												 : gameconfig.game_opponentmaterial;
		} else {
			legacy_s16 sign = owner - FRAME_CHECKPOINT_OWNER_OFFSET;
			if (sign < 0 || sign >= (legacy_s16)GAMESTATE_BREAKABLE_OBJECT_COUNT ||
				frame_state->game_object_destroyed[sign] == 0) {
				continue;
			}
			curtransshape_ptr->pos.x =
				frame_relative_track_position(frame_state->game_particle_x[index],
											  roadside_sign_positions[sign].x, camera->position.x);
			curtransshape_ptr->pos.y =
				frame_relative_track_position(frame_state->game_particle_y[index],
											  roadside_sign_positions[sign].y, camera->position.y);
			curtransshape_ptr->pos.z =
				frame_relative_track_position(frame_state->game_particle_z[index],
											  roadside_sign_positions[sign].z, camera->position.z);
		}
		struct TRACKOBJECT *object =
			&particle_scene_objects[frame_state->game_particle_shape_index[index]];
		frame_add_dynamic_shape(object, index, flags, material, 0, &camera->position);
	}
	return frame_draw_sorted_shapes(cars, 0, 0, 0);
}
#endif

static void frame_position_track_element(struct FRAME_TILE *tile, const struct FRAME_CAMERA *camera,
										 const struct TRACKOBJECT *track_object)
{
	legacy_s16 track_object_world_z;
	if ((track_object->ss_multiTileFlag & FRAME_MULTITILE_ROW) != 0) {
		track_object_world_z = track_row_position((legacy_u16)tile->south);
		tile->last_south = LEGACY_S8_WRAP_ADD(tile->south, 1);
	} else {
		track_object_world_z = track_row_centers[tile->south];
		tile->last_south = tile->south;
	}

	legacy_s16 track_object_world_x;
	if ((track_object->ss_multiTileFlag & FRAME_MULTITILE_COLUMN) != 0) {
		track_object_world_x = track_column_position((legacy_u16)LEGACY_S8_WRAP_ADD(tile->east, 1));
		tile->last_east = LEGACY_S8_WRAP_ADD(tile->east, 1);
	} else {
		track_object_world_x = track_column_centers[tile->east];
		tile->last_east = tile->east;
	}

	tile->position.x = LEGACY_S16_WRAP_SUB(track_object_world_x, camera->position.x);
	tile->position.y = LEGACY_S16_WRAP_SUB(tile->height, camera->position.y);
	tile->position.z = LEGACY_S16_WRAP_SUB(track_object_world_z, camera->position.z);
}

static void frame_track_sort_state(struct FRAME_TILE *tile, struct FRAME_CAR_RENDER *cars,
								   legacy_s8 sorted_overlay)
{
	if (sorted_overlay != 0) {
		if (cars[PLAYER_CAR_INDEX].depth_adjustment != 0) {
			cars[PLAYER_CAR_INDEX].depth_adjustment = -FRAME_WHEEL_SORT_ADJUSTMENT;
		}
		if (cars[OPPONENT_CAR_INDEX].depth_adjustment != 0) {
			cars[OPPONENT_CAR_INDEX].depth_adjustment = LEGACY_S16_WRAP_SUB(
				cars[OPPONENT_CAR_INDEX].depth_adjustment, FRAME_WHEEL_SORT_ADJUSTMENT);
		}
	}
	tile->depth_mask =
		tile->east == start_finish_column && tile->south == start_finish_row ? 0 : -1;
}

static legacy_s16
frame_add_track_element(struct FRAME_TILE *tile, const struct FRAME_CAMERA *camera,
						struct FRAME_CAR_RENDER *cars, legacy_s8 redraw_transform_flags,
						legacy_s8 animated_material, legacy_s8 *overlay_needs_depth_sort)
{
	struct TRACKOBJECT *track_object;
#if defined(RESTUNTS_SDL3)
	if (extended_track_active()) {
		return frame_draw_extended_objects(tile, camera, redraw_transform_flags);
	}
#endif

	if (tile->element == 0) {
		tile->last_east = tile->east;
		tile->last_south = tile->south;
	} else {
		track_object = &trkObjectList[tile->element];
		frame_position_track_element(tile, camera, track_object);
		if (frame_draw_hill_fill(tile, track_object, redraw_transform_flags) != 0 &&
			supersight_enabled != 0) {
			return 1;
		}

		if (frame_prepare_overlay(tile, track_object, redraw_transform_flags, animated_material,
								  overlay_needs_depth_sort) != 0) {
			return 1;
		}

#if defined(RESTUNTS_SDL3)
		currenttransshape->shapeptr = frame_track_shape(track_object, tile->detail);
#else
		if (tile->detail != FRAME_TILE_DETAIL_FULL) {
			currenttransshape->shapeptr = track_object->ss_loShapePtr;
		} else {
			currenttransshape->shapeptr = track_object->ss_shapePtr;
		}
#endif

		currenttransshape->pos = tile->position;
		currenttransshape->rotvec.x = 0;
		currenttransshape->rotvec.y = 0;
		currenttransshape->rotvec.z = track_object->ss_rotY;
		if (track_object->ss_multiTileFlag != FRAME_MULTITILE_NONE) {
			currenttransshape->culling_distance = FRAME_DEFAULT_TRANSFORM_DISTANCE;
		} else {
			currenttransshape->culling_distance = FRAME_SINGLE_TILE_TRANSFORM_DISTANCE;
		}

		currenttransshape->ts_flags =
			track_object->ss_ignoreZBias | redraw_transform_flags | FRAME_TRANSFORM_FLAGS_DEFAULT;
		if (track_object->ss_surfaceType >= 0) {
			currenttransshape->material = track_object->ss_surfaceType;
		} else {
			currenttransshape->material = animated_material;
		}

		if ((track_object->ss_ignoreZBias & FRAME_NO_DEPTH_SORT_FLAG) != 0) {
			currenttransshape->rectptr = &frame_unsorted_shapes_rect;
			legacy_s16 transform_result = shape3d_transform_and_queue(&currenttransshape[0]);
			if (transform_result > 0) {
				return 1;
			}
		} else {
			currenttransshape->rectptr = &frame_sorted_shapes_rect;
			transformed_shape_add_for_sort(0, 0);
			legacy_s8 had_sorted_overlay = *overlay_needs_depth_sort;
			if (*overlay_needs_depth_sort != 0) {
				*overlay_needs_depth_sort = 0;
				transformed_shape_add_for_sort(-FRAME_SINGLE_TILE_TRANSFORM_DISTANCE, 0);
			}
			frame_track_sort_state(tile, cars, had_sorted_overlay);
		}

		frame_add_roadside_sign(tile, camera, redraw_transform_flags);
	}

	return 0;
}

static void frame_add_tile_cars(const struct FRAME_TILE *tile, const struct FRAME_CAMERA *camera,
								struct FRAME_CAR_RENDER *cars, legacy_s8 redraw_transform_flags)
{
	if ((cars[PLAYER_CAR_INDEX].east == tile->east ||
		 cars[PLAYER_CAR_INDEX].east == tile->last_east) &&
		(cars[PLAYER_CAR_INDEX].south == tile->south ||
		 cars[PLAYER_CAR_INDEX].south == tile->last_south)) {
		frame_add_car(&frame_state->playerstate, PLAYER_CAR_INDEX, FRAME_PLAYER_SORT_ID,
					  &game3dshapes[PLAYER_CAR_WHEEL_SHAPE], player_wheel_vertex_state,
					  player_base_wheel_vertices, player_front_wheel_centers,
					  &frame_player_car_rect, &cars[PLAYER_CAR_INDEX].crash_rect, &camera->position,
					  tile->detail, redraw_transform_flags, gameconfig.game_playermaterial,
					  cars[PLAYER_CAR_INDEX].depth_adjustment & tile->depth_mask);
	}

	if ((cars[OPPONENT_CAR_INDEX].east == tile->east) ||
		(cars[OPPONENT_CAR_INDEX].east == tile->last_east)) {
		if ((cars[OPPONENT_CAR_INDEX].south == tile->south) ||
			(cars[OPPONENT_CAR_INDEX].south == tile->last_south)) {
			const struct CARSTATE *second_car = frame_second_car_state();
			if (second_car == 0) {
				return;
			}
			legacy_s8 ghost_flag = gameconfig.game_opponenttype == 0 ? SHAPE3D_GHOST_FLAG : 0;
			frame_add_car(second_car, OPPONENT_CAR_INDEX, FRAME_OPPONENT_SORT_ID,
						  &game3dshapes[OPPONENT_CAR_WHEEL_SHAPE], opponent_wheel_vertex_state,
						  opponent_base_wheel_vertices, opponent_front_wheel_centers,
						  &frame_opponent_car_rect, &cars[OPPONENT_CAR_INDEX].crash_rect,
						  &camera->position, tile->detail, redraw_transform_flags | ghost_flag,
						  ghost_flag != 0 ? ghost_car_material() : gameconfig.game_opponentmaterial,
						  cars[OPPONENT_CAR_INDEX].depth_adjustment & tile->depth_mask);
		}
	}
}

static legacy_s16 frame_draw_tiles(const struct FRAME_TILE_SELECTION *tiles,
								   const struct FRAME_CAMERA *camera, struct FRAME_CAR_RENDER *cars,
								   legacy_s8 redraw_transform_flags, legacy_s8 animated_material)
{
	/* A deferred overlay can carry over until a depth-sorted track shape. */
	legacy_s8 overlay_needs_depth_sort = 0;

	/* Submit the selected shapes in tile order. Classic lookahead is arranged
	 * back to front; HyperVision's stable grid order needs no depth sorting. */
	struct FRAME_TILE tile;
	for (legacy_s16 tile_index = tiles->first; tile_index < tiles->count; tile_index++) {
		if (tiles->markers[tile_index] != FRAME_TILE_DRAW_MARKER) {
			continue;
		}
		tile.east = tiles->east[tile_index];
		tile.south = tiles->south[tile_index];
		tile.element = tiles->elements[tile_index];
		tile.terrain = tiles->terrain[tile_index];
		tile.detail = supersight_enabled != 0
						  ? tiles->lookahead[tile_index].detail >= tiles->detail_threshold
						  : tiles->detail[tile_index];
		tile.depth_mask = 0;
#if defined(RESTUNTS_SDL3)
		tile.adaptive_flags = frame_adaptive_active != 0 ? frame_adaptive_tile_flags(&tile) : 0;
		if (tile.adaptive_flags == FRAME_ADAPTIVE_HIDE) {
			continue;
		}
		if ((tile.adaptive_flags & FRAME_ADAPTIVE_LOW_GEOMETRY) != 0) {
			tile.detail = FRAME_TILE_DETAIL_FULL + 1;
		}
#endif
		if (
#if defined(RESTUNTS_SDL3)
			!extended_track_active() &&
#endif
			frame_draw_fences(&tile, tiles, camera, redraw_transform_flags) != 0 &&
			supersight_enabled != 0) {
			return 1;
		}

		if (frame_draw_terrain(&tile, camera, redraw_transform_flags) != 0) {
			return 1;
		}

		transformedshape_counter = 0;
		curtransshape_ptr = currenttransshape;
		if (frame_add_track_element(&tile, camera, cars, redraw_transform_flags, animated_material,
									&overlay_needs_depth_sort) != 0) {
			return 1;
		}

		frame_add_tile_cars(&tile, camera, cars, redraw_transform_flags);

		frame_add_start_flag(&tile, camera, redraw_transform_flags);

		if (frame_draw_sorted_shapes(cars, &tile, camera, redraw_transform_flags) != 0 &&
			supersight_enabled != 0) {
			return 1;
		}
	}
	return 0;
}

#if defined(RESTUNTS_SDL3)
void frame_supersight_reset(void)
{
	frame_adaptive_restart(&frame_adaptive);
	frame_adaptive_active = 0;
}

static void frame_draw_supersight(struct FRAME_TILE_SELECTION *tiles, struct FRAME_CAMERA *camera,
								  struct FRAME_CAR_RENDER *cars, legacy_s8 buffer_index,
								  legacy_s8 redraw_transform_flags, legacy_s8 animated_material)
{
	(void)buffer_index;
	/* Full quality bypasses the adaptive policy; reductions apply only to
	 * isolated presentation snapshots and never to authoritative rendering. */
	tiles->detail_threshold = FRAME_TILE_DETAIL_FULL + 1;
	frame_place_cars(tiles, cars);
	if (frame_draw_tiles(tiles, camera, cars, redraw_transform_flags, animated_material) == 0 &&
		frame_adaptive_active != 0) {
		frame_draw_adaptive_particles(camera, cars, redraw_transform_flags);
	}
}
#else
static void frame_draw_classic(const struct FRAME_TILE_SELECTION *tiles,
							   struct FRAME_CAMERA *camera, struct FRAME_CAR_RENDER *cars,
							   legacy_s8 buffer_index, legacy_s8 redraw_transform_flags,
							   legacy_s8 animated_material)
{
	legacy_s8 initial_brake_paint = backlights_paint_override;
	frame_racing_line_enabled = 1;
	frame_racing_line_submitted = 0;
	frame_place_cars(tiles, cars);
	(void)frame_draw_tiles(tiles, camera, cars, redraw_transform_flags, animated_material);
	if (frame_retry_without_racing_line() != 0) {
		polyinfo_reset();
		backlights_paint_override = initial_brake_paint;
		frame_begin(buffer_index);
		frame_draw_clouds(camera, redraw_transform_flags);
		cars[PLAYER_CAR_INDEX].explosion_visible = 0;
		cars[OPPONENT_CAR_INDEX].explosion_visible = 0;
		frame_place_cars(tiles, cars);
		(void)frame_draw_tiles(tiles, camera, cars, redraw_transform_flags, animated_material);
	}
	frame_racing_line_enabled = 1;
}

static legacy_s16 supersight_attempt_hint;
static legacy_u8 supersight_probe_frames;
static legacy_u8 supersight_view_valid;
static legacy_s16 supersight_previous_view[FRAME_SUPERSIGHT_VIEW_KEY_COUNT];

void frame_supersight_reset(void)
{
	supersight_attempt_hint = 0;
	supersight_probe_frames = 0;
	supersight_view_valid = 0;
}

static legacy_s16 frame_supersight_first_attempt(const struct FRAME_TILE_SELECTION *tiles,
												 const struct FRAME_CAMERA *camera)
{
	/* Reuse nearby views, but immediately probe full quality after a change
	 * of camera, graphics settings, viewport, tile, or heading sector. Explicit
	 * race/replay seek hooks invalidate the hint even when the pose is similar. */
	legacy_s16 view[FRAME_SUPERSIGHT_VIEW_KEY_COUNT];
	view[0] = tiles->camera_east;
	view[1] = tiles->camera_south;
	view[2] = cameramode;
	view[3] = followOpponentFlag;
	view[4] = detail_level;
	view[5] = slow_video_mgmt_copy;
	view[6] = LEGACY_S16_SAR(camera->yaw, FRAME_LOOKAHEAD_HEADING_SHIFT);
	view[7] = LEGACY_S16_SAR(camera->pitch, FRAME_LOOKAHEAD_HEADING_SHIFT);
	view[8] = LEGACY_S16_SAR(camera->roll, FRAME_LOOKAHEAD_HEADING_SHIFT);
	view[9] = LEGACY_S16_SAR(camera->position.y, FRAME_CAMERA_TILE_SHIFT);
	view[10] = select_rect_rc.left;
	view[11] = select_rect_rc.right;
	view[12] = select_rect_rc.top;
	view[13] = select_rect_rc.bottom;
	view[14] = cameramode == CAMERA_MODE_CUSTOM ? custom_camera.distance : 0;
	view[15] = cameramode == CAMERA_MODE_CUSTOM ? custom_camera.elevation_angle : 0;
	view[16] = cameramode == CAMERA_MODE_CUSTOM ? custom_camera.azimuth_angle : 0;
	view[17] = cameramode == CAMERA_MODE_TRACKSIDE ? camera->position.x : 0;
	view[18] = cameramode == CAMERA_MODE_TRACKSIDE ? camera->position.y : 0;
	view[19] = cameramode == CAMERA_MODE_TRACKSIDE ? camera->position.z : 0;

	for (legacy_s16 index = 0; index < FRAME_SUPERSIGHT_VIEW_KEY_COUNT; index++) {
		if (view[index] != supersight_previous_view[index]) {
			supersight_view_valid = 0;
		}
		supersight_previous_view[index] = view[index];
	}
	if (supersight_view_valid == 0 || supersight_probe_frames >= FRAME_SUPERSIGHT_PROBE_INTERVAL) {
		supersight_attempt_hint = 0;
		supersight_probe_frames = 0;
		supersight_view_valid = 1;
	}
	supersight_probe_frames++;
	return supersight_attempt_hint;
}

static void frame_draw_supersight(struct FRAME_TILE_SELECTION *tiles, struct FRAME_CAMERA *camera,
								  struct FRAME_CAR_RENDER *cars, legacy_s8 buffer_index,
								  legacy_s8 redraw_transform_flags, legacy_s8 animated_material)
{
	/* Failed attempts stop immediately. Reuse tile lookup and car placement,
	 * and avoid repeating known-overfull quality levels on nearby frames. */
	struct FRAME_CAR_RENDER placed_cars[FRAME_EXPLOSION_CAR_COUNT] = {{0}};
	frame_place_cars(tiles, placed_cars);
	legacy_s16 attempt = frame_supersight_first_attempt(tiles, camera);
	tiles->first = attempt > 1 ? (attempt - 1) * FRAME_SUPERSIGHT_DISCARD_BATCH : 0;
	if (tiles->first > FRAME_SUPERSIGHT_TILE_COUNT - FRAME_SUPERSIGHT_MINIMUM_TILES) {
		tiles->first = FRAME_SUPERSIGHT_TILE_COUNT - FRAME_SUPERSIGHT_MINIMUM_TILES;
	}
	legacy_s8 initial_brake_paint = backlights_paint_override;
	frame_racing_line_enabled = 1;
	frame_racing_line_submitted = 0;
	for (;;) {
		legacy_s16 threshold_index = attempt;
		if (threshold_index >= (legacy_s16)sizeof(supersight_detail_thresholds)) {
			threshold_index = sizeof(supersight_detail_thresholds) - 1;
		}
		tiles->detail_threshold = supersight_detail_thresholds[threshold_index];
		cars[PLAYER_CAR_INDEX] = placed_cars[PLAYER_CAR_INDEX];
		cars[OPPONENT_CAR_INDEX] = placed_cars[OPPONENT_CAR_INDEX];
		legacy_s16 exhausted =
			frame_draw_tiles(tiles, camera, cars, redraw_transform_flags, animated_material);
		if (frame_retry_without_racing_line() == 0) {
			if (exhausted == 0 ||
				tiles->first == FRAME_SUPERSIGHT_TILE_COUNT - FRAME_SUPERSIGHT_MINIMUM_TILES) {
				supersight_attempt_hint = attempt;
				frame_racing_line_enabled = 1;
				return;
			}
			attempt++;
			if (attempt > 1) {
				tiles->first += FRAME_SUPERSIGHT_DISCARD_BATCH;
				if (tiles->first > FRAME_SUPERSIGHT_TILE_COUNT - FRAME_SUPERSIGHT_MINIMUM_TILES) {
					tiles->first = FRAME_SUPERSIGHT_TILE_COUNT - FRAME_SUPERSIGHT_MINIMUM_TILES;
				}
			}
		}
		polyinfo_reset();
		backlights_paint_override = initial_brake_paint;
		frame_begin(buffer_index);
		frame_draw_clouds(camera, redraw_transform_flags);
	}
}

#endif

static void frame_draw_explosions(struct FRAME_CAR_RENDER *cars, struct RECTANGLE *cliprect)
{
	// Draw the three explosion images in successive four-frame phases.
	struct VECTOR offset_vector;
	struct RECTANGLE *redraw_rect;
	for (legacy_s16 car_index = 0; car_index < FRAME_EXPLOSION_CAR_COUNT; car_index++) {
		if (cars[car_index].explosion_visible == 0) {
			continue;
		}
		if (slow_video_mgmt_copy == 0) {
			if (car_index == PLAYER_CAR_INDEX) {
				redraw_rect = &cars[PLAYER_CAR_INDEX].crash_rect;
			} else {
				redraw_rect = &cars[OPPONENT_CAR_INDEX].crash_rect;
			}
		} else {
			if (car_index == PLAYER_CAR_INDEX) {
				redraw_rect = &frame_player_car_rect;
			} else {
				redraw_rect = &frame_opponent_car_rect;
			}
		}

		if (rect_intersect(redraw_rect, cliprect) == 0) {
			sprite_set_target_clip_bounds(redraw_rect->left, redraw_rect->right, redraw_rect->top,
										  redraw_rect->bottom);
			offset_vector.x =
				LEGACY_S16_SAR(LEGACY_S16_WRAP_ADD(redraw_rect->right, redraw_rect->left),
							   FRAME_RECT_CENTER_SHIFT);
			offset_vector.y =
				LEGACY_S16_SAR(LEGACY_S16_WRAP_ADD(redraw_rect->top, redraw_rect->bottom),
							   FRAME_RECT_CENTER_SHIFT);
			legacy_s16 extent = LEGACY_S16_WRAP_SUB(redraw_rect->right, redraw_rect->left);
			legacy_s16 height_or_scale = LEGACY_S16_WRAP_SUB(redraw_rect->bottom, redraw_rect->top);
			if (height_or_scale > extent) {
				extent = height_or_scale;
			}

			legacy_s16 explosion_index =
				LEGACY_S16_SAR(frame_state->game_frame, FRAME_EXPLOSION_FRAME_SHIFT) %
				FRAME_EXPLOSION_VARIANT_COUNT;
			height_or_scale = LEGACY_S16_FROM_BITS((legacy_u16)LEGACY_S32_DIV_OR_ZERO(
				LEGACY_S32_WRAP_MUL((legacy_s32)extent, FRAME_EXPLOSION_FIXED_SCALE),
				(legacy_s32)sdgame2_widths[explosion_index]));
			shape2d_draw_scaled_transparent_clipped(height_or_scale, sdgame2shapes[explosion_index],
													offset_vector.x, offset_vector.y);
		}
	}
}

static void frame_draw_cockpit_effects(struct RECTANGLE *cliprect)
{
	// Depict windscreen cracking after a crash
	sprite_set_target_clip_bounds(0, FRAME_SCREEN_WIDTH, cliprect->top, cliprect->bottom);
	if (cameramode == CAMERA_MODE_COCKPIT) {
		const struct CARSTATE *viewed_carstate = frame_viewed_car_state();
		const struct GHOST_CAMERA_STATE *ghost_camera = frame_viewed_ghost_camera();
		legacy_s16 frame = frame_state->game_frame;
		legacy_s16 crash_frame = frame_state->game_pEndFrame;
		if (ghost_camera != 0) {
			frame = ghost_camera->frame;
			crash_frame = ghost_camera->crash_frame;
		} else if (viewed_carstate == &frame_state->opponentstate) {
			crash_frame = frame_state->game_oEndFrame;
		}

		if (viewed_carstate->car_crashBmpFlag == CRASH_EVENT_COLLISION) {
			if (slow_video_mgmt_copy != 0) {
				rect_union(
					init_crak(frame - crash_frame, cliprect->top, cliprect->bottom - cliprect->top),
					frame_layer_rects, frame_layer_rects);
			} else {
				init_crak(frame - crash_frame, cliprect->top, cliprect->bottom - cliprect->top);
			}
		} else if (viewed_carstate->car_crashBmpFlag == CRASH_EVENT_WATER) {
			if (slow_video_mgmt_copy != 0) {
				rect_union(do_sinking(frame - crash_frame, cliprect->top,
									  cliprect->bottom - cliprect->top),
						   frame_layer_rects, frame_layer_rects);
			} else {
				do_sinking(frame - crash_frame, cliprect->top, cliprect->bottom - cliprect->top);
			}
		}
	}
}

static void frame_draw_dashboard_top(const struct RECTANGLE *cliprect)
{
	if (dastbmp_y == 0 || dashboard_visible == 0) {
		return;
	}
	if (slow_video_mgmt_copy != 0) {
		struct RECTANGLE mask_bounds;
		mask_bounds.left = 0;
		mask_bounds.right = FRAME_SCREEN_WIDTH;
		mask_bounds.top = dastbmp_y;
		mask_bounds.bottom = dashbmp_y_copy;
		rect_union(frame_layer_rects, &mask_bounds, frame_layer_rects);
	}
	if (supersight_enabled != 0 && replaybar_enabled != 0) {
		/* Custom tops can extend below the replay toolbar boundary. */
		sprite_set_target_clip_bounds(0, FRAME_SCREEN_WIDTH, 0, height_above_replaybar);
		shape2d_rle_mask_position_clipped(dasmshapeptr);
		shape2d_rle_or_position_clipped(
			(struct SHAPE2D far *)dos_memory_make_pointer(dastseg, dastbmp_y2));
		sprite_set_target_clip_bounds(0, FRAME_SCREEN_WIDTH, cliprect->top, cliprect->bottom);
	} else {
		shape2d_render_bmp_as_mask(dasmshapeptr);
		shape2d_rle_or_far_pointer(dastbmp_y2, dastseg);
	}
}

static void frame_draw_elapsed_time(void)
{
	// Show elapsed time
	if (game_replay_mode == REPLAY_MODE_LIVE) {
		if (frame_state->game_inputmode != GAME_INPUT_MODE_WAITING) {
#if defined(RESTUNTS_SDL3) && !defined(RESTUNTS_HEADLESS)
			if (extended_replay_is_active()) {
				extended_replay_format_runtime_time(&resID_byte1,
													extended_replay_clocks.input_frame, 0);
			} else
#endif
				format_frame_as_string(&resID_byte1, elapsed_time1 + elapsed_time2, 0);
			font_set_fontdef2(fontledresptr);
			if (slow_video_mgmt_copy != 0) {
				rect_union(intro_draw_text(&resID_byte1, FRAME_ELAPSED_TIME_X,
										   roofbmpheight + FRAME_ELAPSED_TIME_Y_OFFSET,
										   dialog_fnt_colour, 0),
						   &frame_elapsed_time_rect, &frame_elapsed_time_rect);
			} else {
				intro_draw_text(&resID_byte1, FRAME_ELAPSED_TIME_X,
								roofbmpheight + FRAME_ELAPSED_TIME_Y_OFFSET, dialog_fnt_colour, 0);
			}

			font_set_fontdef();
		}
	}
}

static void frame_finish(legacy_s8 buffer_index, struct RECTANGLE *cliprect,
						 legacy_s16 skybox_requires_full_redraw, legacy_s16 camera_yaw)
{
	if (slow_video_mgmt_copy != 0) {
		rect_union(draw_ingame_text(), frame_layer_rects, frame_layer_rects);
		if (skybox_requires_full_redraw != 0) {
			frame_layer_rects[0] = *cliprect;
			for (legacy_s16 rect_index = 1; rect_index < FRAME_DIRTY_RECT_COUNT; rect_index++) {
				frame_layer_rects[rect_index] = empty_rect;
			}
		}

		for (legacy_s16 rect_index = 0; rect_index < FRAME_DIRTY_RECT_COUNT; rect_index++) {
			active_frame_rects[rect_index] = frame_layer_rects[rect_index];
		}
		frame_buffer_camera_headings[buffer_index] = camera_yaw;
		last_rendered_camera_heading = camera_yaw;

	} else {
		draw_ingame_text();
	}
}

void update_frame(legacy_s8 buffer_index, struct RECTANGLE *cliprect)
{
	/* Ghost guidance adds many small polygons. Native queue growth keeps those
	 * from consuming the classic scene's fixed budget before nearer cars/roads. */
	polyinfo_set_supersight(supersight_enabled
#if defined(RESTUNTS_SDL3)
							|| ghost_is_active()
#endif
	);
	if (supersight_enabled == 0) {
		frame_supersight_reset();
	}
#if defined(RESTUNTS_SDL3)
	frame_adaptive_active = supersight_enabled != 0 && frame_uses_snapshot != 0 &&
							frame_adaptive.quality != 0 && !extended_track_active();
#endif
	struct FRAME_CAR_RENDER cars[FRAME_EXPLOSION_CAR_COUNT];
	cars[PLAYER_CAR_INDEX].explosion_visible = 0;
	cars[OPPONENT_CAR_INDEX].explosion_visible = 0;
	legacy_s8 redraw_transform_flags = frame_begin(buffer_index);
	struct FRAME_CAMERA camera;
#if defined(RESTUNTS_SDL3)
	if (extended_track_active()) {
		const struct CARSTATE *viewed = frame_viewed_car_state();
		extended_geometry_set_context(viewed->car_position.lx, viewed->car_position.lz);
	}
#endif
	frame_setup_camera(&camera);
	legacy_s8 animated_material = frame_animated_material();
	struct FRAME_TILE_SELECTION tiles;
	tiles.lookahead = frame_setup_projection(&camera, cliprect);
	frame_draw_clouds(&camera, redraw_transform_flags);
	frame_select_tiles(&tiles, &camera);
#if defined(RESTUNTS_SDL3)
	if (supersight_enabled != 0) {
		shape3d_hires_ground_begin(&camera.position);
		shape3d_hires_shadows_begin(&camera.position);
		frame_add_car_shadow(&frame_state->playerstate, &simd_player, &camera.position,
							 &game3dshapes[PLAYER_CAR_WHEEL_SHAPE]);
		if (gameconfig.game_opponenttype != 0) {
			frame_add_car_shadow(&frame_state->opponentstate, &simd_opponent, &camera.position,
								 &game3dshapes[OPPONENT_CAR_WHEEL_SHAPE]);
		}
	}
#endif
	if (supersight_enabled != 0) {
		frame_draw_supersight(&tiles, &camera, cars, buffer_index, redraw_transform_flags,
							  animated_material);
	} else {
#if defined(RESTUNTS_SDL3)
		frame_place_cars(&tiles, cars);
		frame_draw_tiles(&tiles, &camera, cars, redraw_transform_flags, animated_material);
#else
		frame_draw_classic(&tiles, &camera, cars, buffer_index, redraw_transform_flags,
						   animated_material);
#endif
	}

	legacy_s16 skybox_requires_full_redraw =
		skybox_render(buffer_index, cliprect, camera.skybox_parameter, &camera.pitch_roll_rotation,
					  camera.roll, camera.yaw, camera.position.y);
	sprite_set_target_clip_bounds(0, FRAME_SCREEN_WIDTH, cliprect->top, cliprect->bottom);
	shape3d_render_queued_primitives();
	frame_draw_explosions(cars, cliprect);
	frame_draw_cockpit_effects(cliprect);
	/* Cockpit trim covers scene effects, while the timer and race messages
	 * remain above every custom upper-dashboard silhouette. */
	frame_draw_dashboard_top(cliprect);
	frame_draw_elapsed_time();
	frame_finish(buffer_index, cliprect, skybox_requires_full_redraw, camera.yaw);
	polyinfo_set_supersight(0);
}

/* A visual snapshot may lag behind physics. Damage, sinking and disappearing
 * cars still follow the authoritative events, including their original timing. */
static void frame_preserve_authoritative_events(struct GAMESTATE *presentation,
												struct CARSTATE *ghost,
												struct GHOST_CAMERA_STATE *ghost_camera)
{
	presentation->playerstate.car_crashBmpFlag = state.playerstate.car_crashBmpFlag;
	presentation->opponentstate.car_crashBmpFlag = state.opponentstate.car_crashBmpFlag;
	presentation->game_pEndFrame = state.game_pEndFrame;
	presentation->game_oEndFrame = state.game_oEndFrame;
	if (ghost != 0) {
		const struct CARSTATE *confirmed_ghost = ghost_car_state();
		ghost->car_crashBmpFlag =
			confirmed_ghost != 0 ? confirmed_ghost->car_crashBmpFlag : CRASH_EVENT_NONE;
	}
	if (ghost_camera != 0) {
		const struct GHOST_CAMERA_STATE *confirmed_camera = ghost_camera_state();
		if (confirmed_camera != 0) {
			ghost_camera->frame = confirmed_camera->frame;
			ghost_camera->crash_frame = confirmed_camera->crash_frame;
		}
	}
}

void update_frame_snapshot(legacy_s8 buffer_index, struct RECTANGLE *cliprect,
						   const struct GAMESTATE *render_state,
						   const struct CARSTATE *render_ghost,
						   const struct GHOST_CAMERA_STATE *render_ghost_camera)
{
	/* External-camera clearance borrows the collision and wheel-travel scratch.
	 * Extra presentations must not leave their visual geometry for physics. */
	struct TRACK_COLLISION_SNAPSHOT saved_collision;
	track_collision_capture(&saved_collision);
	legacy_s16 saved_plane = planindex_copy;
	legacy_s16 saved_heading = wheel_heading_offset;
	legacy_s16 saved_pitch = car_initial_pitch;
	legacy_s16 saved_roll = car_initial_roll;
	legacy_s16 saved_yaw = car_initial_yaw;
	struct VECTOR saved_forward_travel = wheel_forward_travel;
	struct VECTOR saved_world_travel = wheel_world_travel;
	struct LEGACY_EXECUTION_RESIDUE saved_residue = legacy_execution_residue;
	legacy_s16 saved_render_headings = legacy_render_player_headings_active;

	struct GAMESTATE presentation = *render_state;
	struct CARSTATE ghost;
	struct GHOST_CAMERA_STATE ghost_camera;
	if (render_ghost != 0) {
		ghost = *render_ghost;
	}
	if (render_ghost_camera != 0) {
		ghost_camera = *render_ghost_camera;
	}
	frame_preserve_authoritative_events(&presentation, render_ghost != 0 ? &ghost : 0,
										render_ghost_camera != 0 ? &ghost_camera : 0);
	frame_state = &presentation;
	frame_ghost = render_ghost != 0 ? &ghost : 0;
	frame_ghost_camera = render_ghost_camera != 0 ? &ghost_camera : 0;
	frame_uses_snapshot = 1;
	update_frame(buffer_index, cliprect);
	frame_uses_snapshot = 0;
	frame_state = &state;
	frame_ghost = 0;
	frame_ghost_camera = 0;

	track_collision_restore(&saved_collision);
	planindex_copy = saved_plane;
	wheel_heading_offset = saved_heading;
	car_initial_pitch = saved_pitch;
	car_initial_roll = saved_roll;
	car_initial_yaw = saved_yaw;
	wheel_forward_travel = saved_forward_travel;
	wheel_world_travel = saved_world_travel;
	legacy_execution_residue = saved_residue;
	legacy_render_player_headings_active = saved_render_headings;
}
