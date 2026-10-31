#ifndef RESTUNTS_TRACK_SETUP_INTERNAL_H
#define RESTUNTS_TRACK_SETUP_INTERNAL_H
#include "track_types.h"
#include "../extended/extended_track_setup.h"
#define TRACK_GRID_SIZE 30
#define TRACK_GRID_LAST_INDEX 29
#define TRACK_SETUP_TILE_COUNT 901U
#define TRACK_TERRAIN_NO_CONNECTION 99U
#define TRACK_INVALID_ELEMENT_FIRST 253U
#define TRACK_LARGE_ELEMENT_FIRST 182U
#define TRACK_GENERIC_LARGE_ELEMENT 4U
#define TRACK_TILE_INDEX_UNASSIGNED 255U
#define TRACK_TERRAIN_HILL 6U
#define TRACK_HILLROAD_TERRAIN_FIRST 7U
#define TRACK_HILLROAD_TERRAIN_END 11U
#define TRACK_CAMERA_COUNT_MAX 64U
#define TRACK_CAMERA_RESERVED_INDEX 48U
#define TRACK_CAMERA_HILL_HEIGHT 450
#define TRACK_PIECE_SUBTYPE_MASK 15U
#define TRACK_PIECE_REVERSE_FLAG 16U
#define TRACK_PIECE_TRAVERSAL_SHIFT 4U
#define TRACK_ARROW_NONE 0U
#define TRACK_CAMERA_VECTOR_STRIDE 2U
#define TRACK_CAMERA_VECTOR_FORWARD_OFFSET 1U
#define TRACK_CAMERA_VECTOR_REVERSE_OFFSET 2U
#define TRACK_ORIENTATION_COUNT 4U
enum TRACK_ORIENTATION_INDEX {
	TRACK_ORIENTATION_INDEX_NORTH = 0,
	TRACK_ORIENTATION_INDEX_EAST = 1,
	TRACK_ORIENTATION_INDEX_SOUTH = 2,
	TRACK_ORIENTATION_INDEX_WEST = 3
};

enum TRACK_ORIENTATION {
	TRACK_ORIENTATION_NORTH = 0,
	TRACK_ORIENTATION_EAST = ANGLE_QUARTER_TURN,
	TRACK_ORIENTATION_SOUTH = ANGLE_HALF_TURN,
	TRACK_ORIENTATION_WEST = ANGLE_THREE_QUARTER_TURN
};

#define TRACK_ORIENTATION_NOT_FOUND (-1)
#define TRACK_PREVIOUS_PIECE_NONE (-1)
enum TRACK_TRAVERSAL {
	TRACK_TRAVERSAL_UNMATCHED = -1,
	TRACK_TRAVERSAL_FORWARD = 0,
	TRACK_TRAVERSAL_REVERSE = 1
};
#define TRACK_EXIT_POINT_FIRST 1U
#define TRACK_EXIT_POINT_LAST 12U
#define TRACK_EXIT_POINT_TABLE_SIZE 13U
#define TRACK_ENTRY_POINT_NONE 0U

enum TRACK_SEAM_SCAN_DIRECTION { TRACK_SEAM_SCAN_COLUMNS = 0, TRACK_SEAM_SCAN_ROWS = 1 };

#define TRACK_JUMP_CONNECTION_CODE 1U
#define TRACK_JUMP_MAX_EMPTY_TILES 2U
#define TRACK_JUMP_BACKTRACK_LENGTH_LIMIT 1U
#define TRACK_RUNWAY_MINIMUM_LENGTH 2U
#define TRACK_CAMERA_RUNWAY_THRESHOLD 3U
#define TRACK_START_FINISH_VARIANT_COUNT 3U
#define PLAN_TRACK_ROUTE_LENGTH 18U
#define PLAN_TRACK_ROUTE_ENTRY_SIZE 2U
#define PLAN_TRACK_START_ROW 28
#define PLAN_TRACK_OPPONENT_SPEED 200U
#define PLAN_TRACK_PATH_INDEX 28U
#define PLAN_TRACK_PATH_Z_OFFSET 302
#define PLAN_TRACK_OPPONENT_X 96000L
#define PLAN_TRACK_POSITION_SHIFT 6U

#pragma pack(push, 1)
struct TRACK_SETUP_BRANCH {
	legacy_s8 column;
	legacy_s8 row;
	legacy_u8 tile_element;
	legacy_u8 subtype;
	legacy_s8 connection_status;
	legacy_u8 runway_length;
	legacy_s8 previous_column;
	legacy_s8 previous_row;
	legacy_u8 previous_tile_element;
	legacy_u8 previous_subtype;
	legacy_s8 previous_connection_status;
	legacy_u8 previous_connection_code;
	legacy_s16 previous_piece;
};
#pragma pack(pop)

typedef legacy_s8
	track_setup_branch_must_be_14_bytes[(sizeof(struct TRACK_SETUP_BRANCH) == 14) ? 1 : -1];

/* The traversal scratch arrays remain on the caller's stack. Deferred paths
 * retain the original packed representation in their far allocation. */
struct TRACK_SETUP_WALK {
	struct TRACK_SETUP_BRANCH far *branches;
	legacy_u8 visited_tiles[TRACK_SETUP_VISITED_TILE_CAPACITY];
	legacy_u8 subtype_by_piece[TRACK_SETUP_PIECE_CAPACITY];
	legacy_s8 connection_by_piece[TRACK_SETUP_PIECE_CAPACITY];
	legacy_u16 branch_count;
	legacy_s16 previous_piece;
	legacy_s16 orientation;
	legacy_u8 tile_element;
	legacy_u8 tile_entry_point;
	legacy_u8 previous_connection_code;
	legacy_u8 subtype;
	legacy_s8 connection_status;
	legacy_u8 previous_subtype;
	legacy_s8 previous_connection_status;
	legacy_u8 previous_tile_element;
	legacy_s8 previous_column;
	legacy_s8 previous_row;
	legacy_s8 column;
	legacy_s8 row;
	legacy_u8 runway_length;
	legacy_u8 jump_length;
	legacy_u8 path_closed;
	legacy_u8 match_count;
	legacy_u8 backtrack_required;
};

extern const legacy_u8 track_entry_points_northwest[TRACK_ORIENTATION_COUNT];
extern const legacy_u8 track_entry_points_north[TRACK_ORIENTATION_COUNT];
extern const legacy_u8 track_entry_points_west[TRACK_ORIENTATION_COUNT];
extern const legacy_u8 track_entry_points_owner[TRACK_ORIENTATION_COUNT];
legacy_u8 track_setup_entry_point(const legacy_u8 *points, legacy_s16 orientation);
legacy_s16 track_setup_error(struct TRACK_SETUP_BRANCH far *branches, legacy_u8 error_code,
							 legacy_s8 column, legacy_s8 row);
legacy_s16 track_setup_match_blocks(struct TRACK_SETUP_WALK *walk);
#endif
