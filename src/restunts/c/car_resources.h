#ifndef RESTUNTS_CAR_RESOURCES_H
#define RESTUNTS_CAR_RESOURCES_H

#include "legacy.h"

/* Selected car and opponent resource names and result labels. */

extern legacy_s8 car_resource_name[];
extern legacy_s8 opponent_resource_name[];
extern legacy_s8 gnam_string[];
extern legacy_s8 gsna_string[];
extern legacy_s8 opponent_highscore_name[];

/* Optional one-byte `fand` enables adhesion. Optional `adhs` version 1 stores
 * max distance (u16 LE), minimum alignment cosine (u16 LE, Q14), and a material
 * mask (u8), after its version byte. Missing settings retain legacy defaults.
 * Loaded separately from SIMD so stock resource and replay layouts stay fixed. */
#define CAR_ADHESION_SETTINGS_VERSION 1U
#define CAR_ADHESION_SETTINGS_SIZE 6U
#define CAR_ADHESION_DEFAULT_DISTANCE 12U
#define CAR_ADHESION_MAX_DISTANCE 511U
#define CAR_ADHESION_DEFAULT_ALIGNMENT 8192U
#define CAR_ADHESION_MAX_ALIGNMENT 16384U
#define CAR_ADHESION_SURFACE_MASK 15U

struct CAR_SURFACE_ADHESION_SETTINGS {
	legacy_u16 max_distance;
	legacy_u16 min_alignment;
	legacy_u8 surface_mask;
};

extern legacy_u8 car_surface_adhesion[2];
extern struct CAR_SURFACE_ADHESION_SETTINGS car_surface_adhesion_settings[2];
void reset_car_capabilities(void);

#endif
