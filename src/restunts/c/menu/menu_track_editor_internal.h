#ifndef RESTUNTS_MENU_TRACK_EDITOR_INTERNAL_H
#define RESTUNTS_MENU_TRACK_EDITOR_INTERNAL_H

#include "../externs.h"
#include "../fileio.h"
#include "../legacy.h"
#include "../memmgr.h"
#include "menu_internal.h"
#include "../platform.h"
#ifdef RESTUNTS_AMIGA
#include "../../platform/amiga/amiga.h"
#endif
#include "../shape2d/shape2d.h"
#include "../shape3d/shape3d.h"
#include "../track/trackdata_layout.h"
#include "../ui/ui_text.h"
#include "../timing.h"
#include "../ui/ui_dialog.h"
#include "../game_input.h"
#include "../ui/ui_input.h"
#include "../track/track_objects.h"
#include "../highscore/highscore.h"
#include "menu_common.h"
#include "../keyboard.h"

#define TRACK_EDITOR_CACHE_INVALID 255U
#define TRACK_EDITOR_POSITION_UNSET 255U
#define TRACK_EDITOR_PAGE_UNSET 255U
#define TRACK_EDITOR_HOVER_UNSET 255U
#define TRACK_EDITOR_MOUSE_NO_HIT 255U
#define TRACK_EDITOR_SAVE_CANCELLED 255U
#define TRACK_EDITOR_DIALOG_CANCELLED 255U
#define TRACK_EDITOR_SCREEN_WIDTH 320U
#define TRACK_EDITOR_SCREEN_HEIGHT 200U
#define TRACK_EDITOR_MAP_LEFT 8
#define TRACK_EDITOR_MAP_TOP 4
#define TRACK_EDITOR_MAP_CLIP_RIGHT 200
#define TRACK_EDITOR_MAP_CLIP_BOTTOM 179
#define TRACK_EDITOR_TILE_SIZE 16U
#define TRACK_EDITOR_PALETTE_LEFT 220U
#define TRACK_EDITOR_PALETTE_TOP 36U
#define TRACK_EDITOR_PALETTE_WIDTH 96U
#define TRACK_EDITOR_PALETTE_ACTION_TOP 28
#define TRACK_EDITOR_PALETTE_ACTION_WIDTH 48U
#define TRACK_EDITOR_MULTITILE_CURSOR_SIZE 32U
#define TRACK_EDITOR_LABEL_Y 192
#define TRACK_EDITOR_HORIZONTAL_SCROLLBAR_X 9
#define TRACK_EDITOR_HORIZONTAL_SCROLLBAR_LENGTH 181
#define TRACK_EDITOR_VERTICAL_SCROLLBAR_X 202
#define TRACK_EDITOR_VERTICAL_SCROLLBAR_LENGTH 176
#define TRACK_EDITOR_PAGE_BAR_X 221
#define TRACK_EDITOR_PAGE_BAR_WIDTH 95
#define TRACK_EDITOR_PAGE_BAR_Y 133
#define TRACK_EDITOR_TITLE_X 217
#define TRACK_EDITOR_TITLE_Y 3
#define TRACK_EDITOR_TITLE_WIDTH 102
#define TRACK_EDITOR_TITLE_HEIGHT 22
#define TRACK_EDITOR_LEFT_FRAME_X 5
#define TRACK_EDITOR_LEFT_FRAME_Y 0
#define TRACK_EDITOR_LEFT_FRAME_WIDTH 206
#define TRACK_EDITOR_LEFT_FRAME_HEIGHT 190
#define TRACK_EDITOR_RIGHT_FRAME_X 217
#define TRACK_EDITOR_RIGHT_FRAME_Y 32
#define TRACK_EDITOR_RIGHT_FRAME_WIDTH 102
#define TRACK_EDITOR_RIGHT_FRAME_HEIGHT 158
#define TRACK_EDITOR_WIDE_BUTTON_X 221
#define TRACK_EDITOR_WIDE_BUTTON_Y 140
#define TRACK_EDITOR_WIDE_BUTTON_WIDTH 94
#define TRACK_EDITOR_BUTTON_LEFT_X 221
#define TRACK_EDITOR_BUTTON_RIGHT_X 269
#define TRACK_EDITOR_BUTTON_UPPER_Y 156
#define TRACK_EDITOR_BUTTON_LOWER_Y 172
#define TRACK_EDITOR_BUTTON_WIDTH 46
#define TRACK_EDITOR_BUTTON_HEIGHT 14
#define TRACK_EDITOR_WATER_RAISED_ROAD_FIRST 34U
#define TRACK_EDITOR_WATER_RAISED_ROAD_LAST 35U
#define TRACK_EDITOR_WATER_BRIDGE_FIRST 103U
#define TRACK_EDITOR_WATER_BRIDGE_LAST 108U
#define TRACK_EDITOR_WATER_SHIP_FIRST 171U
#define TRACK_EDITOR_WATER_SHIP_LAST 174U
#define TRACK_EDITOR_TRACK_FILE_SIZE 1802UL
#define TRACK_EDITOR_TRANSPARENT_COLOR 15U
#define TRACK_EDITOR_TILE_PREVIEW_RIGHT 15
#define TRACK_EDITOR_TILE_PREVIEW_BOTTOM 14
#define TRACK_EDITOR_BLINK_INITIAL_COUNT 99U
#define TRACK_EDITOR_BLINK_INTERVAL 15U
#define TRACK_EDITOR_SKYBOX_MAP_INDEX 900U
#define TRACK_EDITOR_DIALOG_NO_CHANGE 5U

enum TRACK_EDITOR_VALIDATION_RESULT {
	TRACK_EDITOR_VALIDATION_OK = 0,
	TRACK_EDITOR_ERROR_INVALID_WATER_TILE = 12,
	TRACK_EDITOR_ERROR_INVALID_HILL_TILE = 13,
	TRACK_EDITOR_ERROR_INVALID_TERRAIN_TILE = 14
};

extern legacy_u8 far *progress_box_shape;

struct TRACK_EDITOR_MAP_CELL {
	legacy_u16 source_row;
	legacy_u16 source_column;
	legacy_u16 source_index;
	legacy_u16 cache_index;
	legacy_s16 x;
	legacy_s16 y;
	legacy_u8 tile;
	legacy_u8 terrain;
};

extern legacy_s8 track_editor_terrain_shape_names[];
extern legacy_s8 track_editor_cursor_shape_names[];
extern legacy_s8 track_editor_under_cursor_shape_names[];
extern const legacy_s8 track_editor_error_resource_ids[];
extern const struct BUTTON_AREA track_editor_buttons[5];
extern const legacy_u16 track_editor_page_keys[10];
extern const legacy_u8 track_editor_maximum_columns[2];
extern const legacy_u8 track_editor_maximum_rows[2];

/* Resources and interaction state belong to one editor invocation. */
struct TRACK_EDITOR_SESSION {
	legacy_u8 cached_track[132];
	legacy_u8 cached_terrain[132];
	struct SPRITE far *cursor_sprites[4];
	legacy_s8 far *shape_resource;
	legacy_s8 far *text_resource;
	legacy_s8 far *text_name_resource;
	legacy_u8 selection_column[2];
	legacy_u8 selection_row[2];
	legacy_u8 map_column_offset;
	legacy_u8 map_row_offset;
	legacy_u8 previous_column_offset;
	legacy_u8 previous_row_offset;
	legacy_u8 page;
	legacy_u8 previous_page;
	legacy_u8 selected_tile;
	legacy_u8 saved_tile;
	legacy_u8 last_column;
	legacy_u8 last_row;
	legacy_u8 hovered_tile;
	legacy_u8 previous_hovered_tile;
	legacy_u8 multi_tile;
	legacy_u8 tile_width;
	legacy_u8 tile_height;
	legacy_u8 cursor_width;
	legacy_u8 cursor_height;
	legacy_u8 map_dirty;
	legacy_u8 palette_dirty;
	legacy_u8 scrollbars_dirty;
	legacy_u8 validate_track;
	legacy_u8 validation_error;
	legacy_u8 track_changed;
	legacy_u8 menu_active;
	legacy_u8 blit_mode;
	legacy_u8 cursor_drawn;
	legacy_u8 focus;
	legacy_u8 blink_focus;
	legacy_u8 animation_saved_tile;
	legacy_u16 path_animation_index;
	legacy_u16 blink_timer;
	legacy_u16 key;
	legacy_s16 previous_label_width;
	legacy_s16 cursor_x;
	legacy_s16 cursor_y;
};

void preRender_icons(legacy_u8 page);
void draw_2DtrackMap(legacy_u8 column_offset, legacy_u8 row_offset, legacy_u8 *cached_track,
					 legacy_u8 *cached_terrain);
void track_editor_remove_invalid_multitile_links(void);
legacy_s16 track_editor_remove_invalid_terrain_tiles(void);
legacy_u8 track_editor_palette_tile(legacy_u8 page, legacy_u8 row, legacy_u8 column);
legacy_u8 track_editor_map_tile(legacy_u8 column, legacy_u8 row);
void track_editor_toggle_highlight(legacy_s16 x, legacy_s16 y, legacy_u8 width, legacy_u8 height);
void track_editor_show_message(legacy_s8 far *text_resource, const legacy_s8 *resource_id);
void track_editor_draw_window(struct TRACK_EDITOR_SESSION *editor);
void track_editor_update_palette(struct TRACK_EDITOR_SESSION *editor);
void track_editor_redraw(struct TRACK_EDITOR_SESSION *editor);
void track_editor_position_cursor(struct TRACK_EDITOR_SESSION *editor);
void track_editor_draw_tile_label(struct TRACK_EDITOR_SESSION *editor);
void track_editor_report_validation(struct TRACK_EDITOR_SESSION *editor);
void track_editor_wait_for_input(struct TRACK_EDITOR_SESSION *editor);
void track_editor_advance_path_animation(struct TRACK_EDITOR_SESSION *editor);
void track_editor_handle_key(struct TRACK_EDITOR_SESSION *editor);
void load_tracks_menu_shapes(void);

#endif
