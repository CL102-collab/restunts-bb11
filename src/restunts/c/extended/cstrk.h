#ifndef RESTUNTS_CSTRK_H
#define RESTUNTS_CSTRK_H

/* SDL3 extended-track storage. This module has no game-state or resource dependency. */
#include <stddef.h>
#include <stdint.h>
#include "../legacy.h"

#define CSTRK_VERSION 15U
#define CSTRK_MIN_VERSION 3U
#define CSTRK_LOWER_SURFACE_VERSION 4U
#define CSTRK_SLOPED_STUNT_VERSION 5U
#define CSTRK_GENERIC_SLOPE_VERSION 6U
#define CSTRK_TUNNEL_SLALOM_VERSION 7U
#define CSTRK_ROLL_RAMP_VERSION 8U
#define CSTRK_PINE_GROUP_VERSION 9U
#define CSTRK_PINE_SIX_VERSION 10U
#define CSTRK_WHEAT_FIELD_VERSION 11U
#define CSTRK_TULIP_FIELD_VERSION 12U
#define CSTRK_VINE_FIELD_VERSION 12U
#define CSTRK_CHECKPOINT_VERSION 13U
#define CSTRK_COMBINED_CHECKPOINT_VERSION 14U
#define CSTRK_CUSTOM_HORIZON_VERSION 15U
#define CSTRK_VERSION_OFFSET 8U
#define CSTRK_HORIZON_OFFSET 12U
#define CSTRK_HEADER_SIZE 32U
#define CSTRK_PAYLOAD_SIZE_OFFSET 28U
#define CSTRK_OBJECT_SIZE 16U
#define CSTRK_OBJECT_LOWER_SURFACE_OFFSET 12U
#define CSTRK_OBJECT_TUNNEL_SLALOM_OFFSET 13U
#define CSTRK_OBJECT_ROLL_RAMP_OFFSET 14U
#define CSTRK_OBJECT_PINE_COUNT_OFFSET 15U
#define CSTRK_PINE_TREE_ID 153U
#define CSTRK_WHEAT_FIELD_ID 215U
#define CSTRK_TULIP_FIELD_ID 216U
#define CSTRK_VINE_FIELD_ID 217U
#define CSTRK_BLUE_CHECKPOINT_ID 218U
#define CSTRK_RED_CHECKPOINT_ID 219U
#define CSTRK_COMBINED_CHECKPOINT_ID 220U
#define CSTRK_CHECKPOINT_BLUE_MASK 1U
#define CSTRK_CHECKPOINT_RED_MASK 2U
#define CSTRK_CHECKPOINT_BOTH_MASK (CSTRK_CHECKPOINT_BLUE_MASK | CSTRK_CHECKPOINT_RED_MASK)
#define CSTRK_PINE_GROUP_OLD_COUNT 5U /* Decode-only alias for the six-tree group. */
#define CSTRK_PINE_GROUP_SMALL_COUNT 6U
#define CSTRK_PINE_GROUP_LARGE_COUNT 10U
#define CSTRK_OVERPASS_FIRST_ID 101U
#define CSTRK_OVERPASS_LAST_ID 102U
#define CSTRK_TUNNEL_FIRST_ID 66U
#define CSTRK_TUNNEL_LAST_ID 67U
#define CSTRK_SLALOM_FIRST_ID 115U
#define CSTRK_SLALOM_LAST_ID 116U
#define CSTRK_ROLL_RAMP_FIRST_ID 85U
#define CSTRK_ROLL_RAMP_LAST_ID 86U
#define CSTRK_ROLL_RAMP_TILE_COUNT 2U
#define CSTRK_ROLL_RAMP_OCCUPIED_MASK 3U
#define CSTRK_MAX_DIMENSION 90U
#define CSTRK_MAX_HEIGHT 4U
#define CSTRK_MAX_NOTES 8000U
#define CSTRK_MAX_OBJECTS (CSTRK_MAX_DIMENSION * CSTRK_MAX_DIMENSION)
#define CSTRK_MAX_LEGACY_ID 214U
#define CSTRK_HORIZON_COUNT 5U
#define CSTRK_MAX_HORIZON_NAME 127U
#define CSTRK_CUSTOM_HORIZON_LENGTH_SIZE 2U
#define CSTRK_CUSTOM_HORIZON_TRAILER_MAX_SIZE                                                      \
	(CSTRK_CUSTOM_HORIZON_LENGTH_SIZE + CSTRK_MAX_HORIZON_NAME)
#define CSTRK_HORIZON_DEFAULT CSTRK_HORIZON_DESERT
#define CSTRK_CORNER_DIRECTION_MASK 7U
#define CSTRK_CORNER_SURFACE_SHIFT 3U
#define CSTRK_CORNER_SURFACE_MASK 3U
#define CSTRK_TILE_WORLD_SIZE 1024
#define CSTRK_LEVEL_WORLD_HEIGHT 450

/* Corner bits are ordered clockwise from northwest, matching the wire grid. */
enum CSTRK_TERRAIN_PROFILE_ID {
	CSTRK_TERRAIN_PROFILE_FLAT = 0,
	CSTRK_TERRAIN_PROFILE_RAISED = 6,
	CSTRK_TERRAIN_PROFILE_NORTH = 7,
	CSTRK_TERRAIN_PROFILE_WEST = 8,
	CSTRK_TERRAIN_PROFILE_SOUTH = 9,
	CSTRK_TERRAIN_PROFILE_EAST = 10,
	CSTRK_TERRAIN_PROFILE_OUTER_NORTHWEST = 11,
	CSTRK_TERRAIN_PROFILE_OUTER_SOUTHWEST = 12,
	CSTRK_TERRAIN_PROFILE_OUTER_SOUTHEAST = 13,
	CSTRK_TERRAIN_PROFILE_OUTER_NORTHEAST = 14,
	CSTRK_TERRAIN_PROFILE_INNER_SOUTHEAST = 15,
	CSTRK_TERRAIN_PROFILE_INNER_NORTHEAST = 16,
	CSTRK_TERRAIN_PROFILE_INNER_NORTHWEST = 17,
	CSTRK_TERRAIN_PROFILE_INNER_SOUTHWEST = 18
};

struct CSTRK_TERRAIN_PROFILE {
	legacy_u8 terrain_id;
	legacy_u8 base_height;
	legacy_u8 high_mask;
};

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
	uint8_t half_water_corner; /* Legacy name for the mixed-material corner direction. */
	legacy_u8 corner_surface;  /* Material in that corner; ignored when there is no corner. */
};

/* Width, height and occupied_mask describe map-axis cells after rotation.
 * Bit (row * width + column) claims that cell. Unclaimed cells remain usable.
 * Vertices touching claimed cells lie on the rigid support plane
 * base_height + slope_x*x + slope_y*y; other vertices may form hills.
 * IDs refer to the original catalog, extended scenery or a checkpoint gate;
 * resource admission is separate. */
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
	legacy_u8 lower_surface; /* Overpass road underneath; paved for every other object. */
	legacy_u8 tunnel_slalom; /* Slalom with the tunnel shell, 0 or 1 (v7+). */
	legacy_u8 roll_ramp;	 /* Two-tile barrel-roll launch ramp, 0 or 1 (v8+). */
	legacy_u8 pine_count;	 /* Single 0; group 6 (v10+) or 10 (v9+). */
};

struct CSTRK_TRACK {
	legacy_u16 version;
	uint16_t width;
	uint16_t height;
	legacy_u8 horizon;
	/* Optional UTF-8 basename including .png; horizon remains the built-in fallback. */
	legacy_s8 custom_horizon[CSTRK_MAX_HORIZON_NAME + 1U];
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
 * Revisions below CSTRK_MIN_VERSION are unsupported. Decode validates the known
 * layout even for newer revisions; the UI warns when version exceeds CSTRK_VERSION.
 * Supported older tracks upgrade to the current revision when decoded; future
 * revisions retain their numeric metadata. Encode preserves the stored revision. */
/* Extended-only, drive-through crop scenery; not native catalog indexes. */
legacy_int cstrk_is_crop_field(legacy_u16 id);
/* Extended road gates use their authored color and independent rotation. */
legacy_int cstrk_is_checkpoint(legacy_u16 id);
/* Colors whose complete route requires this gate; zero for every other ID. */
legacy_u8 cstrk_checkpoint_colors(legacy_u16 id);
void cstrk_init(struct CSTRK_TRACK *track);
void cstrk_free(struct CSTRK_TRACK *track);
int cstrk_create(struct CSTRK_TRACK *track, uint16_t width, uint16_t height, char *error,
				 size_t error_size);
int cstrk_validate(const struct CSTRK_TRACK *track, char *error, size_t error_size);
/* Earliest revision admitting this support plane; historical eligibility only.
 * Current slope geometry uses one shared transform for every catalog family. */
legacy_u16 cstrk_object_slope_version(const struct CSTRK_OBJECT *object);
/* Classify an original flat, straight slope, or inner/outer hill corner at any
 * supported base level. Returns zero for cliffs, saddles and diagonal ramps.
 * A non-NULL profile receives the original terrain ID and its minimum level. */
legacy_int cstrk_terrain_profile(const struct CSTRK_TRACK *track, legacy_u16 x, legacy_u16 y,
								 struct CSTRK_TERRAIN_PROFILE *profile);
int cstrk_set_notes(struct CSTRK_TRACK *track, const char *notes, size_t length, char *error,
					size_t error_size);
/* Empty selects the built-in horizon. Custom names are UTF-8 PNG basenames,
 * never paths. The setter preserves the track on failure and permits aliases. */
legacy_int cstrk_custom_horizon_name_valid(const legacy_s8 *name, size_t length);
legacy_int cstrk_set_custom_horizon(struct CSTRK_TRACK *track, const legacy_s8 *name, size_t length,
									legacy_char *error, size_t error_size);
int cstrk_add_object(struct CSTRK_TRACK *track, const struct CSTRK_OBJECT *object, char *error,
					 size_t error_size);
int cstrk_remove_object(struct CSTRK_TRACK *track, uint32_t index);
int cstrk_encode(const struct CSTRK_TRACK *track, uint8_t **data, size_t *size, char *error,
				 size_t error_size);
int cstrk_decode(const uint8_t *data, size_t size, struct CSTRK_TRACK *track, char *error,
				 size_t error_size);
int cstrk_validate_bytes(const uint8_t *data, size_t size, char *error, size_t error_size);

#endif
