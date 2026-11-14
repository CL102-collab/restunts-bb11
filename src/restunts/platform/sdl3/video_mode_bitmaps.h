#ifndef RESTUNTS_SDL3_VIDEO_MODE_BITMAPS_H
#define RESTUNTS_SDL3_VIDEO_MODE_BITMAPS_H

#include "../../c/legacy.h"

struct SHAPE2D;

/* Copy the original resource's authored !egN/!cgN tables without altering any
 * VGA bitmap pixels. Parsed resources contain signed-run encoded table data. */
void sdl3_video_bitmap_register(const void *resource, legacy_u32 size, legacy_u8 parsed);
void sdl3_video_bitmap_forget(const void *begin, legacy_u32 size);
void sdl3_video_bitmap_reset(void);
void sdl3_video_bitmap_select(void *resource, legacy_u8 index);

/* Normal DOS loaders convert before drawing, preserving source checker phase.
 * Alternate loaders retain VGA pixels for draw-time numbered palette maps. */
void sdl3_video_bitmap_set_raw(void *resource);
legacy_u8 sdl3_video_bitmap_source_phase(const void *source);

/* The returned table belongs to the source resource and changes on palette
 * selection. Capture table[color] in composed samples, not the table pointer.
 * EGA entries are packed nibbles; CGA entries are four packed two-bit samples.
 * NULL means an unregistered or tableless source and requires color matching. */
const legacy_u8 *sdl3_video_bitmap_table(const void *source, legacy_u8 mode);

/* Fallback for bitmap samples without an explicitly registered source. */
const legacy_u8 *sdl3_video_bitmap_default_table(legacy_u8 mode);

/* DOS bitmap copies retain their nominal resource row phase after moving. */
legacy_u8 sdl3_video_bitmap_row_phase(const struct SHAPE2D *shape, legacy_u16 draw_y);

#endif
