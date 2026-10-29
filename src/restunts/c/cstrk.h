#ifndef RESTUNTS_CSTRK_H
#define RESTUNTS_CSTRK_H

/* SDL3 extended-track storage. This module has no game-state or resource dependency. */
#include <stddef.h>
#include <stdint.h>
#include "legacy.h"

#define CSTRK_VERSION 1U
#define CSTRK_VERSION_OFFSET 8U
#define CSTRK_HORIZON_OFFSET 12U
#define CSTRK_HEADER_SIZE 32U
#define CSTRK_OBJECT_SIZE 16U
#define CSTRK_MAX_DIMENSION 90U
#define CSTRK_MAX_HEIGHT 4U
#define CSTRK_MAX_NOTES 8000U
#define CSTRK_MAX_OBJECTS (CSTRK_MAX_DIMENSION * CSTRK_MAX_DIMENSION)
#define CSTRK_MAX_LEGACY_ID 214U
#define CSTRK_HORIZON_COUNT 5U
#define CSTRK_HORIZON_DEFAULT CSTRK_HORIZON_DESERT

enum CSTRK_HORIZON {
	CSTRK_HORIZON_DESERT = 0,
	CSTRK_HORIZON_TROPICAL = 1,
	CSTRK_HORIZON_ALPINE = 2,
	CSTRK_HORIZON_CITY = 3,
	CSTRK_HORIZON_COUNTRY = 4
};

enum CSTRK_TERRAIN_SURFACE {
	CSTRK_TERRAIN_GRASS = 0,
	CSTRK_TERRAIN_ICE = 1,
	CSTRK_TERRAIN_DIRT = 2,
	CSTRK_TERRAIN_WATER = 3
};

enum CSTRK_ROAD_SURFACE { CSTRK_ROAD_PAVED = 0, CSTRK_ROAD_DIRT = 1, CSTRK_ROAD_ICE = 2 };

enum CSTRK_WATER_CORNER {
	CSTRK_WATER_NONE = 0,
	CSTRK_WATER_NORTHWEST = 1,
	CSTRK_WATER_NORTHEAST = 2,
	CSTRK_WATER_SOUTHEAST = 3,
	CSTRK_WATER_SOUTHWEST = 4
};

struct CSTRK_CELL {
	uint8_t surface;
	uint8_t half_water_corner;
};

/* Width, height and occupied_mask describe map-axis cells after rotation.
 * Bit (row * width + column) claims that cell. Unclaimed cells remain usable.
 * Terrain support is the rigid plane base_height + slope_x*x + slope_y*y.
 * Legacy IDs refer to the original game catalog; resource admission is separate. */
struct CSTRK_OBJECT {
	uint16_t legacy_id;
	uint8_t x;
	uint8_t y;
	uint8_t rotation;
	uint8_t surface;
	uint8_t width;
	uint8_t height;
	uint8_t occupied_mask;
	uint8_t base_height;
	int8_t slope_x;
	int8_t slope_y;
};

struct CSTRK_TRACK {
	legacy_u16 version;
	uint16_t width;
	uint16_t height;
	legacy_u8 horizon;
	uint8_t *corner_heights;  /* Row-major (width+1) * (height+1). */
	struct CSTRK_CELL *cells; /* Row-major width * height. */
	struct CSTRK_OBJECT *objects;
	uint32_t object_count;
	uint32_t notes_length;
	char notes[CSTRK_MAX_NOTES + 1U]; /* ASCII text, NUL-terminated in memory only. */
};

/* Initialize once before create/decode; free may be called repeatedly.
 * Mutating operations preserve the destination on failure. All int results
 * are 1 for success and 0 for failure. An optional error buffer receives text.
 * Encode allocates *data using malloc; its caller must free that allocation.
 * Version is positive revision metadata. Decode validates the known layout even
 * for newer revisions; the UI warns when version exceeds CSTRK_VERSION. */
void cstrk_init(struct CSTRK_TRACK *track);
void cstrk_free(struct CSTRK_TRACK *track);
int cstrk_create(struct CSTRK_TRACK *track, uint16_t width, uint16_t height, char *error,
				 size_t error_size);
int cstrk_validate(const struct CSTRK_TRACK *track, char *error, size_t error_size);
int cstrk_set_notes(struct CSTRK_TRACK *track, const char *notes, size_t length, char *error,
					size_t error_size);
int cstrk_add_object(struct CSTRK_TRACK *track, const struct CSTRK_OBJECT *object, char *error,
					 size_t error_size);
int cstrk_remove_object(struct CSTRK_TRACK *track, uint32_t index);
int cstrk_encode(const struct CSTRK_TRACK *track, uint8_t **data, size_t *size, char *error,
				 size_t error_size);
int cstrk_decode(const uint8_t *data, size_t size, struct CSTRK_TRACK *track, char *error,
				 size_t error_size);
int cstrk_validate_bytes(const uint8_t *data, size_t size, char *error, size_t error_size);

#endif
