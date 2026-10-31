#ifndef RESTUNTS_HIRES_SETTINGS_H
#define RESTUNTS_HIRES_SETTINGS_H

#include "../legacy.h"

extern legacy_u8 supersight_enabled;

/* Input routing stays with the game; display policy stays with the renderer. */
void hires_settings_select_display(legacy_s16 key);

#ifdef RESTUNTS_SDL3
#include "frame_adaptive.h"
#include "hires.h"

struct STARTUP_OPTIONS;
void hires_settings_reset_startup(struct STARTUP_OPTIONS *options);
void hires_settings_parse_startup(const legacy_s8 *argument, struct STARTUP_OPTIONS *options);
void hires_settings_apply_startup(const struct STARTUP_OPTIONS *options);
#endif

#endif
