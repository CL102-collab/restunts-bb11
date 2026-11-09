#include "extended_cstrk_horizon.h"

#include <stdio.h>
#include <string.h>

#define HORIZON_ASCII_CONTROL_LAST 0x1fU
#define HORIZON_UNICODE_CONTROL_FIRST 0x7fU
#define HORIZON_UNICODE_CONTROL_LAST 0x9fU
#define HORIZON_UTF8_CONTINUATION 0x80U
#define HORIZON_UTF8_CONTINUATION_MASK 0xc0U
#define HORIZON_UTF8_PAYLOAD_MASK 0x3fU
#define HORIZON_UTF8_PAYLOAD_BITS 6U
#define HORIZON_UTF8_TWO_FIRST 0xc2U
#define HORIZON_UTF8_TWO_LAST 0xdfU
#define HORIZON_UTF8_TWO_MASK 0x1fU
#define HORIZON_UTF8_TWO_MINIMUM 0x80U
#define HORIZON_UTF8_THREE_FIRST 0xe0U
#define HORIZON_UTF8_THREE_LAST 0xefU
#define HORIZON_UTF8_THREE_MASK 0x0fU
#define HORIZON_UTF8_THREE_MINIMUM 0x800U
#define HORIZON_UTF8_FOUR_FIRST 0xf0U
#define HORIZON_UTF8_FOUR_LAST 0xf4U
#define HORIZON_UTF8_FOUR_MASK 0x07U
#define HORIZON_UTF8_FOUR_MINIMUM 0x10000UL
#define HORIZON_UNICODE_SURROGATE_FIRST 0xd800UL
#define HORIZON_UNICODE_SURROGATE_LAST 0xdfffUL
#define HORIZON_UNICODE_MAXIMUM 0x10ffffUL
#define HORIZON_PNG_EXTENSION_LENGTH 4U

static legacy_int horizon_fail(legacy_char *error, size_t size, const legacy_char *message)
{
	if (error && size) {
		snprintf(error, size, "%s", message);
	}
	return 0;
}

static legacy_int horizon_utf8_scalar(const legacy_s8 *name, size_t length, size_t *offset,
									  legacy_u32 *scalar)
{
	legacy_u8 first = (legacy_u8)name[(*offset)++];
	legacy_u8 continuation = 0;
	legacy_u32 minimum = 0;
	*scalar = first;
	if (first < HORIZON_UTF8_CONTINUATION) {
		return 1;
	}
	if (first >= HORIZON_UTF8_TWO_FIRST && first <= HORIZON_UTF8_TWO_LAST) {
		continuation = 1U;
		minimum = HORIZON_UTF8_TWO_MINIMUM;
		*scalar &= HORIZON_UTF8_TWO_MASK;
	} else if (first >= HORIZON_UTF8_THREE_FIRST && first <= HORIZON_UTF8_THREE_LAST) {
		continuation = 2U;
		minimum = HORIZON_UTF8_THREE_MINIMUM;
		*scalar &= HORIZON_UTF8_THREE_MASK;
	} else if (first >= HORIZON_UTF8_FOUR_FIRST && first <= HORIZON_UTF8_FOUR_LAST) {
		continuation = 3U;
		minimum = HORIZON_UTF8_FOUR_MINIMUM;
		*scalar &= HORIZON_UTF8_FOUR_MASK;
	} else {
		return 0;
	}
	if (continuation > length - *offset) {
		return 0;
	}
	for (legacy_u8 index = 0; index < continuation; index++) {
		legacy_u8 byte = (legacy_u8)name[(*offset)++];
		if ((byte & HORIZON_UTF8_CONTINUATION_MASK) != HORIZON_UTF8_CONTINUATION) {
			return 0;
		}
		*scalar = (*scalar << HORIZON_UTF8_PAYLOAD_BITS) | (byte & HORIZON_UTF8_PAYLOAD_MASK);
	}
	return *scalar >= minimum && *scalar <= HORIZON_UNICODE_MAXIMUM &&
		   (*scalar < HORIZON_UNICODE_SURROGATE_FIRST || *scalar > HORIZON_UNICODE_SURROGATE_LAST);
}

legacy_int cstrk_custom_horizon_name_valid(const legacy_s8 *name, size_t length)
{
	if (!length) {
		return 1;
	}
	if (!name || length > CSTRK_MAX_HORIZON_NAME || length <= HORIZON_PNG_EXTENSION_LENGTH) {
		return 0;
	}
	const legacy_s8 *extension = name + length - HORIZON_PNG_EXTENSION_LENGTH;
	if (extension[0] != '.' || (extension[1] != 'p' && extension[1] != 'P') ||
		(extension[2] != 'n' && extension[2] != 'N') ||
		(extension[3] != 'g' && extension[3] != 'G')) {
		return 0;
	}
	for (size_t offset = 0; offset < length;) {
		legacy_u32 scalar;
		if (!horizon_utf8_scalar(name, length, &offset, &scalar) ||
			scalar <= HORIZON_ASCII_CONTROL_LAST ||
			(scalar >= HORIZON_UNICODE_CONTROL_FIRST && scalar <= HORIZON_UNICODE_CONTROL_LAST) ||
			(scalar < HORIZON_UTF8_CONTINUATION && strchr("<>:\"/\\|?*", (legacy_char)scalar))) {
			return 0;
		}
	}
	return 1;
}

legacy_int cstrk_horizon_valid(const struct CSTRK_TRACK *track, legacy_char *error,
							   size_t error_size)
{
	size_t length = 0;
	while (length < sizeof(track->custom_horizon) && track->custom_horizon[length]) {
		length++;
	}
	if (!cstrk_custom_horizon_name_valid(track->custom_horizon, length)) {
		return horizon_fail(
			error, error_size,
			"Custom horizon must be a valid UTF-8 PNG basename of at most 127 bytes");
	}
	if (length && track->version < CSTRK_CUSTOM_HORIZON_VERSION) {
		return horizon_fail(error, error_size, "Custom horizons require track version v15");
	}
	return 1;
}

legacy_int cstrk_set_custom_horizon(struct CSTRK_TRACK *track, const legacy_s8 *name, size_t length,
									legacy_char *error, size_t error_size)
{
	if (!track) {
		return horizon_fail(error, error_size, "Track is required");
	}
	if (!cstrk_custom_horizon_name_valid(name, length)) {
		return horizon_fail(
			error, error_size,
			"Custom horizon must be a valid UTF-8 PNG basename of at most 127 bytes");
	}
	if (length && track->version < CSTRK_CUSTOM_HORIZON_VERSION) {
		return horizon_fail(error, error_size, "Custom horizons require track version v15");
	}
	if (length) {
		memmove(track->custom_horizon, name, length);
	}
	track->custom_horizon[length] = 0;
	if (error && error_size) {
		error[0] = 0;
	}
	return 1;
}
