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
#endif

#include "shape3d_pipeline_internal.h"

static legacy_u16 shape3d_average_depth(legacy_s32 sum, legacy_u16 vertex_count);
static void polyinfo_emit_point(legacy_u16 *point_index, legacy_u8 *rect_flags,
								struct POINT2D *point);
static legacy_s8 polyinfo_is_facing_camera(const legacy_u8 far *record);
legacy_u16 shape3d_prepare_primitive_vertices(const struct SHAPE3D *shape,
											  struct SHAPE3D_TRANSFORM_CONTEXT *context,
											  legacy_u16 *any_vertex_behind);
static void shape3d_project_near_intersection(struct SHAPE3D_TRANSFORM_CONTEXT *context,
											  legacy_u16 front_index, legacy_u16 behind_index,
											  struct POINT2D *point);
static void shape3d_emit_polygon_intersection(struct SHAPE3D_TRANSFORM_CONTEXT *context,
											  legacy_u16 front_index, legacy_u16 behind_index,
											  legacy_u16 *point_count, legacy_u8 *clip_flags);
static void shape3d_emit_clipped_polygon_edge(struct SHAPE3D_TRANSFORM_CONTEXT *context,
											  legacy_u16 previous_index, legacy_u16 vertex_index,
											  legacy_u16 *point_count, legacy_u8 *clip_flags,
											  struct POINT2D *projected_point);
static legacy_u8 shape3d_emit_polygon(struct SHAPE3D_TRANSFORM_CONTEXT *context,
									  legacy_u16 any_vertex_behind, legacy_s32 *depth_sum);
static void shape3d_adjust_polygon_bounds(void);
static legacy_u16 shape3d_prepare_polygon(struct SHAPE3D_TRANSFORM_CONTEXT *context,
										  legacy_u16 any_vertex_behind, legacy_u16 primitive_flags,
										  const legacy_u8 far *front_facing_masks,
										  legacy_s32 *depth_sum);
static legacy_u16 shape3d_prepare_line(struct SHAPE3D_TRANSFORM_CONTEXT *context,
									   legacy_s32 *depth_sum);
static void shape3d_adjust_round_bounds(const struct POINT2D *center, legacy_u16 radius,
										legacy_s16 padding);
static legacy_u16 shape3d_prepare_wheel(struct SHAPE3D_TRANSFORM_CONTEXT *context,
										legacy_u16 any_vertex_behind, legacy_s32 *depth_sum);
static void shape3d_adjust_sphere_bounds(const struct POINT2D *center, legacy_u16 radius);
static legacy_u16 shape3d_prepare_sphere(struct SHAPE3D_TRANSFORM_CONTEXT *context,
										 legacy_s32 *depth_sum);
static legacy_u16 shape3d_prepare_point(struct SHAPE3D_TRANSFORM_CONTEXT *context,
										legacy_s32 *depth_sum);
legacy_u16 shape3d_prepare_primitive(struct SHAPE3D_TRANSFORM_CONTEXT *context,
									 legacy_u8 primitive_type, legacy_u16 any_vertex_behind,
									 legacy_u16 primitive_flags,
									 const legacy_u8 far *front_facing_masks,
									 legacy_s32 *depth_sum);
legacy_u16 shape3d_insert_primitive(legacy_u8 primitive_type, legacy_u16 primitive_flags,
									legacy_s32 depth_sum);

static legacy_u16 shape3d_average_depth(legacy_s32 sum, legacy_u16 vertex_count)
{
	switch (vertex_count) {
		case 1U:
			return (legacy_u16)sum;
		case 2U:
			return (legacy_u16)LEGACY_S32_SAR(sum, 1U);
		case 4U:
			return (legacy_u16)LEGACY_S32_SAR(sum, 2U);
		case 8U:
			return (legacy_u16)LEGACY_S32_SAR(sum, 3U);
		default:
			/* The original calls __aFuldiv even for negative depth sums.
			 * Preserve that unsigned division; powers of two use signed shifts. */
			return (legacy_u16)LEGACY_U32_DIV_OR_ZERO((legacy_u32)sum, vertex_count);
	}
}

static void polyinfo_emit_point(legacy_u16 *point_index, legacy_u8 *rect_flags,
								struct POINT2D *point)
{
	polyinfo_write_point(transshapepolyinfo, *point_index, point);
	if (*rect_flags != 0) {
		*rect_flags &= shape3d_geometry_clip_flags(point);
	}
	*point_index = LEGACY_U16_WRAP_ADD(*point_index, 1U);
}

static legacy_s8 polyinfo_is_facing_camera(const legacy_u8 far *record)
{
	struct POINT2D points[3];
	for (legacy_u16 index = 0; index < 3U; index++) {
		polyinfo_read_point(record, index, &points[index]);
	}
	return is_facing_camera(points);
}

legacy_u16 shape3d_prepare_primitive_vertices(const struct SHAPE3D *shape,
											  struct SHAPE3D_TRANSFORM_CONTEXT *context,
											  legacy_u16 *any_vertex_behind)
{
	legacy_u8 common_clip_flags = SHAPE3D_ALL_RECT_CLIP_FLAGS;
	*any_vertex_behind = 0;
	transshapeprimindexptr = transshapeprimitives;
	legacy_u16 all_vertices_behind = 1;
	for (legacy_u16 vertex_count = 0; vertex_count < transshapenumvertscopy;
		 vertex_count = LEGACY_U16_WRAP_ADD(vertex_count, 1U)) {
		legacy_u16 vertex_index = transshapeprimindexptr[0];
		transshapeprimindexptr++;
		polyvertpointptrtab[vertex_count] = &context->projected_vertices[vertex_index];
		if (context->vertex_clip_flags[vertex_index] == SHAPE3D_VERTEX_UNTRANSFORMED) {
			shape3d_cache_vertex(shape, context, vertex_index);
		}
		if (context->vertex_clip_flags[vertex_index] == 0) {
			all_vertices_behind = 0;
			if (common_clip_flags != 0) {
				common_clip_flags &= shape3d_vertex_rect_flags(context, vertex_index);
			}
		} else if (context->vertex_clip_flags[vertex_index] == 1) {
			*any_vertex_behind = 1;
		}
	}
	return all_vertices_behind == 0 && (common_clip_flags == 0 || *any_vertex_behind != 0);
}

static void shape3d_project_near_intersection(struct SHAPE3D_TRANSFORM_CONTEXT *context,
											  legacy_u16 front_index, legacy_u16 behind_index,
											  struct POINT2D *point)
{
#if defined(RESTUNTS_TEMPLEOS)
	shape3d_native_near_intersection(&context->view_vertices[front_index],
									 &context->view_vertices[behind_index], point);
#else
	struct VECTOR intersection;

	vector_interpolate_at_z(&context->view_vertices[front_index],
							&context->view_vertices[behind_index], &intersection,
							SHAPE3D_NEAR_CLIP_Z);
	shape3d_project_point(&intersection, point);
#endif
}

static void shape3d_emit_polygon_intersection(struct SHAPE3D_TRANSFORM_CONTEXT *context,
											  legacy_u16 front_index, legacy_u16 behind_index,
											  legacy_u16 *point_count, legacy_u8 *clip_flags)
{
	struct POINT2D point;

	shape3d_project_near_intersection(context, front_index, behind_index, &point);
	if (point.px != context->projected_vertices[front_index].px ||
		point.py != context->projected_vertices[front_index].py) {
		polyinfo_emit_point(point_count, clip_flags, &point);
	}
}

static void shape3d_emit_clipped_polygon_edge(struct SHAPE3D_TRANSFORM_CONTEXT *context,
											  legacy_u16 previous_index, legacy_u16 vertex_index,
											  legacy_u16 *point_count, legacy_u8 *clip_flags,
											  struct POINT2D *projected_point)
{
	if (context->vertex_clip_flags[vertex_index] != 0) {
		if (context->vertex_clip_flags[previous_index] == 0) {
			shape3d_emit_polygon_intersection(context, previous_index, vertex_index, point_count,
											  clip_flags);
		}
	} else {
		if (context->vertex_clip_flags[previous_index] != 0) {
			shape3d_emit_polygon_intersection(context, vertex_index, previous_index, point_count,
											  clip_flags);
		}
		polyinfo_emit_point(point_count, clip_flags, projected_point);
	}
}

static legacy_u8 shape3d_emit_polygon(struct SHAPE3D_TRANSFORM_CONTEXT *context,
									  legacy_u16 any_vertex_behind, legacy_s32 *depth_sum)
{
	transshapeprimindexptr = transshapeprimitives;
	*depth_sum = 0;
	legacy_u8 common_clip_flags = SHAPE3D_ALL_RECT_CLIP_FLAGS;
	legacy_u16 previous_vertex_index = 0;
	if (any_vertex_behind != 0) {
		previous_vertex_index = transshapeprimitives[transshapenumvertscopy - 1];
	}
	legacy_u16 output_point_index = 0U;
	for (legacy_u16 i = 0; i < transshapenumvertscopy; i = LEGACY_U16_WRAP_ADD(i, 1U)) {
		legacy_u16 vertex_index = transshapeprimindexptr[0];
		transshapeprimindexptr++;
		*depth_sum = LEGACY_S32_WRAP_ADD_S16(*depth_sum, context->view_vertices[vertex_index].z);
		if (any_vertex_behind == 0) {
			polyinfo_emit_point(&output_point_index, &common_clip_flags, polyvertpointptrtab[i]);
		} else {
			shape3d_emit_clipped_polygon_edge(context, previous_vertex_index, vertex_index,
											  &output_point_index, &common_clip_flags,
											  polyvertpointptrtab[i]);
			previous_vertex_index = vertex_index;
		}
	}
	if (any_vertex_behind != 0) {
		transshapenumvertscopy = output_point_index;
	}
	return common_clip_flags;
}

static void shape3d_adjust_polygon_bounds(void)
{
	struct POINT2D point;
	for (legacy_u16 i = 0; i < transshapenumvertscopy; i = LEGACY_U16_WRAP_ADD(i, 1U)) {
		polyinfo_read_point(transshapepolyinfo, i, &point);
#if defined(RESTUNTS_TEMPLEOS)
		point.px = shape3d_logical_coordinate(point.px);
		point.py = shape3d_logical_coordinate(point.py);
#endif
		if (point.px < transshaperectptr->left) {
			transshaperectptr->left = point.px;
		}
		if (transshaperectptr->right < point.px + 1) {
			transshaperectptr->right = point.px + 1;
		}
		if (transshaperectptr->top > point.py) {
			transshaperectptr->top = point.py;
		}
		if (transshaperectptr->bottom < point.py + 1) {
			transshaperectptr->bottom = point.py + 1;
		}
	}
}

static legacy_u16 shape3d_prepare_polygon(struct SHAPE3D_TRANSFORM_CONTEXT *context,
										  legacy_u16 any_vertex_behind, legacy_u16 primitive_flags,
										  const legacy_u8 far *front_facing_masks,
										  legacy_s32 *depth_sum)
{
	legacy_u8 common_clip_flags = shape3d_emit_polygon(context, any_vertex_behind, depth_sum);
	legacy_u16 cull_backface =
		(primitive_flags & SHAPE3D_PRIMITIVE_ALWAYS_VISIBLE_FLAG) == 0 &&
		((legacy_u32)context->front_facing_mask & LEGACY_READ_U32_LE(front_facing_masks)) == 0UL;
	if (transshapenumvertscopy == 0 || common_clip_flags != 0 ||
		(cull_backface != 0 && polyinfo_is_facing_camera(transshapepolyinfo) == 0)) {
		return 0;
	}
	if ((transshapeflags & SHAPE3D_USE_BOUNDING_RECT_FLAG) != 0) {
		shape3d_adjust_polygon_bounds();
	}
	return 1;
}

static legacy_u16 shape3d_prepare_line(struct SHAPE3D_TRANSFORM_CONTEXT *context,
									   legacy_s32 *depth_sum)
{
	legacy_u16 first_index = transshapeprimitives[0];
	legacy_u16 second_index = transshapeprimitives[1];
	if (context->vertex_clip_flags[first_index] + context->vertex_clip_flags[second_index] == 2) {
		return 0;
	}
	if (context->vertex_clip_flags[first_index] != 0) {
		shape3d_project_near_intersection(context, second_index, first_index,
										  &context->projected_vertices[first_index]);
	} else if (context->vertex_clip_flags[second_index] != 0) {
		shape3d_project_near_intersection(context, first_index, second_index,
										  &context->projected_vertices[second_index]);
	}

	/* Preserve the signed 16-bit sum used to sort lines, including car wheels. */
	*depth_sum = (legacy_s32)LEGACY_S16_WRAP_ADD(context->view_vertices[first_index].z,
												 context->view_vertices[second_index].z);
	polyinfo_write_point(transshapepolyinfo, 0U, polyvertpointptrtab[0]);
	polyinfo_write_point(transshapepolyinfo, 1U, polyvertpointptrtab[1]);
	if ((transshapeflags & SHAPE3D_USE_BOUNDING_RECT_FLAG) != 0) {
		shape3d_adjust_geometry_bounds(polyvertpointptrtab[0], transshaperectptr);
		shape3d_adjust_geometry_bounds(polyvertpointptrtab[1], transshaperectptr);
	}
	transshapenumvertscopy = 2;
	return 1;
}

static void shape3d_adjust_round_bounds(const struct POINT2D *center, legacy_u16 radius,
										legacy_s16 padding)
{
#if defined(RESTUNTS_TEMPLEOS)
	shape3d_native_round_bounds(center, radius, padding, transshaperectptr);
#else
	struct POINT2D point;
	point.px = LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_SUB(center->px, radius), padding);
	point.py = LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_SUB(center->py, radius), padding);
	shape3d_adjust_geometry_bounds(&point, transshaperectptr);
	point.px = LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_ADD(center->px, radius), padding);
	point.py = LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_ADD(center->py, radius), padding);
	shape3d_adjust_geometry_bounds(&point, transshaperectptr);
#endif
}

static legacy_u16 shape3d_prepare_wheel(struct SHAPE3D_TRANSFORM_CONTEXT *context,
										legacy_u16 any_vertex_behind, legacy_s32 *depth_sum)
{
	if (any_vertex_behind != 0) {
		return 0;
	}
	struct POINT2D points[4];
	for (legacy_u16 i = 0; i < 4; i++) {
		points[i] = *polyvertpointptrtab[i];
		polyinfo_write_point(transshapepolyinfo, i, &points[i]);
	}
	legacy_s16 first_face = is_facing_camera(points);
	if (first_face != 0) {
		*depth_sum =
			LEGACY_S32_SHL((legacy_s32)context->view_vertices[transshapeprimitives[0]].z, 2U);
	} else {
		points[0] = *polyvertpointptrtab[3];
		points[1] = *polyvertpointptrtab[4];
		points[2] = *polyvertpointptrtab[5];
		points[3] = *polyvertpointptrtab[0];
		for (legacy_u16 i = 0; i < 4; i++) {
			polyinfo_write_point(transshapepolyinfo, i, &points[i]);
		}
		*depth_sum =
			LEGACY_S32_SHL((legacy_s32)context->view_vertices[transshapeprimitives[3]].z, 2U);
	}

	legacy_u16 radius = polarRadius2D(LEGACY_S16_WRAP_SUB(points[0].px, points[1].px),
									  LEGACY_S16_WRAP_SUB(points[0].py, points[1].py));
	legacy_u16 other_radius = polarRadius2D(LEGACY_S16_WRAP_SUB(points[0].px, points[2].px),
											LEGACY_S16_WRAP_SUB(points[0].py, points[2].py));
	if (other_radius > radius) {
		radius = other_radius;
	}
	if ((transshapeflags & SHAPE3D_USE_BOUNDING_RECT_FLAG) != 0) {
		shape3d_adjust_round_bounds(&points[0], radius, 1);
		shape3d_adjust_round_bounds(&points[3], radius, 1);
	}
	transshapenumvertscopy = 4;
	return 1;
}

static void shape3d_adjust_sphere_bounds(const struct POINT2D *center, legacy_u16 radius)
{
	struct POINT2D point;

	point.px = LEGACY_S16_WRAP_SUB(center->px, radius);
	point.py = LEGACY_S16_WRAP_SUB(center->py, radius);
	shape3d_adjust_geometry_bounds(&point, transshaperectptr);
	/* Original SEG006 sphere code overwrites the second point's X with Y + radius;
	 * its Y stays at Y - radius. Crash explosions scale from these bounds. */
	point.px = LEGACY_S16_WRAP_ADD(center->py, radius);
	shape3d_adjust_geometry_bounds(&point, transshaperectptr);
}

static legacy_u16 shape3d_prepare_sphere(struct SHAPE3D_TRANSFORM_CONTEXT *context,
										 legacy_s32 *depth_sum)
{
	legacy_u16 center_index = transshapeprimitives[0];
	legacy_u16 radius_index = transshapeprimitives[1];
	*depth_sum = (legacy_s32)LEGACY_S16_WRAP_ADD(context->view_vertices[center_index].z,
												 context->view_vertices[radius_index].z);
	if (context->vertex_clip_flags[center_index] + context->vertex_clip_flags[radius_index] != 0) {
		return 0;
	}
	polyinfo_write_point(transshapepolyinfo, 0U, polyvertpointptrtab[0]);
	struct VECTOR center = context->view_vertices[center_index];
	struct VECTOR endpoint = context->view_vertices[radius_index];
	struct VECTOR radius_vector;
	radius_vector.x = LEGACY_S16_WRAP_SUB(center.x, endpoint.x);
	radius_vector.y = LEGACY_S16_WRAP_SUB(center.y, endpoint.y);
	radius_vector.z = LEGACY_S16_WRAP_SUB(center.z, endpoint.z);
#if defined(RESTUNTS_TEMPLEOS)
	legacy_u16 screen_radius = shape3d_native_sphere_radius(&radius_vector, center.z);
#else
	legacy_u16 screen_radius = projection_scale_x_wrapped(polarRadius3D(&radius_vector), center.z);
#endif
	polyinfo_write_word(transshapepolyinfo, 5U, screen_radius);
	if ((transshapeflags & SHAPE3D_USE_BOUNDING_RECT_FLAG) != 0) {
		shape3d_adjust_sphere_bounds(polyvertpointptrtab[0], screen_radius);
	}
	transshapenumvertscopy = 2;
	return 1;
}

static legacy_u16 shape3d_prepare_point(struct SHAPE3D_TRANSFORM_CONTEXT *context,
										legacy_s32 *depth_sum)
{
	legacy_u16 vertex_index = transshapeprimitives[0];
	if (context->vertex_clip_flags[vertex_index] != 0) {
		return 0;
	}
	*depth_sum = context->view_vertices[vertex_index].z;
	polyinfo_write_point(transshapepolyinfo, 0U, polyvertpointptrtab[0]);
	if ((transshapeflags & SHAPE3D_USE_BOUNDING_RECT_FLAG) != 0) {
		shape3d_adjust_geometry_bounds(polyvertpointptrtab[0], transshaperectptr);
	}
	transshapenumvertscopy = 1;
	return 1;
}

legacy_u16 shape3d_prepare_primitive(struct SHAPE3D_TRANSFORM_CONTEXT *context,
									 legacy_u8 primitive_type, legacy_u16 any_vertex_behind,
									 legacy_u16 primitive_flags,
									 const legacy_u8 far *front_facing_masks, legacy_s32 *depth_sum)
{
	switch (primitive_type) {
		case RENDER_PRIMITIVE_POLYGON:
			return shape3d_prepare_polygon(context, any_vertex_behind, primitive_flags,
										   front_facing_masks, depth_sum);
		case RENDER_PRIMITIVE_LINE:
			return shape3d_prepare_line(context, depth_sum);
		case RENDER_PRIMITIVE_WHEEL:
			return shape3d_prepare_wheel(context, any_vertex_behind, depth_sum);
		case RENDER_PRIMITIVE_SPHERE:
			return shape3d_prepare_sphere(context, depth_sum);
		case RENDER_PRIMITIVE_POINT:
			return shape3d_prepare_point(context, depth_sum);
		default:
			return 0;
	}
}

legacy_u16 shape3d_insert_primitive(legacy_u8 primitive_type, legacy_u16 primitive_flags,
									legacy_s32 depth_sum)
{
	transshapepolyinfo[POLYINFO_VERTEX_COUNT_OFFSET] = transshapenumvertscopy;
	transshapepolyinfo[POLYINFO_TYPE_OFFSET] = primitive_type;
	if ((transshapeflags & SHAPE3D_GHOST_FLAG) != 0U) {
		transshapepolyinfo[POLYINFO_TYPE_OFFSET] |= RENDER_PRIMITIVE_GHOST_FLAG;
		queued_ghost_primitives++;
	}
	if (transprimitivepaintjob == BACKLIGHT_PAINT_DEFAULT) {
		transshapepolyinfo[POLYINFO_MATERIAL_OFFSET] = backlights_paint_override;
	} else {
		transshapepolyinfo[POLYINFO_MATERIAL_OFFSET] = transprimitivepaintjob;
	}
	if ((transshapeflags & SHAPE3D_BLACK_GRILLE_FLAG) != 0U &&
		primitive_type == RENDER_PRIMITIVE_POLYGON &&
		transshapepolyinfo[POLYINFO_MATERIAL_OFFSET] == SHAPE3D_STOCK_GRILLE_MATERIAL) {
		transshapepolyinfo[POLYINFO_TYPE_OFFSET] |= RENDER_PRIMITIVE_BLACK_GRILLE_FLAG;
	}
	legacy_u16 depth = shape3d_average_depth(depth_sum, transshapenumvertscopy);
	polyinfo_write_word(transshapepolyinfo, 0U, depth);
	legacy_u16 sort_by_depth;
	if ((transshapeflags & SHAPE3D_NO_DEPTH_SORT_FLAG) != 0 ||
		(primitive_flags & SHAPE3D_PRIMITIVE_SKIP_DEPTH_SORT_FLAG) != 0) {
		sort_by_depth = 0;
	} else {
		sort_by_depth = 1;
	}
	polygon_buffer_full = polygon_insert_newest(depth, sort_by_depth);
	return polygon_buffer_full;
}
