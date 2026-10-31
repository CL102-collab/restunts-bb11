#include "extended_math.h"

#ifdef RESTUNTS_SDL3
#include "extended_geometry.h"
#include "../track/track_collision.h"

struct PLANE far *extended_math_selected_plane(legacy_s16 plane_index)
{
	return plane_index >= EXTENDED_GEOMETRY_PLANE_INDEX ? current_planptr : &planptr[plane_index];
}
#endif
