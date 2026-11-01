#ifndef RESTUNTS_SHAPE3D_NATIVE_H
#define RESTUNTS_SHAPE3D_NATIVE_H

#include "shape3d.h"

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

#endif
