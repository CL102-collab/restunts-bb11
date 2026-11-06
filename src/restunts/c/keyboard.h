#ifndef RESTUNTS_KEYBOARD_H
#define RESTUNTS_KEYBOARD_H

#include "legacy.h"

enum KEY_CODE {
	KEY_BACKSPACE = 8,
	KEY_TAB = 9,
	KEY_ENTER = 13,
	KEY_ESCAPE = 27,
	KEY_SPACE = 32,
	KEY_F1 = 15104,
	KEY_F2 = 15360,
	KEY_F3 = 15616,
	KEY_F4 = 15872,
	KEY_F5 = 16128,
	KEY_F6 = 16384,
	KEY_F7 = 16640,
	KEY_F8 = 16896,
	KEY_F9 = 17152,
	KEY_F10 = 17408,
	/* BIOS extended codes 0x8500 and 0x8600 as signed 16-bit words. */
	KEY_F11 = -31488,
	KEY_F12 = -31232,
	/* BIOS extended code 0x8800. */
	KEY_SHIFT_F12 = -30720,
	KEY_HOME = 18176,
	KEY_UP = 18432,
	KEY_LEFT = 19200,
	KEY_RIGHT = 19712,
	KEY_END = 20224,
	KEY_DOWN = 20480,
	KEY_INSERT = 20992,
	KEY_DELETE = 21248,
	KEY_SHIFT_F1 = 21504,
	KEY_SHIFT_F10 = 23808,
	KEY_CTRL_F9 = 26112
};

void kb_init_interrupt(void);
void kb_exit_handler(void);
legacy_s16 kb_get_key_state(legacy_s16 key);
legacy_s16 dos_kb_get_char(void);
void dos_kb_set_numlock(void);
void dos_kb_clear_numlock(void);
legacy_s16 kb_call_readchar_callback(void);
legacy_s16 kb_read_char(void);
legacy_s16 kb_checking(void);
void flush_stdin(void);
legacy_s16 kb_check(void);

#endif
