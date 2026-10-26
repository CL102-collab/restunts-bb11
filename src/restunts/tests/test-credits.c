/* Credit resource selection, drawing and cancellation order use a trace
 * captured before extraction; display and timer endpoints are stubbed. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../c/intro.c"
#undef printf
#undef strcpy
#undef memcpy

static legacy_u32 trace_hash = UINT32_C(2166136261);
static struct SHAPE2D shapes[11];
static struct SPRITE window;
struct SPRITE *render_window_sprite;
static legacy_u32 poll_count, cancel_poll, blit_count, cancel_blit;
static legacy_u32 elapsed;
static legacy_s16 repeat_result;
static legacy_s8 resource[16];

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
	return ++blit_count == cancel_blit;
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
	return ++poll_count == cancel_poll ? 27 : 0;
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

int main(void)
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
	assert(trace_hash == UINT32_C(0x47365795));
#endif
	return 0;
}
