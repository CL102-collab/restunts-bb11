#include "ui_dialog_private.h"

legacy_s8 selected_file_extension[7];

const legacy_s8 *file_dialog_selected_extension(void)
{
	return selected_file_extension;
}

static void file_dialog_draw_frame(struct FILE_DIALOG *dialog, const legacy_s8 *directory,
								   legacy_s8 far *prompt)
{
	preRender_line(dialog->positions[4] - 4, dialog->positions[5] + 4,
				   dialog->positions[4] + FILE_DIALOG_SEPARATOR_WIDTH, dialog->positions[5] + 4,
				   dialog_border_color);
	font_set_colors(dialog_fnt_colour, dialog_background_color);
	copy_string(&resID_byte1, prompt);
	font_draw_text_opaque(&resID_byte1, dialog->positions[0], dialog->positions[1]);
	for (legacy_u16 index = 0; index < 10U; index++) {
		dialog->hit_areas[index].x1 = dialog->positions[2];
		dialog->hit_areas[index].x2 = dialog->positions[2] + FILE_DIALOG_LIST_WIDTH;
		if (index == 9U) {
			dialog->hit_areas[index].y1 = dialog->hit_areas[index - 1U].y1 + 10;
		} else {
			dialog->hit_areas[index].y1 = dialog->positions[3U + index * 2U];
		}
		dialog->hit_areas[index].y2 = dialog->hit_areas[index].y1 + 10;
	}
	font_set_colors(dialog_fnt_colour, dialog_background_color);
	font_draw_text_opaque(directory, dialog->positions[2], dialog->positions[3]);
}

static legacy_u16 file_dialog_edit_directory(struct FILE_DIALOG *dialog, legacy_s8 *directory)
{
	font_set_colors(dialog_fnt_colour, dialog_background_color);
	return (legacy_u16)call_read_line(directory, FILE_DIALOG_DIRECTORY_MAX_LENGTH,
									  dialog->positions[2], dialog->positions[3],
									  DIALOG_INPUT_TIMEOUT);
}

static void file_dialog_append_name(struct FILE_DIALOG *dialog, const legacy_s8 *path,
									legacy_u8 extended)
{
#ifdef RESTUNTS_SDL3
	legacy_s16 handled = extended_ui_dialog_append_name(dialog, path, extended);
	if (handled < 0) {
		return;
	}
	if (handled == 0) {
		parse_filepath_separators(dialog->filenames[dialog->file_count], path);
	}
#else
	(void)extended;
	parse_filepath_separators(dialog->filenames[dialog->file_count], path);
#endif
	dialog->file_count++;
}

static void file_dialog_collect_names(struct FILE_DIALOG *dialog, const legacy_s8 *found_path,
									  legacy_u8 extended)
{
	do {
		if (dialog->file_count >= 128U) {
			break;
		}
		file_dialog_append_name(dialog, found_path, extended);
	} while (dialog->file_count < 128U && (found_path = file_find_next_alt()) != 0);
}

static void file_dialog_sort_names(struct FILE_DIALOG *dialog)
{
	for (legacy_u16 index = 0; index + 1U < dialog->file_count; index++) {
		for (legacy_u16 compare_index = index + 1U; compare_index < dialog->file_count;
			 compare_index++) {
			if (strcmp(dialog->filenames[index], dialog->filenames[compare_index]) > 0) {
				strcpy(&resID_byte1, dialog->filenames[index]);
				strcpy(dialog->filenames[index], dialog->filenames[compare_index]);
				strcpy(dialog->filenames[compare_index], &resID_byte1);
#ifdef RESTUNTS_SDL3
				extended_ui_dialog_swap(dialog, index, compare_index);
#endif
			}
		}
	}
}

static void file_dialog_draw_scroll_labels(struct FILE_DIALOG *dialog)
{
	if (dialog->file_count > 7U) {
		copy_string(&resID_byte1, locate_text_res(mainresptr, file_scroll_up_label_id));
		font_draw_text_opaque(&resID_byte1, font_centered_text_x(&resID_byte1),
							  dialog->hit_areas[1].y1);
		copy_string(&resID_byte1, locate_text_res(mainresptr, file_scroll_down_label_id));
		font_draw_text_opaque(&resID_byte1, font_centered_text_x(&resID_byte1),
							  dialog->hit_areas[9].y1 - 1);
	}
}

static void file_dialog_draw_rows(struct FILE_DIALOG *dialog)
{
	dialog->previous_selected = dialog->selected;
	dialog->previous_scroll = dialog->scroll;
	mouse_draw_opaque_check();
	for (legacy_u16 visible_row = 0; visible_row < 7U; visible_row++) {
		legacy_s16 candidate = (legacy_s16)(dialog->scroll + (legacy_s16)visible_row);
		if (dialog->file_count != 0 && candidate == dialog->selected) {
			font_set_colors(dialog_background_color, dialog_fnt_colour);
		} else {
			font_set_colors(dialog_fnt_colour, dialog_background_color);
		}
		if (candidate < (legacy_s16)dialog->file_count) {
			strcpy(&resID_byte1, dialog->filenames[(legacy_u8)candidate]);
#ifdef RESTUNTS_SDL3
			extended_ui_dialog_label(dialog, candidate, &resID_byte1);
#endif
			font_draw_text_opaque(&resID_byte1, dialog->positions[2],
								  dialog->hit_areas[visible_row + 2U].y1);
		} else if (dialog->file_count == 0) {
			strcpy(&resID_byte1, visible_row == 0 ? "No files found" : "");
			font_draw_text_opaque(&resID_byte1, dialog->positions[2],
								  dialog->hit_areas[visible_row + 2U].y1);
		} else {
			font_draw_text_opaque("        ", dialog->positions[2],
								  dialog->hit_areas[visible_row + 2U].y1);
		}
		legacy_u16 text_width = (legacy_u16)font_text_width(&resID_byte1);
		sprite_fill_rect(dialog->positions[2] + text_width, dialog->hit_areas[visible_row + 2U].y1,
						 dialog->positions[2] + FILE_DIALOG_LIST_WIDTH - text_width -
							 dialog->positions[2],
						 8, dialog_background_color);
	}
	mouse_draw_transparent_check();
}

static legacy_u16 file_dialog_apply_mouse(struct FILE_DIALOG *dialog, legacy_s16 hit,
										  legacy_u16 key)
{
	if (hit == -1) {
		return key;
	}
	if (dialog->file_count == 0 && hit != 0) {
		return key;
	}
	if (hit != 0 && hit != 1 && hit != 9) {
		legacy_s16 candidate = (legacy_s16)(dialog->scroll + hit - 2);
		if (candidate < (legacy_s16)dialog->file_count) {
			dialog->selected = (legacy_s8)candidate;
		}
		return key;
	}
	if ((mouse_butstate & 3U) == 0) {
		return key;
	}
	if (hit == 0) {
		dialog->selected = 0;
		dialog->scroll = -1;
	} else if (hit == 1) {
		if ((legacy_s16)dialog->selected + dialog->scroll != 0) {
			dialog->selected--;
		}
		if (dialog->selected < dialog->scroll) {
			dialog->scroll = dialog->selected;
		}
	} else {
		if (dialog->selected != (legacy_s8)(dialog->file_count - 1U)) {
			dialog->selected++;
		}
	}
	return 0;
}

static legacy_s8 file_dialog_apply_key(struct FILE_DIALOG *dialog, legacy_u16 key)
{
	if (dialog->file_count == 0 && key != KEY_ESCAPE && key != KEY_UP) {
		return 0;
	}
	if (key == KEY_ENTER || key == KEY_SPACE) {
		return 1;
	}
	if (key == KEY_ESCAPE) {
		return -1;
	}
	if (key == KEY_UP) {
		dialog->selected--;
	} else if (key == KEY_DOWN) {
		if (dialog->selected != (legacy_s8)(dialog->file_count - 1U)) {
			dialog->selected++;
		}
	} else if (key < 256U &&
			   (g_ascii_props[key] & (RST_ASC_CHAR_UPPER | RST_ASC_CHAR_LOWER)) != 0) {
		legacy_u8 character = (legacy_u8)dialog_ascii_lower(key);
		for (legacy_u16 index = 0; index < dialog->file_count; index++) {
			if ((legacy_u8)dialog_ascii_lower((legacy_u8)dialog->filenames[index][0]) ==
				character) {
				dialog->selected = (legacy_s8)index;
				break;
			}
		}
	}
	return 0;
}

static legacy_s8 file_dialog_choose(struct FILE_DIALOG *dialog, legacy_s8 *directory,
									legacy_s8 *filename)
{
	dialog->selected = 0;
	dialog->scroll = 0;
	dialog->previous_selected = -1;
	dialog->previous_scroll = -1;
	(void)timer_get_delta_alt();
	dialog->search_again = 0;
	legacy_s8 result = 0;
	for (;;) {
		if (dialog->selected != dialog->previous_selected ||
			dialog->scroll != dialog->previous_scroll) {
			file_dialog_draw_rows(dialog);
		}
		legacy_u16 key = (legacy_u16)input_checking((legacy_s16)timer_get_delta_alt());
		legacy_s16 hit = (legacy_s16)mouse_multi_hittest(10, dialog->hit_areas);
		key = file_dialog_apply_mouse(dialog, hit, key);
		result = file_dialog_apply_key(dialog, key);
		if (dialog->selected < dialog->scroll) {
			dialog->scroll = dialog->selected;
		}
		if (dialog->scroll < 0) {
			key = file_dialog_edit_directory(dialog, directory);
			if (key == KEY_ESCAPE) {
				result = 0;
			} else {
				dialog->search_again = 1;
			}
			break;
		}
		while ((legacy_s16)(dialog->scroll + 6) < dialog->selected) {
			dialog->scroll++;
		}
		if (result == 0) {
			continue;
		}
		if (result < 0) {
			result = 0;
			break;
		}
		strcpy(filename, dialog->filenames[(legacy_u8)dialog->selected]);
#ifdef RESTUNTS_SDL3
		extended_ui_dialog_selected_extension(dialog, selected_file_extension);
#endif
		result = 1;
		break;
	}
	return result;
}

static legacy_s8 do_fileselect_dialog_impl(legacy_s8 *directory, legacy_s8 *filename,
										   legacy_s8 *extension, legacy_s8 far *prompt,
										   legacy_u8 merge_formats)
{
	struct FILE_DIALOG dialog;
#ifdef RESTUNTS_SDL3
	extended_ui_dialog_initialize(&dialog, merge_formats, extension);
#else
	(void)merge_formats;
#endif
	selected_file_extension[0] = 0;
	if (strlen(extension) < sizeof(selected_file_extension)) {
		strcpy(selected_file_extension, extension);
	}
	legacy_s16 dialog_result = LEGACY_S16_FROM_BITS(show_text_input_dialog(
		DIALOG_SAVE_BACKGROUND, locate_text_res(mainresptr, file_load_dialog_id),
		DIALOG_AUTO_POSITION, DIALOG_AUTO_POSITION, dialog_border_color, dialog.positions));
	if (dialog_result < 0) {
		return 0;
	}
	legacy_u8 saved_busy = g_is_busy;
	g_is_busy = 1;
	file_dialog_draw_frame(&dialog, directory, prompt);
	legacy_s8 result;
	for (;;) {
		mouse_draw_transparent_check();
		dialog.file_count = 0;
		const legacy_s8 *found_path = file_combine_and_find(directory, "*", extension);
		if (found_path != 0) {
			file_dialog_collect_names(&dialog, found_path, 0);
		}
#ifdef RESTUNTS_SDL3
		extended_ui_dialog_collect(&dialog, directory, file_dialog_collect_names);
#endif
		if (dialog.file_count == 0) {
#ifdef __EMSCRIPTEN__
			/* An invalid path must return to the list before the player chooses
			 * to edit again, rather than reopening the touch keyboard forever. */
			if (!sdl3_soft_keyboard_enabled()) {
#endif
				if (file_dialog_edit_directory(&dialog, directory) == KEY_ESCAPE) {
					result = 0;
					break;
				}
				continue;
#ifdef __EMSCRIPTEN__
			}
#endif
		}
		file_dialog_sort_names(&dialog);
		file_dialog_draw_scroll_labels(&dialog);
		result = file_dialog_choose(&dialog, directory, filename);
		if (dialog.search_again == 0) {
			break;
		}
	}
	sprite_pop_background();
	g_is_busy = saved_busy;
	return result;
}

legacy_s8 do_fileselect_dialog(legacy_s8 *directory, legacy_s8 *filename, legacy_s8 *extension,
							   legacy_s8 far *prompt)
{
#ifdef RESTUNTS_SDL3
	sdl3_input_push_modal();
#endif
	legacy_s8 result = do_fileselect_dialog_impl(directory, filename, extension, prompt, 0);
#ifdef RESTUNTS_SDL3
	sdl3_input_pop_modal();
#endif
	return result;
}

legacy_s8 do_format_fileselect_dialog(legacy_s8 *directory, legacy_s8 *filename,
									  legacy_s8 *extension, legacy_s8 far *prompt)
{
#ifdef RESTUNTS_SDL3
	sdl3_input_push_modal();
#endif
	legacy_s8 result = do_fileselect_dialog_impl(directory, filename, extension, prompt, 1);
#ifdef RESTUNTS_SDL3
	sdl3_input_pop_modal();
#endif
	return result;
}
