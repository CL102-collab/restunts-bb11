#include "track_geometry.h"
#include "../game_input.h"
legacy_u8 hires_track_geometry_repaint_unchanged(legacy_u8 material, legacy_u8 original)
{
#ifdef RESTUNTS_HEADLESS
	(void)material;
	(void)original;
	return 0;
#else
	return supersight_enabled != 0 && material == original;
#endif
}
