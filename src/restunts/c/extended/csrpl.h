#ifndef RESTUNTS_CSRPL_H
#define RESTUNTS_CSRPL_H

#include <stddef.h>
#include <stdint.h>
#include "cstrk.h"
#include "../driving_rules.h"

/* Track revisions always advance the replay revision. Increment the physics
 * revision independently when simulation changes affect replay compatibility;
 * filename metadata changes have their own independent revision offset. */
#define CSRPL_PHYSICS_REVISION 4U
#define CSRPL_FILENAME_REVISION 1U
#define CSRPL_OPPONENT_REVISION 1U
#define CSRPL_VERSION                                                                              \
	(CSTRK_VERSION + CSRPL_PHYSICS_REVISION + CSRPL_FILENAME_REVISION + CSRPL_OPPONENT_REVISION)
/* Compatibility cutoffs remain fixed when a later track revision advances. */
#define CSRPL_MIN_VERSION 11U
#define CSRPL_FIXED_POINT_SOLID_VERSION 12U
#define CSRPL_LONG_TRACK_NAME_VERSION 17U
#define CSRPL_OPPONENT_COMMAND_VERSION 21U
#define CSRPL_VERSION_OFFSET 8U
#define CSRPL_DRIVING_RULES_OFFSET 26U

#if CSRPL_VERSION > UINT16_MAX
#error "The replay revision must fit in its 16-bit header field."
#endif
#define CSRPL_HEADER_SIZE 64U
#define CSRPL_CAR_ID_SIZE 4U
#define CSRPL_TRACK_NAME_OFFSET 41U
#define CSRPL_LEGACY_TRACK_NAME_SIZE 9U
#define CSRPL_TRACK_NAME_SIZE 13U
#define CSRPL_INPUT_MASK 63U
#define CSRPL_CLASSIC_OPPONENT_LAST 6U
#define CSRPL_UNIT_OPPONENT_TYPE 7U
#define CSRPL_OPPONENT_ID_SIZE 4U
#define CSRPL_OPPONENT_ID_OFFSET 54U
#define CSRPL_FLAG_CLASSIC_TRACK 1U
#define CSRPL_FLAG_OPPONENT_COMMANDS 2U
#define CSRPL_KNOWN_FLAGS (CSRPL_FLAG_CLASSIC_TRACK | CSRPL_FLAG_OPPONENT_COMMANDS)
#define CSRPL_CLASSIC_TRACK_SIZE 1802U
#define CSRPL_OPPONENT_COMMAND_SIZE 8U

#define CSRPL_OPPONENT_STEERING_LIMIT 240
#define CSRPL_OPPONENT_ROUTE_POINT_LIMIT 255U

/* Explicit command serialization keeps structure padding off the wire. */
struct CSRPL_OPPONENT_COMMAND {
	legacy_u8 input;
	legacy_u8 flags;
	legacy_s16 steering;
	legacy_s16 route_index;
	legacy_u16 route_point;
};

static inline void csrpl_opponent_command_read(const legacy_u8 *wire,
											   struct CSRPL_OPPONENT_COMMAND *command)
{
	command->input = wire[0];
	command->flags = wire[1];
	command->steering = LEGACY_READ_S16_LE(wire + 2U);
	command->route_index = LEGACY_READ_S16_LE(wire + 4U);
	command->route_point = LEGACY_READ_U16_LE(wire + 6U);
}

static inline void csrpl_opponent_command_write(legacy_u8 *wire,
												const struct CSRPL_OPPONENT_COMMAND *command)
{
	wire[0] = command->input;
	wire[1] = command->flags;
	LEGACY_WRITE_U16_LE(wire + 2U, (legacy_u16)command->steering);
	LEGACY_WRITE_U16_LE(wire + 4U, (legacy_u16)command->route_index);
	LEGACY_WRITE_U16_LE(wire + 6U, command->route_point);
}

static inline legacy_int csrpl_opponent_command_valid(const legacy_u8 *wire)
{
	struct CSRPL_OPPONENT_COMMAND command;
	csrpl_opponent_command_read(wire, &command);
	return (command.input & (legacy_u8)~CSRPL_INPUT_MASK) == 0U && command.flags == 0U &&
		   command.steering >= -CSRPL_OPPONENT_STEERING_LIMIT &&
		   command.steering <= CSRPL_OPPONENT_STEERING_LIMIT && command.route_index >= 0 &&
		   command.route_point <= CSRPL_OPPONENT_ROUTE_POINT_LIMIT;
}

/* This is an in-memory structure, never a packed on-disk structure. */
struct CSRPL_METADATA {
	legacy_u16 version;
	legacy_u16 driving_rules;
	legacy_u32 flags;
	legacy_u8 opponent_id[CSRPL_OPPONENT_ID_SIZE];
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
	const legacy_u8 *opponent_commands;
};

/* Revisions below CSRPL_MIN_VERSION are unsupported. Decode validates the known
 * layout even for newer revisions and preserves the recorded revision and rules.
 * Older track-name fields are zero-extended in memory; encoding an older revision
 * rejects nonzero name bytes beyond its wire field rather than truncating them.
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

/* Extended opponents append one fixed-size command per frame after the player
 * stream. The wire bytes are independent of native structure padding. */
legacy_int csrpl_encoded_size_with_flags(size_t track_size, legacy_u32 frame_count,
										 legacy_u32 flags, size_t *out_size);
legacy_int csrpl_encode_with_commands(const struct CSRPL_METADATA *metadata,
									  const legacy_u8 *track_data, size_t track_size,
									  const legacy_u8 *inputs, size_t input_count,
									  const legacy_u8 *commands, size_t command_count,
									  legacy_u8 **out_data, size_t *out_size, legacy_char *error,
									  size_t error_size);

#endif
