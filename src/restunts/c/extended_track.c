#include "extended_track.h"

#ifdef RESTUNTS_SDL3
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "externs.h"
#include "track_objects.h"
#include "trackdata_layout.h"
#include "extended_geometry.h"

struct EXTENDED_TRACK_BINDINGS {
	legacy_s16 *primary;
	legacy_s16 *alternate;
	legacy_s8 *opponent;
	legacy_u8 *elements;
	legacy_u8 *terrain;
	legacy_s8 *route_elements;
	legacy_s8 *route_flags;
	legacy_u8 *signs;
	legacy_s8 *columns;
	legacy_s8 *rows;
};

struct EXTENDED_TRACK_STORAGE {
	legacy_s16 primary[EXTENDED_TRACK_ROUTE_CAPACITY];
	legacy_s16 alternate[EXTENDED_TRACK_ROUTE_CAPACITY];
	legacy_s8 opponent[(EXTENDED_TRACK_ROUTE_CAPACITY + 3U) * 2U];
	legacy_u8 elements[EXTENDED_TRACK_MAX_CELLS + 1U];
	legacy_u8 terrain[EXTENDED_TRACK_MAX_CELLS + 1U];
	legacy_s8 route_elements[EXTENDED_TRACK_ROUTE_CAPACITY];
	legacy_s8 route_flags[EXTENDED_TRACK_ROUTE_CAPACITY];
	legacy_u8 signs[EXTENDED_TRACK_MAX_CELLS + 1U];
	legacy_s8 columns[EXTENDED_TRACK_ROUTE_CAPACITY];
	legacy_s8 rows[EXTENDED_TRACK_ROUTE_CAPACITY];
	int32_t occupied[EXTENDED_TRACK_MAX_CELLS];
	int32_t origins[EXTENDED_TRACK_MAX_CELLS];
	int32_t origin_heads[EXTENDED_TRACK_MAX_CELLS];
	int32_t origin_next[CSTRK_MAX_OBJECTS];
	uint8_t object_ids[CSTRK_MAX_OBJECTS];
};

static struct CSTRK_TRACK current_track;
static struct EXTENDED_TRACK_STORAGE *storage;
static struct EXTENDED_TRACK_BINDINGS legacy_bindings;
static int bound;

int extended_track_active(void)
{
	return storage != NULL;
}
int extended_track_width(void)
{
	return storage ? current_track.width : 30;
}
int extended_track_height(void)
{
	return storage ? current_track.height : 30;
}
unsigned extended_track_route_capacity(void)
{
	return storage ? EXTENDED_TRACK_ROUTE_CAPACITY : TRACKDATA_MAP_SIZE;
}
const struct CSTRK_TRACK *extended_track_model(void)
{
	return storage ? &current_track : NULL;
}

static void restore_bindings(void)
{
	if (!bound) {
		return;
	}
	track_primary_route_links = legacy_bindings.primary;
	track_alternate_route_links = legacy_bindings.alternate;
	opponent_route_track_indices = legacy_bindings.opponent;
	track_element_map = legacy_bindings.elements;
	track_terrain_map = legacy_bindings.terrain;
	track_route_element_ids = legacy_bindings.route_elements;
	track_route_traversal_flags = legacy_bindings.route_flags;
	roadside_sign_indices_by_tile = legacy_bindings.signs;
	track_route_columns = legacy_bindings.columns;
	track_route_rows = legacy_bindings.rows;
	bound = 0;
}

void extended_track_rebind(void)
{
	if (!storage || bound) {
		return;
	}
	legacy_bindings.primary = track_primary_route_links;
	legacy_bindings.alternate = track_alternate_route_links;
	legacy_bindings.opponent = opponent_route_track_indices;
	legacy_bindings.elements = track_element_map;
	legacy_bindings.terrain = track_terrain_map;
	legacy_bindings.route_elements = track_route_element_ids;
	legacy_bindings.route_flags = track_route_traversal_flags;
	legacy_bindings.signs = roadside_sign_indices_by_tile;
	legacy_bindings.columns = track_route_columns;
	legacy_bindings.rows = track_route_rows;
	track_primary_route_links = storage->primary;
	track_alternate_route_links = storage->alternate;
	opponent_route_track_indices = storage->opponent;
	track_element_map = storage->elements;
	track_terrain_map = storage->terrain;
	track_route_element_ids = storage->route_elements;
	track_route_traversal_flags = storage->route_flags;
	roadside_sign_indices_by_tile = storage->signs;
	track_route_columns = storage->columns;
	track_route_rows = storage->rows;
	bound = 1;
}

void extended_track_unload(void)
{
	restore_bindings();
	free(storage);
	storage = NULL;
	cstrk_free(&current_track);
	init_row_tables();
}

/* Every road rotation has an original resource spelling. Match its model and
 * shape, keeping handed pieces distinct. Symmetric straights and scenery can
 * reuse the opposite spelling, since their route and shape are unchanged. */
static int object_footprint_valid(const struct CSTRK_OBJECT *object)
{
	if (object->legacy_id >= 182U) {
		return 0; /* Runtime-only hill/overlay shapes. */
	}
	const struct TRACKOBJECT *original = &trkObjectList[object->legacy_id];
	if (original->ss_physicalModel < 0 || (object->lower_surface != CSTRK_ROAD_PAVED &&
										   original->ss_physicalModel != PHYSICAL_MODEL_OVERPASS)) {
		return 0;
	}
	unsigned flags = (unsigned)(legacy_u8)original->ss_multiTileFlag;
	unsigned width = 1U + ((flags & 2U) != 0U);
	unsigned height = 1U + ((flags & 1U) != 0U);
	unsigned mask = (1U << (width * height)) - 1U;
	int model = original->ss_physicalModel;
	unsigned angle = (unsigned)(legacy_u16)original->ss_rotY & 1023U;
	if (model == PHYSICAL_MODEL_LARGE_CORNER || model == PHYSICAL_MODEL_BANKED_CORNER ||
		model == PHYSICAL_MODEL_ELEVATED_CORNER) {
		static const unsigned masks_by_angle[4] = {11U, 14U, 13U, 7U};
		mask = masks_by_angle[angle / 256U];
	} else if (model == PHYSICAL_MODEL_CHICANE_LEFT_RIGHT ||
			   model == PHYSICAL_MODEL_CHICANE_RIGHT_LEFT) {
		mask = (model == PHYSICAL_MODEL_CHICANE_LEFT_RIGHT) == (angle == 0U) ? 6U : 9U;
	}
	for (unsigned rotation = 0; rotation < object->rotation; rotation++) {
		unsigned next_mask = 0;
		for (unsigned y = 0; y < height; y++) {
			for (unsigned x = 0; x < width; x++) {
				if (mask & (1U << (y * width + x))) {
					next_mask |= 1U << (x * height + height - 1U - y);
				}
			}
		}
		mask = next_mask;
		unsigned temporary = width;
		width = height;
		height = temporary;
	}
	return width == object->width && height == object->height && mask == object->occupied_mask;
}

static int resolve_object_id(const struct CSTRK_OBJECT *object)
{
	if (!object_footprint_valid(object)) {
		return -1;
	}
	const struct TRACKOBJECT *original = &trkObjectList[object->legacy_id];
	unsigned angle = ((unsigned)(legacy_u16)original->ss_rotY + object->rotation * 256U) & 1023U;
	if (object->rotation == 0) {
		return object->legacy_id;
	}
	for (unsigned pass = 0; pass < 2; pass++) {
		for (unsigned id = 1; id < 182U; id++) {
			const struct TRACKOBJECT *candidate = &trkObjectList[id];
			if (candidate->ss_physicalModel != original->ss_physicalModel ||
				candidate->ss_surfaceType != original->ss_surfaceType ||
				candidate->ss_shapePtr != original->ss_shapePtr ||
				candidate->ss_loShapePtr != original->ss_loShapePtr) {
				continue;
			}
			unsigned candidate_angle = (unsigned)(legacy_u16)candidate->ss_rotY & 1023U;
			unsigned flags = (unsigned)(legacy_u8)candidate->ss_multiTileFlag;
			if ((1U + ((flags & 2U) != 0U)) != object->width ||
				(1U + ((flags & 1U) != 0U)) != object->height) {
				continue;
			}
			if (candidate_angle == angle ||
				original->ss_physicalModel == PHYSICAL_MODEL_CROSSROAD) {
				return (int)id;
			}
			if (pass && ((candidate_angle ^ angle) == 512U) &&
				(original->ss_physicalModel == PHYSICAL_MODEL_ROAD ||
				 original->ss_physicalModel == PHYSICAL_MODEL_LOOP ||
				 original->ss_physicalModel == PHYSICAL_MODEL_TUNNEL ||
				 original->ss_physicalModel == PHYSICAL_MODEL_PIPE ||
				 original->ss_physicalModel == PHYSICAL_MODEL_HALF_PIPE ||
				 original->ss_physicalModel == PHYSICAL_MODEL_ELEVATED_ROAD ||
				 original->ss_physicalModel == PHYSICAL_MODEL_ELEVATED_SPAN ||
				 original->ss_physicalModel == PHYSICAL_MODEL_SOLID_ROAD ||
				 original->ss_physicalModel == PHYSICAL_MODEL_OVERPASS ||
				 original->ss_physicalModel == PHYSICAL_MODEL_HIGHWAY ||
				 original->ss_physicalModel == PHYSICAL_MODEL_SLALOM ||
				 original->ss_physicalModel == PHYSICAL_MODEL_CHICANE_LEFT_RIGHT ||
				 original->ss_physicalModel == PHYSICAL_MODEL_CHICANE_RIGHT_LEFT ||
				 original->ss_physicalModel == PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT)) {
				return (int)id;
			}
		}
	}
	/* Rotationally symmetric scenery has no directional resource variant. */
	if (!original->ss_trkObjInfoPtr && object->width == 1 && object->height == 1) {
		return object->legacy_id;
	}
	return -1;
}

static legacy_s16 object_hill_id(const struct CSTRK_TRACK *track, const struct CSTRK_OBJECT *object,
								 legacy_s16 id)
{
	if (!object->slope_x && !object->slope_y) {
		return id;
	}
	struct CSTRK_TERRAIN_PROFILE profile;
	if (!cstrk_terrain_profile(track, object->x, object->y, &profile) ||
		profile.terrain_id < CSTRK_TERRAIN_PROFILE_NORTH ||
		profile.terrain_id > CSTRK_TERRAIN_PROFILE_EAST) {
		return -1;
	}
	const struct TRACKOBJECT *definition = &trkObjectList[id];
	if (definition->ss_physicalModel == PHYSICAL_MODEL_ROAD) {
		/* Surface is independent of the stored legacy road spelling. */
		static const legacy_u8 roads[][2] = {{4, 5}, {14, 15}, {24, 25}};
		legacy_u16 axis = ((legacy_u16)definition->ss_rotY / ANGLE_QUARTER_TURN) & 1U;
		id = roads[object->surface][axis];
	}
	legacy_u8 hill_id = subst_hillroad_track(profile.terrain_id, (legacy_u8)id);
	return hill_id ? hill_id : -1;
}

static int prepare_storage(const struct CSTRK_TRACK *track, struct EXTENDED_TRACK_STORAGE *target,
						   char *error, size_t error_size)
{
	memset(target, 0, sizeof(*target));
	for (unsigned cell = 0; cell < EXTENDED_TRACK_MAX_CELLS; cell++) {
		target->occupied[cell] = -1;
		target->origins[cell] = -1;
		target->origin_heads[cell] = -1;
	}
	memset(target->signs, 255, sizeof(target->signs));
	for (uint32_t index = 0; index < track->object_count; index++) {
		const struct CSTRK_OBJECT *object = &track->objects[index];
		legacy_s16 id = (legacy_s16)resolve_object_id(object);
		if (id < 0) {
			if (error && error_size) {
				snprintf(error, error_size, "Unsupported track element %u rotation or footprint",
						 object->legacy_id);
			}
			return 0;
		}
		id = object_hill_id(track, object, id);
		if (id < 0) {
			if (error && error_size) {
				snprintf(error, error_size,
						 "Track element %u is not compatible with the original hill slope",
						 object->legacy_id);
			}
			return 0;
		}
		target->object_ids[index] = (uint8_t)id;
		unsigned anchor = object->y * track->width + object->x;
		target->origin_next[index] = target->origin_heads[anchor];
		target->origin_heads[anchor] = (int32_t)index;
		if (target->origins[anchor] < 0 || trkObjectList[id].ss_trkObjInfoPtr) {
			target->origins[anchor] = (int32_t)index;
		}
		for (unsigned dy = 0; dy < object->height; dy++) {
			for (unsigned dx = 0; dx < object->width; dx++) {
				if (!(object->occupied_mask & (1U << (dy * object->width + dx)))) {
					continue;
				}
				target->occupied[(object->y + dy) * track->width + object->x + dx] = (int32_t)index;
			}
		}
	}
	/* Draw/route map retains resource anchor and continuation semantics. Road
 * anchors take priority over decoration anchors in a free inner quadrant;
 * collision and rendering use the independent object list and occupancy. */
	for (unsigned pass = 0; pass < 2; pass++) {
		for (uint32_t index = 0; index < track->object_count; index++) {
			const struct CSTRK_OBJECT *object = &track->objects[index];
			unsigned id = target->object_ids[index];
			int road = trkObjectList[id].ss_trkObjInfoPtr != NULL;
			if ((unsigned)road != pass) {
				continue;
			}
			unsigned anchor = (track->height - 1U - object->y) * track->width + object->x;
			target->elements[anchor] = (uint8_t)id;
			for (unsigned dy = 0; dy < object->height; dy++) {
				for (unsigned dx = 0; dx < object->width; dx++) {
					if ((!dx && !dy) ||
						!(object->occupied_mask & (1U << (dy * object->width + dx)))) {
						continue;
					}
					unsigned cell =
						(track->height - 1U - object->y - dy) * track->width + object->x + dx;
					target->elements[cell] = dy ? (dx ? TRACK_TILE_CONTINUATION_SOUTHEAST
													  : TRACK_TILE_CONTINUATION_SOUTH)
												: TRACK_TILE_CONTINUATION_EAST;
				}
			}
		}
	}
	/* Native hill-road IDs are resolved above. The extended adapter reads
	 * terrain profiles and their absolute base heights from the shared grid,
	 * so the legacy map stays neutral instead of substituting these IDs twice. */
	return 1;
}

int extended_track_load(const uint8_t *data, size_t size, char *error, size_t error_size)
{
	struct CSTRK_TRACK next;
	cstrk_init(&next);
	if (!cstrk_decode(data, size, &next, error, error_size)) {
		return 0;
	}
	struct EXTENDED_TRACK_STORAGE *next_storage = malloc(sizeof(*next_storage));
	if (!next_storage || !prepare_storage(&next, next_storage, error, error_size)) {
		if (!next_storage && error && error_size) {
			snprintf(error, error_size, "Not enough memory for extended track");
		}
		free(next_storage);
		cstrk_free(&next);
		return 0;
	}
	restore_bindings();
	free(storage);
	cstrk_free(&current_track);
	current_track = next;
	storage = next_storage;
	extended_track_rebind();
	init_row_tables();
	return 1;
}

int extended_track_activate(const struct CSTRK_TRACK *track, char *error, size_t error_size)
{
	uint8_t *data = NULL;
	size_t size = 0;
	if (!cstrk_encode(track, &data, &size, error, error_size)) {
		return 0;
	}
	int result = extended_track_load(data, size, error, error_size);
	free(data);
	return result;
}

const struct CSTRK_OBJECT *extended_track_object_at(int column, int row)
{
	if (!storage || column < 0 || row < 0 || column >= current_track.width ||
		row >= current_track.height) {
		return NULL;
	}
	int32_t index = storage->occupied[row * current_track.width + column];
	return index < 0 ? NULL : &current_track.objects[index];
}

const struct CSTRK_OBJECT *extended_track_object_origin_at(int column, int row)
{
	if (!storage || column < 0 || row < 0 || column >= current_track.width ||
		row >= current_track.height) {
		return NULL;
	}
	int32_t index = storage->origins[row * current_track.width + column];
	return index < 0 ? NULL : &current_track.objects[index];
}

const struct CSTRK_OBJECT *extended_track_object_for_id(int column, int row, unsigned id)
{
	if (!storage || column < 0 || row < 0 || column >= current_track.width ||
		row >= current_track.height) {
		return NULL;
	}
	for (int32_t index = storage->origin_heads[row * current_track.width + column]; index >= 0;
		 index = storage->origin_next[index]) {
		if (storage->object_ids[index] == id) {
			return &current_track.objects[index];
		}
	}
	return NULL;
}

unsigned extended_track_object_id(const struct CSTRK_OBJECT *object)
{
	if (!storage || !object || !current_track.object_count) {
		return 0;
	}
	uintptr_t address = (uintptr_t)object;
	uintptr_t begin = (uintptr_t)current_track.objects;
	size_t bytes = current_track.object_count * sizeof(*object);
	if (address < begin || address - begin >= bytes || (address - begin) % sizeof(*object)) {
		return 0;
	}
	return storage->object_ids[(address - begin) / sizeof(*object)];
}

legacy_s16 extended_track_elevation(int column, int row, int local_x, int local_y)
{
	if (!storage || column < 0 || row < 0 || column >= current_track.width ||
		row >= current_track.height) {
		return 0;
	}
	struct CSTRK_TERRAIN_PROFILE profile;
	if (!cstrk_terrain_profile(&current_track, (legacy_u16)column, (legacy_u16)row, &profile)) {
		return 0;
	}
	if (profile.terrain_id == CSTRK_TERRAIN_PROFILE_FLAT ||
		profile.terrain_id == CSTRK_TERRAIN_PROFILE_RAISED) {
		/* Starting positions on plateaus are queried before collision resources
		 * are necessarily loaded. Their absolute height needs no plane template. */
		return profile.base_height * CSTRK_LEVEL_WORLD_HEIGHT;
	}
	if (local_x < 0) {
		local_x = 0;
	} else if (local_x > CSTRK_TILE_WORLD_SIZE) {
		local_x = CSTRK_TILE_WORLD_SIZE;
	}
	if (local_y < 0) {
		local_y = 0;
	} else if (local_y > CSTRK_TILE_WORLD_SIZE) {
		local_y = CSTRK_TILE_WORLD_SIZE;
	}
	/* Boundary samples belong to this cell, including the far map edge. */
	legacy_s32 world_x = (legacy_s32)column * CSTRK_TILE_WORLD_SIZE + local_x;
	legacy_s32 world_z = (legacy_s32)(current_track.height - row) * CSTRK_TILE_WORLD_SIZE - local_y;
	legacy_s32 sample_x = world_x - (local_x == CSTRK_TILE_WORLD_SIZE);
	legacy_s32 sample_z = world_z + (local_y == CSTRK_TILE_WORLD_SIZE) - (local_y == 0);
	struct EXTENDED_TERRAIN_SAMPLE sample;
	if (!extended_geometry_terrain(&current_track, sample_x, sample_z, &sample)) {
		return 0;
	}
	const struct PLANE *plane = &sample.plane;
	legacy_s32 x =
		LEGACY_S16_WRAP_SUB((legacy_s16)world_x, sample.origin.x) - plane->plane_origin.x;
	legacy_s32 z =
		LEGACY_S16_WRAP_SUB((legacy_s16)world_z, sample.origin.z) - plane->plane_origin.z;
	return (legacy_s16)(plane->plane_origin.y - ((legacy_s64)plane->plane_normal.x * x +
												 (legacy_s64)plane->plane_normal.z * z) /
													plane->plane_normal.y);
}
#endif
