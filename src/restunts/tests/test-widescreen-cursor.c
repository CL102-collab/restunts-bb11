#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../c/externs.h"
#include "../c/platform.h"
#include "../c/game_input.h"
#include "../c/hires/hires.h"
#include "../c/hires/widescreen.h"
#include "../c/shape2d/shape2d.h"
#include "../c/shape2d/shape2d_internal.h"

#define TEST_VGA_SEGMENT 0xA000U
#define TEST_AND_SEGMENT 0x2000U
#define TEST_OR_SEGMENT 0x3000U
#define TEST_CURSOR_SIZE 3U
#define TEST_CURSOR_CENTER 4U
#define TEST_BACKGROUND 3U
#define TEST_CURSOR_WHITE 15U
#define TEST_CURSOR_X 300
#define TEST_CURSOR_Y 180
#define TEST_CURSOR_NEXT_X 20
#define TEST_CURSOR_NEXT_Y 30
#define TEST_PARENT_FIRST 7U
#define TEST_PARENT_SECOND 8U
#define TEST_PARENT_FIRST_OFFSET (5U * VIDEO_FRAMEBUFFER_WIDTH + 5U)
#define TEST_PARENT_SECOND_OFFSET (6U * VIDEO_FRAMEBUFFER_WIDTH + 6U)

static legacy_u8 rows[VIDEO_FRAMEBUFFER_HEIGHT * LEGACY_WORD_BYTES];
static legacy_u8 reference[VIDEO_FRAMEBUFFER_WIDTH * VIDEO_FRAMEBUFFER_HEIGHT];
static struct SPRITE and_cursor, or_cursor;

static void prepare_cursor(legacy_u16 segment, struct SPRITE *sprite, legacy_u8 mask)
{
	struct SHAPE2D *shape = dos_memory_make_pointer(segment, 0);
	memset(shape, 0, SHAPE2D_HEADER_SIZE + TEST_CURSOR_SIZE * TEST_CURSOR_SIZE);
	shape->width = TEST_CURSOR_SIZE;
	shape->height = TEST_CURSOR_SIZE;
	legacy_u8 *pixels = (legacy_u8 *)shape + SHAPE2D_HEADER_SIZE;
	memset(pixels, mask ? LEGACY_U8_MAX : 0, TEST_CURSOR_SIZE * TEST_CURSOR_SIZE);
	pixels[0] = 0;
	pixels[TEST_CURSOR_CENTER] = mask ? 0 : TEST_CURSOR_WHITE;
	memset(sprite, 0, sizeof(*sprite));
	sprite->sprite_bitmapptr = shape;
}

static void assert_scene_unchanged(const legacy_u8 *legacy)
{
	assert(memcmp(legacy, reference, sizeof(reference)) == 0);
}

static legacy_u8 output_at(const legacy_u8 *image, legacy_s32 width, legacy_s32 height,
						   legacy_s32 x, legacy_s32 y)
{
	legacy_s32 art_width = height * WIDESCREEN_ASPECT_WIDTH / WIDESCREEN_ASPECT_HEIGHT;
	legacy_s32 left = (width - art_width) / 2;
	legacy_s32 output_x =
		left + (x * art_width + VIDEO_FRAMEBUFFER_WIDTH - 1) / VIDEO_FRAMEBUFFER_WIDTH;
	legacy_s32 output_y = (y * height + VIDEO_FRAMEBUFFER_HEIGHT - 1) / VIDEO_FRAMEBUFFER_HEIGHT;
	return image[output_y * width + output_x];
}

static void assert_cursor(const legacy_u8 *legacy)
{
	legacy_s32 width, height;
	const legacy_u8 *image = hires_framebuffer(legacy, &width, &height);
	assert(image != NULL && width == hires_presentation_width());
	assert(height == hires_presentation_height());
	assert(output_at(image, width, height, mouse_xpos + 1, mouse_ypos + 1) == TEST_CURSOR_WHITE);
	assert(output_at(image, width, height, mouse_xpos, mouse_ypos) == 0);
	assert(output_at(image, width, height, mouse_xpos + 2, mouse_ypos) == TEST_BACKGROUND);
	assert_scene_unchanged(legacy);
}

static void assert_erased(const legacy_u8 *legacy)
{
	legacy_s32 width, height;
	const legacy_u8 *image = hires_framebuffer(legacy, &width, &height);
	assert(image != NULL);
	for (legacy_s32 pixel = 0; pixel < width * height; pixel++) {
		assert(image[pixel] == TEST_BACKGROUND);
	}
	assert_scene_unchanged(legacy);
}

static void test_cursor(legacy_s32 width, legacy_s32 height, legacy_s32 scale)
{
	legacy_u8 *legacy = dos_memory_make_pointer(TEST_VGA_SEGMENT, 0);
	hires_shutdown();
	memset(legacy, TEST_BACKGROUND, sizeof(reference));
	memset(reference, TEST_BACKGROUND, sizeof(reference));
	hires_set_enabled(1);
	hires_set_render_scale(scale);
	widescreen_set_enabled(1);
	widescreen_set_racing(1);
	widescreen_set_output_size(width, height);
	assert(widescreen_active());
	widescreen_begin_frame();
	sprite_select_screen();
	struct SPRITE saved = drawing_sprite;
	mouse_xpos = TEST_CURSOR_X;
	mouse_ypos = TEST_CURSOR_Y;
	mouse_draw_transparent();
	assert(mouse_background_dirty != 0);
	assert(memcmp(&saved, &drawing_sprite, sizeof(saved)) == 0);
	assert_cursor(legacy);
	mouse_draw_opaque();
	assert(mouse_background_dirty == 0);
	assert_erased(legacy);

	/* Nested cursor work must preserve the surrounding toolbar/dialog layer. */
	widescreen_overlay_begin(WIDESCREEN_OVERLAY_CENTER);
	assert(widescreen_overlay_write(legacy, TEST_PARENT_FIRST_OFFSET, TEST_PARENT_FIRST));
	mouse_xpos = TEST_CURSOR_NEXT_X;
	mouse_ypos = TEST_CURSOR_NEXT_Y;
	mouse_draw_transparent();
	assert(widescreen_overlay_write(legacy, TEST_PARENT_SECOND_OFFSET, TEST_PARENT_SECOND));
	widescreen_overlay_end();
	widescreen_overlay_clear(WIDESCREEN_OVERLAY_CENTER);
	assert_cursor(legacy);
	mouse_draw_opaque();
	assert_erased(legacy);

	/* A wide cursor has no saved legacy background to restore after F12 or
	 * disabling widescreen changes the presentation between draw and erase. */
	mouse_draw_transparent();
	widescreen_set_enabled(0);
	mouse_draw_opaque();
	assert(mouse_background_dirty == 0);
	assert_erased(legacy);
}

legacy_int main(void)
{
	static const legacy_s32 sizes[][2] = {{1280, 720}, {1280, 800}, {1080, 720}};
	static const legacy_s32 scales[] = {HIRES_SCALE, HIRES_MEDIUM_SCALE, HIRES_MINIMUM_SCALE};
	for (legacy_s32 y = 0; y < VIDEO_FRAMEBUFFER_HEIGHT; y++) {
		LEGACY_WRITE_U16_LE(rows + y * LEGACY_WORD_BYTES, y * VIDEO_FRAMEBUFFER_WIDTH);
	}
	memset(&screen_sprite, 0, sizeof(screen_sprite));
	screen_sprite.sprite_bitmapptr = dos_memory_make_pointer(TEST_VGA_SEGMENT, 0);
	screen_sprite.sprite_lineofs = rows;
	screen_sprite.sprite_right = screen_sprite.sprite_raster_right = VIDEO_FRAMEBUFFER_WIDTH;
	screen_sprite.sprite_pitch = screen_sprite.sprite_buffer_width = VIDEO_FRAMEBUFFER_WIDTH;
	screen_sprite.sprite_bottom = VIDEO_FRAMEBUFFER_HEIGHT;
	prepare_cursor(TEST_AND_SEGMENT, &and_cursor, 1);
	prepare_cursor(TEST_OR_SEGMENT, &or_cursor, 0);
	mouse_medium_sprite = &and_cursor;
	mouse_small_sprite = &or_cursor;
	/* Wide draws must never access a legacy saved-background sprite. */
	mouse_background_sprite = NULL;
	video_x_alignment = 1;
	for (legacy_u32 ratio = 0; ratio < sizeof(sizes) / sizeof(sizes[0]); ratio++) {
		for (legacy_u32 scale = 0; scale < sizeof(scales) / sizeof(scales[0]); scale++) {
			test_cursor(sizes[ratio][0], sizes[ratio][1], scales[scale]);
		}
	}
	hires_shutdown();
	puts("Widescreen cursor alignment, transparent planes, erasure and nesting passed.");
	return 0;
}
