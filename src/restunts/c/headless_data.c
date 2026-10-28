#include "externs.h"
#include "physics_internal.h"
#include "state_internal.h"
#include "resource.h"
#include "race_stats.h"
#include "car_speed.h"
#include "track_types.h"
#include "track_objects.h"
#include "track_collision.h"
#include "wheel_transform.h"
#include "camera.h"
#include "car_resources.h"
#include "residue.h"

/* Mutable engine state formerly allocated by dseg.asm. */
struct GAMEINFO gameconfig;
struct GAMEINFO gameconfigcopy;
struct GAMESTATE state;
struct SIMD simd_player;
struct SIMD simd_opponent;

legacy_s16 video_shape_width_scale;
legacy_s16 video_x_alignment;
legacy_s16 video_x_alignment_mask;
legacy_s16 video_buffer_height_divisor;
legacy_s16 video_uses_page_flipping;
legacy_s16 video_page_count;

legacy_u8 frame_callback_countdown;
legacy_u8 slow_replay_countdown;
legacy_u16 elapsed_time1;
legacy_u16 elapsed_time2;
legacy_u8 race_exit_request;
legacy_u8 race_start_sequence_state;
legacy_u8 game_replay_mode;
legacy_s8 replay_filename[REPLAY_FILENAME_SIZE];
legacy_s16 start_flag_animation;
legacy_s16 checkpoint_frame_interval;
legacy_s16 timer_ticks_per_frame;
legacy_s16 track_angle;
legacy_s8 start_finish_column;
legacy_s8 start_finish_row;
legacy_s8 hillFlag;
legacy_s16 hillHeightConsts[2] = {0, 450};

legacy_s16 viewport_bottom_cache;
legacy_s16 run_game_random;
legacy_s8 replaybar_toggle;
legacy_s8 is_in_replay;
legacy_s8 cameramode;
legacy_s8 replay_playback_speed;
legacy_s8 game_replay_mode_copy;
legacy_s8 frame_buffer_index;
legacy_s8 recording_limit_warning_requested;
legacy_s8 dashb_toggle;
legacy_s8 dashboard_buffer_index;
legacy_s8 show_penalty_counter;
legacy_s16 replay_overflow_acknowledged_word;
legacy_s8 is_in_replay_copy;
legacy_s8 followOpponentFlag;
legacy_s8 idle_expired;
legacy_s8 kbormouse;
legacy_s8 g_is_busy;
legacy_u16 framespersec;

void far *gameresptr;
struct PLANE far *planptr;
struct PLANE far *current_planptr;
struct TRACK_WALL far *wallptr;
struct GAMESTATE far *cvxptr;

legacy_u16 legacy_closed_hihat_offset;

#ifdef RESTUNTS_SDL3
#define TRACK_POSITION_TABLE_CAPACITY 91
#else
#define TRACK_POSITION_TABLE_CAPACITY 30
#endif
legacy_s16 trackrows[TRACK_POSITION_TABLE_CAPACITY];
legacy_s16 terrainrows[TRACK_POSITION_TABLE_CAPACITY];
legacy_s16 track_row_positions[TRACK_POSITION_TABLE_CAPACITY];
legacy_s16 track_row_centers[TRACK_POSITION_TABLE_CAPACITY];
legacy_s16 terrainpos[TRACK_POSITION_TABLE_CAPACITY];
legacy_s16 terraincenterpos[TRACK_POSITION_TABLE_CAPACITY];
legacy_s16 track_column_positions[TRACK_POSITION_TABLE_CAPACITY];
legacy_s16 track_column_centers[TRACK_POSITION_TABLE_CAPACITY];

legacy_s16 far *track_primary_route_links;
legacy_s16 far *track_alternate_route_links;
legacy_s8 far *opponent_route_track_indices;
legacy_s16 far *player_aero_resistance_table;
legacy_s16 far *opponent_aero_resistance_table;
legacy_s16 far *reserved_trackside_camera_words;
legacy_s16 far *trackside_camera_ground_heights;
legacy_s16 far *roadside_sign_headings;
struct VECTOR far *trackside_camera_positions;
struct VECTOR far *roadside_sign_positions;
legacy_s8 far *track_highscore_table;
legacy_s8 far *sprite_background_state_stack;
legacy_s8 far *replay_header_buffer;
legacy_u8 far *track_element_map;
legacy_u8 far *track_terrain_map;
legacy_s8 far *replay_input_buffer;
legacy_s8 far *track_route_element_ids;
legacy_s8 far *track_route_traversal_flags;
legacy_u8 far *roadside_sign_indices_by_tile;
legacy_s8 far *track_and_directory_backup;
legacy_s8 far *track_route_columns;
legacy_s8 far *track_route_rows;
legacy_u8 far *roadside_sign_shape_indices;

#define FILE_DIALOG_PATH_BUFFER_SIZE 81U

legacy_s8 g_path_buf[94];
legacy_s8 track_directory[FILE_DIALOG_PATH_BUFFER_SIZE];
legacy_s8 replay_directory[FILE_DIALOG_PATH_BUFFER_SIZE];

legacy_s8 car_resource_name[] = "carcoun";
legacy_s8 opponent_resource_name[] = "opp1";
legacy_s8 opponent_name_text_id[] = "nam";
legacy_s8 opponent_path_resource_id[] = "path";
legacy_s8 opponent_speed_resource_id[] = "sped";
legacy_s8 gnam_string[32];
legacy_s8 gsna_string[32];
legacy_s8 opponent_highscore_name[3];

legacy_s8 textresprefix = 'e';
const legacy_s8 missing_shape_error_format[] = "locateshape - %-4.4s SHAPE NOT FOUND\r\n";
const legacy_s8 missing_sound_error_format[] = "locatesound - %-4.4s SOUND NOT FOUND\r\n";

/* Lookup tables whose byte-for-byte values affect replay simulation. */
legacy_u8 atantable[257] = {
	0,	 1,	  1,   2,	3,	 3,	  4,   4,	5,	 6,	  6,   7,	8,	 8,	  9,   10,	10,	 11,  11,
	12,	 13,  13,  14,	15,	 15,  16,  16,	17,	 18,  18,  19,	20,	 20,  21,  22,	22,	 23,  23,
	24,	 25,  25,  26,	27,	 27,  28,  28,	29,	 30,  30,  31,	31,	 32,  33,  33,	34,	 34,  35,
	36,	 36,  37,  38,	38,	 39,  39,  40,	41,	 41,  42,  42,	43,	 44,  44,  45,	45,	 46,  46,
	47,	 48,  48,  49,	49,	 50,  51,  51,	52,	 52,  53,  53,	54,	 55,  55,  56,	56,	 57,  57,
	58,	 58,  59,  60,	60,	 61,  61,  62,	62,	 63,  63,  64,	65,	 65,  66,  66,	67,	 67,  68,
	68,	 69,  69,  70,	70,	 71,  71,  72,	72,	 73,  74,  74,	75,	 75,  76,  76,	77,	 77,  78,
	78,	 79,  79,  80,	80,	 81,  81,  82,	82,	 83,  83,  84,	84,	 84,  85,  85,	86,	 86,  87,
	87,	 88,  88,  89,	89,	 90,  90,  91,	91,	 91,  92,  92,	93,	 93,  94,  94,	95,	 95,  96,
	96,	 96,  97,  97,	98,	 98,  99,  99,	99,	 100, 100, 101, 101, 102, 102, 102, 103, 103, 104,
	104, 104, 105, 105, 106, 106, 106, 107, 107, 108, 108, 108, 109, 109, 110, 110, 110, 111, 111,
	112, 112, 112, 113, 113, 113, 114, 114, 115, 115, 115, 116, 116, 116, 117, 117, 118, 118, 118,
	119, 119, 119, 120, 120, 120, 121, 121, 121, 122, 122, 122, 123, 123, 123, 124, 124, 124, 125,
	125, 125, 126, 126, 126, 127, 127, 127, 128, 128};

legacy_s8 steerWhlRespTable_20fps[64] = {0, 8, -8, 0, 0, 7, -7, 0, 0, 6, -6, 0, 0, 5, -5, 0,
										 0, 4, -4, 0, 0, 4, -4, 0, 0, 3, -3, 0, 0, 3, -3, 0,
										 0, 2, -2, 0, 0, 2, -2, 0, 0, 2, -2, 0, 0, 1, -1, 0,
										 0, 1, -1, 0, 0, 1, -1, 0, 0, 1, -1, 0, 0, 1, -1, 0};

legacy_s8 steerWhlRespTable_10fps[62] = {0, 16, -16, 0, 0, 14, -14, 0, 0, 12, -12, 0, 0, 10, -10, 0,
										 0, 8,	-8,	 0, 0, 8,  -8,	0, 0, 6,  -6,  0, 0, 6,	 -6,  0,
										 0, 4,	-4,	 0, 0, 4,  -4,	0, 0, 4,  -4,  0, 0, 2,	 -2,  0,
										 0, 2,	-2,	 0, 0, 1,  -1,	0, 0, 1,  -1,  0, 0, 1};

legacy_s8 *steerWhlRespTable_ptr;
legacy_s16 grassDecelDivTab[5] = {255, 256, 192, 128, 64};

legacy_u8 roadside_sign_forward_types[6] = {0, 0, 1, 0, 1, 0};
legacy_u8 roadside_sign_reverse_types[6] = {0, 1, 0, 0, 1, 0};
legacy_u8 terrConnDataEtoW[20] = {0, 0, 0, 0, 0, 0, 1, 2, 1, 3, 0, 2, 3, 0, 0, 1, 1, 3, 2, 0};
legacy_u8 terrConnDataWtoE[20] = {0, 0, 0, 0, 0, 0, 1, 2, 0, 3, 1, 0, 0, 3, 2, 2, 3, 1, 1, 0};
legacy_u8 terrConnDataNtoS[20] = {0, 0, 0, 0, 0, 0, 1, 1, 5, 0, 4, 5, 0, 0, 4, 1, 5, 4, 1, 0};
legacy_u8 terrConnDataStoN[19] = {0, 0, 0, 0, 0, 0, 1, 0, 5, 1, 4, 0, 5, 4, 0, 5, 1, 1, 4};

struct POINT2D breakable_object_bounds[2] = {{5, 40}, {5, 10}};
struct POINT2D start_finish_pole_bounds[2] = {{6, 121}, {6, 9}};
struct POINT2D track_auxiliary_obstacle_bounds[2] = {{1, 10}, {1, 10}};
legacy_s16 wheel_gravity_steps[4] = {21, 21, 15, 15};

struct VECTOR scenery_collision_points[1] = {{0, 0, 0}};
struct VECTOR elevated_road_collision_points[8] = {{-120, 0, -281}, {-120, 0, -231}, {-120, 0, 281},
												   {-120, 0, 231},	{120, 0, -281},	 {120, 0, -231},
												   {120, 0, 281},	{120, 0, 231}};
struct VECTOR corkscrew_lr_collision_points[2] = {{-60, 0, -512}, {60, 0, 512}};
struct VECTOR corkscrew_up_collision_points[2] = {{-392, 0, 0}, {-632, 0, 0}};
struct VECTOR corkscrew_down_collision_points[2] = {{392, 0, 0}, {632, 0, 0}};
struct VECTOR slalom_collision_points[4] = {
	{23, 0, -255}, {97, 0, -255}, {-97, 0, 255}, {-23, 0, 255}};

/* Shared simulation scratch data. */
struct MATRIX car_to_world_rotation;
struct MATRIX wheel_heading_rotation;
struct MATRIX plane_heading_rotation;
struct VECTOR wheel_forward_travel;
struct VECTOR wheel_world_travel;
legacy_s32 car_working_x;
legacy_s32 car_working_y;
legacy_s32 car_working_z;
legacy_s16 car_working_roll;
legacy_s16 car_initial_roll;
legacy_s16 car_working_yaw;
legacy_s16 car_initial_yaw;
legacy_s16 car_working_pitch;
legacy_s16 car_initial_pitch;
legacy_s16 planindex;
legacy_s16 planindex_copy;
legacy_s16 wheel_heading_offset;
legacy_s16 cached_plane_heading = 9999;
legacy_s16 cached_wheel_heading = 9999;
legacy_s16 elem_xCenter;
legacy_s16 elem_zCenter;
legacy_s16 terrainHeight;
legacy_s8 current_surf_type;
legacy_s16 nextPosAndNormalIP;
legacy_s16 wallindex;
legacy_s16 elRdWallRelated;
legacy_s16 wallHeight;
legacy_s16 wallStartX;
legacy_s16 wallStartZ;
legacy_s16 wallOrientation;
legacy_s8 track_wall_collision_enabled;
legacy_s8 corkFlag;
legacy_s16 penalty_time;
legacy_s16 track_pieces_counter;
legacy_u8 roadside_sign_count;
legacy_u8 track_validation_column;
legacy_u8 track_validation_row;
legacy_u8 trackside_camera_count;
/* The original DGROUP reserves sixteen consecutive opponent-speed bytes. */
legacy_u8 oppnentSped[OPPONENT_SPEED_COUNT];
legacy_u8 vector_saved_z_low;
legacy_u8 vector_saved_z_high;
legacy_u16 legacy_divide_fault_segment;
legacy_u16 legacy_divide_fault_offset;

/* Track collision bounds.  Adjacent legacy labels intentionally exposed
 * overlapping windows; the C arrays make each window explicit. */
legacy_s16 loopSurface_ZBounds0[6] = {0, 224, 389, 449, 389, 224};
legacy_s16 loopSurface_ZBounds1[6] = {224, 389, 449, 389, 224, 0};
legacy_s16 loopSurface_maxZ = 449;
legacy_s16 loopSurface_XBounds0[6] = {-400, -400, -352, -304, -270, -235};
legacy_s16 loopSurface_XBounds1[6] = {-400, -352, -304, -270, -235, -200};
legacy_s16 loopBase_ZBounds0[6] = {0, 178, 360, 536, 704, 868};
legacy_s16 loopBase_ZBounds1[6] = {178, 360, 536, 704, 868, 2000};
legacy_s16 loopBae_InnXBounds0[6] = {0, -20, -40, -60, -80, -100};
legacy_s16 loopBase_InnXBounds1[6] = {-20, -40, -60, -80, -100, -120};
legacy_s16 loopBase_OutXBounds0[6] = {400, 361, 320, 276, 226, 174};
legacy_s16 loopBase_OutXBounds1[6] = {361, 320, 276, 226, 174, 120};
legacy_s16 bkRdEntr_triang_zAdjust[4] = {-251, -84, 84, 251};
legacy_s16 corkLR_negZBound[12] = {0,	 -94,  -187, -280, -373, -466,
								   -559, -652, -745, -838, -931, -1024};
legacy_s16 corkLR_posZBound[12] = {0, 1024, 931, 838, 745, 652, 559, 466, 373, 280, 187, 94};
legacy_s16 highEntrZBounds0[6] = {-512, -334, -168, 0, 168, 334};
legacy_s16 highEntrZBounds1[6] = {-334, -168, 0, 168, 334, 1000};
legacy_s16 highEntrXInnBounds0[6] = {0, 0, 0, 0, 0, 120};
legacy_s16 highEntrXInnBounds1[6] = {0, 0, 0, 0, 120, 120};
legacy_s16 highEntrXOutBounds0[6] = {120, 168, 216, 264, 312, 360};
legacy_s16 highEntrXOutBounds1[6] = {168, 216, 264, 312, 360, 360};
