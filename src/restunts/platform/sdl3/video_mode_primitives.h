#ifndef RESTUNTS_SDL3_VIDEO_MODE_PRIMITIVES_H
#define RESTUNTS_SDL3_VIDEO_MODE_PRIMITIVES_H

#include "../../c/legacy.h"

struct POINT2D;

/* Draw the authored EGA/CGA material into the companion framebuffer only.
 * The caller freezes companion writes before the ordinary VGA pass and ends
 * primitive capture after this function. Legacy execution residue is untouched. */
void sdl3_video_mode_render_primitive(const legacy_u8 *record, struct POINT2D *points);

#endif
