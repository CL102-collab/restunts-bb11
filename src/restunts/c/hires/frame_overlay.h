#ifndef RESTUNTS_HIRES_FRAME_OVERLAY_H
#define RESTUNTS_HIRES_FRAME_OVERLAY_H

#include "../math/math.h"

void frame_supersight_reset(void);
void frame_supersight_show_status(const legacy_s8 *name);
/* Static menus retain mode-change notices independently of F11 diagnostics. */
legacy_s16 frame_status_overlay_active(void);
legacy_s16 frame_status_expire_idle(void);
struct RECTANGLE *frame_status_draw_text(void);
/* Queries used when composing the generic diagnostic overlay. */
legacy_s16 frame_status_redraw_pending(void);
legacy_s16 frame_status_expire_at(legacy_u32 now);
struct RECTANGLE *frame_status_redraw_bounds(void);
legacy_u16 frame_hires_fps_target(legacy_u16 target);
legacy_u16 frame_hires_filename_reserved(legacy_s16 y, legacy_u16 reserved);

#endif
