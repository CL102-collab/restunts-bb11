#ifdef RESTUNTS_SDL3
#include "../platform/sdl3/sdl3.h"
#include "../platform/sdl3/touch_game.h"
#include "../platform/sdl3/controller_game.h"
#include "frame_interpolation.h"
#include "frame_adaptive.h"
#include "hires.h"
#include "presentation.h"
#endif
#include "dashboard.h"
#include "fileio.h"
#include "game_input.h"
#include "memmgr.h"
#include "platform.h"
#include "race.h"
#include "ghost.h"
#include "race_resources.h"
#include "replay.h"
#include "replay_record.h"
#include "replay_viewer.h"
#include "shape2d.h"
#include "ui_dialog.h"
#include "ui_text.h"
#include "shape3d.h"
#include "crash_state.h"
#include "track_objects.h"
#include "camera.h"
#include "video_frame.h"
#include "car_audio.h"
#include "audio_control.h"
#include "externs.h"
#include "keyboard.h"
#include "timing.h"
#include "frame_internal.h"

#define RACE_SCREEN_WIDTH 320
#define RACE_SCREEN_HEIGHT 200
#define RACE_REPLAY_BAR_TOP 151
#define RACE_START_CAMERA_HEIGHT_OFFSET 1408L
#define RACE_REPLAY_RESTORE_WAIT_TICKS 500
#define RACE_PROJECTION_HORIZONTAL_SCALE 35
#define RACE_PROJECTION_VERTICAL_DIVISOR 6
#define RACE_OPPONENT_PROGRESS_DIALOG_Y 80
#define RACE_FRAME_LIMIT_MULTIPLIER 1500
#define RACE_FINAL_WAIT_TICKS 100
#define MOUSE_BUTTON_MASK 3
#define RACE_REPLAY_MODE_UNINITIALIZED (-1)
#define RACE_START_POSITION_DISTANCE (-240)
#define RACE_START_POSITION_SCALE_SHIFT 6U
#define RACE_RANDOM_VALUE_SHIFT 3U
#define RACE_REWIND_SCAN_CODE 0x10
#define RACE_CONTROL_SCAN_CODE 0x1D
/* Match the replay scrub units; the 100 Hz timer doubles speed after ten seconds. */
#define RACE_REWIND_UNITS_PER_FRAME 20UL
#define RACE_REWIND_BASE_SPEED 3UL
#define RACE_REWIND_DOUBLE_SPEED 6UL
#define RACE_REWIND_DOUBLE_AFTER_TICKS 1000UL

struct RACE_VIEWPORT_CACHE {
	legacy_s16 roof_height;
	legacy_s16 dashboard_bottom;
	legacy_u8 supersight;
};

struct RACE_REWIND_STATE {
	legacy_s16 active;
	legacy_u16 origin_frame;
	legacy_u32 held_ticks;
	legacy_u32 accumulated;
};

static void race_rewind_accumulate(struct RACE_REWIND_STATE *rewind, legacy_u32 ticks,
								   legacy_u32 speed)
{
	legacy_u32 limit = (legacy_u32)rewind->origin_frame * RACE_REWIND_UNITS_PER_FRAME;
	legacy_u32 remaining = limit - rewind->accumulated;
	if (ticks > remaining / speed) {
		rewind->accumulated = limit;
	} else {
		rewind->accumulated += ticks * speed;
	}
}

static void race_rewind_seek(struct RACE_REWIND_STATE *rewind)
{
	legacy_u32 delta = timer_get_delta_alt();
	legacy_u32 base_ticks = RACE_REWIND_DOUBLE_AFTER_TICKS - rewind->held_ticks;
	if (base_ticks > delta) {
		base_ticks = delta;
	}
	rewind->held_ticks += base_ticks;
	race_rewind_accumulate(rewind, base_ticks, RACE_REWIND_BASE_SPEED);
	race_rewind_accumulate(rewind, delta - base_ticks, RACE_REWIND_DOUBLE_SPEED);
	legacy_u16 target =
		rewind->origin_frame - (legacy_u16)(rewind->accumulated / RACE_REWIND_UNITS_PER_FRAME);
	if (target != (legacy_u16)state.game_frame) {
		frame_supersight_reset();
		restore_gamestate(target);
		elapsed_time2 = target;
		while ((legacy_u16)state.game_frame != target) {
			update_gamestate();
		}
	}
}

static void race_rewind_resume(struct RACE_REWIND_STATE *rewind)
{
	/* The next timer tick records new input at the selected frame. Keep the
	 * restored crash/finish state so releasing Q cannot revive a wrecked car. */
	dos_interrupts_disable();
	if (recording_limit_warning_requested != 0 && elapsed_time1 == 0 &&
		(legacy_u16)state.game_frame < gameconfig.game_recordedframes) {
		recording_limit_warning_requested = 0;
		replay_overflow_acknowledged_word = LEGACY_S16_FROM_BITS(
			LEGACY_U16_REPLACE_LOW_BYTE(replay_overflow_acknowledged_word, 0U));
	}
	gameconfig.game_recordedframes = (legacy_u16)state.game_frame;
	elapsed_time2 = (legacy_u16)state.game_frame;
	replay_recording_flags = REPLAY_RECORDING_ACTIVE_FLAG | REPLAY_RECORDING_MODIFIED_FLAG;
	replay_playback_speed = REPLAY_PLAYBACK_NORMAL;
	frame_callback_countdown = (legacy_u8)timer_ticks_per_frame;
	race_exit_request = state.game_end_event != CRASH_EVENT_NONE &&
						state.game_frame_in_sec >= state.game_frames_per_sec;
	game_replay_mode = REPLAY_MODE_LIVE;
	is_in_replay = 0;
	dos_interrupts_enable();
	rewind->active = 0;
#ifdef RESTUNTS_SDL3
	sdl3_touch_set_rewind_active(0);
#endif
	kbormouse = 0;
	audio_carstate();
}

static legacy_s16 race_rewind_key_held(void)
{
	/* Control-Q belongs to the quit shortcut, including during an active rewind. */
	return kb_get_key_state(RACE_REWIND_SCAN_CODE) != 0 &&
		   kb_get_key_state(RACE_CONTROL_SCAN_CODE) == 0;
}

static void race_update_rewind(struct RACE_REWIND_STATE *rewind)
{
	if (rewind->active != 0) {
		if (race_rewind_key_held() != 0) {
			race_rewind_seek(rewind);
		} else {
			race_rewind_resume(rewind);
		}
		return;
	}

	if (idle_expired != 0 || game_replay_mode != REPLAY_MODE_LIVE ||
		state.game_inputmode == GAME_INPUT_MODE_WAITING ||
		state.game_end_event == CRASH_EVENT_EXIT ||
		(race_exit_request != 0 && (race_exit_request == REPLAY_EXIT_REQUESTED ||
									state.game_end_event == CRASH_EVENT_NONE)) ||
		race_rewind_key_held() == 0) {
		return;
	}

#ifdef RESTUNTS_SDL3
	/* Preserve held Q while rewinding temporarily uses the playback engine. */
	sdl3_touch_set_rewind_active(1);
#endif
	/* Freeze both recording and playback before touching the saved timeline.
	 * Pending timer input beyond the displayed frame is discarded on release. */
	dos_interrupts_disable();
	rewind->origin_frame = (legacy_u16)state.game_frame;
	elapsed_time2 = rewind->origin_frame;
	game_replay_mode = REPLAY_MODE_PLAYBACK;
	is_in_replay = 1;
	race_exit_request = 0;
	dos_interrupts_enable();
	rewind->accumulated = 0;
	rewind->held_ticks = 0;
	rewind->active = 1;
	replay_playback_speed = REPLAY_PLAYBACK_NORMAL;
	audio_carstate();
	(void)timer_get_delta_alt();
}

static legacy_u16 race_prepare_mode(void)
{
	if (idle_expired == 0) {
		if (gameconfig.game_recordedframes != 0) {
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

static void race_update_dashboard_layout(legacy_s16 rewind_active)
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
		if (supersight_enabled != 0 && dashbmp_y_copy > height_above_replaybar) {
			dashbmp_y_copy = height_above_replaybar;
		}
	}
}

static void race_update_viewport(struct RACE_VIEWPORT_CACHE *cache, legacy_s16 rewind_active)
{
	if (game_replay_mode != game_replay_mode_copy || dashb_toggle != dashb_toggle_copy ||
		replaybar_toggle != replaybar_toggle_copy || is_in_replay != is_in_replay_copy ||
		followOpponentFlag != followOpponentFlag_copy || cache->supersight != supersight_enabled) {
		cache->supersight = supersight_enabled;
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

static legacy_u32 race_ghost_frame(void)
{
	/* The truck sequence advances simulation frames before the race clock starts. */
	return game_replay_mode == REPLAY_MODE_PAUSED
			   ? 0
			   : (legacy_u32)(legacy_u16)state.game_frame + elapsed_time1;
}

#ifdef RESTUNTS_SDL3
#define RACE_PRESENTATION_VIEW_COUNT 13U
#define RACE_PRESENTATION_SLOW_INTERVAL_SCALE 2U
#define RACE_PRESENTATION_FAST_FRAME_SPAN 2
#define RACE_CONTROL_POLL_DELAY_MS 1U

struct RACE_PRESENTATION {
	struct PRESENTATION_CLOCK clock;
	struct GAMESTATE previous;
	struct GAMESTATE current;
	struct GAMESTATE interpolated;
	struct CARSTATE ghost_interpolated;
	struct GHOST_CAMERA_STATE ghost_camera_interpolated;
	legacy_u64 sample_time;
	legacy_u64 control_time;
	legacy_u16 sample_span;
	legacy_s16 sample_frame;
	legacy_s16 view[RACE_PRESENTATION_VIEW_COUNT];
	legacy_u8 history_valid;
	legacy_u8 ghost_valid;
	legacy_u8 started;
	legacy_u8 interpolating;
};

static struct RACE_PRESENTATION race_presentation;

/* Replay speed changes the interval between captured keyframes, not the visual rate. */
static legacy_u64 race_presentation_interval(void)
{
	legacy_u16 rate = framespersec > 0 ? (legacy_u16)framespersec : GAME_FRAME_RATE_NORMAL;
	legacy_u64 interval = PRESENTATION_SECOND_NS / rate;
	if (game_replay_mode == REPLAY_MODE_PLAYBACK && replay_playback_speed == REPLAY_PLAYBACK_SLOW) {
		interval *= RACE_PRESENTATION_SLOW_INTERVAL_SCALE;
	}
	return interval;
}

static legacy_u32 race_presentation_fraction(legacy_u64 now)
{
	if (race_presentation.history_valid == 0 ||
		race_presentation.sample_frame != state.game_frame) {
		return FRAME_INTERPOLATION_ONE;
	}
	legacy_u64 age = now > race_presentation.sample_time ? now - race_presentation.sample_time : 0;
	legacy_u64 interval = race_presentation_interval();
	if (age >= interval) {
		return FRAME_INTERPOLATION_ONE;
	}
	/* Each pair starts one visual slot into its completed physics interval:
	 * thirds at 20 Hz, sixths at 10 Hz. Keep the 60 Hz period rational so
	 * fractional slots share presentation_due's exact deadlines and the final
	 * slot reaches the keyframe. Late draws skip slots and hold the endpoint. */
	legacy_u64 elapsed =
		(age * PRESENTATION_RATE / PRESENTATION_SECOND_NS + 1U) * PRESENTATION_SECOND_NS;
	interval *= PRESENTATION_RATE;
	return elapsed < interval ? (legacy_u32)(elapsed * FRAME_INTERPOLATION_ONE / interval)
							  : FRAME_INTERPOLATION_ONE;
}

static void race_presentation_sync(legacy_s16 rewinding)
{
	legacy_s16 view[RACE_PRESENTATION_VIEW_COUNT] = {supersight_enabled,
													 cameramode,
													 followOpponentFlag,
													 game_replay_mode,
													 is_in_replay,
													 replay_playback_speed,
													 framespersec,
													 rewinding,
													 elapsed_time1,
													 custom_camera.distance,
													 custom_camera.elevation_angle,
													 custom_camera.azimuth_angle,
													 camera_track_height_offset};
	legacy_u8 changed = 0;
	for (legacy_u16 index = 0; index < sizeof(view) / sizeof(view[0]); index++) {
		if (race_presentation.view[index] != view[index]) {
			changed = 1;
			race_presentation.view[index] = view[index];
		}
	}
	if (changed != 0) {
		race_presentation.history_valid = 0;
		race_presentation.current = state;
		race_presentation.sample_frame = state.game_frame;
		race_presentation.sample_time = presentation_now();
		race_presentation.control_time = 0;
		race_presentation.ghost_valid = 0;
		race_presentation.started = 0;
		presentation_reset(&race_presentation.clock, presentation_now());
	}
}

static void race_presentation_capture(void)
{
	if (race_presentation.sample_frame == state.game_frame) {
		return;
	}
	legacy_s16 span = LEGACY_S16_WRAP_SUB(state.game_frame, race_presentation.sample_frame);
	race_presentation.history_valid = span == 1 || (span == RACE_PRESENTATION_FAST_FRAME_SPAN &&
													game_replay_mode == REPLAY_MODE_PLAYBACK &&
													replay_playback_speed == REPLAY_PLAYBACK_FAST);
	race_presentation.sample_span = race_presentation.history_valid != 0 ? (legacy_u16)span : 1;
	race_presentation.previous = race_presentation.current;
	race_presentation.current = state;
	race_presentation.sample_frame = state.game_frame;
	race_presentation.sample_time = presentation_now();
	presentation_reset(&race_presentation.clock, race_presentation.sample_time);
}

static void race_presentation_interpolate_ghost(legacy_u32 fraction)
{
	const struct CARSTATE *car = ghost_car_state();
	const struct GHOST_CAMERA_STATE *camera = ghost_camera_state();
	race_presentation.ghost_valid = car != 0 && camera != 0;
	if (race_presentation.ghost_valid == 0) {
		return;
	}
	race_presentation.ghost_interpolated = *car;
	race_presentation.ghost_camera_interpolated = *camera;
	if (race_presentation.history_valid == 0) {
		return;
	}
	legacy_u32 lag = race_presentation.sample_span * (FRAME_INTERPOLATION_ONE - fraction);
	/* Recorded ghosts can have a different tick rate. Sample their cached
	 * replay at this same visual instant instead of blending repeated poses. */
	(void)ghost_sample_render_pose(race_ghost_frame(), framespersec, lag,
								   &race_presentation.ghost_interpolated,
								   &race_presentation.ghost_camera_interpolated);
}

#endif

static void race_draw_frame(void)
{
#ifdef RESTUNTS_SDL3
	legacy_s32 render_scale =
		supersight_enabled != 0 ? frame_adaptive_render_scale(&frame_adaptive) : HIRES_SCALE;
	if (hires_render_scale() != render_scale) {
		hires_set_render_scale(render_scale);
		/* Companion pixels were invalidated; rebuild static dashboard/replay art. */
		full_redraw_frames_remaining = video_page_count;
	}
	sdl3_video_begin_track_frame(supersight_enabled);
#endif
	if (full_redraw_frames_remaining != 0) {
		replay_controls_drawn[dashboard_buffer_index] = 0;
		if (dashboard_visible != 0) {
			sprite_set_target_clip_bounds(0, RACE_SCREEN_WIDTH, dashbmp_y_copy,
										  height_above_replaybar);
			setup_car_shapes(DASHBOARD_OPERATION_REDRAW_STATIC);
		}

		if (replaybar_enabled != 0) {
			sprite_set_target_clip_bounds(0, RACE_SCREEN_WIDTH, 0, RACE_SCREEN_HEIGHT);
			loop_game(REPLAY_LOOP_DRAW_CONTROLS, state.game_frame, state.game_frame);
		}
	} else {
		if (replaybar_enabled == 0) {
			replay_controls_drawn[dashboard_buffer_index] = 0;
		}
	}

#ifdef RESTUNTS_SDL3
	if (race_presentation.interpolating != 0) {
		update_frame_snapshot(
			frame_buffer_index, &rect_windshield, &race_presentation.interpolated,
			race_presentation.ghost_valid != 0 ? &race_presentation.ghost_interpolated : 0,
			race_presentation.ghost_valid != 0 ? &race_presentation.ghost_camera_interpolated : 0);
	} else
#endif
	{
		update_frame(frame_buffer_index, &rect_windshield);
	}
	struct RECTANGLE dashboard_mask_rect;
	if (dastbmp_y != 0 && dashboard_visible != 0) {
		if (slow_video_mgmt_copy != 0) {
			dashboard_mask_rect.left = 0;
			dashboard_mask_rect.right = RACE_SCREEN_WIDTH;
			dashboard_mask_rect.top = dastbmp_y;
			dashboard_mask_rect.bottom = dashbmp_y_copy;
			if (active_frame_rects != 0) {
				rect_union(active_frame_rects, &dashboard_mask_rect, active_frame_rects);
			}
		}

		if (supersight_enabled != 0 && replaybar_enabled != 0) {
			/* Custom dashboard tops can extend below the replay toolbar boundary. */
			sprite_set_target_clip_bounds(0, RACE_SCREEN_WIDTH, 0, height_above_replaybar);
			shape2d_rle_mask_position_clipped(dasmshapeptr);
			shape2d_rle_or_position_clipped(
				(struct SHAPE2D far *)dos_memory_make_pointer(dastseg, dastbmp_y2));
		} else {
			shape2d_render_bmp_as_mask(dasmshapeptr);
			shape2d_rle_or_far_pointer(dastbmp_y2, dastseg);
		}
	}

	frame_present(&rect_windshield);
	frame_fps_present_roof();
	if (dashboard_visible != 0) {
		sprite_set_target_clip_bounds(0, RACE_SCREEN_WIDTH, dashbmp_y_copy, height_above_replaybar);
		setup_car_shapes(DASHBOARD_OPERATION_UPDATE);
		sprite_set_target_clip_bounds(0, RACE_SCREEN_WIDTH, 0, RACE_SCREEN_HEIGHT);
	}
	if (frame_display_overlay_active() != 0) {
		/* Keep diagnostic text above every layer of custom dashboard artwork. */
		mouse_draw_opaque_check();
		frame_fps_draw_text();
		mouse_draw_transparent_check();
	}

	if (full_redraw_frames_remaining != 0) {
		full_redraw_frames_remaining--;
	}

	if (video_uses_page_flipping != 0) {
		mouse_draw_opaque_check();
		sprite_present_mcga_backbuffer();
		frame_buffer_index ^= 1;
		dashboard_buffer_index = frame_buffer_index;
		mouse_draw_transparent_check();
	}
	frame_fps_record_presented();
#ifdef RESTUNTS_SDL3
	sdl3_video_end_frame();
#endif
}

#ifdef RESTUNTS_SDL3
static void race_draw_visual(void)
{
	legacy_u32 fraction = race_presentation_fraction(presentation_now());
	frame_interpolate_state(&race_presentation.interpolated, &state, &race_presentation.previous,
							fraction);
	race_presentation_interpolate_ghost(fraction);

	if (video_uses_page_flipping != 0) {
		sprite_select_mcga_backbuffer();
		dashboard_buffer_index = frame_buffer_index;
	} else {
		sprite_select_render_window();
	}
	race_presentation.interpolating = 1;
	race_draw_frame();
	race_presentation.interpolating = 0;
}

static void race_present_interpolation(void)
{
	/* Extra presentations blend captured states without stepping physics.
	 * Replay, controls and timer callbacks retain their authoritative cadence. */
	if (supersight_enabled == 0 || race_presentation.started == 0 ||
		state.game_frame != elapsed_time2 || state.game_inputmode == GAME_INPUT_MODE_WAITING ||
		(game_replay_mode == REPLAY_MODE_PLAYBACK && is_in_replay != 0) || race_exit_request != 0) {
		return;
	}
	if (presentation_due(&race_presentation.clock, presentation_now()) != 0) {
		race_draw_visual();
	}
}
#endif

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

static legacy_u16 race_frame_is_ready(legacy_s16 *last_processed_frame)
{
	if (state.game_frame != elapsed_time2) {
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
	if (supersight_enabled != 0 && (game_replay_mode == REPLAY_MODE_PAUSED ||
									state.game_inputmode == GAME_INPUT_MODE_WAITING)) {
		legacy_u64 now = presentation_now();
		if (now < race_presentation.control_time) {
			sdl3_platform_delay(RACE_CONTROL_POLL_DELAY_MS);
			sdl3_platform_pump();
			return 0;
		}
		legacy_u16 rate = framespersec > 0 ? (legacy_u16)framespersec : GAME_FRAME_RATE_NORMAL;
		race_presentation.control_time = now + PRESENTATION_SECOND_NS / rate;
		*last_processed_frame = state.game_frame;
		return 1;
	}
	if (supersight_enabled != 0 && game_replay_mode == REPLAY_MODE_PLAYBACK && is_in_replay == 0) {
		wait_for_sample = 1;
	}
#endif
	if (wait_for_sample != 0 && race_exit_request == 0 &&
		state.game_inputmode != GAME_INPUT_MODE_WAITING) {
		if (*last_processed_frame == state.game_frame) {
#ifdef RESTUNTS_SDL3
			/* Timer callbacks are dispatched on this thread. Yield while waiting
			 * for the next input sample, then deliver elapsed 100 Hz ticks. */
			sdl3_platform_delay(RACE_CONTROL_POLL_DELAY_MS);
			sdl3_platform_pump();
#endif
			return 0;
		}
		*last_processed_frame = state.game_frame;
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
											  legacy_s16 *last_processed_frame,
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
	if (game_replay_mode == REPLAY_MODE_LIVE && state.game_frame != elapsed_time2) {
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
	legacy_s16 last_processed_frame = -1;
	struct RACE_REWIND_STATE rewind = {0};
#ifdef RESTUNTS_SDL3
	legacy_u8 controller_menu_pending = 0;
#endif
	frame_supersight_reset();
	frame_fps_reset();
#ifdef RESTUNTS_SDL3
	race_presentation = (struct RACE_PRESENTATION){0};
	presentation_reset(&race_presentation.clock, presentation_now());
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
			if (rewind.active == 0 && state.game_frame == last_processed_frame) {
				race_present_interpolation();
			}
#endif
			continue;
		}
		if (state.game_inputmode == GAME_INPUT_MODE_WAITING &&
			game_replay_mode == REPLAY_MODE_LIVE) {
			elapsed_time2 = 0;
			gameconfig.game_recordedframes = 0;
			state.game_frame = 0;
		}

		if (slow_video_mgmt_copy != slow_video_mgmt) {
			slow_video_mgmt_copy = slow_video_mgmt;
			init_rect_arrays();
		}

		if (rewind.active == 0) {
			race_check_recording_limit();
		}
		if (video_uses_page_flipping != 0) {
			sprite_select_mcga_backbuffer();
			dashboard_buffer_index = frame_buffer_index;
		} else {
			sprite_select_render_window();
		}

		ghost_update(race_ghost_frame(), framespersec);
		race_update_viewport(cache, rewind.active);
#ifdef RESTUNTS_SDL3
		race_presentation_capture();
		if (supersight_enabled == 0 || rewind.active != 0 ||
			(game_replay_mode == REPLAY_MODE_PLAYBACK && is_in_replay != 0) ||
			presentation_due(&race_presentation.clock, presentation_now()) != 0) {
			if (supersight_enabled != 0) {
				race_draw_visual();
			} else {
				race_draw_frame();
			}
			race_presentation.started = 1;
		}
#else
		race_draw_frame();
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
				format_frame_as_string(&resID_byte1, state.game_frame + elapsed_time1, 1);
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
			if (RACE_FRAME_LIMIT_MULTIPLIER * framespersec == state.game_frame + elapsed_time1) {
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
	/* Auto restores menu resolution; a manually locked scale stays selected. */
	hires_set_render_scale(frame_adaptive.preset == FRAME_ADAPTIVE_PRESET_AUTO
							   ? HIRES_SCALE
							   : frame_adaptive_render_scale(&frame_adaptive));
#endif
}

void run_game(void)
{
	rect_windshield.left = 0;
	rect_windshield.right = RACE_SCREEN_WIDTH;
	struct RACE_VIEWPORT_CACHE cache;
	cache.roof_height = -1;
	cache.dashboard_bottom = -1;
	cache.supersight = supersight_enabled;
	viewport_bottom_cache = -1;
	run_game_random = LEGACY_S16_SHL(get_kevinrandom(), RACE_RANDOM_VALUE_SHIFT);
	replaybar_toggle = 1;
	is_in_replay = 0;
	if (race_prepare_mode() == 0) {
		return;
	}
	if (idle_expired == 0 && gameconfig.game_recordedframes == 0 && ghost_prepare_race() != 0) {
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
		race_initialize_state();
#ifdef RESTUNTS_SDL3
		sdl3_touch_set_game_active(1);
		sdl3_input_set_gameplay_active(idle_expired == 0);
#endif
		race_run_frames(&cache);
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
