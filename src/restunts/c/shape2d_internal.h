#ifndef RESTUNTS_SHAPE2D_INTERNAL_H
#define RESTUNTS_SHAPE2D_INTERNAL_H

#include "shape2d.h"

extern legacy_s8 window_row_table_overflow_message[];
extern legacy_s8 video_window_resource_name[];
extern legacy_s8 window_release_order_message[];
/* Reserved seg012 storage: SPRITE structs followed by line offsets. */
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
extern legacy_u8 wnd_defs[];
#else
extern legacy_u8 *far wnd_defs;
#endif
/* Near pointer relative to seg012 for the current SPRITE in wnd_defs. */
extern legacy_s8 *far next_wnd_def;
extern struct SPRITE far drawing_sprite;
extern struct SPRITE far screen_sprite;
extern struct SPRITE far *mcga_backbuffer_sprite;
extern struct SPRITE far *mouse_background_sprite;
extern struct SPRITE far *mouse_medium_sprite;
extern struct SPRITE far *mouse_small_sprite;
extern legacy_s8 mouse_background_dirty;
extern legacy_u8 sprite_background_stack_depth;
#define SPRITE_BACKGROUND_STACK_CAPACITY 4U
extern struct SPRITE far *sprite_ptrs[SPRITE_BACKGROUND_STACK_CAPACITY];
extern legacy_s16 sprite_background_saved_x[SPRITE_BACKGROUND_STACK_CAPACITY];
extern legacy_s16 sprite_background_saved_y[SPRITE_BACKGROUND_STACK_CAPACITY];
extern legacy_u8 far sprite_palette_map[];
extern legacy_u16 raster_fill_pattern;
extern legacy_u16 raster_alternate_color;

legacy_u16 shape2d_get_word(const legacy_u8 far *source);
void shape2d_put_word(legacy_u8 far *destination, legacy_u16 value);
legacy_u16 shape2d_get_line_offset(legacy_u16 sprite_segment, legacy_u16 y);
legacy_u8 far *shape2d_line_pointer(const struct SPRITE far *sprite, legacy_u16 offset);
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
#define shape2d_line_base(sprite) 0U
#else
#define shape2d_line_base(sprite) dos_memory_pointer_offset((sprite)->sprite_lineofs)
#endif

#endif
