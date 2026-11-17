#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../c/hires/hires.h"
#include "../c/hires/widescreen.h"
#include "../c/hires/render_workers.h"
#include "../c/platform.h"
#include "../c/shape2d/shape2d_internal.h"

#define TEST_GUARD_WORDS 16
#define TEST_PADDING_WORDS 7
#define TEST_GUARD_COLOR 0x5a37c9e1U
#define TEST_BACKGROUND_INDEX 3
#define TEST_BACKGROUND_COLOR 0xff3579bdU
#define TEST_PALETTE_SIZE 256
#define TEST_VGA_SEGMENT 0xA000U
#define TEST_DASHBOARD_INDEX 4
#define TEST_LEFT_HUD_INDEX 5
#define TEST_RIGHT_HUD_INDEX 6
#define TEST_DASHBOARD_COLOR 0xff28a044U
#define TEST_LEFT_HUD_COLOR 0xffd04040U
#define TEST_RIGHT_HUD_COLOR 0xffc0b030U
#define TEST_HUD_MARGIN 2
#define TEST_HUD_SIZE 12
#define TEST_DASHBOARD_TOP 100
#define TEST_UNSEEDED_X 10
#define TEST_UNSEEDED_Y 10
#define TEST_MODAL_HUD_WIDTH 50
#define TEST_MODAL_LEFT_INDEX 7
#define TEST_MODAL_RIGHT_INDEX 8

struct SPRITE drawing_sprite;

void dos_process_exit(legacy_s16 status)
{
	exit(status);
}

void hires_render_workers_shutdown(void)
{
}

legacy_s32 hires_render_workers_count(void)
{
	return 0;
}

legacy_s32 hires_render_workers_run(legacy_s32 count, void (*job)(void *, legacy_s32),
									void *context)
{
	for (legacy_s32 index = 0; index < count; index++) {
		job(context, index);
	}
	return 0;
}

static void test_ratio_selection(void)
{
	static const struct {
		legacy_s32 output_width, output_height;
		legacy_s32 render_width, render_height;
	} cases[] = {
		{1920, 1080, 1280, 720},
		{1920, 1200, 1280, 800},
		{2160, 1440, 1080, 720},
		{2560, 1080, 1280, 720},
		{2400, 1080, 1280, 720},
		/* Fractional CSS canvas heights round up without changing their ratio. */
		{1068, 601, 1280, 720},
		{1068, 668, 1280, 800},
		{1067, 712, 1080, 720},
		/* A single output row is tolerated; two rows choose the next fitting ratio. */
		{1280, 721, 1280, 720},
		{1280, 722, 1280, 800},
		{1280, 801, 1280, 800},
		{1280, 802, 1080, 720},
		{1080, 721, 1080, 720},
		{1080, 722, 0, 0},
		/* Width rounding has the same limited slack, not a nearest-ratio policy. */
		{1279, 720, 1280, 720},
		{1278, 720, 1280, 800},
		{1599, 1000, 1280, 800},
		{1598, 1000, 1080, 720},
		{1499, 1000, 1080, 720},
		{1498, 1000, 0, 0},
		{1280, 960, 0, 0},
		{900, 1600, 0, 0},
		{0, 1080, 0, 0},
		{1920, 0, 0, 0},
	};
	for (legacy_u32 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		legacy_s32 width = -1, height = -1;
		widescreen_dimensions_for_output(cases[index].output_width, cases[index].output_height,
										 &width, &height);
		assert(width == cases[index].render_width);
		assert(height == cases[index].render_height);
	}
}

static void test_activation_and_resolutions(void)
{
	static const struct {
		legacy_s32 width, height;
	} cases[] = {{1280, 720}, {1280, 800}, {1080, 720}};
	static const legacy_s32 scales[] = {HIRES_SCALE, HIRES_MEDIUM_SCALE, HIRES_MINIMUM_SCALE};
	widescreen_set_enabled(0);
	widescreen_set_racing(0);
	hires_set_enabled(0);
	widescreen_set_output_size(cases[0].width, cases[0].height);
	assert(!widescreen_active());
	widescreen_set_enabled(1);
	assert(widescreen_enabled());
	assert(!widescreen_active());
	widescreen_set_racing(1);
	assert(widescreen_racing());
	/* The checkbox and a wide display must not widen the classic renderer. */
	assert(!widescreen_active());
	hires_set_enabled(1);
	assert(widescreen_active());
	for (legacy_u32 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		widescreen_set_output_size(cases[index].width, cases[index].height);
		assert(widescreen_render_width() == cases[index].width);
		assert(widescreen_render_height() == cases[index].height);
		assert(widescreen_logical_width() == cases[index].width / HIRES_SCALE);
		assert(widescreen_logical_height() == cases[index].height / HIRES_SCALE);
		for (legacy_u32 scale = 0; scale < sizeof(scales) / sizeof(scales[0]); scale++) {
			hires_set_render_scale(scales[scale]);
			assert(hires_render_width() == cases[index].width * scales[scale] / HIRES_SCALE);
			assert(hires_render_height() == cases[index].height * scales[scale] / HIRES_SCALE);
		}
	}
	widescreen_set_racing(0);
	assert(!widescreen_active());
	assert(hires_render_width() == VIDEO_FRAMEBUFFER_WIDTH);
	assert(hires_render_height() == VIDEO_FRAMEBUFFER_HEIGHT);
	widescreen_set_racing(1);
	assert(widescreen_active());
	widescreen_set_output_size(1280, 960);
	assert(!widescreen_active());
	widescreen_set_output_size(cases[0].width, cases[0].height);
	assert(widescreen_active());
	widescreen_set_enabled(0);
	assert(!widescreen_active());
	hires_set_render_scale(HIRES_SCALE);
	assert(hires_render_width() == HIRES_WIDTH);
	assert(hires_render_height() == HIRES_HEIGHT);
	hires_shutdown();
}

static void test_composition_guards(void)
{
	static const struct {
		legacy_s32 width, height;
	} cases[] = {{1280, 720}, {1280, 800}, {1080, 720}};
	static const legacy_s32 scales[] = {HIRES_SCALE, HIRES_MEDIUM_SCALE, HIRES_MINIMUM_SCALE};
	legacy_u8 *legacy = dos_memory_make_pointer(TEST_VGA_SEGMENT, 0);
	memset(legacy, TEST_BACKGROUND_INDEX, VIDEO_FRAMEBUFFER_WIDTH * VIDEO_FRAMEBUFFER_HEIGHT);
	legacy_u32 palette[TEST_PALETTE_SIZE] = {0};
	palette[TEST_BACKGROUND_INDEX] = TEST_BACKGROUND_COLOR;
	widescreen_set_enabled(1);
	widescreen_set_racing(1);
	hires_set_enabled(1);
	for (legacy_u32 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		widescreen_set_output_size(cases[index].width, cases[index].height);
		for (legacy_u32 scale = 0; scale < sizeof(scales) / sizeof(scales[0]); scale++) {
			hires_set_render_scale(scales[scale]);
			legacy_s32 width = cases[index].width * scales[scale] / HIRES_SCALE;
			legacy_s32 height = cases[index].height * scales[scale] / HIRES_SCALE;
			legacy_s32 pitch_words = width + TEST_PADDING_WORDS;
			legacy_s32 words = TEST_GUARD_WORDS + height * pitch_words + TEST_GUARD_WORDS;
			legacy_u32 *guarded = malloc((size_t)words * sizeof(*guarded));
			assert(guarded != NULL);
			for (legacy_s32 word = 0; word < words; word++) {
				guarded[word] = TEST_GUARD_COLOR;
			}
			legacy_u32 *output = guarded + TEST_GUARD_WORDS;
			hires_copy_framebuffer_argb(legacy, palette, output, pitch_words * sizeof(*output));
			for (legacy_s32 word = 0; word < TEST_GUARD_WORDS; word++) {
				assert(guarded[word] == TEST_GUARD_COLOR);
				assert(output[height * pitch_words + word] == TEST_GUARD_COLOR);
			}
			for (legacy_s32 y = 0; y < height; y++) {
				for (legacy_s32 x = 0; x < width; x++) {
					assert(output[y * pitch_words + x] == TEST_BACKGROUND_COLOR);
				}
				for (legacy_s32 x = width; x < pitch_words; x++) {
					assert(output[y * pitch_words + x] == TEST_GUARD_COLOR);
				}
			}
			legacy_s32 indexed_width = -1, indexed_height = -1;
			const legacy_u8 *indexed = hires_framebuffer(legacy, &indexed_width, &indexed_height);
			assert(indexed != NULL && indexed_width == width && indexed_height == height);
			for (legacy_s32 pixel = 0; pixel < width * height; pixel++) {
				assert(indexed[pixel] == TEST_BACKGROUND_INDEX);
			}
			free(guarded);
		}
	}
	widescreen_set_racing(0);
	widescreen_set_enabled(0);
	hires_shutdown();
}

static void test_setup_overlay_target(legacy_u8 *legacy, legacy_u8 *rows)
{
	memset(&drawing_sprite, 0, sizeof(drawing_sprite));
	drawing_sprite.sprite_bitmapptr = (struct SHAPE2D *)legacy;
	drawing_sprite.sprite_lineofs = rows;
	drawing_sprite.sprite_pitch = VIDEO_FRAMEBUFFER_WIDTH;
	drawing_sprite.sprite_buffer_width = VIDEO_FRAMEBUFFER_WIDTH;
	drawing_sprite.sprite_right = VIDEO_FRAMEBUFFER_WIDTH;
	drawing_sprite.sprite_raster_right = VIDEO_FRAMEBUFFER_WIDTH;
	drawing_sprite.sprite_bottom = VIDEO_FRAMEBUFFER_HEIGHT;
	for (legacy_s32 y = 0; y < VIDEO_FRAMEBUFFER_HEIGHT; y++) {
		LEGACY_WRITE_U16_LE(rows + y * LEGACY_WORD_BYTES, y * VIDEO_FRAMEBUFFER_WIDTH);
	}
}

static void test_draw_overlay_rectangle(legacy_u8 *legacy, enum WIDESCREEN_OVERLAY overlay,
										legacy_s32 left, legacy_s32 top, legacy_s32 right,
										legacy_s32 bottom, legacy_u8 color)
{
	widescreen_overlay_begin(overlay);
	for (legacy_s32 y = top; y < bottom; y++) {
		for (legacy_s32 x = left; x < right; x++) {
			legacy_u16 offset = y * VIDEO_FRAMEBUFFER_WIDTH + x;
			/* Match production 2D primitives: notify companion storage first,
			 * then write the original indexed destination byte. */
			hires_write(legacy, offset, color);
			legacy[offset] = color;
		}
	}
	widescreen_overlay_end();
}

static void test_overlay_geometry_and_retirement(void)
{
	static const struct {
		legacy_s32 width, height;
	} cases[] = {{1280, 720}, {1280, 800}, {1080, 720}};
	static const legacy_s32 scales[] = {HIRES_SCALE, HIRES_MEDIUM_SCALE, HIRES_MINIMUM_SCALE};
	legacy_u8 *legacy = dos_memory_make_pointer(TEST_VGA_SEGMENT, 0);
	legacy_u8 rows[VIDEO_FRAMEBUFFER_HEIGHT * LEGACY_WORD_BYTES];
	legacy_u32 palette[TEST_PALETTE_SIZE] = {0};
	palette[TEST_BACKGROUND_INDEX] = TEST_BACKGROUND_COLOR;
	palette[TEST_DASHBOARD_INDEX] = TEST_DASHBOARD_COLOR;
	palette[TEST_LEFT_HUD_INDEX] = TEST_LEFT_HUD_COLOR;
	palette[TEST_RIGHT_HUD_INDEX] = TEST_RIGHT_HUD_COLOR;
	widescreen_set_enabled(1);
	widescreen_set_racing(1);
	hires_set_enabled(1);
	for (legacy_u32 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		widescreen_set_output_size(cases[index].width, cases[index].height);
		for (legacy_u32 scale = 0; scale < sizeof(scales) / sizeof(scales[0]); scale++) {
			hires_set_render_scale(scales[scale]);
			legacy_s32 width = hires_render_width(), height = hires_render_height();
			hires_forget(legacy);
			memset(legacy, TEST_BACKGROUND_INDEX,
				   VIDEO_FRAMEBUFFER_WIDTH * VIDEO_FRAMEBUFFER_HEIGHT);
			test_setup_overlay_target(legacy, rows);
			assert(hires_begin(&drawing_sprite));
			hires_end();
			widescreen_begin_frame();
			test_draw_overlay_rectangle(legacy, WIDESCREEN_OVERLAY_CENTER, 0, TEST_DASHBOARD_TOP,
										VIDEO_FRAMEBUFFER_WIDTH, VIDEO_FRAMEBUFFER_HEIGHT,
										TEST_DASHBOARD_INDEX);
			test_draw_overlay_rectangle(legacy, WIDESCREEN_OVERLAY_LEFT, TEST_HUD_MARGIN,
										TEST_HUD_MARGIN, TEST_HUD_MARGIN + TEST_HUD_SIZE,
										TEST_HUD_MARGIN + TEST_HUD_SIZE, TEST_LEFT_HUD_INDEX);
			test_draw_overlay_rectangle(legacy, WIDESCREEN_OVERLAY_RIGHT,
										VIDEO_FRAMEBUFFER_WIDTH - TEST_HUD_MARGIN - TEST_HUD_SIZE,
										VIDEO_FRAMEBUFFER_HEIGHT - TEST_HUD_MARGIN - TEST_HUD_SIZE,
										VIDEO_FRAMEBUFFER_WIDTH - TEST_HUD_MARGIN,
										VIDEO_FRAMEBUFFER_HEIGHT - TEST_HUD_MARGIN,
										TEST_RIGHT_HUD_INDEX);
			legacy_u32 *output = malloc((size_t)width * height * sizeof(*output));
			assert(output != NULL);
			hires_copy_framebuffer_argb(legacy, palette, output, width * sizeof(*output));
			legacy_s32 cockpit_width = height * WIDESCREEN_ASPECT_WIDTH / WIDESCREEN_ASPECT_HEIGHT;
			legacy_s32 cockpit_left = (width - cockpit_width) / 2;
			legacy_s32 cockpit_right = cockpit_left + cockpit_width;
			legacy_s32 lower_row = height * 3 / 4;
			/* Dashboard artwork keeps a centered 4:3 area; both side gaps must
			 * expose the scene instead of stretching the authored bitmap. */
			assert(output[lower_row * width + cockpit_left] == TEST_DASHBOARD_COLOR);
			assert(output[lower_row * width + cockpit_right - 1] == TEST_DASHBOARD_COLOR);
			assert(output[lower_row * width + cockpit_left - 1] == TEST_BACKGROUND_COLOR);
			assert(output[lower_row * width + cockpit_right] == TEST_BACKGROUND_COLOR);
			legacy_s32 left_x = (TEST_HUD_MARGIN * cockpit_width + VIDEO_FRAMEBUFFER_WIDTH - 1) /
								VIDEO_FRAMEBUFFER_WIDTH;
			legacy_s32 top_y = (TEST_HUD_MARGIN * height + VIDEO_FRAMEBUFFER_HEIGHT - 1) /
							   VIDEO_FRAMEBUFFER_HEIGHT;
			legacy_s32 right_edge =
				width - TEST_HUD_MARGIN * cockpit_width / VIDEO_FRAMEBUFFER_WIDTH;
			legacy_s32 bottom_y = height - TEST_HUD_MARGIN * height / VIDEO_FRAMEBUFFER_HEIGHT - 1;
			/* Corner text keeps the original scaled edge margins and appears
			 * outside the cockpit area on both sides. */
			assert(left_x < cockpit_left);
			assert(right_edge > cockpit_right);
			assert(output[top_y * width + left_x] == TEST_LEFT_HUD_COLOR);
			assert(output[top_y * width + left_x - 1] == TEST_BACKGROUND_COLOR);
			assert(output[bottom_y * width + right_edge - 1] == TEST_RIGHT_HUD_COLOR);
			assert(output[bottom_y * width + right_edge] == TEST_BACKGROUND_COLOR);
			widescreen_begin_frame();
			hires_copy_framebuffer_argb(legacy, palette, output, width * sizeof(*output));
			for (legacy_s32 pixel = 0; pixel < width * height; pixel++) {
				assert(output[pixel] == TEST_BACKGROUND_COLOR);
			}
			free(output);
		}
	}
	widescreen_set_racing(0);
	widescreen_set_enabled(0);
	hires_shutdown();
}

static void test_unseeded_overlay_preserves_world(void)
{
	legacy_u8 *legacy = dos_memory_make_pointer(TEST_VGA_SEGMENT, 0);
	legacy_u8 rows[VIDEO_FRAMEBUFFER_HEIGHT * LEGACY_WORD_BYTES];
	legacy_u32 palette[TEST_PALETTE_SIZE] = {0};
	palette[TEST_BACKGROUND_INDEX] = TEST_BACKGROUND_COLOR;
	palette[TEST_LEFT_HUD_INDEX] = TEST_LEFT_HUD_COLOR;
	widescreen_set_enabled(1);
	widescreen_set_racing(1);
	widescreen_set_output_size(1280, 720);
	hires_set_enabled(1);
	hires_render_width();
	hires_forget(legacy);
	memset(legacy, TEST_BACKGROUND_INDEX, VIDEO_FRAMEBUFFER_WIDTH * VIDEO_FRAMEBUFFER_HEIGHT);
	test_setup_overlay_target(legacy, rows);
	widescreen_begin_frame();
	test_draw_overlay_rectangle(legacy, WIDESCREEN_OVERLAY_LEFT, TEST_UNSEEDED_X, TEST_UNSEEDED_Y,
								TEST_UNSEEDED_X + 1, TEST_UNSEEDED_Y + 1, TEST_LEFT_HUD_INDEX);
	widescreen_begin_frame();
	legacy_s32 width = hires_render_width(), height = hires_render_height();
	legacy_u32 *output = malloc((size_t)width * height * sizeof(*output));
	assert(output != NULL);
	hires_copy_framebuffer_argb(legacy, palette, output, width * sizeof(*output));
	assert(output[TEST_UNSEEDED_Y * HIRES_SCALE * width + TEST_UNSEEDED_X * HIRES_SCALE] ==
		   TEST_BACKGROUND_COLOR);
	free(output);
	widescreen_set_racing(0);
	widescreen_set_enabled(0);
	hires_shutdown();
}

static legacy_u8 test_modal_hud_pixel(legacy_s32 x, legacy_s32 y, legacy_u8 background)
{
	if (y >= TEST_HUD_MARGIN && y < TEST_HUD_MARGIN + TEST_HUD_SIZE) {
		if (x >= TEST_HUD_MARGIN && x < TEST_HUD_MARGIN + TEST_MODAL_HUD_WIDTH) {
			return TEST_MODAL_LEFT_INDEX;
		}
		if (x >= VIDEO_FRAMEBUFFER_WIDTH - TEST_HUD_MARGIN - TEST_MODAL_HUD_WIDTH &&
			x < VIDEO_FRAMEBUFFER_WIDTH - TEST_HUD_MARGIN) {
			return TEST_MODAL_RIGHT_INDEX;
		}
	}
	return background;
}

static void test_nested_modal_background_and_restoration(void)
{
	legacy_u8 *legacy = dos_memory_make_pointer(TEST_VGA_SEGMENT, 0);
	legacy_u8 rows[VIDEO_FRAMEBUFFER_HEIGHT * LEGACY_WORD_BYTES];
	widescreen_set_enabled(1);
	widescreen_set_racing(1);
	widescreen_set_output_size(1080, 720);
	hires_set_enabled(1);
	hires_set_render_scale(HIRES_MINIMUM_SCALE);
	assert(hires_render_width() == 270 && hires_render_height() == 180);
	hires_forget(legacy);
	memset(legacy, TEST_BACKGROUND_INDEX, VIDEO_FRAMEBUFFER_WIDTH * VIDEO_FRAMEBUFFER_HEIGHT);
	test_setup_overlay_target(legacy, rows);
	widescreen_begin_frame();
	assert(hires_begin(&drawing_sprite));
	legacy_s32 width = widescreen_logical_width(), height = widescreen_logical_height();
	legacy_s32 crop_width = height * WIDESCREEN_ASPECT_WIDTH / WIDESCREEN_ASPECT_HEIGHT;
	legacy_s32 crop_left = (width - crop_width) / 2;
	for (legacy_s32 y = 0; y < height; y++) {
		for (legacy_s32 x = 0; x < width; x++) {
			hires_fill_pixel(x, y,
							 x < crop_left || x >= crop_left + crop_width ? TEST_RIGHT_HUD_INDEX
																		  : TEST_LEFT_HUD_INDEX);
		}
	}
	hires_end();
	test_draw_overlay_rectangle(legacy, WIDESCREEN_OVERLAY_LEFT, TEST_HUD_MARGIN, TEST_HUD_MARGIN,
								TEST_HUD_MARGIN + TEST_MODAL_HUD_WIDTH,
								TEST_HUD_MARGIN + TEST_HUD_SIZE, TEST_MODAL_LEFT_INDEX);
	test_draw_overlay_rectangle(legacy, WIDESCREEN_OVERLAY_RIGHT,
								VIDEO_FRAMEBUFFER_WIDTH - TEST_HUD_MARGIN - TEST_MODAL_HUD_WIDTH,
								TEST_HUD_MARGIN, VIDEO_FRAMEBUFFER_WIDTH - TEST_HUD_MARGIN,
								TEST_HUD_MARGIN + TEST_HUD_SIZE, TEST_MODAL_RIGHT_INDEX);
	legacy_u32 generation = widescreen_generation();
	widescreen_push_modal();
	assert(widescreen_modal_active() && !widescreen_active());
	assert(hires_render_width() == VIDEO_FRAMEBUFFER_WIDTH);
	assert(hires_render_height() == VIDEO_FRAMEBUFFER_HEIGHT);
	assert(widescreen_generation() != generation);
	/* Capture must use the central visible companion, not the VGA fallback
	 * or the wide scene's outer strips. */
	for (legacy_s32 pixel = 0; pixel < VIDEO_FRAMEBUFFER_WIDTH * VIDEO_FRAMEBUFFER_HEIGHT;
		 pixel++) {
		/* Complete edge HUDs keep their authored coordinates. A clipped copy
		 * must not remain in the outer two pixels of the centered crop. */
		assert(legacy[pixel] == test_modal_hud_pixel(pixel % VIDEO_FRAMEBUFFER_WIDTH,
													 pixel / VIDEO_FRAMEBUFFER_WIDTH,
													 TEST_LEFT_HUD_INDEX));
	}
	generation = widescreen_generation();
	legacy[TEST_UNSEEDED_X] = TEST_DASHBOARD_INDEX;
	widescreen_push_modal();
	assert(widescreen_modal_active() && !widescreen_active());
	assert(widescreen_generation() == generation);
	assert(legacy[TEST_UNSEEDED_X] == TEST_DASHBOARD_INDEX);
	widescreen_pop_modal();
	assert(widescreen_modal_active() && !widescreen_active());
	assert(widescreen_generation() == generation);
	assert(legacy[TEST_UNSEEDED_X] == TEST_DASHBOARD_INDEX);
	widescreen_pop_modal();
	assert(!widescreen_modal_active() && widescreen_active());
	assert(widescreen_generation() != generation);
	assert(hires_render_width() == 270 && hires_render_height() == 180);
	for (legacy_s32 pixel = 0; pixel < VIDEO_FRAMEBUFFER_WIDTH * VIDEO_FRAMEBUFFER_HEIGHT;
		 pixel++) {
		assert(legacy[pixel] == test_modal_hud_pixel(pixel % VIDEO_FRAMEBUFFER_WIDTH,
													 pixel / VIDEO_FRAMEBUFFER_WIDTH,
													 TEST_BACKGROUND_INDEX));
	}
	generation = widescreen_generation();
	widescreen_pop_modal();
	assert(widescreen_generation() == generation && !widescreen_modal_active());
	widescreen_set_enabled(0);
	/* Default dialogs retain their existing background ownership. */
	widescreen_push_modal();
	legacy[TEST_UNSEEDED_X] = TEST_DASHBOARD_INDEX;
	widescreen_pop_modal();
	assert(legacy[TEST_UNSEEDED_X] == TEST_DASHBOARD_INDEX);
	widescreen_set_racing(0);
	hires_shutdown();
}

legacy_int main(void)
{
	test_ratio_selection();
	test_activation_and_resolutions();
	test_composition_guards();
	test_overlay_geometry_and_retirement();
	test_unseeded_overlay_preserves_world();
	test_nested_modal_background_and_restoration();
	puts("Widescreen tests passed");
	return 0;
}
