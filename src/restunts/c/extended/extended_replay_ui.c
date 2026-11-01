#include "extended_replay_ui.h"
#ifdef RESTUNTS_SDL3
#include "../fileio.h"
#include "../game_input.h"
#include "../math.h"
#include "../memmgr.h"
#include "../menu/menu_internal.h"
#include "../race/race_resources.h"
#include "../replay/replay.h"
#include "../replay/replay_viewer.h"
#include "../ghost/ghost.h"
#include "../replay/replay_viewer_internal.h"
#include "../resource.h"
#include "../shape2d/shape2d.h"
#include "../timing.h"
#include "../ui/ui_dialog.h"
#include "../ui/ui_input.h"
#include "../ui/ui_text.h"
#include "../crash_state.h"
#include "../track/track_objects.h"
#include "../camera.h"
#include "../video_frame.h"
#include "../race/race_graphics.h"
#include "../car/car_audio.h"
#include "../externs.h"
#include "../keyboard.h"
#include "../frame/frame_internal.h"
#ifdef RESTUNTS_AMIGA
#include "../../platform/amiga/amiga.h"
#endif
#ifdef RESTUNTS_SDL3
#include "extended_replay.h"
#include "extended_replay_runtime.h"
#include "extended_track.h"
#include "extended_file.h"
#include "../../platform/sdl3/touch_game.h"
#include "../../platform/sdl3/controller_game.h"
#endif

#define REPLAY_SCRUB_ACCELERATION_DIVISOR 50L
#define REPLAY_SCRUB_INITIAL_SPEED 3
#define REPLAY_SCRUB_FIXED_SCALE 20L
#define REPLAY_SCRUB_MAX_SPEED 100
#define REPLAY_INPUT_SETTLE_DELTA 1000

legacy_s16 extended_replay_ui_scrub(legacy_s16 direction)
{
	if (!extended_replay_is_active()) {
		return 0;
	}
	legacy_u64 accumulated =
		replay_scrub_begin(direction > 0 ? REPLAY_CONTROL_FAST_FORWARD : REPLAY_CONTROL_REWIND);
	legacy_u32 origin = extended_replay_input_frame();
	legacy_u32 limit = direction > 0 ? extended_replay_recorded_frames() - origin : origin;
	legacy_u64 maximum = (legacy_u64)limit * REPLAY_SCRUB_FIXED_SCALE;
	if (accumulated > maximum) {
		accumulated = maximum;
	}
	legacy_u32 amount = (legacy_u32)(accumulated / REPLAY_SCRUB_FIXED_SCALE);
	legacy_u32 target = direction > 0 ? origin + amount : origin - amount;
	legacy_u8 menu_requested = 0;
	while (((legacy_u8)input_combined_flags & INPUT_ACTION_BUTTON_MASK) != 0U) {
		if (sdl3_controller_take_menu_request() != 0) {
			menu_requested = 1;
			break;
		}
		/* Match classic toolbar acceleration without narrowing long CSRPL distances. */
		legacy_u64 speed =
			accumulated / REPLAY_SCRUB_ACCELERATION_DIVISOR + REPLAY_SCRUB_INITIAL_SPEED;
		if (speed > REPLAY_SCRUB_MAX_SPEED) {
			speed = REPLAY_SCRUB_MAX_SPEED;
		}
		legacy_u32 delta = timer_get_delta_alt();
		accumulated += (legacy_u64)delta * speed;
		if (accumulated > maximum) {
			accumulated = maximum;
		}
		amount = (legacy_u32)(accumulated / REPLAY_SCRUB_FIXED_SCALE);
		target = direction > 0 ? origin + amount : origin - amount;
		replay_controls_draw(extended_replay_frame(), target);
		input_do_checking((legacy_s16)(delta > LEGACY_S16_MAX ? LEGACY_S16_MAX : delta));
	}
	frame_supersight_reset();
	replay_controls_select(REPLAY_CONTROL_PAUSE);
	replay_draw_waiting();
	(void)extended_replay_seek(target);
	replay_controls_draw(extended_replay_frame(), target);
	input_do_checking(REPLAY_INPUT_SETTLE_DELTA);
	if (menu_requested) {
		replay_pause_menu();
	}
	return 1;
}
legacy_s16 extended_replay_ui_format_time(legacy_s8 *out, legacy_u32 frame, legacy_s16 fraction)
{
	if (!extended_replay_is_active()) {
		return 0;
	}
	extended_replay_format_runtime_time(out, frame, fraction);
	return 1;
}
legacy_s16 extended_replay_ui_format_current_time(legacy_s8 *out)
{
	return extended_replay_ui_format_time(out, extended_replay_frame(), 1);
}
legacy_u32 extended_replay_ui_displayed_time(legacy_u32 current, legacy_u32 fallback)
{
	return extended_replay_is_active() ? current : fallback;
}
legacy_s16 extended_replay_ui_redraw_time(void)
{
	return extended_replay_is_active();
}
legacy_s16 extended_replay_ui_uses_legacy_limit(void)
{
	return !extended_replay_is_active();
}
legacy_s16 extended_replay_ui_timeline(legacy_u32 recorded, legacy_u32 current, legacy_u16 range,
									   legacy_s16 *recorded_position, legacy_s16 *current_position)
{
	if (!extended_replay_is_active()) {
		return 0;
	}
	*recorded_position = (legacy_s16)extended_replay_timeline_position(
		recorded, extended_replay_recorded_frames(), range);
	*current_position = (legacy_s16)extended_replay_timeline_position(
		current, extended_replay_recorded_frames(), range);
	return 1;
}
legacy_s16 extended_replay_ui_restart(void)
{
	if (!extended_replay_is_active()) {
		return 0;
	}
	extended_replay_runtime_begin();
	return 1;
}
legacy_s16 extended_replay_ui_branch(void)
{
	if (!extended_replay_is_active()) {
		return 0;
	}
	(void)extended_replay_branch(extended_replay_frame());
	return 1;
}
legacy_s16 extended_replay_ui_seek(legacy_u32 frame)
{
	if (!extended_replay_is_active()) {
		return 0;
	}
	(void)extended_replay_seek(frame);
	return 1;
}
legacy_s16 extended_replay_ui_seek_end(void)
{
	return extended_replay_ui_seek(extended_replay_recorded_frames());
}
legacy_s16 extended_replay_ui_seek_draw(legacy_u32 frame)
{
	if (!extended_replay_ui_seek(frame)) {
		return 0;
	}
	replay_controls_draw(extended_replay_frame(), frame);
	return 1;
}
legacy_s16 extended_replay_ui_draw_current(void)
{
	if (!extended_replay_is_active()) {
		return 0;
	}
	replay_controls_draw(extended_replay_frame(), extended_replay_frame());
	return 1;
}
void extended_replay_ui_begin_live_track(void)
{
	if (extended_track_active() && is_in_replay == 0) {
		extended_replay_runtime_begin();
	}
}
const legacy_s8 *extended_replay_ui_extension(const legacy_s8 *fallback)
{
	return extended_replay_is_active() ? (const legacy_s8 *)".csrpl" : fallback;
}
legacy_s16 extended_replay_ui_load_error(legacy_s8 saved_flags)
{
	if (strcmp(file_dialog_selected_extension(), ".csrpl") != 0) {
		return 0;
	}
	replay_recording_flags = saved_flags;
	show_dialog(DIALOG_TYPE_ACKNOWLEDGEMENT, DIALOG_SAVE_BACKGROUND, (void *)extended_file_error(),
				DIALOG_AUTO_POSITION, DIALOG_AUTO_POSITION, dialog_border_color, 0, 0);
	return 1;
}
legacy_s16 extended_replay_ui_advance_input(legacy_s16 *handled)
{
	*handled = extended_replay_is_active();
	return *handled && extended_replay_advance_input();
}
legacy_s16 extended_replay_ui_record_input(legacy_u8 input)
{
	if (!extended_replay_is_active()) {
		return 0;
	}
	if (!extended_replay_record_input(input)) {
		race_exit_request = 1;
	}
	return 1;
}
legacy_s16 extended_replay_ui_or_input(legacy_u8 action)
{
	if (!extended_replay_is_active()) {
		return 0;
	}
	legacy_u32 frame = extended_replay_frame();
	(void)extended_replay_replace_input(frame,
										(legacy_u8)(extended_replay_input_at(frame) | action));
	return 1;
}
#endif
