#ifndef RESTUNTS_HIRES_SHAPE3D_PIPELINE_H
#define RESTUNTS_HIRES_SHAPE3D_PIPELINE_H
#include "shape3d_hires.h"
#if defined(RESTUNTS_TEMPLEOS)
#define SHAPE3D_NATIVE_SCALE 2
#define SHAPE3D_NATIVE_COORD_LIMIT ((legacy_s32)LEGACY_S16_MAX / SHAPE3D_NATIVE_SCALE)

/* The native rasterizer uses twice the logical screen resolution. */
legacy_s16 shape3d_native_coordinate(legacy_s64 value);
void shape3d_project_coordinates(legacy_s64 x, legacy_s64 y, legacy_s64 z, struct POINT2D *point);
void shape3d_project_point(struct VECTOR *vector, struct POINT2D *point);
legacy_s16 shape3d_logical_coordinate(legacy_s16 value);
legacy_u16 shape3d_geometry_clip_flags(struct POINT2D *point);
void shape3d_adjust_geometry_bounds(struct POINT2D *point, struct RECTANGLE *rectangle);

void shape3d_native_near_intersection(const struct VECTOR *front, const struct VECTOR *behind,
									  struct POINT2D *point);
void shape3d_native_round_bounds(const struct POINT2D *center, legacy_u16 radius,
								 legacy_s16 padding, struct RECTANGLE *rectangle);
legacy_u16 shape3d_native_sphere_radius(struct VECTOR *radius_vector, legacy_s16 depth);

#endif

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
