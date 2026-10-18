#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../c/ui_dialog.h"
#include "../c/ui_dialog_internal.h"
#include "../c/ui_text.h"
#include "../c/shape2d.h"
#include "../c/timing.h"
#include "../c/game_input.h"
#include "../c/keyboard.h"

legacy_s16 font_glyph_height;
legacy_s16 dialog_fnt_colour;
legacy_s16 dialog_background_color;
legacy_s16 performGraphColor;

static legacy_u64 trace_hash = UINT64_C(1469598103934665603);
static legacy_u16 scripted_keys[16];
static legacy_s16 scripted_hits[16];
static legacy_u32 input_index;
static legacy_u32 timer_calls;
static legacy_s16 save_succeeds;
#ifdef RESTUNTS_AMIGA
static legacy_u16 presented_dialogs;
void amiga_video_present(void)
{
	++presented_dialogs;
}
#endif

#define FIXTURE_INPUT_FIELD_COUNT 2U
#define FIXTURE_INPUT_POSITION_STRIDE 2U
#define FIXTURE_INPUT_POSITION_COUNT (FIXTURE_INPUT_FIELD_COUNT * FIXTURE_INPUT_POSITION_STRIDE)
#define FIXTURE_INPUT_FONT_HEIGHT 8
#define FIXTURE_INPUT_DIALOG_WIDTH 56
#define FIXTURE_INPUT_DIALOG_HEIGHT 20
#define FIXTURE_INPUT_FRAME_PADDING 8
#define FIXTURE_INPUT_OUTLINE_INSET 4
#define FIXTURE_INPUT_CONTENT_PADDING 8
#define FIXTURE_INPUT_CENTERED_X 128
#define FIXTURE_INPUT_CENTERED_Y 90
#define FIXTURE_INPUT_EXPLICIT_X 40U
#define FIXTURE_INPUT_EXPLICIT_Y 120U
#define FIXTURE_INPUT_ANDROID_Y 8
#define FIXTURE_INPUT_FIRST_LINE_OFFSET 1
#define FIXTURE_INPUT_LINE_HEIGHT 10
#define FIXTURE_INPUT_NAME_PREFIX_WIDTH 22
#define FIXTURE_INPUT_PATH_PREFIX_WIDTH 19
#define FIXTURE_INPUT_UNSET_POSITION -1

static legacy_u8 capture_geometry;
static legacy_u32 geometry_pushes, geometry_pops, geometry_clips, geometry_outlines, geometry_lines;
static struct BUTTON_AREA geometry_saved, geometry_clip, geometry_outline;
static legacy_s16 geometry_line_x[FIXTURE_INPUT_FIELD_COUNT];
static legacy_s16 geometry_line_y[FIXTURE_INPUT_FIELD_COUNT];

static void trace_word(legacy_u16 value)
{
	trace_hash = (trace_hash ^ (value & 255U)) * UINT64_C(1099511628211);
	trace_hash = (trace_hash ^ (value >> 8)) * UINT64_C(1099511628211);
}

static void trace_rectangle(legacy_s16 left, legacy_s16 right, legacy_s16 top, legacy_s16 bottom)
{
	trace_word((legacy_u16)left);
	trace_word((legacy_u16)right);
	trace_word((legacy_u16)top);
	trace_word((legacy_u16)bottom);
}

void mouse_draw_opaque_check(void)
{
	trace_word(1);
}
void mouse_draw_transparent_check(void)
{
	trace_word(2);
}
void sprite_pop_background(void)
{
	if (capture_geometry) {
		geometry_pops++;
	}
	trace_word(3);
}
void sprite_select_screen(void)
{
	trace_word(4);
}
void check_input(void)
{
	trace_word(5);
}
void sprite_clear_target(legacy_u8 color)
{
	trace_word(6);
	trace_word(color);
}

legacy_s16 sprite_push_background(legacy_s16 left, legacy_s16 right, legacy_s16 top,
								  legacy_s16 bottom)
{
	if (capture_geometry) {
		geometry_saved = (struct BUTTON_AREA){left, right, top, bottom};
		geometry_pushes++;
	}
	trace_word(7);
	trace_rectangle(left, right, top, bottom);
	return save_succeeds;
}

void sprite_set_target_clip_bounds(legacy_u16 left, legacy_u16 right, legacy_u16 top,
								   legacy_u16 bottom)
{
	if (capture_geometry) {
		geometry_clip =
			(struct BUTTON_AREA){LEGACY_S16_FROM_BITS(left), LEGACY_S16_FROM_BITS(right),
								 LEGACY_S16_FROM_BITS(top), LEGACY_S16_FROM_BITS(bottom)};
		geometry_clips++;
	}
	trace_word(8);
	trace_rectangle(left, right, top, bottom);
}

void sprite_draw_rect_outline(legacy_s16 left, legacy_s16 top, legacy_s16 right, legacy_s16 bottom,
							  legacy_s16 color)
{
	if (capture_geometry) {
		geometry_outline = (struct BUTTON_AREA){left, right, top, bottom};
		geometry_outlines++;
	}
	trace_word(9);
	trace_rectangle(left, right, top, bottom);
	trace_word(color);
}

void font_set_colors(legacy_s16 color, legacy_s16 background_color)
{
	trace_word(10);
	trace_word(color);
	trace_word(background_color);
}

legacy_s16 font_text_width(const legacy_s8 *text)
{
	trace_word(11);
	legacy_s16 width = 0;
	while (*text != 0) {
		trace_word((legacy_u8)*text);
		width += 2 + ((legacy_u8)*text++ % 5);
	}
	trace_word(0);
	return width;
}

void font_draw_text_opaque(const legacy_s8 *text, legacy_s16 x, legacy_s16 y)
{
	if (capture_geometry) {
		assert(geometry_lines < FIXTURE_INPUT_FIELD_COUNT);
		geometry_line_x[geometry_lines] = x;
		geometry_line_y[geometry_lines++] = y;
	}
	trace_word(12);
	trace_word(x);
	trace_word(y);
	while (*text != 0) {
		trace_word((legacy_u8)*text++);
	}
	trace_word(0);
}

legacy_u32 timer_get_delta_alt(void)
{
	trace_word(13);
	timer_calls++;
	return 3U * timer_calls;
}

legacy_u32 slow_timer_wait_ticks(legacy_u32 ticks)
{
#ifdef RESTUNTS_AMIGA
	assert(presented_dialogs == 1);
#endif
	trace_word(14);
	trace_word((legacy_u16)ticks);
	return ticks;
}

legacy_s16 input_checking(legacy_s16 delta)
{
	trace_word(15);
	trace_word(delta);
	assert(input_index < 16U);
	return (legacy_s16)scripted_keys[input_index++];
}

legacy_s16 mouse_multi_hittest(legacy_s16 count, const struct BUTTON_AREA *buttons)
{
	trace_word(16);
	trace_word(count);
	for (legacy_s16 i = 0; i < count; i++) {
		trace_rectangle(buttons[i].x1, buttons[i].x2, buttons[i].y1, buttons[i].y2);
	}
	return scripted_hits[input_index - 1U];
}

static void configure_input(legacy_u32 scenario, legacy_s16 choice_count)
{
	static const legacy_u16 navigation[] = {0, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, 'X'};
	for (legacy_u32 i = 0; i < 16U; i++) {
		scripted_keys[i] = KEY_ENTER;
		scripted_hits[i] = -1;
	}
	for (legacy_u32 i = 0; i < 6U; i++) {
		scripted_keys[i] = navigation[(scenario + i) % 6U];
		if ((scenario + i) % 3U == 0U) {
			scripted_hits[i] = (scenario + i) % choice_count;
		}
	}
	if (scenario % 4U == 0U) {
		scripted_keys[6] = KEY_ESCAPE;
	}
	if (scenario % 4U == 1U) {
		scripted_keys[6] = KEY_SPACE;
	}
	if (scenario % 4U == 2U) {
		scripted_keys[6] = 'a';
	}
	if (scenario % 4U == 3U) {
		scripted_keys[6] = 'B';
	}
	input_index = 0;
	timer_calls = 0;
}

static void run_dialog_case(legacy_u32 scenario)
{
	static const legacy_s8 *texts[] = {(const legacy_s8 *)"Heading]Text @ here}@[ Alpha][ Beta]",
									   (const legacy_s8 *)"Heading]Text} [ Alpha] [ Beta] [ Gamma]",
									   (const legacy_s8 *)"Prompt}[ Alpha] [ Beta]",
									   (const legacy_s8 *)"Heading] [ Alpha][ Beta][ Gamma]"};
	legacy_s16 type = scenario % 7U;
	legacy_s16 count = scenario % 2U == 0U ? 2 : 3;
	legacy_s16 choices[40];
	for (legacy_u32 i = 0; i < 40U; i++) {
		choices[i] = 0;
	}
	if (scenario % 5U == 0U) {
		choices[1] = 1;
	}
	configure_input(scenario, count);
	save_succeeds = scenario % 11U != 0U;
	font_glyph_height = 5 + scenario % 8U;
	dialog_fnt_colour = scenario % 16U;
	dialog_background_color = 7;
	performGraphColor = 9;
	trace_word((legacy_u16)scenario);
#ifdef RESTUNTS_AMIGA
	presented_dialogs = 0;
#endif
	legacy_u16 result = show_dialog(
		type, scenario % 2U, (void *)texts[scenario % 4U],
		scenario % 3U == 0U ? DIALOG_AUTO_POSITION : (legacy_u16)(scenario % 400U),
		scenario % 3U == 1U ? DIALOG_AUTO_POSITION : (legacy_u16)(scenario % 250U), 3,
		scenario % 3U == 0U && type != DIALOG_TYPE_PLACEHOLDERS ? 0 : choices, scenario % count);
	trace_word(result);
#ifdef RESTUNTS_AMIGA
	assert(presented_dialogs == ((scenario % 2U == 0U || save_succeeds) &&
								 (type == DIALOG_TYPE_MESSAGE || type == DIALOG_TYPE_DELAY)));
#endif
	for (legacy_u32 i = 0; i < 40U; i++) {
		trace_word(choices[i]);
	}
	trace_word(dialog_background_color);
}

static void reset_geometry_capture(void)
{
	capture_geometry = 1;
	geometry_pushes = geometry_pops = geometry_clips = geometry_outlines = geometry_lines = 0;
	font_glyph_height = FIXTURE_INPUT_FONT_HEIGHT;
	save_succeeds = 1;
}

static void run_input_geometry_case(legacy_u8 text_input, legacy_s16 save_background,
									legacy_u16 x_argument, legacy_u16 y_argument)
{
	static legacy_s8 prompt[] = "Name @]Path @]";
	legacy_s16 positions[FIXTURE_INPUT_POSITION_COUNT];
	reset_geometry_capture();
	legacy_u16 result;
	if (text_input) {
		result = show_text_input_dialog(save_background, prompt, x_argument, y_argument,
										dialog_fnt_colour, positions);
	} else {
		result = show_dialog(DIALOG_TYPE_PLACEHOLDERS, save_background, prompt, x_argument,
							 y_argument, dialog_fnt_colour, positions, 0);
	}
	legacy_s16 expected_x = x_argument == DIALOG_AUTO_POSITION ? FIXTURE_INPUT_CENTERED_X
															   : LEGACY_S16_FROM_BITS(x_argument);
	legacy_s16 expected_y = y_argument == DIALOG_AUTO_POSITION ? FIXTURE_INPUT_CENTERED_Y
															   : LEGACY_S16_FROM_BITS(y_argument);
#ifdef __ANDROID__
	if (text_input) {
		expected_y = FIXTURE_INPUT_ANDROID_Y;
	}
#endif
	assert(result == FIXTURE_INPUT_FIELD_COUNT);
	assert(geometry_clips == 1U && geometry_outlines == 1U &&
		   geometry_lines == FIXTURE_INPUT_FIELD_COUNT);
	assert(geometry_clip.x1 == expected_x);
	assert(geometry_clip.x2 == expected_x + FIXTURE_INPUT_DIALOG_WIDTH);
	assert(geometry_clip.y1 == expected_y - FIXTURE_INPUT_FRAME_PADDING);
	assert(geometry_clip.y2 ==
		   expected_y + FIXTURE_INPUT_DIALOG_HEIGHT + FIXTURE_INPUT_FRAME_PADDING);
	assert(geometry_clip.y1 >= 0);
	assert(geometry_outline.x1 == geometry_clip.x1 + FIXTURE_INPUT_OUTLINE_INSET);
	assert(geometry_outline.x2 == geometry_clip.x2 - FIXTURE_INPUT_OUTLINE_INSET);
	assert(geometry_outline.y1 == geometry_clip.y1 + FIXTURE_INPUT_OUTLINE_INSET);
	assert(geometry_outline.y2 == geometry_clip.y2 - FIXTURE_INPUT_OUTLINE_INSET);
	assert(positions[0] ==
		   expected_x + FIXTURE_INPUT_CONTENT_PADDING + FIXTURE_INPUT_NAME_PREFIX_WIDTH);
	assert(positions[1] == expected_y + FIXTURE_INPUT_FIRST_LINE_OFFSET);
	assert(positions[2] ==
		   expected_x + FIXTURE_INPUT_CONTENT_PADDING + FIXTURE_INPUT_PATH_PREFIX_WIDTH);
	assert(positions[3] == positions[1] + FIXTURE_INPUT_LINE_HEIGHT);
	for (legacy_u32 line = 0; line < FIXTURE_INPUT_FIELD_COUNT; line++) {
		assert(geometry_line_x[line] == expected_x + FIXTURE_INPUT_CONTENT_PADDING);
		assert(geometry_line_y[line] == positions[line * FIXTURE_INPUT_POSITION_STRIDE + 1U]);
		assert(positions[line * FIXTURE_INPUT_POSITION_STRIDE + 1U] >= geometry_clip.y1);
		assert(positions[line * FIXTURE_INPUT_POSITION_STRIDE + 1U] + font_glyph_height <=
			   geometry_clip.y2);
	}
	assert(geometry_pushes == (legacy_u32)save_background && geometry_pops == 0);
	if (save_background != 0) {
		assert(geometry_saved.x1 == geometry_clip.x1 && geometry_saved.x2 == geometry_clip.x2);
		assert(geometry_saved.y1 == geometry_clip.y1 && geometry_saved.y2 == geometry_clip.y2);
	}
	capture_geometry = 0;
}

static void test_text_input_geometry(void)
{
	static const legacy_u16 x_arguments[] = {DIALOG_AUTO_POSITION, FIXTURE_INPUT_EXPLICIT_X};
	static const legacy_u16 y_arguments[] = {DIALOG_AUTO_POSITION, FIXTURE_INPUT_EXPLICIT_Y};
	for (legacy_u8 text_input = 0; text_input <= 1U; text_input++) {
		for (legacy_s16 save_background = DIALOG_NO_BACKGROUND_SAVE;
			 save_background <= DIALOG_SAVE_BACKGROUND; save_background++) {
			for (legacy_u32 x = 0; x < sizeof(x_arguments) / sizeof(x_arguments[0]); x++) {
				for (legacy_u32 y = 0; y < sizeof(y_arguments) / sizeof(y_arguments[0]); y++) {
					run_input_geometry_case(text_input, save_background, x_arguments[x],
											y_arguments[y]);
				}
			}
		}
	}
	reset_geometry_capture();
	save_succeeds = 0;
	legacy_s8 prompt[] = "Name @]Path @]";
	legacy_s16 positions[FIXTURE_INPUT_POSITION_COUNT];
	for (legacy_u32 i = 0; i < FIXTURE_INPUT_POSITION_COUNT; i++) {
		positions[i] = FIXTURE_INPUT_UNSET_POSITION;
	}
	assert(show_text_input_dialog(DIALOG_SAVE_BACKGROUND, prompt, DIALOG_AUTO_POSITION,
								  DIALOG_AUTO_POSITION, dialog_fnt_colour,
								  positions) == DIALOG_FAILURE_RESULT);
	assert(geometry_pushes == 1U && geometry_clips == 0 && geometry_outlines == 0 &&
		   geometry_lines == 0 && geometry_pops == 0);
	for (legacy_u32 i = 0; i < FIXTURE_INPUT_POSITION_COUNT; i++) {
		assert(positions[i] == FIXTURE_INPUT_UNSET_POSITION);
	}
	capture_geometry = 0;
	puts("Text-entry geometry and saved backgrounds passed (Android/native placement).");
}

int main(void)
{
	for (legacy_u32 scenario = 0; scenario < 420U; scenario++) {
		run_dialog_case(scenario);
	}
	/* Trace includes drawing, geometry, disabled choices, placeholders, input polling
	 * and background lifetime, with one call per integer conversion. */
#ifdef DIALOG_RECORD_BASELINE
	fprintf(stdout, "test-ui-dialog.c=0x%016" LEGACY_PRIx64 "\n", trace_hash);
#else
	assert(trace_hash == UINT64_C(0x5b92617a4f634339));
#endif
	test_text_input_geometry();
	puts("Dialog interaction snapshots passed (420 scenarios).");
	return 0;
}
