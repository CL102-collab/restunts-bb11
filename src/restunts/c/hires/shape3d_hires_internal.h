#ifndef RESTUNTS_SHAPE3D_HIRES_INTERNAL_H
#define RESTUNTS_SHAPE3D_HIRES_INTERNAL_H

#include "shape3d_hires.h"
#if defined(RESTUNTS_SDL3)
#include "hires.h"
#include "hypervision.h"
#include <stddef.h>

#define HIRES_NEAR_CLIP_Z 12
#define HIRES_CLIP_ROUNDING_MARGIN 8
/* Keep the original displayed pixel width through medium-close views,
 * then let perspective narrow the stroke at greater distances. */
#define HIRES_LINE_DIAMETER 1.5
/* Subpixel decals can miss pixel centers at some positions and orientations. */
#define HIRES_MIN_DECAL_WIDTH 0.75
#define HIRES_DECAL_WIDTH_REDUCTION 2
#define HIRES_MAX_POLYGON_POINTS 20
#define HIRES_ROUND_POINTS 64
#define HIRES_ROUND_SMALL_POINTS 8U
#define HIRES_ROUND_MEDIUM_POINTS 16U
#define HIRES_ROUND_LARGE_POINTS 32U
#define HIRES_ROUND_SMALL_RADIUS 3.0
#define HIRES_ROUND_MEDIUM_RADIUS 12.0
#define HIRES_ROUND_LARGE_RADIUS 48.0
#define HIRES_SPHERE_HORIZONTAL_SCALE 0.5
#define HIRES_SPHERE_VERTICAL_SCALE (13.0 / 32.0)
#define HIRES_WHEEL_INNER_SCALE (9472.0 / TRIG_FIXED_ONE)

struct HIRES_PRIMITIVE {
	struct SHAPE3D_HIRES_POINT points[HIRES_MAX_POLYGON_POINTS];
	legacy_u32 count;
	legacy_u32 round_points;
	legacy_u32 shape;
	legacy_u32 family;
	legacy_u32 shadow_surface;
	legacy_s32 attached;
	legacy_f64 size;
};

struct HIRES_PAINT {
	struct HIRES_BACKEND_MATERIAL material;
	legacy_s32 scale;
};

struct HIRES_SHAPE {
	legacy_s32 depth_mode;
	legacy_s32 shadow_receiver;
};

#define HIRES_CAR_SHADOW_COUNT 2
#define HIRES_CAR_SHADOW_REACH 256.0
#define HIRES_CAR_SHADOW_HEIGHT_SCALE 0.6
/* Cotangent of the 70-degree sun elevation; preserve the authored approximation. */
#define HIRES_CAR_SHADOW_SUN_COTANGENT 0.363970
#define HIRES_CAR_SHADOW_OFFSET_LIMIT_SCALE 0.7
#define HIRES_CAR_SHADOW_RECEIVER_TOLERANCE 0.5
#define HIRES_CAR_SHADOW_SHRINK_HEIGHT 128
#define HIRES_CAR_SHADOW_FALLBACK_HEIGHT_SCALE 0.5
#define HIRES_CAR_SHADOW_FALLBACK_DIAGONAL 1.7
#define HIRES_CAR_SHADOW_FALLBACK_EDGE_SCALE 8
#define HIRES_CAR_SHADOW_FADE_HEIGHT 64
#define HIRES_CAR_SHADOW_MAX_OPACITY 96
#define HIRES_CAR_SHADOW_BOUNDS_CORNERS 8
#define HIRES_CAR_SHADOW_BOUNDS_EDGES 12
#define HIRES_CAR_SHADOW_CORNER_MAX_X 1
#define HIRES_CAR_SHADOW_CORNER_MAX_Z 2
#define HIRES_CAR_SHADOW_CORNER_BOTTOM 4
#define HIRES_SHADOW_SURFACE_INITIAL_CAPACITY 32U
#define HIRES_SHADOW_SURFACE_MAX_POINTS SHAPE3D_POLYGON_MAX_VERTICES
#define HIRES_SHADOW_GRILLE_TRANSMISSION 0.5
#define HIRES_SHADOW_SURFACE_MAX_HITS 4U
#define HIRES_SHADOW_INTERSECTION_EPSILON 1e-9

struct HIRES_CAR_SHADOW {
	struct VECTOR position;
	legacy_f64 cosine, sine;
	legacy_f64 half_width, half_length;
	legacy_f64 min_x, max_x, min_z, max_z, height;
	legacy_s32 projected;
	const struct SHADOW_SILHOUETTE *silhouette;
	struct SHAPE3D_HIRES_VECTOR minimum, maximum;
	legacy_f64 surface_top, grille_top;
};

struct SHADOW_SURFACE {
	struct SHAPE3D_HIRES_VECTOR vertices[HIRES_SHADOW_SURFACE_MAX_POINTS];
	struct SHAPE3D_HIRES_VECTOR minimum, maximum, normal;
	legacy_f64 distance;
	legacy_u32 count, cars;
	legacy_s32 grille, axis;
};

#define SHADOW_SILHOUETTE_WIDTH 128
#define SHADOW_SILHOUETTE_LENGTH 256
#define SHADOW_SILHOUETTE_ROUND_POINTS 12
#define SHADOW_SILHOUETTE_LINE_RADIUS 0.75
#define SHADOW_SILHOUETTE_BORDER_TEXELS 2
#define SHADOW_SILHOUETTE_INITIAL_BOUND (LEGACY_U16_MAX + 1.0)

struct SHADOW_PROJECTION {
	legacy_f64 x, z, sphere_vertical;
	legacy_s16 ground_height;
	legacy_s32 enabled;
};

struct SHADOW_SILHOUETTE {
	struct SHADOW_PROJECTION projection;
	const struct SHAPE3D *shape;
	const legacy_u8 *vertex_bytes;
	legacy_f64 min_x, min_z, max_x, max_z, height, scale_x, scale_z;
	legacy_u8 pixels[SHADOW_SILHOUETTE_WIDTH * SHADOW_SILHOUETTE_LENGTH];
};

struct SHADOW_SILHOUETTE_POINT {
	legacy_f64 x, z;
};

struct SHAPE3D_HIRES_STATE {
	struct HIRES_CAR_SHADOW car_shadows[HIRES_CAR_SHADOW_COUNT];
	legacy_s32 car_shadow_count;
	legacy_s32 shadow_model_pending;
	struct MATRIX shadow_view;
	legacy_f64 shadow_inverse[3][3];
	legacy_f64 shadow_ground_y;
	legacy_f64 shadow_light_x, shadow_light_z;
	legacy_f64 shadow_light_cotangent;
	struct SHADOW_SURFACE *shadow_surfaces;
	legacy_f64 *shadow_grille_hits;
	size_t shadow_surface_count, shadow_surface_capacity;
	struct HIRES_PRIMITIVE *primitives;
	struct HIRES_SHAPE *shapes;
	legacy_u32 primitive_capacity;
	legacy_u32 primitive_count;
	legacy_u32 current_shape;
	legacy_u32 current_family;
	legacy_s32 rendered_depth_valid;
	legacy_u32 rendered_generation;
	legacy_f64 model_scale;
	struct HIRES_DEPTH_PLANE ground_plane;
	legacy_s32 ground_enabled;
	legacy_s32 batching;
};

extern struct SHAPE3D_HIRES_STATE shape3d_hires_state;
void shape3d_hires_project_intersection(const struct SHAPE3D_HIRES_VECTOR *first,
										const struct SHAPE3D_HIRES_VECTOR *second,
										struct SHAPE3D_HIRES_POINT *point);
legacy_s32 shape3d_hires_ceil_coordinate(legacy_f64 coordinate);
legacy_f64 shape3d_hires_absolute_coordinate(legacy_f64 value);
legacy_f64 shape3d_hires_shadow_light_offset(const struct HIRES_CAR_SHADOW *shadow, legacy_f64 gap);
legacy_f64 shape3d_hires_shadow_silhouette_sample(const struct SHADOW_SILHOUETTE *silhouette,
												  legacy_f64 x, legacy_f64 z);

#endif
#endif
