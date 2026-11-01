#ifndef RESTUNTS_REPLAY_VIEWER_PRIVATE_H
#define RESTUNTS_REPLAY_VIEWER_PRIVATE_H

#include "../extended/extended_menu_runtime.h"
#include "../extended/extended_replay_ui.h"
#include "../fileio.h"
#include "../game_input.h"
#include "../math/math.h"
#include "../memmgr.h"
#include "../menu/menu_internal.h"
#include "../race/race_resources.h"
#include "replay.h"
#include "replay_viewer.h"
#include "../ghost/ghost.h"
#include "replay_viewer_internal.h"
#include "../resource.h"
#include "../shape2d/shape2d.h"
#include "../timing.h"
#include "../ui/ui_dialog.h"
#include "../ui/ui_input.h"
#include "../ui/ui_text.h"
#include "../crash_state.h"
#include "../track/track_objects.h"
#include "../camera.h"
#include "../video/video_frame.h"
#include "../race/race_graphics.h"
#include "../car/car_audio.h"
#include "../externs.h"
#include "../keyboard.h"
#include "../frame/frame_internal.h"
#ifdef RESTUNTS_AMIGA
#include "../../platform/amiga/amiga.h"
#endif
#ifdef RESTUNTS_SDL3
#include "../extended/extended_replay_runtime.h"
#include "../../platform/sdl3/touch_game.h"
#include "../../platform/sdl3/controller_game.h"
#endif

#define REPLAY_PLAYER_COUNT 2U
#define REPLAY_CONTROL_COUNT 9U
#define REPLAY_ACTION_CONTROL_COUNT 7U
#define REPLAY_LAST_ACTION_CONTROL (REPLAY_ACTION_CONTROL_COUNT - 1U)
#define REPLAY_CONTROL_PLAYER_STRIDE REPLAY_PLAYER_COUNT
#define REPLAY_NO_SELECTION 255U

#define REPLAY_SHAPE_BACKGROUND 0U
#define REPLAY_SHAPE_CAMERA_FIRST 1U
#define REPLAY_SHAPE_CONTROL_INACTIVE_FIRST 5U
#define REPLAY_SHAPE_CONTROL_ACTIVE_FIRST 14U

#define REPLAY_TOTAL_TIME_X 216
#define REPLAY_CURRENT_TIME_X 152
#define REPLAY_TIME_Y 187
#define REPLAY_TIMELINE_X 154
#define REPLAY_TIMELINE_Y 177
#define REPLAY_TIMELINE_WIDTH 116
#define REPLAY_TIMELINE_POSITION_RANGE 110U
#define REPLAY_TIMELINE_HEIGHT 6
#define REPLAY_TIMELINE_CURSOR_EDGE_OFFSET 5
#define REPLAY_TIMELINE_CURSOR_RIGHT_X (REPLAY_TIMELINE_X + REPLAY_TIMELINE_CURSOR_EDGE_OFFSET)
#define REPLAY_TIMELINE_CURSOR_BOTTOM_Y (REPLAY_TIMELINE_Y + REPLAY_TIMELINE_CURSOR_EDGE_OFFSET)
#define REPLAY_WAITING_TEXT_Y 100
#define REPLAY_TIME_INCLUDE_FRACTION 1

#define REPLAY_PAUSE_OPTION_COUNT 8U
#define REPLAY_MODE_OPTION_COUNT 5U
#define CAR_ID_LENGTH 4U
#define TRACK_SKYBOX_ELEMENT_INDEX 900U
#define REPLAY_LOAD_WAIT_VALUE 150

#define REPLAY_SCRUB_ACCELERATION_DIVISOR 50L
#define REPLAY_SCRUB_INITIAL_SPEED 3
#define REPLAY_SCRUB_FIXED_SCALE 20L
#define REPLAY_SCRUB_MAX_SPEED 100
#define REPLAY_INPUT_SETTLE_DELTA 1000
#define REPLAY_RESTART_WAIT_TICKS 50UL

#define REPLAY_CUSTOM_CAMERA_MIN_DISTANCE 120
#define REPLAY_CUSTOM_CAMERA_MAX_DISTANCE 1500
#define REPLAY_TRACK_CAMERA_MAX_HEIGHT 900
#define REPLAY_CUSTOM_CAMERA_ELEVATION_LIMIT CUSTOM_CAMERA_ELEVATION_LIMIT
#define REPLAY_CUSTOM_CAMERA_MODIFIER_SCAN_CODE 29

#define REPLAY_DIRECTION_ANGLE_SHIFT 8U
#define REPLAY_DIRECTION_MASK 3U
#define REPLAY_DIALOG_INITIAL_CHOICE 0
#define REPLAY_DIALOG_ACCEPTED_MINIMUM 1
#define REPLAY_DIALOG_CONFIRMED_CHOICE 1
#define REPLAY_RESTART_DIALOG_CAPACITY 512U

#define REPLAY_FILE_CHECK_ARGUMENT 2

#define REPLAY_BAR_HIDDEN_STATE (-1)
#define REPLAY_FIRST_FRAME 0
#define REPLAY_SINGLE_FRAME_DELTA 1

enum REPLAY_DIRECTION {
	REPLAY_DIRECTION_UP = 0,
	REPLAY_DIRECTION_RIGHT = 1,
	REPLAY_DIRECTION_DOWN = 2,
	REPLAY_DIRECTION_LEFT = 3
};

enum REPLAY_PAUSE_ACTION {
	REPLAY_PAUSE_ACTION_FINISH = 1,
	REPLAY_PAUSE_ACTION_RESTART = 2,
	REPLAY_PAUSE_ACTION_CONTINUE = 3,
	REPLAY_PAUSE_ACTION_LOAD = 4,
	REPLAY_PAUSE_ACTION_SAVE = 5,
	REPLAY_PAUSE_ACTION_DISPLAY_OPTIONS = 6,
	REPLAY_PAUSE_ACTION_EXIT = 7
};

enum REPLAY_MODE_ACTION {
	REPLAY_MODE_ACTION_DASHBOARD = 0,
	REPLAY_MODE_ACTION_BAR = 1,
	REPLAY_MODE_ACTION_CAMERA = 2,
	REPLAY_MODE_ACTION_DETAIL = 3,
	REPLAY_MODE_ACTION_FOLLOW_OPPONENT = 4
};

enum REPLAY_SAVE_STATUS {
	REPLAY_SAVE_CANCELLED = -1,
	REPLAY_SAVE_RETRY = 0,
	REPLAY_SAVE_READY = 1
};

#ifdef RESTUNTS_SDL3
#define REPLAY_TOUCH_SKIP_SECONDS 10L
#define REPLAY_TOUCH_LEFT_BUTTON 1U
#define REPLAY_TOUCH_NO_SEEK (-1L)

#endif

legacy_u32 replay_current_frame(void);
legacy_u32 replay_input_frame(void);
legacy_u32 replay_recorded_frames(void);
void replay_controls_select(legacy_u8 selection);
void replay_controls_draw(legacy_u32 recorded_frame, legacy_u32 current_frame);
void replay_draw_waiting(void);
void replay_pause_menu(void);
legacy_s32 replay_scrub_begin(legacy_u8 selection);
void replay_fast_forward(void);
void replay_rewind(void);
void loop_game(legacy_s16 operation, legacy_s16 recorded_frame, legacy_s16 current_frame);

#endif
