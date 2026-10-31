#ifndef RESTUNTS_VIDEO_PAGES_H
#define RESTUNTS_VIDEO_PAGES_H

#include "shape2d/shape2d.h"

#define VGA_PLANE_COUNT 4U
#define VGA_PAGE_PLANE_BYTES 16384U
#ifdef RESTUNTS_SDL3
#include "hires/hires.h"
#endif

/* Page sprites retain chunky 320-byte rows. Only the raster helpers translate
 * those logical offsets into VGA planes; resource bitmaps remain ordinary RAM. */
#if (defined(__WATCOMC__) && defined(__I86__) && defined(RESTUNTS_FULL)) ||                        \
	defined(RESTUNTS_VGA_TEST)
#define RESTUNTS_HAS_VGA_PAGES 1
void video_pages_initialize(void);
legacy_u8 video_pages_is_active(void);
void video_pages_begin_race(void);
void video_pages_end_race(void);
void video_pages_select_backbuffer(void);
void video_pages_present(void);
legacy_u8 video_pages_is_target(const void far *bitmap);
legacy_u8 video_pages_read_pixel(const legacy_u8 far *bitmap, legacy_u16 offset);
void video_pages_write_pixel(legacy_u8 far *bitmap, legacy_u16 offset, legacy_u8 color);
void video_pages_fill_span(legacy_u8 far *bitmap, legacy_u16 offset, legacy_u16 count,
						   legacy_u8 color);
void video_pages_copy_span(legacy_u8 far *destination, legacy_u16 destination_offset,
						   const legacy_u8 far *source, legacy_u16 source_offset, legacy_u16 count);
void video_pages_raster_span(legacy_u8 far *destination, legacy_u16 destination_offset,
							 const legacy_u8 far *source, legacy_u16 source_offset,
							 legacy_u16 count, legacy_s16 operation, const legacy_u8 far *palette);
void video_pages_pattern_span(legacy_u8 far *bitmap, legacy_u16 offset, legacy_u16 count,
							  legacy_u8 pattern, legacy_u8 color, legacy_u8 alternate_color,
							  legacy_s16 two_colors);
#else
/* Host regressions and replay dumps keep the original packed-memory path. */
static inline void video_pages_initialize(void)
{
}
static inline legacy_u8 video_pages_is_active(void)
{
	return 0;
}
static inline void video_pages_begin_race(void)
{
}
static inline void video_pages_end_race(void)
{
}
static inline void video_pages_select_backbuffer(void)
{
}
static inline void video_pages_present(void)
{
}
static inline legacy_u8 video_pages_is_target(const void far *bitmap)
{
	(void)bitmap;
	return 0;
}
static inline legacy_u8 video_pages_read_pixel(const legacy_u8 far *bitmap, legacy_u16 offset)
{
	return bitmap[offset];
}
static inline void video_pages_write_pixel(legacy_u8 far *bitmap, legacy_u16 offset,
										   legacy_u8 color)
{
#ifdef RESTUNTS_SDL3
	hires_write(bitmap, offset, color);
#endif
	bitmap[offset] = color;
}
static inline void video_pages_fill_span(legacy_u8 far *bitmap, legacy_u16 offset, legacy_u16 count,
										 legacy_u8 color)
{
	while (count-- != 0) {
		video_pages_write_pixel(bitmap, offset++, color);
	}
}
static inline void video_pages_raster_span(legacy_u8 far *destination,
										   legacy_u16 destination_offset,
										   const legacy_u8 far *source, legacy_u16 source_offset,
										   legacy_u16 count, legacy_s16 operation,
										   const legacy_u8 far *palette)
{
#ifdef RESTUNTS_SDL3
	hires_raster(destination, destination_offset, source, source_offset, count, operation, palette);
#endif
	while (count-- != 0) {
		legacy_u8 value = source[source_offset++];
		if (operation == SHAPE2D_RASTER_AND) {
			destination[destination_offset] &= value;
		} else if (operation == SHAPE2D_RASTER_OR) {
			destination[destination_offset] |= value;
		} else if (operation == SHAPE2D_RASTER_MAP) {
			value = palette[value];
			if (value != SHAPE2D_TRANSPARENT_COLOR) {
				destination[destination_offset] = value;
			}
		} else {
			destination[destination_offset] = value;
		}
		destination_offset++;
	}
}
static inline void video_pages_copy_span(legacy_u8 far *destination, legacy_u16 destination_offset,
										 const legacy_u8 far *source, legacy_u16 source_offset,
										 legacy_u16 count)
{
	video_pages_raster_span(destination, destination_offset, source, source_offset, count,
							SHAPE2D_RASTER_COPY, 0);
}
static inline void video_pages_pattern_span(legacy_u8 far *bitmap, legacy_u16 offset,
											legacy_u16 count, legacy_u8 pattern, legacy_u8 color,
											legacy_u8 alternate_color, legacy_s16 two_colors)
{
	while (count-- != 0) {
		pattern = (legacy_u8)((pattern << 1) | (pattern >> (LEGACY_BYTE_BITS - 1U)));
		if ((pattern & 1U) != 0) {
			video_pages_write_pixel(bitmap, offset, two_colors != 0 ? alternate_color : color);
		} else if (two_colors != 0) {
			video_pages_write_pixel(bitmap, offset, color);
		}
		offset++;
	}
}
#endif

/* SDL companion pixels share the span path with planar VGA. The underlying
 * byte framebuffer remains packed, including when high resolution is disabled. */
static inline legacy_u8 video_pages_uses_raster_hooks(const void far *bitmap)
{
#ifdef RESTUNTS_SDL3
	(void)bitmap;
	return 1;
#else
	return video_pages_is_target(bitmap);
#endif
}

#endif
