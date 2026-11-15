#include "../opponent/unit_shortcut.h"

#ifdef RESTUNTS_SDL3
#include <math.h>
#include "../externs.h"
#include "extended_track.h"
#include "../track/track_objects.h"
#include "../track/track_setup_internal.h"
#include "../track/trackdata_layout.h"

#define UNIT_SHORTCUT_WORLD_PER_MPH (22.0 / 3.0)
#define UNIT_SHORTCUT_MAX_FOOTPRINT_SIDE 2U
#define UNIT_SHORTCUT_OVERLAP_PROBE_CAPACITY 2U
#define UNIT_SHORTCUT_MINIMUM_POINTS 3U

struct UNIT_SHORTCUT_FOOTPRINT {
	legacy_f64 minimum_x, maximum_x, minimum_z, maximum_z, height;
};

static legacy_int unit_shortcut_open_model(const struct TRACKOBJECT *object)
{
	return object->ss_ssOvelay == 0 && (object->ss_physicalModel == PHYSICAL_MODEL_ROAD ||
										object->ss_physicalModel == PHYSICAL_MODEL_SHARP_CORNER ||
										object->ss_physicalModel == PHYSICAL_MODEL_LARGE_CORNER);
}

static legacy_int unit_shortcut_extended_cell(const struct CSTRK_TRACK *track,
											  const struct CSTRK_OBJECT *object, legacy_u16 column,
											  legacy_u16 row)
{
	const struct CSTRK_OBJECT *overlaps[UNIT_SHORTCUT_OVERLAP_PROBE_CAPACITY];
	if (extended_track_object_at(column, row) != object ||
		extended_track_objects_in_cell((legacy_s16)column, (legacy_s16)row, overlaps,
									   UNIT_SHORTCUT_OVERLAP_PROBE_CAPACITY) != 1U ||
		overlaps[0] != object) {
		return 0;
	}
	const struct CSTRK_CELL *cell = &track->cells[row * track->width + column];
	if (cell->surface != CSTRK_TERRAIN_GRASS || cell->half_water_corner != 0) {
		return 0;
	}
	for (legacy_u16 corner_row = row; corner_row <= row + 1U; corner_row++) {
		for (legacy_u16 corner_column = column; corner_column <= column + 1U; corner_column++) {
			if (track->corner_heights[corner_row * (track->width + 1U) + corner_column] !=
				object->base_height) {
				return 0;
			}
		}
	}
	return 1;
}

static legacy_int unit_shortcut_footprint(legacy_s16 piece,
										  struct UNIT_SHORTCUT_FOOTPRINT *footprint)
{
	if (piece <= 0 || piece >= track_pieces_counter ||
		track_alternate_route_links[piece] != TRACK_ROUTE_LINK_NONE) {
		return 0;
	}
	legacy_u8 id = (legacy_u8)track_route_element_ids[piece];
	if (id >= sizeof(trkObjectList) / sizeof(trkObjectList[0]) ||
		!unit_shortcut_open_model(&trkObjectList[id])) {
		return 0;
	}
	legacy_u16 column = (legacy_u8)track_route_columns[piece];
	legacy_u16 row = (legacy_u8)track_route_rows[piece];
	legacy_u16 width =
		1U + (((legacy_u8)trkObjectList[id].ss_multiTileFlag & MULTI_TILE_COLUMN_EDGE_FLAG) != 0);
	legacy_u16 height =
		1U + (((legacy_u8)trkObjectList[id].ss_multiTileFlag & MULTI_TILE_ROW_EDGE_FLAG) != 0);
	const struct CSTRK_TRACK *track = NULL;
	const struct CSTRK_OBJECT *object = NULL;
	footprint->height = 0;
	if (extended_track_active()) {
		track = extended_track_model();
		object = extended_track_object_for_id(column, row, id);
		if (!track || !track->cells || !track->corner_heights || !object || object->x != column ||
			object->y != row || extended_track_object_id(object) != id ||
			cstrk_is_checkpoint(object->legacy_id) || object->slope_x || object->slope_y ||
			object->roll_ramp || object->tunnel_slalom || object->pine_count ||
			object->lower_surface != CSTRK_ROAD_PAVED || object->width == 0 ||
			object->height == 0 || object->width > UNIT_SHORTCUT_MAX_FOOTPRINT_SIDE ||
			object->height > UNIT_SHORTCUT_MAX_FOOTPRINT_SIDE) {
			return 0;
		}
		width = object->width;
		height = object->height;
		legacy_u16 full_mask = (1U << (width * height)) - 1U;
		if (object->occupied_mask != full_mask || column + width > track->width ||
			row + height > track->height) {
			return 0;
		}
		footprint->height = object->base_height * CSTRK_LEVEL_WORLD_HEIGHT;
	}
	if (column + width > extended_track_width() || row + height > extended_track_height()) {
		return 0;
	}
	for (legacy_u16 cell_row = row; cell_row < row + height; cell_row++) {
		for (legacy_u16 cell_column = column; cell_column < column + width; cell_column++) {
			if (roadside_sign_indices_by_tile[trackrows[cell_row] + cell_column] !=
				TRACK_TILE_INDEX_UNASSIGNED) {
				return 0;
			}
			if (track) {
				if (!unit_shortcut_extended_cell(track, object, cell_column, cell_row)) {
					return 0;
				}
			} else if (track_terrain_map[terrainrows[cell_row] + cell_column] !=
					   CSTRK_TERRAIN_PROFILE_FLAT) {
				return 0;
			}
		}
	}
	footprint->minimum_x = column * TRACK_TILE_SIZE;
	footprint->maximum_x = (column + width) * TRACK_TILE_SIZE;
	footprint->minimum_z = (extended_track_height() - row - height) * TRACK_TILE_SIZE;
	footprint->maximum_z = (extended_track_height() - row) * TRACK_TILE_SIZE;
	return 1;
}

static legacy_int unit_shortcut_contains(const struct UNIT_SHORTCUT_FOOTPRINT *footprint,
										 const struct UNIT_ROUTE_POINT *point,
										 const struct UNIT_CAR_LIMITS *limits, legacy_f64 clearance)
{
	if (!isfinite(point->x) || !isfinite(point->y) || !isfinite(point->z) ||
		!isfinite(point->half_width) || point->half_width <= 0 || point->y != footprint->height) {
		return 0;
	}
	legacy_f64 normal_width =
		fmax(0, point->half_width - limits->half_width - UNIT_SHORTCUT_ROAD_CLEARANCE);
	legacy_f64 maximum_offset =
		UNIT_SHORTCUT_MAX_LANE_FRACTION *
		(normal_width + point->half_width * UNIT_SHORTCUT_EXTRA_WIDTH_FRACTION);
	legacy_f64 inset = clearance + maximum_offset;
	return point->x - inset >= footprint->minimum_x && point->x + inset <= footprint->maximum_x &&
		   point->z - inset >= footprint->minimum_z && point->z + inset <= footprint->maximum_z;
}
#endif

void unit_shortcut_mark_route(struct UNIT_ROUTE_POINT *points, legacy_u32 count,
							  const struct UNIT_CAR_LIMITS *limits, legacy_u8 owoot)
{
	if (!points) {
		return;
	}
	for (legacy_u32 index = 0; index < count; index++) {
		points[index].shortcut_safe = 0;
	}
#ifdef RESTUNTS_SDL3
	if (owoot || count < UNIT_SHORTCUT_MINIMUM_POINTS || !limits || !isfinite(limits->half_width) ||
		!isfinite(limits->half_length) || !isfinite(limits->max_speed) || limits->half_width <= 0 ||
		limits->half_length <= 0 || limits->max_speed <= 0 || !track_alternate_route_links ||
		!track_route_element_ids || !track_route_columns || !track_route_rows ||
		!roadside_sign_indices_by_tile || (!extended_track_active() && !track_terrain_map)) {
		return;
	}
	/* The disk encloses the complete car at any heading. The extra tick keeps
	 * every widened sample well inside a visited native tile at the selected
	 * physics rate. The 10 Hz mode needs twice the 20 Hz movement allowance. */
	legacy_f64 frame_rate =
		framespersec == GAME_FRAME_RATE_NORMAL ? GAME_FRAME_RATE_NORMAL : GAME_FRAME_RATE_LOW;
	legacy_f64 clearance = hypot(limits->half_width, limits->half_length) +
						   limits->max_speed * UNIT_SHORTCUT_WORLD_PER_MPH / frame_rate +
						   UNIT_SHORTCUT_ROAD_CLEARANCE;
	for (legacy_u32 index = 1; index + 1U < count; index++) {
		legacy_s16 piece = points[index].route_index;
		if (points[index - 1U].route_index != piece || points[index + 1U].route_index != piece) {
			continue;
		}
		struct UNIT_SHORTCUT_FOOTPRINT footprint;
		if (!unit_shortcut_footprint(piece, &footprint)) {
			continue;
		}
		/* A rectangle is convex: enclosing all three expanded endpoint disks
		 * also encloses every joining line segment and its swept vehicle body.
		 * Neighbor widths are checked as expanded even if their flags stay off. */
		if (unit_shortcut_contains(&footprint, &points[index - 1U], limits, clearance) &&
			unit_shortcut_contains(&footprint, &points[index], limits, clearance) &&
			unit_shortcut_contains(&footprint, &points[index + 1U], limits, clearance)) {
			points[index].shortcut_safe = 1;
		}
	}
#else
	(void)limits;
	(void)owoot;
#endif
}
