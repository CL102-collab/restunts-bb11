#include "highscore_internal.h"
#ifdef RESTUNTS_SDL3
#include "opponent/opponent_animation.h"
#endif

static legacy_u32 end_player_finish_time(void)
{
#ifdef RESTUNTS_SDL3
	return extended_highscore_player_finish((legacy_u16)gState_total_finish_time);
#else
	return (legacy_u16)gState_total_finish_time;
#endif
}

static legacy_u32 end_opponent_finish_time(void)
{
#ifdef RESTUNTS_SDL3
	return extended_highscore_opponent_finish((legacy_u16)gState_opponent_finish_time);
#else
	return (legacy_u16)gState_opponent_finish_time;
#endif
}

static legacy_u32 end_penalty_time(void)
{
#ifdef RESTUNTS_SDL3
	return extended_highscore_penalty((legacy_u16)gState_penalty);
#else
	return (legacy_u16)gState_penalty;
#endif
}

static void end_hiscore_draw_animation_frame(legacy_s8 far *animation_resource,
											 legacy_u8 far *frame_sequence, legacy_u8 frame_index,
											 legacy_s16 animation_x, legacy_s16 animation_y,
											 struct SPRITE far *animation_sprite,
											 legacy_u8 draw_direct_copy)
{
	opponent_animation_frame_id[3] = (legacy_s8)(frame_sequence[frame_index] + '0');
	struct SHAPE2D far *frame_shape =
		(struct SHAPE2D far *)locate_shape_fatal(animation_resource, opponent_animation_frame_id);
	mouse_draw_opaque_check();
	if (video_uses_page_flipping != 0) {
		sprite_select_target(animation_sprite);
		shape2d_rle_copy(frame_shape, 0, 0);
		sprite_select_screen_compat();
		/* Preserve resource access order independently of argument evaluation. */
		legacy_u16 height = shape2d_get_height(frame_shape);
		legacy_u16 width = shape2d_get_width(frame_shape);
		sprite_set_target_clip_bounds(
			animation_x,
			LEGACY_S16_WRAP_ADD(animation_x, LEGACY_S16_WRAP_MUL(width, video_shape_width_scale)),
			animation_y, LEGACY_S16_WRAP_ADD(animation_y, height));
		sprite_copy_image_at(animation_sprite->sprite_bitmapptr, animation_x, animation_y);
		sprite_select_screen_compat();
	} else {
		shape2d_rle_copy(frame_shape, animation_x, animation_y);
	}
	if (draw_direct_copy != 0) {
		shape2d_rle_copy(frame_shape, animation_x, animation_y);
	}
	mouse_draw_transparent_check();
}

static void end_hiscore_advance_animation(legacy_s16 delta, legacy_s16 *timer, legacy_u8 *frame,
										  const legacy_u8 far *frame_sequence)
{
	*timer = LEGACY_S16_WRAP_ADD(*timer, delta);
	if (*timer >= END_SCREEN_ANIMATION_FRAME_TICKS) {
		*timer = LEGACY_S16_WRAP_SUB(*timer, END_SCREEN_ANIMATION_FRAME_TICKS);
		(*frame)++;
		if (frame_sequence[*frame] == 0) {
			*frame = 0;
		}
	}
}

static void end_hiscore_update_animation(
	legacy_s16 delta, legacy_s16 *timer, legacy_u8 *frame, legacy_u8 *previous_frame,
	legacy_s8 far *animation_resource, legacy_u8 far *frame_sequence, legacy_s16 animation_x,
	legacy_s16 animation_y, struct SPRITE far *animation_sprite, legacy_u8 draw_direct_copy)
{
	end_hiscore_advance_animation(delta, timer, frame, frame_sequence);
	if (*previous_frame != *frame) {
		*previous_frame = *frame;
		end_hiscore_draw_animation_frame(animation_resource, frame_sequence, *frame, animation_x,
										 animation_y, animation_sprite, draw_direct_copy);
	}
}

static void end_hiscore_initialize(struct END_SCREEN_STATE *screen)
{
	ensure_file_exists(END_SCREEN_TRACK_RESOURCE_INDEX);
	screen->misc_resource = (legacy_s8 far *)file_load_resfile(results_misc_resource_name);
	screen->opponent_resource = 0;
	if (gameconfig.game_opponenttype != 0) {
		opponent_resource_name[3] = (legacy_s8)((legacy_u8)gameconfig.game_opponenttype + '0');
		screen->opponent_resource = (legacy_s8 far *)file_load_resfile(opponent_resource_name);
	}

	render_window_sprite = sprite_make_wnd(END_SCREEN_WIDTH, END_SCREEN_HEIGHT, END_SCREEN_COLOR);
	screen->animation_sprite = 0;
	if (video_uses_page_flipping != 0) {
		screen->animation_sprite = sprite_make_wnd(END_SCREEN_ANIMATION_WIDTH,
												   END_SCREEN_ANIMATION_HEIGHT, END_SCREEN_COLOR);
	}
	screen->blit_mode = MENU_BLIT_MODE_INITIAL;
	sprite_select_render_window_and_clear();
	draw_button(0, 0, 0, END_SCREEN_WIDTH, END_SCREEN_TOP_HEIGHT, button_top_color,
				button_bottom_color, button_fill_color, 0);
	draw_button(0, 0, END_SCREEN_BOTTOM_Y, END_SCREEN_WIDTH, END_SCREEN_BOTTOM_HEIGHT,
				button_top_color, button_bottom_color, button_fill_color, 0);
}

static void end_hiscore_draw_player_time(struct END_SCREEN_STATE *screen)
{
	screen->text_y = END_SCREEN_TEXT_START_Y;
	end_hiscore_set_text(screen->misc_resource, elapsed_time_label_id);
	legacy_s8 number[HIGHSCORE_FORMAT_BUFFER_SIZE];
	if (end_player_finish_time() != 0) {
		end_format_time(number, end_player_finish_time() - end_penalty_time());
		strcat(&resID_byte1, number);
		if (((legacy_u8)replay_recording_flags & REPLAY_RECORDING_MODIFIED_FLAG) != 0) {
			end_hiscore_append_text(screen->misc_resource, continued_race_label_id);
		}
		end_hiscore_draw_current_text(&screen->text_y);
		if (end_penalty_time() != 0) {
			end_hiscore_set_text(screen->misc_resource, penalty_time_label_id);
			end_format_time(number, end_penalty_time());
			strcat(&resID_byte1, number);
			end_hiscore_draw_current_text(&screen->text_y);
		}
	} else {
		end_hiscore_append_text(screen->misc_resource, player_did_not_finish_label_id);
		end_hiscore_draw_current_text(&screen->text_y);
	}
}

static void end_hiscore_draw_opponent_time(struct END_SCREEN_STATE *screen)
{
	screen->outcome = END_SCREEN_OUTCOME_NONE;
	legacy_s8 number[HIGHSCORE_FORMAT_BUFFER_SIZE];
	if (gameconfig.game_opponenttype != 0) {
		if (end_opponent_finish_time() == 0) {
			end_hiscore_set_text(screen->misc_resource, opponent_unfinished_time_label_id);
			end_hiscore_append_text(screen->misc_resource, opponent_did_not_finish_label_id);
			if (end_player_finish_time() != 0) {
				screen->outcome = END_SCREEN_OUTCOME_LOSS;
			}
		} else if (end_player_finish_time() == 0 ||
				   end_opponent_finish_time() < end_player_finish_time()) {
			end_hiscore_set_text(screen->misc_resource, opponent_win_time_label_id);
			end_format_time(number, end_opponent_finish_time());
			strcat(&resID_byte1, number);
			screen->outcome = END_SCREEN_OUTCOME_WIN;
		} else {
			end_hiscore_set_text(screen->misc_resource, opponent_loss_time_label_id);
			end_format_time(number, end_opponent_finish_time());
			strcat(&resID_byte1, number);
			screen->outcome = END_SCREEN_OUTCOME_LOSS;
		}
		end_hiscore_draw_current_text(&screen->text_y);
	}
}

static void end_hiscore_load_music(struct END_SCREEN_STATE *screen)
{
	if (screen->outcome == END_SCREEN_OUTCOME_LOSS) {
		file_load_audiores(victory_music_resource_name, victory_instrument_resource_name,
						   victory_song_id);
	} else {
		file_load_audiores(race_end_music_resource_name, race_end_instrument_resource_name,
						   race_end_song_id);
	}

	screen->opponent_active = (legacy_u8)gameconfig.game_opponenttype;
	if (screen->outcome == END_SCREEN_OUTCOME_NONE && gState_pEndFrame != gState_oEndFrame) {
		screen->opponent_active = 0;
	}
}

static void end_hiscore_draw_statistics(struct END_SCREEN_STATE *screen)
{
	end_hiscore_set_text(screen->misc_resource, average_speed_label_id);
	legacy_u32 duration = LEGACY_U16_WRAP_ADD(gState_pEndFrame, elapsed_time1);
#ifdef RESTUNTS_SDL3
	duration = extended_highscore_duration(duration);
#endif
	legacy_u16 average_speed;
	if (duration != 0) {
		average_speed = (legacy_u16)(LEGACY_U32_DIV_OR_ZERO((legacy_u32)gState_travDist,
															(legacy_u32)duration) >>
									 END_SCREEN_SPEED_FRACTION_BITS);
	} else {
		average_speed = 0;
	}
	legacy_s8 number[HIGHSCORE_FORMAT_BUFFER_SIZE];
	format_integer(number, average_speed, 0, END_SCREEN_NUMBER_WIDTH);
	strcat(&resID_byte1, number);
	end_hiscore_append_text(screen->misc_resource, average_speed_units_id);
	end_hiscore_draw_current_text(&screen->text_y);

	if (gState_impactSpeed != 0) {
		end_hiscore_set_text(screen->misc_resource, impact_speed_label_id);
		format_integer(number, (legacy_u16)gState_impactSpeed >> END_SCREEN_SPEED_FRACTION_BITS, 0,
					   END_SCREEN_NUMBER_WIDTH);
		strcat(&resID_byte1, number);
		end_hiscore_append_text(screen->misc_resource, impact_speed_units_id);
		end_hiscore_draw_current_text(&screen->text_y);
	}

	end_hiscore_set_text(screen->misc_resource, top_speed_label_id);
	format_integer(number, (legacy_u16)gState_topSpeed >> END_SCREEN_SPEED_FRACTION_BITS, 0,
				   END_SCREEN_NUMBER_WIDTH);
	strcat(&resID_byte1, number);
	end_hiscore_append_text(screen->misc_resource, top_speed_units_id);
	end_hiscore_draw_current_text(&screen->text_y);
	if (gState_jumpCount != 0) {
		end_hiscore_set_text(screen->misc_resource, jump_count_label_id);
		format_integer(number, gState_jumpCount, 0, END_SCREEN_NUMBER_WIDTH);
		strcat(&resID_byte1, number);
		hiscore_draw_text(&resID_byte1, font_centered_text_x(&resID_byte1), screen->text_y,
						  dialog_fnt_colour, 0);
	}
}

static void end_hiscore_choose_text_variants(struct END_SCREEN_STATE *screen)
{
	previous_end_opening_variant = end_opening_variant;
	previous_end_outcome_variant = end_outcome_variant;
	previous_end_closing_variant = end_closing_variant;
	legacy_s16 random_value = (legacy_s16)get_super_random();
	end_opening_variant = (legacy_s16)(random_value % END_SCREEN_TEXT_VARIANT_COUNT);
	if (end_opening_variant == previous_end_opening_variant) {
		end_opening_variant = end_text_alternate_variant[(legacy_u16)end_opening_variant];
	}
	random_value = (legacy_s16)get_super_random();
	end_closing_variant = (legacy_s16)(random_value % END_SCREEN_TEXT_VARIANT_COUNT);
	if (end_closing_variant == previous_end_closing_variant) {
		end_closing_variant = end_text_alternate_variant[(legacy_u16)end_closing_variant];
	}

	random_value = (legacy_s16)get_super_random();
	if (screen->outcome == END_SCREEN_OUTCOME_WIN) {
		end_outcome_variant = (legacy_s16)(random_value % END_SCREEN_WIN_VARIANT_COUNT);
		if (end_player_finish_time() != 0) {
			end_outcome_variant =
				LEGACY_S16_WRAP_ADD(end_outcome_variant, END_SCREEN_FINISHED_VARIANT_OFFSET);
		}
	} else {
		end_outcome_variant = (legacy_s16)(random_value % END_SCREEN_OUTCOME_VARIANT_COUNT);
	}
	if (end_outcome_variant == previous_end_outcome_variant) {
		end_outcome_variant = end_outcome_alternate_variant[(legacy_u16)end_outcome_variant];
	}
}

static void end_hiscore_prepare_animation(struct END_SCREEN_STATE *screen)
{
	screen->animation_resource = 0;
	screen->animation_sequence = 0;
	screen->text_prefix = 0;
#ifdef RESTUNTS_SDL3
	hires_highscore_reset_animation(screen);
#endif
	if (screen->opponent_active != 0) {
		if (((legacy_u8)replay_recording_flags & REPLAY_RECORDING_RESTARTABLE_FLAG) == 0) {
			end_hiscore_choose_text_variants(screen);
		}

		if (screen->outcome == END_SCREEN_OUTCOME_WIN) {
			opponent_win_animation_name[3] = (legacy_s8)(screen->opponent_active + '0');
			screen->animation_resource = (legacy_s8 far *)file_load_resource(
				FILE_RESOURCE_SHAPE2D_COLLECTION, opponent_win_animation_name);
#ifdef RESTUNTS_AMIGA
			amiga_video_palette_scene((const legacy_char *)opponent_win_animation_name, "!pal");
#endif
			screen->animation_sequence =
				(legacy_u8 far *)locate_shape_alt(screen->opponent_resource, opponent_win_text_id);
			end_outcome_variant =
				(legacy_s16)(LEGACY_U16_WRAP_ADD(get_kevinrandom(), gState_frame) &
							 END_SCREEN_BINARY_RANDOM_MASK);
			if (end_player_finish_time() != 0) {
				end_outcome_variant =
					LEGACY_S16_WRAP_ADD(end_outcome_variant, END_SCREEN_FINISHED_VARIANT_OFFSET);
			}
			screen->text_prefix = 'v';
		} else {
			opponent_loss_animation_name[3] = (legacy_s8)(screen->opponent_active + '0');
			screen->animation_resource = (legacy_s8 far *)file_load_resource(
				FILE_RESOURCE_SHAPE2D_COLLECTION, opponent_loss_animation_name);
#ifdef RESTUNTS_AMIGA
			amiga_video_palette_scene((const legacy_char *)opponent_loss_animation_name, "!pal");
#endif
			screen->animation_sequence =
				(legacy_u8 far *)locate_shape_alt(screen->opponent_resource, opponent_loss_text_id);
			end_outcome_variant =
				(legacy_s16)(LEGACY_U16_WRAP_ADD(get_kevinrandom(), gState_frame) &
							 END_SCREEN_FOUR_WAY_RANDOM_MASK);
			screen->text_prefix = 'd';
		}
#ifdef RESTUNTS_SDL3
		hires_highscore_load_animation(screen);
#endif
	}
}

static void end_hiscore_validate_track(struct END_SCREEN_STATE *screen)
{
#ifdef RESTUNTS_SDL3
	if (extended_highscore_validate_current(&screen->score_status)) {
		return;
	}
#endif
	screen->score_status = 0;
	file_build_path(track_directory, gameconfig.game_trackname, track_file_extension, g_path_buf);
	legacy_u8 far *track_resource =
		(legacy_u8 far *)file_load_resource(FILE_RESOURCE_BINARY_OPTIONAL, g_path_buf);
	if (track_resource == 0) {
		legacy_u16 result =
			show_dialog(DIALOG_TYPE_ACKNOWLEDGEMENT, DIALOG_SAVE_BACKGROUND,
						locate_text_res(mainresptr, highscore_track_disk_prompt_id),
						DIALOG_AUTO_POSITION, DIALOG_AUTO_POSITION, dialog_border_color, 0, 0);
		if (result != 0) {
			track_resource =
				(legacy_u8 far *)file_load_resource(FILE_RESOURCE_BINARY_OPTIONAL, g_path_buf);
		}
	}
	if (track_resource != 0) {
		for (legacy_u16 i = 0; i < END_SCREEN_TRACK_VALIDATION_BYTES; i++) {
			if (track_resource[i] != track_element_map[i]) {
				screen->score_status = -1;
				break;
			}
		}
		mmgr_release((legacy_s8 far *)track_resource);
	} else {
		screen->score_status = -1;
	}
}

static void end_hiscore_check_score(struct END_SCREEN_STATE *screen)
{
	end_hiscore_validate_track(screen);
	if (screen->score_status == 0 && highscore_load_or_create(0) != 0) {
		if (highscore_load_or_create(1) != 0) {
			screen->score_status = -1;
		}
	}
	screen->finish_time = 0;
#ifdef RESTUNTS_SDL3
	if (extended_highscore_check_current(&screen->score_status, &screen->finish_time)) {
		return;
	}
#endif
	struct HIGHSCORE_ENTRY far *scores;
	if (screen->score_status == 0 && gState_total_finish_time != 0) {
		screen->finish_time = gState_total_finish_time;
		scores = (struct HIGHSCORE_ENTRY far *)track_highscore_table;
		if (((legacy_u8)replay_recording_flags & REPLAY_RECORDING_HIGHSCORE_INELIGIBLE_FLAGS) ==
				0 &&
			highscore_entry_time(&scores[HIGHSCORE_LAST_ENTRY_INDEX]) >
				(legacy_u16)screen->finish_time) {
			screen->score_status = 1;
		}
	}
}

static void end_hiscore_enter_score(struct END_SCREEN_STATE *screen, void far *prompt,
									legacy_u8 car_flag)
{
#ifdef RESTUNTS_SDL3
	if (extended_highscore_enter_current(screen->finish_time, prompt, car_flag)) {
		return;
	}
#endif
	enter_hiscore((legacy_s16)screen->finish_time, prompt, car_flag);
}

static void end_hiscore_show_opponent(struct END_SCREEN_STATE *screen)
{
	opponent_animation_frame_id[3] = '1';
	struct SHAPE2D far *frame_shape = (struct SHAPE2D far *)locate_shape_fatal(
		screen->animation_resource, opponent_animation_frame_id);
	legacy_s16 animation_width =
		LEGACY_S16_WRAP_MUL(shape2d_get_width(frame_shape), video_shape_width_scale);
	screen->animation_x = LEGACY_S16_WRAP_SUB(END_SCREEN_ANIMATION_RIGHT, animation_width);
	legacy_s16 animation_height = (legacy_s16)shape2d_get_height(frame_shape);
	screen->animation_y = LEGACY_S16_WRAP_SUB(END_SCREEN_ANIMATION_BOTTOM, animation_height);
	screen->animation_y = LEGACY_S16_FROM_BITS(
		LEGACY_U16_SAR((legacy_u16)screen->animation_y, END_SCREEN_ANIMATION_CENTER_SHIFT));
	draw_three_color_beveled_border(
		LEGACY_S16_WRAP_SUB(screen->animation_x, END_SCREEN_ANIMATION_BORDER_INSET),
		LEGACY_S16_WRAP_SUB(screen->animation_y, END_SCREEN_ANIMATION_BORDER_INSET),
		LEGACY_S16_WRAP_ADD(animation_width, END_SCREEN_ANIMATION_BORDER_GROWTH),
		LEGACY_S16_WRAP_ADD(shape2d_get_height(frame_shape), END_SCREEN_ANIMATION_BORDER_GROWTH),
		dialog_fnt_colour, 0, end_animation_border_shadow_color);
	opponent_animation_frame_id[3] =
		(legacy_s8)(screen->animation_sequence[screen->animation_frame] + '0');
	shape2d_rle_copy((struct SHAPE2D far *)locate_shape_fatal(screen->animation_resource,
															  opponent_animation_frame_id),
					 screen->animation_x, screen->animation_y);
	screen->previous_animation_frame = screen->animation_frame;
#ifdef RESTUNTS_SDL3
	hires_highscore_show_animation(screen, animation_width, animation_height);
#endif
	font_set_colors(0, 0);
	end_hiscore_draw_opponent_text(screen->opponent_resource, screen->outcome, screen->text_prefix,
								   screen->animation_x);
	screen->evaluation_screen = 0;
}

static void end_hiscore_enter_opponent_score(struct END_SCREEN_STATE *screen)
{
	screen->score_status = 0;
	screen->evaluation_screen = 1;
	draw_button(locate_text_res(screen->misc_resource, result_continue_button_id),
				END_SCREEN_EVALUATION_BUTTON_X, END_SCREEN_BUTTON_Y, END_SCREEN_BUTTON_WIDTH,
				END_SCREEN_BUTTON_HEIGHT, button_top_color, button_bottom_color, button_fill_color,
				0);
	(void)sprite_blit_to_video(render_window_sprite, LEGACY_S8_FROM_BITS(screen->blit_mode));
	screen->blit_mode = MENU_BLIT_MODE_REFRESH;
	menu_reset_animation_timers();
	check_input();
	sprite_select_screen_compat();
	struct BUTTON_AREA menu_areas[END_SCREEN_MENU_AREA_COUNT];
	for (legacy_u16 i = 0; i < END_SCREEN_MENU_AREA_COUNT; i++) {
		menu_areas[i].x1 = result_button_left[i];
		menu_areas[i].x2 = result_button_right[i];
		menu_areas[i].y1 = hiscore_buttons_y1[i];
		menu_areas[i].y2 = hiscore_buttons_y2[i];
	}
	legacy_u16 text_resource_count =
		screen->outcome == END_SCREEN_OUTCOME_NONE ? 1U : END_SCREEN_TEXT_VARIANT_COUNT;
	for (;;) {
		legacy_s16 delta = (legacy_s16)menu_animate_button_highlight(
			END_SCREEN_BUTTON_COUNT, menu_areas, menu_highlight_second_color,
			menu_highlight_first_color);
		end_hiscore_update_animation(delta, &screen->animation_timer, &screen->animation_frame,
									 &screen->previous_animation_frame, screen->animation_resource,
									 screen->animation_sequence, screen->animation_x,
									 screen->animation_y, screen->animation_sprite, 0);
#ifdef RESTUNTS_SDL3
		hires_highscore_update_animation(screen, end_hiscore_draw_animation_frame);
#endif
		legacy_u16 input = (legacy_u16)input_checking((legacy_s16)text_resource_count);
#ifdef RESTUNTS_SDL3
		if (hires_highscore_display_input(input)) {
			continue;
		}
#endif
		if (input == KEY_ENTER || input == KEY_SPACE || input == KEY_ESCAPE) {
			break;
		}
	}

	sprite_select_render_window();
	draw_button(0, 0, 0, END_SCREEN_WIDTH, END_SCREEN_TOP_HEIGHT, button_top_color,
				button_bottom_color, button_fill_color, 0);
	sprite_set_target_clip_bounds(END_SCREEN_TEXT_LEFT, END_SCREEN_ANIMATION_RIGHT,
								  hiscore_buttons_y1[0],
								  LEGACY_S16_WRAP_ADD(hiscore_buttons_y2[0], 1));
	sprite_clear_target(button_fill_color);
	mouse_draw_opaque_check();
	end_hiscore_enter_score(screen,
							locate_text_res(screen->misc_resource, opponent_highscore_prompt_id),
							screen->outcome);
}

static void end_hiscore_draw_evaluation(struct END_SCREEN_STATE *screen)
{
	if (screen->opponent_active != 0 && screen->score_status == 2) {
		screen->score_status = 0;
		sprite_select_render_window();
		highscore_draw_table();
		screen->selected = 1;
		screen->evaluation_screen = 1;
		return;
	}

	if (screen->opponent_active == 0) {
		if (screen->score_status > 0) {
			check_input();
			mouse_draw_opaque_check();
			end_hiscore_enter_score(
				screen, locate_text_res(screen->misc_resource, solo_highscore_prompt_id), 0);
			screen->score_status = 0;
			screen->blit_mode = MENU_BLIT_MODE_REFRESH;
		} else {
			mouse_draw_opaque_check();
			if (screen->score_status == -1) {
				end_hiscore_set_text(screen->misc_resource, highscore_unavailable_message_id);
				hiscore_draw_text(&resID_byte1, font_centered_text_x(&resID_byte1),
								  END_SCREEN_NO_SCORE_MESSAGE_Y, dialog_fnt_colour, 0);
			} else {
				highscore_draw_table();
			}
		}
		return;
	}

	end_hiscore_show_opponent(screen);
	if (screen->score_status > 0) {
		end_hiscore_enter_opponent_score(screen);
	}
}

static void end_hiscore_draw_menu(struct END_SCREEN_STATE *screen)
{
	screen->selected = 1;
	screen->previous_selection = 1;
	menu_reset_animation_timers();
	sprite_select_render_window();
	legacy_s16 menu_offset;
	if (screen->opponent_active == 0 || screen->score_status == -1) {
		menu_offset = END_SCREEN_MENU_OFFSET_WITHOUT_FIRST_BUTTON;
	} else {
		menu_offset = 0;
		draw_button(locate_text_res(screen->misc_resource, screen->evaluation_screen != 0
															   ? result_evaluation_button_id
															   : result_highscore_button_id),
					LEGACY_S16_WRAP_ADD(result_button_left[0], 1), END_SCREEN_BUTTON_Y,
					END_SCREEN_BUTTON_WIDTH, END_SCREEN_BUTTON_HEIGHT, button_top_color,
					button_bottom_color, button_fill_color, 0);
	}
	draw_button(locate_text_res(screen->misc_resource, result_replay_button_id),
				LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_ADD(result_button_left[1], menu_offset), 1),
				END_SCREEN_BUTTON_Y, END_SCREEN_BUTTON_WIDTH, END_SCREEN_BUTTON_HEIGHT,
				button_top_color, button_bottom_color, button_fill_color, 0);
	draw_button(locate_text_res(screen->misc_resource, screen->opponent_active != 0
														   ? result_race_button_id
														   : result_drive_button_id),
				LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_ADD(result_button_left[2], menu_offset), 1),
				END_SCREEN_BUTTON_Y, END_SCREEN_BUTTON_WIDTH, END_SCREEN_BUTTON_HEIGHT,
				button_top_color, button_bottom_color, button_fill_color, 0);
	draw_button(locate_text_res(screen->misc_resource, result_main_menu_button_data),
				LEGACY_S16_WRAP_ADD(LEGACY_S16_WRAP_ADD(result_button_left[3], menu_offset), 1),
				END_SCREEN_BUTTON_Y, END_SCREEN_BUTTON_WIDTH, END_SCREEN_BUTTON_HEIGHT,
				button_top_color, button_bottom_color, button_fill_color, 0);
	for (legacy_u16 i = 0; i < END_SCREEN_BUTTON_COUNT; i++) {
		screen->button_areas[i].x1 = LEGACY_S16_WRAP_ADD(result_button_left[i], menu_offset);
		screen->button_areas[i].x2 = LEGACY_S16_WRAP_ADD(result_button_right[i], menu_offset);
		screen->button_areas[i].y1 = hiscore_buttons_y1[i];
		screen->button_areas[i].y2 = hiscore_buttons_y2[i];
	}
	check_input();
	(void)sprite_blit_to_video(render_window_sprite, LEGACY_S8_FROM_BITS(screen->blit_mode));
	screen->blit_mode = MENU_BLIT_MODE_REFRESH;
	sprite_select_screen_compat();
}

static void end_hiscore_redraw_selection(struct END_SCREEN_STATE *screen)
{
	if (screen->previous_selection != screen->selected) {
		screen->previous_selection = screen->selected;
		sprite_select_screen_compat();
		sprite_set_target_clip_bounds(0, END_SCREEN_WIDTH, hiscore_buttons_y1[0],
									  LEGACY_S16_WRAP_ADD(hiscore_buttons_y2[0], 1));
		mouse_draw_opaque_check();
		sprite_putimage(render_window_sprite->sprite_bitmapptr);
		mouse_draw_transparent_check();
		(void)timer_get_delta_alt();
		menu_reset_animation_timers();
	}
}

static void end_hiscore_select_mouse(struct END_SCREEN_STATE *screen)
{
	legacy_s16 hit;
	if (screen->opponent_active == 0 || screen->score_status == -1) {
		hit = (legacy_s16)mouse_multi_hittest(END_SCREEN_REDUCED_BUTTON_COUNT,
											  &screen->button_areas[1]);
		if (hit != -1) {
			screen->selected = (legacy_u8)(hit + 1);
		}
	} else {
		hit = (legacy_s16)mouse_multi_hittest(END_SCREEN_BUTTON_COUNT, screen->button_areas);
		if (hit != -1) {
			screen->selected = (legacy_u8)hit;
		}
	}
}

static legacy_s16 end_hiscore_navigate_menu(struct END_SCREEN_STATE *screen, legacy_u16 input)
{
	if (input == KEY_LEFT) {
		if (screen->opponent_active == 0 || screen->score_status == -1) {
			screen->selected = screen->selected <= 1 ? END_SCREEN_LAST_BUTTON_INDEX
													 : (legacy_u8)(screen->selected - 1U);
		} else {
			screen->selected = screen->selected == 0 ? END_SCREEN_LAST_BUTTON_INDEX
													 : (legacy_u8)(screen->selected - 1U);
		}
		return 1;
	}
	if (input == KEY_RIGHT) {
		if (screen->selected < END_SCREEN_LAST_BUTTON_INDEX) {
			screen->selected++;
		} else {
			screen->selected =
				(screen->opponent_active == 0 || screen->score_status == -1) ? 1U : 0U;
		}
		return 1;
	}
	return 0;
}

static legacy_s16 end_hiscore_handle_menu_input(struct END_SCREEN_STATE *screen, legacy_u16 input)
{
#ifdef RESTUNTS_SDL3
	if (hires_highscore_display_input(input)) {
		return 0;
	}
#endif
	if (input == 0) {
		return 0;
	}
	if (end_hiscore_navigate_menu(screen, input)) {
		return 0;
	}
	if (input != KEY_ENTER && input != KEY_SPACE) {
		return 0;
	}

	if (screen->selected == 0) {
		sprite_select_render_window();
		draw_button(0, 0, 0, END_SCREEN_WIDTH, END_SCREEN_TOP_HEIGHT, button_top_color,
					button_bottom_color, button_fill_color, 0);
		screen->score_status = screen->evaluation_screen != 0 ? 0 : 2;
		return -1;
	}
	return 1;
}

static legacy_s16 end_hiscore_run_menu(struct END_SCREEN_STATE *screen)
{
	for (;;) {
		end_hiscore_redraw_selection(screen);
		legacy_s16 delta = (legacy_s16)menu_animate_button_highlight(
			screen->selected, screen->button_areas, menu_highlight_second_color,
			menu_highlight_first_color);
		if (screen->evaluation_screen == 0 && screen->outcome != END_SCREEN_OUTCOME_NONE) {
			end_hiscore_update_animation(delta, &screen->animation_timer, &screen->animation_frame,
										 &screen->previous_animation_frame,
										 screen->animation_resource, screen->animation_sequence,
										 screen->animation_x, screen->animation_y,
										 screen->animation_sprite, 1);
#ifdef RESTUNTS_SDL3
			hires_highscore_update_animation(screen, end_hiscore_draw_animation_frame);
#endif
		}
		legacy_u16 input = (legacy_u16)input_checking(delta);
		end_hiscore_select_mouse(screen);
		legacy_s16 result = end_hiscore_handle_menu_input(screen, input);
		if (result != 0) {
			return result;
		}
	}
}

static legacy_u16 end_hiscore_release(struct END_SCREEN_STATE *screen)
{
#ifdef RESTUNTS_SDL3
	opponent_animation_unload();
#endif
	audio_unload();
	if (screen->opponent_active != 0) {
		mmgr_release(screen->animation_resource);
	}
	if (video_uses_page_flipping != 0) {
		sprite_free_wnd(screen->animation_sprite);
	}
	sprite_free_wnd(render_window_sprite);
	if (gameconfig.game_opponenttype != 0) {
		unload_resource(screen->opponent_resource);
	}
	unload_resource(screen->misc_resource);
	return (legacy_u16)(screen->selected - 1U);
}

legacy_u16 end_hiscore(void)
{
#ifdef RESTUNTS_AMIGA
	amiga_video_palette_scene("SDTITL", "!def");
#endif
	struct END_SCREEN_STATE screen;
	end_hiscore_initialize(&screen);
	end_hiscore_draw_player_time(&screen);
	end_hiscore_draw_opponent_time(&screen);
	end_hiscore_load_music(&screen);
	end_hiscore_draw_statistics(&screen);
	end_hiscore_prepare_animation(&screen);
	end_hiscore_check_score(&screen);
	screen.animation_frame = 0;
	screen.animation_timer = END_SCREEN_ANIMATION_FRAME_TICKS;
	screen.evaluation_screen = 1;
	for (;;) {
		end_hiscore_draw_evaluation(&screen);
		end_hiscore_draw_menu(&screen);
		if (end_hiscore_run_menu(&screen) > 0) {
			return end_hiscore_release(&screen);
		}
	}
}
