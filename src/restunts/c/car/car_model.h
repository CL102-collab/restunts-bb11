#ifndef RESTUNTS_CAR_MODEL_H
#define RESTUNTS_CAR_MODEL_H

#include "../shape3d/shape3d.h"

/* Car model resources and wheel geometry shared with scene drawing. */

extern legacy_s8 car_shape_resource_name[];
extern legacy_s8 far *carresptr;
extern legacy_s8 far *car2resptr;
extern struct VECTOR player_front_wheel_centers[2];
extern struct VECTOR player_base_wheel_vertices[24];
extern struct VECTOR opponent_front_wheel_centers[2];
extern struct VECTOR opponent_base_wheel_vertices[24];
extern legacy_s16 player_wheel_vertex_state[];
extern legacy_s16 opponent_wheel_vertex_state[];
extern legacy_s16 neutral_wheel_suspension[];
extern legacy_s8 backlights_paint_override;

#endif
