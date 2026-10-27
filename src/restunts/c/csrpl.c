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

int csrpl_encoded_size(size_t track_size, uint32_t frame_count, size_t *out_size)
{
	if (out_size == NULL || track_size == 0U || track_size > UINT32_MAX ||
		track_size > SIZE_MAX - CSRPL_HEADER_SIZE) {
		return 0;
	}
	size_t size = CSRPL_HEADER_SIZE + track_size;
	if ((uint64_t)frame_count > (uint64_t)(SIZE_MAX - size)) {
		return 0;
	}
	*out_size = size + (size_t)frame_count;
	return 1;
}

static int csrpl_valid_metadata(const struct CSRPL_METADATA *metadata, char *error,
								 size_t error_size)
{
	if (metadata->frames_per_second != 10U && metadata->frames_per_second != 20U) {
		return csrpl_fail(error, error_size, "Replay frame rate must be 10 or 20 Hz.");
	}
	if (metadata->player_transmission > 1U || metadata->opponent_transmission > 1U ||
		metadata->opponent_type > 6U) {
		return csrpl_fail(error, error_size, "Invalid replay transmission or opponent.");
	}
	return 1;
}

static int csrpl_valid_inputs(const uint8_t *inputs, size_t size, char *error,
							   size_t error_size)
{
	if (size != 0U && inputs == NULL) {
		return csrpl_fail(error, error_size, "Missing replay inputs.");
	}
	for (size_t index = 0; index < size; index++) {
		if ((inputs[index] & (uint8_t)~CSRPL_INPUT_MASK) != 0U) {
			return csrpl_fail(error, error_size, "Replay input contains unsupported control flags.");
		}
	}
	return 1;
}

int csrpl_decode(const uint8_t *data, size_t size, struct CSRPL_VIEW *out,
				 char *error, size_t error_size)
{
	if (data == NULL || out == NULL || size < CSRPL_HEADER_SIZE) {
		return csrpl_fail(error, error_size, "Truncated replay header.");
	}
	if (memcmp(data, csrpl_signature, sizeof(csrpl_signature)) != 0) {
		return csrpl_fail(error, error_size, "Not a Chocolate Stunts replay.");
	}
	if (csrpl_read16(data + 8U) != CSRPL_VERSION ||
		csrpl_read16(data + 10U) != CSRPL_HEADER_SIZE) {
		return csrpl_fail(error, error_size, "Unsupported Chocolate Stunts replay version.");
	}
	if (csrpl_read32(data + 12U) != 0U || csrpl_read16(data + 26U) != 0U) {
		return csrpl_fail(error, error_size, "Unsupported replay flags or reserved fields.");
	}
	for (size_t index = 50U; index < CSRPL_HEADER_SIZE; index++) {
		if (data[index] != 0U) {
			return csrpl_fail(error, error_size, "Nonzero reserved replay bytes.");
		}
	}
	struct CSRPL_VIEW view;
	memset(&view, 0, sizeof(view));
	view.track_size = csrpl_read32(data + 16U);
	view.metadata.frame_count = csrpl_read32(data + 20U);
	view.metadata.frames_per_second = csrpl_read16(data + 24U);
	memcpy(view.metadata.player_car_id, data + 28U, CSRPL_CAR_ID_SIZE);
	view.metadata.player_material = data[32];
	view.metadata.player_transmission = data[33];
	view.metadata.opponent_type = data[34];
	memcpy(view.metadata.opponent_car_id, data + 35U, CSRPL_CAR_ID_SIZE);
	view.metadata.opponent_material = data[39];
	view.metadata.opponent_transmission = data[40];
	memcpy(view.metadata.track_name, data + 41U, CSRPL_TRACK_NAME_SIZE);
	size_t expected_size;
	if (!csrpl_encoded_size(view.track_size, view.metadata.frame_count, &expected_size) ||
		expected_size != size) {
		return csrpl_fail(error, error_size, "Replay length does not match its track and frame counts.");
	}
	if (!csrpl_valid_metadata(&view.metadata, error, error_size)) {
		return 0;
	}
	view.track_data = data + CSRPL_HEADER_SIZE;
	view.inputs = view.track_data + view.track_size;
	if (!cstrk_validate_bytes(view.track_data, view.track_size, error, error_size) ||
		!csrpl_valid_inputs(view.inputs, view.metadata.frame_count, error, error_size)) {
		return 0;
	}
	*out = view;
	if (error != NULL && error_size != 0U) {
		error[0] = '\0';
	}
	return 1;
}

int csrpl_encode(const struct CSRPL_METADATA *metadata, const uint8_t *track_data,
				 size_t track_size, const uint8_t *inputs, size_t input_count,
				 uint8_t **out_data, size_t *out_size, char *error, size_t error_size)
{
	size_t size;
	if (metadata == NULL || track_data == NULL || out_data == NULL || out_size == NULL ||
		!csrpl_encoded_size(track_size, metadata->frame_count, &size)) {
		return csrpl_fail(error, error_size, "Invalid replay output or size.");
	}
	if ((uint64_t)input_count != (uint64_t)metadata->frame_count) {
		return csrpl_fail(error, error_size, "Replay input count does not match its frame count.");
	}
	if (!csrpl_valid_metadata(metadata, error, error_size) ||
		!cstrk_validate_bytes(track_data, track_size, error, error_size) ||
		!csrpl_valid_inputs(inputs, input_count, error, error_size)) {
		return 0;
	}
	uint8_t *data = (uint8_t *)calloc(1U, size);
	if (data == NULL) {
		return csrpl_fail(error, error_size, "Not enough memory for replay.");
	}
	memcpy(data, csrpl_signature, sizeof(csrpl_signature));
	csrpl_write16(data + 8U, CSRPL_VERSION);
	csrpl_write16(data + 10U, CSRPL_HEADER_SIZE);
	csrpl_write32(data + 16U, (uint32_t)track_size);
	csrpl_write32(data + 20U, metadata->frame_count);
	csrpl_write16(data + 24U, metadata->frames_per_second);
	memcpy(data + 28U, metadata->player_car_id, CSRPL_CAR_ID_SIZE);
	data[32] = metadata->player_material;
	data[33] = metadata->player_transmission;
	data[34] = metadata->opponent_type;
	memcpy(data + 35U, metadata->opponent_car_id, CSRPL_CAR_ID_SIZE);
	data[39] = metadata->opponent_material;
	data[40] = metadata->opponent_transmission;
	memcpy(data + 41U, metadata->track_name, CSRPL_TRACK_NAME_SIZE);
	memcpy(data + CSRPL_HEADER_SIZE, track_data, track_size);
	if (input_count != 0U) {
		memcpy(data + CSRPL_HEADER_SIZE + track_size, inputs, input_count);
	}
	*out_data = data;
	*out_size = size;
	if (error != NULL && error_size != 0U) {
		error[0] = '\0';
	}
	return 1;
}
