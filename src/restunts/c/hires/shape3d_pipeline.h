#ifndef RESTUNTS_HIRES_SHAPE3D_PIPELINE_H
#define RESTUNTS_HIRES_SHAPE3D_PIPELINE_H
#include "shape3d_hires.h"
#ifdef RESTUNTS_SDL3
#define SHAPE3D_HIRES_VERTEX_CAPACITY 255U
#define SHAPE3D_SHADOW_PRIMITIVE_CAPACITY (LEGACY_U8_MAX + 1U)
#define SHAPE3D_SHADOW_BOUNDS_VERTEX_COUNT 8U
#define SHAPE3D_SHADOW_PRIMITIVE_FLAGS_OFFSET 1U
struct SHAPE3D_HIRES_TRANSFORM_CONTEXT {
	struct MATRIX object_to_world_rotation;
	struct SHAPE3D_HIRES_VECTOR object_translation;
	struct SHAPE3D_HIRES_VECTOR view_origin;
	struct SHAPE3D_HIRES_VECTOR hires_vertices[SHAPE3D_HIRES_VERTEX_CAPACITY];
	const struct SHAPE3D_HIRES_VECTOR *source_vertices;
	legacy_u32 shadow_surfaces[SHAPE3D_SHADOW_PRIMITIVE_CAPACITY];
	legacy_u32 shadow_surface_count;
	legacy_u32 shadow_primitive;
};
struct SHAPE3D_TRANSFORM_CONTEXT;

/* Shared implementation behind the classic and precise-shape entry points. */
legacy_u16 hypervision_transform_and_queue(struct TRANSFORMEDSHAPE3D *instance,
										   const struct SHAPE3D_HIRES_VECTOR *vertices);
void shape3d_collect_shadow_polygons(const struct TRANSFORMEDSHAPE3D *instance,
									 struct SHAPE3D_TRANSFORM_CONTEXT *context);
void shape3d_hires_prepare_instance(const struct TRANSFORMEDSHAPE3D *instance,
									struct SHAPE3D_TRANSFORM_CONTEXT *context,
									const struct MATRIX *object_rotation);
void shape3d_hires_prepare_pretransformed(struct SHAPE3D_TRANSFORM_CONTEXT *context);
void shape3d_hires_prepare_view_translation(struct SHAPE3D_TRANSFORM_CONTEXT *context);
legacy_s32 shape3d_hires_visibility_reach(const struct TRANSFORMEDSHAPE3D *instance,
										  const struct SHAPE3D_TRANSFORM_CONTEXT *context);
legacy_s32 shape3d_hires_above_camera(const struct SHAPE3D_TRANSFORM_CONTEXT *context);
void shape3d_hires_cache_vertex(const struct SHAPE3D *shape,
								struct SHAPE3D_TRANSFORM_CONTEXT *context, legacy_u16 index);
legacy_u8 shape3d_hires_vertex_rect_flags(const struct SHAPE3D_TRANSFORM_CONTEXT *context,
										  legacy_u16 index);
void shape3d_hires_begin_legacy_shape(struct SHAPE3D_TRANSFORM_CONTEXT *context);
void shape3d_hires_queue_legacy_primitive(const struct SHAPE3D_TRANSFORM_CONTEXT *context,
										  legacy_u8 primitive_type, legacy_u16 primitive_flags);
void shape3d_hires_finish_legacy_primitive(const struct SHAPE3D_TRANSFORM_CONTEXT *context,
										   legacy_u8 primitive_type, legacy_u16 primitive_visible);
void shape3d_hires_next_legacy_primitive(struct SHAPE3D_TRANSFORM_CONTEXT *context);

legacy_u8 shape3d_hires_render_scene(void);
legacy_u32 shape3d_hires_scene_count(void);
void shape3d_hires_set_half_scale(legacy_s16 half_scale);
#endif
#endif
