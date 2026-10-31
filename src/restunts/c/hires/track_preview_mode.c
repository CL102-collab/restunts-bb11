#include "track_preview_mode.h"
#ifdef RESTUNTS_SDL3
#include "hires.h"

legacy_s16 hires_track_preview_live(void)
{
	return hires_enabled();
}
#endif
