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
#if defined(RESTUNTS_SDL3) && defined(RESTUNTS_FULL) && !defined(RESTUNTS_HEADLESS)
#include "../../platform/sdl3/video_mode.h"
#define SHAPE2D_VIDEO_SOURCE_BEGIN(shape, x, y) sdl3_video_mode_source_begin_at(shape, x, y)
#define SHAPE2D_VIDEO_SOURCE_END() sdl3_video_mode_source_end()
#else
#define SHAPE2D_VIDEO_SOURCE_BEGIN(shape, x, y)
#define SHAPE2D_VIDEO_SOURCE_END()
#endif

#define SHAPE2D_FIXED_FRACTION_BITS LEGACY_BYTE_BITS
#define SHAPE2D_FIXED_ONE 65536UL
#define SHAPE2D_ZERO_WIDTH_RASTER_PIXELS 131072UL

static void sprite_draw_dissolve_row(legacy_u16 shape_segment, legacy_u8 far *bitmap,
									 legacy_u16 source, legacy_u16 destination, legacy_u16 width,
									 legacy_u16 row_phase)
{
	legacy_u16 remaining = width;
	legacy_u16 pattern = row_phase;
	static const legacy_u8 advance_count[4] = {3, 1, 4, 2};
	static const legacy_u8 skip_count[4] = {1, 3, 0, 2};
	for (;;) {
		pattern &= 3U;
		legacy_u16 skip = skip_count[pattern];
		if (LEGACY_S16_FROM_BITS(remaining) <= (legacy_s16)skip) {
			break;
		}
		remaining = LEGACY_U16_WRAP_SUB(remaining, skip);
		source = LEGACY_U16_WRAP_ADD(source, skip);
		destination = LEGACY_U16_WRAP_ADD(destination, skip);
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
		video_pages_copy_span(bitmap, destination, dos_memory_make_pointer(shape_segment, 0),
							  source, 1);
#else
		legacy_u8 far *source_ptr = dos_memory_make_pointer(shape_segment, source);
		video_pages_write_pixel(bitmap, destination, *source_ptr);
#endif
		legacy_u16 advance = advance_count[pattern];
		source = LEGACY_U16_WRAP_ADD(source, advance);
		destination = LEGACY_U16_WRAP_ADD(destination, advance);
		remaining = LEGACY_U16_WRAP_SUB(remaining, advance);
		pattern++;
	}
}

void sprite_draw_dissolve_phase(struct SHAPE2D far *shape, legacy_u16 phase)
{
	legacy_u16 shape_segment = dos_memory_pointer_segment(shape);
	legacy_u8 far *bitmap = (legacy_u8 far *)dos_memory_make_pointer(
		dos_memory_pointer_segment(drawing_sprite.sprite_bitmapptr), 0);
	legacy_u16 width = shape2d_get_width(shape);
	legacy_u16 height = shape2d_get_height(shape);
	legacy_u16 pos_x = shape2d_get_pos_x(shape);
	legacy_u16 pos_y = shape2d_get_pos_y(shape);
	legacy_u16 line_table_start =
		LEGACY_U16_WRAP_ADD(shape2d_line_base(&drawing_sprite), (legacy_u16)(pos_y << 1));
	legacy_u16 line_table_end = LEGACY_U16_WRAP_ADD(line_table_start, (legacy_u16)(height << 1));
	legacy_u16 data_start =
		LEGACY_U16_WRAP_ADD(dos_memory_pointer_offset(shape), SHAPE2D_HEADER_SIZE);
	legacy_u16 source_row_step = (legacy_u16)((legacy_u32)width * 12UL);
	static const legacy_u8 row_order[12] = {11, 5, 8, 2, 10, 4, 7, 1, 9, 3, 6, 0};
	SHAPE2D_VIDEO_SOURCE_BEGIN(shape, pos_x, pos_y);
	for (legacy_s16 order_index = 11; order_index >= 0; order_index--) {
		legacy_u16 selector = row_order[order_index];
		legacy_u16 line_entry = LEGACY_U16_WRAP_ADD(line_table_start, (legacy_u16)(selector << 1));
		legacy_u16 source =
			LEGACY_U16_WRAP_ADD(data_start, (legacy_u16)((legacy_u32)width * selector));
		legacy_u16 row_phase = (legacy_u16)phase;
		while (line_entry < line_table_end) {
			legacy_u8 far *line_entry_ptr = shape2d_line_pointer(&drawing_sprite, line_entry);
			legacy_u16 destination = LEGACY_U16_WRAP_ADD(shape2d_get_word(line_entry_ptr), pos_x);
			legacy_u16 row_source = source;
			sprite_draw_dissolve_row(shape_segment, bitmap, source, destination, width, row_phase);
			row_phase++;
			line_entry = LEGACY_U16_WRAP_ADD(line_entry, 24U);
			source = LEGACY_U16_WRAP_ADD(row_source, source_row_step);
		}
		phase++;
	}
	SHAPE2D_VIDEO_SOURCE_END();
}

void sprite_draw_palette_mapped(struct SHAPE2D far *shape)
{
	legacy_u16 shape_segment = dos_memory_pointer_segment(shape);
	legacy_u8 far *bitmap = (legacy_u8 far *)dos_memory_make_pointer(
		dos_memory_pointer_segment(drawing_sprite.sprite_bitmapptr), 0);
	legacy_u16 width = shape2d_get_width(shape);
	legacy_u16 row_count = shape2d_get_height(shape);
	legacy_u16 destination =
		LEGACY_U16_WRAP_ADD(shape2d_get_line_offset(dos_memory_pointer_segment(&drawing_sprite),
													shape2d_get_pos_y(shape)),
							shape2d_get_pos_x(shape));
	legacy_u16 destination_advance = LEGACY_U16_WRAP_SUB(drawing_sprite.sprite_pitch, width);
	legacy_u16 source = LEGACY_U16_WRAP_ADD(dos_memory_pointer_offset(shape), SHAPE2D_HEADER_SIZE);
	legacy_u8 planar = video_pages_uses_raster_hooks(bitmap);
	legacy_u16 old_row_count;
	SHAPE2D_VIDEO_SOURCE_BEGIN(shape, shape2d_get_pos_x(shape), shape2d_get_pos_y(shape));
	do {
		legacy_u16 column_count = width;
		if (planar != 0 && width != 0) {
			const legacy_u8 far *source_bitmap =
				(const legacy_u8 far *)dos_memory_make_pointer(shape_segment, 0);
			video_pages_raster_span(bitmap, destination, source_bitmap, source, width,
									SHAPE2D_RASTER_MAP, sprite_palette_map);
			source = LEGACY_U16_WRAP_ADD(source, width);
			destination = LEGACY_U16_WRAP_ADD(destination, width);
		} else {
			do {
				legacy_u8 far *source_ptr =
					(legacy_u8 far *)dos_memory_make_pointer(shape_segment, source);
				legacy_u8 source_color = *source_ptr;
				source++;
				legacy_u8 mapped_color = sprite_palette_map[source_color];
				if (mapped_color != SHAPE2D_TRANSPARENT_COLOR) {
					video_pages_write_pixel(bitmap, destination, mapped_color);
				}
				destination++;
				column_count--;
			} while (column_count != 0);
		}
		destination = LEGACY_U16_WRAP_ADD(destination, destination_advance);
		old_row_count = row_count;
		row_count = LEGACY_U16_WRAP_SUB(row_count, 1U);
	} while (old_row_count != LEGACY_U16_SIGN_BIT && LEGACY_S16_FROM_BITS(row_count) > 0);
	SHAPE2D_VIDEO_SOURCE_END();
}

static void sprite_clear_shape_impl(struct SHAPE2D far *shape, legacy_u16 x, legacy_u16 y)
{
	legacy_u8 far *bitmap = (legacy_u8 far *)dos_memory_make_pointer(
		dos_memory_pointer_segment(drawing_sprite.sprite_bitmapptr), 0);
	legacy_u16 shape_segment = dos_memory_pointer_segment(shape);
	legacy_u16 line_entry =
		LEGACY_U16_WRAP_ADD(shape2d_line_base(&drawing_sprite), (legacy_u16)(y << 1));
	legacy_u16 destination =
		LEGACY_U16_WRAP_ADD(dos_memory_pointer_offset(shape), SHAPE2D_HEADER_SIZE);
	legacy_u16 width = shape2d_get_word((legacy_u8 far *)shape);
	legacy_u16 row_count =
		shape2d_get_word((legacy_u8 far *)shape + offsetof(struct SHAPE2D, height));
	legacy_u8 planar = video_pages_uses_raster_hooks(bitmap);
	legacy_u16 old_row_count;
	do {
		legacy_u8 far *line_entry_ptr = shape2d_line_pointer(&drawing_sprite, line_entry);
		legacy_u16 source = LEGACY_U16_WRAP_ADD(shape2d_get_word(line_entry_ptr), x);
		legacy_u16 column_count = width;
		if (planar != 0) {
			legacy_u8 far *destination_bitmap =
				(legacy_u8 far *)dos_memory_make_pointer(shape_segment, 0);
			video_pages_copy_span(destination_bitmap, destination, bitmap, source, width);
			destination = LEGACY_U16_WRAP_ADD(destination, width);
			column_count = 0;
		}
		while (column_count != 0) {
			legacy_u8 far *destination_ptr =
				(legacy_u8 far *)dos_memory_make_pointer(shape_segment, destination);
			*destination_ptr = bitmap[source];
			destination++;
			source++;
			column_count--;
		}
		line_entry = LEGACY_U16_WRAP_ADD(line_entry, 2U);
		old_row_count = row_count;
		row_count = LEGACY_U16_WRAP_SUB(row_count, 1U);
	} while (old_row_count != LEGACY_U16_SIGN_BIT && LEGACY_S16_FROM_BITS(row_count) > 0);
}

void sprite_clear_shape_alt(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y)
{
	legacy_u8 far *shape_bytes = (legacy_u8 far *)shape;
	shape2d_put_word(shape_bytes + offsetof(struct SHAPE2D, position_x), (legacy_u16)x);
	shape2d_put_word(shape_bytes + offsetof(struct SHAPE2D, position_y), (legacy_u16)y);
	sprite_clear_shape_impl(shape, (legacy_u16)x, (legacy_u16)y);
}

void sprite_clear_shape(struct SHAPE2D far *shape)
{
	sprite_clear_shape_impl(shape, shape2d_get_pos_x(shape), shape2d_get_pos_y(shape));
}

static legacy_u16 shape2d_scaled_anchor(legacy_u16 anchor, legacy_u16 scale)
{
	legacy_s32 product =
		(legacy_s32)LEGACY_S16_FROM_BITS(anchor) * (legacy_s32)LEGACY_S16_FROM_BITS(scale);
	return (legacy_u16)((legacy_u32)product >> SHAPE2D_FIXED_FRACTION_BITS);
}

#define SHAPE2D_TRANSFORM_HALF (SHAPE2D_TRANSFORM_ONE / 2L)
/* This domain keeps both Q14 products and their sum within signed 32 bits. */
#define SHAPE2D_TRANSFORM_COORDINATE_LIMIT ((legacy_s32)(LEGACY_S16_MAX / 4U))
#define SHAPE2D_TRANSFORM_SEGMENT_BYTES (LEGACY_U16_MAX + 1UL)

static legacy_u8 shape2d_transform_coordinate_valid(legacy_s16 coordinate)
{
	return coordinate >= -SHAPE2D_TRANSFORM_COORDINATE_LIMIT &&
		   coordinate <= SHAPE2D_TRANSFORM_COORDINATE_LIMIT;
}

void sprite_copy_image_transformed(struct SPRITE far *source,
								   const struct SHAPE2D_TRANSFORM *transform)
{
	legacy_s16 left = transform->left;
	legacy_s16 top = transform->top;
	legacy_s16 right = transform->right;
	legacy_s16 bottom = transform->bottom;
	if (left < (legacy_s16)drawing_sprite.sprite_raster_left) {
		left = (legacy_s16)drawing_sprite.sprite_raster_left;
	}
	if (top < (legacy_s16)drawing_sprite.sprite_top) {
		top = (legacy_s16)drawing_sprite.sprite_top;
	}
	if (right > (legacy_s16)drawing_sprite.sprite_raster_right) {
		right = (legacy_s16)drawing_sprite.sprite_raster_right;
	}
	if (bottom > (legacy_s16)drawing_sprite.sprite_bottom) {
		bottom = (legacy_s16)drawing_sprite.sprite_bottom;
	}
	if (left >= right || top >= bottom || transform->source_width == 0 ||
		transform->source_height == 0 || transform->source_width > source->sprite_pitch ||
		transform->source_height > shape2d_get_height(source->sprite_bitmapptr) ||
		transform->source_width > SHAPE2D_TRANSFORM_COORDINATE_LIMIT ||
		transform->source_height > SHAPE2D_TRANSFORM_COORDINATE_LIMIT ||
		!shape2d_transform_coordinate_valid(left) || !shape2d_transform_coordinate_valid(top) ||
		!shape2d_transform_coordinate_valid(right) || !shape2d_transform_coordinate_valid(bottom) ||
		!shape2d_transform_coordinate_valid(transform->source_pivot_x) ||
		!shape2d_transform_coordinate_valid(transform->source_pivot_y) ||
		!shape2d_transform_coordinate_valid(transform->target_pivot_x) ||
		!shape2d_transform_coordinate_valid(transform->target_pivot_y)) {
		return;
	}
	legacy_u32 source_extent = SHAPE2D_HEADER_SIZE +
							   (legacy_u32)(transform->source_height - 1U) * source->sprite_pitch +
							   transform->source_width;
	if (source_extent > SHAPE2D_TRANSFORM_SEGMENT_BYTES) {
		return;
	}
	legacy_u8 far *source_pixels = (legacy_u8 far *)source->sprite_bitmapptr;
	legacy_u8 far *target_pixels = (legacy_u8 far *)dos_memory_make_pointer(
		dos_memory_pointer_segment(drawing_sprite.sprite_bitmapptr), 0);
	legacy_s32 delta_x = (legacy_s32)left - transform->target_pivot_x;
	legacy_s32 delta_y = (legacy_s32)top - transform->target_pivot_y;
	legacy_s32 row_x = (legacy_s32)transform->source_pivot_x * SHAPE2D_TRANSFORM_ONE +
					   delta_x * transform->inverse_xx + delta_y * transform->inverse_xy +
					   SHAPE2D_TRANSFORM_HALF;
	legacy_s32 row_y = (legacy_s32)transform->source_pivot_y * SHAPE2D_TRANSFORM_ONE +
					   delta_x * transform->inverse_yx + delta_y * transform->inverse_yy +
					   SHAPE2D_TRANSFORM_HALF;
	legacy_s32 source_right = (legacy_s32)transform->source_width * SHAPE2D_TRANSFORM_ONE;
	legacy_s32 source_bottom = (legacy_s32)transform->source_height * SHAPE2D_TRANSFORM_ONE;
	SHAPE2D_VIDEO_SOURCE_BEGIN(source->sprite_bitmapptr, (legacy_u16)transform->left,
							   (legacy_u16)transform->top);
	for (legacy_s16 y = top; y < bottom; y++) {
		legacy_u16 destination = LEGACY_U16_WRAP_ADD(
			shape2d_get_line_offset(dos_memory_pointer_segment(&drawing_sprite), (legacy_u16)y),
			(legacy_u16)left);
		legacy_s32 sample_x = row_x;
		legacy_s32 sample_y = row_y;
		for (legacy_s16 x = left; x < right; x++) {
			if (sample_x >= 0 && sample_x < source_right && sample_y >= 0 &&
				sample_y < source_bottom) {
				legacy_u16 source_x = (legacy_u16)(sample_x >> SHAPE2D_TRANSFORM_FRACTION_BITS);
				legacy_u16 source_y = (legacy_u16)(sample_y >> SHAPE2D_TRANSFORM_FRACTION_BITS);
				legacy_u16 source_offset =
					(legacy_u16)(SHAPE2D_HEADER_SIZE + (legacy_u32)source_y * source->sprite_pitch +
								 source_x);
				legacy_u8 color = source_pixels[source_offset];
				if (color != SHAPE2D_TRANSPARENT_COLOR) {
#if defined(RESTUNTS_SDL3) && defined(RESTUNTS_FULL) && !defined(RESTUNTS_HEADLESS)
					if (sdl3_video_mode_selected != SDL3_VIDEO_MODE_VGA) {
						sdl3_video_mode_source_sample(source_x, source_y);
					}
#endif
					video_pages_write_pixel(target_pixels, destination, color);
#if defined(RESTUNTS_SDL3) && defined(RESTUNTS_FULL) && !defined(RESTUNTS_HEADLESS)
					sdl3_video_mode_copy_sample(target_pixels, destination, source_pixels,
												source_offset);
#endif
				}
			}
			sample_x += transform->inverse_xx;
			sample_y += transform->inverse_yx;
			destination++;
		}
		row_x += transform->inverse_xy;
		row_y += transform->inverse_yy;
	}
	SHAPE2D_VIDEO_SOURCE_END();
}

struct SHAPE2D_SCALED_DRAW {
	legacy_u16 shape_segment;
	legacy_u16 source;
#if defined(RESTUNTS_SDL3) && defined(RESTUNTS_FULL) && !defined(RESTUNTS_HEADLESS)
	legacy_u16 source_start;
#endif
	legacy_u16 source_width;
	legacy_u16 scaled_width;
	legacy_u16 scaled_height;
	legacy_u16 step;
	legacy_u16 horizontal_start;
	legacy_u16 vertical_fraction;
	legacy_u16 x;
	legacy_u16 y;
};

static legacy_s16 shape2d_clip_scaled_columns(struct SHAPE2D_SCALED_DRAW *draw)
{
	legacy_u16 overflow;
	if (LEGACY_S16_FROM_BITS(draw->x) < LEGACY_S16_FROM_BITS(drawing_sprite.sprite_raster_left)) {
		overflow = LEGACY_U16_WRAP_SUB(LEGACY_U16_WRAP_ADD(draw->x, draw->scaled_width),
									   drawing_sprite.sprite_raster_left);
		if (LEGACY_S16_FROM_BITS(overflow) <= 0) {
			return 0;
		}
		legacy_u16 skipped = LEGACY_U16_WRAP_SUB(draw->scaled_width, overflow);
		draw->scaled_width = overflow;
		draw->x = drawing_sprite.sprite_raster_left;
		legacy_u32 product = (legacy_u32)skipped * draw->step;
		draw->horizontal_start = (legacy_u8)product;
		draw->source =
			LEGACY_U16_WRAP_ADD(draw->source, (legacy_u16)(product >> SHAPE2D_FIXED_FRACTION_BITS));
	}
	overflow = LEGACY_U16_WRAP_SUB(LEGACY_U16_WRAP_ADD(draw->x, draw->scaled_width),
								   drawing_sprite.sprite_raster_right);
	if (LEGACY_S16_FROM_BITS(overflow) >= 0) {
		draw->scaled_width = LEGACY_U16_WRAP_SUB(draw->scaled_width, overflow);
		if (LEGACY_S16_FROM_BITS(draw->scaled_width) <= 0) {
			return 0;
		}
	}
	return 1;
}

static legacy_s16 shape2d_clip_scaled_rows(struct SHAPE2D_SCALED_DRAW *draw)
{
	legacy_u16 overflow;
	if (LEGACY_S16_FROM_BITS(draw->y) < LEGACY_S16_FROM_BITS(drawing_sprite.sprite_top)) {
		overflow = LEGACY_U16_WRAP_SUB(LEGACY_U16_WRAP_ADD(draw->y, draw->scaled_height),
									   drawing_sprite.sprite_top);
		if (LEGACY_S16_FROM_BITS(overflow) <= 0) {
			return 0;
		}
		legacy_u16 skipped = LEGACY_U16_WRAP_SUB(draw->scaled_height, overflow);
		draw->scaled_height = overflow;
		draw->y = drawing_sprite.sprite_top;
		legacy_u32 product = (legacy_u32)skipped * draw->step;
		draw->vertical_fraction = (legacy_u8)product;
		legacy_u16 source_rows = (legacy_u16)(product >> SHAPE2D_FIXED_FRACTION_BITS);
		draw->source =
			LEGACY_U16_WRAP_ADD(draw->source, LEGACY_U16_WRAP_MUL(source_rows, draw->source_width));
	}
	overflow = LEGACY_U16_WRAP_SUB(LEGACY_U16_WRAP_ADD(draw->y, draw->scaled_height),
								   drawing_sprite.sprite_bottom);
	if (LEGACY_S16_FROM_BITS(overflow) >= 0) {
		draw->scaled_height = LEGACY_U16_WRAP_SUB(draw->scaled_height, overflow);
		if (LEGACY_S16_FROM_BITS(draw->scaled_height) <= 0) {
			return 0;
		}
	}
	return 1;
}

static void shape2d_render_scaled(struct SHAPE2D_SCALED_DRAW *draw)
{
	legacy_u8 far *bitmap = (legacy_u8 far *)dos_memory_make_pointer(
		dos_memory_pointer_segment(drawing_sprite.sprite_bitmapptr), 0);
	legacy_u16 destination = LEGACY_U16_WRAP_ADD(
		shape2d_get_line_offset(dos_memory_pointer_segment(&drawing_sprite), draw->y), draw->x);
	legacy_u16 destination_advance =
		LEGACY_U16_WRAP_SUB(drawing_sprite.sprite_pitch, draw->scaled_width);
	legacy_u16 row_source = draw->source;
	legacy_u16 row_count = draw->scaled_height;
	do {
		legacy_u16 column_count = draw->scaled_width;
		legacy_u16 horizontal_fraction = draw->horizontal_start;
		do {
			legacy_u8 far *source_ptr =
				(legacy_u8 far *)dos_memory_make_pointer(draw->shape_segment, draw->source);
			legacy_u8 color = *source_ptr;
			if (color != SHAPE2D_TRANSPARENT_COLOR) {
#if defined(RESTUNTS_SDL3) && defined(RESTUNTS_FULL) && !defined(RESTUNTS_HEADLESS)
				if (sdl3_video_mode_selected != SDL3_VIDEO_MODE_VGA) {
					legacy_u16 index = LEGACY_U16_WRAP_SUB(draw->source, draw->source_start);
					sdl3_video_mode_source_sample(index % draw->source_width,
												  index / draw->source_width);
				}
#endif
				video_pages_write_pixel(bitmap, destination, color);
			}
			destination++;
			horizontal_fraction = LEGACY_U16_WRAP_ADD(horizontal_fraction, draw->step);
			draw->source = LEGACY_U16_WRAP_ADD(draw->source,
											   horizontal_fraction >> SHAPE2D_FIXED_FRACTION_BITS);
			horizontal_fraction &= LEGACY_U8_MAX;
			column_count--;
		} while (column_count != 0);
		legacy_u16 old_row_count = row_count;
		row_count = LEGACY_U16_WRAP_SUB(row_count, 1U);
		if (old_row_count == LEGACY_U16_SIGN_BIT || LEGACY_S16_FROM_BITS(row_count) <= 0) {
			break;
		}
		destination = LEGACY_U16_WRAP_ADD(destination, destination_advance);
		draw->source = row_source;
		draw->vertical_fraction = LEGACY_U16_WRAP_ADD(draw->vertical_fraction, draw->step);
		legacy_u16 source_rows = draw->vertical_fraction >> SHAPE2D_FIXED_FRACTION_BITS;
		if (source_rows != 0) {
			draw->source = LEGACY_U16_WRAP_ADD(
				draw->source, LEGACY_U16_WRAP_MUL(source_rows, draw->source_width));
			draw->vertical_fraction &= LEGACY_U8_MAX;
			row_source = draw->source;
		}
	} while (1);
}

static void shape2d_scale_transparent_impl(struct SHAPE2D far *shape, legacy_u16 scale,
										   legacy_u16 x, legacy_u16 y, legacy_s16 clipped)
{
	if (scale < 2U) {
		return;
	}
	struct SHAPE2D_SCALED_DRAW draw;
	draw.shape_segment = dos_memory_pointer_segment(shape);
	draw.x = LEGACY_U16_WRAP_SUB(x, shape2d_scaled_anchor(shape2d_get_anchor_x(shape), scale));
	draw.y = LEGACY_U16_WRAP_SUB(y, shape2d_scaled_anchor(shape2d_get_anchor_y(shape), scale));
	draw.source_width = shape2d_get_width(shape);
	legacy_u32 product = (legacy_u32)shape2d_get_height(shape) * scale;
	draw.scaled_height = (legacy_u16)(product >> SHAPE2D_FIXED_FRACTION_BITS);
	if (draw.scaled_height == 0) {
		return;
	}
	product = (legacy_u32)draw.source_width * scale;
	draw.scaled_width = (legacy_u16)(product >> SHAPE2D_FIXED_FRACTION_BITS);
	if (draw.scaled_width == 0) {
		return;
	}
	draw.source = LEGACY_U16_WRAP_ADD(dos_memory_pointer_offset(shape), SHAPE2D_HEADER_SIZE);
#if defined(RESTUNTS_SDL3) && defined(RESTUNTS_FULL) && !defined(RESTUNTS_HEADLESS)
	draw.source_start = draw.source;
#endif
	draw.step = (legacy_u16)LEGACY_U32_DIV_OR_ZERO(SHAPE2D_FIXED_ONE, scale);
	draw.horizontal_start = 0;
	draw.vertical_fraction = 0;
	legacy_u16 center_skip = (legacy_u16)((draw.step >> SHAPE2D_FIXED_FRACTION_BITS) >> 1);
	draw.source = LEGACY_U16_WRAP_ADD(
		draw.source, (legacy_u16)((legacy_u32)center_skip * ((legacy_u32)draw.source_width + 1UL)));

	SHAPE2D_VIDEO_SOURCE_BEGIN(shape, draw.x, draw.y);
	if (clipped != 0) {
		if (!shape2d_clip_scaled_columns(&draw) || !shape2d_clip_scaled_rows(&draw)) {
			SHAPE2D_VIDEO_SOURCE_END();
			return;
		}
	}

	shape2d_render_scaled(&draw);
	SHAPE2D_VIDEO_SOURCE_END();
}

void shape2d_draw_scaled_transparent_clipped(legacy_s16 scale, struct SHAPE2D far *shape,
											 legacy_s16 x, legacy_s16 y)
{
	shape2d_scale_transparent_impl(shape, (legacy_u16)scale, (legacy_u16)x, (legacy_u16)y, 1);
}

void shape2d_draw_scaled_transparent(legacy_s16 scale, struct SHAPE2D far *shape, legacy_s16 x,
									 legacy_s16 y)
{
	shape2d_scale_transparent_impl(shape, (legacy_u16)scale, (legacy_u16)x, (legacy_u16)y, 0);
}

static void sprite_shape_to_1_impl(struct SHAPE2D far *shape, legacy_u16 x, legacy_u16 y,
								   legacy_s16 operation)
{
	legacy_u16 shape_segment = dos_memory_pointer_segment(shape);
	legacy_u8 far *bitmap = (legacy_u8 far *)dos_memory_make_pointer(
		dos_memory_pointer_segment(drawing_sprite.sprite_bitmapptr), 0);
	legacy_u16 source = LEGACY_U16_WRAP_ADD(dos_memory_pointer_offset(shape), SHAPE2D_HEADER_SIZE);
	legacy_u16 destination = LEGACY_U16_WRAP_ADD(
		shape2d_get_line_offset(dos_memory_pointer_segment(&drawing_sprite), y), x);
	legacy_u16 width = shape2d_get_width(shape);
	legacy_u16 row_count = shape2d_get_height(shape);
	legacy_u8 planar = video_pages_uses_raster_hooks(bitmap);
	legacy_u16 old_row_count;
	SHAPE2D_VIDEO_SOURCE_BEGIN(shape, x, y);
	do {
		legacy_u32 pixel_count;
		if (width == 0 && operation != SHAPE2D_RASTER_COPY) {
			pixel_count = SHAPE2D_ZERO_WIDTH_RASTER_PIXELS;
		} else {
			pixel_count = width;
		}
		if (planar != 0 && width != 0) {
			const legacy_u8 far *source_bitmap =
				(const legacy_u8 far *)dos_memory_make_pointer(shape_segment, 0);
			video_pages_raster_span(bitmap, destination, source_bitmap, source, width, operation,
									NULL);
			source = LEGACY_U16_WRAP_ADD(source, width);
			destination = LEGACY_U16_WRAP_ADD(destination, width);
			pixel_count = 0;
		}
		while (pixel_count != 0) {
			legacy_u8 far *source_ptr =
				(legacy_u8 far *)dos_memory_make_pointer(shape_segment, source);
			if (operation == SHAPE2D_RASTER_OR) {
				video_pages_write_pixel(bitmap, destination,
										video_pages_read_pixel(bitmap, destination) | *source_ptr);
			} else if (operation == SHAPE2D_RASTER_COPY) {
				video_pages_write_pixel(bitmap, destination, *source_ptr);
			} else {
				video_pages_write_pixel(bitmap, destination,
										video_pages_read_pixel(bitmap, destination) & *source_ptr);
			}
			source++;
			destination++;
			pixel_count--;
		}
		if (width == 1 && operation == SHAPE2D_RASTER_AND) {
			video_pages_write_pixel(bitmap, destination, 0);
		}
		destination = LEGACY_U16_WRAP_ADD(destination,
										  LEGACY_U16_WRAP_SUB(drawing_sprite.sprite_pitch, width));
		old_row_count = row_count;
		row_count = LEGACY_U16_WRAP_SUB(row_count, 1U);
	} while (old_row_count != LEGACY_U16_SIGN_BIT && LEGACY_S16_FROM_BITS(row_count) > 0);
	SHAPE2D_VIDEO_SOURCE_END();
}

void sprite_shape_to_1(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y)
{
	sprite_shape_to_1_impl(shape, (legacy_u16)x, (legacy_u16)y, SHAPE2D_RASTER_COPY);
}

void sprite_shape_to_1_alt(struct SHAPE2D far *shape)
{
	sprite_shape_to_1_impl(shape, shape2d_get_pos_x(shape), shape2d_get_pos_y(shape),
						   SHAPE2D_RASTER_COPY);
}

void putpixel_iconMask(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y)
{
	sprite_shape_to_1_impl(shape, (legacy_u16)x, (legacy_u16)y, SHAPE2D_RASTER_AND);
}

void putpixel_iconFillings(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y)
{
	sprite_shape_to_1_impl(shape, (legacy_u16)x, (legacy_u16)y, SHAPE2D_RASTER_OR);
}

void sprite_putpixel_clipped(legacy_s16 x, legacy_s16 y, legacy_s16 color)
{
	legacy_u16 x_bits = (legacy_u16)x;
	legacy_u16 y_bits = (legacy_u16)y;
	if (LEGACY_S16_FROM_BITS(x_bits) < LEGACY_S16_FROM_BITS(drawing_sprite.sprite_left) ||
		LEGACY_S16_FROM_BITS(x_bits) >= LEGACY_S16_FROM_BITS(drawing_sprite.sprite_right) ||
		LEGACY_S16_FROM_BITS(y_bits) < LEGACY_S16_FROM_BITS(drawing_sprite.sprite_top) ||
		LEGACY_S16_FROM_BITS(y_bits) >= LEGACY_S16_FROM_BITS(drawing_sprite.sprite_bottom)) {
		return;
	}
	legacy_u8 far *bitmap = (legacy_u8 far *)dos_memory_make_pointer(
		dos_memory_pointer_segment(drawing_sprite.sprite_bitmapptr), 0);
	legacy_u16 destination = LEGACY_U16_WRAP_ADD(
		shape2d_get_line_offset(dos_memory_pointer_segment(&drawing_sprite), y_bits), x_bits);
	video_pages_write_pixel(bitmap, destination, (legacy_u8)color);
}

void set_fontdefseg(void far *data)
{
	active_font_definition = (legacy_u8 far *)data;
}

void sprite_xor_rect_clipped(legacy_s16 x, legacy_s16 y, legacy_s16 width, legacy_s16 height,
							 legacy_s16 color)
{
	legacy_s16 clipped_height;
	legacy_s16 clipped_x;
	legacy_s16 clipped_y;
	legacy_s16 clipped_width;
	if (!shape2d_clip_rectangle(x, y, width, height, &clipped_x, &clipped_y, &clipped_width,
								&clipped_height)) {
		return;
	}
	if (clipped_width <= 0 || clipped_height <= 0) {
		return;
	}
	legacy_u8 far *bitmap = (legacy_u8 far *)dos_memory_make_pointer(
		dos_memory_pointer_segment(drawing_sprite.sprite_bitmapptr), 0);
	legacy_u16 destination = LEGACY_U16_WRAP_ADD(
		shape2d_get_line_offset(dos_memory_pointer_segment(&drawing_sprite), (legacy_u16)clipped_y),
		(legacy_u16)clipped_x);
	legacy_u16 row_count = (legacy_u16)clipped_height;
	legacy_u8 color_bits = (legacy_u8)color;
	legacy_u16 old_row_count;
	do {
		legacy_u16 column_count = (legacy_u16)clipped_width;
		do {
#if defined(RESTUNTS_SDL3) && defined(RESTUNTS_FULL) && !defined(RESTUNTS_HEADLESS)
			if (sdl3_video_mode_selected != SDL3_VIDEO_MODE_VGA) {
				sdl3_video_mode_xor_begin(bitmap, destination, color_bits);
			}
#endif
			video_pages_write_pixel(bitmap, destination,
									video_pages_read_pixel(bitmap, destination) ^ color_bits);
			destination++;
			column_count--;
		} while (column_count != 0);
		destination =
			LEGACY_U16_WRAP_ADD(destination, LEGACY_U16_WRAP_SUB(drawing_sprite.sprite_pitch,
																 (legacy_u16)clipped_width));
		old_row_count = row_count;
		row_count = LEGACY_U16_WRAP_SUB(row_count, 1U);
	} while (old_row_count != LEGACY_U16_SIGN_BIT && LEGACY_S16_FROM_BITS(row_count) > 0);
}

void sprite_copy_rect_shifted(legacy_s16 source_x, legacy_s16 source_y, legacy_s16 width,
							  legacy_s16 height, legacy_s16 destination_shift)
{
	legacy_s16 dividend = LEGACY_S16_WRAP_ADD(source_x, destination_shift);
	legacy_s16 divisor = LEGACY_S16_FROM_BITS(drawing_sprite.sprite_buffer_width);
	legacy_s16 remainder;
	legacy_s16 quotient;
	if ((legacy_u16)divisor == 0U ||
		((legacy_u16)dividend == LEGACY_U16_SIGN_BIT && divisor == -1)) {
		quotient = 0;
		remainder = 0;
	} else {
		quotient = LEGACY_S16_DIV_OR_ZERO(dividend, divisor);
		remainder = (legacy_s16)(dividend % divisor);
	}
	legacy_u16 source_line = LEGACY_U16_WRAP_ADD(shape2d_line_base(&screen_sprite),
												 (legacy_u16)((legacy_u16)source_y << 1));
	legacy_u16 destination_line =
		LEGACY_U16_WRAP_ADD(shape2d_line_base(&drawing_sprite),
							(legacy_u16)(LEGACY_U16_WRAP_ADD(source_y, quotient) << 1));
	legacy_u8 far *source_bitmap = (legacy_u8 far *)dos_memory_make_pointer(
		dos_memory_pointer_segment(screen_sprite.sprite_bitmapptr), 0);
	legacy_u8 far *destination_bitmap = (legacy_u8 far *)dos_memory_make_pointer(
		dos_memory_pointer_segment(drawing_sprite.sprite_bitmapptr), 0);
	legacy_u8 planar = video_pages_uses_raster_hooks(source_bitmap) ||
					   video_pages_uses_raster_hooks(destination_bitmap);
	legacy_u16 row_count = (legacy_u16)height;
	legacy_u16 old_row_count;
	do {
		legacy_u16 source =
			LEGACY_U16_WRAP_ADD(shape2d_get_word(shape2d_line_pointer(&screen_sprite, source_line)),
								(legacy_u16)source_x);
		legacy_u16 destination = LEGACY_U16_WRAP_ADD(
			shape2d_get_word(shape2d_line_pointer(&drawing_sprite, destination_line)),
			(legacy_u16)remainder);
		legacy_u16 column_count = (legacy_u16)width;
		if (planar != 0) {
			video_pages_copy_span(destination_bitmap, destination, source_bitmap, source,
								  column_count);
			column_count = 0;
		}
		while (column_count != 0) {
			destination_bitmap[destination] = source_bitmap[source];
			source++;
			destination++;
			column_count--;
		}
		source_line = LEGACY_U16_WRAP_ADD(source_line, 2U);
		destination_line = LEGACY_U16_WRAP_ADD(destination_line, 2U);
		old_row_count = row_count;
		row_count = LEGACY_U16_WRAP_SUB(row_count, 1U);
	} while (old_row_count != LEGACY_U16_SIGN_BIT && LEGACY_S16_FROM_BITS(row_count) > 0);
}

void sprite_set_palette_map(legacy_s16 destination_index, legacy_s16 count, void far *source_data)
{
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
	/* The native static palette may straddle a registered 64 KiB address page. */
	const legacy_u8 *source = source_data;
	legacy_u8 *destination = sprite_palette_map + (legacy_u16)destination_index;
	for (legacy_u16 remaining = (legacy_u16)count; remaining != 0; remaining--) {
		*destination++ = *source++;
	}
#else
	legacy_u16 source_segment = dos_memory_pointer_segment(source_data);
	legacy_u16 source = dos_memory_pointer_offset(source_data);
	legacy_u16 destination_segment = dos_memory_pointer_segment(sprite_palette_map);
	legacy_u16 destination = LEGACY_U16_WRAP_ADD(dos_memory_pointer_offset(sprite_palette_map),
												 (legacy_u16)destination_index);
	legacy_u16 remaining = (legacy_u16)count;
	while (remaining != 0) {
		legacy_u8 far *source_ptr =
			(legacy_u8 far *)dos_memory_make_pointer(source_segment, source);
		legacy_u8 far *destination_ptr =
			(legacy_u8 far *)dos_memory_make_pointer(destination_segment, destination);
		*destination_ptr = *source_ptr;
		source++;
		destination++;
		remaining--;
	}
#endif
}

// like locate_resource_by_index()
