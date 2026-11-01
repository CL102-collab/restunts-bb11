#ifndef RESTUNTS_INTRO_VIEW_H
#define RESTUNTS_INTRO_VIEW_H

#include "math/math.h"

struct INTRO_VIEW {
	struct VECTOR camera;
	struct VECTOR car_position;
	legacy_s16 car_rotation;
	legacy_s16 horizontal_angle;
	legacy_s16 vertical_angle;
	legacy_s16 draw_car;
	legacy_s16 primary_logo;
};

#endif
