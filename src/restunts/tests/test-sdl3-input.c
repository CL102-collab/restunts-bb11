#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../platform/sdl3/sdl3.h"
#include "../platform/sdl3/music.h"
#include "../platform/sdl3/touch.h"
#include "../platform/sdl3/controller_game.h"
#include "../c/platform.h"
#include "../c/keyboard.h"
#include "../c/game_input.h"
#include "../c/hires.h"
#include "../c/frame_adaptive.h"
#include "../c/video_frame.h"
#include "../c/presentation.h"

legacy_s32 sdl3_batch_mode;
static legacy_u8 framebuffer[65536];
static legacy_u8 high_resolution_framebuffer[HIRES_WIDTH * HIRES_HEIGHT];
static legacy_u8 high_resolution_active;
static legacy_s32 active_render_scale = HIRES_SCALE;

static legacy_u32 argb_framebuffer[HIRES_WIDTH * HIRES_HEIGHT];
static legacy_u8 argb_active;
static legacy_u32 frame_generation;
static legacy_s32 presented_raster_width, presented_raster_height;

legacy_s32 hires_render_scale(void)
{
	return active_render_scale;
}

void hires_set_render_scale(legacy_s32 scale)
{
	if (active_render_scale != scale) {
		active_render_scale = scale;
		argb_active = 0;
		frame_generation++;
	}
}

legacy_s32 hires_render_width(void)
{
	return SDL3_SCREEN_WIDTH * active_render_scale;
}

legacy_s32 hires_render_height(void)
{
	return SDL3_SCREEN_HEIGHT * active_render_scale;
}

static legacy_u32 render_timing_samples;
static legacy_u64 render_timing_last_ns;

void frame_render_timing_reset(void)
{
	render_timing_samples = 0;
	render_timing_last_ns = 0;
}

void frame_render_timing_record(legacy_u64 elapsed_ns)
{
	render_timing_samples++;
	render_timing_last_ns = elapsed_ns;
}

static legacy_u32 first_callbacks;
static legacy_u32 second_callbacks;
static legacy_u32 audio_ticks;
static legacy_u8 quit_cleaned_up;
static legacy_u8 music_toggle_available;
static legacy_u32 music_toggle_requests;

void sdl3_music_sync(void)
{
}

legacy_s32 sdl3_music_toggle(void)
{
	music_toggle_requests++;
	return music_toggle_available;
}

const legacy_u32 *hires_framebuffer_argb(const legacy_u8 *legacy, const legacy_u32 *palette)
{
	(void)legacy;
	(void)palette;
	return argb_active ? argb_framebuffer : NULL;
}

const legacy_u8 *hires_framebuffer(const legacy_u8 *legacy, legacy_s32 *width, legacy_s32 *height)
{
	*width = high_resolution_active ? hires_render_width() : SDL3_SCREEN_WIDTH;
	*height = high_resolution_active ? hires_render_height() : SDL3_SCREEN_HEIGHT;
	return high_resolution_active ? high_resolution_framebuffer : legacy;
}

void hires_copy_framebuffer_argb(const legacy_u8 *legacy, const legacy_u32 *palette,
								 legacy_u32 *destination, legacy_s32 pitch)
{
	legacy_s32 width, height;
	const legacy_u8 *indexed = hires_framebuffer(legacy, &width, &height);
	const legacy_u32 *argb = hires_framebuffer_argb(legacy, palette);
	presented_raster_width = width;
	presented_raster_height = height;
	assert(pitch >= width * (legacy_s32)sizeof(*destination));
	for (legacy_s32 row = 0; row < height; row++) {
		legacy_u32 *output = (legacy_u32 *)((legacy_u8 *)destination + (size_t)row * pitch);
		for (legacy_s32 column = 0; column < width; column++) {
			size_t pixel = (size_t)row * width + column;
			output[column] = argb != NULL ? argb[pixel] : palette[indexed[pixel]];
		}
	}
}

legacy_u32 hires_generation(void)
{
	return frame_generation;
}

legacy_s32 hires_enabled(void)
{
	return high_resolution_active;
}

void hires_forget(const void *base)
{
	assert(base == framebuffer);
}

void hires_shutdown(void)
{
	high_resolution_active = false;
	argb_active = 0;
	active_render_scale = HIRES_SCALE;
	frame_generation++;
}

void *dos_memory_make_pointer(legacy_u16 segment, legacy_u16 offset)
{
	assert(segment == 0xA000U);
	return framebuffer + offset;
}

void dos_process_exit(legacy_s16 status)
{
	exit(status);
}

legacy_s16 kb_parse_key(legacy_s16 key)
{
	return key;
}

void dos_audio_shutdown(void)
{
}

void call_exitlist2(void)
{
	quit_cleaned_up = true;
}

void sdl3_audio_update(void)
{
	/* A sample block must observe both sequence callbacks for the current tick. */
	assert(first_callbacks == second_callbacks);
	audio_ticks++;
}

static void first_callback(void)
{
	assert(first_callbacks == second_callbacks);
	first_callbacks++;
}

static void second_callback(void)
{
	assert(first_callbacks == second_callbacks + 1U);
	second_callbacks++;
}

static void send_key(SDL_Scancode scan, SDL_Keymod modifiers, legacy_u8 down, legacy_u8 repeat)
{
	SDL_Event event;
	SDL_zero(event);
	event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
	event.key.scancode = scan;
	event.key.mod = modifiers;
	event.key.down = down;
	event.key.repeat = repeat;
	assert(SDL_PushEvent(&event));
}

static void test_keyboard(void)
{
	kb_init_interrupt();
	send_key(SDL_SCANCODE_Q, SDL_KMOD_NONE, true, false);
	assert(kb_get_key_state(16) == 1);
	assert(dos_kb_get_char() == 'q');
	assert(kb_read_char() == 0);
	send_key(SDL_SCANCODE_Q, SDL_KMOD_NONE, false, false);
	assert(kb_get_key_state(16) == 0);
	send_key(SDL_SCANCODE_KP_8, SDL_KMOD_NUM, true, false);
	assert(kb_get_key_state(72) == 1);
	assert(kb_read_char() == KEY_UP);
	send_key(SDL_SCANCODE_KP_8, SDL_KMOD_NONE, false, false);
	send_key(SDL_SCANCODE_F1, SDL_KMOD_SHIFT, true, false);
	assert(kb_read_char() == KEY_SHIFT_F1);
	send_key(SDL_SCANCODE_F1, SDL_KMOD_NONE, false, false);
	const SDL_Keymod f11_modifiers[] = {SDL_KMOD_NONE, SDL_KMOD_LSHIFT, SDL_KMOD_RSHIFT,
										SDL_KMOD_SHIFT};
	for (legacy_u32 modifier = 0; modifier < SDL_arraysize(f11_modifiers); modifier++) {
		send_key(SDL_SCANCODE_F11, f11_modifiers[modifier], true, false);
		send_key(SDL_SCANCODE_F11, f11_modifiers[modifier], true, true);
		send_key(SDL_SCANCODE_F11, f11_modifiers[modifier], true, false);
		assert(kb_read_char() == KEY_F11);
		assert(kb_read_char() == 0);
		/* Changing Shift while F11 remains held must not toggle the display again. */
		send_key(SDL_SCANCODE_F11, SDL_KMOD_NONE, true, false);
		send_key(SDL_SCANCODE_F11, SDL_KMOD_SHIFT, true, true);
		assert(kb_read_char() == 0);
		send_key(SDL_SCANCODE_F11, SDL_KMOD_NONE, false, false);
	}
	send_key(SDL_SCANCODE_F12, SDL_KMOD_CTRL, true, false);
	assert(kb_read_char() == 0);
	send_key(SDL_SCANCODE_F12, SDL_KMOD_NONE, false, false);
	send_key(SDL_SCANCODE_F12, SDL_KMOD_NONE, true, false);
	assert(kb_read_char() == KEY_F12);
	send_key(SDL_SCANCODE_F12, SDL_KMOD_NONE, false, false);
	send_key(SDL_SCANCODE_F12, SDL_KMOD_SHIFT, true, false);
	send_key(SDL_SCANCODE_F12, SDL_KMOD_SHIFT, true, true);
	send_key(SDL_SCANCODE_F12, SDL_KMOD_SHIFT, true, false);
	assert(kb_read_char() == KEY_SHIFT_F12);
	assert(kb_read_char() == 0);
	/* Releasing Shift while F12 remains held must not trigger a plain toggle. */
	send_key(SDL_SCANCODE_F12, SDL_KMOD_NONE, true, false);
	assert(kb_read_char() == 0);
	send_key(SDL_SCANCODE_F12, SDL_KMOD_NONE, false, false);
	const SDL_Keymod rejected_modifiers[] = {SDL_KMOD_CTRL,
											 SDL_KMOD_ALT,
											 SDL_KMOD_GUI,
											 SDL_KMOD_SHIFT | SDL_KMOD_CTRL,
											 SDL_KMOD_SHIFT | SDL_KMOD_ALT,
											 SDL_KMOD_SHIFT | SDL_KMOD_GUI};
	const SDL_Scancode shortcuts[] = {SDL_SCANCODE_F11, SDL_SCANCODE_F12};
	for (legacy_u32 shortcut = 0; shortcut < SDL_arraysize(shortcuts); shortcut++) {
		for (legacy_u32 modifier = 0; modifier < SDL_arraysize(rejected_modifiers); modifier++) {
			send_key(shortcuts[shortcut], rejected_modifiers[modifier], true, false);
			assert(kb_read_char() == 0);
			send_key(shortcuts[shortcut], SDL_KMOD_NONE, true, false);
			assert(kb_read_char() == 0);
			send_key(shortcuts[shortcut], SDL_KMOD_NONE, false, false);
		}
	}
	send_key(SDL_SCANCODE_F12, SDL_KMOD_NONE, true, false);
	assert(kb_read_char() == KEY_F12);
	SDL_Event event;
	SDL_zero(event);
	event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
	assert(SDL_PushEvent(&event));
	assert(kb_get_key_state(88) == 0);
	assert(kb_get_key_state(-1) == 0);
	assert(kb_get_key_state(500) == 0);
}

static void test_music_shortcut(void)
{
	const legacy_s16 f10_scancode = (legacy_u16)KEY_F10 >> LEGACY_BYTE_BITS;
	kb_init_interrupt();
	music_toggle_requests = 0;
	music_toggle_available = true;
	send_key(SDL_SCANCODE_F10, SDL_KMOD_LSHIFT, true, false);
	assert(kb_checking() == 0);
	assert(kb_read_char() == 0);
	assert(kb_get_key_state(f10_scancode) == 0);
	assert(music_toggle_requests == 1);
	/* Keep the entire accepted press consumed, including repeats after the
	 * music context changes or Shift is released while F10 is held. */
	music_toggle_available = false;
	send_key(SDL_SCANCODE_F10, SDL_KMOD_LSHIFT, true, true);
	send_key(SDL_SCANCODE_F10, SDL_KMOD_LSHIFT, true, false);
	send_key(SDL_SCANCODE_F10, SDL_KMOD_NONE, true, false);
	assert(kb_read_char() == 0);
	assert(kb_get_key_state(f10_scancode) == 0);
	assert(music_toggle_requests == 1);
	SDL_Event event;
	SDL_zero(event);
	event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
	assert(SDL_PushEvent(&event));
	send_key(SDL_SCANCODE_F10, SDL_KMOD_LSHIFT, true, true);
	assert(kb_read_char() == 0);
	assert(kb_get_key_state(f10_scancode) == 0);
	assert(music_toggle_requests == 1);
	send_key(SDL_SCANCODE_F10, SDL_KMOD_NONE, false, false);

	/* An unavailable music shortcut retains the legacy Shift+F10 action. Entering
	 * a music context during the same held press must not toggle it. */
	send_key(SDL_SCANCODE_F10, SDL_KMOD_LSHIFT, true, false);
	assert(kb_read_char() == KEY_SHIFT_F10);
	assert(kb_get_key_state(f10_scancode) == 1);
	assert(music_toggle_requests == 2);
	music_toggle_available = true;
	send_key(SDL_SCANCODE_F10, SDL_KMOD_LSHIFT, true, true);
	send_key(SDL_SCANCODE_F10, SDL_KMOD_LSHIFT, true, false);
	assert(kb_read_char() == KEY_SHIFT_F10);
	assert(kb_read_char() == KEY_SHIFT_F10);
	assert(kb_read_char() == 0);
	assert(music_toggle_requests == 2);
	send_key(SDL_SCANCODE_F10, SDL_KMOD_NONE, false, false);

	const SDL_Keymod modifiers[] = {SDL_KMOD_NONE, SDL_KMOD_SHIFT | SDL_KMOD_CTRL,
									SDL_KMOD_SHIFT | SDL_KMOD_ALT, SDL_KMOD_SHIFT | SDL_KMOD_GUI};
	for (legacy_u32 modifier = 0; modifier < SDL_arraysize(modifiers); modifier++) {
		send_key(SDL_SCANCODE_F10, modifiers[modifier], true, false);
		assert(kb_read_char() != 0);
		send_key(SDL_SCANCODE_F10, SDL_KMOD_LSHIFT, true, false);
		assert(kb_read_char() == KEY_SHIFT_F10);
		assert(music_toggle_requests == 2);
		send_key(SDL_SCANCODE_F10, SDL_KMOD_NONE, false, false);
	}
	/* Lock keys do not modify a function-key shortcut. */
	send_key(SDL_SCANCODE_F10, SDL_KMOD_RSHIFT | SDL_KMOD_CAPS | SDL_KMOD_NUM, true, false);
	assert(kb_read_char() == 0);
	assert(kb_get_key_state(f10_scancode) == 0);
	assert(music_toggle_requests == 3);
	send_key(SDL_SCANCODE_F10, SDL_KMOD_NONE, false, false);
	assert(kb_get_key_state(f10_scancode) == 0);
	music_toggle_available = false;
}

static void test_timer(void)
{
	dos_timer_setup_interrupt();
	assert(dos_timer_register_callback(first_callback));
	assert(dos_timer_register_callback(second_callback));
	SDL_Delay(35);
	legacy_u32 current = timer_get_counter();
	assert(current >= 3);
	assert(first_callbacks == current);
	assert(audio_ticks == current);
	dos_timer_set_callbacks_suspended(1);
	legacy_u32 paused = timer_get_counter();
	legacy_u32 realtime = dos_timer_get_realtime_counter();
	legacy_u32 callbacks_at_pause = first_callbacks;
	SDL_Delay(25);
	assert(timer_get_counter() == paused);
	assert(first_callbacks == callbacks_at_pause);
	assert(dos_timer_get_realtime_counter() >= realtime + 2);
	realtime = dos_timer_get_realtime_counter();
	assert(audio_ticks == realtime);
	dos_timer_set_callbacks_suspended(0);
	SDL_Delay(15);
	assert(timer_get_counter() > paused);
	legacy_u32 slow = timer_get_slow_counter();
	realtime = dos_timer_get_realtime_counter();
	assert(slow <= realtime / 5U && slow >= current / 5U);
	dos_timer_unregister_callback(first_callback);
	dos_timer_unregister_callback(second_callback);
	dos_timer_set_callbacks_suspended(1);
	dos_timer_reset_counter();
	assert(timer_get_counter() == 0);
	dos_timer_shutdown();
}

static void assert_coordinate(legacy_f32 actual, legacy_f32 expected)
{
	assert(actual > expected - 0.1f && actual < expected + 0.1f);
}

static void check_video_aspect(legacy_s32 width, legacy_s32 height, legacy_f32 left, legacy_f32 top)
{
	assert(SDL_SetWindowSize(sdl3_video_window(), width, height));
	assert(SDL_SyncWindow(sdl3_video_window()));
	SDL_PumpEvents();
	sdl3_video_present();
	SDL_FRect bounds;
	assert(SDL_GetRenderLogicalPresentationRect(SDL_GetRenderer(sdl3_video_window()), &bounds));
	assert_coordinate(bounds.x, left);
	assert_coordinate(bounds.y, top);
	assert_coordinate(bounds.w, 960.0f);
	assert_coordinate(bounds.h, 720.0f);
	legacy_f32 x;
	legacy_f32 y;
	sdl3_video_game_to_window(0, 0, &x, &y);
	assert_coordinate(x, left);
	assert_coordinate(y, top);
	sdl3_video_game_to_window(320, 200, &x, &y);
	assert_coordinate(x, left + 960.0f);
	assert_coordinate(y, top + 720.0f);
	/* Ten original pixels occupy 30 horizontally and 36 vertically: VGA 6:5. */
	sdl3_video_game_to_window(10, 10, &x, &y);
	assert_coordinate(x, left + 30.0f);
	assert_coordinate(y, top + 36.0f);
}

#define TEST_VSYNC_ENVIRONMENT "RESTUNTS_VSYNC"
#define TEST_VSYNC_INTERVAL 1

static void set_vsync_environment(const legacy_char *setting)
{
	if (setting != NULL) {
		assert(SDL_setenv_unsafe(TEST_VSYNC_ENVIRONMENT, setting, true) == 0);
	} else {
		assert(SDL_unsetenv_unsafe(TEST_VSYNC_ENVIRONMENT) == 0);
	}
}

static void assert_video_vsync(legacy_int expected)
{
	assert(sdl3_video_window() != NULL);
	SDL_Renderer *renderer = SDL_GetRenderer(sdl3_video_window());
	assert(renderer != NULL);
	legacy_int actual;
	assert(SDL_GetRenderVSync(renderer, &actual));
	assert(actual == expected);
}

static void test_video_vsync(void)
{
	const struct {
		const legacy_char *setting;
		legacy_int interval;
	} cases[] = {{NULL, TEST_VSYNC_INTERVAL}, {"0", SDL_RENDERER_VSYNC_DISABLED},
				 {"1", TEST_VSYNC_INTERVAL},  {"", TEST_VSYNC_INTERVAL},
				 {"00", TEST_VSYNC_INTERVAL}, {"invalid", TEST_VSYNC_INTERVAL}};
	const legacy_char *original = getenv(TEST_VSYNC_ENVIRONMENT);
	legacy_char *saved = original != NULL ? SDL_strdup(original) : NULL;
	assert(original == NULL || saved != NULL);
	for (size_t index = 0; index < SDL_arraysize(cases); index++) {
		set_vsync_environment(cases[index].setting);
		/* Recreating the renderer must re-read the selected interval. The dummy
		 * driver exposes SDL's VSync state without timing-dependent assertions. */
		dos_video_set_mode_13h();
		assert_video_vsync(cases[index].interval);
		/* Fullscreen changes and subsequent presentations retain that choice. */
		sdl3_video_toggle_fullscreen();
		assert(SDL_SyncWindow(sdl3_video_window()));
		sdl3_video_present();
		assert_video_vsync(cases[index].interval);
		sdl3_video_toggle_fullscreen();
		assert(SDL_SyncWindow(sdl3_video_window()));
		sdl3_video_present();
		assert_video_vsync(cases[index].interval);
	}
	sdl3_video_shutdown();
	set_vsync_environment("1");
	sdl3_batch_mode = true;
	dos_video_set_mode_13h();
	assert(sdl3_video_window() == NULL);
	sdl3_batch_mode = false;
	set_vsync_environment(saved);
	SDL_free(saved);
}

#define TEST_BORDERLESS_ENVIRONMENT "RESTUNTS_BORDERLESS"

static void set_borderless_environment(const legacy_char *setting)
{
	if (setting != NULL) {
		assert(SDL_setenv_unsafe(TEST_BORDERLESS_ENVIRONMENT, setting, true) == 0);
	} else {
		assert(SDL_unsetenv_unsafe(TEST_BORDERLESS_ENVIRONMENT) == 0);
	}
}

static void assert_borderless_video(legacy_u8 borderless, legacy_u8 fullscreen)
{
	assert(sdl3_video_window() != NULL);
	assert(SDL_SyncWindow(sdl3_video_window()));
	SDL_WindowFlags flags = SDL_GetWindowFlags(sdl3_video_window());
	assert((flags & SDL_WINDOW_RESIZABLE) != 0);
	assert(((flags & SDL_WINDOW_BORDERLESS) != 0) == borderless);
	assert(((flags & SDL_WINDOW_FULLSCREEN) != 0) == fullscreen);
	/* Dummy video retains the requested borders but cannot maximize. Actual
	 * maximization and compositor behavior require a window manager. */
}

static void test_video_borderless(void)
{
	const struct {
		const legacy_char *setting;
		legacy_u8 borderless;
	} cases[] = {{NULL, false}, {"1", true},		{"0", false}, {"1", true},
				 {"", false},	{"invalid", false}, {"01", false}};
	const legacy_char *original = getenv(TEST_BORDERLESS_ENVIRONMENT);
	legacy_char *saved = original != NULL ? SDL_strdup(original) : NULL;
	assert(original == NULL || saved != NULL);
	for (legacy_u32 index = 0; index < SDL_arraysize(cases); index++) {
		set_borderless_environment(cases[index].setting);
		/* Recreation re-reads the option and must not inherit earlier borders. */
		dos_video_set_mode_13h();
		assert_borderless_video(cases[index].borderless, false);
		sdl3_video_toggle_fullscreen();
		assert_borderless_video(cases[index].borderless, true);
		sdl3_video_toggle_fullscreen();
		assert_borderless_video(cases[index].borderless, false);
	}
	sdl3_video_shutdown();
	set_borderless_environment("1");
	sdl3_batch_mode = true;
	dos_video_set_mode_13h();
	assert(sdl3_video_window() == NULL);
	sdl3_batch_mode = false;
	set_borderless_environment(saved);
	SDL_free(saved);
}

static void test_video_and_mouse(void)
{
	dos_video_set_mode_13h();
	assert(sdl3_video_window() != NULL);
	legacy_u8 red[] = {63, 0, 0};
	dos_video_set_palette(3, 1, red);
	memset(framebuffer, 3, 64000);
	sdl3_video_present();
	assert(dos_video_enable_planar_pages() == 0);
	check_video_aspect(1100, 720, 70.0f, 0.0f);
	check_video_aspect(960, 800, 0.0f, 40.0f);
	check_video_aspect(960, 720, 0.0f, 0.0f);
	legacy_f32 x;
	legacy_f32 y;
	legacy_f32 window_x;
	legacy_f32 window_y;
	sdl3_video_game_to_window(160, 100, &window_x, &window_y);
	sdl3_video_window_to_game(window_x, window_y, &x, &y);
	assert(x > 159.9f && x < 160.1f);
	assert(y > 99.9f && y < 100.1f);
	assert(dos_mouse_init(320, 200) != 0);
	assert(dos_mouse_get_button_count() >= 2);
	dos_mouse_set_minmax(15, 10, 300, 180);
	dos_mouse_set_position(0, 250);
	legacy_s16 buttons;
	legacy_s16 mx;
	legacy_s16 my;
	dos_mouse_get_state(&buttons, &mx, &my);
	assert(mx == 15 && my == 180);
	SDL_Event event;
	SDL_zero(event);
	event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
	event.button.button = SDL_BUTTON_RIGHT;
	event.button.down = true;
	event.button.x = window_x;
	event.button.y = window_y;
	assert(SDL_PushEvent(&event));
	dos_mouse_get_state(&buttons, &mx, &my);
	assert(buttons == 2 && mx == 160 && my == 100);
	event.type = SDL_EVENT_MOUSE_BUTTON_UP;
	event.button.down = false;
	assert(SDL_PushEvent(&event));
	event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
	event.button.button = SDL_BUTTON_LEFT;
	event.button.down = true;
	assert(SDL_PushEvent(&event));
	event.type = SDL_EVENT_MOUSE_BUTTON_UP;
	event.button.down = false;
	assert(SDL_PushEvent(&event));
	sdl3_platform_pump();
	dos_mouse_get_state(&buttons, &mx, &my);
	assert(buttons == 0);
	dos_mouse_get_state(&buttons, &mx, &my);
	assert(buttons == 1);
	dos_mouse_get_state(&buttons, &mx, &my);
	assert(buttons == 0);
	sdl3_input_touch_mouse(window_x, window_y, 1);
	sdl3_input_touch_mouse(window_x, window_y, 0);
	dos_mouse_get_state(&buttons, &mx, &my);
	assert(buttons == 1 && mx == 160 && my == 100);
	dos_mouse_get_state(&buttons, &mx, &my);
	assert(buttons == 0);
	/* The core translates the mouse edge once; no duplicate keyboard action. */
	assert(dos_kb_get_char() == 0);
}

#define TEST_TOUCH_DEVICE 1
#define TEST_TOUCH_REWIND_SCANCODE 16

enum TEST_TOUCH_FINGER {
	TEST_TOUCH_REWIND_FINGER = 1,
	TEST_TOUCH_STEERING_FINGER,
	TEST_TOUCH_THROTTLE_FINGER,
	TEST_TOUCH_SHIFT_FINGER,
	TEST_TOUCH_SHORTCUT_FINGER
};

static void send_touch_control(SDL_EventType type, SDL_FingerID finger, enum TOUCH_CONTROL control)
{
	SDL_Window *window = sdl3_video_window();
	SDL_Rect area;
	assert(window != NULL && SDL_GetWindowSafeArea(window, &area));
	legacy_int width, height;
	assert(SDL_GetWindowSize(window, &width, &height));
	struct TOUCH_CIRCLE circle = sdl3_touch_circle(control, &area);
	SDL_Event event;
	SDL_zero(event);
	event.type = type;
	event.tfinger.windowID = SDL_GetWindowID(window);
	event.tfinger.touchID = TEST_TOUCH_DEVICE;
	event.tfinger.fingerID = finger;
	event.tfinger.x = circle.x / width;
	event.tfinger.y = circle.y / height;
	assert(SDL_PushEvent(&event));
}

static void test_touch_shortcut_keys(void)
{
	static const struct {
		enum TOUCH_CONTROL control;
		SDL_Scancode scancode;
		legacy_u16 key;
	} shortcuts[] = {{TOUCH_CAMERA, SDL_SCANCODE_C, 'c'}, {TOUCH_FOLLOW, SDL_SCANCODE_T, 't'}};
	sdl3_platform_pump();
	kb_init_interrupt();
	sdl3_touch_enable(true);
	sdl3_touch_configure(true, true, false, false);
	for (legacy_u32 index = 0; index < SDL_arraysize(shortcuts); index++) {
		/* Touch must deliver the same character as a physical, unmodified key press. */
		send_key(shortcuts[index].scancode, SDL_KMOD_NONE, true, false);
		assert(kb_read_char() == shortcuts[index].key);
		send_key(shortcuts[index].scancode, SDL_KMOD_NONE, false, false);
		assert(kb_read_char() == 0);
		send_touch_control(SDL_EVENT_FINGER_DOWN, TEST_TOUCH_SHORTCUT_FINGER,
						   shortcuts[index].control);
		assert(kb_read_char() == shortcuts[index].key);
		send_touch_control(SDL_EVENT_FINGER_MOTION, TEST_TOUCH_SHORTCUT_FINGER,
						   shortcuts[index].control);
		send_touch_control(SDL_EVENT_FINGER_DOWN, TEST_TOUCH_SHORTCUT_FINGER,
						   shortcuts[index].control);
		assert(kb_read_char() == 0);
		send_touch_control(SDL_EVENT_FINGER_UP, TEST_TOUCH_SHORTCUT_FINGER,
						   shortcuts[index].control);
		assert(kb_read_char() == 0);
		send_touch_control(SDL_EVENT_FINGER_DOWN, TEST_TOUCH_SHORTCUT_FINGER,
						   shortcuts[index].control);
		send_touch_control(SDL_EVENT_FINGER_UP, TEST_TOUCH_SHORTCUT_FINGER,
						   shortcuts[index].control);
		assert(kb_read_char() == shortcuts[index].key && kb_read_char() == 0);
	}
	sdl3_touch_enable(false);
}

static void send_touch_holds(SDL_EventType type)
{
	send_touch_control(type, TEST_TOUCH_REWIND_FINGER, TOUCH_REWIND);
	send_touch_control(type, TEST_TOUCH_STEERING_FINGER, TOUCH_LEFT);
	send_touch_control(type, TEST_TOUCH_THROTTLE_FINGER, TOUCH_ACCELERATE);
}

static void assert_touch_holds(legacy_u8 held)
{
	assert(kb_get_key_state(TEST_TOUCH_REWIND_SCANCODE) == held);
	assert(kb_get_key_state((legacy_u16)KEY_LEFT >> LEGACY_BYTE_BITS) == held);
	assert(kb_get_key_state((legacy_u16)KEY_UP >> LEGACY_BYTE_BITS) == held);
}

static void send_touch_shift(void)
{
	send_touch_control(SDL_EVENT_FINGER_DOWN, TEST_TOUCH_SHIFT_FINGER, TOUCH_SHIFT_UP);
	send_touch_control(SDL_EVENT_FINGER_UP, TEST_TOUCH_SHIFT_FINGER, TOUCH_SHIFT_UP);
}

static void test_touch_focus_and_background(void)
{
	static const SDL_EventType reset_events[] = {SDL_EVENT_WINDOW_FOCUS_LOST,
												 SDL_EVENT_WILL_ENTER_BACKGROUND};
	sdl3_platform_pump();
	kb_init_interrupt();
	sdl3_touch_enable(true);
	sdl3_touch_configure(true, true, true, false);
	for (legacy_u32 index = 0; index < SDL_arraysize(reset_events); index++) {
		send_touch_holds(SDL_EVENT_FINGER_DOWN);
		send_touch_shift();
		assert_touch_holds(true);
		assert(sdl3_touch_take_shift_flags() == INPUT_SHIFT_UP_FLAG);
		/* Leave a second shift pending while all three control fingers stay held. */
		send_touch_shift();
		sdl3_platform_pump();
		assert(kb_checking() != 0);
		SDL_Event event;
		SDL_zero(event);
		event.type = reset_events[index];
		event.window.windowID = SDL_GetWindowID(sdl3_video_window());
		assert(SDL_PushEvent(&event));
		assert_touch_holds(false);
		assert(sdl3_touch_take_shift_flags() == 0);
		assert(kb_read_char() == 0);
		/* Reusing the canceled finger IDs must establish fresh holds and tap actions. */
		send_touch_holds(SDL_EVENT_FINGER_DOWN);
		send_touch_shift();
		assert_touch_holds(true);
		assert(sdl3_touch_take_shift_flags() == INPUT_SHIFT_UP_FLAG);
		send_touch_holds(SDL_EVENT_FINGER_UP);
		assert_touch_holds(false);
		assert(kb_check() == 0);
	}
	sdl3_touch_enable(false);
}

static void assert_presented_color(legacy_s32 x, legacy_s32 y, Uint8 red, Uint8 green, Uint8 blue)
{
	SDL_Renderer *renderer = SDL_GetRenderer(sdl3_video_window());
	SDL_FRect bounds;
	assert(SDL_GetRenderLogicalPresentationRect(renderer, &bounds));
	SDL_Surface *surface = SDL_RenderReadPixels(renderer, NULL);
	assert(surface != NULL);
	/* Readback is cropped to SDL's viewport, while assertions use window pixels. */
	x -= (legacy_s32)bounds.x;
	y -= (legacy_s32)bounds.y;
	Uint8 actual_red;
	Uint8 actual_green;
	Uint8 actual_blue;
	assert(SDL_ReadSurfacePixel(surface, x, y, &actual_red, &actual_green, &actual_blue, NULL));
	if (actual_red != red || actual_green != green || actual_blue != blue) {
		fprintf(stderr,
				"Presented pixel (%" LEGACY_PRId32 ", %" LEGACY_PRId32
				"): got (%d, %d, %d), expected (%d, %d, %d)\n",
				x, y, actual_red, actual_green, actual_blue, red, green, blue);
	}
	assert(actual_red == red && actual_green == green && actual_blue == blue);
	SDL_DestroySurface(surface);
}

enum {
	PAGE_VIDEO_WINDOW_WIDTH = SDL3_SCREEN_WIDTH,
	PAGE_VIDEO_WINDOW_HEIGHT = 240,
	PAGE_VIDEO_PIXELS = SDL3_SCREEN_WIDTH * SDL3_SCREEN_HEIGHT,
	PAGE_VIDEO_FIRST_INDEX = 16,
	PAGE_VIDEO_SECOND_INDEX,
	PAGE_VIDEO_COLOR_COUNT = 2,
	PAGE_VIDEO_DAC_MAX = 63,
	PAGE_VIDEO_REFRESH_DELAY_MS = 11
};
#define PAGE_VIDEO_RED 0xFFFF0000U
#define PAGE_VIDEO_GREEN 0xFF00FF00U
#define PAGE_VIDEO_BLUE 0xFF0000FFU
#define PAGE_VIDEO_YELLOW 0xFFFFFF00U
#define PAGE_VIDEO_WHITE 0xFFFFFFFFU
#define PAGE_VIDEO_BLACK 0xFF000000U

static void assert_presented_page(legacy_u32 top, legacy_u32 bottom)
{
	SDL_Surface *surface = SDL_RenderReadPixels(SDL_GetRenderer(sdl3_video_window()), NULL);
	assert(surface != NULL);
	assert(surface->w == PAGE_VIDEO_WINDOW_WIDTH && surface->h == PAGE_VIDEO_WINDOW_HEIGHT);
	for (legacy_s32 y = 0; y < surface->h; y++) {
		legacy_u32 expected = y < surface->h / 2 ? top : bottom;
		legacy_u8 red = (legacy_u8)(expected >> LEGACY_WORD_BITS);
		legacy_u8 green = (legacy_u8)(expected >> LEGACY_BYTE_BITS);
		legacy_u8 blue = (legacy_u8)expected;
		for (legacy_s32 x = 0; x < surface->w; x++) {
			legacy_u8 actual_red, actual_green, actual_blue;
			assert(SDL_ReadSurfacePixel(surface, x, y, &actual_red, &actual_green, &actual_blue,
										NULL));
			if (actual_red != red || actual_green != green || actual_blue != blue) {
				fprintf(stderr,
						"Published page pixel (%" LEGACY_PRId32 ", %" LEGACY_PRId32
						"): got (%d, %d, %d), expected (%d, %d, %d)\n",
						x, y, actual_red, actual_green, actual_blue, red, green, blue);
			}
			assert(actual_red == red && actual_green == green && actual_blue == blue);
		}
	}
	SDL_DestroySurface(surface);
}

static void request_page_repaint(legacy_u32 event_type)
{
	SDL_Renderer *renderer = SDL_GetRenderer(sdl3_video_window());
	/* Model contents lost after a window repaint. Merely suppressing an upload
	 * while drawing is insufficient: the completed page must be redrawn. */
	assert(SDL_SetRenderDrawColor(renderer, LEGACY_U8_MAX, 0, LEGACY_U8_MAX, SDL_ALPHA_OPAQUE));
	assert(SDL_RenderClear(renderer));
	assert(SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE));
	SDL_Event event;
	SDL_zero(event);
	event.type = event_type;
	event.window.windowID = SDL_GetWindowID(sdl3_video_window());
	assert(SDL_PushEvent(&event));
	sdl3_platform_pump();
}

static void test_completed_video_pages(void)
{
	hires_shutdown();
	frame_adaptive_reset(&frame_adaptive);
	assert(
		SDL_SetWindowSize(sdl3_video_window(), PAGE_VIDEO_WINDOW_WIDTH, PAGE_VIDEO_WINDOW_HEIGHT));
	assert(SDL_SyncWindow(sdl3_video_window()));
	legacy_u8 colors[] = {PAGE_VIDEO_DAC_MAX, 0, 0, 0, 0, PAGE_VIDEO_DAC_MAX};
	dos_video_set_palette(PAGE_VIDEO_FIRST_INDEX, PAGE_VIDEO_COLOR_COUNT, colors);
	memset(framebuffer, PAGE_VIDEO_FIRST_INDEX, PAGE_VIDEO_PIXELS);
	sdl3_video_present();
	assert_presented_page(PAGE_VIDEO_RED, PAGE_VIDEO_RED);

	sdl3_video_begin_track_frame(1);
	memset(framebuffer, PAGE_VIDEO_SECOND_INDEX, PAGE_VIDEO_PIXELS / 2);
	legacy_u8 next_colors[] = {PAGE_VIDEO_DAC_MAX, PAGE_VIDEO_DAC_MAX, 0, 0, PAGE_VIDEO_DAC_MAX, 0};
	dos_video_set_palette(PAGE_VIDEO_FIRST_INDEX, PAGE_VIDEO_COLOR_COUNT, next_colors);
	request_page_repaint(SDL_EVENT_WINDOW_EXPOSED);
	assert_presented_page(PAGE_VIDEO_RED, PAGE_VIDEO_RED);
	assert(frame_adaptive.samples == 0);
	memset(framebuffer + PAGE_VIDEO_PIXELS / 2, PAGE_VIDEO_SECOND_INDEX, PAGE_VIDEO_PIXELS / 2);
	sdl3_video_end_frame();
	assert_presented_page(PAGE_VIDEO_GREEN, PAGE_VIDEO_GREEN);
	assert(frame_adaptive.samples == 1);

	/* Reuse the other page with different top and bottom regions, so a stale
	 * half or a palette borrowed from the next frame cannot pass unnoticed. */
	sdl3_video_begin_frame();
	memset(framebuffer, PAGE_VIDEO_FIRST_INDEX, PAGE_VIDEO_PIXELS / 2);
	legacy_u8 blue[] = {0, 0, PAGE_VIDEO_DAC_MAX};
	dos_video_set_palette(PAGE_VIDEO_SECOND_INDEX, 1, blue);
	request_page_repaint(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
	assert_presented_page(PAGE_VIDEO_GREEN, PAGE_VIDEO_GREEN);
	sdl3_video_end_frame();
	assert_presented_page(PAGE_VIDEO_YELLOW, PAGE_VIDEO_BLUE);
	assert(frame_adaptive.samples == 1);

	/* Unbracketed menu palette fades must still become new completed pages. */
	legacy_u8 faded_colors[] = {
		PAGE_VIDEO_DAC_MAX, PAGE_VIDEO_DAC_MAX, PAGE_VIDEO_DAC_MAX, PAGE_VIDEO_DAC_MAX, 0, 0};
	dos_video_set_palette(PAGE_VIDEO_FIRST_INDEX, PAGE_VIDEO_COLOR_COUNT, faded_colors);
	SDL_Delay(PAGE_VIDEO_REFRESH_DELAY_MS);
	sdl3_video_refresh();
	assert_presented_page(PAGE_VIDEO_WHITE, PAGE_VIDEO_RED);
	assert(frame_adaptive.samples == 1);
}

static void test_video_page_lifetime(void)
{
	legacy_u8 colors[] = {PAGE_VIDEO_DAC_MAX, PAGE_VIDEO_DAC_MAX, PAGE_VIDEO_DAC_MAX, 0, 0,
						  PAGE_VIDEO_DAC_MAX};
	dos_video_set_palette(PAGE_VIDEO_FIRST_INDEX, PAGE_VIDEO_COLOR_COUNT, colors);
	high_resolution_active = true;
	hires_set_render_scale(HIRES_SCALE);
	for (size_t pixel = 0; pixel < SDL_arraysize(argb_framebuffer); pixel++) {
		argb_framebuffer[pixel] =
			pixel < SDL_arraysize(argb_framebuffer) / 2 ? PAGE_VIDEO_RED : PAGE_VIDEO_GREEN;
	}
	argb_active = true;
	frame_generation++;
	sdl3_video_present();
	assert_presented_page(PAGE_VIDEO_RED, PAGE_VIDEO_GREEN);

	sdl3_video_begin_track_frame(0);
	/* A resolution change retires hires storage. Neither the preceding ARGB
	 * pixels nor its dimensions may be borrowed by the published page. */
	memset(argb_framebuffer, 0, sizeof(argb_framebuffer));
	hires_set_render_scale(FRAME_ADAPTIVE_HALF_SCALE);
	memset(high_resolution_framebuffer, PAGE_VIDEO_SECOND_INDEX,
		   sizeof(high_resolution_framebuffer));
	legacy_u8 blue[] = {0, 0, PAGE_VIDEO_DAC_MAX};
	dos_video_set_palette(PAGE_VIDEO_SECOND_INDEX, 1, blue);
	request_page_repaint(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
	assert_presented_page(PAGE_VIDEO_RED, PAGE_VIDEO_GREEN);
	sdl3_video_end_frame();
	assert_presented_page(PAGE_VIDEO_BLUE, PAGE_VIDEO_BLUE);

	sdl3_video_begin_track_frame(0);
	hires_shutdown();
	memset(high_resolution_framebuffer, 0, sizeof(high_resolution_framebuffer));
	memset(framebuffer, PAGE_VIDEO_FIRST_INDEX, PAGE_VIDEO_PIXELS);
	request_page_repaint(SDL_EVENT_WINDOW_EXPOSED);
	assert_presented_page(PAGE_VIDEO_BLUE, PAGE_VIDEO_BLUE);
	sdl3_video_end_frame();
	assert_presented_page(PAGE_VIDEO_WHITE, PAGE_VIDEO_WHITE);

	/* Mode recreation must discard the front page even if a drawing frame
	 * was abandoned, and the first repaint must show the newly cleared mode. */
	legacy_u32 completed_samples = frame_adaptive.samples;
	sdl3_video_begin_track_frame(1);
	memset(framebuffer, PAGE_VIDEO_SECOND_INDEX, PAGE_VIDEO_PIXELS);
	dos_video_set_mode_13h();
	assert(
		SDL_SetWindowSize(sdl3_video_window(), PAGE_VIDEO_WINDOW_WIDTH, PAGE_VIDEO_WINDOW_HEIGHT));
	assert(SDL_SyncWindow(sdl3_video_window()));
	request_page_repaint(SDL_EVENT_WINDOW_EXPOSED);
	assert_presented_page(PAGE_VIDEO_BLACK, PAGE_VIDEO_BLACK);
	assert(frame_adaptive.samples == completed_samples);
	assert(render_timing_samples == 0);
	frame_adaptive_reset(&frame_adaptive);
}

#define SCREENSHOT_TEST_FILENAME "SHOT0000.PNG"
#define SCREENSHOT_TEST_EXISTING_CONTENT "Keep this existing screenshot unchanged."

enum {
	SCREENSHOT_TEST_DIRECTORY_BYTES = 64,
	SCREENSHOT_TEST_EXISTING_FILE = 1,
	SCREENSHOT_TEST_EXISTING_DIRECTORY,
	SCREENSHOT_TEST_FIRST_IMAGE
};

static void screenshot_test_path(legacy_u32 number,
								 legacy_char path[sizeof(SCREENSHOT_TEST_FILENAME)])
{
	assert(snprintf(path, sizeof(SCREENSHOT_TEST_FILENAME), "SHOT%04" LEGACY_PRIu32 ".PNG",
					number) == (legacy_s32)sizeof(SCREENSHOT_TEST_FILENAME) - 1);
}

static void assert_no_screenshot(legacy_u32 number)
{
	legacy_char path[sizeof(SCREENSHOT_TEST_FILENAME)];
	screenshot_test_path(number, path);
	assert(!SDL_GetPathInfo(path, NULL));
}

static void assert_screenshot(legacy_u32 number, legacy_s32 width, legacy_s32 height,
							  legacy_u32 top, legacy_u32 bottom)
{
	legacy_char path[sizeof(SCREENSHOT_TEST_FILENAME)];
	screenshot_test_path(number, path);
	SDL_Surface *surface = SDL_LoadPNG(path);
	assert(surface != NULL);
	assert(surface->w == width && surface->h == height);
	for (legacy_s32 y = 0; y < height; y++) {
		legacy_u32 expected = y < height / 2 ? top : bottom;
		for (legacy_s32 x = 0; x < width; x++) {
			legacy_u8 red, green, blue, alpha;
			assert(SDL_ReadSurfacePixel(surface, x, y, &red, &green, &blue, &alpha));
			assert(red == (legacy_u8)(expected >> LEGACY_WORD_BITS));
			assert(green == (legacy_u8)(expected >> LEGACY_BYTE_BITS));
			assert(blue == (legacy_u8)expected);
			assert(alpha == SDL_ALPHA_OPAQUE);
		}
	}
	SDL_DestroySurface(surface);
}

static void press_screenshot_key(SDL_Keymod modifiers)
{
	send_key(SDL_SCANCODE_F12, modifiers, true, false);
	assert(kb_read_char() == 0);
	assert(kb_get_key_state(DOS_KB_F12_SCANCODE) == 0);
}

static void release_screenshot_key(void)
{
	send_key(SDL_SCANCODE_F12, SDL_KMOD_NONE, false, false);
	assert(kb_read_char() == 0);
	assert(kb_get_key_state(DOS_KB_F12_SCANCODE) == 0);
}

static void test_screenshots(void)
{
	/* --data-dir changes the working directory before game startup. Keep every
	 * screenshot assertion in an isolated directory matching that contract. */
	legacy_char *original_directory = SDL_GetCurrentDirectory();
	assert(original_directory != NULL);
	legacy_char directory[SCREENSHOT_TEST_DIRECTORY_BYTES];
	legacy_s32 length = snprintf(directory, sizeof(directory), "screenshot-test-%" LEGACY_PRIu64,
								 (legacy_u64)SDL_GetTicksNS());
	assert(length > 0 && length < (legacy_s32)sizeof(directory));
	assert(!SDL_GetPathInfo(directory, NULL));
	assert(SDL_CreateDirectory(directory));
	assert(chdir(directory) == 0);
	kb_init_interrupt();
	sdl3_video_shutdown();
	press_screenshot_key(SDL_KMOD_LCTRL);
	release_screenshot_key();
	assert(sdl3_video_window() == NULL);
	assert_no_screenshot(SCREENSHOT_TEST_EXISTING_FILE);

	legacy_char path[sizeof(SCREENSHOT_TEST_FILENAME)];
	screenshot_test_path(SCREENSHOT_TEST_EXISTING_FILE, path);
	FILE *existing = fopen(path, "wb");
	assert(existing != NULL);
	assert(fwrite(SCREENSHOT_TEST_EXISTING_CONTENT, 1, sizeof(SCREENSHOT_TEST_EXISTING_CONTENT),
				  existing) == sizeof(SCREENSHOT_TEST_EXISTING_CONTENT));
	assert(fclose(existing) == 0);
	screenshot_test_path(SCREENSHOT_TEST_EXISTING_DIRECTORY, path);
	assert(SDL_CreateDirectory(path));

	dos_video_set_mode_13h();
	hires_shutdown();
	frame_adaptive_reset(&frame_adaptive);
	legacy_u8 colors[] = {PAGE_VIDEO_DAC_MAX, 0, 0, 0, 0, PAGE_VIDEO_DAC_MAX};
	dos_video_set_palette(PAGE_VIDEO_FIRST_INDEX, PAGE_VIDEO_COLOR_COUNT, colors);
	memset(framebuffer, PAGE_VIDEO_FIRST_INDEX, PAGE_VIDEO_PIXELS);
	sdl3_video_present();
	legacy_u32 image_number = SCREENSHOT_TEST_FIRST_IMAGE;
	press_screenshot_key(SDL_KMOD_LCTRL);
	assert_screenshot(image_number++, SDL3_SCREEN_WIDTH, SDL3_SCREEN_HEIGHT, PAGE_VIDEO_RED,
					  PAGE_VIDEO_RED);
	/* Repeats, duplicate down events and releasing Ctrl while F12 remains held
	 * must neither capture again nor reach the HyperVision action. */
	send_key(SDL_SCANCODE_F12, SDL_KMOD_LCTRL, true, true);
	press_screenshot_key(SDL_KMOD_LCTRL);
	press_screenshot_key(SDL_KMOD_NONE);
	press_screenshot_key(SDL_KMOD_SHIFT);
	assert_no_screenshot(image_number);
	release_screenshot_key();

	sdl3_video_begin_frame();
	memset(framebuffer, PAGE_VIDEO_SECOND_INDEX, PAGE_VIDEO_PIXELS / 2);
	legacy_u8 next_colors[] = {PAGE_VIDEO_DAC_MAX, PAGE_VIDEO_DAC_MAX, 0, 0, PAGE_VIDEO_DAC_MAX, 0};
	dos_video_set_palette(PAGE_VIDEO_FIRST_INDEX, PAGE_VIDEO_COLOR_COUNT, next_colors);
	press_screenshot_key(SDL_KMOD_RCTRL | SDL_KMOD_CAPS | SDL_KMOD_NUM);
	assert_screenshot(image_number++, SDL3_SCREEN_WIDTH, SDL3_SCREEN_HEIGHT, PAGE_VIDEO_RED,
					  PAGE_VIDEO_RED);
	release_screenshot_key();
	memset(framebuffer + PAGE_VIDEO_PIXELS / 2, PAGE_VIDEO_SECOND_INDEX, PAGE_VIDEO_PIXELS / 2);
	sdl3_video_end_frame();
	press_screenshot_key(SDL_KMOD_CTRL);
	assert_screenshot(image_number++, SDL3_SCREEN_WIDTH, SDL3_SCREEN_HEIGHT, PAGE_VIDEO_GREEN,
					  PAGE_VIDEO_GREEN);
	release_screenshot_key();

	high_resolution_active = true;
	hires_set_render_scale(HIRES_SCALE);
	for (size_t pixel = 0; pixel < SDL_arraysize(argb_framebuffer); pixel++) {
		argb_framebuffer[pixel] =
			pixel < SDL_arraysize(argb_framebuffer) / 2 ? PAGE_VIDEO_RED : PAGE_VIDEO_GREEN;
	}
	argb_active = true;
	frame_generation++;
	sdl3_video_present();
	sdl3_video_begin_frame();
	/* A pending resolution change destroys the source ARGB pixels. Capture
	 * must still retain the completed page's pixels and native dimensions. */
	memset(argb_framebuffer, 0, sizeof(argb_framebuffer));
	hires_set_render_scale(FRAME_ADAPTIVE_HALF_SCALE);
	memset(high_resolution_framebuffer, PAGE_VIDEO_SECOND_INDEX,
		   sizeof(high_resolution_framebuffer));
	legacy_u8 blue[] = {0, 0, PAGE_VIDEO_DAC_MAX};
	dos_video_set_palette(PAGE_VIDEO_SECOND_INDEX, 1, blue);
	press_screenshot_key(SDL_KMOD_RCTRL);
	assert_screenshot(image_number++, HIRES_WIDTH, HIRES_HEIGHT, PAGE_VIDEO_RED, PAGE_VIDEO_GREEN);
	release_screenshot_key();
	sdl3_video_end_frame();
	press_screenshot_key(SDL_KMOD_LCTRL);
	assert_screenshot(image_number++, SDL3_SCREEN_WIDTH * FRAME_ADAPTIVE_HALF_SCALE,
					  SDL3_SCREEN_HEIGHT * FRAME_ADAPTIVE_HALF_SCALE, PAGE_VIDEO_BLUE,
					  PAGE_VIDEO_BLUE);
	release_screenshot_key();

	const SDL_Keymod rejected[] = {SDL_KMOD_CTRL | SDL_KMOD_SHIFT, SDL_KMOD_CTRL | SDL_KMOD_ALT,
								   SDL_KMOD_CTRL | SDL_KMOD_GUI};
	for (legacy_u32 modifier = 0; modifier < SDL_arraysize(rejected); modifier++) {
		send_key(SDL_SCANCODE_F12, rejected[modifier], true, false);
		assert(kb_read_char() == 0);
		press_screenshot_key(SDL_KMOD_CTRL);
		release_screenshot_key();
	}
	send_key(SDL_SCANCODE_F12, SDL_KMOD_NONE, true, false);
	assert(kb_read_char() == KEY_F12);
	send_key(SDL_SCANCODE_F12, SDL_KMOD_LCTRL, true, false);
	assert(kb_read_char() == 0);
	release_screenshot_key();
	assert_no_screenshot(image_number);

	screenshot_test_path(SCREENSHOT_TEST_EXISTING_FILE, path);
	existing = fopen(path, "rb");
	assert(existing != NULL);
	legacy_char saved[sizeof(SCREENSHOT_TEST_EXISTING_CONTENT)];
	assert(fread(saved, 1, sizeof(saved), existing) == sizeof(saved));
	assert(memcmp(saved, SCREENSHOT_TEST_EXISTING_CONTENT, sizeof(saved)) == 0);
	assert(fgetc(existing) == EOF);
	assert(fclose(existing) == 0);
	for (legacy_u32 number = SCREENSHOT_TEST_EXISTING_FILE; number < image_number; number++) {
		screenshot_test_path(number, path);
		assert(SDL_RemovePath(path));
	}
	assert(chdir(original_directory) == 0);
	assert(SDL_RemovePath(directory));
	SDL_free(original_directory);
	hires_shutdown();
	dos_video_set_mode_13h();
}

static void test_adaptive_frame_timing(void)
{
	frame_adaptive_reset(&frame_adaptive);
	frame_render_timing_reset();
	const legacy_u64 prepared_render_ns = PRESENTATION_SECOND_NS / 2U;
	sdl3_video_begin_track_frame(1);
	sdl3_video_add_render_work(prepared_render_ns);
	sdl3_video_refresh();
	assert(frame_adaptive.samples == 0);
	assert(render_timing_samples == 0);
	sdl3_video_end_frame();
	assert(frame_adaptive.samples == 1);
	assert(render_timing_samples == 1);
	/* Adaptive quality caps stalls; the HUD must retain their actual duration. */
	assert(frame_adaptive.elapsed_ns ==
		   SDL_min(render_timing_last_ns, FRAME_ADAPTIVE_MAX_SAMPLE_NS));
	assert(render_timing_last_ns >= prepared_render_ns);
	assert(frame_adaptive.elapsed_ns > 0);
	legacy_u64 elapsed = frame_adaptive.elapsed_ns;
	/* Incidental presentations, menus and classic frames must not influence quality. */
	sdl3_video_present();
	sdl3_video_redraw();
	assert(render_timing_samples == 1);
	sdl3_video_begin_frame();
	sdl3_video_end_frame();
	assert(render_timing_samples == 2 && render_timing_last_ns > 0);
	sdl3_video_begin_track_frame(0);
	sdl3_video_end_frame();
	assert(render_timing_samples == 3 && render_timing_last_ns > 0);
	assert(frame_adaptive.samples == 1);
	assert(frame_adaptive.elapsed_ns == elapsed);
	for (enum FRAME_ADAPTIVE_PRESET preset = FRAME_ADAPTIVE_PRESET_FULL;
		 preset <= FRAME_ADAPTIVE_PRESET_LOW; preset++) {
		frame_adaptive_set_preset(&frame_adaptive, preset);
		struct FRAME_ADAPTIVE_STATE unchanged = frame_adaptive;
		legacy_u32 completed_render_samples = render_timing_samples;
		sdl3_video_begin_track_frame(1);
		sdl3_video_refresh();
		sdl3_video_end_frame();
		assert(memcmp(&frame_adaptive, &unchanged, sizeof(unchanged)) == 0);
		assert(render_timing_samples == completed_render_samples + 1U);
		/* Preview/menu frames retain the chosen lock as well as its raster scale. */
		sdl3_video_begin_frame();
		assert(hires_render_scale() == frame_adaptive_render_scale(&frame_adaptive));
		sdl3_video_end_frame();
		assert(memcmp(&frame_adaptive, &unchanged, sizeof(unchanged)) == 0);
	}
	frame_adaptive_set_preset(&frame_adaptive, FRAME_ADAPTIVE_PRESET_AUTO);
	sdl3_video_begin_track_frame(1);
	sdl3_video_end_frame();
	assert(frame_adaptive.samples == 1 && frame_adaptive.elapsed_ns > 0);
	sdl3_video_begin_frame();
	assert(hires_render_scale() == HIRES_SCALE);
	sdl3_video_end_frame();
}

static void test_high_resolution_video(void)
{
	legacy_u8 colors[] = {63, 0, 0, 0, 0, 63, 0, 63, 0};
	dos_video_set_palette(3, 3, colors);
	memset(framebuffer, 3, 64000);
	memset(high_resolution_framebuffer, 3, sizeof(high_resolution_framebuffer));
	for (legacy_s32 row = 0; row < 800; row++) {
		high_resolution_framebuffer[row * 1280 + 101] = 4;
	}
	high_resolution_active = true;
	frame_generation++;
	check_video_aspect(1100, 720, 70.0f, 0.0f);
	check_video_aspect(960, 800, 0.0f, 40.0f);
	assert(SDL_SetWindowSize(sdl3_video_window(), 1280, 960));
	assert(SDL_SyncWindow(sdl3_video_window()));
	SDL_PumpEvents();
	sdl3_video_present();
	/* Adjacent output pixels must retain detail smaller than one legacy pixel. */
	assert_presented_color(100, 120, 255, 0, 0);
	assert_presented_color(101, 120, 0, 0, 255);
	assert_presented_color(102, 120, 255, 0, 0);
	for (legacy_s32 row = 0; row < 800; row++) {
		high_resolution_framebuffer[row * 1280 + 101] = 5;
	}
	frame_generation++;
	SDL_Delay(11);
	sdl3_video_refresh();
	assert_presented_color(101, 120, 0, 255, 0);
	sdl3_video_begin_frame();
	for (legacy_s32 row = 0; row < 800; row++) {
		high_resolution_framebuffer[row * 1280 + 102] = 4;
	}
	frame_generation++;
	SDL_Delay(11);
	sdl3_video_refresh();
	assert_presented_color(102, 120, 255, 0, 0);
	sdl3_video_end_frame();
	assert_presented_color(102, 120, 0, 0, 255);
	for (legacy_u32 pixel = 0; pixel < 1280 * 800; pixel++) {
		argb_framebuffer[pixel] = 0xFF123456U;
	}
	argb_active = true;
	frame_generation++;
	SDL_Delay(11);
	sdl3_video_refresh();
	assert_presented_color(101, 120, 18, 52, 86);
	argb_active = false;
	high_resolution_active = false;
	frame_generation++;
	SDL_Delay(11);
	sdl3_video_refresh();
	assert_presented_color(101, 120, 255, 0, 0);
	assert_presented_color(102, 120, 255, 0, 0);
	check_video_aspect(960, 720, 0.0f, 0.0f);
}

enum {
	SCALE_VIDEO_WINDOW_WIDTH = HIRES_WIDTH,
	SCALE_VIDEO_WINDOW_HEIGHT = 1024,
	SCALE_VIDEO_VIEW_HEIGHT = 960,
	SCALE_VIDEO_BORDER_TOP = (SCALE_VIDEO_WINDOW_HEIGHT - SCALE_VIDEO_VIEW_HEIGHT) / 2,
	SCALE_VIDEO_MARKER_X = 25,
	SCALE_VIDEO_MARKER_Y = 30,
	SCALE_VIDEO_RED_INDEX = 3,
	SCALE_VIDEO_BLUE_INDEX = 4,
	SCALE_VIDEO_DAC_MAX = 63
};
#define SCALE_VIDEO_BACKGROUND_ARGB 0xFF123456U
#define SCALE_VIDEO_MARKER_ARGB 0xFFABCDEFU
#define SCALE_VIDEO_CORNER_ARGB 0xFF00FF00U

static void assert_scale_video_layout(void)
{
	legacy_int width, height;
	assert(SDL_GetWindowSize(sdl3_video_window(), &width, &height));
	assert(width == SCALE_VIDEO_WINDOW_WIDTH && height == SCALE_VIDEO_WINDOW_HEIGHT);
	SDL_FRect bounds;
	assert(SDL_GetRenderLogicalPresentationRect(SDL_GetRenderer(sdl3_video_window()), &bounds));
	assert_coordinate(bounds.x, 0);
	assert_coordinate(bounds.y, SCALE_VIDEO_BORDER_TOP);
	assert_coordinate(bounds.w, SCALE_VIDEO_WINDOW_WIDTH);
	assert_coordinate(bounds.h, SCALE_VIDEO_VIEW_HEIGHT);
	legacy_f32 x, y;
	sdl3_video_game_to_window(SDL3_SCREEN_WIDTH / 2, SDL3_SCREEN_HEIGHT / 2, &x, &y);
	assert_coordinate(x, SCALE_VIDEO_WINDOW_WIDTH / 2);
	assert_coordinate(y, SCALE_VIDEO_WINDOW_HEIGHT / 2);
	sdl3_video_window_to_game(x, y, &x, &y);
	assert_coordinate(x, SDL3_SCREEN_WIDTH / 2);
	assert_coordinate(y, SDL3_SCREEN_HEIGHT / 2);
}

static void test_dynamic_resolution_video(void)
{
	const legacy_s32 scales[] = {HIRES_SCALE, FRAME_ADAPTIVE_HALF_SCALE,
								 FRAME_ADAPTIVE_MINIMUM_SCALE, HIRES_SCALE};
	legacy_u8 colors[] = {SCALE_VIDEO_DAC_MAX, 0, 0, 0, 0, SCALE_VIDEO_DAC_MAX};
	dos_video_set_palette(SCALE_VIDEO_RED_INDEX, 2, colors);
	assert(SDL_SetWindowSize(sdl3_video_window(), SCALE_VIDEO_WINDOW_WIDTH,
							 SCALE_VIDEO_WINDOW_HEIGHT));
	assert(SDL_SyncWindow(sdl3_video_window()));
	SDL_PumpEvents();
	memset(framebuffer, SCALE_VIDEO_RED_INDEX, sizeof(framebuffer));
	high_resolution_active = 1;
	for (size_t pass = 0; pass < sizeof(scales) / sizeof(scales[0]); pass++) {
		hires_set_render_scale(scales[pass]);
		legacy_s32 width = SDL3_SCREEN_WIDTH * scales[pass];
		legacy_s32 height = SDL3_SCREEN_HEIGHT * scales[pass];
		legacy_s32 source_x = SCALE_VIDEO_MARKER_X * scales[pass] + scales[pass] - 1;
		legacy_s32 source_y = SCALE_VIDEO_MARKER_Y * scales[pass] + scales[pass] - 1;
		legacy_s32 output_scale = HIRES_SCALE / scales[pass];
		legacy_s32 output_left = source_x * output_scale;
		legacy_s32 output_x = output_left + output_scale / 2;
		legacy_s32 output_y =
			SCALE_VIDEO_BORDER_TOP + (legacy_s32)((source_y + HIRES_SAMPLE_CENTER_OFFSET) *
												  SCALE_VIDEO_VIEW_HEIGHT / height);
		memset(high_resolution_framebuffer, SCALE_VIDEO_RED_INDEX,
			   sizeof(high_resolution_framebuffer));
		high_resolution_framebuffer[(size_t)source_y * width + source_x] = SCALE_VIDEO_BLUE_INDEX;
		argb_active = 0;
		frame_generation++;
		sdl3_video_begin_track_frame(0);
		assert(hires_render_scale() == scales[pass]);
		sdl3_video_end_frame();
		assert(presented_raster_width == width && presented_raster_height == height);
		assert_scale_video_layout();
		assert_presented_color(output_x, output_y, 0, 0, LEGACY_U8_MAX);
		assert_presented_color(output_left - 1, output_y, LEGACY_U8_MAX, 0, 0);
		assert_presented_color(output_left + output_scale, output_y, LEGACY_U8_MAX, 0, 0);
		for (size_t pixel = 0; pixel < (size_t)width * height; pixel++) {
			argb_framebuffer[pixel] = SCALE_VIDEO_BACKGROUND_ARGB;
		}
		argb_framebuffer[(size_t)source_y * width + source_x] = SCALE_VIDEO_MARKER_ARGB;
		argb_framebuffer[(size_t)width * height - 1] = SCALE_VIDEO_CORNER_ARGB;
		argb_active = 1;
		frame_generation++;
		sdl3_video_begin_track_frame(0);
		sdl3_video_end_frame();
		assert(presented_raster_width == width && presented_raster_height == height);
		assert_scale_video_layout();
		assert_presented_color(output_x, output_y, 0xABU, 0xCDU, 0xEFU);
		assert_presented_color(output_left - 1, output_y, 0x12U, 0x34U, 0x56U);
		assert_presented_color(SCALE_VIDEO_WINDOW_WIDTH - 1,
							   SCALE_VIDEO_BORDER_TOP + SCALE_VIDEO_VIEW_HEIGHT - 1, 0,
							   LEGACY_U8_MAX, 0);
	}
	/* Menu/intro frames restore full artwork resolution; classic mode still
	 * uploads the original framebuffer without resizing the window. */
	hires_set_render_scale(FRAME_ADAPTIVE_MINIMUM_SCALE);
	sdl3_video_begin_frame();
	assert(hires_render_scale() == HIRES_SCALE);
	sdl3_video_end_frame();
	assert(presented_raster_width == HIRES_WIDTH && presented_raster_height == HIRES_HEIGHT);
	hires_shutdown();
	sdl3_video_present();
	assert(presented_raster_width == SDL3_SCREEN_WIDTH &&
		   presented_raster_height == SDL3_SCREEN_HEIGHT);
	assert_scale_video_layout();
	assert_presented_color(SCALE_VIDEO_WINDOW_WIDTH / 2, SCALE_VIDEO_WINDOW_HEIGHT / 2,
						   LEGACY_U8_MAX, 0, 0);
}

static void check_fullscreen(legacy_u8 expected)
{
	assert(SDL_SyncWindow(sdl3_video_window()));
	sdl3_platform_pump();
	assert(((SDL_GetWindowFlags(sdl3_video_window()) & SDL_WINDOW_FULLSCREEN) != 0) == expected);
	SDL_Renderer *renderer = SDL_GetRenderer(sdl3_video_window());
	/* SDL output pointers require the library's exact integer type. */
	legacy_int width;
	legacy_int height;
	assert(SDL_GetRenderOutputSize(renderer, &width, &height));
	legacy_f32 expected_width = (legacy_f32)width;
	legacy_f32 expected_height = expected_width * 3.0f / 4.0f;
	if (expected_height > height) {
		expected_height = (legacy_f32)height;
		expected_width = expected_height * 4.0f / 3.0f;
	}
	SDL_FRect bounds;
	assert(SDL_GetRenderLogicalPresentationRect(renderer, &bounds));
	assert_coordinate(bounds.x, ((legacy_f32)width - expected_width) / 2.0f);
	assert_coordinate(bounds.y, ((legacy_f32)height - expected_height) / 2.0f);
	assert_coordinate(bounds.w, expected_width);
	assert_coordinate(bounds.h, expected_height);
	legacy_f32 x;
	legacy_f32 y;
	sdl3_video_game_to_window(160, 100, &x, &y);
	assert_coordinate(x, width / 2.0f);
	assert_coordinate(y, height / 2.0f);
	sdl3_video_window_to_game(x, y, &x, &y);
	assert_coordinate(x, 160.0f);
	assert_coordinate(y, 100.0f);
}

static void test_fullscreen_shortcut(void)
{
	kb_init_interrupt();
	check_video_aspect(1100, 720, 70.0f, 0.0f);
	/* SDL reads and writes window coordinates through exact native integer pointers. */
	legacy_int original_x;
	legacy_int original_y;
	assert(SDL_GetWindowPosition(sdl3_video_window(), &original_x, &original_y));
	send_key(SDL_SCANCODE_LALT, SDL_KMOD_LALT, true, false);
	send_key(SDL_SCANCODE_RETURN, SDL_KMOD_LALT, true, false);
	assert(kb_read_char() == 0);
	check_fullscreen(true);
	assert(kb_get_key_state(28) == 0);
	assert(kb_get_key_state(56) == 1);
	/* Neither auto-repeat nor duplicate key-down events toggle a held shortcut. */
	send_key(SDL_SCANCODE_RETURN, SDL_KMOD_LALT, true, true);
	send_key(SDL_SCANCODE_RETURN, SDL_KMOD_LALT, true, false);
	assert(kb_read_char() == 0);
	check_fullscreen(true);
	SDL_Event focus_lost;
	SDL_zero(focus_lost);
	focus_lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
	assert(SDL_PushEvent(&focus_lost));
	send_key(SDL_SCANCODE_LALT, SDL_KMOD_NONE, false, false);
	send_key(SDL_SCANCODE_RETURN, SDL_KMOD_NONE, true, true);
	assert(kb_read_char() == 0);
	assert(kb_get_key_state(28) == 0);
	assert(kb_get_key_state(56) == 0);
	send_key(SDL_SCANCODE_RETURN, SDL_KMOD_NONE, false, false);
	send_key(SDL_SCANCODE_RALT, SDL_KMOD_RALT, true, false);
	send_key(SDL_SCANCODE_KP_ENTER, SDL_KMOD_RALT, true, false);
	assert(kb_read_char() == 0);
	check_fullscreen(false);
	assert(kb_get_key_state(28) == 0);
	send_key(SDL_SCANCODE_KP_ENTER, SDL_KMOD_NONE, false, false);
	send_key(SDL_SCANCODE_RALT, SDL_KMOD_NONE, false, false);
	assert(kb_read_char() == 0);
	legacy_int width;
	legacy_int height;
	legacy_int restored_x;
	legacy_int restored_y;
	assert(SDL_GetWindowSize(sdl3_video_window(), &width, &height));
	assert(width == 1100 && height == 720);
	assert(SDL_GetWindowPosition(sdl3_video_window(), &restored_x, &restored_y));
	assert(restored_x == original_x && restored_y == original_y);
	/* Consuming the shortcut must not suppress the next ordinary Enter. */
	send_key(SDL_SCANCODE_RETURN, SDL_KMOD_NONE, true, false);
	assert(kb_read_char() == KEY_ENTER);
	assert(kb_get_key_state(28) == 1);
	send_key(SDL_SCANCODE_RETURN, SDL_KMOD_NONE, false, false);
	assert(kb_get_key_state(28) == 0);
	send_key(SDL_SCANCODE_KP_ENTER, SDL_KMOD_NONE, true, false);
	assert(kb_read_char() == KEY_ENTER);
	send_key(SDL_SCANCODE_KP_ENTER, SDL_KMOD_NONE, false, false);
	assert(kb_get_key_state(28) == 0);
}

static void test_joystick(void)
{
	SDL_Event focus;
	SDL_zero(focus);
	focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
	assert(SDL_PushEvent(&focus));
	sdl3_platform_pump();
	assert(SDL_InitSubSystem(SDL_INIT_JOYSTICK));
	SDL_VirtualJoystickDesc descriptor;
	SDL_INIT_INTERFACE(&descriptor);
	descriptor.type = SDL_JOYSTICK_TYPE_UNKNOWN;
	descriptor.naxes = 2;
	descriptor.nbuttons = 2;
	descriptor.nhats = 1;
	descriptor.name = "Restunts platform test";
	SDL_JoystickID id = SDL_AttachVirtualJoystick(&descriptor);
	assert(id != 0);
	SDL_Joystick *virtual_joystick = SDL_OpenJoystick(id);
	assert(virtual_joystick != NULL);
	dos_joystick_reset_calibration();
	assert(dos_joystick_is_enabled());
	assert(SDL_SetJoystickVirtualAxis(virtual_joystick, 0, -32768));
	assert(SDL_SetJoystickVirtualAxis(virtual_joystick, 1, 32767));
	assert(SDL_SetJoystickVirtualButton(virtual_joystick, 0, true));
	SDL_UpdateJoysticks();
	assert(dos_get_joy_flags() == (8 | 2 | 16));
	assert(dos_joystick_get_scaled_axis(0) == -32);
	assert(dos_joystick_get_scaled_axis(1) == 31);
	assert(SDL_SetJoystickVirtualAxis(virtual_joystick, 0, 0));
	assert(SDL_SetJoystickVirtualAxis(virtual_joystick, 1, 0));
	assert(SDL_SetJoystickVirtualButton(virtual_joystick, 0, false));
	assert(SDL_SetJoystickVirtualHat(virtual_joystick, 0, SDL_HAT_RIGHTUP));
	SDL_UpdateJoysticks();
	assert(dos_get_joy_flags() == (4 | 1));
	/* Disabling racing input retains the device for menu navigation. */
	SDL_CloseJoystick(virtual_joystick);
	assert(SDL_GetJoystickFromID(id) != NULL);
	dos_joystick_set_enabled(0);
	assert(SDL_GetJoystickFromID(id) != NULL);
	assert(sdl3_joystick_menu_flags() == (INPUT_STEER_RIGHT_FLAG | INPUT_ACCELERATE_FLAG));
	assert(dos_get_joy_flags() == 0);
	assert(dos_joystick_get_scaled_axis(0) == 0);
	dos_joystick_set_enabled(1);
	virtual_joystick = SDL_GetJoystickFromID(id);
	assert(virtual_joystick != NULL);
	assert(SDL_SetJoystickVirtualHat(virtual_joystick, 0, SDL_HAT_LEFTDOWN));
	SDL_UpdateJoysticks();
	assert(dos_get_joy_flags() == (INPUT_STEER_LEFT_FLAG | INPUT_BRAKE_FLAG));
	/* Selecting racing input does not accumulate extra SDL references. */
	dos_joystick_set_enabled(1);
	dos_joystick_set_enabled(0);
	assert(SDL_GetJoystickFromID(id) != NULL);
	assert(SDL_DetachVirtualJoystick(id));
	sdl3_platform_pump();
	id = SDL_AttachVirtualJoystick(&descriptor);
	assert(id != 0);
	sdl3_platform_pump();
	assert(SDL_GetJoystickFromID(id) != NULL);
	dos_joystick_set_enabled(1);
	assert(SDL_GetJoystickFromID(id) != NULL);
	/* Device removal while enabled releases the platform reference as well. */
	assert(SDL_DetachVirtualJoystick(id));
	sdl3_platform_pump();
	assert(SDL_GetJoystickFromID(id) == NULL);
	assert(dos_get_joy_flags() == 0);
	dos_joystick_set_enabled(0);
}

#define TEST_CONTROLLER_AXIS_SCALE 32
#define TEST_CONTROLLER_AXIS_MIN LEGACY_S16_FROM_BITS(LEGACY_U16_SIGN_BIT)
#define TEST_CONTROLLER_AXIS_MAX ((legacy_s16)LEGACY_S16_MAX)
#define TEST_CONTROLLER_PARTIAL_TURN ((legacy_s16)(LEGACY_U16_SIGN_BIT / 4U))
#define TEST_CONTROLLER_RECENTER_TURN ((legacy_s16)(TEST_CONTROLLER_PARTIAL_TURN >> 1U))
#define TEST_CONTROLLER_STEERING_NOISE ((legacy_s16)(TEST_CONTROLLER_PARTIAL_TURN >> 3U))
#define TEST_CONTROLLER_PEDAL_THRESHOLD ((legacy_s16)(LEGACY_U16_SIGN_BIT / 2U))
#define TEST_CONTROLLER_MENU_REPEAT_WAIT_MS 400U
#define TEST_CONTROLLER_MENU_REPEAT_INTERVAL_WAIT_MS 120U
#define TEST_CONTROLLER_SCAN_ESCAPE 1
#define TEST_CONTROLLER_SCAN_Q 16
#define TEST_CONTROLLER_SCAN_R 19
#define TEST_CONTROLLER_SCAN_T 20
#define TEST_CONTROLLER_SCAN_ENTER 28
#define TEST_CONTROLLER_SCAN_SPACE 57
#define TEST_CONTROLLER_SCAN_D 32
#define TEST_CONTROLLER_SCAN_C 46
#define TEST_CONTROLLER_SCAN_UP 72
#define TEST_CONTROLLER_SCAN_DOWN 80
#define TEST_CONTROLLER_SCAN_LEFT 75
#define TEST_CONTROLLER_SCAN_RIGHT 77

static SDL_JoystickID attach_test_controller(SDL_Joystick **device)
{
	SDL_VirtualJoystickDesc descriptor;
	SDL_INIT_INTERFACE(&descriptor);
	descriptor.type = SDL_JOYSTICK_TYPE_GAMEPAD;
	descriptor.naxes = SDL_GAMEPAD_AXIS_COUNT;
	descriptor.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
	descriptor.name = "Restunts mapped controller test";
	SDL_JoystickID id = SDL_AttachVirtualJoystick(&descriptor);
	assert(id != 0);
	*device = SDL_OpenJoystick(id);
	assert(*device != NULL);
	/* Virtual joystick triggers start at their raw negative endpoint. */
	assert(SDL_SetJoystickVirtualAxis(*device, SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
									  TEST_CONTROLLER_AXIS_MIN));
	assert(SDL_SetJoystickVirtualAxis(*device, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
									  TEST_CONTROLLER_AXIS_MIN));
	SDL_UpdateJoysticks();
	sdl3_platform_pump();
	assert(SDL_IsGamepad(id));
	assert(SDL_GetGamepadFromID(id) != NULL);
	return id;
}

static void controller_axis(SDL_Joystick *device, SDL_GamepadAxis axis, legacy_s16 value)
{
	assert(SDL_SetJoystickVirtualAxis(device, axis, value));
	SDL_UpdateJoysticks();
	sdl3_platform_pump();
}

static void controller_button(SDL_Joystick *device, SDL_GamepadButton button, legacy_u8 down)
{
	assert(SDL_SetJoystickVirtualButton(device, button, down));
	SDL_UpdateJoysticks();
	sdl3_platform_pump();
}

static void test_controller_shortcuts(SDL_Joystick *device)
{
	sdl3_input_set_gameplay_active(true);
	static const struct {
		SDL_GamepadButton button;
		legacy_u16 key;
		legacy_s16 scan;
	} shortcuts[] = {{SDL_GAMEPAD_BUTTON_EAST, KEY_ESCAPE, TEST_CONTROLLER_SCAN_ESCAPE},
					 {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, KEY_ESCAPE, TEST_CONTROLLER_SCAN_ESCAPE},
					 {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, KEY_ESCAPE, TEST_CONTROLLER_SCAN_ESCAPE},
					 {SDL_GAMEPAD_BUTTON_NORTH, 't', TEST_CONTROLLER_SCAN_T},
					 {SDL_GAMEPAD_BUTTON_WEST, 'd', TEST_CONTROLLER_SCAN_D},
					 {SDL_GAMEPAD_BUTTON_DPAD_LEFT, 'q', TEST_CONTROLLER_SCAN_Q},
					 {SDL_GAMEPAD_BUTTON_BACK, 'c', TEST_CONTROLLER_SCAN_C}};
	for (legacy_u32 i = 0; i < SDL_arraysize(shortcuts); i++) {
		controller_button(device, shortcuts[i].button, true);
		assert(kb_read_char() == shortcuts[i].key);
		assert(kb_get_key_state(shortcuts[i].scan) == 1);
		assert(kb_read_char() == 0);
		controller_button(device, shortcuts[i].button, false);
		assert(kb_get_key_state(shortcuts[i].scan) == 0);
		assert(kb_read_char() == 0);
	}
	/* Releasing the controller's Q must preserve a held physical keyboard Q. */
	send_key(SDL_SCANCODE_Q, SDL_KMOD_NONE, true, false);
	assert(kb_read_char() == 'q');
	controller_button(device, SDL_GAMEPAD_BUTTON_DPAD_LEFT, true);
	assert(kb_read_char() == 'q');
	controller_button(device, SDL_GAMEPAD_BUTTON_DPAD_LEFT, false);
	assert(kb_get_key_state(TEST_CONTROLLER_SCAN_Q) == 1);
	send_key(SDL_SCANCODE_Q, SDL_KMOD_NONE, false, false);
	assert(kb_get_key_state(TEST_CONTROLLER_SCAN_Q) == 0);
	/* Y holds T independently of a physical T and never presses restart R. */
	send_key(SDL_SCANCODE_T, SDL_KMOD_NONE, true, false);
	assert(kb_read_char() == 't');
	controller_button(device, SDL_GAMEPAD_BUTTON_NORTH, true);
	assert(kb_read_char() == 't');
	assert(kb_get_key_state(TEST_CONTROLLER_SCAN_R) == 0);
	controller_button(device, SDL_GAMEPAD_BUTTON_NORTH, false);
	assert(kb_get_key_state(TEST_CONTROLLER_SCAN_T) == 1);
	send_key(SDL_SCANCODE_T, SDL_KMOD_NONE, false, false);
	assert(kb_get_key_state(TEST_CONTROLLER_SCAN_T) == 0);
	/* Right has no shortcut, driving flag or race-start action while driving. */
	controller_button(device, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, true);
	assert(kb_read_char() == 0);
	assert(kb_get_key_state(TEST_CONTROLLER_SCAN_T) == 0);
	assert(kb_get_key_state(TEST_CONTROLLER_SCAN_RIGHT) == 0);
	assert(sdl3_controller_driving_flags() == 0);
	assert(!sdl3_controller_take_race_start_request());
	controller_button(device, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, false);
	/* Stick clicks have no action while driving. */
	controller_button(device, SDL_GAMEPAD_BUTTON_LEFT_STICK, true);
	controller_button(device, SDL_GAMEPAD_BUTTON_RIGHT_STICK, true);
	assert(kb_read_char() == 0);
	controller_button(device, SDL_GAMEPAD_BUTTON_LEFT_STICK, false);
	controller_button(device, SDL_GAMEPAD_BUTTON_RIGHT_STICK, false);
	sdl3_input_set_gameplay_active(false);
}

static void test_controller_triggers(SDL_Joystick *device)
{
	static const SDL_GamepadAxis triggers[] = {SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
											   SDL_GAMEPAD_AXIS_RIGHT_TRIGGER};
	for (legacy_u32 i = 0; i < SDL_arraysize(triggers); i++) {
		controller_axis(device, triggers[i], TEST_CONTROLLER_AXIS_MAX);
		assert(kb_read_char() == KEY_SPACE);
		assert(kb_get_key_state(TEST_CONTROLLER_SCAN_SPACE) == 1);
		assert(kb_read_char() == 0);
		controller_axis(device, triggers[i], TEST_CONTROLLER_AXIS_MIN);
		assert(kb_get_key_state(TEST_CONTROLLER_SCAN_SPACE) == 0);
	}
	/* Controller confirmation must not become gear input while driving. */
	sdl3_input_set_gameplay_active(true);
	controller_button(device, SDL_GAMEPAD_BUTTON_SOUTH, true);
	controller_axis(device, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, TEST_CONTROLLER_AXIS_MAX);
	assert(kb_read_char() == KEY_ENTER);
	assert(kb_read_char() == KEY_SPACE);
	assert(kb_get_key_state(TEST_CONTROLLER_SCAN_ENTER) == 0);
	assert(kb_get_key_state(TEST_CONTROLLER_SCAN_SPACE) == 0);
	assert(sdl3_controller_driving_flags() == 0);
	controller_button(device, SDL_GAMEPAD_BUTTON_SOUTH, false);
	controller_axis(device, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, TEST_CONTROLLER_AXIS_MIN);
	sdl3_input_set_gameplay_active(false);
}

static void test_controller_navigation(SDL_Joystick *device)
{
	sdl3_input_set_gameplay_active(false);
	sdl3_input_set_driving_mode(SDL3_DRIVING_KEYBOARD);
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTX, TEST_CONTROLLER_AXIS_MAX);
	assert(kb_read_char() == KEY_RIGHT);
	assert(kb_get_key_state(TEST_CONTROLLER_SCAN_RIGHT) == 0);
	assert(kb_read_char() == 0);
	SDL_Delay(TEST_CONTROLLER_MENU_REPEAT_WAIT_MS);
	assert(kb_read_char() == KEY_RIGHT);
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTX, 0);
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTY, TEST_CONTROLLER_AXIS_MIN);
	assert(kb_read_char() == KEY_UP);
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTY, TEST_CONTROLLER_AXIS_MAX);
	assert(kb_read_char() == KEY_DOWN);
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTY, 0);
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTX, TEST_CONTROLLER_AXIS_MIN);
	assert(kb_read_char() == KEY_LEFT);
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTX, 0);
	assert(kb_read_char() == 0);
}

static void test_controller_driving(SDL_Joystick *device)
{
	sdl3_input_set_gameplay_active(true);
	sdl3_input_set_driving_mode(SDL3_DRIVING_KEYBOARD);
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTX, TEST_CONTROLLER_PARTIAL_TURN);
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTY, TEST_CONTROLLER_AXIS_MIN);
	assert(sdl3_input_driving_mode() == SDL3_DRIVING_CONTROLLER);
	assert(sdl3_controller_scaled_x() > 0);
	assert(sdl3_controller_scaled_x() < TEST_CONTROLLER_AXIS_SCALE);
	assert((sdl3_controller_driving_flags() & INPUT_PEDAL_MASK) == INPUT_ACCELERATE_FLAG);
	assert(kb_get_key_state(TEST_CONTROLLER_SCAN_RIGHT) == 0);
	assert(kb_read_char() == 0);
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTY, TEST_CONTROLLER_AXIS_MAX);
	assert((sdl3_controller_driving_flags() & INPUT_PEDAL_MASK) == INPUT_BRAKE_FLAG);
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTY, 0);
	assert((sdl3_controller_driving_flags() & INPUT_PEDAL_MASK) == 0);
	/* Keyboard steering takes over even while the stick remains deflected. */
	send_key(SDL_SCANCODE_LEFT, SDL_KMOD_NONE, true, false);
	assert(kb_read_char() == KEY_LEFT);
	assert(sdl3_input_driving_mode() == SDL3_DRIVING_KEYBOARD);
	assert(kb_get_key_state(TEST_CONTROLLER_SCAN_LEFT) == 1);
	assert(sdl3_input_driving_mode() == SDL3_DRIVING_KEYBOARD);
	send_key(SDL_SCANCODE_LEFT, SDL_KMOD_NONE, false, false);
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTX, TEST_CONTROLLER_RECENTER_TURN);
	assert(sdl3_input_driving_mode() == SDL3_DRIVING_KEYBOARD);
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTX, 0);
	assert(sdl3_input_driving_mode() == SDL3_DRIVING_KEYBOARD);
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTX, TEST_CONTROLLER_AXIS_MIN);
	assert(sdl3_input_driving_mode() == SDL3_DRIVING_CONTROLLER);
	assert(sdl3_controller_scaled_x() == -TEST_CONTROLLER_AXIS_SCALE);
	/* D-pad gears are independent from steering and menu navigation. */
	controller_button(device, SDL_GAMEPAD_BUTTON_DPAD_UP, true);
	assert((sdl3_controller_driving_flags() & INPUT_SHIFT_UP_FLAG) != 0);
	controller_button(device, SDL_GAMEPAD_BUTTON_DPAD_UP, false);
	controller_button(device, SDL_GAMEPAD_BUTTON_DPAD_DOWN, true);
	controller_button(device, SDL_GAMEPAD_BUTTON_DPAD_DOWN, false);
	assert((sdl3_controller_driving_flags() & INPUT_SHIFT_DOWN_FLAG) != 0);
	assert(kb_read_char() == 0);
	assert(sdl3_controller_driving_flags() == 0);
	/* Left stick rotates the camera without selecting a steering source. */
	sdl3_input_set_driving_mode(SDL3_DRIVING_KEYBOARD);
	controller_axis(device, SDL_GAMEPAD_AXIS_LEFTX, TEST_CONTROLLER_AXIS_MAX);
	assert((sdl3_controller_camera_flags() & INPUT_STEER_RIGHT_FLAG) != 0);
	assert(sdl3_input_driving_mode() == SDL3_DRIVING_KEYBOARD);
	assert(sdl3_controller_driving_flags() == 0);
	controller_axis(device, SDL_GAMEPAD_AXIS_LEFTX, 0);
	controller_axis(device, SDL_GAMEPAD_AXIS_LEFTY, TEST_CONTROLLER_AXIS_MIN);
	assert((sdl3_controller_camera_flags() & INPUT_ACCELERATE_FLAG) != 0);
	controller_axis(device, SDL_GAMEPAD_AXIS_LEFTY, 0);
	/* Dialogs accept the stick without driving or rotating the camera. */
	sdl3_input_push_modal();
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTX, 0);
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTY, TEST_CONTROLLER_AXIS_MAX);
	controller_axis(device, SDL_GAMEPAD_AXIS_LEFTX, TEST_CONTROLLER_AXIS_MAX);
	assert(kb_read_char() == KEY_DOWN);
	assert(sdl3_controller_driving_flags() == 0);
	assert(sdl3_controller_camera_flags() == 0);
	assert(sdl3_input_driving_mode() == SDL3_DRIVING_KEYBOARD);
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTY, 0);
	controller_axis(device, SDL_GAMEPAD_AXIS_LEFTX, 0);
	sdl3_input_pop_modal();
	controller_button(device, SDL_GAMEPAD_BUTTON_START, true);
	assert(sdl3_controller_take_menu_request());
	assert(!sdl3_controller_take_menu_request());
	assert(kb_read_char() == 0);
	controller_button(device, SDL_GAMEPAD_BUTTON_START, false);
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTX, 0);
	sdl3_input_set_gameplay_active(false);
}

static void test_controller_replay(SDL_Joystick *device)
{
	sdl3_input_set_gameplay_active(true);
	sdl3_input_set_replay_active(true);
	sdl3_input_set_driving_mode(SDL3_DRIVING_KEYBOARD);
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTX, TEST_CONTROLLER_AXIS_MAX);
	assert(kb_read_char() == KEY_RIGHT);
	assert(sdl3_input_driving_mode() == SDL3_DRIVING_KEYBOARD);
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTX, 0);
	static const struct {
		SDL_GamepadButton button;
		legacy_u16 key;
		legacy_s16 scan;
	} replay_directions[] = {
		{SDL_GAMEPAD_BUTTON_DPAD_UP, KEY_UP, TEST_CONTROLLER_SCAN_UP},
		{SDL_GAMEPAD_BUTTON_DPAD_DOWN, KEY_DOWN, TEST_CONTROLLER_SCAN_DOWN},
		{SDL_GAMEPAD_BUTTON_DPAD_LEFT, KEY_LEFT, TEST_CONTROLLER_SCAN_LEFT},
		{SDL_GAMEPAD_BUTTON_DPAD_RIGHT, KEY_RIGHT, TEST_CONTROLLER_SCAN_RIGHT}};
	for (legacy_u32 direction = 0; direction < SDL_arraysize(replay_directions); direction++) {
		controller_button(device, replay_directions[direction].button, true);
		assert(kb_read_char() == replay_directions[direction].key);
		assert(kb_read_char() == 0);
		assert(kb_get_key_state(replay_directions[direction].scan) == 1);
		assert(kb_get_key_state(TEST_CONTROLLER_SCAN_Q) == 0);
		assert(kb_get_key_state(TEST_CONTROLLER_SCAN_T) == 0);
		assert(sdl3_controller_driving_flags() == 0);
		SDL_Delay(TEST_CONTROLLER_MENU_REPEAT_WAIT_MS);
		assert(kb_read_char() == replay_directions[direction].key);
		assert(kb_read_char() == 0);
		controller_button(device, replay_directions[direction].button, false);
		assert(kb_get_key_state(replay_directions[direction].scan) == 0);
		assert(kb_read_char() == 0);
	}
	controller_axis(device, SDL_GAMEPAD_AXIS_LEFTX, TEST_CONTROLLER_AXIS_MIN);
	assert((sdl3_controller_camera_flags() & INPUT_STEER_LEFT_FLAG) != 0);
	controller_axis(device, SDL_GAMEPAD_AXIS_LEFTX, 0);
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTX, 0);
	controller_button(device, SDL_GAMEPAD_BUTTON_START, true);
	assert(sdl3_controller_take_menu_request());
	assert(kb_read_char() == 0);
	controller_button(device, SDL_GAMEPAD_BUTTON_START, false);
	/* Nested dialogs keep replay navigation and suppress camera rotation. */
	sdl3_input_push_modal();
	sdl3_input_push_modal();
	sdl3_input_pop_modal();
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTX, TEST_CONTROLLER_AXIS_MIN);
	controller_axis(device, SDL_GAMEPAD_AXIS_LEFTX, TEST_CONTROLLER_AXIS_MAX);
	assert(kb_read_char() == KEY_LEFT);
	assert(sdl3_controller_camera_flags() == 0);
	assert(sdl3_controller_driving_flags() == 0);
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTX, 0);
	controller_axis(device, SDL_GAMEPAD_AXIS_LEFTX, 0);
	sdl3_input_pop_modal();
	sdl3_input_set_replay_active(false);
	assert(sdl3_controller_driving_flags() == 0);
	sdl3_input_set_gameplay_active(false);
}

static void test_controller_stick_confirm(SDL_Joystick *device)
{
	static const struct {
		legacy_u8 gameplay;
		legacy_u8 replay;
		legacy_u8 modal;
	} contexts[] = {
		{false, false, false}, {true, true, false}, {true, false, true}, {true, true, true}};
	for (legacy_u32 context = 0; context < SDL_arraysize(contexts); context++) {
		sdl3_input_set_gameplay_active(contexts[context].gameplay);
		sdl3_input_set_replay_active(contexts[context].replay);
		if (contexts[context].modal) {
			sdl3_input_push_modal();
		}
		controller_button(device, SDL_GAMEPAD_BUTTON_RIGHT_STICK, true);
		assert(kb_read_char() == KEY_ENTER);
		assert(kb_get_key_state(TEST_CONTROLLER_SCAN_ENTER) == 1);
		assert(kb_get_key_state(TEST_CONTROLLER_SCAN_SPACE) == 0);
		assert(kb_read_char() == 0);
		controller_button(device, SDL_GAMEPAD_BUTTON_RIGHT_STICK, false);
		assert(kb_get_key_state(TEST_CONTROLLER_SCAN_ENTER) == 0);
		controller_button(device, SDL_GAMEPAD_BUTTON_LEFT_STICK, true);
		assert(kb_read_char() == 0);
		assert(kb_get_key_state(TEST_CONTROLLER_SCAN_ENTER) == 0);
		controller_button(device, SDL_GAMEPAD_BUTTON_LEFT_STICK, false);
		if (contexts[context].modal) {
			sdl3_input_pop_modal();
		}
	}
	/* A click cannot skip truck unloading or shift gear during a race. */
	sdl3_input_set_replay_active(false);
	sdl3_input_set_gameplay_active(true);
	controller_button(device, SDL_GAMEPAD_BUTTON_RIGHT_STICK, true);
	assert(kb_read_char() == 0);
	assert(kb_get_key_state(TEST_CONTROLLER_SCAN_ENTER) == 0);
	assert(sdl3_controller_driving_flags() == 0);
	assert(!sdl3_controller_take_race_start_request());
	controller_button(device, SDL_GAMEPAD_BUTTON_RIGHT_STICK, false);
	sdl3_input_set_gameplay_active(false);
}

static void test_controller_dpad_context(SDL_Joystick *device)
{
	static const enum SDL3_DRIVING_INPUT modes[] = {SDL3_DRIVING_KEYBOARD, SDL3_DRIVING_JOYSTICK,
													SDL3_DRIVING_MOUSE, SDL3_DRIVING_CONTROLLER};
	static const struct {
		SDL_GamepadButton button;
		legacy_u16 menu_key;
		legacy_s16 menu_scan;
		legacy_u16 gameplay_key;
		legacy_s16 gameplay_scan;
	} directions[] = {{SDL_GAMEPAD_BUTTON_DPAD_LEFT, KEY_LEFT, TEST_CONTROLLER_SCAN_LEFT, 'q',
					   TEST_CONTROLLER_SCAN_Q},
					  {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, KEY_RIGHT, TEST_CONTROLLER_SCAN_RIGHT, 0,
					   TEST_CONTROLLER_SCAN_T}};
	/* Every selected racing device still accepts menu navigation from the D-pad. */
	sdl3_input_set_gameplay_active(false);
	for (legacy_u32 mode = 0; mode < SDL_arraysize(modes); mode++) {
		sdl3_input_set_driving_mode(modes[mode]);
		for (legacy_u32 direction = 0; direction < SDL_arraysize(directions); direction++) {
			controller_button(device, directions[direction].button, true);
			assert(kb_read_char() == directions[direction].menu_key);
			assert(kb_get_key_state(directions[direction].menu_scan) == 1);
			assert(kb_get_key_state(directions[direction].gameplay_scan) == 0);
			assert(sdl3_input_driving_mode() == modes[mode]);
			assert(kb_read_char() == 0);
			controller_button(device, directions[direction].button, false);
			assert(kb_get_key_state(directions[direction].menu_scan) == 0);
		}
	}
	/* Replay uses arrows; racing keeps Left Q and leaves Right unassigned. */
	sdl3_input_set_gameplay_active(true);
	for (legacy_u32 replay = 0; replay <= 1U; replay++) {
		sdl3_input_set_replay_active((legacy_u8)replay);
		for (legacy_u32 direction = 0; direction < SDL_arraysize(directions); direction++) {
			controller_button(device, directions[direction].button, true);
			legacy_u8 gameplay_pressed = !replay && directions[direction].gameplay_key != 0;
			assert(kb_read_char() ==
				   (replay ? directions[direction].menu_key : directions[direction].gameplay_key));
			assert(kb_get_key_state(directions[direction].gameplay_scan) == gameplay_pressed);
			assert(kb_get_key_state(directions[direction].menu_scan) == (legacy_s16)replay);
			/* A held direction changes meaning throughout a nested dialog. */
			sdl3_input_push_modal();
			sdl3_input_push_modal();
			assert(kb_read_char() == directions[direction].menu_key);
			assert(kb_read_char() == 0);
			assert(kb_get_key_state(directions[direction].gameplay_scan) == 0);
			assert(kb_get_key_state(directions[direction].menu_scan) == 1);
			sdl3_input_pop_modal();
			controller_button(device, directions[direction].button, false);
			assert(kb_get_key_state(directions[direction].menu_scan) == 0);
			controller_button(device, directions[direction].button, true);
			assert(kb_read_char() == directions[direction].menu_key);
			assert(kb_get_key_state(directions[direction].menu_scan) == 1);
			assert(kb_get_key_state(directions[direction].gameplay_scan) == 0);
			sdl3_input_pop_modal();
			assert(kb_get_key_state(directions[direction].menu_scan) == (legacy_s16)replay);
			assert(kb_get_key_state(directions[direction].gameplay_scan) == gameplay_pressed);
			assert(kb_read_char() == (replay ? directions[direction].menu_key : 0));
			controller_button(device, directions[direction].button, false);
			assert(kb_get_key_state(directions[direction].gameplay_scan) == 0);
			assert(kb_read_char() == 0);
		}
	}
	sdl3_input_set_replay_active(false);
	sdl3_input_set_gameplay_active(false);
}

static void test_controller_dpad_repeat(SDL_Joystick *device)
{
	static const struct {
		SDL_GamepadButton button;
		legacy_u16 key;
		legacy_s16 scan;
	} directions[] = {{SDL_GAMEPAD_BUTTON_DPAD_UP, KEY_UP, TEST_CONTROLLER_SCAN_UP},
					  {SDL_GAMEPAD_BUTTON_DPAD_DOWN, KEY_DOWN, TEST_CONTROLLER_SCAN_DOWN},
					  {SDL_GAMEPAD_BUTTON_DPAD_LEFT, KEY_LEFT, TEST_CONTROLLER_SCAN_LEFT},
					  {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, KEY_RIGHT, TEST_CONTROLLER_SCAN_RIGHT}};
	sdl3_input_set_gameplay_active(false);
	for (legacy_u32 direction = 0; direction < SDL_arraysize(directions); direction++) {
		controller_button(device, directions[direction].button, true);
		assert(kb_read_char() == directions[direction].key);
		assert(kb_read_char() == 0);
		assert(kb_get_key_state(directions[direction].scan) == 1);
		SDL_Delay(TEST_CONTROLLER_MENU_REPEAT_WAIT_MS);
		assert(kb_read_char() == directions[direction].key);
		assert(kb_read_char() == 0);
		SDL_Delay(TEST_CONTROLLER_MENU_REPEAT_INTERVAL_WAIT_MS);
		assert(kb_read_char() == directions[direction].key);
		assert(kb_read_char() == 0);
		controller_button(device, directions[direction].button, false);
		assert(kb_get_key_state(directions[direction].scan) == 0);
		SDL_Delay(TEST_CONTROLLER_MENU_REPEAT_INTERVAL_WAIT_MS);
		assert(kb_read_char() == 0);
	}
	/* A complete tap queues one direction even when it ends before polling. */
	assert(SDL_SetJoystickVirtualButton(device, SDL_GAMEPAD_BUTTON_DPAD_DOWN, true));
	SDL_UpdateJoysticks();
	assert(SDL_SetJoystickVirtualButton(device, SDL_GAMEPAD_BUTTON_DPAD_DOWN, false));
	SDL_UpdateJoysticks();
	assert(kb_read_char() == KEY_DOWN);
	assert(kb_read_char() == 0);
	SDL_Delay(TEST_CONTROLLER_MENU_REPEAT_WAIT_MS);
	assert(kb_read_char() == 0);
	/* Changing the effective direction queues it once and restarts the delay. */
	controller_button(device, SDL_GAMEPAD_BUTTON_DPAD_UP, true);
	assert(kb_read_char() == KEY_UP);
	controller_button(device, SDL_GAMEPAD_BUTTON_DPAD_DOWN, true);
	assert(kb_read_char() == 0);
	controller_button(device, SDL_GAMEPAD_BUTTON_DPAD_UP, false);
	assert(kb_read_char() == KEY_DOWN);
	assert(kb_read_char() == 0);
	SDL_Delay(TEST_CONTROLLER_MENU_REPEAT_INTERVAL_WAIT_MS);
	assert(kb_read_char() == 0);
	SDL_Delay(TEST_CONTROLLER_MENU_REPEAT_WAIT_MS);
	assert(kb_read_char() == KEY_DOWN);
	controller_button(device, SDL_GAMEPAD_BUTTON_DPAD_DOWN, false);
	assert(kb_read_char() == 0);
}

static void test_controller_dpad_repeat_context(SDL_Joystick *device)
{
	sdl3_input_set_gameplay_active(true);
	controller_button(device, SDL_GAMEPAD_BUTTON_DPAD_UP, true);
	assert(kb_read_char() == 0);
	assert((sdl3_controller_driving_flags() & INPUT_SHIFT_UP_FLAG) != 0);
	sdl3_input_set_replay_active(true);
	assert(kb_read_char() == KEY_UP);
	assert(kb_read_char() == 0);
	assert(kb_get_key_state(TEST_CONTROLLER_SCAN_UP) == 1);
	assert(sdl3_controller_driving_flags() == 0);
	SDL_Delay(TEST_CONTROLLER_MENU_REPEAT_WAIT_MS);
	assert(kb_read_char() == KEY_UP);
	/* A file selector nested inside replay accepts and repeats the same hold. */
	sdl3_input_push_modal();
	assert(kb_read_char() == KEY_UP);
	assert(kb_read_char() == 0);
	SDL_Delay(TEST_CONTROLLER_MENU_REPEAT_WAIT_MS);
	assert(kb_read_char() == KEY_UP);
	sdl3_input_pop_modal();
	assert(kb_read_char() == KEY_UP);
	assert(kb_read_char() == 0);
	assert(kb_get_key_state(TEST_CONTROLLER_SCAN_UP) == 1);
	SDL_Delay(TEST_CONTROLLER_MENU_REPEAT_INTERVAL_WAIT_MS);
	assert(kb_read_char() == 0);
	/* Returning to driving removes menu repeats and restores the held shift. */
	sdl3_input_set_replay_active(false);
	assert(kb_get_key_state(TEST_CONTROLLER_SCAN_UP) == 0);
	assert((sdl3_controller_driving_flags() & INPUT_SHIFT_UP_FLAG) != 0);
	SDL_Delay(TEST_CONTROLLER_MENU_REPEAT_WAIT_MS);
	assert(kb_read_char() == 0);
	controller_button(device, SDL_GAMEPAD_BUTTON_DPAD_UP, false);
	/* The held live Left shortcut does not become repeated key presses. */
	controller_button(device, SDL_GAMEPAD_BUTTON_DPAD_LEFT, true);
	assert(kb_read_char() == 'q');
	SDL_Delay(TEST_CONTROLLER_MENU_REPEAT_WAIT_MS);
	assert(kb_read_char() == 0);
	assert(kb_get_key_state(TEST_CONTROLLER_SCAN_Q) == 1);
	controller_button(device, SDL_GAMEPAD_BUTTON_DPAD_LEFT, false);
	/* Right is inert while driving, then becomes a repeating replay arrow. */
	controller_button(device, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, true);
	assert(kb_read_char() == 0);
	assert(kb_get_key_state(TEST_CONTROLLER_SCAN_T) == 0);
	assert(kb_get_key_state(TEST_CONTROLLER_SCAN_RIGHT) == 0);
	SDL_Delay(TEST_CONTROLLER_MENU_REPEAT_WAIT_MS);
	assert(kb_read_char() == 0);
	sdl3_input_set_replay_active(true);
	assert(kb_read_char() == KEY_RIGHT);
	assert(kb_read_char() == 0);
	SDL_Delay(TEST_CONTROLLER_MENU_REPEAT_WAIT_MS);
	assert(kb_read_char() == KEY_RIGHT);
	assert(kb_get_key_state(TEST_CONTROLLER_SCAN_RIGHT) == 1);
	assert(kb_get_key_state(TEST_CONTROLLER_SCAN_T) == 0);
	controller_button(device, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, false);
	sdl3_input_set_replay_active(false);
	sdl3_input_set_gameplay_active(false);
}

static void test_controller_race_start(SDL_Joystick *device)
{
	static const enum SDL3_DRIVING_INPUT modes[] = {SDL3_DRIVING_KEYBOARD, SDL3_DRIVING_JOYSTICK,
													SDL3_DRIVING_MOUSE, SDL3_DRIVING_CONTROLLER};
	sdl3_input_set_gameplay_active(true);
	for (legacy_u32 mode = 0; mode < SDL_arraysize(modes); mode++) {
		sdl3_input_set_driving_mode(modes[mode]);
		controller_axis(device, SDL_GAMEPAD_AXIS_LEFTX, TEST_CONTROLLER_AXIS_MAX);
		assert(!sdl3_controller_take_race_start_request());
		controller_axis(device, SDL_GAMEPAD_AXIS_LEFTX, 0);
		controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTX, TEST_CONTROLLER_STEERING_NOISE);
		assert(!sdl3_controller_take_race_start_request());
		controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTX, 0);
		controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTX, TEST_CONTROLLER_PARTIAL_TURN);
		assert(sdl3_controller_take_race_start_request());
		assert(!sdl3_controller_take_race_start_request());
		controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTX, TEST_CONTROLLER_RECENTER_TURN);
		assert(!sdl3_controller_take_race_start_request());
		controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTX, 0);
		assert(!sdl3_controller_take_race_start_request());
		controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTY, TEST_CONTROLLER_PARTIAL_TURN);
		assert(!sdl3_controller_take_race_start_request());
		controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTY, TEST_CONTROLLER_PEDAL_THRESHOLD);
		assert(sdl3_controller_take_race_start_request());
		assert(!sdl3_controller_take_race_start_request());
		controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTY, -TEST_CONTROLLER_PEDAL_THRESHOLD);
		assert(!sdl3_controller_take_race_start_request());
		controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTY, TEST_CONTROLLER_AXIS_MIN);
		assert(sdl3_controller_take_race_start_request());
		controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTY, 0);
		assert(!sdl3_controller_take_race_start_request());
	}
	/* Preserve a complete pedal tap between game polls. */
	assert(SDL_SetJoystickVirtualAxis(device, SDL_GAMEPAD_AXIS_RIGHTY, TEST_CONTROLLER_AXIS_MIN));
	SDL_UpdateJoysticks();
	assert(SDL_SetJoystickVirtualAxis(device, SDL_GAMEPAD_AXIS_RIGHTY, 0));
	SDL_UpdateJoysticks();
	assert(sdl3_controller_take_race_start_request());
	assert(!sdl3_controller_take_race_start_request());
	/* Entering another input context discards an unconsumed start request. */
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTX, TEST_CONTROLLER_PARTIAL_TURN);
	sdl3_input_push_modal();
	assert(!sdl3_controller_take_race_start_request());
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTX, 0);
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTY, TEST_CONTROLLER_AXIS_MIN);
	assert(kb_read_char() == KEY_UP);
	assert(!sdl3_controller_take_race_start_request());
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTY, 0);
	sdl3_input_pop_modal();
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTX, TEST_CONTROLLER_PARTIAL_TURN);
	sdl3_input_set_replay_active(true);
	assert(!sdl3_controller_take_race_start_request());
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTX, TEST_CONTROLLER_AXIS_MAX);
	assert(kb_read_char() == KEY_RIGHT);
	assert(!sdl3_controller_take_race_start_request());
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTX, 0);
	sdl3_input_set_replay_active(false);
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTX, TEST_CONTROLLER_PARTIAL_TURN);
	sdl3_input_set_gameplay_active(false);
	assert(!sdl3_controller_take_race_start_request());
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTX, 0);
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTY, TEST_CONTROLLER_AXIS_MAX);
	assert(kb_read_char() == KEY_DOWN);
	assert(!sdl3_controller_take_race_start_request());
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTY, 0);
}

static void test_controller(void)
{
	kb_init_interrupt();
	sdl3_input_set_driving_mode(SDL3_DRIVING_KEYBOARD);
	SDL_Joystick *device;
	SDL_JoystickID id = attach_test_controller(&device);
	assert(dos_get_joy_flags() == 0);
	assert(sdl3_joystick_menu_flags() == 0);
	assert(kb_read_char() == 0);
	test_controller_shortcuts(device);
	test_controller_triggers(device);
	test_controller_navigation(device);
	test_controller_driving(device);
	test_controller_replay(device);
	test_controller_stick_confirm(device);
	test_controller_dpad_context(device);
	test_controller_dpad_repeat(device);
	test_controller_dpad_repeat_context(device);
	test_controller_race_start(device);
	sdl3_input_set_gameplay_active(true);
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTX, TEST_CONTROLLER_PARTIAL_TURN);
	/* Focus loss clears shortcuts while preserving physical key ownership. */
	controller_button(device, SDL_GAMEPAD_BUTTON_DPAD_LEFT, true);
	assert(kb_read_char() == 'q');
	SDL_Event focus_lost;
	SDL_zero(focus_lost);
	focus_lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
	assert(SDL_PushEvent(&focus_lost));
	sdl3_platform_pump();
	assert(kb_get_key_state(TEST_CONTROLLER_SCAN_Q) == 0);
	assert(!sdl3_controller_take_race_start_request());
	assert(kb_read_char() == 0);
	focus_lost.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
	assert(SDL_PushEvent(&focus_lost));
	sdl3_platform_pump();
	assert(kb_get_key_state(TEST_CONTROLLER_SCAN_Q) == 0);
	controller_button(device, SDL_GAMEPAD_BUTTON_DPAD_LEFT, false);
	controller_button(device, SDL_GAMEPAD_BUTTON_DPAD_LEFT, true);
	assert(kb_read_char() == 'q');
	/* Removal releases buttons and makes keyboard steering available. */
	sdl3_input_set_driving_mode(SDL3_DRIVING_CONTROLLER);
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTY, TEST_CONTROLLER_AXIS_MIN);
	SDL_CloseJoystick(device);
	assert(SDL_DetachVirtualJoystick(id));
	sdl3_platform_pump();
	assert(SDL_GetGamepadFromID(id) == NULL);
	assert(sdl3_controller_driving_flags() == 0);
	assert(sdl3_controller_scaled_x() == 0);
	assert(kb_get_key_state(TEST_CONTROLLER_SCAN_Q) == 0);
	assert(sdl3_input_driving_mode() == SDL3_DRIVING_KEYBOARD);
	assert(!sdl3_controller_take_race_start_request());
	sdl3_input_set_gameplay_active(false);
	id = attach_test_controller(&device);
	controller_axis(device, SDL_GAMEPAD_AXIS_RIGHTX, TEST_CONTROLLER_AXIS_MAX);
	assert(kb_read_char() == KEY_RIGHT);
	SDL_CloseJoystick(device);
	assert(SDL_DetachVirtualJoystick(id));
	sdl3_platform_pump();
}

legacy_int main(void)
{
	SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
	SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
	assert(SDL_Init(SDL_INIT_EVENTS));
	sdl3_batch_mode = 1;
	dos_video_set_mode_13h();
	dos_joystick_set_enabled(1);
	assert(dos_mouse_init(320, 200) == 0);
	sdl3_platform_pump();
	assert(sdl3_video_window() == NULL);
	assert(SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD) == 0);
	dos_joystick_set_enabled(0);
	sdl3_batch_mode = 0;
	test_keyboard();
	test_music_shortcut();
	test_timer();
	test_video_vsync();
	test_video_borderless();
	test_video_and_mouse();
	test_touch_focus_and_background();
	test_touch_shortcut_keys();
	test_high_resolution_video();
	test_dynamic_resolution_video();
	test_completed_video_pages();
	test_video_page_lifetime();
	test_screenshots();
	test_adaptive_frame_timing();
	test_fullscreen_shortcut();
	test_joystick();
	test_controller();
	SDL_Event quit;
	SDL_zero(quit);
	quit.type = SDL_EVENT_QUIT;
	assert(SDL_PushEvent(&quit));
	sdl3_platform_pump();
	assert(quit_cleaned_up);
	sdl3_platform_shutdown();
	puts("SDL3 keyboard, timer, video coordinates, mouse, joystick and controller passed.");
	return 0;
}
