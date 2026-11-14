/* Credit resource selection, drawing and cancellation order use a trace
 * captured before extraction; display and timer endpoints are stubbed. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../c/keyboard.h"
#include "../c/intro.c"
#undef printf
#undef strcpy
#undef strcmp
#undef memcpy

static legacy_u32 trace_hash = UINT32_C(2166136261);
static struct SHAPE2D shapes[11];
static struct SPRITE window;
struct SPRITE *render_window_sprite;
static legacy_u32 poll_count, cancel_poll, blit_count, cancel_blit;
static legacy_u32 elapsed;
static legacy_s16 repeat_result;
static legacy_s8 resource[16];
static legacy_u8 scripted_input;
static legacy_s16 scripted_key;
static legacy_u32 scripted_poll, scripted_blit, shortcut_count, shape_count;
static legacy_u8 fps_visible, enhanced_visible;
static legacy_u8 font_data[4];
legacy_u8 *active_font_definition;

enum INTRO_INPUT_PHASE {
	INTRO_INPUT_PRODUCTION_FADE,
	INTRO_INPUT_PRODUCTION_WAIT,
	INTRO_INPUT_TITLE_FADE,
	INTRO_INPUT_TITLE_WAIT,
	INTRO_INPUT_PHASE_COUNT
};

#define INTRO_INPUT_TEST_TICKS 100U
#define INTRO_INPUT_TEST_WAIT_POLLS (INTRO_PAGE_INPUT_DELAY / INTRO_INPUT_TEST_TICKS)

legacy_s16 handle_ingame_kb_shortcuts(legacy_s16 key)
{
	assert(key == KEY_F11 || key == KEY_F12 || key == KEY_SHIFT_F12);
	shortcut_count++;
	if (key == KEY_F11) {
		fps_visible ^= 1U;
	} else if (key == KEY_F12) {
		enhanced_visible ^= 1U;
	} else {
		enhanced_visible = 1;
	}
	return 1;
}

void sprite_select_screen_and_clear(void)
{
}

legacy_s8 *locate_shape_fatal(legacy_s8 *chunk, const legacy_s8 *name)
{
	assert(chunk == resource);
	assert(strcmp((const legacy_char *)name, "prod") == 0 ||
		   strcmp((const legacy_char *)name, "titl") == 0);
	return (legacy_s8 *)&shapes[0];
}

void load_audio_finalize(void *audio)
{
	assert(audio == NULL);
}

void font_set_fontdef2(void *data)
{
	active_font_definition = data;
}

void font_set_colors(legacy_s16 color, legacy_s16 background)
{
	(void)color;
	(void)background;
}

static void trace(legacy_u32 value)
{
	for (legacy_u32 i = 0; i < 4; i++) {
		trace_hash = (trace_hash ^ (value & 255U)) * UINT32_C(16777619);
		value >>= 8;
	}
}
static void trace_text(const legacy_s8 *text)
{
	while (*text) {
		trace((legacy_u8)*text++);
	}
	trace(0);
}
void far *file_load_resfile(const legacy_s8 *name)
{
	trace(1);
	trace_text(name);
	return resource;
}
void locate_many_resources(legacy_s8 far *chunk, const legacy_s8 *ids, legacy_s8 far **result)
{
	assert(chunk == resource);
	trace(2);
	trace_text(ids);
	for (legacy_u32 i = 0; i < 11; i++) {
		legacy_s8 *shape = (legacy_s8 *)&shapes[i];
		memcpy(result + i, &shape, sizeof(shape));
	}
}
void sprite_select_render_window_and_clear(void)
{
	trace(3);
}
legacy_u16 shape2d_get_pos_x(const struct SHAPE2D far *shape)
{
	return shape->position_x;
}
legacy_u16 shape2d_get_pos_y(const struct SHAPE2D far *shape)
{
	return shape->position_y;
}
legacy_u16 shape2d_get_width(const struct SHAPE2D far *shape)
{
	return shape->width;
}
legacy_u16 shape2d_get_height(const struct SHAPE2D far *shape)
{
	return shape->height;
}
legacy_s8 far *locate_text_res(legacy_s8 far *chunk, const legacy_s8 *id)
{
	assert(chunk == resource);
	trace(4);
	trace_text(id);
	return (legacy_s8 *)"Text";
}
legacy_s8 far *locate_shape_alt(legacy_s8 far *chunk, const legacy_s8 *id)
{
	assert(chunk == resource);
	trace(5);
	trace_text(id);
	return (legacy_s8 *)"Shape";
}
void copy_string(legacy_s8 *destination, legacy_s8 far *source)
{
	strcpy((legacy_char *)destination, (const legacy_char *)source);
}
struct RECTANGLE *intro_draw_text(legacy_s8 *text, legacy_s16 x, legacy_s16 y, legacy_s16 color,
								  legacy_s16 shadow)
{
	trace(6);
	trace_text(text);
	trace(x);
	trace(y);
	trace(color);
	trace(shadow);
	return 0;
}
void unload_resource(void far *chunk)
{
	assert(chunk == resource);
	trace(7);
}
legacy_s16 sprite_blit_to_video(struct SPRITE far *sprite, legacy_s16 fade)
{
	assert(sprite == &window);
	trace(8);
	trace(fade);
	blit_count++;
	if (scripted_input != 0) {
		return blit_count == scripted_blit ? scripted_key : 0;
	}
	return blit_count == cancel_blit;
}
void sprite_select_screen_compat(void)
{
	trace(9);
}
legacy_u32 timer_get_delta_alt(void)
{
	trace(10);
	return elapsed;
}
void mouse_draw_opaque_check(void)
{
	trace(11);
}
void sprite_copy_image_at(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y)
{
	trace(12);
	trace(shape - shapes);
	trace(x);
	trace(y);
}
void sprite_fill_rect_clipped(legacy_s16 x, legacy_s16 y, legacy_s16 width, legacy_s16 height,
							  legacy_s16 color)
{
	trace(13);
	trace(x);
	trace(y);
	trace(width);
	trace(height);
	trace(color);
}
void mouse_draw_transparent_check(void)
{
	trace(14);
}
legacy_s16 input_do_checking(legacy_s16 ticks)
{
	trace(15);
	trace(ticks);
	assert(poll_count < 2000);
	poll_count++;
	if (scripted_input != 0) {
		return poll_count == scripted_poll ? scripted_key : 0;
	}
	if (blit_count >= 2 && repeat_result != 0) {
		return repeat_result;
	}
	return poll_count == cancel_poll ? KEY_ESCAPE : 0;
}
void sprite_select_render_window(void)
{
	trace(16);
}
void sprite_set_target_clip_bounds(legacy_u16 left, legacy_u16 right, legacy_u16 top,
								   legacy_u16 bottom)
{
	trace(17);
	trace(left);
	trace(right);
	trace(top);
	trace(bottom);
}
void sprite_clear_target(legacy_u8 color)
{
	trace(18);
	trace(color);
}
void sprite_shape_to_1_alt(struct SHAPE2D far *shape)
{
	trace(19);
	trace(shape - shapes);
	shape_count++;
}
void sprite_putimage(struct SHAPE2D far *shape)
{
	assert(shape == &shapes[0]);
	trace(20);
}
void sprite_clear_shape(struct SHAPE2D far *shape)
{
	assert(shape == &shapes[0]);
	trace(21);
}
legacy_s16 input_repeat_check(legacy_s16 ticks)
{
	trace(22);
	trace(ticks);
	return repeat_result;
}

static void prepare_input_script(legacy_s16 key)
{
	scripted_input = 1;
	scripted_key = key;
	scripted_poll = scripted_blit = shortcut_count = shape_count = 0;
	poll_count = blit_count = 0;
	fps_visible = enhanced_visible = 0;
	elapsed = INTRO_INPUT_TEST_TICKS;
	ui_temp_resource = resource;
	render_window_sprite = &window;
	window.sprite_bitmapptr = &shapes[0];
	fontnptr = font_data;
	active_font_definition = font_data;
	shapes[0].position_y = 0;
}

static void select_input_phase(enum INTRO_INPUT_PHASE phase)
{
	switch (phase) {
		case INTRO_INPUT_PRODUCTION_FADE:
			scripted_blit = 1;
			break;
		case INTRO_INPUT_PRODUCTION_WAIT:
			scripted_poll = 1;
			break;
		case INTRO_INPUT_TITLE_FADE:
			scripted_blit = 2;
			break;
		case INTRO_INPUT_TITLE_WAIT:
			scripted_poll = INTRO_INPUT_TEST_WAIT_POLLS + 1U;
			break;
		default:
			assert(0);
	}
}

static void assert_shortcut_effect(legacy_s16 key)
{
	assert(shortcut_count == 1);
	assert(fps_visible == (key == KEY_F11));
	assert(enhanced_visible == (key != KEY_F11));
}

static void test_title_shortcuts(void)
{
	static const legacy_s16 shortcuts[] = {KEY_F11, KEY_F12,
#ifdef RESTUNTS_SDL3
										   KEY_SHIFT_F12
#endif
	};
	for (legacy_u16 key = 0; key < sizeof(shortcuts) / sizeof(shortcuts[0]); key++) {
		for (legacy_u16 phase = 0; phase < INTRO_INPUT_PHASE_COUNT; phase++) {
			prepare_input_script(shortcuts[key]);
			select_input_phase((enum INTRO_INPUT_PHASE)phase);
			assert(run_intro(NULL) == 0);
			assert(blit_count == 2);
			assert(poll_count == INTRO_INPUT_TEST_WAIT_POLLS * 2U);
			assert_shortcut_effect(shortcuts[key]);
		}
	}
	static const legacy_s16 dismissals[] = {KEY_ESCAPE, KEY_SPACE, KEY_ENTER, 'x'};
	for (legacy_u16 key = 0; key < sizeof(dismissals) / sizeof(dismissals[0]); key++) {
		for (legacy_u16 phase = 0; phase < INTRO_INPUT_PHASE_COUNT; phase++) {
			prepare_input_script(dismissals[key]);
			select_input_phase((enum INTRO_INPUT_PHASE)phase);
			assert(run_intro(NULL) == dismissals[key]);
			assert(shortcut_count == 0);
		}
	}
	scripted_input = 0;
}

static void test_credits_shortcuts(void)
{
	static const legacy_s16 shortcuts[] = {KEY_F11, KEY_F12,
#ifdef RESTUNTS_SDL3
										   KEY_SHIFT_F12
#endif
	};
	for (legacy_u16 key = 0; key < sizeof(shortcuts) / sizeof(shortcuts[0]); key++) {
		prepare_input_script(shortcuts[key]);
		elapsed = CREDITS_ANIMATION_INTERVAL;
		scripted_poll = 1;
		assert(credits_slide_arrow(&shapes[1], CREDITS_LEFT_COLUMN_X, 0, 0, 0) == 0);
		assert(poll_count > 1);
		assert_shortcut_effect(shortcuts[key]);

		prepare_input_script(shortcuts[key]);
		elapsed = CREDITS_ANIMATION_INTERVAL;
		scripted_poll = 1;
		struct SHAPE2D *credit_shapes[CREDITS_RESOURCE_COUNT];
		for (legacy_u16 shape = 0; shape < CREDITS_RESOURCE_COUNT; shape++) {
			credit_shapes[shape] = &shapes[shape];
		}
		credits_animate_shapes(credit_shapes, 0, 0);
		assert(shape_count == CREDITS_ANIMATION_END_INDEX - CREDITS_FIRST_ANIMATION_INDEX);
		assert_shortcut_effect(shortcuts[key]);

		for (legacy_u16 fade = 0; fade < 2U; fade++) {
			prepare_input_script(shortcuts[key]);
			scripted_blit = fade != 0;
			scripted_poll = fade == 0;
			assert(credits_present_closing(credit_shapes, 0) == 0);
			assert_shortcut_effect(shortcuts[key]);
		}
	}
	static const legacy_s16 dismissals[] = {KEY_ESCAPE, KEY_SPACE, KEY_ENTER, 'x'};
	for (legacy_u16 key = 0; key < sizeof(dismissals) / sizeof(dismissals[0]); key++) {
		prepare_input_script(dismissals[key]);
		elapsed = CREDITS_ANIMATION_INTERVAL;
		scripted_poll = 1;
		assert(credits_slide_arrow(&shapes[1], CREDITS_LEFT_COLUMN_X, 0, 0, 0) == dismissals[key]);
		assert(shortcut_count == 0);

		struct SHAPE2D *credit_shapes[CREDITS_RESOURCE_COUNT];
		for (legacy_u16 shape = 0; shape < CREDITS_RESOURCE_COUNT; shape++) {
			credit_shapes[shape] = &shapes[shape];
		}
		prepare_input_script(dismissals[key]);
		elapsed = CREDITS_ANIMATION_INTERVAL;
		scripted_poll = 1;
		credits_animate_shapes(credit_shapes, 0, 0);
		assert(shape_count == 1);
		assert(shortcut_count == 0);

		for (legacy_u16 fade = 0; fade < 2U; fade++) {
			prepare_input_script(dismissals[key]);
			scripted_blit = fade != 0;
			scripted_poll = fade == 0;
			assert(credits_present_closing(credit_shapes, 0) == 1);
			assert(shortcut_count == 0);
		}
	}
	scripted_input = 0;
}

legacy_int main(void)
{
	for (legacy_u32 scenario = 0; scenario < 32; scenario++) {
		trace(scenario);
		memset(shapes, 0, sizeof(shapes));
		for (legacy_u32 i = 0; i < 11; i++) {
			shapes[i].position_x = 200;
			shapes[i].position_y = 130 + i;
			shapes[i].width = 20;
			shapes[i].height = 10;
		}
		ui_temp_resource = resource;
		render_window_sprite = &window;
		window.sprite_bitmapptr = &shapes[0];
		video_shape_width_scale = (scenario & 1) + 1;
		elapsed = (scenario & 2) ? 5 : 1;
		cancel_poll = (scenario & 4) ? 1 : (scenario & 8) ? 70 : 0;
		cancel_blit = (scenario & 16) ? 2 : 0;
		repeat_result = scenario & 8;
		poll_count = blit_count = 0;
		trace(load_intro_resources());
		trace(poll_count);
		trace(blit_count);
		trace(waitflag);
	}
#ifdef CREDITS_RECORD_BASELINE
	printf("Credits fingerprint: %08" LEGACY_PRIx32 "\n", trace_hash);
#else
	/* Title/credit waits now poll through the elapsed-preserving intro filter. */
	assert(trace_hash == UINT32_C(0xb92520b5));
#endif
	test_title_shortcuts();
	test_credits_shortcuts();
	return 0;
}
