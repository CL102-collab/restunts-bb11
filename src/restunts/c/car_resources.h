#ifndef RESTUNTS_CAR_RESOURCES_H
#define RESTUNTS_CAR_RESOURCES_H

#include "legacy.h"

/* Selected car and opponent resource names and result labels. */

extern legacy_s8 car_resource_name[];
extern legacy_s8 opponent_resource_name[];
extern legacy_s8 gnam_string[];
extern legacy_s8 gsna_string[];
extern legacy_s8 opponent_highscore_name[];

/* Optional one-byte `fand` car resource: value 1 enables surface adhesion.
 * Loaded separately from SIMD so stock resource and replay layouts stay fixed. */
extern legacy_u8 car_surface_adhesion[2];
void reset_car_capabilities(void);

#endif
