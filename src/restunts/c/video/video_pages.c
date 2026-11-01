#include "video_pages.h"

#ifdef RESTUNTS_HAS_VGA_PAGES
#include "../platform.h"
#include "../externs.h"
#include "../shape2d/shape2d.h"
#include "../shape2d/shape2d_internal.h"

#define VGA_PAGE_SEGMENT 0xA000U
#define VGA_SECOND_PAGE_SEGMENT 0xA400U
#define VGA_PAGE_PIXELS 64000U
#define VGA_ALL_PLANES 15U
#define VGA_PLANE_SHIFT 2U
#define VGA_PLANE_MASK (VGA_PLANE_COUNT - 1U)
#define VGA_PAGE_COUNT 2U
#define VGA_PARAGRAPH_BYTES 16U

static legacy_u8 pages_active;
static struct SPRITE hidden_page;

/* A000 and A400 identify physical page bases in each plane. Row offsets and
 * pitches stay in pixels, so resource decoding and clipping keep their layout. */
static legacy_u16 video_pages_address(const void far *bitmap, legacy_u16 offset)
{
	return (
		legacy_u16)((dos_memory_pointer_segment(bitmap) - VGA_PAGE_SEGMENT) * VGA_PARAGRAPH_BYTES +
					((legacy_u16)(dos_memory_pointer_offset(bitmap) + offset) >> VGA_PLANE_SHIFT));
}

#ifdef RESTUNTS_VGA_TEST
/* The host model implements VGA read-map and write-mask register semantics. */
legacy_u8 video_pages_test_read(legacy_u16 address);
void video_pages_test_write(legacy_u16 address, legacy_u8 value);
#define video_pages_read_vram video_pages_test_read
#define video_pages_write_vram video_pages_test_write
#else
static inline legacy_u8 video_pages_read_vram(legacy_u16 address)
{
	return *(volatile legacy_u8 far *)dos_memory_make_pointer(VGA_PAGE_SEGMENT, address);
}
static inline void video_pages_write_vram(legacy_u16 address, legacy_u8 value)
{
	*(volatile legacy_u8 far *)dos_memory_make_pointer(VGA_PAGE_SEGMENT, address) = value;
}
#endif

legacy_u8 video_pages_is_active(void)
{
	return pages_active;
}

legacy_u8 video_pages_is_target(const void far *bitmap)
{
	if (pages_active == 0) {
		return 0;
	}
	legacy_u16 segment = dos_memory_pointer_segment(bitmap);
	return pages_active != 0 && (segment == VGA_PAGE_SEGMENT || segment == VGA_SECOND_PAGE_SEGMENT);
}

legacy_u8 video_pages_read_pixel(const legacy_u8 far *bitmap, legacy_u16 offset)
{
	if (video_pages_is_target(bitmap) == 0) {
		return bitmap[offset];
	}
	dos_video_set_read_plane(
		(legacy_u8)((offset + dos_memory_pointer_offset(bitmap)) & VGA_PLANE_MASK));
	return video_pages_read_vram(video_pages_address(bitmap, offset));
}

void video_pages_write_pixel(legacy_u8 far *bitmap, legacy_u16 offset, legacy_u8 color)
{
	if (video_pages_is_target(bitmap) == 0) {
		bitmap[offset] = color;
		return;
	}
	dos_video_set_write_planes(
		(legacy_u8)(1U << ((offset + dos_memory_pointer_offset(bitmap)) & VGA_PLANE_MASK)));
	video_pages_write_vram(video_pages_address(bitmap, offset), color);
}

void video_pages_fill_span(legacy_u8 far *bitmap, legacy_u16 offset, legacy_u16 count,
						   legacy_u8 color)
{
	if (video_pages_is_target(bitmap) == 0) {
		while (count-- != 0) {
			bitmap[offset++] = color;
		}
		return;
	}
	while (count != 0 && ((offset + dos_memory_pointer_offset(bitmap)) & VGA_PLANE_MASK) != 0) {
		video_pages_write_pixel(bitmap, offset++, color);
		count--;
	}
	if (count >= VGA_PLANE_COUNT) {
		dos_video_set_write_planes(VGA_ALL_PLANES);
		do {
			video_pages_write_vram(video_pages_address(bitmap, offset), color);
			offset += VGA_PLANE_COUNT;
			count -= VGA_PLANE_COUNT;
		} while (count >= VGA_PLANE_COUNT);
	}
	while (count-- != 0) {
		video_pages_write_pixel(bitmap, offset++, color);
	}
}

static legacy_u8 video_pages_apply_raster(legacy_u8 previous, legacy_u8 value, legacy_s16 operation)
{
	if (operation == SHAPE2D_RASTER_AND) {
		return previous & value;
	}
	if (operation == SHAPE2D_RASTER_OR) {
		return previous | value;
	}
	return value;
}

void video_pages_raster_span(legacy_u8 far *destination, legacy_u16 destination_offset,
							 const legacy_u8 far *source, legacy_u16 source_offset,
							 legacy_u16 count, legacy_s16 operation, const legacy_u8 far *palette)
{
	legacy_u8 destination_vga = video_pages_is_target(destination);
	legacy_u8 source_vga = video_pages_is_target(source);
	legacy_u16 step = destination_vga != 0 || source_vga != 0 ? VGA_PLANE_COUNT : 1U;
	if (destination_vga != 0 && source_vga != 0 &&
		dos_memory_pointer_segment(destination) == dos_memory_pointer_segment(source)) {
		/* Preserve the original forward traversal for overlapping screen moves. */
		step = 1U;
	}
	/* Each pass uses a fixed plane. Most blits therefore change VGA registers
	 * at most four times per row instead of once for every pixel. */
	for (legacy_u16 first = 0; first < step; first++) {
		for (legacy_u32 index = first; index < count; index += step) {
			legacy_u16 source_index = (legacy_u16)(source_offset + index);
			legacy_u16 destination_index = (legacy_u16)(destination_offset + index);
			legacy_u8 value = source_vga != 0 ? video_pages_read_pixel(source, source_index)
											  : source[source_index];
			if (operation == SHAPE2D_RASTER_MAP) {
				value = palette[value];
				if (value == SHAPE2D_TRANSPARENT_COLOR) {
					continue;
				}
			}
			if (operation == SHAPE2D_RASTER_AND || operation == SHAPE2D_RASTER_OR) {
				legacy_u8 previous = destination_vga != 0
										 ? video_pages_read_pixel(destination, destination_index)
										 : destination[destination_index];
				value = video_pages_apply_raster(previous, value, operation);
			}
			if (destination_vga != 0) {
				video_pages_write_pixel(destination, destination_index, value);
			} else {
				destination[destination_index] = value;
			}
		}
	}
}

void video_pages_copy_span(legacy_u8 far *destination, legacy_u16 destination_offset,
						   const legacy_u8 far *source, legacy_u16 source_offset, legacy_u16 count)
{
	video_pages_raster_span(destination, destination_offset, source, source_offset, count,
							SHAPE2D_RASTER_COPY, 0);
}

void video_pages_pattern_span(legacy_u8 far *bitmap, legacy_u16 offset, legacy_u16 count,
							  legacy_u8 pattern, legacy_u8 color, legacy_u8 alternate_color,
							  legacy_s16 two_colors)
{
	for (legacy_u16 plane = 0; plane < VGA_PLANE_COUNT; plane++) {
		legacy_u8 rotated =
			(legacy_u8)((pattern << (plane + 1U)) | (pattern >> (LEGACY_BYTE_BITS - 1U - plane)));
		for (legacy_u32 index = plane; index < count; index += VGA_PLANE_COUNT) {
			if ((rotated & 1U) != 0) {
				video_pages_write_pixel(bitmap, (legacy_u16)(offset + index),
										two_colors != 0 ? alternate_color : color);
			} else if (two_colors != 0) {
				video_pages_write_pixel(bitmap, (legacy_u16)(offset + index), color);
			}
			rotated = (legacy_u8)((rotated << VGA_PLANE_COUNT) |
								  (rotated >> (LEGACY_BYTE_BITS - VGA_PLANE_COUNT)));
		}
	}
}

void video_pages_initialize(void)
{
	pages_active = dos_video_enable_planar_pages();
	if (pages_active == 0) {
		return;
	}
	screen_sprite.sprite_bitmapptr =
		(struct SHAPE2D far *)dos_memory_make_pointer(VGA_PAGE_SEGMENT, 0);
	fmemcpy(&hidden_page, &screen_sprite, sizeof(hidden_page));
	hidden_page.sprite_bitmapptr =
		(struct SHAPE2D far *)dos_memory_make_pointer(VGA_SECOND_PAGE_SEGMENT, 0);
	mcga_backbuffer_sprite = &hidden_page;
	sprite_select_screen();
}

void video_pages_begin_race(void)
{
	if (pages_active == 0) {
		return;
	}
	video_uses_page_flipping = 1;
	video_page_count = VGA_PAGE_COUNT;
	full_redraw_frames_remaining = video_page_count;
	frame_buffer_index = 0;
	dashboard_buffer_index = 0;
	video_pages_fill_span((legacy_u8 far *)hidden_page.sprite_bitmapptr, 0, VGA_PAGE_PIXELS, 0);
}

void video_pages_end_race(void)
{
	if (pages_active == 0) {
		return;
	}
	video_uses_page_flipping = 0;
	video_page_count = 1;
	sprite_select_screen();
}

void video_pages_select_backbuffer(void)
{
	if (pages_active != 0) {
		sprite_select_target(&hidden_page);
	}
}

void video_pages_present(void)
{
	if (pages_active == 0) {
		return;
	}
	struct SHAPE2D far *previous = screen_sprite.sprite_bitmapptr;
	dos_video_show_page(video_pages_address(hidden_page.sprite_bitmapptr, 0));
	screen_sprite.sprite_bitmapptr = hidden_page.sprite_bitmapptr;
	hidden_page.sprite_bitmapptr = previous;
	sprite_select_screen();
}
#endif
