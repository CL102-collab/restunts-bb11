#ifndef RESTUNTS_WHEEL_TRANSFORM_H
#define RESTUNTS_WHEEL_TRANSFORM_H

#include "math/math.h"

/* Shared wheel travel and orientation used by collision geometry and car drawing. */

extern legacy_s16 car_initial_roll;
extern legacy_s16 car_initial_yaw;
extern legacy_s16 car_initial_pitch;
extern struct MATRIX car_to_world_rotation;
extern struct VECTOR wheel_forward_travel;
extern legacy_s16 wheel_heading_offset;
extern struct VECTOR wheel_world_travel;

void transform_wheel_travel_to_world(void);

#endif
