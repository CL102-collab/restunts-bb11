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
#include "../shape3d/shape3d.h"
#include "../shape2d/shape2d_internal.h"
#include "../shape3d/shape3d_internal.h"
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

#include "../shape3d/shape3d_pipeline_internal.h"

#ifdef RESTUNTS_SDL3
static struct SHAPE3D_HIRES_VECTOR hires_backend_origin;
static void shape3d_hires_rotate(const struct SHAPE3D_HIRES_VECTOR *source,
								 const struct MATRIX *matrix, struct SHAPE3D_HIRES_VECTOR *result);
static void shape3d_hires_transform_vertex(const struct SHAPE3D *shape, legacy_u16 index,
										   struct SHAPE3D_HIRES_PIPELINE_CONTEXT *context);
void shape3d_hires_collect_shadow_polygons(const struct TRANSFORMEDSHAPE3D *instance,
										   struct SHAPE3D_HIRES_PIPELINE_CONTEXT *context);
static legacy_s32 hires_backend_bounds_are_clipped(const struct SHAPE3D *shape,
												   struct SHAPE3D_HIRES_PIPELINE_CONTEXT *context);
static legacy_s32 hires_backend_primitive_vertices(const struct SHAPE3D *shape,
												   struct SHAPE3D_HIRES_PIPELINE_CONTEXT *context,
												   const legacy_u8 *indices, legacy_u16 count,
												   legacy_u8 type);
legacy_u16 hires_backend_transform_and_queue(struct TRANSFORMEDSHAPE3D *instance,
											 const struct SHAPE3D_HIRES_VECTOR *vertices);
legacy_u16 shape3d_hires_transform_and_queue(struct TRANSFORMEDSHAPE3D *instance,
											 const struct SHAPE3D_HIRES_VECTOR *vertices);
void shape3d_hires_set_origin(const struct SHAPE3D_HIRES_VECTOR *origin);

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

static void shape3d_hires_transform_vertex(const struct SHAPE3D *shape, legacy_u16 index,
										   struct SHAPE3D_HIRES_PIPELINE_CONTEXT *context)
{
	struct SHAPE3D_HIRES_VECTOR source;
	if (context->hires.source_vertices != NULL) {
		source = context->hires.source_vertices[index];
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
	shape3d_hires_rotate(&source, &context->hires.object_to_world_rotation, &world);
	struct SHAPE3D_HIRES_VECTOR *view = &context->hires.hires_vertices[index];
	if ((transshapeflags & SHAPE3D_PRETRANSFORMED_FLAG) != 0) {
		shape3d_hires_rotate(&world, &mat_temp, view);
		view->x += context->hires.object_translation.x;
		view->y += context->hires.object_translation.y;
		view->z += context->hires.object_translation.z;
	} else {
		/* Adjacent shapes can express one world vertex using different local
		 * coordinates. Combine position before the camera transform so both
		 * reach exactly the same screen position, without per-shape rounding. */
		world.x += context->hires.object_translation.x;
		world.y += context->hires.object_translation.y;
		world.z += context->hires.object_translation.z;
		shape3d_hires_rotate(&world, &mat_temp, view);
	}
}

void shape3d_hires_collect_shadow_polygons(const struct TRANSFORMEDSHAPE3D *instance,
										   struct SHAPE3D_HIRES_PIPELINE_CONTEXT *context)
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
			shape3d_hires_cache_vertex(shape, context, (legacy_u16)vertex);
		}
	}
	if (!shape3d_hires_shadow_overlap(context->hires.hires_vertices, bounds_count)) {
		return;
	}
	/* Shadow blockers retain their geometry even when camera visibility rejects them. */
	const legacy_u8 *primitive = shape->shape3d_primitives;
	while (primitive[0] != 0 &&
		   context->hires.shadow_surface_count < SHAPE3D_SHADOW_PRIMITIVE_CAPACITY) {
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
						shape3d_hires_cache_vertex(shape, context, index);
					}
					vertices[vertex] = context->hires.hires_vertices[index];
				}
				if (vertex == count) {
					surface = shape3d_hires_shadow_polygon(vertices, count,
														   pattern_type == SHAPE3D_PATTERN_MASKED);
				}
			}
		}
		context->hires.shadow_surfaces[context->hires.shadow_surface_count++] = surface;
		primitive = indices + count;
	}
}

static legacy_s32 hires_backend_bounds_are_clipped(const struct SHAPE3D *shape,
												   struct SHAPE3D_HIRES_PIPELINE_CONTEXT *context)
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
	legacy_u16 bounds_count = context->hires.source_vertices != NULL
								  ? shape->shape3d_numverts
								  : SHAPE3D_SHADOW_BOUNDS_VERTEX_COUNT;
	for (legacy_u16 index = 0; index < bounds_count; index++) {
		shape3d_hires_cache_vertex(shape, context, index);
		if (context->vertex_clip_flags[index] != 0) {
			any_behind = 1;
		} else {
			any_front = 1;
			common_clip_flags &= shape3d_hires_clip_flags(&context->hires.hires_vertices[index]);
			if (common_clip_flags == 0) {
				return 0;
			}
		}
	}
	/* A box crossing the near plane may expand into the view after clipping,
	 * even when every already-projected corner lies on one side of it. */
	return !any_front || (!any_behind && common_clip_flags != 0);
}

static legacy_s32 hires_backend_primitive_vertices(const struct SHAPE3D *shape,
												   struct SHAPE3D_HIRES_PIPELINE_CONTEXT *context,
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
			shape3d_hires_cache_vertex(shape, context, index);
		}
		if (context->vertex_clip_flags[index] != 0) {
			any_behind = 1;
		} else {
			any_front = 1;
			if (common_clip_flags != 0) {
				common_clip_flags &=
					shape3d_hires_clip_flags(&context->hires.hires_vertices[index]);
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

static void shape3d_hires_prepare_transform(struct TRANSFORMEDSHAPE3D *instance,
											struct SHAPE3D_HIRES_PIPELINE_CONTEXT *context)
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
	for (legacy_u16 index = 0; index < transshapenumverts; index++) {
		context->vertex_clip_flags[index] = SHAPE3D_VERTEX_UNTRANSFORMED;
	}
	context->visibility_mask = -1;
	context->front_facing_mask = 0;
	struct MATRIX *rotation = mat_rot_zxy(instance->rotvec.x, instance->rotvec.y,
										  instance->rotvec.z, MATRIX_ROTATION_ORDER_ZXY);
	shape3d_hires_prepare_instance(instance, context, rotation);
	mat_multiply(rotation, &mat_temp, &context->object_to_view_rotation);
	if ((transshapeflags & SHAPE3D_PRETRANSFORMED_FLAG) != 0) {
		shape3d_hires_prepare_pretransformed(context);
	} else {
		shape3d_hires_prepare_view_translation(context);
	}
}

static void shape3d_hires_prepare_visibility(const struct TRANSFORMEDSHAPE3D *instance,
											 struct SHAPE3D_HIRES_PIPELINE_CONTEXT *context)
{
	if ((transshapeflags & SHAPE3D_PRETRANSFORMED_FLAG) != 0 ||
		shape3d_hires_visibility_reach(instance, context)) {
		return;
	}
	struct MATRIX inverse;
	mat_invert(&context->object_to_view_rotation, &inverse);
	struct VECTOR forward = {0, 0, SHAPE3D_FORWARD_VECTOR_SCALE};
	struct VECTOR direction;
	mat_mul_vector(&forward, &inverse, &direction);
	if (direction.y <= 0 || shape3d_hires_above_camera(context)) {
		shape_view_direction_sector = vector_direction_sector(&direction);
		context->visibility_mask = invpow2tbl[shape_view_direction_sector];
		context->front_facing_mask = invpow2tbl[shape_view_direction_sector];
	}
}

legacy_u16 hires_backend_transform_and_queue(struct TRANSFORMEDSHAPE3D *instance,
											 const struct SHAPE3D_HIRES_VECTOR *vertices)
{
	const struct SHAPE3D *shape = instance->shapeptr;
	transshapenumverts = shape->shape3d_numverts;
	if (transshapenumverts > SHAPE3D_VERTEX_CAPACITY) {
		return 1;
	}
	struct SHAPE3D_HIRES_PIPELINE_CONTEXT context;
	shape3d_hires_prepare_transform(instance, &context);
	context.hires.source_vertices = vertices;
	legacy_s32 bounds_clipped = hires_backend_bounds_are_clipped(shape, &context);
	shape3d_hires_collect_shadow_polygons(instance, &context);
	if (bounds_clipped != 0) {
		return (legacy_u16)-1;
	}
	shape3d_hires_prepare_visibility(instance, &context);
	legacy_s32 depth_mode = (transshapeflags & SHAPE3D_BACKGROUND_FLAG) != 0
								? SHAPE3D_HIRES_DEPTH_BACKGROUND
								: SHAPE3D_HIRES_DEPTH_SORTED;
	shape3d_hires_begin_shape(hires_backend_scene_count, depth_mode);
	shape3d_hires_set_shadow_receiver(
		(transshapeflags & (SHAPE3D_NO_SHADOW_RECEIVE_FLAG | SHAPE3D_GHOST_FLAG)) == 0);
	const legacy_u8 *primitive = shape->shape3d_primitives;
	legacy_u32 initial_count = hires_backend_scene_count;
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
			hires_backend_primitive_vertices(shape, &context, indices, count, type);
		if (visible != 0) {
			shape3d_hires_queue(hires_backend_scene_count, type, count, indices,
								context.hires.hires_vertices, flags);
			if (type == RENDER_PRIMITIVE_POLYGON) {
				legacy_s32 cull_backface =
					(flags & SHAPE3D_PRIMITIVE_ALWAYS_VISIBLE_FLAG) == 0 &&
					((legacy_u32)context.front_facing_mask &
					 LEGACY_READ_U32_LE(shape->shape3d_front_facing_masks +
										(size_t)primitive_index * SHAPE3D_VISIBILITY_MASK_SIZE)) ==
						0;
				visible = shape3d_hires_polygon_visible(hires_backend_scene_count, cull_backface);
			} else if (type == RENDER_PRIMITIVE_SPHERE || type == RENDER_PRIMITIVE_WHEEL) {
				struct RECTANGLE bounds = {HIRES_WIDTH / HIRES_SCALE, 0, HIRES_HEIGHT / HIRES_SCALE,
										   0};
				shape3d_hires_update_bounds(hires_backend_scene_count, type, &bounds);
				visible = bounds.left < bounds.right && bounds.top < bounds.bottom;
			}
		}
		if (visible != 0) {
			if (primitive_index < context.hires.shadow_surface_count) {
				shape3d_hires_set_shadow_surface(hires_backend_scene_count,
												 context.hires.shadow_surfaces[primitive_index]);
			}
			if ((transshapeflags & SHAPE3D_USE_BOUNDING_RECT_FLAG) != 0) {
				shape3d_hires_update_bounds(hires_backend_scene_count, type, transshaperectptr);
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
			hires_backend_scene_reserve();
			hires_backend_scene[hires_backend_scene_count++] =
				(struct HIRES_BACKEND_SCENE_PRIMITIVE){material, type};
		}
		if (!attached) {
			parent_visible = visible;
		}
		primitive = indices + count;
	}
	return hires_backend_scene_count != initial_count ? 0 : (legacy_u16)-1;
}

legacy_u16 shape3d_hires_transform_and_queue(struct TRANSFORMEDSHAPE3D *instance,
											 const struct SHAPE3D_HIRES_VECTOR *vertices)
{
	return hires_backend_transform_and_queue(instance, vertices);
}

void shape3d_hires_set_origin(const struct SHAPE3D_HIRES_VECTOR *origin)
{
	hires_backend_origin = origin != NULL ? *origin : (struct SHAPE3D_HIRES_VECTOR){0};
}

void shape3d_hires_prepare_instance(const struct TRANSFORMEDSHAPE3D *instance,
									struct SHAPE3D_HIRES_PIPELINE_CONTEXT *context,
									const struct MATRIX *object_rotation)
{
	context->hires.shadow_surface_count = 0;
	context->hires.source_vertices = NULL;
	context->hires.object_to_world_rotation = *object_rotation;
	context->hires.object_translation = (struct SHAPE3D_HIRES_VECTOR){
		instance->pos.x + hires_backend_origin.x, instance->pos.y + hires_backend_origin.y,
		instance->pos.z + hires_backend_origin.z};
}
void shape3d_hires_prepare_pretransformed(struct SHAPE3D_HIRES_PIPELINE_CONTEXT *context)
{
	context->hires.view_origin = context->hires.object_translation;
}
void shape3d_hires_prepare_view_translation(struct SHAPE3D_HIRES_PIPELINE_CONTEXT *context)
{
	shape3d_hires_rotate(&context->hires.object_translation, &mat_temp,
						 &context->hires.view_origin);
}
legacy_s32 shape3d_hires_visibility_reach(const struct TRANSFORMEDSHAPE3D *instance,
										  const struct SHAPE3D_HIRES_PIPELINE_CONTEXT *context)
{
	legacy_f64 reach = (legacy_f64)instance->culling_distance * 2;
	return context->hires.view_origin.x > -reach && context->hires.view_origin.x < reach &&
		   context->hires.view_origin.z > -reach && context->hires.view_origin.z < reach;
}
legacy_s32 shape3d_hires_above_camera(const struct SHAPE3D_HIRES_PIPELINE_CONTEXT *context)
{
	return context->hires.object_translation.y >= 0;
}
void shape3d_hires_cache_vertex(const struct SHAPE3D *shape,
								struct SHAPE3D_HIRES_PIPELINE_CONTEXT *context, legacy_u16 index)
{
	shape3d_hires_transform_vertex(shape, index, context);
	context->vertex_clip_flags[index] =
		context->hires.hires_vertices[index].z < SHAPE3D_NEAR_CLIP_Z;
}
legacy_u8 shape3d_hires_vertex_rect_flags(const struct SHAPE3D_HIRES_PIPELINE_CONTEXT *context,
										  legacy_u16 index)
{
	return shape3d_hires_clip_flags(&context->hires.hires_vertices[index]);
}
legacy_s16 shape3d_hires_try_transform(struct TRANSFORMEDSHAPE3D *instance,
									   legacy_u16 *hires_result)
{
	if (!hires_enabled()) {
		return 0;
	}
	*hires_result = hires_backend_transform_and_queue(instance, NULL);
	return 1;
}

legacy_u32 shape3d_hires_queued_primitive_count(legacy_u32 classic_count)
{
	return hires_enabled() ? shape3d_hires_scene_count() : classic_count;
}

#endif
