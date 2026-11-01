#ifndef RESTUNTS_HIRES_TRACK_MENU_PREVIEW_H
#define RESTUNTS_HIRES_TRACK_MENU_PREVIEW_H

#ifdef RESTUNTS_SDL3
#include "../externs.h"

#define HIRES_TRACK_MENU_SCORE_FIELDS 4U

struct HIRES_TRACK_MENU_PREVIEW {
	legacy_u8 live;
	legacy_u32 tick;
	legacy_u8 score_visible;
	legacy_u8 score_offsets[HIRES_TRACK_MENU_SCORE_FIELDS];
	legacy_s8 score_text[RESID_BUFFER_SIZE];
};

typedef void (*HIRES_TRACK_MENU_DRAW_HEADER)(const struct HIRES_TRACK_MENU_PREVIEW *preview);

void hires_track_menu_preview_retain(struct HIRES_TRACK_MENU_PREVIEW *preview);
void hires_track_menu_preview_release(struct HIRES_TRACK_MENU_PREVIEW *preview);
void hires_track_menu_preview_cache_score(struct HIRES_TRACK_MENU_PREVIEW *preview,
										  const legacy_s8 *text, const legacy_u8 *offsets);
void hires_track_menu_preview_present(struct HIRES_TRACK_MENU_PREVIEW *preview,
									  HIRES_TRACK_MENU_DRAW_HEADER draw_header);
#endif

#endif
