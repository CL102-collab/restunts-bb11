#ifndef RESTUNTS_UI_FILE_DIALOG_INTERNAL_H
#define RESTUNTS_UI_FILE_DIALOG_INTERNAL_H
#include "../legacy.h"
#include "../game_input.h"
struct FILE_DIALOG {
	legacy_s16 positions[40];
	struct BUTTON_AREA hit_areas[10];
	legacy_s8 filenames[128][13];
	legacy_s8 selected;
	legacy_s8 scroll;
	legacy_s8 previous_selected;
	legacy_s8 previous_scroll;
	legacy_u8 file_count;
	legacy_u8 search_again;
#ifdef RESTUNTS_SDL3
	legacy_u8 extended[128];
	legacy_u8 merge_formats;
	const legacy_s8 *legacy_extension;
#endif
};
#endif
