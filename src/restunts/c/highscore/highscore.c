#include "highscore_internal.h"
#include "../video/video_color.h"
#ifdef RESTUNTS_SDL3
#include "../extended/extended_track.h"
#endif

legacy_u8 ranking_highlight;

legacy_s16 ranking_entry_order[HIGHSCORE_ENTRY_COUNT];

struct RECTANGLE *hiscore_draw_text(legacy_s8 *text, legacy_s16 x, legacy_s16 y, legacy_s16 color,
									legacy_s16 shadow_color)
{
	highscore_text_bounds.left = LEGACY_S16_WRAP_SUB(x, 1);
	highscore_text_bounds.right =
		LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_ADD(x, font_text_width(text)), 1);
	highscore_text_bounds.top = LEGACY_S16_WRAP_SUB(y, 1);
	highscore_text_bounds.bottom =
		LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_ADD(y, font_glyph_height), 1);
	font_set_colors(shadow_color, 0);
	font_draw_text(text, LEGACY_S16_WRAP_ADD(x, 1), LEGACY_S16_WRAP_ADD(y, 1));
	font_draw_text(text, LEGACY_S16_WRAP_SUB(x, 1), LEGACY_S16_WRAP_ADD(y, 1));
	font_draw_text(text, LEGACY_S16_WRAP_ADD(x, 1), LEGACY_S16_WRAP_SUB(y, 1));
	font_draw_text(text, LEGACY_S16_WRAP_SUB(x, 1), LEGACY_S16_WRAP_SUB(y, 1));
	font_set_colors(color, 0);
	font_draw_text(text, LEGACY_S16_FROM_BITS((legacy_u16)x), LEGACY_S16_FROM_BITS((legacy_u16)y));
	return &highscore_text_bounds;
}

void far *highscore_read_with_retry(legacy_s16 operation, const legacy_s8 *filename,
									void far *destination)
{
	if (operation == HIGHSCORE_READ_ONCE_OPERATION) {
		return file_read_nofatal(filename, destination);
	}
	if (operation != HIGHSCORE_READ_RETRY_OPERATION) {
		return 0;
	}
	do {
		void far *result = file_read_nofatal(filename, destination);
		if (result != 0) {
			return result;
		}
	} while (show_disk_error_dialog() != HIGHSCORE_READ_RETRY_CANCEL_RESULT);
	return 0;
}

void highscore_make_empty_entry(struct HIGHSCORE_ENTRY *record)
{
	legacy_u8 *record_bytes = (legacy_u8 *)record;
	for (legacy_u16 offset = 0; offset < HIGHSCORE_COMBINED_NAME_TEXT_BYTES; offset++) {
		record_bytes[offset] = '.';
	}
	record_bytes[HIGHSCORE_COMBINED_NAME_TEXT_BYTES] = 0;
	record->car_flag = 0;
	record->opponent[0] = '.';
	record->opponent[1] = '.';
	record->opponent[2] = '/';
	for (legacy_u16 offset = 3U; offset < HIGHSCORE_OPPONENT_TEXT_BYTES; offset++) {
		record->opponent[offset] = '.';
	}
	record->opponent[HIGHSCORE_OPPONENT_TEXT_BYTES] = 0;
	highscore_entry_set_time(record, HIGHSCORE_EMPTY_TIME);
}

legacy_s16 highscore_load_or_create(legacy_s16 create_default)
{
	ranking_highlight = HIGHSCORE_NO_HIGHLIGHT;
	for (legacy_u16 entry = 0; entry < HIGHSCORE_ENTRY_COUNT; entry++) {
		ranking_entry_order[entry] = entry;
	}
#ifdef RESTUNTS_SDL3
	legacy_s16 result;
	if (extended_highscore_load_current(create_default, &result)) {
		return result;
	}
#endif
	file_build_path(track_directory, gameconfig.game_trackname, ".hig", g_path_buf);
	if (create_default == 0) {
		g_is_busy = 1;
		void far *read_result = highscore_read_with_retry(HIGHSCORE_READ_ONCE_OPERATION, g_path_buf,
														  track_highscore_table);
		g_is_busy = 0;
		return read_result == 0 ? 1 : 0;
	}

	struct HIGHSCORE_ENTRY record;
	highscore_make_empty_entry(&record);
	struct HIGHSCORE_ENTRY far *scores = (struct HIGHSCORE_ENTRY far *)track_highscore_table;
	for (legacy_u16 entry = 0; entry < HIGHSCORE_ENTRY_COUNT; entry++) {
		scores[entry] = record;
	}
	return file_write_fatal(g_path_buf, track_highscore_table, HIGHSCORE_TABLE_SIZE_BYTES) != 0;
}

void highscore_save_sorted(void)
{
	struct HIGHSCORE_ENTRY far *scores = (struct HIGHSCORE_ENTRY far *)track_highscore_table;
	struct HIGHSCORE_ENTRY ordered_scores[HIGHSCORE_ENTRY_COUNT];
	for (legacy_u16 entry = 0; entry < HIGHSCORE_ENTRY_COUNT; entry++) {
		legacy_u16 source_entry = (legacy_u16)ranking_entry_order[entry];
		ordered_scores[entry] = scores[source_entry];
	}
	file_build_path(track_directory, gameconfig.game_trackname, ".hig", g_path_buf);
	g_is_busy = 1;
	(void)file_write_fatal(g_path_buf, ordered_scores, HIGHSCORE_TABLE_SIZE_BYTES);
	g_is_busy = 0;
}

legacy_u16 highscore_format_fields(const legacy_s8 *player_name, const legacy_s8 *car_name,
								   const legacy_s8 *opponent, legacy_u8 car_flag,
								   legacy_u8 *text_offsets)
{
	text_offsets[0] = 0;
	strcpy(&resID_byte1, player_name);
	legacy_u16 output_offset = (legacy_u16)strlen(&resID_byte1) + 1U;
	text_offsets[1] = (legacy_u8)output_offset;
	strcpy(&resID_byte1 + output_offset, car_name);
	output_offset =
		LEGACY_U16_WRAP_ADD(output_offset, (legacy_u16)strlen(&resID_byte1 + output_offset) + 1U);
	text_offsets[2] = (legacy_u8)output_offset;

	legacy_s8 *output = &resID_byte1 + output_offset;
	*output = 0;
	if (car_flag == 1) {
		strcat(output, "(");
	}
	strcat(output, opponent);
	if (car_flag == 1) {
		strcat(output, ")");
	}
	output_offset = LEGACY_U16_WRAP_ADD(output_offset, (legacy_u16)strlen(output) + 1U);

	return output_offset;
}

void print_highscore_entry(legacy_s16 entry, legacy_u8 *text_offsets)
{
#ifdef RESTUNTS_SDL3
	if (extended_highscore_print_entry(entry, text_offsets)) {
		return;
	}
#endif
	struct HIGHSCORE_ENTRY far *scores = (struct HIGHSCORE_ENTRY far *)track_highscore_table;
	struct HIGHSCORE_ENTRY record = scores[ranking_entry_order[entry]];

	legacy_u16 output_offset = highscore_format_fields(
		record.player_name, record.car_name, record.opponent, record.car_flag, text_offsets);

	legacy_s16 saved_frame_rate = framespersec;
	framespersec = GAME_FRAME_RATE_NORMAL;
	legacy_s16 frame_count = LEGACY_S16_FROM_BITS(highscore_entry_time(&record));
	legacy_s8 formatted_time[HIGHSCORE_FORMAT_BUFFER_SIZE];
	format_frame_as_string(formatted_time, frame_count == -1 ? 0 : frame_count, 1);
	text_offsets[3] = (legacy_u8)output_offset;
	strcpy(&resID_byte1 + output_offset, formatted_time);
	framespersec = saved_frame_rate;
}

void highscore_draw_table(void)
{
	sprite_select_render_window();
#ifdef RESTUNTS_SDL3
	extended_highscore_clear_table();
#endif
	copy_string(&resID_byte1, locate_text_res(mainresptr, "hs1"));
	strcat(&resID_byte1, " '");
#ifdef RESTUNTS_SDL3
	strcat(&resID_byte1, extended_track_name());
#else
	strcat(&resID_byte1, gameconfig.game_trackname);
#endif
	strcat(&resID_byte1, "'");
	hiscore_draw_text(&resID_byte1, font_centered_text_x(&resID_byte1), HIGHSCORE_TITLE_Y,
					  VIDEO_UI_COLOR(dialog_fnt_colour), 0);

	legacy_s8 far *text = locate_text_res(mainresptr, "hs2");
	copy_string(&resID_byte1, text);
	hiscore_draw_text(&resID_byte1, HIGHSCORE_PLAYER_COLUMN_X, HIGHSCORE_HEADING_Y,
					  VIDEO_UI_COLOR(dialog_fnt_colour), 0);
	text = locate_text_res(mainresptr, "hs3");
	copy_string(&resID_byte1, text);
	hiscore_draw_text(&resID_byte1, HIGHSCORE_CAR_COLUMN_X, HIGHSCORE_HEADING_Y,
					  VIDEO_UI_COLOR(dialog_fnt_colour), 0);
	text = locate_text_res(mainresptr, "hs5");
	copy_string(&resID_byte1, text);
	hiscore_draw_text(&resID_byte1, HIGHSCORE_OPPONENT_COLUMN_X, HIGHSCORE_HEADING_Y,
					  VIDEO_UI_COLOR(dialog_fnt_colour), 0);
	text = locate_text_res(mainresptr, "hs4");
	copy_string(&resID_byte1, text);
	hiscore_draw_text(&resID_byte1, HIGHSCORE_TIME_COLUMN_X, HIGHSCORE_HEADING_Y,
					  VIDEO_UI_COLOR(dialog_fnt_colour), 0);

	font_set_fontdef2(fontnptr);
	legacy_u8 text_offsets[HIGHSCORE_TEXT_FIELD_COUNT];
	for (legacy_u16 entry = 0; entry < HIGHSCORE_ENTRY_COUNT; entry++) {
		print_highscore_entry(entry, text_offsets);
		legacy_s16 row = LEGACY_S16_WRAP_ADD(LEGACY_U16_WRAP_MUL(entry, HIGHSCORE_ROW_HEIGHT),
											 HIGHSCORE_FIRST_ROW_Y);
		legacy_s16 color =
			entry == (legacy_u8)ranking_highlight ? VIDEO_UI_COLOR(dialog_border_color) : 0;
		font_set_colors(color, 0);
		font_draw_text(&resID_byte1 + text_offsets[0], HIGHSCORE_PLAYER_COLUMN_X, row);
		font_draw_text(&resID_byte1 + text_offsets[1], HIGHSCORE_CAR_COLUMN_X, row);
		font_draw_text(&resID_byte1 + text_offsets[2], HIGHSCORE_OPPONENT_COLUMN_X, row);
		legacy_s16 time_x = HIGHSCORE_TIME_COLUMN_X;
#ifdef RESTUNTS_SDL3
		time_x =
			extended_highscore_time_x(&resID_byte1 + text_offsets[3], time_x, END_SCREEN_WIDTH);
#endif
		font_draw_text(&resID_byte1 + text_offsets[3], time_x, row);
	}
	font_set_fontdef();
}

void enter_hiscore(legacy_s16 frame_count, void far *prompt, legacy_u8 car_flag)
{
	legacy_u16 time_bits = (legacy_u16)frame_count;
	if (framespersec == GAME_FRAME_RATE_LOW) {
		time_bits = LEGACY_U16_WRAP_MUL(time_bits, HIGHSCORE_LOW_FRAME_RATE_TIME_SCALE);
	}
	struct HIGHSCORE_ENTRY far *scores = (struct HIGHSCORE_ENTRY far *)track_highscore_table;
	if (highscore_entry_time(&scores[HIGHSCORE_LAST_ENTRY_INDEX]) <= time_bits) {
		highscore_draw_table();
		return;
	}

	legacy_u16 entry = 0;
	while (highscore_entry_time(&scores[entry]) <= time_bits) {
		if (entry >= HIGHSCORE_ENTRY_COUNT) {
			break;
		}
		ranking_entry_order[entry] = (legacy_s16)entry;
		entry++;
	}
	legacy_u16 rank = entry;
	ranking_highlight = (legacy_u8)rank;
	while (entry < HIGHSCORE_LAST_ENTRY_INDEX) {
		ranking_entry_order[entry + 1U] = (legacy_s16)entry;
		entry++;
	}
	ranking_entry_order[rank] = HIGHSCORE_LAST_ENTRY_INDEX;

	struct HIGHSCORE_ENTRY record;
	legacy_u8 *record_bytes = (legacy_u8 *)&record;
	for (legacy_u16 copied = 0; copied < sizeof(record); copied++) {
		record_bytes[copied] = 0;
	}
	strcpy(record.car_name, gnam_string);
	record.car_flag = car_flag;
	if (gameconfig.game_opponenttype != 0) {
		strcpy(record.opponent, opponent_highscore_name);
		record.opponent[2] = '/';
		strcpy(&record.opponent[3], gsna_string);
	} else {
		strcpy(record.opponent, " ");
	}
	highscore_entry_set_time(&record, time_bits);
	scores[HIGHSCORE_LAST_ENTRY_INDEX] = record;

	sprite_select_render_window();
	highscore_draw_table();
	sprite_blit_to_video(render_window_sprite, -1);
	legacy_s16 positions[2];
	show_text_input_dialog(DIALOG_NO_BACKGROUND_SAVE, prompt, DIALOG_AUTO_POSITION,
						   DIALOG_AUTO_POSITION, VIDEO_UI_COLOR(dialog_border_color), positions);
	check_input();
	call_read_line(highscore_player_name_input, HIGHSCORE_NAME_MAX_CHARACTERS, positions[0],
				   positions[1], HIGHSCORE_NAME_INPUT_TIMEOUT);
	strcpy(record.player_name, highscore_player_name_input);
	scores[HIGHSCORE_LAST_ENTRY_INDEX] = record;

	sprite_select_render_window();
	highscore_draw_table();
	sprite_blit_to_video(render_window_sprite, -1);
	highscore_save_sorted();
	highscore_draw_table();
}
