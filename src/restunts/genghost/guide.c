#include <float.h>
#include "guide.h"
#include "routes.h"
#include "../c/externs.h"
#include "../c/game_input.h"

extern double fabs(double);

static double bounded(double value, double low, double high)
{
	return value < low ? low : value > high ? high : value;
}

static int angle_difference(int first, int second)
{
	int delta = (first - second) % 1024;
	return delta < -512 ? delta + 1024 : delta > 512 ? delta - 1024 : delta;
}

void genghost_guide_capture(struct GENGHOST_GUIDE_POINT *point, unsigned char input)
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

static double pose_cost(const struct GENGHOST_GUIDE_POINT *point)
{
	const struct CARSTATE *car = &state.playerstate;
	double dx = ((double)point->position.lx - car->car_position.lx) / 64.0;
	double dy = ((double)point->position.ly - car->car_position.ly) / 64.0;
	double dz = ((double)point->position.lz - car->car_position.lz) / 64.0;
	double yaw = angle_difference(point->rotation.x, car->car_rotate.x);
	double pitch = angle_difference(point->rotation.y, car->car_rotate.y);
	double roll = angle_difference(point->rotation.z, car->car_rotate.z);
	double speed = ((double)point->speed - car->car_actual_speed) / 256.0;
	/* Height and attitude keep nearby loop, bridge and crossing samples distinct. */
	return dx * dx + dy * dy * 2.0 + dz * dz + yaw * yaw * 8.0 + pitch * pitch * 8.0 +
		   roll * roll * 8.0 + speed * speed * 0.25;
}

unsigned char genghost_guide_step(const struct GENGHOST_GUIDE_POINT *points, unsigned int count,
								  unsigned int *cursor, double correction)
{
	if (!count) {
		return 0;
	}
	const struct CARSTATE *car = &state.playerstate;
	unsigned int expected = *cursor < count ? *cursor : count - 1U;
	unsigned int first = expected > 3U ? expected - 3U : 0U;
	unsigned int end = expected + 9U < count ? expected + 9U : count;
	unsigned int best = expected;
	double cost = DBL_MAX;
	for (unsigned int i = first; i < end; ++i) {
		double delta = (double)i - expected;
		double candidate = pose_cost(&points[i]) + delta * delta * 0.2;
		if (candidate < cost) {
			cost = candidate;
			best = i;
		}
	}
	const struct GENGHOST_GUIDE_POINT *point = &points[best];
	unsigned char input = point->input;
	*cursor = best + 1U;
	/* Preserve airborne and heavily banked stunt inputs. Tiny steering corrections
 * intended for a flat corner can destroy a jump, corkscrew or collision launch. */
	if (correction > 0.0 && car->car_sumSurfAllWheels && point->contact &&
		fabs((double)angle_difference(car->car_rotate.y, 0)) < 80.0 &&
		fabs((double)angle_difference(car->car_rotate.z, 0)) < 80.0 && best + 1U < count) {
		struct VECTOR error = {
			(legacy_s16)bounded(((double)point->position.lx - car->car_position.lx) / 64.0,
								-32767.0, 32767.0),
			0,
			(legacy_s16)bounded(((double)point->position.lz - car->car_position.lz) / 64.0,
								-32767.0, 32767.0)};
		struct VECTOR local, forward = {0, 0, 1024}, heading, heading_local;
		struct MATRIX reference = *mat_rot_zxy(-point->rotation.z, -point->rotation.y,
											   -point->rotation.x, MATRIX_ROTATION_ORDER_ZXY);
		mat_mul_vector(&forward, &reference, &heading);
		struct MATRIX current = *mat_rot_zxy(car->car_rotate.z, car->car_rotate.y,
											 car->car_rotate.x, MATRIX_ROTATION_ORDER_YXZ);
		mat_mul_vector(&error, &current, &local);
		mat_mul_vector(&heading, &current, &heading_local);
		double change = correction * bounded(local.x * 0.45 + heading_local.x * 0.4, -80.0, 80.0);
		if (fabs(change) >= 1.5) {
			double wanted = bounded(points[best + 1U].steering + change, -240.0, 240.0);
			input = (unsigned char)((input & ~INPUT_STEERING_MASK) | genghost_steering_key(wanted));
		}
	}
	/* Time alignment can repeat or jump past a shift key. Reconcile the recorded
 * gear only when it actually differs; exact replay following leaves inputs intact. */
	if (car->car_transmission == TRANSMISSION_MANUAL && !car->car_changing_gear &&
		car->car_current_gear != point->gear) {
		input &= (unsigned char)~INPUT_ACTION_BUTTON_MASK;
		input |= car->car_current_gear < point->gear ? INPUT_SHIFT_UP_FLAG : INPUT_SHIFT_DOWN_FLAG;
	}
	return input;
}
