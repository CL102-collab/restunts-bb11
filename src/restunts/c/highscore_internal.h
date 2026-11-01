#ifndef RESTUNTS_HIGHSCORE_INTERNAL_H
#define RESTUNTS_HIGHSCORE_INTERNAL_H

#include "extended/extended_highscore_ui.h"
#include "fileio.h"
#include "legacy.h"
#include "memmgr.h"
#include "menu/menu_internal.h"
#include "platform.h"
#ifdef RESTUNTS_AMIGA
#include "../platform/amiga/amiga.h"
#endif
#include "replay/replay.h"
#include "resource.h"
#include "shape2d/shape2d.h"
#include "ui/ui_text.h"
#include "timing.h"
#include "ui/ui_input.h"
#include "game_input.h"
#include "ui/ui_dialog.h"
#include "race/race_stats.h"
#include "audio/audio_control.h"
#include "car/car_resources.h"
#include "highscore.h"
#include "menu/menu_common.h"
#include "externs.h"
#include "keyboard.h"
#ifdef RESTUNTS_SDL3
#include "hires/highscore_animation.h"
#endif

#define HIGHSCORE_READ_RETRY_CANCEL_RESULT 2

#define HIGHSCORE_NO_HIGHLIGHT LEGACY_U8_MAX
#define HIGHSCORE_EMPTY_TIME LEGACY_U16_MAX
#define HIGHSCORE_FORMAT_BUFFER_SIZE 18
#define HIGHSCORE_TEXT_FIELD_COUNT 4
#define HIGHSCORE_NAME_MAX_CHARACTERS 16
#define HIGHSCORE_NAME_INPUT_TIMEOUT 30000UL
#define HIGHSCORE_LOW_FRAME_RATE_TIME_SCALE 2U

#define HIGHSCORE_TITLE_Y 5
#define HIGHSCORE_HEADING_Y 15
#define HIGHSCORE_FIRST_ROW_Y 25
#define HIGHSCORE_ROW_HEIGHT 10U
#define HIGHSCORE_PLAYER_COLUMN_X 16
#define HIGHSCORE_CAR_COLUMN_X 120
#define HIGHSCORE_OPPONENT_COLUMN_X 224
#define HIGHSCORE_TIME_COLUMN_X 272

#define END_SCREEN_WIDTH 320U
#define END_SCREEN_HEIGHT 200U
#define END_SCREEN_COLOR 15U
#define END_SCREEN_TOP_HEIGHT 100
#define END_SCREEN_BOTTOM_Y 101
#define END_SCREEN_BOTTOM_HEIGHT 99
#define END_SCREEN_TEXT_START_Y 107
#define END_SCREEN_ANIMATION_WIDTH 200U
#define END_SCREEN_ANIMATION_HEIGHT 100U
#define END_SCREEN_ANIMATION_RIGHT 312
#define END_SCREEN_ANIMATION_BOTTOM 99
#define END_SCREEN_ANIMATION_CENTER_SHIFT 1U
#define END_SCREEN_ANIMATION_BORDER_INSET 3
#define END_SCREEN_ANIMATION_BORDER_GROWTH 5
#define END_SCREEN_ANIMATION_FRAME_TICKS 30

#define END_SCREEN_SPEED_FRACTION_BITS 8U
#define END_SCREEN_NUMBER_WIDTH 3

#define END_SCREEN_TEXT_VARIANT_COUNT 3U
#define END_SCREEN_WIN_VARIANT_COUNT 2
#define END_SCREEN_OUTCOME_VARIANT_COUNT 4
#define END_SCREEN_FINISHED_VARIANT_OFFSET 2
#define END_SCREEN_BINARY_RANDOM_MASK 1U
#define END_SCREEN_FOUR_WAY_RANDOM_MASK 3U

#define END_SCREEN_TEXT_WORD_CAPACITY 32
#define END_SCREEN_TEXT_ID_SIZE 4
#define END_SCREEN_TEXT_LEFT 8
#define END_SCREEN_TEXT_TOP 8
#define END_SCREEN_TEXT_LINE_HEIGHT 8
#define END_SCREEN_TEXT_RIGHT_MARGIN 16
#define END_SCREEN_TEXT_OUTPUT_CAPACITY 80U

#define END_SCREEN_TRACK_RESOURCE_INDEX 4
#define END_SCREEN_TRACK_VALIDATION_BYTES 901U
#define END_SCREEN_MENU_AREA_COUNT 5U
#define END_SCREEN_BUTTON_COUNT 4U
#define END_SCREEN_REDUCED_BUTTON_COUNT 3
#define END_SCREEN_MENU_OFFSET_WITHOUT_FIRST_BUTTON (-36)
#define END_SCREEN_LAST_BUTTON_INDEX 3U
#define END_SCREEN_EVALUATION_BUTTON_X 129
#define END_SCREEN_BUTTON_Y 175
#define END_SCREEN_BUTTON_WIDTH 70
#define END_SCREEN_BUTTON_HEIGHT 21
#define END_SCREEN_NO_SCORE_MESSAGE_Y 50

enum HIGHSCORE_READ_OPERATION {
	HIGHSCORE_READ_RETRY_OPERATION = 9,
	HIGHSCORE_READ_ONCE_OPERATION = 10
};

enum END_SCREEN_OUTCOME {
	END_SCREEN_OUTCOME_LOSS = 0,
	END_SCREEN_OUTCOME_WIN = 1,
	END_SCREEN_OUTCOME_NONE = 2
};

extern legacy_u8 ranking_highlight;
extern legacy_s16 ranking_entry_order[HIGHSCORE_ENTRY_COUNT];

legacy_s16 get_super_random(void);

struct END_SCREEN_TEXT_LINE {
	legacy_s8 word[END_SCREEN_TEXT_WORD_CAPACITY];
	legacy_u16 word_length, output_length;
	legacy_s16 line_width, line_y;
};

struct END_SCREEN_STATE {
	legacy_s8 far *misc_resource;
	legacy_s8 far *opponent_resource;
	legacy_s8 far *animation_resource;
	legacy_u8 far *animation_sequence;
	struct SPRITE far *animation_sprite;
	struct BUTTON_AREA button_areas[END_SCREEN_BUTTON_COUNT];
	legacy_s8 score_status;
	legacy_u8 outcome, opponent_active, evaluation_screen;
	legacy_u8 selected, previous_selection, blit_mode;
	legacy_u8 animation_frame, previous_animation_frame, text_prefix;
#ifdef RESTUNTS_SDL3
	legacy_u32 finish_time;
#else
	legacy_s16 finish_time;
#endif
	legacy_s16 animation_x, animation_y, animation_timer, text_y;
#ifdef RESTUNTS_SDL3
	struct HIRES_HIGHSCORE_ANIMATION hires_animation;
#endif
};

void end_format_time(legacy_s8 *out, legacy_u32 frame);
struct RECTANGLE *hiscore_draw_text(legacy_s8 *text, legacy_s16 x, legacy_s16 y, legacy_s16 color,
									legacy_s16 shadow_color);
void far *highscore_read_with_retry(legacy_s16 operation, const legacy_s8 *filename,
									void far *destination);
void highscore_make_empty_entry(struct HIGHSCORE_ENTRY *record);
legacy_s16 highscore_load_or_create(legacy_s16 create_default);
void highscore_save_sorted(void);
legacy_u16 highscore_format_fields(const legacy_s8 *player_name, const legacy_s8 *car_name,
								   const legacy_s8 *opponent, legacy_u8 car_flag,
								   legacy_u8 *text_offsets);
void print_highscore_entry(legacy_s16 entry, legacy_u8 *text_offsets);
void highscore_draw_table(void);
void enter_hiscore(legacy_s16 frame_count, void far *prompt, legacy_u8 car_flag);
void end_hiscore_set_text(legacy_s8 far *resource, legacy_s8 *text_id);
void end_hiscore_append_text(legacy_s8 far *resource, legacy_s8 *text_id);
void end_hiscore_draw_current_text(legacy_s16 *y);
void end_hiscore_draw_opponent_text(legacy_s8 far *opponent_resource, legacy_u8 outcome,
									legacy_u8 text_prefix, legacy_s16 animation_x);
legacy_u16 end_hiscore(void);

#endif
