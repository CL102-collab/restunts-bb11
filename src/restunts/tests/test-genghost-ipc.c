#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <SDL3/SDL.h>
#include "../c/legacy.h"

// Compile the production packet reader with deterministic open failures.
// Unused planner/process functions are discarded by the linker.
static legacy_uint open_count, delay_count, failures;
static legacy_int failure_code;
static FILE *test_fopen(const legacy_char *path, const legacy_char *mode)
{
	assert(strcmp(mode, "rb") == 0);
	++open_count;
	if (failures) {
		--failures;
		errno = failure_code;
		return NULL;
	}
	return fopen(path, mode);
}
static void test_delay(Uint32 milliseconds)
{
	assert(milliseconds > 0U);
	++delay_count;
}
#define fopen test_fopen
#define SDL_Delay test_delay
#include "../genghost/parallel.c"
#undef fopen
#undef SDL_Delay

static legacy_char packet_path[] = "/tmp/restunts-ipc-read-XXXXXX";
static const legacy_uint payload = 0x78563412U;
static const legacy_u64 expected_sequence = 42U;
static const legacy_uint transient_failure_count = 3U;
static const legacy_uint malformed_case_count = 4U;
static const legacy_int haiku_general_error = -1;

static void reset(legacy_uint fail_count, legacy_int error)
{
	open_count = delay_count = 0U;
	failures = fail_count;
	failure_code = error;
}
static void packet(legacy_u64 sequence, legacy_int status, legacy_int bad_magic,
				   legacy_int truncated)
{
	struct cooperative_header header = {"GGCOOP2", sequence, sizeof(payload), status};
	if (bad_magic) {
		header.magic[0] = '?';
	}
	FILE *file = fopen(packet_path, "wb");
	assert(file);
	assert(fwrite(&header, 1U, sizeof(header), file) == sizeof(header));
	if (!truncated) {
		assert(fwrite(&payload, 1U, sizeof(payload), file) == sizeof(payload));
	}
	assert(fclose(file) == 0);
}
static legacy_int receive(legacy_int expected)
{
	legacy_uint value = 0U;
	legacy_int status = -1;
	legacy_int result =
		cooperative_read(packet_path, expected_sequence, &value, sizeof(value), &status);
	assert(result == expected);
	if (result > 0) {
		assert(value == payload && status == 0);
	}
	return result;
}
legacy_int main(void)
{
	legacy_int descriptor = mkstemp(packet_path);
	assert(descriptor >= 0 && close(descriptor) == 0 && remove(packet_path) == 0);
	reset(0U, 0);
	receive(0);
	assert(open_count == 1U && delay_count == 0U);

	const legacy_int transient_errors[] = {haiku_general_error, EINTR, EIO, EACCES};
	for (size_t i = 0; i < sizeof(transient_errors) / sizeof(*transient_errors); ++i) {
		packet(expected_sequence, 0, 0, 0);
		reset(transient_failure_count, transient_errors[i]);
		receive(1);
		assert(open_count == transient_failure_count + 1U &&
			   delay_count == transient_failure_count);
		assert(access(packet_path, F_OK) != 0 && errno == ENOENT);

		packet(expected_sequence, 0, 0, 0);
		reset(COOPERATIVE_FILE_ATTEMPTS + 1U, transient_errors[i]);
		receive(-1);
		assert(open_count == COOPERATIVE_FILE_ATTEMPTS);
		assert(delay_count == COOPERATIVE_FILE_ATTEMPTS - 1U);
		assert(remove(packet_path) == 0);
	}

	reset(1U, EIO);
	receive(0);
	assert(open_count == 2U && delay_count == 1U);

	for (legacy_uint malformed = 0; malformed < malformed_case_count; ++malformed) {
		packet(expected_sequence + (malformed == 1U), malformed == 2U ? 3 : 0, malformed == 0U,
			   malformed == 3U);
		reset(0U, 0);
		receive(-1);
		assert(open_count == 1U && delay_count == 0U);
		assert(remove(packet_path) == 0);
	}
	packet(expected_sequence - 1U, 0, 0, 0);
	reset(0U, 0);
	receive(0);
	assert(open_count == 1U && delay_count == 0U);
	assert(remove(packet_path) == 0);
	puts("Cooperative IPC transient/persistent open, missing inbox and malformed packet checks "
		 "passed.");
	return 0;
}
