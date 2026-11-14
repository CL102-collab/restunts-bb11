#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../c/hires/frame_interpolation.h"
#include "../c/crash_state.h"

static void test_motion_and_isolation(void)
{
	struct GAMESTATE previous = {0};
	struct GAMESTATE current = {0};
	struct GAMESTATE result;
	current.game_frame = 123;
	current.game_penalty = 50;
	current.playerstate.car_rev_speed = 600;
	current.playerstate.car_current_gear = 3;
	current.opponentstate.car_rev_speed = 700;
	previous.playerstate.car_position = (struct VECTORLONG){100, 200, 300};
	current.playerstate.car_position = (struct VECTORLONG){160, 180, 270};
	previous.playerstate.car_rotate = (struct VECTOR){1020, 4, 3};
	current.playerstate.car_rotate = (struct VECTOR){4, 1020, 1};
	previous.playerstate.car_steeringAngle = -40;
	current.playerstate.car_steeringAngle = 20;
	for (legacy_u16 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
		previous.playerstate.car_suspension_deflection[wheel] = (legacy_s16)(40 + wheel);
		current.playerstate.car_suspension_deflection[wheel] = (legacy_s16)(30 + wheel);
	}
	previous.opponentstate.car_position = (struct VECTORLONG){-100, -200, -300};
	current.opponentstate.car_position = (struct VECTORLONG){-160, -180, -270};
	previous.opponentstate.car_rotate = (struct VECTOR){4, 1020, 1};
	current.opponentstate.car_rotate = (struct VECTOR){1020, 4, 3};
	previous.opponentstate.car_steeringAngle = 10;
	current.opponentstate.car_steeringAngle = 30;
	previous.opponentstate.car_suspension_deflection[3] = 20;
	current.opponentstate.car_suspension_deflection[3] = 40;
	previous.game_follow_camera_position[0] = (struct VECTOR){100, 100, 100};
	current.game_follow_camera_position[0] = (struct VECTOR){90, 100, 110};
	previous.game_follow_camera_position[1] = (struct VECTOR){200, 300, 400};
	current.game_follow_camera_position[1] = (struct VECTOR){400, 100, 800};
	struct GAMESTATE saved_previous = previous;
	struct GAMESTATE saved_current = current;
	frame_hires_interpolate_state(&result, &current, &previous, FRAME_INTERPOLATION_ONE / 2U);
	assert(result.playerstate.car_position.lx == 130);
	assert(result.playerstate.car_position.ly == 190);
	assert(result.playerstate.car_position.lz == 285);
	assert(result.playerstate.car_rotate.x == 0);
	assert(result.playerstate.car_rotate.y == 0);
	assert(result.playerstate.car_rotate.z == 2);
	assert(result.playerstate.car_steeringAngle == -10);
	for (legacy_u16 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
		assert(result.playerstate.car_suspension_deflection[wheel] == 35 + wheel);
	}
	assert(result.opponentstate.car_position.lx == -130);
	assert(result.opponentstate.car_position.ly == -190);
	assert(result.opponentstate.car_position.lz == -285);
	assert(result.opponentstate.car_rotate.x == 0);
	assert(result.opponentstate.car_rotate.y == 0);
	assert(result.opponentstate.car_rotate.z == 2);
	assert(result.opponentstate.car_steeringAngle == 20);
	assert(result.opponentstate.car_suspension_deflection[3] == 30);
	assert(result.game_follow_camera_position[0].x == 95);
	assert(result.game_follow_camera_position[0].y == 100);
	assert(result.game_follow_camera_position[0].z == 105);
	assert(result.game_follow_camera_position[1].x == 300);
	assert(result.game_follow_camera_position[1].y == 200);
	assert(result.game_follow_camera_position[1].z == 600);
	assert(result.game_frame == current.game_frame);
	assert(result.game_penalty == current.game_penalty);
	assert(result.playerstate.car_rev_speed == current.playerstate.car_rev_speed);
	assert(result.playerstate.car_current_gear == current.playerstate.car_current_gear);
	assert(result.opponentstate.car_rev_speed == current.opponentstate.car_rev_speed);
	assert(memcmp(&previous, &saved_previous, sizeof(previous)) == 0);
	assert(memcmp(&current, &saved_current, sizeof(current)) == 0);
	/* Signed division must truncate tiny deltas toward zero from the previous pose. */
	frame_hires_interpolate_state(&result, &current, &previous, 1);
	assert(result.playerstate.car_position.lx == 100);
	assert(result.playerstate.car_position.ly == 200);
	assert(result.playerstate.car_rotate.x == 1020);
	assert(result.playerstate.car_rotate.z == 3);
}

static void test_endpoints_and_bounds(void)
{
	struct CARSTATE previous = {0};
	struct CARSTATE current = {0};
	struct CARSTATE result;
	previous.car_position = (struct VECTORLONG){INT32_MIN, INT32_MAX, 10};
	current.car_position = (struct VECTORLONG){INT32_MAX, INT32_MIN, 20};
	previous.car_rotate = (struct VECTOR){INT16_MIN, -1, 17};
	current.car_rotate = (struct VECTOR){INT16_MAX, 1, 27};
	previous.car_steeringAngle = INT16_MIN;
	current.car_steeringAngle = INT16_MAX;
	previous.car_suspension_deflection[0] = INT16_MAX;
	current.car_suspension_deflection[0] = INT16_MIN;
	current.car_rev_speed = 999;
	frame_hires_interpolate_car(&result, &current, &previous, FRAME_INTERPOLATION_ONE / 2U);
	assert(result.car_position.lx == -1);
	assert(result.car_position.ly == 0);
	assert(result.car_position.lz == 15);
	assert(result.car_rotate.x == 0);
	assert(result.car_rotate.y == 0);
	assert(result.car_steeringAngle == -1);
	assert(result.car_suspension_deflection[0] == 0);
	frame_hires_interpolate_car(&result, &current, &previous, 0);
	struct CARSTATE expected = previous;
	expected.car_rev_speed = current.car_rev_speed;
	assert(memcmp(&result, &expected, sizeof(result)) == 0);
	frame_hires_interpolate_car(&result, &current, &previous, FRAME_INTERPOLATION_ONE);
	assert(memcmp(&result, &current, sizeof(result)) == 0);
	frame_hires_interpolate_car(&result, &current, &previous, UINT32_MAX);
	assert(memcmp(&result, &current, sizeof(result)) == 0);

	struct VECTOR previous_vector = {INT16_MIN, INT16_MAX, 10};
	struct VECTOR current_vector = {INT16_MAX, INT16_MIN, 20};
	struct VECTOR result_vector;
	frame_hires_interpolate_vector(&result_vector, &current_vector, &previous_vector,
								   FRAME_INTERPOLATION_ONE / 2U);
	assert(result_vector.x == -1);
	assert(result_vector.y == 0);
	assert(result_vector.z == 15);
	frame_hires_interpolate_vector(&result_vector, &current_vector, &previous_vector, 0);
	assert(memcmp(&result_vector, &previous_vector, sizeof(result_vector)) == 0);
	frame_hires_interpolate_vector(&result_vector, &current_vector, &previous_vector, UINT32_MAX);
	assert(memcmp(&result_vector, &current_vector, sizeof(result_vector)) == 0);
}

static void test_wrapped_angles(void)
{
	struct CARSTATE previous = {0};
	struct CARSTATE current = {0};
	struct CARSTATE result;
	previous.car_rotate = (struct VECTOR){1020, 4, 0};
	current.car_rotate = (struct VECTOR){4, 1020, ANGLE_HALF_TURN};
	frame_hires_interpolate_car(&result, &current, &previous, FRAME_INTERPOLATION_ONE / 4U);
	assert(result.car_rotate.x == 1022);
	assert(result.car_rotate.y == 2);
	assert(result.car_rotate.z == 896);
	frame_hires_interpolate_car(&result, &current, &previous, FRAME_INTERPOLATION_ONE / 2U);
	assert(result.car_rotate.x == 0);
	assert(result.car_rotate.y == 0);
	assert(result.car_rotate.z == 768);
	frame_hires_interpolate_car(&result, &current, &previous, FRAME_INTERPOLATION_ONE * 3U / 4U);
	assert(result.car_rotate.x == 2);
	assert(result.car_rotate.y == 1022);
	assert(result.car_rotate.z == 640);
}

static void test_crash_and_camera_discontinuities(void)
{
	struct GAMESTATE previous = {0};
	struct GAMESTATE current = {0};
	struct GAMESTATE result;
	previous.playerstate.car_position.lx = 100;
	current.playerstate.car_position.lx = 200;
	current.playerstate.car_crashBmpFlag = CRASH_EVENT_COLLISION;
	previous.opponentstate.car_position.lx = 300;
	current.opponentstate.car_position.lx = 400;
	previous.opponentstate.car_crashBmpFlag = CRASH_EVENT_COLLISION;
	previous.game_follow_camera_position[0].x = 100;
	current.game_follow_camera_position[0].x = 400;
	previous.game_follow_camera_position[1].x = 200;
	current.game_follow_camera_position[1].x = 500;
	frame_hires_interpolate_state(&result, &current, &previous, FRAME_INTERPOLATION_ONE / 2U);
	assert(memcmp(&result.playerstate, &current.playerstate, sizeof(result.playerstate)) == 0);
	assert(memcmp(&result.opponentstate, &current.opponentstate, sizeof(result.opponentstate)) ==
		   0);
	assert(result.game_follow_camera_position[0].x == 400);
	assert(result.game_follow_camera_position[1].x == 500);
	frame_hires_interpolate_state(&result, &current, &previous, 0);
	assert(result.playerstate.car_position.lx == 200);
	assert(result.opponentstate.car_position.lx == 400);
	assert(result.game_follow_camera_position[0].x == 400);
	assert(result.game_follow_camera_position[1].x == 500);
	/* Motion after a confirmed crash still has continuous authoritative snapshots. */
	previous.playerstate.car_crashBmpFlag = current.playerstate.car_crashBmpFlag;
	frame_hires_interpolate_state(&result, &current, &previous, FRAME_INTERPOLATION_ONE / 2U);
	assert(result.playerstate.car_position.lx == 150);
	assert(result.game_follow_camera_position[0].x == 250);
}

static void test_follow_camera_ignores_trackside_changes(void)
{
	const legacy_s32 position_scale = 64;
	const struct VECTOR origins[GAMESTATE_CAR_VECTOR_COUNT] = {{1000, 100, 2000},
															   {2200, 200, 3500}};
	const struct VECTOR movement = {60, 12, 30};
	const struct VECTOR follow_offset = {-450, 270, -180};
	const legacy_u32 fractions[] = {0,
									FRAME_INTERPOLATION_ONE / 6U,
									FRAME_INTERPOLATION_ONE / 3U,
									FRAME_INTERPOLATION_ONE / 2U,
									FRAME_INTERPOLATION_ONE * 2U / 3U,
									FRAME_INTERPOLATION_ONE * 5U / 6U,
									FRAME_INTERPOLATION_ONE};
	struct GAMESTATE previous = {0};
	struct GAMESTATE current = {0};
	struct GAMESTATE result;
	for (legacy_u16 car = 0; car < GAMESTATE_CAR_VECTOR_COUNT; car++) {
		struct CARSTATE *previous_car =
			car == PLAYER_CAR_INDEX ? &previous.playerstate : &previous.opponentstate;
		struct CARSTATE *current_car =
			car == PLAYER_CAR_INDEX ? &current.playerstate : &current.opponentstate;
		previous_car->car_position =
			(struct VECTORLONG){origins[car].x * position_scale, origins[car].y * position_scale,
								origins[car].z * position_scale};
		current_car->car_position =
			(struct VECTORLONG){(origins[car].x + movement.x) * position_scale,
								(origins[car].y + movement.y) * position_scale,
								(origins[car].z + movement.z) * position_scale};
		previous.game_follow_camera_position[car] =
			(struct VECTOR){origins[car].x + follow_offset.x, origins[car].y + follow_offset.y,
							origins[car].z + follow_offset.z};
		current.game_follow_camera_position[car] =
			(struct VECTOR){origins[car].x + follow_offset.x + movement.x,
							origins[car].y + follow_offset.y + movement.y,
							origins[car].z + follow_offset.z + movement.z};
	}
	/* Nearest-trackside changes must not advance either helicopter camera
	 * ahead of the car. Cover each car separately and both at the same time. */
	for (legacy_u16 changes = 1; changes < (1U << GAMESTATE_CAR_VECTOR_COUNT); changes++) {
		for (legacy_u16 car = 0; car < GAMESTATE_CAR_VECTOR_COUNT; car++) {
			current.game_trackside_camera_index[car] = (changes & (1U << car)) != 0;
		}
		for (legacy_u16 slot = 0; slot < sizeof(fractions) / sizeof(fractions[0]); slot++) {
			frame_hires_interpolate_state(&result, &current, &previous, fractions[slot]);
			for (legacy_u16 car = 0; car < GAMESTATE_CAR_VECTOR_COUNT; car++) {
				const struct CARSTATE *rendered_car =
					car == PLAYER_CAR_INDEX ? &result.playerstate : &result.opponentstate;
				const struct VECTOR *camera = &result.game_follow_camera_position[car];
				assert(rendered_car->car_position.lx / position_scale - camera->x ==
					   -follow_offset.x);
				assert(rendered_car->car_position.ly / position_scale - camera->y ==
					   -follow_offset.y);
				assert(rendered_car->car_position.lz / position_scale - camera->z ==
					   -follow_offset.z);
				assert(result.game_trackside_camera_index[car] ==
					   current.game_trackside_camera_index[car]);
			}
		}
	}
}

static void test_particles(void)
{
	struct GAMESTATE previous = {0};
	struct GAMESTATE current = {0};
	struct GAMESTATE result;
	previous.game_particles_active = 1;
	current.game_particles_active = 1;
	for (legacy_u16 part = 0; part < GAMESTATE_PARTICLE_SLOT_COUNT; part++) {
		previous.game_particle_forward_speed[part] = 20;
		current.game_particle_forward_speed[part] = 20;
		previous.game_particle_x[part] = 900;
		current.game_particle_x[part] = 1000;
		previous.game_particle_y[part] = -100;
		current.game_particle_y[part] = 100;
		previous.game_particle_z[part] = -900;
		current.game_particle_z[part] = -1000;
		previous.game_particle_rotation_x[part] = 1020;
		current.game_particle_rotation_x[part] = 4;
		previous.game_particle_rotation_y[part] = 4;
		current.game_particle_rotation_y[part] = 1020;
		previous.game_particle_heading[part] = 20;
		current.game_particle_heading[part] = 40;
	}
	struct GAMESTATE saved_previous = previous;
	struct GAMESTATE saved_current = current;
	frame_hires_interpolate_state(&result, &current, &previous, FRAME_INTERPOLATION_ONE / 2U);
	for (legacy_u16 part = 0; part < GAMESTATE_PARTICLE_SLOT_COUNT; part++) {
		assert(result.game_particle_x[part] == 950);
		assert(result.game_particle_y[part] == 0);
		assert(result.game_particle_z[part] == -950);
		assert(result.game_particle_rotation_x[part] == 0);
		assert(result.game_particle_rotation_y[part] == 0);
		assert(result.game_particle_heading[part] == 30);
	}
	assert(memcmp(&previous, &saved_previous, sizeof(previous)) == 0);
	assert(memcmp(&current, &saved_current, sizeof(current)) == 0);
	frame_hires_interpolate_state(&result, &current, &previous, 0);
	assert(memcmp(&result, &previous, sizeof(result)) == 0);
	frame_hires_interpolate_state(&result, &current, &previous, FRAME_INTERPOLATION_ONE);
	assert(memcmp(&result, &current, sizeof(result)) == 0);
	frame_hires_interpolate_state(&result, &current, &previous, UINT32_MAX);
	assert(memcmp(&result, &current, sizeof(result)) == 0);

	previous.game_particle_forward_speed[0] = 0;
	current.game_particle_forward_speed[1] = 0;
	current.game_particle_owner[2] = 1;
	current.game_particle_shape_index[3] = 1;
	current.game_particle_forward_speed[4] = 30;
	frame_hires_interpolate_state(&result, &current, &previous, FRAME_INTERPOLATION_ONE / 2U);
	for (legacy_u16 part = 0; part < 5; part++) {
		assert(result.game_particle_x[part] == current.game_particle_x[part]);
		assert(result.game_particle_heading[part] == current.game_particle_heading[part]);
	}
	assert(result.game_particle_x[5] == 950);
	previous.game_particles_active = 0;
	frame_hires_interpolate_state(&result, &current, &previous, FRAME_INTERPOLATION_ONE / 2U);
	assert(memcmp(&result, &current, sizeof(result)) == 0);
	previous.game_particles_active = 1;
	current.game_particles_active = 0;
	frame_hires_interpolate_state(&result, &current, &previous, FRAME_INTERPOLATION_ONE / 2U);
	assert(memcmp(&result, &current, sizeof(result)) == 0);
}

legacy_int main(void)
{
	test_motion_and_isolation();
	test_endpoints_and_bounds();
	test_wrapped_angles();
	test_crash_and_camera_discontinuities();
	test_follow_camera_ignores_trackside_changes();
	test_particles();
	return 0;
}
