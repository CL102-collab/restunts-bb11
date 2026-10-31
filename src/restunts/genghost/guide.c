#include <float.h>
#include "guide.h"
#include "routes.h"
#include "../c/externs.h"
#include "../c/game_input.h"
#include "../c/extended_track.h"

extern legacy_f64 fabs(legacy_f64);

static legacy_f64 bounded(legacy_f64 value, legacy_f64 low, legacy_f64 high)
{
	return value < low ? low : value > high ? high : value;
}

static legacy_int angle_difference(legacy_int first, legacy_int second)
{
	legacy_int delta = (first - second) % ANGLE_FULL_TURN;
	return delta < -ANGLE_HALF_TURN	 ? delta + ANGLE_FULL_TURN
		   : delta > ANGLE_HALF_TURN ? delta - ANGLE_FULL_TURN
									 : delta;
}

void genghost_guide_capture(struct GENGHOST_GUIDE_POINT *point, legacy_u8 input)
{
	const struct CARSTATE *car = &state.playerstate;
	point->position = car->car_position;
	point->rotation = car->car_rotate;
	point->steering = car->car_steeringAngle;
	point->speed = car->car_actual_speed;
	point->gear = car->car_current_gear;
	point->contact = car->car_sumSurfAllWheels;
	point->input = input;
}

static legacy_f64 pose_cost(const struct GENGHOST_GUIDE_POINT *point)
{
	const struct CARSTATE *car = &state.playerstate;
	legacy_f64 dx = ((legacy_f64)point->position.lx - car->car_position.lx) / 64.0;
	legacy_f64 dy = ((legacy_f64)point->position.ly - car->car_position.ly) / 64.0;
	legacy_f64 dz = ((legacy_f64)point->position.lz - car->car_position.lz) / 64.0;
	legacy_f64 yaw = angle_difference(point->rotation.x, car->car_rotate.x);
	legacy_f64 pitch = angle_difference(point->rotation.y, car->car_rotate.y);
	legacy_f64 roll = angle_difference(point->rotation.z, car->car_rotate.z);
	legacy_f64 speed = ((legacy_f64)point->speed - car->car_actual_speed) / 256.0;
	/* Height and attitude keep nearby loop, bridge and crossing samples distinct. */
	return dx * dx + dy * dy * 2.0 + dz * dz + yaw * yaw * 8.0 + pitch * pitch * 8.0 +
		   roll * roll * 8.0 + speed * speed * 0.25;
}

legacy_u8 genghost_guide_step(const struct GENGHOST_GUIDE_POINT *points, legacy_uint count,
							  legacy_uint *cursor, legacy_f64 correction)
{
	if (!count) {
		return 0;
	}
	const struct CARSTATE *car = &state.playerstate;
	legacy_uint expected = *cursor < count ? *cursor : count - 1U;
	legacy_uint first = expected > 3U ? expected - 3U : 0U;
	legacy_uint end = expected + 9U < count ? expected + 9U : count;
	legacy_uint best = expected;
	legacy_f64 cost = DBL_MAX;
	for (legacy_uint i = first; i < end; ++i) {
		legacy_f64 delta = (legacy_f64)i - expected;
		legacy_f64 candidate = pose_cost(&points[i]) + delta * delta * 0.2;
		if (candidate < cost) {
			cost = candidate;
			best = i;
		}
	}
	const struct GENGHOST_GUIDE_POINT *point = &points[best];
	legacy_u8 input = point->input;
	*cursor = best + 1U;
	/* Preserve airborne and heavily banked stunt inputs. Tiny steering corrections
 * intended for a flat corner can destroy a jump, corkscrew or collision launch. */
	if (correction > 0.0 && car->car_sumSurfAllWheels && point->contact &&
		fabs((legacy_f64)angle_difference(car->car_rotate.y, 0)) < 80.0 &&
		fabs((legacy_f64)angle_difference(car->car_rotate.z, 0)) < 80.0 && best + 1U < count) {
		struct VECTOR error = {
			(legacy_s16)bounded(((legacy_f64)point->position.lx - car->car_position.lx) / 64.0,
								-32767.0, 32767.0),
			0,
			(legacy_s16)bounded(((legacy_f64)point->position.lz - car->car_position.lz) / 64.0,
								-32767.0, 32767.0)};
		if (extended_track_active()) {
			/* Grade transforms tilt the road's local axes. Include measured height
			 * error when projecting a correction into that tilted car frame. */
			error.y = (legacy_s16)bounded(
				((legacy_f64)point->position.ly - car->car_position.ly) / 64.0, -32767.0, 32767.0);
		}
		struct VECTOR local, forward = {0, 0, 1024}, heading, heading_local;
		struct MATRIX reference = *mat_rot_zxy(-point->rotation.z, -point->rotation.y,
											   -point->rotation.x, MATRIX_ROTATION_ORDER_ZXY);
		mat_mul_vector(&forward, &reference, &heading);
		struct MATRIX current = *mat_rot_zxy(car->car_rotate.z, car->car_rotate.y,
											 car->car_rotate.x, MATRIX_ROTATION_ORDER_YXZ);
		mat_mul_vector(&error, &current, &local);
		mat_mul_vector(&heading, &current, &heading_local);
		legacy_f64 change =
			correction * bounded(local.x * 0.45 + heading_local.x * 0.4, -80.0, 80.0);
		if (fabs(change) >= 1.5) {
			legacy_f64 wanted = bounded(points[best + 1U].steering + change, -240.0, 240.0);
			input = (legacy_u8)((input & ~INPUT_STEERING_MASK) | genghost_steering_key(wanted));
		}
	}
	/* Time alignment can repeat or jump past a shift key. Reconcile the recorded
 * gear only when it actually differs; exact replay following leaves inputs intact. */
	if (car->car_transmission == TRANSMISSION_MANUAL && !car->car_changing_gear &&
		car->car_current_gear != point->gear) {
		input &= (legacy_u8)~INPUT_ACTION_BUTTON_MASK;
		input |= car->car_current_gear < point->gear ? INPUT_SHIFT_UP_FLAG : INPUT_SHIFT_DOWN_FLAG;
	}
	return input;
}
