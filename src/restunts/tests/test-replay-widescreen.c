#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../c/replay/replay_toolbar.c"
#include "../c/hires/hires.h"
#include "../c/hires/widescreen.h"
#include "../c/hires/render_workers.h"
#include "../c/platform.h"
#include "../c/shape2d/shape2d_internal.h"

#define TEST_BACKGROUND_COLOR 3
#define TEST_COCKPIT_COLOR 4
#define TEST_TOOLBAR_COLOR 5
#define TEST_INACTIVE_COLOR 6
#define TEST_ACTIVE_COLOR 7
#define TEST_MARKER_COLOR 15
#define TEST_TEXT_COLOR_FIRST 16
#define TEST_TEXT_COLOR_COUNT 10
#define TEST_VGA_SEGMENT 0xA000U
#define TEST_RECORDED_FRAMES 100U
#define TEST_CURRENT_FRAME 20U
#define TEST_NEXT_FRAME 60U
#define TEST_TOOLBAR_TOP 151
#define TEST_INTERIOR_MARGIN 2

static legacy_u8 *screen;
static struct SHAPE2D shapes[23];

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

static void draw_pixel(legacy_s16 x, legacy_s16 y, legacy_u8 color)
{
	assert(x >= 0 && x < VIDEO_FRAMEBUFFER_WIDTH && y >= 0 && y < VIDEO_FRAMEBUFFER_HEIGHT);
	legacy_u16 offset = (legacy_u16)(y * VIDEO_FRAMEBUFFER_WIDTH + x);
	if (!hires_write(screen, offset, color)) {
		screen[offset] = color;
	}
}

void sprite_fill_rect(legacy_s16 x, legacy_s16 y, legacy_s16 width, legacy_s16 height,
					  legacy_s16 color)
{
	for (legacy_s16 row = 0; row < height; row++) {
		for (legacy_s16 column = 0; column < width; column++) {
			draw_pixel(x + column, y + row, (legacy_u8)color);
		}
	}
}

void sprite_draw_rect_outline(legacy_s16 left, legacy_s16 top, legacy_s16 right, legacy_s16 bottom,
							  legacy_s16 color)
{
	for (legacy_s16 x = left; x <= right; x++) {
		draw_pixel(x, top, (legacy_u8)color);
		draw_pixel(x, bottom, (legacy_u8)color);
	}
	for (legacy_s16 y = top; y <= bottom; y++) {
		draw_pixel(left, y, (legacy_u8)color);
		draw_pixel(right, y, (legacy_u8)color);
	}
}

void shape2d_rle_copy_at_position(struct SHAPE2D *shape)
{
	legacy_u32 index = (legacy_u32)(shape - shapes);
	if (index == REPLAY_SHAPE_BACKGROUND) {
		sprite_fill_rect(0, TEST_TOOLBAR_TOP, VIDEO_FRAMEBUFFER_WIDTH,
						 VIDEO_FRAMEBUFFER_HEIGHT - TEST_TOOLBAR_TOP, TEST_TOOLBAR_COLOR);
	} else if (index >= REPLAY_SHAPE_CONTROL_INACTIVE_FIRST) {
		legacy_u32 control = index >= REPLAY_SHAPE_CONTROL_ACTIVE_FIRST
								 ? index - REPLAY_SHAPE_CONTROL_ACTIVE_FIRST
								 : index - REPLAY_SHAPE_CONTROL_INACTIVE_FIRST;
		const struct BUTTON_AREA *button = &game_camera_buttons[control];
		sprite_fill_rect(
			button->x1, button->y1, button->x2 - button->x1 + 1, button->y2 - button->y1 + 1,
			index >= REPLAY_SHAPE_CONTROL_ACTIVE_FIRST ? TEST_ACTIVE_COLOR : TEST_INACTIVE_COLOR);
	}
}

void mouse_draw_opaque_check(void)
{
}
void mouse_draw_transparent_check(void)
{
}
void font_set_colors(legacy_s16 foreground, legacy_s16 background)
{
	(void)foreground;
	(void)background;
}
void font_set_fontdef2(void *data)
{
	(void)data;
}
void font_set_fontdef(void)
{
}
void font_draw_text_opaque(const legacy_s8 *text, legacy_s16 x, legacy_s16 y)
{
	draw_pixel(x, y, TEST_TEXT_COLOR_FIRST + text[0] - '0');
}
void format_frame_as_string(legacy_s8 *text, legacy_s16 frame, legacy_s16 fraction)
{
	(void)fraction;
	text[0] = '0' + frame % TEST_TEXT_COLOR_COUNT;
	text[1] = 0;
}
legacy_u32 extended_replay_recorded_frames(void)
{
	return gameconfig.game_recordedframes;
}
legacy_s16 extended_replay_ui_format_time(legacy_s8 *text, legacy_u32 frame, legacy_s16 fraction)
{
	(void)text;
	(void)frame;
	(void)fraction;
	return 0;
}
legacy_u32 extended_replay_ui_displayed_time(legacy_u32 current, legacy_u32 fallback)
{
	(void)current;
	return fallback;
}
legacy_s16 extended_replay_ui_redraw_time(void)
{
	return 0;
}
legacy_s16 extended_replay_ui_timeline(legacy_u32 recorded, legacy_u32 current, legacy_u16 range,
									   legacy_s16 *recorded_position, legacy_s16 *current_position)
{
	(void)recorded;
	(void)current;
	(void)range;
	(void)recorded_position;
	(void)current_position;
	return 0;
}

static legacy_u8 artwork_pixel(legacy_s16 x, legacy_s16 y)
{
	legacy_s32 width, height;
	const legacy_u8 *pixels = hires_framebuffer(screen, &width, &height);
	legacy_s32 artwork_width = height * WIDESCREEN_ASPECT_WIDTH / WIDESCREEN_ASPECT_HEIGHT;
	legacy_s32 left = (width - artwork_width) / 2;
	/* Sample inside the displayed authored pixel, including non-integer scaling. */
	legacy_s32 sample_x =
		left + (x * artwork_width + VIDEO_FRAMEBUFFER_WIDTH - 1) / VIDEO_FRAMEBUFFER_WIDTH;
	legacy_s32 sample_y = (y * height + VIDEO_FRAMEBUFFER_HEIGHT - 1) / VIDEO_FRAMEBUFFER_HEIGHT;
	assert(pixels != NULL);
	return pixels[sample_y * width + sample_x];
}

static void test_paused_toolbar_redraw(legacy_s32 width, legacy_s32 height, legacy_s32 scale)
{
	legacy_u8 rows[VIDEO_FRAMEBUFFER_HEIGHT * LEGACY_WORD_BYTES];
	screen = dos_memory_make_pointer(TEST_VGA_SEGMENT, 0);
	memset(screen, TEST_BACKGROUND_COLOR, VIDEO_FRAMEBUFFER_WIDTH * VIDEO_FRAMEBUFFER_HEIGHT);
	memset(&drawing_sprite, 0, sizeof(drawing_sprite));
	drawing_sprite.sprite_bitmapptr = (struct SHAPE2D *)screen;
	drawing_sprite.sprite_lineofs = rows;
	for (legacy_s32 row = 0; row < VIDEO_FRAMEBUFFER_HEIGHT; row++) {
		LEGACY_WRITE_U16_LE(rows + row * LEGACY_WORD_BYTES, row * VIDEO_FRAMEBUFFER_WIDTH);
	}
	widescreen_set_enabled(1);
	widescreen_set_racing(1);
	widescreen_set_output_size(width, height);
	hires_set_enabled(1);
	hires_set_render_scale(scale);
	widescreen_begin_frame();
	widescreen_overlay_begin(WIDESCREEN_OVERLAY_CENTER);
	sprite_fill_rect(0, TEST_TOOLBAR_TOP, VIDEO_FRAMEBUFFER_WIDTH,
					 VIDEO_FRAMEBUFFER_HEIGHT - TEST_TOOLBAR_TOP, TEST_COCKPIT_COLOR);
	widescreen_overlay_end();
	for (legacy_u32 index = 0; index < sizeof(shapes) / sizeof(shapes[0]); index++) {
		rplyshapes[index] = &shapes[index];
	}
	memset(replay_controls_drawn, 0, 2 * sizeof(replay_controls_drawn[0]));
	memset(replay_control_active_cache, 0, sizeof(replay_control_active_cache));
	dashboard_buffer_index = 0;
	cameramode = CAMERA_MODE_CUSTOM;
	dialog_fnt_colour = TEST_INACTIVE_COLOR;
	replay_marker_color = TEST_MARKER_COLOR;
	replay_timeline_background_color = TEST_TOOLBAR_COLOR;
	gameconfig.game_recordedframes = TEST_RECORDED_FRAMES;
	elapsed_time1 = 0;
	game_replay_mode = REPLAY_MODE_PLAYBACK;
	is_in_replay = 1;
	replay_selected_control = REPLAY_CONTROL_PAUSE;
	replay_controls_select(REPLAY_CONTROL_PAUSE);
	replaybar_enabled = 0;
	replay_controls_draw(TEST_RECORDED_FRAMES, TEST_CURRENT_FRAME);
	assert(replay_controls_drawn[0] == 0);
	assert(artwork_pixel(game_camera_buttons[REPLAY_CONTROL_PAUSE].x1,
						 game_camera_buttons[REPLAY_CONTROL_PAUSE].y1) == TEST_COCKPIT_COLOR);

	/* The race transition prepares the new viewport before entering the paused loop. */
	replaybar_enabled = 1;
	widescreen_begin_frame();
	replay_controls_draw(TEST_RECORDED_FRAMES, TEST_CURRENT_FRAME);
	assert(artwork_pixel(game_camera_buttons[REPLAY_CONTROL_PAUSE].x1,
						 game_camera_buttons[REPLAY_CONTROL_PAUSE].y1) == TEST_MARKER_COLOR);
	legacy_u32 generation = hires_generation();
	/* Hover selection, time and drag changes redraw without another race frame. */
	replay_selected_control = REPLAY_CONTROL_PLAY;
	replay_controls_draw(TEST_RECORDED_FRAMES, TEST_NEXT_FRAME + 1);
	assert(hires_generation() != generation);
	assert(artwork_pixel(game_camera_buttons[REPLAY_CONTROL_PLAY].x1,
						 game_camera_buttons[REPLAY_CONTROL_PLAY].y1) == TEST_MARKER_COLOR);
	assert(artwork_pixel(game_camera_buttons[REPLAY_CONTROL_PAUSE].x1,
						 game_camera_buttons[REPLAY_CONTROL_PAUSE].y1) == TEST_ACTIVE_COLOR);
	assert(artwork_pixel(REPLAY_CURRENT_TIME_X, REPLAY_TIME_Y) == TEST_TEXT_COLOR_FIRST + 1);
	legacy_s16 cursor = (legacy_s16)replay_timeline_position(
		TEST_NEXT_FRAME + 1, TEST_RECORDED_FRAMES, REPLAY_TIMELINE_POSITION_RANGE);
	assert(artwork_pixel(REPLAY_TIMELINE_X + cursor, REPLAY_TIMELINE_Y) == TEST_MARKER_COLOR);
	/* Clicking Play then Pause refreshes the independent button face immediately. */
	replay_controls_select(REPLAY_CONTROL_PLAY);
	replay_controls_draw(TEST_RECORDED_FRAMES, TEST_NEXT_FRAME + 1);
	assert(artwork_pixel(game_camera_buttons[REPLAY_CONTROL_PLAY].x1 + TEST_INTERIOR_MARGIN,
						 game_camera_buttons[REPLAY_CONTROL_PLAY].y1 + TEST_INTERIOR_MARGIN) ==
		   TEST_ACTIVE_COLOR);
	replay_selected_control = REPLAY_CONTROL_PAUSE;
	replay_controls_select(REPLAY_CONTROL_PAUSE);
	replay_controls_draw(TEST_RECORDED_FRAMES, TEST_NEXT_FRAME + 1);
	assert(artwork_pixel(game_camera_buttons[REPLAY_CONTROL_PLAY].x1 + TEST_INTERIOR_MARGIN,
						 game_camera_buttons[REPLAY_CONTROL_PLAY].y1 + TEST_INTERIOR_MARGIN) ==
		   TEST_INACTIVE_COLOR);
	/* Removing the toolbar must expose only the preserved world samples. */
	widescreen_overlay_clear(WIDESCREEN_OVERLAY_CENTER);
	legacy_s32 output_width, output_height;
	const legacy_u8 *image = hires_framebuffer(screen, &output_width, &output_height);
	for (legacy_s32 pixel = 0; pixel < output_width * output_height; pixel++) {
		assert(image[pixel] == TEST_BACKGROUND_COLOR);
	}
	hires_shutdown();
	widescreen_set_enabled(0);
	widescreen_set_racing(0);
}

legacy_int main(void)
{
	static const legacy_s32 scales[] = {HIRES_SCALE, HIRES_MEDIUM_SCALE, HIRES_MINIMUM_SCALE};
	for (legacy_u32 index = 0; index < sizeof(scales) / sizeof(scales[0]); index++) {
		test_paused_toolbar_redraw(1280, 720, scales[index]);
		test_paused_toolbar_redraw(1280, 800, scales[index]);
		test_paused_toolbar_redraw(1080, 720, scales[index]);
	}
	puts("Widescreen paused replay toolbar tests passed");
	return 0;
}
