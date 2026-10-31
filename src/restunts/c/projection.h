#ifndef RESTUNTS_PROJECTION_H
#define RESTUNTS_PROJECTION_H

#include "math.h"

extern struct MATRIX mat_temp;
extern legacy_u16 projection_center_x;
extern legacy_u16 projection_center_y;
extern legacy_u16 projection_focal_length_x;
extern legacy_u16 projection_focal_length_y;
extern struct RECTANGLE select_rect_rc;

void projection_set_origin(legacy_u16 x, legacy_u16 y);

#endif
