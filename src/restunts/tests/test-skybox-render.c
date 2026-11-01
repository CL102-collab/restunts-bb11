#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "../c/externs.h"
#include "../c/frame/frame_internal.h"
#include "../c/shape2d/shape2d.h"
#include "../c/shape2d/shape2d_internal.h"
#include "../c/skybox.h"
#include "../c/projection.h"

legacy_u8 atantable[257];
legacy_u8 vector_saved_z_low;
legacy_u8 vector_saved_z_high;
legacy_s16 video_x_alignment = 1;
legacy_s16 video_x_alignment_mask = -1;

static struct SHAPE2D images[4];
static legacy_u32 call_hash;
static legacy_u32 clear_count, image_count, polygon_count;
static legacy_u8 clear_color;
static struct POINT2D first_polygon[4];

static void record_word(legacy_u16 value)
{
	call_hash = (call_hash ^ value) * 16777619UL;
}

void fatal_error(const legacy_s8 *format, ...)
{
	(void)format;
	abort();
}

void sprite_set_target_clip_bounds(legacy_u16 left, legacy_u16 right, legacy_u16 top,
								   legacy_u16 bottom)
{
	record_word(1);
	record_word(left);
	record_word(right);
	record_word(top);
	record_word(bottom);
	drawing_sprite.sprite_raster_left = left;
	drawing_sprite.sprite_raster_right = right;
	drawing_sprite.sprite_top = top;
	drawing_sprite.sprite_bottom = bottom;
}

void sprite_clear_target(legacy_u8 color)
{
	record_word(2);
	record_word(color);
	clear_color = color;
	clear_count++;
}

void sprite_copy_image_at(struct SHAPE2D far *shape, legacy_s16 x, legacy_s16 y)
{
	record_word(3);
	record_word((legacy_u16)(shape - images));
	record_word((legacy_u16)x);
	record_word((legacy_u16)y);
	image_count++;
}

void skybox_fill_polygon(legacy_u16 color, legacy_u16 count, struct POINT2D *points)
{
	record_word(4);
	record_word(color);
	record_word(count);
	for (legacy_u32 i = 0; i < count; i++) {
		if (polygon_count == 0 && i < 4U) {
			first_polygon[i] = points[i];
		}
		record_word((legacy_u16)points[i].px);
		record_word((legacy_u16)points[i].py);
	}
	polygon_count++;
}

static void reset_scene(void)
{
	shape3d_set_legacy_render_stack(0, 0, 0, 0);
	call_hash = 2166136261UL;
	clear_count = 0;
	image_count = 0;
	polygon_count = 0;
	projection_focal_length_x = 256;
	projection_focal_length_y = 256;
	projection_center_x = 160;
	projection_center_y = 100;
	skybox.minimum_height = 20;
	skybox.maximum_height = 40;
	skybox.sky_color = 3;
	skybox.ground_color = 6;
	for (legacy_u32 i = 0; i < 4; i++) {
		skybox.heights[i] = 20 + i * 5;
		skyboxes[i] = &images[i];
	}
	for (legacy_u32 i = 0; i < 15; i++) {
		frame_rects_page0[i].left = i * 20;
		frame_rects_page0[i].right = i * 20 + 20;
		frame_rects_page0[i].top = 30 + i * 3;
		frame_rects_page0[i].bottom = 60 + i * 3;
		frame_layer_rects[i] = frame_rects_page0[i];
		frame_rect_change_flags[i] = i % 4;
	}
	active_frame_rects = frame_rects_page0;
	memset(merged_redraw_rects, 0, 45 * sizeof(merged_redraw_rects[0]));
	redraw_rect_count = 0;
	frame_buffer_camera_headings[0] = 0;
	frame_buffer_camera_headings[1] = 0;
	last_rendered_camera_heading = 0;
	detail_level = 0;
	full_redraw_frames_remaining = 0;
	slow_video_mgmt_copy = 0;
}

static void test_level_horizon(void)
{
	reset_scene();
	struct MATRIX rotation = *mat_rot_zxy(0, 0, 0, MATRIX_ROTATION_ORDER_ZXY);
	struct RECTANGLE clip = {0, 320, 20, 180};
	assert(skybox_render(0, &clip, 1, &rotation, 0, 0, 0) == 0);
	assert(clear_count == 2);
	assert(image_count == 5);
	assert(polygon_count == 0);
	assert(drawing_sprite.sprite_top == 100);
	assert(drawing_sprite.sprite_bottom == 180);

	reset_scene();
	slow_video_mgmt_copy = 1;
	assert(skybox_render(0, &clip, -1, &rotation, 0, 0, 0) == 1);
	assert(clear_count == 1);
	assert(image_count == 0);
	assert(rect_skybox.left == 0);
	assert(rect_skybox.right == 320);
	assert(rect_skybox.top == 20);
	assert(rect_skybox.bottom == 180);
}

static void test_rolled_full_viewport_redraw(void)
{
	static const struct {
		legacy_s16 roll, direction, camera_y;
		legacy_u8 color;
	} cases[] = {
		{1, -1, 0, 3}, {256, 1, 16000, 3}, {256, 1, -16000, 6}, {1, 1, 12000, 3}, {1, 1, -12000, 6},
	};
	/* Real projections put the horizon behind the camera, then beyond the
	 * right, left, bottom and top edges. Every whole-viewport fill must tell
	 * frame_finish to retain the full clip for presentation and the next frame.
	 * Otherwise a following level view can leave old ground in the sky. */
	for (legacy_u32 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		for (legacy_s16 slow_copy = 0; slow_copy <= 1; slow_copy++) {
			for (legacy_s16 detail = 0; detail <= 4; detail++) {
				reset_scene();
				slow_video_mgmt_copy = slow_copy;
				detail_level = detail;
				struct MATRIX rotation =
					*mat_rot_zxy(cases[index].roll, 0, 0, MATRIX_ROTATION_ORDER_ZXY);
				struct RECTANGLE clip = {0, 320, 20, 180};
				assert(skybox_render(0, &clip, cases[index].direction, &rotation, cases[index].roll,
									 0, cases[index].camera_y) == 1);
				assert(clear_count == 1 && clear_color == cases[index].color);
				assert(image_count == 0 && polygon_count == 0);
				assert(drawing_sprite.sprite_raster_left == clip.left);
				assert(drawing_sprite.sprite_raster_right == clip.right);
				assert(drawing_sprite.sprite_top == clip.top);
				assert(drawing_sprite.sprite_bottom == clip.bottom);
			}
		}
	}
}

static void prepare_legacy_handoff(legacy_s16 *player, legacy_s16 *opponent)
{
	player[0] = 11;
	player[1] = 22;
	player[2] = 33;
	player[3] = 44;
	opponent[0] = 55;
	opponent[1] = 66;
	opponent[2] = 77;
	opponent[3] = -6547;
	struct SHAPE3D_LEGACY_OPPONENT_RENDER_CONTEXT context;
	context.wheel_headings = opponent;
	context.polyinfo_offset = 0;
	context.polyinfo_segment = 0;
	context.material_color_offset = 0;
	shape3d_set_legacy_render_stack(player, 0, 0, &context);
}

static void assert_words(const legacy_s16 *words, legacy_s16 first, legacy_s16 second,
						 legacy_s16 third, legacy_s16 fourth)
{
	assert(words[0] == first && words[1] == second && words[2] == third && words[3] == fourth);
}

static void test_legacy_skybox_handoff(void)
{
	reset_scene();
	legacy_s16 opponent[4];
	legacy_s16 player[4];
	prepare_legacy_handoff(player, opponent);
	struct MATRIX rotation = *mat_rot_zxy(0, 0, 0, MATRIX_ROTATION_ORDER_ZXY);
	struct RECTANGLE clip = {0, 320, 37, 180};
	assert(skybox_render(0, &clip, 1, &rotation, 0, 0, 0) == 0);
	/* The archived fourth heading becomes clip.top, not a fixed zero. */
	assert_words(player, 11, 22, 33, 44);
	assert_words(opponent, 55, 0, 320, 37);

	reset_scene();
	prepare_legacy_handoff(player, opponent);
	shape3d_set_legacy_render_stack(0, 0, 0, 0);
	assert(skybox_render(0, &clip, 1, &rotation, 0, 0, 0) == 0);
	assert_words(player, 11, 22, 33, 44);
	assert_words(opponent, 55, 66, 77, -6547);

	reset_scene();
	prepare_legacy_handoff(player, opponent);
	/* A sky-only early return never assigns the original local rectangle. */
	assert(skybox_render(0, &clip, -1, &rotation, 0, 0, 0) == 0);
	assert_words(player, 11, 22, 33, 44);
	assert_words(opponent, 55, 66, 77, -6547);
	/* The inverted horizon uses direct clip arguments in the original,
	 * although the C implementation uses temporary rectangles to draw it. */
	rotation = *mat_rot_zxy(0, 512, 0, MATRIX_ROTATION_ORDER_ZXY);
	assert(skybox_render(0, &clip, -1, &rotation, 0, 0, 0) == 1);
	assert_words(player, 11, 22, 33, 44);
	assert_words(opponent, 55, 66, 77, -6547);

	reset_scene();
	prepare_legacy_handoff(player, opponent);
	rotation = *mat_rot_zxy(0, 0, 0, MATRIX_ROTATION_ORDER_ZXY);
	slow_video_mgmt_copy = 1;
	assert(skybox_render(0, &clip, 1, &rotation, 0, 0, 0) == 0);
	assert_words(opponent, 55, 66, 77, -6547);
	full_redraw_frames_remaining = 1;
	assert(skybox_render(0, &clip, 1, &rotation, 0, 0, 0) == 0);
	assert_words(opponent, 55, 0, 320, 37);

	reset_scene();
	prepare_legacy_handoff(player, opponent);
	/* The rolled path can project a level horizon: its base is 100 and the
	 * single textured strip covers x=0..320. It leaves the player points alone. */
	assert(skybox_render(0, &clip, 1, &rotation, 1, 0, 0) == 0);
	assert(image_count == 5);
	assert_words(player, 11, 22, 33, 44);
	assert_words(opponent, 100, 0, 320, 37);
	prepare_legacy_handoff(player, opponent);
	clip.left = 400;
	clip.right = 500;
	/* Rejecting the strip rectangle still preserves the writes before clipping. */
	assert(skybox_render(0, &clip, 1, &rotation, 1, 0, 0) == 0);
	assert_words(opponent, 100, 0, 320, 37);

	reset_scene();
	prepare_legacy_handoff(player, opponent);
	clip.left = 0;
	clip.right = 320;
	rotation = *mat_rot_zxy(256, 0, 0, MATRIX_ROTATION_ORDER_ZXY);
	assert(skybox_render(0, &clip, 1, &rotation, 1, 0, 0) == 1);
	assert(polygon_count == 2);
	/* Original local points 2 and 3 precede their reversed argument order
	 * in the sky polygon and the C array's reuse for the ground polygon. */
	assert_words(player, first_polygon[3].px, first_polygon[3].py, first_polygon[2].px,
				 first_polygon[2].py);
	assert_words(opponent, 55, 66, 77, -6547);
	shape3d_set_legacy_render_stack(0, 0, 0, 0);
}

static legacy_u16 random_word(legacy_u32 *seed)
{
	*seed = *seed * 1664525UL + 1013904223UL;
	return (legacy_u16)(*seed >> 16);
}

static legacy_u32 skybox_fingerprint(legacy_s16 rolled, legacy_s16 slow_copy)
{
	legacy_u32 hash = 2166136261UL;
	struct MATRIX rotation;
	struct RECTANGLE clip;
	legacy_u32 seed = 161803UL;
	for (legacy_u32 iteration = 0; iteration < 8192; iteration++) {
		reset_scene();
		clip.left = 0;
		clip.right = 320;
		clip.top = random_word(&seed) % 80;
		clip.bottom = 120 + random_word(&seed) % 81;
		legacy_s16 angle = random_word(&seed) % 1024;
		legacy_s16 camera_y = LEGACY_S16_FROM_BITS(random_word(&seed));
		legacy_s16 direction = iteration % 2 == 0 ? 1 : -1;
		legacy_u16 rotate_z = random_word(&seed) % 1024;
		legacy_u16 rotate_x = random_word(&seed) % 1024;
		legacy_u16 rotate_y = random_word(&seed) % 1024;
		rotation = *mat_rot_zxy(rotate_z, rotate_x, rotate_y, MATRIX_ROTATION_ORDER_ZXY);
		if (iteration % 3 == 0) {
			/* Small bank angles exercise the textured horizon-strip path. */
			rotation = *mat_rot_zxy(iteration % 32, 0, 0, MATRIX_ROTATION_ORDER_ZXY);
			camera_y = iteration % 1500;
			direction = 1;
		}
		detail_level = iteration % 5;
		slow_video_mgmt_copy = slow_copy;
		full_redraw_frames_remaining = iteration % 3 == 1;
		last_rendered_camera_heading = angle;
		frame_buffer_camera_headings[iteration % 2] = angle;
		legacy_s16 result =
			skybox_render(iteration % 2, &clip, direction, &rotation, rolled, angle, camera_y);
		record_word((legacy_u16)result);
		record_word((legacy_u16)redraw_rect_count);
		for (legacy_u32 i = 0; i < 15; i++) {
			record_word((legacy_u16)frame_rect_change_flags[i]);
			record_word((legacy_u16)frame_layer_rects[i].left);
			record_word((legacy_u16)frame_layer_rects[i].right);
			record_word((legacy_u16)frame_layer_rects[i].top);
			record_word((legacy_u16)frame_layer_rects[i].bottom);
		}
		for (legacy_u32 i = 0; i < (legacy_u32)redraw_rect_count; i++) {
			record_word((legacy_u16)merged_redraw_rects[i].left);
			record_word((legacy_u16)merged_redraw_rects[i].right);
			record_word((legacy_u16)merged_redraw_rects[i].top);
			record_word((legacy_u16)merged_redraw_rects[i].bottom);
		}
		record_word((legacy_u16)frame_buffer_camera_headings[0]);
		record_word((legacy_u16)frame_buffer_camera_headings[1]);
		hash = (hash ^ call_hash) * 16777619UL;
	}
	return hash;
}

int main(void)
{
	/* Geometry and rectangle merging use the real implementations. Raster
	 * callbacks fingerprint their arguments and order without a video device.
	 * Rolled-view baselines include the original long-line slope rounding and
	 * full-redraw return flag for whole-viewport fills. Each random camera
	 * coordinate now uses a single draw before signed conversion. */
	static const legacy_u32 expected[] = {0xd50e9f01UL, 0x119b140bUL, 0xef627bc6UL, 0xd202c0d0UL};

	test_level_horizon();
	test_rolled_full_viewport_redraw();
	test_legacy_skybox_handoff();
	for (legacy_u32 i = 0; i < 4; i++) {
#ifdef SKYBOX_RECORD_BASELINE
		fprintf(stdout, "skybox%" LEGACY_PRIu32 "=%08" LEGACY_PRIx32 "\n", i,
				skybox_fingerprint(i & 1U, i >> 1U));
#else
		assert(skybox_fingerprint(i & 1U, i >> 1U) == expected[i]);
#endif
	}
	return 0;
}
