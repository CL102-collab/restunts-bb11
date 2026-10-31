#ifndef RESTUNTS_EXTENDED_REPLAY_UI_H
#define RESTUNTS_EXTENDED_REPLAY_UI_H
#include "legacy.h"
void replay_controls_draw(legacy_u32 recorded_frame, legacy_u32 current_frame);
void replay_controls_select(legacy_u8 selection);
void replay_draw_waiting(void);
void replay_pause_menu(void);
legacy_s32 replay_scrub_begin(legacy_u8 selection);
#ifdef RESTUNTS_SDL3
legacy_s16 extended_replay_ui_scrub(legacy_s16 direction);
legacy_s16 extended_replay_ui_format_time(legacy_s8 *out, legacy_u32 frame, legacy_s16 fraction);
legacy_s16 extended_replay_ui_format_current_time(legacy_s8 *out);
legacy_u32 extended_replay_ui_displayed_time(legacy_u32 current, legacy_u32 fallback);
legacy_s16 extended_replay_ui_redraw_time(void);
legacy_s16 extended_replay_ui_uses_legacy_limit(void);
legacy_s16 extended_replay_ui_timeline(legacy_u32 recorded, legacy_u32 current, legacy_u16 range,
									   legacy_s16 *recorded_position, legacy_s16 *current_position);
legacy_s16 extended_replay_ui_restart(void);
legacy_s16 extended_replay_ui_branch(void);
legacy_s16 extended_replay_ui_seek(legacy_u32 frame);
legacy_s16 extended_replay_ui_seek_end(void);
legacy_s16 extended_replay_ui_seek_draw(legacy_u32 frame);
legacy_s16 extended_replay_ui_draw_current(void);
void extended_replay_ui_begin_live_track(void);
const legacy_s8 *extended_replay_ui_extension(const legacy_s8 *fallback);
legacy_s16 extended_replay_ui_load_error(legacy_s8 saved_flags);
legacy_s16 extended_replay_ui_advance_input(legacy_s16 *handled);
legacy_s16 extended_replay_ui_record_input(legacy_u8 input);
legacy_s16 extended_replay_ui_or_input(legacy_u8 action);
#endif
#endif
