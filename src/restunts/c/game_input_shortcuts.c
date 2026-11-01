#include "fileio.h"
#include "game_input.h"
#include "keyboard.h"
#include "memmgr.h"
#include "platform.h"
#include "resource.h"
#include "shape2d/shape2d.h"
#include "timing.h"
#include "ui/ui_text.h"
#include "crash_state.h"
#include "camera.h"
#include "externs.h"
#include "ghost.h"
#include "frame/frame_internal.h"
#include "hires/settings.h"
#ifdef RESTUNTS_AMIGA
#include "../platform/amiga/amiga.h"
#endif
#ifdef RESTUNTS_SDL3
#include "../platform/sdl3/controller_game.h"
#endif

#define CUSTOM_CAMERA_CHASE_RIGHT_STEPS 3
#define CUSTOM_CAMERA_CHASE_DOWN_STEPS 2
#define CUSTOM_CAMERA_CHASE_ZOOM_IN_STEPS 2
#define CUSTOM_CAMERA_CHASE_ZOOM_OUT_STEPS 3

#define CUSTOM_CAMERA_CHASE_AZIMUTH                                                                \
	(CUSTOM_CAMERA_INITIAL_AZIMUTH_ANGLE + CUSTOM_CAMERA_CHASE_RIGHT_STEPS * CAMERA_ANGLE_STEP)
#define CUSTOM_CAMERA_CHASE_ELEVATION                                                              \
	(CUSTOM_CAMERA_INITIAL_ELEVATION_ANGLE - CUSTOM_CAMERA_CHASE_DOWN_STEPS * CAMERA_ANGLE_STEP)

enum CUSTOM_CAMERA_PRESET {
	CUSTOM_CAMERA_PRESET_ORIGINAL,
	CUSTOM_CAMERA_PRESET_CLOSE,
	CUSTOM_CAMERA_PRESET_NORMAL,
	CUSTOM_CAMERA_PRESET_FAR,
	CUSTOM_CAMERA_PRESET_COUNT
};

static legacy_u8 custom_camera_preset = CUSTOM_CAMERA_PRESET_ORIGINAL;
static struct CUSTOM_CAMERA custom_camera_original;
static legacy_s8 custom_camera_original_dashboard;

static legacy_u8 h_key_toggle;

static legacy_s16 input_handle_display_shortcut(legacy_s16 key)
{
	switch (key) {
		case KEY_F11:
			fps_display_enabled = fps_display_enabled == FRAME_FPS_DISPLAY_OFF
									  ? FRAME_FPS_DISPLAY_ON
									  : FRAME_FPS_DISPLAY_OFF;
			frame_fps_reset();
			full_redraw_frames_remaining = (legacy_s8)video_page_count;
			return 1;

#ifdef RESTUNTS_SDL3
		case KEY_SHIFT_F12:
#endif
		case KEY_F12:
			hires_settings_select_display(key);
			return 1;

		case 'D':
		case 'd':
			dashb_toggle ^= 1;
			return 1;

		case 'H':
		case 'h':
			h_key_toggle ^= 1;
			return 1;

		case 'M':
		case 'm':
			select_mouse_driving();
			mouse_minmax_position(LEGACY_S8_FROM_BITS(mouse_driving_enabled));
			return 1;

		case 'R':
		case 'r':
			replaybar_toggle ^= 1;
			return 1;
	}
	return 0;
}

static void input_restore_custom_camera(void)
{
	if (custom_camera_preset != CUSTOM_CAMERA_PRESET_ORIGINAL) {
		custom_camera = custom_camera_original;
		dashb_toggle = custom_camera_original_dashboard;
		custom_camera_preset = CUSTOM_CAMERA_PRESET_ORIGINAL;
	}
}

void camera_select_mode(legacy_s8 mode)
{
	if (mode != cameramode) {
		input_restore_custom_camera();
	}
	cameramode = mode;
}

static void input_cycle_custom_camera(void)
{
	static const legacy_s16 distances[CUSTOM_CAMERA_PRESET_COUNT] = {
		CUSTOM_CAMERA_INITIAL_DISTANCE,
		CUSTOM_CAMERA_INITIAL_DISTANCE - CUSTOM_CAMERA_CHASE_ZOOM_IN_STEPS * CAMERA_ZOOM_STEP,
		CUSTOM_CAMERA_INITIAL_DISTANCE,
		CUSTOM_CAMERA_INITIAL_DISTANCE + CUSTOM_CAMERA_CHASE_ZOOM_OUT_STEPS * CAMERA_ZOOM_STEP};

	if (cameramode != CAMERA_MODE_CUSTOM) {
		camera_select_mode(CAMERA_MODE_CUSTOM);
		return;
	}
	if (custom_camera_preset == CUSTOM_CAMERA_PRESET_FAR) {
		input_restore_custom_camera();
		return;
	}
	if (custom_camera_preset == CUSTOM_CAMERA_PRESET_ORIGINAL) {
		custom_camera_original = custom_camera;
		custom_camera_original_dashboard = dashb_toggle;
	}
	custom_camera_preset++;
	custom_camera.distance = distances[custom_camera_preset];
	custom_camera.elevation_angle = CUSTOM_CAMERA_CHASE_ELEVATION;
	custom_camera.azimuth_angle = CUSTOM_CAMERA_CHASE_AZIMUTH;
	dashb_toggle = 0;
}

static legacy_s16 input_handle_camera_shortcut(legacy_s16 key)
{
	switch (key) {
		case 'C':
		case 'c':
			if (game_replay_mode != REPLAY_MODE_PAUSED) {
				camera_select_mode((legacy_s8)(((legacy_u8)cameramode + 1U) & CAMERA_MODE_MASK));
			}
			return 1;

		case KEY_F1:
			camera_select_mode(CAMERA_MODE_COCKPIT);
			return 1;
		case KEY_F2:
			camera_select_mode(CAMERA_MODE_FOLLOW);
			return 1;
		case KEY_F3:
			input_cycle_custom_camera();
			return 1;
		case KEY_F4:
			camera_select_mode(CAMERA_MODE_TRACKSIDE);
			return 1;
	}
	return 0;
}

#ifdef RESTUNTS_SDL3
legacy_s16 input_update_controller_camera(void)
{
	legacy_u16 flags = (legacy_u16)sdl3_controller_camera_flags();
	if (cameramode != CAMERA_MODE_CUSTOM) {
		return 0;
	}
	legacy_s16 changed = 0;
	if ((flags & INPUT_STEER_RIGHT_FLAG) != 0) {
		changed |= camera_adjust_custom_direction(KEY_RIGHT);
	} else if ((flags & INPUT_STEER_LEFT_FLAG) != 0) {
		changed |= camera_adjust_custom_direction(KEY_LEFT);
	}
	if ((flags & INPUT_ACCELERATE_FLAG) != 0) {
		changed |= camera_adjust_custom_direction(KEY_UP);
	} else if ((flags & INPUT_BRAKE_FLAG) != 0) {
		changed |= camera_adjust_custom_direction(KEY_DOWN);
	}
	return changed;
}
#endif

legacy_s16 handle_ingame_kb_shortcuts(legacy_s16 key)
{
	if (input_handle_display_shortcut(key) || input_handle_camera_shortcut(key)) {
		return 1;
	}
	switch (key) {
		case KEY_ESCAPE:
			if (game_replay_mode == REPLAY_MODE_LIVE) {
				update_crash_state(CRASH_EVENT_EXIT, PLAYER_CAR_INDEX);
			}
			race_exit_request = 1;
			return 1;

		case 't':
			if (followOpponentFlag != 0 || gameconfig.game_opponenttype != 0 ||
				ghost_car_state() != 0) {
				followOpponentFlag ^= 1;
			}
			return 1;
	}

	if (game_replay_mode != REPLAY_MODE_PAUSED) {
		return 0;
	}

	game_replay_mode = REPLAY_MODE_LIVE;
	race_start_sequence_state = RACE_START_SEQUENCE_INACTIVE;
	init_game_state_with_frame_rate_byte(configured_frame_rate);
	return 1;
}
