#ifndef RESTUNTS_REPLAY_VIEWER_INTERNAL_H
#define RESTUNTS_REPLAY_VIEWER_INTERNAL_H

#include "../game_input.h"
#include "../legacy.h"

struct RECTANGLE;
struct SHAPE2D;

extern legacy_u8 replay_selected_control;
extern legacy_u8 replay_control_left_neighbor[10];
extern legacy_u8 replay_control_right_neighbor[10];
extern legacy_u8 replay_control_up_neighbor[10];
extern legacy_u8 replay_control_down_neighbor[10];
extern legacy_u8 game_camera_buttons_count[4];
extern struct BUTTON_AREA game_camera_buttons[9];
extern legacy_s16 replay_pan_button_left;
extern legacy_s16 replay_pan_button_right;
extern legacy_s16 replay_zoom_button_top;
extern legacy_s16 replay_pan_button_top;
extern legacy_s16 replay_zoom_button_bottom;
extern legacy_s16 replay_pan_button_bottom;
extern struct BUTTON_AREA replay_hidden_bar_camera_button;
extern legacy_s16 replay_current_position_cache[2];
extern legacy_u8 replay_selection_cache[2];
extern legacy_s16 replay_displayed_time_cache[2];
extern struct SHAPE2D far *rplyshapes[23];
extern legacy_u8 replay_control_active[9];
extern legacy_u8 replay_legacy_fast_play_active;
extern legacy_u8 replay_legacy_play_active;
extern legacy_u8 replay_camera_mode_cache[2];
extern legacy_s16 replay_recorded_position_cache[2];
extern legacy_u8 replay_control_active_cache[18];
extern legacy_s16 replay_timeline_background_color;
extern legacy_s16 replay_marker_color;
extern legacy_s8 replay_pause_menu_id[];
extern legacy_s8 replay_continue_dialog_id[];
extern legacy_s8 replay_save_prompt_id[];
extern legacy_s8 replay_file_extension[];
extern legacy_s8 replay_overwrite_dialog_id[];
extern legacy_s8 replay_save_error_message_id[];
extern legacy_s8 replay_mode_options_dialog_id[];
extern legacy_s8 replay_control_shape_ids[];

extern legacy_s8 replay_filename_input[];

#endif
