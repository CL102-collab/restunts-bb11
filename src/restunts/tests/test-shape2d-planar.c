/* Reuse the segmented-memory fixtures and independent legacy RLE fingerprint.
 * The backend below stores VGA pixels separately from the addressable RAM, so
 * accidentally bypassing a page helper cannot silently satisfy the comparison.
 * VGA register/plane mapping is covered by test-video-pages.c. */
#define main shape2d_reference_main
#include "test-shape2d-render.c"
#undef main
#include "../c/video_pages.h"

static legacy_u8 planar_enabled;
static legacy_u8 planar_pixels[65536];
static legacy_u8 expected_pixels[65536];
static legacy_u32 raster_spans[4];
static legacy_u32 fill_spans;
static legacy_u32 pixel_writes;
static legacy_u8 last_read_plane, last_write_plane;
static legacy_u32 read_plane_changes, write_plane_changes;

legacy_u8 video_pages_is_target(const void far *bitmap)
{
	return planar_enabled != 0 && bitmap == dos_memory_make_pointer(TEST_BITMAP_SEGMENT, 0);
}

legacy_u8 video_pages_read_pixel(const legacy_u8 far *bitmap, legacy_u16 offset)
{
	if (video_pages_is_target(bitmap) != 0) {
		legacy_u8 plane = (legacy_u8)(offset & 3U);
		if (last_read_plane != plane) {
			last_read_plane = plane;
			read_plane_changes++;
		}
		return planar_pixels[offset];
	}
	return bitmap[offset];
}

void video_pages_write_pixel(legacy_u8 far *bitmap, legacy_u16 offset, legacy_u8 color)
{
	if (video_pages_is_target(bitmap) != 0) {
		legacy_u8 plane = (legacy_u8)(offset & 3U);
		if (last_write_plane != plane) {
			last_write_plane = plane;
			write_plane_changes++;
		}
		planar_pixels[offset] = color;
		pixel_writes++;
	} else {
		bitmap[offset] = color;
	}
}

void video_pages_fill_span(legacy_u8 far *bitmap, legacy_u16 offset, legacy_u16 count,
						   legacy_u8 color)
{
	if (video_pages_is_target(bitmap) != 0) {
		fill_spans++;
	}
	while (count-- != 0) {
		video_pages_write_pixel(bitmap, offset++, color);
	}
}

void video_pages_raster_span(legacy_u8 far *destination, legacy_u16 destination_offset,
							 const legacy_u8 far *source, legacy_u16 source_offset,
							 legacy_u16 count, legacy_s16 operation, const legacy_u8 far *palette)
{
	assert(operation >= SHAPE2D_RASTER_AND && operation <= SHAPE2D_RASTER_MAP);
	if (video_pages_is_target(destination) != 0) {
		raster_spans[operation]++;
	}
	while (count-- != 0) {
		legacy_u8 value = video_pages_read_pixel(source, source_offset++);
		if (operation == SHAPE2D_RASTER_AND) {
			value &= video_pages_read_pixel(destination, destination_offset);
		} else if (operation == SHAPE2D_RASTER_OR) {
			value |= video_pages_read_pixel(destination, destination_offset);
		} else if (operation == SHAPE2D_RASTER_MAP) {
			value = palette[value];
			if (value == 255U) {
				destination_offset++;
				continue;
			}
		}
		video_pages_write_pixel(destination, destination_offset++, value);
	}
}

void video_pages_copy_span(legacy_u8 far *destination, legacy_u16 destination_offset,
						   const legacy_u8 far *source, legacy_u16 source_offset, legacy_u16 count)
{
	video_pages_raster_span(destination, destination_offset, source, source_offset, count,
							SHAPE2D_RASTER_COPY, NULL);
}

static void reset_planar_bitmap(void)
{
	reset_bitmap();
	memset(planar_pixels, 0x5a, sizeof(planar_pixels));
}

static void draw_rle_case(legacy_u32 scenario)
{
	static const legacy_u8 stream[] = {252, 0, 1, 255, 63, 5, 72, 253, 9, 0, 11, 0};
	static const legacy_s16 positions[] = {-32768, -6, -1, 0, 1, 60, 64, 32767};
	static const legacy_u16 widths[] = {0, 1, 6, 0x8000};
	legacy_u32 unclipped = scenario / 128;
	scenario %= 128;
	reset_planar_bitmap();
	struct SHAPE2D *shape = make_shape(widths[scenario % 4], 6, scenario & 1 ? 0xfff0 : 16);
	write_rle(shape_offset, stream, sizeof(stream));
	if (unclipped == 0) {
		if ((scenario & 4) != 0) {
			write_rle(shape_offset, stream + sizeof(stream) - 1, 1);
		}
		shape2d_rle_copy_clipped(shape, positions[(scenario / 4) % 8],
								 positions[(scenario / 8) % 8]);
	} else {
		shape->position_x = 0xfffe;
		shape->position_y = 2;
		if (scenario % 3 == 0) {
			shape2d_rle_copy_at_position(shape);
		} else if (scenario % 3 == 1) {
			shape2d_render_bmp_as_mask(shape);
		} else {
			shape2d_rle_or_far_pointer(shape_offset, TEST_SOURCE_SEGMENT);
		}
	}
}

static void draw_rle_clipped_mask_case(legacy_u32 scenario)
{
	reset_planar_bitmap();
	draw_rle_clipped_mask(scenario);
}

static void draw_rle_mask_case(legacy_u32 scenario)
{
	static const legacy_u8 values[] = {0, 255, 0x33, 0x80};
	static const legacy_u16 widths[] = {1, 3, 7, 64, 127};
	static const legacy_u16 positions[] = {0, 1, 2, 3, 0xfffe};
	legacy_u8 stream[130];
	legacy_u8 literal = scenario >= 200;
	legacy_u8 use_or = (scenario / 100) % 2;
	legacy_u8 value = values[scenario % 4];
	reset_planar_bitmap();
	struct SHAPE2D *shape = make_shape(widths[(scenario / 4) % 5], 128, scenario & 1 ? 0xffe0 : 16);
	if (literal != 0) {
		stream[0] = 128;
		for (legacy_u32 index = 0; index < 128; index++) {
			stream[index + 1] = (legacy_u8)(value + index * 13);
		}
		stream[129] = 0;
		write_rle(shape_offset, stream, sizeof(stream));
	} else {
		stream[0] = 127;
		stream[1] = value;
		stream[2] = 0;
		write_rle(shape_offset, stream, 3);
	}
	shape->position_x = positions[(scenario / 20) % 5];
	shape->position_y = 2;
	legacy_u32 previous_writes = pixel_writes;
	if (use_or != 0) {
		shape2d_rle_or_far_pointer(shape_offset, TEST_SOURCE_SEGMENT);
	} else {
		shape2d_render_bmp_as_mask(shape);
	}
	if (planar_enabled != 0 && literal == 0 && value == (use_or != 0 ? 0U : 255U)) {
		assert(pixel_writes == previous_writes);
	}
}

static void test_repeated_mask_plane_switches(void)
{
	static const legacy_u8 stream[] = {127, 0x33, 0};
	planar_enabled = 1;
	for (legacy_u32 use_or = 0; use_or < 2; use_or++) {
		reset_planar_bitmap();
		struct SHAPE2D *shape = make_shape(127, 1, 16);
		write_rle(shape_offset, stream, sizeof(stream));
		last_read_plane = last_write_plane = 255;
		read_plane_changes = write_plane_changes = 0;
		if (use_or != 0) {
			shape2d_rle_or_far_pointer(shape_offset, TEST_SOURCE_SEGMENT);
		} else {
			shape2d_render_bmp_as_mask(shape);
		}
		assert(read_plane_changes == 4 && write_plane_changes == 4);
	}
}

static void draw_raw_case(legacy_u32 scenario)
{
	static const legacy_s16 positions[] = {-32768, -6, -1, 0, 1, 59, 64, 32767};
	static const legacy_u16 widths[] = {0, 1, 6, 64, 67};
	reset_planar_bitmap();
	struct SHAPE2D *shape = make_shape(widths[scenario % 5], 6, scenario & 1 ? 0xfff0 : 16);
	legacy_s16 x = positions[(scenario / 5) % 8];
	legacy_s16 y = positions[(scenario / 40) % 8];
	switch ((scenario / 320) % 4) {
		case SHAPE2D_RASTER_AND:
			sprite_putimage_and(shape, (legacy_u16)x, (legacy_u16)y);
			break;
		case SHAPE2D_RASTER_OR:
			sprite_putimage_or(shape, (legacy_u16)x, (legacy_u16)y);
			break;
		case SHAPE2D_RASTER_COPY:
			sprite_copy_image_at(shape, x, y);
			break;
		case SHAPE2D_RASTER_MAP:
			sprite_putimage_transparent(shape, x, y);
			break;
	}
}

static void draw_clear_case(legacy_u32 scenario)
{
	reset_planar_bitmap();
	sprite_set_target_clip_bounds(scenario % 5, 60 - scenario % 7, scenario % 9,
								  40 - scenario % 11);
	sprite_clear_target((legacy_u8)(scenario * 17));
}

static void draw_transformed_case(legacy_u32 scenario)
{
	reset_planar_bitmap();
	draw_transform_fixture(scenario);
}

static void draw_ten_degree_transformed_case(legacy_u32 scenario)
{
	reset_planar_bitmap();
	draw_ten_degree_transform_fixture(scenario);
}

static void compare_targets(legacy_u32 cases, void (*draw_case)(legacy_u32))
{
	legacy_u8 *bitmap = dos_memory_make_pointer(TEST_BITMAP_SEGMENT, 0);
	for (legacy_u32 scenario = 0; scenario < cases; scenario++) {
		planar_enabled = 0;
		draw_case(scenario);
		memmove(expected_pixels, bitmap, sizeof(expected_pixels));
		planar_enabled = 1;
		draw_case(scenario);
		assert(memcmp(expected_pixels, planar_pixels, sizeof(expected_pixels)) == 0);
		for (legacy_u32 offset = 0; offset < sizeof(expected_pixels); offset++) {
			assert(bitmap[offset] == 0x5a);
		}
	}
}

int main(void)
{
	planar_enabled = 0;
	check_fingerprint("RAM RLE", 0x45b9c13cUL, test_rle);
	compare_targets(256, draw_rle_case);
	compare_targets(256, draw_rle_clipped_mask_case);
	compare_targets(400, draw_rle_mask_case);
	test_repeated_mask_plane_switches();
	for (legacy_u32 index = 0; index < 256; index++) {
		sprite_palette_map[index] = (legacy_u8)(index % 3 == 0 ? 255 : index + 17);
	}
	compare_targets(1280, draw_raw_case);
	compare_targets(100, draw_clear_case);
	compare_targets(TEST_TRANSFORM_CASE_COUNT, draw_transformed_case);
	compare_targets(2U, draw_ten_degree_transformed_case);
	for (legacy_u32 operation = 0; operation < 4; operation++) {
		assert(raster_spans[operation] != 0);
	}
	assert(fill_spans != 0);
	assert(pixel_writes != 0);
	return 0;
}
