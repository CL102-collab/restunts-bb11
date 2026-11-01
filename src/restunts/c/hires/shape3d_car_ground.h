#ifndef RESTUNTS_HIRES_SHAPE3D_CAR_GROUND_H
#define RESTUNTS_HIRES_SHAPE3D_CAR_GROUND_H

#include "../shape3d/shape3d.h"

#if defined(RESTUNTS_SDL3)
/* Cache the neutral visible underside before wheel suspension changes vertices. */
void shape3d_cache_car_ground_offset(legacy_u16 index, const struct SHAPE3D *shape);
/* Invalidate cached geometry before car resources are replaced or freed. */
void shape3d_reset_car_ground_offsets(void);
#endif

#endif
