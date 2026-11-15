#include "unit_collision.h"
#define UNIT_COLLISION_PI 3.14159265358979323846
#define UNIT_COLLISION_AXES 6U
#define UNIT_COLLISION_MINIMUM_MOTION 1e-12

legacy_int unit_collision_swept_boxes(const struct UNIT_VEHICLE *first,
									  const struct UNIT_VEHICLE *last,
									  const struct UNIT_VEHICLE *player, legacy_f64 time,
									  legacy_f64 margin)
{
	legacy_f64 x = first->x - player->x, z = first->z - player->z;
	legacy_f64 dx = last->x - first->x - player->vx * time;
	legacy_f64 dz = last->z - first->z - player->vz * time;
	legacy_f64 width = fmax(first->half_width, last->half_width);
	legacy_f64 length = fmax(first->half_length, last->half_length);
	legacy_f64 radius = hypot(width, length);
	legacy_f64 combined = radius + hypot(player->half_width, player->half_length) + margin;
	legacy_f64 squared = dx * dx + dz * dz;
	legacy_f64 nearest = squared > UNIT_COLLISION_MINIMUM_MOTION
							 ? fmax(0.0, fmin(1.0, -(x * dx + z * dz) / squared))
							 : 0.0;
	if (hypot(x + nearest * dx, z + nearest * dz) > combined) {
		return 0;
	}
	legacy_f64 turn = fabs(remainder(last->yaw - first->yaw, 2.0 * UNIT_COLLISION_PI));
	/* Support varies at most radius * angle over a rotation. Inflating each
	 * endpoint projection covers every intermediate orientation. */
	legacy_f64 reserve = radius * turn;
	legacy_f64 axes[UNIT_COLLISION_AXES] = {first->yaw,	 first->yaw + UNIT_COLLISION_PI / 2.0,
											last->yaw,	 last->yaw + UNIT_COLLISION_PI / 2.0,
											player->yaw, player->yaw + UNIT_COLLISION_PI / 2.0};
	legacy_f64 begin = 0.0, end = 1.0;
	for (legacy_u32 axis = 0; axis < UNIT_COLLISION_AXES; axis++) {
		legacy_f64 a = axes[axis], ax = sin(a), az = cos(a);
		legacy_f64 extent = fmax(unit_collision_projected_extent(length, width, first->yaw - a),
								 unit_collision_projected_extent(length, width, last->yaw - a)) +
							reserve +
							unit_collision_projected_extent(player->half_length, player->half_width,
															player->yaw - a) +
							margin;
		legacy_f64 position = x * ax + z * az, motion = dx * ax + dz * az;
		if (fabs(motion) <= UNIT_COLLISION_MINIMUM_MOTION) {
			if (fabs(position) > extent) {
				return 0;
			}
			continue;
		}
		legacy_f64 first_time = (-extent - position) / motion;
		legacy_f64 last_time = (extent - position) / motion;
		begin = fmax(begin, fmin(first_time, last_time));
		end = fmin(end, fmax(first_time, last_time));
		if (begin > end) {
			return 0;
		}
	}
	return 1;
}
