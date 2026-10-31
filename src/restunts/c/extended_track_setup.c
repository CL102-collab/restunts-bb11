#include "extended_track_setup.h"
#include "track_setup_internal.h"
#include "externs.h"
#include "track_objects.h"
#ifdef RESTUNTS_SDL3
#include "extended_geometry.h"
#include "roll_ramp.h"
#endif
legacy_s16 extended_track_setup_terrain_validated(void)
{
	/* Shared corner vertices have already been validated by the format codec. */
	return extended_track_active();
}
legacy_s16 extended_track_setup_legacy_elements(void)
{
	return !extended_track_active();
}
void extended_track_setup_start_element(struct TRACK_SETUP_WALK *walk)
{
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		const struct CSTRK_OBJECT *object = extended_track_object_at(walk->column, walk->row);
		walk->tile_element = object && object->x == walk->column && object->y == walk->row
								 ? (legacy_u8)extended_track_object_id(object)
								 : 0;
	}
#else
	(void)walk;
#endif
}
legacy_s16 extended_track_setup_piece_matches(legacy_u8 existing, legacy_u8 current)
{
	return !extended_track_active() || existing == current;
}
legacy_u16 extended_track_setup_branch_limit(void)
{
	return extended_track_active() ? TRACK_SETUP_BRANCH_COUNT : TRACK_SETUP_LEGACY_BRANCH_COUNT;
}
legacy_u16 extended_track_setup_cell_count(void)
{
	return (legacy_u16)(extended_track_width() * extended_track_height() + 1);
}

const struct TRKOBJINFO *extended_track_setup_info(legacy_s16 column, legacy_s16 row, legacy_u16 id)
{
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		return extended_track_route_info(column, row, id);
	}
#else
	(void)column;
	(void)row;
#endif
	return trkObjectList[id].ss_trkObjInfoPtr;
}
legacy_u8 extended_track_setup_jump_limit(const struct TRACK_SETUP_WALK *walk,
										  legacy_u8 legacy_limit)
{
#ifdef RESTUNTS_SDL3
	if (walk->previous_connection_code == ROLL_RAMP_LAUNCH_CONNECTION && extended_track_active()) {
		const struct CSTRK_OBJECT *object = extended_track_object_for_id(
			walk->previous_column, walk->previous_row, walk->previous_tile_element);
		if (object && object->roll_ramp) {
			return ROLL_RAMP_JUMP_MAX_EMPTY_TILES;
		}
	}
#else
	(void)walk;
#endif
	return legacy_limit;
}
legacy_u8 extended_track_setup_connection_code(legacy_u8 code)
{
#ifdef RESTUNTS_SDL3
	if (code == ROLL_RAMP_LAUNCH_CONNECTION) {
		return 0;
	}
#endif
	return code;
}
legacy_s16 extended_track_setup_reversible(const struct TRACK_SETUP_WALK *walk)
{
#ifdef RESTUNTS_SDL3
	const struct CSTRK_OBJECT *object =
		extended_track_active()
			? extended_track_object_for_id(walk->column, walk->row, walk->tile_element)
			: NULL;
	return !object || !object->roll_ramp;
#else
	(void)walk;
	return 1;
#endif
}
legacy_s16 extended_track_setup_resolve_piece(struct TRACK_SETUP_WALK *walk, legacy_s16 *status)
{
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		const struct CSTRK_OBJECT *object = extended_track_object_at(walk->column, walk->row);
		if (!object) {
			walk->tile_element = 0;
			*status = TRACK_SETUP_OK;
			return 1;
		}
		legacy_s16 dx = walk->column - object->x, dy = walk->row - object->y;
		walk->tile_element = (legacy_u8)extended_track_object_id(object);
		walk->column = (legacy_s8)object->x;
		walk->row = (legacy_s8)object->y;
		const legacy_u8 *entries =
			dy ? (dx ? track_entry_points_northwest : track_entry_points_north)
			   : (dx ? track_entry_points_west : track_entry_points_owner);
		walk->tile_entry_point = track_setup_entry_point(entries, walk->orientation);
		if (!walk->jump_length && walk->tile_entry_point == TRACK_ENTRY_POINT_NONE) {
			*status = track_setup_error(walk->branches, TRACK_SETUP_INTERNAL_ERROR, walk->column,
										walk->row);
		} else {
			*status = track_setup_match_blocks(walk);
		}
		return 1;
	}
#else
	(void)walk;
	(void)status;
#endif
	return 0;
}
void extended_track_setup_jump_source(const struct TRACK_SETUP_WALK *walk, legacy_s8 *column,
									  legacy_s8 *row)
{
#ifdef RESTUNTS_SDL3
	if (walk->previous_connection_code == ROLL_RAMP_LAUNCH_CONNECTION && extended_track_active()) {
		const struct CSTRK_OBJECT *object = extended_track_object_for_id(
			walk->previous_column, walk->previous_row, walk->previous_tile_element);
		if (object && object->roll_ramp) {
			if (walk->orientation == TRACK_ORIENTATION_SOUTH) {
				*row = (legacy_s8)(object->y + object->height - 1U);
			} else if (walk->orientation == TRACK_ORIENTATION_EAST) {
				*column = (legacy_s8)(object->x + object->width - 1U);
			}
		}
	}
#else
	(void)walk;
	(void)column;
	(void)row;
#endif
}
#ifdef RESTUNTS_SDL3
static legacy_s16 extended_setup_base_height(legacy_s8 column, legacy_s8 row, legacy_u8 id,
											 const struct VECTOR *local)
{
	const struct CSTRK_OBJECT *object =
		extended_track_object_for_id((legacy_u8)column, (legacy_u8)row, id);
	if (object) {
		struct VECTOR origin;
		extended_geometry_object_origin(extended_track_model(), object, &origin);
		return LEGACY_S16_WRAP_ADD(origin.y,
								   extended_geometry_object_grade(object, local->x, local->z));
	}
	struct CSTRK_TERRAIN_PROFILE profile;
	if (!cstrk_terrain_profile(extended_track_model(), (legacy_u16)column, (legacy_u16)row,
							   &profile)) {
		return 0;
	}
	/* Native hill route vectors already include their incline or span height. */
	return (legacy_s16)(profile.base_height * CSTRK_LEVEL_WORLD_HEIGHT);
}
#endif
legacy_s16 extended_track_setup_roadside_height(const struct TRACK_SETUP_WALK *walk,
												struct VECTOR *vector)
{
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		vector->y = LEGACY_S16_WRAP_ADD(
			vector->y, extended_setup_base_height(walk->previous_column, walk->previous_row,
												  walk->previous_tile_element, vector));
		return 1;
	}
#else
	(void)walk;
	(void)vector;
#endif
	return 0;
}
legacy_s16 extended_track_setup_camera_height(const struct TRACK_SETUP_WALK *walk,
											  const struct VECTOR *vector, legacy_s16 *height)
{
#ifdef RESTUNTS_SDL3
	if (extended_track_active()) {
		*height = extended_setup_base_height(walk->column, walk->row, walk->tile_element, vector);
		return 1;
	}
#else
	(void)walk;
	(void)vector;
	(void)height;
#endif
	return 0;
}
legacy_s16 extended_track_setup_jump_connection(legacy_u8 code)
{
	return code == TRACK_JUMP_CONNECTION_CODE
#ifdef RESTUNTS_SDL3
		   || code == ROLL_RAMP_LAUNCH_CONNECTION
#endif
		;
}
