#ifndef RESTUNTS_RENDERER_SETTINGS_H
#define RESTUNTS_RENDERER_SETTINGS_H

/* Compile-time routing only; implementations belong to each renderer. */
#if defined(RESTUNTS_SDL3)
#include "../hires/settings.h"
#define renderer_active hires_active
#define renderer_settings_select_display hires_settings_select_display
#else
#include "../supersight/settings.h"
#define renderer_active supersight_enabled
#define renderer_settings_select_display supersight_settings_select_display
#endif

#endif
