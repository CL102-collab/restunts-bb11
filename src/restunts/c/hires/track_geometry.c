#if defined(RESTUNTS_SDL3)
#include "track_geometry.h"
#include "settings.h"
legacy_u8 hires_geometry_repaint_enabled(void)
{
#ifdef RESTUNTS_HEADLESS
	return 0;
#else
	return hires_active != 0;
#endif
}
#endif
