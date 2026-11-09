#include <math.h>
#include <string.h>

#include "extended_checkpoints.h"
#include "../c/externs.h"
#include "../c/extended/extended_track.h"
#include "../c/extended/extended_checkpoint.h"
#include "../c/extended/extended_owoot_internal.h"
#include "../c/track/track_objects.h"
#include "../c/track/trackdata_layout.h"

#undef memset

#define GENGHOST_CHECKPOINT_BITS LEGACY_BYTE_BITS
#define GENGHOST_CHECKPOINT_VISITED_BYTES                                                          \
	((CSTRK_MAX_OBJECTS + GENGHOST_CHECKPOINT_BITS - 1U) / GENGHOST_CHECKPOINT_BITS)
#define GENGHOST_CHECKPOINT_COLOR_COUNT 2U
#define GENGHOST_CHECKPOINT_RUNOUT (TRACK_TILE_SIZE / 4.0)

extern legacy_f64 sqrt(legacy_f64);

static legacy_u8 object_color(const struct CSTRK_OBJECT *object)
{
	if (!object) {
		return 0;
	}
	return object->legacy_id == CSTRK_BLUE_CHECKPOINT_ID  ? GENGHOST_CHECKPOINT_BLUE
		   : object->legacy_id == CSTRK_RED_CHECKPOINT_ID ? GENGHOST_CHECKPOINT_RED
														  : 0;
}

static const struct CSTRK_OBJECT *checkpoint_object(legacy_int piece)
{
	if (!extended_track_active()) {
		return NULL;
	}
	return extended_track_object_for_id((legacy_u8)track_route_columns[piece],
										(legacy_u8)track_route_rows[piece],
										(legacy_u8)track_route_element_ids[piece]);
}

legacy_u8 extended_genghost_checkpoint_piece_color(legacy_int piece)
{
	return object_color(checkpoint_object(piece));
}

legacy_u8 extended_genghost_checkpoint_colors(const legacy_int *path, size_t count)
{
	const struct CSTRK_TRACK *track = extended_track_model();
	if (!extended_track_active() || !track) {
		return GENGHOST_CHECKPOINT_NONE;
	}
	legacy_u16 total[GENGHOST_CHECKPOINT_COLOR_COUNT] = {0};
	legacy_u16 crossed[GENGHOST_CHECKPOINT_COLOR_COUNT] = {0};
	legacy_u8 visited[GENGHOST_CHECKPOINT_VISITED_BYTES] = {0};
	for (legacy_u32 index = 0; index < track->object_count; index++) {
		legacy_u8 color = object_color(&track->objects[index]);
		if (color) {
			total[color == GENGHOST_CHECKPOINT_RED]++;
		}
	}
	if (!total[0] && !total[1]) {
		return GENGHOST_CHECKPOINT_NONE;
	}
	for (size_t index = 0; index < count; index++) {
		const struct CSTRK_OBJECT *object = checkpoint_object(path[index]);
		legacy_u8 color = object_color(object);
		if (!color) {
			continue;
		}
		size_t placed = (size_t)(object - track->objects);
		legacy_u8 bit = 1U << (placed % GENGHOST_CHECKPOINT_BITS);
		if (!(visited[placed / GENGHOST_CHECKPOINT_BITS] & bit)) {
			visited[placed / GENGHOST_CHECKPOINT_BITS] |= bit;
			crossed[color == GENGHOST_CHECKPOINT_RED]++;
		}
	}
	legacy_u8 complete = 0;
	for (legacy_u8 color = 0; color < GENGHOST_CHECKPOINT_COLOR_COUNT; color++) {
		if (total[color] && total[color] == crossed[color]) {
			complete |= 1U << color;
		}
	}
	return complete;
}

legacy_int extended_genghost_checkpoint_piece_required(const struct GENGHOST_ROUTE *route,
													   legacy_int piece)
{
	const struct CSTRK_OBJECT *object = checkpoint_object(piece);
	if (!(object_color(object) & route->checkpoint_colors)) {
		return 0;
	}
#if !defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY)
	return !extended_checkpoint_passed(object, PLAYER_CAR_INDEX);
#else
	return 1;
#endif
}

size_t extended_genghost_checkpoint_frontier(const struct GENGHOST_ROUTE *route)
{
#if !defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY)
	if (route->checkpoint_colors && route->checkpoint_colors != GENGHOST_CHECKPOINT_NONE) {
		for (size_t index = 0; index < route->piece_count; index++) {
			if (extended_genghost_checkpoint_piece_required(route, route->pieces[index])) {
				return index;
			}
		}
	}
#endif
	return route->piece_count;
}

legacy_int extended_genghost_checkpoint_target(const struct GENGHOST_ROUTE *route,
											   const struct GENGHOST_DRIVER *driver,
											   legacy_f64 lookahead,
											   struct GENGHOST_WAYPOINT *target)
{
	size_t frontier = extended_genghost_checkpoint_frontier(route);
	if (frontier == route->piece_count) {
		return 0;
	}
	size_t first = 0;
	while (first < route->point_count && route->points[first].piece_index < frontier) {
		first++;
	}
	if (first == route->point_count || route->points[first].piece_index != frontier) {
		return 0;
	}
	size_t last = first;
	while (last + 1U < route->point_count && route->points[last + 1U].piece_index == frontier) {
		last++;
	}
	legacy_f64 gate_distance = (route->points[first].distance + route->points[last].distance) * 0.5;
	if (gate_distance > driver->progress + lookahead + TRACK_TILE_SIZE &&
		target->distance < gate_distance) {
		return 0;
	}
	legacy_int piece = route->pieces[frontier];
	struct VECTOR entry[ROUTE_GEOMETRY_POINT_COUNT], exit[ROUTE_GEOMETRY_POINT_COUNT];
	const struct TRKOBJINFO *info = extended_track_route_info(
		(legacy_u8)track_route_columns[piece], (legacy_u8)track_route_rows[piece],
		(legacy_u8)track_route_element_ids[piece]);
	if (!info) {
		return 0;
	}
	info += (legacy_u8)track_route_traversal_flags[piece] & OWOOT_ROUTE_SUBTYPE_MASK;
	(void)get_track_route_point((legacy_s16)piece, entry, 0, NULL);
	(void)get_track_route_point((legacy_s16)piece, exit, info->route_point_count - 1, NULL);
	legacy_f64 dx = LEGACY_S16_WRAP_SUB(exit[0].x, entry[0].x);
	legacy_f64 dz = LEGACY_S16_WRAP_SUB(exit[0].z, entry[0].z);
	legacy_f64 length = sqrt(dx * dx + dz * dz);
	if (length == 0.0) {
		return 0;
	}
	legacy_s16 orientation;
	*target = route->points[first];
	genghost_route_piece_frame(piece, &target->x, &target->z, &orientation);
	target->x += dx * GENGHOST_CHECKPOINT_RUNOUT / length;
	target->z += dz * GENGHOST_CHECKPOINT_RUNOUT / length;
	target->distance = gate_distance;
	target->height_specified = 1;
	target->y = genghost_route_piece_height(piece, target->x, target->z);
	return 1;
}
