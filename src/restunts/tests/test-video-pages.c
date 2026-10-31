#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "../c/externs.h"
#include "../c/platform.h"
#include "../c/shape2d/shape2d.h"
#include "../c/shape2d/shape2d_internal.h"
#include "../c/video_pages.h"

#define TEST_PAGE_PIXELS 65536UL
#define TEST_VISIBLE_PIXELS 64000U
#define TEST_PAGE_PLANE_BYTES 16384U
#define TEST_FIRST_PAGE_SEGMENT 0xA000U
#define TEST_SECOND_PAGE_SEGMENT 0xA400U
#define TEST_SOURCE_SEGMENT 0x2000U
#define TEST_DESTINATION_SEGMENT 0x4000U

/* The renderer sees segmented pointers; hardware reads and writes see four
 * independent planes selected by the modeled VGA registers. The reference
 * raster below uses only packed bytes and forward-copy ordering. */
static legacy_u8 memory[0x100000];
static legacy_u8 planes[4][65536];
static legacy_u8 reference_pages[2][65536];
static legacy_u8 reference_ram[65536];
static legacy_u8 palette[256];
static legacy_u8 write_mask, read_plane, hardware_supported;
static legacy_u32 write_register_changes, read_register_changes, vram_writes;
static legacy_u16 shown_address;
static legacy_u32 present_count;

struct SPRITE far screen_sprite, drawing_sprite;
struct SPRITE far *mcga_backbuffer_sprite;
legacy_s16 video_uses_page_flipping, video_page_count;
legacy_s8 frame_buffer_index, dashboard_buffer_index;
legacy_s8 full_redraw_frames_remaining;

void far *dos_memory_make_pointer(legacy_u16 segment, legacy_u16 offset)
{
	size_t address = (size_t)segment * 16U + offset;
	assert(address < sizeof(memory));
	return memory + address;
}

legacy_u16 dos_memory_pointer_segment(const void far *pointer)
{
	ptrdiff_t address = (const legacy_u8 *)pointer - memory;
	assert(address >= 0 && (size_t)address < sizeof(memory));
	if (address >= 0xA4000 && address < 0xA8000) {
		return TEST_SECOND_PAGE_SEGMENT;
	}
	if (address >= 0xA0000 && address < 0xA4000) {
		return TEST_FIRST_PAGE_SEGMENT;
	}
	return (legacy_u16)((size_t)address >> 4);
}

legacy_u16 dos_memory_pointer_offset(const void far *pointer)
{
	ptrdiff_t address = (const legacy_u8 *)pointer - memory;
	return (legacy_u16)(address - (size_t)dos_memory_pointer_segment(pointer) * 16U);
}

void far *__fmemcpy(void far *destination, const void far *source, legacy_u16 count)
{
	return memmove(destination, source, count);
}

void sprite_select_target(struct SPRITE far *sprite)
{
	memmove(&drawing_sprite, sprite, sizeof(drawing_sprite));
}

void sprite_select_screen(void)
{
	sprite_select_target(&screen_sprite);
}

legacy_u8 dos_video_enable_planar_pages(void)
{
	if (hardware_supported != 0) {
		memset(planes, 0, sizeof(planes));
		write_mask = 15U;
		read_plane = 0;
		shown_address = 0;
	}
	return hardware_supported;
}

void dos_video_set_write_planes(legacy_u8 mask)
{
	assert(mask < 16U);
	if (write_mask != mask) {
		write_register_changes++;
		write_mask = mask;
	}
}

void dos_video_set_read_plane(legacy_u8 plane)
{
	assert(plane < 4U);
	if (read_plane != plane) {
		read_register_changes++;
		read_plane = plane;
	}
}

void dos_video_show_page(legacy_u16 address)
{
	assert(address == 0 || address == TEST_PAGE_PLANE_BYTES);
	shown_address = address;
	present_count++;
}

legacy_u8 video_pages_test_read(legacy_u16 address)
{
	return planes[read_plane][address];
}

void video_pages_test_write(legacy_u16 address, legacy_u8 value)
{
	for (legacy_u32 plane = 0; plane < 4U; plane++) {
		if ((write_mask & (1U << plane)) != 0) {
			planes[plane][address] = value;
		}
	}
	vram_writes++;
}

static legacy_u8 *page_pointer(legacy_u32 page)
{
	return dos_memory_make_pointer(page == 0 ? TEST_FIRST_PAGE_SEGMENT : TEST_SECOND_PAGE_SEGMENT,
								   0);
}

static legacy_u8 *source_pointer(void)
{
	return dos_memory_make_pointer(TEST_SOURCE_SEGMENT, 0);
}

static legacy_u8 *destination_pointer(void)
{
	return dos_memory_make_pointer(TEST_DESTINATION_SEGMENT, 0);
}

static void reset_fixture(void)
{
	memset(planes, 0xD7, sizeof(planes));
	for (legacy_u32 page = 0; page < 2U; page++) {
		for (legacy_u32 pixel = 0; pixel < TEST_PAGE_PIXELS; pixel++) {
			legacy_u8 value = (legacy_u8)(pixel * 37UL + pixel / 13UL + page * 113U);
			reference_pages[page][pixel] = value;
			planes[pixel & 3U][page * TEST_PAGE_PLANE_BYTES + pixel / 4U] = value;
		}
	}
	for (legacy_u32 pixel = 0; pixel < TEST_PAGE_PIXELS; pixel++) {
		source_pointer()[pixel] = (legacy_u8)(pixel * 23UL + pixel / 11UL);
		destination_pointer()[pixel] = reference_ram[pixel] = (legacy_u8)(pixel * 17UL + 41U);
	}
	write_mask = read_plane = 255U;
	write_register_changes = read_register_changes = vram_writes = 0;
}

static void assert_pages_match(void)
{
	for (legacy_u32 page = 0; page < 2U; page++) {
		for (legacy_u32 pixel = 0; pixel < TEST_PAGE_PIXELS; pixel++) {
			assert(planes[pixel & 3U][page * TEST_PAGE_PLANE_BYTES + pixel / 4U] ==
				   reference_pages[page][pixel]);
		}
	}
	for (legacy_u32 plane = 0; plane < 4U; plane++) {
		for (legacy_u32 address = 2UL * TEST_PAGE_PLANE_BYTES; address < 65536UL; address++) {
			assert(planes[plane][address] == 0xD7U);
		}
	}
}

static void reference_raster(legacy_u8 *destination, legacy_u16 destination_offset,
							 const legacy_u8 *source, legacy_u16 source_offset, legacy_u16 count,
							 legacy_s16 operation)
{
	for (legacy_u32 index = 0; index < count; index++) {
		legacy_u8 value = source[source_offset++];
		if (operation == SHAPE2D_RASTER_AND) {
			destination[destination_offset] &= value;
		} else if (operation == SHAPE2D_RASTER_OR) {
			destination[destination_offset] |= value;
		} else if (operation == SHAPE2D_RASTER_MAP) {
			value = palette[value];
			if (value != 255U) {
				destination[destination_offset] = value;
			}
		} else {
			destination[destination_offset] = value;
		}
		destination_offset++;
	}
}

static void test_pixels_and_fills(void)
{
	static const legacy_u16 offsets[] = {0, 1, 2, 3, 319, 320, 63999, 64000, 65534, 65535};
	static const legacy_u16 counts[] = {0, 1, 2, 3, 4, 5, 9, 320, 64000, 65535};
	for (legacy_u32 page = 0; page < 2U; page++) {
		reset_fixture();
		for (legacy_u32 index = 0; index < sizeof(offsets) / sizeof(offsets[0]); index++) {
			legacy_u16 offset = offsets[index];
			legacy_u8 value = (legacy_u8)(index * 9U + 3U);
			video_pages_write_pixel(page_pointer(page), offset, value);
			reference_pages[page][offset] = value;
			assert(video_pages_read_pixel(page_pointer(page), offset) == value);
		}
		/* A far pointer may carry an offset as well as a segment. */
		for (legacy_u16 pointer_offset = 0; pointer_offset < 16U; pointer_offset++) {
			legacy_u8 value = (legacy_u8)(pointer_offset + 0xA0U);
			video_pages_write_pixel(page_pointer(page) + pointer_offset, 65535U, value);
			reference_pages[page][(legacy_u16)(65535U + pointer_offset)] = value;
			assert(video_pages_read_pixel(page_pointer(page) + pointer_offset, 65535U) == value);
		}
		assert_pages_match();
		for (legacy_u16 alignment = 0; alignment < 4U; alignment++) {
			for (legacy_u32 index = 0; index < sizeof(counts) / sizeof(counts[0]); index++) {
				reset_fixture();
				legacy_u16 start = (legacy_u16)(65533U + alignment);
				video_pages_fill_span(page_pointer(page), start, counts[index], 0xACU);
				for (legacy_u32 pixel = 0; pixel < counts[index]; pixel++) {
					reference_pages[page][(legacy_u16)(start + pixel)] = 0xACU;
				}
				assert(write_register_changes <= 7U);
				assert(vram_writes <= counts[index] / 4U + 6U);
				assert_pages_match();
			}
		}
	}
}

static void test_raster_spans(void)
{
	for (legacy_u16 source_alignment = 0; source_alignment < 4U; source_alignment++) {
		for (legacy_u16 destination_alignment = 0; destination_alignment < 4U;
			 destination_alignment++) {
			for (legacy_s16 operation = SHAPE2D_RASTER_AND; operation <= SHAPE2D_RASTER_MAP;
				 operation++) {
				for (legacy_u32 directions = 0; directions < 4U; directions++) {
					reset_fixture();
					legacy_u16 source_offset = (legacy_u16)(65533U + source_alignment);
					legacy_u16 destination_offset = (legacy_u16)(65533U + destination_alignment);
					legacy_u8 *source = (directions & 1U) != 0 ? page_pointer(0) : source_pointer();
					legacy_u8 *destination =
						(directions & 2U) != 0 ? page_pointer(1) : destination_pointer();
					legacy_u8 *reference_source =
						(directions & 1U) != 0 ? reference_pages[0] : source_pointer();
					legacy_u8 *reference_destination =
						(directions & 2U) != 0 ? reference_pages[1] : reference_ram;
					video_pages_raster_span(destination, destination_offset, source, source_offset,
											329U, operation, palette);
					reference_raster(reference_destination, destination_offset, reference_source,
									 source_offset, 329U, operation);
					if (directions == 2U) {
						assert(write_register_changes <= 4U);
						assert(read_register_changes <= 4U);
					}
					assert(memcmp(destination_pointer(), reference_ram, sizeof(reference_ram)) ==
						   0);
					assert_pages_match();
				}
			}
		}
	}
	reset_fixture();
	video_pages_copy_span(page_pointer(1), 65535U, page_pointer(0), 3U, 65535U);
	reference_raster(reference_pages[1], 65535U, reference_pages[0], 3U, 65535U,
					 SHAPE2D_RASTER_COPY);
	assert(write_register_changes <= 4U && read_register_changes <= 4U);
	assert_pages_match();
	reset_fixture();
	video_pages_copy_span(page_pointer(1), 0, page_pointer(0), 0, 0);
	assert(vram_writes == 0 && write_register_changes == 0 && read_register_changes == 0);
	assert_pages_match();
	reset_fixture();
	video_pages_copy_span(page_pointer(1) + 13U, 65535U, page_pointer(0) + 7U, 65535U, 329U);
	reference_raster(reference_pages[1], 12U, reference_pages[0], 6U, 329U, SHAPE2D_RASTER_COPY);
	assert_pages_match();
}

static void test_overlapping_copies(void)
{
	static const legacy_u16 offsets[][2] = {{0, 1},		{1, 0},		{65534, 65535}, {65535, 65534},
											{65530, 0}, {0, 65530}, {0, 0},			{0, 4}};
	for (legacy_u32 index = 0; index < sizeof(offsets) / sizeof(offsets[0]); index++) {
		reset_fixture();
		video_pages_copy_span(page_pointer(0), offsets[index][1], page_pointer(0),
							  offsets[index][0], 19U);
		reference_raster(reference_pages[0], offsets[index][1], reference_pages[0],
						 offsets[index][0], 19U, SHAPE2D_RASTER_COPY);
		assert_pages_match();
	}
}

static void test_pattern_spans(void)
{
	static const legacy_u8 patterns[] = {0, 255, 0x81, 0x69};
	static const legacy_u16 counts[] = {0, 1, 3, 4, 9, 321};
	for (legacy_u16 alignment = 0; alignment < 4U; alignment++) {
		for (legacy_u32 pattern_index = 0; pattern_index < sizeof(patterns); pattern_index++) {
			for (legacy_s16 two_colors = 0; two_colors <= 1; two_colors++) {
				for (legacy_u32 index = 0; index < sizeof(counts) / sizeof(counts[0]); index++) {
					reset_fixture();
					legacy_u16 start = (legacy_u16)(65533U + alignment);
					legacy_u8 pattern = patterns[pattern_index];
					video_pages_pattern_span(page_pointer(1), start, counts[index], pattern, 37U,
											 198U, two_colors);
					for (legacy_u32 pixel = 0; pixel < counts[index]; pixel++) {
						pattern = (legacy_u8)((pattern << 1) | (pattern >> 7));
						if ((pattern & 1U) != 0) {
							reference_pages[1][(legacy_u16)(start + pixel)] =
								two_colors != 0 ? 198U : 37U;
						} else if (two_colors != 0) {
							reference_pages[1][(legacy_u16)(start + pixel)] = 37U;
						}
					}
					assert(write_register_changes <= 4U);
					assert_pages_match();
				}
			}
		}
	}
}

static void test_page_lifecycle(void)
{
	reset_fixture();
	frame_buffer_index = dashboard_buffer_index = 1;
	video_pages_begin_race();
	assert(video_uses_page_flipping == 1 && video_page_count == 2);
	assert(frame_buffer_index == 0 && dashboard_buffer_index == 0);
	memset(reference_pages[1], 0, TEST_VISIBLE_PIXELS);
	assert_pages_match();
	video_pages_select_backbuffer();
	assert(drawing_sprite.sprite_bitmapptr == (struct SHAPE2D *)page_pointer(1));
	assert(drawing_sprite.sprite_pitch == 320 && drawing_sprite.sprite_bottom == 200);
	video_pages_write_pixel(page_pointer(1), 1234U, 21U);
	reference_pages[1][1234] = 21U;
	video_pages_present();
	assert(present_count == 1 && shown_address == TEST_PAGE_PLANE_BYTES);
	assert(screen_sprite.sprite_bitmapptr == (struct SHAPE2D *)page_pointer(1));
	assert(drawing_sprite.sprite_bitmapptr == screen_sprite.sprite_bitmapptr);
	video_pages_select_backbuffer();
	assert(drawing_sprite.sprite_bitmapptr == (struct SHAPE2D *)page_pointer(0));
	video_pages_write_pixel(page_pointer(0), 1234U, 83U);
	reference_pages[0][1234] = 83U;
	video_pages_present();
	assert(present_count == 2 && shown_address == 0);
	assert(screen_sprite.sprite_bitmapptr == (struct SHAPE2D *)page_pointer(0));
	assert_pages_match();
	video_pages_end_race();
	assert(video_uses_page_flipping == 0 && video_page_count == 1);
	assert(drawing_sprite.sprite_bitmapptr == screen_sprite.sprite_bitmapptr);
	assert_pages_match();
	video_pages_begin_race();
	memset(reference_pages[1], 0, TEST_VISIBLE_PIXELS);
	assert_pages_match();
	video_pages_end_race();
}

static void test_unsupported_fallback(void)
{
	hardware_supported = 0;
	mcga_backbuffer_sprite = NULL;
	video_pages_initialize();
	assert(video_pages_is_active() == 0);
	assert(video_pages_is_target(page_pointer(0)) == 0);
	assert(mcga_backbuffer_sprite == NULL);
	video_pages_begin_race();
	video_pages_end_race();
	assert(video_uses_page_flipping == 0 && video_page_count == 1);
	struct SHAPE2D *previous_target = drawing_sprite.sprite_bitmapptr;
	legacy_u32 previous_present_count = present_count;
	video_pages_select_backbuffer();
	video_pages_present();
	assert(drawing_sprite.sprite_bitmapptr == previous_target);
	assert(present_count == previous_present_count);
	reset_fixture();
	video_pages_fill_span(destination_pointer(), 65535U, 7U, 51U);
	for (legacy_u16 index = 0; index < 7U; index++) {
		reference_ram[(legacy_u16)(65535U + index)] = 51U;
	}
	video_pages_raster_span(destination_pointer(), 11U, source_pointer(), 65535U, 321U,
							SHAPE2D_RASTER_MAP, palette);
	reference_raster(reference_ram, 11U, source_pointer(), 65535U, 321U, SHAPE2D_RASTER_MAP);
	assert(memcmp(destination_pointer(), reference_ram, sizeof(reference_ram)) == 0);
	video_pages_write_pixel(page_pointer(0), 139U, 213U);
	assert(page_pointer(0)[139] == 213U);
	assert(video_pages_read_pixel(page_pointer(0), 139U) == 213U);
	assert(vram_writes == 0 && write_register_changes == 0 && read_register_changes == 0);
	assert_pages_match();
}

int main(void)
{
	hardware_supported = 1;
	screen_sprite.sprite_bitmapptr = (struct SHAPE2D *)page_pointer(0);
	screen_sprite.sprite_pitch = screen_sprite.sprite_right = 320;
	screen_sprite.sprite_bottom = 200;
	video_page_count = 1;
	for (legacy_u32 index = 0; index < sizeof(palette); index++) {
		palette[index] = index % 7U == 0 ? 255U : (legacy_u8)(index ^ 0x55U);
	}
	video_pages_initialize();
	assert(video_pages_is_active() != 0 && mcga_backbuffer_sprite != NULL);
	assert(video_pages_is_target(page_pointer(0)) != 0);
	assert(video_pages_is_target(page_pointer(1)) != 0);
	assert(video_pages_is_target(source_pointer()) == 0);
	test_pixels_and_fills();
	test_raster_spans();
	test_overlapping_copies();
	test_pattern_spans();
	test_page_lifecycle();
	test_unsupported_fallback();
	puts("VGA page raster and lifecycle checks passed");
	return 0;
}
