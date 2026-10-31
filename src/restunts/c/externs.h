#ifndef RESTUNTS_EXTERNS_H
#define RESTUNTS_EXTERNS_H

#ifdef RESTUNTS_SDL3
/* Declare the host CRT before the legacy spelling aliases below. */
#include <string.h>
#include <stdlib.h>
#endif

/* The original timer shutdown restores INT 8 and the BIOS PIT divisor. */
#ifdef RESTUNTS_ORIGINAL
#define legacy_timer_shutdown audio_stop_unk
#define sprite_select_render_window sprite_copy_wnd_to_1
#endif

/* Track arena symbols exported by the unchanged original executable. */
#ifdef RESTUNTS_ORIGINAL
#define player_aero_resistance_table td04_aerotable_pl
#define opponent_aero_resistance_table td05_aerotable_op
#define track_highscore_table td11_highscores
#define replay_header_buffer td13_rpl_header
#define track_element_map td14_elem_map_main
#define track_terrain_map td15_terr_map_main
#define replay_input_buffer td16_rpl_buffer
#define track_route_element_ids td17_trk_elem_ordered
#define track_and_directory_backup td20_trk_file_appnd
#define track_route_columns td21_col_from_path
#define track_route_rows td22_row_from_path
#define track_primary_route_links td01_track_file_cpy
#define track_alternate_route_links td02_penalty_related
#define opponent_route_track_indices trackdata3
#define reserved_trackside_camera_words trackdata6
#define trackside_camera_ground_heights trackdata7
#define roadside_sign_headings td08_direction_related
#define trackside_camera_positions trackdata9
#define roadside_sign_positions td10_track_check_rel
#define sprite_background_state_stack trackdata12
#define track_route_traversal_flags trackdata18
#define roadside_sign_indices_by_tile trackdata19
#define roadside_sign_shape_indices trackdata23
#define track_column_positions trackpos2
#define track_column_centers trackcenterpos2
#define track_row_positions trackpos
#define track_row_centers trackcenterpos
#endif

/* Shared dump tools retain original assembly linkage in oracle builds. */
#ifdef RESTUNTS_ORIGINAL
#define viewport_bottom_cache word_449EA
#define recording_limit_warning_requested byte_46467
#define track_directory byte_3B80C
#define replay_directory byte_3B85E
#endif

#ifdef RESTUNTS_ORIGINAL
/* Keep the original-linked dump harness bound to the original exports. */
#define frame_present sub_19F14
#define reset_race_loop_state init_unknown
#define race_exit_request byte_449DA
#define replay_playback_speed byte_449E6
#define frame_buffer_index byte_44346
#define dashboard_buffer_index byte_4432A
#define start_flag_animation word_44DCA
#define full_redraw_frames_remaining byte_454A4
#endif

#include "math.h"
#include "track/track_types.h"

#include "replay/replay.h"
#include "gamestate.h"

#pragma pack(push, 1)

#define SIMD_GEAR_RATIO_COUNT 7U
#define SIMD_KNOB_POINT_COUNT 7U
#define SIMD_TORQUE_CURVE_SIZE 104U
#define SIMD_RESERVED_HANDLING_COUNT 7U
/* Red #5, at SIMD resource offset 0xAE: speed colour low, optional RPM colour high. */
#define SIMD_NEEDLE_COLORS_INDEX 4U
#define SIMD_SURFACE_GRIP_COUNT 4U
#define SIMD_RESERVED_GRIP_SIZE 10U
#define SIMD_COLLISION_POINT_COUNT 2U
#define SIMD_WHEEL_COORDINATE_COUNT 4U
#define SIMD_STEERING_DOT_COUNT 62U
#define SIMD_SPEEDOMETER_POINT_COUNT 208U
#define SIMD_REV_COUNTER_POINT_COUNT 256U
#define SIMD_DOS_STRUCTURE_SIZE 776U
#define SIMD_RESOURCE_SIZE 772U
#define SIMD_RESOURCE_WORD_SIZE 2U
#define SIMD_RESOURCE_POINT_SIZE 4U
#define SIMD_RESOURCE_VECTOR_SIZE 6U
#define SIMD_RESOURCE_POINT_Y_OFFSET SIMD_RESOURCE_WORD_SIZE
#define SIMD_RESOURCE_VECTOR_Y_OFFSET SIMD_RESOURCE_WORD_SIZE
#define SIMD_RESOURCE_VECTOR_Z_OFFSET (2U * SIMD_RESOURCE_WORD_SIZE)
#define SIMD_RESOURCE_NUM_GEARS_OFFSET 0U
#define SIMD_RESOURCE_RESERVED_GEAR_OFFSET 1U
#define SIMD_RESOURCE_CAR_MASS_OFFSET 2U
#define SIMD_RESOURCE_BRAKING_EFFICIENCY_OFFSET 4U
#define SIMD_RESOURCE_IDLE_RPM_OFFSET 6U
#define SIMD_RESOURCE_DOWNSHIFT_RPM_OFFSET 8U
#define SIMD_RESOURCE_UPSHIFT_RPM_OFFSET 10U
#define SIMD_RESOURCE_MAX_RPM_OFFSET 12U
#define SIMD_RESOURCE_GEAR_RATIOS_OFFSET 14U
#define SIMD_RESOURCE_KNOB_POINTS_OFFSET 28U
#define SIMD_RESOURCE_AERO_RESISTANCE_OFFSET 56U
#define SIMD_RESOURCE_IDLE_TORQUE_OFFSET 58U
#define SIMD_RESOURCE_TORQUE_CURVE_OFFSET 59U
#define SIMD_RESOURCE_RESERVED_TORQUE_OFFSET 163U
#define SIMD_RESOURCE_GRIP_OFFSET 164U
#define SIMD_RESOURCE_RESERVED_HANDLING_OFFSET 166U
#define SIMD_RESOURCE_SLIDING_OFFSET 180U
#define SIMD_RESOURCE_SURFACE_GRIP_OFFSET 182U
#define SIMD_RESOURCE_RESERVED_GRIP_OFFSET 190U
#define SIMD_RESOURCE_COLLISION_POINTS_OFFSET 200U
#define SIMD_RESOURCE_CAR_HEIGHT_OFFSET 208U
#define SIMD_RESOURCE_WHEEL_COORDINATES_OFFSET 210U
#define SIMD_RESOURCE_STEERING_DOTS_OFFSET 234U
#define SIMD_RESOURCE_SPEEDOMETER_CENTER_OFFSET 296U
#define SIMD_RESOURCE_SPEEDOMETER_POINT_COUNT_OFFSET 300U
#define SIMD_RESOURCE_SPEEDOMETER_POINTS_OFFSET 302U
#define SIMD_RESOURCE_REV_COUNTER_CENTER_OFFSET 510U
#define SIMD_RESOURCE_REV_COUNTER_POINT_COUNT_OFFSET 514U
#define SIMD_RESOURCE_REV_COUNTER_POINTS_OFFSET 516U

struct SIMD {
	legacy_s8 num_gears;
	legacy_s8 reserved_gear_byte;
	legacy_s16 car_mass;
	legacy_s16 braking_eff;
	legacy_s16 idle_rpm;
	legacy_s16 downshift_rpm;
	legacy_s16 upshift_rpm;
	legacy_s16 max_rpm;
	legacy_u16 gear_ratios[SIMD_GEAR_RATIO_COUNT];
	struct POINT2D knob_points[SIMD_KNOB_POINT_COUNT];
	legacy_s16 aero_resistance;
	legacy_s8 idle_torque;
	legacy_s8 torque_curve[SIMD_TORQUE_CURVE_SIZE];
	legacy_s8 reserved_torque_byte;
	legacy_s16 grip;
	legacy_s16 reserved_handling_words[SIMD_RESERVED_HANDLING_COUNT];
	legacy_s16 sliding;
	legacy_s16 surface_grip[SIMD_SURFACE_GRIP_COUNT];
	legacy_s8 reserved_grip_bytes[SIMD_RESERVED_GRIP_SIZE];
	struct POINT2D collide_points[SIMD_COLLISION_POINT_COUNT];
	legacy_s16 car_height;
	struct VECTOR wheel_coords[SIMD_WHEEL_COORDINATE_COUNT];
	legacy_s8 steeringdots[SIMD_STEERING_DOT_COUNT];
	struct POINT2D spdcenter;
	legacy_s16 spdnumpoints;
	legacy_s8 spdpoints[SIMD_SPEEDOMETER_POINT_COUNT];
	struct POINT2D revcenter;
	legacy_s16 revnumpoints;
	legacy_s8 revpoints[SIMD_REV_COUNTER_POINT_COUNT];
	legacy_s16 far *aerorestable;
};

#pragma pack(pop)

/* These records contain 16-bit near/far pointers.  Their DOS ABI layout is
 * meaningful only when the DOS memory model supplies those pointer sizes. */
#if defined(RESTUNTS_DOS16)
typedef char
	legacy_simd_must_be_776_bytes[(sizeof(struct SIMD) == SIMD_DOS_STRUCTURE_SIZE) ? 1 : -1];

#endif

legacy_u16 simd_decode(struct SIMD *destination, const legacy_u8 far *source);

#define OPPONENT_SPEED_COUNT 16U

extern struct GAMEINFO gameconfig;
extern struct GAMEINFO gameconfigcopy;

extern struct GAMESTATE state;
extern struct SIMD simd_player;
extern struct SIMD simd_opponent;

void copy_string(legacy_s8 *destination, legacy_s8 far *source);
void setup_legacy_penalty_route_word(void);
void setup_aero_trackdata(void far *carresptr, legacy_s16 is_opponent);
void load_opponent_data(void);

extern legacy_s16 video_shape_width_scale;
extern legacy_s16 video_x_alignment;
extern legacy_s16 video_x_alignment_mask;
extern legacy_s16 video_buffer_height_divisor;
extern legacy_s16 video_uses_page_flipping;
extern legacy_s16 video_page_count;

extern legacy_u8 frame_callback_countdown;
extern legacy_u8 slow_replay_countdown;
extern legacy_u16 elapsed_time1;
extern legacy_u16 elapsed_time2;
extern legacy_u8 race_exit_request;
extern legacy_u8 race_start_sequence_state;
extern legacy_u8 game_replay_mode;
extern legacy_s16 start_flag_animation;

extern legacy_s16 unused_legacy_word;		 // Unused legacy declaration.
extern legacy_s16 checkpoint_frame_interval; // fps * 30
extern legacy_s16 timer_ticks_per_frame;	 // 100 / fps
extern legacy_s16 track_angle;
extern legacy_s8 *steerWhlRespTable_ptr;
extern legacy_s8 steerWhlRespTable_10fps[62];
extern legacy_s8 steerWhlRespTable_20fps[64];
extern legacy_s8 start_finish_column, start_finish_row;
extern legacy_s8 hillFlag;
extern legacy_s16 hillHeightConsts[];

extern struct RECTANGLE rect_windshield;
extern legacy_s16 viewport_bottom_cache;
extern legacy_s16 run_game_random;
extern legacy_s8 replaybar_toggle;
extern legacy_s8 is_in_replay;
extern legacy_s8 replay_playback_speed;
extern legacy_s8 game_replay_mode_copy;
extern legacy_s8 frame_buffer_index;
extern legacy_s8 recording_limit_warning_requested;
extern legacy_s8 dashb_toggle;
extern legacy_s8 dashboard_buffer_index;
extern legacy_s8 show_penalty_counter;
extern legacy_s16 reserved_race_word;
extern legacy_s16 replay_overflow_acknowledged_word;
extern legacy_s8 mouse_driving_enabled;
extern void far *gameresptr;
extern void far *dasmshapeptr;
extern legacy_s8 dashb_toggle_copy;
extern legacy_s8 replaybar_toggle_copy;
extern legacy_s8 is_in_replay_copy;
extern legacy_s16 roofbmpheight_copy;
extern legacy_s8 dashboard_visible;
extern legacy_s8 replaybar_enabled;
extern legacy_s16 dashbmp_y_copy;
extern legacy_s16 height_above_replaybar;
extern legacy_s8 full_redraw_frames_remaining;
extern legacy_s8 replay_controls_drawn[];
extern legacy_s16 dastseg;
extern legacy_s16 dastbmp_y;
extern legacy_s16 dastbmp_y2;
extern legacy_s16 dashbmp_y;
extern legacy_s16 roofbmpheight;
extern struct RECTANGLE *active_frame_rects;

extern void update_player_tick(legacy_s8);
extern void emit_crash_particles(legacy_s16, legacy_s16, legacy_s16);
extern void update_crash_particles(void);
extern void init_kevinrandom(const legacy_s8 *seed);
extern void get_kevinrandom_seed(legacy_s8 *seed);
extern legacy_s16 get_kevinrandom(void);
extern void init_row_tables(void);
extern void init_trackdata(void);
extern void reset_race_loop_state(void);
extern void init_video_geometry_flags(void);

extern void update_frame(legacy_s8, struct RECTANGLE *rc);

extern legacy_s16 mouse_butstate;
extern legacy_s16 mouse_xpos;
extern legacy_s16 mouse_ypos;
extern legacy_s16 performGraphColor;
#define RESID_BUFFER_SIZE 80
#define RESID_TEXT_OFFSET 6
extern legacy_s8 resID_buffer[RESID_BUFFER_SIZE];
/* Legacy labels for byte 0 and byte 6 of the same scratch buffer. */
#define resID_byte1 resID_buffer[0]
#define resource_text_payload (resID_buffer + RESID_TEXT_OFFSET)
extern legacy_s16 waitflag;

extern void far *fontnptr;
extern void far *fontdefptr;
extern void far *mainresptr;
extern struct GAMESTATE far *cvxptr;
extern legacy_s16 trackrows[];
extern legacy_s16 terrainrows[];
extern legacy_s16 track_row_positions[];
extern legacy_s16 track_row_centers[];
extern legacy_s16 terrainpos[];
extern legacy_s16 terraincenterpos[];
extern legacy_s16 track_column_positions[];
extern legacy_s16 track_column_centers[];
extern legacy_s16 far *track_primary_route_links;	//trackdata1;
extern legacy_s16 far *track_alternate_route_links; //trackdata2;
extern legacy_s8 far *opponent_route_track_indices;
extern legacy_s16 far *player_aero_resistance_table;
extern legacy_s16 far *opponent_aero_resistance_table;
extern legacy_s16 far *reserved_trackside_camera_words;
extern legacy_s16 far *trackside_camera_ground_heights;
extern legacy_s16 far *roadside_sign_headings; //trackdata8;
/* TV camera positions, one vector per camera. */
extern struct VECTOR far *trackside_camera_positions;
/* Point each TV camera looks at, one vector per camera. */
extern struct VECTOR far *roadside_sign_positions; // trackdata10;
extern legacy_s8 far *track_highscore_table;
extern legacy_s8 far *sprite_background_state_stack;
extern legacy_s8 far *replay_header_buffer;
extern legacy_u8 far *track_element_map;
extern legacy_u8 far *track_terrain_map;
extern legacy_s8 far *replay_input_buffer;
extern legacy_s8 far *track_route_element_ids;
extern legacy_s8 far *track_route_traversal_flags;
extern legacy_u8 far *roadside_sign_indices_by_tile;
/* Saved element/terrain maps followed by the track and replay directories. */
extern legacy_s8 far *track_and_directory_backup;
extern legacy_s8 far *track_route_columns;
extern legacy_s8 far *track_route_rows;
extern legacy_u8 far *roadside_sign_shape_indices; // indexes into trkObjectList
extern legacy_s8 kbormouse;
extern legacy_s8 passed_security;
extern legacy_s8 g_is_busy;
extern legacy_s8 g_path_buf[];
extern legacy_s8 track_directory[];
extern legacy_s8 idle_expired;
extern legacy_u16 dialog_border_color;
extern legacy_s8 replay_directory[];
extern legacy_s8 replay_recording_flags;
extern legacy_s8 main_resource_name[];
extern legacy_s8 random_wait_legacy_bytes[];
extern legacy_s8 default_font_filename[];
extern legacy_s8 compact_font_filename[];
extern legacy_s8 track_arena_resource_name[];
extern legacy_s8 default_track_name[];
extern legacy_s8 checkpoint_resource_name[];
extern legacy_s8 track_editor_resource_wildcard[];
extern legacy_s8 selection_song_id[];
extern legacy_s8 selection_instrument_resource_name[];
extern legacy_s8 selection_music_resource_name[];
extern legacy_s8 main_exit_dialog_id[];

extern legacy_u16 framespersec;
extern legacy_u16 configured_frame_rate;
extern legacy_u16 slow_video_mgmt;
extern legacy_u16 slow_video_mgmt_copy;
extern legacy_u8 detail_level;

extern legacy_u16 pspofs;
extern legacy_u16 pspseg;
extern legacy_u16 mmgr_arena_end_segment;
extern legacy_u16 mmgr_arena_start_segment;

extern struct MEMCHUNK *mmgr_live_sentinel;
extern struct MEMCHUNK *mmgr_last_live_chunk;
extern struct MEMCHUNK *mmgr_first_cached_chunk;
extern struct MEMCHUNK *mmgr_cache_sentinel;
extern legacy_u16 mmgr_high_water_segment;

extern legacy_u16 slow_timer_deadline_low;
extern legacy_u16 slow_timer_deadline_high;
extern void(far *exitlistfuncs[])(void);
extern const legacy_s8 exit_handler_overflow_message[];

extern const legacy_s8 resource_slot_exhaustion_message[];
extern const legacy_s8 resource_arena_exhaustion_message[];
extern const legacy_s8 resource_block_missing_message[];
extern const legacy_s8 resource_resize_memory_exhaustion_message[];
extern const legacy_s8 resource_resize_non_top_block_message[];
extern const legacy_s8 file_read_error_format[];
extern const legacy_s8 file_write_error_format[];
extern const legacy_s8 file_size_error_format[];
extern const legacy_s8 invalid_pack_type_error_format[];
extern legacy_s8 audiodriverstring[];

extern legacy_u16 gState_frame;
extern legacy_s8 is_audioloaded;
extern void far *songfileptr;
extern void far *voicefileptr;
extern legacy_s8 textresprefix; // = 'e'
extern legacy_s8 *shapeexts[];
extern legacy_u8 palmap[];

extern legacy_s16 *material_clrlist_ptr;
extern legacy_s16 *material_clrlist_ptr_cpy;
extern legacy_s16 *material_clrlist2_ptr;
extern legacy_s16 *material_clrlist2_ptr_cpy;
extern legacy_s16 *material_patlist_ptr;
extern legacy_s16 *material_patlist_ptr_cpy;
extern legacy_s16 *material_patlist2_ptr;
extern legacy_s16 *material_patlist2_ptr_cpy;
extern legacy_u16 reserved_material_video_word;

extern legacy_s16 get_track_route_point(legacy_s16 car_route_index, struct VECTOR *car_route_target,
										legacy_s16 car_route_point_index,
										legacy_s8 *optional_speed);
extern void init_carstate_from_simd(struct CARSTATE *carstate, struct SIMD *simd,
									legacy_s8 transmission, legacy_s32 posX, legacy_s32 posY,
									legacy_s32 posZ, legacy_s16 track_angle);
extern void init_game_state(legacy_s16 initialization_mode);
extern void init_game_state_with_frame_rate(legacy_u16 frame_rate);
extern void init_game_state_with_frame_rate_byte(legacy_u16 frame_rate);
extern void restore_gamestate(legacy_u16 frame);
extern void update_gamestate(void);
/* Forward replay reconstruction; omit audio snapshots and checkpoint writes. */
extern void update_gamestate_silent(void);
/* Supply the original caller register when a tool uses a different replay loop. */
extern void update_gamestate_with_legacy_si(legacy_s16 caller_si);
extern void init_rect_arrays(void);
extern void frame_present(struct RECTANGLE *rect);
extern void font_set_fontdef(void);

extern legacy_s16 run_intro_looped(void);
extern legacy_s8 setup_intro(void);
extern legacy_s8 load_intro_resources(void);

extern legacy_s8 run_menu(void);
extern legacy_s8 setup_track(void);
extern void run_tracks_menu(legacy_s16 reload_track);
extern void run_opponent_menu(void);

extern void run_car_menu(legacy_s8 *carid, legacy_s8 *material, legacy_s8 *transmission,
						 legacy_u16 opponent_type);

extern legacy_u16 end_hiscore(void);
extern legacy_u16 run_option_menu(void);

extern void legacy_timer_shutdown(void);

extern void show_graphic_levels_menu(void);
extern void calibrate_joystick_driving(void);
extern void select_keyboard_driving(void);
extern void select_mouse_driving(void);
extern void toggle_music_with_dialog(void);
extern void show_pause_dialog(void);
extern void show_exit_to_dos_dialog(void);
extern void toggle_effects_with_dialog(void);

extern void mouse_draw_opaque(void);
extern void mouse_draw_transparent(void);

extern void video_set_mode4(void);
extern void video_set_mode7(void);
extern void video_set_mode_13h(void);

extern void sprite_set_target_clip_bounds(legacy_u16 left, legacy_u16 right, legacy_u16 top,
										  legacy_u16 bottom);

struct SPRITE;

extern void timer_setup_interrupt(void);

#ifdef RESTUNTS_SDL3
/* MinGW's CRT exports _stricmp with a different return type and ABI. */
#define _stricmp restunts_stricmp
#endif

extern void *_memcpy(void *, const void *, legacy_u16);
extern legacy_s8 *_strcpy(legacy_s8 *dest, const legacy_s8 *src);
extern legacy_s8 *_strcat(legacy_s8 *dest, const legacy_s8 *src);
extern legacy_s16 _strcmp(const legacy_s8 *dest, const legacy_s8 *src);
extern legacy_s16 _stricmp(const legacy_s8 *dest, const legacy_s8 *src);
extern legacy_u16 _strlen(const legacy_s8 *str);
extern void far *__fmemcpy(void far *, const void far *, legacy_u16);
extern legacy_u16 _abs(legacy_u16);
extern legacy_s16 _rand(void);
extern void _srand(legacy_u16);

#define memcpy _memcpy
#define strcpy _strcpy
#define strcat _strcat
#define strlen _strlen
#define fmemcpy __fmemcpy
#define strcmp _strcmp
#define stricmp _stricmp
#define abs _abs
#define printf _printf
#define rand _rand
#define srand _srand

#endif
