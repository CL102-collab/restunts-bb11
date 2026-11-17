/* Exercise DOS mode choice and replay pointer geometry on the host SDL build. */
#define __DJGPP__ 1
#include "../platform/sdl3/video.c"
#include <assert.h>
#include <math.h>

#define TEST_FLOAT_TOLERANCE 0.001f
#define TEST_RENDER_WIDTH 1280
#define TEST_RENDER_HEIGHT 720
#define TEST_CENTER_X 640
#define TEST_CENTER_Y 360
#define TEST_ARTWORK_LEFT 160
#define TEST_ARTWORK_RIGHT 1120
#define TEST_LEGACY_CENTER_X 160
#define TEST_LEGACY_CENTER_Y 100

static legacy_s32 test_hires_enabled = 1;
static legacy_u8 test_vga[SCREEN_BYTES];

legacy_s32 hires_enabled(void)
{
	return test_hires_enabled;
}
/* This mode-selection fixture has no composed scene to snapshot for a dialog. */
const legacy_u8 *hires_framebuffer(const legacy_u8 *legacy, legacy_s32 *width, legacy_s32 *height)
{
	(void)legacy;
	(void)width;
	(void)height;
	return NULL;
}
void hires_forget(const void *base)
{
	(void)base;
}
void *dos_memory_make_pointer(legacy_u16 segment, legacy_u16 offset)
{
	(void)segment;
	(void)offset;
	return test_vga;
}
legacy_u8 sdl3_soft_keyboard_active(void)
{
	return 0;
}
SDL_FRect sdl3_soft_keyboard_game_area(SDL_Window *target)
{
	(void)target;
	return (SDL_FRect){0};
}

static SDL_DisplayMode test_mode(legacy_s32 width, legacy_s32 height, SDL_PixelFormat format)
{
	SDL_DisplayMode mode = {0};
	mode.w = width;
	mode.h = height;
	mode.format = format;
	return mode;
}

static void test_modes(void)
{
	SDL_DisplayMode vga = test_mode(320, 200, SDL_PIXELFORMAT_INDEX8);
	SDL_DisplayMode classic = test_mode(1280, 1024, SDL_PIXELFORMAT_INDEX8);
	SDL_DisplayMode wide = test_mode(1280, 720, SDL_PIXELFORMAT_INDEX8);
	SDL_DisplayMode medium = test_mode(1280, 800, SDL_PIXELFORMAT_INDEX8);
	SDL_DisplayMode three_two = test_mode(1080, 720, SDL_PIXELFORMAT_INDEX8);
	SDL_DisplayMode fallback = test_mode(1024, 600, SDL_PIXELFORMAT_INDEX8);
	SDL_DisplayMode smaller = test_mode(800, 600, SDL_PIXELFORMAT_INDEX8);
	SDL_DisplayMode truecolor = test_mode(1280, 720, SDL_PIXELFORMAT_ARGB8888);
	legacy_s32 width, height;

	widescreen_set_racing(1);
	widescreen_set_enabled(0);
	mode_presentation_size(&wide, &width, &height);
	assert(width == SDL3_SCREEN_WIDTH && height == PRESENTATION_HEIGHT);
	assert(mode_viewport_width(&wide) < mode_viewport_width(&classic));
	assert(mode_is_better(&classic, &wide));

	widescreen_set_enabled(1);
	assert(mode_is_better(&wide, &classic));
	assert(mode_is_better(&medium, &classic));
	assert(mode_is_better(&wide, &truecolor));
	assert(!mode_is_better(&truecolor, &wide));
	assert(mode_is_better(&three_two, &fallback));
	assert(mode_is_better(&fallback, &smaller));
	mode_presentation_size(&vga, &width, &height);
	assert(width == SDL3_SCREEN_WIDTH && height == PRESENTATION_HEIGHT);

	widescreen_set_racing(0);
	mode_presentation_size(&wide, &width, &height);
	assert(width == SDL3_SCREEN_WIDTH && height == PRESENTATION_HEIGHT);
	widescreen_set_racing(1);
	test_hires_enabled = 0;
	mode_presentation_size(&wide, &width, &height);
	assert(width == SDL3_SCREEN_WIDTH && height == PRESENTATION_HEIGHT);
	test_hires_enabled = 1;

	/* Nested race dialogs select classic presentation, then resume the wide scene. */
	widescreen_set_output_size(TEST_RENDER_WIDTH, TEST_RENDER_HEIGHT);
	assert(widescreen_active());
	widescreen_push_modal();
	assert(!dos_widescreen_requested());
	mode_presentation_size(&wide, &width, &height);
	assert(width == SDL3_SCREEN_WIDTH && height == PRESENTATION_HEIGHT);
	widescreen_push_modal();
	widescreen_pop_modal();
	mode_presentation_size(&wide, &width, &height);
	assert(width == SDL3_SCREEN_WIDTH && height == PRESENTATION_HEIGHT);
	widescreen_pop_modal();
	assert(dos_widescreen_requested());
	assert(widescreen_active());
	mode_presentation_size(&wide, &width, &height);
	assert(width == TEST_RENDER_WIDTH && height == TEST_RENDER_HEIGHT);
}

static void assert_pointer(legacy_f32 window_x, legacy_f32 window_y, legacy_f32 expected_x,
						   legacy_f32 expected_y)
{
	legacy_f32 x, y, roundtrip_x, roundtrip_y;
	sdl3_video_window_to_game(window_x, window_y, &x, &y);
	assert(fabsf(x - expected_x) < TEST_FLOAT_TOLERANCE);
	assert(fabsf(y - expected_y) < TEST_FLOAT_TOLERANCE);
	sdl3_video_game_to_window(x, y, &roundtrip_x, &roundtrip_y);
	assert(fabsf(roundtrip_x - window_x) < TEST_FLOAT_TOLERANCE);
	assert(fabsf(roundtrip_y - window_y) < TEST_FLOAT_TOLERANCE);
}

static void test_replay_pointer_mapping(void)
{
	renderer = NULL;
	surface_output = true;
	presentation_width = TEST_RENDER_WIDTH;
	presentation_height = TEST_RENDER_HEIGHT;
	presentation_raster_width = TEST_RENDER_WIDTH;
	presentation_raster_height = TEST_RENDER_HEIGHT;
	surface_viewport = (SDL_Rect){0, 0, TEST_RENDER_WIDTH, TEST_RENDER_HEIGHT};
	assert_pointer(TEST_ARTWORK_LEFT, 0, 0, 0);
	assert_pointer(TEST_ARTWORK_RIGHT, TEST_RENDER_HEIGHT, SDL3_SCREEN_WIDTH, SDL3_SCREEN_HEIGHT);
	assert_pointer(TEST_CENTER_X, TEST_CENTER_Y, TEST_LEGACY_CENTER_X, TEST_LEGACY_CENTER_Y);
	legacy_f32 x, y;
	sdl3_video_window_to_game(0, TEST_CENTER_Y, &x, &y);
	assert(x < 0);
	sdl3_video_window_to_game(TEST_RENDER_WIDTH, TEST_CENTER_Y, &x, &y);
	assert(x > SDL3_SCREEN_WIDTH);

	/* A 3:2 viewport has 60-pixel side extensions around the same 960-pixel art. */
	presentation_width = 1080;
	presentation_raster_width = 1080;
	surface_viewport = (SDL_Rect){50, 20, 1080, TEST_RENDER_HEIGHT};
	assert_pointer(110, 20, 0, 0);
	assert_pointer(1070, 740, SDL3_SCREEN_WIDTH, SDL3_SCREEN_HEIGHT);
	assert_pointer(590, 380, TEST_LEGACY_CENTER_X, TEST_LEGACY_CENTER_Y);

	/* 16:10 artwork rounds its 4:3 width at the selected quality level. */
	presentation_width = TEST_RENDER_WIDTH;
	presentation_height = 800;
	presentation_raster_width = 320;
	presentation_raster_height = 200;
	surface_viewport = (SDL_Rect){0, 0, TEST_RENDER_WIDTH, 800};
	assert_pointer(108, 0, 0, 0);
	assert_pointer(1172, 800, SDL3_SCREEN_WIDTH, SDL3_SCREEN_HEIGHT);
	assert_pointer(640, 400, TEST_LEGACY_CENTER_X, TEST_LEGACY_CENTER_Y);

	/* Menus stay 4:3, and the letterbox offset participates in hit testing. */
	presentation_width = SDL3_SCREEN_WIDTH;
	presentation_height = PRESENTATION_HEIGHT;
	surface_viewport = (SDL_Rect){TEST_ARTWORK_LEFT, 0, 960, TEST_RENDER_HEIGHT};
	assert_pointer(TEST_ARTWORK_LEFT, 0, 0, 0);
	assert_pointer(TEST_ARTWORK_RIGHT, TEST_RENDER_HEIGHT, SDL3_SCREEN_WIDTH, SDL3_SCREEN_HEIGHT);
}

legacy_int main(void)
{
	test_modes();
	test_replay_pointer_mapping();
	puts("SDL3 widescreen video regression tests passed");
	return 0;
}
