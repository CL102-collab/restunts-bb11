#include "menu_track_editor_internal.h"

legacy_u8 far *progress_box_shape;

legacy_s8 track_editor_terrain_shape_names[] =
	"flatlakelak1lak2lak3lak4highgoungouwgousgouegou1gou2gou3gou4gou5gou6gou7gou8";

legacy_s8 track_editor_cursor_shape_names[] = "crs0crs1crs2crs3";

legacy_s8 track_editor_under_cursor_shape_names[] = "ucr0ucr1ucr2ucr3";

const legacy_s8 track_editor_error_resource_ids[] = "eokenseieemseedewwefuenpestejsejdeteewaefteat";

const struct BUTTON_AREA track_editor_buttons[5] = {{9, 199, 181, 187},
													{202, 206, 4, 179},
													{220, 315, 132, 139},
													{8, 199, 4, 179},
													{220, 315, 36, 187}};

const legacy_u16 track_editor_page_keys[10] = {KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5,
											   KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10};

const legacy_u8 track_editor_maximum_columns[2] = {30, 6};

const legacy_u8 track_editor_maximum_rows[2] = {29, 9};

void preRender_icons(legacy_u8 page)
{
	for (legacy_u16 row = 0; row < 6U; row++) {
		for (legacy_u16 column = 0; column < 6U; column++) {
			legacy_u8 tile = progress_box_shape[(legacy_u16)page * 36U + row * 6U + column];
			legacy_u16 x = LEGACY_U16_WRAP_ADD(TRACK_EDITOR_PALETTE_LEFT,
											   LEGACY_U16_WRAP_MUL(column, TRACK_EDITOR_TILE_SIZE));
			legacy_u16 y = LEGACY_U16_WRAP_ADD(TRACK_EDITOR_PALETTE_TOP,
											   LEGACY_U16_WRAP_MUL(row, TRACK_EDITOR_TILE_SIZE));
			if (page == 0) {
				sprite_shape_to_1(track_editor_terrain_shapes[tile], x, y);
				continue;
			}
			if (tile >= TRACK_TILE_CONTINUATION_SOUTHEAST) {
				continue;
			}

			sprite_shape_to_1(track_editor_terrain_shapes[0], x, y);
			legacy_u8 multi_tile = trkObjectList[tile].ss_multiTileFlag;
			if (multi_tile == 1U || multi_tile == 3U) {
				sprite_shape_to_1(track_editor_terrain_shapes[0], x, LEGACY_U16_WRAP_ADD(y, 16U));
			}
			if (multi_tile == 2U || multi_tile == 3U) {
				sprite_shape_to_1(track_editor_terrain_shapes[0], LEGACY_U16_WRAP_ADD(x, 16U), y);
			}
			if (multi_tile == 3U) {
				sprite_shape_to_1(track_editor_terrain_shapes[0], LEGACY_U16_WRAP_ADD(x, 16U),
								  LEGACY_U16_WRAP_ADD(y, 16U));
			}
			putpixel_iconMask(track_editor_tile_masks[tile], x, y);
			putpixel_iconFillings(track_editor_tile_shapes[tile], x, y);
		}
	}
}

static void track_editor_load_resources(struct TRACK_EDITOR_SESSION *editor)
{
	editor->shape_resource = (legacy_s8 far *)file_load_shape2d_fatal("sdtedit");
	locate_many_resources(editor->shape_resource, track_editor_terrain_shape_names,
						  (legacy_s8 far **)track_editor_terrain_shapes);
	locate_many_resources(editor->shape_resource, track_editor_cursor_shape_names,
						  (legacy_s8 far **)track_editor_cursor_shapes);
	locate_many_resources(editor->shape_resource, track_editor_under_cursor_shape_names,
						  (legacy_s8 far **)track_editor_under_cursor_shapes);
	for (legacy_u16 index = 0; index < 4U; index++) {
		editor->cursor_sprites[index] = sprite_make_wnd(
			LEGACY_U16_WRAP_MUL(shape2d_get_width(track_editor_cursor_shapes[index]),
								(legacy_u16)video_shape_width_scale),
			shape2d_get_height(track_editor_cursor_shapes[index]), TRACK_EDITOR_TRANSPARENT_COLOR);
	}

	editor->text_resource = (legacy_s8 far *)file_load_resfile("tedit");
	render_window_sprite = sprite_make_wnd(TRACK_EDITOR_SCREEN_WIDTH, TRACK_EDITOR_SCREEN_HEIGHT,
										   TRACK_EDITOR_TRANSPARENT_COLOR);
	progress_box_shape = (legacy_u8 far *)locate_shape_alt(editor->text_resource, "pbox");
	legacy_s8 far *shape_name_resource = locate_shape_alt(editor->text_resource, "snam");
	legacy_s8 far *mask_name_resource = locate_shape_alt(editor->text_resource, "mnam");
	editor->text_name_resource = locate_shape_alt(editor->text_resource, "tnam");

	for (legacy_u16 index = 0; index < 132U; index++) {
		editor->cached_track[index] = TRACK_EDITOR_CACHE_INVALID;
		editor->cached_terrain[index] = TRACK_EDITOR_CACHE_INVALID;
	}
	for (legacy_u16 index = 0; index < 186U; index++) {
		__fmemcpy(&resID_byte1, shape_name_resource + index * 4U, 4U);
		track_editor_tile_shapes[index] =
			(struct SHAPE2D far *)locate_shape_fatal(editor->shape_resource, &resID_byte1);
		__fmemcpy(&resID_byte1, mask_name_resource + index * 4U, 4U);
		track_editor_tile_masks[index] =
			(struct SHAPE2D far *)locate_shape_fatal(editor->shape_resource, &resID_byte1);
	}
}

static void track_editor_initialize_session(struct TRACK_EDITOR_SESSION *editor)
{
	editor->last_column = TRACK_EDITOR_POSITION_UNSET;
	editor->last_row = TRACK_EDITOR_POSITION_UNSET;
	editor->saved_tile = 0;
	editor->selected_tile = 0;
	editor->previous_page = TRACK_EDITOR_PAGE_UNSET;
	editor->map_dirty = 1;
	editor->palette_dirty = 1;
	editor->scrollbars_dirty = 1;
	editor->validate_track = 1;
	editor->validation_error = TRACK_EDITOR_VALIDATION_OK;
	editor->track_changed = 0;
	editor->menu_active = 1;
	editor->blit_mode = MENU_BLIT_MODE_INITIAL;
	editor->page = 1;
	editor->selection_column[0] = track_validation_column;
	editor->selection_row[0] = track_validation_row;
	editor->selection_column[1] = 0;
	editor->selection_row[1] = 7;
	editor->map_column_offset = 0;
	editor->map_row_offset = 0;
	editor->previous_column_offset = TRACK_EDITOR_POSITION_UNSET;
	editor->previous_row_offset = TRACK_EDITOR_POSITION_UNSET;
	editor->previous_hovered_tile = TRACK_EDITOR_HOVER_UNSET;
	editor->previous_label_width = 0;
	editor->focus = 0;
	editor->path_animation_index = 0;
}

static void track_editor_release_resources(struct TRACK_EDITOR_SESSION *editor)
{
	sprite_free_wnd(render_window_sprite);
	for (legacy_u16 index = 4U; index != 0; index--) {
		sprite_free_wnd(editor->cursor_sprites[index - 1U]);
	}
	unload_resource(editor->text_resource);
	mmgr_free(editor->shape_resource);
}

void load_tracks_menu_shapes(void)
{
#ifdef RESTUNTS_AMIGA
	amiga_video_palette_scene("SDTITL", "!def");
#endif
	struct TRACK_EDITOR_SESSION session;
	struct TRACK_EDITOR_SESSION *editor = &session;

	track_editor_load_resources(editor);
	track_editor_initialize_session(editor);
	track_editor_draw_window(editor);
	while (editor->menu_active != 0) {
		track_editor_update_palette(editor);

		if (editor->validate_track != 0) {
			editor->validate_track = 0;
			editor->validation_error = (legacy_u8)track_editor_remove_invalid_terrain_tiles();
		}

		track_editor_redraw(editor);
		track_editor_position_cursor(editor);
		track_editor_draw_tile_label(editor);
		track_editor_report_validation(editor);
		track_editor_wait_for_input(editor);

		if (editor->path_animation_index != 0) {
			track_editor_advance_path_animation(editor);
			continue;
		}

		track_editor_handle_key(editor);

		if (editor->menu_active != 0) {
			editor->palette_dirty = 1;
			editor->map_dirty = 1;
		}
	}

	track_editor_release_resources(editor);
}
