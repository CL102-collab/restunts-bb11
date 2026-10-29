/* Asset-free native reference for the browser route checker's differential
 * tests. Each input line contains one complete hexadecimal CSTRK file; each
 * output line contains the actual game's admission/route result as JSON. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../c/cstrk.h"
#include "../c/extended_track.h"
#include "../c/externs.h"
#include "../c/memmgr.h"
#include "../c/track_objects.h"

#undef printf
#undef strlen

#define ORACLE_MAX_TRACK_BYTES                                                                     \
	(CSTRK_HEADER_SIZE + (CSTRK_MAX_DIMENSION + 1U) * (CSTRK_MAX_DIMENSION + 1U) +                 \
	 2U * CSTRK_MAX_DIMENSION * CSTRK_MAX_DIMENSION + CSTRK_OBJECT_SIZE * CSTRK_MAX_OBJECTS +      \
	 CSTRK_MAX_NOTES)
#define ORACLE_MAX_HEX_BYTES (2U * ORACLE_MAX_TRACK_BYTES)
/* Include optional CRLF and the terminating NUL without accepting extra data. */
#define ORACLE_LINE_CAPACITY (ORACLE_MAX_HEX_BYTES + 3U)

extern void full_data_initialize(void);

static void write_json_string(const char *value)
{
	fputc('"', stdout);
	for (const unsigned char *cursor = (const unsigned char *)value; *cursor; cursor++) {
		switch (*cursor) {
			case '"':
				fputs("\\\"", stdout);
				break;
			case '\\':
				fputs("\\\\", stdout);
				break;
			case '\n':
				fputs("\\n", stdout);
				break;
			case '\r':
				fputs("\\r", stdout);
				break;
			case '\t':
				fputs("\\t", stdout);
				break;
			default:
				if (*cursor < 0x20U) {
					printf("\\u%04x", (unsigned)*cursor);
				} else {
					fputc(*cursor, stdout);
				}
		}
	}
	fputc('"', stdout);
}

static void write_load_error(const char *message)
{
	fputs("{\"loadError\":", stdout);
	write_json_string(message);
	fputs("}\n", stdout);
}

static int hex_digit(unsigned char value)
{
	if (value >= '0' && value <= '9') {
		return value - '0';
	}
	if (value >= 'a' && value <= 'f') {
		return value - 'a' + 10;
	}
	if (value >= 'A' && value <= 'F') {
		return value - 'A' + 10;
	}
	return -1;
}

static void check_track(const char *line, size_t length, unsigned char *bytes)
{
	/* Every fixture receives a fresh extended owner, including its occupancy,
	 * map, route and branch storage. No previous failure coordinates survive. */
	extended_track_unload();
	track_validation_column = 0;
	track_validation_row = 0;
	track_pieces_counter = 0;
	if (length == 0U || length % 2U != 0U) {
		write_load_error("Expected a nonempty, even-length hexadecimal CSTRK file.");
		return;
	}
	for (size_t index = 0; index < length / 2U; index++) {
		int high = hex_digit((unsigned char)line[index * 2U]);
		int low = hex_digit((unsigned char)line[index * 2U + 1U]);
		if (high < 0 || low < 0) {
			write_load_error("Track input contains a non-hexadecimal character.");
			return;
		}
		bytes[index] = (unsigned char)((unsigned)high * 16U + (unsigned)low);
	}
	char error[256];
	if (!extended_track_load(bytes, length / 2U, error, sizeof(error))) {
		write_load_error(error);
		return;
	}
	legacy_s16 status = track_setup();
	printf("{\"status\":%d,\"column\":%u,\"row\":%u,\"pieces\":%d,\"route\":[", (int)status,
		   (unsigned)track_validation_column, (unsigned)track_validation_row,
		   (int)track_pieces_counter);
	for (legacy_s16 index = 0; index < track_pieces_counter; index++) {
		if (index) {
			fputc(',', stdout);
		}
		printf("{\"id\":%u,\"x\":%u,\"y\":%u,\"flags\":%u,\"primary\":%d,\"alternate\":%d}",
			   (unsigned)(legacy_u8)track_route_element_ids[index],
			   (unsigned)(legacy_u8)track_route_columns[index],
			   (unsigned)(legacy_u8)track_route_rows[index],
			   (unsigned)(legacy_u8)track_route_traversal_flags[index],
			   (int)track_primary_route_links[index], (int)track_alternate_route_links[index]);
	}
	fputs("]}\n", stdout);
}

int main(void)
{
	char *line = malloc(ORACLE_LINE_CAPACITY);
	unsigned char *bytes = malloc(ORACLE_MAX_TRACK_BYTES);
	if (!line || !bytes) {
		fputs("Unable to allocate the route oracle's bounded input buffers.\n", stderr);
		free(line);
		free(bytes);
		return EXIT_FAILURE;
	}
	full_data_initialize();
	mmgr_init_conventional_arena();
	init_row_tables();
	init_trackdata();
	while (fgets(line, (int)ORACLE_LINE_CAPACITY, stdin)) {
		size_t length = strlen(line);
		int complete = length > 0U && line[length - 1U] == '\n';
		int full = !complete && length == ORACLE_LINE_CAPACITY - 1U;
		while (length > 0U && (line[length - 1U] == '\n' || line[length - 1U] == '\r')) {
			length--;
		}
		if (full || length > ORACLE_MAX_HEX_BYTES) {
			if (!complete) {
				int value;
				do {
					value = fgetc(stdin);
				} while (value != '\n' && value != EOF);
			}
			write_load_error("Hexadecimal track input exceeds the maximum CSTRK size.");
		} else {
			check_track(line, length, bytes);
		}
		extended_track_unload();
		if (fflush(stdout) != 0) {
			break;
		}
	}
	int failed = ferror(stdin) || ferror(stdout);
	extended_track_unload();
	free(bytes);
	free(line);
	return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
