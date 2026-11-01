#ifndef RESTUNTS_FRAME_RENDER_PRIVATE_H
#define RESTUNTS_FRAME_RENDER_PRIVATE_H

/* Shared renderer declarations and data; implementations stay in C modules. */
#include "frame_internal.h"
#include "frame_render_internal.h"
#include "../game_input.h"
#include "../track/trackdata_layout.h"
#include "../ui/ui_text.h"
#include "../shape3d/shape3d.h"
#include "../track/track_types.h"
#include "../track/track_objects.h"
#include "../track/track_collision.h"
#include "../wheel_transform.h"
#include "../camera.h"
#include "../video_frame.h"
#include "../shape2d/shape2d.h"
#ifdef RESTUNTS_AMIGA
#include "../shape2d/shape2d_internal.h"
#endif
#include "../car/car_model.h"
#include "../scene_resources.h"
#include "../projection.h"
#include "../skybox.h"
#include "../race/race_graphics.h"
#include "../platform.h"
#include "../math/math.h"
#include "../externs.h"
#include "../crash_state.h"
#include "../ghost/ghost.h"
#include "../residue.h"
#include "../hires/frame_render.h"

#if defined(RESTUNTS_SDL3)
#include "../shape3d/shape3d_internal.h"
#include "../owoot/owoot_road.h"
#include "../extended/extended_frame.h"
#include "../extended/extended_frame_layout.h"
#endif

extern const struct GAMESTATE *frame_state;

struct FRAME_CAR_RENDER {
	legacy_s8 east, south;
	legacy_s16 depth_adjustment;
	struct RECTANGLE crash_rect;
	legacy_s8 explosion_visible;
};

#if defined(RESTUNTS_SDL3)
#define FRAME_TRACK_LAST_X extended_frame_last_x()
#define TRACK_GRID_LAST_COORDINATE extended_frame_last_y()
#else
#define FRAME_TRACK_LAST_X 29
#define TRACK_GRID_LAST_COORDINATE 29
#endif
#define TRACK_WORLD_TILE_SHIFT 16U
#define FRAME_CAR_WHEEL_COUNT 4
#define FRAME_LOOKAHEAD_TILE_COUNT 23
#define FRAME_LOOKAHEAD_DEPTH_TILES 4
#define FRAME_LOOKAHEAD_WIDTH_TILES 2
#define FRAME_CAR_UP_VECTOR_LENGTH 30000
#define FRAME_CAR_NEAR_SORT_ADJUSTMENT 2048
#define FRAME_CAR_TRANSFORM_DISTANCE 300
#define FRAME_SCREEN_WIDTH 320
#define FRAME_DIRTY_RECT_COUNT 15
#define FRAME_FONT_HEIGHT_OFFSET 14U
#define FRAME_DEBRIS_SLOT_COUNT 24
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
#define FRAME_NO_DEPTH_SORT_FLAG 1U
#define FRAME_WHEEL_SORT_ADJUSTMENT 1024
#define FRAME_CHECKPOINT_NONE 255U
#define FRAME_CHECKPOINT_TRACK_OBJECT_BASE 212U
#define FRAME_CHECKPOINT_OWNER_OFFSET 2
#define FRAME_CHECKPOINT_TRANSFORM_DISTANCE 100
#define FRAME_START_FLAG_CENTER_Z 56
#define FRAME_START_FLAG_VERTEX_COUNT 4U
#define FRAME_START_FLAG_FIRST_VERTEX 8U
#define FRAME_EXPLOSION_CAR_COUNT 2
#define FRAME_EXPLOSION_FRAME_SHIFT 2U
#define FRAME_EXPLOSION_VARIANT_COUNT 3
#define FRAME_EXPLOSION_FIXED_SCALE 256L
#define FRAME_ELAPSED_TIME_X 140
#define FRAME_ELAPSED_TIME_Y_OFFSET 2
#define FRAME_DIRTY_RECT_CHANGED 3
#define FRAME_SKYBOX_RECT_INDEX 5
#define FRAME_DETAIL_FASTEST 4
#define FRAME_CAR_LOW_DETAIL_FIRST 3
#define FRAME_STEERED_WHEEL_FIRST_VERTEX 8U
#define FRAME_SLOW_VIDEO_TRANSFORM_FLAG 8
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

extern const legacy_s8 fence_by_edge[FRAME_FENCE_EDGE_CLASS_COUNT][FRAME_FENCE_EDGE_CLASS_COUNT];

#if defined(RESTUNTS_SDL3)
#else

#endif

#define FRAME_RACING_LINE_BATCH_SIZE 60U
#define FRAME_RACING_LINE_BOUNDS_VERTICES 8U
#define FRAME_RACING_LINE_SEGMENT_VERTICES 4U
#define FRAME_RACING_LINE_PRIMITIVE_SIZE 7U
#define FRAME_RACING_LINE_MINIMUM_COORDINATE (-(legacy_s16)LEGACY_S16_MAX - 1)

#if !defined(RESTUNTS_SDL3)
extern legacy_u16 polygon_buffer_full;
extern legacy_u8 frame_racing_line_enabled;
extern legacy_u8 frame_racing_line_submitted;

#endif

struct TRACKOBJECT *frame_track_object_from_legacy_index(legacy_u8 index);
void transformed_shape_add_for_sort(legacy_s16 z_adjust, legacy_s16 type);
legacy_s16 frame_relative_position(legacy_s32 position, legacy_s16 camera_position);
legacy_s16 frame_relative_position_sum(legacy_s32 first, legacy_s32 second,
									   legacy_s16 camera_position);
legacy_s16 frame_relative_track_position(legacy_s32 offset, legacy_s16 track_position,
										 legacy_s16 camera_position);
legacy_s8 frame_tile_from_world(legacy_s32 position);
legacy_s8 frame_south_tile_from_world(legacy_s32 position);
legacy_s8 frame_tile_from_world_offset(legacy_s32 position, legacy_s16 offset);
legacy_s8 frame_south_tile_from_world_offset(legacy_s32 position, legacy_s16 offset);
struct SHAPE3D *frame_terrain_shape(const struct TRACKOBJECT *object, legacy_s8 detail);
legacy_s16 frame_car_z_adjust(const legacy_s8 *wheel_surfaces, struct MATRIX *rotation);
void frame_add_dynamic_shape(struct TRACKOBJECT *track_object, legacy_s16 state_index,
							 legacy_s16 flags, legacy_s16 material, legacy_s16 z_adjust,
							 const struct VECTOR *camera_position);
void frame_prepare_flat_track_shape(struct TRANSFORMEDSHAPE3D *shape, legacy_s8 tile_east,
									legacy_s8 tile_south, const struct VECTOR *camera_position,
									legacy_s16 flags, legacy_s16 rotation);
void init_rect_arrays(void);
void font_set_fontdef2(void far *data);
void font_set_fontdef(void);
void frame_mark_changed_rects(void);
void frame_present(struct RECTANGLE *cliprect);
void frame_prepare_car_model(const struct TRACKOBJECT *track_object,
							 const struct CARSTATE *carstate, struct SHAPE3D *wheel_shape,
							 legacy_s16 *wheel_angles, struct VECTOR *wheel_vectors,
							 struct VECTOR *wheel_vector, legacy_s8 tile_detail);
void frame_prepare_car_clipping(const struct CARSTATE *carstate, struct RECTANGLE *slow_rect,
								struct RECTANGLE *crash_rect);
void frame_add_car(const struct CARSTATE *carstate, legacy_s8 debris_owner, legacy_u16 car_object,
				   struct SHAPE3D *wheel_shape, legacy_s16 *wheel_angles,
				   struct VECTOR *wheel_vectors, struct VECTOR *wheel_vector,
				   struct RECTANGLE *slow_rect, struct RECTANGLE *crash_rect,
				   const struct VECTOR *camera_position, legacy_s8 tile_detail, legacy_s8 flags,
				   legacy_s16 material, legacy_s16 z_adjust);
legacy_s16 frame_border_index(legacy_s8 offset);
struct MATRIX *frame_car_rotation(legacy_s16 rot_x, legacy_s16 rot_y, legacy_s16 rot_z);
#if defined(RESTUNTS_SDL3)
legacy_s32 frame_has_complete_tile_lookup(const struct FRAME_TILE_SELECTION *tiles);
#endif
#if defined(RESTUNTS_SDL3)
legacy_s16 frame_lookup_world_tile(const struct FRAME_TILE_SELECTION *tiles, legacy_s16 east,
								   legacy_s16 south);
#endif
legacy_s16 frame_find_car_wheel(const struct CARSTATE *carstate, const struct SIMD *simd,
								const struct FRAME_TILE_SELECTION *tiles,
								legacy_s8 *result_tile_east, legacy_s8 *result_tile_south);
legacy_s8 frame_begin(legacy_s8 buffer_index);
const struct CARSTATE *frame_second_car_state(void);
const struct CARSTATE *frame_viewed_car_state(void);
const struct GHOST_CAMERA_STATE *frame_viewed_ghost_camera(void);
legacy_s16 frame_position_camera(struct FRAME_CAMERA *camera, const struct VECTOR *car_pos,
								 legacy_s16 car_rot_x, legacy_s16 car_rot_y, legacy_s16 car_rot_z,
								 const struct GHOST_CAMERA_STATE *ghost_camera);
void frame_aim_external_camera(struct FRAME_CAMERA *camera, const struct VECTOR *car_pos);
void frame_setup_camera(struct FRAME_CAMERA *camera);
legacy_s8 frame_animated_material(void);
const struct FRAME_LOOKAHEAD_TILE *frame_setup_projection(struct FRAME_CAMERA *camera,
														  struct RECTANGLE *cliprect);
legacy_s16 frame_background_camera_height(const struct FRAME_CAMERA *camera);
void frame_draw_clouds(struct FRAME_CAMERA *camera, legacy_s8 redraw_transform_flags);
void frame_resolve_track_tile(struct FRAME_TILE *tile);
legacy_s16 frame_lookahead_is_covered(const struct FRAME_LOOKAHEAD_TILE *lookahead,
									  legacy_s16 multitile_flag, legacy_s8 east_offset,
									  legacy_s8 south_offset);
void frame_mark_covered_tiles(struct FRAME_TILE_SELECTION *tiles, const struct FRAME_TILE *tile,
							  legacy_s16 tile_index);
void frame_select_track_tile(struct FRAME_TILE_SELECTION *tiles, struct FRAME_TILE *tile,
							 legacy_s16 tile_index);
void frame_select_lookahead_tile(struct FRAME_TILE_SELECTION *tiles, struct FRAME_TILE *tile,
								 legacy_s16 tile_index, legacy_s8 detail_threshold);
void frame_select_tiles(struct FRAME_TILE_SELECTION *tiles, const struct FRAME_CAMERA *camera);
void frame_place_cars(const struct FRAME_TILE_SELECTION *tiles, struct FRAME_CAR_RENDER *cars);
legacy_s16 frame_draw_fences(const struct FRAME_TILE *tile,
							 const struct FRAME_TILE_SELECTION *tiles,
							 const struct FRAME_CAMERA *camera, legacy_s8 redraw_transform_flags);
legacy_s16 frame_draw_elevated_corners(struct FRAME_TILE *tile, const struct FRAME_CAMERA *camera,
									   legacy_s8 redraw_transform_flags);
legacy_s16 frame_draw_terrain(struct FRAME_TILE *tile, const struct FRAME_CAMERA *camera,
							  legacy_s8 redraw_transform_flags);
legacy_s16 frame_draw_hill_fill(const struct FRAME_TILE *tile,
								const struct TRACKOBJECT *track_object,
								legacy_s8 redraw_transform_flags);
legacy_s16 frame_prepare_overlay(const struct FRAME_TILE *tile,
								 const struct TRACKOBJECT *track_object,
								 legacy_s8 redraw_transform_flags, legacy_s8 animated_material,
								 legacy_s8 *overlay_needs_depth_sort);
void frame_add_roadside_sign(const struct FRAME_TILE *tile, const struct FRAME_CAMERA *camera,
							 legacy_s8 redraw_transform_flags);
void frame_animate_start_flag(void);
void frame_add_start_flag(const struct FRAME_TILE *tile, const struct FRAME_CAMERA *camera,
						  legacy_s8 redraw_transform_flags);
void frame_select_brake_paint(legacy_s16 shape_index);
#if !defined(RESTUNTS_SDL3)
legacy_s16 frame_retry_without_racing_line(void);
#endif
legacy_s16 frame_has_racing_line(const struct FRAME_TILE *tile);
struct VECTOR frame_racing_line_origin(const struct FRAME_TILE *tile,
									   const struct FRAME_CAMERA *camera);
void frame_build_racing_line_segment(struct SHAPE3D *shape,
									 const struct GHOST_RACING_LINE_SEGMENT *segment,
									 legacy_u16 count, const struct VECTOR *origin,
									 struct VECTOR *minimum, struct VECTOR *maximum);
legacy_s16 frame_draw_racing_line(const struct FRAME_TILE *tile, const struct FRAME_CAMERA *camera,
								  legacy_s8 redraw_transform_flags);
void frame_mark_collision_explosion(struct FRAME_CAR_RENDER *cars, legacy_s16 shape_index);
legacy_s16 frame_draw_sorted_shapes(struct FRAME_CAR_RENDER *cars, const struct FRAME_TILE *tile,
									const struct FRAME_CAMERA *camera,
									legacy_s8 redraw_transform_flags);
void frame_position_track_element(struct FRAME_TILE *tile, const struct FRAME_CAMERA *camera,
								  const struct TRACKOBJECT *track_object);
void frame_track_sort_state(struct FRAME_TILE *tile, struct FRAME_CAR_RENDER *cars,
							legacy_s8 sorted_overlay);
legacy_s16 frame_add_track_element(struct FRAME_TILE *tile, const struct FRAME_CAMERA *camera,
								   struct FRAME_CAR_RENDER *cars, legacy_s8 redraw_transform_flags,
								   legacy_s8 animated_material,
								   legacy_s8 *overlay_needs_depth_sort);
void frame_add_tile_cars(const struct FRAME_TILE *tile, const struct FRAME_CAMERA *camera,
						 struct FRAME_CAR_RENDER *cars, legacy_s8 redraw_transform_flags);
legacy_s16 frame_prepare_draw_tile(struct FRAME_TILE *tile,
								   const struct FRAME_TILE_SELECTION *tiles, legacy_s16 tile_index);
legacy_s16 frame_draw_tile_background(struct FRAME_TILE *tile,
									  const struct FRAME_TILE_SELECTION *tiles,
									  const struct FRAME_CAMERA *camera,
									  legacy_s8 redraw_transform_flags, legacy_s16 first_pass,
									  legacy_s16 pass);
legacy_s16 frame_draw_tile_objects(struct FRAME_TILE *tile,
								   const struct FRAME_TILE_SELECTION *tiles,
								   const struct FRAME_CAMERA *camera, struct FRAME_CAR_RENDER *cars,
								   legacy_s8 redraw_transform_flags, legacy_s8 animated_material,
								   legacy_s8 *overlay_needs_depth_sort);
legacy_s16 frame_draw_tiles(const struct FRAME_TILE_SELECTION *tiles,
							const struct FRAME_CAMERA *camera, struct FRAME_CAR_RENDER *cars,
							legacy_s8 redraw_transform_flags, legacy_s8 animated_material);
#if !(defined(RESTUNTS_SDL3))
void frame_draw_classic(const struct FRAME_TILE_SELECTION *tiles, struct FRAME_CAMERA *camera,
						struct FRAME_CAR_RENDER *cars, legacy_s8 buffer_index,
						legacy_s8 redraw_transform_flags, legacy_s8 animated_material);
#endif
void frame_draw_explosions(struct FRAME_CAR_RENDER *cars, struct RECTANGLE *cliprect);
void frame_draw_cockpit_effects(struct RECTANGLE *cliprect);
void frame_draw_dashboard_top(const struct RECTANGLE *cliprect);
void frame_draw_elapsed_time(void);
void frame_finish(legacy_s8 buffer_index, struct RECTANGLE *cliprect,
				  legacy_s16 skybox_requires_full_redraw, legacy_s16 camera_yaw);
void update_frame(legacy_s8 buffer_index, struct RECTANGLE *cliprect);

#endif
