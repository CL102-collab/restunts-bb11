#include "track_preview_mode.h"
#ifdef RESTUNTS_SDL3
#include "hires.h"

legacy_s16 hires_track_preview_live(legacy_s16 track_active)
{
	return track_active && hires_enabled();
}
#endif
