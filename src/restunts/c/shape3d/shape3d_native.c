#include "../externs.h"
#include "../math/projection.h"
#include "shape3d_pipeline_internal.h"
#include "shape3d_native.h"

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

#if defined(RESTUNTS_TEMPLEOS)
void shape3d_native_near_intersection(const struct VECTOR *front, const struct VECTOR *behind,
									  struct POINT2D *point)
{
	legacy_s64 depth_span = (legacy_s64)front->z - behind->z;
	legacy_s64 near_offset = SHAPE3D_NEAR_CLIP_Z - (legacy_s64)behind->z;
	/* Preserve the intersection's fraction until the native projection divides. */
	shape3d_project_coordinates(
		behind->x * depth_span + ((legacy_s64)front->x - behind->x) * near_offset,
		behind->y * depth_span + ((legacy_s64)front->y - behind->y) * near_offset,
		SHAPE3D_NEAR_CLIP_Z * depth_span, point);
}

void shape3d_native_round_bounds(const struct POINT2D *center, legacy_u16 radius,
								 legacy_s16 padding, struct RECTANGLE *rectangle)
{
	/* Large offscreen wheels must not wrap their saved-background bounds. */
	legacy_s64 extent = (legacy_s64)radius + padding * SHAPE3D_NATIVE_SCALE;
	struct POINT2D point;
	point.px = shape3d_native_coordinate(center->px - extent);
	point.py = shape3d_native_coordinate(center->py - extent);
	shape3d_adjust_geometry_bounds(&point, rectangle);
	point.px = shape3d_native_coordinate(center->px + extent);
	point.py = shape3d_native_coordinate(center->py + extent);
	shape3d_adjust_geometry_bounds(&point, rectangle);
}

legacy_u16 shape3d_native_sphere_radius(struct VECTOR *radius_vector, legacy_s16 depth)
{
	return shape3d_native_coordinate((legacy_s64)projection_focal_length_x *
									 (legacy_u16)polarRadius3D(radius_vector) *
									 SHAPE3D_NATIVE_SCALE / depth);
}
#endif
