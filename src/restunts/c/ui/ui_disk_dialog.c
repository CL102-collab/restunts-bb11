#include "ui_dialog_private.h"
#define FILE_DIALOG_STEM_MAX_LENGTH 8U

const legacy_u8 far quiz_question_suffixes[20] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
												  'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j'};

void ensure_file_exists(legacy_s16 file_index)
{
	static legacy_s8 *const message_ids[] = {missing_disk1_message_id, missing_disk2_message_id,
											 missing_disk3_message_id, missing_disk4_message_id};

	legacy_s8 *message_id = message_ids[file_index - 1];
	while (file_find(findfilenames[file_index]) == 0) {
		show_dialog(DIALOG_TYPE_ACKNOWLEDGEMENT, DIALOG_SAVE_BACKGROUND,
					locate_text_res(mainresptr, message_id), -1, -1, dialog_border_color, 0, 0);
		mouse_draw_opaque_check();
		kbormouse = 0;
	}
}

void show_waiting(void)
{
	show_dialog(DIALOG_TYPE_MESSAGE, DIALOG_NO_BACKGROUND_SAVE,
				locate_text_res(mainresptr, waiting_message_id), -1, waitflag, dialog_border_color,
				0, 0);
	mouse_draw_opaque_check();
}

static legacy_s16 do_savefile_dialog_impl(legacy_s8 *primary, legacy_s8 *secondary,
										  legacy_s8 far *prompt, legacy_u16 max_length)
{
	legacy_s16 positions[6];
	legacy_s16 result = LEGACY_S16_FROM_BITS(show_text_input_dialog(
		DIALOG_SAVE_BACKGROUND, locate_text_res(mainresptr, file_save_dialog_id),
		DIALOG_AUTO_POSITION, DIALOG_AUTO_POSITION, dialog_border_color, positions));
	if (result < 0) {
		return 0;
	}

	font_set_colors(dialog_fnt_colour, dialog_background_color);
	copy_string(&resID_byte1, prompt);
	font_draw_text_opaque(&resID_byte1, positions[0], positions[1]);
	font_set_colors(dialog_fnt_colour, dialog_background_color);
	font_draw_text_opaque(primary, positions[2], positions[3]);
	font_draw_text_opaque(secondary, positions[4], positions[5]);
	mouse_draw_transparent_check();

	result = 0;
	for (;;) {
		legacy_s16 key = LEGACY_S16_FROM_BITS(call_read_line(secondary, max_length, positions[4],
															 positions[5], DIALOG_INPUT_TIMEOUT));
		for (legacy_s16 character_index = 0; secondary[character_index] != 0; character_index++) {
			if (secondary[character_index] == ' ') {
				secondary[character_index] = '_';
			}
		}
		if (key == KEY_ESCAPE) {
			break;
		}
		if (key == KEY_ENTER) {
			result = 1;
			break;
		}
		key =
			LEGACY_S16_FROM_BITS(call_read_line(primary, FILE_DIALOG_DIRECTORY_MAX_LENGTH,
												positions[2], positions[3], DIALOG_INPUT_TIMEOUT));
		if (key == KEY_ESCAPE) {
			break;
		}
	}

	sprite_pop_background();
	return result;
}

legacy_s16 do_savefile_dialog_limit(legacy_s8 *primary, legacy_s8 *secondary, legacy_s8 far *prompt,
									legacy_u16 max_length)
{
#ifdef RESTUNTS_SDL3
	sdl3_input_push_modal();
#endif
	legacy_s16 result = do_savefile_dialog_impl(primary, secondary, prompt, max_length);
#ifdef RESTUNTS_SDL3
	sdl3_input_pop_modal();
#endif
	return result;
}

legacy_s16 do_savefile_dialog(legacy_s8 *primary, legacy_s8 *secondary, legacy_s8 far *prompt)
{
	return do_savefile_dialog_limit(primary, secondary, prompt, FILE_DIALOG_STEM_MAX_LENGTH);
}

legacy_s16 show_disk_error_dialog(void)
{
	input_push_status();
	legacy_s16 result;
	if (g_is_busy != 0) {
		result = show_dialog(DIALOG_TYPE_MENU, DIALOG_SAVE_BACKGROUND,
							 locate_text_res(mainresptr, disk_retry_dialog_id), -1, -1,
							 dialog_border_color, 0, 0) == 0;
	} else {
		show_dialog(DIALOG_TYPE_ACKNOWLEDGEMENT, DIALOG_SAVE_BACKGROUND,
					locate_text_res(mainresptr, disk_error_dialog_id), -1, -1, dialog_border_color,
					0, 0);
		result = 1;
	}
	input_pop_status();
	return result;
}

void security_check(legacy_s16 question_index)
{
	legacy_s8 question_id[4] = "q00";
	question_id[2] = quiz_question_suffixes[(legacy_u16)question_index];
	legacy_s8 answer_id[4] = "a00";
	answer_id[2] = question_id[2];
	void far *resource = file_load_resfile("misc");
	legacy_s8 question_text[1024];
	copy_string(question_text, locate_text_res(resource, "cop"));
	copy_string(&resID_byte1, locate_text_res(resource, question_id));
	strcat(question_text, resource_text_payload);
	legacy_u8 question_parts[6];
	for (legacy_u16 i = 0; i < 6U; i++) {
		question_parts[i] = (legacy_u8)(&resID_byte1)[i];
	}

	legacy_s16 positions[8];
	show_text_input_dialog(DIALOG_SAVE_BACKGROUND, (void far *)question_text, DIALOG_AUTO_POSITION,
						   SECURITY_DIALOG_Y, performGraphColor, positions);
	(&resID_byte1)[2] = 0;
	(&resID_byte1)[0] = question_parts[0];
	(&resID_byte1)[1] = question_parts[1];
	font_draw_text(&resID_byte1, positions[0], positions[1]);
	(&resID_byte1)[0] = question_parts[2];
	(&resID_byte1)[1] = question_parts[3];
	font_draw_text(&resID_byte1, positions[2], positions[3]);
	(&resID_byte1)[0] = question_parts[4];
	(&resID_byte1)[1] = question_parts[5];
	font_draw_text(&resID_byte1, positions[4], positions[5]);

	copy_string(&resID_byte1, locate_text_res(resource, answer_id));
	legacy_u16 answer_length = (legacy_u16)strlen(&resID_byte1);
	legacy_s8 answer[22];
	answer[0] = 0;
	legacy_u16 attempts = 0;
	for (;;) {
		call_read_line(answer, answer_length, positions[6], positions[7], DIALOG_INPUT_TIMEOUT);
		for (legacy_u16 i = 0; answer[i] != 0; i++) {
			legacy_u8 character = (legacy_u8)answer[i];

			if ((g_ascii_props[character] & RST_ASC_CHAR_UPPER) != 0) {
				answer[i] = (legacy_s8)(character + ASCII_CASE_OFFSET);
			}
		}
		if (strcmp(answer, &resID_byte1) == 0) {
			passed_security = 1;
			break;
		}
		attempts++;
		if (passed_security != 0 || attempts == 3U) {
			break;
		}
	}

	sprite_pop_background();
	mouse_draw_transparent_check();
	unload_resource(resource);
}
