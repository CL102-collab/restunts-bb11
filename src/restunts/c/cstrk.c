#include "cstrk.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t cstrk_magic[8] = {'C', 'S', 'T', 'R', 'K', 0x1a, '\r', '\n'};

static int cstrk_fail(char *error, size_t size, const char *format, ...)
{
	if (error != NULL && size != 0U) {
		va_list args;
		va_start(args, format);
		vsnprintf(error, size, format, args);
		va_end(args);
	}
	return 0;
}

static void cstrk_success(char *error, size_t size)
{
	if (error != NULL && size != 0U) {
		error[0] = '\0';
	}
}

static uint16_t cstrk_read16(const uint8_t *data)
{
	return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

static uint32_t cstrk_read32(const uint8_t *data)
{
	return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) | ((uint32_t)data[2] << 16U) |
		   ((uint32_t)data[3] << 24U);
}

static void cstrk_write16(uint8_t *data, uint16_t value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8U);
}

static void cstrk_write32(uint8_t *data, uint32_t value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8U);
	data[2] = (uint8_t)(value >> 16U);
	data[3] = (uint8_t)(value >> 24U);
}

static int cstrk_dimensions(uint16_t width, uint16_t height)
{
	return width >= 1U && width <= CSTRK_MAX_DIMENSION && height >= 1U &&
		   height <= CSTRK_MAX_DIMENSION;
}

#define CSTRK_TERRAIN_CORNER_COUNT 4U
#define CSTRK_TERRAIN_PROFILE_MASK_COUNT (1U << CSTRK_TERRAIN_CORNER_COUNT)
#define CSTRK_TERRAIN_PROFILE_INVALID LEGACY_U8_MAX

legacy_int cstrk_terrain_profile(const struct CSTRK_TRACK *track, legacy_u16 x, legacy_u16 y,
								 struct CSTRK_TERRAIN_PROFILE *profile)
{
	static const legacy_u8 terrain_ids[CSTRK_TERRAIN_PROFILE_MASK_COUNT] = {
		CSTRK_TERRAIN_PROFILE_FLAT,
		CSTRK_TERRAIN_PROFILE_OUTER_NORTHWEST,
		CSTRK_TERRAIN_PROFILE_OUTER_NORTHEAST,
		CSTRK_TERRAIN_PROFILE_NORTH,
		CSTRK_TERRAIN_PROFILE_OUTER_SOUTHEAST,
		CSTRK_TERRAIN_PROFILE_INVALID,
		CSTRK_TERRAIN_PROFILE_EAST,
		CSTRK_TERRAIN_PROFILE_INNER_SOUTHWEST,
		CSTRK_TERRAIN_PROFILE_OUTER_SOUTHWEST,
		CSTRK_TERRAIN_PROFILE_WEST,
		CSTRK_TERRAIN_PROFILE_INVALID,
		CSTRK_TERRAIN_PROFILE_INNER_SOUTHEAST,
		CSTRK_TERRAIN_PROFILE_SOUTH,
		CSTRK_TERRAIN_PROFILE_INNER_NORTHEAST,
		CSTRK_TERRAIN_PROFILE_INNER_NORTHWEST,
		CSTRK_TERRAIN_PROFILE_INVALID};
	if (track == NULL || track->corner_heights == NULL ||
		!cstrk_dimensions(track->width, track->height) || x >= track->width || y >= track->height) {
		return 0;
	}
	size_t stride = track->width + 1U, vertex = (size_t)y * stride + x;
	legacy_u8 heights[CSTRK_TERRAIN_CORNER_COUNT] = {
		track->corner_heights[vertex], track->corner_heights[vertex + 1U],
		track->corner_heights[vertex + stride + 1U], track->corner_heights[vertex + stride]};
	legacy_u8 minimum = CSTRK_MAX_HEIGHT, maximum = 0;
	for (legacy_u8 corner = 0; corner < CSTRK_TERRAIN_CORNER_COUNT; corner++) {
		if (heights[corner] > CSTRK_MAX_HEIGHT) {
			return 0;
		}
		if (heights[corner] < minimum) {
			minimum = heights[corner];
		}
		if (heights[corner] > maximum) {
			maximum = heights[corner];
		}
	}
	if (maximum - minimum > 1) {
		return 0;
	}
	legacy_u8 mask = 0;
	for (legacy_u8 corner = 0; corner < CSTRK_TERRAIN_CORNER_COUNT; corner++) {
		if (heights[corner] > minimum) {
			mask |= (legacy_u8)(1U << corner);
		}
	}
	legacy_u8 id = terrain_ids[mask];
	if (id == CSTRK_TERRAIN_PROFILE_INVALID) {
		return 0;
	}
	if (mask == 0 && minimum != 0) {
		id = CSTRK_TERRAIN_PROFILE_RAISED;
	}
	if (profile != NULL) {
		profile->terrain_id = id;
		profile->base_height = minimum;
		profile->high_mask = mask;
	}
	return 1;
}

static int cstrk_notes_valid(const char *notes, size_t length, char *error, size_t error_size)
{
	if (length > CSTRK_MAX_NOTES || (length != 0U && notes == NULL)) {
		return cstrk_fail(error, error_size, "Notes must contain at most %u ASCII characters",
						  CSTRK_MAX_NOTES);
	}
	for (size_t index = 0; index < length; index++) {
		unsigned char value = (unsigned char)notes[index];
		if (value != '\t' && value != '\r' && value != '\n' && (value < 0x20U || value > 0x7eU)) {
			return cstrk_fail(error, error_size, "Notes contain a non-text ASCII byte at %zu",
							  index);
		}
	}
	return 1;
}

void cstrk_init(struct CSTRK_TRACK *track)
{
	if (track != NULL) {
		memset(track, 0, sizeof(*track));
		track->version = CSTRK_VERSION;
	}
}

void cstrk_free(struct CSTRK_TRACK *track)
{
	if (track != NULL) {
		free(track->corner_heights);
		free(track->cells);
		free(track->objects);
		cstrk_init(track);
	}
}

int cstrk_create(struct CSTRK_TRACK *track, uint16_t width, uint16_t height, char *error,
				 size_t error_size)
{
	struct CSTRK_TRACK next;
	if (track == NULL || !cstrk_dimensions(width, height)) {
		return cstrk_fail(error, error_size, "Track dimensions must be between 1 and %u",
						  CSTRK_MAX_DIMENSION);
	}
	cstrk_init(&next);
	next.width = width;
	next.height = height;
	next.corner_heights = calloc(((size_t)width + 1U) * ((size_t)height + 1U), 1U);
	next.cells = calloc((size_t)width * height, sizeof(*next.cells));
	if (next.corner_heights == NULL || next.cells == NULL) {
		cstrk_free(&next);
		return cstrk_fail(error, error_size, "Cannot allocate track terrain");
	}
	for (size_t index = 0; index < (size_t)width * height; index++) {
		next.cells[index].corner_surface = CSTRK_TERRAIN_WATER;
	}
	cstrk_free(track);
	*track = next;
	cstrk_success(error, error_size);
	return 1;
}

#define CSTRK_ORIGINAL_AXIS_COUNT 2U
#define CSTRK_ORIGINAL_DIRECTION_COUNT 4U
/* Catalog spellings admitted before generic slopes. These tables only protect
 * older file revisions; current rendering and collision do not select by ID. */
static const legacy_u16 cstrk_original_road_groups[] = {4U, 14U, 24U};
static const legacy_u16 cstrk_original_ramp_groups[] = {36U, 56U, 95U};
static const legacy_u8 cstrk_original_ramp_rotations[CSTRK_ORIGINAL_DIRECTION_COUNT] = {1U, 3U, 0U,
																						2U};

legacy_u16 cstrk_object_slope_version(const struct CSTRK_OBJECT *object)
{
	if (!object || (!object->slope_x && !object->slope_y)) {
		return CSTRK_MIN_VERSION;
	}
	if (object->width != 1U || object->height != 1U || (object->slope_x && object->slope_y)) {
		return CSTRK_GENERIC_SLOPE_VERSION;
	}
	for (legacy_u8 group = 0;
		 group < sizeof(cstrk_original_road_groups) / sizeof(cstrk_original_road_groups[0]);
		 group++) {
		legacy_u16 offset = object->legacy_id - cstrk_original_road_groups[group];
		if (offset < CSTRK_ORIGINAL_AXIS_COUNT &&
			(offset + object->rotation) % CSTRK_ORIGINAL_AXIS_COUNT == (object->slope_x != 0)) {
			return CSTRK_MIN_VERSION;
		}
	}
	for (legacy_u8 group = 0;
		 group < sizeof(cstrk_original_ramp_groups) / sizeof(cstrk_original_ramp_groups[0]);
		 group++) {
		legacy_u16 offset = object->legacy_id - cstrk_original_ramp_groups[group];
		if (offset < CSTRK_ORIGINAL_DIRECTION_COUNT) {
			legacy_u8 direction =
				object->slope_x ? (object->slope_x < 0 ? 1U : 3U) : (object->slope_y < 0 ? 0U : 2U);
			legacy_u8 required_rotation =
				(CSTRK_ORIGINAL_AXIS_COUNT + CSTRK_ORIGINAL_DIRECTION_COUNT - direction) %
				CSTRK_ORIGINAL_DIRECTION_COUNT;
			if ((cstrk_original_ramp_rotations[offset] + object->rotation) %
					CSTRK_ORIGINAL_DIRECTION_COUNT ==
				required_rotation) {
				return CSTRK_MIN_VERSION;
			}
		}
	}
	/* V5 added aligned tunnels and slaloms without changing their IDs. */
	legacy_u16 first =
		object->legacy_id >= CSTRK_TUNNEL_FIRST_ID && object->legacy_id <= CSTRK_TUNNEL_LAST_ID
			? CSTRK_TUNNEL_FIRST_ID
			: CSTRK_SLALOM_FIRST_ID;
	if ((first == CSTRK_TUNNEL_FIRST_ID || (object->legacy_id >= CSTRK_SLALOM_FIRST_ID &&
											object->legacy_id <= CSTRK_SLALOM_LAST_ID)) &&
		(object->legacy_id - first + object->rotation) % CSTRK_ORIGINAL_AXIS_COUNT ==
			(object->slope_x != 0)) {
		return CSTRK_SLOPED_STUNT_VERSION;
	}
	return CSTRK_GENERIC_SLOPE_VERSION;
}

static int cstrk_validate_object(const struct CSTRK_TRACK *track, const struct CSTRK_OBJECT *object,
								 uint32_t index, uint8_t *occupied, char *error, size_t error_size)
{
	if (object->legacy_id == 0U || object->legacy_id > CSTRK_MAX_LEGACY_ID ||
		object->rotation > 3U || object->surface > CSTRK_ROAD_ICE || object->width < 1U ||
		object->width > 2U || object->height < 1U || object->height > 2U ||
		object->x + object->width > track->width || object->y + object->height > track->height) {
		return cstrk_fail(error, error_size, "Object %u has an invalid ID, surface or footprint",
						  (unsigned)index);
	}
	legacy_u8 overpass =
		object->legacy_id >= CSTRK_OVERPASS_FIRST_ID && object->legacy_id <= CSTRK_OVERPASS_LAST_ID;
	if (object->lower_surface > CSTRK_ROAD_ICE ||
		(!overpass && object->lower_surface != CSTRK_ROAD_PAVED) ||
		(overpass && track->version < CSTRK_LOWER_SURFACE_VERSION &&
		 object->lower_surface != object->surface)) {
		return cstrk_fail(error, error_size, "Object %u has an invalid lower road surface",
						  (legacy_uint)index);
	}
	unsigned mask_limit = (1U << (object->width * object->height)) - 1U;
	if (object->occupied_mask == 0U || (object->occupied_mask & ~mask_limit) != 0U) {
		return cstrk_fail(error, error_size, "Object %u has an invalid occupancy mask",
						  (unsigned)index);
	}
	if (object->base_height > CSTRK_MAX_HEIGHT || object->slope_x < -1 || object->slope_x > 1 ||
		object->slope_y < -1 || object->slope_y > 1) {
		return cstrk_fail(error, error_size, "Object %u has an invalid support plane",
						  (unsigned)index);
	}
	if (object->slope_x != 0 && object->slope_y != 0) {
		return cstrk_fail(error, error_size,
						  "Object %u requires one continuous straight hill slope",
						  (legacy_uint)index);
	}
	legacy_u16 slope_version = cstrk_object_slope_version(object);
	if (track->version < slope_version) {
		return cstrk_fail(error, error_size,
						  "Object %u requires track version v%u for this sloped placement",
						  (legacy_uint)index, slope_version);
	}
	/* Only corners touching a claimed cell support the piece. The far corner
	 * of an unclaimed cell can form a hill without bending the road. */
	for (legacy_u8 row = 0; row < object->height; row++) {
		for (legacy_u8 column = 0; column < object->width; column++) {
			if (!(object->occupied_mask & (1U << (row * object->width + column)))) {
				continue;
			}
			for (legacy_u8 dy = 0; dy <= 1U; dy++) {
				for (legacy_u8 dx = 0; dx <= 1U; dx++) {
					legacy_s16 expected = object->base_height + object->slope_x * (column + dx) +
										  object->slope_y * (row + dy);
					size_t vertex = ((size_t)object->y + row + dy) * (track->width + 1U) +
									object->x + column + dx;
					if (expected < 0 || expected > (legacy_s16)CSTRK_MAX_HEIGHT ||
						track->corner_heights[vertex] != expected) {
						return cstrk_fail(error, error_size,
										  "Object %u does not fit a continuous terrain plane",
										  (legacy_uint)index);
					}
				}
			}
		}
	}
	for (unsigned row = 0; row < object->height; row++) {
		for (unsigned column = 0; column < object->width; column++) {
			if ((object->occupied_mask & (1U << (row * object->width + column))) == 0U) {
				continue;
			}
			size_t cell = ((size_t)object->y + row) * track->width + object->x + column;
			if (occupied[cell] != 0U) {
				return cstrk_fail(error, error_size, "Object %u overlaps an occupied terrain cell",
								  (unsigned)index);
			}
			occupied[cell] = 1U;
		}
	}
	return 1;
}

int cstrk_validate(const struct CSTRK_TRACK *track, char *error, size_t error_size)
{
	uint8_t occupied[CSTRK_MAX_OBJECTS] = {0};
	if (track == NULL || !cstrk_dimensions(track->width, track->height) ||
		track->corner_heights == NULL || track->cells == NULL) {
		return cstrk_fail(error, error_size, "Track terrain or dimensions are invalid");
	}
	if (track->version < CSTRK_MIN_VERSION) {
		return cstrk_fail(error, error_size,
						  "Unsupported track version %u; minimum supported version is %u",
						  (legacy_uint)track->version, CSTRK_MIN_VERSION);
	}
	if (track->horizon >= CSTRK_HORIZON_COUNT) {
		return cstrk_fail(error, error_size, "Track has an unknown horizon");
	}
	if (track->object_count > (uint32_t)track->width * track->height ||
		(track->object_count != 0U && track->objects == NULL)) {
		return cstrk_fail(error, error_size, "Track has too many objects or no object storage");
	}
	for (unsigned row = 0; row <= track->height; row++) {
		for (unsigned column = 0; column <= track->width; column++) {
			size_t vertex = (size_t)row * (track->width + 1U) + column;
			int height = track->corner_heights[vertex];
			if (height > (int)CSTRK_MAX_HEIGHT) {
				return cstrk_fail(error, error_size, "Terrain vertex %zu exceeds height %u", vertex,
								  CSTRK_MAX_HEIGHT);
			}
			if (column > 0U && abs(height - track->corner_heights[vertex - 1U]) > 1) {
				return cstrk_fail(error, error_size, "Terrain has an unsupported horizontal cliff");
			}
			if (row > 0U && abs(height - track->corner_heights[vertex - track->width - 1U]) > 1) {
				return cstrk_fail(error, error_size, "Terrain has an unsupported vertical cliff");
			}
		}
	}
	for (size_t index = 0; index < (size_t)track->width * track->height; index++) {
		if (!cstrk_terrain_profile(track, (legacy_u16)(index % track->width),
								   (legacy_u16)(index / track->width), NULL)) {
			return cstrk_fail(error, error_size,
							  "Terrain cell %zu does not match an original hill profile", index);
		}
		const struct CSTRK_CELL *cell = &track->cells[index];
		if (cell->surface > CSTRK_TERRAIN_WATER ||
			cell->half_water_corner > CSTRK_WATER_SOUTHWEST ||
			(cell->half_water_corner != CSTRK_WATER_NONE &&
			 (cell->corner_surface > CSTRK_TERRAIN_WATER ||
			  cell->corner_surface == cell->surface))) {
			return cstrk_fail(error, error_size, "Terrain cell %zu has an invalid surface corner",
							  index);
		}
	}
	for (uint32_t index = 0; index < track->object_count; index++) {
		if (!cstrk_validate_object(track, &track->objects[index], index, occupied, error,
								   error_size)) {
			return 0;
		}
	}
	if (!cstrk_notes_valid(track->notes, track->notes_length, error, error_size)) {
		return 0;
	}
	cstrk_success(error, error_size);
	return 1;
}

int cstrk_set_notes(struct CSTRK_TRACK *track, const char *notes, size_t length, char *error,
					size_t error_size)
{
	if (track == NULL || !cstrk_notes_valid(notes, length, error, error_size)) {
		return track == NULL ? cstrk_fail(error, error_size, "Track is required") : 0;
	}
	if (length != 0U) {
		memmove(track->notes, notes, length);
	}
	track->notes[length] = '\0';
	track->notes_length = (uint32_t)length;
	cstrk_success(error, error_size);
	return 1;
}

int cstrk_add_object(struct CSTRK_TRACK *track, const struct CSTRK_OBJECT *object, char *error,
					 size_t error_size)
{
	uint8_t occupied[CSTRK_MAX_OBJECTS] = {0};
	if (object == NULL || !cstrk_validate(track, error, error_size)) {
		return object == NULL ? cstrk_fail(error, error_size, "Object is required") : 0;
	}
	if (track->object_count == (uint32_t)track->width * track->height) {
		return cstrk_fail(error, error_size, "Track has no free object capacity");
	}
	for (uint32_t index = 0; index < track->object_count; index++) {
		if (!cstrk_validate_object(track, &track->objects[index], index, occupied, error,
								   error_size)) {
			return 0;
		}
	}
	/* Copy before realloc so an object already in track->objects is safe input. */
	struct CSTRK_OBJECT next_object = *object;
	if (!cstrk_validate_object(track, &next_object, track->object_count, occupied, error,
							   error_size)) {
		return 0;
	}
	struct CSTRK_OBJECT *next = realloc(track->objects, (track->object_count + 1U) * sizeof(*next));
	if (next == NULL) {
		return cstrk_fail(error, error_size, "Cannot allocate track object");
	}
	track->objects = next;
	track->objects[track->object_count++] = next_object;
	cstrk_success(error, error_size);
	return 1;
}

int cstrk_remove_object(struct CSTRK_TRACK *track, uint32_t index)
{
	if (track == NULL || track->objects == NULL || index >= track->object_count) {
		return 0;
	}
	memmove(track->objects + index, track->objects + index + 1U,
			(size_t)(track->object_count - index - 1U) * sizeof(*track->objects));
	track->object_count--;
	return 1;
}

static size_t cstrk_payload_size(uint16_t width, uint16_t height, uint32_t objects, uint32_t notes)
{
	return ((size_t)width + 1U) * ((size_t)height + 1U) + 2U * (size_t)width * height +
		   (size_t)objects * CSTRK_OBJECT_SIZE + notes;
}

int cstrk_encode(const struct CSTRK_TRACK *track, uint8_t **data, size_t *size, char *error,
				 size_t error_size)
{
	if (data == NULL || size == NULL) {
		return cstrk_fail(error, error_size, "Encoded data and size destinations are required");
	}
	if (!cstrk_validate(track, error, error_size)) {
		return 0;
	}
	size_t payload =
		cstrk_payload_size(track->width, track->height, track->object_count, track->notes_length);
	uint8_t *encoded = calloc(CSTRK_HEADER_SIZE + payload, 1U);
	if (encoded == NULL) {
		return cstrk_fail(error, error_size, "Cannot allocate encoded track");
	}
	memcpy(encoded, cstrk_magic, sizeof(cstrk_magic));
	cstrk_write16(encoded + CSTRK_VERSION_OFFSET, track->version);
	cstrk_write32(encoded + CSTRK_HORIZON_OFFSET, track->horizon);
	cstrk_write16(encoded + 10U, CSTRK_HEADER_SIZE);
	cstrk_write16(encoded + 16U, track->width);
	cstrk_write16(encoded + 18U, track->height);
	cstrk_write32(encoded + 20U, track->notes_length);
	cstrk_write32(encoded + 24U, track->object_count);
	cstrk_write32(encoded + 28U, (uint32_t)payload);
	uint8_t *cursor = encoded + CSTRK_HEADER_SIZE;
	size_t corners = ((size_t)track->width + 1U) * ((size_t)track->height + 1U);
	memcpy(cursor, track->corner_heights, corners);
	cursor += corners;
	for (size_t index = 0; index < (size_t)track->width * track->height; index++) {
		const struct CSTRK_CELL *cell = &track->cells[index];
		*cursor++ = cell->surface;
		legacy_u8 corner = cell->half_water_corner;
		if (corner != CSTRK_WATER_NONE) {
			corner |= (legacy_u8)((cell->corner_surface ^ CSTRK_TERRAIN_WATER)
								  << CSTRK_CORNER_SURFACE_SHIFT);
		}
		*cursor++ = corner;
	}
	for (uint32_t index = 0; index < track->object_count; index++) {
		const struct CSTRK_OBJECT *object = &track->objects[index];
		cstrk_write16(cursor, object->legacy_id);
		cursor[2] = object->x;
		cursor[3] = object->y;
		cursor[4] = object->rotation;
		cursor[5] = object->surface;
		cursor[6] = object->width;
		cursor[7] = object->height;
		cursor[8] = object->occupied_mask;
		cursor[9] = object->base_height;
		cursor[10] = (uint8_t)object->slope_x;
		cursor[11] = (uint8_t)object->slope_y;
		cursor[CSTRK_OBJECT_LOWER_SURFACE_OFFSET] =
			track->version < CSTRK_LOWER_SURFACE_VERSION ? 0 : object->lower_surface;
		cursor += CSTRK_OBJECT_SIZE;
	}
	memcpy(cursor, track->notes, track->notes_length);
	*data = encoded;
	*size = CSTRK_HEADER_SIZE + payload;
	cstrk_success(error, error_size);
	return 1;
}

int cstrk_decode(const uint8_t *data, size_t size, struct CSTRK_TRACK *track, char *error,
				 size_t error_size)
{
	struct CSTRK_TRACK next;
	if (track == NULL || data == NULL || size < CSTRK_HEADER_SIZE) {
		return cstrk_fail(error, error_size, "Track header is missing or truncated");
	}
	legacy_u16 version = cstrk_read16(data + CSTRK_VERSION_OFFSET);
	legacy_u32 horizon = cstrk_read32(data + CSTRK_HORIZON_OFFSET);
	if (memcmp(data, cstrk_magic, sizeof(cstrk_magic)) != 0 ||
		cstrk_read16(data + 10U) != CSTRK_HEADER_SIZE || horizon >= CSTRK_HORIZON_COUNT) {
		return cstrk_fail(error, error_size, "Unknown track magic, header size or horizon");
	}
	if (version < CSTRK_MIN_VERSION) {
		return cstrk_fail(error, error_size,
						  "Unsupported track version %u; minimum supported version is %u",
						  (legacy_uint)version, CSTRK_MIN_VERSION);
	}
	uint16_t width = cstrk_read16(data + 16U);
	uint16_t height = cstrk_read16(data + 18U);
	uint32_t notes_length = cstrk_read32(data + 20U);
	uint32_t objects = cstrk_read32(data + 24U);
	if (!cstrk_dimensions(width, height) || notes_length > CSTRK_MAX_NOTES ||
		objects > (uint32_t)width * height) {
		return cstrk_fail(error, error_size,
						  "Track dimensions, notes length or object count exceed limits");
	}
	size_t payload = cstrk_payload_size(width, height, objects, notes_length);
	if (cstrk_read32(data + 28U) != payload || size != CSTRK_HEADER_SIZE + payload) {
		return cstrk_fail(error, error_size, "Track payload is truncated or has extra bytes");
	}
	cstrk_init(&next);
	if (!cstrk_create(&next, width, height, error, error_size)) {
		return 0;
	}
	/* Validate revision-specific object semantics before upgrading metadata. */
	next.version = version;
	next.horizon = (legacy_u8)horizon;
	const uint8_t *cursor = data + CSTRK_HEADER_SIZE;
	size_t corners = ((size_t)width + 1U) * ((size_t)height + 1U);
	memcpy(next.corner_heights, cursor, corners);
	cursor += corners;
	for (size_t index = 0; index < (size_t)width * height; index++) {
		next.cells[index].surface = *cursor++;
		legacy_u8 corner = *cursor++;
		legacy_u8 direction = corner & CSTRK_CORNER_DIRECTION_MASK;
		legacy_u8 known_bits =
			CSTRK_CORNER_DIRECTION_MASK | (CSTRK_CORNER_SURFACE_MASK << CSTRK_CORNER_SURFACE_SHIFT);
		if ((corner & (legacy_u8)~known_bits) != 0 ||
			(direction == CSTRK_WATER_NONE && corner != CSTRK_WATER_NONE)) {
			cstrk_free(&next);
			return cstrk_fail(error, error_size, "Terrain cell %zu has invalid corner bits", index);
		}
		next.cells[index].half_water_corner = direction;
		next.cells[index].corner_surface =
			((corner >> CSTRK_CORNER_SURFACE_SHIFT) & CSTRK_CORNER_SURFACE_MASK) ^
			CSTRK_TERRAIN_WATER;
	}
	if (objects != 0U) {
		next.objects = calloc(objects, sizeof(*next.objects));
		if (next.objects == NULL) {
			cstrk_free(&next);
			return cstrk_fail(error, error_size, "Cannot allocate decoded track objects");
		}
	}
	next.object_count = objects;
	for (uint32_t index = 0; index < objects; index++) {
		struct CSTRK_OBJECT *object = &next.objects[index];
		legacy_u32 flags = cstrk_read32(cursor + CSTRK_OBJECT_LOWER_SURFACE_OFFSET);
		if ((version < CSTRK_LOWER_SURFACE_VERSION ? flags : flags & ~(legacy_u32)LEGACY_U8_MAX) !=
			0U) {
			cstrk_free(&next);
			return cstrk_fail(error, error_size, "Object %u has unknown reserved flags",
							  (unsigned)index);
		}
		object->legacy_id = cstrk_read16(cursor);
		object->x = cursor[2];
		object->y = cursor[3];
		object->rotation = cursor[4];
		object->surface = cursor[5];
		object->width = cursor[6];
		object->height = cursor[7];
		object->occupied_mask = cursor[8];
		object->base_height = cursor[9];
		object->slope_x = (int8_t)((int)cursor[10] - (cursor[10] >= 128U ? 256 : 0));
		object->slope_y = (int8_t)((int)cursor[11] - (cursor[11] >= 128U ? 256 : 0));
		object->lower_surface = version < CSTRK_LOWER_SURFACE_VERSION &&
										object->legacy_id >= CSTRK_OVERPASS_FIRST_ID &&
										object->legacy_id <= CSTRK_OVERPASS_LAST_ID
									? object->surface
									: cursor[CSTRK_OBJECT_LOWER_SURFACE_OFFSET];
		cursor += CSTRK_OBJECT_SIZE;
	}
	memcpy(next.notes, cursor, notes_length);
	next.notes[notes_length] = '\0';
	next.notes_length = notes_length;
	if (!cstrk_validate(&next, error, error_size)) {
		cstrk_free(&next);
		return 0;
	}
	next.version = version < CSTRK_VERSION ? CSTRK_VERSION : version;
	cstrk_free(track);
	*track = next;
	cstrk_success(error, error_size);
	return 1;
}

int cstrk_validate_bytes(const uint8_t *data, size_t size, char *error, size_t error_size)
{
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	int valid = cstrk_decode(data, size, &track, error, error_size);
	cstrk_free(&track);
	return valid;
}
