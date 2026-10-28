#include "trackdata_layout.h"
#include "memmgr.h"
#include "car_speed.h"
#include "track_types.h"
#include "track_objects.h"
#include "track_collision.h"
#include "opponent.h"
#include "externs.h"
#include "extended_track.h"

#define TRACK_GRID_SIZE 30
#define TRACK_GRID_LAST_INDEX 29
#define TRACK_SETUP_TILE_COUNT 901U
#ifdef RESTUNTS_SDL3
#define TRACK_SETUP_BRANCH_COUNT EXTENDED_TRACK_ROUTE_CAPACITY
#define TRACK_SETUP_VISITED_TILE_CAPACITY (EXTENDED_TRACK_MAX_CELLS + 4U)
#define TRACK_SETUP_PIECE_CAPACITY (EXTENDED_TRACK_ROUTE_CAPACITY + 1U)
#else
#define TRACK_SETUP_BRANCH_COUNT 64U
#define TRACK_SETUP_VISITED_TILE_CAPACITY 904U
#define TRACK_SETUP_PIECE_CAPACITY 902U
#endif
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

enum TRACK_SETUP_ERROR {
	TRACK_SETUP_OK = 0,
	TRACK_SETUP_NO_START_FINISH = 1,
	TRACK_SETUP_INTERNAL_ERROR = 2,
	TRACK_SETUP_MANY_START_FINISH = 3,
	TRACK_SETUP_ELEMENT_MISMATCH = 4,
	TRACK_SETUP_WRONG_WAY = 5,
	TRACK_SETUP_MANY_ELEMENTS = 6,
	TRACK_SETUP_NO_PATH = 7,
	TRACK_SETUP_MANY_PATHS = 8,
	TRACK_SETUP_NO_RUNWAY = 9,
	TRACK_SETUP_LONG_JUMP = 10,
	TRACK_SETUP_TERRAIN_MISMATCH = 11
};

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

/* Arriving on a tile, the entry point the walk uses depends on the direction
 * of travel and on which continuation marker (if any) sent it here. Each row
 * is indexed by orientation / ANGLE_QUARTER_TURN; a zero means
 * the combination does not
 * occur, exactly as the original chains fell through to zero. */
static const legacy_u8 track_entry_points_northwest[TRACK_ORIENTATION_COUNT] = {12, 0, 0, 9};
static const legacy_u8 track_entry_points_north[TRACK_ORIENTATION_COUNT] = {11, 6, 0, 7};
static const legacy_u8 track_entry_points_west[TRACK_ORIENTATION_COUNT] = {10, 0, 5, 8};
static const legacy_u8 track_entry_points_owner[TRACK_ORIENTATION_COUNT] = {2, 4, 1, 3};

static const legacy_u8
	track_start_finish_elements[TRACK_ORIENTATION_COUNT][TRACK_START_FINISH_VARIANT_COUNT] = {
		{1, 134, 147}, {136, 149, 180}, {135, 148, 179}, {137, 150, 181}};

static legacy_s16 track_setup_start_finish_orientation(legacy_u8 tile_element)
{
	for (legacy_u16 orientation_index = 0U; orientation_index < TRACK_ORIENTATION_COUNT;
		 orientation_index++) {
		for (legacy_u16 variant_index = 0U; variant_index < TRACK_START_FINISH_VARIANT_COUNT;
			 variant_index++) {
			if (tile_element == track_start_finish_elements[orientation_index][variant_index]) {
				return (legacy_s16)(orientation_index * ANGLE_QUARTER_TURN);
			}
		}
	}
	return TRACK_ORIENTATION_NOT_FOUND;
}

static legacy_u8 track_setup_entry_point(const legacy_u8 *points, legacy_s16 orientation)
{
	if (orientation == TRACK_ORIENTATION_NORTH) {
		return points[TRACK_ORIENTATION_INDEX_NORTH];
	}
	if (orientation == TRACK_ORIENTATION_EAST) {
		return points[TRACK_ORIENTATION_INDEX_EAST];
	}
	if (orientation == TRACK_ORIENTATION_SOUTH) {
		return points[TRACK_ORIENTATION_INDEX_SOUTH];
	}
	if (orientation == TRACK_ORIENTATION_WEST) {
		return points[TRACK_ORIENTATION_INDEX_WEST];
	}
	return TRACK_ENTRY_POINT_NONE;
}

/* Leaving a piece through one of its exit points steps on to the neighbour
 * tile and fixes the orientation the next piece is entered with. Entry 0 is
 * never used: the original switch had no case for it and left the position
 * and the orientation untouched. */
struct TRACK_SETUP_STEP {
	legacy_s16 column;
	legacy_s16 row;
	legacy_s16 orientation;
};

static const struct TRACK_SETUP_STEP track_setup_steps[TRACK_EXIT_POINT_TABLE_SIZE] = {
	{0, 0, TRACK_ORIENTATION_NORTH}, {0, -1, TRACK_ORIENTATION_NORTH},
	{0, 1, TRACK_ORIENTATION_SOUTH}, {1, 0, TRACK_ORIENTATION_EAST},
	{-1, 0, TRACK_ORIENTATION_WEST}, {1, -1, TRACK_ORIENTATION_NORTH},
	{-1, 1, TRACK_ORIENTATION_WEST}, {1, 1, TRACK_ORIENTATION_EAST},
	{2, 0, TRACK_ORIENTATION_EAST},	 {2, 1, TRACK_ORIENTATION_EAST},
	{1, 1, TRACK_ORIENTATION_SOUTH}, {0, 2, TRACK_ORIENTATION_SOUTH},
	{1, 2, TRACK_ORIENTATION_SOUTH}};

static const legacy_u16 plan_track_route[PLAN_TRACK_ROUTE_LENGTH] = {0, 1, 2, 3, 4, 1, 2, 3, 4,
																	 1, 2, 3, 4, 0, 1, 2, 3, 0};

static legacy_s8 track_setup_add_s8(legacy_s8 value, legacy_s16 amount)
{
	return LEGACY_S8_WRAP_ADD(value, amount);
}

static void track_setup_rotate_vector(struct VECTOR *vector, legacy_s16 orientation)
{
	legacy_s16 temporary;

	if (orientation == TRACK_ORIENTATION_EAST) {
		temporary = vector->x;
		vector->x = vector->z;
		vector->z = LEGACY_S16_WRAP_NEGATE(temporary);
	} else if (orientation == TRACK_ORIENTATION_SOUTH) {
		vector->x = LEGACY_S16_WRAP_NEGATE(vector->x);
		vector->z = LEGACY_S16_WRAP_NEGATE(vector->z);
	} else if (orientation == TRACK_ORIENTATION_WEST) {
		temporary = vector->x;
		vector->x = LEGACY_S16_WRAP_NEGATE(vector->z);
		vector->z = temporary;
	}
}

static void track_setup_link_piece(legacy_s16 source_piece, legacy_s16 destination_piece)
{
	if (track_primary_route_links[source_piece] == TRACK_ROUTE_LINK_NONE) {
		track_primary_route_links[source_piece] = destination_piece;
	} else {
		track_alternate_route_links[source_piece] = destination_piece;
	}
}

static legacy_s16 track_setup_error(struct TRACK_SETUP_BRANCH far *branches, legacy_u8 error_code,
									legacy_s8 column, legacy_s8 row)
{
	if (column == -1) {
		column = 0;
	} else if (column == extended_track_width()) {
		column = (legacy_s8)(extended_track_width() - 1);
	}
	if (row == -1) {
		row = 0;
	} else if (row == extended_track_height()) {
		row = (legacy_s8)(extended_track_height() - 1);
	}
	track_validation_column = (legacy_u8)column;
	track_validation_row = (legacy_u8)row;
	mmgr_release((legacy_s8 far *)branches);
	return error_code;
}

/* Neighbouring terrain tiles have to agree on the connection code of the
 * edge they share. The map is walked twice the same way: once west to east
 * along every row, once north to south down every column. Returns 0 when
 * every seam matches, otherwise the reported error code. */
static legacy_s16 track_setup_terrain_seams(struct TRACK_SETUP_BRANCH far *branches,
											const legacy_u8 *incoming, const legacy_u8 *outgoing,
											legacy_s16 scan_direction)
{
	/* CSTRK shares corner vertices and validates slopes at decode time. */
	if (extended_track_active()) {
		return TRACK_SETUP_OK;
	}
	for (legacy_s8 outer = 0; outer < TRACK_GRID_SIZE; outer++) {
		legacy_u8 previous_connection_code = TRACK_TERRAIN_NO_CONNECTION;
		for (legacy_s8 inner = 0; inner < TRACK_GRID_SIZE; inner++) {
			legacy_s8 row = scan_direction == TRACK_SEAM_SCAN_ROWS ? outer : inner;
			legacy_s8 column = scan_direction == TRACK_SEAM_SCAN_ROWS ? inner : outer;
			legacy_u8 tile_terrain = track_terrain_map[terrainrows[row] + column];
			if (incoming[tile_terrain] != previous_connection_code &&
				previous_connection_code != TRACK_TERRAIN_NO_CONNECTION) {
				return track_setup_error(branches, TRACK_SETUP_TERRAIN_MISMATCH, column, row);
			}
			previous_connection_code = outgoing[tile_terrain];
		}
	}

	return TRACK_SETUP_OK;
}

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

static legacy_s16 track_setup_find_start(struct TRACK_SETUP_WALK *walk)
{
	legacy_u8 start_finish_count = 0;
	for (walk->row = 0; walk->row < extended_track_height(); walk->row++) {
		for (walk->column = 0; walk->column < extended_track_width(); walk->column++) {
			legacy_s16 tile_index = trackrows[walk->row] + walk->column;
			walk->tile_element = track_element_map[tile_index];
#ifdef RESTUNTS_SDL3
			if (extended_track_active()) {
				const struct CSTRK_OBJECT *object =
					extended_track_object_at(walk->column, walk->row);
				walk->tile_element = object && object->x == walk->column && object->y == walk->row
										 ? (legacy_u8)extended_track_object_id(object)
										 : 0;
			}
#endif
			if (walk->tile_element >= TRACK_INVALID_ELEMENT_FIRST) {
				walk->tile_element = 0;
			}
			if (!extended_track_active() && walk->tile_element >= TRACK_LARGE_ELEMENT_FIRST) {
				walk->tile_element = TRACK_GENERIC_LARGE_ELEMENT;
				track_element_map[tile_index] = TRACK_GENERIC_LARGE_ELEMENT;
			}

			walk->orientation = track_setup_start_finish_orientation(walk->tile_element);

			if (walk->orientation != TRACK_ORIENTATION_NOT_FOUND) {
				track_angle = walk->orientation;
				if (start_finish_count != 0) {
					return track_setup_error(walk->branches, TRACK_SETUP_MANY_START_FINISH,
											 walk->column, walk->row);
				}
				start_finish_column = walk->column;
				start_finish_row = walk->row;
				legacy_u8 tile_terrain = track_terrain_map[terrainrows[walk->row] + walk->column];
				hillFlag = tile_terrain == TRACK_TERRAIN_HILL;
				start_finish_count = LEGACY_U8_WRAP_ADD(start_finish_count, 1U);
			}
		}
	}

	if (start_finish_count == 0) {
		return track_setup_error(walk->branches, TRACK_SETUP_NO_START_FINISH, walk->column,
								 walk->row);
	}

	return TRACK_SETUP_OK;
}

static void track_setup_revisit(struct TRACK_SETUP_WALK *walk, legacy_u16 block_index)
{
	if (walk->connection_status != TRACK_TRAVERSAL_UNMATCHED &&
		walk->visited_tiles[trackrows[walk->row] + walk->column] != 0) {
		for (legacy_s16 existing_piece = 0; existing_piece < track_pieces_counter;
			 existing_piece++) {
			if ((legacy_u8)track_route_columns[existing_piece] == (legacy_u8)walk->column &&
				(legacy_u8)track_route_rows[existing_piece] == (legacy_u8)walk->row &&
				walk->subtype_by_piece[existing_piece] == (legacy_u8)block_index &&
				walk->connection_by_piece[existing_piece] == walk->connection_status &&
				(!extended_track_active() ||
				 (legacy_u8)track_route_element_ids[existing_piece] == walk->tile_element)) {
				walk->connection_status = TRACK_TRAVERSAL_UNMATCHED;
				track_setup_link_piece(walk->previous_piece, existing_piece);
				if (existing_piece == 0) {
					walk->path_closed = 1;
				}
				break;
			}
		}
	}
}

static legacy_s16 track_setup_push_branch(struct TRACK_SETUP_WALK *walk, legacy_u16 block_index)
{
	if (walk->branch_count == (extended_track_active() ? TRACK_SETUP_BRANCH_COUNT : 64U)) {
		return track_setup_error(walk->branches, TRACK_SETUP_MANY_PATHS, walk->column, walk->row);
	}
	struct TRACK_SETUP_BRANCH far *branch = &walk->branches[walk->branch_count];
	branch->column = walk->column;
	branch->row = walk->row;
	branch->tile_element = walk->tile_element;
	branch->subtype = (legacy_u8)block_index;
	branch->connection_status = walk->connection_status;
	branch->previous_connection_code = walk->previous_connection_code;
	branch->previous_piece = walk->previous_piece;
	branch->runway_length = walk->runway_length;
	branch->previous_column = walk->previous_column;
	branch->previous_row = walk->previous_row;
	branch->previous_tile_element = walk->previous_tile_element;
	branch->previous_subtype = walk->previous_subtype;
	branch->previous_connection_status = walk->previous_connection_status;
	walk->branch_count = LEGACY_U16_WRAP_ADD(walk->branch_count, 1U);
	return TRACK_SETUP_OK;
}

static legacy_s16 track_setup_match_blocks(struct TRACK_SETUP_WALK *walk)
{
	struct TRACKOBJECT *track_object = &trkObjectList[walk->tile_element];
	struct TRKOBJINFO *track_info = track_object->ss_trkObjInfoPtr;
	legacy_s8 selected_connection_status = TRACK_TRAVERSAL_UNMATCHED;
	struct TRKOBJINFO *current_info;
	if (track_info != 0) {
		for (legacy_u16 block_index = 0; block_index < (legacy_u8)track_info->si_noOfBlocks;
			 block_index++) {
			current_info = &track_info[block_index];
			walk->connection_status = TRACK_TRAVERSAL_UNMATCHED;
			if ((legacy_u8)current_info->si_entryPoint == walk->tile_entry_point) {
				if ((legacy_u8)current_info->si_entryType != walk->previous_connection_code) {
					return track_setup_error(walk->branches, TRACK_SETUP_ELEMENT_MISMATCH,
											 walk->column, walk->row);
				}
				walk->connection_status = TRACK_TRAVERSAL_FORWARD;
			} else if ((legacy_u8)current_info->si_exitPoint == walk->tile_entry_point) {
				if ((legacy_u8)current_info->si_exitType != walk->previous_connection_code) {
					return track_setup_error(walk->branches, TRACK_SETUP_ELEMENT_MISMATCH,
											 walk->column, walk->row);
				}
				walk->connection_status = TRACK_TRAVERSAL_REVERSE;
			}

			track_setup_revisit(walk, block_index);

			if (walk->connection_status != TRACK_TRAVERSAL_UNMATCHED) {
				if (walk->match_count == 0) {
					walk->subtype = (legacy_u8)block_index;
					selected_connection_status = walk->connection_status;
				} else {
					legacy_s16 seam_status = track_setup_push_branch(walk, block_index);
					if (seam_status != TRACK_SETUP_OK) {
						return seam_status;
					}
				}
				walk->match_count = LEGACY_U8_WRAP_ADD(walk->match_count, 1U);
			}
		}
	}
	if (walk->match_count != 0) {
		walk->connection_status = selected_connection_status;
	}
	return TRACK_SETUP_OK;
}

static legacy_s16 track_setup_resolve_piece(struct TRACK_SETUP_WALK *walk)
{
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		const struct CSTRK_OBJECT *object = extended_track_object_at(walk->column, walk->row);
		if (!object) {
			walk->tile_element = 0;
			return TRACK_SETUP_OK;
		}
		int dx = walk->column - object->x, dy = walk->row - object->y;
		walk->tile_element = (legacy_u8)extended_track_object_id(object);
		walk->column = (legacy_s8)object->x;
		walk->row = (legacy_s8)object->y;
		const legacy_u8 *entries =
			dy ? (dx ? track_entry_points_northwest : track_entry_points_north)
			   : (dx ? track_entry_points_west : track_entry_points_owner);
		walk->tile_entry_point = track_setup_entry_point(entries, walk->orientation);
		if (!walk->jump_length && walk->tile_entry_point == TRACK_ENTRY_POINT_NONE) {
			return track_setup_error(walk->branches, TRACK_SETUP_INTERNAL_ERROR, walk->column,
									 walk->row);
		}
		return track_setup_match_blocks(walk);
	}
#endif
	walk->tile_element = track_element_map[trackrows[walk->row] + walk->column];
	legacy_u8 tile_terrain = track_terrain_map[terrainrows[walk->row] + walk->column];
	if (walk->tile_element != 0 && tile_terrain >= TRACK_HILLROAD_TERRAIN_FIRST &&
		tile_terrain < TRACK_HILLROAD_TERRAIN_END) {
		walk->tile_element = subst_hillroad_track(tile_terrain, walk->tile_element);
	}

	if (walk->tile_element == TRACK_TILE_CONTINUATION_SOUTHEAST) {
		walk->column = track_setup_add_s8(walk->column, -1);
		walk->row = track_setup_add_s8(walk->row, -1);
		walk->tile_entry_point =
			track_setup_entry_point(track_entry_points_northwest, walk->orientation);
		walk->tile_element = track_element_map[trackrows[walk->row] + walk->column];
	} else if (walk->tile_element == TRACK_TILE_CONTINUATION_SOUTH) {
		walk->row = track_setup_add_s8(walk->row, -1);
		walk->tile_entry_point =
			track_setup_entry_point(track_entry_points_north, walk->orientation);
		walk->tile_element = track_element_map[trackrows[walk->row] + walk->column];
	} else if (walk->tile_element == TRACK_TILE_CONTINUATION_EAST) {
		walk->column = track_setup_add_s8(walk->column, -1);
		walk->tile_entry_point =
			track_setup_entry_point(track_entry_points_west, walk->orientation);
		walk->tile_element = track_element_map[trackrows[walk->row] + walk->column];
	} else {
		walk->tile_entry_point =
			track_setup_entry_point(track_entry_points_owner, walk->orientation);
	}

	if (walk->jump_length == 0 && walk->tile_entry_point == TRACK_ENTRY_POINT_NONE) {
		return track_setup_error(walk->branches, TRACK_SETUP_INTERNAL_ERROR, walk->column,
								 walk->row);
	}
	return track_setup_match_blocks(walk);
}

static legacy_s16 track_setup_jump(struct TRACK_SETUP_WALK *walk)
{
	if (walk->runway_length < TRACK_RUNWAY_MINIMUM_LENGTH) {
		return track_setup_error(walk->branches, TRACK_SETUP_NO_RUNWAY, walk->column, walk->row);
	}
	walk->runway_length = LEGACY_U8_WRAP_ADD(walk->runway_length, 1U);
	walk->jump_length = LEGACY_U8_WRAP_ADD(walk->jump_length, 1U);
	if (walk->orientation == TRACK_ORIENTATION_NORTH) {
		walk->column = walk->previous_column;
		walk->row = track_setup_add_s8(walk->previous_row, -(legacy_s16)walk->jump_length - 1);
	} else if (walk->orientation == TRACK_ORIENTATION_EAST) {
		walk->row = walk->previous_row;
		walk->column = track_setup_add_s8(walk->previous_column, (legacy_s16)walk->jump_length + 1);
	} else if (walk->orientation == TRACK_ORIENTATION_SOUTH) {
		walk->column = walk->previous_column;
		walk->row = track_setup_add_s8(walk->previous_row, (legacy_s16)walk->jump_length + 1);
	} else if (walk->orientation == TRACK_ORIENTATION_WEST) {
		walk->row = walk->previous_row;
		walk->column =
			track_setup_add_s8(walk->previous_column, -(legacy_s16)walk->jump_length - 1);
	}
	return TRACK_SETUP_OK;
}

static legacy_s16 track_setup_backtrack(struct TRACK_SETUP_WALK *walk)
{
	walk->branch_count = LEGACY_U16_WRAP_SUB(walk->branch_count, 1U);
	struct TRACK_SETUP_BRANCH far *branch = &walk->branches[walk->branch_count];
	walk->column = branch->column;
	walk->row = branch->row;
	walk->tile_element = branch->tile_element;
	walk->subtype = branch->subtype;
	walk->connection_status = branch->connection_status;
	walk->previous_connection_code = branch->previous_connection_code;
	walk->previous_piece = branch->previous_piece;
	walk->runway_length = branch->runway_length;
	walk->previous_column = branch->previous_column;
	walk->previous_row = branch->previous_row;
	walk->previous_tile_element = branch->previous_tile_element;
	walk->previous_subtype = branch->previous_subtype;
	walk->previous_connection_status = branch->previous_connection_status;
	if (walk->jump_length > TRACK_JUMP_BACKTRACK_LENGTH_LIMIT) {
		return track_setup_error(walk->branches, TRACK_SETUP_LONG_JUMP, walk->column, walk->row);
	}
	return TRACK_SETUP_OK;
}

static void track_setup_place_roadside_sign(struct TRACK_SETUP_WALK *walk, legacy_u8 arrow_code)
{
	struct TRACKOBJECT *previous_track_object = &trkObjectList[walk->previous_tile_element];
	struct TRKOBJINFO *previous_info =
		&previous_track_object->ss_trkObjInfoPtr[walk->previous_subtype];
	legacy_u16 opponent_path_offset =
		(legacy_u16)((legacy_u8)previous_info->reverse_path_offset_low |
					 LEGACY_U16_SHL((legacy_u8)previous_info->reverse_path_offset_high, 8U));
	struct VECTOR *camera_vectors;
	if (walk->previous_connection_status == TRACK_TRAVERSAL_REVERSE && opponent_path_offset != 0) {
		camera_vectors = track_vector_from_legacy_offset(opponent_path_offset);
	} else {
		camera_vectors = previous_info->route_vectors;
	}
	legacy_u16 index = LEGACY_U16_WRAP_MUL((legacy_u8)previous_info->route_point_count,
										   TRACK_CAMERA_VECTOR_STRIDE);
	if (walk->previous_connection_status == TRACK_TRAVERSAL_REVERSE) {
		index = LEGACY_U16_WRAP_ADD(index, TRACK_CAMERA_VECTOR_REVERSE_OFFSET);
	} else {
		index = LEGACY_U16_WRAP_ADD(index, TRACK_CAMERA_VECTOR_FORWARD_OFFSET);
	}
	struct VECTOR camera_vector = camera_vectors[index];
	if (walk->connection_status == TRACK_TRAVERSAL_REVERSE) {
		arrow_code = roadside_sign_reverse_types[LEGACY_S8_FROM_BITS(arrow_code)];
	} else {
		arrow_code = roadside_sign_forward_types[LEGACY_S8_FROM_BITS(arrow_code)];
	}
	walk->orientation = (legacy_s16)previous_info->route_orientation;
	track_setup_rotate_vector(&camera_vector, walk->orientation);
	roadside_sign_headings[roadside_sign_count] =
		walk->previous_connection_status == TRACK_TRAVERSAL_REVERSE
			? (walk->orientation ^ TRACK_ORIENTATION_SOUTH)
			: walk->orientation;
	roadside_sign_shape_indices[roadside_sign_count] = arrow_code;
	if (track_terrain_map[terrainrows[walk->previous_row] + walk->previous_column] ==
		TRACK_TERRAIN_HILL) {
		camera_vector.y = LEGACY_S16_WRAP_ADD(camera_vector.y, TRACK_CAMERA_HILL_HEIGHT);
	}
	legacy_s16 camera_index = (legacy_s16)roadside_sign_count;
	roadside_sign_positions[camera_index].y = camera_vector.y;
	legacy_s16 base_position =
		track_object_base_z(previous_track_object, (legacy_u8)walk->previous_row);
	roadside_sign_positions[camera_index].z = LEGACY_S16_WRAP_ADD(camera_vector.z, base_position);
	base_position = track_object_base_x(previous_track_object, (legacy_u8)walk->previous_column);
	roadside_sign_positions[camera_index].x = LEGACY_S16_WRAP_ADD(camera_vector.x, base_position);
	roadside_sign_indices_by_tile[trackrows[walk->previous_row] + walk->previous_column] =
		roadside_sign_count;
	roadside_sign_count = LEGACY_U8_WRAP_ADD(roadside_sign_count, 1U);
}

static void track_setup_roadside_sign(struct TRACK_SETUP_WALK *walk,
									  struct TRKOBJINFO *current_info)
{
	legacy_u8 arrow_code = (legacy_u8)current_info->roadside_sign_type;
	if (arrow_code == TRACK_ARROW_NONE) {
		walk->runway_length = LEGACY_U8_WRAP_ADD(walk->runway_length, 1U);
	} else {
		if (arrow_code != LEGACY_U8_MAX && walk->runway_length > TRACK_CAMERA_RUNWAY_THRESHOLD &&
			roadside_sign_count != TRACK_CAMERA_RESERVED_INDEX) {
			track_setup_place_roadside_sign(walk, arrow_code);
		}
		walk->runway_length = 0;
	}
}

static legacy_s16 track_setup_append_piece(struct TRACK_SETUP_WALK *walk)
{
	walk->jump_length = 0;
	walk->visited_tiles[trackrows[walk->row] + walk->column] = 1;
	walk->subtype_by_piece[track_pieces_counter] = walk->subtype;
	walk->connection_by_piece[track_pieces_counter] = walk->connection_status;
	if (walk->previous_piece != TRACK_PREVIOUS_PIECE_NONE) {
		track_setup_link_piece(walk->previous_piece, track_pieces_counter);
	}
	walk->previous_piece = (legacy_s16)track_pieces_counter;
	track_route_columns[track_pieces_counter] = walk->column;
	track_route_rows[track_pieces_counter] = walk->row;
	track_route_traversal_flags[track_pieces_counter] = (legacy_u8)(LEGACY_U16_WRAP_ADD(
		LEGACY_U16_SHL((legacy_u8)walk->connection_status, TRACK_PIECE_TRAVERSAL_SHIFT),
		walk->subtype));
	track_route_element_ids[track_pieces_counter] = walk->tile_element;

	struct TRKOBJINFO *track_info = trkObjectList[walk->tile_element].ss_trkObjInfoPtr;
	struct TRKOBJINFO *current_info = &track_info[walk->subtype];
	track_setup_roadside_sign(walk, current_info);

	track_pieces_counter = LEGACY_S16_WRAP_ADD(track_pieces_counter, 1);
	if ((unsigned)track_pieces_counter == extended_track_route_capacity()) {
		return track_setup_error(walk->branches, TRACK_SETUP_MANY_ELEMENTS, walk->column,
								 walk->row);
	}
	current_info = &track_info[walk->subtype];
	if (walk->connection_status == TRACK_TRAVERSAL_REVERSE) {
		walk->tile_entry_point = (legacy_u8)current_info->si_entryPoint;
		walk->previous_connection_code = (legacy_u8)current_info->si_entryType;
	} else {
		walk->tile_entry_point = (legacy_u8)current_info->si_exitPoint;
		walk->previous_connection_code = (legacy_u8)current_info->si_exitType;
	}
	walk->previous_column = walk->column;
	walk->previous_row = walk->row;
	walk->previous_connection_status = walk->connection_status;
	walk->previous_subtype = walk->subtype;
	walk->previous_tile_element = walk->tile_element;

	if (walk->tile_entry_point >= TRACK_EXIT_POINT_FIRST &&
		walk->tile_entry_point <= TRACK_EXIT_POINT_LAST) {
		const struct TRACK_SETUP_STEP *step = &track_setup_steps[walk->tile_entry_point];

		walk->column = track_setup_add_s8(walk->column, step->column);
		walk->row = track_setup_add_s8(walk->row, step->row);
		walk->orientation = step->orientation;
	}
	return TRACK_SETUP_OK;
}

static legacy_s16 track_setup_outside_grid(const struct TRACK_SETUP_WALK *walk)
{
	return walk->column < 0 || walk->row < 0 || walk->column >= extended_track_width() ||
		   walk->row >= extended_track_height();
}

static legacy_s16 track_setup_walk(struct TRACK_SETUP_WALK *walk)
{
	legacy_s16 status;

	for (;;) {
		walk->match_count = 0;
		walk->backtrack_required = track_setup_outside_grid(walk);
		if (walk->backtrack_required == 0) {
			status = track_setup_resolve_piece(walk);
			if (status != TRACK_SETUP_OK) {
				return status;
			}
			if (walk->match_count == 0) {
				if (walk->previous_connection_code != TRACK_JUMP_CONNECTION_CODE ||
					walk->jump_length >= TRACK_JUMP_MAX_EMPTY_TILES) {
					walk->backtrack_required = 1;
				} else {
					status = track_setup_jump(walk);
					if (status != TRACK_SETUP_OK) {
						return status;
					}
					continue;
				}
			}
		}
		if (walk->backtrack_required != 0) {
			if (walk->branch_count == 0) {
				if (walk->path_closed == 0) {
					return track_setup_error(walk->branches, TRACK_SETUP_NO_PATH, walk->column,
											 walk->row);
				}
				return TRACK_SETUP_OK;
			}
			status = track_setup_backtrack(walk);
			if (status != TRACK_SETUP_OK) {
				return status;
			}
		}
		status = track_setup_append_piece(walk);
		if (status != TRACK_SETUP_OK) {
			return status;
		}
	}
}

static void track_setup_place_camera(struct TRACK_SETUP_WALK *walk, legacy_s16 sampled_piece,
									 legacy_s16 camera_index, legacy_s16 far *camera_height,
									 legacy_s16 far *reserved_camera_words)
{
	walk->tile_element = (legacy_u8)track_route_element_ids[sampled_piece];
	walk->subtype =
		(legacy_u8)track_route_traversal_flags[sampled_piece] & TRACK_PIECE_SUBTYPE_MASK;
	walk->connection_status =
		((legacy_u8)track_route_traversal_flags[sampled_piece] & TRACK_PIECE_REVERSE_FLAG) != 0
			? TRACK_TRAVERSAL_REVERSE
			: TRACK_TRAVERSAL_FORWARD;
	struct TRACKOBJECT *track_object = &trkObjectList[walk->tile_element];
	struct TRKOBJINFO *current_info = &track_object->ss_trkObjInfoPtr[walk->subtype];
	legacy_u16 opponent_path_offset =
		(legacy_u16)((legacy_u8)current_info->reverse_path_offset_low |
					 LEGACY_U16_SHL((legacy_u8)current_info->reverse_path_offset_high, 8U));
	struct VECTOR *camera_vectors;
	if (walk->connection_status == TRACK_TRAVERSAL_REVERSE && opponent_path_offset != 0) {
		camera_vectors = track_vector_from_legacy_offset(opponent_path_offset);
	} else {
		camera_vectors = current_info->route_vectors;
	}
	legacy_u16 index = LEGACY_U16_WRAP_MUL((legacy_u8)current_info->route_point_count, 2U);
	struct VECTOR camera_vector = camera_vectors[index];
	walk->orientation = (legacy_s16)current_info->route_orientation;
	track_setup_rotate_vector(&camera_vector, walk->orientation);
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		camera_height[camera_index] = extended_track_elevation(walk->column, walk->row, 512, 512);
	} else
#endif
		if (track_terrain_map[terrainrows[walk->row] + walk->column] == 6) {
		camera_height[camera_index] = TRACK_CAMERA_HILL_HEIGHT;
	} else {
		camera_height[camera_index] = 0;
	}
	reserved_camera_words[camera_index] = 0;
	trackside_camera_positions[camera_index].y =
		LEGACY_S16_WRAP_ADD(camera_height[camera_index], camera_vector.y);
	legacy_s16 base_position = track_object_base_z(track_object, (legacy_u8)walk->row);
	trackside_camera_positions[camera_index].z =
		LEGACY_S16_WRAP_ADD(base_position, camera_vector.z);
	base_position = track_object_base_x(track_object, (legacy_u8)walk->column);
	trackside_camera_positions[camera_index].x =
		LEGACY_S16_WRAP_ADD(base_position, camera_vector.x);
}

static void track_setup_cameras(struct TRACK_SETUP_WALK *walk)
{
	legacy_s16 far *camera_height = trackside_camera_ground_heights;
	legacy_s16 far *reserved_camera_words = reserved_trackside_camera_words;
	track_validation_column = (legacy_u8)start_finish_column;
	track_validation_row = (legacy_u8)start_finish_row;
	legacy_u16 camera_count = (legacy_u16)LEGACY_S16_DIV_OR_ZERO(track_pieces_counter, 3);
	if (camera_count > TRACK_CAMERA_COUNT_MAX) {
		camera_count = TRACK_CAMERA_COUNT_MAX;
	}
	trackside_camera_count = (legacy_u8)camera_count;
	for (legacy_u16 index = 0;
		 index < (unsigned)(extended_track_width() * extended_track_height() + 1); index++) {
		walk->subtype_by_piece[index] = 0;
	}
	legacy_s16 camera_index = 0;
	for (legacy_u16 sample_index = 0; sample_index < trackside_camera_count; sample_index++) {
		legacy_s16 sampled_piece = LEGACY_S16_FROM_BITS((legacy_u16)LEGACY_S32_DIV_OR_ZERO(
			LEGACY_S32_WRAP_MUL((legacy_s32)track_pieces_counter, (legacy_s32)sample_index),
			(legacy_s32)(legacy_u16)trackside_camera_count));
		walk->column = LEGACY_S8_FROM_BITS((legacy_u8)track_route_columns[sampled_piece]);
		walk->row = LEGACY_S8_FROM_BITS((legacy_u8)track_route_rows[sampled_piece]);
		legacy_s16 tile_index = terrainrows[walk->row] + walk->column;
		if (walk->subtype_by_piece[tile_index] != 0) {
			continue;
		}
		walk->subtype_by_piece[tile_index] = 1;
		track_setup_place_camera(walk, sampled_piece, camera_index, camera_height,
								 reserved_camera_words);
		camera_index = LEGACY_S16_WRAP_ADD(camera_index, 1);
	}
	trackside_camera_count = (legacy_u8)camera_index;
}

legacy_s16 track_setup(void)
{
	struct TRACK_SETUP_WALK walk = {0};
	walk.branches = (struct TRACK_SETUP_BRANCH far *)mmgr_alloc_resbytes(
		(const legacy_s8 *)"tcomp", (extended_track_active() ? TRACK_SETUP_BRANCH_COUNT : 64U) *
										sizeof(struct TRACK_SETUP_BRANCH));
	if (walk.branches == 0) {
		return TRACK_SETUP_INTERNAL_ERROR;
	}

	walk.jump_length = 0;
	track_pieces_counter = 0;
	for (legacy_u16 index = 0;
		 index < (unsigned)(extended_track_width() * extended_track_height() + 1); index++) {
		roadside_sign_indices_by_tile[index] = TRACK_TILE_INDEX_UNASSIGNED;
	}

	legacy_s16 seam_status = track_setup_terrain_seams(walk.branches, terrConnDataEtoW,
													   terrConnDataWtoE, TRACK_SEAM_SCAN_ROWS);
	if (seam_status != TRACK_SETUP_OK) {
		return seam_status;
	}
	seam_status = track_setup_terrain_seams(walk.branches, terrConnDataNtoS, terrConnDataStoN,
											TRACK_SEAM_SCAN_COLUMNS);
	if (seam_status != TRACK_SETUP_OK) {
		return seam_status;
	}

	seam_status = track_setup_find_start(&walk);
	if (seam_status != TRACK_SETUP_OK) {
		return seam_status;
	}
	track_pieces_counter = 0;
	walk.branch_count = 0;
	roadside_sign_count = 0;
	trackside_camera_count = 0;
	walk.runway_length = 0;
	walk.path_closed = 0;
	for (legacy_u16 index = 0;
		 index < (unsigned)(extended_track_width() * extended_track_height() + 1); index++) {
		walk.visited_tiles[index] = 0;
	}
	for (legacy_u16 index = 0; index < extended_track_route_capacity(); index++) {
		track_primary_route_links[index] = TRACK_ROUTE_LINK_NONE;
		track_alternate_route_links[index] = TRACK_ROUTE_LINK_NONE;
	}

	walk.column = LEGACY_S8_FROM_BITS((legacy_u8)start_finish_column);
	walk.row = LEGACY_S8_FROM_BITS((legacy_u8)start_finish_row);
	walk.orientation = (legacy_s16)track_angle;
	walk.previous_connection_code = 0;
	walk.previous_piece = TRACK_PREVIOUS_PIECE_NONE;
	/* Branch snapshots may precede the first piece, when no predecessor exists. */
	walk.previous_column = 0;
	walk.previous_row = 0;
	walk.previous_tile_element = 0;
	walk.previous_subtype = 0;
	walk.previous_connection_status = TRACK_TRAVERSAL_UNMATCHED;

	seam_status = track_setup_walk(&walk);
	if (seam_status != TRACK_SETUP_OK) {
		return seam_status;
	}
	track_setup_cameras(&walk);
	mmgr_release((legacy_s8 far *)walk.branches);
	return TRACK_SETUP_OK;
}

void init_plantrak(void)
{
	init_game_state(GAMESTATE_INIT_TIMING_ONLY);
	state.game_inputmode = GAME_INPUT_MODE_INTRO;
	planptr = &plan_memres;
	start_finish_column = 1;
	start_finish_row = PLAN_TRACK_START_ROW;

	track_route_element_ids[0] = 7;
	track_route_element_ids[1] = 6;
	track_route_element_ids[2] = 8;
	track_route_element_ids[3] = 9;
	track_route_element_ids[4] = 7;

	track_route_columns[0] = 1;
	track_route_columns[1] = 0;
	track_route_columns[2] = 0;
	track_route_columns[3] = 1;
	track_route_columns[4] = 1;

	track_route_rows[0] = start_finish_row;
	track_route_rows[1] = start_finish_row;
	track_route_rows[2] = LEGACY_U8_WRAP_ADD(start_finish_row, 1U);
	track_route_rows[3] = LEGACY_U8_WRAP_ADD(start_finish_row, 1U);
	track_route_rows[4] = start_finish_row;

	track_route_traversal_flags[0] = 0;
	track_route_traversal_flags[1] = 0;
	track_route_traversal_flags[2] = 0;
	track_route_traversal_flags[3] = 0;
	track_route_traversal_flags[4] = 0;

	legacy_u8 route_index;
	for (route_index = 0U; route_index < PLAN_TRACK_ROUTE_LENGTH; route_index++) {
		legacy_u16 route_table_offset =
			LEGACY_U16_WRAP_MUL(route_index, PLAN_TRACK_ROUTE_ENTRY_SIZE);
		LEGACY_WRITE_U16_LE((legacy_u8 far *)opponent_route_track_indices + route_table_offset,
							plan_track_route[route_index]);
	}

	oppnentSped[0] = PLAN_TRACK_OPPONENT_SPEED;
	legacy_s16 path_z =
		LEGACY_S16_WRAP_ADD(track_row_positions[PLAN_TRACK_PATH_INDEX], PLAN_TRACK_PATH_Z_OFFSET);
	init_carstate_from_simd(&state.opponentstate, &simd_opponent, TRANSMISSION_AUTOMATIC,
							(legacy_s32)PLAN_TRACK_OPPONENT_X, 0L,
							LEGACY_S32_SHL((legacy_s32)path_z, PLAN_TRACK_POSITION_SHIFT), 0);

	route_index = (legacy_u8)state.opponentstate.car_route_point_index;
	state.opponentstate.car_route_point_index = LEGACY_S8_WRAP_ADD(route_index, ROUTE_POINT_STEP);
	opponent_route_advance((legacy_s16)route_index);
}
