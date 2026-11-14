#include <assert.h>
#include <string.h>

#include "../c/frame/frame_internal.h"
#include "../c/externs.h"
#include "../c/shape3d/shape3d.h"
#include "../c/shape3d/shape3d_internal.h"
#include "../c/shape2d/shape2d.h"
#include "../c/shape2d/shape2d_internal.h"
#include "../c/residue.h"
#include "../c/platform.h"
#include "../c/ui/ui_text.h"
#include "../c/memmgr.h"
#include "../c/camera.h"
#include "../c/video/video_frame.h"
#include "../c/fatal.h"
#include "../c/game_input.h"
#include "../c/dashboard.h"
#include "../c/race/race_graphics.h"
#include "../c/race/race_stats.h"
#include "extended-replay-test-stubs.h"

#undef strcmp
#undef memcpy
#undef strlen

struct LEGACY_EXECUTION_RESIDUE legacy_execution_residue;
legacy_s16 legacy_render_player_headings_active;
legacy_u8 fps_display_enabled;
legacy_u8 mph_display_enabled;
legacy_u8 renderer_active;
static legacy_u32 realtime_ticks;

legacy_u32 dos_timer_get_realtime_counter(void)
{
	return realtime_ticks;
}

static legacy_u8 crack_lines[16];
static legacy_u8 crack_info[6];
static legacy_u16 crack_offset;
static legacy_u16 last_line[DRAW_LINE_WORD_COUNT];
static legacy_u32 drawn_lines;
static legacy_s16 player[LEGACY_RESIDUE_WORD_COUNT];
static legacy_s16 opponent[LEGACY_RESIDUE_WORD_COUNT];

struct TEXT_DRAW {
	char text[REPLAY_FILENAME_SIZE];
	legacy_s16 x;
	legacy_s16 y;
	legacy_s16 color;
	legacy_s16 shadow_color;
};

enum {
	OPPONENT_TEST_WIDTH = 24,
	OPPONENT_TEST_HEIGHT = 15,
	OPPONENT_TEST_LEFT_SHAPE = 3,
	OPPONENT_TEST_RIGHT_SHAPE = 4,
	OPPONENT_TEST_SHAPE_COUNT = 2,
	OPPONENT_TEST_LEFT_X = 68,
	OPPONENT_TEST_RIGHT_X = 228,
	OPPONENT_TEST_ICON_Y = 113,
	OPPONENT_TEST_TEXT_Y = 116,
	OPPONENT_TEST_FONT_HEIGHT = 8,
	OPPONENT_TEST_COMPACT_BOTTOM = 125,
	OPPONENT_TEST_STOCK_BOTTOM = 128,
	OPPONENT_TEST_TALL_BOTTOM = 151,
	OPPONENT_TEST_SCREEN_BOTTOM = 200,
	OPPONENT_TEST_ROOF_HEIGHT = 16,
	OPPONENT_TEST_TALL_ARROW_HEIGHT = 23,
	OPPONENT_TEST_TALL_FONT_HEIGHT = 20,
	OPPONENT_TEST_TEXT_SHADOW_HEIGHT = 1,
	OPPONENT_TEST_LABEL_X = 108,
	OPPONENT_TEST_LABEL_RIGHT = 213,
	OPPONENT_TEST_COMPACT_ICON_Y = 110,
	OPPONENT_TEST_COMPACT_TEXT_Y = 113,
	OPPONENT_TEST_TALL_ARROW_Y = 102,
	OPPONENT_TEST_TALL_ARROW_TEXT_Y = 105,
	OPPONENT_TEST_TALL_FONT_ICON_Y = 101,
	OPPONENT_TEST_TALL_FONT_Y = 104,
	OPPONENT_TEST_HIGH_ROOF = 114,
	OPPONENT_TEST_HIGH_ROOF_TEXT_Y = 117
};

enum {
	ROUTE_TEST_ICON_X = 148,
	ROUTE_TEST_DEFAULT_Y = 93,
	ROUTE_TEST_COMPACT_BOTTOM = 100,
	ROUTE_TEST_ROOF_TOP = 8,
	ROUTE_TEST_HIGH_ROOF = 96,
	ROUTE_TEST_SMALL_BOTTOM = 105,
	ROUTE_TEST_ARROW_Y = 85,
	ROUTE_TEST_TEXT_Y = 91,
	ROUTE_TEST_TALL_ARROW_Y = 77,
	ROUTE_TEST_TALL_TEXT_Y = 79,
	ROUTE_TEST_GROUP_Y = 82,
	ROUTE_TEST_OPPONENT_GROUP_Y = 65,
	ROUTE_TEST_OPPONENT_PENALTY_Y = 74,
	ROUTE_TEST_OPPONENT_ICON_Y = 85,
	ROUTE_TEST_OPPONENT_LABEL_Y = 88,
	ROUTE_TEST_PENALTY_FRAMES = 40,
	ROUTE_TEST_TEXT_X = 124,
	ROUTE_TEST_TEXT_RIGHT = 197,
	ROUTE_TEST_PAGE_COUNT = 2,
	ROUTE_TEST_PHASE_COUNT = 6
};

static struct SHAPE2D opponent_direction_shapes[OPPONENT_TEST_SHAPE_COUNT];
static struct {
	struct SHAPE2D *shape;
	legacy_s16 x;
	legacy_s16 y;
	legacy_u16 count;
} opponent_icon_draw;

#ifdef RESTUNTS_SDL3
#define STATUS_TEST_PREFIX "HyperVision: "
#else
#define STATUS_TEST_PREFIX "SuperSight: "
#endif

enum {
	DIAGNOSTIC_TEST_LEFT = 8,
	DIAGNOSTIC_TEST_TOP = 3,
	DIAGNOSTIC_TEST_FONT_WIDTH = 8,
	DIAGNOSTIC_TEST_FONT_HEIGHT = 8,
	DIAGNOSTIC_TEST_LINE_HEIGHT = 12,
#ifdef RESTUNTS_SDL3
	DIAGNOSTIC_TEST_LINE_COUNT = 2,
#else
	DIAGNOSTIC_TEST_LINE_COUNT = 1,
#endif
	DIAGNOSTIC_TEST_BOTTOM = DIAGNOSTIC_TEST_TOP +
							 (DIAGNOSTIC_TEST_LINE_COUNT - 1) * DIAGNOSTIC_TEST_LINE_HEIGHT +
							 DIAGNOSTIC_TEST_FONT_HEIGHT + 1,
	DIAGNOSTIC_TEST_RIGHT =
		DIAGNOSTIC_TEST_LEFT + (sizeof("65535 FPS") - 1U) * DIAGNOSTIC_TEST_FONT_WIDTH + 1U,
	DIAGNOSTIC_TEST_FILENAME_LINES = 4,
	DIAGNOSTIC_TEST_FILENAME_LABEL_Y =
		DIAGNOSTIC_TEST_TOP + DIAGNOSTIC_TEST_FILENAME_LINES * DIAGNOSTIC_TEST_LINE_HEIGHT
};

#define TEXT_DRAW_CAPACITY 12U
static struct TEXT_DRAW text_draws[TEXT_DRAW_CAPACITY];
static legacy_u32 text_draw_count;
static struct RECTANGLE text_bounds;
static struct RECTANGLE restored_roof_bounds;
static struct RECTANGLE copied_roof_bounds;
static legacy_u32 restored_roof_count;
static legacy_u32 copied_roof_count;
static struct SHAPE2D roof_shape;
static struct SHAPE2D frame_shape;
static struct SPRITE frame_sprite;
struct SPRITE far *render_window_sprite;
legacy_s8 dashboard_roof_shape_id[] = "roof";

enum {
	COMPOSITION_TEST_WIDTH = 320,
	COMPOSITION_TEST_HEIGHT = 200,
	COMPOSITION_TEST_RENDER = 0,
	COMPOSITION_TEST_FIRST_PAGE,
	COMPOSITION_TEST_SECOND_PAGE,
	COMPOSITION_TEST_BUFFER_COUNT,
	COMPOSITION_TEST_SCENE_COLOR = 1,
	COMPOSITION_TEST_ROOF_COLOR = 3,
	COMPOSITION_TEST_DASHBOARD_COLOR = 6,
	COMPOSITION_TEST_FPS_TOP = 3,
	COMPOSITION_TEST_ROOF_HEIGHT = 40,
	COMPOSITION_TEST_SINGLE_PAGE = 1
};

enum COMPOSITION_TEST_PHASE {
	COMPOSITION_TEST_LONG_STATUS,
	COMPOSITION_TEST_SHORT_STATUS,
	COMPOSITION_TEST_EXPIRED_STATUS,
	COMPOSITION_TEST_PHASE_COUNT
};

static struct {
	legacy_u8 active;
	legacy_u8 target;
	legacy_u8 pixels[COMPOSITION_TEST_BUFFER_COUNT][COMPOSITION_TEST_HEIGHT]
					[COMPOSITION_TEST_WIDTH];
} composition;

static void composition_fill(const struct RECTANGLE *bounds, legacy_u8 color)
{
	for (legacy_s16 y = bounds->top; y < bounds->bottom; y++) {
		for (legacy_s16 x = bounds->left; x < bounds->right; x++) {
			assert(x >= 0 && x < COMPOSITION_TEST_WIDTH);
			assert(y >= 0 && y < COMPOSITION_TEST_HEIGHT);
			composition.pixels[composition.target][y][x] = color;
		}
	}
}

enum {
	MPH_TEST_PAGE_CAPACITY = 2,
	MPH_TEST_ALLOCATION_FLAGS = 15,
	MPH_TEST_VARIABLE_ADVANCE = 9,
	MPH_TEST_STANDARD_ADVANCE = 8,
	MPH_TEST_BACKGROUND_FIRST = 8,
	MPH_TEST_BACKGROUND_COLORS = 4,
	MPH_TEST_PATTERN_ROW_MULTIPLIER = 3,
	MPH_TEST_GLYPH_PATTERN_DIVISOR = 4,
	MPH_TEST_SHADOW_OFFSET = 1
};

static legacy_u8 mph_variable_font;
static legacy_u16 mph_capture_count;
static legacy_u16 mph_restore_count;
static legacy_u16 mph_allocation_count;
static legacy_u16 mph_free_count;
static legacy_u32 mph_available_bytes = LEGACY_U32_MAX;
static struct SHAPE2D mph_target_shapes[COMPOSITION_TEST_BUFFER_COUNT];
static struct SPRITE mph_target_sprites[COMPOSITION_TEST_BUFFER_COUNT];
static struct {
	struct SPRITE sprite;
	struct SHAPE2D shape;
	legacy_u16 width;
	legacy_u16 height;
	legacy_u8 pending;
	legacy_u8 target;
	legacy_u8 pixels[COMPOSITION_TEST_WIDTH * COMPOSITION_TEST_HEIGHT];
} mph_backing[MPH_TEST_PAGE_CAPACITY];

static void mph_test_restore_target(void)
{
	if (mph_variable_font == 0 || composition.active == 0) {
		return;
	}
	for (legacy_u8 target = 0; target < COMPOSITION_TEST_BUFFER_COUNT; target++) {
		if (drawing_sprite.sprite_bitmapptr == &mph_target_shapes[target]) {
			composition.target = target;
			return;
		}
	}
	assert(0 && "Unexpected MPH drawing target");
}

void sprite_select_target(struct SPRITE far *target)
{
	drawing_sprite = *target;
	mph_test_restore_target();
}

legacy_u32 mmgr_get_res_ofs_diff_scaled(void)
{
	return mph_available_bytes;
}

struct SPRITE far *sprite_make_wnd(legacy_u16 width, legacy_u16 height, legacy_u16 flags)
{
	assert(mph_variable_font != 0 && flags == MPH_TEST_ALLOCATION_FLAGS);
	assert(mph_allocation_count < MPH_TEST_PAGE_CAPACITY);
	assert(width > 0 && width <= COMPOSITION_TEST_WIDTH);
	assert(height > 0 && height <= COMPOSITION_TEST_HEIGHT);
	legacy_u16 index = mph_allocation_count++;
	memset(&mph_backing[index], 0, sizeof(mph_backing[index]));
	mph_backing[index].width = width;
	mph_backing[index].height = height;
	mph_backing[index].shape.width = width;
	mph_backing[index].shape.height = height;
	mph_backing[index].sprite.sprite_bitmapptr = &mph_backing[index].shape;
	return &mph_backing[index].sprite;
}

void sprite_free_wnd(struct SPRITE far *sprite)
{
	assert(mph_allocation_count != 0);
	legacy_u16 index = mph_allocation_count - 1U;
	assert(sprite == &mph_backing[index].sprite);
	/* The real allocator releases each window using its original row count. */
	assert(sprite->sprite_bitmapptr->height == mph_backing[index].height);
	mph_allocation_count--;
	mph_free_count++;
}

static legacy_u16 mph_test_backing_index(struct SHAPE2D far *shape)
{
	for (legacy_u16 index = 0; index < mph_allocation_count; index++) {
		if (shape == &mph_backing[index].shape) {
			assert(shape->width != 0 && shape->width <= mph_backing[index].width);
			assert(shape->height != 0 && shape->height <= mph_backing[index].height);
			return index;
		}
	}
	assert(0 && "Unexpected MPH backing shape");
	return 0;
}

void sprite_clear_shape_alt(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y)
{
	legacy_u16 index = mph_test_backing_index(shape);
	assert(mph_backing[index].pending == 0);
	assert(composition.active != 0 && x >= 0 && y >= 0);
	assert(x + shape->width <= COMPOSITION_TEST_WIDTH);
	assert(y + shape->height <= COMPOSITION_TEST_HEIGHT);
	mph_test_restore_target();
	mph_backing[index].target = composition.target;
	mph_backing[index].pending = 1;
	shape->position_x = (legacy_u16)x;
	shape->position_y = (legacy_u16)y;
	for (legacy_u16 row = 0; row < shape->height; row++) {
		memcpy(&mph_backing[index].pixels[row * shape->width],
			   &composition.pixels[composition.target][y + row][x], shape->width);
	}
	mph_capture_count++;
}

void sprite_shape_to_1(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y)
{
	legacy_u16 index = mph_test_backing_index(shape);
	assert(mph_backing[index].pending != 0);
	assert(composition.active != 0 && x >= 0 && y >= 0);
	assert(x + shape->width <= COMPOSITION_TEST_WIDTH);
	assert(y + shape->height <= COMPOSITION_TEST_HEIGHT);
	mph_test_restore_target();
	assert(composition.target == mph_backing[index].target);
	for (legacy_u16 row = 0; row < shape->height; row++) {
		memcpy(&composition.pixels[composition.target][y + row][x],
			   &mph_backing[index].pixels[row * shape->width], shape->width);
	}
	mph_backing[index].pending = 0;
	mph_restore_count++;
}

void sprite_shape_to_1_alt(struct SHAPE2D far *shape)
{
	sprite_shape_to_1(shape, (legacy_s16)shape->position_x, (legacy_s16)shape->position_y);
}

void font_set_fontdef(void)
{
	font_glyph_height = DIAGNOSTIC_TEST_FONT_HEIGHT;
}

void shape2d_put_word(legacy_u8 far *destination, legacy_u16 value)
{
	LEGACY_WRITE_U16_LE(destination, value);
}

static legacy_s16 mph_test_glyph_advance(legacy_s8 character)
{
	if (mph_variable_font != 0 && character != 'P' && character != 'H') {
		return MPH_TEST_VARIABLE_ADVANCE;
	}
	return MPH_TEST_STANDARD_ADVANCE;
}

static void mph_test_text_pixels(legacy_u8 pixels[COMPOSITION_TEST_HEIGHT][COMPOSITION_TEST_WIDTH],
								 const legacy_s8 *text, legacy_s16 x, legacy_s16 y, legacy_u8 color,
								 legacy_u8 shadow)
{
	/* Sparse masks preserve holes and spaces, unlike the solid diagnostic stub. */
	for (legacy_s16 layer = MPH_TEST_SHADOW_OFFSET; layer >= 0; layer--) {
		legacy_s16 glyph_x = x;
		for (const legacy_s8 *character = text; *character != 0; character++) {
			legacy_s16 advance = mph_test_glyph_advance(*character);
			if (*character != ' ') {
				for (legacy_s16 row = 0; row < font_glyph_height; row++) {
					for (legacy_s16 column = 0; column < advance - 1; column++) {
						if (row == 0 || column == 0 ||
							((*character + row + column) % MPH_TEST_GLYPH_PATTERN_DIVISOR) == 0) {
							pixels[y + row + layer][glyph_x + column + layer] =
								layer != 0 ? shadow : color;
						}
					}
				}
			}
			glyph_x += advance;
		}
	}
}

void sprite_save_context(struct SPRITE context[SPRITE_STATE_COUNT])
{
	context[0] = drawing_sprite;
	context[1] = screen_sprite;
}

void sprite_restore_context(struct SPRITE context[SPRITE_STATE_COUNT])
{
	drawing_sprite = context[0];
	screen_sprite = context[1];
	mph_test_restore_target();
}

void sprite_set_target_clip_bounds(legacy_u16 left, legacy_u16 right, legacy_u16 top,
								   legacy_u16 bottom)
{
	drawing_sprite.sprite_raster_left = left;
	drawing_sprite.sprite_raster_right = right;
	drawing_sprite.sprite_top = top;
	drawing_sprite.sprite_bottom = bottom;
}

legacy_s8 far *locate_shape_nofatal(legacy_s8 far *resource, const legacy_s8 *name)
{
	assert(resource == stdaresptr);
	assert(name == dashboard_roof_shape_id);
	return (legacy_s8 far *)&roof_shape;
}

void shape2d_rle_copy_position_clipped(struct SHAPE2D far *shape)
{
	assert(shape == &roof_shape);
	restored_roof_bounds.left = drawing_sprite.sprite_raster_left;
	restored_roof_bounds.right = drawing_sprite.sprite_raster_right;
	restored_roof_bounds.top = drawing_sprite.sprite_top;
	restored_roof_bounds.bottom = drawing_sprite.sprite_bottom;
	restored_roof_count++;
	if (composition.active != 0) {
		composition_fill(&restored_roof_bounds, COMPOSITION_TEST_ROOF_COLOR);
	}
}

void sprite_putimage(struct SHAPE2D far *shape)
{
	assert(shape == &frame_shape);
	copied_roof_bounds.left = drawing_sprite.sprite_raster_left;
	copied_roof_bounds.right = drawing_sprite.sprite_raster_right;
	copied_roof_bounds.top = drawing_sprite.sprite_top;
	copied_roof_bounds.bottom = drawing_sprite.sprite_bottom;
	copied_roof_count++;
	if (composition.active != 0) {
		for (legacy_s16 y = copied_roof_bounds.top; y < copied_roof_bounds.bottom; y++) {
			for (legacy_s16 x = copied_roof_bounds.left; x < copied_roof_bounds.right; x++) {
				composition.pixels[COMPOSITION_TEST_FIRST_PAGE][y][x] =
					composition.pixels[COMPOSITION_TEST_RENDER][y][x];
			}
		}
	}
}

static struct RECTANGLE mph_clear_bounds;
static legacy_u16 mph_clear_count;

void sprite_fill_rect_clipped(legacy_s16 x, legacy_s16 y, legacy_s16 width, legacy_s16 height,
							  legacy_s16 color)
{
	mph_clear_bounds = (struct RECTANGLE){x, x + width, y, y + height};
	mph_clear_count++;
	assert(color == 0);
	if (composition.active != 0) {
		composition_fill(&mph_clear_bounds, (legacy_u8)color);
	}
}

legacy_s16 font_text_width(const legacy_s8 *text)
{
	legacy_s16 width = 0;
	for (; *text != 0; text++) {
		width += mph_test_glyph_advance(*text);
	}
	return width;
}

void mouse_draw_opaque_check(void)
{
}

void mouse_draw_transparent_check(void)
{
}

void fatal_error(const legacy_s8 *format, ...)
{
	(void)format;
	assert(0 && "Unexpected fatal error");
}

legacy_s8 far *locate_text_res(legacy_s8 far *resource, const legacy_s8 *name)
{
	assert(resource == gameresptr);
	if (strcmp((const legacy_char *)name, "rpl") == 0) {
		return (legacy_s8 *)"Replay";
	}
	if (strcmp((const legacy_char *)name, "opp") == 0) {
		return (legacy_s8 *)"Opponent Near";
	}
	if (strcmp((const legacy_char *)name, "www") == 0) {
		return (legacy_s8 *)"Wrong Way";
	}
	if (strcmp((const legacy_char *)name, "pen") == 0) {
		return (legacy_s8 *)"Penalty: ";
	}
	if (strcmp((const legacy_char *)name, "dm1") == 0) {
		return (legacy_s8 *)"Demo";
	}
	assert(strcmp((const legacy_char *)name, "dm2") == 0);
	return (legacy_s8 *)"Press a key";
}

struct RECTANGLE *intro_draw_text(legacy_s8 *text, legacy_s16 x, legacy_s16 y, legacy_s16 color,
								  legacy_s16 shadow_color)
{
	size_t length = strlen((const legacy_char *)text);
	assert(text_draw_count < sizeof(text_draws) / sizeof(text_draws[0]));
	struct TEXT_DRAW *draw = &text_draws[text_draw_count++];
	assert(length < sizeof(draw->text));
	memcpy(draw->text, text, length + 1);
	draw->x = x;
	draw->y = y;
	draw->color = color;
	draw->shadow_color = shadow_color;
	/* Include the actual glyph advances and the shadow's extra pixel. */
	text_bounds.left = x;
	text_bounds.right = x + font_text_width(text) + OPPONENT_TEST_TEXT_SHADOW_HEIGHT;
	text_bounds.top = y;
	text_bounds.bottom = y + font_glyph_height + OPPONENT_TEST_TEXT_SHADOW_HEIGHT;
	if (composition.active != 0) {
		if (mph_variable_font != 0) {
			mph_test_text_pixels(composition.pixels[composition.target], text, x, y,
								 (legacy_u8)color, (legacy_u8)shadow_color);
		} else {
			/* Solid glyph bounds expose subsequent opaque cockpit overwrites. */
			composition_fill(&text_bounds, (legacy_u8)color);
		}
	}
	return &text_bounds;
}

legacy_s16 font_centered_text_x(const legacy_s8 *text)
{
	return (320 - strlen((const legacy_char *)text) * 8) / 2;
}

void format_frame_as_string(legacy_s8 *destination, legacy_s16 frame_count,
							legacy_s16 include_hundredths)
{
	assert(frame_count == ROUTE_TEST_PENALTY_FRAMES && include_hundredths == 0);
	copy_string(destination, (legacy_s8 *)"2.00");
}

void sprite_putpixel_clipped(legacy_s16 x, legacy_s16 y, legacy_s16 color)
{
	(void)x;
	(void)y;
	(void)color;
	assert(0 && "Unexpected ghost pixel");
}

legacy_u16 shape2d_get_width(const struct SHAPE2D far *shape)
{
	return shape->width;
}

legacy_u16 shape2d_get_height(const struct SHAPE2D far *shape)
{
	return shape->height;
}

void sprite_putimage_transparent(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y)
{
	assert(shape == &opponent_direction_shapes[0] || shape == &opponent_direction_shapes[1]);
	assert(opponent_icon_draw.count == 0);
	opponent_icon_draw.shape = shape;
	opponent_icon_draw.x = x;
	opponent_icon_draw.y = y;
	opponent_icon_draw.count++;
	if (composition.active != 0) {
		struct RECTANGLE bounds = {x, x + shape->width, y, y + shape->height};
		/* Native bitmaps obey the viewport, unlike the legacy text writer. */
		if (bounds.top < drawing_sprite.sprite_top) {
			bounds.top = drawing_sprite.sprite_top;
		}
		if (bounds.bottom > drawing_sprite.sprite_bottom) {
			bounds.bottom = drawing_sprite.sprite_bottom;
		}
		composition_fill(&bounds, (legacy_u8)dialog_fnt_colour);
	}
}

legacy_s8 far *locate_shape_alt(legacy_s8 far *resource, const legacy_s8 *name)
{
	assert(resource == gameresptr);
	if (strcmp((const legacy_char *)name, "crak") == 0) {
		return (legacy_s8 far *)crack_lines;
	}
	assert(strcmp((const legacy_char *)name, "cinf") == 0);
	return (legacy_s8 far *)crack_info;
}

legacy_u16 dos_memory_pointer_offset(const void far *pointer)
{
	assert(pointer == crack_lines);
	return crack_offset;
}

void sprite_draw_line_from_setup(const legacy_u16 *line)
{
	memcpy(last_line, line, sizeof(last_line));
	drawn_lines++;
}

static void assert_words(const legacy_s16 *words, legacy_s16 a, legacy_s16 b, legacy_s16 c,
						 legacy_s16 d)
{
	assert(words[0] == a && words[1] == b && words[2] == c && words[3] == d);
}

static void set_line(legacy_u32 index, legacy_s16 x1, legacy_s16 y1, legacy_s16 x2, legacy_s16 y2)
{
	LEGACY_WRITE_U16_LE(crack_lines + index * 8, x1);
	LEGACY_WRITE_U16_LE(crack_lines + index * 8 + 2, y1);
	LEGACY_WRITE_U16_LE(crack_lines + index * 8 + 4, x2);
	LEGACY_WRITE_U16_LE(crack_lines + index * 8 + 6, y2);
}

static void reset_overlay(legacy_s16 dirty_rects)
{
	struct SHAPE3D_LEGACY_OPPONENT_RENDER_CONTEXT context;
	legacy_u32 i;

	memset(&context, 0, sizeof(context));
	context.wheel_headings = opponent;
	shape3d_set_legacy_render_stack(player, 0x9000, 0x4000, &context);
	for (i = 0; i < LEGACY_RESIDUE_WORD_COUNT; i++) {
		player[i] = 100 + i;
		opponent[i] = 200 + i;
	}
	drawing_sprite.sprite_raster_left = 0;
	drawing_sprite.sprite_raster_right = 320;
	drawing_sprite.sprite_top = 0;
	drawing_sprite.sprite_bottom = 200;
	slow_video_mgmt_copy = dirty_rects;
	framespersec = 20;
	dialog_fnt_colour = 7;
	drawn_lines = 0;
	crack_offset = 11;
	LEGACY_WRITE_U16_LE(crack_info, 2);
	LEGACY_WRITE_U16_LE(crack_info + 2, 1);
	LEGACY_WRITE_U16_LE(crack_info + 4, 2);
	set_line(0, 20, 40, 100, 160);
	set_line(1, 140, 40, 200, 100);
}

static void test_incremental_crack_overlay(void)
{
	struct RECTANGLE *bounds;

	reset_overlay(1);
	bounds = init_crak(0, 10, 100);
	assert(drawn_lines == 3);
	assert(last_line[DRAW_LINE_COLOR_INDEX] == 7);
	assert(last_line[DRAW_LINE_START_Y_INDEX] == 30);
	assert(last_line[DRAW_LINE_END_Y_INDEX] == 90);
	assert(bounds->left == 20 && bounds->right == 101);
	assert(bounds->top == 29 && bounds->bottom == 92);
	/* SEG003 init_crak -> SEG012 preRender_line places the first four line
	 * words over player headings; the later bounds calls save SI/DI over the
	 * last two opponent words. These replace the preceding scene's residue. */
	assert_words(player, 0, 20, 0, 30);
	assert_words(opponent, 0, 0, 0, 11);

	/* A later animation frame retains the last line's index and coordinates.
	 * Clamp to the final frame and vary the normalized resource offset. */
	crack_offset = 3;
	init_crak(300, 10, 100);
	assert(drawn_lines == 9);
	assert_words(player, 0, 140, 0, 30);
	assert_words(opponent, 0, 0, 1, 3);
}

static void test_rejected_crack_lines(void)
{
	reset_overlay(1);
	set_line(0, 20, -10, 100, -10);
	init_crak(0, 0, 200);
	assert(drawn_lines == 0);
	assert_words(player, 0, 20, 0, 0);
	assert_words(opponent, 0, 0, 0, 11);

	reset_overlay(1);
	set_line(0, -20, 10, -10, 20);
	init_crak(0, 0, 200);
	assert(drawn_lines == 0);
	assert(opponent[0] == 0 && opponent[1] == 11);
	assert(opponent[2] == 0 && opponent[3] == 11);
}

static void test_direct_redraw_stack(void)
{
	reset_overlay(0);
	init_crak(0, 10, 100);
	assert(drawn_lines == 3);
	/* Without pending rect_union arguments, the line buffer is four bytes
	 * higher. A drawn line leaves SEG012's return CS and the buffer address. */
	assert_words(player, 0x49cc, (legacy_s16)0x8fb0, 0, 20);
	assert_words(opponent, 7, DRAW_LINE_MODE_X_MAJOR_RIGHT, 0, 0);

	reset_overlay(0);
	set_line(0, 20, -10, 100, -10);
	init_crak(0, 0, 200);
	assert(drawn_lines == 0);
	/* A rejected line never makes the pixel call: its setup argument stays. */
	assert_words(player, -10, (legacy_s16)0x8fb0, 0, 20);
	assert_words(opponent, 7,
				 DRAW_LINE_MODE_HORIZONTAL | (DRAW_LINE_CLIP_TOP << DRAW_LINE_CLIP_SHIFT), 0, 0);
}

static void test_explicit_crack_context(void)
{
	reset_overlay(1);
	preRender_line(20, 30, 100, 90, 7);
	assert_words(player, 100, 101, 102, 103);
	assert_words(opponent, 200, 201, 202, 203);

	shape3d_set_legacy_render_stack(0, 0, 0, 0);
	init_crak(0, 10, 100);
	assert(drawn_lines == 4);
	assert_words(player, 100, 101, 102, 103);
	assert_words(opponent, 200, 201, 202, 203);
}

static void reset_ingame_text(const char *filename)
{
	reset_overlay(1);
	video_x_alignment = 1;
	memset(&state, 0, sizeof(state));
	assert(strlen(filename) < sizeof(replay_filename));
	memcpy(replay_filename, filename, strlen(filename) + 1);
	game_replay_mode = REPLAY_MODE_PLAYBACK;
	idle_expired = 0;
	passed_security = 1;
	cameramode = CAMERA_MODE_TRACKSIDE;
	text_draw_count = 0;
	memset(&opponent_icon_draw, 0, sizeof(opponent_icon_draw));
	fps_display_enabled = FRAME_FPS_DISPLAY_OFF;
	mph_display_enabled = FRAME_MPH_DISPLAY_OFF;
	mph_clear_count = 0;
	frame_fps_reset();
	realtime_ticks = 0;
	roofbmpheight_copy = 0;
	dashboard_visible = 0;
	font_glyph_height = 8;
	video_uses_page_flipping = 0;
	restored_roof_count = copied_roof_count = 0;
	frame_sprite.sprite_bitmapptr = &frame_shape;
	render_window_sprite = &frame_sprite;
}

static legacy_u32 diagnostic_text_start;

static struct RECTANGLE *draw_complete_ingame_text(void)
{
	struct RECTANGLE *bounds = draw_ingame_text();
	diagnostic_text_start = text_draw_count;
	frame_fps_present_roof();
	frame_fps_draw_text();
	return bounds;
}

static void assert_text(legacy_u32 index, const legacy_char *text, legacy_s16 x, legacy_s16 y)
{
	assert(index < text_draw_count);
	assert(strcmp(text_draws[index].text, text) == 0);
	assert(text_draws[index].x == x && text_draws[index].y == y);
	assert(text_draws[index].color == dialog_fnt_colour);
	assert(text_draws[index].shadow_color == 0);
}

static void test_replay_filename_survives_blink(void)
{
	static const char *filenames[] = {"A", "DEFAULT", "RACE2026", "MOUNTAINRUN2026",
									  "ABCDEFGHIJKLMNOPQRSTUVWXYZ12345"};
	for (legacy_u32 name = 0; name < sizeof(filenames) / sizeof(filenames[0]); name++) {
		for (legacy_u32 frame_rate = 10; frame_rate <= 20; frame_rate += 10) {
			reset_ingame_text(filenames[name]);
			dialog_fnt_colour = 9 + name;
			framespersec = frame_rate;
			legacy_s16 filename_x = 312 - strlen(filenames[name]) * 8;
			for (legacy_u32 frame = 0; frame < frame_rate * 2; frame++) {
				state.game_frame = frame;
				text_draw_count = 0;
				struct RECTANGLE *bounds = draw_complete_ingame_text();
				assert_text(0, filenames[name], filename_x, 3);
				assert(bounds->top == 3 && bounds->right == 313);
				if (frame % frame_rate < frame_rate / 2) {
					assert(text_draw_count == 2);
					assert_text(1, "Replay", 264, 15);
					assert(bounds->left == (filename_x < 264 ? filename_x : 264));
					assert(bounds->bottom == 24);
				} else {
					assert(text_draw_count == 1);
					/* Incremental redraw must still copy the nonblinking filename. */
					assert(bounds->left == filename_x && bounds->bottom == 12);
				}
			}
		}
	}
}

static void test_wrapped_replay_filename(void)
{
	static const struct {
		legacy_u32 length;
		legacy_u32 lines;
	} cases[] = {{38, 1}, {39, 2}, {76, 2}, {127, 4}};
	for (legacy_u32 name = 0; name < sizeof(cases) / sizeof(cases[0]); name++) {
		char filename[REPLAY_FILENAME_SIZE];
		for (legacy_u32 character = 0; character < cases[name].length; character++) {
			filename[character] = 'A' + character % 26;
		}
		filename[cases[name].length] = 0;
		for (legacy_u32 phase = 0; phase < 3; phase++) {
			reset_ingame_text(filename);
			if (phase == 1) {
				state.game_frame = framespersec / 2;
			} else if (phase == 2) {
				game_replay_mode = REPLAY_MODE_PAUSED;
			}
			struct RECTANGLE *bounds = draw_complete_ingame_text();
			legacy_u32 line_count = cases[name].lines;
			assert(text_draw_count == line_count + (phase == 0));
			char reconstructed[REPLAY_FILENAME_SIZE];
			legacy_u32 copied = 0;
			for (legacy_u32 line = 0; line < line_count; line++) {
				legacy_u32 length = line + 1 == line_count ? cases[name].length - copied : 38;
				char expected[39];
				memcpy(expected, filename + copied, length);
				expected[length] = 0;
				assert_text(line, expected, 312 - length * 8, 3 + line * 12);
				memcpy(reconstructed + copied, text_draws[line].text, length);
				copied += length;
			}
			reconstructed[copied] = 0;
			assert(strcmp(reconstructed, filename) == 0);
			assert(strcmp((const legacy_char *)replay_filename, filename) == 0);
			assert(bounds->left == 8 && bounds->right == 313);
			assert(bounds->top == 3);
			if (phase == 0) {
				assert_text(line_count, "Replay", 264, 3 + line_count * 12);
				assert(bounds->bottom == (legacy_s16)((line_count + 1) * 12));
			} else {
				assert(bounds->bottom == (legacy_s16)(line_count * 12));
			}
		}
	}
}

static void test_replay_filename_when_paused(void)
{
	reset_ingame_text("RACE2026");
	game_replay_mode = REPLAY_MODE_PAUSED;
	struct RECTANGLE *bounds = draw_complete_ingame_text();
	assert(text_draw_count == 1);
	assert_text(0, "RACE2026", 248, 3);
	assert(bounds->left == 248 && bounds->right == 313);
	assert(bounds->top == 3 && bounds->bottom == 12);
}

static void test_unnamed_replay_overlay(void)
{
	reset_ingame_text("");
	struct RECTANGLE *bounds = draw_complete_ingame_text();
	assert(text_draw_count == 1);
	assert_text(0, "Replay", 264, 15);
	assert(bounds->top == 15 && bounds->bottom == 24);
	state.game_frame = 10;
	text_draw_count = 0;
	bounds = draw_complete_ingame_text();
	assert(text_draw_count == 0);
	assert(memcmp(bounds, &empty_rect, sizeof(*bounds)) == 0);

	game_replay_mode = REPLAY_MODE_PAUSED;
	bounds = draw_complete_ingame_text();
	assert(text_draw_count == 0);
	assert(memcmp(bounds, &empty_rect, sizeof(*bounds)) == 0);
}

static void test_filename_hidden_in_live_race_and_demo(void)
{
	reset_ingame_text("DEFAULT");
	game_replay_mode = REPLAY_MODE_LIVE;
	state.game_inputmode = GAME_INPUT_MODE_ACTIVE;
	struct RECTANGLE *bounds = draw_complete_ingame_text();
	assert(text_draw_count == 0);
	assert(memcmp(bounds, &empty_rect, sizeof(*bounds)) == 0);

	game_replay_mode = REPLAY_MODE_PLAYBACK;
	idle_expired = 1;
	bounds = draw_complete_ingame_text();
	assert(text_draw_count == 2);
	assert_text(0, "Demo", 144, 170);
	assert_text(1, "Press a key", 116, 182);
	assert(bounds->top == 170 && bounds->bottom == 191);
}

static void assert_fps(const legacy_char *expected, legacy_s16 color)
{
	text_draw_count = 0;
	struct RECTANGLE *bounds = draw_complete_ingame_text();
	const struct TEXT_DRAW *fps = &text_draws[diagnostic_text_start];
	assert(text_draw_count == diagnostic_text_start + DIAGNOSTIC_TEST_LINE_COUNT);
	assert(strcmp(fps->text, expected) == 0);
	assert(fps->x == 8 && fps->y == 3);
	assert(fps->color == color && fps->shadow_color == 0);
	assert(bounds->left == 8 && bounds->top == 3);
	assert(bounds->right >= (legacy_s16)(8 + strlen(expected) * 8 + 1));
}

static void present_frames(legacy_u32 frames, legacy_u32 ticks)
{
	legacy_u32 start = realtime_ticks;
	for (legacy_u32 frame = 1; frame <= frames; frame++) {
		realtime_ticks = LEGACY_U32_WRAP_ADD(start, ticks * frame / frames);
		frame_fps_record_presented();
	}
}

static void test_opponent_indicator_fits_viewport(void)
{
	static const struct {
		legacy_s16 top;
		legacy_s16 bottom;
		legacy_u16 arrow_height;
		legacy_s16 font_height;
		legacy_s16 icon_y;
		legacy_s16 text_y;
	} cases[] = {
		{OPPONENT_TEST_ROOF_HEIGHT, OPPONENT_TEST_COMPACT_BOTTOM, OPPONENT_TEST_HEIGHT,
		 OPPONENT_TEST_FONT_HEIGHT, OPPONENT_TEST_COMPACT_ICON_Y, OPPONENT_TEST_COMPACT_TEXT_Y},
		{OPPONENT_TEST_ROOF_HEIGHT, OPPONENT_TEST_STOCK_BOTTOM, OPPONENT_TEST_HEIGHT,
		 OPPONENT_TEST_FONT_HEIGHT, OPPONENT_TEST_ICON_Y, OPPONENT_TEST_TEXT_Y},
		{OPPONENT_TEST_ROOF_HEIGHT, OPPONENT_TEST_TALL_BOTTOM, OPPONENT_TEST_HEIGHT,
		 OPPONENT_TEST_FONT_HEIGHT, OPPONENT_TEST_ICON_Y, OPPONENT_TEST_TEXT_Y},
		{OPPONENT_TEST_ROOF_HEIGHT, OPPONENT_TEST_SCREEN_BOTTOM, OPPONENT_TEST_HEIGHT,
		 OPPONENT_TEST_FONT_HEIGHT, OPPONENT_TEST_ICON_Y, OPPONENT_TEST_TEXT_Y},
		{OPPONENT_TEST_ROOF_HEIGHT, OPPONENT_TEST_COMPACT_BOTTOM, OPPONENT_TEST_TALL_ARROW_HEIGHT,
		 OPPONENT_TEST_FONT_HEIGHT, OPPONENT_TEST_TALL_ARROW_Y, OPPONENT_TEST_TALL_ARROW_TEXT_Y},
		{OPPONENT_TEST_ROOF_HEIGHT, OPPONENT_TEST_COMPACT_BOTTOM, OPPONENT_TEST_HEIGHT,
		 OPPONENT_TEST_TALL_FONT_HEIGHT, OPPONENT_TEST_TALL_FONT_ICON_Y, OPPONENT_TEST_TALL_FONT_Y},
		{OPPONENT_TEST_HIGH_ROOF, OPPONENT_TEST_SCREEN_BOTTOM, OPPONENT_TEST_HEIGHT,
		 OPPONENT_TEST_FONT_HEIGHT, OPPONENT_TEST_HIGH_ROOF, OPPONENT_TEST_HIGH_ROOF_TEXT_Y}};
	static const legacy_u8 indicators[] = {ROUTE_INDICATOR_LEFT, ROUTE_INDICATOR_RIGHT};
	for (legacy_u16 direction = 0; direction < OPPONENT_TEST_SHAPE_COUNT; direction++) {
		for (legacy_u16 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
			reset_ingame_text("");
			game_replay_mode = REPLAY_MODE_LIVE;
			cameramode = CAMERA_MODE_COCKPIT;
			state.game_inputmode = GAME_INPUT_MODE_ACTIVE;
			state.game_opponent_route_indicator = indicators[direction];
			font_glyph_height = cases[index].font_height;
			drawing_sprite.sprite_top = cases[index].top;
			drawing_sprite.sprite_bottom = cases[index].bottom;
			struct SHAPE2D *shape = &opponent_direction_shapes[direction];
			shape->width = OPPONENT_TEST_WIDTH;
			shape->height = cases[index].arrow_height;
			sdgame2shapes[OPPONENT_TEST_LEFT_SHAPE] = &opponent_direction_shapes[0];
			sdgame2shapes[OPPONENT_TEST_RIGHT_SHAPE] = &opponent_direction_shapes[1];
			struct RECTANGLE bounds = *draw_ingame_text();
			legacy_s16 x = direction == 0 ? OPPONENT_TEST_LEFT_X : OPPONENT_TEST_RIGHT_X;
			assert(opponent_icon_draw.count == 1 && opponent_icon_draw.shape == shape);
			assert(opponent_icon_draw.x == x && opponent_icon_draw.y == cases[index].icon_y);
			assert(opponent_icon_draw.y >= drawing_sprite.sprite_top);
			assert(opponent_icon_draw.y + shape->height <= drawing_sprite.sprite_bottom);
			assert(text_draw_count == 1);
			assert_text(0, "Opponent Near", OPPONENT_TEST_LABEL_X, cases[index].text_y);
			assert(text_draws[0].y >= drawing_sprite.sprite_top);
			assert(text_draws[0].y + font_glyph_height + OPPONENT_TEST_TEXT_SHADOW_HEIGHT <=
				   drawing_sprite.sprite_bottom);
			assert(bounds.top == cases[index].icon_y);
			legacy_s16 icon_bottom = cases[index].icon_y + cases[index].arrow_height;
			legacy_s16 label_bottom =
				cases[index].text_y + cases[index].font_height + OPPONENT_TEST_TEXT_SHADOW_HEIGHT;
			assert(bounds.bottom == (icon_bottom > label_bottom ? icon_bottom : label_bottom));
			assert(bounds.left == (direction == 0 ? OPPONENT_TEST_LEFT_X : OPPONENT_TEST_LABEL_X));
			assert(bounds.right == (direction == 0 ? OPPONENT_TEST_LABEL_RIGHT
												   : OPPONENT_TEST_RIGHT_X + OPPONENT_TEST_WIDTH));

			/* The next frame must report no current bounds after the indicator disappears;
			 * the frame renderer retains the preceding rectangle for cleanup. */
			state.game_opponent_route_indicator = ROUTE_INDICATOR_NONE;
			text_draw_count = 0;
			opponent_icon_draw.count = 0;
			assert(memcmp(draw_ingame_text(), &empty_rect, sizeof(bounds)) == 0);
			assert(text_draw_count == 0 && opponent_icon_draw.count == 0);
		}
	}
}

static void test_opponent_indicator_requires_room(void)
{
	reset_ingame_text("");
	game_replay_mode = REPLAY_MODE_LIVE;
	cameramode = CAMERA_MODE_COCKPIT;
	state.game_inputmode = GAME_INPUT_MODE_ACTIVE;
	state.game_opponent_route_indicator = ROUTE_INDICATOR_LEFT;
	opponent_direction_shapes[0].width = OPPONENT_TEST_WIDTH;
	opponent_direction_shapes[0].height = OPPONENT_TEST_HEIGHT;
	sdgame2shapes[OPPONENT_TEST_LEFT_SHAPE] = &opponent_direction_shapes[0];
	drawing_sprite.sprite_top = OPPONENT_TEST_HIGH_ROOF;
	drawing_sprite.sprite_bottom = OPPONENT_TEST_COMPACT_BOTTOM;
	assert(memcmp(draw_ingame_text(), &empty_rect, sizeof(empty_rect)) == 0);
	assert(text_draw_count == 0 && opponent_icon_draw.count == 0);
}

static void reset_player_route(legacy_u8 indicator, legacy_s16 top, legacy_s16 bottom)
{
	reset_ingame_text("");
	game_replay_mode = REPLAY_MODE_LIVE;
	cameramode = CAMERA_MODE_COCKPIT;
	state.game_inputmode = GAME_INPUT_MODE_ACTIVE;
	state.game_player_route_indicator = indicator;
	followOpponentFlag = 0;
	show_penalty_counter = 0;
	drawing_sprite.sprite_top = top;
	drawing_sprite.sprite_bottom = bottom;
	for (legacy_u16 direction = 0; direction < OPPONENT_TEST_SHAPE_COUNT; direction++) {
		opponent_direction_shapes[direction].width = OPPONENT_TEST_WIDTH;
		opponent_direction_shapes[direction].height = OPPONENT_TEST_HEIGHT;
		sdgame2shapes[OPPONENT_TEST_LEFT_SHAPE + direction] = &opponent_direction_shapes[direction];
	}
	/* Leave the old default bounds: moved icons must report their actual size
	 * and position rather than using this fixed rectangle. */
	rect_ingame_text2 =
		(struct RECTANGLE){ROUTE_TEST_ICON_X, ROUTE_TEST_ICON_X + OPPONENT_TEST_WIDTH,
						   ROUTE_TEST_DEFAULT_Y, ROUTE_TEST_DEFAULT_Y + OPPONENT_TEST_HEIGHT};
}

static void test_player_route_fits_viewport(void)
{
	static const struct {
		legacy_s16 top;
		legacy_s16 bottom;
		legacy_u16 arrow_height;
		legacy_s16 font_height;
		legacy_s16 icon_y;
		legacy_s16 text_y;
	} cases[] = {{ROUTE_TEST_ROOF_TOP, ROUTE_TEST_COMPACT_BOTTOM, OPPONENT_TEST_HEIGHT,
				  OPPONENT_TEST_FONT_HEIGHT, ROUTE_TEST_ARROW_Y, ROUTE_TEST_TEXT_Y},
				 {OPPONENT_TEST_ROOF_HEIGHT, OPPONENT_TEST_COMPACT_BOTTOM, OPPONENT_TEST_HEIGHT,
				  OPPONENT_TEST_FONT_HEIGHT, ROUTE_TEST_DEFAULT_Y, ROUTE_TEST_DEFAULT_Y},
				 {OPPONENT_TEST_ROOF_HEIGHT, OPPONENT_TEST_STOCK_BOTTOM, OPPONENT_TEST_HEIGHT,
				  OPPONENT_TEST_FONT_HEIGHT, ROUTE_TEST_DEFAULT_Y, ROUTE_TEST_DEFAULT_Y},
				 {OPPONENT_TEST_ROOF_HEIGHT, OPPONENT_TEST_SCREEN_BOTTOM, OPPONENT_TEST_HEIGHT,
				  OPPONENT_TEST_FONT_HEIGHT, ROUTE_TEST_DEFAULT_Y, ROUTE_TEST_DEFAULT_Y},
				 {ROUTE_TEST_ROOF_TOP, ROUTE_TEST_COMPACT_BOTTOM, OPPONENT_TEST_TALL_ARROW_HEIGHT,
				  OPPONENT_TEST_TALL_FONT_HEIGHT, ROUTE_TEST_TALL_ARROW_Y, ROUTE_TEST_TALL_TEXT_Y},
				 {ROUTE_TEST_HIGH_ROOF, OPPONENT_TEST_STOCK_BOTTOM, OPPONENT_TEST_HEIGHT,
				  OPPONENT_TEST_FONT_HEIGHT, ROUTE_TEST_HIGH_ROOF, ROUTE_TEST_HIGH_ROOF}};
	static const legacy_u8 indicators[] = {ROUTE_INDICATOR_LEFT, ROUTE_INDICATOR_RIGHT,
										   ROUTE_INDICATOR_WRONG_WAY};
	for (legacy_u16 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		for (legacy_u16 indicator = 0; indicator < sizeof(indicators) / sizeof(indicators[0]);
			 indicator++) {
			reset_player_route(indicators[indicator], cases[index].top, cases[index].bottom);
			font_glyph_height = cases[index].font_height;
			for (legacy_u16 direction = 0; direction < OPPONENT_TEST_SHAPE_COUNT; direction++) {
				opponent_direction_shapes[direction].height = cases[index].arrow_height;
			}
			struct RECTANGLE bounds = *draw_ingame_text();
			assert(bounds.top >= cases[index].top && bounds.bottom <= cases[index].bottom);
			if (indicators[indicator] == ROUTE_INDICATOR_WRONG_WAY) {
				assert(text_draw_count == 1 && opponent_icon_draw.count == 0);
				assert_text(0, "Wrong Way", ROUTE_TEST_TEXT_X, cases[index].text_y);
				assert(bounds.left == ROUTE_TEST_TEXT_X && bounds.right == ROUTE_TEST_TEXT_RIGHT);
				assert(bounds.top == cases[index].text_y);
				assert(bounds.bottom == cases[index].text_y + cases[index].font_height +
											OPPONENT_TEST_TEXT_SHADOW_HEIGHT);
			} else {
				assert(text_draw_count == 0 && opponent_icon_draw.count == 1);
				assert(opponent_icon_draw.shape == &opponent_direction_shapes[indicator]);
				assert(opponent_icon_draw.x == ROUTE_TEST_ICON_X &&
					   opponent_icon_draw.y == cases[index].icon_y);
				assert(bounds.left == ROUTE_TEST_ICON_X &&
					   bounds.right == ROUTE_TEST_ICON_X + OPPONENT_TEST_WIDTH);
				assert(bounds.top == cases[index].icon_y &&
					   bounds.bottom == cases[index].icon_y + cases[index].arrow_height);
			}
			state.game_player_route_indicator = ROUTE_INDICATOR_NONE;
			text_draw_count = opponent_icon_draw.count = 0;
			assert(memcmp(draw_ingame_text(), &empty_rect, sizeof(bounds)) == 0);
			assert(text_draw_count == 0 && opponent_icon_draw.count == 0);
		}
	}
}

static void test_route_information_moves_together(void)
{
	static const legacy_u8 players[] = {ROUTE_INDICATOR_LEFT, ROUTE_INDICATOR_RIGHT,
										ROUTE_INDICATOR_WRONG_WAY};
	for (legacy_u16 player = 0; player < sizeof(players) / sizeof(players[0]); player++) {
		reset_player_route(players[player], ROUTE_TEST_ROOF_TOP, ROUTE_TEST_COMPACT_BOTTOM);
		show_penalty_counter = 1;
		penalty_time = ROUTE_TEST_PENALTY_FRAMES;
		struct RECTANGLE bounds = *draw_ingame_text();
		assert(bounds.top == ROUTE_TEST_GROUP_Y && bounds.bottom == ROUTE_TEST_COMPACT_BOTTOM);
		if (players[player] == ROUTE_INDICATOR_WRONG_WAY) {
			assert(text_draw_count == 2 && opponent_icon_draw.count == 0);
			assert_text(0, "Wrong Way", ROUTE_TEST_TEXT_X, ROUTE_TEST_GROUP_Y);
		} else {
			assert(text_draw_count == 1 && opponent_icon_draw.count == 1);
			assert(opponent_icon_draw.y == ROUTE_TEST_GROUP_Y);
		}
		assert(strcmp(text_draws[text_draw_count - 1].text, "Penalty: 2.00") == 0);
		assert(text_draws[text_draw_count - 1].y == ROUTE_TEST_TEXT_Y);
	}
	for (legacy_u8 penalty = 0; penalty <= 1; penalty++) {
		reset_player_route(ROUTE_INDICATOR_WRONG_WAY, ROUTE_TEST_ROOF_TOP,
						   ROUTE_TEST_COMPACT_BOTTOM);
		state.game_opponent_route_indicator = ROUTE_INDICATOR_LEFT;
		show_penalty_counter = penalty;
		penalty_time = ROUTE_TEST_PENALTY_FRAMES;
		struct RECTANGLE bounds = *draw_ingame_text();
		assert(bounds.top == ROUTE_TEST_OPPONENT_GROUP_Y &&
			   bounds.bottom == ROUTE_TEST_COMPACT_BOTTOM);
		assert_text(0, "Wrong Way", ROUTE_TEST_TEXT_X, ROUTE_TEST_OPPONENT_GROUP_Y);
		assert(opponent_icon_draw.count == 1 && opponent_icon_draw.y == ROUTE_TEST_OPPONENT_ICON_Y);
		assert_text(1, "Opponent Near", OPPONENT_TEST_LABEL_X, ROUTE_TEST_OPPONENT_LABEL_Y);
		assert(text_draw_count == (legacy_u32)(2 + penalty));
		if (penalty != 0) {
			assert(text_draws[2].y == ROUTE_TEST_OPPONENT_PENALTY_Y);
		}
	}
	/* Each notice fits alone here, but the combined layout does not. Never
	 * clip one notice or spill a text shadow into the cached dashboard. */
	reset_player_route(ROUTE_INDICATOR_WRONG_WAY, ROUTE_TEST_HIGH_ROOF, ROUTE_TEST_SMALL_BOTTOM);
	show_penalty_counter = 1;
	assert(memcmp(draw_ingame_text(), &empty_rect, sizeof(empty_rect)) == 0);
	assert(text_draw_count == 0 && opponent_icon_draw.count == 0);
	show_penalty_counter = 0;
	reset_ingame_text("");
}

static void test_player_route_disappearance_preserves_dashboard(void)
{
	static const legacy_u8 indicators[ROUTE_TEST_PHASE_COUNT] = {
		ROUTE_INDICATOR_WRONG_WAY, ROUTE_INDICATOR_NONE,  ROUTE_INDICATOR_LEFT,
		ROUTE_INDICATOR_NONE,	   ROUTE_INDICATOR_RIGHT, ROUTE_INDICATOR_NONE};
	const struct RECTANGLE screen = {0, COMPOSITION_TEST_WIDTH, 0, COMPOSITION_TEST_HEIGHT};
	const struct RECTANGLE scene = {0, COMPOSITION_TEST_WIDTH, ROUTE_TEST_ROOF_TOP,
									ROUTE_TEST_COMPACT_BOTTOM};
	for (legacy_u8 flipping = 0; flipping <= 1; flipping++) {
		reset_player_route(ROUTE_INDICATOR_NONE, scene.top, scene.bottom);
		video_uses_page_flipping = flipping;
		legacy_u16 pages = flipping != 0 ? ROUTE_TEST_PAGE_COUNT : COMPOSITION_TEST_SINGLE_PAGE;
		memset(&composition, 0, sizeof(composition));
		composition.active = 1;
		/* The static cockpit is seeded once, matching cached redraws. */
		for (legacy_u8 target = 0; target < COMPOSITION_TEST_BUFFER_COUNT; target++) {
			composition.target = target;
			composition_fill(&screen, COMPOSITION_TEST_DASHBOARD_COLOR);
		}
		for (legacy_u16 phase = 0; phase < ROUTE_TEST_PHASE_COUNT; phase++) {
			state.game_player_route_indicator = indicators[phase];
			for (legacy_u16 page = 0; page < pages; page++) {
				legacy_u8 target =
					flipping != 0 ? COMPOSITION_TEST_FIRST_PAGE + page : COMPOSITION_TEST_RENDER;
				frame_buffer_index = dashboard_buffer_index = (legacy_s8)page;
				composition.target = target;
				composition_fill(&scene, COMPOSITION_TEST_SCENE_COLOR);
				text_draw_count = opponent_icon_draw.count = 0;
				struct RECTANGLE bounds = *draw_ingame_text();
				legacy_u32 drawn = 0;
				for (legacy_s16 y = scene.top; y < scene.bottom; y++) {
					for (legacy_s16 x = scene.left; x < scene.right; x++) {
						drawn += composition.pixels[target][y][x] == dialog_fnt_colour;
					}
				}
				if (indicators[phase] == ROUTE_INDICATOR_NONE) {
					assert(drawn == 0 && memcmp(&bounds, &empty_rect, sizeof(bounds)) == 0);
				} else {
					legacy_u32 expected =
						indicators[phase] == ROUTE_INDICATOR_WRONG_WAY
							? (ROUTE_TEST_TEXT_RIGHT - ROUTE_TEST_TEXT_X) *
								  (OPPONENT_TEST_FONT_HEIGHT + OPPONENT_TEST_TEXT_SHADOW_HEIGHT)
							: OPPONENT_TEST_WIDTH * OPPONENT_TEST_HEIGHT;
					assert(drawn == expected);
					assert(bounds.top >= scene.top && bounds.bottom <= scene.bottom);
				}
				if (flipping == 0) {
					for (legacy_s16 y = scene.top; y < scene.bottom; y++) {
						memcpy(composition.pixels[COMPOSITION_TEST_FIRST_PAGE][y],
							   composition.pixels[COMPOSITION_TEST_RENDER][y],
							   COMPOSITION_TEST_WIDTH);
					}
				}
				for (legacy_u8 buffer = 0; buffer < COMPOSITION_TEST_BUFFER_COUNT; buffer++) {
					for (legacy_s16 y = scene.bottom; y < COMPOSITION_TEST_HEIGHT; y++) {
						for (legacy_s16 x = 0; x < COMPOSITION_TEST_WIDTH; x++) {
							assert(composition.pixels[buffer][y][x] ==
								   COMPOSITION_TEST_DASHBOARD_COLOR);
						}
					}
				}
			}
		}
		composition.active = 0;
	}
	reset_ingame_text("");
}

static void test_fps_sampling(void)
{
	reset_ingame_text("");
	fps_display_enabled = FRAME_FPS_DISPLAY_ON;
	frame_fps_record_presented();
	assert_fps("0 FPS", 4);
	present_frames(20, 100);
	assert_fps("20 FPS", 2);
	/* More than twenty and fractional rates retain floor semantics. */
	present_frames(24, 100);
	assert_fps("24 FPS", 2);
	present_frames(19, 100);
	assert_fps("19 FPS", 4);
	present_frames(20, 101);
	assert_fps("19 FPS", 4);
	/* Rendering without presenting must not change the sample. */
	for (legacy_u32 frame = 0; frame < 50; frame++) {
		assert_fps("19 FPS", 4);
	}
	present_frames(1, 300);
	assert_fps("0 FPS", 4);
	present_frames(20, 100);
	assert_fps("20 FPS", 2);
	fps_display_enabled = FRAME_FPS_DISPLAY_OFF;
	text_draw_count = 0;
	game_replay_mode = REPLAY_MODE_PAUSED;
	struct RECTANGLE *bounds = draw_complete_ingame_text();
	assert(text_draw_count == 0);
	assert(memcmp(bounds, &empty_rect, sizeof(*bounds)) == 0);
	frame_fps_record_presented();
	fps_display_enabled = FRAME_FPS_DISPLAY_ON;
	frame_fps_record_presented();
	assert_fps("0 FPS", 4);
	present_frames(20, 100);
	assert_fps("20 FPS", 2);
	/* Unsigned time differences continue across a 32-bit tick wrap. */
	frame_fps_reset();
	realtime_ticks = LEGACY_U32_MAX - 49UL;
	frame_fps_record_presented();
	present_frames(20, 100);
	assert_fps("20 FPS", 2);
}

static void test_fps_outside_race(void)
{
	reset_ingame_text("REPLAY");
	/* Intro rendering must ignore cockpit and replay state left by a previous race. */
	dashboard_visible = 1;
	roofbmpheight_copy = 20;
	rect_ingame_text = (struct RECTANGLE){100, 200, 150, 190};
	struct RECTANGLE saved_text_rect = rect_ingame_text;
	struct RECTANGLE *bounds = frame_fps_draw_text();
	assert(memcmp(bounds, &empty_rect, sizeof(*bounds)) == 0);
	assert(text_draw_count == 0);
	fps_display_enabled = FRAME_FPS_DISPLAY_ON;
	frame_fps_record_presented();
	present_frames(20, DOS_TIMER_REALTIME_TICKS_PER_SECOND);
	bounds = frame_fps_draw_text();
	assert(text_draw_count == DIAGNOSTIC_TEST_LINE_COUNT);
	assert(strcmp(text_draws[0].text, "20 FPS") == 0);
	assert(text_draws[0].x == 8 && text_draws[0].y == 3);
	assert(text_draws[0].color == 2 && text_draws[0].shadow_color == 0);
	assert(bounds->left == 8 && bounds->right == 57);
	assert(bounds->top == DIAGNOSTIC_TEST_TOP && bounds->bottom == DIAGNOSTIC_TEST_BOTTOM);
	assert(restored_roof_count == 0 && copied_roof_count == 0);
	assert(memcmp(&rect_ingame_text, &saved_text_rect, sizeof(rect_ingame_text)) == 0);
}

static void test_fps_idle_expiry(void)
{
	reset_ingame_text("");
	fps_display_enabled = FRAME_FPS_DISPLAY_ON;
	frame_fps_record_presented();
	present_frames(20, 100);
	assert_fps("20 FPS", 2);
	/* Expiry is measured from the latest presentation, not the sample window. */
	present_frames(10, 50);
	realtime_ticks += 99;
	assert(frame_fps_expire_idle() == 0);
	assert_fps("20 FPS", 2);
	realtime_ticks++;
	assert(frame_fps_expire_idle() == 1);
	assert_fps("0 FPS", 4);
	assert(frame_fps_expire_idle() == 0);
	frame_fps_record_presented();
	realtime_ticks += 200;
	assert(frame_fps_expire_idle() == 0);
	assert_fps("0 FPS", 4);
	/* Resuming rendering starts a fresh sample and can expire again. */
	frame_fps_reset();
	realtime_ticks = LEGACY_U32_MAX - 149UL;
	frame_fps_record_presented();
	present_frames(20, 100);
	assert_fps("20 FPS", 2);
	realtime_ticks = LEGACY_U32_WRAP_ADD(realtime_ticks, 100);
	fps_display_enabled = FRAME_FPS_DISPLAY_OFF;
	assert(frame_fps_expire_idle() == 0);
	fps_display_enabled = FRAME_FPS_DISPLAY_ON;
	assert(frame_fps_expire_idle() == 1);
	assert_fps("0 FPS", 4);
}

static void test_fps_camera_modes(void)
{
	for (legacy_u8 mode = REPLAY_MODE_LIVE; mode <= REPLAY_MODE_PAUSED; mode++) {
		for (legacy_u16 camera = CAMERA_MODE_COCKPIT; camera < CAMERA_MODE_COUNT; camera++) {
			reset_ingame_text("TEST");
			fps_display_enabled = FRAME_FPS_DISPLAY_ON;
			game_replay_mode = mode;
			cameramode = camera;
			state.game_inputmode = GAME_INPUT_MODE_ACTIVE;
			followOpponentFlag = 1;
			assert_fps("0 FPS", 4);
		}
	}
}

static void test_fps_and_long_replay_filename(void)
{
	legacy_char filename[REPLAY_FILENAME_SIZE];
	memset(filename, 'R', sizeof(filename) - 1);
	filename[sizeof(filename) - 1] = 0;
	reset_ingame_text(filename);
	fps_display_enabled = FRAME_FPS_DISPLAY_ON;
	frame_fps_record_presented();
	present_frames(20, 100);
	assert_fps("20 FPS", 2);
	assert(text_draw_count == DIAGNOSTIC_TEST_FILENAME_LINES + 1U + DIAGNOSTIC_TEST_LINE_COUNT);
	assert(text_draws[0].y == DIAGNOSTIC_TEST_TOP);
	const struct TEXT_DRAW *fps = &text_draws[diagnostic_text_start];
	assert(text_draws[0].x > fps->x + (legacy_s16)strlen(fps->text) * DIAGNOSTIC_TEST_FONT_WIDTH);
#ifdef RESTUNTS_SDL3
	const struct TEXT_DRAW *timing = fps + 1;
	assert(text_draws[1].y == timing->y);
	assert(text_draws[1].x >
		   timing->x + (legacy_s16)strlen(timing->text) * DIAGNOSTIC_TEST_FONT_WIDTH);
#endif
	legacy_char reconstructed[REPLAY_FILENAME_SIZE];
	legacy_u32 copied = 0;
	for (legacy_u32 line = 0; line + 1U < diagnostic_text_start; line++) {
		legacy_u32 length = strlen(text_draws[line].text);
		memcpy(reconstructed + copied, text_draws[line].text, length);
		copied += length;
	}
	reconstructed[copied] = 0;
	assert(strcmp(reconstructed, filename) == 0);
	assert_text(diagnostic_text_start - 1U, "Replay", 264, DIAGNOSTIC_TEST_FILENAME_LABEL_Y);
}

static void test_fps_on_cockpit_roof(void)
{
	for (legacy_s16 roof_height = 0; roof_height <= COMPOSITION_TEST_ROOF_HEIGHT; roof_height++) {
		for (legacy_s16 page_flipping = 0; page_flipping <= 1; page_flipping++) {
			reset_ingame_text("");
			fps_display_enabled = FRAME_FPS_DISPLAY_ON;
			dashboard_visible = 1;
			roofbmpheight_copy = roof_height;
			video_uses_page_flipping = page_flipping;
			sprite_set_target_clip_bounds(0, 320, roof_height, 200);
			struct SPRITE original_context[SPRITE_STATE_COUNT];
			sprite_save_context(original_context);
			assert_fps("0 FPS", 4);
			assert(memcmp(&drawing_sprite, original_context, sizeof(drawing_sprite)) == 0);
			assert(memcmp(&screen_sprite, original_context + 1, sizeof(screen_sprite)) == 0);
			frame_fps_present_roof();
			assert(memcmp(&drawing_sprite, original_context, sizeof(drawing_sprite)) == 0);
			assert(memcmp(&screen_sprite, original_context + 1, sizeof(screen_sprite)) == 0);
			assert(restored_roof_count == (legacy_u32)(roof_height > 3));
			assert(copied_roof_count == (legacy_u32)(roof_height > 3 && page_flipping == 0));
			if (roof_height > 3) {
				assert(restored_roof_bounds.left == DIAGNOSTIC_TEST_LEFT &&
					   restored_roof_bounds.right == DIAGNOSTIC_TEST_RIGHT);
				assert(restored_roof_bounds.top == 3);
				assert(
					restored_roof_bounds.bottom ==
					(roof_height < DIAGNOSTIC_TEST_BOTTOM ? roof_height : DIAGNOSTIC_TEST_BOTTOM));
				if (page_flipping == 0) {
					assert(memcmp(&copied_roof_bounds, &restored_roof_bounds,
								  sizeof(copied_roof_bounds)) == 0);
				}
			}
			/* Hidden dashboards and disabled FPS never touch the roof. */
			dashboard_visible = 0;
			assert_fps("0 FPS", 4);
			frame_fps_present_roof();
			assert(restored_roof_count == (legacy_u32)(roof_height > 3));
			assert(copied_roof_count == (legacy_u32)(roof_height > 3 && page_flipping == 0));
		}
	}
}

#if defined(RESTUNTS_SDL3)
enum {
	SUPERSIGHT_TEST_SLOW_FPS = 59,
	SUPERSIGHT_TEST_TARGET_FPS = 60,
	SUPERSIGHT_TEST_RED = 4,
	SUPERSIGHT_TEST_GREEN = 2
};

static void test_renderer_fps_target(void)
{
	reset_ingame_text("");
	game_replay_mode = REPLAY_MODE_PAUSED;
	fps_display_enabled = FRAME_FPS_DISPLAY_ON;
	renderer_active = 1;
	frame_fps_record_presented();
	assert_fps("0 FPS", SUPERSIGHT_TEST_RED);
	assert(text_draw_count == DIAGNOSTIC_TEST_LINE_COUNT);
	present_frames(SUPERSIGHT_TEST_SLOW_FPS, DOS_TIMER_REALTIME_TICKS_PER_SECOND);
	assert_fps("59 FPS", SUPERSIGHT_TEST_RED);
	assert(text_draw_count == DIAGNOSTIC_TEST_LINE_COUNT);
	present_frames(SUPERSIGHT_TEST_TARGET_FPS, DOS_TIMER_REALTIME_TICKS_PER_SECOND);
	assert_fps("60 FPS", SUPERSIGHT_TEST_GREEN);
	assert(text_draw_count == DIAGNOSTIC_TEST_LINE_COUNT);
}
#endif

enum {
	STATUS_TEST_DURATION = 2U * DOS_TIMER_REALTIME_TICKS_PER_SECOND,
	STATUS_TEST_LEFT = 8,
#ifdef RESTUNTS_SDL3
	STATUS_TEST_TOP = 27,
#else
	STATUS_TEST_TOP = 15,
#endif
	STATUS_TEST_BOTTOM = STATUS_TEST_TOP + 9,
	STATUS_TEST_FONT_WIDTH = 8,
	STATUS_TEST_RIGHT =
		STATUS_TEST_LEFT + (sizeof(STATUS_TEST_PREFIX "Medium") - 1U) * STATUS_TEST_FONT_WIDTH + 1U,
	STATUS_TEST_PAGE_COUNT = 2
};

static void reset_status_text(void)
{
	/* Drain any prior status without giving F11's sampling reset another role. */
	realtime_ticks = LEGACY_U32_WRAP_ADD(realtime_ticks, STATUS_TEST_DURATION);
	fps_display_enabled = FRAME_FPS_DISPLAY_OFF;
	frame_fps_expire_idle();
	text_draw_count = 0;
	frame_fps_draw_text();
	frame_fps_draw_text();
	reset_ingame_text("");
	game_replay_mode = REPLAY_MODE_PAUSED;
}

static void assert_status_bounds(const struct RECTANGLE *bounds)
{
	assert(bounds->left == STATUS_TEST_LEFT && bounds->right == STATUS_TEST_RIGHT);
	assert(bounds->top == STATUS_TEST_TOP && bounds->bottom == STATUS_TEST_BOTTOM);
}

static void test_renderer_status_names_and_copy(void)
{
	static const struct {
		const legacy_s8 *name;
		const legacy_char *text;
	} cases[] = {
		{"On", STATUS_TEST_PREFIX "On"},	 {"Auto", STATUS_TEST_PREFIX "Auto"},
		{"Off", STATUS_TEST_PREFIX "Off"},	 {"Full", STATUS_TEST_PREFIX "Full"},
		{"High", STATUS_TEST_PREFIX "High"}, {"Medium", STATUS_TEST_PREFIX "Medium"},
		{"Low", STATUS_TEST_PREFIX "Low"},
	};
	for (legacy_u32 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		reset_status_text();
		assert(frame_display_overlay_active() == 0);
		frame_renderer_show_status(cases[index].name);
		assert(frame_display_overlay_active() != 0);
		struct RECTANGLE saved_ingame_bounds = rect_ingame_text;
		struct RECTANGLE *bounds = frame_fps_draw_text();
		assert(text_draw_count == 1);
		assert_text(0, cases[index].text, STATUS_TEST_LEFT, STATUS_TEST_TOP);
		assert_status_bounds(bounds);
		assert(fps_display_enabled == 0);
		assert(restored_roof_count == 0 && copied_roof_count == 0);
		assert(memcmp(&rect_ingame_text, &saved_ingame_bounds, sizeof(rect_ingame_text)) == 0);
	}
	reset_status_text();
	legacy_s8 name[] = "Medium trailing text";
	frame_renderer_show_status(name);
	memset(name, 'X', sizeof(name) - 1U);
	frame_fps_reset();
	frame_fps_record_presented();
	frame_fps_draw_text();
	assert_text(0, STATUS_TEST_PREFIX "Medium", STATUS_TEST_LEFT, STATUS_TEST_TOP);
}

static void test_renderer_status_expiry_and_replacement(void)
{
	reset_status_text();
	realtime_ticks = LEGACY_U32_MAX - DOS_TIMER_REALTIME_TICKS_PER_SECOND + 1U;
	frame_renderer_show_status("Auto");
	frame_fps_draw_text();
	realtime_ticks = LEGACY_U32_WRAP_ADD(realtime_ticks, STATUS_TEST_DURATION - 1U);
	assert(frame_fps_expire_idle() == 0);
	text_draw_count = 0;
	fps_display_enabled = FRAME_FPS_DISPLAY_ON;
	frame_fps_reset();
	frame_fps_draw_text();
	assert(text_draw_count == DIAGNOSTIC_TEST_LINE_COUNT + 1U);
	assert(strcmp(text_draws[0].text, "0 FPS") == 0);
	assert_text(DIAGNOSTIC_TEST_LINE_COUNT, STATUS_TEST_PREFIX "Auto", STATUS_TEST_LEFT,
				STATUS_TEST_TOP);
	fps_display_enabled = FRAME_FPS_DISPLAY_OFF;
	realtime_ticks = LEGACY_U32_WRAP_ADD(realtime_ticks, 1U);
	/* Querying before the waiting loop must retain its repaint request. */
	assert(frame_display_overlay_active() != 0);
	assert(frame_fps_expire_idle() == 1);
	assert(frame_fps_expire_idle() == 1);
	text_draw_count = 0;
	assert_status_bounds(frame_fps_draw_text());
	assert(text_draw_count == 0);
	assert(frame_display_overlay_active() == 0);
	assert(frame_fps_expire_idle() == 0);
	assert(memcmp(frame_fps_draw_text(), &empty_rect, sizeof(empty_rect)) == 0);

	frame_renderer_show_status("Medium");
	frame_fps_draw_text();
	realtime_ticks = LEGACY_U32_WRAP_ADD(realtime_ticks, STATUS_TEST_DURATION - 1U);
	frame_renderer_show_status("Low");
	realtime_ticks = LEGACY_U32_WRAP_ADD(realtime_ticks, STATUS_TEST_DURATION - 1U);
	assert(frame_fps_expire_idle() == 0);
	text_draw_count = 0;
	assert_status_bounds(frame_fps_draw_text());
	assert(text_draw_count == 1);
	assert_text(0, STATUS_TEST_PREFIX "Low", STATUS_TEST_LEFT, STATUS_TEST_TOP);
	realtime_ticks = LEGACY_U32_WRAP_ADD(realtime_ticks, 1U);
	assert(frame_fps_expire_idle() == 1);
	text_draw_count = 0;
	assert_status_bounds(draw_complete_ingame_text());
	assert(text_draw_count == 0 && fps_display_enabled == 0);
	assert(frame_fps_expire_idle() == 0);
}

static void test_renderer_status_roof_cleanup(void)
{
	const legacy_s16 roof_heights[] = {0, STATUS_TEST_TOP, STATUS_TEST_TOP + 3,
									   STATUS_TEST_BOTTOM + 3};
	for (legacy_u32 roof = 0; roof < sizeof(roof_heights) / sizeof(roof_heights[0]); roof++) {
		for (legacy_u8 flipping = 0; flipping <= 1; flipping++) {
			for (legacy_u8 fps = FRAME_FPS_DISPLAY_OFF; fps < FRAME_FPS_DISPLAY_MODE_COUNT; fps++) {
				legacy_u32 diagnostic_lines =
					fps == FRAME_FPS_DISPLAY_ON ? DIAGNOSTIC_TEST_LINE_COUNT : 0;
				reset_status_text();
				game_replay_mode = REPLAY_MODE_LIVE;
				state.game_inputmode = GAME_INPUT_MODE_ACTIVE;
				fps_display_enabled = fps;
				dashboard_visible = 1;
				roofbmpheight_copy = roof_heights[roof];
				video_uses_page_flipping = flipping;
				sprite_set_target_clip_bounds(0, 320, roofbmpheight_copy, 200);
				struct SPRITE saved_context[SPRITE_STATE_COUNT];
				sprite_save_context(saved_context);
				frame_renderer_show_status("Medium");
				draw_complete_ingame_text();
				assert(text_draw_count == diagnostic_lines + 1U);
				assert_text(diagnostic_lines, STATUS_TEST_PREFIX "Medium", STATUS_TEST_LEFT,
							STATUS_TEST_TOP);
				legacy_s16 top = fps != 0 ? 3 : STATUS_TEST_TOP;
				legacy_u32 restored = roofbmpheight_copy > top;
				assert(restored_roof_count == restored);
				frame_fps_present_roof();
				assert(copied_roof_count == (restored && flipping == 0));
				if (restored != 0) {
					assert(restored_roof_bounds.left == STATUS_TEST_LEFT);
					assert(restored_roof_bounds.right == STATUS_TEST_RIGHT);
					assert(restored_roof_bounds.top == top);
					assert(restored_roof_bounds.bottom == (roofbmpheight_copy < STATUS_TEST_BOTTOM
															   ? roofbmpheight_copy
															   : STATUS_TEST_BOTTOM));
				}
				/* A shorter replacement still restores the full old text region. */
				frame_renderer_show_status("Off");
				text_draw_count = 0;
				draw_complete_ingame_text();
				frame_fps_present_roof();
				assert_text(diagnostic_lines, STATUS_TEST_PREFIX "Off", STATUS_TEST_LEFT,
							STATUS_TEST_TOP);
				assert(restored_roof_count == restored * 2U);
				realtime_ticks = LEGACY_U32_WRAP_ADD(realtime_ticks, STATUS_TEST_DURATION);
				assert(frame_fps_expire_idle() == 1);
				text_draw_count = 0;
				struct RECTANGLE *bounds = draw_complete_ingame_text();
				assert(text_draw_count == diagnostic_lines);
				assert(bounds->right == STATUS_TEST_RIGHT && bounds->bottom == STATUS_TEST_BOTTOM);
				assert(restored_roof_count == restored * 3U);
				frame_fps_present_roof();
				assert(copied_roof_count == (flipping == 0 ? restored * 3U : 0));
				/* Presenting twice must not copy stale bounds a second time. */
				frame_fps_present_roof();
				assert(copied_roof_count == (flipping == 0 ? restored * 3U : 0));
				assert(memcmp(&drawing_sprite, saved_context, sizeof(drawing_sprite)) == 0);
				assert(memcmp(&screen_sprite, saved_context + 1, sizeof(screen_sprite)) == 0);
				/* Both page buffers are cleaned before status-only visibility ends. */
				if (flipping != 0) {
					text_draw_count = 0;
					draw_complete_ingame_text();
					frame_fps_present_roof();
				}
				assert(frame_display_overlay_active() == fps);
				assert(fps_display_enabled == fps);
			}
		}
	}
}

static void test_renderer_status_paused_page_cleanup(void)
{
	reset_status_text();
	video_uses_page_flipping = 1;
	dashboard_visible = 1;
	roofbmpheight_copy = STATUS_TEST_BOTTOM;
	frame_renderer_show_status("Low");
	assert_status_bounds(draw_complete_ingame_text());
	frame_fps_present_roof();
	realtime_ticks = LEGACY_U32_WRAP_ADD(realtime_ticks, STATUS_TEST_DURATION);
	for (legacy_u16 page = 0; page < STATUS_TEST_PAGE_COUNT; page++) {
		/* Each paused page needs its own repaint, even after expiry is consumed. */
		assert(frame_fps_expire_idle() == 1);
		assert(frame_display_overlay_active() != 0);
		text_draw_count = 0;
		assert_status_bounds(draw_complete_ingame_text());
		assert(text_draw_count == 0);
		frame_fps_present_roof();
	}
	assert(restored_roof_count == STATUS_TEST_PAGE_COUNT + 1U && copied_roof_count == 0);
	assert(fps_display_enabled == 0);
	assert(frame_fps_expire_idle() == 0);
	assert(frame_display_overlay_active() == 0);
}

static void test_diagnostic_text_after_dashboard(void)
{
	const struct RECTANGLE dashboard_bounds = {0, COMPOSITION_TEST_WIDTH, 0,
											   COMPOSITION_TEST_HEIGHT};
	const legacy_s16 roof_heights[] = {0, COMPOSITION_TEST_ROOF_HEIGHT};
	for (legacy_u8 flipping = 0; flipping <= 1; flipping++) {
		for (legacy_u16 roof = 0; roof < sizeof(roof_heights) / sizeof(roof_heights[0]); roof++) {
			reset_status_text();
			fps_display_enabled = FRAME_FPS_DISPLAY_ON;
			dashboard_visible = 1;
			roofbmpheight_copy = roof_heights[roof];
			video_uses_page_flipping = flipping;
			memset(&composition, 0, sizeof(composition));
			composition.active = 1;
			legacy_u16 pages =
				flipping != 0 ? STATUS_TEST_PAGE_COUNT : COMPOSITION_TEST_SINGLE_PAGE;
			for (legacy_u16 phase = COMPOSITION_TEST_LONG_STATUS;
				 phase < COMPOSITION_TEST_PHASE_COUNT; phase++) {
				if (phase == COMPOSITION_TEST_LONG_STATUS) {
					frame_renderer_show_status("Medium");
				} else if (phase == COMPOSITION_TEST_SHORT_STATUS) {
					frame_renderer_show_status("Off");
				} else {
					realtime_ticks = LEGACY_U32_WRAP_ADD(realtime_ticks, STATUS_TEST_DURATION);
				}
				for (legacy_u16 page = 0; page < pages; page++) {
					legacy_u8 target = flipping != 0 ? COMPOSITION_TEST_FIRST_PAGE + page
													 : COMPOSITION_TEST_RENDER;
					composition.target = target;
					const struct RECTANGLE scene_bounds = {
						0, COMPOSITION_TEST_WIDTH, roofbmpheight_copy, COMPOSITION_TEST_HEIGHT};
					composition_fill(&scene_bounds, COMPOSITION_TEST_SCENE_COLOR);
					text_draw_count = 0;
					struct RECTANGLE bounds = *draw_ingame_text();
					assert(text_draw_count == 0);
					assert(bounds.left == STATUS_TEST_LEFT && bounds.right == STATUS_TEST_RIGHT);
					assert(bounds.top == COMPOSITION_TEST_FPS_TOP &&
						   bounds.bottom == STATUS_TEST_BOTTOM);
					if (phase == COMPOSITION_TEST_EXPIRED_STATUS) {
						/* Preparation must leave each pending page cleanup for the final paint. */
						assert(frame_fps_expire_idle() != 0);
					}
					/* A custom dashboard can cover every diagnostic row, including the roof. */
					composition_fill(&dashboard_bounds, COMPOSITION_TEST_DASHBOARD_COLOR);
					if (flipping == 0) {
						for (legacy_s16 y = scene_bounds.top; y < scene_bounds.bottom; y++) {
							memcpy(composition.pixels[COMPOSITION_TEST_FIRST_PAGE][y],
								   composition.pixels[COMPOSITION_TEST_RENDER][y],
								   COMPOSITION_TEST_WIDTH);
						}
						composition.target = COMPOSITION_TEST_FIRST_PAGE;
					}
					frame_fps_present_roof();
					frame_fps_draw_text();
					assert(text_draw_count ==
						   DIAGNOSTIC_TEST_LINE_COUNT + (phase != COMPOSITION_TEST_EXPIRED_STATUS));
					assert(strcmp(text_draws[0].text, "0 FPS") == 0);
					assert(composition.pixels[composition.target][COMPOSITION_TEST_FPS_TOP]
											 [STATUS_TEST_LEFT] == text_draws[0].color);
#ifdef RESTUNTS_SDL3
					legacy_s16 timing_y = DIAGNOSTIC_TEST_TOP + DIAGNOSTIC_TEST_LINE_HEIGHT;
					assert_text(1, "0.0ms", DIAGNOSTIC_TEST_LEFT, timing_y);
					assert(composition.pixels[composition.target][timing_y][DIAGNOSTIC_TEST_LEFT] ==
						   dialog_fnt_colour);
#endif
					assert(
						composition.pixels[composition.target][STATUS_TEST_TOP][STATUS_TEST_LEFT] ==
						(phase == COMPOSITION_TEST_EXPIRED_STATUS ? COMPOSITION_TEST_DASHBOARD_COLOR
																  : dialog_fnt_colour));
					assert(
						composition
							.pixels[composition.target][STATUS_TEST_TOP][STATUS_TEST_RIGHT - 1U] ==
						(phase == COMPOSITION_TEST_LONG_STATUS ? dialog_fnt_colour
															   : COMPOSITION_TEST_DASHBOARD_COLOR));
				}
			}
			assert(frame_fps_expire_idle() == 0);
			composition.active = 0;
		}
	}
	reset_status_text();
}

static void test_renderer_status_replay_filename_collision(void)
{
	legacy_char filename[REPLAY_FILENAME_SIZE];
	memset(filename, 'R', sizeof(filename) - 1U);
	filename[sizeof(filename) - 1U] = 0;
	for (legacy_u8 fps = FRAME_FPS_DISPLAY_OFF; fps < FRAME_FPS_DISPLAY_MODE_COUNT; fps++) {
		reset_status_text();
		reset_ingame_text(filename);
		fps_display_enabled = fps;
		legacy_u32 diagnostic_lines = fps == FRAME_FPS_DISPLAY_ON ? DIAGNOSTIC_TEST_LINE_COUNT : 0;
		frame_renderer_show_status("Medium");
		draw_complete_ingame_text();
		assert_text(diagnostic_text_start + diagnostic_lines, STATUS_TEST_PREFIX "Medium",
					STATUS_TEST_LEFT, STATUS_TEST_TOP);
		legacy_char reconstructed[REPLAY_FILENAME_SIZE];
		legacy_u32 copied = 0;
		for (legacy_u32 index = 0; index + 1U < diagnostic_text_start; index++) {
			legacy_u32 length = strlen(text_draws[index].text);
			memcpy(reconstructed + copied, text_draws[index].text, length);
			copied += length;
		}
		reconstructed[copied] = 0;
		assert(strcmp(reconstructed, filename) == 0);
		for (legacy_u32 first = 0; first < text_draw_count; first++) {
			for (legacy_u32 second = first + 1U; second < text_draw_count; second++) {
				if (text_draws[first].y == text_draws[second].y) {
					legacy_s16 first_right =
						text_draws[first].x +
						(legacy_s16)strlen(text_draws[first].text) * STATUS_TEST_FONT_WIDTH;
					legacy_s16 second_right =
						text_draws[second].x +
						(legacy_s16)strlen(text_draws[second].text) * STATUS_TEST_FONT_WIDTH;
					assert(first_right < text_draws[second].x ||
						   second_right < text_draws[first].x);
				}
			}
		}
		assert(strcmp(text_draws[diagnostic_text_start - 1U].text, "Replay") == 0);
		assert(strcmp((const legacy_char *)replay_filename, filename) == 0);
	}
	reset_status_text();
}

#ifdef RESTUNTS_SDL3
enum {
	RENDER_TEST_SAMPLE_COUNT = 100,
	RENDER_TEST_LINE_Y = DIAGNOSTIC_TEST_TOP + DIAGNOSTIC_TEST_LINE_HEIGHT,
	RENDER_TEST_LINE_BOTTOM = DIAGNOSTIC_TEST_BOTTOM,
	RENDER_TEST_HALF_SAMPLES = RENDER_TEST_SAMPLE_COUNT / 2
};
#define RENDER_TEST_FIRST_NS 8400000ULL
#define RENDER_TEST_ROUND_NS 8450000ULL
#define RENDER_TEST_BASE_NS 10000000ULL
#define RENDER_TEST_DOUBLE_NS (RENDER_TEST_BASE_NS * 2U)

static void assert_render_timing(const legacy_char *expected)
{
	text_draw_count = 0;
	struct RECTANGLE *bounds = frame_fps_draw_text();
	assert(text_draw_count == 2);
	assert(strcmp(text_draws[0].text, "0 FPS") == 0);
	assert_text(1, expected, STATUS_TEST_LEFT, RENDER_TEST_LINE_Y);
	assert(bounds->bottom == RENDER_TEST_LINE_BOTTOM);
}

static void test_render_timing(void)
{
	reset_status_text();
	frame_render_timing_reset();
	fps_display_enabled = FRAME_FPS_DISPLAY_ON;
	assert_render_timing("0.0ms");
	frame_render_timing_record(RENDER_TEST_FIRST_NS);
	assert_render_timing("8.4ms");
	/* FPS resets and HUD toggles retain actual rendering history. */
	frame_fps_reset();
	assert_render_timing("8.4ms");
	fps_display_enabled = FRAME_FPS_DISPLAY_OFF;
	text_draw_count = 0;
	assert(frame_display_overlay_active() == 0);
	assert(memcmp(frame_fps_draw_text(), &empty_rect, sizeof(empty_rect)) == 0);
	assert(text_draw_count == 0);
	for (legacy_u32 frame = 0; frame < RENDER_TEST_SAMPLE_COUNT; frame++) {
		frame_render_timing_record(RENDER_TEST_BASE_NS);
	}
	fps_display_enabled = FRAME_FPS_DISPLAY_ON;
	assert_render_timing("10.0ms");
	for (legacy_u32 frame = 0; frame < RENDER_TEST_HALF_SAMPLES; frame++) {
		frame_render_timing_record(RENDER_TEST_DOUBLE_NS);
	}
	assert_render_timing("15.0ms");
	for (legacy_u32 frame = 0; frame < RENDER_TEST_HALF_SAMPLES; frame++) {
		frame_render_timing_record(RENDER_TEST_DOUBLE_NS);
	}
	assert_render_timing("20.0ms");
	/* Backend transitions discard old samples, then round to the nearest tenth. */
	frame_render_timing_reset();
	frame_render_timing_record(RENDER_TEST_ROUND_NS);
	assert_render_timing("8.5ms");
	frame_render_timing_reset();
	frame_render_timing_record(~(legacy_u64)0);
	assert_render_timing("65535.0ms");
	frame_render_timing_reset();
	frame_render_timing_record(RENDER_TEST_FIRST_NS);
	dashboard_visible = 1;
	roofbmpheight_copy = STATUS_TEST_BOTTOM;
	frame_renderer_show_status("Medium");
	text_draw_count = 0;
	draw_complete_ingame_text();
	assert(text_draw_count == 3);
	assert_text(1, "8.4ms", STATUS_TEST_LEFT, RENDER_TEST_LINE_Y);
	assert_text(2, STATUS_TEST_PREFIX "Medium", STATUS_TEST_LEFT, STATUS_TEST_TOP);
	assert(restored_roof_bounds.top == 3 && restored_roof_bounds.bottom == STATUS_TEST_BOTTOM);
	realtime_ticks += STATUS_TEST_DURATION;
	text_draw_count = 0;
	draw_complete_ingame_text();
	assert(text_draw_count == 2);
	assert(restored_roof_bounds.bottom == STATUS_TEST_BOTTOM);
	text_draw_count = 0;
	draw_complete_ingame_text();
	assert(restored_roof_bounds.bottom == RENDER_TEST_LINE_BOTTOM);
	reset_status_text();
}
#endif

#define STATUS_ONLY_TEST_FPS 24U
#define STATUS_ONLY_TEST_FPS_TEXT "24 FPS"

static void assert_saved_diagnostics(const legacy_char *expected_fps)
{
	text_draw_count = 0;
	frame_fps_draw_text();
	assert(text_draw_count == DIAGNOSTIC_TEST_LINE_COUNT);
	assert(strcmp(text_draws[0].text, expected_fps) == 0);
	assert(text_draws[0].x == DIAGNOSTIC_TEST_LEFT && text_draws[0].y == DIAGNOSTIC_TEST_TOP);
#ifdef RESTUNTS_SDL3
	assert_text(1, "8.4ms", DIAGNOSTIC_TEST_LEFT, RENDER_TEST_LINE_Y);
#endif
}

static void test_status_only_preserves_diagnostics(void)
{
	for (legacy_u8 flipping = 0; flipping <= 1; flipping++) {
		reset_status_text();
		video_uses_page_flipping = flipping;
		fps_display_enabled = FRAME_FPS_DISPLAY_ON;
		frame_fps_record_presented();
		present_frames(STATUS_ONLY_TEST_FPS, DOS_TIMER_REALTIME_TICKS_PER_SECOND);
#ifdef RESTUNTS_SDL3
		frame_render_timing_reset();
		frame_render_timing_record(RENDER_TEST_FIRST_NS);
#endif
		struct RECTANGLE saved_ingame_bounds = rect_ingame_text;
		/* A static menu may stay open beyond the normal FPS idle timeout.
		 * Status queries, draws and expiry must leave that sample untouched. */
		realtime_ticks = LEGACY_U32_WRAP_ADD(realtime_ticks, STATUS_TEST_DURATION);
		assert(frame_renderer_status_overlay_active() == 0);
		assert(frame_renderer_status_expire_idle() == 0);
		text_draw_count = 0;
		struct RECTANGLE *bounds = frame_renderer_status_draw_text();
		assert(text_draw_count == 0 && memcmp(bounds, &empty_rect, sizeof(*bounds)) == 0);
		assert_saved_diagnostics(STATUS_ONLY_TEST_FPS_TEXT);

		frame_renderer_show_status("Medium");
		assert(frame_renderer_status_overlay_active() != 0 &&
			   frame_renderer_status_expire_idle() == 0);
		text_draw_count = 0;
		bounds = frame_renderer_status_draw_text();
		assert(text_draw_count == 1);
		assert_text(0, STATUS_TEST_PREFIX "Medium", STATUS_TEST_LEFT, STATUS_TEST_TOP);
		assert_status_bounds(bounds);
		assert(fps_display_enabled == FRAME_FPS_DISPLAY_ON);

		realtime_ticks = LEGACY_U32_WRAP_ADD(realtime_ticks, STATUS_TEST_DURATION);
		legacy_u16 pages = flipping != 0 ? STATUS_TEST_PAGE_COUNT : COMPOSITION_TEST_SINGLE_PAGE;
		for (legacy_u16 page = 0; page < pages; page++) {
			/* Visibility checks retain the pending request until that page is
			 * restored, including the second page after the notice disappears. */
			assert(frame_renderer_status_overlay_active() != 0);
			assert(frame_renderer_status_expire_idle() != 0);
			text_draw_count = 0;
			bounds = frame_renderer_status_draw_text();
			assert(text_draw_count == 0);
			assert_status_bounds(bounds);
			assert(fps_display_enabled == FRAME_FPS_DISPLAY_ON);
		}
		assert(frame_renderer_status_overlay_active() == 0 &&
			   frame_renderer_status_expire_idle() == 0);
		text_draw_count = 0;
		bounds = frame_renderer_status_draw_text();
		assert(text_draw_count == 0 && memcmp(bounds, &empty_rect, sizeof(*bounds)) == 0);
		assert_saved_diagnostics(STATUS_ONLY_TEST_FPS_TEXT);
		assert(restored_roof_count == 0 && copied_roof_count == 0);
		assert(memcmp(&rect_ingame_text, &saved_ingame_bounds, sizeof(rect_ingame_text)) == 0);

		/* Supported animated views still draw and expire the preserved FPS;
		 * the independent render-time sample survives the FPS idle reset. */
		assert(frame_display_overlay_active() != 0);
		assert(frame_fps_expire_idle() != 0);
		assert_saved_diagnostics("0 FPS");
		assert(fps_display_enabled == FRAME_FPS_DISPLAY_ON);
	}
	reset_status_text();
}

static legacy_u8 mph_test_pattern(legacy_u8 target, legacy_u16 phase, legacy_s16 x, legacy_s16 y)
{
	return (legacy_u8)(MPH_TEST_BACKGROUND_FIRST +
					   (x + y * MPH_TEST_PATTERN_ROW_MULTIPLIER + target + phase) %
						   MPH_TEST_BACKGROUND_COLORS);
}

static void mph_test_fill_pattern(legacy_u8 target, legacy_u16 phase)
{
	for (legacy_s16 y = 0; y < COMPOSITION_TEST_HEIGHT; y++) {
		for (legacy_s16 x = 0; x < COMPOSITION_TEST_WIDTH; x++) {
			composition.pixels[target][y][x] = mph_test_pattern(target, phase, x, y);
		}
	}
}

static void mph_test_assert_frame(legacy_u8 target, legacy_u16 phase, const legacy_s8 *text,
								  legacy_s16 y)
{
	static legacy_u8 expected[COMPOSITION_TEST_HEIGHT][COMPOSITION_TEST_WIDTH];
	for (legacy_s16 row = 0; row < COMPOSITION_TEST_HEIGHT; row++) {
		for (legacy_s16 column = 0; column < COMPOSITION_TEST_WIDTH; column++) {
			expected[row][column] = mph_test_pattern(target, phase, column, row);
		}
	}
	if (text != 0) {
		legacy_s16 x = REPLAY_TEXT_RIGHT_X - font_text_width(text);
		mph_test_text_pixels(expected, text, x, y, (legacy_u8)dialog_fnt_colour, 0);
	}
	assert(memcmp(composition.pixels[target], expected, sizeof(expected)) == 0);
}

static void mph_test_select_target(legacy_u8 target)
{
	sprite_select_target(&mph_target_sprites[target]);
	assert(composition.target == target);
}

static void test_mph_overlay(void)
{
	enum {
		MPH_TEST_RIGHT = 312,
		MPH_TEST_BOTTOM = 197,
		MPH_TEST_REPLAY_TOP = 151,
		MPH_TEST_REPLAY_TEXT_TOP = 139,
		MPH_TEST_TOP = 188,
		MPH_TEST_WHEEL_SPEED = 7U << LEGACY_BYTE_BITS,
		MPH_TEST_ALLOCATION_WIDTH = 62,
		MPH_TEST_ALLOCATION_HEIGHT = 9,
		MPH_TEST_ALLOCATION_BYTES = 576,
		MPH_TEST_SHORT_FONT_HEIGHT = 6,
		MPH_TEST_REPLAY_SAMPLE = 3
	};
	static const struct {
		legacy_u16 speed;
		const legacy_char *text;
	} samples[] = {{100U << LEGACY_BYTE_BITS, "100 MPH"},
				   {99U << LEGACY_BYTE_BITS, "99 MPH"},
				   {(9U << LEGACY_BYTE_BITS) + LEGACY_U8_MAX, "9 MPH"},
				   {0, "0 MPH"},
				   {LEGACY_U16_MAX, "255 MPH"},
				   {1U << LEGACY_BYTE_BITS, "1 MPH"}};
	for (legacy_u8 flipping = 0; flipping <= 1; flipping++) {
		for (legacy_u8 dashboard = 0; dashboard <= 1; dashboard++) {
			reset_status_text();
			video_uses_page_flipping = flipping;
			video_page_count =
				flipping != 0 ? MPH_TEST_PAGE_CAPACITY : COMPOSITION_TEST_SINGLE_PAGE;
			dashboard_visible = dashboard;
			mph_display_enabled = FRAME_MPH_DISPLAY_ON;
			mph_variable_font = 1;
			mph_capture_count = mph_restore_count = mph_allocation_count = mph_free_count = 0;
			memset(&composition, 0, sizeof(composition));
			composition.active = 1;
			for (legacy_u8 target = 0; target < COMPOSITION_TEST_BUFFER_COUNT; target++) {
				memset(&mph_target_sprites[target], 0, sizeof(mph_target_sprites[target]));
				mph_target_sprites[target].sprite_bitmapptr = &mph_target_shapes[target];
				mph_target_sprites[target].sprite_right = COMPOSITION_TEST_WIDTH;
				mph_target_sprites[target].sprite_bottom = COMPOSITION_TEST_HEIGHT;
				mph_test_fill_pattern(target, 0);
			}
			screen_sprite = mph_target_sprites[COMPOSITION_TEST_FIRST_PAGE];
			mph_test_select_target(COMPOSITION_TEST_RENDER);
			/* A failed reservation must allocate nothing and allow ordinary cleanup. */
			mph_available_bytes = 0;
			assert(frame_mph_initialize() != 0 && mph_allocation_count == 0);
			frame_mph_shutdown();
			mph_available_bytes = MPH_TEST_ALLOCATION_BYTES * video_page_count - 1U;
			assert(frame_mph_initialize() != 0 && mph_allocation_count == 0);
			frame_mph_shutdown();
			mph_available_bytes++;
			assert(frame_mph_initialize() == 0);
			mph_available_bytes = LEGACY_U32_MAX;
			assert(mph_allocation_count == video_page_count);
			for (legacy_u16 page = 0; page < mph_allocation_count; page++) {
				assert(mph_backing[page].width == MPH_TEST_ALLOCATION_WIDTH);
				assert(mph_backing[page].height == MPH_TEST_ALLOCATION_HEIGHT);
			}
			assert(font_text_width((const legacy_s8 *)"100 MPH") == MPH_TEST_ALLOCATION_WIDTH - 1);
			legacy_u16 phases[MPH_TEST_PAGE_CAPACITY] = {0, 0};
			for (legacy_u8 camera = CAMERA_MODE_COCKPIT; camera <= CAMERA_MODE_TRACKSIDE;
				 camera++) {
				cameramode = camera;
				for (legacy_u16 sample = 0; sample < sizeof(samples) / sizeof(samples[0]);
					 sample++) {
					legacy_s16 bottom = sample >= MPH_TEST_REPLAY_SAMPLE ? MPH_TEST_REPLAY_TOP
																		 : COMPOSITION_TEST_HEIGHT;
					legacy_s16 text_y =
						bottom == COMPOSITION_TEST_HEIGHT ? MPH_TEST_TOP : MPH_TEST_REPLAY_TEXT_TOP;
					for (legacy_u16 page = 0; page < video_page_count; page++) {
						frame_buffer_index = (legacy_s8)page;
						legacy_u8 target = COMPOSITION_TEST_FIRST_PAGE + page;
						/* Scene composition currently targets a different bitmap. Restoring
						 * the previous HUD must still find the captured screen/page. */
						mph_test_select_target(COMPOSITION_TEST_RENDER);
						struct SPRITE before_restore[SPRITE_STATE_COUNT] = {drawing_sprite,
																			screen_sprite};
						frame_mph_restore_text();
						assert(memcmp(&drawing_sprite, &before_restore[0],
									  sizeof(drawing_sprite)) == 0);
						assert(memcmp(&screen_sprite, &before_restore[1], sizeof(screen_sprite)) ==
							   0);
						assert(composition.target == COMPOSITION_TEST_RENDER);
						mph_test_assert_frame(target, phases[page], 0, 0);
						/* Emulate a new scene or updated dashboard before capturing the HUD. */
						phases[page]++;
						mph_test_fill_pattern(target, phases[page]);
						mph_test_select_target(target);
						state.playerstate.car_actual_speed = samples[sample].speed;
						state.playerstate.car_rev_speed = MPH_TEST_WHEEL_SPEED;
						text_draw_count = 0;
						struct SPRITE saved_context[SPRITE_STATE_COUNT] = {drawing_sprite,
																		   screen_sprite};
						struct RECTANGLE bounds = *frame_mph_draw_text(bottom);
						assert(text_draw_count == 1);
						legacy_s16 x = MPH_TEST_RIGHT -
									   font_text_width((const legacy_s8 *)samples[sample].text);
						assert_text(0, samples[sample].text, x, text_y);
						assert(bounds.left == x && bounds.right == MPH_TEST_RIGHT + 1);
						assert(bounds.top == text_y &&
							   bounds.bottom == text_y + font_glyph_height + 1);
						/* Every pixel, including spaces, glyph holes, shadows, former leading
						 * digits, and the replay toolbar must match a fresh transparent draw. */
						mph_test_assert_frame(target, phases[page],
											  (const legacy_s8 *)samples[sample].text, text_y);
						mph_test_assert_frame(COMPOSITION_TEST_RENDER, 0, 0, 0);
						assert(memcmp(&drawing_sprite, &saved_context[0], sizeof(drawing_sprite)) ==
							   0);
						assert(memcmp(&screen_sprite, &saved_context[1], sizeof(screen_sprite)) ==
							   0);
					}
				}
			}
			/* Toggling off restores every page, even when no new text is drawn. */
			mph_display_enabled = FRAME_MPH_DISPLAY_OFF;
			legacy_u16 captures = mph_capture_count;
			for (legacy_u16 page = 0; page < video_page_count; page++) {
				frame_buffer_index = (legacy_s8)page;
				mph_test_select_target(COMPOSITION_TEST_RENDER);
				frame_mph_restore_text();
				mph_test_assert_frame(COMPOSITION_TEST_FIRST_PAGE + page, phases[page], 0, 0);
				text_draw_count = 0;
				assert(frame_mph_draw_text(COMPOSITION_TEST_HEIGHT) == &empty_rect);
				assert(text_draw_count == 0 && mph_capture_count == captures);
			}
			assert(mph_capture_count != 0 && mph_restore_count == mph_capture_count);
			assert(mph_clear_count == 0);
			/* Demo presentation cannot capture or paint the field. */
			mph_display_enabled = FRAME_MPH_DISPLAY_ON;
			idle_expired = 1;
			assert(frame_mph_draw_text(COMPOSITION_TEST_HEIGHT) == &empty_rect);
			assert(mph_capture_count == captures);
			idle_expired = 0;
			/* A shorter current capture must not confuse the LIFO window allocator. */
			frame_buffer_index = 0;
			font_glyph_height = MPH_TEST_SHORT_FONT_HEIGHT;
			mph_test_select_target(COMPOSITION_TEST_FIRST_PAGE);
			text_draw_count = 0;
			frame_mph_draw_text(COMPOSITION_TEST_HEIGHT);
			frame_mph_shutdown();
			assert(mph_allocation_count == 0 && mph_free_count == video_page_count);
			composition.active = 0;
			mph_variable_font = 0;
		}
	}
	reset_status_text();
}

legacy_int main(void)
{
	test_mph_overlay();
	test_incremental_crack_overlay();
	test_rejected_crack_lines();
	test_direct_redraw_stack();
	test_explicit_crack_context();
	test_replay_filename_survives_blink();
	test_wrapped_replay_filename();
	test_replay_filename_when_paused();
	test_unnamed_replay_overlay();
	test_filename_hidden_in_live_race_and_demo();
	test_opponent_indicator_fits_viewport();
	test_opponent_indicator_requires_room();
	test_player_route_fits_viewport();
	test_route_information_moves_together();
	test_player_route_disappearance_preserves_dashboard();
	test_fps_sampling();
	test_fps_outside_race();
	test_fps_idle_expiry();
	test_fps_camera_modes();
	test_fps_and_long_replay_filename();
	test_fps_on_cockpit_roof();
#if defined(RESTUNTS_SDL3)
	test_renderer_fps_target();
#endif
	test_renderer_status_names_and_copy();
	test_renderer_status_expiry_and_replacement();
	test_renderer_status_roof_cleanup();
	test_renderer_status_paused_page_cleanup();
	test_diagnostic_text_after_dashboard();
	test_renderer_status_replay_filename_collision();
	test_status_only_preserves_diagnostics();
#ifdef RESTUNTS_SDL3
	test_render_timing();
#endif
	return 0;
}
