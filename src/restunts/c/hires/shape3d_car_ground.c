#include "shape3d_car_ground.h"

#if defined(RESTUNTS_SDL3)
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "shape3d_mesh.h"
#include "shape3d_hires.h"
#include "../math/projection.h"

#define CAR_WHEEL_VERTEX_GROUP_SIZE 6
#define CAR_WHEEL_CENTER_SAMPLE_OFFSET 3U
#define CAR_GROUND_ROUNDING_ULPS 16U

/* Physics places neutral tire contacts 384 fixed-point units below the body.
 * Cache the visible underside before suspension can change model vertices.
 * Sphere endpoints encode size, and unused bounding vertices are not geometry. */
#define CAR_PHYSICS_TIRE_BOTTOM -6
#define CAR_GROUND_OFFSET_CACHE_COUNT 2U
#define CAR_GROUND_SPHERE_CAPACITY 255U
#define CAR_GROUND_SPHERE_HEIGHT_NUMERATOR 13U
#define CAR_GROUND_SPHERE_HEIGHT_DENOMINATOR 32UL

struct CAR_GROUND_SPHERE {
	legacy_s16 center_y;
	legacy_u32 diameter;
};

struct CAR_GROUND_BOUNDS {
	legacy_s32 lowest;
	legacy_u16 has_geometry, sphere_count;
	struct CAR_GROUND_SPHERE spheres[CAR_GROUND_SPHERE_CAPACITY];
	struct CAR_GROUND_SPHERE *extended_spheres;
	legacy_u16 sphere_capacity;
};

static struct {
	const struct SHAPE3D *shape;
	struct CAR_GROUND_BOUNDS bounds;
	legacy_u16 focal_x, focal_y;
	legacy_s16 offset;
} hires_car_ground_offsets[CAR_GROUND_OFFSET_CACHE_COUNT];

/* Round extents upward with integer arithmetic, including full signed-word
 * coordinate differences. This runs only while loading a model. */
static legacy_s32 shape3d_hires_car_extent(legacy_s32 first, legacy_s32 second, legacy_s32 third)
{
	legacy_u32 first_size = (legacy_u32)(first < 0 ? -first : first);
	legacy_u32 second_size = (legacy_u32)(second < 0 ? -second : second);
	legacy_u32 third_size = (legacy_u32)(third < 0 ? -third : third);
	legacy_u64 squared = (legacy_u64)first_size * first_size +
						 (legacy_u64)second_size * second_size +
						 (legacy_u64)third_size * third_size;
	legacy_u32 lower = 0;
	legacy_u32 upper = first_size + second_size + third_size;
	while (lower < upper) {
		legacy_u32 middle = lower + (upper - lower) / 2U;
		if ((legacy_u64)middle * middle < squared) {
			lower = middle + 1U;
		} else {
			upper = middle;
		}
	}
	return (legacy_s32)lower;
}

static legacy_s16 shape3d_hires_car_has_area(const struct VECTOR *vertices, legacy_u16 count)
{
	legacy_s64 ax = 0, ay = 0, az = 0;
	for (legacy_u16 index = 1; index < count; index++) {
		legacy_s64 bx = (legacy_s32)vertices[index].x - vertices[0].x;
		legacy_s64 by = (legacy_s32)vertices[index].y - vertices[0].y;
		legacy_s64 bz = (legacy_s32)vertices[index].z - vertices[0].z;
		if (ax == 0 && ay == 0 && az == 0) {
			ax = bx;
			ay = by;
			az = bz;
		} else if (ay * bz != az * by || az * bx != ax * bz || ax * by != ay * bx) {
			return 1;
		}
	}
	return 0;
}

static legacy_s16 shape3d_hires_car_precise_has_area(const struct SHAPE3D_HIRES_VECTOR *vertices,
													 legacy_u16 count)
{
	for (legacy_u16 index = 1; index + 1U < count; index++) {
		legacy_f64 ax = vertices[index].x - vertices[0].x;
		legacy_f64 ay = vertices[index].y - vertices[0].y;
		legacy_f64 az = vertices[index].z - vertices[0].z;
		for (legacy_u16 next = index + 1U; next < count; next++) {
			legacy_f64 bx = vertices[next].x - vertices[0].x;
			legacy_f64 by = vertices[next].y - vertices[0].y;
			legacy_f64 bz = vertices[next].z - vertices[0].z;
			if (ay * bz != az * by || az * bx != ax * bz || ax * by != ay * bx) {
				return 1;
			}
		}
	}
	return 0;
}

static void shape3d_hires_car_ground_bottom(struct CAR_GROUND_BOUNDS *bounds, legacy_s32 bottom)
{
	if (!bounds->has_geometry || bottom < bounds->lowest) {
		bounds->lowest = bottom;
	}
	bounds->has_geometry = 1;
}

/* Keep fractional centers and axes together until the final underside. Rounding
 * each control first can enlarge a scaled tyre by a whole model unit. */
static void shape3d_hires_car_precise_wheel_bottom(struct CAR_GROUND_BOUNDS *bounds,
												   const struct SHAPE3D_HIRES_VECTOR *vertices)
{
	legacy_f64 radius = 0;
	legacy_u16 has_rim = 0;
	for (legacy_u16 rim = 0; rim < CAR_WHEEL_VERTEX_GROUP_SIZE;
		 rim += SHAPE3D_WHEEL_RIM_VERTEX_COUNT) {
		if (!shape3d_hires_car_precise_has_area(&vertices[rim], SHAPE3D_WHEEL_RIM_VERTEX_COUNT)) {
			continue;
		}
		legacy_f64 first_y = vertices[rim + SHAPE3D_WHEEL_FIRST_AXIS].y - vertices[rim].y;
		legacy_f64 second_y = vertices[rim + SHAPE3D_WHEEL_SECOND_AXIS].y - vertices[rim].y;
		legacy_f64 rim_radius = hypot(first_y, second_y);
		if (rim_radius > radius) {
			radius = rim_radius;
		}
		has_rim = 1;
	}
	if (has_rim) {
		/* Wheel coordinates are finite; DJGPP's libm has no C99 fmin. */
		legacy_f64 center = vertices[0].y;
		if (vertices[CAR_WHEEL_CENTER_SAMPLE_OFFSET].y < center) {
			center = vertices[CAR_WHEEL_CENTER_SAMPLE_OFFSET].y;
		}
		legacy_f64 tolerance = CAR_GROUND_ROUNDING_ULPS * DBL_EPSILON * (fabs(center) + radius + 1);
		shape3d_hires_car_ground_bottom(bounds, (legacy_s32)floor(center - radius + tolerance));
	}
}

static void shape3d_hires_car_ground_clear(struct CAR_GROUND_BOUNDS *bounds)
{
	free(bounds->extended_spheres);
	memset(bounds, 0, sizeof(*bounds));
}

static const struct CAR_GROUND_SPHERE *
shape3d_hires_car_ground_spheres(const struct CAR_GROUND_BOUNDS *bounds)
{
	return bounds->extended_spheres != NULL ? bounds->extended_spheres : bounds->spheres;
}

static void shape3d_hires_car_ground_bounds(const struct SHAPE3D *shape,
											struct CAR_GROUND_BOUNDS *bounds)
{
	static const legacy_u8 vertex_counts[SHAPE3D_PRIMITIVE_TYPE_COUNT] = {0, 1, 2,	3, 4, 5, 6, 7,
																		  8, 9, 10, 2, 6, 3, 0, 0};
	memset(bounds, 0, sizeof(*bounds));
	shape = hires_shape3d_car_mesh(shape);
	if (shape == 0 || shape->shape3d_vertex_bytes == 0 || shape->shape3d_primitives == 0) {
		return;
	}
	bounds->sphere_capacity = CAR_GROUND_SPHERE_CAPACITY;
	if (shape->shape3d_numprimitives > CAR_GROUND_SPHERE_CAPACITY) {
		bounds->extended_spheres =
			malloc((size_t)shape->shape3d_numprimitives * sizeof(*bounds->extended_spheres));
		if (bounds->extended_spheres == NULL) {
			return;
		}
		bounds->sphere_capacity = shape->shape3d_numprimitives;
	}
	const struct SHAPE3D_HIRES_VECTOR *precise = hires_shape3d_car_vertices(shape);
	const legacy_u8 *primitive = shape->shape3d_primitives;
	for (legacy_u32 index = 0; index < shape->shape3d_numprimitives; index++) {
		legacy_u8 type = primitive[0];
		if (type >= SHAPE3D_PRIMITIVE_TYPE_COUNT) {
			shape3d_hires_car_ground_clear(bounds);
			return;
		}
		legacy_u8 count = vertex_counts[type];
		const legacy_u8 *indices =
			primitive + SHAPE3D_PRIMITIVE_HEADER_SIZE + shape->shape3d_numpaints;
		primitive = indices + count * hires_shape3d_index_size(shape);
		if (type == SHAPE3D_PRIMITIVE_EMPTY || type > SHAPE3D_PRIMITIVE_WHEEL ||
			(shape->shape3d_visibility_masks != 0 &&
			 LEGACY_READ_U32_LE(shape->shape3d_visibility_masks +
								index * SHAPE3D_VISIBILITY_MASK_SIZE) == 0)) {
			continue;
		}
		struct VECTOR vertices[SHAPE3D_POLYGON_MAX_VERTICES];
		struct SHAPE3D_HIRES_VECTOR precise_points[SHAPE3D_POLYGON_MAX_VERTICES];
		for (legacy_u16 vertex = 0; vertex < count; vertex++) {
			legacy_u16 vertex_index = hires_shape3d_vertex_index(shape, indices, vertex);
			if (vertex_index >= shape->shape3d_numverts) {
				shape3d_hires_car_ground_clear(bounds);
				return;
			}
			if (precise != NULL) {
				precise_points[vertex] = precise[vertex_index];
				/* Ground presentation uses whole model units. Round the underside
				 * down so fractional bodywork cannot sink into its receiving plane. */
				vertices[vertex].x = (legacy_s16)precise[vertex_index].x;
				vertices[vertex].y = (legacy_s16)precise[vertex_index].y;
				if (vertices[vertex].y > precise[vertex_index].y) {
					vertices[vertex].y--;
				}
				vertices[vertex].z = (legacy_s16)precise[vertex_index].z;
			} else {
				shape3d_vertex_read(shape, vertex_index, &vertices[vertex]);
			}
		}
		if (type == SHAPE3D_PRIMITIVE_WHEEL) {
			if (precise != NULL) {
				shape3d_hires_car_precise_wheel_bottom(bounds, precise_points);
				continue;
			}
			legacy_s32 radius = 0;
			legacy_u16 has_rim = 0;
			for (legacy_u16 rim = 0; rim < CAR_WHEEL_VERTEX_GROUP_SIZE;
				 rim += SHAPE3D_WHEEL_RIM_VERTEX_COUNT) {
				if (!shape3d_hires_car_has_area(&vertices[rim], SHAPE3D_WHEEL_RIM_VERTEX_COUNT)) {
					continue;
				}
				legacy_s32 first_y =
					(legacy_s32)vertices[rim + SHAPE3D_WHEEL_FIRST_AXIS].y - vertices[rim].y;
				legacy_s32 second_y =
					(legacy_s32)vertices[rim + SHAPE3D_WHEEL_SECOND_AXIS].y - vertices[rim].y;
				legacy_s32 rim_radius = shape3d_hires_car_extent(first_y, second_y, 0);
				if (rim_radius > radius) {
					radius = rim_radius;
				}
				has_rim = 1;
			}
			if (has_rim) {
				/* Either visible rim is extruded to both centers by draw_wheel. */
				legacy_s32 center = vertices[0].y < vertices[CAR_WHEEL_CENTER_SAMPLE_OFFSET].y
										? vertices[0].y
										: vertices[CAR_WHEEL_CENTER_SAMPLE_OFFSET].y;
				shape3d_hires_car_ground_bottom(bounds, center - radius);
			}
		} else if (type == SHAPE3D_PRIMITIVE_SPHERE) {
			legacy_u32 diameter =
				(legacy_u32)shape3d_hires_car_extent((legacy_s32)vertices[1].x - vertices[0].x,
													 (legacy_s32)vertices[1].y - vertices[0].y,
													 (legacy_s32)vertices[1].z - vertices[0].z);
			if (diameter == 0) {
				continue;
			}
			if (bounds->sphere_count == bounds->sphere_capacity) {
				shape3d_hires_car_ground_clear(bounds);
				return;
			}
			struct CAR_GROUND_SPHERE *spheres =
				bounds->extended_spheres != NULL ? bounds->extended_spheres : bounds->spheres;
			struct CAR_GROUND_SPHERE *sphere = &spheres[bounds->sphere_count++];
			sphere->center_y = vertices[0].y;
			sphere->diameter = diameter;
		} else if (type == SHAPE3D_PRIMITIVE_POINT || type == SHAPE3D_PRIMITIVE_LINE ||
				   (precise != NULL ? shape3d_hires_car_precise_has_area(precise_points, count)
									: shape3d_hires_car_has_area(vertices, count))) {
			for (legacy_u16 vertex = 0; vertex < count; vertex++) {
				shape3d_hires_car_ground_bottom(bounds, vertices[vertex].y);
			}
		}
	}
}

static legacy_s16 shape3d_hires_calculate_car_ground_level(const struct CAR_GROUND_BOUNDS *bounds,
														   legacy_u16 focal_x, legacy_u16 focal_y,
														   legacy_s16 reference_height)
{
	legacy_s64 lowest = bounds->lowest;
	legacy_u16 found = bounds->has_geometry;
	for (legacy_u16 index = 0; index < bounds->sphere_count; index++) {
		const struct CAR_GROUND_SPHERE *sphere = &shape3d_hires_car_ground_spheres(bounds)[index];
		/* Match draw_sphere's screen-space vertical extent, not its diameter
		 * control vertex. Fall back to the nominal aspect before projection. */
		legacy_u64 numerator =
			(legacy_u64)sphere->diameter * CAR_GROUND_SPHERE_HEIGHT_NUMERATOR * focal_x;
		legacy_u32 denominator = CAR_GROUND_SPHERE_HEIGHT_DENOMINATOR * focal_y;
		legacy_s64 radius = focal_x != 0 && focal_y != 0
								? (legacy_s64)((numerator + denominator - 1) / denominator)
								: (legacy_s64)((sphere->diameter + 1U) / 2U);
		legacy_s64 bottom = sphere->center_y - radius;
		if (!found || bottom < lowest) {
			lowest = bottom;
		}
		found = 1;
	}
	if (!found) {
		return 0;
	}
	legacy_s64 offset = lowest - reference_height;
	if (offset > (legacy_s32)LEGACY_S16_MAX) {
		offset = LEGACY_S16_MAX;
	} else if (offset < -(legacy_s32)LEGACY_S16_MAX) {
		offset = -(legacy_s32)LEGACY_S16_MAX;
	}
	return (legacy_s16)offset;
}

legacy_s16 shape3d_hires_car_ground_height(const struct SHAPE3D *shape)
{
	struct CAR_GROUND_BOUNDS bounds;
	shape3d_hires_car_ground_bounds(shape, &bounds);
	legacy_s16 height = shape3d_hires_calculate_car_ground_level(&bounds, projection_focal_length_x,
																 projection_focal_length_y, 0);
	shape3d_hires_car_ground_clear(&bounds);
	return height;
}

void shape3d_hires_reset_car_ground_offsets(void)
{
	for (legacy_u16 index = 0; index < CAR_GROUND_OFFSET_CACHE_COUNT; index++) {
		shape3d_hires_car_ground_clear(&hires_car_ground_offsets[index].bounds);
		hires_car_ground_offsets[index].shape = 0;
		hires_car_ground_offsets[index].offset = 0;
	}
}

void shape3d_hires_cache_car_ground_offset(legacy_u16 index, const struct SHAPE3D *shape)
{
	shape3d_hires_car_ground_clear(&hires_car_ground_offsets[index].bounds);
	hires_car_ground_offsets[index].shape = shape;
	shape3d_hires_car_ground_bounds(shape, &hires_car_ground_offsets[index].bounds);
	hires_car_ground_offsets[index].focal_x = projection_focal_length_x;
	hires_car_ground_offsets[index].focal_y = projection_focal_length_y;
	hires_car_ground_offsets[index].offset = shape3d_hires_calculate_car_ground_level(
		&hires_car_ground_offsets[index].bounds, projection_focal_length_x,
		projection_focal_length_y, CAR_PHYSICS_TIRE_BOTTOM);
}

legacy_s16 shape3d_hires_car_ground_offset(const struct SHAPE3D *shape)
{
	for (legacy_u16 index = 0; shape != 0 && index < CAR_GROUND_OFFSET_CACHE_COUNT; index++) {
		if (shape == hires_car_ground_offsets[index].shape) {
			if (hires_car_ground_offsets[index].focal_x != projection_focal_length_x ||
				hires_car_ground_offsets[index].focal_y != projection_focal_length_y) {
				hires_car_ground_offsets[index].focal_x = projection_focal_length_x;
				hires_car_ground_offsets[index].focal_y = projection_focal_length_y;
				hires_car_ground_offsets[index].offset = shape3d_hires_calculate_car_ground_level(
					&hires_car_ground_offsets[index].bounds, projection_focal_length_x,
					projection_focal_length_y, CAR_PHYSICS_TIRE_BOTTOM);
			}
			return hires_car_ground_offsets[index].offset;
		}
	}
	return 0;
}
#endif
