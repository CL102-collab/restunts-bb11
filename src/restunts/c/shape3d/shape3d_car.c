#include "../fileio.h"
#include "../memmgr.h"
#include "shape3d.h"
#include "../car/car_model.h"
#include "../scene_resources.h"
#include "../owoot/owoot.h"
#if defined(RESTUNTS_SDL3)
#include "../hires/shape3d_hires.h"
#include "../hires/shape3d_car_ground.h"
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
