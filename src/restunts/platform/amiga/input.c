#include "amiga.h"
#include "../../c/platform.h"
#include "../../c/keyboard.h"
#include "../../c/game_input.h"
#include <intuition/intuition.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <string.h>

#define KEY_BUFFER_CAPACITY 64U
#define KEY_RAW_CODE_COUNT 128U
#define KEY_RELEASE_FLAG LEGACY_U8_SIGN_BIT
static legacy_u8 keys[KEY_RAW_CODE_COUNT];
static legacy_u16 key_buffer[KEY_BUFFER_CAPACITY];
static legacy_u16 key_read, key_count;
static legacy_s16 mouse_x = AMIGA_WIDTH / 2U, mouse_y = AMIGA_HEIGHT / 2U, mouse_buttons;
static legacy_s16 mouse_min_x, mouse_min_y, mouse_max_x = AMIGA_WIDTH - 1U,
											mouse_max_y = AMIGA_HEIGHT - 1U;
static legacy_u8 joystick_enabled;
struct MOUSE_TRANSITION {
	legacy_s16 buttons, x, y;
};
static struct MOUSE_TRANSITION transitions[KEY_BUFFER_CAPACITY];
static legacy_u16 transition_read, transition_count;

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

/* Amiga raw keycodes -> IBM XT scan codes used by the shared game. */
static const legacy_u8 scan_codes[KEY_RAW_CODE_COUNT] = {
	[0x00] = 41, [0x01] = 2,  [0x02] = 3,  [0x03] = 4,	[0x04] = 5,	 [0x05] = 6,  [0x06] = 7,
	[0x07] = 8,	 [0x08] = 9,  [0x09] = 10, [0x0a] = 11, [0x0b] = 12, [0x0c] = 13, [0x0d] = 43,
	[0x0f] = 82, [0x10] = 16, [0x11] = 17, [0x12] = 18, [0x13] = 19, [0x14] = 20, [0x15] = 21,
	[0x16] = 22, [0x17] = 23, [0x18] = 24, [0x19] = 25, [0x1a] = 26, [0x1b] = 27, [0x1d] = 79,
	[0x1e] = 80, [0x1f] = 81, [0x20] = 30, [0x21] = 31, [0x22] = 32, [0x23] = 33, [0x24] = 34,
	[0x25] = 35, [0x26] = 36, [0x27] = 37, [0x28] = 38, [0x29] = 39, [0x2a] = 40, [0x2d] = 75,
	[0x2e] = 76, [0x2f] = 77, [0x30] = 86, [0x31] = 44, [0x32] = 45, [0x33] = 46, [0x34] = 47,
	[0x35] = 48, [0x36] = 49, [0x37] = 50, [0x38] = 51, [0x39] = 52, [0x3a] = 53, [0x3c] = 83,
	[0x3d] = 71, [0x3e] = 72, [0x3f] = 73, [0x40] = 57, [0x41] = 14, [0x42] = 15, [0x43] = 28,
	[0x44] = 28, [0x45] = 1,  [0x46] = 83, [0x4a] = 74, [0x4c] = 72, [0x4d] = 80, [0x4e] = 77,
	[0x4f] = 75, [0x50] = 59, [0x51] = 60, [0x52] = 61, [0x53] = 62, [0x54] = 63, [0x55] = 64,
	[0x56] = 65, [0x57] = 66, [0x58] = 67, [0x59] = 68, [0x5c] = 53, [0x5d] = 55, [0x5e] = 78,
	[0x60] = 42, [0x61] = 54, [0x62] = 58, [0x63] = 29, [0x64] = 56, [0x65] = 56};

static void queue_key(legacy_u16 value)
{
	if (value != 0 && key_count < KEY_BUFFER_CAPACITY) {
		key_buffer[(key_read + key_count) % KEY_BUFFER_CAPACITY] = value;
		++key_count;
	}
}

static void raw_key(legacy_u16 raw, legacy_u16 qualifier)
{
	legacy_u8 code = raw & (KEY_RELEASE_FLAG - 1U);
	legacy_u8 scan = scan_codes[code];
	keys[code] = (raw & KEY_RELEASE_FLAG) == 0;
	if ((raw & KEY_RELEASE_FLAG) || scan == 0 || scan >= DOS_KB_PRIMARY_KEYMAP_SIZE) {
		return;
	}
	legacy_u16 value;
	if (qualifier & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) {
		value = dos_kb_keymap5[scan];
	} else if (qualifier & IEQUALIFIER_CONTROL) {
		value = dos_kb_keymap4[scan];
	} else if (qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) {
		value = dos_kb_keymap2[scan];
	} else if (qualifier & IEQUALIFIER_CAPSLOCK) {
		value = dos_kb_keymap3[scan];
	} else {
		value = dos_kb_keymap1[scan];
	}
	if (value & DOS_KB_EXTENDED_KEY_FLAG) {
		if (value >= DOS_KB_EXTENDED_KEY_NORMALIZE_MIN) {
			value &= DOS_KB_SCANCODE_MASK;
		}
		value <<= LEGACY_BYTE_BITS;
	}
	queue_key(value);
}

static legacy_s16 clamp(legacy_s16 v, legacy_s16 lo, legacy_s16 hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

static void mouse_transition(void)
{
	if (transition_count < KEY_BUFFER_CAPACITY) {
		struct MOUSE_TRANSITION *t =
			&transitions[(transition_read + transition_count) % KEY_BUFFER_CAPACITY];
		t->buttons = mouse_buttons;
		t->x = mouse_x;
		t->y = mouse_y;
		++transition_count;
	}
}

void amiga_input_pump(void)
{
	struct Window *window = amiga_video_window();
	struct IntuiMessage *message;
	if (window == NULL) {
		return;
	}
	while ((message = (struct IntuiMessage *)GetMsg(window->UserPort)) != NULL) {
		ULONG kind = message->Class;
		UWORD code = message->Code, qualifier = message->Qualifier;
		WORD x = message->MouseX, y = message->MouseY;
		ReplyMsg((struct Message *)message);
		if (kind == IDCMP_RAWKEY) {
			raw_key(code, qualifier);
		} else if (kind == IDCMP_INACTIVEWINDOW) {
			memset(keys, 0, sizeof(keys));
			mouse_buttons = 0;
			mouse_transition();
		} else if (kind == IDCMP_MOUSEMOVE || kind == IDCMP_MOUSEBUTTONS) {
			if (kind == IDCMP_MOUSEMOVE) {
				mouse_x = clamp(mouse_x + x, mouse_min_x, mouse_max_x);
				mouse_y = clamp(mouse_y + y, mouse_min_y, mouse_max_y);
			}
			if (kind == IDCMP_MOUSEBUTTONS) {
				if (code == SELECTDOWN) {
					mouse_buttons |= 1;
				} else if (code == SELECTUP) {
					mouse_buttons &= ~1;
				} else if (code == MENUDOWN) {
					mouse_buttons |= 2;
				} else if (code == MENUUP) {
					mouse_buttons &= ~2;
				}
				mouse_transition();
			}
		}
	}
}

void kb_init_interrupt(void)
{
	memset(keys, 0, sizeof(keys));
	key_read = key_count = 0;
}
void kb_exit_handler(void)
{
	kb_init_interrupt();
}
legacy_s16 kb_get_key_state(legacy_s16 key)
{
	amiga_platform_poll();
	if (key <= 0) {
		return 0;
	}
	for (legacy_u16 i = 0; i < KEY_RAW_CODE_COUNT; ++i) {
		if (scan_codes[i] == key && keys[i]) {
			return 1;
		}
	}
	return 0;
}
legacy_s16 kb_read_char(void)
{
	amiga_platform_pump();
	if (!key_count) {
		return 0;
	}
	legacy_u16 value = key_buffer[key_read];
	key_read = (key_read + 1U) % KEY_BUFFER_CAPACITY;
	--key_count;
	return LEGACY_S16_FROM_BITS(value);
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
	amiga_platform_pump();
	return key_count ? LEGACY_S16_FROM_BITS(key_buffer[key_read]) : 0;
}
legacy_s16 kb_check(void)
{
	amiga_platform_pump();
	key_count = 0;
	return 0;
}
void flush_stdin(void)
{
	while (kb_read_char() == 0) {
	}
}
void dos_kb_set_numlock(void)
{ /* Keypad navigation is mapped explicitly. */
}
void dos_kb_clear_numlock(void)
{
}
legacy_s16 dos_mouse_init(legacy_s16 width, legacy_s16 height)
{
	dos_mouse_set_minmax(0, 0, width - 1, height - 1);
	dos_mouse_set_position(width / 2, height / 2);
	return amiga_video_window() != NULL ? -1 : 0;
}
void dos_mouse_set_minmax(legacy_s16 min_x, legacy_s16 min_y, legacy_s16 max_x, legacy_s16 max_y)
{
	mouse_min_x = clamp(min_x, 0, AMIGA_WIDTH - 1U);
	mouse_min_y = clamp(min_y, 0, AMIGA_HEIGHT - 1U);
	mouse_max_x = clamp(max_x, mouse_min_x, AMIGA_WIDTH - 1U);
	mouse_max_y = clamp(max_y, mouse_min_y, AMIGA_HEIGHT - 1U);
	mouse_x = clamp(mouse_x, mouse_min_x, mouse_max_x);
	mouse_y = clamp(mouse_y, mouse_min_y, mouse_max_y);
}
void dos_mouse_set_position(legacy_s16 x, legacy_s16 y)
{
	mouse_x = clamp(x, mouse_min_x, mouse_max_x);
	mouse_y = clamp(y, mouse_min_y, mouse_max_y);
}
void dos_mouse_get_state(legacy_s16 *buttons, legacy_s16 *x, legacy_s16 *y)
{
	amiga_platform_pump();
	if (transition_count) {
		struct MOUSE_TRANSITION *t = &transitions[transition_read];
		*buttons = t->buttons;
		*x = clamp(t->x, mouse_min_x, mouse_max_x);
		*y = clamp(t->y, mouse_min_y, mouse_max_y);
		transition_read = (transition_read + 1U) % KEY_BUFFER_CAPACITY;
		--transition_count;
	} else {
		*buttons = mouse_buttons;
		*x = mouse_x;
		*y = mouse_y;
	}
}
legacy_u16 dos_mouse_get_button_count(void)
{
	return amiga_video_window() != NULL ? 2 : 0;
}
void dos_joystick_reset_calibration(void)
{
	joystick_enabled = 1;
}
void dos_joystick_set_enabled(legacy_u8 enabled)
{
	joystick_enabled = enabled != 0;
}
legacy_u8 dos_joystick_is_enabled(void)
{
	return joystick_enabled;
}
legacy_s16 dos_joystick_get_scaled_axis(legacy_u16 axis)
{
	legacy_u16 value;
	if (!joystick_enabled || axis > 1U) {
		return 0;
	}
	value = *(volatile UWORD *)0xDFF00CUL; /* JOY1DAT: joystick on game port 2. */
	if (axis == 0) {
		return ((value & 2U) ? 32 : 0) - ((value & 512U) ? 32 : 0);
	}
	value ^= value >> 1;
	return ((value & 1U) ? 32 : 0) - ((value & 256U) ? 32 : 0);
}
legacy_s16 dos_get_joy_flags(void)
{
	legacy_s16 flags = 0, x, y;
	amiga_platform_poll();
	if (!joystick_enabled) {
		return 0;
	}
	x = dos_joystick_get_scaled_axis(0);
	y = dos_joystick_get_scaled_axis(1);
	if (x < 0) {
		flags |= INPUT_STEER_LEFT_FLAG;
	}
	if (x > 0) {
		flags |= INPUT_STEER_RIGHT_FLAG;
	}
	if (y < 0) {
		flags |= INPUT_ACCELERATE_FLAG;
	}
	if (y > 0) {
		flags |= INPUT_BRAKE_FLAG;
	}
	if ((*(volatile UBYTE *)0xBFE001UL & 128U) == 0) {
		flags |= INPUT_PRIMARY_ACTION_FLAG;
	}
	return flags;
}
