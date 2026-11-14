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
#if defined(RESTUNTS_SDL3) && defined(RESTUNTS_FULL) && !defined(RESTUNTS_HEADLESS)
#include "../../platform/sdl3/video_mode.h"
#define SHAPE2D_VIDEO_SOURCE_BEGIN(shape, x, y) sdl3_video_mode_source_begin_at(shape, x, y)
#define SHAPE2D_VIDEO_SOURCE_END() sdl3_video_mode_source_end()
#else
#define SHAPE2D_VIDEO_SOURCE_BEGIN(shape, x, y)
#define SHAPE2D_VIDEO_SOURCE_END()
#endif
#define PALETTE_MAP_TRANSPARENT 255U

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
#if defined(RESTUNTS_FULL) && !defined(RESTUNTS_HEADLESS)
	if (sdl3_video_mode_selected != SDL3_VIDEO_MODE_VGA &&
		sdl3_video_mode_raster(bitmap, destination, &value, 0, 1, operation, NULL)) {
		return;
	}
#endif
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
	SHAPE2D_VIDEO_SOURCE_BEGIN(shape, x, y);
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
			SHAPE2D_VIDEO_SOURCE_END();
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
	SHAPE2D_VIDEO_SOURCE_BEGIN(shape, x, y);
	do {
		if (planar != 0) {
			if (!shape2d_rle_raster_span(&cursor, bitmap, destination, clip.width, operation)) {
				SHAPE2D_VIDEO_SOURCE_END();
				return;
			}
			destination = LEGACY_U16_WRAP_ADD(destination, clip.width);
		} else {
			legacy_u16 count = clip.width;
			do {
				if (!shape2d_rle_next(&cursor, &value)) {
					SHAPE2D_VIDEO_SOURCE_END();
					return;
				}
				shape2d_write_raster(bitmap, destination, value, operation, 0);
				destination++;
				count--;
			} while (count != 0);
		}
		rows--;
		if (rows == 0) {
			SHAPE2D_VIDEO_SOURCE_END();
			return;
		}
		skip = clip.source_advance;
		if (!shape2d_rle_skip(&cursor, skip)) {
			SHAPE2D_VIDEO_SOURCE_END();
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
	SHAPE2D_VIDEO_SOURCE_BEGIN(shape, x, y);
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
	SHAPE2D_VIDEO_SOURCE_END();
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
