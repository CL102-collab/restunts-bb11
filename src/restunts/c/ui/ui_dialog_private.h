#ifndef RESTUNTS_UI_DIALOG_PRIVATE_H
#define RESTUNTS_UI_DIALOG_PRIVATE_H

#include "ui_file_dialog_internal.h"
#include "../extended/extended_ui_dialog.h"
#include "../fileio.h"
#include "../legacy.h"
#include "../memmgr.h"
#include "../platform.h"
#include "../resource.h"
#include "../shape2d/shape2d.h"
#include "ui_dialog.h"
#include "ui_text.h"
#include "../timing.h"
#include "ui_input.h"
#include "../game_input.h"
#include "ui_dialog_internal.h"
#include "../externs.h"
#include "../keyboard.h"
#include "../shape3d/shape3d.h"
#ifdef RESTUNTS_SDL3
#include "../../platform/sdl3/controller_game.h"
#endif
#ifdef RESTUNTS_AMIGA
#include "../../platform/amiga/amiga.h"
#endif
#ifdef __EMSCRIPTEN__
#include "../../platform/sdl3/touch_game.h"
#endif

#define RST_ASC_CHAR_UPPER 1
#define RST_ASC_CHAR_LOWER 2
#define ASCII_CASE_OFFSET 32U
#define DIALOG_DEFAULT_WIDTH 32
#define DIALOG_WIDTH_PADDING 24U
#define DIALOG_ALIGNMENT_MASK 65528U
#define DIALOG_SCREEN_WIDTH 320
#define DIALOG_SCREEN_HEIGHT 200
#define DIALOG_CENTER_DIVISOR 2
#define DIALOG_CONTENT_WIDTH_REDUCTION 16
#define DIALOG_NO_SELECTION 255U
#define FILE_DIALOG_SEPARATOR_WIDTH 171
#define FILE_DIALOG_LIST_WIDTH 162
#define FILE_DIALOG_DIRECTORY_MAX_LENGTH 18
#define DIALOG_INPUT_TIMEOUT 30000UL
#define SECURITY_DIALOG_Y 120U
#define DIALOG_LINE_BUFFER_CAPACITY 128
#define DIALOG_CHOICE_BUFFER_CAPACITY 80
#define DIALOG_CHOICE_CAPACITY 20
#define DIALOG_LINE_HEIGHT_PADDING 2
#define DIALOG_PARAGRAPH_HEIGHT 4
#define DIALOG_CHOICE_PARAGRAPH_HEIGHT 3
#define DIALOG_PLACEHOLDER_POSITION_STRIDE 2U
#define DIALOG_DELAY_TICKS 8UL

extern const legacy_u8 far quiz_question_suffixes[20];
extern const legacy_u8 far g_ascii_props[256];

struct DIALOG_CONTENT {
	legacy_s8 line_buffer[DIALOG_LINE_BUFFER_CAPACITY];
	legacy_s8 far *choice_texts[DIALOG_CHOICE_CAPACITY];
	legacy_u8 choice_lengths[DIALOG_CHOICE_CAPACITY];
	struct BUTTON_AREA choices[DIALOG_CHOICE_CAPACITY];
	legacy_s8 far *cursor;
	legacy_s16 line_height;
	legacy_s16 dialog_width;
	legacy_s16 dialog_height;
	legacy_s16 x;
	legacy_s16 y;
	legacy_u16 line_length;
	legacy_u8 choice_count;
	legacy_u8 placeholder_index;
};

extern legacy_s8 selected_file_extension[7];

void sprite_xor_rect_outline(legacy_s16 left, legacy_s16 top, legacy_s16 right, legacy_s16 bottom,
							 legacy_s16 color);
legacy_u16 dialog_ascii_lower(legacy_u16 character);
legacy_u16 show_dialog(legacy_s16 dialog_type, legacy_s16 save_background, void far *text_resource,
					   legacy_u16 x_argument, legacy_u16 y_argument, legacy_s16 border_color,
					   legacy_s16 *disabled_choices, legacy_s16 initial_choice);
const legacy_s8 *file_dialog_selected_extension(void);
legacy_s8 do_fileselect_dialog(legacy_s8 *directory, legacy_s8 *filename, legacy_s8 *extension,
							   legacy_s8 far *prompt);
legacy_s8 do_format_fileselect_dialog(legacy_s8 *directory, legacy_s8 *filename,
									  legacy_s8 *extension, legacy_s8 far *prompt);
void ensure_file_exists(legacy_s16 file_index);
void show_waiting(void);
legacy_s16 do_savefile_dialog(legacy_s8 *primary, legacy_s8 *secondary, legacy_s8 far *prompt);
legacy_s16 show_disk_error_dialog(void);
void security_check(legacy_s16 question_index);

#endif
