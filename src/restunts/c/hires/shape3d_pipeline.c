#include <stddef.h>
#include <limits.h>
#if defined(RESTUNTS_SDL3)
#include <float.h>
#include <math.h>
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

#include "shape3d_pipeline.h"
#include "shape3d_mesh.h"
#include "../shape3d/shape3d_pipeline_internal.h"

#ifdef RESTUNTS_SDL3
#define SHAPE3D_HIRES_EVALUATE_AS_DECLARED 0
#define SHAPE3D_HIRES_EVALUATE_AS_DOUBLE 1
#define SHAPE3D_HIRES_IDENTITY_MAX_COORDINATE (DBL_MAX / TRIG_FIXED_ONE)
/* Conservative resource bounds include empty corners beyond the visible skin.
 * Limit the cheaper racing mesh to a small footprint in physical render pixels. */
#define SHAPE3D_HIRES_FAR_CAR_MAX_PIXELS 320.0

/* Keep native models on the existing inline scratch arrays. Extended model
 * storage belongs to this call, so geometry workers never share transform state. */
static struct SHAPE3D_HIRES_VECTOR *
shape3d_hires_transformed_vertices(struct SHAPE3D_HIRES_PIPELINE_CONTEXT *context)
{
	return context->hires.extended_vertices != NULL ? context->hires.extended_vertices
													: context->hires.hires_vertices;
}

static legacy_u8 *shape3d_hires_vertex_flags(struct SHAPE3D_HIRES_PIPELINE_CONTEXT *context)
{
	return context->extended_vertex_clip_flags != NULL ? context->extended_vertex_clip_flags
													   : context->vertex_clip_flags;
}

static legacy_u32 *shape3d_hires_shadow_surfaces(struct SHAPE3D_HIRES_PIPELINE_CONTEXT *context)
{
	return context->hires.extended_shadow_surfaces != NULL ? context->hires.extended_shadow_surfaces
														   : context->hires.shadow_surfaces;
}

static void shape3d_hires_free_model_scratch(struct SHAPE3D_HIRES_PIPELINE_CONTEXT *context)
{
	if (context->hires.extended_vertices != NULL) {
		free(context->hires.extended_vertices);
	}
	if (context->extended_vertex_clip_flags != NULL) {
		free(context->extended_vertex_clip_flags);
	}
	if (context->hires.extended_shadow_surfaces != NULL) {
		free(context->hires.extended_shadow_surfaces);
	}
}

static legacy_u8
shape3d_hires_allocate_model_scratch(const struct SHAPE3D *shape,
									 struct SHAPE3D_HIRES_PIPELINE_CONTEXT *context)
{
	context->hires.extended_vertices = NULL;
	context->extended_vertex_clip_flags = NULL;
	context->hires.extended_shadow_surfaces = NULL;
	if (shape->shape3d_numverts > SHAPE3D_HIRES_VERTEX_CAPACITY) {
		context->hires.extended_vertices =
			malloc((size_t)shape->shape3d_numverts * sizeof(*context->hires.extended_vertices));
		context->extended_vertex_clip_flags = malloc(shape->shape3d_numverts);
		if (context->hires.extended_vertices == NULL ||
			context->extended_vertex_clip_flags == NULL) {
			goto allocation_failed;
		}
	}
	if (shape->shape3d_numprimitives > SHAPE3D_SHADOW_PRIMITIVE_CAPACITY) {
		context->hires.extended_shadow_surfaces =
			malloc((size_t)shape->shape3d_numprimitives *
				   sizeof(*context->hires.extended_shadow_surfaces));
		if (context->hires.extended_shadow_surfaces == NULL) {
			goto allocation_failed;
		}
	}
	return 1;

allocation_failed:
	shape3d_hires_free_model_scratch(context);
	shape3d_hires_geometry_error("Cannot allocate HyperVision model scratch");
	return 0;
}

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
		/* Validated native vertex indices fit the packed resource offset. */
		const legacy_u8 *vertex = shape->shape3d_vertex_bytes + (size_t)index * SHAPE3D_VERTEX_SIZE;
		source =
			(struct SHAPE3D_HIRES_VECTOR){LEGACY_READ_S16_LE(vertex + SHAPE3D_VERTEX_X_OFFSET),
										  LEGACY_READ_S16_LE(vertex + SHAPE3D_VERTEX_Y_OFFSET),
										  LEGACY_READ_S16_LE(vertex + SHAPE3D_VERTEX_Z_OFFSET)};
	}
	legacy_f64 scale = shape_half_scale != 0 ? 0.5 : 1.0;
	source.x *= scale;
	source.y *= scale;
	source.z *= scale;
	struct SHAPE3D_HIRES_VECTOR world;
#if FLT_EVAL_METHOD == SHAPE3D_HIRES_EVALUATE_AS_DECLARED ||                                       \
	FLT_EVAL_METHOD == SHAPE3D_HIRES_EVALUATE_AS_DOUBLE
	if (context->hires.object_rotation_identity &&
		(context->hires.source_vertices == NULL ||
		 (fabs(source.x) <= SHAPE3D_HIRES_IDENTITY_MAX_COORDINATE &&
		  fabs(source.y) <= SHAPE3D_HIRES_IDENTITY_MAX_COORDINATE &&
		  fabs(source.z) <= SHAPE3D_HIRES_IDENTITY_MAX_COORDINATE))) {
		/* Native signed-word vertices always fit the exact scaling range.
		 * Scaling by the identity diagonal and back is exact in this range.
		 * Its three dot products have the same signed-zero terms; retain their
		 * sum so zero coordinates keep their original IEEE rounding behavior. */
		legacy_f64 zero = source.x * 0.0 + source.y * 0.0 + source.z * 0.0;
		world = (struct SHAPE3D_HIRES_VECTOR){source.x + zero, source.y + zero, source.z + zero};
	} else
#endif
	{
		shape3d_hires_rotate(&source, &context->hires.object_to_world_rotation, &world);
	}
	struct SHAPE3D_HIRES_VECTOR *view = &shape3d_hires_transformed_vertices(context)[index];
	if ((context->flags & SHAPE3D_PRETRANSFORMED_FLAG) != 0) {
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
		(context->flags &
		 (SHAPE3D_NO_SHADOW_RECEIVE_FLAG | SHAPE3D_GHOST_FLAG | SHAPE3D_BACKGROUND_FLAG)) != 0) {
		return;
	}
	const struct SHAPE3D *shape = instance->shapeptr;
	legacy_u32 bounds_count = context->vertex_count < SHAPE3D_SHADOW_BOUNDS_VERTEX_COUNT
								  ? context->vertex_count
								  : SHAPE3D_SHADOW_BOUNDS_VERTEX_COUNT;
	/* The camera bounds test can stop early or use only four flat corners. */
	for (legacy_u32 vertex = 0; vertex < bounds_count; vertex++) {
		if (shape3d_hires_vertex_flags(context)[vertex] == SHAPE3D_VERTEX_UNTRANSFORMED) {
			shape3d_hires_cache_vertex(shape, context, (legacy_u16)vertex);
		}
	}
	if (!shape3d_hires_shadow_overlap(shape3d_hires_transformed_vertices(context), bounds_count)) {
		return;
	}
	/* Shadow blockers retain their geometry even when camera visibility rejects them. */
	const legacy_u8 *primitive = shape->shape3d_primitives;
	while (context->hires.shadow_surface_count < shape->shape3d_numprimitives &&
		   primitive[0] != SHAPE3D_PRIMITIVE_EMPTY && primitive[0] < SHAPE3D_PRIMITIVE_TYPE_COUNT) {
		legacy_u32 surface = 0;
		legacy_u32 count = primidxcounttab[primitive[0]];
		const legacy_u8 *indices = primitive + SHAPE3D_PRIMITIVE_HEADER_SIZE + context->paint_count;
		if (primtypetab[primitive[0]] == RENDER_PRIMITIVE_POLYGON &&
			count <= SHAPE3D_POLYGON_MAX_VERTICES &&
			(primitive[SHAPE3D_SHADOW_PRIMITIVE_FLAGS_OFFSET] &
			 SHAPE3D_PRIMITIVE_SKIP_DEPTH_SORT_FLAG) == 0) {
			legacy_u16 material = primitive[SHAPE3D_PRIMITIVE_HEADER_SIZE + context->material];
			legacy_u16 pattern_type = shape3d_material_value(material_patlist_ptr_cpy, material);
			if (pattern_type <= SHAPE3D_PATTERN_TWO_COLOR &&
				(pattern_type != SHAPE3D_PATTERN_MASKED ||
				 shape3d_material_value(material_patlist2_ptr_cpy, material) != 0U)) {
				struct SHAPE3D_HIRES_VECTOR vertices[SHAPE3D_POLYGON_MAX_VERTICES];
				legacy_u32 vertex;
				for (vertex = 0; vertex < count; vertex++) {
					legacy_u16 index = hires_shape3d_vertex_index(shape, indices, vertex);
					if (index >= context->vertex_count) {
						break;
					}
					if (shape3d_hires_vertex_flags(context)[index] ==
						SHAPE3D_VERTEX_UNTRANSFORMED) {
						shape3d_hires_cache_vertex(shape, context, index);
					}
					vertices[vertex] = shape3d_hires_transformed_vertices(context)[index];
				}
				if (vertex == count) {
					surface = shape3d_hires_shadow_polygon(vertices, count,
														   pattern_type == SHAPE3D_PATTERN_MASKED);
				}
			}
		}
		shape3d_hires_shadow_surfaces(context)[context->hires.shadow_surface_count++] = surface;
		primitive = indices + count * hires_shape3d_index_size(shape);
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
		if (shape3d_hires_vertex_flags(context)[index] != 0) {
			any_behind = 1;
		} else {
			any_front = 1;
			common_clip_flags &= shape3d_hires_clip_prepared(
				&shape3d_hires_transformed_vertices(context)[index], &context->clip);
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
		legacy_u16 index = hires_shape3d_vertex_index(shape, indices, vertex);
		if (index >= shape->shape3d_numverts) {
			return 0;
		}
		if (shape3d_hires_vertex_flags(context)[index] == SHAPE3D_VERTEX_UNTRANSFORMED) {
			shape3d_hires_cache_vertex(shape, context, index);
		}
		if (shape3d_hires_vertex_flags(context)[index] != 0) {
			any_behind = 1;
		} else {
			any_front = 1;
			if (common_clip_flags != 0) {
				common_clip_flags &= shape3d_hires_clip_prepared(
					&shape3d_hires_transformed_vertices(context)[index], &context->clip);
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
	context->paint_count = instance->shapeptr->shape3d_numpaints;
	context->material = instance->material;
	if (context->material >= context->paint_count) {
		context->material = 0;
	}
	context->flags = instance->ts_flags;
	context->rectangle = (context->flags & SHAPE3D_USE_BOUNDING_RECT_FLAG) != 0
							 ? shape3d_hires_geometry_rectangle(instance->rectptr)
							 : NULL;
	for (legacy_u16 index = 0; index < context->vertex_count; index++) {
		shape3d_hires_vertex_flags(context)[index] = SHAPE3D_VERTEX_UNTRANSFORMED;
	}
	context->visibility_mask = -1;
	context->front_facing_mask = 0;
	/* Match mat_rot_zxy's ZXY products with call-local scratch matrices.
	 * Geometry workers share the view matrix but never rotation temporaries. */
	struct MATRIX rotation_z, rotation_x, rotation_y, rotation_temp, rotation;
	if (((instance->rotvec.x | instance->rotvec.y) & ANGLE_MASK) == 0) {
		/* Static track shapes and generated terrain need only yaw. Identity
		 * factors are exact in the legacy fixed-point matrix product. */
		mat_rot_y(&rotation, instance->rotvec.z);
	} else {
		mat_rot_z(&rotation_z, instance->rotvec.x);
		mat_rot_x(&rotation_x, instance->rotvec.y);
		mat_rot_y(&rotation_y, instance->rotvec.z);
		mat_multiply(&rotation_z, &rotation_x, &rotation_temp);
		mat_multiply(&rotation_temp, &rotation_y, &rotation);
	}
	shape3d_hires_prepare_instance(instance, context, &rotation);
	if (((instance->rotvec.x | instance->rotvec.y | instance->rotvec.z) & ANGLE_MASK) == 0) {
		context->object_to_view_rotation = mat_temp;
	} else {
		mat_multiply(&rotation, &mat_temp, &context->object_to_view_rotation);
	}
	if ((context->flags & SHAPE3D_PRETRANSFORMED_FLAG) != 0) {
		shape3d_hires_prepare_pretransformed(context);
	} else {
		shape3d_hires_prepare_view_translation(context);
	}
}

static void shape3d_hires_prepare_visibility(const struct TRANSFORMEDSHAPE3D *instance,
											 struct SHAPE3D_HIRES_PIPELINE_CONTEXT *context)
{
	if ((context->flags & SHAPE3D_PRETRANSFORMED_FLAG) != 0 ||
		shape3d_hires_visibility_reach(instance, context)) {
		return;
	}
	struct MATRIX inverse;
	mat_invert(&context->object_to_view_rotation, &inverse);
	struct VECTOR forward = {0, 0, SHAPE3D_FORWARD_VECTOR_SCALE};
	struct VECTOR direction;
	mat_mul_vector(&forward, &inverse, &direction);
	if (direction.y <= 0 || shape3d_hires_above_camera(context)) {
		context->view_sector = vector_direction_sector(&direction);
		context->visibility_mask = invpow2tbl[context->view_sector];
		context->front_facing_mask = invpow2tbl[context->view_sector];
	}
}

static legacy_s32 hires_shape3d_far_car_fits(const struct SHAPE3D *shape,
											 struct SHAPE3D_HIRES_PIPELINE_CONTEXT *context)
{
	legacy_f64 minimum_x = 0, maximum_x = 0, minimum_y = 0, maximum_y = 0;
	for (legacy_u16 index = 0; index < SHAPE3D_SHADOW_BOUNDS_VERTEX_COUNT; index++) {
		shape3d_hires_cache_vertex(shape, context, index);
		if (shape3d_hires_vertex_flags(context)[index] != 0) {
			return 0;
		}
		struct SHAPE3D_HIRES_POINT point;
		shape3d_hires_project(&shape3d_hires_transformed_vertices(context)[index], &point);
		if (index == 0) {
			minimum_x = maximum_x = point.x;
			minimum_y = maximum_y = point.y;
		} else {
			minimum_x = point.x < minimum_x ? point.x : minimum_x;
			maximum_x = point.x > maximum_x ? point.x : maximum_x;
			minimum_y = point.y < minimum_y ? point.y : minimum_y;
			maximum_y = point.y > maximum_y ? point.y : maximum_y;
		}
	}
	return maximum_x - minimum_x <= SHAPE3D_HIRES_FAR_CAR_MAX_PIXELS &&
		   maximum_y - minimum_y <= SHAPE3D_HIRES_FAR_CAR_MAX_PIXELS;
}

legacy_u16 hires_backend_transform_and_queue(struct TRANSFORMEDSHAPE3D *instance,
											 const struct SHAPE3D_HIRES_VECTOR *vertices)
{
	struct TRANSFORMEDSHAPE3D enhanced_instance;
	struct SHAPE3D *far_mesh = NULL;
	if (vertices == NULL) {
		far_mesh = hires_shape3d_car_far_mesh(instance->shapeptr);
		struct SHAPE3D *enhanced = hires_shape3d_car_mesh(instance->shapeptr);
		if (enhanced != instance->shapeptr) {
			enhanced_instance = *instance;
			enhanced_instance.shapeptr = enhanced;
			instance = &enhanced_instance;
		}
		vertices = hires_shape3d_car_vertices(instance->shapeptr);
	}
	const struct SHAPE3D *shape = instance->shapeptr;
	struct SHAPE3D_HIRES_PIPELINE_CONTEXT context;
	context.vertex_count = shape->shape3d_numverts;
	if (!shape3d_hires_allocate_model_scratch(shape, &context)) {
		return 1;
	}
	legacy_u16 result;
	shape3d_hires_prepare_transform(instance, &context);
	shape3d_hires_prepare_clip(&context.clip);
	context.hires.source_vertices = vertices;
	if (far_mesh != NULL && hires_shape3d_far_car_fits(shape, &context)) {
		enhanced_instance = *instance;
		enhanced_instance.shapeptr = far_mesh;
		instance = &enhanced_instance;
		shape = far_mesh;
		context.vertex_count = far_mesh->shape3d_numverts;
		context.hires.source_vertices = hires_shape3d_car_vertices(far_mesh);
		for (legacy_u16 index = 0; index < SHAPE3D_SHADOW_BOUNDS_VERTEX_COUNT; index++) {
			shape3d_hires_vertex_flags(&context)[index] = SHAPE3D_VERTEX_UNTRANSFORMED;
		}
	}
	legacy_s32 bounds_clipped = hires_backend_bounds_are_clipped(shape, &context);
	shape3d_hires_collect_shadow_polygons(instance, &context);
	if (bounds_clipped != 0) {
		result = (legacy_u16)-1;
		goto complete;
	}
	shape3d_hires_prepare_visibility(instance, &context);
	legacy_s32 depth_mode = (context.flags & SHAPE3D_BACKGROUND_FLAG) != 0
								? SHAPE3D_HIRES_DEPTH_BACKGROUND
								: SHAPE3D_HIRES_DEPTH_SORTED;
	shape3d_hires_begin_shape(hires_backend_scene_count, depth_mode);
	shape3d_hires_set_shadow_receiver(
		(context.flags & (SHAPE3D_NO_SHADOW_RECEIVE_FLAG | SHAPE3D_GHOST_FLAG)) == 0);
	const legacy_u8 *primitive = shape->shape3d_primitives;
	legacy_u32 initial_count = hires_backend_scene_count;
	legacy_s32 parent_visible = 1;
	for (legacy_u32 primitive_index = 0; primitive_index < shape->shape3d_numprimitives;
		 primitive_index++) {
		legacy_u8 resource_type = primitive[0];
		if (resource_type == SHAPE3D_PRIMITIVE_EMPTY ||
			resource_type >= SHAPE3D_PRIMITIVE_TYPE_COUNT) {
			break;
		}
		legacy_u16 count = primidxcounttab[resource_type];
		legacy_u8 type = primtypetab[resource_type];
		legacy_u16 flags = primitive[1];
		const legacy_u8 *indices = primitive + SHAPE3D_PRIMITIVE_HEADER_SIZE + context.paint_count;
		legacy_s32 attached = (flags & SHAPE3D_PRIMITIVE_SKIP_DEPTH_SORT_FLAG) != 0;
		legacy_s32 visible =
			type != RENDER_PRIMITIVE_UNSUPPORTED && count != 0 && (!attached || parent_visible) &&
			(LEGACY_READ_U32_LE(shape->shape3d_visibility_masks +
								(size_t)primitive_index * SHAPE3D_VISIBILITY_MASK_SIZE) &
			 (legacy_u32)context.visibility_mask) != 0 &&
			hires_backend_primitive_vertices(shape, &context, indices, count, type);
		if (visible != 0) {
			if (hires_shape3d_index_size(shape) == sizeof(legacy_u8)) {
				shape3d_hires_queue(hires_backend_scene_count, type, count, indices,
									shape3d_hires_transformed_vertices(&context), flags);
			} else {
				/* The rasterizer consumes one primitive at a time. Gather its
				 * wide global indices into the unchanged local byte-index API. */
				struct SHAPE3D_HIRES_VECTOR points[SHAPE3D_POLYGON_MAX_VERTICES];
				legacy_u8 local_indices[SHAPE3D_POLYGON_MAX_VERTICES];
				for (legacy_u16 vertex = 0; vertex < count; vertex++) {
					legacy_u16 index = hires_shape3d_vertex_index(shape, indices, vertex);
					points[vertex] = shape3d_hires_transformed_vertices(&context)[index];
					local_indices[vertex] = (legacy_u8)vertex;
				}
				shape3d_hires_queue(hires_backend_scene_count, type, count, local_indices, points,
									flags);
			}
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
				shape3d_hires_set_shadow_surface(
					hires_backend_scene_count,
					shape3d_hires_shadow_surfaces(&context)[primitive_index]);
			}
			if (context.rectangle != NULL) {
				shape3d_hires_update_bounds(hires_backend_scene_count, type, context.rectangle);
			}
			legacy_u16 material = primitive[SHAPE3D_PRIMITIVE_HEADER_SIZE + context.material];
			if (material == BACKLIGHT_PAINT_DEFAULT && shape3d_hires_thread_state == NULL) {
				material = backlights_paint_override;
			}
			if ((context.flags & SHAPE3D_BLACK_GRILLE_FLAG) != 0U &&
				type == RENDER_PRIMITIVE_POLYGON && material == SHAPE3D_STOCK_GRILLE_MATERIAL) {
				type |= RENDER_PRIMITIVE_BLACK_GRILLE_FLAG;
			}
			if ((context.flags & SHAPE3D_GHOST_FLAG) != 0) {
				type |= RENDER_PRIMITIVE_GHOST_FLAG;
			}
			if (!hires_backend_scene_reserve()) {
				result = 1;
				goto complete;
			}
			hires_backend_scene[hires_backend_scene_count++] =
				(struct HIRES_BACKEND_SCENE_PRIMITIVE){material, type};
		}
		if (!attached) {
			parent_visible = visible;
		}
		primitive = indices + count * hires_shape3d_index_size(shape);
	}
	result = hires_backend_scene_count != initial_count ? 0 : (legacy_u16)-1;

complete:
	shape3d_hires_free_model_scratch(&context);
	return result;
}

legacy_u16 shape3d_hires_transform_and_queue(struct TRANSFORMEDSHAPE3D *instance,
											 const struct SHAPE3D_HIRES_VECTOR *vertices)
{
	return hires_backend_transform_and_queue(instance, vertices);
}

void shape3d_hires_set_origin(const struct SHAPE3D_HIRES_VECTOR *origin)
{
	shape3d_hires_state.geometry_origin =
		origin != NULL ? *origin : (struct SHAPE3D_HIRES_VECTOR){0};
}

void shape3d_hires_prepare_instance(const struct TRANSFORMEDSHAPE3D *instance,
									struct SHAPE3D_HIRES_PIPELINE_CONTEXT *context,
									const struct MATRIX *object_rotation)
{
	context->hires.shadow_surface_count = 0;
	context->hires.source_vertices = NULL;
	context->hires.object_to_world_rotation = *object_rotation;
	context->hires.object_rotation_identity =
		object_rotation->m._11 == TRIG_FIXED_ONE && object_rotation->m._22 == TRIG_FIXED_ONE &&
		object_rotation->m._33 == TRIG_FIXED_ONE && object_rotation->m._12 == 0 &&
		object_rotation->m._13 == 0 && object_rotation->m._21 == 0 && object_rotation->m._23 == 0 &&
		object_rotation->m._31 == 0 && object_rotation->m._32 == 0;
	context->hires.object_translation =
		(struct SHAPE3D_HIRES_VECTOR){instance->pos.x + shape3d_hires_state.geometry_origin.x,
									  instance->pos.y + shape3d_hires_state.geometry_origin.y,
									  instance->pos.z + shape3d_hires_state.geometry_origin.z};
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
	shape3d_hires_vertex_flags(context)[index] =
		shape3d_hires_transformed_vertices(context)[index].z < SHAPE3D_NEAR_CLIP_Z;
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
