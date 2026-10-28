#include "externs.h"
#include "track_types.h"
#include "track_objects.h"
#include "opponent.h"
#include "residue.h"
#include "trackdata_layout.h"
#include "extended_track.h"

#define MULTI_TILE_ROW_FLAG 1U
#define MULTI_TILE_COLUMN_FLAG 2U
#define ROUTE_TRACK_INDEX_SIZE 2U

#define HILLROAD_TERRAIN_FIRST 7U
#define HILLROAD_TERRAIN_LAST 10U
#define HILLROAD_TERRAIN_COUNT 4
#define HILLROAD_PIECE_VARIANT_COUNT 6
#define HILLROAD_SHAPE_COUNT 4
#define HILLROAD_DIRECT_SHAPE_COUNT 3U
#define HILLROAD_ALTERNATE_SHAPE_INDEX 3U

#define DEFAULT_PLANE_NORMAL_Y 8192

/* Multi-tile origins can reach index 30. The original tables end there,
 * aliasing the CHHT pointer offset and elapsed replay time respectively.
 * Preserve those live words without depending on C global-variable layout. */
legacy_s16 track_row_position(legacy_u16 row)
{
	if (!extended_track_active() && row == TRACK_GRID_SIZE) {
		return LEGACY_S16_FROM_BITS(legacy_closed_hihat_offset);
	}
	return track_row_positions[row];
}

legacy_s16 track_column_position(legacy_u16 column)
{
	if (!extended_track_active() && column == TRACK_GRID_SIZE) {
		return LEGACY_S16_FROM_BITS(elapsed_time2);
	}
	return track_column_positions[column];
}

legacy_s16 track_object_base_x(const struct TRACKOBJECT *track_object, legacy_u8 column)
{
	if (((legacy_u8)track_object->ss_multiTileFlag & MULTI_TILE_COLUMN_FLAG) != 0) {
		return track_column_position(column + 1U);
	}
	return track_column_centers[column];
}

legacy_s16 track_object_base_z(const struct TRACKOBJECT *track_object, legacy_u8 row)
{
	if (((legacy_u8)track_object->ss_multiTileFlag & MULTI_TILE_ROW_FLAG) != 0) {
		return track_row_position(row);
	}
	return track_row_centers[row];
}

/* Hand the opponent its next route point: the route index table in
 * opponent_route_track_indices is looked up through the car's current entry, and the route
 * walker is given that track piece. */
void opponent_route_advance(legacy_s16 route_point)
{
	legacy_u16 route_table_offset =
		LEGACY_U16_WRAP_MUL(state.opponentstate.car_route_index, ROUTE_TRACK_INDEX_SIZE);
	legacy_s16 route_track_index = LEGACY_READ_S16_LE(
		(const legacy_u8 far *)opponent_route_track_indices + route_table_offset);
	get_track_route_point(route_track_index, &state.opponentstate.car_route_target, route_point,
						  &state.game_opponent_target_speed);
}

/* Hill terrain 7..10 replaces a flat road piece with the sloped variant that
 * matches the hill. Each terrain row lists the road pieces it accepts: the
 * first three map one-to-one, the last three are the alternative spellings of
 * one and the same piece. */
static const legacy_u8 hillroad_pieces[HILLROAD_TERRAIN_COUNT][HILLROAD_PIECE_VARIANT_COUNT] = {
	{4, 14, 24, 39, 59, 98},
	{5, 15, 25, 36, 56, 95},
	{4, 14, 24, 38, 58, 97},
	{5, 15, 25, 37, 57, 96}};

static const legacy_u8 hillroad_shapes[HILLROAD_TERRAIN_COUNT][HILLROAD_SHAPE_COUNT] = {
	{182, 186, 190, 194}, {183, 187, 191, 195}, {184, 188, 192, 196}, {185, 189, 193, 197}};

legacy_u8 subst_hillroad_track(legacy_u8 terrain, legacy_u8 track)
{
	if (terrain < HILLROAD_TERRAIN_FIRST || terrain > HILLROAD_TERRAIN_LAST) {
		return 0;
	}

	legacy_u16 row = terrain - HILLROAD_TERRAIN_FIRST;
	for (legacy_u16 piece = 0U; piece < HILLROAD_PIECE_VARIANT_COUNT; piece++) {
		if (hillroad_pieces[row][piece] == track) {
			return hillroad_shapes[row][piece < HILLROAD_DIRECT_SHAPE_COUNT
											? piece
											: HILLROAD_ALTERNATE_SHAPE_INDEX];
		}
	}

	return 0;
}

struct PLANE far plan_memres = {
	0, 0, {0, 0, 0}, {0, DEFAULT_PLANE_NORMAL_Y, 0}, {{0, 0, 0, 0, 0, 0, 0, 0, 0}}};
