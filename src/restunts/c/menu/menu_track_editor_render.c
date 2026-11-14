#include "menu_track_editor_internal.h"
#include "../video/video_color.h"

void track_editor_toggle_highlight(legacy_s16 x, legacy_s16 y, legacy_u8 width, legacy_u8 height)
{
	sprite_xor_rect_outline(x, LEGACY_S16_WRAP_SUB(y, 1), LEGACY_S16_WRAP_ADD(x, width),
							LEGACY_S16_WRAP_SUB(LEGACY_S16_WRAP_ADD(y, height), 1),
							VIDEO_UI_COLOR(track_editor_highlight_color));
}

void track_editor_draw_window(struct TRACK_EDITOR_SESSION *editor)
{
	sprite_select_render_window_and_clear();
	draw_button(locate_text_res(editor->text_resource, "bti"), TRACK_EDITOR_TITLE_X,
				TRACK_EDITOR_TITLE_Y, TRACK_EDITOR_TITLE_WIDTH, TRACK_EDITOR_TITLE_HEIGHT,
				VIDEO_UI_COLOR(button_top_color), VIDEO_UI_COLOR(button_bottom_color),
				VIDEO_UI_COLOR(button_fill_color), 0);
	draw_three_color_beveled_border(TRACK_EDITOR_LEFT_FRAME_X, TRACK_EDITOR_LEFT_FRAME_Y,
									TRACK_EDITOR_LEFT_FRAME_WIDTH, TRACK_EDITOR_LEFT_FRAME_HEIGHT,
									VIDEO_UI_VALUE(track_editor_frame_outer_color, 11),
									VIDEO_UI_VALUE(track_editor_frame_inner_color, 9),
									VIDEO_UI_VALUE(track_editor_frame_opposite_color, 1));
	draw_three_color_beveled_border(TRACK_EDITOR_RIGHT_FRAME_X, TRACK_EDITOR_RIGHT_FRAME_Y,
									TRACK_EDITOR_RIGHT_FRAME_WIDTH, TRACK_EDITOR_RIGHT_FRAME_HEIGHT,
									VIDEO_UI_VALUE(track_editor_frame_outer_color, 11),
									VIDEO_UI_VALUE(track_editor_frame_inner_color, 9),
									VIDEO_UI_VALUE(track_editor_frame_opposite_color, 1));
	draw_button(locate_text_res(editor->text_resource, "bsc"), TRACK_EDITOR_WIDE_BUTTON_X,
				TRACK_EDITOR_WIDE_BUTTON_Y, TRACK_EDITOR_WIDE_BUTTON_WIDTH,
				TRACK_EDITOR_BUTTON_HEIGHT, VIDEO_UI_COLOR(button_top_color),
				VIDEO_UI_COLOR(button_bottom_color), VIDEO_UI_COLOR(button_fill_color), 0);
	draw_button(locate_text_res(editor->text_resource, "blo"), TRACK_EDITOR_BUTTON_LEFT_X,
				TRACK_EDITOR_BUTTON_UPPER_Y, TRACK_EDITOR_BUTTON_WIDTH, TRACK_EDITOR_BUTTON_HEIGHT,
				VIDEO_UI_COLOR(button_top_color), VIDEO_UI_COLOR(button_bottom_color),
				VIDEO_UI_COLOR(button_fill_color), 0);
	draw_button(locate_text_res(editor->text_resource, "bsa"), TRACK_EDITOR_BUTTON_LEFT_X,
				TRACK_EDITOR_BUTTON_LOWER_Y, TRACK_EDITOR_BUTTON_WIDTH, TRACK_EDITOR_BUTTON_HEIGHT,
				VIDEO_UI_COLOR(button_top_color), VIDEO_UI_COLOR(button_bottom_color),
				VIDEO_UI_COLOR(button_fill_color), 0);
	draw_button(locate_text_res(editor->text_resource, "bcl"), TRACK_EDITOR_BUTTON_RIGHT_X,
				TRACK_EDITOR_BUTTON_UPPER_Y, TRACK_EDITOR_BUTTON_WIDTH, TRACK_EDITOR_BUTTON_HEIGHT,
				VIDEO_UI_COLOR(button_top_color), VIDEO_UI_COLOR(button_bottom_color),
				VIDEO_UI_COLOR(button_fill_color), 0);
	draw_button(locate_text_res(editor->text_resource, "bex"), TRACK_EDITOR_BUTTON_RIGHT_X,
				TRACK_EDITOR_BUTTON_LOWER_Y, TRACK_EDITOR_BUTTON_WIDTH, TRACK_EDITOR_BUTTON_HEIGHT,
				VIDEO_UI_COLOR(button_top_color), VIDEO_UI_COLOR(button_bottom_color),
				VIDEO_UI_COLOR(button_fill_color), 0);
}

static void track_editor_keep_selection_visible(struct TRACK_EDITOR_SESSION *editor)
{
	if (editor->selection_column[0] == 29U && editor->tile_width == 2U) {
		editor->selection_column[0]--;
	}
	if (editor->selection_row[0] == 29U && editor->tile_height == 2U) {
		editor->selection_row[0]--;
	}
	while ((legacy_s16)(editor->selection_column[0] - editor->map_column_offset +
						editor->tile_width) > 12) {
		editor->map_column_offset++;
	}
	while (editor->selection_column[0] < editor->map_column_offset) {
		editor->map_column_offset--;
	}
	while ((legacy_s16)(editor->selection_row[0] - editor->map_row_offset + editor->tile_height) >
		   11) {
		editor->map_row_offset++;
	}
	while (editor->selection_row[0] < editor->map_row_offset) {
		editor->map_row_offset--;
	}
	if (editor->map_column_offset != editor->previous_column_offset ||
		editor->map_row_offset != editor->previous_row_offset) {
		editor->previous_column_offset = editor->map_column_offset;
		editor->previous_row_offset = editor->map_row_offset;
		editor->map_dirty = 1;
		editor->scrollbars_dirty = 1;
	}
}

static void track_editor_draw_palette_page(struct TRACK_EDITOR_SESSION *editor)
{
	editor->palette_dirty = 1;
	editor->previous_page = editor->page;
	while (editor->selection_row[1] < 6U &&
		   track_editor_palette_tile(editor->page, editor->selection_row[1],
									 editor->selection_column[1]) >=
			   TRACK_TILE_CONTINUATION_SOUTH) {
		legacy_u8 value = track_editor_palette_tile(editor->page, editor->selection_row[1],
													editor->selection_column[1]);
		if (value == TRACK_TILE_CONTINUATION_EAST) {
			editor->selection_column[1]--;
		} else {
			editor->selection_row[1]--;
		}
	}
	sprite_select_render_window();
	preRender_icons(editor->page);
	if (editor->page == 0) {
		scrollbar_update(0, TRACK_EDITOR_PAGE_BAR_X, TRACK_EDITOR_PAGE_BAR_WIDTH,
						 TRACK_EDITOR_PAGE_BAR_Y, 5, 0, 1, 1);
	} else {
		scrollbar_update(0, TRACK_EDITOR_PAGE_BAR_X, TRACK_EDITOR_PAGE_BAR_WIDTH,
						 TRACK_EDITOR_PAGE_BAR_Y, 5, editor->page - 1U, 1, 10);
	}
}

void track_editor_update_palette(struct TRACK_EDITOR_SESSION *editor)
{
	if (editor->palette_dirty != 0 || editor->page != editor->previous_page) {
		editor->tile_width = 1;
		editor->tile_height = 1;
		editor->multi_tile = 0;
		if (editor->page != 0) {
			editor->multi_tile = (legacy_u8)trkObjectList[editor->selected_tile].ss_multiTileFlag;
			if (editor->multi_tile == 1U) {
				editor->tile_height = 2;
				editor->multi_tile = 1;
			} else if (editor->multi_tile == 2U) {
				editor->tile_width = 2;
				editor->multi_tile = 2;
			} else if (editor->multi_tile == 3U) {
				editor->tile_width = 2;
				editor->tile_height = 2;
				editor->multi_tile = 3;
			}
		}

		if (editor->focus == 0) {
			track_editor_keep_selection_visible(editor);
		}

		if (editor->page != editor->previous_page) {
			track_editor_draw_palette_page(editor);
		}
	}
}

void track_editor_redraw(struct TRACK_EDITOR_SESSION *editor)
{
	if (editor->map_dirty != 0 || editor->palette_dirty != 0) {
		sprite_select_render_window();
		if (editor->map_dirty != 0) {
			editor->map_dirty = 0;
			if (editor->scrollbars_dirty != 0) {
				editor->scrollbars_dirty = 0;
				scrollbar_update(0, TRACK_EDITOR_HORIZONTAL_SCROLLBAR_X, TRACK_EDITOR_LABEL_Y,
								 TRACK_EDITOR_HORIZONTAL_SCROLLBAR_LENGTH, 5,
								 editor->map_column_offset, 12, 30);
				scrollbar_update(0, TRACK_EDITOR_VERTICAL_SCROLLBAR_X, 5, 4,
								 TRACK_EDITOR_VERTICAL_SCROLLBAR_LENGTH, editor->map_row_offset, 11,
								 30);
			}
			sprite_set_target_clip_bounds(TRACK_EDITOR_MAP_LEFT, TRACK_EDITOR_MAP_CLIP_RIGHT,
										  TRACK_EDITOR_MAP_TOP, TRACK_EDITOR_MAP_CLIP_BOTTOM);
			draw_2DtrackMap(editor->map_column_offset, editor->map_row_offset, editor->cached_track,
							editor->cached_terrain);
			sprite_set_target_clip_bounds(0, TRACK_EDITOR_SCREEN_WIDTH, 0,
										  TRACK_EDITOR_SCREEN_HEIGHT);
		}

		if (editor->palette_dirty != 0) {
			editor->palette_dirty = 0;
			sprite_select_target(editor->cursor_sprites[editor->multi_tile]);
			if (editor->page == 0) {
				sprite_shape_to_1(track_editor_terrain_shapes[editor->selected_tile], 0, 0);
				preRender_line(1, 0, TRACK_EDITOR_TILE_PREVIEW_RIGHT, 0,
							   VIDEO_UI_COLOR(performGraphColor));
				preRender_line(1, TRACK_EDITOR_TILE_PREVIEW_BOTTOM, TRACK_EDITOR_TILE_PREVIEW_RIGHT,
							   TRACK_EDITOR_TILE_PREVIEW_BOTTOM, VIDEO_UI_COLOR(performGraphColor));
				preRender_line(1, 0, 1, TRACK_EDITOR_TILE_PREVIEW_BOTTOM,
							   VIDEO_UI_COLOR(performGraphColor));
				preRender_line(TRACK_EDITOR_TILE_PREVIEW_RIGHT, 0, TRACK_EDITOR_TILE_PREVIEW_RIGHT,
							   TRACK_EDITOR_TILE_PREVIEW_BOTTOM, VIDEO_UI_COLOR(performGraphColor));
			} else {
				sprite_shape_to_1(track_editor_cursor_shapes[editor->multi_tile], 0, 0);
				if (editor->selected_tile != 0) {
					putpixel_iconMask(track_editor_tile_masks[editor->selected_tile], 0, 0);
					putpixel_iconFillings(track_editor_tile_shapes[editor->selected_tile], 0, 0);
				}
			}
		}

		sprite_blit_to_video(render_window_sprite, LEGACY_S8_FROM_BITS(editor->blit_mode));
		editor->blit_mode = MENU_BLIT_MODE_REFRESH;
		editor->previous_hovered_tile = TRACK_EDITOR_HOVER_UNSET;
	}
}

void track_editor_position_cursor(struct TRACK_EDITOR_SESSION *editor)
{
	sprite_select_screen_compat();
	if (editor->focus == 0) {
		editor->cursor_width = (legacy_u8)(editor->tile_width << 4);
		editor->cursor_height = (legacy_u8)(editor->tile_height << 4);
		editor->cursor_x =
			(legacy_s16)((editor->selection_column[0] - editor->map_column_offset) * 16U + 8U);
		editor->cursor_y =
			(legacy_s16)((editor->selection_row[0] - editor->map_row_offset) * 16U + 4U);
		editor->hovered_tile =
			track_editor_map_tile(editor->selection_column[0], editor->selection_row[0]);
	} else {
		editor->cursor_width = TRACK_EDITOR_TILE_SIZE;
		editor->cursor_height = TRACK_EDITOR_TILE_SIZE;
		editor->cursor_y = (legacy_s16)(editor->selection_row[1] * TRACK_EDITOR_TILE_SIZE +
										TRACK_EDITOR_PALETTE_TOP);
		if (editor->selection_row[1] == 6U) {
			editor->cursor_x = TRACK_EDITOR_PALETTE_LEFT;
			editor->cursor_width = TRACK_EDITOR_PALETTE_WIDTH;
			editor->cursor_height = 8U;
		} else if (editor->selection_row[1] == 7U) {
			editor->selection_column[1] = 0;
			editor->cursor_x = TRACK_EDITOR_PALETTE_LEFT;
			editor->cursor_width = TRACK_EDITOR_PALETTE_WIDTH;
			editor->cursor_y = LEGACY_S16_WRAP_SUB(editor->cursor_y, 8);
		} else if (editor->selection_row[1] > 7U) {
			editor->cursor_y = LEGACY_S16_WRAP_SUB(editor->cursor_y, 8);
			editor->selection_column[1] = editor->selection_column[1] < 3U ? 0U : 3U;
			editor->cursor_x = (legacy_s16)(editor->selection_column[1] * TRACK_EDITOR_TILE_SIZE +
											TRACK_EDITOR_PALETTE_LEFT);
			editor->cursor_width = TRACK_EDITOR_PALETTE_ACTION_WIDTH;
		} else {
			editor->cursor_x = (legacy_s16)(editor->selection_column[1] * TRACK_EDITOR_TILE_SIZE +
											TRACK_EDITOR_PALETTE_LEFT);
			if (track_editor_palette_tile(editor->page, editor->selection_row[1],
										  editor->selection_column[1] + 6U) ==
				TRACK_TILE_CONTINUATION_SOUTH) {
				editor->cursor_height = TRACK_EDITOR_MULTITILE_CURSOR_SIZE;
			}
			if (editor->selection_column[1] < 5U &&
				track_editor_palette_tile(editor->page, editor->selection_row[1],
										  editor->selection_column[1] + 1U) ==
					TRACK_TILE_CONTINUATION_EAST) {
				editor->cursor_width = TRACK_EDITOR_MULTITILE_CURSOR_SIZE;
			}
		}
		editor->hovered_tile =
			editor->selection_row[1] < 6U
				? track_editor_palette_tile(editor->page, editor->selection_row[1],
											editor->selection_column[1])
				: 0;
		if (editor->hovered_tile >= TRACK_TILE_CONTINUATION_SOUTHEAST || editor->page == 0) {
			editor->hovered_tile = 0;
		}
	}
}

void track_editor_draw_tile_label(struct TRACK_EDITOR_SESSION *editor)
{
	if (editor->hovered_tile != editor->previous_hovered_tile) {
		mouse_draw_opaque_check();
		font_set_colors(VIDEO_UI_COLOR(dialog_fnt_colour), 0);
		legacy_s8 *resource_id = &resID_byte1;
		__fmemcpy(resource_id, editor->text_name_resource + (legacy_u16)editor->hovered_tile * 3U,
				  3U);
		resource_id[3] = 0;
		legacy_s8 far *text = locate_text_res(editor->text_resource, resource_id);
		copy_string(resource_id, text);
		legacy_s16 label_width = (legacy_s16)font_text_width(resource_id);
		font_draw_text_opaque(resource_id, TRACK_EDITOR_MAP_LEFT, TRACK_EDITOR_LABEL_Y);
		if (editor->previous_label_width > label_width) {
			sprite_fill_rect(LEGACY_S16_WRAP_ADD(label_width, TRACK_EDITOR_MAP_LEFT),
							 TRACK_EDITOR_LABEL_Y,
							 LEGACY_S16_WRAP_SUB(editor->previous_label_width, label_width), 8, 0);
		}
		mouse_draw_transparent_check();
		editor->previous_label_width = label_width;
		editor->previous_hovered_tile = editor->hovered_tile;
	}
}

void track_editor_report_validation(struct TRACK_EDITOR_SESSION *editor)
{
	if (editor->validation_error != TRACK_EDITOR_VALIDATION_OK) {
		legacy_s8 *resource_id = (legacy_s8 *)track_editor_error_resource_ids +
								 (legacy_u16)editor->validation_error * 3U;
		__fmemcpy(&resID_byte1, resource_id, 3U);
		*(&resID_byte1 + 3) = 0;
		track_editor_show_message(editor->text_resource, &resID_byte1);
		editor->validation_error = TRACK_EDITOR_VALIDATION_OK;
	}
}
