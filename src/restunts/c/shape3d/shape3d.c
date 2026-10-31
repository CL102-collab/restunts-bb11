#include <stddef.h>
#include <limits.h>
#if defined(RESTUNTS_SDL3)
#include <stdlib.h>
#include <string.h>
#endif
#include "../externs.h"
#include "../fileio.h"
#include "../legacy.h"
#include "../memmgr.h"
#include "shape3d.h"
#include "../shape2d/shape2d_internal.h"
#include "shape3d_internal.h"
#include "../shape2d/shape2d.h"
#include "../math_internal.h"
#include "../car/car_model.h"
#include "../projection.h"
#include "../residue.h"
#if defined(RESTUNTS_SDL3)
#undef memcpy
#include "../fatal.h"
#include "../hires/hires.h"
#include "../hires/shape3d_hires.h"
#endif

#include "shape3d_pipeline_internal.h"

#ifdef RESTUNTS_SDL3
static struct SHAPE3D_HIRES_VECTOR hypervision_origin;
#endif

void shape3d_vertex_read(const struct SHAPE3D *shape, legacy_u16 index, struct VECTOR *destination);
void shape3d_vertex_write(struct SHAPE3D *shape, legacy_u16 index, const struct VECTOR *source);
#if (defined(RESTUNTS_TEMPLEOS))
legacy_s16 shape3d_native_coordinate(legacy_s64 value);
#endif
#if (defined(RESTUNTS_TEMPLEOS))
void shape3d_project_coordinates(legacy_s64 x, legacy_s64 y, legacy_s64 z, struct POINT2D *point);
#endif
#if (defined(RESTUNTS_TEMPLEOS))
void shape3d_project_point(struct VECTOR *vector, struct POINT2D *point);
#endif
#if (defined(RESTUNTS_TEMPLEOS))
legacy_s16 shape3d_logical_coordinate(legacy_s16 value);
#endif
#if (defined(RESTUNTS_TEMPLEOS))
legacy_u16 shape3d_geometry_clip_flags(struct POINT2D *point);
#endif
#if (defined(RESTUNTS_TEMPLEOS))
void shape3d_adjust_geometry_bounds(struct POINT2D *point, struct RECTANGLE *rectangle);
#endif
static void shape3d_transform_vertex(const struct SHAPE3D *shape, legacy_u16 index,
									 legacy_s16 half_scale, struct MATRIX *matrix,
									 struct VECTOR *translation, struct VECTOR *transformed);
#if (defined(RESTUNTS_SDL3))
static void shape3d_hires_rotate(const struct SHAPE3D_HIRES_VECTOR *source,
								 const struct MATRIX *matrix, struct SHAPE3D_HIRES_VECTOR *result);
#endif
#if (defined(RESTUNTS_SDL3))
static void shape3d_hires_transform_vertex(const struct SHAPE3D *shape, legacy_u16 index,
										   struct SHAPE3D_TRANSFORM_CONTEXT *context);
#endif
static void shape3d_prepare_instance(struct TRANSFORMEDSHAPE3D *instance,
									 struct SHAPE3D_TRANSFORM_CONTEXT *context);
static void shape3d_prepare_visibility(struct TRANSFORMEDSHAPE3D *instance,
									   struct SHAPE3D_TRANSFORM_CONTEXT *context);
void shape3d_cache_vertex(const struct SHAPE3D *shape, struct SHAPE3D_TRANSFORM_CONTEXT *context,
						  legacy_u16 index);
legacy_u8 shape3d_vertex_rect_flags(struct SHAPE3D_TRANSFORM_CONTEXT *context, legacy_u16 index);
static legacy_u16 shape3d_bounds_are_clipped(struct TRANSFORMEDSHAPE3D *instance,
											 struct SHAPE3D_TRANSFORM_CONTEXT *context);
#if (defined(RESTUNTS_SDL3))
static void shape3d_collect_shadow_polygons(const struct TRANSFORMEDSHAPE3D *instance,
											struct SHAPE3D_TRANSFORM_CONTEXT *context);
#endif
#if (defined(RESTUNTS_SDL3))
static legacy_s32 hypervision_bounds_are_clipped(const struct SHAPE3D *shape,
												 struct SHAPE3D_TRANSFORM_CONTEXT *context);
#endif
#if (defined(RESTUNTS_SDL3))
static legacy_s32 hypervision_primitive_vertices(const struct SHAPE3D *shape,
												 struct SHAPE3D_TRANSFORM_CONTEXT *context,
												 const legacy_u8 *indices, legacy_u16 count,
												 legacy_u8 type);
#endif
#if (defined(RESTUNTS_SDL3))
static legacy_u16 hypervision_transform_and_queue(struct TRANSFORMEDSHAPE3D *instance,
												  const struct SHAPE3D_HIRES_VECTOR *vertices);
#endif
#if (defined(RESTUNTS_SDL3))
legacy_u16 shape3d_hires_transform_and_queue(struct TRANSFORMEDSHAPE3D *instance,
											 const struct SHAPE3D_HIRES_VECTOR *vertices);
#endif
#if (defined(RESTUNTS_SDL3))
void shape3d_hires_set_origin(const struct SHAPE3D_HIRES_VECTOR *origin);
#endif
legacy_u16 shape3d_transform_and_queue(struct TRANSFORMEDSHAPE3D *instance);

void shape3d_vertex_read(const struct SHAPE3D *shape, legacy_u16 index, struct VECTOR *destination)
{
	const legacy_u8 far *source =
		shape->shape3d_vertex_bytes + LEGACY_U16_WRAP_MUL(index, SHAPE3D_VERTEX_SIZE);
	destination->x = LEGACY_READ_S16_LE(source + SHAPE3D_VERTEX_X_OFFSET);
	destination->y = LEGACY_READ_S16_LE(source + SHAPE3D_VERTEX_Y_OFFSET);
	destination->z = LEGACY_READ_S16_LE(source + SHAPE3D_VERTEX_Z_OFFSET);
}

void shape3d_vertex_write(struct SHAPE3D *shape, legacy_u16 index, const struct VECTOR *source)
{
	legacy_u8 far *destination =
		shape->shape3d_vertex_bytes + LEGACY_U16_WRAP_MUL(index, SHAPE3D_VERTEX_SIZE);
	LEGACY_WRITE_U16_LE(destination + SHAPE3D_VERTEX_X_OFFSET, (legacy_u16)source->x);
	LEGACY_WRITE_U16_LE(destination + SHAPE3D_VERTEX_Y_OFFSET, (legacy_u16)source->y);
	LEGACY_WRITE_U16_LE(destination + SHAPE3D_VERTEX_Z_OFFSET, (legacy_u16)source->z);
}

#if (defined(RESTUNTS_TEMPLEOS))
legacy_s16 shape3d_native_coordinate(legacy_s64 value)
{
	if (value < -SHAPE3D_NATIVE_COORD_LIMIT) {
		return -SHAPE3D_NATIVE_COORD_LIMIT;
	}
	if (value > SHAPE3D_NATIVE_COORD_LIMIT) {
		return SHAPE3D_NATIVE_COORD_LIMIT;
	}
	return (legacy_s16)value;
}
#endif

#if (defined(RESTUNTS_TEMPLEOS))
void shape3d_project_coordinates(legacy_s64 x, legacy_s64 y, legacy_s64 z, struct POINT2D *point)
{
	/* Scale before division: half a DOS pixel is a complete native pixel. */
	point->px = shape3d_native_coordinate((legacy_s64)(legacy_s16)projection_center_x *
											  SHAPE3D_NATIVE_SCALE +
										  x * projection_focal_length_x * SHAPE3D_NATIVE_SCALE / z);
	point->py = shape3d_native_coordinate((legacy_s64)(legacy_s16)projection_center_y *
											  SHAPE3D_NATIVE_SCALE -
										  y * projection_focal_length_y * SHAPE3D_NATIVE_SCALE / z);
}
#endif

#if (defined(RESTUNTS_TEMPLEOS))
void shape3d_project_point(struct VECTOR *vector, struct POINT2D *point)
{
	shape3d_project_coordinates(vector->x, vector->y, vector->z, point);
}
#endif

#if (defined(RESTUNTS_TEMPLEOS))
legacy_s16 shape3d_logical_coordinate(legacy_s16 value)
{
	/* Floor negative coordinates so clipping and saved backgrounds agree. */
	if (value < 0) {
		return (
			legacy_s16)(-((-(legacy_s32)value + SHAPE3D_NATIVE_SCALE - 1) / SHAPE3D_NATIVE_SCALE));
	}
	return value / SHAPE3D_NATIVE_SCALE;
}
#endif

#if (defined(RESTUNTS_TEMPLEOS))
legacy_u16 shape3d_geometry_clip_flags(struct POINT2D *point)
{
	struct POINT2D logical;
	logical.px = shape3d_logical_coordinate(point->px);
	logical.py = shape3d_logical_coordinate(point->py);
	return rect_compare_point(&logical);
}
#endif

#if (defined(RESTUNTS_TEMPLEOS))
void shape3d_adjust_geometry_bounds(struct POINT2D *point, struct RECTANGLE *rectangle)
{
	struct POINT2D logical;
	logical.px = shape3d_logical_coordinate(point->px);
	logical.py = shape3d_logical_coordinate(point->py);
	rect_adjust_from_point(&logical, rectangle);
}
#endif

static void shape3d_transform_vertex(const struct SHAPE3D *shape, legacy_u16 index,
									 legacy_s16 half_scale, struct MATRIX *matrix,
									 struct VECTOR *translation, struct VECTOR *transformed)
{
	struct VECTOR source;

	shape3d_vertex_read(shape, index, &source);
	if (half_scale != 0) {
		/* Arithmetic shifts preserve the original flooring for negatives. */
		source.x = LEGACY_S16_SAR(source.x, 1U);
		source.y = LEGACY_S16_SAR(source.y, 1U);
		source.z = LEGACY_S16_SAR(source.z, 1U);
	}
	mat_mul_vector(&source, matrix, transformed);
	transformed->x = LEGACY_S16_WRAP_ADD(transformed->x, translation->x);
	transformed->y = LEGACY_S16_WRAP_ADD(transformed->y, translation->y);
	transformed->z = LEGACY_S16_WRAP_ADD(transformed->z, translation->z);
}

#if (defined(RESTUNTS_SDL3))
static void shape3d_hires_rotate(const struct SHAPE3D_HIRES_VECTOR *source,
								 const struct MATRIX *matrix, struct SHAPE3D_HIRES_VECTOR *result)
{
	result->x =
		(source->x * matrix->m._11 + source->y * matrix->m._12 + source->z * matrix->m._13) /
		TRIG_FIXED_ONE;
	result->y =
		(source->x * matrix->m._21 + source->y * matrix->m._22 + source->z * matrix->m._23) /
		TRIG_FIXED_ONE;
	result->z =
		(source->x * matrix->m._31 + source->y * matrix->m._32 + source->z * matrix->m._33) /
		TRIG_FIXED_ONE;
}
#endif

#if (defined(RESTUNTS_SDL3))
static void shape3d_hires_transform_vertex(const struct SHAPE3D *shape, legacy_u16 index,
										   struct SHAPE3D_TRANSFORM_CONTEXT *context)
{
	struct SHAPE3D_HIRES_VECTOR source;
	if (context->source_vertices != NULL) {
		source = context->source_vertices[index];
	} else {
		struct VECTOR vertex;
		shape3d_vertex_read(shape, index, &vertex);
		source = (struct SHAPE3D_HIRES_VECTOR){vertex.x, vertex.y, vertex.z};
	}
	legacy_f64 scale = shape_half_scale != 0 ? 0.5 : 1.0;
	source.x *= scale;
	source.y *= scale;
	source.z *= scale;
	struct SHAPE3D_HIRES_VECTOR world;
	shape3d_hires_rotate(&source, &context->object_to_world_rotation, &world);
	struct SHAPE3D_HIRES_VECTOR *view = &context->hires_vertices[index];
	if ((transshapeflags & SHAPE3D_PRETRANSFORMED_FLAG) != 0) {
		shape3d_hires_rotate(&world, &mat_temp, view);
		view->x += context->object_translation.x;
		view->y += context->object_translation.y;
		view->z += context->object_translation.z;
	} else {
		/* Adjacent shapes can express one world vertex using different local
		 * coordinates. Combine position before the camera transform so both
		 * reach exactly the same screen position, without per-shape rounding. */
		world.x += context->object_translation.x;
		world.y += context->object_translation.y;
		world.z += context->object_translation.z;
		shape3d_hires_rotate(&world, &mat_temp, view);
	}
}
#endif

static void shape3d_prepare_instance(struct TRANSFORMEDSHAPE3D *instance,
									 struct SHAPE3D_TRANSFORM_CONTEXT *context)
{
	transshapeprimitives = instance->shapeptr->shape3d_primitives;
	transshapenumpaints = instance->shapeptr->shape3d_numpaints;
	transshapematerial = instance->material;
	if (transshapematerial >= transshapenumpaints) {
		transshapematerial = 0;
	}
	transshapeflags = instance->ts_flags;
	if ((transshapeflags & SHAPE3D_USE_BOUNDING_RECT_FLAG) != 0) {
		transshaperectptr = instance->rectptr;
	}
	for (legacy_u16 i = 0; i < transshapenumverts; i++) {
		context->vertex_clip_flags[i] = SHAPE3D_VERTEX_UNTRANSFORMED;
	}

	context->visibility_mask = -1;
	context->front_facing_mask = 0;
	struct MATRIX *object_rotation = mat_rot_zxy(instance->rotvec.x, instance->rotvec.y,
												 instance->rotvec.z, MATRIX_ROTATION_ORDER_ZXY);
#if defined(RESTUNTS_SDL3)
	context->shadow_surface_count = 0;
	context->source_vertices = NULL;
	context->object_to_world_rotation = *object_rotation;
	context->object_translation = (struct SHAPE3D_HIRES_VECTOR){
		instance->pos.x + hypervision_origin.x, instance->pos.y + hypervision_origin.y,
		instance->pos.z + hypervision_origin.z};
#endif
	if ((transshapeflags & SHAPE3D_PRETRANSFORMED_FLAG) != 0) {
		mat_multiply(object_rotation, &mat_temp, &context->object_to_view_rotation);
		context->view_translation = instance->pos;
#if defined(RESTUNTS_SDL3)
		context->view_origin = context->object_translation;
#endif
		return;
	}
#if defined(RESTUNTS_SDL3)
	if (hires_enabled()) {
		shape3d_hires_rotate(&context->object_translation, &mat_temp, &context->view_origin);
	} else
#endif
	{
		mat_mul_vector(&instance->pos, &mat_temp, &context->view_translation);
	}
	mat_multiply(object_rotation, &mat_temp, &context->object_to_view_rotation);
}

static void shape3d_prepare_visibility(struct TRANSFORMEDSHAPE3D *instance,
									   struct SHAPE3D_TRANSFORM_CONTEXT *context)
{
	if ((transshapeflags & SHAPE3D_PRETRANSFORMED_FLAG) != 0) {
		return;
	}
#if defined(RESTUNTS_SDL3)
	if (hires_enabled()) {
		legacy_f64 reach = (legacy_f64)instance->culling_distance * 2;
		if (context->view_origin.x > -reach && context->view_origin.x < reach &&
			context->view_origin.z > -reach && context->view_origin.z < reach) {
			return;
		}
	} else
#endif
	{
		if (LEGACY_S16_SHL(instance->culling_distance, 1U) >
				absolute_word(context->view_translation.x) &&
			LEGACY_S16_SHL(instance->culling_distance, 1U) >
				absolute_word(context->view_translation.z)) {
			return;
		}
	}
	struct MATRIX inverse_view_rotation;
	mat_invert(&context->object_to_view_rotation, &inverse_view_rotation);
	struct VECTOR forward_vector;
	forward_vector.x = 0;
	forward_vector.y = 0;
	forward_vector.z = SHAPE3D_FORWARD_VECTOR_SCALE;
	struct VECTOR view_direction;
	mat_mul_vector(&forward_vector, &inverse_view_rotation, &view_direction);
	legacy_s32 above_camera = instance->pos.y >= 0;
#if defined(RESTUNTS_SDL3)
	if (hires_enabled()) {
		above_camera = context->object_translation.y >= 0;
	}
#endif
	if (view_direction.y <= 0 || above_camera) {
		shape_view_direction_sector = vector_direction_sector(&view_direction);
		context->visibility_mask = invpow2tbl[shape_view_direction_sector];
		context->front_facing_mask = invpow2tbl[shape_view_direction_sector];
	}
}

void shape3d_cache_vertex(const struct SHAPE3D *shape, struct SHAPE3D_TRANSFORM_CONTEXT *context,
						  legacy_u16 index)
{
#if defined(RESTUNTS_SDL3)
	if (hires_enabled()) {
		shape3d_hires_transform_vertex(shape, index, context);
		context->vertex_clip_flags[index] = context->hires_vertices[index].z < SHAPE3D_NEAR_CLIP_Z;
		return;
	}
#endif
	struct VECTOR transformed;
	shape3d_transform_vertex(shape, index, shape_half_scale, &context->object_to_view_rotation,
							 &context->view_translation, &transformed);
	context->view_vertices[index] = transformed;
	if (transformed.z < SHAPE3D_NEAR_CLIP_Z) {
		context->vertex_clip_flags[index] = 1;
	} else {
		context->vertex_clip_flags[index] = 0;
		shape3d_project_point(&transformed, &context->projected_vertices[index]);
	}
}

legacy_u8 shape3d_vertex_rect_flags(struct SHAPE3D_TRANSFORM_CONTEXT *context, legacy_u16 index)
{
#if defined(RESTUNTS_SDL3)
	if (hires_enabled()) {
		return shape3d_hires_clip_flags(&context->hires_vertices[index]);
	}
#endif
	return (legacy_u8)shape3d_geometry_clip_flags(&context->projected_vertices[index]);
}

static legacy_u16 shape3d_bounds_are_clipped(struct TRANSFORMEDSHAPE3D *instance,
											 struct SHAPE3D_TRANSFORM_CONTEXT *context)
{
	if (transshapenumverts <= 8) {
		transshapenumvertscopy = transshapenumverts;
	} else {
		transshapenumvertscopy = 8;
	}
	struct VECTOR fifth_vertex;
	struct VECTOR first_vertex;
	if (transshapenumvertscopy > 4) {
		shape3d_vertex_read(instance->shapeptr, 0U, &first_vertex);
		shape3d_vertex_read(instance->shapeptr, 4U, &fifth_vertex);
		if (first_vertex.y == fifth_vertex.y) {
			transshapenumvertscopy = 4;
		}
	}

	legacy_u8 common_clip_flags = SHAPE3D_ALL_RECT_CLIP_FLAGS;
	legacy_u16 i;
	legacy_u16 all_vertices_behind = 1;
	legacy_u16 any_vertex_behind = 0;
	for (i = 0; i < transshapenumvertscopy; i = LEGACY_U16_WRAP_ADD(i, 1U)) {
		polyvertpointptrtab[i] = &context->projected_vertices[i];
		shape3d_cache_vertex(instance->shapeptr, context, i);
		if (context->vertex_clip_flags[i] != 0) {
			any_vertex_behind = 1;
			continue;
		}
		all_vertices_behind = 0;
		if (common_clip_flags != 0) {
			common_clip_flags &= shape3d_vertex_rect_flags(context, i);
		}
		if (common_clip_flags == 0) {
			break;
		}
	}
	return i == transshapenumvertscopy && (all_vertices_behind != 0 || any_vertex_behind == 0 ||
										   LEGACY_S16_FROM_BITS(instance->culling_distance) <
											   absolute_word(context->view_translation.x));
}

#if (defined(RESTUNTS_SDL3))
static void shape3d_collect_shadow_polygons(const struct TRANSFORMEDSHAPE3D *instance,
											struct SHAPE3D_TRANSFORM_CONTEXT *context)
{
	if (!shape3d_hires_shadows_active() || !hires_enabled() ||
		hires_render_scale() == HIRES_MINIMUM_SCALE ||
		(transshapeflags &
		 (SHAPE3D_NO_SHADOW_RECEIVE_FLAG | SHAPE3D_GHOST_FLAG | SHAPE3D_BACKGROUND_FLAG)) != 0) {
		return;
	}
	const struct SHAPE3D *shape = instance->shapeptr;
	legacy_u32 bounds_count = transshapenumverts < SHAPE3D_SHADOW_BOUNDS_VERTEX_COUNT
								  ? transshapenumverts
								  : SHAPE3D_SHADOW_BOUNDS_VERTEX_COUNT;
	/* The camera bounds test can stop early or use only four flat corners. */
	for (legacy_u32 vertex = 0; vertex < bounds_count; vertex++) {
		if (context->vertex_clip_flags[vertex] == SHAPE3D_VERTEX_UNTRANSFORMED) {
			shape3d_cache_vertex(shape, context, (legacy_u16)vertex);
		}
	}
	if (!shape3d_hires_shadow_overlap(context->hires_vertices, bounds_count)) {
		return;
	}
	/* Shadow blockers retain their geometry even when camera visibility rejects them. */
	const legacy_u8 *primitive = shape->shape3d_primitives;
	while (primitive[0] != 0 && context->shadow_surface_count < SHAPE3D_SHADOW_PRIMITIVE_CAPACITY) {
		legacy_u32 surface = 0;
		legacy_u32 count = primidxcounttab[primitive[0]];
		const legacy_u8 *indices = primitive + SHAPE3D_PRIMITIVE_HEADER_SIZE + transshapenumpaints;
		if (primtypetab[primitive[0]] == RENDER_PRIMITIVE_POLYGON &&
			count <= SHAPE3D_POLYGON_MAX_VERTICES &&
			(primitive[SHAPE3D_SHADOW_PRIMITIVE_FLAGS_OFFSET] &
			 SHAPE3D_PRIMITIVE_SKIP_DEPTH_SORT_FLAG) == 0) {
			legacy_u16 material = primitive[SHAPE3D_PRIMITIVE_HEADER_SIZE + transshapematerial];
			legacy_u16 pattern_type = shape3d_material_value(material_patlist_ptr_cpy, material);
			if (pattern_type <= SHAPE3D_PATTERN_TWO_COLOR &&
				(pattern_type != SHAPE3D_PATTERN_MASKED ||
				 shape3d_material_value(material_patlist2_ptr_cpy, material) != 0U)) {
				struct SHAPE3D_HIRES_VECTOR vertices[SHAPE3D_POLYGON_MAX_VERTICES];
				legacy_u32 vertex;
				for (vertex = 0; vertex < count; vertex++) {
					legacy_u16 index = indices[vertex];
					if (index >= transshapenumverts) {
						break;
					}
					if (context->vertex_clip_flags[index] == SHAPE3D_VERTEX_UNTRANSFORMED) {
						shape3d_cache_vertex(shape, context, index);
					}
					vertices[vertex] = context->hires_vertices[index];
				}
				if (vertex == count) {
					surface = shape3d_hires_shadow_polygon(vertices, count,
														   pattern_type == SHAPE3D_PATTERN_MASKED);
				}
			}
		}
		context->shadow_surfaces[context->shadow_surface_count++] = surface;
		primitive = indices + count;
	}
}
#endif

#if (defined(RESTUNTS_SDL3))
static legacy_s32 hypervision_bounds_are_clipped(const struct SHAPE3D *shape,
												 struct SHAPE3D_TRANSFORM_CONTEXT *context)
{
	/* Resource models begin with eight enclosing box corners. Tiny shapes can
	 * consist only of a sphere center/radius or wheel axes, which do not bound
	 * their coverage; let their projected primitive extents handle rejection. */
	if (shape->shape3d_numverts <= SHAPE3D_SHADOW_BOUNDS_VERTEX_COUNT) {
		return 0;
	}
	legacy_u8 common_clip_flags = SHAPE3D_ALL_RECT_CLIP_FLAGS;
	legacy_s32 any_front = 0;
	legacy_s32 any_behind = 0;
	/* Generated fractional meshes have no resource bounding-box prefix. Include
	 * all their vertices, since a visible corner can follow eight clipped ones. */
	legacy_u16 bounds_count = context->source_vertices != NULL ? shape->shape3d_numverts
															   : SHAPE3D_SHADOW_BOUNDS_VERTEX_COUNT;
	for (legacy_u16 index = 0; index < bounds_count; index++) {
		shape3d_cache_vertex(shape, context, index);
		if (context->vertex_clip_flags[index] != 0) {
			any_behind = 1;
		} else {
			any_front = 1;
			common_clip_flags &= shape3d_hires_clip_flags(&context->hires_vertices[index]);
			if (common_clip_flags == 0) {
				return 0;
			}
		}
	}
	/* A box crossing the near plane may expand into the view after clipping,
	 * even when every already-projected corner lies on one side of it. */
	return !any_front || (!any_behind && common_clip_flags != 0);
}
#endif

#if (defined(RESTUNTS_SDL3))
static legacy_s32 hypervision_primitive_vertices(const struct SHAPE3D *shape,
												 struct SHAPE3D_TRANSFORM_CONTEXT *context,
												 const legacy_u8 *indices, legacy_u16 count,
												 legacy_u8 type)
{
	legacy_u8 common_clip_flags = SHAPE3D_ALL_RECT_CLIP_FLAGS;
	legacy_s32 any_front = 0;
	legacy_s32 any_behind = 0;
	for (legacy_u16 vertex = 0; vertex < count; vertex++) {
		legacy_u16 index = indices[vertex];
		if (index >= shape->shape3d_numverts) {
			return 0;
		}
		if (context->vertex_clip_flags[index] == SHAPE3D_VERTEX_UNTRANSFORMED) {
			shape3d_cache_vertex(shape, context, index);
		}
		if (context->vertex_clip_flags[index] != 0) {
			any_behind = 1;
		} else {
			any_front = 1;
			if (common_clip_flags != 0) {
				common_clip_flags &= shape3d_hires_clip_flags(&context->hires_vertices[index]);
			}
		}
	}
	/* Polygon and line adapters clip intersections; round primitives need
	 * their complete defining axes in front of the near plane. */
	if (any_behind != 0 && type != RENDER_PRIMITIVE_POLYGON && type != RENDER_PRIMITIVE_LINE) {
		return 0;
	}
	if (type == RENDER_PRIMITIVE_SPHERE || type == RENDER_PRIMITIVE_WHEEL) {
		return any_front;
	}
	return any_front != 0 && (any_behind != 0 || common_clip_flags == 0);
}
#endif

#if (defined(RESTUNTS_SDL3))
static legacy_u16 hypervision_transform_and_queue(struct TRANSFORMEDSHAPE3D *instance,
												  const struct SHAPE3D_HIRES_VECTOR *vertices)
{
	const struct SHAPE3D *shape = instance->shapeptr;
	transshapenumverts = shape->shape3d_numverts;
	if (transshapenumverts > SHAPE3D_VERTEX_CAPACITY) {
		return 1;
	}
	struct SHAPE3D_TRANSFORM_CONTEXT context;
	shape3d_prepare_instance(instance, &context);
	context.source_vertices = vertices;
	legacy_s32 bounds_clipped = hypervision_bounds_are_clipped(shape, &context);
	shape3d_collect_shadow_polygons(instance, &context);
	if (bounds_clipped != 0) {
		return (legacy_u16)-1;
	}
	shape3d_prepare_visibility(instance, &context);
	legacy_s32 depth_mode = (transshapeflags & SHAPE3D_BACKGROUND_FLAG) != 0
								? SHAPE3D_HIRES_DEPTH_BACKGROUND
								: SHAPE3D_HIRES_DEPTH_SORTED;
	shape3d_hires_begin_shape(hypervision_scene_count, depth_mode);
	shape3d_hires_set_shadow_receiver(
		(transshapeflags & (SHAPE3D_NO_SHADOW_RECEIVE_FLAG | SHAPE3D_GHOST_FLAG)) == 0);
	const legacy_u8 *primitive = shape->shape3d_primitives;
	legacy_u32 initial_count = hypervision_scene_count;
	legacy_s32 parent_visible = 1;
	for (legacy_u16 primitive_index = 0; primitive_index < shape->shape3d_numprimitives;
		 primitive_index++) {
		legacy_u8 resource_type = primitive[0];
		if (resource_type == SHAPE3D_PRIMITIVE_EMPTY ||
			resource_type >= SHAPE3D_PRIMITIVE_TYPE_COUNT) {
			break;
		}
		legacy_u16 count = primidxcounttab[resource_type];
		legacy_u8 type = primtypetab[resource_type];
		legacy_u16 flags = primitive[1];
		const legacy_u8 *indices = primitive + SHAPE3D_PRIMITIVE_HEADER_SIZE + transshapenumpaints;
		legacy_s32 attached = (flags & SHAPE3D_PRIMITIVE_SKIP_DEPTH_SORT_FLAG) != 0;
		legacy_s32 visible =
			type != RENDER_PRIMITIVE_UNSUPPORTED && count != 0 && (!attached || parent_visible) &&
			(LEGACY_READ_U32_LE(shape->shape3d_visibility_masks +
								(size_t)primitive_index * SHAPE3D_VISIBILITY_MASK_SIZE) &
			 (legacy_u32)context.visibility_mask) != 0 &&
			hypervision_primitive_vertices(shape, &context, indices, count, type);
		if (visible != 0) {
			shape3d_hires_queue(hypervision_scene_count, type, count, indices,
								context.hires_vertices, flags);
			if (type == RENDER_PRIMITIVE_POLYGON) {
				legacy_s32 cull_backface =
					(flags & SHAPE3D_PRIMITIVE_ALWAYS_VISIBLE_FLAG) == 0 &&
					((legacy_u32)context.front_facing_mask &
					 LEGACY_READ_U32_LE(shape->shape3d_front_facing_masks +
										(size_t)primitive_index * SHAPE3D_VISIBILITY_MASK_SIZE)) ==
						0;
				visible = shape3d_hires_polygon_visible(hypervision_scene_count, cull_backface);
			} else if (type == RENDER_PRIMITIVE_SPHERE || type == RENDER_PRIMITIVE_WHEEL) {
				struct RECTANGLE bounds = {HIRES_WIDTH / HIRES_SCALE, 0, HIRES_HEIGHT / HIRES_SCALE,
										   0};
				shape3d_hires_update_bounds(hypervision_scene_count, type, &bounds);
				visible = bounds.left < bounds.right && bounds.top < bounds.bottom;
			}
		}
		if (visible != 0) {
			if (primitive_index < context.shadow_surface_count) {
				shape3d_hires_set_shadow_surface(hypervision_scene_count,
												 context.shadow_surfaces[primitive_index]);
			}
			if ((transshapeflags & SHAPE3D_USE_BOUNDING_RECT_FLAG) != 0) {
				shape3d_hires_update_bounds(hypervision_scene_count, type, transshaperectptr);
			}
			legacy_u16 material = primitive[SHAPE3D_PRIMITIVE_HEADER_SIZE + transshapematerial];
			if (material == BACKLIGHT_PAINT_DEFAULT) {
				material = backlights_paint_override;
			}
			if ((transshapeflags & SHAPE3D_BLACK_GRILLE_FLAG) != 0U &&
				type == RENDER_PRIMITIVE_POLYGON && material == SHAPE3D_STOCK_GRILLE_MATERIAL) {
				type |= RENDER_PRIMITIVE_BLACK_GRILLE_FLAG;
			}
			if ((transshapeflags & SHAPE3D_GHOST_FLAG) != 0) {
				type |= RENDER_PRIMITIVE_GHOST_FLAG;
			}
			hypervision_scene_reserve();
			hypervision_scene[hypervision_scene_count++] =
				(struct HYPERVISION_SCENE_PRIMITIVE){material, type};
		}
		if (!attached) {
			parent_visible = visible;
		}
		primitive = indices + count;
	}
	return hypervision_scene_count != initial_count ? 0 : (legacy_u16)-1;
}
#endif

#if (defined(RESTUNTS_SDL3))
legacy_u16 shape3d_hires_transform_and_queue(struct TRANSFORMEDSHAPE3D *instance,
											 const struct SHAPE3D_HIRES_VECTOR *vertices)
{
	return hypervision_transform_and_queue(instance, vertices);
}
#endif

#if (defined(RESTUNTS_SDL3))
void shape3d_hires_set_origin(const struct SHAPE3D_HIRES_VECTOR *origin)
{
	hypervision_origin = origin != NULL ? *origin : (struct SHAPE3D_HIRES_VECTOR){0};
}
#endif

legacy_u16 shape3d_transform_and_queue(struct TRANSFORMEDSHAPE3D *instance)
{
#if defined(RESTUNTS_SDL3)
	if (hires_enabled()) {
		return hypervision_transform_and_queue(instance, NULL);
	}
#endif
	if (polygon_buffer_full != 0) {
		return 1;
	}
#if defined(RESTUNTS_SDL3)
	polyinfo_reserve();
#endif
	transshapenumverts = instance->shapeptr->shape3d_numverts;
	/* Shape files store this count in one byte. Reject a damaged descriptor
	 * before it can overrun the fixed-size transformation work arrays. */
	if (transshapenumverts > SHAPE3D_VERTEX_CAPACITY) {
		return 1;
	}
	legacy_u8 far *visibility_masks = instance->shapeptr->shape3d_visibility_masks;
	legacy_u8 far *front_facing_masks = instance->shapeptr->shape3d_front_facing_masks;
	struct SHAPE3D_TRANSFORM_CONTEXT context;
	shape3d_prepare_instance(instance, &context);
	shape_polygon_predecessor = polygon_list_tail;
	polygon_insertion_cursor = polygon_list_tail;
	shape_polygon_count = 0;
	legacy_u16 bounds_clipped = shape3d_bounds_are_clipped(instance, &context);
#if defined(RESTUNTS_SDL3)
	shape3d_collect_shadow_polygons(instance, &context);
#endif
	if (bounds_clipped != 0) {
		return (legacy_u16)-1;
	}
	shape3d_prepare_visibility(instance, &context);
	transshapeprimitives = instance->shapeptr->shape3d_primitives;
#if defined(RESTUNTS_SDL3)
	legacy_u32 shadow_primitive = 0;
	legacy_s32 depth_mode = SHAPE3D_HIRES_DEPTH_SORTED;
	if ((transshapeflags & SHAPE3D_BACKGROUND_FLAG) != 0) {
		depth_mode = SHAPE3D_HIRES_DEPTH_BACKGROUND;
	} else if ((transshapeflags & SHAPE3D_NO_DEPTH_SORT_FLAG) != 0) {
		depth_mode = SHAPE3D_HIRES_DEPTH_ORDERED;
	}
	shape3d_hires_begin_shape(polyinfonumpolys, depth_mode);
	shape3d_hires_set_shadow_receiver(
		(transshapeflags & (SHAPE3D_NO_SHADOW_RECEIVE_FLAG | SHAPE3D_GHOST_FLAG)) == 0);
#endif

	legacy_s32 depth_sum;
	legacy_u16 queued_primitive_count = 0;
	legacy_u8 primitive_type;
	legacy_u16 any_vertex_behind;
	for (;;) {
		transshapeprimptr = transshapeprimitives + primidxcounttab[transshapeprimitives[0]] +
							transshapenumpaints + 2;
		legacy_u16 primitive_flags = transshapeprimitives[1];
		legacy_u16 primitive_visible = 0;
		if ((LEGACY_READ_U32_LE(visibility_masks) & (legacy_u32)context.visibility_mask) != 0UL) {
			transshapenumvertscopy = primidxcounttab[transshapeprimitives[0]];
			primitive_type = primtypetab[transshapeprimitives[0]];
#if defined(RESTUNTS_SDL3)
			polyinfo_reserve();
#endif
			transshapepolyinfo = polyinfoptr + polyinfoptrnext;
			polygon_record_offsets[polyinfonumpolys] = polyinfoptrnext;
			transprimitivepaintjob = transshapeprimitives[2 + transshapematerial];
			transshapeprimitives += 2 + transshapenumpaints;
			if (shape3d_prepare_primitive_vertices(instance->shapeptr, &context,
												   &any_vertex_behind) != 0) {
#if defined(RESTUNTS_SDL3)
				/* Visibility must use the same fractional projection as rasterization. */
				shape3d_hires_queue(polyinfonumpolys, primitive_type, transshapenumvertscopy,
									transshapeprimitives, context.hires_vertices, primitive_flags);
#endif
				primitive_visible =
					shape3d_prepare_primitive(&context, primitive_type, any_vertex_behind,
											  primitive_flags, front_facing_masks, &depth_sum);
#if defined(RESTUNTS_SDL3)
				if (primitive_visible != 0) {
					if (shadow_primitive < context.shadow_surface_count) {
						shape3d_hires_set_shadow_surface(polyinfonumpolys,
														 context.shadow_surfaces[shadow_primitive]);
					}
					if ((transshapeflags & SHAPE3D_USE_BOUNDING_RECT_FLAG) != 0) {
						shape3d_hires_update_bounds(polyinfonumpolys, primitive_type,
													transshaperectptr);
					}
				}
#endif
			}
		}

		transshapeprimitives = transshapeprimptr;
#if defined(RESTUNTS_SDL3)
		shadow_primitive++;
#endif
		visibility_masks += 4U;
		front_facing_masks += 4U;
		if (primitive_visible != 0) {
			queued_primitive_count = LEGACY_U16_WRAP_ADD(queued_primitive_count, 1U);
			if (shape3d_insert_primitive(primitive_type, primitive_flags, depth_sum) != 0) {
				return 1;
			}
		} else if ((primitive_flags & SHAPE3D_PRIMITIVE_SKIP_DEPTH_SORT_FLAG) == 0) {
			while (transshapeprimitives[0] != 0 &&
				   (transshapeprimitives[1] & SHAPE3D_PRIMITIVE_SKIP_DEPTH_SORT_FLAG) != 0) {
				transshapeprimitives +=
					primidxcounttab[transshapeprimitives[0]] + transshapenumpaints + 2;
#if defined(RESTUNTS_SDL3)
				shadow_primitive++;
#endif
				visibility_masks += 4U;
				front_facing_masks += 4U;
			}
		}
		if (transshapeprimitives[0] == 0) {
			return queued_primitive_count != 0 ? 0 : (legacy_u16)-1;
		}
	}
}
