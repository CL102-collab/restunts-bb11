#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../c/fileio.h"
#include "../c/legacy.h"
#include "../c/memmgr.h"
#include "../c/menu_internal.h"
#include "../c/platform.h"
#include "../c/shape2d.h"
#include "../c/shape3d.h"
#include "../c/ui_text.h"
#include "../c/timing.h"
#include "../c/game_input.h"
#include "../c/ui_input.h"
#include "../c/ui_dialog.h"
#include "../c/car_speed.h"
#include "../c/video_frame.h"
#include "../c/car_model.h"
#include "../c/scene_resources.h"
#include "../c/car_resources.h"
#include "../c/menu_common.h"
#include "../c/externs.h"
#include "../c/keyboard.h"
#include "../c/frame_internal.h"
#include "../c/shape3d_hires.h"
#include "../c/opponent_portrait.h"
#include "../c/menu_background.h"
#include "../c/shape2d_internal.h"

#undef printf

struct SPRITE *render_window_sprite;

static legacy_u64 trace_hash = UINT64_C(1469598103934665603);
static legacy_u32 scenario, frame_index, file_index, allocation_index, sprite_index;
static legacy_u32 acceleration_step;
static legacy_u8 resource_bytes[64][32];
static struct SHAPE2D fixture_shapes[4];
static struct SPRITE fixture_sprites[4];
static const legacy_s8 *fixture_files[] = {(const legacy_s8 *)"CARVETT.RES",
										   (const legacy_s8 *)"CARANSX.RES",
										   (const legacy_s8 *)"CARCOUN.RES"};

enum SHOWROOM_TEST_SELECTION {
	SHOWROOM_TEST_DONE,
	SHOWROOM_TEST_NEXT,
	SHOWROOM_TEST_PREVIOUS,
	SHOWROOM_TEST_TRANSMISSION,
	SHOWROOM_TEST_COLOR,
	SHOWROOM_TEST_SELECTION_COUNT
};

enum SHOWROOM_TEST_ESCAPE_MODE {
	SHOWROOM_TEST_ESCAPE_KEYBOARD,
	SHOWROOM_TEST_ESCAPE_MOUSE,
	SHOWROOM_TEST_ESCAPE_BROWSED,
	SHOWROOM_TEST_ESCAPE_MODE_COUNT
};

static legacy_u8 escape_test, escape_selection, escape_mode;
static legacy_u16 escape_opponent;
static const legacy_s16 escape_browse_selections[] = {
	SHOWROOM_TEST_NEXT, SHOWROOM_TEST_TRANSMISSION, SHOWROOM_TEST_COLOR};
#define SHOWROOM_TEST_BROWSE_ACTION_COUNT                                                          \
	(sizeof(escape_browse_selections) / sizeof(escape_browse_selections[0]))

#define SHOWROOM_TEST_MODEL_SCALE 20
#define SHOWROOM_TEST_SCREEN_WIDTH 320
#define SHOWROOM_TEST_BACKGROUND_HEIGHT 103
#define SHOWROOM_TEST_HALF_WIDTH 48
#define SHOWROOM_TEST_HALF_LENGTH 103
#define SHOWROOM_TEST_FIRST_GROUND_HEIGHT 37
#define SHOWROOM_TEST_NEXT_GROUND_HEIGHT -61
#define SHOWROOM_TEST_STATUS_EXPIRY_FRAME 18U
#define SHOWROOM_TEST_LOW_LIGHT_MINIMUM_COTANGENT 1.0

enum SHOWROOM_TEST_SHADOW_STAGE {
	SHOWROOM_TEST_SHADOW_IDLE,
	SHOWROOM_TEST_SHADOW_CAMERA,
	SHOWROOM_TEST_SHADOW_LIGHT,
	SHOWROOM_TEST_SHADOW_CAR,
	SHOWROOM_TEST_SHADOW_MODEL
};

legacy_u8 supersight_enabled;
legacy_u8 fps_display_enabled;
static legacy_u8 display_toggle_test, display_scenario, display_target, display_pending;
static legacy_u8 display_fps_drawn, display_previous_fps, display_refresh_pending;
static legacy_u8 display_status_active, display_status_expire_pending;
/* Static menus may show mode notices without using diagnostic drawing/sampling. */
static legacy_u8 status_only_test;
static legacy_u32 display_status_draw_count, display_status_expire_count, display_shift_shortcuts;
static legacy_u32 display_present_count, display_fps_draw_count, display_record_count;
static legacy_u32 display_reset_count, display_shortcut_count, display_sprite_free_count;
static struct RECTANGLE display_clip, display_fps_bounds, display_previous_fps_bounds;
static const struct RECTANGLE display_portrait_bounds = {240, 320, 0, 83};
static legacy_u8 display_portrait_pending, display_portrait_mode, display_portrait_legacy_drawn;
static legacy_u32 display_portrait_draws, display_enhanced_portrait_draws;
static legacy_u8 display_model_scaled, display_shadow_stage, display_previous_shadow;
static legacy_u8 display_shadow_restored;
static legacy_u32 display_shadow_models, display_shadow_frames[2], display_shadow_erasures;
static struct VECTOR display_shadow_camera, display_shadow_position;
static legacy_s16 display_shadow_heading, display_ground_height;
static legacy_u32 display_car_loads, display_ground_queries, display_load_present_count;
static legacy_u8 drawing_frame, completed_frame;
static legacy_u32 committed_frames;
static legacy_u8 display_background_mode, display_background_restored;
static const struct RECTANGLE display_background_bounds = {0, SHOWROOM_TEST_SCREEN_WIDTH, 0,
														   SHOWROOM_TEST_BACKGROUND_HEIGHT};

void sdl3_video_begin_track_frame(legacy_u8 adaptive)
{
	assert(adaptive == 0 && drawing_frame == 0);
	drawing_frame = 1;
	completed_frame = 0;
}

void sdl3_video_add_render_work(legacy_u64 elapsed_ns)
{
	assert(drawing_frame != 0);
	(void)elapsed_ns;
}

void sdl3_video_end_frame(void)
{
	assert(drawing_frame != 0 && completed_frame != 0);
	drawing_frame = 0;
	committed_frames++;
}

legacy_s16 shape3d_car_ground_height(const struct SHAPE3D *shape)
{
	assert(shape == &game3dshapes[PLAYER_CAR_LOW_SHAPE]);
	if (display_toggle_test != 0) {
		display_ground_queries++;
		assert(display_ground_queries == display_car_loads);
		return display_ground_height;
	}
	return 0;
}

void shape3d_hires_set_model_scale(legacy_f64 scale)
{
	assert(scale == SHOWROOM_TEST_MODEL_SCALE);
	if (display_toggle_test != 0) {
		display_model_scaled = 1;
	}
}

void shape3d_hires_shadows_begin(const struct VECTOR *camera_position)
{
	assert(supersight_enabled != 0);
	if (display_toggle_test != 0) {
		assert(display_model_scaled != 0);
		assert(display_shadow_stage == SHOWROOM_TEST_SHADOW_IDLE);
		display_shadow_camera = *camera_position;
		display_shadow_stage = SHOWROOM_TEST_SHADOW_CAMERA;
	}
}

void shape3d_hires_set_shadow_light(legacy_s16 heading, legacy_f64 cotangent)
{
	assert(supersight_enabled != 0);
	/* The showroom light stays to the left/front as either preview rotates. */
	assert(heading > 0 && heading < ANGLE_QUARTER_TURN);
	assert(cotangent > SHOWROOM_TEST_LOW_LIGHT_MINIMUM_COTANGENT);
	if (display_toggle_test != 0) {
		assert(display_shadow_stage == SHOWROOM_TEST_SHADOW_CAMERA);
		display_shadow_stage = SHOWROOM_TEST_SHADOW_LIGHT;
	}
}

void shape3d_hires_shadow_car(const struct VECTOR *relative_position, legacy_s16 heading,
							  legacy_s16 half_width, legacy_s16 half_length)
{
	assert(supersight_enabled != 0);
	if (display_toggle_test != 0) {
		assert(display_shadow_stage == SHOWROOM_TEST_SHADOW_LIGHT);
		assert(half_width == SHOWROOM_TEST_HALF_WIDTH * SHOWROOM_TEST_MODEL_SCALE);
		assert(half_length == SHOWROOM_TEST_HALF_LENGTH * SHOWROOM_TEST_MODEL_SCALE);
		display_shadow_position = *relative_position;
		display_shadow_heading = heading;
		display_shadow_stage = SHOWROOM_TEST_SHADOW_CAR;
	}
}

void shape3d_hires_shadow_projected_model(const struct SHAPE3D *shape, legacy_s16 ground_height)
{
	assert(supersight_enabled != 0);
	assert(shape == &game3dshapes[PLAYER_CAR_LOW_SHAPE]);
	if (display_toggle_test != 0) {
		assert(display_shadow_stage == SHOWROOM_TEST_SHADOW_CAR);
		assert(ground_height == display_ground_height);
		display_shadow_stage = SHOWROOM_TEST_SHADOW_MODEL;
		display_shadow_models++;
	}
}

void opponent_portrait_draw_at(const struct SPRITE *target, const struct SHAPE2D *original,
							   legacy_u8 opponent, legacy_s16 x, legacy_s16 y)
{
	assert(target == &drawing_sprite && original == &fixture_shapes[1]);
	assert(opponent == 2 && x == 240 && y == 0);
	if (display_toggle_test != 0) {
		assert((display_scenario & 4U) != 0 && display_target == 1);
		assert(display_portrait_legacy_drawn != 0);
		display_portrait_legacy_drawn = 0;
		display_portrait_mode = supersight_enabled;
		display_portrait_pending = 1;
		display_portrait_draws++;
		display_enhanced_portrait_draws += supersight_enabled != 0;
	}
}

static void assert_contains(const struct RECTANGLE *outer, const struct RECTANGLE *inner)
{
	assert(outer->left <= inner->left && outer->right >= inner->right);
	assert(outer->top <= inner->top && outer->bottom >= inner->bottom);
}

void menu_background_draw(const struct SPRITE *target, const struct SHAPE2D *original,
						  enum MENU_BACKGROUND kind)
{
	assert(target == &drawing_sprite && original == &fixture_shapes[0]);
	assert(kind == MENU_BACKGROUND_SHOWROOM);
	if (display_toggle_test != 0) {
		assert(display_target == 1);
		if (display_background_mode != supersight_enabled) {
			/* Both on/off transitions and scale presets must restore the wall,
			 * floor and the strip beneath the opponent panel, beyond the car. */
			assert_contains(&display_clip, &display_background_bounds);
		}
		display_background_mode = supersight_enabled;
		display_background_restored = 1;
	}
}

void menu_background_unload(void)
{
}

#ifdef RESTUNTS_SDL3
static void assert_shadow_floor_covered(void)
{
	struct RECTANGLE floor = carmenu_cliprect;
	if ((display_scenario & 4U) != 0) {
		floor.right = display_portrait_bounds.left;
	}
	assert_contains(&display_clip, &floor);
}

#endif

static void record_preview_copy(void)
{
	if (display_toggle_test == 0 || display_pending == 0) {
		return;
	}
	assert(display_target == 0);
	assert(display_fps_drawn == (frame_display_overlay_active() != 0));
	assert(display_reset_count != 0);
#ifdef RESTUNTS_SDL3
	if (supersight_enabled != 0 || display_previous_shadow != 0) {
		assert(display_shadow_restored != 0);
		assert_shadow_floor_covered();
		if (supersight_enabled == 0) {
			display_shadow_erasures++;
		}
	}
	display_previous_shadow = supersight_enabled;
#endif
	if (display_fps_drawn != 0) {
		assert_contains(&display_clip, &display_fps_bounds);
	}
	if (display_previous_fps != 0) {
		assert_contains(&display_clip, &display_previous_fps_bounds);
	}
#ifdef RESTUNTS_SDL3
	if ((display_scenario & 4U) != 0) {
		assert(display_portrait_mode == supersight_enabled);
	}
	if (display_portrait_pending != 0) {
		assert_contains(&display_clip, &display_portrait_bounds);
		display_portrait_pending = 0;
	}
#endif
	display_previous_fps = display_fps_drawn;
	display_previous_fps_bounds = display_fps_bounds;
	display_fps_drawn = display_pending = display_refresh_pending = 0;
	display_present_count++;
	display_load_present_count++;
	assert(display_record_count + 1U == display_present_count);
}

legacy_s16 frame_status_overlay_active(void)
{
	return display_status_active != 0;
}

legacy_s16 frame_display_overlay_active(void)
{
	assert(status_only_test == 0);
	return fps_display_enabled != 0 || frame_status_overlay_active() != 0;
}

legacy_s16 frame_status_expire_idle(void)
{
	if (display_status_active != 0 && display_status_expire_pending != 0) {
		display_status_active = display_status_expire_pending = 0;
		display_status_expire_count++;
		return 1;
	}
	return 0;
}

legacy_s16 frame_fps_expire_idle(void)
{
	assert(status_only_test == 0);
	return frame_status_expire_idle();
}

void frame_fps_reset(void)
{
	assert(status_only_test == 0);
	display_reset_count++;
}

void frame_fps_record_presented(void)
{
	assert(status_only_test == 0);
	completed_frame = 1;
	if (display_toggle_test != 0) {
		display_record_count++;
		assert(display_record_count == display_present_count);
	}
}

struct RECTANGLE *frame_status_draw_text(void)
{
	if (frame_status_overlay_active() == 0) {
		return NULL;
	}
	display_status_draw_count++;
	return &display_fps_bounds;
}

struct RECTANGLE *frame_fps_draw_text(void)
{
	assert(status_only_test == 0);
	if (frame_display_overlay_active() == 0) {
		return NULL;
	}
	if (display_status_active != 0 && fps_display_enabled == 0) {
		display_status_draw_count++;
	}
	if (display_toggle_test != 0) {
		assert(display_target == 1 && display_pending != 0);
		assert(display_fps_drawn == 0);
		display_fps_bounds.left = 8;
		display_fps_bounds.right = display_fps_draw_count % 2U == 0U ? 81 : 41;
		display_fps_bounds.top = 3;
		display_fps_bounds.bottom = 12;
		assert_contains(&display_clip, &display_fps_bounds);
		display_fps_drawn = 1;
		display_fps_draw_count++;
	}
	return &display_fps_bounds;
}
static legacy_u8 predictive_preview_test;
static legacy_u32 preview_present_count;
static legacy_s16 preview_last_rotation;
static legacy_u32 preview_physics_steps, preview_toggle_count;
static legacy_u64 preview_now, preview_previous_input_time;

legacy_u64 presentation_now(void)
{
	return preview_now;
}

legacy_s16 handle_ingame_kb_shortcuts(legacy_s16 key)
{
	assert(key == KEY_F11 || key == KEY_F12 || key == KEY_SHIFT_F12);
	if (key == KEY_F11) {
		fps_display_enabled = fps_display_enabled == FRAME_FPS_DISPLAY_OFF ? FRAME_FPS_DISPLAY_ON
																		   : FRAME_FPS_DISPLAY_OFF;
		frame_fps_reset();
	} else {
		if (key == KEY_SHIFT_F12) {
			supersight_enabled = display_status_active = 1;
			display_shift_shortcuts++;
		} else {
			supersight_enabled ^= 1U;
			display_status_active = 1;
		}
		/* F12 discards all enhanced sprite surfaces in the real renderer. */
		display_portrait_mode = 255;
		display_background_mode = LEGACY_U8_MAX;
	}
	display_shortcut_count++;
	display_refresh_pending = display_toggle_test;
	return 1;
}

static void trace_word(legacy_u16 value)
{
	trace_hash = (trace_hash ^ (value & 255U)) * UINT64_C(1099511628211);
	trace_hash = (trace_hash ^ (value >> 8)) * UINT64_C(1099511628211);
}

static void trace_text(const legacy_s8 *text)
{
	if (text == 0) {
		trace_word(65535U);
		return;
	}
	while (*text != 0) {
		trace_word((legacy_u8)*text++);
	}
	trace_word(0);
}

static void trace_pointer(const void *pointer)
{
	if (pointer == 0) {
		trace_word(0);
		return;
	}
	for (legacy_u32 i = 0; i < 64U; i++) {
		if (pointer == resource_bytes[i]) {
			trace_word(100U + i);
			return;
		}
	}
	for (legacy_u32 i = 0; i < 4U; i++) {
		if (pointer == &fixture_sprites[i]) {
			trace_word(200U + i);
			return;
		}
		if (pointer == &fixture_shapes[i]) {
			trace_word(300U + i);
			return;
		}
	}
	trace_word(1);
}

static void trace_rect(const struct RECTANGLE *rect)
{
	trace_word(rect->left);
	trace_word(rect->right);
	trace_word(rect->top);
	trace_word(rect->bottom);
}

legacy_s16 _strcmp(const legacy_s8 *dest, const legacy_s8 *src)
{
	while (*dest != 0 && *dest == *src) {
		dest++;
		src++;
	}
	return (legacy_s16)((legacy_u8)*dest - (legacy_u8)*src);
}

legacy_s8 *_strcpy(legacy_s8 *dest, const legacy_s8 *src)
{
	legacy_s8 *result = dest;
	do {
		*dest++ = *src;
	} while (*src++ != 0);
	return result;
}

void draw_button(legacy_s8 *text, legacy_s16 x, legacy_s16 y, legacy_s16 width, legacy_s16 height,
				 legacy_s16 top_color, legacy_s16 bottom_color, legacy_s16 fill_color,
				 legacy_s16 font_color)
{
	trace_word(1002);
	trace_text(text);
	trace_word((legacy_u16)x);
	trace_word((legacy_u16)y);
	trace_word((legacy_u16)width);
	trace_word((legacy_u16)height);
	trace_word((legacy_u16)top_color);
	trace_word((legacy_u16)bottom_color);
	trace_word((legacy_u16)fill_color);
	trace_word((legacy_u16)font_color);
}

void ensure_file_exists(legacy_s16 unused)
{
	trace_word(1003);
	trace_word((legacy_u16)unused);
}

const legacy_s8 *file_combine_and_find(const legacy_s8 *dir, const legacy_s8 *name,
									   const legacy_s8 *ext)
{
	trace_word(1004);
	trace_text(dir);
	trace_text(name);
	trace_text(ext);
	file_index = 0;
	if (scenario % 17U == 0U) {
		return 0;
	}
	return fixture_files[file_index++];
}

const legacy_s8 *file_find_next_alt(void)
{
	trace_word(1005);

	if (file_index < 3U) {
		return fixture_files[file_index++];
	}
	return 0;
}

void *file_load_resfile(const legacy_s8 *filename)
{
	trace_word(1006);
	trace_text(filename);
	assert(allocation_index < 64U);
	return resource_bytes[allocation_index++];
}

void *file_load_shape2d_fatal(const legacy_s8 *shapename)
{
	trace_word(1007);
	trace_text(shapename);
	assert(allocation_index < 64U);
	return resource_bytes[allocation_index++];
}

void font_draw_text(const legacy_s8 *text, legacy_s16 x, legacy_s16 y)
{
	trace_word(1008);
	trace_text(text);
	trace_word((legacy_u16)x);
	trace_word((legacy_u16)y);
}

void font_set_colors(legacy_s16 color, legacy_s16 background_color)
{
	trace_word(1009);
	trace_word((legacy_u16)color);
	trace_word((legacy_u16)background_color);
}

void font_set_fontdef(void)
{
	trace_word(1010);
}

void font_set_fontdef2(void *data)
{
	trace_word(1011);
	trace_pointer(data);
}

void init_game_state(legacy_s16 initialization_mode)
{
	trace_word(1012);
	trace_word((legacy_u16)initialization_mode);
	acceleration_step = 0;
	state.playerstate.car_rev_speed = 0;
}

legacy_s16 input_checking(legacy_s16 frame_delta)
{
	assert(drawing_frame == 0);
	trace_word(1013);
	trace_word((legacy_u16)frame_delta);
	if (escape_test != 0) {
		legacy_u32 exit_frame =
			escape_mode == SHOWROOM_TEST_ESCAPE_BROWSED
				? SHOWROOM_TEST_BROWSE_ACTION_COUNT
				: (escape_mode == SHOWROOM_TEST_ESCAPE_KEYBOARD ? escape_selection : 0);
		assert(frame_index <= exit_frame);
		return frame_index == exit_frame
				   ? KEY_ESCAPE
				   : (escape_mode == SHOWROOM_TEST_ESCAPE_BROWSED ? KEY_ENTER : KEY_DOWN);
	}
	if (display_toggle_test != 0) {
		assert(display_refresh_pending == 0);
		if (frame_index == SHOWROOM_TEST_STATUS_EXPIRY_FRAME) {
			display_status_expire_pending = 1;
			assert(frame_fps_expire_idle() != 0);
		}
		static const legacy_u16 keys[] = {
			KEY_DOWN, KEY_DOWN, KEY_DOWN, KEY_F11, 0, KEY_F12, 0,
			/* Change the model between F12 transitions to refresh its ground height. */
			KEY_UP, KEY_UP, KEY_ENTER, KEY_DOWN, KEY_DOWN, KEY_F11, KEY_F11, 0, KEY_F12, KEY_F11,
			KEY_ENTER, KEY_UP, KEY_UP, KEY_UP, KEY_ENTER};
		assert(frame_index < sizeof(keys) / sizeof(keys[0]));
#ifdef RESTUNTS_SDL3
		/* The first enable has completed; cycle a lock while keeping it enabled. */
		if (frame_index == 6 && (display_scenario & 8U) == 0) {
			return KEY_SHIFT_F12;
		}
#endif
		return keys[frame_index];
	}
	if (predictive_preview_test != 0) {
		if (supersight_enabled != 0) {
			assert(preview_now - preview_previous_input_time >= 10000000ULL);
		}
		preview_previous_input_time = preview_now;
		if (predictive_preview_test == 2 &&
			((preview_toggle_count == 0 && preview_now >= 250000000ULL) ||
			 (preview_toggle_count == 1 && preview_now >= 750000000ULL))) {
			preview_toggle_count++;
			return KEY_F12;
		}
		return preview_now >= 1000000000ULL ? KEY_ENTER : 0;
	}
	static const legacy_u16 keys[] = {0,		 KEY_DOWN, KEY_ENTER, KEY_ENTER, KEY_DOWN,
									  KEY_ENTER, KEY_DOWN, KEY_ENTER, KEY_DOWN,	 KEY_ENTER,
									  KEY_UP,	 KEY_UP,   KEY_UP,	  KEY_UP,	 KEY_ENTER};
	assert(frame_index < 100U);
	if (frame_index >= 15U) {
		return KEY_ENTER;
	}
	return keys[frame_index];
}

legacy_s8 *locate_shape_fatal(legacy_s8 *data, const legacy_s8 *name)
{
	trace_word(1014);
	trace_pointer(data);
	trace_text(name);
	return (legacy_s8 *)&fixture_shapes[0];
}

legacy_s8 *locate_text_res(legacy_s8 *data, const legacy_s8 *name)
{
	trace_word(1015);
	trace_pointer(data);
	trace_text(name);
	if (_strcmp(name, car_description_id) == 0) {
		return (legacy_s8 *)"First]Second]]Third]";
	}
	return (legacy_s8 *)name;
}

legacy_s16 menu_animate_button_highlight(legacy_s16 item_index, const struct BUTTON_AREA *buttons,
										 legacy_s16 second_color, legacy_s16 first_color)
{
	trace_word(1016);
	trace_word((legacy_u16)item_index);
	trace_pointer(buttons);
	trace_word((legacy_u16)second_color);
	trace_word((legacy_u16)first_color);
	if (predictive_preview_test != 0) {
		preview_now += 1000000ULL;
		return preview_now % 10000000ULL == 0;
	}
	preview_now += 20000000ULL;
	return (legacy_s16)(1U + frame_index % 3U);
}

void menu_reset_animation_timers(void)
{
	trace_word(1017);
}

void menu_update_idle_counter(legacy_u16 elapsed, legacy_s16 limit)
{
	trace_word(1018);
	trace_word((legacy_u16)elapsed);
	trace_word((legacy_u16)limit);
	if (scenario % 5U == 0U && frame_index >= 7U) {
		idle_expired = 1;
	}
}

void *mmgr_free(legacy_s8 *ptr)
{
	trace_word(1019);
	trace_pointer(ptr);
	return 0;
}

void mouse_draw_opaque_check(void)
{
	trace_word(1020);
}

void mouse_draw_transparent_check(void)
{
	trace_word(1021);
}

legacy_s16 mouse_multi_hittest(legacy_s16 count, const struct BUTTON_AREA *buttons)
{
	trace_word(1022);
	trace_word((legacy_u16)count);
	if (escape_test != 0) {
		legacy_s16 selection = -1;
		if (escape_mode != SHOWROOM_TEST_ESCAPE_KEYBOARD) {
			selection = escape_mode == SHOWROOM_TEST_ESCAPE_BROWSED &&
								frame_index < SHOWROOM_TEST_BROWSE_ACTION_COUNT
							? escape_browse_selections[frame_index]
							: escape_selection;
		}
		frame_index++;
		return selection;
	}
	if (display_toggle_test != 0) {
		frame_index++;
		return -1;
	}
	if (predictive_preview_test != 0) {
		frame_index++;
		return 0;
	}
	trace_pointer(buttons);
	for (legacy_u32 i = 0; i < (legacy_u32)count; i++) {
		trace_word(buttons[i].x1);
		trace_word(buttons[i].x2);
		trace_word(buttons[i].y1);
		trace_word(buttons[i].y2);
	}
	legacy_s16 result = -1;
	if (frame_index == 10U && scenario % 3U == 0U) {
		result = 4;
	}
	if (frame_index >= 15U) {
		result = 0;
	}
	frame_index++;
	return result;
}

legacy_s16 polarAngle(legacy_s16 z, legacy_s16 y)
{
	trace_word(1023);
	trace_word((legacy_u16)z);
	trace_word((legacy_u16)y);
	return (legacy_s16)(z + y);
}

legacy_s16 rect_intersect(struct RECTANGLE *r1, struct RECTANGLE *r2)
{
	trace_word(1024);
	trace_rect(r1);
	trace_rect(r2);
	if (display_toggle_test != 0) {
		if (r1->left < r2->left) {
			r1->left = r2->left;
		}
		if (r1->right > r2->right) {
			r1->right = r2->right;
		}
		if (r1->top < r2->top) {
			r1->top = r2->top;
		}
		if (r1->bottom > r2->bottom) {
			r1->bottom = r2->bottom;
		}
		return 1;
	}
	r1->left = r2->left;
	r1->right = r2->right;
	r1->top = r2->top;
	r1->bottom = r2->bottom;
	return 1;
}

void rect_union(struct RECTANGLE *r1, struct RECTANGLE *r2, struct RECTANGLE *outrc)
{
	trace_word(1025);
	trace_rect(r1);
	trace_rect(r2);

	*outrc = *r1;
	if (display_toggle_test != 0) {
		if (r2->left < outrc->left) {
			outrc->left = r2->left;
		}
		if (r2->right > outrc->right) {
			outrc->right = r2->right;
		}
		if (r2->top < outrc->top) {
			outrc->top = r2->top;
		}
	}
	if (r2->bottom > outrc->bottom) {
		outrc->bottom = r2->bottom;
	}
}

legacy_u16 select_cliprect_rotate(legacy_s16 angZ, legacy_s16 angX, legacy_s16 angY,
								  struct RECTANGLE *cliprect, legacy_s16 half_scale)
{
	trace_word(1026);
	trace_word((legacy_u16)angZ);
	trace_word((legacy_u16)angX);
	trace_word((legacy_u16)angY);
	trace_rect(cliprect);
	trace_word((legacy_u16)half_scale);
	if (display_toggle_test != 0) {
		display_model_scaled = display_shadow_restored = 0;
		display_shadow_stage = SHOWROOM_TEST_SHADOW_IDLE;
	}
	return 0;
}

void set_projection(legacy_s16 horizontal_fov_degrees, legacy_s16 vertical_fov_degrees,
					legacy_s16 width, legacy_s16 height)
{
	trace_word(1027);
	trace_word((legacy_u16)horizontal_fov_degrees);
	trace_word((legacy_u16)vertical_fov_degrees);
	trace_word((legacy_u16)width);
	trace_word((legacy_u16)height);
}

void setup_aero_trackdata(void *carresptr, legacy_s16 is_opponent)
{
	trace_word(1028);
	trace_pointer(carresptr);
	trace_word((legacy_u16)is_opponent);
}

legacy_u16 shape2d_get_height(const struct SHAPE2D *shape)
{
	trace_word(1029);
	trace_pointer(shape);
	if (display_toggle_test != 0 && shape == &fixture_shapes[1]) {
		return 83;
	}
	return 12;
}

legacy_u16 shape2d_get_width(const struct SHAPE2D *shape)
{
	trace_word(1030);
	trace_pointer(shape);
	if (display_toggle_test != 0 && shape == &fixture_shapes[1]) {
		return 80;
	}
	return 30;
}

void shape3d_free_car_shapes(void)
{
	trace_word(1031);
}

void shape3d_load_car_shapes(legacy_s8 *carid, legacy_s8 *opponent_carid)
{
	trace_word(1032);
	trace_text(carid);
	trace_text(opponent_carid);
	if (display_toggle_test != 0) {
		display_car_loads++;
		display_load_present_count = 0;
		display_ground_height = display_car_loads == 1 ? SHOWROOM_TEST_FIRST_GROUND_HEIGHT
													   : SHOWROOM_TEST_NEXT_GROUND_HEIGHT;
	}
}

void shape3d_render_queued_primitives(void)
{
	if (display_toggle_test != 0) {
		assert(display_pending == 0);
#ifdef RESTUNTS_SDL3
		assert(display_background_restored != 0);
		display_background_restored = 0;
		if (supersight_enabled != 0 || display_previous_shadow != 0) {
			assert(display_shadow_restored != 0);
		}
#endif
		display_shadow_frames[supersight_enabled != 0]++;
		display_pending = 1;
	}
	if (predictive_preview_test != 0) {
		preview_present_count++;
	}
	trace_word(1033);
}

legacy_u16 shape3d_transform_and_queue(struct TRANSFORMEDSHAPE3D *instance)
{
#ifdef RESTUNTS_SDL3
	if (display_toggle_test != 0) {
		assert(display_model_scaled != 0);
		assert(((instance->ts_flags & SHAPE3D_NO_SHADOW_RECEIVE_FLAG) != 0) ==
			   (supersight_enabled != 0));
		if (supersight_enabled != 0) {
			assert(display_shadow_stage == SHOWROOM_TEST_SHADOW_MODEL);
			assert(instance->pos.y == carmenu_carpos.y);
			assert(display_shadow_camera.x == -display_shadow_position.x);
			assert(display_shadow_camera.y == -display_shadow_position.y);
			assert(display_shadow_camera.z == -display_shadow_position.z);
			assert(display_shadow_position.x == instance->pos.x);
			assert(display_shadow_position.y == instance->pos.y + display_ground_height);
			assert(display_shadow_position.z == instance->pos.z);
			assert(display_shadow_heading == instance->rotvec.z);
		} else {
			assert(display_shadow_stage == SHOWROOM_TEST_SHADOW_IDLE);
		}
	}
#endif
	if (predictive_preview_test != 0) {
		assert(instance->rotvec.z >= preview_last_rotation);
		preview_last_rotation = instance->rotvec.z;
	}
	trace_word(1034);
	trace_pointer(instance);
	trace_word(instance->rotvec.z);
	trace_word(instance->material);
	trace_word(instance->ts_flags);
	trace_word(instance->pos.x);
	trace_word(instance->pos.y);
	trace_word(instance->pos.z);
	if (instance->rectptr != 0) {
		instance->rectptr->left = 3;
		instance->rectptr->right = 128;
		instance->rectptr->top = 7;
		instance->rectptr->bottom = 88;
		if (display_toggle_test != 0) {
			/* Keep the car disjoint from the counter to expose missing FPS dirt. */
			instance->rectptr->left = 100;
			instance->rectptr->right = 180;
			instance->rectptr->top = 30;
			instance->rectptr->bottom = 80;
		}
	}
	return 0;
}

legacy_s16 sprite_blit_to_video(struct SPRITE *sprite, legacy_s16 mode)
{
	/* Dissolve phases must still be able to present through input polling. */
	assert(drawing_frame == 0);
	trace_word(1035);
	trace_pointer(sprite);
	trace_word((legacy_u16)mode);
	if (display_toggle_test != 0) {
		assert(sprite == render_window_sprite);
		record_preview_copy();
	}
	return 0;
}

void sprite_clear_shape_alt(struct SHAPE2D *shape, legacy_s16 x, legacy_s16 y)
{
	trace_word(1036);
	trace_pointer(shape);
	trace_word((legacy_u16)x);
	trace_word((legacy_u16)y);
}

void sprite_clear_target(legacy_u8 color)
{
	trace_word(1037);
	trace_word((legacy_u16)color);
}

void sprite_copy_image_at(struct SHAPE2D *shape, legacy_s16 x, legacy_s16 y)
{
	if (display_toggle_test != 0 && display_target == 1 && x == 240 && y == 0) {
		display_portrait_legacy_drawn = 1;
	}
	trace_word(1038);
	trace_pointer(shape);
	trace_word((legacy_u16)x);
	trace_word((legacy_u16)y);
}

void sprite_free_wnd(struct SPRITE *wndsprite)
{
	if (display_toggle_test != 0) {
		display_sprite_free_count++;
	}
	trace_word(1039);
	trace_pointer(wndsprite);
}

struct SPRITE *sprite_make_wnd(legacy_u16 width, legacy_u16 height, legacy_u16 color)
{
	trace_word(1040);
	trace_word((legacy_u16)width);
	trace_word((legacy_u16)height);
	trace_word((legacy_u16)color);
	assert(sprite_index < 4U);
	fixture_sprites[sprite_index].sprite_bitmapptr = &fixture_shapes[sprite_index];
	return &fixture_sprites[sprite_index++];
}

void sprite_putimage(struct SHAPE2D *shape)
{
	if (display_toggle_test != 0) {
		if (display_target == 1) {
#ifdef RESTUNTS_SDL3
			if (supersight_enabled != 0 || display_previous_shadow != 0) {
				assert_shadow_floor_covered();
				display_shadow_restored = 1;
			}
#endif
			if (display_load_present_count != 0 && (display_scenario & 4U) != 0
#ifdef RESTUNTS_SDL3
				&& display_background_mode != LEGACY_U8_MAX
#endif
			) {
				/* The showroom floor must stop before the opponent portrait. */
				assert(display_clip.right <= display_portrait_bounds.left);
			}
			if (display_previous_fps != 0) {
				/* The background copy must erase old digits even after F11 turns off. */
				assert_contains(&display_clip, &display_previous_fps_bounds);
			}
		} else if (display_target == 0) {
			assert(shape == render_window_sprite->sprite_bitmapptr);
			record_preview_copy();
		}
	}
	trace_word(1041);
	trace_pointer(shape);
}

void sprite_putimage_transparent(struct SHAPE2D *shape, legacy_s16 x, legacy_s16 y)
{
	if (display_toggle_test != 0 && display_target == 1 && x == 240 && y == 0) {
		display_portrait_legacy_drawn = 1;
	}
	trace_word(1042);
	trace_pointer(shape);
	trace_word((legacy_u16)x);
	trace_word((legacy_u16)y);
}

void sprite_putpixel_clipped(legacy_s16 x, legacy_s16 y, legacy_s16 color)
{
	trace_word(1043);
	trace_word((legacy_u16)x);
	trace_word((legacy_u16)y);
	trace_word((legacy_u16)color);
}

void sprite_select_mcga_backbuffer(void)
{
	display_target = 2;
	trace_word(1044);
}

void sprite_select_render_window(void)
{
	display_target = 1;
	trace_word(1045);
}

void sprite_select_render_window_and_clear(void)
{
	display_target = 1;
	trace_word(1046);
}

void sprite_select_screen_compat(void)
{
	display_target = 0;
	trace_word(1047);
}

void sprite_set_target_clip_bounds(legacy_u16 left, legacy_u16 right, legacy_u16 top,
								   legacy_u16 bottom)
{
	display_clip.left = left;
	display_clip.right = right;
	display_clip.top = top;
	display_clip.bottom = bottom;
	trace_word(1048);
	trace_word((legacy_u16)left);
	trace_word((legacy_u16)right);
	trace_word((legacy_u16)top);
	trace_word((legacy_u16)bottom);
}

void sprite_shape_to_1_alt(struct SHAPE2D *shape)
{
	trace_word(1049);
	trace_pointer(shape);
}

legacy_u32 timer_get_delta_alt(void)
{
	trace_word(1050);

	return 3;
}

void unload_resource(void *resptr)
{
	trace_word(1051);
	trace_pointer(resptr);
}

void update_car_speed(legacy_s8 input, legacy_s16 car_index, struct CARSTATE *carstate,
					  struct SIMD *simd)
{
	trace_word(1052);
	trace_word((legacy_u16)input);
	trace_word((legacy_u16)car_index);
	trace_pointer(carstate);
	trace_pointer(simd);
	trace_word(carstate->car_transmission);
	acceleration_step++;
	if (predictive_preview_test != 0) {
		preview_physics_steps++;
	}
	carstate->car_rev_speed =
		(legacy_s16)((scenario % 3U == 0U ? acceleration_step % 64U : acceleration_step) * 256U);
}

static void run_car_case(legacy_u32 index)
{
	legacy_s8 material = index % 6U;
	legacy_s8 transmission = index % 2U;
	scenario = index;
	frame_index = 0;
	preview_now = preview_previous_input_time = 0;
	file_index = 0;
	allocation_index = 0;
	sprite_index = 0;
	idle_expired = 0;
	video_uses_page_flipping = index % 2U;
	slow_video_mgmt = index % 3U == 0U;
	if (display_toggle_test != 0) {
		video_uses_page_flipping = display_scenario & 1U;
		slow_video_mgmt = (display_scenario >> 1) & 1U;
	}
	framespersec = 10 + index % 3U;
	miscptr = (legacy_s8 *)resource_bytes[63];
	fontnptr = (legacy_s8 *)resource_bytes[62];
	font_glyph_height = 8;
	game3dshapes[PLAYER_CAR_LOW_SHAPE].shape3d_numpaints = 3;
	for (legacy_u32 i = 0; i < 7U; i++) {
		oppresources[i] = (legacy_s8 *)&fixture_shapes[1];
	}
	trace_word(index);
	legacy_s8 car_id[5] = "COUN";
	legacy_u16 opponent_type = index % 3U == 0U ? 2U : 0U;
	if (display_toggle_test != 0) {
		opponent_type = (display_scenario & 4U) != 0 ? 2U : 0U;
	}
	if (escape_test != 0) {
		opponent_type = escape_opponent;
	}
	run_car_menu(car_id, &material, &transmission, opponent_type);
	if (escape_test != 0) {
		legacy_u8 browsed = escape_mode == SHOWROOM_TEST_ESCAPE_BROWSED;
		assert(_strcmp(car_id, (const legacy_s8 *)(browsed != 0 ? "VETT" : "COUN")) == 0);
		assert(material == (browsed != 0 ? 2 : 1));
		assert(transmission == (browsed != 0 ? TRANSMISSION_MANUAL : TRANSMISSION_AUTOMATIC));
		assert(frame_index ==
			   (browsed != 0
					? SHOWROOM_TEST_BROWSE_ACTION_COUNT
					: (escape_mode == SHOWROOM_TEST_ESCAPE_KEYBOARD ? escape_selection : 0)) +
				   1U);
	}
	if (display_toggle_test != 0) {
		assert(_strcmp(car_id, (const legacy_s8 *)"VETT") == 0);
		assert(material == 1 && transmission == 0);
	}
	trace_text(car_id);
	trace_word((legacy_u8)material);
	trace_word((legacy_u8)transmission);
	trace_word(framespersec);
	trace_word(idle_expired);
	trace_word(waitflag);
}

static void test_display_toggles(void)
{
	predictive_preview_test = 0;
	display_toggle_test = 1;
	for (display_scenario = 0; display_scenario < 32; display_scenario++) {
		legacy_u8 initial_supersight = (display_scenario >> 3) & 1U;
		legacy_u8 initial_fps = (display_scenario >> 4) & 1U;
		supersight_enabled = initial_supersight;
		fps_display_enabled = initial_fps;
		display_pending = display_fps_drawn = display_previous_fps = display_refresh_pending = 0;
		display_present_count = display_fps_draw_count = display_record_count = 0;
		display_status_active = display_status_expire_pending = 0;
		display_status_draw_count = display_status_expire_count = display_shift_shortcuts = 0;
		display_reset_count = display_shortcut_count = display_sprite_free_count = 0;
		display_portrait_pending = display_portrait_legacy_drawn = 0;
		display_portrait_mode = 255;
		display_background_mode = LEGACY_U8_MAX;
		display_background_restored = 0;
		display_portrait_draws = display_enhanced_portrait_draws = 0;
		display_previous_shadow = display_shadow_restored = 0;
		display_shadow_models = display_shadow_erasures = 0;
		display_car_loads = display_ground_queries = display_load_present_count = 0;
		display_shadow_frames[0] = display_shadow_frames[1] = 0;
		simd_player.collide_points[0].px = SHOWROOM_TEST_HALF_WIDTH;
		simd_player.collide_points[1].px = SHOWROOM_TEST_HALF_LENGTH;
		committed_frames = 0;
		run_car_case(1);
#ifdef RESTUNTS_SDL3
		assert(drawing_frame == 0 && committed_frames != 0);
#endif
		assert(frame_index == 22);
		assert(supersight_enabled == initial_supersight);
		/* Four F11 presses return the diagnostics to their initial state. */
		assert(fps_display_enabled == initial_fps);
#ifdef RESTUNTS_SDL3
		assert(display_shift_shortcuts == (initial_supersight == 0));
#else
		assert(display_shift_shortcuts == 0);
#endif
		assert(display_shortcut_count == 6U + display_shift_shortcuts);
		assert(display_status_draw_count != 0);
		assert(display_reset_count == 7);
		assert(display_status_expire_count == 1);
		assert(display_fps_draw_count > 0 && display_fps_draw_count <= display_present_count);
		assert(display_present_count == display_record_count);
		assert(display_pending == 0);
		assert(display_shadow_frames[0] != 0 && display_shadow_frames[1] != 0);
#ifdef RESTUNTS_SDL3
		assert(display_shadow_models == display_shadow_frames[1]);
		assert(display_shadow_erasures == 1);
		assert(display_car_loads == 2 && display_ground_queries == display_car_loads);
		assert(display_sprite_free_count == sprite_index);
		if ((display_scenario & 4U) != 0) {
			assert(display_portrait_draws == 4U + display_shift_shortcuts);
			assert(display_enhanced_portrait_draws == 2U + display_shift_shortcuts);
		} else {
			assert(display_portrait_draws == 0);
		}
		assert(display_portrait_pending == 0 && display_portrait_legacy_drawn == 0);
#endif
	}
	display_toggle_test = 0;
	puts("Car menu display toggles and overlay cleanup passed (32 scenarios).");
}

static void test_car_escape_back(void)
{
	static const legacy_u16 opponent_types[] = {0, 2};
	predictive_preview_test = display_toggle_test = supersight_enabled = fps_display_enabled = 0;
	escape_test = 1;
	for (legacy_u32 index = 0; index < sizeof(opponent_types) / sizeof(opponent_types[0]);
		 index++) {
		escape_opponent = opponent_types[index];
		for (escape_mode = 0; escape_mode < SHOWROOM_TEST_ESCAPE_MODE_COUNT; escape_mode++) {
			for (escape_selection = 0; escape_selection < SHOWROOM_TEST_SELECTION_COUNT;
				 escape_selection++) {
				run_car_case(1);
			}
		}
	}
	escape_test = 0;
	puts("Car menu Escape returns without activation and preserves choices (30 scenarios).");
}

int main(void)
{
	for (legacy_u32 index = 0; index < 102U; index++) {
		run_car_case(index);
	}
	/* Original implementation trace: car discovery and sorting, car changes,
	 * graph rendering, animation phases, navigation, idle exit and resource cleanup. */
	assert(trace_hash == UINT64_C(0x25191d328ccaebd8));
#ifdef RESTUNTS_SDL3
	legacy_u32 reference_physics_steps = 0;
	legacy_u32 reference_polls = 0;
	for (legacy_u8 mode = 0; mode < 3; mode++) {
		supersight_enabled = mode == 1;
		predictive_preview_test = mode == 2 ? 2 : 1;
		preview_present_count = preview_physics_steps = preview_toggle_count = 0;
		preview_last_rotation = 0;
		run_car_case(1);
		assert(preview_last_rotation >= 97 && preview_last_rotation <= 100);
		if (mode == 0) {
			reference_physics_steps = preview_physics_steps;
			reference_polls = frame_index;
			assert(reference_polls == 1000);
		} else {
			assert(preview_physics_steps == reference_physics_steps);
			assert(frame_index <= reference_polls);
		}
		if (mode == 1) {
			assert(frame_index == 100);
			assert(preview_present_count == 60);
			/* The last 60 Hz deadline is 983.33 ms, drawn at the next millisecond. */
			assert(preview_last_rotation == 98);
		}
	}
#endif
	test_display_toggles();
	test_car_escape_back();
	puts("Car menu interaction snapshots passed (102 scenarios).");
	return 0;
}
