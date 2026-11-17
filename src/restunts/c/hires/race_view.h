#ifndef RESTUNTS_HIRES_RACE_VIEW_H
#define RESTUNTS_HIRES_RACE_VIEW_H
#include "../legacy.h"
struct RACE_HIRES_VIEWPORT {
	legacy_u8 hires_active;
	legacy_u8 widescreen_active;
	legacy_s32 widescreen_width;
	legacy_s32 widescreen_height;
	legacy_u32 widescreen_generation;
};
void race_hires_clip_dashboard(void);
legacy_s16 race_hires_viewport_changed(const struct RACE_HIRES_VIEWPORT *cache);
void race_hires_cache_viewport(struct RACE_HIRES_VIEWPORT *cache);
#endif
