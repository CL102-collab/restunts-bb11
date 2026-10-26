#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../c/externs.h"
#include "../c/dashboard.h"
#include "../c/shape2d.h"
#include "../c/shape3d.h"
#include "../c/platform.h"
#include "../c/fileio.h"
#include "../c/memmgr.h"
#include "../c/game_input.h"
#undef printf
#undef memcpy
#undef memset

static legacy_u32 trace;
enum DASHBOARD_TEST_CONSTANTS {
	TEST_TRANSFORM_LEFT_SHAPE = 40,
	TEST_TRANSFORM_RIGHT_SHAPE = 41,
	TEST_SHAPE_COUNT = 42,
	TEST_PANEL_WIDTH = 12,
	TEST_GEARBOX_SIZE = 20,
	TEST_PANEL_HEIGHT = 10,
	TEST_PANEL_X = 100,
	TEST_PANEL_Y = 130,
	TEST_PANEL_PIVOT_X = 6,
	TEST_PANEL_PIVOT_Y = 5,
	TEST_WHEEL_X = 90,
	TEST_WHEEL_Y = 120,
	TEST_WHEEL_SIZE = 40,
	TEST_DIGIT_COLOR = 210,
	TEST_WHEEL_COLOR = 230,
	TEST_RPM_COLOR = 55,
	TEST_FULL_STEERING = 88
};
static struct SHAPE2D shapes[TEST_SHAPE_COUNT];
static struct SPRITE sprites[2];
static legacy_u16 sprite_count;
static legacy_u16 live_sprite_count;
static struct SPRITE drawing_context[SPRITE_STATE_COUNT];
static legacy_s32 capture_pixels;
static legacy_u8 pixel_buffers[4][320 * 200];
static legacy_s32 optional_shapes;
static legacy_u16 optional_instrument_transforms;
static legacy_s32 capture_live_pixels;
static legacy_u32 transformed_panel_count;
static legacy_u32 wheel_draw_count;
static legacy_u32 wheel_mask_count;
static struct SHAPE2D_TRANSFORM last_transform;
static legacy_s8 resources[2];
static legacy_s32 capture_needle_lines;
static legacy_u32 needle_line_count;
static struct NEEDLE_LINE {
	legacy_u16 x, y, x2, y2, color;
} needle_lines[2];

static void record(legacy_u16 value)
{
	trace = (trace ^ (value & 255U)) * 16777619UL;
	trace = (trace ^ (value >> 8)) * 16777619UL;
}
static legacy_u16 shape_id(const struct SHAPE2D *shape)
{
	return shape->width;
}
static legacy_u8 *pixels_for_shape(const struct SHAPE2D *shape)
{
	if (shape == &shapes[35] || shape == &shapes[36]) {
		return pixel_buffers[shape - &shapes[35]];
	}
	assert(shape == &shapes[38] || shape == &shapes[39]);
	return pixel_buffers[2 + shape - &shapes[38]];
}
static legacy_u8 background_pixel(legacy_u16 x, legacy_u16 y)
{
	return (legacy_u8)(1 + (x * 5 + y * 17) % 250);
}
static legacy_u8 instrument_pixel(legacy_u16 x, legacy_u16 y)
{
	return (legacy_u8)(1 + (x * 11 + y * 7) % 200);
}
static legacy_u8 fixture_pixel(struct SHAPE2D *shape, legacy_u16 x, legacy_u16 y)
{
	if (shape == &shapes[3]) {
		if (capture_live_pixels != 0 && x == 0 && y == 0) {
			return SHAPE2D_TRANSPARENT_COLOR;
		}
		return instrument_pixel(x, y);
	}
	if (shape == &shapes[30]) {
		return background_pixel(x, y);
	}
	if (capture_live_pixels != 0 && shape >= &shapes[0] && shape <= &shapes[2]) {
		return (legacy_u8)(TEST_WHEEL_COLOR + shape - shapes);
	}
	if (capture_live_pixels != 0 && shape >= &shapes[20] && shape <= &shapes[29]) {
		return (legacy_u8)(TEST_DIGIT_COLOR + shape - &shapes[20]);
	}
	return 251;
}
static void draw_pixels(struct SHAPE2D *shape, legacy_s16 x, legacy_s16 y, legacy_s32 raw_bitmap)
{
	if (capture_pixels == 0) {
		return;
	}
	struct SPRITE *target = &drawing_context[0];
	legacy_u8 *pixels = pixels_for_shape(target->sprite_bitmapptr);
	legacy_u8 *source = raw_bitmap ? pixels_for_shape(shape) : NULL;
	for (legacy_s16 row = 0; row < shape->height; row++) {
		legacy_s16 target_y = y + row;
		if (target_y < target->sprite_top || target_y >= target->sprite_bottom) {
			continue;
		}
		for (legacy_s16 column = 0; column < shape->width; column++) {
			legacy_s16 target_x = x + column;
			if (target_x < target->sprite_left || target_x >= target->sprite_right) {
				continue;
			}
			pixels[target_y * target->sprite_pitch + target_x] =
				raw_bitmap ? source[row * shape->width + column]
						   : fixture_pixel(shape, column, row);
		}
	}
}
static void select_pixels(struct SHAPE2D *shape)
{
	drawing_context[0].sprite_bitmapptr = shape;
	drawing_context[0].sprite_left = 0;
	drawing_context[0].sprite_top = 0;
	drawing_context[0].sprite_right = shape->width;
	drawing_context[0].sprite_bottom = shape->height;
	drawing_context[0].sprite_pitch = shape->width;
}
legacy_u16 shape2d_get_width(const struct SHAPE2D *shape)
{
	return shape->width;
}
legacy_u16 shape2d_get_height(const struct SHAPE2D *shape)
{
	return shape->height;
}
legacy_u16 shape2d_get_anchor_x(const struct SHAPE2D *shape)
{
	return shape->centre_x;
}
legacy_u16 shape2d_get_anchor_y(const struct SHAPE2D *shape)
{
	return shape->centre_y;
}
legacy_u16 shape2d_get_pos_x(const struct SHAPE2D *shape)
{
	return shape->position_x;
}
legacy_u16 shape2d_get_pos_y(const struct SHAPE2D *shape)
{
	return shape->position_y;
}
void mouse_draw_opaque_check(void)
{
	record(1);
}
void mouse_draw_transparent_check(void)
{
	record(2);
}
void sprite_select_screen(void)
{
	select_pixels(&shapes[38]);
	record(3);
}
void sprite_select_screen_compat(void)
{
	select_pixels(&shapes[38]);
	record(4);
}
void sprite_select_mcga_backbuffer(void)
{
	select_pixels(&shapes[39]);
	record(5);
}
void shape2d_rle_copy_at_position(struct SHAPE2D *shape)
{
	record(10);
	record(shape_id(shape));
}
void shape2d_rle_copy_position_clipped(struct SHAPE2D *shape)
{
	if (capture_live_pixels != 0) {
		draw_pixels(shape, shape->position_x, shape->position_y, 0);
		if (shape >= &shapes[0] && shape <= &shapes[2]) {
			wheel_draw_count++;
		}
	}
	record(11);
	record(shape_id(shape));
}
void shape2d_render_bmp_as_mask(struct SHAPE2D *shape)
{
	wheel_mask_count++;
	record(12);
	record(shape_id(shape));
}
void shape2d_rle_copy(struct SHAPE2D *shape, legacy_s16 x, legacy_s16 y)
{
	draw_pixels(shape, x, y, 0);
	record(20);
	record(shape_id(shape));
	record((legacy_u16)x);
	record((legacy_u16)y);
}
void shape2d_rle_copy_clipped(struct SHAPE2D *shape, legacy_s16 x, legacy_s16 y)
{
	draw_pixels(shape, x, y, 0);
	record(21);
	record(shape_id(shape));
	record((legacy_u16)x);
	record((legacy_u16)y);
}
void sprite_copy_image_at(struct SHAPE2D *shape, legacy_s16 x, legacy_s16 y)
{
	if (shape == dashboard_gearbox_sprite->sprite_bitmapptr ||
		shape == dashboard_instrument_sprite->sprite_bitmapptr) {
		draw_pixels(shape, x, y, 1);
	}
	record(22);
	record(shape_id(shape));
	record((legacy_u16)x);
	record((legacy_u16)y);
}
/* This mock uses exact quarter turns, independently of the production Q14
 * rasterizer. It exposes dashboard composition and stale-pixel restoration. */
void sprite_copy_image_transformed(struct SPRITE *source, const struct SHAPE2D_TRANSFORM *transform)
{
	assert(capture_live_pixels != 0);
	assert(source == dashboard_instrument_sprite);
	assert(source->sprite_bitmapptr->width == TEST_GEARBOX_SIZE);
	last_transform = *transform;
	transformed_panel_count++;
	struct SPRITE *target = &drawing_context[0];
	legacy_u8 *input = pixels_for_shape(source->sprite_bitmapptr);
	legacy_u8 *output = pixels_for_shape(target->sprite_bitmapptr);
	for (legacy_u16 y = 0; y < transform->source_height; y++) {
		for (legacy_u16 x = 0; x < transform->source_width; x++) {
			legacy_s16 dx = x - transform->source_pivot_x;
			legacy_s16 dy = y - transform->source_pivot_y;
			legacy_s16 tx, ty;
			if (transform->inverse_xx == SHAPE2D_TRANSFORM_ONE) {
				tx = transform->target_pivot_x + dx;
				ty = transform->target_pivot_y + dy;
			} else if (transform->inverse_xy == -SHAPE2D_TRANSFORM_ONE) {
				tx = transform->target_pivot_x + dy;
				ty = transform->target_pivot_y - dx;
			} else {
				assert(transform->inverse_xy == SHAPE2D_TRANSFORM_ONE);
				tx = transform->target_pivot_x - dy;
				ty = transform->target_pivot_y + dx;
			}
			legacy_u8 color = input[y * source->sprite_bitmapptr->width + x];
			if (tx >= target->sprite_left && tx < target->sprite_right &&
				ty >= target->sprite_top && ty < target->sprite_bottom &&
				color != SHAPE2D_TRANSPARENT_COLOR) {
				output[ty * target->sprite_pitch + tx] = color;
			}
		}
	}
}
void sprite_and_image_at_anchor(struct SHAPE2D *shape, legacy_s16 x, legacy_s16 y)
{
	record(23);
	record(shape_id(shape));
	record((legacy_u16)x);
	record((legacy_u16)y);
}
void sprite_or_image_at_anchor(struct SHAPE2D *shape, legacy_s16 x, legacy_s16 y)
{
	record(24);
	record(shape_id(shape));
	record((legacy_u16)x);
	record((legacy_u16)y);
}
void sprite_clear_shape_alt(struct SHAPE2D *shape, legacy_s16 x, legacy_s16 y)
{
	record(25);
	record(shape_id(shape));
	record((legacy_u16)x);
	record((legacy_u16)y);
}
void sprite_putimage_or(struct SHAPE2D *shape, legacy_u16 x, legacy_u16 y)
{
	if (capture_live_pixels != 0) {
		draw_pixels(shape, x, y, 0);
	}
	record(26);
	record(shape_id(shape));
	record((legacy_u16)x);
	record((legacy_u16)y);
}

void sprite_set_target_clip_bounds(legacy_u16 left, legacy_u16 right, legacy_u16 top,
								   legacy_u16 bottom)
{
	drawing_context[0].sprite_left = left;
	drawing_context[0].sprite_right = right;
	drawing_context[0].sprite_top = top;
	drawing_context[0].sprite_bottom = bottom;
	record(30);
	record(left);
	record(right);
	record(top);
	record(bottom);
}
void preRender_line(legacy_u16 x, legacy_u16 y, legacy_u16 x2, legacy_u16 y2, legacy_u16 color)
{
	if (capture_live_pixels != 0) {
		struct SPRITE *target = &drawing_context[0];
		legacy_u8 *pixels = pixels_for_shape(target->sprite_bitmapptr);
		assert(x < target->sprite_right && y < target->sprite_bottom);
		assert(x2 < target->sprite_right && y2 < target->sprite_bottom);
		pixels[y * target->sprite_pitch + x] = (legacy_u8)color;
		pixels[y2 * target->sprite_pitch + x2] = (legacy_u8)color;
	}
	if (capture_needle_lines != 0) {
		assert(needle_line_count < 2);
		struct NEEDLE_LINE *line = &needle_lines[needle_line_count++];
		line->x = x;
		line->y = y;
		line->x2 = x2;
		line->y2 = y2;
		line->color = color;
	}
	record(31);
	record(x);
	record(y);
	record(x2);
	record(y2);
	record(color);
}
void shape2d_rle_or_far_pointer(legacy_u16 offset, legacy_u16 segment)
{
	record(32);
	record(offset);
	record(segment);
}
legacy_u16 dos_memory_pointer_offset(const void *pointer)
{
	return shape_id(pointer);
}
legacy_u16 dos_memory_pointer_segment(const void *pointer)
{
	return shape_id(pointer) + 10;
}
struct SPRITE *sprite_make_wnd(legacy_u16 width, legacy_u16 height, legacy_u16 flags)
{
	assert(sprite_count < 2);
	live_sprite_count++;
	record(33);
	record(width);
	record(height);
	record(flags);
	struct SPRITE *result = &sprites[sprite_count];
	result->sprite_bitmapptr = &shapes[35 + sprite_count++];
	if (capture_pixels != 0) {
		result->sprite_bitmapptr->width = width;
		result->sprite_bitmapptr->height = height;
	}
	return result;
}
void sprite_free_wnd(struct SPRITE *sprite)
{
	assert(live_sprite_count != 0);
	assert(sprite == &sprites[--live_sprite_count]);
	record(34);
	record(shape_id(sprite->sprite_bitmapptr));
}
void sprite_select_target(struct SPRITE *sprite)
{
	select_pixels(sprite->sprite_bitmapptr);
	record(35);
	record(shape_id(sprite->sprite_bitmapptr));
}
void sprite_save_context(struct SPRITE saved_context[SPRITE_STATE_COUNT])
{
	memcpy(saved_context, drawing_context, sizeof(drawing_context));
	record(41);
}
void sprite_restore_context(struct SPRITE saved_context[SPRITE_STATE_COUNT])
{
	memcpy(drawing_context, saved_context, sizeof(drawing_context));
	record(42);
}
void sprite_clear_target(legacy_u8 color)
{
	if (capture_pixels != 0) {
		struct SPRITE *target = &drawing_context[0];
		legacy_u8 *pixels = pixels_for_shape(target->sprite_bitmapptr);
		for (legacy_s16 y = target->sprite_top; y < target->sprite_bottom; y++) {
			memset(pixels + y * target->sprite_pitch + target->sprite_left, color,
				   target->sprite_right - target->sprite_left);
		}
	}
	record(43);
	record(color);
}
void *file_load_resource(legacy_s16 type, const legacy_s8 *name)
{
	record(36);
	record(type);
	record(name[4]);
	return &resources[type == FILE_RESOURCE_SHAPE2D ? 1 : 0];
}
void *mmgr_free(legacy_s8 *pointer)
{
	record(37);
	record(pointer == &resources[1]);
	return 0;
}
void locate_many_resources(legacy_s8 *data, const legacy_s8 *names, legacy_s8 **result)
{
	(void)data;
	legacy_u32 first = names == dashboard_wheel_and_instrument_ids ? 0
					   : names == dashboard_gear_and_dot_shape_ids ? 10
																   : 20;
	legacy_u32 count = first == 0 ? 9 : first == 10 ? 6 : 10;
	record(38);
	record(first);
	for (legacy_u32 i = 0; i < count; i++) {
		result[i] = (legacy_s8 *)&shapes[first + i];
	}
}
legacy_s8 *locate_shape_nofatal(legacy_s8 *data, const legacy_s8 *name)
{
	(void)data;
	if (memcmp(name, "imt1", 4) == 0) {
		return (optional_instrument_transforms & 1U) != 0
				   ? (legacy_s8 *)&shapes[TEST_TRANSFORM_LEFT_SHAPE]
				   : 0;
	}
	if (memcmp(name, "imt3", 4) == 0) {
		return (optional_instrument_transforms & 2U) != 0
				   ? (legacy_s8 *)&shapes[TEST_TRANSFORM_RIGHT_SHAPE]
				   : 0;
	}
	legacy_u32 index = name == dashboard_roof_shape_id ? 31 : 32;
	record(39);
	record(index);
	return optional_shapes ? (legacy_s8 *)&shapes[index] : 0;
}
legacy_s8 *locate_shape_fatal(legacy_s8 *data, const legacy_s8 *name)
{
	(void)data;
	legacy_u32 index = name == dashboard_background_shape_id ? 30
					   : name == dashboard_roof_shape_id	 ? 31
					   : name == dashboard_top_shape_id		 ? 32
															 : 33;
	record(40);
	record(index);
	return (legacy_s8 *)&shapes[index];
}
static void capture_cache(legacy_u32 buffer)
{
	record(dashboard_gear_knob_visible_cache[buffer]);
	record(dashboard_gear_knob_x_cache[buffer]);
	record(dashboard_gear_knob_y_cache[buffer]);
	record(dashboard_wheel_shape_cache[buffer]);
	record(dashboard_steering_position_cache[buffer]);
	record(dashboard_steering_dot_x_cache[buffer]);
	record(dashboard_steering_dot_y_cache[buffer]);
	record(dashboard_speed_index_cache[buffer]);
	record(dashboard_rpm_index_cache[buffer]);
}
static void initialize_scenario(legacy_u32 scenario)
{
	memset(&state, 0, sizeof(state));
	memset(&simd_player, 0, sizeof(simd_player));
	memset(sprites, 0, sizeof(sprites));
	memset(drawing_context, 0, sizeof(drawing_context));
	optional_instrument_transforms = 0;
	for (legacy_u32 i = 0; i < TEST_SHAPE_COUNT; i++) {
		shapes[i].width = i + 1;
		shapes[i].height = i + 2;
		shapes[i].centre_x = 3;
		shapes[i].centre_y = 4;
		shapes[i].position_x = 20 + i;
		shapes[i].position_y = 50 + i;
	}
	for (legacy_u32 i = 0; i < sizeof(simd_player.steeringdots); i++) {
		simd_player.steeringdots[i] = 40 + (i % 50);
	}
	for (legacy_u32 i = 0; i < sizeof(simd_player.spdpoints); i++) {
		simd_player.spdpoints[i] = 10 + (i % 50);
	}
	for (legacy_u32 i = 0; i < sizeof(simd_player.revpoints); i++) {
		simd_player.revpoints[i] = 20 + (i % 50);
	}
	simd_player.reserved_handling_words[SIMD_NEEDLE_COLORS_INDEX] = 15;
	simd_player.spdnumpoints = 20;
	simd_player.revnumpoints = 20;
	simd_player.spdcenter.py = (scenario % 3) - 1;
	simd_player.revcenter.px = 40;
	simd_player.revcenter.py = 80;
	optional_shapes = (scenario / 3) % 2;
	video_uses_page_flipping = (scenario / 6) % 2;
	dashboard_buffer_index = (scenario / 12) % 2;
	frame_buffer_index = (scenario / 24) % 2;
	video_shape_width_scale = 1;
	video_x_alignment_mask = -1;
	height_above_replaybar = 180;
	meter_needle_color = 15;
	memcpy(gameconfig.game_playercarid, "PMIN", 4);
	sprite_count = 0;
	live_sprite_count = 0;
	full_redraw_frames_remaining = 0;
}
/* Full-entry traces cover resource lifetime, both buffers, mouse ordering,
 * cache invalidation, wheel movement, and the 99/100/199/200 digit boundaries. */
static void run_scenario(legacy_u32 scenario)
{
	initialize_scenario(scenario);
	setup_car_shapes(DASHBOARD_OPERATION_LOAD);
	setup_car_shapes(DASHBOARD_OPERATION_REDRAW_STATIC);
	static const legacy_s16 steering[] = {-88, -80, 0, 80, 88, 0};
	static const legacy_u16 speeds[] = {0, 99, 100, 199, 200, 255};
	for (legacy_u32 i = 0; i < 6; i++) {
		state.playerstate.car_steeringAngle = steering[i];
		state.playerstate.car_rev_speed = speeds[i] << 8;
		state.playerstate.car_currpm = i * 600;
		state.playerstate.car_knob_x = i + 10;
		state.playerstate.car_knob_y = i + 15;
		state.playerstate.car_changing_gear = i % 2;
		state.playerstate.car_gear_change_delay = i % 3;
		full_redraw_frames_remaining = i % 2;
		setup_car_shapes(DASHBOARD_OPERATION_UPDATE);
		capture_cache((legacy_u8)dashboard_buffer_index);
		setup_car_shapes(DASHBOARD_OPERATION_UPDATE);
		capture_cache((legacy_u8)dashboard_buffer_index);
	}
	setup_car_shapes(DASHBOARD_OPERATION_UNLOAD);
	setup_car_shapes(-1);
}
/* Alternating compositions must copy only their own rectangle, even when the
 * physical scratch bitmap is larger. Check both containment directions and
 * crossed dimensions requiring separate buffers, plus screen/replay clipping. */
static void test_dashboard_scratch_pixels(void)
{
	static const struct {
		legacy_s16 dash_x, dash_y;
		legacy_u16 dash_width, dash_height;
		legacy_s16 gear_x, gear_y;
		legacy_u16 gear_width, gear_height;
		legacy_u16 instrument_width, instrument_height;
		legacy_u16 window_count;
	} cases[] = {
		{0, 100, 320, 100, 238, 143, 64, 56, 188, 75, 1},
		{11, 87, 300, 113, 211, 130, 91, 46, 40, 30, 1},
		{50, 120, 240, 60, 38, 110, 80, 83, 120, 40, 2},
		{0, 100, 320, 100, 309, 174, 31, 24, 60, 48, 1},
		{0, 100, 320, 100, -5, 150, 60, 50, 70, 60, 1},
		{0, 100, 320, 100, 220, 130, 60, 50, 80, 40, 2},
	};
	for (legacy_u32 mode = 0; mode < 2; mode++) {
		for (legacy_u32 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
			initialize_scenario(mode * 6);
			capture_pixels = 1;
			shapes[30].position_x = cases[index].dash_x;
			shapes[30].position_y = cases[index].dash_y;
			shapes[30].width = cases[index].dash_width;
			shapes[30].height = cases[index].dash_height;
			shapes[4].position_x = cases[index].gear_x;
			shapes[4].position_y = cases[index].gear_y;
			shapes[4].width = cases[index].gear_width;
			shapes[4].height = cases[index].gear_height;
			shapes[3].position_x = 20;
			shapes[3].position_y = 104;
			shapes[3].width = cases[index].instrument_width;
			shapes[3].height = cases[index].instrument_height;
			shapes[38].width = shapes[39].width = 320;
			shapes[38].height = shapes[39].height = 200;
			setup_car_shapes(DASHBOARD_OPERATION_LOAD);
			assert(sprite_count == cases[index].window_count);
			assert(dashbmp_y == cases[index].dash_y);
			setup_car_shapes(DASHBOARD_OPERATION_REDRAW_STATIC);
			struct SPRITE second_context = drawing_context[1];
			for (legacy_u32 pass = 0; pass < 4; pass++) {
				/* Pass 1 only changes gauges, pass 2 only hides the knob;
				 * the other passes compose both panels in sequence. */
				legacy_s32 draw_gear = pass != 1;
				legacy_s32 draw_instruments = pass != 2;
				state.playerstate.car_changing_gear = pass == 2 ? 0 : 1;
				state.playerstate.car_gear_change_delay = pass == 2 ? 0 : 1;
				if (draw_instruments) {
					state.playerstate.car_currpm = (pass + 1) * 600;
				}
				memset(pixel_buffers[0], 255, sizeof(pixel_buffers[0]));
				memset(pixel_buffers[1], 255, sizeof(pixel_buffers[1]));
				memset(pixel_buffers[2], 253, sizeof(pixel_buffers[2]));
				memset(pixel_buffers[3], 254, sizeof(pixel_buffers[3]));
				setup_car_shapes(DASHBOARD_OPERATION_UPDATE);
				assert(dashboard_gear_knob_visible_cache[0] == (pass != 2));
				assert(drawing_context[0].sprite_bitmapptr == &shapes[38 + mode]);
				assert(drawing_context[0].sprite_left == 0);
				assert(drawing_context[0].sprite_right == 320);
				assert(drawing_context[0].sprite_top == 0);
				assert(drawing_context[0].sprite_bottom == height_above_replaybar);
				assert(memcmp(&second_context, &drawing_context[1], sizeof(second_context)) == 0);
				for (legacy_u32 page = 0; page < 2; page++) {
					for (legacy_s16 y = 0; y < 200; y++) {
						for (legacy_s16 x = 0; x < 320; x++) {
							legacy_u8 expected = (legacy_u8)(253 + page);
							if (page == mode && y < height_above_replaybar) {
								if (draw_gear && x >= cases[index].gear_x &&
									y >= cases[index].gear_y &&
									x < cases[index].gear_x + cases[index].gear_width &&
									y < cases[index].gear_y + cases[index].gear_height) {
									expected = pass == 2 ? 0 : 251;
									if (pass == 2 && x >= cases[index].dash_x &&
										y >= cases[index].dash_y &&
										x < cases[index].dash_x + cases[index].dash_width &&
										y < cases[index].dash_y + cases[index].dash_height) {
										expected = background_pixel(x - cases[index].dash_x,
																	y - cases[index].dash_y);
									}
								}
								if (draw_instruments && x >= 20 && y >= 104 &&
									x < 20 + cases[index].instrument_width &&
									y < 104 + cases[index].instrument_height) {
									expected = instrument_pixel(x - 20, y - 104);
								}
							}
							assert(pixel_buffers[2 + page][y * 320 + x] == expected);
						}
					}
				}
			}
			setup_car_shapes(DASHBOARD_OPERATION_UNLOAD);
			assert(live_sprite_count == 0);
			capture_pixels = 0;
		}
	}
}
static void test_needle_colors(void)
{
	static const struct {
		legacy_u16 resource_word;
		legacy_u16 speed_color;
		legacy_u16 rpm_color;
	} cases[] = {
		{0x0010, 16, 16}, /* Stock cars retain palette index 16 for both needles. */
		{0x0000, 0, 0},	  {0x005F, 95, 95},	  {0x0080, 128, 128}, {0x00FF, 255, 255},
		{0x0F04, 4, 15},  {0xFF80, 128, 255}, {0x80FF, 255, 128}, {0x8000, 0, 128},
	};
	for (legacy_u32 i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		for (legacy_s16 speed_center_y = -1; speed_center_y <= 1; speed_center_y++) {
			initialize_scenario(0);
			legacy_u8 resource[SIMD_RESOURCE_SIZE] = {0};
			/* Red #5 is a little-endian word at byte 0xAE in the car's simd resource. */
			LEGACY_WRITE_U16_LE(resource + 0xAE, cases[i].resource_word);
			assert(simd_decode(&simd_player, resource) == SIMD_RESOURCE_SIZE);
			simd_player.spdcenter.px = 10;
			simd_player.spdcenter.py = speed_center_y;
			simd_player.spdnumpoints = 1;
			simd_player.spdpoints[0] = 20;
			simd_player.spdpoints[1] = 30;
			simd_player.revcenter.px = 40;
			simd_player.revcenter.py = 50;
			simd_player.revnumpoints = 1;
			simd_player.revpoints[0] = 60;
			simd_player.revpoints[1] = 70;
			meter_needle_color = 66;
			setup_car_shapes(DASHBOARD_OPERATION_LOAD);
			setup_car_shapes(DASHBOARD_OPERATION_REDRAW_STATIC);
			needle_line_count = 0;
			capture_needle_lines = 1;
			setup_car_shapes(DASHBOARD_OPERATION_UPDATE);
			capture_needle_lines = 0;
			legacy_u32 rpm_line_index = 0;
			if (speed_center_y == 1) {
				assert(needle_line_count == 2);
				assert(needle_lines[0].x == 10 && needle_lines[0].y == 1);
				assert(needle_lines[0].x2 == 20 && needle_lines[0].y2 == 30);
				assert(needle_lines[0].color == cases[i].speed_color);
				rpm_line_index = 1;
			} else {
				assert(needle_line_count == 1);
			}
			assert(needle_lines[rpm_line_index].x == 40 && needle_lines[rpm_line_index].y == 50);
			assert(needle_lines[rpm_line_index].x2 == 60 && needle_lines[rpm_line_index].y2 == 70);
			assert(needle_lines[rpm_line_index].color == cases[i].rpm_color);
			setup_car_shapes(DASHBOARD_OPERATION_UNLOAD);
		}
	}
}
static void initialize_transform_fixture(legacy_u32 mode, legacy_u16 descriptors)
{
	initialize_scenario(mode * 6);
	capture_pixels = 1;
	capture_live_pixels = 1;
	optional_instrument_transforms = descriptors;
	transformed_panel_count = 0;
	wheel_draw_count = 0;
	wheel_mask_count = 0;
	simd_player.spdcenter.py = 0;
	static const legacy_s8 digit_positions[] = {2, 4, 5, 4, 8, 4};
	memcpy(simd_player.spdpoints, digit_positions, sizeof(digit_positions));
	simd_player.revcenter.px = 10;
	simd_player.revcenter.py = 1;
	simd_player.revnumpoints = 2;
	simd_player.revpoints[0] = 11;
	simd_player.revpoints[1] = 1;
	simd_player.revpoints[2] = 11;
	simd_player.revpoints[3] = 2;
	simd_player.reserved_handling_words[SIMD_NEEDLE_COLORS_INDEX] = TEST_RPM_COLOR;
	shapes[3].width = TEST_PANEL_WIDTH;
	shapes[3].height = TEST_PANEL_HEIGHT;
	shapes[3].position_x = TEST_PANEL_X;
	shapes[3].position_y = TEST_PANEL_Y;
	shapes[3].centre_x = TEST_PANEL_PIVOT_X;
	shapes[3].centre_y = TEST_PANEL_PIVOT_Y;
	/* Exercise an instrument source sharing a larger gearbox scratch bitmap. */
	shapes[4].width = shapes[4].height = TEST_GEARBOX_SIZE;
	for (legacy_u16 index = 0; index < 3; index++) {
		shapes[index].width = shapes[index].height = TEST_WHEEL_SIZE;
		shapes[index].position_x = TEST_WHEEL_X;
		shapes[index].position_y = TEST_WHEEL_Y;
	}
	for (legacy_u16 index = 20; index < 30; index++) {
		shapes[index].width = 2;
		shapes[index].height = 3;
	}
	for (legacy_u16 index = TEST_TRANSFORM_LEFT_SHAPE; index < TEST_SHAPE_COUNT; index++) {
		shapes[index].width = shapes[index].height = 1;
		shapes[index].centre_x = 0;
		shapes[index].centre_y = index == TEST_TRANSFORM_LEFT_SHAPE
									 ? (legacy_u16)-SHAPE2D_TRANSFORM_ONE
									 : SHAPE2D_TRANSFORM_ONE;
		shapes[index].position_x = index == TEST_TRANSFORM_LEFT_SHAPE
									   ? SHAPE2D_TRANSFORM_ONE
									   : (legacy_u16)-SHAPE2D_TRANSFORM_ONE;
		shapes[index].position_y = 0;
	}
	shapes[30].position_x = 0;
	shapes[30].position_y = 100;
	shapes[30].width = 320;
	shapes[30].height = 100;
	shapes[38].width = shapes[39].width = 320;
	shapes[38].height = shapes[39].height = 200;
	memset(pixel_buffers, 253, sizeof(pixel_buffers));
}
static void assert_transform_panel(legacy_s16 inverse_xy)
{
	assert(last_transform.source_width == TEST_PANEL_WIDTH);
	assert(last_transform.source_height == TEST_PANEL_HEIGHT);
	assert(last_transform.source_pivot_x == TEST_PANEL_PIVOT_X);
	assert(last_transform.source_pivot_y == TEST_PANEL_PIVOT_Y);
	assert(last_transform.target_pivot_x == TEST_PANEL_X + TEST_PANEL_PIVOT_X);
	assert(last_transform.target_pivot_y == TEST_PANEL_Y + TEST_PANEL_PIVOT_Y);
	assert(last_transform.left == TEST_WHEEL_X);
	assert(last_transform.top == TEST_WHEEL_Y);
	assert(last_transform.right == TEST_WHEEL_X + TEST_WHEEL_SIZE);
	assert(last_transform.bottom == TEST_WHEEL_Y + TEST_WHEEL_SIZE);
	assert(last_transform.inverse_xy == inverse_xy);
	assert(last_transform.inverse_yx == -inverse_xy);
	assert(last_transform.inverse_xx == (inverse_xy == 0 ? SHAPE2D_TRANSFORM_ONE : 0));
	assert(last_transform.inverse_yy == last_transform.inverse_xx);
}
/* Opt-in displays must follow all wheel poses and refresh their entire source
 * panel on either gauge change. Repaint the wheel before copying fresh digits
 * so transparent corners and shorter numbers cannot retain old display pixels. */
static void test_rotating_instrument_panel(void)
{
	for (legacy_u32 mode = 0; mode < 2; mode++) {
		initialize_transform_fixture(mode, 3);
		setup_car_shapes(DASHBOARD_OPERATION_LOAD);
		for (legacy_u16 page = 0; page < 2; page++) {
			dashboard_buffer_index = page;
			frame_buffer_index = page;
			if (mode != 0) {
				sprite_select_mcga_backbuffer();
			} else {
				sprite_select_screen();
			}
			setup_car_shapes(DASHBOARD_OPERATION_REDRAW_STATIC);
			state.playerstate.car_steeringAngle = -TEST_FULL_STEERING;
			state.playerstate.car_rev_speed = 199U << 8;
			state.playerstate.car_currpm = 0;
			legacy_u32 before = transformed_panel_count;
			setup_car_shapes(DASHBOARD_OPERATION_UPDATE);
			assert(transformed_panel_count == before + 1);
			assert_transform_panel(-SHAPE2D_TRANSFORM_ONE);
			legacy_u8 *pixels = pixel_buffers[2 + mode];
			assert(pixels[133 * 320 + 105] == TEST_DIGIT_COLOR + 9);
			assert(pixels[141 * 320 + 101] == TEST_WHEEL_COLOR);
			legacy_u32 wheels = wheel_draw_count;
			setup_car_shapes(DASHBOARD_OPERATION_UPDATE);
			assert(transformed_panel_count == before + 1);
			assert(wheel_draw_count == wheels);

			/* Removing two digits must restore the panel beneath them. */
			state.playerstate.car_rev_speed = 7U << 8;
			setup_car_shapes(DASHBOARD_OPERATION_UPDATE);
			assert(transformed_panel_count == before + 2);
			assert(wheel_draw_count == ++wheels);
			assert(pixels[133 * 320 + 105] == TEST_DIGIT_COLOR + 7);
			assert(pixels[139 * 320 + 105] == instrument_pixel(2, 4));
			assert(pixels[136 * 320 + 105] == instrument_pixel(5, 4));
			assert(pixels[130 * 320 + 102] == TEST_RPM_COLOR);

			/* RPM-only changes also rebuild and rotate the live source. */
			state.playerstate.car_currpm = 128;
			setup_car_shapes(DASHBOARD_OPERATION_UPDATE);
			assert(transformed_panel_count == before + 3);
			assert(wheel_draw_count == ++wheels);
			assert(pixels[130 * 320 + 102] == instrument_pixel(11, 1));
			assert(pixels[130 * 320 + 103] == TEST_RPM_COLOR);

			/* A pose change with identical gauges still repositions them. */
			state.playerstate.car_steeringAngle = TEST_FULL_STEERING;
			setup_car_shapes(DASHBOARD_OPERATION_UPDATE);
			assert(transformed_panel_count == before + 4);
			assert(wheel_draw_count == ++wheels);
			assert_transform_panel(SHAPE2D_TRANSFORM_ONE);
			assert(pixels[137 * 320 + 107] == TEST_DIGIT_COLOR + 7);
			assert(pixels[133 * 320 + 105] == instrument_pixel(4, 6));
			assert(pixels[135 * 320 + 101] == TEST_WHEEL_COLOR + 2);
			state.playerstate.car_steeringAngle = 0;
			setup_car_shapes(DASHBOARD_OPERATION_UPDATE);
			assert(transformed_panel_count == before + 5);
			assert(wheel_draw_count == ++wheels);
			assert_transform_panel(0);
			assert(pixels[134 * 320 + 108] == TEST_DIGIT_COLOR + 7);
			assert(wheel_mask_count == 0);
			full_redraw_frames_remaining = 1;
			setup_car_shapes(DASHBOARD_OPERATION_UPDATE);
			assert(transformed_panel_count == before + 6);
			assert(wheel_draw_count == ++wheels);
			full_redraw_frames_remaining = 0;
		}
		/* The transformed output envelope must respect the replay bar. */
		height_above_replaybar = TEST_PANEL_Y + TEST_PANEL_PIVOT_Y;
		full_redraw_frames_remaining = 1;
		setup_car_shapes(DASHBOARD_OPERATION_UPDATE);
		assert(last_transform.bottom == height_above_replaybar);
		assert(drawing_context[0].sprite_bottom == height_above_replaybar);
		setup_car_shapes(DASHBOARD_OPERATION_UNLOAD);
		assert(live_sprite_count == 0);
		capture_live_pixels = 0;
		capture_pixels = 0;
	}
	/* A missing or malformed direction must retain the fixed-panel path. */
	for (legacy_u16 descriptors = 0; descriptors <= 3; descriptors++) {
		initialize_transform_fixture(0, descriptors);
		shapes[TEST_TRANSFORM_RIGHT_SHAPE].width = 2;
		setup_car_shapes(DASHBOARD_OPERATION_LOAD);
		setup_car_shapes(DASHBOARD_OPERATION_REDRAW_STATIC);
		state.playerstate.car_steeringAngle = TEST_FULL_STEERING;
		setup_car_shapes(DASHBOARD_OPERATION_UPDATE);
		assert(transformed_panel_count == 0);
		state.playerstate.car_steeringAngle = -TEST_FULL_STEERING;
		setup_car_shapes(DASHBOARD_OPERATION_UPDATE);
		assert(transformed_panel_count == ((descriptors & 1U) != 0 ? 1U : 0U));
		setup_car_shapes(DASHBOARD_OPERATION_UNLOAD);
		capture_live_pixels = 0;
		capture_pixels = 0;
	}
}
int main(void)
{
	trace = 2166136261UL;
	for (legacy_u32 scenario = 0; scenario < 48; scenario++) {
		run_scenario(scenario);
	}
	assert(trace == 0x62cfc3d5UL);
	test_needle_colors();
	test_dashboard_scratch_pixels();
	test_rotating_instrument_panel();
	puts("Dashboard snapshots, needle colors, scratch pixels, and rotating instruments passed.");
	return 0;
}
