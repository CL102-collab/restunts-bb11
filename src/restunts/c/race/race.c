#include "race_internal.h"
#include "../extended/extended_replay_ui.h"
#ifdef RESTUNTS_AMIGA_TRACE
#include <stdio.h>
#endif
#ifdef RESTUNTS_SDL3
#include "../../platform/sdl3/sdl3.h"
#include "../../platform/sdl3/touch_game.h"
#include "../../platform/sdl3/controller_game.h"
#include "../extended/extended_replay_runtime.h"
#endif
#ifdef RESTUNTS_AMIGA
#include "../../platform/amiga/amiga.h"
#endif
#include "../dashboard.h"
#include "../fileio.h"
#include "../game_input.h"
#include "../memmgr.h"
#include "../platform.h"
#include "race.h"
#include "../ghost/ghost.h"
#include "race_resources.h"
#include "../replay/replay.h"
#include "../replay/replay_record.h"
#include "../replay/replay_viewer.h"
#include "../shape2d/shape2d.h"
#include "../ui/ui_dialog.h"
#include "../ui/ui_text.h"
#include "../shape3d/shape3d.h"
#include "../crash_state.h"
#include "../track/track_objects.h"
#include "../camera.h"
#include "../video_frame.h"
#include "../car/car_audio.h"
#include "../audio/audio_control.h"
#include "../externs.h"
#include "../keyboard.h"
#include "../timing.h"
#include "../frame/frame_internal.h"

#ifdef RESTUNTS_AMIGA_TRACE
#define AMIGA_NORMAL_RACE_TRACE(stage)                                                             \
	do {                                                                                           \
		puts("Amiga normal race: " stage);                                                         \
		fflush(stdout);                                                                            \
	} while (0)
#else
#define AMIGA_NORMAL_RACE_TRACE(stage) ((void)0)
#endif

static legacy_u16 race_prepare_mode(void)
{
	if (idle_expired == 0) {
		if (race_recorded_frames() != 0) {
			camera_select_mode(CAMERA_MODE_COCKPIT);
			game_replay_mode = REPLAY_MODE_PLAYBACK;
			is_in_replay = 1;
		} else {
			camera_select_mode(CAMERA_MODE_COCKPIT);
			game_replay_mode = REPLAY_MODE_PAUSED;
		}
	} else {
		camera_select_mode((legacy_s8)(((legacy_u8)cameramode + 1U) & CAMERA_MODE_MASK));

		game_replay_mode = REPLAY_MODE_PLAYBACK;
		if (file_load_replay(0, "default") != 0) {
			return 0;
		}
		track_setup();
	}

	return 1;
}

static void race_initialize_state(void)
{
#ifdef RESTUNTS_SDL3
	extended_replay_ui_begin_live_track();
#endif
	kbormouse = 0;
	replay_playback_speed = REPLAY_PLAYBACK_NORMAL;
	race_exit_request = 1;
	set_frame_callback();
	game_replay_mode_copy = RACE_REPLAY_MODE_UNINITIALIZED;
	frame_buffer_index = 0;
	dashboard_buffer_index = 0;
	recording_limit_warning_requested = 0;
	if (idle_expired == 0) {
		camera_select_mode(CAMERA_MODE_COCKPIT);
	}
	dashb_toggle = 0;

	if (idle_expired != 0) {
		framespersec = gameconfig.game_framespersec;

		init_game_state(GAMESTATE_INIT_RESET_CHECKPOINTS);
	} else {
		if (is_in_replay == 0) {
			replay_filename[0] = 0;
			dashb_toggle = 1;
			show_penalty_counter = 0;
			init_game_state_with_frame_rate(configured_frame_rate);
			reserved_race_word = 0;
			replay_overflow_acknowledged_word = LEGACY_S16_FROM_BITS(
				LEGACY_U16_REPLACE_LOW_BYTE(replay_overflow_acknowledged_word, 0U));
			race_start_sequence_state = RACE_START_SEQUENCE_FLAG_ANIMATION;
			mouse_minmax_position(mouse_driving_enabled);
			game_replay_mode = REPLAY_MODE_PAUSED;

			state.playerstate.car_position.lx = LEGACY_S32_WRAP_ADD(
				state.playerstate.car_position.lx,
				LEGACY_S32_SHL((legacy_s32)multiply_and_scale(sin_fast(track_angle),
															  RACE_START_POSITION_DISTANCE),
							   RACE_START_POSITION_SCALE_SHIFT));
			state.playerstate.car_position.lz = LEGACY_S32_WRAP_ADD(
				state.playerstate.car_position.lz,
				LEGACY_S32_SHL((legacy_s32)multiply_and_scale(cos_fast(track_angle),
															  RACE_START_POSITION_DISTANCE),
							   RACE_START_POSITION_SCALE_SHIFT));
			state.playerstate.car_position.ly = LEGACY_S32_WRAP_ADD(
				state.playerstate.car_position.ly, RACE_START_CAMERA_HEIGHT_OFFSET);
			replay_recording_flags = REPLAY_RECORDING_ACTIVE_FLAG;
		} else {
			game_replay_mode = REPLAY_MODE_PLAYBACK;
			start_flag_animation = RACE_REPLAY_RESTORE_WAIT_TICKS;
			framespersec = gameconfig.game_framespersec;
#ifdef RESTUNTS_SDL3
			if (extended_replay_ui_seek_end()) {
				return;
			}
#endif
			restore_gamestate(0);
			restore_gamestate(gameconfig.game_recordedframes);

			while (gameconfig.game_recordedframes != state.game_frame) {
				if (input_do_checking(1) == KEY_ESCAPE) {
					break;
				}
				update_gamestate();
			}

			elapsed_time2 = gameconfig.game_recordedframes;
		}
	}
}

static void race_check_recording_limit(void)
{
	if (recording_limit_warning_requested != 0) {
		input_push_status();
		audio_suspend();
		legacy_s16 dialog_choice =
			show_dialog(DIALOG_TYPE_MENU, DIALOG_SAVE_BACKGROUND,
						locate_text_res(gameresptr, "rbf"), -1, -1, dialog_border_color, 0, 0);
		if (dialog_choice == -1) {
			dialog_choice = 0;
		}

		audio_resume();
		dos_timer_set_callbacks_suspended(0);
		input_pop_status();
		if (dialog_choice != 0) {
			update_crash_state(CRASH_EVENT_EXIT, PLAYER_CAR_INDEX);
			race_exit_request = 1;
		}

		recording_limit_warning_requested = 0;
		frame_fps_reset();
	}
}

void race_update_dashboard_layout(legacy_s16 rewind_active)
{
	game_replay_mode_copy = game_replay_mode;
	dashb_toggle_copy = dashb_toggle;
	replaybar_toggle_copy = replaybar_toggle;
	is_in_replay_copy = is_in_replay;
	followOpponentFlag_copy = followOpponentFlag;
	roofbmpheight_copy = 0;
	dashboard_visible = 0;

	if (game_replay_mode != REPLAY_MODE_PLAYBACK || idle_expired != 0 || rewind_active != 0 ||
		(replaybar_toggle == 0 && is_in_replay == 0)) {
		replaybar_enabled = 0;
	} else {
		replaybar_enabled = 1;
	}

	if (idle_expired != 0) {
		dashbmp_y_copy = RACE_SCREEN_HEIGHT;
	} else if (dashb_toggle == 0 || followOpponentFlag != 0) {
		if (game_replay_mode == REPLAY_MODE_PLAYBACK) {
			if (replaybar_enabled != 0) {
				dashbmp_y_copy = RACE_REPLAY_BAR_TOP;
			} else {
				dashbmp_y_copy = RACE_SCREEN_HEIGHT;
			}
		} else {
			dashbmp_y_copy = RACE_SCREEN_HEIGHT;
		}
	} else {
		if (game_replay_mode != REPLAY_MODE_PLAYBACK || replaybar_enabled == 0) {
			height_above_replaybar = RACE_SCREEN_HEIGHT;
		} else {
			height_above_replaybar = RACE_REPLAY_BAR_TOP;
		}

		dashboard_visible = 1;
		roofbmpheight_copy = roofbmpheight;
		dashbmp_y_copy = dashbmp_y;
		race_hires_clip_dashboard();
	}
}

void race_update_viewport(struct RACE_VIEWPORT_CACHE *cache, legacy_s16 rewind_active)
{
	if (game_replay_mode != game_replay_mode_copy || dashb_toggle != dashb_toggle_copy ||
		replaybar_toggle != replaybar_toggle_copy || is_in_replay != is_in_replay_copy ||
		followOpponentFlag != followOpponentFlag_copy ||
		race_hires_viewport_changed(&cache->hires) != 0) {
		race_hires_cache_viewport(&cache->hires);
		race_update_dashboard_layout(rewind_active);
		if (cache->roof_height != roofbmpheight_copy || dashbmp_y_copy != viewport_bottom_cache ||
			cache->dashboard_bottom != height_above_replaybar) {
			full_redraw_frames_remaining = video_page_count;
			set_projection(RACE_PROJECTION_HORIZONTAL_SCALE,
						   LEGACY_S16_DIV_OR_ZERO(dashbmp_y_copy, RACE_PROJECTION_VERTICAL_DIVISOR),
						   RACE_SCREEN_WIDTH, dashbmp_y_copy);
			rect_windshield.top = roofbmpheight_copy;
			rect_windshield.bottom = dashbmp_y_copy;
			cache->roof_height = roofbmpheight_copy;
			viewport_bottom_cache = dashbmp_y_copy;
			cache->dashboard_bottom = height_above_replaybar;
		}
	}
}

static void race_handle_driving_input(void)
{
	legacy_s16 input_key;

	do {
		input_key = dos_kb_get_char();
		if (input_key != 0 && handle_ingame_kb_shortcuts(input_key) != 0) {
			break;
		}
		/* Driving controls are sampled by state. Drain all unhandled repeats so
		 * they cannot delay commands; a consumed callback keeps its modal boundary. */
	} while (input_key != 0);

	if (game_replay_mode == REPLAY_MODE_PAUSED) {
		dos_mouse_get_state(&mouse_butstate, &mouse_xpos, &mouse_ypos);
		legacy_s16 start_requested = ((mouse_butstate & MOUSE_BUTTON_MASK) != 0) ||
									 ((get_kb_or_joy_flags() & INPUT_ACTION_BUTTON_MASK) != 0);
#ifdef RESTUNTS_SDL3
		start_requested |= sdl3_controller_take_race_start_request() != 0;
#endif
		if (start_requested != 0) {
			game_replay_mode = REPLAY_MODE_LIVE;
			race_start_sequence_state = RACE_START_SEQUENCE_INACTIVE;
			init_game_state_with_frame_rate(configured_frame_rate);
		}
	}
}

static legacy_u16 race_handle_exit_request(void)
{
	if (race_exit_request != 0) {
		if ((game_replay_mode != REPLAY_MODE_LIVE || state.game_end_event == CRASH_EVENT_EXIT) &&
			race_exit_request != REPLAY_EXIT_REQUESTED) {
			race_exit_request = 0;
			game_replay_mode = REPLAY_MODE_PLAYBACK;
			mouse_minmax_position(0);
			loop_game(REPLAY_LOOP_LOAD_RESOURCES, REPLAY_LOOP_UNUSED_ARGUMENT,
					  REPLAY_LOOP_UNUSED_ARGUMENT);
			loop_game(REPLAY_LOOP_SELECT_CONTROL, REPLAY_CONTROL_PAUSE,
					  REPLAY_LOOP_UNUSED_ARGUMENT);
			is_in_replay = 1;
			audio_carstate();
		} else {
			return 1;
		}
	}
	return 0;
}

static legacy_u16 race_frame_is_ready(legacy_s64 *last_processed_frame)
{
	if (race_current_frame() != race_input_frame()) {
#ifdef RESTUNTS_SDL3
		if (game_replay_mode == REPLAY_MODE_LIVE) {
#else
		if ((mouse_driving_enabled != 0 || dos_joystick_is_enabled() != 0) &&
			game_replay_mode == REPLAY_MODE_LIVE) {
#endif
			replay_apply_analog_steering_history();
		}
		update_gamestate();
		return 0;
	}

	legacy_s16 wait_for_sample = game_replay_mode == REPLAY_MODE_LIVE;
#ifdef RESTUNTS_SDL3
	legacy_u16 presentation_ready;
	if (race_presentation_poll(last_processed_frame, &wait_for_sample, &presentation_ready) != 0) {
		return presentation_ready;
	}
#endif
	if (wait_for_sample != 0 && race_exit_request == 0 &&
		state.game_inputmode != GAME_INPUT_MODE_WAITING) {
		if (*last_processed_frame == (legacy_s64)race_current_frame()) {
#ifdef RESTUNTS_SDL3
			/* Timer callbacks are dispatched on this thread. Yield while waiting
			 * for the next input sample, then deliver elapsed 100 Hz ticks. */
			sdl3_platform_delay(RACE_CONTROL_POLL_DELAY_MS);
			sdl3_platform_pump();
#endif
			return 0;
		}
		*last_processed_frame = race_current_frame();
	}

	return 1;
}

static legacy_s16 race_process_frame_input(void)
{
	if (idle_expired == 0) {
		if (race_handle_exit_request() != 0) {
			return 1;
		}

		if (game_replay_mode == REPLAY_MODE_PLAYBACK) {
			loop_game(REPLAY_LOOP_HANDLE_INPUT, REPLAY_LOOP_UNUSED_ARGUMENT,
					  REPLAY_LOOP_UNUSED_ARGUMENT);
			return 0;
		}

		race_handle_driving_input();

	} else {
		if (dos_kb_get_char() != 0 || race_exit_request != 0 || get_kb_or_joy_flags() != 0) {
			return 1;
		}
	}
	return 0;
}

#ifdef RESTUNTS_SDL3
static legacy_s16 race_handle_controller_menu(struct RACE_REWIND_STATE *rewind,
											  legacy_s64 *last_processed_frame,
											  legacy_u8 *menu_pending)
{
	if (sdl3_controller_take_menu_request() != 0) {
		*menu_pending = 1;
	}
	if (*menu_pending == 0) {
		return 0;
	}
	if (rewind->active != 0) {
		race_rewind_resume(rewind);
		*last_processed_frame = -1;
	}
	/* Sampled live input must reach physics before switching to replay. */
	if (game_replay_mode == REPLAY_MODE_LIVE && race_current_frame() != race_input_frame()) {
		return 0;
	}
	*menu_pending = 0;
	if (game_replay_mode != REPLAY_MODE_PLAYBACK) {
		(void)handle_ingame_kb_shortcuts(KEY_ESCAPE);
		if (race_handle_exit_request() != 0) {
			return 1;
		}
	}
	sdl3_input_set_replay_active(1);
	loop_game(REPLAY_LOOP_OPEN_MENU, REPLAY_LOOP_UNUSED_ARGUMENT, REPLAY_LOOP_UNUSED_ARGUMENT);
	return race_handle_exit_request();
}
#endif

static void race_run_frames(struct RACE_VIEWPORT_CACHE *cache)
{
#ifdef RESTUNTS_AMIGA_TRACE
	legacy_u16 traced_frames = 0;
#endif
	legacy_s64 last_processed_frame = -1;
	struct RACE_REWIND_STATE rewind = {0};
#ifdef RESTUNTS_SDL3
	legacy_u8 controller_menu_pending = 0;
#endif
	frame_supersight_reset();
	frame_fps_reset();
#ifdef RESTUNTS_SDL3
	race_presentation_initialize();
#endif

	while (1) {
#ifdef RESTUNTS_SDL3
		sdl3_input_set_replay_active(game_replay_mode == REPLAY_MODE_PLAYBACK &&
									 rewind.active == 0);
		if (race_handle_controller_menu(&rewind, &last_processed_frame, &controller_menu_pending) !=
			0) {
			break;
		}
		(void)input_update_controller_camera();
#endif
		legacy_s16 was_rewinding = rewind.active;
		race_update_rewind(&rewind);
		if (was_rewinding != 0 && rewind.active == 0) {
			last_processed_frame = -1;
		}
		/* Commands must remain responsive while waiting for input samples or
		 * catching up physics, before another potentially expensive draw. */
		if (rewind.active == 0 && idle_expired == 0 && game_replay_mode == REPLAY_MODE_LIVE &&
			race_exit_request == 0 && kb_checking() != 0) {
			race_handle_driving_input();
		}
#ifdef RESTUNTS_SDL3
		race_presentation_sync(rewind.active);
#endif
		if (race_frame_is_ready(&last_processed_frame) == 0) {
#ifdef RESTUNTS_SDL3
			race_presentation_idle(rewind.active, last_processed_frame);
#endif
			continue;
		}
		if (state.game_inputmode == GAME_INPUT_MODE_WAITING &&
			game_replay_mode == REPLAY_MODE_LIVE) {
#ifdef RESTUNTS_SDL3
			if (!extended_replay_ui_restart())
#endif
			{
				elapsed_time2 = 0;
				gameconfig.game_recordedframes = 0;
				state.game_frame = 0;
			}
		}

		if (slow_video_mgmt_copy != slow_video_mgmt) {
			slow_video_mgmt_copy = slow_video_mgmt;
			init_rect_arrays();
		}

		if (rewind.active == 0) {
			race_check_recording_limit();
		}
		race_select_render_target();

		ghost_update(race_ghost_frame(), framespersec);
		if (state.game_frame == 0) {
			AMIGA_NORMAL_RACE_TRACE("viewport begin");
		}
		race_update_viewport(cache, rewind.active);
		if (state.game_frame == 0) {
			AMIGA_NORMAL_RACE_TRACE("viewport complete");
		}
#ifdef RESTUNTS_SDL3
		race_presentation_draw(rewind.active);
#else
		if (state.game_frame == 0) {
			AMIGA_NORMAL_RACE_TRACE("draw begin");
		}
		race_draw_frame();
		if (state.game_frame == 0) {
			AMIGA_NORMAL_RACE_TRACE("draw complete");
		}
#ifdef RESTUNTS_AMIGA_TRACE
		if (++traced_frames <= 8 || (traced_frames % 50) == 0) {
			fprintf(stdout, "Amiga normal race: frame=%d elapsed=%u mode=%d start=%d\n",
					(legacy_int)state.game_frame, (legacy_uint)elapsed_time2,
					(legacy_int)game_replay_mode, (legacy_int)race_start_sequence_state);
			fflush(stdout);
		}
#endif
#endif
		if (game_replay_mode == REPLAY_MODE_PAUSED &&
			race_start_sequence_state == RACE_START_SEQUENCE_INACTIVE) {
			game_replay_mode = REPLAY_MODE_LIVE;
			init_game_state_with_frame_rate(configured_frame_rate);
		}

		if (rewind.active == 0 && race_process_frame_input() != 0) {
			break;
		}
	}
}

static void race_finish_opponent(void)
{
	legacy_s16 opponent_progress_text_position[2];
	if (game_replay_mode == REPLAY_MODE_LIVE && gameconfig.game_opponenttype != 0 &&
		state.opponentstate.car_crashBmpFlag == CRASH_EVENT_NONE) {
		show_dialog(DIALOG_TYPE_PLACEHOLDERS, DIALOG_NO_BACKGROUND_SAVE,
					locate_text_res(gameresptr, "cop"), -1, RACE_OPPONENT_PROGRESS_DIALOG_Y,
					performGraphColor, opponent_progress_text_position, 0);
		replay_overflow_acknowledged_word = LEGACY_S16_FROM_BITS(
			LEGACY_U16_REPLACE_LOW_BYTE(replay_overflow_acknowledged_word, 1U));
		legacy_s16 frame_counter = framespersec;
		frame_counter--;

		while (1) {
			replay_update_input_tick(1);
			update_gamestate();
			frame_counter++;
			if (frame_counter == framespersec) {
				frame_counter = 0;
#ifdef RESTUNTS_SDL3
				if (!extended_replay_ui_format_current_time(&resID_byte1))
#endif
				{
					format_frame_as_string(&resID_byte1, state.game_frame + elapsed_time1, 1);
				}
				mouse_draw_opaque_check();
				font_draw_text_opaque(&resID_byte1, font_centered_text_x(&resID_byte1),
									  opponent_progress_text_position[1]);
				mouse_draw_transparent_check();
			}

			if (input_do_checking(1) == KEY_ESCAPE) {
				break;
			}
			if (state.opponentstate.car_crashBmpFlag != CRASH_EVENT_NONE) {
				break;
			}
			if (
#ifdef RESTUNTS_SDL3
				extended_replay_ui_uses_legacy_limit() &&
#endif
				RACE_FRAME_LIMIT_MULTIPLIER * framespersec == state.game_frame + elapsed_time1) {
				break;
			}
		}
	}
}

static void race_release_resources(void)
{
	if (video_uses_page_flipping != 0 && video_backbuffer_copy_required() != 0) {
		mouse_draw_opaque_check();
		sprite_select_mcga_backbuffer();
		sprite_copy_rect_shifted(0, 0, RACE_SCREEN_WIDTH, RACE_SCREEN_HEIGHT, 0);
		sprite_present_mcga_backbuffer();
		mouse_draw_transparent_check();
	}

	sprite_select_screen_compat();
	is_in_replay = 1;
	audio_carstate();
	audio_remove_driver_timer();
	race_finish_opponent();

	replay_overflow_acknowledged_word =
		LEGACY_S16_FROM_BITS(LEGACY_U16_REPLACE_LOW_BYTE(replay_overflow_acknowledged_word, 0U));
	mouse_minmax_position(0);
	remove_frame_callback();
	free_player_cars();
#ifdef RESTUNTS_SDL3
	race_presentation_finish();
#endif
}

void run_game(void)
{
	AMIGA_NORMAL_RACE_TRACE("run_game begin");
	rect_windshield.left = 0;
	rect_windshield.right = RACE_SCREEN_WIDTH;
	struct RACE_VIEWPORT_CACHE cache;
	cache.roof_height = -1;
	cache.dashboard_bottom = -1;
	race_hires_cache_viewport(&cache.hires);
	viewport_bottom_cache = -1;
	run_game_random = LEGACY_S16_SHL(get_kevinrandom(), RACE_RANDOM_VALUE_SHIFT);
	replaybar_toggle = 1;
	is_in_replay = 0;
	if (race_prepare_mode() == 0) {
		return;
	}
	if (idle_expired == 0 && race_recorded_frames() == 0 && ghost_prepare_race() != 0) {
		replay_recording_flags = 0;
		ghost_end_race();
		show_dialog(DIALOG_TYPE_MESSAGE, DIALOG_NO_BACKGROUND_SAVE,
					"Unable to prepare ghost replay.", -1, -1, dialog_border_color, 0, 0);
		return;
	}
	if (setup_player_cars() != 0) {
		free_player_cars();
		show_insufficient_memory_dialog();
	} else {
		AMIGA_NORMAL_RACE_TRACE("initialize begin");
		race_initialize_state();
		AMIGA_NORMAL_RACE_TRACE("initialize complete; frames begin");
#ifdef RESTUNTS_SDL3
		sdl3_touch_set_game_active(1);
		sdl3_input_set_gameplay_active(idle_expired == 0);
#endif
#ifdef RESTUNTS_AMIGA
		/* Input polling must not present an unfinished frame or spend more
		 * time converting pixels than the simulation can catch up. */
		amiga_video_set_auto_refresh(0);
#endif
		race_run_frames(&cache);
#ifdef RESTUNTS_AMIGA
		amiga_video_set_auto_refresh(1);
#endif
		AMIGA_NORMAL_RACE_TRACE("frames complete");
#ifdef RESTUNTS_SDL3
		sdl3_touch_set_game_active(0);
		sdl3_input_set_gameplay_active(0);
#endif
		race_release_resources();
	}
	ghost_end_race();
	waitflag = RACE_FINAL_WAIT_TICKS;
	check_input();
	show_waiting();
}
