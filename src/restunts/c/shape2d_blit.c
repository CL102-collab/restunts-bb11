#include <stddef.h>
#ifdef RESTUNTS_SDL3
#include "../platform/sdl3/sdl3.h"
#endif
#ifdef RESTUNTS_AMIGA
#include "../platform/amiga/amiga.h"
#endif
#include "externs.h"
#include "fatal.h"
#include "legacy.h"
#include "memmgr.h"
#include "platform.h"
#include "shape2d.h"
#include "shape2d_internal.h"
#include "game_input.h"
#include "video_pages.h"
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
static struct SPRITE native_background_contexts[SPRITE_BACKGROUND_STACK_CAPACITY]
											   [SPRITE_STATE_COUNT];
#endif

#define WINDOW_DEFINITION_TABLE_BYTES 3600U
#define WINDOW_ALLOCATION_OVERHEAD 18L
#define SPRITE_WINDOW_LEGACY_ARGUMENT 15U
#define PALETTE_MAP_TRANSPARENT 255U
#define MCGA_WINDOW_WIDTH 320U
#define MCGA_WINDOW_HEIGHT 200U
#define DOS_PARAGRAPH_SHIFT 4U
#define DOS_WINDOW_EXTRA_PARAGRAPH_COUNT 1U

struct SHAPE2D_CLIP {
	legacy_u16 source;
	legacy_u16 source_advance;
	legacy_u16 destination;
	legacy_u16 destination_advance;
	legacy_u16 width;
	legacy_u16 rows;
};

struct SHAPE2D_RLE_CURSOR {
	legacy_u16 shape_segment;
#ifdef RESTUNTS_AMIGA
	const legacy_u8 *source_base;
#endif
	legacy_u16 source;
	legacy_u16 remaining;
	legacy_u8 value;
	legacy_s16 literal;
};

static void shape2d_write_raster(legacy_u8 far *bitmap, legacy_u16 destination, legacy_u8 value,
								 legacy_s16 operation, legacy_u8 planar)
{
#ifdef RESTUNTS_SDL3
	hires_raster(bitmap, destination, &value, 0, 1, operation, NULL);
	planar = 0;
#endif
	if (planar != 0) {
		if (operation == SHAPE2D_RASTER_OR) {
			value |= video_pages_read_pixel(bitmap, destination);
		} else if (operation != SHAPE2D_RASTER_COPY) {
			value &= video_pages_read_pixel(bitmap, destination);
		}
		video_pages_write_pixel(bitmap, destination, value);
		return;
	}
	if (operation == SHAPE2D_RASTER_OR) {
		bitmap[destination] |= value;
	} else if (operation == SHAPE2D_RASTER_COPY) {
		bitmap[destination] = value;
	} else {
		bitmap[destination] &= value;
	}
}

static void shape2d_apply_planar_run(legacy_u8 far *bitmap, legacy_u16 destination,
									 legacy_u16 count, legacy_u8 value, legacy_s16 operation)
{
	if ((operation == SHAPE2D_RASTER_AND && value == LEGACY_U8_MAX) ||
		(operation == SHAPE2D_RASTER_OR && value == 0U)) {
		return;
	}
	if (operation == SHAPE2D_RASTER_COPY || (operation == SHAPE2D_RASTER_AND && value == 0U) ||
		(operation == SHAPE2D_RASTER_OR && value == LEGACY_U8_MAX)) {
		video_pages_fill_span(bitmap, destination, count, value);
		return;
	}
	/* RLE runs contain at most 128 pixels. Visit each plane together so
	 * repeated masks change the read/write registers at most four times. */
	for (legacy_u16 plane = 0; plane < VGA_PLANE_COUNT; plane++) {
		for (legacy_u16 index = plane; index < count; index += VGA_PLANE_COUNT) {
			shape2d_write_raster(bitmap, LEGACY_U16_WRAP_ADD(destination, index), value, operation,
								 1);
		}
	}
}

static void shape2d_render_rle(struct SHAPE2D far *shape, legacy_u16 x, legacy_u16 y,
							   legacy_s16 operation)
{
	legacy_u16 shape_segment = dos_memory_pointer_segment(shape);
#ifdef RESTUNTS_AMIGA
	/* Resource allocation/compaction cannot occur inside a blit. Cache only
	 * for this draw, retaining the wrapping 16-bit source offset below. */
	const legacy_u8 *source_base = dos_memory_make_pointer(shape_segment, 0);
#endif
	legacy_u16 source = LEGACY_U16_WRAP_ADD(dos_memory_pointer_offset(shape), SHAPE2D_HEADER_SIZE);
	legacy_u16 width = shape2d_get_width(shape);
	legacy_u16 line_entry = LEGACY_U16_WRAP_ADD(shape2d_line_base(&drawing_sprite),
												LEGACY_U16_WRAP_MUL(y, LEGACY_WORD_BYTES));
	legacy_u16 destination =
		LEGACY_U16_WRAP_ADD(shape2d_get_word(shape2d_line_pointer(&drawing_sprite, line_entry)), x);
	legacy_u8 far *bitmap = (legacy_u8 far *)dos_memory_make_pointer(
		dos_memory_pointer_segment(drawing_sprite.sprite_bitmapptr), 0);
	legacy_u16 remaining = width;
	legacy_u8 planar = video_pages_uses_raster_hooks(bitmap);
	legacy_u8 value = 0;
	for (;;) {
#ifdef RESTUNTS_AMIGA
		legacy_u8 control_bits = source_base[source];
#else
		legacy_u8 far *source_ptr = (legacy_u8 far *)dos_memory_make_pointer(shape_segment, source);
		legacy_u8 control_bits = *source_ptr;
#endif
		source++;
		legacy_s8 control = LEGACY_S8_FROM_BITS(control_bits);
		if (control == 0) {
			return;
		}
		legacy_s16 literal = control < 0;
		legacy_u16 count;
		if (literal != 0) {
			count = (legacy_u8)(0U - control_bits);
		} else {
			count = control_bits;
#ifdef RESTUNTS_AMIGA
			value = source_base[source];
#else
			source_ptr = (legacy_u8 far *)dos_memory_make_pointer(shape_segment, source);
			value = *source_ptr;
#endif
			source++;
		}
		do {
			legacy_u16 span = 1U;
			if (planar != 0 && LEGACY_S16_FROM_BITS(remaining) > 0) {
				span = count < remaining ? count : remaining;
				if (literal != 0) {
					video_pages_raster_span(
						bitmap, destination,
						(legacy_u8 far *)dos_memory_make_pointer(shape_segment, 0), source, span,
						operation, NULL);
					source = LEGACY_U16_WRAP_ADD(source, span);
				} else {
					shape2d_apply_planar_run(bitmap, destination, span, value, operation);
				}
			} else {
				if (literal != 0) {
#ifdef RESTUNTS_AMIGA
					value = source_base[source];
#else
					source_ptr = (legacy_u8 far *)dos_memory_make_pointer(shape_segment, source);
					value = *source_ptr;
#endif
					source++;
				}
				shape2d_write_raster(bitmap, destination, value, operation, planar);
			}
			destination = LEGACY_U16_WRAP_ADD(destination, span);
			legacy_u16 old_remaining = remaining;
			remaining = LEGACY_U16_WRAP_SUB(remaining, span);
			if (old_remaining == LEGACY_U16_SIGN_BIT || LEGACY_S16_FROM_BITS(remaining) <= 0) {
				line_entry = LEGACY_U16_WRAP_ADD(line_entry, LEGACY_WORD_BYTES);
				destination = LEGACY_U16_WRAP_ADD(
					shape2d_get_word(shape2d_line_pointer(&drawing_sprite, line_entry)), x);
				remaining = width;
			}
			count = LEGACY_U16_WRAP_SUB(count, span);
		} while (count != 0);
	}
}

static void shape2d_render_rle_at_anchor(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y,
										 legacy_s16 operation)
{
	shape2d_render_rle(shape, shape2d_anchored_x(shape, x), shape2d_anchored_y(shape, y),
					   operation);
}

/* Several entry points differ only in the raster operation they apply at
 * the position stored in the sprite header. */
static void shape2d_render_rle_at_position(struct SHAPE2D far *shape, legacy_s16 operation)
{
	shape2d_render_rle(shape, shape2d_get_pos_x(shape), shape2d_get_pos_y(shape), operation);
}

void shape2d_render_bmp_as_mask(struct SHAPE2D far *shape)
{
	shape2d_render_rle_at_position(shape, SHAPE2D_RASTER_AND);
}

void shape2d_rle_mask_at_anchor(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y)
{
	shape2d_render_rle_at_anchor(shape, x, y, SHAPE2D_RASTER_AND);
}

void shape2d_rle_mask(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y)
{
	shape2d_render_rle(shape, (legacy_u16)x, (legacy_u16)y, SHAPE2D_RASTER_AND);
}

void shape2d_rle_or_far_pointer(legacy_u16 offset, legacy_u16 segment)
{
	struct SHAPE2D far *shape = (struct SHAPE2D far *)dos_memory_make_pointer(segment, offset);
	shape2d_render_rle_at_position(shape, SHAPE2D_RASTER_OR);
}

void shape2d_rle_copy(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y)
{
	shape2d_render_rle(shape, (legacy_u16)x, (legacy_u16)y, SHAPE2D_RASTER_COPY);
}

void shape2d_rle_copy_at_position(struct SHAPE2D far *shape)
{
	shape2d_render_rle_at_position(shape, SHAPE2D_RASTER_COPY);
}

void shape2d_rle_copy_at_anchor(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y)
{
	shape2d_render_rle_at_anchor(shape, x, y, SHAPE2D_RASTER_COPY);
}

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

static legacy_s16 shape2d_clip_blit_rows(legacy_u16 width, legacy_u16 *y, legacy_u16 *source,
										 legacy_u16 *clipped_rows)
{
	legacy_u16 sum;
	legacy_u16 overflow;
	if (LEGACY_S16_FROM_BITS((*y)) < LEGACY_S16_FROM_BITS(drawing_sprite.sprite_top)) {
		sum = LEGACY_U16_WRAP_ADD((*y), (*clipped_rows));
		if (LEGACY_S16_FROM_BITS(sum) <= LEGACY_S16_FROM_BITS(drawing_sprite.sprite_top)) {
			return 0;
		}
		legacy_u16 visible = LEGACY_U16_WRAP_SUB(sum, drawing_sprite.sprite_top);
		overflow = LEGACY_U16_WRAP_SUB((*clipped_rows), visible);
		(*source) = LEGACY_U16_WRAP_ADD((*source), (legacy_u16)((legacy_u32)overflow * width));
		(*clipped_rows) = visible;
		(*y) = drawing_sprite.sprite_top;
	}
	sum = LEGACY_U16_WRAP_ADD((*y), (*clipped_rows));
	if (LEGACY_S16_FROM_BITS(sum) > LEGACY_S16_FROM_BITS(drawing_sprite.sprite_bottom)) {
		overflow = LEGACY_U16_WRAP_SUB(sum, drawing_sprite.sprite_bottom);
		if (LEGACY_S16_FROM_BITS((*clipped_rows)) <= LEGACY_S16_FROM_BITS(overflow)) {
			return 0;
		}
		(*clipped_rows) = LEGACY_U16_WRAP_SUB((*clipped_rows), overflow);
	}

	return 1;
}

static legacy_s16 shape2d_clip_blit(struct SHAPE2D far *shape, legacy_u16 x, legacy_u16 y,
									struct SHAPE2D_CLIP *clip)
{
	legacy_u16 width = shape2d_get_width(shape);
	legacy_u16 height = shape2d_get_height(shape);
	legacy_u16 source = LEGACY_U16_WRAP_ADD(dos_memory_pointer_offset(shape), SHAPE2D_HEADER_SIZE);
	legacy_u16 clipped_rows = height;
	if (!shape2d_clip_blit_rows(width, &y, &source, &clipped_rows)) {
		return 0;
	}

	legacy_u16 visible = width;
	clip->source_advance = 0;
	legacy_u16 sum;
	legacy_u16 overflow;
	if (LEGACY_S16_FROM_BITS(x) < LEGACY_S16_FROM_BITS(drawing_sprite.sprite_left)) {
		sum = LEGACY_U16_WRAP_ADD(x, visible);
		if (LEGACY_S16_FROM_BITS(sum) <= LEGACY_S16_FROM_BITS(drawing_sprite.sprite_left)) {
			return 0;
		}
		visible = LEGACY_U16_WRAP_SUB(sum, drawing_sprite.sprite_left);
		overflow = LEGACY_U16_WRAP_SUB(width, visible);
		source = LEGACY_U16_WRAP_ADD(source, overflow);
		legacy_u16 sprite_width =
			LEGACY_U16_WRAP_SUB(drawing_sprite.sprite_right, drawing_sprite.sprite_left);
		if (LEGACY_S16_FROM_BITS(drawing_sprite.sprite_right) <=
			LEGACY_S16_FROM_BITS(drawing_sprite.sprite_left)) {
			return 0;
		}
		if (LEGACY_S16_FROM_BITS(visible) >= LEGACY_S16_FROM_BITS(sprite_width)) {
			visible = sprite_width;
		}
		clip->source_advance = LEGACY_U16_WRAP_SUB(width, visible);
		x = drawing_sprite.sprite_left;
	} else {
		sum = LEGACY_U16_WRAP_ADD(x, visible);
		if (LEGACY_S16_FROM_BITS(sum) >= LEGACY_S16_FROM_BITS(drawing_sprite.sprite_right)) {
			overflow = LEGACY_U16_WRAP_SUB(sum, drawing_sprite.sprite_right);
			if (LEGACY_S16_FROM_BITS(visible) <= LEGACY_S16_FROM_BITS(overflow)) {
				return 0;
			}
			visible = LEGACY_U16_WRAP_SUB(visible, overflow);
			clip->source_advance = overflow;
		}
	}
	if (LEGACY_S16_FROM_BITS(visible) <= 0) {
		return 0;
	}

	clip->source = source;
	clip->destination = LEGACY_U16_WRAP_ADD(
		shape2d_get_line_offset(dos_memory_pointer_segment(&drawing_sprite), y), x);
	clip->destination_advance = LEGACY_U16_WRAP_SUB(drawing_sprite.sprite_pitch, visible);
	clip->width = visible;
	clip->rows = clipped_rows;
	return 1;
}

static legacy_u8 shape2d_rle_read(struct SHAPE2D_RLE_CURSOR *cursor)
{
#ifdef RESTUNTS_AMIGA
	legacy_u8 value = cursor->source_base[cursor->source];
#else
	legacy_u8 value =
		*(legacy_u8 far *)dos_memory_make_pointer(cursor->shape_segment, cursor->source);
#endif
	cursor->source++;
	return value;
}

static legacy_s16 shape2d_rle_next(struct SHAPE2D_RLE_CURSOR *cursor, legacy_u8 *value)
{
	if (cursor->remaining == 0) {
		legacy_u8 control_bits = shape2d_rle_read(cursor);
		legacy_s8 control = LEGACY_S8_FROM_BITS(control_bits);
		if (control == 0) {
			return 0;
		}
		cursor->literal = control < 0;
		if (cursor->literal != 0) {
			cursor->remaining = (legacy_u8)(0U - control_bits);
		} else {
			cursor->remaining = control_bits;
			cursor->value = shape2d_rle_read(cursor);
		}
	}
	if (cursor->literal != 0) {
		cursor->value = shape2d_rle_read(cursor);
	}
	*value = cursor->value;
	cursor->remaining--;
	return 1;
}

static legacy_s16 shape2d_rle_skip(struct SHAPE2D_RLE_CURSOR *cursor, legacy_u16 skip)
{
	legacy_u8 value;

	while (skip != 0) {
		if (!shape2d_rle_next(cursor, &value)) {
			return 0;
		}
		skip--;
	}
	return 1;
}

/* Decode a clipped row in runs so changing VGA planes costs once per run,
 * rather than once per pixel. The cursor may start partway through a run. */
static legacy_s16 shape2d_rle_raster_span(struct SHAPE2D_RLE_CURSOR *cursor, legacy_u8 far *bitmap,
										  legacy_u16 destination, legacy_u16 count,
										  legacy_s16 operation)
{
	while (count != 0U) {
		legacy_u8 value;
		if (!shape2d_rle_next(cursor, &value)) {
			return 0;
		}
		legacy_u16 span = cursor->remaining + 1U;
		if (span > count) {
			span = count;
		}
		if (cursor->literal != 0) {
			video_pages_raster_span(
				bitmap, destination,
				(legacy_u8 far *)dos_memory_make_pointer(cursor->shape_segment, 0),
				LEGACY_U16_WRAP_SUB(cursor->source, 1U), span, operation, NULL);
			cursor->source = LEGACY_U16_WRAP_ADD(cursor->source, span - 1U);
		} else {
			shape2d_apply_planar_run(bitmap, destination, span, value, operation);
		}
		cursor->remaining -= span - 1U;
		destination = LEGACY_U16_WRAP_ADD(destination, span);
		count -= span;
	}
	return 1;
}

static void shape2d_render_rle_clipped(struct SHAPE2D far *shape, legacy_u16 x, legacy_u16 y,
									   legacy_s16 operation)
{
	struct SHAPE2D_CLIP clip;
	if (!shape2d_clip_blit(shape, x, y, &clip)) {
		return;
	}
	legacy_u16 width = shape2d_get_width(shape);
	legacy_u16 height = shape2d_get_height(shape);
	legacy_u16 data_start =
		LEGACY_U16_WRAP_ADD(dos_memory_pointer_offset(shape), SHAPE2D_HEADER_SIZE);
	if (clip.source == data_start && clip.source_advance == 0 && clip.width == width &&
		clip.rows == height) {
		shape2d_render_rle(shape, x, y, operation);
		return;
	}
	struct SHAPE2D_RLE_CURSOR cursor;
	cursor.shape_segment = dos_memory_pointer_segment(shape);
#ifdef RESTUNTS_AMIGA
	cursor.source_base = dos_memory_make_pointer(cursor.shape_segment, 0);
#endif
	cursor.source = data_start;
	cursor.remaining = 0;
	cursor.value = 0;
	cursor.literal = 0;
	legacy_u16 skip = LEGACY_U16_WRAP_SUB(clip.source, data_start);
	if (!shape2d_rle_skip(&cursor, skip)) {
		return;
	}
	legacy_u8 far *bitmap = (legacy_u8 far *)dos_memory_make_pointer(
		dos_memory_pointer_segment(drawing_sprite.sprite_bitmapptr), 0);
	legacy_u16 destination = clip.destination;
	legacy_u16 rows = clip.rows;
	legacy_u8 planar = video_pages_uses_raster_hooks(bitmap);
	legacy_u8 value;
	do {
		if (planar != 0) {
			if (!shape2d_rle_raster_span(&cursor, bitmap, destination, clip.width, operation)) {
				return;
			}
			destination = LEGACY_U16_WRAP_ADD(destination, clip.width);
		} else {
			legacy_u16 count = clip.width;
			do {
				if (!shape2d_rle_next(&cursor, &value)) {
					return;
				}
				shape2d_write_raster(bitmap, destination, value, operation, 0);
				destination++;
				count--;
			} while (count != 0);
		}
		rows--;
		if (rows == 0) {
			return;
		}
		skip = clip.source_advance;
		if (!shape2d_rle_skip(&cursor, skip)) {
			return;
		}
		destination = LEGACY_U16_WRAP_ADD(destination, clip.destination_advance);
	} while (1);
}

void shape2d_rle_copy_clipped(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y)
{
	shape2d_render_rle_clipped(shape, (legacy_u16)x, (legacy_u16)y, SHAPE2D_RASTER_COPY);
}

void shape2d_rle_copy_position_clipped(struct SHAPE2D far *shape)
{
	shape2d_render_rle_clipped(shape, shape2d_get_pos_x(shape), shape2d_get_pos_y(shape),
							   SHAPE2D_RASTER_COPY);
}

void shape2d_rle_mask_position_clipped(struct SHAPE2D far *shape)
{
	shape2d_render_rle_clipped(shape, shape2d_get_pos_x(shape), shape2d_get_pos_y(shape),
							   SHAPE2D_RASTER_AND);
}

void shape2d_rle_or_position_clipped(struct SHAPE2D far *shape)
{
	shape2d_render_rle_clipped(shape, shape2d_get_pos_x(shape), shape2d_get_pos_y(shape),
							   SHAPE2D_RASTER_OR);
}

void shape2d_rle_copy_anchor_clipped(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y)
{
	shape2d_render_rle_clipped(shape, shape2d_anchored_x(shape, x), shape2d_anchored_y(shape, y),
							   SHAPE2D_RASTER_COPY);
}

static void sprite_putimage_at(struct SHAPE2D far *shape, legacy_u16 x, legacy_u16 y,
							   legacy_s16 operation)
{
	struct SHAPE2D_CLIP clip;
	if (!shape2d_clip_blit(shape, x, y, &clip)) {
		return;
	}
	legacy_u16 shape_segment = dos_memory_pointer_segment(shape);
	legacy_u8 far *bitmap = (legacy_u8 far *)dos_memory_make_pointer(
		dos_memory_pointer_segment(drawing_sprite.sprite_bitmapptr), 0);
	legacy_u16 row_count = clip.rows;
	legacy_u16 old_row_count;
	legacy_u8 planar = video_pages_uses_raster_hooks(bitmap);
	legacy_u8 far *source_bitmap = (legacy_u8 far *)dos_memory_make_pointer(shape_segment, 0);
	do {
		if (planar != 0) {
			video_pages_raster_span(bitmap, clip.destination, source_bitmap, clip.source,
									clip.width, operation, sprite_palette_map);
			clip.source = LEGACY_U16_WRAP_ADD(clip.source, clip.width);
			clip.destination = LEGACY_U16_WRAP_ADD(clip.destination, clip.width);
		} else {
			legacy_u16 column_count = clip.width;
			do {
				legacy_u8 far *source_ptr =
					(legacy_u8 far *)dos_memory_make_pointer(shape_segment, clip.source);
				if (operation == SHAPE2D_RASTER_MAP) {
					legacy_u8 mapped_color = sprite_palette_map[*source_ptr];
					if (mapped_color != PALETTE_MAP_TRANSPARENT) {
						bitmap[clip.destination] = mapped_color;
					}
				} else if (operation == SHAPE2D_RASTER_OR) {
					bitmap[clip.destination] |= *source_ptr;
				} else if (operation == SHAPE2D_RASTER_COPY) {
					bitmap[clip.destination] = *source_ptr;
				} else {
					bitmap[clip.destination] &= *source_ptr;
				}
				clip.source++;
				clip.destination++;
				column_count--;
			} while (column_count != 0);
		}
		clip.source = LEGACY_U16_WRAP_ADD(clip.source, clip.source_advance);
		clip.destination = LEGACY_U16_WRAP_ADD(clip.destination, clip.destination_advance);
		old_row_count = row_count;
		row_count = LEGACY_U16_WRAP_SUB(row_count, 1U);
	} while (old_row_count != LEGACY_U16_SIGN_BIT && LEGACY_S16_FROM_BITS(row_count) > 0);
}

static void sprite_putimage_at_anchor(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y,
									  legacy_s16 operation)
{
	sprite_putimage_at(shape, shape2d_anchored_x(shape, x), shape2d_anchored_y(shape, y),
					   operation);
}

void sprite_putimage(struct SHAPE2D far *shape)
{
	sprite_putimage_at(shape, shape2d_get_pos_x(shape), shape2d_get_pos_y(shape),
					   SHAPE2D_RASTER_COPY);
}

void sprite_putimage_at_shape_anchor(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y)
{
	sprite_putimage_at_anchor(shape, x, y, SHAPE2D_RASTER_COPY);
}

void sprite_putimage_and(struct SHAPE2D far *shape, legacy_u16 x, legacy_u16 y)
{
	sprite_putimage_at(shape, x, y, SHAPE2D_RASTER_AND);
}

void sprite_putimage_or(struct SHAPE2D far *shape, legacy_u16 x, legacy_u16 y)
{
	sprite_putimage_at(shape, x, y, SHAPE2D_RASTER_OR);
}

void sprite_copy_image_at(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y)
{
	sprite_putimage_at(shape, (legacy_u16)x, (legacy_u16)y, SHAPE2D_RASTER_COPY);
}

void sprite_and_image_at_anchor(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y)
{
	sprite_putimage_at_anchor(shape, x, y, SHAPE2D_RASTER_AND);
}

void sprite_or_image_at_anchor(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y)
{
	sprite_putimage_at_anchor(shape, x, y, SHAPE2D_RASTER_OR);
}

void sprite_putimage_transparent(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y)
{
	sprite_putimage_at(shape, (legacy_u16)x, (legacy_u16)y, SHAPE2D_RASTER_MAP);
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
