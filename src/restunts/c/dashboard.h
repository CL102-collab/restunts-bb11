#ifndef RESTUNTS_DASHBOARD_H
#define RESTUNTS_DASHBOARD_H

#include "legacy.h"

enum DASHBOARD_OPERATION {
	DASHBOARD_OPERATION_LOAD = 0,
	DASHBOARD_OPERATION_REDRAW_STATIC = 1,
	DASHBOARD_OPERATION_UPDATE = 2,
	DASHBOARD_OPERATION_UNLOAD = 3
};

extern legacy_s8 far *stdaresptr;
extern legacy_s8 far *stdbresptr;
extern struct SHAPE2D far *whlshapes[];
extern struct SHAPE2D far *gnobshapes[];
extern struct SHAPE2D far *digshapes[];
extern struct SPRITE far *dashboard_instrument_sprite;
extern struct SPRITE far *dashboard_gearbox_sprite;
extern legacy_s16 dashboard_rpm_index_cache[];
extern legacy_s16 dashboard_gear_knob_x_cache[];
extern legacy_s16 dashboard_gear_knob_y_cache[];
extern legacy_s16 dashboard_speed_index_cache[];
extern legacy_s16 dashboard_steering_dot_x_cache[];
extern legacy_s16 dashboard_steering_dot_y_cache[];
extern legacy_s16 dashboard_steering_position_cache[];
extern legacy_u8 dashboard_wheel_shape_cache[];
extern legacy_u8 dashboard_gear_knob_visible_cache[];
extern legacy_s8 dashboard_wheel_and_instrument_ids[];
extern legacy_s8 dashboard_gear_and_dot_shape_ids[];
extern legacy_s8 dashboard_digit_shape_ids[];
extern legacy_s8 dashboard_background_shape_id[];
extern legacy_s8 dashboard_roof_shape_id[];
extern legacy_s8 dashboard_top_shape_id[];
extern legacy_s8 dashboard_mask_shape_id[];
extern legacy_s8 dashboard_primary_resource_name[];
extern legacy_s8 dashboard_secondary_resource_name[];

void setup_car_shapes(legacy_s16 operation);

#endif
