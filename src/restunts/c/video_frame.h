#ifndef RESTUNTS_VIDEO_FRAME_H
#define RESTUNTS_VIDEO_FRAME_H

#include "math/math.h"

/* Frame rectangles and backbuffer policy shared with screen controllers. */

extern struct RECTANGLE *alternate_frame_rects;
extern struct RECTANGLE empty_rect;
extern struct RECTANGLE full_screen_rect;

legacy_s16 video_backbuffer_copy_required(void);

#ifdef RESTUNTS_SDL3
/* Keep frame boundaries available to controllers without SDL type dependencies. */
void sdl3_video_begin_frame(void);
void sdl3_video_begin_track_frame(legacy_u8 adaptive);
/* Add already completed geometry preparation to the currently measured frame.
 * The caller consumes each prepared batch once; idle time is never included. */
void sdl3_video_add_render_work(legacy_u64 elapsed_ns);
void sdl3_video_end_frame(void);
#endif

#endif
