#include <stddef.h>
#ifdef RESTUNTS_SDL3
#include "../../platform/sdl3/sdl3.h"
#endif
#ifdef RESTUNTS_AMIGA
#include "../../platform/amiga/amiga.h"
#endif
#include "../externs.h"
#include "../fatal.h"
#include "../legacy.h"
#include "../memmgr.h"
#include "../platform.h"
#include "shape2d.h"
#include "shape2d_internal.h"
#include "../game_input.h"
#include "../video/video_pages.h"
#if defined(RESTUNTS_SDL3) && defined(RESTUNTS_FULL) && !defined(RESTUNTS_HEADLESS) &&             \
	!defined(RESTUNTS_TEMPLEOS)
#include "../../platform/sdl3/video_mode.h"
#endif
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
static struct SPRITE native_background_contexts[SPRITE_BACKGROUND_STACK_CAPACITY]
											   [SPRITE_STATE_COUNT];
#endif

#define WINDOW_DEFINITION_TABLE_BYTES 3600U
#define WINDOW_ALLOCATION_OVERHEAD 18L
#define SPRITE_WINDOW_LEGACY_ARGUMENT 15U
#define MCGA_WINDOW_WIDTH 320U
#define MCGA_WINDOW_HEIGHT 200U
#define DOS_PARAGRAPH_SHIFT 4U
#define DOS_WINDOW_EXTRA_PARAGRAPH_COUNT 1U

struct SPRITE far *sprite_make_wnd(legacy_u16 width, legacy_u16 height, legacy_u16 unused_flags)
{
	(void)unused_flags;

#if !defined(RESTUNTS_SDL3) && !defined(RESTUNTS_AMIGA)
	legacy_u16 wnddefseg = dos_memory_pointer_segment(&wnd_defs);
#endif

	legacy_s16 pages = ((width * height + SHAPE2D_HEADER_SIZE) >> DOS_PARAGRAPH_SHIFT) +
					   DOS_WINDOW_EXTRA_PARAGRAPH_COUNT;
	legacy_s8 far *shapebuf = mmgr_alloc_pages("MCGA WINDOW", pages);

	struct SHAPE2D far *header =
		(struct SHAPE2D far *)dos_memory_make_pointer(dos_memory_pointer_segment(shapebuf), 0);
	shape2d_put_word((legacy_u8 far *)header + offsetof(struct SHAPE2D, width), width);
	shape2d_put_word((legacy_u8 far *)header + offsetof(struct SHAPE2D, height), height);
	shape2d_put_word((legacy_u8 far *)header + offsetof(struct SHAPE2D, position_x), 0U);
	shape2d_put_word((legacy_u8 far *)header + offsetof(struct SHAPE2D, position_y), 0U);
	shape2d_put_word((legacy_u8 far *)header + offsetof(struct SHAPE2D, centre_x), 0U);
	shape2d_put_word((legacy_u8 far *)header + offsetof(struct SHAPE2D, centre_y), 0U);

	// it is safe to read/write the pointers to next_wnd_def/wnd_defs, but not the contents
	legacy_s8 *wnd = next_wnd_def;
	legacy_s8 *nextwnd = next_wnd_def + sizeof(struct SPRITE) + height * sizeof(legacy_u16);
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
	if (nextwnd > (legacy_s8 *)wnd_defs + WINDOW_DEFINITION_TABLE_BYTES) {
#else
	if (dos_memory_pointer_offset(nextwnd) >=
		dos_memory_pointer_offset(&wnd_defs) + WINDOW_DEFINITION_TABLE_BYTES) {
#endif
		fatal_error(window_row_table_overflow_message);
	}
	next_wnd_def = nextwnd;

	// get a writable far pointer to the render_window_sprite
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
	struct SPRITE *farwnd = (struct SPRITE *)wnd;
#else
	struct SPRITE far *farwnd = dos_memory_make_pointer(wnddefseg, dos_memory_pointer_offset(wnd));
#endif

	legacy_u8 *lineofsptr = (legacy_u8 *)(wnd + sizeof(struct SPRITE));
	farwnd->sprite_bitmapptr = header;
	farwnd->sprite_lineofs = lineofsptr;
	farwnd->sprite_left = 0;
	farwnd->sprite_raster_left = 0;
	farwnd->sprite_right = width;
	farwnd->sprite_pitch = width; // ??
	farwnd->sprite_top = 0;
	farwnd->sprite_bottom = height;
	farwnd->sprite_buffer_width = width;
	farwnd->sprite_raster_right = width;

	// create a writable far pointer to the line offsets
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
	legacy_u8 *farlineofsptr = lineofsptr;
#else
	legacy_u8 far *farlineofsptr =
		(legacy_u8 far *)dos_memory_make_pointer(wnddefseg, dos_memory_pointer_offset(lineofsptr));
#endif
	legacy_u16 lineofs = SHAPE2D_HEADER_SIZE;
	// One of several counted loops where the original uses `loop`, which runs
	// 65536 times on a count of zero while this runs none. Reaching it needs a
	// zero-height window; the same applies to the unflip and palette loops in
	// this file.
	for (legacy_s16 i = 0; i < height; i++) {
		shape2d_put_word(farlineofsptr, lineofs);
		farlineofsptr += LEGACY_WORD_BYTES;
		lineofs += width;
	}

#if defined(RESTUNTS_SDL3) && defined(RESTUNTS_FULL) && !defined(RESTUNTS_HEADLESS) &&             \
	!defined(RESTUNTS_TEMPLEOS)
	sdl3_video_mode_target(farwnd);
#endif
	return farwnd;
}

void sprite_free_wnd(struct SPRITE far *render_window_sprite)
{
	// The height comes from the bitmap header, not from the SPRITE: the
	// original walks through sprite_bitmapptr to reach SHAPE2D.s2d_height.
	// sprite_make_wnd initializes both heights alike, and normal clipping edits
	// the drawing_sprite working copy rather than the stored window SPRITE, so using
	// the bitmap field here is structural parity rather than a clipping repair.
	legacy_u16 spritesize =
		sizeof(struct SPRITE) +
		shape2d_get_height(render_window_sprite->sprite_bitmapptr) * sizeof(legacy_u16);
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
	if ((legacy_s8 *)render_window_sprite + spritesize != next_wnd_def) {
#else
	if (dos_memory_pointer_offset(render_window_sprite) + spritesize !=
		dos_memory_pointer_offset(next_wnd_def)) {
#endif
		fatal_error(window_release_order_message);
	}
	next_wnd_def = next_wnd_def - spritesize;
#ifdef RESTUNTS_SDL3
	hires_forget(dos_memory_make_pointer(
		dos_memory_pointer_segment(render_window_sprite->sprite_bitmapptr), 0));
#endif
	mmgr_release((void far *)render_window_sprite->sprite_bitmapptr);
}

void sprite_select_target(struct SPRITE far *target_sprite)
{
	fmemcpy(&drawing_sprite, target_sprite, sizeof(struct SPRITE));
#if defined(RESTUNTS_SDL3) && defined(RESTUNTS_FULL) && !defined(RESTUNTS_HEADLESS) &&             \
	!defined(RESTUNTS_TEMPLEOS)
	sdl3_video_mode_target(&drawing_sprite);
#endif
}

void sprite_select_screen(void)
{
	sprite_select_target(&screen_sprite);
}

void sprite_select_screen_compat(void)
{
	sprite_select_screen();
}

void sprite_select_screen_and_clear(void)
{
	sprite_select_screen();
	sprite_clear_target(0);
}

void sprite_select_render_window(void)
{
#ifdef RESTUNTS_AMIGA
	if (render_window_sprite == &screen_sprite) {
		amiga_video_begin_frame();
		mouse_draw_opaque_check();
	}
#endif
	sprite_select_target(render_window_sprite);
}

void sprite_select_render_window_and_clear(void)
{
	sprite_select_render_window();
	sprite_clear_target(0);
}

void sprite_save_context(struct SPRITE saved_context[SPRITE_STATE_COUNT])
{
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
	saved_context[0] = drawing_sprite;
	saved_context[1] = screen_sprite;
#else
	fmemcpy(saved_context, &drawing_sprite, sizeof(struct SPRITE) * SPRITE_STATE_COUNT);
#endif
}

void sprite_restore_context(struct SPRITE saved_context[SPRITE_STATE_COUNT])
{
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
	drawing_sprite = saved_context[0];
	screen_sprite = saved_context[1];
#else
	fmemcpy(&drawing_sprite, saved_context, sizeof(struct SPRITE) * SPRITE_STATE_COUNT);
#endif
#if defined(RESTUNTS_SDL3) && defined(RESTUNTS_FULL) && !defined(RESTUNTS_HEADLESS) &&             \
	!defined(RESTUNTS_TEMPLEOS)
	sdl3_video_mode_target(&drawing_sprite);
#endif
}

legacy_s16 sprite_push_background(legacy_s16 left, legacy_s16 right, legacy_s16 top,
								  legacy_s16 bottom)
{
	legacy_s16 width = LEGACY_S16_WRAP_SUB(right, left);
	legacy_s16 height = LEGACY_S16_WRAP_SUB(bottom, top);
	legacy_s32 required = ((legacy_s32)width * height) /
							  ((legacy_s32)video_shape_width_scale * video_buffer_height_divisor) +
						  WINDOW_ALLOCATION_OVERHEAD;
	if (mmgr_get_res_ofs_diff_scaled() <= (legacy_u32)required) {
		return 0;
	}

	mouse_draw_opaque_check();
	struct SPRITE far *window =
		sprite_make_wnd((legacy_u16)width, (legacy_u16)height, SPRITE_WINDOW_LEGACY_ARGUMENT);
	legacy_u16 index = sprite_background_stack_depth;
	sprite_ptrs[index] = window;
	sprite_background_saved_x[index] = left;
	sprite_background_saved_y[index] = top;
	struct SPRITE saved_sprites[SPRITE_STATE_COUNT];
	sprite_save_context(saved_sprites);
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
	fmemcpy(native_background_contexts[index], saved_sprites, sizeof(saved_sprites));
#else
	fmemcpy(sprite_background_state_stack + index * sizeof(saved_sprites), saved_sprites,
			sizeof(saved_sprites));
#endif
	sprite_select_screen();
	sprite_clear_shape_alt(window->sprite_bitmapptr, left, top);
	sprite_background_stack_depth++;
	return 1;
}

void sprite_pop_background(void)
{
	if (sprite_background_stack_depth == 0) {
		return;
	}
	sprite_background_stack_depth--;
	legacy_u16 index = sprite_background_stack_depth;
	mouse_draw_opaque_check();
	sprite_shape_to_1(sprite_ptrs[index]->sprite_bitmapptr, sprite_background_saved_x[index],
					  sprite_background_saved_y[index]);
	struct SPRITE saved_sprites[SPRITE_STATE_COUNT];
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
	fmemcpy(saved_sprites, native_background_contexts[index], sizeof(saved_sprites));
#else
	fmemcpy(saved_sprites, sprite_background_state_stack + index * sizeof(saved_sprites),
			sizeof(saved_sprites));
#endif
	sprite_restore_context(saved_sprites);
	sprite_free_wnd(sprite_ptrs[index]);
	mouse_draw_transparent_check();
}

void mouse_draw_opaque(void)
{
	struct SPRITE saved_sprites[SPRITE_STATE_COUNT];

	sprite_save_context(saved_sprites);
	sprite_select_screen();
	sprite_putimage(mouse_background_sprite->sprite_bitmapptr);
	sprite_restore_context(saved_sprites);
	mouse_background_dirty = 0;
}

void mouse_draw_transparent(void)
{
	legacy_s16 aligned_x = mouse_xpos - mouse_xpos % video_x_alignment;
	struct SPRITE saved_sprites[SPRITE_STATE_COUNT];
	sprite_save_context(saved_sprites);
	sprite_select_screen();
	sprite_clear_shape_alt(mouse_background_sprite->sprite_bitmapptr, aligned_x, mouse_ypos);
	sprite_putimage_and(mouse_medium_sprite->sprite_bitmapptr, mouse_xpos, mouse_ypos);
	sprite_putimage_or(mouse_small_sprite->sprite_bitmapptr, mouse_xpos, mouse_ypos);
	sprite_restore_context(saved_sprites);
	mouse_background_dirty = 1;
}

void sprite_clear_target(legacy_u8 color)
{
	legacy_s16 top = drawing_sprite.sprite_top;
	legacy_s16 left = drawing_sprite.sprite_left;
	legacy_s16 right = drawing_sprite.sprite_right;
	legacy_s16 pitch = drawing_sprite.sprite_pitch;
	legacy_u8 far *bitmapptr = (legacy_u8 far *)drawing_sprite.sprite_bitmapptr;

	legacy_s16 lines = drawing_sprite.sprite_bottom - top;
	if (lines <= 0) {
		return;
	}

	legacy_u16 ofs = LEGACY_U16_WRAP_ADD(
		shape2d_get_line_offset(dos_memory_pointer_segment(&drawing_sprite), (legacy_u16)top),
		(legacy_u16)left);

	legacy_s16 width = right - left;
	if (width <= 0) {
		return;
	}

	legacy_s16 widthdiff = pitch - width;

	if (video_pages_uses_raster_hooks(bitmapptr) != 0) {
		for (legacy_s16 i = 0; i < lines; i++) {
			video_pages_fill_span(bitmapptr, ofs, (legacy_u16)width, color);
			ofs = LEGACY_U16_WRAP_ADD(ofs, pitch);
		}
		return;
	}
	for (legacy_s16 i = 0; i < lines; i++) {
		for (legacy_s16 j = 0; j < width; j++) {
			bitmapptr[ofs++] = color;
		}
		ofs += widthdiff;
	}
}

void sprite_present_mcga_backbuffer(void)
{
#ifdef RESTUNTS_AMIGA
	/* The indexed screen is already hidden behind the native planar pages. */
	sprite_select_target(&screen_sprite);
	amiga_video_present();
	return;
#endif
	if (video_pages_is_active() != 0) {
		video_pages_present();
		return;
	}
	if (!mcga_backbuffer_sprite) {
		mcga_backbuffer_sprite =
			sprite_make_wnd(MCGA_WINDOW_WIDTH, MCGA_WINDOW_HEIGHT, SPRITE_WINDOW_LEGACY_ARGUMENT);
	}

	sprite_select_target(&screen_sprite);
	sprite_putimage(mcga_backbuffer_sprite->sprite_bitmapptr);
#ifdef RESTUNTS_SDL3
	sdl3_video_present();
#elif defined(RESTUNTS_AMIGA)
	amiga_video_present();
#endif
}

void sprite_select_mcga_backbuffer(void)
{
#ifdef RESTUNTS_AMIGA
	/* Reuse the indexed screen while the Copper displays the completed
	 * planar frame, saving a second 64 KB indexed allocation and copy. */
	amiga_video_begin_frame();
	sprite_select_target(&screen_sprite);
	return;
#endif
	if (video_pages_is_active() != 0) {
		video_pages_select_backbuffer();
		return;
	}
	if (!mcga_backbuffer_sprite) {
		mcga_backbuffer_sprite =
			sprite_make_wnd(MCGA_WINDOW_WIDTH, MCGA_WINDOW_HEIGHT, SPRITE_WINDOW_LEGACY_ARGUMENT);
	}

	sprite_select_target(mcga_backbuffer_sprite);
}
