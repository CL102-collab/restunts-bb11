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
#include "../math/math_internal.h"
#include "../car/car_model.h"
#include "../math/projection.h"
#include "../residue.h"
#if defined(RESTUNTS_SDL3)
#undef memcpy
#include "../fatal.h"
#include "../hires/hires.h"
#include "../hires/shape3d_hires.h"
#endif

#include "shape3d_pipeline_internal.h"

void shape3d_vertex_read(const struct SHAPE3D *shape, legacy_u16 index, struct VECTOR *destination);
void shape3d_vertex_write(struct SHAPE3D *shape, legacy_u16 index, const struct VECTOR *source);
static void shape3d_transform_vertex(const struct SHAPE3D *shape, legacy_u16 index,
									 legacy_s16 half_scale, struct MATRIX *matrix,
									 struct VECTOR *translation, struct VECTOR *transformed);
void shape3d_prepare_instance(struct TRANSFORMEDSHAPE3D *instance,
							  struct SHAPE3D_TRANSFORM_CONTEXT *context);
void shape3d_prepare_visibility(struct TRANSFORMEDSHAPE3D *instance,
								struct SHAPE3D_TRANSFORM_CONTEXT *context);
void shape3d_cache_vertex(const struct SHAPE3D *shape, struct SHAPE3D_TRANSFORM_CONTEXT *context,
						  legacy_u16 index);
legacy_u8 shape3d_vertex_rect_flags(struct SHAPE3D_TRANSFORM_CONTEXT *context, legacy_u16 index);
static legacy_u16 shape3d_bounds_are_clipped(struct TRANSFORMEDSHAPE3D *instance,
											 struct SHAPE3D_TRANSFORM_CONTEXT *context);
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

void shape3d_prepare_instance(struct TRANSFORMEDSHAPE3D *instance,
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
	shape3d_hires_prepare_instance(instance, context, object_rotation);
#endif
	if ((transshapeflags & SHAPE3D_PRETRANSFORMED_FLAG) != 0) {
		mat_multiply(object_rotation, &mat_temp, &context->object_to_view_rotation);
		context->view_translation = instance->pos;
#if defined(RESTUNTS_SDL3)
		shape3d_hires_prepare_pretransformed(context);
#endif
		return;
	}
#if defined(RESTUNTS_SDL3)
	if (hires_enabled()) {
		shape3d_hires_prepare_view_translation(context);
	} else
#endif
	{
		mat_mul_vector(&instance->pos, &mat_temp, &context->view_translation);
	}
	mat_multiply(object_rotation, &mat_temp, &context->object_to_view_rotation);
}

void shape3d_prepare_visibility(struct TRANSFORMEDSHAPE3D *instance,
								struct SHAPE3D_TRANSFORM_CONTEXT *context)
{
	if ((transshapeflags & SHAPE3D_PRETRANSFORMED_FLAG) != 0) {
		return;
	}
#if defined(RESTUNTS_SDL3)
	if (hires_enabled()) {
		if (shape3d_hires_visibility_reach(instance, context)) {
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
		above_camera = shape3d_hires_above_camera(context);
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
		shape3d_hires_cache_vertex(shape, context, index);
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
		return shape3d_hires_vertex_rect_flags(context, index);
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
	shape3d_hires_begin_legacy_shape(&context);
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
				shape3d_hires_queue_legacy_primitive(&context, primitive_type, primitive_flags);
#endif
				primitive_visible =
					shape3d_prepare_primitive(&context, primitive_type, any_vertex_behind,
											  primitive_flags, front_facing_masks, &depth_sum);
#if defined(RESTUNTS_SDL3)
				shape3d_hires_finish_legacy_primitive(&context, primitive_type, primitive_visible);
#endif
			}
		}

		transshapeprimitives = transshapeprimptr;
#if defined(RESTUNTS_SDL3)
		shape3d_hires_next_legacy_primitive(&context);
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
				shape3d_hires_next_legacy_primitive(&context);
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
