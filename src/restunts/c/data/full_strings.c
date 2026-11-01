#include "../externs.h"
#include "../menu/menu_internal.h"
#include "../car/car_model.h"
#include "../scene_resources.h"
#include "../ui/ui_dialog_internal.h"
#include "../replay/replay_viewer_internal.h"

/* Exact resource identifiers and messages formerly stored in dseg.asm. */

legacy_s8 car_graph_zero_label[4] = {' ', ' ', '0', 0};

legacy_s8 car_graph_time_labels[10] = {'0', ' ', ' ', '2', '0', ' ', ' ', '4', '0', 0};

legacy_s8 car_graph_hundred_label[4] = {'1', '0', '0', 0};

legacy_s8 car_graph_one_fifty_label[4] = {'1', '5', '0', 0};

legacy_s8 car_graph_fifty_label[4] = {' ', '5', '0', 0};

legacy_s8 credits_shape_ids[45] = {'a', 'r', 'o', 'w', 'a', 'r', 'r', 'w', 'a', 'r', 'w', '1',
								   'a', 'r', 'w', '2', 'a', 'r', 'w', '3', 'a', 'r', 'w', '4',
								   'a', 'r', 'w', '5', 'a', 'r', 'w', '6', 'a', 'r', 'w', '7',
								   'a', 'r', 'w', '8', 't', 'y', 'p', 'e', 0};

legacy_s8 credits_art_heading_id[4] = {'a', 'r', 't', 0};

legacy_s8 average_speed_label_id[4] = {'a', 'v', 's', 0};

/*
The table contains track and scenery shape identifiers
such as bridges, tunnels, roads, trees, barriers, and explosion shapes.
*/
legacy_s8 game_shape_names[580] = {
	'b', 'a', 'r', 'n', 0, 'z', 'b', 'r', 'n', 0, 'b', 'r', 'i', 'd', 0, 'z', 'b', 'r', 'i', 0,
	'b', 't', 'u', 'r', 0, 'z', 'b', 't', 'u', 0, 'c', 'h', 'i', '1', 0, 'z', 'c', 'h', '1', 0,
	'c', 'h', 'i', '2', 0, 'z', 'c', 'h', '2', 0, 'e', 'l', 'r', 'd', 0, 'z', 'e', 'l', 'r', 0,
	'f', 'i', 'n', 'i', 0, 'z', 'f', 'i', 'n', 0, 'g', 'a', 's', 's', 0, 'z', 'g', 'a', 's', 0,
	'l', 'b', 'a', 'n', 0, 'z', 'l', 'b', 'a', 0, 'l', 'o', 'o', 'p', 0, 'z', 'l', 'o', 'o', 0,
	'o', 'f', 'f', 'i', 0, 'z', 'o', 'f', 'f', 0, 'p', 'i', 'p', 'e', 0, 'z', 'p', 'i', 'p', 0,
	'r', 'a', 'm', 'p', 0, 'z', 'r', 'a', 'm', 0, 'r', 'b', 'a', 'n', 0, 'z', 'r', 'b', 'a', 0,
	'r', 'd', 'u', 'p', 0, 'z', 'r', 'd', 'u', 0, 'r', 'o', 'a', 'd', 0, 'z', 'r', 'o', 'a', 0,
	's', 't', 'u', 'r', 0, 'z', 's', 't', 'u', 0, 't', 'e', 'n', 'n', 0, 'z', 't', 'e', 'n', 0,
	't', 'u', 'n', 'n', 0, 'z', 't', 'u', 'n', 0, 't', 'u', 'r', 'n', 0, 'z', 't', 'u', 'r', 0,
	'g', 'o', 'u', 'i', 0, 'g', 'o', 'u', 'o', 0, 'g', 'o', 'u', 'p', 0, 'h', 'i', 'g', 'h', 0,
	'l', 'a', 'k', 'c', 0, 'l', 'a', 'k', 'e', 0, 'c', 'l', 'd', '1', 0, 'c', 'l', 'd', '2', 0,
	'c', 'l', 'd', '3', 0, 's', 'i', 'g', 'l', 0, 's', 'i', 'g', 'r', 0, 't', 'r', 'e', 'e', 0,
	'i', 'n', 't', 'e', 0, 'z', 'i', 'n', 't', 0, 'o', 'f', 'f', 'l', 0, 'z', 'o', 'f', 'l', 0,
	'o', 'f', 'f', 'r', 0, 'z', 'o', 'f', 'r', 0, 'p', 'a', 'l', 'm', 0, 'z', 'p', 'a', 'l', 0,
	'b', 'a', 'n', 'k', 0, 'z', 'b', 'a', 'n', 0, 's', 'o', 'f', 'l', 0, 'z', 's', 'o', 'l', 0,
	's', 'o', 'f', 'r', 0, 'z', 's', 'o', 'r', 0, 's', 'r', 'a', 'm', 0, 'z', 's', 'r', 'a', 0,
	's', 'e', 'l', 'r', 0, 'z', 's', 'e', 'r', 0, 'e', 'l', 's', 'p', 0, 'z', 'e', 's', 'p', 0,
	'c', 'a', 'c', 't', 0, 'c', 'a', 'c', 't', 0, 's', 'p', 'i', 'p', 0, 'z', 's', 'p', 'i', 0,
	's', 'e', 's', 't', 0, 'z', 's', 'e', 's', 0, 'w', 'r', 'o', 'a', 0, 'z', 'w', 'r', 'o', 0,
	'b', 'a', 'r', 'r', 0, 'z', 'b', 'a', 'r', 0, 'l', 'c', 'o', '0', 0, 'z', 'l', 'c', 'o', 0,
	'r', 'c', 'o', '0', 0, 'z', 'r', 'c', 'o', 0, 'g', 'w', 'r', 'o', 0, 'z', 'g', 'w', 'r', 0,
	'l', 'c', 'o', '1', 0, 'r', 'c', 'o', '1', 0, 'l', 'o', 'o', '1', 0, 'h', 'i', 'g', '1', 0,
	'h', 'i', 'g', '2', 0, 'h', 'i', 'g', '3', 0, 'w', 'i', 'n', 'd', 0, 'z', 'w', 'i', 'n', 0,
	'b', 'o', 'a', 't', 0, 'z', 'b', 'o', 'a', 0, 'r', 'e', 's', 't', 0, 'z', 'r', 'e', 's', 0,
	'h', 'p', 'i', 'p', 0, 'z', 'h', 'p', 'i', 0, 'v', 'c', 'o', 'r', 0, 'z', 'v', 'c', 'o', 0,
	't', 'u', 'n', '2', 0, 'p', 'i', 'p', '2', 0, 'f', 'e', 'n', 'c', 0, 'z', 'f', 'e', 'n', 0,
	'c', 'f', 'e', 'n', 0, 'z', 'c', 'f', 'e', 0, 'f', 'l', 'a', 'g', 0, 't', 'r', 'u', 'k', 0,
	'e', 'x', 'p', '0', 0, 'e', 'x', 'p', '1', 0, 'e', 'x', 'p', '2', 0, 'e', 'x', 'p', '3', 0};

legacy_s8 car_automatic_button_id[4] = {'b', 'a', 'u', 0};

legacy_s8 car_automatic_toggle_id[4] = {'b', 'a', 'u', 0};

legacy_s8 opponent_car_button_id[4] = {'b', 'c', 'a', 0};

legacy_s8 opponent_none_button_id[4] = {'b', 'c', 'l', 0};

legacy_s8 car_color_button_id[4] = {'b', 'c', 'o', 0};

legacy_s8 result_continue_button_id[4] = {'b', 'c', 't', 0};

legacy_s8 opponent_done_button_id[4] = {'b', 'd', 'o', 0};

legacy_s8 car_done_button_id[4] = {'b', 'd', 'o', 0};

legacy_s8 result_drive_button_id[4] = {'b', 'd', 'r', 0};

legacy_s8 result_evaluation_button_id[4] = {'b', 'e', 'v', 0};

legacy_s8 result_highscore_button_id[4] = {'b', 'h', 'i', 0};

legacy_s8 opponent_previous_button_id[4] = {'b', 'l', 'a', 0};

legacy_s8 car_previous_button_id[4] = {'b', 'l', 'a', 0};

legacy_s8 car_manual_button_id[4] = {'b', 'm', 'a', 0};

legacy_s8 car_manual_toggle_id[4] = {'b', 'm', 'a', 0};

legacy_s8 result_main_menu_button_data[60] = {
	'b', 'm', 'm', 0, 2, 0, 1, 0, 2, 0, 3, 0, 4, 0, 1, 0, 4, 0, 0, 0, 5, 0, 0, 0, 0, 0, 6, 0, 5, 0,
	6,	 0,	  5,   0, 1, 0, 1, 0, 2, 0, 3, 0, 5, 0, 0, 0, 6, 0, 2, 0, 3, 0, 4, 0, 4, 0, 0, 0, 6, 0};

legacy_s8 opponent_next_button_id[4] = {'b', 'n', 'x', 0};

legacy_s8 car_next_button_id[4] = {'b', 'n', 'x', 0};

legacy_s8 result_race_button_id[4] = {'b', 'r', 'a', 0};

legacy_s8 result_replay_button_id[4] = {'b', 'r', 'p', 0};

legacy_s8 car_resource_wildcard[5] = {'c', 'a', 'r', '*', 0};

legacy_s8 opponent_portrait_clip_id[5] = {'c', 'l', 'i', 'p', 0};

legacy_s8 continued_race_label_id[4] = {'c', 'o', 'n', 0};

legacy_s8 replay_continue_dialog_id[4] = {'c', 'o', 'n', 0};

legacy_s8 credits_title_id[4] = {'c', 'r', 'e', 0};

legacy_s8 credits_resource_name[5] = {'c', 'r', 'e', 'd', 0};

legacy_s8 opponent_neutral_result_text_id[4] = {'d', '4', 'a', 0};

legacy_s8 dashboard_background_shape_id[5] = {'d', 'a', 's', 'h', 0};

legacy_s8 dashboard_mask_shape_id[5] = {'d', 'a', 's', 'm', 0};

legacy_s8 dashboard_top_shape_id[5] = {'d', 'a', 's', 't', 0};

legacy_s8 disk_retry_dialog_id[4] = {'d', 'e', 'a', 0};

legacy_s8 replay_filename_input[10] = {'D', 'E', 'F', 'A', 'U', 'L', 'T', 0, 0, 0};

legacy_s8 disk_error_dialog_id[4] = {'d', 'e', 'r', 0};

legacy_s8 credits_design_heading_id[4] = {'d', 'e', 's', 0};

legacy_s8 opponent_description_id[4] = {'d', 'e', 's', 0};

legacy_s8 car_description_id[4] = {'d', 'e', 's', 0};

legacy_s8 dashboard_digit_shape_ids[41] = {'d', 'i', 'g', '0', 'd', 'i', 'g', '1', 'd', 'i', 'g',
										   '2', 'd', 'i', 'g', '3', 'd', 'i', 'g', '4', 'd', 'i',
										   'g', '5', 'd', 'i', 'g', '6', 'd', 'i', 'g', '7', 'd',
										   'i', 'g', '8', 'd', 'i', 'g', '9', 0};

legacy_s8 player_did_not_finish_label_id[4] = {'d', 'n', 'f', 0};

legacy_s8 opponent_did_not_finish_label_id[4] = {'d', 'n', 'f', 0};

legacy_s8 exit_to_dos_dialog_id[4] = {'d', 'o', 's', 0};

legacy_s8 elapsed_time_label_id[4] = {'e', 'l', 't', 0};

const legacy_s8 exit_handler_overflow_message[22] = {'E', 'X', 'I', 'T', ' ', 'L', 'I', 'S',
													 'T', ' ', 'O', 'V', 'E', 'R', 'F', 'L',
													 'O', 'W', 13,	10,	 0,	  0};

legacy_s8 replay_overwrite_dialog_id[4] = {'f', 'e', 'x', 0};

legacy_s8 credits_third_designer_shape_id[5] = {'g', 'b', 'r', 'a', 0};

legacy_s8 credits_first_opponent_shape_id[5] = {'g', 'b', 'r', 'a', 0};

legacy_s8 credits_third_musician_shape_id[5] = {'g', 'b', 'r', 'i', 0};

legacy_s8 credits_second_artist_shape_id[5] = {'g', 'd', 'a', 'v', 0};

legacy_s8 credits_first_designer_shape_id[5] = {'g', 'd', 'o', 'n', 0};

legacy_s8 credits_first_logo_shape_id[5] = {'g', 'd', 's', '0', 0};

legacy_s8 credits_second_logo_shape_id[5] = {'g', 'd', 's', '1', 0};

legacy_s8 credits_second_designer_shape_id[5] = {'g', 'k', 'e', 'v', 0};

legacy_s8 credits_producer_shape_id[5] = {'g', 'k', 'e', 'v', 0};

legacy_s8 credits_fourth_artist_shape_id[5] = {'g', 'k', 'e', 'v', 0};

legacy_s8 credits_second_musician_shape_id[5] = {'g', 'k', 'r', 'i', 0};

legacy_s8 credits_first_artist_shape_id[5] = {'g', 'm', 's', 'm', 0};

legacy_s8 credits_first_musician_shape_id[5] = {'g', 'm', 's', 'y', 0};

legacy_s8 credits_third_artist_shape_id[5] = {'g', 'n', 'i', 'c', 0};

legacy_s8 dashboard_gear_and_dot_shape_ids[25] = {'g', 'n', 'o', 'b', 'g', 'n', 'a', 'b', 'd',
												  'o', 't', ' ', 'd', 'o', 't', 'a', 'd', 'o',
												  't', '1', 'd', 'o', 't', '2', 0};

legacy_s8 car_graph_shape_id[5] = {'g', 'r', 'a', 'p', 0};

legacy_s8 credits_second_opponent_shape_id[5] = {'g', 'r', 'i', 'c', 0};

legacy_s8 credits_fourth_designer_shape_id[5] = {'g', 'r', 'o', 'b', 0};

legacy_s8 credits_fifth_designer_shape_id[5] = {'g', 's', 't', 'a', 0};

legacy_s8 highscore_unavailable_message_id[4] = {'h', 'n', 'a', 0};

legacy_s8 missing_disk1_message_id[4] = {'i', 'd', '1', 0};

legacy_s8 missing_disk2_message_id[4] = {'i', 'd', '2', 0};

legacy_s8 missing_disk3_message_id[4] = {'i', 'd', '3', 0};

legacy_s8 missing_disk4_message_id[4] = {'i', 'd', '4', 0};

legacy_s8 highscore_track_disk_prompt_id[4] = {'i', 'h', 'd', 0};

legacy_s8 impact_speed_label_id[4] = {'i', 'm', 'p', 0};

legacy_s8 opponent_highscore_prompt_id[4] = {'i', 'n', 'h', 0};

legacy_s8 solo_highscore_prompt_id[4] = {'i', 'n', 'h', 0};

legacy_s8 jump_count_label_id[4] = {'j', 'u', 'm', 0};

legacy_s8 keyboard_driving_dialog_id[4] = {'k', 'e', 'y', 0};

legacy_s8 file_load_dialog_id[4] = {'l', 'o', 'a', 0};

legacy_s8 opponent_loss_text_id[5] = {'l', 'o', 's', 'e', 0};

legacy_s8 file_scroll_down_label_id[4] = {'l', 's', 'd', 0};

legacy_s8 file_scroll_up_label_id[4] = {'l', 's', 'u', 0};

legacy_s8 replay_mode_options_dialog_id[4] = {'m', 'd', 'o', 0};

legacy_s8 replay_pause_menu_id[4] = {'m', 'e', 'n', 0};

legacy_s8 insufficient_memory_dialog_id[4] = {'m', 'e', 'r', 0};

legacy_s8 opponent_misc_resource_name[5] = {'m', 'i', 's', 'c', 0};

legacy_s8 car_misc_resource_name[5] = {'m', 'i', 's', 'c', 0};

legacy_s8 random_wait_legacy_bytes[5] = {'m', 'i', 's', 'c', 0};

legacy_s8 results_misc_resource_name[5] = {'m', 'i', 's', 'c', 0};

legacy_s8 music_disabled_message_id[4] = {'m', 'o', 'f', 0};

legacy_s8 music_enabled_message_id[4] = {'m', 'o', 'n', 0};

legacy_s8 mouse_driving_dialog_id[4] = {'m', 'o', 'u', 0};

legacy_s8 average_speed_units_id[4] = {'m', 'p', 'h', 0};

legacy_s8 impact_speed_units_id[4] = {'m', 'p', 'h', 0};

legacy_s8 top_speed_units_id[4] = {'m', 'p', 'h', 0};

legacy_s8 graphics_options_dialog_id[4] = {'m', 'r', 'l', 0};

legacy_s8 frame_rate_changed_message_id[4] = {'m', 'r', 's', 0};

legacy_s8 credits_music_heading_id[4] = {'m', 'u', 's', 0};

legacy_s8 opponent_unfinished_time_label_id[4] = {'o', 'l', 't', 0};

legacy_s8 opponent_loss_time_label_id[4] = {'o', 'l', 't', 0};

legacy_s8 opponent_animation_frame_id[5] = {'o', 'p', '0', '1', 0};

legacy_s8 opponent_portrait_shape_ids[29] = {'o', 'p', 'p', '0', 'o', 'p', 'p', '1', 'o', 'p',
											 'p', '2', 'o', 'p', 'p', '3', 'o', 'p', 'p', '4',
											 'o', 'p', 'p', '5', 'o', 'p', 'p', '6', 0};

legacy_s8 opponent_loss_animation_name[10] = {'o', 'p', 'p', '2', 'l', 'o', 's', 'e', 0, 0};

legacy_s8 opponent_win_animation_name[8] = {'o', 'p', 'p', '2', 'w', 'i', 'n', 0};

legacy_s8 credits_opponent_heading_id[4] = {'o', 'p', 'r', 0};

legacy_s8 race_end_song_id[5] = {'O', 'V', 'E', 'R', 0};

legacy_s8 opponent_win_time_label_id[4] = {'o', 'w', 't', 0};

legacy_s8 pause_dialog_id[4] = {'p', 'a', 'u', 0};

legacy_s8 penalty_time_label_id[4] = {'p', 'p', 't', 0};

legacy_s8 credits_production_heading_id[4] = {'p', 'r', 'o', 0};

legacy_s8 opponent_racing_car_label_id[5] = {'r', 'a', 'c', 0, 0};

legacy_s8 replay_save_prompt_id[4] = {'r', 'e', 'p', 0};

legacy_s8 dashboard_roof_shape_id[5] = {'r', 'o', 'o', 'f', 0};

legacy_s8 replay_control_shape_ids[93] = {
	'r', 'p', 'l', 'y', 'r', 'p', 'i', 'c', 'r', 'p', 'a', 'c', 'r', 'p', 'm', 'c', 'r', 'p', 't',
	'c', 'b', 'o', 'f', '6', 'b', 'o', 'f', '5', 'b', 'o', 'f', '4', 'b', 'o', 'f', '3', 'b', 'o',
	'f', '2', 'b', 'o', 'f', '1', 'b', 'o', 'f', '0', 'z', 'o', 'o', 'm', 'p', 'a', 'n', 'n', 'b',
	'o', 'n', '6', 'b', 'o', 'n', '5', 'b', 'o', 'n', '4', 'b', 'o', 'n', '3', 'b', 'o', 'n', '2',
	'b', 'o', 'n', '1', 'b', 'o', 'f', '0', 'z', 'o', 'o', 'm', 'p', 'a', 'n', 'n', 0};

const legacy_s8 file_read_error_format[15] = {'%', 's', ' ', 'F', 'I', 'L', 'E', ' ',
											  'E', 'R', 'R', 'O', 'R', 13,	0};

const legacy_s8 file_write_error_format[16] = {'%', 's', ' ', 'F', 'I', 'L', 'E', ' ',
											   'E', 'R', 'R', 'O', 'R', 13,	 0,	  0};

const legacy_s8 file_size_error_format[14] = {'%', 's', ' ', 'F', 'I', 'L', 'E',
											  ' ', 'E', 'R', 'R', 'O', 'R', 0};

const legacy_s8 invalid_pack_type_error_format[23] = {'%', 's', ' ', 'I', 'N', 'V', 'A', 'L',
													  'I', 'D', ' ', 'P', 'A', 'C', 'K', ' ',
													  'T', 'Y', 'P', 'E', 13,  0,	0};

legacy_s8 file_save_dialog_id[5] = {'s', 'a', 'v', 0, 0};

legacy_s8 main_menu_background_data[18] = {'s', 'c', 'r', 'n', 0, 0, 1, 2, 4,
										   0,	3,	 0,	  3,   0, 1, 4, 2, 0};

legacy_s8 opponent_menu_background_id[5] = {'s', 'c', 'r', 'n', 0};

legacy_s8 car_menu_shapes_name[7] = {'s', 'd', 'c', 's', 'e', 'l', 0};

legacy_s8 main_menu_shapes_name[7] = {'s', 'd', 'm', 's', 'e', 'l', 0};

legacy_s8 opponent_menu_shapes_name[7] = {'s', 'd', 'o', 's', 'e', 'l', 0};

legacy_s8 replay_save_error_message_id[4] = {'s', 'e', 'r', 0};

legacy_s8 victory_instrument_resource_name[7] = {'s', 'k', 'i', 'd', 'm', 's', 0};

legacy_s8 race_end_instrument_resource_name[7] = {'s', 'k', 'i', 'd', 'm', 's', 0};

legacy_s8 race_end_music_resource_name[9] = {'s', 'k', 'i', 'd', 'o', 'v', 'e', 'r', 0};

legacy_s8 victory_music_resource_name[9] = {'s', 'k', 'i', 'd', 'v', 'i', 'c', 't', 0};

legacy_s8 effects_disabled_message_id[4] = {'s', 'o', 'f', 0};

legacy_s8 effects_enabled_message_id[4] = {'s', 'o', 'n', 0};

legacy_s8 dashboard_primary_resource_name[10] = {'s', 't', 'd', 'a', 'x', 'x', 'x', 'x', 0, 0};

legacy_s8 dashboard_secondary_resource_name[9] = {'s', 't', 'd', 'b', 'x', 'x', 'x', 'x', 0};

legacy_s8 car_preview_top_shape_id[5] = {'s', 't', 'o', 'p', 0};

legacy_s8 car_shape_resource_name[7] = {'s', 't', 'x', 'x', 'x', 0, 0};

legacy_s8 top_speed_label_id[4] = {'t', 'o', 'p', 0};

legacy_s8 victory_song_id[5] = {'V', 'I', 'C', 'T', 0};

legacy_s8 waiting_message_id[4] = {'w', 'a', 'i', 0};

legacy_s8 dashboard_wheel_and_instrument_ids[37] = {
	'w', 'h', 'l', '1', 'w', 'h', 'l', '2', 'w', 'h', 'l', '3', 'i', 'n', 's', '2', 'g', 'b', 'o',
	'x', 'i', 'n', 's', '1', 'i', 'n', 's', '3', 'i', 'n', 'm', '1', 'i', 'n', 'm', '3', 0};

legacy_s8 window_release_order_message[32] = {'W', 'i', 'n', 'd', 'o', 'w', ' ', 'R', 'e', 'l', 'e',
											  'a', 's', 'e', 'd', ' ', 'O', 'u', 't', ' ', 'o', 'f',
											  ' ', 'O', 'r', 'd', 'e', 'r', 13,	 10,  0,   0};

legacy_s8 window_row_table_overflow_message[36] = {
	'w', 'i', 'n', 'd', 'o', 'w', 'd', 'e', 'f', ' ', '-', ' ', 'O', 'U', 'T', ' ', 'O', 'F',
	' ', 'R', 'O', 'W', ' ', 'T', 'A', 'B', 'L', 'E', ' ', 'S', 'P', 'A', 'C', 'E', 13,	 0};

legacy_s8 opponent_win_text_id[5] = {'w', 'i', 'n', 'n', 0};

legacy_s8 car_resource_extension[5] = {'.', 'r', 'e', 's', 0};

legacy_s8 replay_file_extension[5] = {'.', 'r', 'p', 'l', 0};

legacy_s8 track_file_extension[5] = {'.', 't', 'r', 'k', 0};

/* AdLib FM also works on the OPL synthesizer of Sound Blaster cards. */
legacy_s8 audiodriverstring[5] = {'a', 'd', '1', '5', 0};
