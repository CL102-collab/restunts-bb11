#ifndef RESTUNTS_HIRES_TRACK_RENDER_H
#define RESTUNTS_HIRES_TRACK_RENDER_H

#include "extended_frame.h"
#include "../hires/settings.h"
#include "../shape3d/shape3d.h"

struct CSTRK_TRACK;
struct CSTRK_OBJECT;
struct CSTRK_CELL;
struct TRACKOBJECT;
struct FRAME_EXTENDED_TERRAIN;
struct FRAME_EXTENDED_OBJECT;
#include "../hires/shape3d_hires.h"
#include "../hires/frame_adaptive.h"

#if defined(RESTUNTS_SDL3)

#define HIRES_FRAME_EXTENDED_GROUND_AREA_EPSILON 0.00000001
#define HIRES_FRAME_EXTENDED_ROAD_OVERSCAN 8
#define HIRES_FRAME_EXTENDED_LOOKAHEAD_RADIUS 28

struct FRAME_HIRES_EXTENDED_PRECISE_GROUND_FRAGMENT {
	legacy_u16 count;
	struct SHAPE3D_HIRES_VECTOR points[SHAPE3D_POLYGON_MAX_VERTICES];
};

#endif

void hires_extended_frame_first_tile_pass(legacy_s16 *first_pass);
legacy_s32 hires_extended_frame_has_complete_tile_lookup(const struct FRAME_TILE_SELECTION *tiles);
legacy_s16 hires_extended_frame_lookup_world_tile(const struct FRAME_TILE_SELECTION *tiles,
												  legacy_s16 east, legacy_s16 south);
void hires_extended_frame_extend_lookahead(struct FRAME_TILE_SELECTION *tiles,
										   const struct FRAME_CAMERA *camera, legacy_u8 adaptive);

legacy_u8 hires_frame_extended_hill_edges(const struct CSTRK_TRACK *track, legacy_s16 x,
										  legacy_s16 y);

void hires_frame_extended_align_hill_markings(struct SHAPE3D *mesh);
legacy_u8 hires_frame_extended_road_grade_edges(const struct SHAPE3D *source, legacy_s16 yaw,
												legacy_u8 edges);

#if defined(RESTUNTS_SDL3)
legacy_u8 hires_frame_extended_upright_scenery(legacy_s16 physical_model);
#endif
#if defined(RESTUNTS_SDL3)
struct VECTOR hires_frame_extended_scenery_anchor(const struct SHAPE3D *source,
												  const struct TRACKOBJECT *definition,
												  const struct CSTRK_OBJECT *object);
#endif
#if defined(RESTUNTS_SDL3)
legacy_u16 hires_frame_extended_queue_precise_object(struct TRANSFORMEDSHAPE3D *instance,
													 const struct CSTRK_OBJECT *object,
													 const struct SHAPE3D *source,
													 const struct VECTOR *upright_anchor);
#endif

void hires_frame_extended_join_low_half(struct VECTOR *points, legacy_u16 count,
										const struct FRAME_EXTENDED_TERRAIN *terrain,
										legacy_s8 detail);

#if defined(RESTUNTS_SDL3)
void hires_frame_extended_append_precise_vertex(struct SHAPE3D_HIRES_VECTOR *output,
												legacy_u16 *count,
												const struct SHAPE3D_HIRES_VECTOR *point);
#endif
#if defined(RESTUNTS_SDL3)
legacy_f64 hires_frame_extended_precise_shore_distance(const struct SHAPE3D_HIRES_VECTOR *point,
													   legacy_u8 corner);
#endif
#if defined(RESTUNTS_SDL3)
legacy_u16 hires_frame_extended_clip_precise_terrain(const struct SHAPE3D_HIRES_VECTOR *source,
													 legacy_u16 count,
													 struct SHAPE3D_HIRES_VECTOR *output,
													 legacy_u8 corner, legacy_u8 secondary);
#endif
#if defined(RESTUNTS_SDL3)
legacy_s16 hires_frame_extended_queue_precise_terrain(const struct SHAPE3D_HIRES_VECTOR *points,
													  legacy_u16 count, legacy_u8 primitive_flags,
													  legacy_u8 material,
													  const struct VECTOR *position,
													  legacy_s8 flags);
#endif
#if defined(RESTUNTS_SDL3)
legacy_s16 hires_frame_extended_paint_precise_terrain(const struct SHAPE3D_HIRES_VECTOR *points,
													  legacy_u16 count, legacy_u8 primitive_flags,
													  legacy_u8 grass_material,
													  const struct CSTRK_CELL *cell,
													  const struct VECTOR *position,
													  legacy_s8 flags);
#endif
#if defined(RESTUNTS_SDL3)
legacy_f64 hires_frame_extended_precise_ground_side(const struct SHAPE3D_HIRES_VECTOR *a,
													const struct SHAPE3D_HIRES_VECTOR *b,
													const struct SHAPE3D_HIRES_VECTOR *point);
#endif
#if defined(RESTUNTS_SDL3)
legacy_u16 hires_frame_extended_clip_precise_ground(const struct SHAPE3D_HIRES_VECTOR *points,
													legacy_u16 count,
													struct SHAPE3D_HIRES_VECTOR *output,
													const struct SHAPE3D_HIRES_VECTOR *a,
													const struct SHAPE3D_HIRES_VECTOR *b,
													legacy_s16 orientation);
#endif
#if defined(RESTUNTS_SDL3)
legacy_s16 hires_frame_extended_append_precise_fragments(
	struct FRAME_HIRES_EXTENDED_PRECISE_GROUND_FRAGMENT **fragments, legacy_u16 *written,
	legacy_u16 *capacity, const struct SHAPE3D_HIRES_VECTOR *points, legacy_u16 count);
#endif
#if defined(RESTUNTS_SDL3)
legacy_s16
hires_frame_extended_paint_precise_terrain_face(const struct VECTOR *source, legacy_u16 count,
												const legacy_u8 *primitive,
												const struct FRAME_EXTENDED_TERRAIN *terrain);
#endif
#if defined(RESTUNTS_SDL3)
legacy_u8 hires_frame_extended_terrain_matches_grade(const struct VECTOR *points, legacy_u16 count,
													 const struct FRAME_EXTENDED_TERRAIN *terrain,
													 const struct CSTRK_OBJECT *object);
#endif

legacy_u8 hires_track_queue_hill_shape(struct TRANSFORMEDSHAPE3D *instance, legacy_u8 edges,
									   legacy_u16 *result);
legacy_u8 hires_track_uses_grade(const struct CSTRK_OBJECT *object, legacy_u8 *result);
legacy_u8 hires_track_object_grade(const struct CSTRK_OBJECT *object, legacy_s16 x, legacy_s16 z,
								   legacy_s16 *result);
void hires_track_collect_terrain_objects(struct FRAME_EXTENDED_TERRAIN *terrain);
void hires_track_select_terrain_shape(const struct TRACKOBJECT *definition, legacy_s8 detail,
									  const struct SHAPE3D **source);
legacy_u8 hires_track_native_support(const struct FRAME_EXTENDED_TERRAIN *terrain);
legacy_s16 hires_track_queue_exhausted(legacy_s16 result);
legacy_u8 hires_track_preserve_ground_polygon(void);
legacy_u8 hires_track_paint_terrain_face(const struct VECTOR *points, legacy_u16 count,
										 const legacy_u8 *primitive,
										 const struct FRAME_EXTENDED_TERRAIN *terrain,
										 legacy_s16 *result);
legacy_u8 hires_track_queue_object(struct TRANSFORMEDSHAPE3D *instance,
								   const struct FRAME_EXTENDED_OBJECT *object,
								   const struct SHAPE3D *source, const struct VECTOR *anchor,
								   legacy_s16 *result);
legacy_s16 hires_track_mark_covered_tiles(struct FRAME_TILE_SELECTION *tiles,
										  const struct FRAME_TILE *tile, legacy_s16 tile_index);
void hires_track_position_tiles(struct FRAME_TILE_SELECTION *tiles,
								const struct FRAME_CAMERA *camera, legacy_u8 adaptive);

const struct VECTOR *hires_track_object_anchor(const struct FRAME_EXTENDED_OBJECT *object,
											   const struct TRACKOBJECT *definition,
											   const struct SHAPE3D *source, struct VECTOR *anchor);

#endif
