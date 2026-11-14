#include "menu_track_editor_internal.h"
#include "../video/video_color.h"

legacy_u8 track_editor_palette_tile(legacy_u8 page, legacy_u8 row, legacy_u8 column)
{
	return progress_box_shape[(legacy_u16)page * 36U + (legacy_u16)row * 6U + column];
}

static void track_editor_skip_previous_placeholders(legacy_u8 page, legacy_u8 *row,
													legacy_u8 *column)
{
	while (track_editor_palette_tile(page, *row, *column) >= TRACK_TILE_CONTINUATION_SOUTH) {
		legacy_u8 tile = track_editor_palette_tile(page, *row, *column);
		if (tile == TRACK_TILE_CONTINUATION_EAST) {
			(*column)--;
		} else {
			(*row)--;
		}
	}
}

void track_editor_show_message(legacy_s8 far *text_resource, const legacy_s8 *resource_id)
{
	show_dialog(DIALOG_TYPE_ACKNOWLEDGEMENT, DIALOG_SAVE_BACKGROUND,
				locate_text_res(text_resource, (legacy_s8 *)resource_id), DIALOG_AUTO_POSITION,
				DIALOG_AUTO_POSITION, VIDEO_UI_COLOR(performGraphColor), 0, 0);
}

static void track_editor_save_track(legacy_u8 *track_changed, legacy_u8 *map_dirty)
{
	g_is_busy = 1;
	legacy_u8 save_status = 0;
	while (save_status == 0) {
		sprite_select_screen_compat();
		*map_dirty = 1;
		legacy_s8 far *text = locate_text_res((legacy_s8 far *)mainresptr, "trk");
		if (do_savefile_dialog(track_directory, gameconfig.game_trackname, text) == 0) {
			save_status = TRACK_EDITOR_SAVE_CANCELLED;
			break;
		}
		file_build_path(track_directory, gameconfig.game_trackname, ".trk", g_path_buf);
		save_status = 1;
		if (file_find(g_path_buf) != 0) {
			legacy_s16 result = LEGACY_S16_FROM_BITS(show_dialog(
				DIALOG_TYPE_MENU, DIALOG_SAVE_BACKGROUND,
				locate_text_res((legacy_s8 far *)mainresptr, "fex"), DIALOG_AUTO_POSITION,
				DIALOG_AUTO_POSITION, VIDEO_UI_COLOR(performGraphColor), 0, 0));
			if (result == -1) {
				save_status = TRACK_EDITOR_SAVE_CANCELLED;
				break;
			}
			if (result == 0) {
				save_status = 0;
				continue;
			}
		}
		legacy_s16 write_result =
			file_write_fatal(g_path_buf, track_element_map, TRACK_EDITOR_TRACK_FILE_SIZE);
		if (write_result == 0) {
			highscore_load_or_create(1);
		}
		if (write_result != 0) {
			track_editor_show_message((legacy_s8 far *)mainresptr, "ser");
			save_status = 0;
		} else {
			*track_changed = 0;
		}
	}
	g_is_busy = 0;
}

static void track_editor_swap_tiles(legacy_u8 *selected_tile, legacy_u8 *saved_tile,
									legacy_u8 *palette_dirty)
{
	legacy_u8 value = *selected_tile;
	*selected_tile = *saved_tile;
	*saved_tile = value;
	*palette_dirty = 1;
}

static void track_editor_hover_map(struct TRACK_EDITOR_SESSION *editor)
{
	legacy_u8 clicked_column =
		(legacy_u8)LEGACY_S16_DIV_OR_ZERO(LEGACY_S16_WRAP_SUB(mouse_xpos, 8), 16);
	legacy_u8 clicked_row =
		(legacy_u8)LEGACY_S16_DIV_OR_ZERO(LEGACY_S16_WRAP_SUB(mouse_ypos, 4), 16);
	if (editor->page != 0) {
		if (clicked_row == 10U &&
			((legacy_u8)trkObjectList[editor->selected_tile].ss_multiTileFlag & 1U) != 0) {
			clicked_row--;
		}
		if (clicked_column == 11U &&
			((legacy_u8)trkObjectList[editor->selected_tile].ss_multiTileFlag & 2U) != 0) {
			clicked_column--;
		}
	}
	clicked_column = (legacy_u8)(clicked_column + editor->map_column_offset);
	clicked_row = (legacy_u8)(clicked_row + editor->map_row_offset);
	if (editor->focus != 0 || editor->selection_column[0] != clicked_column ||
		editor->selection_row[0] != clicked_row) {
		editor->focus = 0;
		editor->selection_column[0] = clicked_column;
		editor->selection_row[0] = clicked_row;
		editor->key = 1;
	}
	if (editor->key == KEY_SPACE) {
		editor->key = KEY_ENTER;
	}
}

static void track_editor_hover_palette(struct TRACK_EDITOR_SESSION *editor)
{
	legacy_u8 clicked_column = (legacy_u8)LEGACY_S16_DIV_OR_ZERO(
		LEGACY_S16_WRAP_SUB(mouse_xpos, TRACK_EDITOR_PALETTE_LEFT), TRACK_EDITOR_TILE_SIZE);
	legacy_u8 clicked_row = (legacy_u8)LEGACY_S16_DIV_OR_ZERO(
		LEGACY_S16_WRAP_SUB(mouse_ypos, TRACK_EDITOR_PALETTE_TOP), TRACK_EDITOR_TILE_SIZE);
	if (clicked_row < 6U) {
		if (track_editor_palette_tile(editor->page, clicked_row, clicked_column) ==
			TRACK_TILE_CONTINUATION_SOUTH) {
			clicked_row--;
		}
		if (track_editor_palette_tile(editor->page, clicked_row, clicked_column) ==
			TRACK_TILE_CONTINUATION_EAST) {
			clicked_column--;
		}
	} else {
		clicked_row = (legacy_u8)LEGACY_S16_DIV_OR_ZERO(
			LEGACY_S16_WRAP_SUB(mouse_ypos, TRACK_EDITOR_PALETTE_ACTION_TOP),
			TRACK_EDITOR_TILE_SIZE);
		if (clicked_row == 7U) {
			clicked_column = 0;
		} else if (clicked_column >= 3U) {
			clicked_column = 3;
		} else {
			clicked_column = 0;
		}
	}
	if (editor->focus == 0 || editor->selection_column[1] != clicked_column ||
		editor->selection_row[1] != clicked_row) {
		editor->selection_column[1] = clicked_column;
		editor->selection_row[1] = clicked_row;
		editor->focus = 1;
		editor->key = 1;
	}
	if (editor->key == KEY_SPACE) {
		editor->key = KEY_ENTER;
	}
}

static void track_editor_mouse_input(struct TRACK_EDITOR_SESSION *editor)
{
	legacy_u8 hit = (legacy_u8)mouse_multi_hittest(5, track_editor_buttons);
	if (hit != TRACK_EDITOR_MOUSE_NO_HIT) {
		if (hit == 0U && (mouse_butstate & 3) != 0) {
			editor->focus = 0;
			legacy_u8 clicked_column = (legacy_u8)scrollbar_update(
				1, TRACK_EDITOR_HORIZONTAL_SCROLLBAR_X, TRACK_EDITOR_LABEL_Y,
				TRACK_EDITOR_HORIZONTAL_SCROLLBAR_LENGTH, 5, editor->map_column_offset, 12, 30);
			editor->selection_column[0] = (legacy_u8)(editor->selection_column[0] + clicked_column -
													  editor->map_column_offset);
			editor->map_column_offset = clicked_column;
			editor->key = 1;
		} else if (hit == 1U && (mouse_butstate & 3) != 0) {
			editor->focus = 0;
			legacy_u8 clicked_row = (legacy_u8)scrollbar_update(
				1, TRACK_EDITOR_VERTICAL_SCROLLBAR_X, 5, 4, TRACK_EDITOR_VERTICAL_SCROLLBAR_LENGTH,
				editor->map_row_offset, 11, 30);
			editor->selection_row[0] =
				(legacy_u8)(editor->selection_row[0] + clicked_row - editor->map_row_offset);
			editor->map_row_offset = clicked_row;
			editor->key = 1;
		} else if (hit == 2U) {
			if ((mouse_butstate & 3) != 0) {
				editor->focus = 1;
				editor->page = (legacy_u8)scrollbar_update(
								   1, TRACK_EDITOR_PAGE_BAR_X, TRACK_EDITOR_PAGE_BAR_WIDTH,
								   TRACK_EDITOR_PAGE_BAR_Y, 5, editor->page - 1U, 1, 10) +
							   1U;
				editor->key = 1;
			}
		} else if (hit == 3U) {
			track_editor_hover_map(editor);
		} else if (hit == 4U) {
			track_editor_hover_palette(editor);
		}
	}
}

void track_editor_wait_for_input(struct TRACK_EDITOR_SESSION *editor)
{
	editor->blink_timer = TRACK_EDITOR_BLINK_INITIAL_COUNT;
	editor->cursor_drawn = 0;
	mouse_draw_opaque_check();
	editor->blink_focus = editor->focus;
	if (editor->blink_focus == 0) {
		sprite_clear_shape_alt(track_editor_under_cursor_shapes[editor->multi_tile],
							   editor->cursor_x, editor->cursor_y);
	}

	editor->key = 0;
	while (editor->key == 0) {
		if (editor->blink_timer > TRACK_EDITOR_BLINK_INTERVAL) {
			mouse_draw_opaque_check();
			if (editor->blink_focus == 0) {
				if (editor->cursor_drawn != 0) {
					sprite_shape_to_1(track_editor_under_cursor_shapes[editor->multi_tile],
									  editor->cursor_x, editor->cursor_y);
				} else {
					sprite_shape_to_1(editor->cursor_sprites[editor->multi_tile]->sprite_bitmapptr,
									  editor->cursor_x, editor->cursor_y);
				}
			} else {
				track_editor_toggle_highlight(editor->cursor_x, editor->cursor_y,
											  editor->cursor_width, editor->cursor_height);
			}
			mouse_draw_transparent_check();
			editor->cursor_drawn ^= 1U;
			editor->blink_timer = 0;
		}

		legacy_u16 delta = (legacy_u16)timer_get_delta_alt();
		editor->blink_timer = LEGACY_U16_WRAP_ADD(editor->blink_timer, delta);
		editor->key = (legacy_u16)input_checking(LEGACY_S16_FROM_BITS(delta));
		track_editor_mouse_input(editor);

		if (editor->key == 1U) {
			editor->last_column = TRACK_EDITOR_POSITION_UNSET;
		}
		if (editor->key == 0 && editor->path_animation_index != 0) {
			editor->key = 1;
		}
	}
	if (editor->path_animation_index != 0) {
		(void)timer_wait_ticks(10UL);
	}

	if (editor->cursor_drawn != 0) {
		mouse_draw_opaque_check();
		if (editor->blink_focus == 0) {
			sprite_shape_to_1(track_editor_under_cursor_shapes[editor->multi_tile],
							  editor->cursor_x, editor->cursor_y);
		} else {
			track_editor_toggle_highlight(editor->cursor_x, editor->cursor_y, editor->cursor_width,
										  editor->cursor_height);
		}
		mouse_draw_transparent_check();
	}
}

void track_editor_advance_path_animation(struct TRACK_EDITOR_SESSION *editor)
{
	if (editor->key == 1U && editor->focus == 0) {
		editor->path_animation_index = (legacy_u16)(track_pieces_counter - 1);
	}
	editor->selection_column[0] = (legacy_u8)track_route_columns[editor->path_animation_index];
	editor->selection_row[0] = (legacy_u8)track_route_rows[editor->path_animation_index];
	editor->selected_tile =
		track_editor_map_tile(editor->selection_column[0], editor->selection_row[0]);
	editor->map_dirty = 1;
	editor->palette_dirty = 1;
	editor->path_animation_index++;
	if (editor->path_animation_index >= (legacy_u16)track_pieces_counter) {
		editor->selected_tile = editor->animation_saved_tile;
		editor->path_animation_index = 0;
	}
	editor->palette_dirty = 1;
	editor->map_dirty = 1;
}

static void track_editor_check_route(struct TRACK_EDITOR_SESSION *editor)
{
	legacy_s16 result = (legacy_s8)track_setup();
	legacy_s8 *resource_id =
		(legacy_s8 *)track_editor_error_resource_ids + (legacy_u16)(legacy_u8)result * 3U;
	__fmemcpy(&resID_byte1, resource_id, 3U);
	*(&resID_byte1 + 3) = 0;
	track_editor_show_message(editor->text_resource, &resID_byte1);
	if (result > 1) {
		editor->focus = 0;
		if (track_pieces_counter == 0) {
			editor->selection_column[0] = track_validation_column;
			editor->selection_row[0] = track_validation_row;
		} else {
			editor->animation_saved_tile = editor->selected_tile;
			editor->selection_column[0] = (legacy_u8)track_route_columns[0];
			editor->selection_row[0] = (legacy_u8)track_route_rows[0];
			editor->selected_tile =
				track_editor_map_tile(editor->selection_column[0], editor->selection_row[0]);
			editor->path_animation_index = 1;
			editor->palette_dirty = 1;
		}
	}
	check_input();
}

static void track_editor_select_skybox(struct TRACK_EDITOR_SESSION *editor)
{
	legacy_u8 dialog_result = LEGACY_S8_FROM_BITS(show_dialog(
		DIALOG_TYPE_MENU, DIALOG_SAVE_BACKGROUND, locate_text_res(editor->text_resource, "mss"),
		DIALOG_AUTO_POSITION, DIALOG_AUTO_POSITION, VIDEO_UI_COLOR(dialog_border_color), 0,
		track_element_map[TRACK_EDITOR_SKYBOX_MAP_INDEX]));
	if (dialog_result != TRACK_EDITOR_DIALOG_CANCELLED &&
		dialog_result != TRACK_EDITOR_DIALOG_NO_CHANGE) {
		track_element_map[TRACK_EDITOR_SKYBOX_MAP_INDEX] = dialog_result;
		editor->map_dirty = 1;
		editor->track_changed = 1;
	}
}

static void track_editor_select_terrain(struct TRACK_EDITOR_SESSION *editor)
{
	legacy_u8 dialog_result = LEGACY_S8_FROM_BITS(show_dialog(
		DIALOG_TYPE_MENU, DIALOG_SAVE_BACKGROUND, locate_text_res(editor->text_resource, "men"),
		DIALOG_AUTO_POSITION, DIALOG_AUTO_POSITION, VIDEO_UI_COLOR(dialog_border_color), 0, 0));
	legacy_s8 terrain_id[5];
	struct SHAPE2D far *terrain_shape;
	if (dialog_result != TRACK_EDITOR_DIALOG_CANCELLED &&
		dialog_result != TRACK_EDITOR_DIALOG_NO_CHANGE) {
		for (legacy_u16 index = 0; index < 900U; index++) {
			track_element_map[index] = 0;
		}
		terrain_id[0] = 't';
		terrain_id[1] = 'e';
		terrain_id[2] = 'r';
		terrain_id[3] = (legacy_s8)('0' + dialog_result);
		terrain_id[4] = 0;
		terrain_shape = (struct SHAPE2D far *)locate_shape_alt(editor->text_resource, terrain_id);
		__fmemcpy(track_terrain_map, terrain_shape, 901U);
		gameconfig.game_trackname[0] = 0;
		editor->map_dirty = 1;
		editor->track_changed = 1;
	}
}

static void track_editor_load_track(struct TRACK_EDITOR_SESSION *editor)
{
	legacy_s16 result = 1;
	if (editor->track_changed != 0) {
		result = (legacy_s16)show_dialog(
			DIALOG_TYPE_MENU, DIALOG_SAVE_BACKGROUND, locate_text_res(editor->text_resource, "chl"),
			DIALOG_AUTO_POSITION, DIALOG_AUTO_POSITION, VIDEO_UI_COLOR(performGraphColor), 0, 0);
	}
	if (result == 0) {
		track_editor_save_track(&editor->track_changed, &editor->map_dirty);
	} else {
		g_is_busy = 1;
		editor->map_dirty = 1;
		legacy_s8 far *text = locate_text_res((legacy_s8 far *)mainresptr, "trk");
		result = do_fileselect_dialog(track_directory, gameconfig.game_trackname, ".trk", text);
		file_build_path(track_directory, gameconfig.game_trackname, ".trk", g_path_buf);
		if (result > 0) {
			file_read_fatal(g_path_buf, track_element_map);
			track_setup();
			editor->focus = 0;
			editor->selection_column[0] = track_validation_column;
			editor->selection_row[0] = track_validation_row;
			editor->track_changed = 0;
			editor->map_dirty = 1;
		}
		g_is_busy = 0;
	}
}

static void track_editor_exit(struct TRACK_EDITOR_SESSION *editor)
{
	legacy_s16 result = 1;
	if (editor->track_changed != 0) {
		result = (legacy_s16)show_dialog(
			DIALOG_TYPE_MENU, DIALOG_SAVE_BACKGROUND, locate_text_res(editor->text_resource, "chx"),
			DIALOG_AUTO_POSITION, DIALOG_AUTO_POSITION, VIDEO_UI_COLOR(performGraphColor), 0, 0);
	}
	if (result == 0) {
		track_editor_save_track(&editor->track_changed, &editor->map_dirty);
	} else {
		editor->menu_active = 0;
	}
}

static void track_editor_select_palette_tile(struct TRACK_EDITOR_SESSION *editor)
{
	editor->selected_tile = track_editor_palette_tile(editor->page, editor->selection_row[1],
													  editor->selection_column[1]);
	if (editor->page != 0) {
		editor->multi_tile = (legacy_u8)trkObjectList[editor->selected_tile].ss_multiTileFlag;
		if ((editor->multi_tile & 1U) != 0 &&
			editor->selection_row[0] - editor->map_row_offset == 10U) {
			editor->selection_row[0]--;
		}
		if ((editor->multi_tile & 2U) != 0 &&
			editor->selection_column[0] - editor->map_column_offset == 11U) {
			editor->selection_column[0]--;
		}
	}
	editor->palette_dirty = 1;
	editor->focus = 0;
}

static void track_editor_activate_palette(struct TRACK_EDITOR_SESSION *editor)
{
	if (editor->selection_row[1] < 6U) {
		track_editor_select_palette_tile(editor);
	} else if (editor->selection_row[1] == 6U) {
		editor->page++;
		if (editor->page > 10U) {
			editor->page = 1;
		}
	} else if (editor->selection_row[1] == 7U) {
		track_editor_select_skybox(editor);
	} else if (editor->selection_row[1] == 8U && editor->selection_column[1] != 0) {
		track_editor_select_terrain(editor);
	} else if (editor->selection_row[1] == 8U) {
		track_editor_load_track(editor);
	} else if (editor->selection_column[1] == 0) {
		track_editor_save_track(&editor->track_changed, &editor->map_dirty);
	} else {
		track_editor_exit(editor);
	}
}

static void track_editor_place_terrain(struct TRACK_EDITOR_SESSION *editor)
{
	legacy_u16 source_index;

	if (editor->selection_column[0] == editor->last_column &&
		editor->selection_row[0] == editor->last_row) {
		track_editor_swap_tiles(&editor->selected_tile, &editor->saved_tile,
								&editor->palette_dirty);
	} else {
		source_index = LEGACY_U16_WRAP_ADD((legacy_u16)terrainrows[editor->selection_row[0]],
										   editor->selection_column[0]);
		editor->saved_tile = track_terrain_map[source_index];
		editor->last_column = editor->selection_column[0];
		editor->last_row = editor->selection_row[0];
	}
	source_index =
		LEGACY_U16_WRAP_ADD((legacy_u16)terrainrows[editor->last_row], editor->last_column);
	track_terrain_map[source_index] = editor->selected_tile;
	editor->track_changed = 1;
	editor->validate_track = 1;
	editor->map_dirty = 1;
}

static void track_editor_place_element(struct TRACK_EDITOR_SESSION *editor)
{
	editor->multi_tile = (legacy_u8)trkObjectList[editor->selected_tile].ss_multiTileFlag;
	if (((editor->multi_tile & 1U) != 0 && editor->selection_row[0] > 28U) ||
		((editor->multi_tile & 2U) != 0 && editor->selection_column[0] > 28U)) {
		editor->palette_dirty = 1;
		editor->map_dirty = 1;
		return;
	}
	legacy_u16 source_index;
	if (editor->selection_column[0] == editor->last_column &&
		editor->selection_row[0] == editor->last_row) {
		track_editor_swap_tiles(&editor->selected_tile, &editor->saved_tile,
								&editor->palette_dirty);
	} else {
		source_index = LEGACY_U16_WRAP_ADD((legacy_u16)trackrows[editor->selection_row[0]],
										   editor->selection_column[0]);
		editor->saved_tile = track_element_map[source_index];
		if (editor->saved_tile >= TRACK_TILE_CONTINUATION_SOUTHEAST) {
			editor->saved_tile = 0;
		}
		editor->last_column = editor->selection_column[0];
		editor->last_row = editor->selection_row[0];
	}
	source_index =
		LEGACY_U16_WRAP_ADD((legacy_u16)trackrows[editor->last_row], editor->last_column);
	track_element_map[source_index] = editor->selected_tile;
	editor->track_changed = 1;
	editor->validate_track = 1;
	editor->map_dirty = 1;
	if (editor->multi_tile == 1U) {
		track_element_map[LEGACY_U16_WRAP_ADD((legacy_u16)trackrows[editor->last_row + 1U],
											  editor->last_column)] = TRACK_TILE_CONTINUATION_SOUTH;
	} else if (editor->multi_tile == 2U) {
		track_element_map[LEGACY_U16_WRAP_ADD(source_index, 1U)] = TRACK_TILE_CONTINUATION_EAST;
	} else if (editor->multi_tile == 3U) {
		track_element_map[LEGACY_U16_WRAP_ADD(source_index, 1U)] = TRACK_TILE_CONTINUATION_EAST;
		source_index =
			LEGACY_U16_WRAP_ADD((legacy_u16)trackrows[editor->last_row + 1U], editor->last_column);
		track_element_map[source_index] = TRACK_TILE_CONTINUATION_SOUTH;
		track_element_map[LEGACY_U16_WRAP_ADD(source_index, 1U)] =
			TRACK_TILE_CONTINUATION_SOUTHEAST;
	}
}

static void track_editor_move_home(struct TRACK_EDITOR_SESSION *editor)
{
	if (editor->focus != 0) {
		editor->selection_row[1] = 0;
	} else {
		if (editor->selection_column[0] == editor->map_column_offset &&
			editor->selection_row[0] == editor->map_row_offset) {
			editor->map_column_offset = 0;
			editor->map_row_offset = 0;
		}
		editor->selection_column[0] = editor->map_column_offset;
		editor->selection_row[0] = editor->map_row_offset;
	}
}

static void track_editor_move_up(struct TRACK_EDITOR_SESSION *editor)
{
	if (editor->selection_row[editor->focus] != 0) {
		editor->last_column = TRACK_EDITOR_POSITION_UNSET;
		editor->selection_row[editor->focus]--;
		if (editor->focus != 0 && editor->selection_row[1] < 6U) {
			track_editor_skip_previous_placeholders(editor->page, &editor->selection_row[1],
													&editor->selection_column[1]);
		}
	}
}

static void track_editor_move_down(struct TRACK_EDITOR_SESSION *editor)
{
	if (editor->selection_row[editor->focus] < track_editor_maximum_rows[editor->focus]) {
		editor->last_column = TRACK_EDITOR_POSITION_UNSET;
		editor->selection_row[editor->focus]++;
		if (editor->focus != 0 && editor->selection_row[1] < 6U) {
			legacy_u8 value = track_editor_palette_tile(editor->page, editor->selection_row[1],
														editor->selection_column[1]);
			if (value == TRACK_TILE_CONTINUATION_EAST) {
				editor->selection_column[1]--;
			} else if (value == TRACK_TILE_CONTINUATION_SOUTH) {
				editor->selection_row[1]++;
			}
		}
	}
}

static void track_editor_move_left(struct TRACK_EDITOR_SESSION *editor)
{
	if (editor->focus != 0 && editor->selection_row[1] == 6U) {
		if (editor->page > 1U) {
			editor->page--;
		}
	} else if (editor->selection_column[editor->focus] != 0) {
		editor->last_column = TRACK_EDITOR_POSITION_UNSET;
		editor->selection_column[editor->focus]--;
		if (editor->focus != 0) {
			if (editor->selection_row[1] > 5U) {
				editor->selection_column[1] = 0;
			} else {
				track_editor_skip_previous_placeholders(editor->page, &editor->selection_row[1],
														&editor->selection_column[1]);
			}
		}
	}
}

static void track_editor_move_right(struct TRACK_EDITOR_SESSION *editor)
{
	if (editor->focus != 0 && editor->selection_row[1] == 6U) {
		if (editor->page < 10U) {
			editor->page++;
		}
	} else {
		legacy_u8 step = editor->focus != 0 && editor->selection_row[1] > 5U ? 3U : 1U;
		while ((legacy_u16)editor->selection_column[editor->focus] + step <
			   track_editor_maximum_columns[editor->focus]) {
			if (editor->focus == 0 || editor->selection_row[1] > 5U) {
				break;
			}
			legacy_u8 value =
				track_editor_palette_tile(editor->page, editor->selection_row[1],
										  (legacy_u8)(editor->selection_column[1] + step));
			if (value < TRACK_TILE_CONTINUATION_SOUTH) {
				break;
			}
			if (value == TRACK_TILE_CONTINUATION_EAST) {
				step++;
			} else {
				editor->selection_row[1]--;
			}
		}
		if ((legacy_u16)editor->selection_column[editor->focus] + step <
			track_editor_maximum_columns[editor->focus]) {
			editor->last_column = TRACK_EDITOR_POSITION_UNSET;
			editor->selection_column[editor->focus] =
				(legacy_u8)(editor->selection_column[editor->focus] + step);
		}
	}
}

static void track_editor_handle_navigation_key(struct TRACK_EDITOR_SESSION *editor)
{
	if (editor->key == (legacy_u16)'+') {
		if (editor->page < 10U) {
			editor->page++;
		}
	} else if (editor->key == (legacy_u16)'-') {
		if (editor->page > 1U) {
			editor->page--;
		}
	} else if (editor->key == KEY_SHIFT_F1) {
		editor->page = 0;
		editor->selected_tile = 0;
	} else if (editor->key == KEY_HOME) {
		track_editor_move_home(editor);
	} else if (editor->key == KEY_UP) {
		track_editor_move_up(editor);
	} else if (editor->key == KEY_DOWN) {
		track_editor_move_down(editor);
	} else if (editor->key == KEY_LEFT) {
		track_editor_move_left(editor);
	} else if (editor->key == KEY_RIGHT) {
		track_editor_move_right(editor);
	}
}

void track_editor_handle_key(struct TRACK_EDITOR_SESSION *editor)
{
	for (legacy_u16 key_index = 0; key_index < 10U; key_index++) {
		if (editor->key == track_editor_page_keys[key_index]) {
			editor->page = (legacy_u8)(key_index + 1U);
			editor->key = 0;
			break;
		}
	}

	if (editor->key == 'c' || editor->key == 'C') {
		track_editor_check_route(editor);
	} else if (editor->key == KEY_ENTER) {
		if (editor->focus != 0) {
			track_editor_activate_palette(editor);
		} else if (editor->page == 0) {
			track_editor_place_terrain(editor);
		} else {
			track_editor_place_element(editor);
		}
	} else if (editor->key == KEY_SPACE || editor->key == KEY_INSERT) {
		editor->focus ^= 1U;
	} else {
		track_editor_handle_navigation_key(editor);
	}
}
