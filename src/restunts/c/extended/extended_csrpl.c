#include "csrpl.h"
#include "cstrk.h"

#include <stdlib.h>
#include <string.h>

static const uint8_t csrpl_signature[8] = {'C', 'S', 'R', 'P', 'L', '\r', '\n', 26};

static int csrpl_fail(char *error, size_t size, const char *message)
{
	if (error != NULL && size != 0U) {
		size_t length = strlen(message);
		if (length >= size) {
			length = size - 1U;
		}
		memcpy(error, message, length);
		error[length] = '\0';
	}
	return 0;
}

static uint16_t csrpl_read16(const uint8_t *data)
{
	return (uint16_t)((uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8));
}

static uint32_t csrpl_read32(const uint8_t *data)
{
	return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) |
		   ((uint32_t)data[3] << 24);
}

static void csrpl_write16(uint8_t *data, uint16_t value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8);
}

static void csrpl_write32(uint8_t *data, uint32_t value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8);
	data[2] = (uint8_t)(value >> 16);
	data[3] = (uint8_t)(value >> 24);
}

legacy_int csrpl_encoded_size_with_flags(size_t track_size, legacy_u32 frame_count,
										 legacy_u32 flags, size_t *out_size)
{
	if (out_size == NULL || track_size == 0U || track_size > LEGACY_U32_MAX ||
		(flags & (legacy_u32)~CSRPL_KNOWN_FLAGS) != 0U) {
		return 0;
	}
	legacy_u64 stride =
		1U + ((flags & CSRPL_FLAG_OPPONENT_COMMANDS) != 0U ? CSRPL_OPPONENT_COMMAND_SIZE : 0U);
	legacy_u64 size = (legacy_u64)CSRPL_HEADER_SIZE + track_size + stride * frame_count;
	if (size > SIZE_MAX) {
		return 0;
	}
	*out_size = (size_t)size;
	return 1;
}

legacy_int csrpl_encoded_size(size_t track_size, legacy_u32 frame_count, size_t *out_size)
{
	return csrpl_encoded_size_with_flags(track_size, frame_count, 0U, out_size);
}

static size_t csrpl_track_name_size(legacy_u16 version)
{
	if (version < CSRPL_LONG_TRACK_NAME_VERSION) {
		return CSRPL_LEGACY_TRACK_NAME_SIZE;
	}
	if (version < CSRPL_FULL_TRACK_NAME_VERSION) {
		return CSRPL_V17_TRACK_NAME_SIZE;
	}
	return CSRPL_TRACK_NAME_SIZE;
}

static int csrpl_valid_metadata(const struct CSRPL_METADATA *metadata, char *error,
								size_t error_size)
{
	if (metadata->version < CSRPL_MIN_VERSION) {
		return csrpl_fail(error, error_size, "Unsupported Chocolate Stunts replay version.");
	}
	for (size_t index = csrpl_track_name_size(metadata->version); index < CSRPL_TRACK_NAME_SIZE;
		 index++) {
		if (metadata->track_name[index] != 0U) {
			return csrpl_fail(error, error_size,
							  "Track name does not fit this older replay revision.");
		}
	}
	if ((metadata->driving_rules & (legacy_u16)~DRIVING_RULE_KNOWN_MASK) != 0U) {
		return csrpl_fail(error, error_size, "Replay driving rules contain unsupported flags.");
	}
	if (metadata->frames_per_second != 10U && metadata->frames_per_second != 20U) {
		return csrpl_fail(error, error_size, "Replay frame rate must be 10 or 20 Hz.");
	}
	if ((metadata->flags & (legacy_u32)~CSRPL_KNOWN_FLAGS) != 0U ||
		(metadata->version < CSRPL_OPPONENT_COMMAND_VERSION && metadata->flags != 0U)) {
		return csrpl_fail(error, error_size, "Unsupported replay flags.");
	}
	legacy_int extended_opponent = metadata->opponent_type > CSRPL_CLASSIC_OPPONENT_LAST;
	if (extended_opponent) {
		if (metadata->version < CSRPL_OPPONENT_COMMAND_VERSION ||
			(metadata->flags & CSRPL_FLAG_OPPONENT_COMMANDS) == 0U) {
			return csrpl_fail(error, error_size, "Extended opponent requires recorded commands.");
		}
		for (size_t index = 0; index < CSRPL_OPPONENT_ID_SIZE; index++) {
			legacy_u8 value = metadata->opponent_id[index];
			if (!((value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9'))) {
				return csrpl_fail(error, error_size, "Invalid four-character opponent identifier.");
			}
		}
	} else {
		if ((metadata->flags & CSRPL_FLAG_OPPONENT_COMMANDS) != 0U) {
			return csrpl_fail(error, error_size, "Classic opponent cannot have recorded commands.");
		}
		for (size_t index = 0; index < CSRPL_OPPONENT_ID_SIZE; index++) {
			if (metadata->opponent_id[index] != 0U) {
				return csrpl_fail(error, error_size, "Classic opponent cannot have an identifier.");
			}
		}
	}
	if (metadata->player_transmission > 1U || metadata->opponent_transmission > 1U ||
		metadata->opponent_type > LEGACY_S8_MAX) {
		return csrpl_fail(error, error_size, "Invalid replay transmission or opponent.");
	}
	return 1;
}

static int csrpl_valid_inputs(const uint8_t *inputs, size_t size, char *error, size_t error_size)
{
	if (size != 0U && inputs == NULL) {
		return csrpl_fail(error, error_size, "Missing replay inputs.");
	}
	for (size_t index = 0; index < size; index++) {
		if ((inputs[index] & (uint8_t)~CSRPL_INPUT_MASK) != 0U) {
			return csrpl_fail(error, error_size,
							  "Replay input contains unsupported control flags.");
		}
	}
	return 1;
}

static legacy_int csrpl_valid_track(legacy_u32 flags, const legacy_u8 *track, size_t size,
									legacy_char *error, size_t error_size)
{
	if ((flags & CSRPL_FLAG_CLASSIC_TRACK) != 0U) {
		return size == CSRPL_CLASSIC_TRACK_SIZE
				   ? 1
				   : csrpl_fail(error, error_size, "Invalid embedded classic track size.");
	}
	return cstrk_validate_bytes(track, size, error, error_size);
}

static legacy_int csrpl_valid_commands(const legacy_u8 *commands, size_t count, legacy_char *error,
									   size_t error_size)
{
	if (count != 0U && commands == NULL) {
		return csrpl_fail(error, error_size, "Missing opponent commands.");
	}
	for (size_t index = 0; index < count; index++) {
		if (!csrpl_opponent_command_valid(commands + index * CSRPL_OPPONENT_COMMAND_SIZE)) {
			return csrpl_fail(error, error_size, "Invalid opponent command.");
		}
	}
	return 1;
}

int csrpl_decode(const uint8_t *data, size_t size, struct CSRPL_VIEW *out, char *error,
				 size_t error_size)
{
	if (data == NULL || out == NULL || size < CSRPL_HEADER_SIZE) {
		return csrpl_fail(error, error_size, "Truncated replay header.");
	}
	if (memcmp(data, csrpl_signature, sizeof(csrpl_signature)) != 0) {
		return csrpl_fail(error, error_size, "Not a Chocolate Stunts replay.");
	}
	legacy_u16 version = csrpl_read16(data + CSRPL_VERSION_OFFSET);
	if (version < CSRPL_MIN_VERSION || csrpl_read16(data + 10U) != CSRPL_HEADER_SIZE) {
		return csrpl_fail(error, error_size, "Unsupported Chocolate Stunts replay version.");
	}
	size_t track_name_size = csrpl_track_name_size(version);
	size_t opponent_id_offset = CSRPL_TRACK_NAME_OFFSET + track_name_size;
	legacy_int has_identifier =
		version >= CSRPL_OPPONENT_COMMAND_VERSION && data[34] > CSRPL_CLASSIC_OPPONENT_LAST;
	size_t reserved_offset = opponent_id_offset + (has_identifier ? CSRPL_OPPONENT_ID_SIZE : 0U);
	for (size_t index = reserved_offset; index < CSRPL_HEADER_SIZE; index++) {
		if (data[index] != 0U) {
			return csrpl_fail(error, error_size, "Nonzero reserved replay bytes.");
		}
	}
	struct CSRPL_VIEW view;
	memset(&view, 0, sizeof(view));
	view.metadata.version = version;
	view.metadata.flags = csrpl_read32(data + 12U);
	if (has_identifier) {
		memcpy(view.metadata.opponent_id, data + opponent_id_offset, CSRPL_OPPONENT_ID_SIZE);
	}
	view.track_size = csrpl_read32(data + 16U);
	view.metadata.frame_count = csrpl_read32(data + 20U);
	view.metadata.frames_per_second = csrpl_read16(data + 24U);
	view.metadata.driving_rules = csrpl_read16(data + CSRPL_DRIVING_RULES_OFFSET);
	memcpy(view.metadata.player_car_id, data + 28U, CSRPL_CAR_ID_SIZE);
	view.metadata.player_material = data[32];
	view.metadata.player_transmission = data[33];
	view.metadata.opponent_type = data[34];
	memcpy(view.metadata.opponent_car_id, data + 35U, CSRPL_CAR_ID_SIZE);
	view.metadata.opponent_material = data[39];
	view.metadata.opponent_transmission = data[40];
	memcpy(view.metadata.track_name, data + CSRPL_TRACK_NAME_OFFSET, track_name_size);
	size_t expected_size;
	if (!csrpl_encoded_size_with_flags(view.track_size, view.metadata.frame_count,
									   view.metadata.flags, &expected_size) ||
		expected_size != size) {
		return csrpl_fail(error, error_size,
						  "Replay length does not match its track and frame counts.");
	}
	if (!csrpl_valid_metadata(&view.metadata, error, error_size)) {
		return 0;
	}
	view.track_data = data + CSRPL_HEADER_SIZE;
	view.inputs = view.track_data + view.track_size;
	view.opponent_commands = (view.metadata.flags & CSRPL_FLAG_OPPONENT_COMMANDS) != 0U
								 ? view.inputs + view.metadata.frame_count
								 : NULL;
	if (!csrpl_valid_track(view.metadata.flags, view.track_data, view.track_size, error,
						   error_size) ||
		!csrpl_valid_inputs(view.inputs, view.metadata.frame_count, error, error_size) ||
		!csrpl_valid_commands(view.opponent_commands,
							  (view.metadata.flags & CSRPL_FLAG_OPPONENT_COMMANDS) != 0U
								  ? view.metadata.frame_count
								  : 0U,
							  error, error_size)) {
		return 0;
	}
	*out = view;
	if (error != NULL && error_size != 0U) {
		error[0] = '\0';
	}
	return 1;
}

legacy_int csrpl_encode_with_commands(const struct CSRPL_METADATA *metadata,
									  const legacy_u8 *track_data, size_t track_size,
									  const legacy_u8 *inputs, size_t input_count,
									  const legacy_u8 *commands, size_t command_count,
									  legacy_u8 **out_data, size_t *out_size, legacy_char *error,
									  size_t error_size)
{
	size_t size;
	if (metadata == NULL || track_data == NULL || out_data == NULL || out_size == NULL ||
		!csrpl_encoded_size_with_flags(track_size, metadata->frame_count, metadata->flags, &size)) {
		return csrpl_fail(error, error_size, "Invalid replay output or size.");
	}
	if ((uint64_t)input_count != (uint64_t)metadata->frame_count) {
		return csrpl_fail(error, error_size, "Replay input count does not match its frame count.");
	}
	size_t expected_commands =
		(metadata->flags & CSRPL_FLAG_OPPONENT_COMMANDS) != 0U ? input_count : 0U;
	if (command_count != expected_commands) {
		return csrpl_fail(error, error_size,
						  "Replay opponent command count does not match frames.");
	}
	if (!csrpl_valid_metadata(metadata, error, error_size) ||
		!csrpl_valid_track(metadata->flags, track_data, track_size, error, error_size) ||
		!csrpl_valid_inputs(inputs, input_count, error, error_size) ||
		!csrpl_valid_commands(commands, command_count, error, error_size)) {
		return 0;
	}
	uint8_t *data = (uint8_t *)calloc(1U, size);
	if (data == NULL) {
		return csrpl_fail(error, error_size, "Not enough memory for replay.");
	}
	memcpy(data, csrpl_signature, sizeof(csrpl_signature));
	csrpl_write16(data + CSRPL_VERSION_OFFSET, metadata->version);
	csrpl_write16(data + 10U, CSRPL_HEADER_SIZE);
	csrpl_write32(data + 12U, metadata->flags);
	csrpl_write32(data + 16U, (uint32_t)track_size);
	csrpl_write32(data + 20U, metadata->frame_count);
	csrpl_write16(data + 24U, metadata->frames_per_second);
	csrpl_write16(data + CSRPL_DRIVING_RULES_OFFSET, metadata->driving_rules);
	memcpy(data + 28U, metadata->player_car_id, CSRPL_CAR_ID_SIZE);
	data[32] = metadata->player_material;
	data[33] = metadata->player_transmission;
	data[34] = metadata->opponent_type;
	size_t track_name_size = csrpl_track_name_size(metadata->version);
	if (metadata->opponent_type > CSRPL_CLASSIC_OPPONENT_LAST) {
		memcpy(data + CSRPL_TRACK_NAME_OFFSET + track_name_size, metadata->opponent_id,
			   CSRPL_OPPONENT_ID_SIZE);
	}
	memcpy(data + 35U, metadata->opponent_car_id, CSRPL_CAR_ID_SIZE);
	data[39] = metadata->opponent_material;
	data[40] = metadata->opponent_transmission;
	memcpy(data + CSRPL_TRACK_NAME_OFFSET, metadata->track_name, track_name_size);
	memcpy(data + CSRPL_HEADER_SIZE, track_data, track_size);
	if (input_count != 0U) {
		memcpy(data + CSRPL_HEADER_SIZE + track_size, inputs, input_count);
	}
	if (command_count != 0U) {
		memcpy(data + CSRPL_HEADER_SIZE + track_size + input_count, commands,
			   command_count * CSRPL_OPPONENT_COMMAND_SIZE);
	}
	*out_data = data;
	*out_size = size;
	if (error != NULL && error_size != 0U) {
		error[0] = '\0';
	}
	return 1;
}

legacy_int csrpl_encode(const struct CSRPL_METADATA *metadata, const legacy_u8 *track_data,
						size_t track_size, const legacy_u8 *inputs, size_t input_count,
						legacy_u8 **out_data, size_t *out_size, legacy_char *error,
						size_t error_size)
{
	return csrpl_encode_with_commands(metadata, track_data, track_size, inputs, input_count, NULL,
									  0U, out_data, out_size, error, error_size);
}
