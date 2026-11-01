#ifndef RESTUNTS_PHYSICS_COLLISION_INTERNAL_H
#define RESTUNTS_PHYSICS_COLLISION_INTERNAL_H

#include "../track/track_types.h"

/* Shared native model geometry, independent of track format and placement. */
legacy_u16 physics_collision_model_points(legacy_u8 tile_element,
										  const struct VECTOR **dependency_points);
void physics_transform_collision_points(const struct VECTOR *dependency_points,
										struct VECTOR *output, legacy_u16 count,
										legacy_u16 orientation, legacy_u16 center_x,
										legacy_u16 center_z, legacy_u16 terrain_height);

#endif
