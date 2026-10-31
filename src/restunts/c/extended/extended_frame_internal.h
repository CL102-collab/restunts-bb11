#ifndef RESTUNTS_EXTENDED_FRAME_INTERNAL_H
#define RESTUNTS_EXTENDED_FRAME_INTERNAL_H

/* Shared renderer declarations and data; implementations stay in C modules. */
#include "extended_frame.h"
#include <stdlib.h>
#include <string.h>
#include "../game_input.h"
#include "../track/trackdata_layout.h"
#include "../frame_render_internal.h"
#include "extended_track.h"
#include "extended_geometry.h"
#include "../shape3d/shape3d_internal.h"
#include "../track/track_objects.h"
#include "../track/track_collision.h"
#include "../scene_resources.h"
#include "../owoot/owoot_road.h"
#include "roll_ramp.h"
#include "../externs.h"
#ifndef RESTUNTS_HEADLESS
#include "extended_replay_runtime.h"
#endif

#define FRAME_EXTENDED_GEOMETRY_MAX_DISTANCE 28672L
#define FRAME_EXTENDED_POSITION_SHIFT 6U

enum FRAME_EXTENDED_TERRAIN_MATERIAL {
	FRAME_EXTENDED_GROUND_GRASS_MATERIAL = 16,
	FRAME_EXTENDED_ICE_MATERIAL = 28,
	FRAME_EXTENDED_DIRT_MATERIAL = 25,
	FRAME_EXTENDED_WATER_MATERIAL = 100,
	FRAME_EXTENDED_HILL_GRASS_MATERIAL = 101,
	FRAME_EXTENDED_HILL_GRASS_MATERIAL_LAST = 105
};

#define FRAME_EXTENDED_TERRAIN_HALF_TILE (CSTRK_TILE_WORLD_SIZE / 2)
#define FRAME_EXTENDED_HILL_EDGE_OVERLAP 2
#define FRAME_EXTENDED_HILL_AUTHORED_EDGE                                                          \
	(FRAME_EXTENDED_TERRAIN_HALF_TILE + FRAME_EXTENDED_HILL_EDGE_OVERLAP)
#define FRAME_EXTENDED_TERRAIN_ALWAYS_VISIBLE 1U
#define FRAME_EXTENDED_OUTER_CORNER_FIRST 11U
#define FRAME_EXTENDED_OUTER_CORNER_LAST 14U
#define FRAME_EXTENDED_TERRAIN_CORNER_COUNT 4U
#define FRAME_EXTENDED_OBJECT_PART_COUNT 2U
#define FRAME_EXTENDED_GROUND_FRAGMENT_INITIAL_CAPACITY 16U
#define FRAME_EXTENDED_CLASSIC_GROUND_FRAGMENT_MAX 256U
#define FRAME_EXTENDED_TERRAIN_NEIGHBOR_COUNT 9U
#define FRAME_EXTENDED_TERRAIN_OBJECT_MAX                                                          \
	(FRAME_EXTENDED_TERRAIN_NEIGHBOR_COUNT * EXTENDED_TRACK_CELL_OBJECT_MAX)
#define FRAME_EXTENDED_ROAD_OVERSCAN 8
#define FRAME_EXTENDED_GROUND_CLIPPED_VERTEX_MAX                                                   \
	(SHAPE3D_POLYGON_MAX_VERTICES + OWOOT_ROAD_TRIANGLE_VERTEX_COUNT)

enum FRAME_EXTENDED_HILL_EDGE {
	FRAME_EXTENDED_HILL_NORTH = 1U,
	FRAME_EXTENDED_HILL_EAST = 2U,
	FRAME_EXTENDED_HILL_SOUTH = 4U,
	FRAME_EXTENDED_HILL_WEST = 8U
};

enum FRAME_EXTENDED_FENCE_SIDE {
	FRAME_EXTENDED_FENCE_NORTH,
	FRAME_EXTENDED_FENCE_EAST,
	FRAME_EXTENDED_FENCE_SOUTH,
	FRAME_EXTENDED_FENCE_WEST,
	FRAME_EXTENDED_FENCE_SIDE_COUNT
};

#define FRAME_EXTENDED_FENCE_STRAIGHT_INDEX_STEP 2U

struct FRAME_EXTENDED_TERRAIN {
	struct CSTRK_TERRAIN_PROFILE profile;
	const struct CSTRK_CELL *cell;
	const struct CSTRK_OBJECT *object;
	const struct CSTRK_OBJECT *objects[FRAME_EXTENDED_TERRAIN_OBJECT_MAX];
	legacy_u16 object_count;
	struct VECTOR position;
	legacy_u8 edges, graded;
	legacy_s16 column, row;
	legacy_s8 flags;
};

struct FRAME_EXTENDED_GROUND_FRAGMENT {
	legacy_u16 count;
	struct VECTOR points[SHAPE3D_POLYGON_MAX_VERTICES];
};

struct FRAME_EXTENDED_OBJECT {
	const struct CSTRK_OBJECT *object;
	const struct TRACKOBJECT *definition;
	struct VECTOR position;
	legacy_u8 native_grass, graded, edges;
	legacy_s8 detail, flags;
};

#define FRAME_TRACK_LAST_X (extended_track_width() - 1)
#define TRACK_GRID_LAST_COORDINATE (extended_track_height() - 1)
#define FRAME_MULTITILE_NONE 0
#define FRAME_MULTITILE_ROW 1
#define FRAME_MULTITILE_COLUMN 2
#define FRAME_MULTITILE_BOTH 3
#define FRAME_TILE_MULTITILE_COVERED_MARKER 1
#define FRAME_EXTENDED_LOOKAHEAD_RADIUS 28
#define FRAME_EXTENDED_LEGACY_ELEMENT_LIMIT 253

#include "../hires/track_frame.h"

void frame_extended_join_object_grade_vertex(struct VECTOR *point,
											 const struct CSTRK_OBJECT *object);
void frame_extended_rotate_vertex(struct VECTOR *point, legacy_s16 yaw);
legacy_s16 frame_extended_border_index(legacy_s16 coordinate, legacy_u16 dimension);
legacy_s16 frame_extended_fence_height(const struct CSTRK_TRACK *track,
									   const struct FRAME_TILE *tile, const struct VECTOR *point);
legacy_s16 frame_draw_extended_fence_span(const struct FRAME_TILE *tile,
										  const struct FRAME_CAMERA *camera,
										  legacy_s8 redraw_transform_flags, legacy_u16 fence_index);
legacy_s16 extended_frame_draw_fences(const struct FRAME_TILE *tile,
									  const struct FRAME_TILE_SELECTION *tiles,
									  const struct FRAME_CAMERA *camera,
									  legacy_s8 redraw_transform_flags);

legacy_s32 frame_extended_shore_distance(const struct VECTOR *point, legacy_u8 corner);
void frame_extended_append_terrain_vertex(struct VECTOR *output, legacy_u16 *count,
										  const struct VECTOR *point);
legacy_u16 frame_extended_clip_terrain(const struct VECTOR *source, legacy_u16 count,
									   struct VECTOR *output, legacy_u8 corner,
									   legacy_u8 secondary);

legacy_s16 frame_extended_paint_terrain_polygon(const struct VECTOR *points, legacy_u16 count,
												legacy_u8 primitive_flags, legacy_u8 grass_material,
												const struct CSTRK_CELL *cell,
												const struct VECTOR *position, legacy_s8 flags);

legacy_u8 frame_extended_native_grass(const struct CSTRK_CELL *cell);
legacy_u8 frame_extended_object_sloped(const struct CSTRK_OBJECT *object);

legacy_s64 frame_extended_ground_side(const struct VECTOR *a, const struct VECTOR *b,
									  const struct VECTOR *point);
legacy_u16 frame_extended_clip_ground(const struct VECTOR *points, legacy_u16 count,
									  struct VECTOR *output, const struct VECTOR *a,
									  const struct VECTOR *b, legacy_s16 orientation);

void frame_extended_remove_hill_grass(struct SHAPE3D *shape);
struct TRANSFORMEDSHAPE3D frame_extended_object_instance(const struct FRAME_EXTENDED_OBJECT *object,
														 struct SHAPE3D *shape);
void frame_extended_apply_object_grade(struct SHAPE3D *mesh, const struct CSTRK_OBJECT *object,
									   const struct VECTOR *upright_anchor);
legacy_s16 frame_extended_draw_object(const struct CSTRK_TRACK *track,
									  const struct CSTRK_OBJECT *model,
									  const struct TRACKOBJECT *definition,
									  const struct FRAME_TILE *tile,
									  const struct FRAME_CAMERA *camera, legacy_s8 flags);
legacy_s16 extended_frame_draw_objects(struct FRAME_TILE *tile, const struct FRAME_CAMERA *camera,
									   legacy_s8 flags);
void extended_frame_draw_preview_tile(legacy_s16 column, legacy_s16 row);
legacy_s16 extended_frame_add_start_flag(const struct FRAME_CAMERA *camera,
										 legacy_s8 redraw_transform_flags);
legacy_s16 extended_frame_car_visible(const struct CARSTATE *carstate);
legacy_s16 extended_frame_background_height(legacy_s16 height);
void extended_frame_position_camera_tiles(struct FRAME_TILE_SELECTION *tiles,
										  const struct FRAME_CAMERA *camera);
void extended_frame_set_context(const struct CARSTATE *viewed);

legacy_u8 extended_frame_should_draw_terrain(const struct FRAME_TILE *tile, legacy_s16 first_pass,
											 legacy_s16 pass);
#if !defined(RESTUNTS_HEADLESS)
legacy_s16 extended_frame_format_elapsed_time(legacy_s8 *out);
#endif

legacy_s16 extended_frame_uses_legacy_tiles(void);
legacy_u8 extended_frame_legacy_element(legacy_u8 element);

legacy_s16 extended_frame_try_draw_terrain(const struct FRAME_TILE *tile,
										   const struct FRAME_CAMERA *camera, legacy_s8 flags,
										   legacy_s16 *result);
legacy_s16 extended_frame_try_draw_objects(struct FRAME_TILE *tile,
										   const struct FRAME_CAMERA *camera, legacy_s8 flags,
										   legacy_s16 *result);

legacy_s16 extended_frame_last_x(void);
legacy_s16 extended_frame_last_y(void);

legacy_u8 frame_extended_uses_grade(const struct CSTRK_OBJECT *object);
legacy_s16 frame_extended_object_grade(const struct CSTRK_OBJECT *object, legacy_s16 local_x,
									   legacy_s16 local_z);
void frame_extended_collect_terrain_objects(struct FRAME_EXTENDED_TERRAIN *terrain);
legacy_s16 frame_extended_draw_supporting_ground(const struct FRAME_EXTENDED_TERRAIN *terrain,
												 legacy_s8 detail);
legacy_s16 frame_extended_draw_terrain_mesh(const struct FRAME_EXTENDED_TERRAIN *terrain,
											legacy_s8 detail);
legacy_s16 extended_frame_draw_terrain(const struct FRAME_TILE *tile,
									   const struct FRAME_CAMERA *camera, legacy_s8 flags);
legacy_s16 frame_extended_queue_terrain_polygon(const struct VECTOR *points, legacy_u16 count,
												legacy_u8 primitive_flags, legacy_u8 material,
												const struct VECTOR *position, legacy_s8 flags);
legacy_s16 frame_extended_append_ground_fragments(struct FRAME_EXTENDED_GROUND_FRAGMENT **fragments,
												  legacy_u16 *written, legacy_u16 *capacity,
												  const struct VECTOR *points, legacy_u16 count);
legacy_s16 frame_extended_paint_terrain_face(const struct VECTOR *points, legacy_u16 count,
											 const legacy_u8 *primitive,
											 const struct FRAME_EXTENDED_TERRAIN *terrain);
legacy_s16 frame_extended_draw_object_mesh(const struct FRAME_EXTENDED_OBJECT *object,
										   const struct TRACKOBJECT *definition,
										   const struct SHAPE3D *source, legacy_u8 surface);
legacy_s16 frame_extended_draw_object_part(const struct FRAME_EXTENDED_OBJECT *object,
										   const struct TRACKOBJECT *definition, legacy_u8 overlay);
void frame_extended_join_hill_vertex(struct VECTOR *point, legacy_u8 edges);
legacy_u8 frame_extended_grade_edges(const struct CSTRK_OBJECT *object);
legacy_s16 extended_frame_mark_covered_tiles(struct FRAME_TILE_SELECTION *tiles,
											 const struct FRAME_TILE *tile, legacy_s16 tile_index);
void extended_frame_position_tiles(struct FRAME_TILE_SELECTION *tiles,
								   const struct FRAME_CAMERA *camera, legacy_u8 adaptive);
legacy_s16 extended_frame_front_fences(const struct FRAME_TILE *tile,
									   const struct FRAME_TILE_SELECTION *tiles,
									   const struct FRAME_CAMERA *camera, legacy_s8 flags);
legacy_u16 frame_extended_queue_hill_shape(struct TRANSFORMEDSHAPE3D *instance, legacy_u8 edges);

#endif
