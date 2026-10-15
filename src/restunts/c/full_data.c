#include "externs.h"
#include "fatal.h"
#include "game_input.h"
#include "platform.h"
#include "shape2d.h"
#include "shape3d.h"
#include "shape3d_internal.h"
#include "math_internal.h"
#include "frame_internal.h"
#include "ui_text.h"
#include "race_resources_internal.h"
#include "menu_internal.h"
#include "race_stats.h"
#include "track_types.h"
#include "camera.h"
#include "video_frame.h"
#include "car_model.h"
#include "scene_resources.h"
#include "projection.h"
#include "skybox.h"
#include "race_graphics.h"
#include "car_audio.h"
#include "ui_dialog_internal.h"
#include "highscore.h"
#include "menu_common.h"

#define FENCE_TRACK_OBJECT_CODE 214U
#define FENCE_SCENE_OBJECT_CODE 215U
#define TRACK_PREVIEW_CAMERA_X 15360
#define TRACK_PREVIEW_CAMERA_Y 20200
#define TRACK_PREVIEW_CAMERA_Z -2800
#define TRACK_PREVIEW_TARGET_X 15360
#define TRACK_PREVIEW_TARGET_Y 2800
#define TRACK_PREVIEW_TARGET_Z 10960
#define TRACK_PREVIEW_VECTOR_Y -10100
#define TRACK_PREVIEW_VECTOR_Z 16760
#define LEGACY_S32_MIN_VALUE ((legacy_s32)(-2147483647L - 1L))
#define WINDOW_DEFINITION_BUFFER_SIZE 3600U
#define VGA_MEMORY_SEGMENT 40960U
#define FULL_AUDIO_ENGINE_SAMPLE_COUNT 500U
#define FULL_AUDIO_ENGINE_RESERVED_PARAMETER1_LOW 16U
#define FULL_AUDIO_ENGINE_RESERVED_PARAMETER1_HIGH 39U
#define FULL_AUDIO_ENGINE_RESERVED_PARAMETER2_LOW 40U
#define FULL_AUDIO_ENGINE_RESERVED_PARAMETER2_HIGH 35U

/* Renderer work areas formerly reserved as anonymous spans in dseg.asm. */
struct SHAPE3D game3dshapes[130];
struct TRANSFORMEDSHAPE3D currenttransshape[29];
struct TRANSFORMEDSHAPE3D *curtransshape_ptr;

#if defined(RESTUNTS_SDL3)
static polyinfo_link initial_polygon_next_index[POLYINFO_SUPERSIGHT_PRIMITIVE_CAPACITY + 1U];
static polyinfo_offset initial_polygon_record_offsets[POLYINFO_SUPERSIGHT_PRIMITIVE_CAPACITY];
polyinfo_link *polygon_next_index = initial_polygon_next_index;
polyinfo_offset *polygon_record_offsets = initial_polygon_record_offsets;
#else
polyinfo_link polygon_next_index[POLYINFO_SUPERSIGHT_PRIMITIVE_CAPACITY + 1U];
polyinfo_offset polygon_record_offsets[POLYINFO_SUPERSIGHT_PRIMITIVE_CAPACITY];
#endif

struct RECTANGLE frame_layer_rects[15];
struct RECTANGLE intro_redraw_cliprect;
struct RECTANGLE full_screen_rect = {0, 320, 0, 200};
struct RECTANGLE frame_rects_page0[15];
struct RECTANGLE frame_rects_page1[15];
struct RECTANGLE merged_redraw_rects[45];
legacy_s8 frame_rect_change_flags[15];
legacy_s16 redraw_rect_sort_indices[45];
legacy_s8 redraw_rect_count;
struct RECTANGLE *active_frame_rects;
struct RECTANGLE *alternate_frame_rects;

/* Camera, menu, and renderer constants. */
struct CUSTOM_CAMERA custom_camera = {CUSTOM_CAMERA_INITIAL_DISTANCE,
									  CUSTOM_CAMERA_INITIAL_ELEVATION_ANGLE,
									  CUSTOM_CAMERA_INITIAL_AZIMUTH_ANGLE};

struct BUTTON_AREA menu_buttons[5] = {{105, 208, 119, 197},
									  {66, 107, 77, 120},
									  {5, 67, 114, 170},
									  {190, 253, 76, 122},
									  {255, 312, 116, 166}};
struct BUTTON_AREA trackmenu_buttons[3] = {
	{16, 112, 171, 197}, {112, 208, 171, 197}, {208, 304, 171, 197}};
struct BUTTON_AREA carmenu_buttons[5] = {{229, 316, 107, 124},
										 {229, 316, 125, 142},
										 {229, 316, 143, 160},
										 {229, 316, 161, 178},
										 {229, 316, 179, 196}};
struct BUTTON_AREA opponentmenu_buttons[5] = {{20, 76, 177, 197},
											  {76, 132, 177, 197},
											  {132, 188, 177, 197},
											  {188, 244, 177, 197},
											  {244, 300, 177, 197}};
legacy_s16 hiscore_buttons_y1[5] = {174, 174, 174, 174, 174};
legacy_s16 hiscore_buttons_y2[5] = {197, 197, 197, 197, 197};

struct RECTANGLE carmenu_cliprect = {0, 320, 0, 95};
struct RECTANGLE car_menu_redraw_cliprect = {0, 320, 0, 0};
struct VECTOR carmenu_carpos = {0, -840, 2880};
struct RECTANGLE empty_rect = {9999, -1, 9999, -1};
struct RECTANGLE trackpreview_cliprect = {0, 320, 0, 200};
struct RECTANGLE intro_cliprect = {0, 320, 0, 200};
struct RECTANGLE rect_ingame_text2 = {148, 172, 93, 108};
struct RECTANGLE rect_ingame_text3 = {68, 92, 113, 128};
struct RECTANGLE rect_ingame_text4 = {228, 252, 113, 128};

legacy_s8 detail_threshold_by_level[6] = {2, 2, 1, 0, 0, 0};
legacy_s16 cloud_heading_offsets[8] = {30, 200, 320, 400, 530, 700, 880, 960};
legacy_s16 hill_fill_offsets_single[2] = {0, 0};
legacy_s16 hill_fill_offsets_row[4] = {0, 512, 0, -512};
legacy_s16 hill_fill_offsets_column[4] = {512, 0, -512, 0};
legacy_s16 hill_fill_offsets_both[8] = {-512, 512, -512, -512, 512, 512, 512, -512};
legacy_s8 track_material_animation[16] = {0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3};
legacy_s16 fence_rotations[8] = {0,
								 0,
								 ANGLE_QUARTER_TURN,
								 ANGLE_QUARTER_TURN,
								 ANGLE_HALF_TURN,
								 ANGLE_HALF_TURN,
								 ANGLE_THREE_QUARTER_TURN,
								 ANGLE_THREE_QUARTER_TURN};
legacy_u8 fence_TrkObjCodes[8] = {FENCE_TRACK_OBJECT_CODE, FENCE_SCENE_OBJECT_CODE,
								  FENCE_TRACK_OBJECT_CODE, FENCE_SCENE_OBJECT_CODE,
								  FENCE_TRACK_OBJECT_CODE, FENCE_SCENE_OBJECT_CODE,
								  FENCE_TRACK_OBJECT_CODE, FENCE_SCENE_OBJECT_CODE};
legacy_s8 fence_tile_offsets_single[2] = {0, 0};
legacy_s8 fence_tile_offsets_row[4] = {0, 0, 0, 1};
/* The original assembly reads a third (0, 0) pair from the next table.
 * Store it explicitly to preserve the duplicate fence without an out-of-bounds read. */
legacy_s8 fence_tile_offsets_column[6] = {0, 0, 1, 0, 0, 0};
legacy_s8 fence_tile_offsets_both[16] = {0, 0, 1, 0, 0, 1, 1, 1, -128, 0, -128, 1, -1, -1, 0, 0};
legacy_s16 track_preview_camera_x = TRACK_PREVIEW_CAMERA_X;
legacy_s16 track_preview_camera_y = TRACK_PREVIEW_CAMERA_Y;
legacy_s16 track_preview_camera_z = TRACK_PREVIEW_CAMERA_Z;
legacy_s16 track_preview_target_x = TRACK_PREVIEW_TARGET_X;
legacy_s16 track_preview_target_y = TRACK_PREVIEW_TARGET_Y;
legacy_s16 track_preview_target_z = TRACK_PREVIEW_TARGET_Z;
struct VECTOR track_preview_horizon_vector = {0, TRACK_PREVIEW_VECTOR_Y, TRACK_PREVIEW_VECTOR_Z};

/* Static scene objects refer to portable C shape records, not dseg offsets. */
struct TRACKOBJECT terrain_scene_objects[19] = {
	{0, 0, &game3dshapes[108], &game3dshapes[109], 0, 0, 0, 0, -1, 0},
	{0, 0, &game3dshapes[45], &game3dshapes[45], 0, 0, 1, 0, -1, 0},
	{0, 0, &game3dshapes[44], &game3dshapes[44], 0, 0, 1, 0, -1, 0},
	{0, ANGLE_THREE_QUARTER_TURN, &game3dshapes[44], &game3dshapes[44], 0, 0, 1, 0, -1, 0},
	{0, ANGLE_HALF_TURN, &game3dshapes[44], &game3dshapes[44], 0, 0, 1, 0, -1, 0},
	{0, ANGLE_QUARTER_TURN, &game3dshapes[44], &game3dshapes[44], 0, 0, 1, 0, -1, 0},
	{0, 0, &game3dshapes[43], &game3dshapes[43], 0, 0, 1, 0, -1, 0},
	{0, 0, &game3dshapes[42], &game3dshapes[42], 0, 0, 1, 0, -1, 0},
	{0, ANGLE_THREE_QUARTER_TURN, &game3dshapes[42], &game3dshapes[42], 0, 0, 1, 0, -1, 0},
	{0, ANGLE_HALF_TURN, &game3dshapes[42], &game3dshapes[42], 0, 0, 1, 0, -1, 0},
	{0, ANGLE_QUARTER_TURN, &game3dshapes[42], &game3dshapes[42], 0, 0, 1, 0, -1, 0},
	{0, 0, &game3dshapes[41], &game3dshapes[41], 0, 0, 1, 0, -1, 0},
	{0, ANGLE_THREE_QUARTER_TURN, &game3dshapes[41], &game3dshapes[41], 0, 0, 1, 0, -1, 0},
	{0, ANGLE_HALF_TURN, &game3dshapes[41], &game3dshapes[41], 0, 0, 1, 0, -1, 0},
	{0, ANGLE_QUARTER_TURN, &game3dshapes[41], &game3dshapes[41], 0, 0, 1, 0, -1, 0},
	{0, 0, &game3dshapes[40], &game3dshapes[40], 0, 0, 1, 0, -1, 0},
	{0, ANGLE_THREE_QUARTER_TURN, &game3dshapes[40], &game3dshapes[40], 0, 0, 1, 0, -1, 0},
	{0, ANGLE_HALF_TURN, &game3dshapes[40], &game3dshapes[40], 0, 0, 1, 0, -1, 0},
	{0, ANGLE_QUARTER_TURN, &game3dshapes[40], &game3dshapes[40], 0, 0, 1, 0, -1, 0}};

struct TRACKOBJECT particle_scene_objects[13] = {
	{0, 0, &game3dshapes[112], &game3dshapes[112], 0, 0, 1, 0, -1, 0},
	{0, 0, &game3dshapes[113], &game3dshapes[113], 0, 0, 1, 0, -1, 0},
	{0, 0, &game3dshapes[114], &game3dshapes[114], 0, 0, 1, 0, -1, 0},
	{0, 0, &game3dshapes[115], &game3dshapes[115], 0, 0, 1, 0, -1, 0},
	{0, 0, &game3dshapes[116], &game3dshapes[116], 0, 0, 1, 0, -1, 0},
	{0, 0, &game3dshapes[117], &game3dshapes[117], 0, 0, 1, 0, -1, 0},
	{0, 0, &game3dshapes[118], &game3dshapes[118], 0, 0, 1, 0, -1, 0},
	{0, 0, &game3dshapes[119], &game3dshapes[119], 0, 0, 1, 0, -1, 0},
	{0, 0, &game3dshapes[120], &game3dshapes[120], 0, 0, 1, 0, -1, 0},
	{0, 0, &game3dshapes[121], &game3dshapes[121], 0, 0, 1, 0, -1, 0},
	{0, 0, &game3dshapes[122], &game3dshapes[122], 0, 0, 1, 0, -1, 0},
	{0, 0, &game3dshapes[123], &game3dshapes[123], 0, 0, 1, 0, -1, 0},
	{0, 0, &game3dshapes[110], &game3dshapes[110], 0, 0, 1, 0, -1, 0}};

struct SHAPE3D *cloud_shapes[8] = {&game3dshapes[48], &game3dshapes[47], &game3dshapes[46],
								   &game3dshapes[48], &game3dshapes[47], &game3dshapes[46],
								   &game3dshapes[48], &game3dshapes[47]};

/* Shape transformation and rasterizer state. */
legacy_u16 transshapenumverts;
legacy_u8 far *transshapeprimitives;
legacy_u16 transshapenumpaints;
legacy_u8 transshapeflags;
legacy_u8 transshapematerial;
struct RECTANGLE *transshaperectptr;
struct MATRIX mat_temp;
struct MATRIX mat_y0;
struct MATRIX mat_y100;
struct MATRIX mat_y200;
struct MATRIX mat_y300;
legacy_s32 direction_sector_sine;
legacy_s32 direction_sector_cosine;
legacy_s32 direction_sector_sine_copy;
legacy_s32 direction_sector_cosine_copy;
legacy_u16 projection_horizontal_half_fov;
legacy_u16 projection_vertical_half_fov;
legacy_u16 projection_half_width = 160;
legacy_u16 projection_origin_x;
legacy_u16 projection_center_x = 160;
legacy_u16 projection_half_height = 100;
legacy_u16 projection_origin_y;
legacy_u16 projection_center_y = 100;
legacy_u16 projection_focal_length_x;
legacy_u16 projection_focal_length_y;

legacy_u16 shape_half_scale;
struct RECTANGLE select_rect_rc;
polyinfo_offset polyinfoptrnext;
legacy_u8 far *polyinfoptr;
legacy_u8 far *transshapepolyinfo;
legacy_u8 far *transshapeprimptr;
legacy_u8 far *transshapeprimindexptr;
legacy_s8 transprimitivepaintjob;
polyinfo_index polyinfonumpolys;
polyinfo_index shape_polygon_predecessor;
polyinfo_index polygon_list_tail;
polyinfo_index shape_polygon_count;
polyinfo_index polygon_insertion_cursor;
legacy_u16 polygon_buffer_full;
struct POINT2D *polyvertpointptrtab[11];

legacy_u8 primidxcounttab[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 2, 6, 3, 0, 0};
legacy_u8 primtypetab[16] = {
	RENDER_PRIMITIVE_POLYGON, RENDER_PRIMITIVE_POINT,		RENDER_PRIMITIVE_LINE,
	RENDER_PRIMITIVE_POLYGON, RENDER_PRIMITIVE_POLYGON,		RENDER_PRIMITIVE_POLYGON,
	RENDER_PRIMITIVE_POLYGON, RENDER_PRIMITIVE_POLYGON,		RENDER_PRIMITIVE_POLYGON,
	RENDER_PRIMITIVE_POLYGON, RENDER_PRIMITIVE_POLYGON,		RENDER_PRIMITIVE_SPHERE,
	RENDER_PRIMITIVE_WHEEL,	  RENDER_PRIMITIVE_UNSUPPORTED, RENDER_PRIMITIVE_POLYGON,
	RENDER_PRIMITIVE_POLYGON};
legacy_s32 invpow2tbl[32] = {LEGACY_S32_MIN_VALUE,
							 1073741824L,
							 536870912L,
							 268435456L,
							 134217728L,
							 67108864L,
							 33554432L,
							 16777216L,
							 8388608L,
							 4194304L,
							 2097152L,
							 1048576L,
							 524288L,
							 262144L,
							 131072L,
							 65536L,
							 32768L,
							 16384L,
							 8192L,
							 4096L,
							 2048L,
							 1024L,
							 512L,
							 256L,
							 128L,
							 64L,
							 32L,
							 16L,
							 8L,
							 4L,
							 2L,
							 1L};

legacy_s16 transformedshape_zarray[29];
legacy_s16 transformedshape_indices[29];
legacy_s8 transformed_shape_sort_types[29];
legacy_s8 transformedshape_counter;
legacy_s8 transshapenumvertscopy;
legacy_s8 backlights_paint_override;
legacy_s16 player_wheel_vertex_state[5];
legacy_s16 opponent_wheel_vertex_state[5];
/* One entry per view. The disassembler named the two words separately, but
 * the renderer indexes them as a pair, so they have to stay one array. */
legacy_s16 frame_buffer_camera_headings[2];
legacy_s16 intro_elapsed_ticks;
legacy_s16 last_rendered_camera_heading;

struct VECTOR player_front_wheel_centers[2];
struct VECTOR player_base_wheel_vertices[24];
struct VECTOR opponent_front_wheel_centers[2];
struct VECTOR opponent_base_wheel_vertices[24];
legacy_s16 neutral_wheel_suspension[4];

struct SHAPE3D logoshape;
struct SHAPE3D logo2shape;
struct SHAPE3D bravshape;

/* Video and sprite state formerly split between dseg and seg012. */
struct SPRITE far drawing_sprite;
struct SPRITE far screen_sprite;
legacy_u16 far full_screen_line_offsets[200];
legacy_u8 far wnd_defs[WINDOW_DEFINITION_BUFFER_SIZE];
legacy_s8 *far next_wnd_def = (legacy_s8 *)&wnd_defs[0];
struct SPRITE far *sprite_ptrs[4];
struct SPRITE far *mcga_backbuffer_sprite;
legacy_u8 sprite_background_stack_depth;
legacy_u16 fontdefseg;
legacy_u16 raster_fill_pattern;
legacy_u16 raster_alternate_color;
legacy_s16 sprite_background_saved_x[4];
legacy_s16 sprite_background_saved_y[4];

struct SKYBOX skybox;
struct RECTANGLE rect_ingame_text;
legacy_s16 intro_colorvalue = 1;
legacy_s16 intro_palette_color_count = 16;
legacy_s16 dialog_fnt_colour = 15;
legacy_s16 font_glyph_height;
legacy_s16 sdgame2_widths[5];
void far *sdgame2shapes[5];
struct SHAPE2D far *skyboxes[4];
void far *fontledresptr;

/* Replay summary and input state. */
legacy_s32 gState_travDist;
legacy_u16 gState_frame;
legacy_s16 gState_total_finish_time;
legacy_s16 gState_opponent_finish_time;
legacy_s16 gState_pEndFrame;
legacy_s16 gState_oEndFrame;
legacy_s16 gState_penalty;
legacy_s16 gState_impactSpeed;
legacy_s16 gState_topSpeed;
legacy_s16 gState_jumpCount;
legacy_s8 replay_recording_flags;
legacy_s16 mouse_butstate;
legacy_s16 mouse_xpos;
legacy_s16 mouse_ypos;

/* Resource ownership. */
void far *mainresptr;
void far *fontnptr;
void far *fontdefptr;
void far *songfileptr;
void far *voicefileptr;
void far *engptr;
void far *eng1ptr;
void far *sdgameresptr;
legacy_s8 far *sdgame2ptr;
legacy_s8 far *skybox_res_ofs;
legacy_s8 far *carresptr;
legacy_s8 far *car2resptr;
legacy_s8 far *game1ptr;
legacy_s8 far *game2ptr;
legacy_s8 far *curshapeptr;
legacy_s8 far *opp_res;
legacy_s8 far *oppresources[7];
legacy_s8 is_audioloaded;

void(far *exitlistfuncs[EXIT_HANDLER_SLOT_COUNT])(void);
legacy_s16 waitflag;

/* Menu and overlay resources. */
struct SHAPE2D far *whlshapes[9];
struct SHAPE2D far *gnobshapes[6];
struct SHAPE2D far *digshapes[10];
struct SHAPE2D far *rplyshapes[23];
struct SHAPE2D far *track_editor_terrain_shapes[19];
struct SHAPE2D far *track_editor_cursor_shapes[4];
struct SHAPE2D far *track_editor_under_cursor_shapes[4];
struct SHAPE2D far *track_editor_tile_shapes[186];
struct SHAPE2D far *track_editor_tile_masks[186];
struct SPRITE far *dashboard_instrument_sprite;
struct SPRITE far *dashboard_gearbox_sprite;
legacy_s8 far *stdaresptr;
legacy_s8 far *stdbresptr;
void far *miscptr;

legacy_s8 mouse_driving_enabled;
legacy_s8 skybox_resources_loaded;
legacy_u8 detail_level;
legacy_s8 shape_view_direction_sector;
legacy_s8 replay_controls_drawn[2];
legacy_s8 dashboard_visible;
legacy_s8 full_redraw_frames_remaining;
legacy_s8 highscore_player_name_input[17];
legacy_s8 loaded_skybox_index;
legacy_s8 dashb_toggle_copy;
legacy_s8 replaybar_toggle_copy;
legacy_s8 followOpponentFlag_copy;
legacy_s8 replaybar_enabled;
legacy_s8 passed_security;
legacy_s8 resID_buffer[RESID_BUFFER_SIZE];
legacy_s16 dashbmp_y;
legacy_s16 dashbmp_y_copy;
legacy_s16 dastbmp_y;
legacy_s16 dastbmp_y2;
legacy_s16 dastseg;
void far *dasmshapeptr;
legacy_s16 roofbmpheight;
legacy_s16 roofbmpheight_copy;
legacy_s16 height_above_replaybar;
legacy_s16 meter_needle_color;
legacy_u16 configured_frame_rate;
legacy_u16 slow_video_mgmt;
legacy_u16 slow_video_mgmt_copy;
legacy_u16 reserved_material_video_word;
legacy_s16 reserved_race_word;
struct RECTANGLE rect_windshield;

legacy_u8 replay_selected_control = 6;
legacy_u8 replay_control_left_neighbor[10] = {1, 7, 3, 4, 5, 6, 7, 8, 8, 0};
legacy_u8 replay_control_right_neighbor[10] = {0, 0, 2, 2, 3, 4, 5, 1, 7, 0};
legacy_u8 replay_control_up_neighbor[10] = {2, 6, 2, 3, 4, 5, 6, 7, 8, 0};
legacy_u8 replay_control_down_neighbor[10] = {0, 1, 0, 0, 1, 1, 1, 7, 8, 0};
legacy_u8 game_camera_buttons_count[4] = {6, 6, 8, 7};
struct BUTTON_AREA game_camera_buttons[9] = {
	{272, 314, 176, 193}, {109, 151, 176, 193}, {274, 314, 156, 173},
	{232, 274, 156, 173}, {190, 232, 156, 173}, {151, 190, 156, 173},
	{108, 151, 156, 173}, {66, 91, 156, 193},	{10, 47, 156, 193}};
legacy_s16 replay_pan_button_left = 10;
legacy_s16 replay_pan_button_right = 47;
legacy_s16 replay_zoom_button_top = 156;
legacy_s16 replay_pan_button_top = 156;
legacy_s16 replay_zoom_button_bottom = 193;
legacy_s16 replay_pan_button_bottom = 193;
struct BUTTON_AREA replay_hidden_bar_camera_button = {0, 104, 151, 200};

legacy_s16 end_text_alternate_variant[3] = {2, 0, 1};
legacy_s16 end_outcome_alternate_variant[4] = {1, 0, 3, 2};
/* The fifth slot is the centered Continue button on the result screen. */
legacy_s16 result_button_left[5] = {4, 84, 164, 244, 128};
legacy_s16 result_button_right[5] = {75, 155, 235, 315, 199};

legacy_s16 menu_highlight_second_color = 5;
legacy_s16 menu_highlight_first_color = 14;
legacy_s16 end_animation_border_shadow_color = 8;
legacy_s16 credits_text_color = 15;
legacy_s16 credits_text_shadow_color = 8;
legacy_s16 credits_title_color = 11;
legacy_s16 credits_title_shadow_color = 3;
legacy_s16 credits_design_heading_color = 12;
legacy_s16 credits_design_heading_shadow_color = 4;
legacy_s16 credits_production_heading_color = 9;
legacy_s16 credits_production_heading_shadow_color = 1;
legacy_s16 credits_art_heading_color = 10;
legacy_s16 credits_art_heading_shadow_color = 2;
legacy_s16 credits_music_heading_color = 13;
legacy_s16 credits_music_heading_shadow_color = 5;
legacy_s16 track_editor_highlight_color = 12;
legacy_s16 button_top_color = 15;
legacy_s16 button_bottom_color = 8;
legacy_s16 button_fill_color = 7;
legacy_s16 graphics_menu_background_color = 9;
legacy_s16 replay_timeline_background_color = 1;
legacy_s16 replay_marker_color = 4;
legacy_s16 performGraphColor = 1;
legacy_u16 dialog_border_color = 4;

legacy_s16 previous_end_opening_variant;
legacy_s16 previous_end_outcome_variant;
legacy_s16 previous_end_closing_variant;
legacy_s16 end_opening_variant;
legacy_s16 end_outcome_variant;
legacy_s16 end_closing_variant;
legacy_s16 dashboard_rpm_index_cache[2];
legacy_s16 dashboard_gear_knob_x_cache[2];
legacy_s16 dashboard_gear_knob_y_cache[2];
legacy_s16 dashboard_speed_index_cache[2];
legacy_u8 dashboard_wheel_shape_cache[2];
legacy_s16 dashboard_steering_dot_x_cache[2];
legacy_s16 dashboard_steering_dot_y_cache[2];
legacy_u8 dashboard_gear_knob_visible_cache[2];
legacy_s16 dashboard_steering_position_cache[2];
legacy_s16 replay_current_position_cache[2];
legacy_u8 replay_selection_cache[2];
legacy_s16 replay_displayed_time_cache[2];
legacy_u8 replay_control_active[9];
legacy_u8 replay_legacy_fast_play_active;
legacy_u8 replay_legacy_play_active;
legacy_u8 replay_camera_mode_cache[2];
legacy_s16 replay_recorded_position_cache[2];
legacy_u8 replay_control_active_cache[18];
legacy_s16 dialog_background_color;
legacy_u16 slow_timer_deadline_low;
legacy_u16 slow_timer_deadline_high;
struct RECTANGLE intro_text_bounds;
struct RECTANGLE highscore_text_bounds;

legacy_u8 palmap[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

/* Current material maps; the copies are switched independently at runtime. */
legacy_s16 material_color_list[SHAPE3D_MATERIAL_COUNT] = {
#ifdef RESTUNTS_AMIGA
	/* Original Amiga stunts DATA+0x3a32: 129 world-palette indexes.
	 * Provenance: tests/fixtures/amiga-palette-reference.json. */
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
	2, 11, 15, 8, 0, 14, 8, 0, 14, 6, 6, 6, 15, 15, 15, 7,
	15, 3, 11, 4, 12, 1, 0, 8, 7, 3, 11, 8, 0, 4, 4, 12,
	1, 1, 9, 9, 4, 4, 12, 12, 0, 8, 7, 7, 7, 14, 14, 15,
	3, 3, 11, 11, 2, 2, 10, 10, 5, 5, 13, 5, 7, 7, 15, 15,
	14, 14, 14, 14, 4, 4, 4, 4, 5, 5, 5, 5, 8, 8, 8, 10,
	15, 7, 6, 4, 9, 10, 2, 2, 10, 10, 15, 15, 6, 6, 4, 6,
	4, 8, 13, 5, 13, 0, 0, 8, 0, 15, 7, 14, 7, 4, 12, 12,
	15,
#else
	0,	 1,	  2,   3,	4,	 5,	  6,   7,	8,	 9,	  10,  11,	12,	 13,  14,  15,	108, 116, 15,
	28,	 29,  14,  28,	31,	 14,  200, 198, 196, 112, 114, 116, 194, 197, 200, 146, 37,	 35,  181,
	29,	 31,  19,  3,	11,	 27,  0,   4,	4,	 12,  156, 154, 152, 150, 42,  40,	38,	 37,  27,
	26,	 25,  24,  72,	70,	 68,  66,  123, 121, 120, 117, 92,	90,	 88,  87,  173, 171, 169, 167,
	20,	 19,  18,  17,	77,	 76,  74,  73,	45,	 44,  42,  41,	159, 175, 174, 172, 29,	 28,  18,
	90,	 15,  7,   200, 219, 136, 99,  101, 103, 104, 106, 17,	20,	 60,  77,  46,	61,	 45,  202,
	190, 186, 183, 180, 0,	 28,  30,  16,	20,	 68,  54,  39,	43,	 12,  17
#endif
};
legacy_s16 material_pattern_list[SHAPE3D_MATERIAL_COUNT] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0};
legacy_s16 material_pattern2_list[SHAPE3D_MATERIAL_COUNT] = {
	-1,	   -1,	   -1,	  -1,	  -1,	 -1,	 -1,	-1,	   -1,	  -1,	 -1, -1, -1, -1, -1,
	-1,	   -1,	   -1,	  0,	  -1,	 -1,	 -1,	30685, -8841, 30685, -1, -1, -1, -1, -1,
	-1,	   -1,	   -1,	  -1,	  30685, -1,	 -1,	-1,	   -1,	  -1,	 -1, -1, -1, -1, -1,
	-1,	   -1,	   -1,	  -1,	  -1,	 -1,	 -1,	-1,	   -1,	  -1,	 -1, -1, -1, -1, -1,
	-1,	   -1,	   -1,	  -1,	  0,	 0,		 0,		0,	   0,	  0,	 0,	 0,	 0,	 0,	 0,
	0,	   0,	   0,	  0,	  0,	 0,		 0,		0,	   0,	  0,	 0,	 0,	 0,	 0,	 0,
	0,	   0,	   0,	  0,	  30685, -1,	 -1,	-1,	   -1,	  -1,	 -1, -1, -1, -1, -1,
	-1,	   0,	   0,	  0,	  0,	 0,		 0,		0,	   0,	  0,	 0,	 0,	 0,	 0,	 -13261,
	13260, -18835, 18834, -18835, 18834, -18835, 18834, 0,	   0};
legacy_s16 *material_clrlist_ptr = material_color_list;
legacy_s16 *material_clrlist2_ptr = material_color_list;
legacy_s16 *material_patlist_ptr = material_pattern_list;
legacy_s16 *material_patlist2_ptr = material_pattern2_list;
legacy_s16 *material_clrlist_ptr_cpy;
legacy_s16 *material_clrlist2_ptr_cpy;
legacy_s16 *material_patlist_ptr_cpy;
legacy_s16 *material_patlist2_ptr_cpy;

legacy_s8 *findfilenames[5] = {"id4", "setup.exe", "sdtitl.*", "tedit.*", "opp1.*"};
legacy_s8 full_empty_extension[] = "";
legacy_s8 *shapeexts[6] = {".PVS", ".XVS", ".VSH", ".PES", ".ESH", full_empty_extension};

legacy_u8 far sprite_palette_map[256] = {
	0,	 1,	  2,   3,	4,	 5,	  6,   7,	8,	 9,	  10,  11,	12,	 13,  14,  15,	16,	 17,  18,
	19,	 20,  21,  22,	23,	 24,  25,  26,	27,	 28,  29,  30,	31,	 32,  33,  34,	35,	 36,  37,
	38,	 39,  40,  41,	42,	 43,  44,  45,	46,	 47,  48,  49,	50,	 51,  52,  53,	54,	 55,  56,
	57,	 58,  59,  60,	61,	 62,  63,  64,	65,	 66,  67,  68,	69,	 70,  71,  72,	73,	 74,  75,
	76,	 77,  78,  79,	80,	 81,  82,  83,	84,	 85,  86,  87,	88,	 89,  90,  91,	92,	 93,  94,
	95,	 96,  97,  98,	99,	 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113,
	114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132,
	133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151,
	152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170,
	171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189,
	190, 191, 192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208,
	209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227,
	228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246,
	247, 248, 249, 250, 251, 252, 253, 254, 255};

void (*spritefunc)(legacy_s16 *, legacy_s16 *, legacy_u16, legacy_u16, legacy_u16);
void (*imagefunc)(legacy_u16, legacy_u16, legacy_u16, legacy_u16, legacy_u16);

struct FULL_AUDIO_ENGINE_DEFINITION player_engine_definition = {
	FULL_AUDIO_ENGINE_SAMPLE_COUNT,
	{FULL_AUDIO_ENGINE_RESERVED_PARAMETER1_LOW, FULL_AUDIO_ENGINE_RESERVED_PARAMETER1_HIGH,
	 FULL_AUDIO_ENGINE_RESERVED_PARAMETER2_LOW, FULL_AUDIO_ENGINE_RESERVED_PARAMETER2_HIGH},
	0,
	0,
	{"ENGI", "ENGI", "STAR", "STOP", "BLOW", "CRAS", "SKID", "SKI2", "BUMP", "SCRA"}};
struct FULL_AUDIO_ENGINE_DEFINITION opponent_engine_definition = {
	FULL_AUDIO_ENGINE_SAMPLE_COUNT,
	{FULL_AUDIO_ENGINE_RESERVED_PARAMETER1_LOW, FULL_AUDIO_ENGINE_RESERVED_PARAMETER1_HIGH,
	 FULL_AUDIO_ENGINE_RESERVED_PARAMETER2_LOW, FULL_AUDIO_ENGINE_RESERVED_PARAMETER2_HIGH},
	0,
	0,
	{"ENGI", "ENGI", "STAR", "STOP", "BLOW", "CRAS", "SKID", "SKI2", "BUMP", "SCRA"}};

static void full_initialize_screen_sprite(struct SPRITE far *sprite)
{
	sprite->sprite_bitmapptr = (struct SHAPE2D far *)dos_memory_make_pointer(VGA_MEMORY_SEGMENT, 0);
	sprite->sprite_reserved_word1 = 0;
	sprite->sprite_reserved_word2 = 0;
	sprite->sprite_reserved_word3 = 0;
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
	sprite->sprite_lineofs = (legacy_u8 *)full_screen_line_offsets;
#else
	sprite->sprite_lineofs = (legacy_u8 *)dos_memory_make_near_pointer(
		dos_memory_pointer_offset(full_screen_line_offsets));
#endif
	sprite->sprite_left = 0;
	sprite->sprite_right = 320;
	sprite->sprite_top = 0;
	sprite->sprite_bottom = 200;
	sprite->sprite_pitch = 320;
	sprite->sprite_reserved_word4 = 0;
	sprite->sprite_buffer_width = 320;
	sprite->sprite_raster_left = 0;
	sprite->sprite_raster_right = 320;
}

void full_data_initialize(void)
{
	for (legacy_u16 index = 0; index < 200U; index++) {
		LEGACY_WRITE_U16_LE((legacy_u8 far *)&full_screen_line_offsets[index], index * 320U);
	}
	for (legacy_u16 index = 0; index < WINDOW_DEFINITION_BUFFER_SIZE; index++) {
		wnd_defs[index] = 0;
	}
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
	next_wnd_def = (legacy_s8 *)wnd_defs;
#else
	next_wnd_def = (legacy_s8 *)dos_memory_make_near_pointer(dos_memory_pointer_offset(wnd_defs));
#endif
	full_initialize_screen_sprite(&drawing_sprite);
	full_initialize_screen_sprite(&screen_sprite);
}
