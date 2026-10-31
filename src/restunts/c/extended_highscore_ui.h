#ifndef RESTUNTS_EXTENDED_HIGHSCORE_UI_H
#define RESTUNTS_EXTENDED_HIGHSCORE_UI_H
#include "legacy.h"
#include "highscore.h"
void highscore_draw_table(void);
void extended_hiscore_show_error(void);
void highscore_make_empty_entry(struct HIGHSCORE_ENTRY *record);
legacy_u16 highscore_format_fields(const legacy_s8 *player_name, const legacy_s8 *car_name,
								   const legacy_s8 *opponent, legacy_u8 car_flag,
								   legacy_u8 *text_offsets);
extern legacy_u8 ranking_highlight;
#ifdef RESTUNTS_SDL3
legacy_u32 extended_highscore_player_finish(legacy_u32 fallback);
legacy_u32 extended_highscore_opponent_finish(legacy_u32 fallback);
legacy_u32 extended_highscore_penalty(legacy_u32 fallback);
legacy_u32 extended_highscore_duration(legacy_u32 fallback);
legacy_s16 extended_highscore_format_runtime_time(legacy_s8 *out, legacy_u32 frame);
legacy_s16 extended_highscore_load_current(legacy_s16 create_default, legacy_s16 *result);
legacy_s16 extended_highscore_print_entry(legacy_s16 entry, legacy_u8 *text_offsets);
void extended_highscore_clear_table(void);
legacy_s16 extended_highscore_time_x(const legacy_s8 *text, legacy_s16 fallback, legacy_s16 width);
legacy_s16 extended_highscore_validate_current(legacy_s8 *status);
legacy_s16 extended_highscore_check_current(legacy_s8 *status, legacy_u32 *finish_time);
legacy_s16 extended_highscore_enter_current(legacy_u32 frames, void far *prompt,
											legacy_u8 car_flag);
legacy_s16 extended_highscore_has_first_entry(legacy_u8 *has_score);
#endif
#endif
