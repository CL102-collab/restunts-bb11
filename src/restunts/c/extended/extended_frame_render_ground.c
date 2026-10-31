#ifdef RESTUNTS_SDL3
#include "extended_frame_internal.h"
#include <stdlib.h>
#include <string.h>

legacy_u8 hires_track_preserve_ground_polygon(void)
{
	return supersight_enabled != 0;
}
legacy_u8 hires_track_paint_terrain_face(const struct VECTOR *points, legacy_u16 count,
										 const legacy_u8 *primitive,
										 const struct FRAME_EXTENDED_TERRAIN *terrain,
										 legacy_s16 *result)
{
	if (!supersight_enabled || !terrain->graded) {
		return 0;
	}
	*result = frame_extended_paint_precise_terrain_face(points, count, primitive, terrain);
	return 1;
}

#endif
