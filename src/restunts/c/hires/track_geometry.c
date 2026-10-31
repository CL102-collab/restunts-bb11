#include "track_geometry.h"
#include "../game_input.h"
legacy_u8 hires_geometry_repaint_enabled(void)
{
#ifdef RESTUNTS_HEADLESS
	return 0;
#else
	return supersight_enabled != 0;
#endif
}
