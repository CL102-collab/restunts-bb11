#ifndef RESTUNTS_EXTENDED_UI_DIALOG_H
#define RESTUNTS_EXTENDED_UI_DIALOG_H
#include "../legacy.h"
struct FILE_DIALOG;
#ifdef RESTUNTS_SDL3
legacy_s16 extended_ui_dialog_append_name(struct FILE_DIALOG *dialog, const legacy_s8 *path,
										  legacy_u8 extended);
void extended_ui_dialog_swap(struct FILE_DIALOG *dialog, legacy_u16 first, legacy_u16 second);
void extended_ui_dialog_label(struct FILE_DIALOG *dialog, legacy_s16 candidate, legacy_s8 *text);
void extended_ui_dialog_selected_extension(struct FILE_DIALOG *dialog, legacy_s8 *out);
void extended_ui_dialog_initialize(struct FILE_DIALOG *dialog, legacy_u8 merge,
								   const legacy_s8 *extension);
void extended_ui_dialog_collect(struct FILE_DIALOG *dialog, const legacy_s8 *directory,
								void (*collect)(struct FILE_DIALOG *, const legacy_s8 *,
												legacy_u8));
#endif
#endif
