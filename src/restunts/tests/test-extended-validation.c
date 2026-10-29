#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "../c/extended_validation.h"
#include "../c/track_objects.h"

static void assert_dialog_lines(const char *message)
{
	size_t length = strlen(message), line = 0;
	assert(length > 0);
	assert(message[length - 1] == ']');
	for (size_t index = 0; index < length; index++) {
		unsigned char character = (unsigned char)message[index];
		assert(character >= 32 && character <= 126);
		assert(character != '[' && character != '}' && character != '@');
		if (character == ']') {
			/* Even a fixed eight-pixel font fits these lines on a 320px screen. */
			assert(line > 0 && line <= 35);
			line = 0;
		} else {
			line++;
		}
	}
	assert(line == 0);
}

static void test_status_messages(void)
{
	static const struct {
		int status;
		const char *cause;
		int location;
	} cases[] = {
		{TRACK_SETUP_NO_START_FINISH, "No start / finish", 0},
		{TRACK_SETUP_INTERNAL_ERROR, "could not be compiled", 0},
		{TRACK_SETUP_MANY_START_FINISH, "More than one start / finish", 1},
		{TRACK_SETUP_ELEMENT_MISMATCH, "pieces do not connect", 1},
		{TRACK_SETUP_WRONG_WAY, "wrong way", 1},
		{TRACK_SETUP_MANY_ELEMENTS, "too many elements", 1},
		{TRACK_SETUP_NO_PATH, "no complete route", 1},
		{TRACK_SETUP_MANY_PATHS, "too many branches", 1},
		{TRACK_SETUP_NO_RUNWAY, "longer runway", 1},
		{TRACK_SETUP_LONG_JUMP, "too many tiles", 1},
		{TRACK_SETUP_TERRAIN_MISMATCH, "Terrain edges do not match", 1},
	};
	char message[256];
	strcpy(message, "previous failure");
	assert(!extended_track_validation_message(TRACK_SETUP_OK, 17, 31, message, sizeof(message)));
	assert(message[0] == '\0');
	for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		assert(extended_track_validation_message(cases[index].status, 17, 31, message,
												 sizeof(message)));
		assert(strstr(message, cases[index].cause));
		assert((strstr(message, "Column 18, row 32.]") != NULL) == cases[index].location);
		assert_dialog_lines(message);
	}
	for (size_t index = 0; index < 2; index++) {
		int status = index ? INT_MIN : INT_MAX;
		char expected[64];
		snprintf(expected, sizeof(expected), "Track validation error %d.", status);
		assert(extended_track_validation_message(status, 0, 0, message, sizeof(message)));
		assert(strstr(message, expected));
		assert(!strstr(message, "Column"));
		assert_dialog_lines(message);
	}
}

static void test_coordinates(void)
{
	static const struct {
		unsigned column;
		unsigned row;
		const char *expected;
	} cases[] = {
		{0, 0, "Column 1, row 1.]"},
		{89, 89, "Column 90, row 90.]"},
		{89, 0, "Column 90, row 1.]"},
		{0, 89, "Column 1, row 90.]"},
	};
	char message[256];
	for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		assert(extended_track_validation_message(TRACK_SETUP_NO_RUNWAY, cases[index].column,
												 cases[index].row, message, sizeof(message)));
		assert(strstr(message, cases[index].expected));
		assert_dialog_lines(message);
	}
	char expected[64];
	snprintf(expected, sizeof(expected), "Column %llu, row %llu.]",
			 (unsigned long long)UINT_MAX + 1ULL, (unsigned long long)UINT_MAX + 1ULL);
	assert(extended_track_validation_message(TRACK_SETUP_NO_RUNWAY, UINT_MAX, UINT_MAX, message,
											 sizeof(message)));
	assert(strstr(message, expected));
	assert_dialog_lines(message);
}

static void test_buffer_boundaries(void)
{
	char complete[256];
	assert(extended_track_validation_message(TRACK_SETUP_NO_RUNWAY, 17, 31, complete,
											 sizeof(complete)));
	size_t required = strlen(complete) + 1;
	for (size_t size = 0; size <= required + 1; size++) {
		unsigned char guarded[300];
		memset(guarded, 0xa5, sizeof(guarded));
		char *buffer = (char *)&guarded[1];
		int result = extended_track_validation_message(TRACK_SETUP_NO_RUNWAY, 17, 31, buffer, size);
		assert(guarded[0] == 0xa5);
		for (size_t index = size + 1; index < sizeof(guarded); index++) {
			assert(guarded[index] == 0xa5);
		}
		assert(result == (size >= required));
		if (size >= required) {
			assert(strcmp(buffer, complete) == 0);
			assert_dialog_lines(buffer);
		} else if (size) {
			assert(buffer[0] == '\0');
		}
	}
	assert(!extended_track_validation_message(TRACK_SETUP_NO_RUNWAY, 0, 0, NULL, 0));
	assert(!extended_track_validation_message(TRACK_SETUP_NO_RUNWAY, 0, 0, NULL, 256));
	assert(!extended_track_validation_message(TRACK_SETUP_OK, 0, 0, NULL, 0));
}

int main(void)
{
	test_status_messages();
	test_coordinates();
	test_buffer_boundaries();
	puts("Extended track validation messages, editor coordinates, and buffer bounds passed");
	return 0;
}
