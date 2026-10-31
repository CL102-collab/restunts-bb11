#include "../fileio.h"
#include "../memmgr.h"
#include "shape3d.h"
#include "../car/car_model.h"
#include "../scene_resources.h"
#include "../owoot/owoot.h"
#if defined(RESTUNTS_SDL3)
#include <string.h>
#include "../hires/shape3d_hires.h"
#include "../projection.h"
#include "../gamestate.h"
#endif

#define CAR_RESOURCE_ID_OFFSET 2U
#define CAR_ID_LENGTH 4
#define CAR_WHEEL_CENTER_COUNT 2U
#define CAR_WHEEL_COUNT 4
#define CAR_WHEEL_VERTEX_GROUP_SIZE 6
#define CAR_WHEEL_VERTEX_COUNT 24U
#define CAR_WHEEL_CENTER_SAMPLE_OFFSET 3U
#define CAR_FIRST_WHEEL_VERTEX 8U
#define CAR_STEERED_WHEEL_VERTEX_COUNT 12
#define CAR_WHEEL_STATE_CACHE_SIZE 5
#define CAR_WHEEL_STEERING_CACHE_INDEX 4
#define CAR_WHEEL_VERTICAL_SCALE_SHIFT 6U
#define PLAYER_EXPLOSION_SHAPE_FIRST 116
#define OPPONENT_EXPLOSION_SHAPE_FIRST 120

#if defined(RESTUNTS_SDL3)
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
};

static struct {
	const struct SHAPE3D *shape;
	struct CAR_GROUND_BOUNDS bounds;
	legacy_u16 focal_x, focal_y;
	legacy_s16 offset;
} car_ground_offsets[CAR_GROUND_OFFSET_CACHE_COUNT];

/* Round extents upward with integer arithmetic, including full signed-word
 * coordinate differences. This runs only while loading a model. */
static legacy_s32 shape3d_car_extent(legacy_s32 first, legacy_s32 second, legacy_s32 third)
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

static legacy_s16 shape3d_car_has_area(const struct VECTOR *vertices, legacy_u16 count)
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

static void shape3d_car_ground_bottom(struct CAR_GROUND_BOUNDS *bounds, legacy_s32 bottom)
{
	if (!bounds->has_geometry || bottom < bounds->lowest) {
		bounds->lowest = bottom;
	}
	bounds->has_geometry = 1;
}

static void shape3d_car_ground_bounds(const struct SHAPE3D *shape, struct CAR_GROUND_BOUNDS *bounds)
{
	static const legacy_u8 vertex_counts[SHAPE3D_PRIMITIVE_TYPE_COUNT] = {0, 1, 2,	3, 4, 5, 6, 7,
																		  8, 9, 10, 2, 6, 3, 0, 0};
	memset(bounds, 0, sizeof(*bounds));
	if (shape == 0 || shape->shape3d_vertex_bytes == 0 || shape->shape3d_primitives == 0) {
		return;
	}
	const legacy_u8 *primitive = shape->shape3d_primitives;
	for (legacy_u16 index = 0; index < shape->shape3d_numprimitives; index++) {
		legacy_u8 type = primitive[0];
		if (type >= SHAPE3D_PRIMITIVE_TYPE_COUNT) {
			memset(bounds, 0, sizeof(*bounds));
			return;
		}
		legacy_u8 count = vertex_counts[type];
		const legacy_u8 *indices =
			primitive + SHAPE3D_PRIMITIVE_HEADER_SIZE + shape->shape3d_numpaints;
		primitive = indices + count;
		if (type == SHAPE3D_PRIMITIVE_EMPTY || type > SHAPE3D_PRIMITIVE_WHEEL ||
			(shape->shape3d_visibility_masks != 0 &&
			 LEGACY_READ_U32_LE(shape->shape3d_visibility_masks +
								index * SHAPE3D_VISIBILITY_MASK_SIZE) == 0)) {
			continue;
		}
		struct VECTOR vertices[SHAPE3D_POLYGON_MAX_VERTICES];
		for (legacy_u16 vertex = 0; vertex < count; vertex++) {
			if (indices[vertex] >= shape->shape3d_numverts) {
				memset(bounds, 0, sizeof(*bounds));
				return;
			}
			shape3d_vertex_read(shape, indices[vertex], &vertices[vertex]);
		}
		if (type == SHAPE3D_PRIMITIVE_WHEEL) {
			legacy_s32 radius = 0;
			legacy_u16 has_rim = 0;
			for (legacy_u16 rim = 0; rim < CAR_WHEEL_VERTEX_GROUP_SIZE;
				 rim += SHAPE3D_WHEEL_RIM_VERTEX_COUNT) {
				if (!shape3d_car_has_area(&vertices[rim], SHAPE3D_WHEEL_RIM_VERTEX_COUNT)) {
					continue;
				}
				legacy_s32 first_y =
					(legacy_s32)vertices[rim + SHAPE3D_WHEEL_FIRST_AXIS].y - vertices[rim].y;
				legacy_s32 second_y =
					(legacy_s32)vertices[rim + SHAPE3D_WHEEL_SECOND_AXIS].y - vertices[rim].y;
				legacy_s32 rim_radius = shape3d_car_extent(first_y, second_y, 0);
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
				shape3d_car_ground_bottom(bounds, center - radius);
			}
		} else if (type == SHAPE3D_PRIMITIVE_SPHERE) {
			legacy_u32 diameter =
				(legacy_u32)shape3d_car_extent((legacy_s32)vertices[1].x - vertices[0].x,
											   (legacy_s32)vertices[1].y - vertices[0].y,
											   (legacy_s32)vertices[1].z - vertices[0].z);
			if (diameter == 0) {
				continue;
			}
			if (bounds->sphere_count == CAR_GROUND_SPHERE_CAPACITY) {
				memset(bounds, 0, sizeof(*bounds));
				return;
			}
			struct CAR_GROUND_SPHERE *sphere = &bounds->spheres[bounds->sphere_count++];
			sphere->center_y = vertices[0].y;
			sphere->diameter = diameter;
		} else if (type == SHAPE3D_PRIMITIVE_POINT || type == SHAPE3D_PRIMITIVE_LINE ||
				   shape3d_car_has_area(vertices, count)) {
			for (legacy_u16 vertex = 0; vertex < count; vertex++) {
				shape3d_car_ground_bottom(bounds, vertices[vertex].y);
			}
		}
	}
}

static legacy_s16 shape3d_calculate_car_ground_level(const struct CAR_GROUND_BOUNDS *bounds,
													 legacy_u16 focal_x, legacy_u16 focal_y,
													 legacy_s16 reference_height)
{
	legacy_s64 lowest = bounds->lowest;
	legacy_u16 found = bounds->has_geometry;
	for (legacy_u16 index = 0; index < bounds->sphere_count; index++) {
		const struct CAR_GROUND_SPHERE *sphere = &bounds->spheres[index];
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

legacy_s16 shape3d_car_ground_height(const struct SHAPE3D *shape)
{
	struct CAR_GROUND_BOUNDS bounds;
	shape3d_car_ground_bounds(shape, &bounds);
	return shape3d_calculate_car_ground_level(&bounds, projection_focal_length_x,
											  projection_focal_length_y, 0);
}

static void shape3d_reset_car_ground_offsets(void)
{
	for (legacy_u16 index = 0; index < CAR_GROUND_OFFSET_CACHE_COUNT; index++) {
		car_ground_offsets[index].shape = 0;
		car_ground_offsets[index].offset = 0;
	}
}

static void shape3d_cache_car_ground_offset(legacy_u16 index, const struct SHAPE3D *shape)
{
	car_ground_offsets[index].shape = shape;
	shape3d_car_ground_bounds(shape, &car_ground_offsets[index].bounds);
	car_ground_offsets[index].focal_x = projection_focal_length_x;
	car_ground_offsets[index].focal_y = projection_focal_length_y;
	car_ground_offsets[index].offset = shape3d_calculate_car_ground_level(
		&car_ground_offsets[index].bounds, projection_focal_length_x, projection_focal_length_y,
		CAR_PHYSICS_TIRE_BOTTOM);
}

legacy_s16 shape3d_car_ground_offset(const struct SHAPE3D *shape)
{
	for (legacy_u16 index = 0; shape != 0 && index < CAR_GROUND_OFFSET_CACHE_COUNT; index++) {
		if (shape == car_ground_offsets[index].shape) {
			if (car_ground_offsets[index].focal_x != projection_focal_length_x ||
				car_ground_offsets[index].focal_y != projection_focal_length_y) {
				car_ground_offsets[index].focal_x = projection_focal_length_x;
				car_ground_offsets[index].focal_y = projection_focal_length_y;
				car_ground_offsets[index].offset = shape3d_calculate_car_ground_level(
					&car_ground_offsets[index].bounds, projection_focal_length_x,
					projection_focal_length_y, CAR_PHYSICS_TIRE_BOTTOM);
			}
			return car_ground_offsets[index].offset;
		}
	}
	return 0;
}
#endif

static void shape3d_init_car_wheel_vertices(const struct SHAPE3D *shape,
											struct VECTOR centers[CAR_WHEEL_CENTER_COUNT],
											struct VECTOR vertices[CAR_WHEEL_VERTEX_COUNT])
{
	struct VECTOR resource_vertex;

	shape3d_vertex_read(shape, CAR_FIRST_WHEEL_VERTEX, &resource_vertex);
	centers[0].x = resource_vertex.x;
	centers[0].z = resource_vertex.z;
	shape3d_vertex_read(shape, CAR_FIRST_WHEEL_VERTEX + CAR_WHEEL_CENTER_SAMPLE_OFFSET,
						&resource_vertex);
	centers[0].x = LEGACY_S16_SAR(LEGACY_S16_WRAP_ADD(centers[0].x, resource_vertex.x), 1U);

	shape3d_vertex_read(shape, CAR_FIRST_WHEEL_VERTEX + CAR_WHEEL_VERTEX_GROUP_SIZE,
						&resource_vertex);
	centers[1].x = resource_vertex.x;
	centers[1].z = resource_vertex.z;
	shape3d_vertex_read(shape,
						CAR_FIRST_WHEEL_VERTEX + CAR_WHEEL_VERTEX_GROUP_SIZE +
							CAR_WHEEL_CENTER_SAMPLE_OFFSET,
						&resource_vertex);
	centers[1].x = LEGACY_S16_SAR(LEGACY_S16_WRAP_ADD(centers[1].x, resource_vertex.x), 1U);

	for (legacy_s16 i = 0; i < CAR_WHEEL_VERTEX_GROUP_SIZE; i++) {
		shape3d_vertex_read(shape, LEGACY_U16_WRAP_ADD(CAR_FIRST_WHEEL_VERTEX, i),
							&resource_vertex);
		vertices[i].x = LEGACY_S16_WRAP_SUB(centers[0].x, resource_vertex.x);
		vertices[i].y = resource_vertex.y;
		vertices[i].z = LEGACY_S16_WRAP_SUB(centers[0].z, resource_vertex.z);

		shape3d_vertex_read(
			shape, LEGACY_U16_WRAP_ADD(CAR_FIRST_WHEEL_VERTEX + CAR_WHEEL_VERTEX_GROUP_SIZE, i),
			&resource_vertex);
		vertices[i + CAR_WHEEL_VERTEX_GROUP_SIZE].x =
			LEGACY_S16_WRAP_SUB(centers[1].x, resource_vertex.x);
		vertices[i + CAR_WHEEL_VERTEX_GROUP_SIZE].y = resource_vertex.y;
		vertices[i + CAR_WHEEL_VERTEX_GROUP_SIZE].z =
			LEGACY_S16_WRAP_SUB(centers[1].z, resource_vertex.z);

		shape3d_vertex_read(
			shape,
			LEGACY_U16_WRAP_ADD(CAR_FIRST_WHEEL_VERTEX + 2U * CAR_WHEEL_VERTEX_GROUP_SIZE, i),
			&vertices[i + 2 * CAR_WHEEL_VERTEX_GROUP_SIZE]);
		shape3d_vertex_read(
			shape,
			LEGACY_U16_WRAP_ADD(CAR_FIRST_WHEEL_VERTEX + 3U * CAR_WHEEL_VERTEX_GROUP_SIZE, i),
			&vertices[i + 3 * CAR_WHEEL_VERTEX_GROUP_SIZE]);
	}
}

void shape3d_load_car_shapes(legacy_s8 player_car_id[], legacy_s8 opponent_car_id[])
{
#if defined(RESTUNTS_SDL3)
	shape3d_hires_shadow_models_reset();
	shape3d_reset_car_ground_offsets();
#endif
	for (legacy_s16 i = 0; i < CAR_ID_LENGTH; i++) {
		car_shape_resource_name[CAR_RESOURCE_ID_OFFSET + i] = player_car_id[i];
	}
	carresptr = file_load_3dres(car_shape_resource_name);
	shape3d_init_shape(locate_shape_fatal(carresptr, "car0"), &game3dshapes[PLAYER_CAR_LOW_SHAPE]);
	shape3d_init_shape(locate_shape_fatal(carresptr, "car1"),
					   &game3dshapes[PLAYER_CAR_WHEEL_SHAPE]);
#if defined(RESTUNTS_SDL3)
	shape3d_cache_car_ground_offset(PLAYER_CAR_INDEX, &game3dshapes[PLAYER_CAR_WHEEL_SHAPE]);
#endif

	owoot_read_wheel_shape((const legacy_u8 far *)locate_shape_fatal(carresptr, "car1"));

	shape3d_init_car_wheel_vertices(&game3dshapes[PLAYER_CAR_WHEEL_SHAPE],
									player_front_wheel_centers, player_base_wheel_vertices);

	for (legacy_s16 i = 0; i < CAR_WHEEL_STATE_CACHE_SIZE; i++) {
		player_wheel_vertex_state[i] = 0;
	}

	shape3d_init_shape(locate_shape_fatal(carresptr, "car2"), &game3dshapes[PLAYER_CAR_HIGH_SHAPE]);
	shape3d_init_shape(locate_shape_fatal(carresptr, "exp0"),
					   &game3dshapes[PLAYER_EXPLOSION_SHAPE_FIRST]);
	shape3d_init_shape(locate_shape_fatal(carresptr, "exp1"),
					   &game3dshapes[PLAYER_EXPLOSION_SHAPE_FIRST + 1]);
	shape3d_init_shape(locate_shape_fatal(carresptr, "exp2"),
					   &game3dshapes[PLAYER_EXPLOSION_SHAPE_FIRST + 2]);
	shape3d_init_shape(locate_shape_fatal(carresptr, "exp3"),
					   &game3dshapes[PLAYER_EXPLOSION_SHAPE_FIRST + 3]);

	if (opponent_car_id[0] != -1) {
		if (player_car_id[0] == opponent_car_id[0] && player_car_id[1] == opponent_car_id[1] &&
			player_car_id[2] == opponent_car_id[2] && player_car_id[3] == opponent_car_id[3]) {
			legacy_u32 resource_size = mmgr_get_chunk_size_bytes(carresptr);
			car2resptr = mmgr_alloc_resbytes("car2", resource_size);
			legacy_u8 far *source_bytes = (legacy_u8 far *)carresptr;
			legacy_u8 far *destination_bytes = (legacy_u8 far *)car2resptr;

			for (legacy_u32 copy_index = 0; copy_index < resource_size; copy_index++) {
				destination_bytes[(legacy_u16)copy_index] = source_bytes[(legacy_u16)copy_index];
			}
		} else {
			for (legacy_s16 i = 0; i < CAR_ID_LENGTH; i++) {
				car_shape_resource_name[CAR_RESOURCE_ID_OFFSET + i] = opponent_car_id[i];
			}
			car2resptr = file_load_3dres(car_shape_resource_name);
		}

		shape3d_init_shape(locate_shape_fatal(car2resptr, "car0"),
						   &game3dshapes[OPPONENT_CAR_LOW_SHAPE]);
		shape3d_init_shape(locate_shape_fatal(car2resptr, "car1"),
						   &game3dshapes[OPPONENT_CAR_WHEEL_SHAPE]);
#if defined(RESTUNTS_SDL3)
		shape3d_cache_car_ground_offset(OPPONENT_CAR_INDEX,
										&game3dshapes[OPPONENT_CAR_WHEEL_SHAPE]);
#endif

		shape3d_init_car_wheel_vertices(&game3dshapes[OPPONENT_CAR_WHEEL_SHAPE],
										opponent_front_wheel_centers, opponent_base_wheel_vertices);
		for (legacy_s16 i = 0; i < CAR_WHEEL_STATE_CACHE_SIZE; i++) {
			opponent_wheel_vertex_state[i] = 0;
		}
		shape3d_init_shape(locate_shape_fatal(car2resptr, "car2"),
						   &game3dshapes[OPPONENT_CAR_HIGH_SHAPE]);
		shape3d_init_shape(locate_shape_fatal(car2resptr, "exp0"),
						   &game3dshapes[OPPONENT_EXPLOSION_SHAPE_FIRST]);
		shape3d_init_shape(locate_shape_fatal(car2resptr, "exp1"),
						   &game3dshapes[OPPONENT_EXPLOSION_SHAPE_FIRST + 1]);
		shape3d_init_shape(locate_shape_fatal(car2resptr, "exp2"),
						   &game3dshapes[OPPONENT_EXPLOSION_SHAPE_FIRST + 2]);
		shape3d_init_shape(locate_shape_fatal(car2resptr, "exp3"),
						   &game3dshapes[OPPONENT_EXPLOSION_SHAPE_FIRST + 3]);
	} else {
		car2resptr = 0;
	}
}

static void shape3d_steer_car_wheel_vertices(struct SHAPE3D *shape, legacy_u16 first_vertex,
											 legacy_s16 steering_angle,
											 const struct VECTOR *base_vertices,
											 const struct VECTOR *front_wheel_centers)
{
	legacy_s16 steering_sine = sin_fast(LEGACY_S16_SAR(steering_angle, 1U));
	legacy_s16 steering_cosine = cos_fast(LEGACY_S16_SAR(steering_angle, 1U));

	struct VECTOR vertex;
	for (legacy_s16 vertex_index = 0; vertex_index < CAR_WHEEL_VERTEX_GROUP_SIZE; vertex_index++) {
		shape3d_vertex_read(shape, LEGACY_U16_WRAP_ADD(first_vertex, vertex_index), &vertex);
		legacy_s16 first_wheel_cosine_component =
			multiply_and_scale(base_vertices[vertex_index].x, steering_cosine);
		vertex.x = LEGACY_S16_WRAP_ADD(
			LEGACY_S16_WRAP_ADD(front_wheel_centers[0].x,
								multiply_and_scale(base_vertices[vertex_index].z, steering_sine)),
			first_wheel_cosine_component);
		first_wheel_cosine_component =
			multiply_and_scale(base_vertices[vertex_index].z, steering_cosine);
		vertex.z = LEGACY_S16_WRAP_ADD(
			LEGACY_S16_WRAP_ADD(front_wheel_centers[0].z,
								multiply_and_scale(base_vertices[vertex_index].x, steering_sine)),
			first_wheel_cosine_component);
		shape3d_vertex_write(shape, LEGACY_U16_WRAP_ADD(first_vertex, vertex_index), &vertex);
	}
	for (legacy_s16 vertex_index = CAR_WHEEL_VERTEX_GROUP_SIZE;
		 vertex_index < CAR_STEERED_WHEEL_VERTEX_COUNT; vertex_index++) {
		shape3d_vertex_read(shape, LEGACY_U16_WRAP_ADD(first_vertex, vertex_index), &vertex);
		legacy_s16 second_wheel_cosine_component =
			multiply_and_scale(base_vertices[vertex_index].x, steering_cosine);
		vertex.x = LEGACY_S16_WRAP_ADD(
			LEGACY_S16_WRAP_ADD(front_wheel_centers[1].x,
								multiply_and_scale(base_vertices[vertex_index].z, steering_sine)),
			second_wheel_cosine_component);
		second_wheel_cosine_component =
			multiply_and_scale(base_vertices[vertex_index].z, steering_cosine);
		vertex.z = LEGACY_S16_WRAP_ADD(
			LEGACY_S16_WRAP_ADD(front_wheel_centers[1].z,
								multiply_and_scale(base_vertices[vertex_index].x, steering_sine)),
			second_wheel_cosine_component);
		shape3d_vertex_write(shape, LEGACY_U16_WRAP_ADD(first_vertex, vertex_index), &vertex);
	}
}

void shape3d_update_car_wheel_vertices(struct SHAPE3D *shape, legacy_u16 first_vertex,
									   legacy_s16 steering_angle,
									   const legacy_s16 *suspension_offsets,
									   legacy_s16 *cached_wheel_state, struct VECTOR *base_vertices,
									   struct VECTOR *front_wheel_centers)
{
	// return ported_sub_204AE_(arg_verts, steering_angle, suspension_offsets, cached_wheel_state,
	// base_vertices, front_wheel_centers);
	// cached_wheel_state[4] caches the steering angle the wheel vertices were last built
	// for, so the test is against steering_angle, not against zero.
	if (cached_wheel_state[CAR_WHEEL_STEERING_CACHE_INDEX] != steering_angle) {
		shape3d_steer_car_wheel_vertices(shape, first_vertex, steering_angle, base_vertices,
										 front_wheel_centers);
		cached_wheel_state[CAR_WHEEL_STEERING_CACHE_INDEX] = steering_angle;
	}

	struct VECTOR vertex;
	for (legacy_s16 wheel_index = 0; wheel_index < CAR_WHEEL_COUNT; wheel_index++) {
		// The original takes |x|, shifts that right six, then re-applies the
		// sign of x (loc_2069F: cwd / xor / sub, sar ax,6, xor / sub).
		legacy_s16 vertical_offset = suspension_offsets[wheel_index];
		if (vertical_offset < 0) {
			vertical_offset = LEGACY_S16_WRAP_NEGATE(vertical_offset);
		}
		vertical_offset = LEGACY_S16_SAR(vertical_offset, CAR_WHEEL_VERTICAL_SCALE_SHIFT);
		if (suspension_offsets[wheel_index] < 0) {
			vertical_offset = LEGACY_S16_WRAP_NEGATE(vertical_offset);
		}

		if (cached_wheel_state[wheel_index] == vertical_offset) {
			continue;
		}
		legacy_s16 vertex_index = wheel_index * CAR_WHEEL_VERTEX_GROUP_SIZE;
		legacy_s16 wheel_vertex_end = vertex_index + CAR_WHEEL_VERTEX_GROUP_SIZE;

		for (; vertex_index < wheel_vertex_end; vertex_index++) {
			shape3d_vertex_read(shape, LEGACY_U16_WRAP_ADD(first_vertex, vertex_index), &vertex);
			vertex.y = LEGACY_S16_WRAP_SUB(base_vertices[vertex_index].y, vertical_offset);
			shape3d_vertex_write(shape, LEGACY_U16_WRAP_ADD(first_vertex, vertex_index), &vertex);
		}
		cached_wheel_state[wheel_index] = vertical_offset;
	}

	return;
}

void shape3d_free_car_shapes(void)
{
#if defined(RESTUNTS_SDL3)
	shape3d_hires_shadow_models_reset();
	shape3d_reset_car_ground_offsets();
#endif
	if (car2resptr != 0) {
		shape3d_update_car_wheel_vertices(&game3dshapes[OPPONENT_CAR_WHEEL_SHAPE],
										  CAR_FIRST_WHEEL_VERTEX, 0, neutral_wheel_suspension,
										  opponent_wheel_vertex_state, opponent_base_wheel_vertices,
										  opponent_front_wheel_centers);
		mmgr_release(car2resptr);
		car2resptr = 0;
	}
	if (carresptr != 0) {
		shape3d_update_car_wheel_vertices(&game3dshapes[PLAYER_CAR_WHEEL_SHAPE],
										  CAR_FIRST_WHEEL_VERTEX, 0, neutral_wheel_suspension,
										  player_wheel_vertex_state, player_base_wheel_vertices,
										  player_front_wheel_centers);
		mmgr_free(carresptr);
		carresptr = 0;
	}

	/* Scene objects can retain these records after their resource is freed. */
	struct SHAPE3D empty_shape = {0};
	for (legacy_s16 shape_index = PLAYER_EXPLOSION_SHAPE_FIRST;
		 shape_index <= OPPONENT_CAR_HIGH_SHAPE; shape_index++) {
		game3dshapes[shape_index] = empty_shape;
	}
}
