#ifndef RESTUNTS_EXTENDED_MENU_RUNTIME_H
#define RESTUNTS_EXTENDED_MENU_RUNTIME_H
#include "legacy.h"
#ifdef RESTUNTS_SDL3
void extended_menu_backup_track(void);
legacy_s16 extended_menu_restore_track(void);
void extended_menu_warn_track_version(void);
void extended_menu_validation_error(legacy_s16 result, legacy_s16 column, legacy_s16 row);
void extended_menu_load_startup_track(void);
void extended_menu_begin_track_recording(void);
legacy_u8 extended_menu_horizon(legacy_u8 fallback);
legacy_s16 extended_menu_live_preview(void);
legacy_s16 extended_menu_load_selected_track(const legacy_s8 *extension, const legacy_s8 *path,
											 const legacy_s8 *previous_name);
legacy_s16 extended_menu_open_editor(void);
legacy_s16 extended_menu_needs_legacy_shapes(void);
#endif
#endif
