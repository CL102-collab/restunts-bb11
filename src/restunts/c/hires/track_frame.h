#ifndef RESTUNTS_HIRES_TRACK_FRAME_H
#define RESTUNTS_HIRES_TRACK_FRAME_H

#include "../extended/extended_frame.h"
#include "../shape3d/shape3d.h"

struct CSTRK_TRACK;
struct CSTRK_OBJECT;
struct CSTRK_CELL;
struct TRACKOBJECT;
struct FRAME_EXTENDED_TERRAIN;
struct FRAME_EXTENDED_OBJECT;
struct FRAME_EXTENDED_GROUND_FRAGMENT;
#include "shape3d_hires.h"
#include "frame_adaptive.h"

#if defined(RESTUNTS_SDL3)

#define FRAME_EXTENDED_GROUND_AREA_EPSILON 0.00000001

struct FRAME_EXTENDED_PRECISE_GROUND_FRAGMENT {
	legacy_u16 count;
	struct SHAPE3D_HIRES_VECTOR points[SHAPE3D_POLYGON_MAX_VERTICES];
};

#endif

legacy_s16 extended_frame_first_tile_pass(void);
legacy_s32 extended_frame_has_complete_tile_lookup(const struct FRAME_TILE_SELECTION *tiles);
legacy_s16 extended_frame_lookup_world_tile(const struct FRAME_TILE_SELECTION *tiles,
											legacy_s16 east, legacy_s16 south);
void extended_frame_extend_lookahead(struct FRAME_TILE_SELECTION *tiles,
									 const struct FRAME_CAMERA *camera, legacy_u8 adaptive);
legacy_s16 extended_frame_mark_covered_tiles(struct FRAME_TILE_SELECTION *tiles,
											 const struct FRAME_TILE *tile, legacy_s16 tile_index);
void extended_frame_position_tiles(struct FRAME_TILE_SELECTION *tiles,
								   const struct FRAME_CAMERA *camera, legacy_u8 adaptive);
legacy_s16 extended_frame_front_fences(const struct FRAME_TILE *tile,
									   const struct FRAME_TILE_SELECTION *tiles,
									   const struct FRAME_CAMERA *camera, legacy_s8 flags);
legacy_u8 frame_extended_hill_edges(const struct CSTRK_TRACK *track, legacy_s16 x, legacy_s16 y);
void frame_extended_join_hill_vertex(struct VECTOR *point, legacy_u8 edges);
legacy_u8 frame_extended_grade_edges(const struct CSTRK_OBJECT *object);
void frame_extended_align_hill_markings(struct SHAPE3D *mesh);
legacy_u8 frame_extended_road_grade_edges(const struct SHAPE3D *source, legacy_s16 yaw,
										  legacy_u8 edges);
legacy_u16 frame_extended_queue_hill_shape(struct TRANSFORMEDSHAPE3D *instance, legacy_u8 edges);
#if defined(RESTUNTS_SDL3)
legacy_u8 frame_extended_upright_scenery(legacy_s16 physical_model);
#endif
#if defined(RESTUNTS_SDL3)
struct VECTOR frame_extended_scenery_anchor(const struct SHAPE3D *source,
											const struct TRACKOBJECT *definition,
											const struct CSTRK_OBJECT *object);
#endif
#if defined(RESTUNTS_SDL3)
legacy_u16 frame_extended_queue_precise_object(struct TRANSFORMEDSHAPE3D *instance,
											   const struct CSTRK_OBJECT *object,
											   const struct SHAPE3D *source,
											   const struct VECTOR *upright_anchor);
#endif
legacy_s16 frame_extended_draw_object_mesh(const struct FRAME_EXTENDED_OBJECT *object,
										   const struct TRACKOBJECT *definition,
										   const struct SHAPE3D *source, legacy_u8 surface);
legacy_s16 frame_extended_draw_object_part(const struct FRAME_EXTENDED_OBJECT *object,
										   const struct TRACKOBJECT *definition, legacy_u8 overlay);
legacy_u8 frame_extended_uses_grade(const struct CSTRK_OBJECT *object);
legacy_s16 frame_extended_object_grade(const struct CSTRK_OBJECT *object, legacy_s16 local_x,
									   legacy_s16 local_z);
void frame_extended_collect_terrain_objects(struct FRAME_EXTENDED_TERRAIN *terrain);
void frame_extended_join_low_half(struct VECTOR *points, legacy_u16 count,
								  const struct FRAME_EXTENDED_TERRAIN *terrain, legacy_s8 detail);
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
#if defined(RESTUNTS_SDL3)
void frame_extended_append_precise_vertex(struct SHAPE3D_HIRES_VECTOR *output, legacy_u16 *count,
										  const struct SHAPE3D_HIRES_VECTOR *point);
#endif
#if defined(RESTUNTS_SDL3)
legacy_f64 frame_extended_precise_shore_distance(const struct SHAPE3D_HIRES_VECTOR *point,
												 legacy_u8 corner);
#endif
#if defined(RESTUNTS_SDL3)
legacy_u16 frame_extended_clip_precise_terrain(const struct SHAPE3D_HIRES_VECTOR *source,
											   legacy_u16 count,
											   struct SHAPE3D_HIRES_VECTOR *output,
											   legacy_u8 corner, legacy_u8 secondary);
#endif
#if defined(RESTUNTS_SDL3)
legacy_s16 frame_extended_queue_precise_terrain(const struct SHAPE3D_HIRES_VECTOR *points,
												legacy_u16 count, legacy_u8 primitive_flags,
												legacy_u8 material, const struct VECTOR *position,
												legacy_s8 flags);
#endif
#if defined(RESTUNTS_SDL3)
legacy_s16 frame_extended_paint_precise_terrain(const struct SHAPE3D_HIRES_VECTOR *points,
												legacy_u16 count, legacy_u8 primitive_flags,
												legacy_u8 grass_material,
												const struct CSTRK_CELL *cell,
												const struct VECTOR *position, legacy_s8 flags);
#endif
#if defined(RESTUNTS_SDL3)
legacy_f64 frame_extended_precise_ground_side(const struct SHAPE3D_HIRES_VECTOR *a,
											  const struct SHAPE3D_HIRES_VECTOR *b,
											  const struct SHAPE3D_HIRES_VECTOR *point);
#endif
#if defined(RESTUNTS_SDL3)
legacy_u16 frame_extended_clip_precise_ground(const struct SHAPE3D_HIRES_VECTOR *points,
											  legacy_u16 count, struct SHAPE3D_HIRES_VECTOR *output,
											  const struct SHAPE3D_HIRES_VECTOR *a,
											  const struct SHAPE3D_HIRES_VECTOR *b,
											  legacy_s16 orientation);
#endif
#if defined(RESTUNTS_SDL3)
legacy_s16 frame_extended_append_precise_fragments(
	struct FRAME_EXTENDED_PRECISE_GROUND_FRAGMENT **fragments, legacy_u16 *written,
	legacy_u16 *capacity, const struct SHAPE3D_HIRES_VECTOR *points, legacy_u16 count);
#endif
#if defined(RESTUNTS_SDL3)
legacy_s16 frame_extended_paint_precise_terrain_face(const struct VECTOR *source, legacy_u16 count,
													 const legacy_u8 *primitive,
													 const struct FRAME_EXTENDED_TERRAIN *terrain);
#endif
#if defined(RESTUNTS_SDL3)
legacy_u8 frame_extended_terrain_matches_grade(const struct VECTOR *points, legacy_u16 count,
											   const struct FRAME_EXTENDED_TERRAIN *terrain,
											   const struct CSTRK_OBJECT *object);
#endif

#endif
