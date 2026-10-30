#ifndef RESTUNTS_CSRPL_H
#define RESTUNTS_CSRPL_H

#include <stddef.h>
#include <stdint.h>
#include "cstrk.h"
#include "driving_rules.h"

/* Track revisions always advance the replay revision. Increment the physics
 * revision independently when simulation changes affect replay compatibility. */
#define CSRPL_PHYSICS_REVISION 2U
#define CSRPL_VERSION (CSTRK_VERSION + CSRPL_PHYSICS_REVISION)
#define CSRPL_MIN_VERSION CSRPL_VERSION
#define CSRPL_VERSION_OFFSET 8U
#define CSRPL_DRIVING_RULES_OFFSET 26U

#if CSRPL_VERSION > UINT16_MAX
#error "The replay revision must fit in its 16-bit header field."
#endif
#define CSRPL_HEADER_SIZE 64U
#define CSRPL_CAR_ID_SIZE 4U
#define CSRPL_TRACK_NAME_SIZE 9U
#define CSRPL_INPUT_MASK 63U

/* This is an in-memory structure, never a packed on-disk structure. */
struct CSRPL_METADATA {
	legacy_u16 version;
	legacy_u16 driving_rules;
	uint8_t player_car_id[CSRPL_CAR_ID_SIZE];
	uint8_t player_material;
	uint8_t player_transmission;
	uint8_t opponent_type;
	uint8_t opponent_car_id[CSRPL_CAR_ID_SIZE];
	uint8_t opponent_material;
	uint8_t opponent_transmission;
	uint8_t track_name[CSRPL_TRACK_NAME_SIZE];
	uint16_t frames_per_second;
	uint32_t frame_count;
};

/* A successfully decoded view borrows the input buffer, which must stay alive. */
struct CSRPL_VIEW {
	struct CSRPL_METADATA metadata;
	const uint8_t *track_data;
	size_t track_size;
	const uint8_t *inputs;
};

/* Revisions below CSRPL_MIN_VERSION are unsupported. Decode validates the known
 * layout even for newer revisions and preserves the recorded revision and rules.
 * The UI warns about newer revisions and differing driving rules. */

/* Functions return 1 on success and 0 on failure. Output is unchanged on failure.
 * encode allocates *out_data with malloc; the caller must free it. An input is
 * one six-bit control sample for each simulation frame, at 10 or 20 Hz. */
int csrpl_encoded_size(size_t track_size, uint32_t frame_count, size_t *out_size);
int csrpl_decode(const uint8_t *data, size_t size, struct CSRPL_VIEW *out, char *error,
				 size_t error_size);
int csrpl_encode(const struct CSRPL_METADATA *metadata, const uint8_t *track_data,
				 size_t track_size, const uint8_t *inputs, size_t input_count, uint8_t **out_data,
				 size_t *out_size, char *error, size_t error_size);

#endif
