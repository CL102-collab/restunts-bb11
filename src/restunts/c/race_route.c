#include "race_route.h"
#include "trackdata_layout.h"
#include "track_objects.h"
#include "externs.h"
#include "residue.h"
#include "crash_state.h"
#include "race_stats.h"
#include "extended_track.h"
#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
#include "extended_replay_runtime.h"
#endif

#define TRACK_GRID_LAST_COORDINATE 29
#ifdef RESTUNTS_SDL3
#define PENALTY_ROUTE_VISITED_CAPACITY (EXTENDED_TRACK_ROUTE_CAPACITY + 3U)
#define PENALTY_ROUTE_PENDING_CAPACITY EXTENDED_TRACK_ROUTE_CAPACITY
#else
#define PENALTY_ROUTE_VISITED_CAPACITY 904U
#define PENALTY_ROUTE_PENDING_CAPACITY 128U
#endif
#define PENALTY_ROUTE_SENTINEL (-1)
#define PENALTY_ROUTE_START_TRACK_INDEX 900U
#define PENALTY_ROUTE_START_TILE_INDEX 11999U
#define PENALTY_ROUTE_START_COLUMN_INDEX 1963U
#define PENALTY_ROUTE_DISTANCE_NONE 0
#define PENALTY_ROUTE_DISTANCE_STEP 1
#define PENALTY_ROUTE_PENDING_NONE 0U
#define PENALTY_ROUTE_PENDING_STEP 1U

enum PENALTY_ROUTE_SENTINEL_VISIT_STATE {
	PENALTY_ROUTE_SENTINEL_UNVISITED = 0,
	PENALTY_ROUTE_SENTINEL_VISITED = 1
};

enum PENALTY_ROUTE_TRACK_VISIT_STATE {
	PENALTY_ROUTE_TRACK_UNVISITED = 0,
	PENALTY_ROUTE_TRACK_VISITED = 1
};

#define PENALTY_ROUTE_INDEX_FIRST 0U
#define MULTI_TILE_FLAGS_NONE 0U
#define MULTI_TILE_ROW_FLAG 1U
#define MULTI_TILE_COLUMN_FLAG 2U
#define TRACK_START_FINISH_PIECE_INDEX 0

enum PENALTY_DETECTION_RESULT { PENALTY_NOT_DETECTED = 0, PENALTY_DETECTED = 1 };

#define ROUTE_POINT_ADVANCE_DISTANCE 275
#define ROUTE_ALIGNMENT_WRAP_LIMIT (ANGLE_FULL_TURN - ANGLE_EIGHTH_TURN)
#define ROUTE_GUIDANCE_DIRECTION_SHIFT 8U
#define ROUTE_DIRECTION_LEFT_SECTOR 1
#define ROUTE_DIRECTION_RIGHT_SECTOR 3
enum ROUTE_HEIGHT_REFERENCE {
	ROUTE_HEIGHT_REFERENCE_TRACK_LEVEL = 0,
	ROUTE_HEIGHT_REFERENCE_CAR_LEVEL = 1
};
#define PENALTY_ROUTE_DECISION_DISTANCE 3
#define PENALTY_ROUTE_CONFIRMATION_COUNT 3
#define PENALTY_SECONDS_PER_SKIPPED_ROUTE 3
#define PENALTY_DISPLAY_DURATION_SHIFT 2U

static legacy_s16 dual_way_switching_enabled = 1;

legacy_u16 dual_way_switching_is_enabled(void)
{
	return dual_way_switching_enabled != 0;
}

void configure_dual_way_switching(legacy_s16 argc, legacy_s8 *argv[])
{
	static const legacy_s8 off_option[] = "--dws:off";
	static const legacy_s8 on_option[] = "--dws:on";
	dual_way_switching_enabled = 1;
	for (legacy_s16 index = 1; index < argc; index++) {
		if (stricmp(argv[index], off_option) == 0) {
			dual_way_switching_enabled = 0;
		} else if (stricmp(argv[index], on_option) == 0) {
			dual_way_switching_enabled = 1;
		}
	}
}

static legacy_s16 penalty_route_next(legacy_s16 track_index)
{
	if (track_index == PENALTY_ROUTE_SENTINEL) {
		return legacy_execution_residue.penalty_route_word;
	}
	if (track_index < 0 || track_index >= track_pieces_counter) {
		return PENALTY_ROUTE_SENTINEL;
	}
	return track_primary_route_links[track_index];
}

static legacy_s16 penalty_route_alternate(legacy_s16 track_index)
{
	if (track_index == PENALTY_ROUTE_SENTINEL) {
		if (extended_track_active()) {
			return PENALTY_ROUTE_SENTINEL;
		}
		return track_primary_route_links[PENALTY_ROUTE_START_TRACK_INDEX];
	}
	return track_alternate_route_links[track_index];
}

static legacy_s16 finish_penalty_route(legacy_s16 *current_track, legacy_s16 *penalty_count,
									   legacy_s16 best_track, legacy_s16 best_distance,
									   legacy_s16 column, legacy_s16 row)
{
	if (best_distance != PENALTY_ROUTE_DISTANCE_NONE) {
		*current_track = best_track;
		*penalty_count = best_distance;
	} else {
		state.game_startcol = column;
		state.game_startcol2 = column;
		state.game_startrow = row;
		state.game_startrow2 = row;
		*penalty_count = PENALTY_ROUTE_OUTSIDE_TRACK;
	}
	return PENALTY_DETECTED;
}

struct PENALTY_ROUTE_SEARCH {
	legacy_u8 visited[PENALTY_ROUTE_VISITED_CAPACITY];
	legacy_s16 pending_track[PENALTY_ROUTE_PENDING_CAPACITY];
	legacy_s16 pending_distance[PENALTY_ROUTE_PENDING_CAPACITY];
	legacy_u16 pending_count;
	legacy_u8 sentinel_visited;
	legacy_s16 track_index;
	legacy_s16 distance;
	legacy_s16 best_track;
	legacy_s16 best_distance;
	legacy_s16 column;
	legacy_s16 row;
};

struct PENALTY_ROUTE_BOUNDS {
	legacy_s16 piece;
	legacy_u8 minimum_column;
	legacy_u8 maximum_column;
	legacy_u8 minimum_row;
	legacy_u8 maximum_row;
};

static legacy_s16 penalty_route_visit(struct PENALTY_ROUTE_SEARCH *search, legacy_s16 next_track)
{
	if (next_track == PENALTY_ROUTE_SENTINEL) {
		if (search->sentinel_visited != PENALTY_ROUTE_SENTINEL_UNVISITED) {
			return 0;
		}
		search->sentinel_visited = PENALTY_ROUTE_SENTINEL_VISITED;
	} else {
		if (next_track < 0 || next_track >= track_pieces_counter ||
			search->visited[next_track] != PENALTY_ROUTE_TRACK_UNVISITED) {
			return 0;
		}
		search->visited[next_track] = PENALTY_ROUTE_TRACK_VISITED;
	}
	return 1;
}

static void penalty_route_bounds(legacy_s16 next_track, struct PENALTY_ROUTE_BOUNDS *bounds)
{
	bounds->piece = next_track;
	legacy_u8 tile_element;
	if (extended_track_active() && next_track == PENALTY_ROUTE_SENTINEL) {
		bounds->minimum_row = bounds->maximum_row = 255U;
		bounds->minimum_column = bounds->maximum_column = 255U;
		return;
	}
	if (next_track == PENALTY_ROUTE_SENTINEL) {
		bounds->minimum_row = (legacy_u8)track_route_columns[PENALTY_ROUTE_START_TRACK_INDEX];
		tile_element = (legacy_u8)replay_input_buffer[PENALTY_ROUTE_START_TILE_INDEX];
	} else {
		bounds->minimum_row = (legacy_u8)track_route_rows[next_track];
		tile_element = (legacy_u8)track_route_element_ids[next_track];
	}
	legacy_u8 multi_tile_flags = trkObjectList[tile_element].ss_multiTileFlag;
	bounds->maximum_row = bounds->minimum_row;
	if ((multi_tile_flags & MULTI_TILE_ROW_FLAG) != MULTI_TILE_FLAGS_NONE) {
		bounds->maximum_row = LEGACY_U8_WRAP_ADD(bounds->maximum_row, TRACK_TILE_COORDINATE_STEP);
	}
	if (next_track == PENALTY_ROUTE_SENTINEL) {
		bounds->minimum_column =
			(legacy_u8)track_and_directory_backup[PENALTY_ROUTE_START_COLUMN_INDEX];
	} else {
		bounds->minimum_column = (legacy_u8)track_route_columns[next_track];
	}
	bounds->maximum_column = bounds->minimum_column;
	if ((multi_tile_flags & MULTI_TILE_COLUMN_FLAG) != MULTI_TILE_FLAGS_NONE) {
		bounds->maximum_column =
			LEGACY_U8_WRAP_ADD(bounds->maximum_column, TRACK_TILE_COORDINATE_STEP);
	}
}

static legacy_s16 penalty_route_contains(const struct PENALTY_ROUTE_SEARCH *search,
										 const struct PENALTY_ROUTE_BOUNDS *bounds)
{
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		if (bounds->piece < 0 || bounds->piece >= track_pieces_counter) {
			return 0;
		}
		const struct CSTRK_OBJECT *object = extended_track_object_at(search->column, search->row);
		return object && object->x == bounds->minimum_column && object->y == bounds->minimum_row &&
			   extended_track_object_id(object) ==
				   (legacy_u8)track_route_element_ids[bounds->piece];
	}
#endif
	return ((legacy_u8)search->column == bounds->minimum_column ||
			(legacy_u8)search->column == bounds->maximum_column) &&
		   ((legacy_u8)search->row == bounds->minimum_row ||
			(legacy_u8)search->row == bounds->maximum_row);
}

static legacy_s16 penalty_route_record_match(struct PENALTY_ROUTE_SEARCH *search,
											 legacy_s16 *next_track, legacy_s16 *current_track,
											 legacy_s16 *penalty_count)
{
	struct PENALTY_ROUTE_BOUNDS bounds;

	penalty_route_bounds(*next_track, &bounds);
	if (!penalty_route_contains(search, &bounds)) {
		return 0;
	}
	if (penalty_route_alternate(search->track_index) != PENALTY_ROUTE_SENTINEL) {
		*next_track = search->track_index;
	}
	state.game_startcol = LEGACY_S8_FROM_BITS(bounds.minimum_column);
	state.game_startcol2 = LEGACY_S8_FROM_BITS(bounds.maximum_column);
	state.game_startrow = LEGACY_S8_FROM_BITS(bounds.minimum_row);
	state.game_startrow2 = LEGACY_S8_FROM_BITS(bounds.maximum_row);
	if (search->distance <= PENALTY_ROUTE_DISTANCE_NONE) {
		*current_track = *next_track;
		*penalty_count = search->distance;
		return 1;
	}
	if (search->best_distance == PENALTY_ROUTE_DISTANCE_NONE ||
		search->best_distance > search->distance) {
		search->best_track = *next_track;
		search->best_distance = search->distance;
	}
	return 0;
}

/* A tile behind the confirmed route is still needed by the wrong-way detector.
 * It must lead back to that route without passing the finish line: a sibling
 * branch only becomes reachable by wrapping around the lap, which is the
 * original dual-way switching bug. Reuse the search scratch storage. */
static legacy_s16 penalty_route_reaches(struct PENALTY_ROUTE_SEARCH *search, legacy_s16 first_track,
										legacy_s16 target_track)
{
	for (legacy_u16 index = 0; index < (legacy_u16)track_pieces_counter; index++) {
		search->visited[index] = PENALTY_ROUTE_TRACK_UNVISITED;
	}
	legacy_u16 pending_count = 0;
	legacy_s16 track_index = first_track;
	for (;;) {
		if (track_index == target_track) {
			return 1;
		}
		if (track_index > TRACK_START_FINISH_PIECE_INDEX && track_index < track_pieces_counter &&
			search->visited[track_index] == PENALTY_ROUTE_TRACK_UNVISITED) {
			search->visited[track_index] = PENALTY_ROUTE_TRACK_VISITED;
			legacy_s16 alternate_track = track_alternate_route_links[track_index];
			if (alternate_track > TRACK_START_FINISH_PIECE_INDEX &&
				alternate_track < track_pieces_counter &&
				pending_count < PENALTY_ROUTE_PENDING_CAPACITY) {
				search->pending_track[pending_count++] = alternate_track;
			}
			track_index = track_primary_route_links[track_index];
			continue;
		}
		if (pending_count == 0) {
			return 0;
		}
		track_index = search->pending_track[--pending_count];
	}
}

static legacy_s16 match_previous_penalty_route(struct PENALTY_ROUTE_SEARCH *search,
											   legacy_s16 *current_track, legacy_s16 *penalty_count)
{
	if (*current_track < 0 || *current_track >= track_pieces_counter) {
		return 0;
	}
	for (legacy_s16 index = 1; index < track_pieces_counter; index++) {
		struct PENALTY_ROUTE_BOUNDS bounds;
		penalty_route_bounds(index, &bounds);
		if (!penalty_route_contains(search, &bounds) ||
			!penalty_route_reaches(search, index, *current_track)) {
			continue;
		}
		state.game_startcol = LEGACY_S8_FROM_BITS(bounds.minimum_column);
		state.game_startcol2 = LEGACY_S8_FROM_BITS(bounds.maximum_column);
		state.game_startrow = LEGACY_S8_FROM_BITS(bounds.minimum_row);
		state.game_startrow2 = LEGACY_S8_FROM_BITS(bounds.maximum_row);
		*current_track = index;
		*penalty_count = PENALTY_ROUTE_FINISH_REACHED;
		return 1;
	}
	return 0;
}

static legacy_s16 search_penalty_route(struct PENALTY_ROUTE_SEARCH *search,
									   legacy_s16 *current_track, legacy_s16 *penalty_count)
{
	search->best_distance = PENALTY_ROUTE_DISTANCE_NONE;
	search->best_track = TRACK_START_FINISH_PIECE_INDEX;
	search->pending_count = PENALTY_ROUTE_PENDING_NONE;
	search->distance = PENALTY_ROUTE_DISTANCE_NONE;
	search->sentinel_visited = PENALTY_ROUTE_SENTINEL_UNVISITED;
	for (legacy_u16 index = PENALTY_ROUTE_INDEX_FIRST; index < (legacy_u16)track_pieces_counter;
		 index++) {
		search->visited[index] = PENALTY_ROUTE_TRACK_UNVISITED;
	}
	search->track_index = (legacy_s16)*current_track;
	legacy_s16 next_track;
	for (;;) {
		next_track = penalty_route_next(search->track_index);
		if ((dual_way_switching_enabled == 0 && (search->distance == PENALTY_ROUTE_FINISH_REACHED ||
												 search->track_index == PENALTY_ROUTE_SENTINEL)) ||
			!penalty_route_visit(search, next_track)) {
			if (search->pending_count == PENALTY_ROUTE_PENDING_NONE) {
				if (dual_way_switching_enabled == 0 &&
					search->best_distance == PENALTY_ROUTE_DISTANCE_NONE &&
					match_previous_penalty_route(search, current_track, penalty_count)) {
					return PENALTY_DETECTED;
				}
				return finish_penalty_route(current_track, penalty_count, search->best_track,
											search->best_distance, search->column, search->row);
			}
			search->pending_count =
				LEGACY_U16_WRAP_SUB(search->pending_count, PENALTY_ROUTE_PENDING_STEP);
			search->track_index = search->pending_track[search->pending_count];
			search->distance = search->pending_distance[search->pending_count];
			continue;
		}
		if ((dual_way_switching_enabled != 0 || next_track != PENALTY_ROUTE_SENTINEL) &&
			penalty_route_record_match(search, &next_track, current_track, penalty_count)) {
			return PENALTY_DETECTED;
		}
		legacy_s16 alternate_track = penalty_route_alternate(search->track_index);
		if (alternate_track != PENALTY_ROUTE_SENTINEL) {
			search->pending_distance[search->pending_count] = search->distance;
			search->pending_track[search->pending_count] = alternate_track;
			search->pending_count =
				LEGACY_U16_WRAP_ADD(search->pending_count, PENALTY_ROUTE_PENDING_STEP);
		}
		if (next_track == TRACK_START_FINISH_PIECE_INDEX) {
			search->distance = PENALTY_ROUTE_FINISH_REACHED;
		} else if (search->distance != PENALTY_ROUTE_FINISH_REACHED) {
			search->distance = LEGACY_S16_WRAP_ADD(search->distance, PENALTY_ROUTE_DISTANCE_STEP);
		}
		search->track_index = next_track;
	}
}

legacy_s16 detect_penalty(legacy_s16 *current_track, legacy_s16 *penalty_count)
{
	struct PENALTY_ROUTE_SEARCH search;

	search.column = LEGACY_S8_FROM_BITS(
		(legacy_u8)((legacy_u32)state.playerstate.car_position.lx >> TRACK_WORLD_TILE_SHIFT));
	search.row = LEGACY_S8_FROM_BITS(LEGACY_U8_WRAP_SUB(
		(legacy_u8)(extended_track_height() - 1),
		(legacy_u8)((legacy_u32)state.playerstate.car_position.lz >> TRACK_WORLD_TILE_SHIFT)));
	if ((search.column == state.game_startcol || search.column == state.game_startcol2) &&
		(search.row == state.game_startrow || search.row == state.game_startrow2)) {
		struct PENALTY_ROUTE_BOUNDS bounds;
		if (extended_track_active()) {
			penalty_route_bounds(*current_track, &bounds);
		}
		if (!extended_track_active() || penalty_route_contains(&search, &bounds)) {
			*penalty_count = PENALTY_ROUTE_DISTANCE_NONE;
			return PENALTY_NOT_DETECTED;
		}
	}
	if (search.column < 0 || search.column >= extended_track_width() || search.row < 0 ||
		search.row >= extended_track_height()) {
		*penalty_count = PENALTY_ROUTE_OUTSIDE_TRACK;
		return PENALTY_DETECTED;
	}

	return search_penalty_route(&search, current_track, penalty_count);
}

void update_penalty_display(void)
{
	if (show_penalty_counter != 0) {
		show_penalty_counter = LEGACY_S8_WRAP_SUB(show_penalty_counter, 1);
	}
}

/* Vector from the player's car to its current route point. A y of -1 marks
 * a route point with no height of its own: the route search still measures
 * the drop to the car, the steering hint treats the point as level. */
static void route_point_delta(struct VECTOR *delta, legacy_s16 unspecified_height_reference)
{
	*delta = state.playerstate.car_route_target;
	delta->x = LEGACY_S16_WRAP_SUB(delta->x, position_to_word(state.playerstate.car_position.lx));
	if (delta->y == ROUTE_POINT_HEIGHT_UNSPECIFIED) {
		delta->y =
			unspecified_height_reference == ROUTE_HEIGHT_REFERENCE_CAR_LEVEL
				? 0
				: LEGACY_S16_WRAP_NEGATE(position_to_word(state.playerstate.car_position.ly));
	} else {
		delta->y =
			LEGACY_S16_WRAP_SUB(delta->y, position_to_word(state.playerstate.car_position.ly));
	}
	delta->z = LEGACY_S16_WRAP_SUB(delta->z, position_to_word(state.playerstate.car_position.lz));
}

static legacy_u8 confirm_player_route(legacy_s16 route_index, legacy_s16 skipped_route_count)
{
	legacy_u8 commit_penalty = 0;

	if (route_index == 0 && state.game_player_previous_route != 0) {
		state.playerstate.car_lap_count = LEGACY_S8_WRAP_ADD(state.playerstate.car_lap_count, 1);
		commit_penalty = 1;
	} else if (skipped_route_count >= 0 && skipped_route_count < PENALTY_ROUTE_DECISION_DISTANCE) {
		state.game_route_confirmation_count = ROUTE_CONFIRMATION_NONE;
		state.game_player_confirmed_route = route_index;
	} else if (skipped_route_count == PENALTY_ROUTE_FINISH_REACHED ||
			   skipped_route_count > PENALTY_ROUTE_DECISION_DISTANCE) {
		if (track_primary_route_links[state.game_player_previous_route] == route_index ||
			track_alternate_route_links[state.game_player_previous_route] == route_index) {
			state.game_route_confirmation_count =
				LEGACY_S8_WRAP_ADD(state.game_route_confirmation_count, ROUTE_CONFIRMATION_STEP);
		} else {
			if (track_primary_route_links[route_index] == state.game_player_previous_route ||
				track_alternate_route_links[route_index] == state.game_player_previous_route) {
				state.game_player_route_status = ROUTE_TRACKING_WRONG_WAY;
			}
			state.game_route_confirmation_count = ROUTE_CONFIRMATION_INITIAL;
		}
		if (state.game_route_confirmation_count >= PENALTY_ROUTE_CONFIRMATION_COUNT) {
			commit_penalty = 1;
		}
	}
	return commit_penalty;
}

void update_player_route_penalty(void)
{
	legacy_s16 route_index = state.game_player_confirmed_route;
	legacy_s16 skipped_route_count;
	legacy_s16 route_distance = detect_penalty(&route_index, &skipped_route_count);
	if (route_distance != 0) {
		legacy_u8 commit_penalty = 0;
		if (skipped_route_count == PENALTY_ROUTE_OUTSIDE_TRACK) {
			state.game_player_route_status = ROUTE_TRACKING_OUTSIDE_TRACK;
			state.game_route_confirmation_count = ROUTE_CONFIRMATION_NONE;
		} else {
			if (state.game_player_route_status == ROUTE_TRACKING_OUTSIDE_TRACK) {
				state.game_player_route_status = ROUTE_TRACKING_NORMAL;
				state.game_route_confirmation_count = ROUTE_CONFIRMATION_NONE;
			}
			if (state.game_player_route_status == ROUTE_TRACKING_NORMAL) {
				commit_penalty = confirm_player_route(route_index, skipped_route_count);
			}
		}
		if (commit_penalty != 0) {
			state.game_player_confirmed_route = route_index;
			state.game_route_confirmation_count = ROUTE_CONFIRMATION_NONE;
			if (skipped_route_count > 0) {
				penalty_time =
					LEGACY_S16_WRAP_MUL(LEGACY_S16_WRAP_MUL(skipped_route_count, framespersec),
										PENALTY_SECONDS_PER_SKIPPED_ROUTE);
				show_penalty_counter = LEGACY_S8_FROM_BITS(
					(legacy_u8)LEGACY_U16_SHL(framespersec, PENALTY_DISPLAY_DURATION_SHIFT));
				state.game_penalty = LEGACY_S16_WRAP_ADD(state.game_penalty, penalty_time);
#if defined(RESTUNTS_SDL3) && (!defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY))
				if (extended_replay_is_active()) {
					uint32_t added = (uint32_t)skipped_route_count * framespersec *
									 PENALTY_SECONDS_PER_SKIPPED_ROUTE;
					if (UINT32_MAX - extended_replay_clocks.penalty < added) {
						extended_replay_clocks.penalty = UINT32_MAX;
					} else {
						extended_replay_clocks.penalty += added;
					}
				}
#endif
			}
		}
		state.game_player_previous_route = route_index;
	}
}

static legacy_s16 player_route_depth(legacy_s8 previous_route_status,
									 struct MATRIX *world_to_car_rotation)
{
	struct VECTOR local_route_delta;
	legacy_s16 route_depth = 0;
	struct VECTOR world_route_delta;
	if (state.playerstate.car_route_index != ROUTE_INDEX_NONE) {
		if ((previous_route_status == ROUTE_TRACKING_NORMAL ||
			 state.game_player_route_status != ROUTE_TRACKING_NORMAL) &&
			(state.playerstate.car_route_index == state.game_player_confirmed_route ||
			 track_primary_route_links[state.game_player_confirmed_route] ==
				 state.playerstate.car_route_index ||
			 track_alternate_route_links[state.game_player_confirmed_route] ==
				 state.playerstate.car_route_index)) {
			route_point_delta(&world_route_delta, ROUTE_HEIGHT_REFERENCE_CAR_LEVEL);
			mat_mul_vector(&world_route_delta, world_to_car_rotation, &local_route_delta);
			route_depth = local_route_delta.z;
		} else {
			state.playerstate.car_route_index = ROUTE_INDEX_NONE;
		}
	}
	return route_depth;
}

static legacy_s8 select_player_route_point(legacy_s16 route_index,
										   struct MATRIX *world_to_car_rotation)
{
	legacy_s8 candidate_route_point = ROUTE_POINT_FIRST;
	legacy_s8 selected_route_point;
	legacy_s8 route_end_reached = 0;
	struct VECTOR world_route_delta;
	struct VECTOR local_route_delta;
	struct VECTOR route_delta_or_best_depth;
	do {
		route_end_reached = LEGACY_S8_FROM_BITS(
			(legacy_u8)get_track_route_point(route_index, &state.playerstate.car_route_target,
											 (legacy_s16)(legacy_u8)candidate_route_point, 0));
		route_point_delta(&world_route_delta, ROUTE_HEIGHT_REFERENCE_TRACK_LEVEL);
		mat_mul_vector(&world_route_delta, world_to_car_rotation, &local_route_delta);
		if (candidate_route_point == ROUTE_POINT_FIRST ||
			(local_route_delta.z < route_delta_or_best_depth.z && local_route_delta.z > 0)) {
			selected_route_point = candidate_route_point;
			route_delta_or_best_depth.z = local_route_delta.z;
		}
		candidate_route_point = LEGACY_S8_WRAP_ADD(candidate_route_point, ROUTE_POINT_STEP);
	} while (route_end_reached == 0);

	return selected_route_point;
}

static void recover_player_route_direction(legacy_s16 route_index, legacy_s8 selected_route_point)
{
	struct VECTOR route_segment_start[ROUTE_GEOMETRY_POINT_COUNT];
	struct VECTOR route_segment_end[ROUTE_GEOMETRY_POINT_COUNT];
	if (selected_route_point == ROUTE_POINT_FIRST) {
		get_track_route_point(route_index, route_segment_start, ROUTE_POINT_FIRST, 0);
		get_track_route_point(route_index, route_segment_end, ROUTE_POINT_SECOND, 0);
	} else {
		get_track_route_point(
			route_index, route_segment_start,
			(legacy_s16)LEGACY_S8_WRAP_SUB(selected_route_point, ROUTE_POINT_STEP), 0);
		get_track_route_point(route_index, route_segment_end,
							  (legacy_s16)(legacy_u8)selected_route_point, 0);
	}
	legacy_s16 route_direction = LEGACY_S16_FROM_BITS(
		(legacy_u16)LEGACY_S16_WRAP_SUB(
			state.playerstate.car_rotate.x,
			polarAngle(LEGACY_S16_WRAP_SUB(route_segment_start[0].x, route_segment_end[0].x),
					   LEGACY_S16_WRAP_SUB(route_segment_end[0].z, route_segment_start[0].z))) &
		ANGLE_MASK);
	if (route_direction > ROUTE_ALIGNMENT_WRAP_LIMIT || route_direction < ANGLE_EIGHTH_TURN) {
		state.game_player_route_status = ROUTE_TRACKING_NORMAL;
		state.game_route_confirmation_count = ROUTE_CONFIRMATION_INITIAL;
		state.playerstate.car_route_index = route_index;
		state.playerstate.car_route_point_index = selected_route_point;
	}
}

static void advance_player_route_point(void)
{
	legacy_u8 route_point = (legacy_u8)state.playerstate.car_route_point_index;
	state.playerstate.car_route_point_index = LEGACY_S8_WRAP_ADD(route_point, ROUTE_POINT_STEP);
	if (get_track_route_point(state.playerstate.car_route_index,
							  &state.playerstate.car_route_target, (legacy_s16)route_point,
							  0) != 0) {
		if (track_alternate_route_links[state.game_player_confirmed_route] !=
			TRACK_ROUTE_LINK_NONE) {
			state.playerstate.car_route_index = ROUTE_INDEX_NONE;
		} else {
			state.playerstate.car_route_index =
				track_primary_route_links[state.game_player_confirmed_route];
		}
		state.playerstate.car_route_point_index = ROUTE_POINT_FIRST;
	}
}

static void update_player_route_indicator(void)
{
	struct MATRIX *world_to_car_rotation;
	struct VECTOR world_route_delta;
	struct VECTOR local_route_delta;
	if (state.playerstate.car_route_index != ROUTE_INDEX_NONE &&
		state.game_player_route_status == ROUTE_TRACKING_NORMAL) {
		route_point_delta(&world_route_delta, ROUTE_HEIGHT_REFERENCE_CAR_LEVEL);
		world_to_car_rotation =
			mat_rot_zxy(state.playerstate.car_rotate.z, state.playerstate.car_rotate.y,
						state.playerstate.car_rotate.x, MATRIX_ROTATION_ORDER_YXZ);
		mat_mul_vector(&world_route_delta, world_to_car_rotation, &local_route_delta);
		state.playerstate.car_route_heading_error =
			LEGACY_S16_FROM_BITS((legacy_u16)polarAngle(LEGACY_S16_WRAP_NEGATE(local_route_delta.x),
														local_route_delta.z) &
								 ANGLE_MASK);
		if (state.playerstate.car_crashBmpFlag == CRASH_EVENT_NONE) {
			legacy_s16 route_direction = LEGACY_U16_SAR(
				LEGACY_U16_WRAP_ADD(state.playerstate.car_route_heading_error, ANGLE_EIGHTH_TURN) &
					ANGLE_MASK,
				ROUTE_GUIDANCE_DIRECTION_SHIFT);
			if (route_direction == ROUTE_DIRECTION_LEFT_SECTOR) {
				state.game_player_route_indicator = ROUTE_INDICATOR_LEFT;
			} else if (route_direction == ROUTE_DIRECTION_RIGHT_SECTOR &&
					   state.playerstate.car_route_has_reverse_path == 0) {
				state.game_player_route_indicator = ROUTE_INDICATOR_RIGHT;
			} else {
				state.game_player_route_indicator = ROUTE_INDICATOR_NONE;
			}
		}
	}
}

static void check_player_finish(void)
{
	if (state.playerstate.car_lap_count != 0) {
		legacy_s16 route_distance = multiply_and_scale(
			cos_fast(track_angle),
			LEGACY_S16_WRAP_SUB(track_row_centers[start_finish_row],
								position_to_word(state.playerstate.car_position.lz)));
		route_distance = LEGACY_S16_WRAP_ADD(
			route_distance,
			multiply_and_scale(
				sin_fast(track_angle),
				LEGACY_S16_WRAP_SUB(track_column_centers[start_finish_column],
									position_to_word(state.playerstate.car_position.lx))));
		if (route_distance < 0) {
			update_crash_state(CRASH_EVENT_FINISH, PLAYER_CAR_INDEX);
		}
	}
}

void update_player_route_guidance(legacy_s8 previous_route_status)
{
	state.game_player_route_indicator = ROUTE_INDICATOR_NONE;
	legacy_s16 route_index;
	struct MATRIX *world_to_car_rotation;
	if (state.game_player_route_status != ROUTE_TRACKING_OUTSIDE_TRACK) {
		world_to_car_rotation =
			mat_rot_zxy(state.playerstate.car_rotate.z, state.playerstate.car_rotate.y,
						state.playerstate.car_rotate.x, MATRIX_ROTATION_ORDER_YXZ);

		legacy_u8 route_selection_required = 0;
		legacy_u8 route_advance_required = 0;
		if (state.game_player_route_status == ROUTE_TRACKING_WRONG_WAY) {
			if (state.playerstate.car_crashBmpFlag == CRASH_EVENT_NONE) {
				state.game_player_route_indicator = ROUTE_INDICATOR_WRONG_WAY;
			}
			route_index = state.game_player_previous_route;
			route_selection_required = 1;
		} else {
			legacy_s16 route_distance_or_direction =
				player_route_depth(previous_route_status, world_to_car_rotation);
			if (route_distance_or_direction < ROUTE_POINT_ADVANCE_DISTANCE) {
				if (state.playerstate.car_route_index == ROUTE_INDEX_NONE) {
					route_index = state.game_player_confirmed_route;
					route_selection_required = 1;
				} else {
					route_advance_required = 1;
				}
			}
		}

		legacy_u8 guidance_required = 1;
		if (route_selection_required != 0) {
			if (track_alternate_route_links[route_index] != TRACK_ROUTE_LINK_NONE) {
				guidance_required = 0;
			} else {
				legacy_s8 selected_route_point =
					select_player_route_point(route_index, world_to_car_rotation);
				if (state.game_player_route_status == ROUTE_TRACKING_WRONG_WAY) {
					recover_player_route_direction(route_index, selected_route_point);
				} else {
					state.playerstate.car_route_index = state.game_player_confirmed_route;
					state.playerstate.car_route_point_index = selected_route_point;
				}
				route_advance_required = 1;
			}
		}

		if (route_advance_required != 0) {
			advance_player_route_point();
		}

		if (guidance_required != 0) {
			update_player_route_indicator();
		}

		check_player_finish();
	}
}
