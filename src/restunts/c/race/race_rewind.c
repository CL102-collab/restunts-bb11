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
#include "../video/video_frame.h"
#include "../car/car_audio.h"
#include "../audio/audio_control.h"
#include "../externs.h"
#include "../keyboard.h"
#include "../timing.h"
#include "../frame/frame_internal.h"

legacy_u32 race_current_frame(void)
{
#ifdef RESTUNTS_SDL3
	return extended_replay_frame();
#else
	return (legacy_u16)state.game_frame;
#endif
}

legacy_u32 race_input_frame(void)
{
#ifdef RESTUNTS_SDL3
	return extended_replay_input_frame();
#else
	return elapsed_time2;
#endif
}

legacy_u32 race_recorded_frames(void)
{
#ifdef RESTUNTS_SDL3
	return extended_replay_recorded_frames();
#else
	return gameconfig.game_recordedframes;
#endif
}

void race_set_input_frame(legacy_u32 frame)
{
#ifdef RESTUNTS_SDL3
	extended_replay_set_input_frame(frame);
#else
	elapsed_time2 = (legacy_u16)frame;
#endif
}

static void race_rewind_accumulate(struct RACE_REWIND_STATE *rewind, legacy_u32 ticks,
								   legacy_u32 speed)
{
	legacy_u64 limit = (legacy_u64)rewind->origin_frame * RACE_REWIND_UNITS_PER_FRAME;
	legacy_u64 remaining = limit - rewind->accumulated;
	if (ticks > remaining / speed) {
		rewind->accumulated = limit;
	} else {
		rewind->accumulated += (legacy_u64)ticks * speed;
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
	legacy_u32 target =
		rewind->origin_frame - (legacy_u32)(rewind->accumulated / RACE_REWIND_UNITS_PER_FRAME);
	if (target != race_current_frame()) {
		frame_supersight_reset();
#ifdef RESTUNTS_SDL3
		if (extended_replay_ui_seek(target)) {
			return;
		}
#endif
		restore_gamestate(target);
		elapsed_time2 = target;
		while ((legacy_u16)state.game_frame != target) {
			update_gamestate();
		}
	}
}

void race_rewind_resume(struct RACE_REWIND_STATE *rewind)
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
#ifdef RESTUNTS_SDL3
	if (!extended_replay_ui_branch())
#endif
	{
		gameconfig.game_recordedframes = (legacy_u16)state.game_frame;
		elapsed_time2 = (legacy_u16)state.game_frame;
	}
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

void race_update_rewind(struct RACE_REWIND_STATE *rewind)
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
	rewind->origin_frame = race_current_frame();
	race_set_input_frame(rewind->origin_frame);
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
