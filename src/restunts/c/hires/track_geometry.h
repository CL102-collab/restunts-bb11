#ifndef RESTUNTS_HIRES_TRACK_GEOMETRY_H
#define RESTUNTS_HIRES_TRACK_GEOMETRY_H

#include "../shape3d/shape3d.h"

legacy_u8 hires_track_geometry_material(const struct SHAPE3D *source, const legacy_u8 *primitive,
										legacy_u16 surface, legacy_s16 physical_model);

#endif
