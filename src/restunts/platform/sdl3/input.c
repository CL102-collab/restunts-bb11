#include "sdl3.h"
#include "music.h"
#include "touch.h"
#include "controller_game.h"
#include "../../c/platform.h"
#include "../../c/keyboard.h"
#include "../../c/game_input.h"
#include "../../c/fatal.h"
#include "../../c/hires.h"
#include <string.h>
#ifdef __ANDROID__
#include <jni.h>
#endif

#define KEY_BUFFER_CAPACITY 64U
#define INPUT_POLL_DELAY_MS 1U
#define JOYSTICK_AXIS_COUNT 2
#define JOYSTICK_AXIS_SCALE 32
#define JOYSTICK_AXIS_DEADZONE 16384
#define JOYSTICK_X_AXIS 0U
#define JOYSTICK_Y_AXIS 1U
#define CONTROLLER_STEERING_ACTIVITY_DEADZONE 2048
#define CONTROLLER_STEERING_ACTIVITY_DELTA 1024
#define CONTROLLER_MENU_REPEAT_DELAY_MS 350U
#define CONTROLLER_MENU_REPEAT_INTERVAL_MS 100U
#define CONTROLLER_CAMERA_REPEAT_INTERVAL_MS 50U
#define CONTROLLER_PRIMARY_CONFIRM_KEY ((legacy_u16)KEY_ENTER)
#define CONTROLLER_TRIGGER_CONFIRM_KEY ((legacy_u16)KEY_SPACE)
#define MOUSE_BUTTON_COUNT 3

enum MOUSE_BUTTON_FLAG { MOUSE_LEFT_FLAG = 1, MOUSE_RIGHT_FLAG = 2, MOUSE_MIDDLE_FLAG = 4 };
enum DOS_KB_NAVIGATION_SCANCODE {
	DOS_KB_ENTER_SCANCODE = 28,
	DOS_KB_PAGE_UP_SCANCODE = 73,
	DOS_KB_PAGE_DOWN_SCANCODE = 81
};

static legacy_u8 keys[SDL_SCANCODE_COUNT];
static legacy_u8 consumed_keys[SDL_SCANCODE_COUNT];
static legacy_u16 key_buffer[KEY_BUFFER_CAPACITY];
static legacy_u32 key_read;
static legacy_u32 key_count;
static legacy_s16 mouse_x;
static legacy_s16 mouse_y;
static legacy_s16 mouse_buttons;
static legacy_s16 mouse_min_x;
static legacy_s16 mouse_min_y;
static legacy_s16 mouse_max_x = SDL3_SCREEN_WIDTH - 1;
static legacy_s16 mouse_max_y = SDL3_SCREEN_HEIGHT - 1;
static legacy_u8 mouse_available;
struct MOUSE_TRANSITION {
	legacy_s16 buttons;
	legacy_s16 x;
	legacy_s16 y;
};
static struct MOUSE_TRANSITION mouse_transitions[KEY_BUFFER_CAPACITY];
static legacy_u32 mouse_transition_read;
static legacy_u32 mouse_transition_count;
static legacy_u8 input_devices_initialized;
static legacy_u8 joystick_enabled;
static SDL_Joystick *joystick;
static SDL_Gamepad *controller;
static legacy_s16 controller_axes[SDL_GAMEPAD_AXIS_COUNT];
static legacy_u8 controller_buttons[SDL_GAMEPAD_BUTTON_COUNT];
static enum SDL3_DRIVING_INPUT driving_mode = SDL3_DRIVING_KEYBOARD;
static legacy_u8 gameplay_active;
static legacy_u8 replay_active;
static legacy_u16 modal_depth;
static legacy_u8 input_focused = true;
static legacy_u8 controller_menu_requested;
static legacy_u8 controller_race_start_requested;
static legacy_s16 controller_shift_pending;
static legacy_s16 controller_menu_direction;
static legacy_u64 controller_menu_repeat_at;
static legacy_u16 controller_dpad_key;
static legacy_u64 controller_dpad_repeat_at;
static legacy_s16 controller_camera_direction;
static legacy_u64 controller_camera_repeat_at;
static legacy_s16 controller_steering_activity_axis;
static legacy_u8 pumping;
static legacy_u64 last_event_poll;

static const legacy_u8 dos_kb_keymap1[DOS_KB_PRIMARY_KEYMAP_SIZE] = {
	0,	 27,  49,  50,	51,	 52,  53,  54,	55,	 56,  57,  48,	45,	 61,  8,   9,	113, 119, 101,
	114, 116, 121, 117, 105, 111, 112, 91,	93,	 13,  0,   97,	115, 100, 102, 103, 104, 106, 107,
	108, 59,  39,  96,	0,	 92,  122, 120, 99,	 118, 98,  110, 109, 44,  46,  47,	0,	 42,  0,
	32,	 0,	  187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 0,	 0,	  199, 200, 201, 45,  203,
	204, 205, 43,  207, 208, 209, 210, 211, 0,	 0,	  0,   0,	0,	 0,	  0};

static const legacy_u8 dos_kb_keymap2[DOS_KB_PRIMARY_KEYMAP_SIZE] = {
	0,	 27,  33,  64,	35,	 36,  37,  94,	38,	 42,  40,  41,	95, 43, 8,	 143, 81,  87, 69,
	82,	 84,  89,  85,	73,	 79,  80,  123, 125, 13,  0,   65,	83, 68, 70,	 71,  72,  74, 75,
	76,	 58,  34,  126, 0,	 124, 90,  88,	67,	 86,  66,  78,	77, 60, 62,	 63,  0,   0,  0,
	32,	 0,	  212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 0,	0,	199, 200, 201, 45, 203,
	204, 205, 43,  207, 208, 209, 210, 211, 0,	 0,	  0,   0,	0,	0,	0};

static const legacy_u8 dos_kb_keymap3[DOS_KB_PRIMARY_KEYMAP_SIZE] = {
	0,	 27,  49,  50,	51,	 52,  53,  54,	55,	 56,  57,  48,	45, 61, 8,	 143, 81,  87, 69,
	82,	 84,  89,  85,	73,	 79,  80,  91,	93,	 13,  0,   65,	83, 68, 70,	 71,  72,  74, 75,
	76,	 59,  39,  96,	0,	 92,  90,  88,	67,	 86,  66,  78,	77, 44, 46,	 47,  0,   0,  0,
	32,	 0,	  212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 0,	0,	199, 200, 201, 45, 203,
	204, 205, 43,  207, 208, 209, 210, 211, 0,	 0,	  0,   0,	0,	0,	0};

static const legacy_u8 dos_kb_keymap4[DOS_KB_PRIMARY_KEYMAP_SIZE] = {
	0,	 27,  33,  0,	35,	 36,  37,  30,	38,	 42,  40,  41,	31,	 43, 127, 9,   17,	23, 5,
	18,	 20,  25,  21,	9,	 15,  16,  27,	29,	 13,  0,   1,	19,	 4,	 6,	  7,   8,	10, 11,
	12,	 59,  44,  96,	0,	 28,  26,  24,	3,	 22,  2,   14,	178, 60, 62,  63,  0,	0,	0,
	32,	 0,	  222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 0,	 0,	 199, 200, 201, 45, 203,
	204, 205, 43,  207, 208, 209, 210, 211, 0,	 0,	  0,   0,	0,	 0,	 0};

static const legacy_u8 dos_kb_keymap5[DOS_KB_ALT_KEYMAP_SIZE] = {
	0,	 27,  33,  64,	35,	 36,  37,  94,	38,	 42,  40,  41,	95,	 43,  8,   143, 144, 145, 146,
	147, 148, 149, 150, 151, 152, 153, 123, 125, 13,  0,   158, 159, 160, 161, 162, 163, 164, 165,
	166, 58,  34,  126, 0,	 124, 172, 173, 174, 175, 176, 177, 178, 60,  62,  63,	0,	 0,	  0,
	32,	 0,	  248, 249, 250, 251, 252, 253, 254, 255, 128, 129, 0,	 0,	  199, 200, 201, 45,  203,
	204, 205, 43,  207, 208, 209, 210, 211, 0,	 0,	  0,   0,	0,	 0,	  0,   0};

static const legacy_u32 scancodes[] = {SDL_SCANCODE_UNKNOWN,
									   SDL_SCANCODE_ESCAPE,
									   SDL_SCANCODE_1,
									   SDL_SCANCODE_2,
									   SDL_SCANCODE_3,
									   SDL_SCANCODE_4,
									   SDL_SCANCODE_5,
									   SDL_SCANCODE_6,
									   SDL_SCANCODE_7,
									   SDL_SCANCODE_8,
									   SDL_SCANCODE_9,
									   SDL_SCANCODE_0,
									   SDL_SCANCODE_MINUS,
									   SDL_SCANCODE_EQUALS,
									   SDL_SCANCODE_BACKSPACE,
									   SDL_SCANCODE_TAB,
									   SDL_SCANCODE_Q,
									   SDL_SCANCODE_W,
									   SDL_SCANCODE_E,
									   SDL_SCANCODE_R,
									   SDL_SCANCODE_T,
									   SDL_SCANCODE_Y,
									   SDL_SCANCODE_U,
									   SDL_SCANCODE_I,
									   SDL_SCANCODE_O,
									   SDL_SCANCODE_P,
									   SDL_SCANCODE_LEFTBRACKET,
									   SDL_SCANCODE_RIGHTBRACKET,
									   SDL_SCANCODE_RETURN,
									   SDL_SCANCODE_LCTRL,
									   SDL_SCANCODE_A,
									   SDL_SCANCODE_S,
									   SDL_SCANCODE_D,
									   SDL_SCANCODE_F,
									   SDL_SCANCODE_G,
									   SDL_SCANCODE_H,
									   SDL_SCANCODE_J,
									   SDL_SCANCODE_K,
									   SDL_SCANCODE_L,
									   SDL_SCANCODE_SEMICOLON,
									   SDL_SCANCODE_APOSTROPHE,
									   SDL_SCANCODE_GRAVE,
									   SDL_SCANCODE_LSHIFT,
									   SDL_SCANCODE_BACKSLASH,
									   SDL_SCANCODE_Z,
									   SDL_SCANCODE_X,
									   SDL_SCANCODE_C,
									   SDL_SCANCODE_V,
									   SDL_SCANCODE_B,
									   SDL_SCANCODE_N,
									   SDL_SCANCODE_M,
									   SDL_SCANCODE_COMMA,
									   SDL_SCANCODE_PERIOD,
									   SDL_SCANCODE_SLASH,
									   SDL_SCANCODE_RSHIFT,
									   SDL_SCANCODE_KP_MULTIPLY,
									   SDL_SCANCODE_LALT,
									   SDL_SCANCODE_SPACE,
									   SDL_SCANCODE_CAPSLOCK,
									   SDL_SCANCODE_F1,
									   SDL_SCANCODE_F2,
									   SDL_SCANCODE_F3,
									   SDL_SCANCODE_F4,
									   SDL_SCANCODE_F5,
									   SDL_SCANCODE_F6,
									   SDL_SCANCODE_F7,
									   SDL_SCANCODE_F8,
									   SDL_SCANCODE_F9,
									   SDL_SCANCODE_F10,
									   SDL_SCANCODE_NUMLOCKCLEAR,
									   SDL_SCANCODE_SCROLLLOCK,
									   SDL_SCANCODE_HOME,
									   SDL_SCANCODE_UP,
									   SDL_SCANCODE_PAGEUP,
									   SDL_SCANCODE_KP_MINUS,
									   SDL_SCANCODE_LEFT,
									   SDL_SCANCODE_KP_5,
									   SDL_SCANCODE_RIGHT,
									   SDL_SCANCODE_KP_PLUS,
									   SDL_SCANCODE_END,
									   SDL_SCANCODE_DOWN,
									   SDL_SCANCODE_PAGEDOWN,
									   SDL_SCANCODE_INSERT,
									   SDL_SCANCODE_DELETE,
									   SDL_SCANCODE_UNKNOWN,
									   SDL_SCANCODE_UNKNOWN,
									   SDL_SCANCODE_NONUSBACKSLASH,
									   SDL_SCANCODE_F11,
									   SDL_SCANCODE_F12};

static legacy_u32 legacy_scancode(legacy_u32 code)
{
	switch (code) {
		case SDL_SCANCODE_RCTRL:
			return DOS_KB_CONTROL_SCANCODE;
		case SDL_SCANCODE_RALT:
			return DOS_KB_ALT_SCANCODE;
		case SDL_SCANCODE_KP_ENTER:
			return DOS_KB_ENTER_SCANCODE;
		case SDL_SCANCODE_KP_7:
			return (legacy_u16)KEY_HOME >> LEGACY_BYTE_BITS;
		case SDL_SCANCODE_KP_8:
			return (legacy_u16)KEY_UP >> LEGACY_BYTE_BITS;
		case SDL_SCANCODE_KP_9:
			return DOS_KB_PAGE_UP_SCANCODE;
		case SDL_SCANCODE_KP_4:
			return (legacy_u16)KEY_LEFT >> LEGACY_BYTE_BITS;
		case SDL_SCANCODE_KP_6:
			return (legacy_u16)KEY_RIGHT >> LEGACY_BYTE_BITS;
		case SDL_SCANCODE_KP_1:
			return (legacy_u16)KEY_END >> LEGACY_BYTE_BITS;
		case SDL_SCANCODE_KP_2:
			return (legacy_u16)KEY_DOWN >> LEGACY_BYTE_BITS;
		case SDL_SCANCODE_KP_3:
			return DOS_KB_PAGE_DOWN_SCANCODE;
		case SDL_SCANCODE_KP_0:
			return (legacy_u16)KEY_INSERT >> LEGACY_BYTE_BITS;
		case SDL_SCANCODE_KP_PERIOD:
			return (legacy_u16)KEY_DELETE >> LEGACY_BYTE_BITS;
		default:
			break;
	}
	for (legacy_u32 index = 1; index < SDL_arraysize(scancodes); index++) {
		if (scancodes[index] == code) {
			return index;
		}
	}
	return 0;
}

static legacy_u8 input_steering_scancode(SDL_Scancode code)
{
	return code == SDL_SCANCODE_LEFT || code == SDL_SCANCODE_RIGHT || code == SDL_SCANCODE_HOME ||
		   code == SDL_SCANCODE_PAGEUP || code == SDL_SCANCODE_END ||
		   code == SDL_SCANCODE_PAGEDOWN || code == SDL_SCANCODE_KP_4 ||
		   code == SDL_SCANCODE_KP_6 || code == SDL_SCANCODE_KP_7 || code == SDL_SCANCODE_KP_9 ||
		   code == SDL_SCANCODE_KP_1 || code == SDL_SCANCODE_KP_3;
}

static void input_key(const SDL_KeyboardEvent *event)
{
	if ((legacy_u32)event->scancode >= SDL_SCANCODE_COUNT) {
		return;
	}
#ifdef __ANDROID__
	if (event->scancode == SDL_SCANCODE_AC_BACK && event->down && !event->repeat) {
		sdl3_input_queue_key(KEY_ESCAPE);
		return;
	}
	if (SDL_TextInputActive(sdl3_video_window()) && event->key >= KEY_SPACE &&
		event->key <= LEGACY_S8_MAX) {
		return;
	}
#endif
	if (event->down && !event->repeat && gameplay_active && !replay_active && modal_depth == 0 &&
		input_steering_scancode(event->scancode)) {
		driving_mode = SDL3_DRIVING_KEYBOARD;
	}
	legacy_u8 was_pressed = keys[event->scancode];
	keys[event->scancode] = event->down;
	if (!event->down) {
		consumed_keys[event->scancode] = false;
		return;
	}
	if (!was_pressed && !event->repeat) {
		consumed_keys[event->scancode] = false;
	}
	if (consumed_keys[event->scancode]) {
		return;
	}
	if ((event->scancode == SDL_SCANCODE_RETURN || event->scancode == SDL_SCANCODE_KP_ENTER) &&
		(event->mod & SDL_KMOD_ALT) != 0) {
		/* Keep Enter consumed until release, even if Alt is released first. */
		consumed_keys[event->scancode] = true;
		if (!was_pressed && !event->repeat) {
			sdl3_video_toggle_fullscreen();
		}
		return;
	}
	if (event->scancode == SDL_SCANCODE_F10 && !was_pressed && !event->repeat &&
		(event->mod & SDL_KMOD_SHIFT) != 0 &&
		(event->mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) == 0 && sdl3_music_toggle()) {
		/* Do not let the accepted shortcut dismiss the intro or a music screen. */
		consumed_keys[event->scancode] = true;
		return;
	}
	if (event->scancode == SDL_SCANCODE_F12 && (event->mod & SDL_KMOD_CTRL) != 0 &&
		(event->mod & (SDL_KMOD_SHIFT | SDL_KMOD_ALT | SDL_KMOD_GUI)) == 0) {
		/* Keep F12 consumed if Ctrl is released before the shortcut key. */
		consumed_keys[event->scancode] = true;
		if (!was_pressed && !event->repeat) {
			sdl3_video_screenshot();
		}
		return;
	}
	legacy_u32 scan = legacy_scancode(event->scancode);
	if (scan == 0) {
		return;
	}
	legacy_u16 value;
	if (scan == DOS_KB_F11_SCANCODE || scan == DOS_KB_F12_SCANCODE) {
		if (was_pressed || event->repeat ||
			(event->mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI))) {
			return;
		}
		value = (legacy_u16)(scan == DOS_KB_F11_SCANCODE	 ? KEY_F11
							 : (event->mod & SDL_KMOD_SHIFT) ? KEY_SHIFT_F12
															 : KEY_F12);
	} else {
		if ((event->mod & SDL_KMOD_ALT) != 0) {
			value = dos_kb_keymap5[scan];
		} else if ((event->mod & SDL_KMOD_CTRL) != 0) {
			value = dos_kb_keymap4[scan];
		} else if ((event->mod & SDL_KMOD_SHIFT) != 0) {
			value = dos_kb_keymap2[scan];
		} else if ((event->mod & SDL_KMOD_CAPS) != 0) {
			value = dos_kb_keymap3[scan];
		} else {
			value = dos_kb_keymap1[scan];
		}
		if ((value & DOS_KB_EXTENDED_KEY_FLAG) != 0) {
			if (value >= DOS_KB_EXTENDED_KEY_NORMALIZE_MIN) {
				value &= DOS_KB_SCANCODE_MASK;
			}
			value <<= LEGACY_BYTE_BITS;
		}
	}
	if (value == 0) {
		return;
	}
	sdl3_input_queue_key(value);
}

void sdl3_input_queue_key(legacy_u16 value)
{
	if (key_count == KEY_BUFFER_CAPACITY) {
		key_read = (key_read + 1U) % KEY_BUFFER_CAPACITY;
		key_count--;
	}
	key_buffer[(key_read + key_count++) % KEY_BUFFER_CAPACITY] = value;
}

static legacy_s16 clamp_mouse(legacy_f32 coordinate, legacy_s16 minimum, legacy_s16 maximum)
{
	if (coordinate < minimum) {
		return minimum;
	}
	if (coordinate > maximum) {
		return maximum;
	}
	return (legacy_s16)coordinate;
}

static void input_mouse_position(legacy_f32 window_x, legacy_f32 window_y)
{
	legacy_f32 x;
	legacy_f32 y;
	sdl3_video_window_to_game(window_x, window_y, &x, &y);
	mouse_x = clamp_mouse(x, mouse_min_x, mouse_max_x);
	mouse_y = clamp_mouse(y, mouse_min_y, mouse_max_y);
}

static void input_mouse_transition(void)
{
	if (mouse_transition_count == KEY_BUFFER_CAPACITY) {
		mouse_transition_read = (mouse_transition_read + 1U) % KEY_BUFFER_CAPACITY;
		mouse_transition_count--;
	}
	struct MOUSE_TRANSITION *transition =
		&mouse_transitions[(mouse_transition_read + mouse_transition_count++) %
						   KEY_BUFFER_CAPACITY];
	transition->buttons = mouse_buttons;
	transition->x = mouse_x;
	transition->y = mouse_y;
}

void sdl3_input_touch_mouse(legacy_f32 x, legacy_f32 y, legacy_u8 down)
{
	legacy_u8 was_down = (mouse_buttons & MOUSE_LEFT_FLAG) != 0;
	input_mouse_position(x, y);
	if (down) {
		mouse_buttons |= MOUSE_LEFT_FLAG;
	} else {
		mouse_buttons &= ~MOUSE_LEFT_FLAG;
	}
	if (was_down != down) {
		input_mouse_transition();
	}
}

static legacy_u8 controller_menu_active(void)
{
	return !gameplay_active || modal_depth != 0;
}

static legacy_u8 controller_navigation_active(void)
{
	return controller_menu_active() || replay_active;
}

static void controller_reset_state(void)
{
	memset(controller_axes, 0, sizeof(controller_axes));
	memset(controller_buttons, 0, sizeof(controller_buttons));
	controller_menu_requested = false;
	controller_race_start_requested = false;
	controller_shift_pending = INPUT_NONE;
	controller_menu_direction = INPUT_NONE;
	controller_menu_repeat_at = 0;
	controller_dpad_key = INPUT_NONE;
	controller_dpad_repeat_at = 0;
	controller_camera_direction = INPUT_NONE;
	controller_camera_repeat_at = 0;
	controller_steering_activity_axis = 0;
}

static void open_joystick(void)
{
	if (sdl3_batch_mode || joystick != NULL || !input_devices_initialized) {
		return;
	}
	/* Match the output parameter type required by SDL. */
	legacy_int count = 0;
	SDL_JoystickID *ids = SDL_GetJoysticks(&count);
	for (legacy_s32 index = 0; index < count && joystick == NULL; index++) {
		/* A mapped gamepad uses its standardized right stick, never raw axes. */
		if (!SDL_IsGamepad(ids[index])) {
			joystick = SDL_OpenJoystick(ids[index]);
		}
	}
	SDL_free(ids);
}

static void open_controller(void)
{
	if (sdl3_batch_mode || controller != NULL || !input_devices_initialized) {
		return;
	}
	legacy_int count = 0;
	SDL_JoystickID *ids = SDL_GetGamepads(&count);
	for (legacy_s32 index = 0; index < count && controller == NULL; index++) {
		controller = SDL_OpenGamepad(ids[index]);
	}
	SDL_free(ids);
	if (controller != NULL) {
		controller_reset_state();
		for (legacy_u32 axis = 0; axis < SDL_GAMEPAD_AXIS_COUNT; axis++) {
			controller_axes[axis] = SDL_GetGamepadAxis(controller, (SDL_GamepadAxis)axis);
		}
		for (legacy_u32 button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; button++) {
			controller_buttons[button] =
				SDL_GetGamepadButton(controller, (SDL_GamepadButton)button);
		}
		controller_steering_activity_axis = controller_axes[SDL_GAMEPAD_AXIS_RIGHTX];
	}
}

static void input_open_devices(void)
{
	if (sdl3_batch_mode || input_devices_initialized) {
		return;
	}
	input_devices_initialized = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
	if (!input_devices_initialized) {
		return;
	}
	open_controller();
	open_joystick();
}

static legacy_u16 controller_button_key(SDL_GamepadButton button)
{
	switch (button) {
		case SDL_GAMEPAD_BUTTON_SOUTH:
			return CONTROLLER_PRIMARY_CONFIRM_KEY;
		case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
			return controller_navigation_active() ? (legacy_u16)KEY_ENTER : 0;
		case SDL_GAMEPAD_BUTTON_EAST:
		case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
		case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
			return KEY_ESCAPE;
		case SDL_GAMEPAD_BUTTON_WEST:
			return 'd';
		case SDL_GAMEPAD_BUTTON_NORTH:
			return 't';
		case SDL_GAMEPAD_BUTTON_BACK:
			return 'c';
		case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
			return controller_navigation_active() ? (legacy_u16)KEY_LEFT : 'q';
		case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
			return controller_navigation_active() ? (legacy_u16)KEY_RIGHT : 0;
		case SDL_GAMEPAD_BUTTON_DPAD_UP:
			return controller_navigation_active() ? (legacy_u16)KEY_UP : 0;
		case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
			return controller_navigation_active() ? (legacy_u16)KEY_DOWN : 0;
		default:
			return 0;
	}
}

static void controller_update_dpad_navigation(void);

static void input_controller_button(const SDL_GamepadButtonEvent *event)
{
	if (!input_focused || controller == NULL || event->which != SDL_GetGamepadID(controller) ||
		event->button >= SDL_GAMEPAD_BUTTON_COUNT) {
		return;
	}
	legacy_u8 was_pressed = controller_buttons[event->button];
	controller_buttons[event->button] = event->down;
	if (controller_navigation_active() && (event->button == SDL_GAMEPAD_BUTTON_DPAD_UP ||
										   event->button == SDL_GAMEPAD_BUTTON_DPAD_DOWN ||
										   event->button == SDL_GAMEPAD_BUTTON_DPAD_LEFT ||
										   event->button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) {
		/* One path queues both the initial menu direction and its timed repeats. */
		controller_update_dpad_navigation();
		return;
	}
	if (!event->down || was_pressed) {
		return;
	}
	if (event->button == SDL_GAMEPAD_BUTTON_START) {
		if (gameplay_active && modal_depth == 0) {
			controller_menu_requested = true;
		} else {
			sdl3_input_queue_key(KEY_ESCAPE);
		}
		return;
	}
	if (!controller_navigation_active()) {
		if (event->button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
			controller_shift_pending |= INPUT_SHIFT_UP_FLAG;
		} else if (event->button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
			controller_shift_pending |= INPUT_SHIFT_DOWN_FLAG;
		}
	}
	legacy_u16 key = controller_button_key((SDL_GamepadButton)event->button);
	if (key != 0) {
		sdl3_input_queue_key(key);
	}
}

static void input_controller_axis(const SDL_GamepadAxisEvent *event)
{
	if (!input_focused || controller == NULL || event->which != SDL_GetGamepadID(controller) ||
		event->axis >= SDL_GAMEPAD_AXIS_COUNT) {
		return;
	}
	legacy_s16 previous = controller_axes[event->axis];
	controller_axes[event->axis] = event->value;
	if (event->axis == SDL_GAMEPAD_AXIS_RIGHTX && gameplay_active && !replay_active &&
		modal_depth == 0) {
		legacy_s32 delta = (legacy_s32)event->value - controller_steering_activity_axis;
		legacy_s32 previous_magnitude = previous < 0 ? -(legacy_s32)previous : previous;
		legacy_s32 magnitude = event->value < 0 ? -(legacy_s32)event->value : event->value;
		legacy_u8 changed_side =
			(event->value < 0 && previous > 0) || (event->value > 0 && previous < 0);
		/* Releasing a deflected stick must not take steering back from the keyboard. */
		if (magnitude < CONTROLLER_STEERING_ACTIVITY_DEADZONE ||
			(!changed_side && magnitude <= previous_magnitude)) {
			controller_steering_activity_axis = event->value;
		} else if (delta <= -CONTROLLER_STEERING_ACTIVITY_DELTA ||
				   delta >= CONTROLLER_STEERING_ACTIVITY_DELTA) {
			driving_mode = SDL3_DRIVING_CONTROLLER;
			controller_steering_activity_axis = event->value;
			controller_race_start_requested = true;
		}
	}
	if (event->axis == SDL_GAMEPAD_AXIS_RIGHTY && gameplay_active && !replay_active &&
		modal_depth == 0) {
		legacy_u8 pedal_pressed =
			event->value < -JOYSTICK_AXIS_DEADZONE || event->value >= JOYSTICK_AXIS_DEADZONE;
		legacy_u8 pedal_was_pressed =
			previous < -JOYSTICK_AXIS_DEADZONE || previous >= JOYSTICK_AXIS_DEADZONE;
		legacy_u8 changed_side =
			(event->value < 0 && previous > 0) || (event->value > 0 && previous < 0);
		if (pedal_pressed && (!pedal_was_pressed || changed_side)) {
			controller_race_start_requested = true;
		}
	}
	if ((event->axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ||
		 event->axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) &&
		previous < JOYSTICK_AXIS_DEADZONE && event->value >= JOYSTICK_AXIS_DEADZONE) {
		sdl3_input_queue_key(CONTROLLER_TRIGGER_CONFIRM_KEY);
	}
}

static legacy_s16 controller_axis_directions(SDL_GamepadAxis x_axis, SDL_GamepadAxis y_axis)
{
	legacy_s16 flags = INPUT_NONE;
	legacy_s16 x = controller_axes[x_axis];
	legacy_s16 y = controller_axes[y_axis];
	if (x < -JOYSTICK_AXIS_DEADZONE) {
		flags |= INPUT_STEER_LEFT_FLAG;
	} else if (x >= JOYSTICK_AXIS_DEADZONE) {
		flags |= INPUT_STEER_RIGHT_FLAG;
	}
	if (y < -JOYSTICK_AXIS_DEADZONE) {
		flags |= INPUT_ACCELERATE_FLAG;
	} else if (y >= JOYSTICK_AXIS_DEADZONE) {
		flags |= INPUT_BRAKE_FLAG;
	}
	return flags;
}

static legacy_u16 controller_direction_key(legacy_s16 directions)
{
	if (directions & INPUT_ACCELERATE_FLAG) {
		return KEY_UP;
	}
	if (directions & INPUT_BRAKE_FLAG) {
		return KEY_DOWN;
	}
	if (directions & INPUT_STEER_LEFT_FLAG) {
		return KEY_LEFT;
	}
	return directions & INPUT_STEER_RIGHT_FLAG ? (legacy_u16)KEY_RIGHT : 0;
}

static void controller_update_navigation(void)
{
	legacy_s16 direction =
		controller_navigation_active() && controller != NULL && input_focused
			? controller_axis_directions(SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY)
			: (legacy_s16)INPUT_NONE;
	legacy_u64 now = SDL_GetTicks();
	if (direction != controller_menu_direction) {
		controller_menu_direction = direction;
		controller_menu_repeat_at = now + CONTROLLER_MENU_REPEAT_DELAY_MS;
		legacy_u16 key = controller_direction_key(direction);
		if (key != 0) {
			sdl3_input_queue_key(key);
		}
	} else if (direction != INPUT_NONE && now >= controller_menu_repeat_at) {
		sdl3_input_queue_key(controller_direction_key(direction));
		controller_menu_repeat_at = now + CONTROLLER_MENU_REPEAT_INTERVAL_MS;
	}
}

static void controller_update_dpad_navigation(void)
{
	legacy_s16 direction = INPUT_NONE;
	if (controller_navigation_active() && controller != NULL && input_focused) {
		if (controller_buttons[SDL_GAMEPAD_BUTTON_DPAD_UP]) {
			direction |= INPUT_ACCELERATE_FLAG;
		}
		if (controller_buttons[SDL_GAMEPAD_BUTTON_DPAD_DOWN]) {
			direction |= INPUT_BRAKE_FLAG;
		}
		if (controller_buttons[SDL_GAMEPAD_BUTTON_DPAD_LEFT]) {
			direction |= INPUT_STEER_LEFT_FLAG;
		}
		if (controller_buttons[SDL_GAMEPAD_BUTTON_DPAD_RIGHT]) {
			direction |= INPUT_STEER_RIGHT_FLAG;
		}
	}
	legacy_u16 key = controller_direction_key(direction);
	legacy_u64 now = SDL_GetTicks();
	if (key != controller_dpad_key) {
		controller_dpad_key = key;
		controller_dpad_repeat_at = now + CONTROLLER_MENU_REPEAT_DELAY_MS;
		if (key != 0) {
			sdl3_input_queue_key(key);
		}
	} else if (key != 0 && now >= controller_dpad_repeat_at) {
		sdl3_input_queue_key(key);
		controller_dpad_repeat_at = now + CONTROLLER_MENU_REPEAT_INTERVAL_MS;
	}
}

static legacy_u8 controller_key_state(SDL_Scancode code)
{
	if (controller == NULL || !input_focused) {
		return false;
	}
	for (legacy_u32 button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; button++) {
		if (!controller_buttons[button]) {
			continue;
		}
		legacy_u16 key = controller_button_key((SDL_GamepadButton)button);
		if ((code == SDL_SCANCODE_Q && key == 'q') || (code == SDL_SCANCODE_R && key == 'r') ||
			(code == SDL_SCANCODE_D && key == 'd') || (code == SDL_SCANCODE_T && key == 't') ||
			(code == SDL_SCANCODE_C && key == 'c') ||
			(code == SDL_SCANCODE_ESCAPE && key == KEY_ESCAPE) ||
			(controller_navigation_active() && ((code == SDL_SCANCODE_LEFT && key == KEY_LEFT) ||
												(code == SDL_SCANCODE_RIGHT && key == KEY_RIGHT) ||
												(code == SDL_SCANCODE_UP && key == KEY_UP) ||
												(code == SDL_SCANCODE_DOWN && key == KEY_DOWN))) ||
			(controller_navigation_active() &&
			 ((code == SDL_SCANCODE_RETURN && key == KEY_ENTER) ||
			  (code == SDL_SCANCODE_SPACE && key == KEY_SPACE)))) {
			return true;
		}
	}
	return controller_navigation_active() &&
		   ((code == SDL_SCANCODE_RETURN && CONTROLLER_TRIGGER_CONFIRM_KEY == KEY_ENTER) ||
			(code == SDL_SCANCODE_SPACE && CONTROLLER_TRIGGER_CONFIRM_KEY == KEY_SPACE)) &&
		   (controller_axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] >= JOYSTICK_AXIS_DEADZONE ||
			controller_axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] >= JOYSTICK_AXIS_DEADZONE);
}

#ifdef __ANDROID__
JNIEXPORT jboolean JNICALL Java_org_restunts_android_GameActivity_requestRemoteShift(
	JNIEnv *env, jclass activity_class, jboolean up)
{
	(void)env;
	(void)activity_class;
	return sdl3_input_request_shift(up == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
}
#endif

void sdl3_platform_pump(void)
{
	if (sdl3_batch_mode) {
		sdl3_timer_pump();
		return;
	}
	if (pumping) {
		return;
	}
	pumping = true;
	input_open_devices();
	sdl3_touch_sync_game();
#ifdef __EMSCRIPTEN__
	/* Menus can poll only input. Share the yield budget with explicit waits
	 * and presentation so repeated device reads do not add browser sleeps. */
	sdl3_browser_yield_if_due();
#endif
	legacy_u8 redraw_requested = false;
	SDL_Event event;
	legacy_u8 poll_events =
		SDL_GetTicks() != last_event_poll || SDL_HasEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
	while (poll_events && SDL_PollEvent(&event)) {
		switch (event.type) {
			case SDL_EVENT_QUIT:
			case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
				pumping = false;
				call_exitlist2();
				return;
			case SDL_EVENT_FINGER_DOWN:
			case SDL_EVENT_FINGER_UP:
			case SDL_EVENT_FINGER_MOTION:
			case SDL_EVENT_FINGER_CANCELED: {
				SDL_Rect area;
				SDL_Window *window = sdl3_video_window();
				if (window != NULL && SDL_GetWindowSafeArea(window, &area)) {
					sdl3_touch_event(&event.tfinger, &area);
				}
				break;
			}
#ifdef __ANDROID__
			case SDL_EVENT_TEXT_INPUT:
				if (SDL_TextInputActive(sdl3_video_window())) {
					for (const legacy_char *text = event.text.text; *text != '\0'; text++) {
						if ((legacy_u8)*text >= KEY_SPACE && (legacy_u8)*text < LEGACY_S8_MAX) {
							sdl3_input_queue_key((legacy_u8)*text);
						}
					}
				}
				break;
#endif
			case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
			case SDL_EVENT_GAMEPAD_BUTTON_UP:
				input_controller_button(&event.gbutton);
				break;
			case SDL_EVENT_GAMEPAD_AXIS_MOTION:
				input_controller_axis(&event.gaxis);
				break;
			case SDL_EVENT_GAMEPAD_UPDATE_COMPLETE:
				if (controller != NULL && event.gdevice.which == SDL_GetGamepadID(controller)) {
					/* Preserve quick stick flicks while keeping both axes in one update. */
					controller_update_navigation();
				}
				break;
			case SDL_EVENT_USER:
				if (input_focused && gameplay_active && !replay_active && modal_depth == 0) {
					sdl3_input_shift_event(&event.user);
				}
				break;
			case SDL_EVENT_KEY_DOWN:
			case SDL_EVENT_KEY_UP:
				input_key(&event.key);
				break;
			case SDL_EVENT_WINDOW_FOCUS_LOST:
			case SDL_EVENT_WILL_ENTER_BACKGROUND:
				sdl3_touch_reset();
				input_focused = false;
				controller_reset_state();
				/* A fullscreen transition can change focus while Enter is held.
				 * Keep consumed shortcuts latched until release or a fresh press. */
				memset(keys, 0, sizeof(keys));
				key_count = 0;
				mouse_buttons = 0;
				mouse_transition_count = 0;
				break;
			case SDL_EVENT_WINDOW_FOCUS_GAINED:
				input_focused = true;
				break;
			case SDL_EVENT_DID_ENTER_FOREGROUND:
				input_focused = true;
				sdl3_timer_rebase();
				redraw_requested = true;
				break;
			case SDL_EVENT_RENDER_DEVICE_RESET:
				sdl3_video_reset_renderer();
				redraw_requested = true;
				break;
			case SDL_EVENT_WINDOW_EXPOSED:
			case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
				redraw_requested = true;
				break;
			case SDL_EVENT_MOUSE_MOTION:
				input_mouse_position(event.motion.x, event.motion.y);
				break;
			case SDL_EVENT_MOUSE_BUTTON_DOWN:
			case SDL_EVENT_MOUSE_BUTTON_UP: {
				legacy_s16 flag = event.button.button == SDL_BUTTON_LEFT	 ? MOUSE_LEFT_FLAG
								  : event.button.button == SDL_BUTTON_RIGHT	 ? MOUSE_RIGHT_FLAG
								  : event.button.button == SDL_BUTTON_MIDDLE ? MOUSE_MIDDLE_FLAG
																			 : 0;
				if (event.button.down) {
					mouse_buttons |= flag;
				} else {
					mouse_buttons &= ~flag;
				}
				input_mouse_position(event.button.x, event.button.y);
				if (flag != 0) {
					if (mouse_transition_count == KEY_BUFFER_CAPACITY) {
						mouse_transition_read = (mouse_transition_read + 1U) % KEY_BUFFER_CAPACITY;
						mouse_transition_count--;
					}
					struct MOUSE_TRANSITION *transition =
						&mouse_transitions[(mouse_transition_read + mouse_transition_count++) %
										   KEY_BUFFER_CAPACITY];
					transition->buttons = mouse_buttons;
					transition->x = mouse_x;
					transition->y = mouse_y;
				}
				break;
			}
			case SDL_EVENT_JOYSTICK_REMOVED:
				if (joystick != NULL && event.jdevice.which == SDL_GetJoystickID(joystick)) {
					SDL_CloseJoystick(joystick);
					joystick = NULL;
					open_joystick();
				}
				break;
			case SDL_EVENT_JOYSTICK_ADDED:
				open_joystick();
				break;
			case SDL_EVENT_GAMEPAD_ADDED:
			case SDL_EVENT_GAMEPAD_REMAPPED:
				/* A newly mapped device must no longer appear as a raw joystick. */
				if (joystick != NULL && SDL_IsGamepad(SDL_GetJoystickID(joystick))) {
					SDL_CloseJoystick(joystick);
					joystick = NULL;
				}
				if (event.type == SDL_EVENT_GAMEPAD_REMAPPED && controller != NULL &&
					event.gdevice.which == SDL_GetGamepadID(controller)) {
					controller_reset_state();
				}
				open_controller();
				open_joystick();
				break;
			case SDL_EVENT_GAMEPAD_REMOVED:
				if (controller != NULL && event.gdevice.which == SDL_GetGamepadID(controller)) {
					SDL_CloseGamepad(controller);
					controller = NULL;
					controller_reset_state();
					if (driving_mode == SDL3_DRIVING_CONTROLLER) {
						driving_mode = SDL3_DRIVING_KEYBOARD;
					}
					open_controller();
				}
				break;
			default:
				break;
		}
	}
	if (poll_events) {
		last_event_poll = SDL_GetTicks();
	}
	controller_update_navigation();
	controller_update_dpad_navigation();
	sdl3_touch_update();
	sdl3_timer_pump();
	if (redraw_requested) {
		sdl3_video_redraw();
	}
	sdl3_video_refresh();
	pumping = false;
}

void sdl3_input_shutdown(void)
{
	sdl3_touch_reset();
	SDL_CloseJoystick(joystick);
	joystick = NULL;
	SDL_CloseGamepad(controller);
	controller = NULL;
	controller_reset_state();
	if (input_devices_initialized) {
		SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
	}
	input_devices_initialized = false;
	joystick_enabled = false;
	driving_mode = SDL3_DRIVING_KEYBOARD;
	gameplay_active = false;
	replay_active = false;
	modal_depth = 0;
	input_focused = true;
	memset(keys, 0, sizeof(keys));
	memset(consumed_keys, 0, sizeof(consumed_keys));
	key_count = 0;
	mouse_available = false;
	mouse_transition_count = 0;
	mouse_transition_read = 0;
}

void sdl3_platform_shutdown(void)
{
	dos_timer_shutdown();
	dos_audio_shutdown();
	sdl3_input_shutdown();
	sdl3_video_shutdown();
	hires_shutdown();
	SDL_Quit();
}

void kb_init_interrupt(void)
{
	memset(keys, 0, sizeof(keys));
	memset(consumed_keys, 0, sizeof(consumed_keys));
	key_count = 0;
	key_read = 0;
}

void kb_exit_handler(void)
{
	memset(keys, 0, sizeof(keys));
	memset(consumed_keys, 0, sizeof(consumed_keys));
	key_count = 0;
}

legacy_s16 kb_get_key_state(legacy_s16 key)
{
	sdl3_platform_pump();
	if (key <= 0 || (legacy_u32)key >= SDL_arraysize(scancodes)) {
		return 0;
	}
	if (controller_key_state((SDL_Scancode)scancodes[key]) ||
		sdl3_touch_key_state((SDL_Scancode)scancodes[key]) ||
		(keys[scancodes[key]] && !consumed_keys[scancodes[key]])) {
		return 1;
	}
	/* Distinct SDL keys share the original XT scancode (keypad, right modifiers). */
	for (legacy_u32 code = 1; code < SDL_SCANCODE_COUNT; code++) {
		if (keys[code] && !consumed_keys[code] && legacy_scancode(code) == (legacy_u32)key) {
			return 1;
		}
	}
	return 0;
}

legacy_s16 kb_read_char(void)
{
	sdl3_platform_pump();
	if (key_count == 0) {
		return 0;
	}
	legacy_u16 result = key_buffer[key_read];
	key_read = (key_read + 1U) % KEY_BUFFER_CAPACITY;
	key_count--;
	return LEGACY_S16_FROM_BITS(result);
}

legacy_s16 dos_kb_get_char(void)
{
	return kb_parse_key(kb_read_char());
}

legacy_s16 kb_call_readchar_callback(void)
{
	return kb_read_char();
}

legacy_s16 kb_checking(void)
{
	sdl3_platform_pump();
	return key_count != 0 ? LEGACY_S16_FROM_BITS(key_buffer[key_read]) : 0;
}

legacy_s16 kb_check(void)
{
	sdl3_platform_pump();
	key_count = 0;
	return 0;
}

void flush_stdin(void)
{
	while (kb_read_char() == 0) {
		sdl3_platform_delay(INPUT_POLL_DELAY_MS);
	}
}

void dos_kb_set_numlock(void)
{
	/* SDL keypad events are explicitly mapped to the game's navigation keys. */
}

void dos_kb_clear_numlock(void)
{
}

legacy_s16 dos_mouse_init(legacy_s16 width, legacy_s16 height)
{
	mouse_available = sdl3_video_window() != NULL;
	dos_mouse_set_minmax(0, 0, width - 1, height - 1);
	dos_mouse_set_position(width / 2, height / 2);
	return mouse_available ? -1 : 0;
}

void dos_mouse_set_minmax(legacy_s16 minimum_x, legacy_s16 minimum_y, legacy_s16 maximum_x,
						  legacy_s16 maximum_y)
{
	mouse_min_x = minimum_x;
	mouse_min_y = minimum_y;
	mouse_max_x = maximum_x > SDL3_SCREEN_WIDTH - 1 ? SDL3_SCREEN_WIDTH - 1 : maximum_x;
	mouse_max_y = maximum_y > SDL3_SCREEN_HEIGHT - 1 ? SDL3_SCREEN_HEIGHT - 1 : maximum_y;
	mouse_x = clamp_mouse(mouse_x, mouse_min_x, mouse_max_x);
	mouse_y = clamp_mouse(mouse_y, mouse_min_y, mouse_max_y);
}

void dos_mouse_set_position(legacy_s16 x, legacy_s16 y)
{
	mouse_x = clamp_mouse(x, mouse_min_x, mouse_max_x);
	mouse_y = clamp_mouse(y, mouse_min_y, mouse_max_y);
	if (sdl3_video_window() != NULL) {
		legacy_f32 window_x;
		legacy_f32 window_y;
		sdl3_video_game_to_window(mouse_x, mouse_y, &window_x, &window_y);
		SDL_WarpMouseInWindow(sdl3_video_window(), window_x, window_y);
	}
}

void dos_mouse_get_state(legacy_s16 *buttons, legacy_s16 *x, legacy_s16 *y)
{
	sdl3_platform_pump();
	/* A press and release can arrive between game polls. Preserve both edges,
	 * including their click coordinates, instead of losing the complete click. */
	if (mouse_transition_count != 0) {
		const struct MOUSE_TRANSITION *transition = &mouse_transitions[mouse_transition_read];
		*buttons = transition->buttons;
		*x = clamp_mouse(transition->x, mouse_min_x, mouse_max_x);
		*y = clamp_mouse(transition->y, mouse_min_y, mouse_max_y);
		mouse_transition_read = (mouse_transition_read + 1U) % KEY_BUFFER_CAPACITY;
		mouse_transition_count--;
	} else {
		*buttons = mouse_buttons;
		*x = mouse_x;
		*y = mouse_y;
	}
}

legacy_u16 dos_mouse_get_button_count(void)
{
	return mouse_available ? MOUSE_BUTTON_COUNT : 0;
}

void dos_joystick_reset_calibration(void)
{
	/* SDL supplies calibrated signed axes; their centre and range are stable. */
	dos_joystick_set_enabled(1);
}

void dos_joystick_set_enabled(legacy_u8 enabled)
{
	joystick_enabled = enabled;
	/* Keep devices open for menus even when another racing input is selected. */
	input_open_devices();
}

legacy_u8 dos_joystick_is_enabled(void)
{
	return joystick_enabled;
}

legacy_s16 dos_joystick_get_scaled_axis(legacy_u16 axis_index)
{
	sdl3_platform_pump();
	if (joystick_enabled == 0 || joystick == NULL || !input_focused ||
		axis_index >= JOYSTICK_AXIS_COUNT) {
		return 0;
	}
	/* The game's analogue steering expects approximately -31 .. +32. */
	return (
		legacy_s16)(((legacy_s32)SDL_GetJoystickAxis(joystick, axis_index) * JOYSTICK_AXIS_SCALE) /
					(legacy_s32)LEGACY_U16_SIGN_BIT);
}

legacy_s16 sdl3_joystick_menu_flags(void)
{
	sdl3_platform_pump();
	if (joystick == NULL || !input_focused) {
		return 0;
	}
	legacy_s16 flags = 0;
	legacy_s16 x = SDL_GetJoystickAxis(joystick, 0);
	legacy_s16 y = SDL_GetJoystickAxis(joystick, 1);
	if (x < -JOYSTICK_AXIS_DEADZONE) {
		flags |= INPUT_STEER_LEFT_FLAG;
	} else if (x >= JOYSTICK_AXIS_DEADZONE) {
		flags |= INPUT_STEER_RIGHT_FLAG;
	}
	if (y < -JOYSTICK_AXIS_DEADZONE) {
		flags |= INPUT_ACCELERATE_FLAG;
	} else if (y >= JOYSTICK_AXIS_DEADZONE) {
		flags |= INPUT_BRAKE_FLAG;
	}
	if (SDL_GetNumJoystickHats(joystick) > 0) {
		legacy_u8 hat = SDL_GetJoystickHat(joystick, 0);
		if (hat & SDL_HAT_UP) {
			flags |= INPUT_ACCELERATE_FLAG;
		}
		if (hat & SDL_HAT_DOWN) {
			flags |= INPUT_BRAKE_FLAG;
		}
		if (hat & SDL_HAT_LEFT) {
			flags |= INPUT_STEER_LEFT_FLAG;
		}
		if (hat & SDL_HAT_RIGHT) {
			flags |= INPUT_STEER_RIGHT_FLAG;
		}
	}
	if (SDL_GetJoystickButton(joystick, 0)) {
		flags |= INPUT_PRIMARY_ACTION_FLAG;
	}
	if (SDL_GetJoystickButton(joystick, 1)) {
		flags |= INPUT_SECONDARY_ACTION_FLAG;
	}
	return flags;
}

legacy_s16 dos_get_joy_flags(void)
{
	return joystick_enabled ? sdl3_joystick_menu_flags() : (legacy_s16)INPUT_NONE;
}

void sdl3_input_set_driving_mode(enum SDL3_DRIVING_INPUT mode)
{
	driving_mode = mode;
	controller_steering_activity_axis = controller_axes[SDL_GAMEPAD_AXIS_RIGHTX];
}

enum SDL3_DRIVING_INPUT sdl3_input_driving_mode(void)
{
	sdl3_platform_pump();
	return driving_mode;
}

void sdl3_input_set_gameplay_active(legacy_u8 active)
{
	if (gameplay_active != active) {
		sdl3_input_reset_shifts();
		controller_menu_direction = INPUT_NONE;
		controller_menu_repeat_at = 0;
		controller_dpad_key = INPUT_NONE;
		controller_dpad_repeat_at = 0;
		controller_camera_direction = INPUT_NONE;
		controller_camera_repeat_at = 0;
		controller_shift_pending = INPUT_NONE;
		controller_race_start_requested = false;
	}
	gameplay_active = active;
}

void sdl3_input_set_replay_active(legacy_u8 active)
{
	if (replay_active != active) {
		sdl3_input_reset_shifts();
		controller_menu_direction = INPUT_NONE;
		controller_menu_repeat_at = 0;
		controller_dpad_key = INPUT_NONE;
		controller_dpad_repeat_at = 0;
		controller_camera_direction = INPUT_NONE;
		controller_camera_repeat_at = 0;
		controller_shift_pending = INPUT_NONE;
		controller_race_start_requested = false;
	}
	replay_active = active;
}

void sdl3_input_push_modal(void)
{
	sdl3_input_reset_shifts();
	if (modal_depth < LEGACY_U16_MAX) {
		modal_depth++;
	}
	controller_menu_direction = INPUT_NONE;
	controller_menu_repeat_at = 0;
	controller_dpad_key = INPUT_NONE;
	controller_dpad_repeat_at = 0;
	controller_camera_direction = INPUT_NONE;
	controller_camera_repeat_at = 0;
	controller_shift_pending = INPUT_NONE;
	controller_race_start_requested = false;
}

void sdl3_input_pop_modal(void)
{
	sdl3_input_reset_shifts();
	if (modal_depth != 0) {
		modal_depth--;
	}
	controller_menu_direction = INPUT_NONE;
	controller_menu_repeat_at = 0;
	controller_dpad_key = INPUT_NONE;
	controller_dpad_repeat_at = 0;
	controller_camera_direction = INPUT_NONE;
	controller_camera_repeat_at = 0;
	controller_shift_pending = INPUT_NONE;
	controller_race_start_requested = false;
}

legacy_s16 sdl3_controller_driving_flags(void)
{
	sdl3_platform_pump();
	if (controller == NULL || !input_focused || !gameplay_active || replay_active ||
		modal_depth != 0) {
		return INPUT_NONE;
	}
	legacy_s16 flags =
		controller_axis_directions(SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY) &
		INPUT_PEDAL_MASK;
	if (controller_buttons[SDL_GAMEPAD_BUTTON_DPAD_UP]) {
		flags |= INPUT_SHIFT_UP_FLAG;
	}
	if (controller_buttons[SDL_GAMEPAD_BUTTON_DPAD_DOWN]) {
		flags |= INPUT_SHIFT_DOWN_FLAG;
	}
	flags |= controller_shift_pending;
	controller_shift_pending = INPUT_NONE;
	return flags;
}

legacy_s16 sdl3_controller_scaled_x(void)
{
	sdl3_platform_pump();
	if (controller == NULL || !input_focused) {
		return 0;
	}
	return (
		legacy_s16)(((legacy_s32)controller_axes[SDL_GAMEPAD_AXIS_RIGHTX] * JOYSTICK_AXIS_SCALE) /
					(legacy_s32)LEGACY_U16_SIGN_BIT);
}

legacy_s16 sdl3_controller_camera_flags(void)
{
	sdl3_platform_pump();
	legacy_s16 direction =
		controller != NULL && input_focused && gameplay_active && modal_depth == 0
			? controller_axis_directions(SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY)
			: (legacy_s16)INPUT_NONE;
	legacy_u64 now = SDL_GetTicks();
	if (direction != controller_camera_direction ||
		(direction != INPUT_NONE && now >= controller_camera_repeat_at)) {
		controller_camera_direction = direction;
		controller_camera_repeat_at = now + CONTROLLER_CAMERA_REPEAT_INTERVAL_MS;
		return direction;
	}
	return INPUT_NONE;
}

legacy_u8 sdl3_controller_take_menu_request(void)
{
	sdl3_platform_pump();
	legacy_u8 requested = controller_menu_requested;
	controller_menu_requested = false;
	return requested;
}

legacy_u8 sdl3_controller_take_race_start_request(void)
{
	sdl3_platform_pump();
	legacy_u8 requested = controller_race_start_requested;
	controller_race_start_requested = false;
	return requested;
}
