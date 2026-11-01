#include "extended_highscore_ui.h"
#ifdef RESTUNTS_SDL3
#include "../fileio.h"
#include "../legacy.h"
#include "../memmgr.h"
#include "../menu/menu_internal.h"
#include "../platform.h"
#ifdef RESTUNTS_AMIGA
#include "../../platform/amiga/amiga.h"
#endif
#include "../replay/replay.h"
#include "../resource.h"
#include "../shape2d/shape2d.h"
#include "../ui/ui_text.h"
#include "../timing.h"
#include "../ui/ui_input.h"
#include "../game_input.h"
#include "../ui/ui_dialog.h"
#include "../race/race_stats.h"
#include "../audio/audio_control.h"
#include "../car/car_resources.h"
#include "../highscore/highscore.h"
#include "../menu/menu_common.h"
#include "../externs.h"
#include "../keyboard.h"
#ifdef RESTUNTS_SDL3
#include "../hires/hires.h"
#include "../opponent/opponent_animation.h"
#include "../shape2d/shape2d_internal.h"
#include "extended_track.h"
#include "extended_highscore.h"
#include "extended_replay_runtime.h"
#include "extended_race_stats.h"
#endif

#define HIGHSCORE_NO_HIGHLIGHT LEGACY_U8_MAX
#define HIGHSCORE_NAME_MAX_CHARACTERS 16
#define HIGHSCORE_NAME_INPUT_TIMEOUT 30000UL

#define END_SCREEN_WIDTH 320U
#define END_SCREEN_TOP_HEIGHT 100

legacy_s16 extended_highscore_print_entry(legacy_s16 entry, legacy_u8 *text_offsets)
{
	if (!extended_track_active()) {
		return 0;
	}
	const struct CSHIG_ENTRY *score =
		extended_highscore_entry((legacy_uint)ranking_entry_order[entry]);
	legacy_u16 output_offset;
	if (score == 0 || score->ticks == CSHIG_EMPTY_TICKS) {
		struct HIGHSCORE_ENTRY empty;
		highscore_make_empty_entry(&empty);
		/* Legacy dots span both name fields. Extended columns use independent
		 * terminated strings, leaving room for the wider time in shared scratch. */
		empty.player_name[sizeof(empty.player_name) - 1U] = 0;
		output_offset = highscore_format_fields(empty.player_name, empty.car_name, empty.opponent,
												empty.car_flag, text_offsets);
	} else {
		output_offset = highscore_format_fields(
			(const legacy_s8 *)score->player_name, (const legacy_s8 *)score->car_name,
			(const legacy_s8 *)score->opponent, score->car_flag, text_offsets);
	}
	legacy_s8 formatted_time[32] = {0};
	(void)extended_highscore_format_time(formatted_time, sizeof(formatted_time),
										 score != 0 ? score->ticks : CSHIG_EMPTY_TICKS);
	text_offsets[3] = (legacy_u8)output_offset;
	strcpy(&resID_byte1 + output_offset, formatted_time);
	return 1;
}
static void extended_hiscore_copy_text(legacy_u8 *destination, size_t capacity,
									   const legacy_s8 *source)
{
	for (size_t i = 0; i < capacity; i++) {
		destination[i] = 0;
	}
	for (size_t i = 0; i + 1U < capacity && source[i] != 0; i++) {
		destination[i] = (legacy_u8)source[i];
	}
}
void extended_hiscore_show_error(void)
{
	const legacy_s8 *source = extended_highscore_error();
	if (source == 0 || source[0] == 0) {
		source = "The high score could not be saved.";
	}
	legacy_s8 message[256];
	size_t used = 0, line_start = 0, last_space = sizeof(message);
	while (*source != 0 && used + 2U < sizeof(message)) {
		legacy_u8 character = (legacy_u8)*source++;
		if (character == ']' || character == '}' || character == '\n' || character == '\r') {
			if (used != line_start) {
				message[used++] = ']';
			}
			line_start = used;
			last_space = sizeof(message);
			continue;
		}
		if (character < 32U || character > 126U) {
			character = '?';
		}
		if (character == ' ' && used == line_start) {
			continue;
		}
		message[used++] = (legacy_s8)character;
		if (character == ' ') {
			last_space = used - 1U;
		}
		if (used - line_start >= 35U) {
			if (last_space > line_start && last_space < used) {
				message[last_space] = ']';
				line_start = last_space + 1U;
			} else {
				message[used++] = ']';
				line_start = used;
			}
			last_space = sizeof(message);
		}
	}
	if (used == 0 || message[used - 1U] != ']') {
		message[used++] = ']';
	}
	message[used] = 0;
	show_dialog(DIALOG_TYPE_ACKNOWLEDGEMENT, DIALOG_SAVE_BACKGROUND, message, DIALOG_AUTO_POSITION,
				DIALOG_AUTO_POSITION, dialog_border_color, 0, 0);
}
legacy_s16 extended_highscore_enter_current(legacy_u32 frames, void far *prompt, legacy_u8 car_flag)
{
	if (!extended_track_active()) {
		return 0;
	}
	ranking_highlight = HIGHSCORE_NO_HIGHLIGHT;
	if (frames == 0 ||
		((legacy_u8)replay_recording_flags & REPLAY_RECORDING_HIGHSCORE_INELIGIBLE_FLAGS) != 0 ||
		!extended_highscore_qualifies(frames, (legacy_u16)framespersec)) {
		highscore_draw_table();
		return 1;
	}
	if (extended_highscore_validate_track(track_directory, gameconfig.game_trackname,
										  extended_track_model()) != 0) {
		extended_hiscore_show_error();
		highscore_draw_table();
		return 1;
	}

	struct CSHIG_ENTRY record = {0};
	extended_hiscore_copy_text(record.car_name, sizeof(record.car_name), gnam_string);
	record.car_flag = car_flag;
	if (gameconfig.game_opponenttype != 0) {
		extended_hiscore_copy_text(record.opponent, sizeof(record.opponent),
								   opponent_highscore_name);
		record.opponent[2] = '/';
		extended_hiscore_copy_text(record.opponent + 3U, sizeof(record.opponent) - 3U, gsna_string);
	} else {
		record.opponent[0] = ' ';
	}
	record.ticks = extended_highscore_ticks(frames, (legacy_u16)framespersec);

	/* Do not insert a nameless row or write the table while the prompt is open. */
	sprite_select_render_window();
	highscore_draw_table();
	sprite_blit_to_video(render_window_sprite, -1);
	legacy_s16 positions[2];
	show_text_input_dialog(DIALOG_NO_BACKGROUND_SAVE, prompt, DIALOG_AUTO_POSITION,
						   DIALOG_AUTO_POSITION, dialog_border_color, positions);
	check_input();
	call_read_line(highscore_player_name_input, HIGHSCORE_NAME_MAX_CHARACTERS, positions[0],
				   positions[1], HIGHSCORE_NAME_INPUT_TIMEOUT);
	extended_hiscore_copy_text(record.player_name, sizeof(record.player_name),
							   highscore_player_name_input);
	legacy_uint rank;
	g_is_busy = 1;
	legacy_int result = extended_highscore_record(track_directory, gameconfig.game_trackname,
												  extended_track_model(), &record, &rank);
	g_is_busy = 0;
	if (result != 0) {
		extended_hiscore_show_error();
	} else if (rank < HIGHSCORE_ENTRY_COUNT) {
		ranking_highlight = (legacy_u8)rank;
	}
	sprite_select_render_window();
	highscore_draw_table();
	sprite_blit_to_video(render_window_sprite, -1);
	return 1;
}
legacy_u32 extended_highscore_player_finish(legacy_u32 fallback)
{
	return extended_replay_is_active() ? extended_race_stats.player_finish : fallback;
}
legacy_u32 extended_highscore_opponent_finish(legacy_u32 fallback)
{
	return extended_replay_is_active() ? extended_race_stats.opponent_finish : fallback;
}
legacy_u32 extended_highscore_penalty(legacy_u32 fallback)
{
	return extended_replay_is_active() ? extended_race_stats.penalty : fallback;
}
legacy_u32 extended_highscore_duration(legacy_u32 fallback)
{
	return extended_replay_is_active() ? extended_race_stats.player_end : fallback;
}
legacy_s16 extended_highscore_format_runtime_time(legacy_s8 *out, legacy_u32 frame)
{
	if (!extended_replay_is_active()) {
		return 0;
	}
	extended_replay_format_runtime_time(out, frame, 1);
	return 1;
}
legacy_s16 extended_highscore_load_current(legacy_s16 create_default, legacy_s16 *result)
{
	if (!extended_track_active()) {
		return 0;
	}
	g_is_busy = 1;
	*result = (legacy_s16)extended_highscore_load_or_create(
		track_directory, gameconfig.game_trackname, extended_track_model(), create_default);
	g_is_busy = 0;
	return 1;
}
void extended_highscore_clear_table(void)
{
	if (extended_track_active()) {
		draw_button(0, 0, 0, END_SCREEN_WIDTH, END_SCREEN_TOP_HEIGHT, button_top_color,
					button_bottom_color, button_fill_color, 0);
	}
}
legacy_s16 extended_highscore_time_x(const legacy_s8 *text, legacy_s16 fallback, legacy_s16 width)
{
	if (extended_track_active()) {
		legacy_s16 right_aligned = width - 1 - font_text_width(text);
		if (right_aligned < fallback) {
			return right_aligned;
		}
	}
	return fallback;
}
legacy_s16 extended_highscore_validate_current(legacy_s8 *status)
{
	if (!extended_track_active()) {
		return 0;
	}
	*status = extended_highscore_validate_track(track_directory, gameconfig.game_trackname,
												extended_track_model()) != 0
				  ? -1
				  : 0;
	return 1;
}
legacy_s16 extended_highscore_check_current(legacy_s8 *status, legacy_u32 *finish_time)
{
	if (!extended_track_active()) {
		return 0;
	}
	legacy_u32 finish = extended_highscore_player_finish((legacy_u16)gState_total_finish_time);
	if (*status == 0 && finish != 0) {
		*finish_time = finish;
		if (((legacy_u8)replay_recording_flags & REPLAY_RECORDING_HIGHSCORE_INELIGIBLE_FLAGS) ==
				0 &&
			extended_highscore_qualifies(finish, (legacy_u16)framespersec)) {
			*status = 1;
		}
	}
	return 1;
}
legacy_s16 extended_highscore_has_first_entry(legacy_u8 *has_score)
{
	if (!extended_track_active()) {
		return 0;
	}
	const struct CSHIG_ENTRY *score = extended_highscore_entry((legacy_u16)ranking_entry_order[0]);
	*has_score = score != 0 && score->ticks != CSHIG_EMPTY_TICKS;
	return 1;
}
#endif
