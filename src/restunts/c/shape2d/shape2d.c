#include <stddef.h>

#include "../externs.h"
#include "../memmgr.h"
#include "../fileio.h"
#include "../legacy.h"
#include "../platform.h"
#include "../resource.h"
#include "shape2d.h"
#include "shape2d_internal.h"
#include "../ui/ui_text.h"
#include "../video/video_pages.h"

extern legacy_u16 fontdefseg;

#define FONTDEF_LINE_START_X_OFFSET 4U
#define FONTDEF_START_X_OFFSET 8U
#define FONTDEF_START_Y_OFFSET 10U
#define FONTDEF_BYTES_PER_ROW_OFFSET 12U
#define FONTDEF_HEIGHT_OFFSET 14U
#define FONTDEF_GLYPH_WIDTH_OFFSET 16U
#define FONTDEF_LINE_HEIGHT_OFFSET 18U
#define FONTDEF_VARIABLE_WIDTH_OFFSET 20U
#define FONTDEF_GLYPH_TABLE_OFFSET 22U
#define FONTDEF_GLYPH_WIDTH_ROUNDING 7U
#define FONTDEF_GLYPH_WIDTH_SHIFT 3U
#define FONT_GLYPH_FIRST_BIT LEGACY_U8_SIGN_BIT
#define FONT_GLYPH_BITS_PER_BYTE LEGACY_BYTE_BITS

#define SHAPE2D_FIXED_HALF LEGACY_U16_SIGN_BIT

legacy_u16 shape2d_get_word(const legacy_u8 far *source)
{
	return LEGACY_READ_U16_LE(source);
}

void shape2d_put_word(legacy_u8 far *destination, legacy_u16 value)
{
	LEGACY_WRITE_U16_LE(destination, value);
}

legacy_u16 shape2d_get_width(const struct SHAPE2D far *shape)
{
	return shape2d_get_word((const legacy_u8 far *)shape + offsetof(struct SHAPE2D, width));
}

legacy_u16 shape2d_get_height(const struct SHAPE2D far *shape)
{
	return shape2d_get_word((const legacy_u8 far *)shape + offsetof(struct SHAPE2D, height));
}

legacy_u16 shape2d_get_anchor_x(const struct SHAPE2D far *shape)
{
	return shape2d_get_word((const legacy_u8 far *)shape + offsetof(struct SHAPE2D, centre_x));
}

legacy_u16 shape2d_get_anchor_y(const struct SHAPE2D far *shape)
{
	return shape2d_get_word((const legacy_u8 far *)shape + offsetof(struct SHAPE2D, centre_y));
}

/* An anchored draw puts the sprite's stored anchor point on (x, y), so the
 * blit origin is the requested point minus that anchor. */
legacy_u16 shape2d_anchored_x(const struct SHAPE2D far *shape, legacy_s16 x)
{
	return LEGACY_U16_WRAP_SUB(x, shape2d_get_anchor_x(shape));
}

legacy_u16 shape2d_anchored_y(const struct SHAPE2D far *shape, legacy_s16 y)
{
	return LEGACY_U16_WRAP_SUB(y, shape2d_get_anchor_y(shape));
}

legacy_u16 shape2d_get_pos_x(const struct SHAPE2D far *shape)
{
	return shape2d_get_word((const legacy_u8 far *)shape + offsetof(struct SHAPE2D, position_x));
}

legacy_u16 shape2d_get_pos_y(const struct SHAPE2D far *shape)
{
	return shape2d_get_word((const legacy_u8 far *)shape + offsetof(struct SHAPE2D, position_y));
}

legacy_u16 shape2d_get_line_offset(legacy_u16 sprite_segment, legacy_u16 y)
{
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
	(void)sprite_segment;
	return shape2d_get_word(drawing_sprite.sprite_lineofs + (legacy_u16)(y << 1));
#else
	legacy_u16 line_entry =
		LEGACY_U16_WRAP_ADD(shape2d_line_base(&drawing_sprite), (legacy_u16)(y << 1));
	legacy_u8 far *line_entry_ptr =
		(legacy_u8 far *)dos_memory_make_pointer(sprite_segment, line_entry);
	return shape2d_get_word(line_entry_ptr);
#endif
}

/* Native line tables are pointers in their own right, not near offsets in the
 * segment containing the sprite. Keep 16-bit row-offset arithmetic intact. */
legacy_u8 far *shape2d_line_pointer(const struct SPRITE far *sprite, legacy_u16 offset)
{
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
	return sprite->sprite_lineofs + offset;
#else
	return (legacy_u8 far *)dos_memory_make_pointer(dos_memory_pointer_segment(sprite), offset);
#endif
}

void sprite_set_target_clip_bounds(legacy_u16 left, legacy_u16 right, legacy_u16 top,
								   legacy_u16 bottom)
{
	drawing_sprite.sprite_raster_left = left;
	drawing_sprite.sprite_left = left;
	drawing_sprite.sprite_raster_right = right;
	drawing_sprite.sprite_right = right;
	drawing_sprite.sprite_top = top;
	drawing_sprite.sprite_bottom = bottom;
}

void sprite_fill_rect(legacy_s16 x, legacy_s16 y, legacy_s16 width, legacy_s16 height,
					  legacy_s16 color)
{
	if (LEGACY_S16_FROM_BITS(width) <= 0 || LEGACY_S16_FROM_BITS(height) <= 0) {
		return;
	}
	legacy_u8 far *bitmap = (legacy_u8 far *)drawing_sprite.sprite_bitmapptr;
	legacy_u16 offset = LEGACY_U16_WRAP_ADD(
		shape2d_get_line_offset(dos_memory_pointer_segment(&drawing_sprite), (legacy_u16)y),
		(legacy_u16)x);
	legacy_u16 row_count = (legacy_u16)height;
	legacy_u16 column_count = (legacy_u16)width;
	legacy_u8 planar = video_pages_uses_raster_hooks(bitmap);
	for (legacy_u16 row = 0; row < row_count; row++) {
		if (planar != 0) {
			video_pages_fill_span(bitmap, offset, column_count, (legacy_u8)color);
			offset = LEGACY_U16_WRAP_ADD(offset, drawing_sprite.sprite_pitch);
			continue;
		}
		for (legacy_u16 column = 0; column < column_count; column++) {
			bitmap[LEGACY_U16_WRAP_ADD(offset, column)] = (legacy_u8)color;
		}
		offset = LEGACY_U16_WRAP_ADD(offset, drawing_sprite.sprite_pitch);
	}
}

legacy_s16 shape2d_clip_rectangle(legacy_s16 x, legacy_s16 y, legacy_s16 width, legacy_s16 height,
								  legacy_s16 *clipped_x, legacy_s16 *clipped_y,
								  legacy_s16 *clipped_width, legacy_s16 *clipped_height)
{
	*clipped_x = LEGACY_S16_FROM_BITS(x);
	*clipped_y = LEGACY_S16_FROM_BITS(y);
	*clipped_width = LEGACY_S16_FROM_BITS(width);
	*clipped_height = LEGACY_S16_FROM_BITS(height);
	legacy_s16 difference = LEGACY_S16_WRAP_SUB(drawing_sprite.sprite_left, *clipped_x);
	if (difference > 0) {
		*clipped_x = LEGACY_S16_FROM_BITS(drawing_sprite.sprite_left);
		*clipped_width = LEGACY_S16_WRAP_SUB(*clipped_width, difference);
		if (*clipped_width <= 0) {
			return 0;
		}
	}
	difference = LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_ADD(*clipped_x, *clipped_width),
									 drawing_sprite.sprite_right);
	if (difference > 0) {
		*clipped_width = LEGACY_S16_WRAP_SUB(*clipped_width, difference);
		if (*clipped_width <= 0) {
			return 0;
		}
	}
	difference = LEGACY_S16_WRAP_SUB(drawing_sprite.sprite_top, *clipped_y);
	if (difference > 0) {
		*clipped_height = LEGACY_S16_WRAP_SUB(*clipped_height, difference);
		if (*clipped_height <= 0) {
			return 0;
		}
		*clipped_y = LEGACY_S16_FROM_BITS(drawing_sprite.sprite_top);
	}
	difference = LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_ADD(*clipped_y, *clipped_height),
									 drawing_sprite.sprite_bottom);
	if (difference > 0) {
		*clipped_height = LEGACY_S16_WRAP_SUB(*clipped_height, difference);
		if (*clipped_height <= 0) {
			return 0;
		}
	}
	return 1;
}

void sprite_fill_rect_clipped(legacy_s16 x, legacy_s16 y, legacy_s16 width, legacy_s16 height,
							  legacy_s16 color)
{
	legacy_s16 clipped_width;
	legacy_s16 clipped_height;
	legacy_s16 clipped_x;
	legacy_s16 clipped_y;
	if (!shape2d_clip_rectangle(x, y, width, height, &clipped_x, &clipped_y, &clipped_width,
								&clipped_height)) {
		return;
	}
	sprite_fill_rect(clipped_x, clipped_y, clipped_width, clipped_height, color);
}

void sprite_draw_rect_outline(legacy_s16 x1, legacy_s16 y1, legacy_s16 x2, legacy_s16 y2,
							  legacy_s16 color)
{
	legacy_s16 width = LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_SUB(x2, x1), 1);
	legacy_s16 height = LEGACY_S16_WRAP_SUB(y2, y1);
	if (width > 0) {
		sprite_fill_rect_clipped(x1, y1, width, 1, color);
		sprite_fill_rect_clipped(x1, y2, width, 1, color);
	}
	if (height > 0) {
		sprite_fill_rect_clipped(x1, y1, 1, height, color);
		sprite_fill_rect_clipped(x2, y1, 1, height, color);
	}
}

static void font_draw_glyph(legacy_u8 far *font_definition, legacy_u8 far *glyph_data,
							legacy_u8 far *bitmap, legacy_u16 current_x, legacy_s16 opaque)
{
	legacy_u8 color = font_definition[0];
	legacy_u8 background = font_definition[2];
	legacy_u16 current_y = shape2d_get_word(font_definition + FONTDEF_START_Y_OFFSET);
	legacy_u16 row_index = current_y;
	legacy_s16 row_count =
		LEGACY_S16_FROM_BITS(shape2d_get_word(font_definition + FONTDEF_HEIGHT_OFFSET));
	legacy_s16 old_row_count;
	legacy_s8 old_byte_count;
	do {
		legacy_u16 destination = LEGACY_U16_WRAP_ADD(
			shape2d_get_line_offset(dos_memory_pointer_segment(&drawing_sprite), row_index),
			current_x);
		legacy_s8 byte_count = LEGACY_S8_FROM_BITS(font_definition[FONTDEF_BYTES_PER_ROW_OFFSET]);
		do {
			legacy_u8 bits = *glyph_data++;
			for (legacy_u8 bit = 0; bit < FONT_GLYPH_BITS_PER_BYTE; bit++) {
				if ((bits & FONT_GLYPH_FIRST_BIT) != 0) {
					video_pages_write_pixel(bitmap, destination, color);
				} else if (opaque != 0) {
					video_pages_write_pixel(bitmap, destination, background);
				}
				bits <<= 1;
				destination++;
			}
			old_byte_count = byte_count;
			byte_count = LEGACY_S8_FROM_BITS((legacy_u8)((legacy_u8)byte_count - 1U));
		} while (old_byte_count != LEGACY_S8_FROM_BITS(LEGACY_U8_SIGN_BIT) && byte_count > 0);
		row_index++;
		old_row_count = row_count;
		row_count = LEGACY_S16_WRAP_SUB(row_count, 1);
	} while (old_row_count != LEGACY_S16_FROM_BITS(LEGACY_U16_SIGN_BIT) && row_count > 0);
}

static void font_draw_text_impl(const legacy_s8 *text, legacy_s16 x, legacy_s16 y,
								legacy_s16 opaque)
{
	legacy_u8 far *font_definition = active_font_definition;
	shape2d_put_word(font_definition + FONTDEF_START_X_OFFSET, (legacy_u16)x);
	shape2d_put_word(font_definition + FONTDEF_START_Y_OFFSET, (legacy_u16)y);
	legacy_u8 far *bitmap = (legacy_u8 far *)dos_memory_make_pointer(
		dos_memory_pointer_segment(drawing_sprite.sprite_bitmapptr), 0);
	legacy_u8 character;
	while ((character = (legacy_u8)*text++) != 0) {
		legacy_u16 glyph_offset = shape2d_get_word(font_definition + FONTDEF_GLYPH_TABLE_OFFSET +
												   (legacy_u16)character * 2U);
		if (glyph_offset == 0) {
			if (character == '\r' || character == '\n') {
				shape2d_put_word(font_definition + FONTDEF_START_X_OFFSET,
								 shape2d_get_word(font_definition + FONTDEF_LINE_START_X_OFFSET));
				shape2d_put_word(
					font_definition + FONTDEF_START_Y_OFFSET,
					LEGACY_U16_WRAP_ADD(
						shape2d_get_word(font_definition + FONTDEF_START_Y_OFFSET),
						shape2d_get_word(font_definition + FONTDEF_LINE_HEIGHT_OFFSET)));
			}
			continue;
		}
		legacy_u8 far *glyph_data = font_definition + glyph_offset;
		legacy_u16 current_x = shape2d_get_word(font_definition + FONTDEF_START_X_OFFSET);
		if (font_definition[FONTDEF_VARIABLE_WIDTH_OFFSET] != 0) {
			legacy_u16 glyph_width = *glyph_data++;
			shape2d_put_word(font_definition + FONTDEF_GLYPH_WIDTH_OFFSET, glyph_width);
			font_definition[FONTDEF_BYTES_PER_ROW_OFFSET] =
				(legacy_u8)((glyph_width + FONTDEF_GLYPH_WIDTH_ROUNDING) >>
							FONTDEF_GLYPH_WIDTH_SHIFT);
		}
		font_draw_glyph(font_definition, glyph_data, bitmap, current_x, opaque);
		shape2d_put_word(
			font_definition + FONTDEF_START_X_OFFSET,
			LEGACY_U16_WRAP_ADD(current_x,
								shape2d_get_word(font_definition + FONTDEF_GLYPH_WIDTH_OFFSET)));
	}
}

void font_draw_text(const legacy_s8 *text, legacy_s16 x, legacy_s16 y)
{
	font_draw_text_impl(text, x, y, 0);
}

void font_draw_text_opaque(const legacy_s8 *text, legacy_s16 x, legacy_s16 y)
{
	font_draw_text_impl(text, x, y, 1);
}

void draw_filled_lines(legacy_s16 *x1arr, legacy_s16 *x2arr, legacy_u16 y, legacy_u16 numlines,
					   legacy_u16 color)
{
	legacy_u16 line_count = (legacy_u16)numlines;
	if (line_count == 0) {
		return;
	}
	legacy_u8 far *bitmap = (legacy_u8 far *)dos_memory_make_pointer(
		dos_memory_pointer_segment(drawing_sprite.sprite_bitmapptr), 0);
	legacy_u8 planar = video_pages_uses_raster_hooks(bitmap);
	legacy_u16 current_y = (legacy_u16)y;
	legacy_u16 old_line_count;
	do {
		legacy_u16 left = (legacy_u16)*x1arr++;
		legacy_u16 right = (legacy_u16)*x2arr++;
		legacy_u16 width = LEGACY_U16_WRAP_ADD(LEGACY_U16_WRAP_SUB(right, left), 1U);
		if (width != 0 && width <= LEGACY_U16_SIGN_BIT) {
			legacy_u16 destination = LEGACY_U16_WRAP_ADD(
				shape2d_get_line_offset(dos_memory_pointer_segment(&drawing_sprite), current_y),
				left);
			if (planar != 0) {
				video_pages_fill_span(bitmap, destination, width, (legacy_u8)color);
			} else {
				do {
					bitmap[destination] = (legacy_u8)color;
					destination++;
					width--;
				} while (width != 0);
			}
		}
		current_y++;
		old_line_count = line_count;
		line_count = LEGACY_U16_WRAP_SUB(line_count, 1U);
	} while (old_line_count != LEGACY_U16_SIGN_BIT && LEGACY_S16_FROM_BITS(line_count) > 0);
}

static legacy_u8 shape2d_rotate_left_8(legacy_u8 value, legacy_u8 count)
{
	count &= 7U;
	if (count == 0) {
		return value;
	}
	return (legacy_u8)((value << count) | (value >> (8U - count)));
}

static void draw_pattern_lines(legacy_s16 *x1arr, legacy_s16 *x2arr, legacy_u16 y,
							   legacy_u16 numlines, legacy_u16 color, legacy_s16 two_colors)
{
	if (((legacy_u16)y & 1U) == 0) {
		raster_fill_pattern = (legacy_u16)((raster_fill_pattern << LEGACY_BYTE_BITS) |
										   (raster_fill_pattern >> LEGACY_BYTE_BITS));
	}
	legacy_u8 far *bitmap = (legacy_u8 far *)dos_memory_make_pointer(
		dos_memory_pointer_segment(drawing_sprite.sprite_bitmapptr), 0);
	legacy_u8 planar = video_pages_uses_raster_hooks(bitmap);
	legacy_u16 line_entry =
		LEGACY_U16_WRAP_ADD(shape2d_line_base(&drawing_sprite), (legacy_u16)((legacy_u16)y << 1));
	legacy_u16 line_count = (legacy_u16)numlines;
	legacy_u16 old_line_count;
	do {
		legacy_u16 left = (legacy_u16)*x1arr++;
		legacy_u16 right = (legacy_u16)*x2arr++;
		legacy_u8 pattern = (legacy_u8)raster_fill_pattern;
		pattern = shape2d_rotate_left_8(pattern, (legacy_u8)left);
		legacy_u8 alternate_color = (legacy_u8)raster_alternate_color;
		legacy_u16 width = LEGACY_U16_WRAP_ADD(LEGACY_U16_WRAP_SUB(right, left), 1U);
		if (width != 0 && width <= LEGACY_U16_SIGN_BIT) {
			legacy_u8 far *line_entry_ptr = shape2d_line_pointer(&drawing_sprite, line_entry);
			legacy_u16 destination = LEGACY_U16_WRAP_ADD(shape2d_get_word(line_entry_ptr), left);
			if (planar != 0) {
				video_pages_pattern_span(bitmap, destination, width, pattern, (legacy_u8)color,
										 alternate_color, two_colors);
			} else {
				do {
					pattern = shape2d_rotate_left_8(pattern, 1U);
					if ((pattern & 1U) != 0) {
						if (two_colors != 0) {
							bitmap[destination] = alternate_color;
						} else {
							bitmap[destination] = (legacy_u8)color;
						}
					} else if (two_colors != 0) {
						bitmap[destination] = (legacy_u8)color;
					}
					destination++;
					width--;
				} while (width != 0);
			}
		}
		line_entry = LEGACY_U16_WRAP_ADD(line_entry, 2U);
		legacy_u16 swapped_pattern = (legacy_u16)((raster_fill_pattern << LEGACY_BYTE_BITS) |
												  (raster_fill_pattern >> LEGACY_BYTE_BITS));
		raster_fill_pattern = swapped_pattern;
		old_line_count = line_count;
		line_count = LEGACY_U16_WRAP_SUB(line_count, 1U);
	} while (old_line_count != LEGACY_U16_SIGN_BIT && LEGACY_S16_FROM_BITS(line_count) > 0);
}

void draw_two_color_lines(legacy_s16 *x1arr, legacy_s16 *x2arr, legacy_u16 y, legacy_u16 numlines,
						  legacy_u16 color)
{
	draw_pattern_lines(x1arr, x2arr, y, numlines, color, 1);
}

void draw_patterned_lines(legacy_s16 *x1arr, legacy_s16 *x2arr, legacy_u16 y, legacy_u16 numlines,
						  legacy_u16 color)
{
	draw_pattern_lines(x1arr, x2arr, y, numlines, color, 0);
}

struct SPRITE_LINE_DRAW {
	legacy_u8 far *bitmap;
	legacy_u16 sprite_segment;
	legacy_u16 x_low;
	legacy_u16 x_high;
	legacy_u16 y_low;
	legacy_u16 y_high;
	legacy_u16 original_y_high;
	legacy_u16 delta;
	legacy_u16 count;
	legacy_u8 color;
};

static void sprite_draw_line_rows(struct SPRITE_LINE_DRAW *draw, legacy_u16 mode)
{
	/* All five walk one row per step; only the horizontal advance
	 * differs - none, a whole pixel either way, or a fractional
	 * slope carried in draw->x_low. */
	legacy_u16 remaining = draw->count;
	legacy_u16 old_low;
	do {
		legacy_u16 destination = LEGACY_U16_WRAP_ADD(
			shape2d_get_line_offset(draw->sprite_segment, draw->original_y_high), draw->x_high);
		video_pages_write_pixel(draw->bitmap, destination, draw->color);
		draw->original_y_high++;
		if (mode == 3U) {
			draw->x_high--;
		} else if (mode == 4U) {
			draw->x_high++;
		} else if (mode == 5U) {
			old_low = draw->x_low;
			draw->x_low = LEGACY_U16_WRAP_SUB(draw->x_low, draw->delta);
			if (old_low < draw->delta) {
				draw->x_high--;
			}
		} else if (mode == 6U) {
			old_low = draw->x_low;
			draw->x_low = LEGACY_U16_WRAP_ADD(draw->x_low, draw->delta);
			if (draw->x_low < old_low) {
				draw->x_high++;
			}
		}
		remaining--;
	} while (remaining != 0);
}

static void sprite_draw_line_columns(struct SPRITE_LINE_DRAW *draw, legacy_u16 mode)
{
	legacy_u16 remaining = draw->count;
	do {
		legacy_u16 destination = LEGACY_U16_WRAP_ADD(
			shape2d_get_line_offset(draw->sprite_segment, draw->y_high), draw->x_high);
		video_pages_write_pixel(draw->bitmap, destination, draw->color);
		if (mode == 7U) {
			draw->x_high--;
		} else {
			draw->x_high++;
		}
		legacy_u16 old_low = draw->y_low;
		draw->y_low = LEGACY_U16_WRAP_ADD(draw->y_low, draw->delta);
		if (draw->y_low < old_low) {
			draw->y_high++;
		}
		remaining--;
	} while (remaining != 0);
}

static void sprite_round_line_coordinate(legacy_u16 *low, legacy_u16 *high)
{
	legacy_u16 old_low = *low;
	*low = LEGACY_U16_WRAP_ADD(*low, SHAPE2D_FIXED_HALF);
	if (*low < old_low) {
		(*high)++;
	}
}

void sprite_draw_line_from_setup(const legacy_u16 *line)
{
	struct SPRITE_LINE_DRAW draw;
	draw.x_low = (legacy_u16)line[0];
	draw.x_high = (legacy_u16)line[1];
	sprite_round_line_coordinate(&draw.x_low, &draw.x_high);
	draw.y_low = (legacy_u16)line[2];
	draw.y_high = (legacy_u16)line[3];
	sprite_round_line_coordinate(&draw.y_low, &draw.y_high);
	draw.original_y_high = (legacy_u16)line[3];
	draw.delta = (legacy_u16)line[6];
	draw.count = (legacy_u16)line[7];
	draw.color = (legacy_u8)line[8];
	legacy_u16 mode = (legacy_u16)line[9];
	draw.bitmap = (legacy_u8 far *)dos_memory_make_pointer(
		dos_memory_pointer_segment(drawing_sprite.sprite_bitmapptr), 0);
	draw.sprite_segment = dos_memory_pointer_segment(&drawing_sprite);

	legacy_u16 destination;
	legacy_u16 remaining;
	switch (mode) {
		case 0:
		case 1:
			destination = LEGACY_U16_WRAP_ADD(
				shape2d_get_line_offset(draw.sprite_segment, draw.y_high), draw.x_high);
			remaining = draw.count;
			if (video_pages_uses_raster_hooks(draw.bitmap) != 0) {
				video_pages_fill_span(draw.bitmap, destination, remaining, draw.color);
				break;
			}
			while (remaining != 0) {
				draw.bitmap[destination] = draw.color;
				destination++;
				remaining--;
			}
			break;
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
			sprite_draw_line_rows(&draw, mode);
			break;
		case 7:
		case 8:
			sprite_draw_line_columns(&draw, mode);
			break;
		case 9:
			destination = LEGACY_U16_WRAP_ADD(
				shape2d_get_line_offset(draw.sprite_segment, draw.y_high), draw.x_high);
			video_pages_write_pixel(draw.bitmap, destination, draw.color);
			break;
	}
}
