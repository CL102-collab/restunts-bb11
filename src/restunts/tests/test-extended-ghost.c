#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../c/extended/extended_ghost.h"
#include "../c/extended/extended_track.h"
#include "../c/platform.h"

static FILE *source;
static struct CSTRK_TRACK current;
static int active;
static legacy_u8 classic_track[CSRPL_CLASSIC_TRACK_SIZE];
legacy_u8 *track_element_map = classic_track;

#define TEST_UNIT_GHOST_FRAMES 2U
#define TEST_UNIT_GHOST_STEERING 120
#define TEST_UNIT_GHOST_ROUTE_INDEX 17
#define TEST_UNIT_GHOST_ROUTE_POINT 23U

legacy_u16 dos_file_open(const legacy_s8 *name, legacy_s16 mode)
{
	(void)name;
	(void)mode;
	if (source) {
		rewind(source);
	}
	return source ? 1 : 0;
}
legacy_s16 dos_file_close(legacy_u16 handle)
{
	(void)handle;
	return 0;
}
legacy_u16 dos_file_read(legacy_u16 handle, void *destination, legacy_u16 count)
{
	(void)handle;
	return (legacy_u16)fread(destination, 1, count, source);
}
legacy_s16 dos_file_seek(legacy_u16 handle, legacy_s32 offset, legacy_s16 origin)
{
	(void)handle;
	return (legacy_s16)fseek(source, offset, origin);
}
legacy_s32 dos_file_tell(legacy_u16 handle)
{
	(void)handle;
	return (legacy_s32)ftell(source);
}
int extended_track_active(void)
{
	return active;
}
const struct CSTRK_TRACK *extended_track_model(void)
{
	return &current;
}

static void test_classic_unit_ghost(void)
{
	static const legacy_u8 inputs[TEST_UNIT_GHOST_FRAMES] = {1U, 5U};
	legacy_u8 commands[TEST_UNIT_GHOST_FRAMES * CSRPL_OPPONENT_COMMAND_SIZE];
	struct CSRPL_OPPONENT_COMMAND command = {1U, 0U, -TEST_UNIT_GHOST_STEERING,
											 TEST_UNIT_GHOST_ROUTE_INDEX,
											 TEST_UNIT_GHOST_ROUTE_POINT};
	for (legacy_u16 frame = 0; frame < TEST_UNIT_GHOST_FRAMES; frame++) {
		csrpl_opponent_command_write(commands + frame * CSRPL_OPPONENT_COMMAND_SIZE, &command);
	}
	struct CSRPL_METADATA metadata = {0};
	metadata.version = CSRPL_VERSION;
	metadata.flags = CSRPL_FLAG_CLASSIC_TRACK | CSRPL_FLAG_OPPONENT_COMMANDS;
	metadata.opponent_type = CSRPL_UNIT_OPPONENT_TYPE;
	memcpy(metadata.opponent_id, "UNIT", CSRPL_OPPONENT_ID_SIZE);
	memcpy(metadata.player_car_id, "PMIN", CSRPL_CAR_ID_SIZE);
	memcpy(metadata.opponent_car_id, "PMIN", CSRPL_CAR_ID_SIZE);
	memcpy(metadata.track_name, "CLASSIC", sizeof("CLASSIC"));
	metadata.frames_per_second = 20U;
	metadata.frame_count = TEST_UNIT_GHOST_FRAMES;
	legacy_u8 *replay = NULL;
	size_t replay_size = 0;
	assert(csrpl_encode_with_commands(&metadata, classic_track, sizeof(classic_track), inputs,
									  TEST_UNIT_GHOST_FRAMES, commands, TEST_UNIT_GHOST_FRAMES,
									  &replay, &replay_size, NULL, 0U));
	source = tmpfile();
	assert(source);
	assert(fwrite(replay, 1U, replay_size, source) == replay_size);
	free(replay);
	struct EXTENDED_GHOST_REPLAY selected = {0};
	struct GAMEINFO config;
	assert(extended_ghost_read((const legacy_s8 *)"classic-unit.csrpl", &selected, &config));
	assert(config.game_opponenttype == CSRPL_UNIT_OPPONENT_TYPE);
	assert(memcmp(config.game_opponentcarid, "PMIN", CSRPL_CAR_ID_SIZE) == 0);
	assert(selected.view.metadata.flags == metadata.flags);
	assert(memcmp(selected.view.metadata.opponent_id, "UNIT", CSRPL_OPPONENT_ID_SIZE) == 0);
	assert(memcmp(selected.view.inputs, inputs, sizeof(inputs)) == 0);
	assert(memcmp(selected.view.opponent_commands, commands, sizeof(commands)) == 0);
	assert(selected.view.track_size == CSRPL_CLASSIC_TRACK_SIZE);
	assert(memcmp(selected.view.track_data, classic_track, sizeof(classic_track)) == 0);
	struct CSRPL_OPPONENT_COMMAND decoded;
	csrpl_opponent_command_read(selected.view.opponent_commands + CSRPL_OPPONENT_COMMAND_SIZE,
								&decoded);
	assert(decoded.input == command.input && decoded.steering == command.steering);
	assert(decoded.route_index == command.route_index &&
		   decoded.route_point == command.route_point);
	active = 0;
	assert(extended_ghost_matches_track(&selected));
	classic_track[CSRPL_CLASSIC_TRACK_SIZE - 1U] ^= 1U;
	assert(!extended_ghost_matches_track(&selected));
	classic_track[CSRPL_CLASSIC_TRACK_SIZE - 1U] ^= 1U;
	assert(extended_ghost_matches_track(&selected));
	active = 1;
	assert(!extended_ghost_matches_track(&selected));
	active = 0;
	extended_ghost_free(&selected);
	assert(selected.bytes == NULL && selected.view.opponent_commands == NULL);
	fclose(source);
	source = NULL;
}

int main(void)
{
	cstrk_init(&current);
	assert(cstrk_create(&current, 90, 90, NULL, 0));
	assert(cstrk_set_notes(&current, "Extended ghost", 14, NULL, 0));
	uint8_t *track = NULL;
	size_t track_size;
	assert(cstrk_encode(&current, &track, &track_size, NULL, 0));
	struct CSRPL_METADATA metadata = {0};
	metadata.version = CSRPL_VERSION;
	metadata.driving_rules =
		DRIVING_RULE_POWER_GEAR | DRIVING_RULE_LEFT_CORNER_BIAS | DRIVING_RULE_OWOOT;
	memcpy(metadata.player_car_id, "P962", 4);
	memcpy(metadata.track_name, "LONGTEST1234", sizeof("LONGTEST1234"));
	metadata.frame_count = 70001;
	metadata.frames_per_second = 20;
	uint8_t *inputs = calloc(metadata.frame_count, 1);
	assert(inputs);
	inputs[70000] = 5;
	uint8_t *replay = NULL;
	size_t replay_size;
	assert(csrpl_encode(&metadata, track, track_size, inputs, metadata.frame_count, &replay,
						&replay_size, NULL, 0));
	source = tmpfile();
	assert(source);
	assert(fwrite(replay, 1, replay_size, source) == replay_size);
	struct EXTENDED_GHOST_REPLAY selected = {0};
	struct GAMEINFO config;
	assert(extended_ghost_read((const legacy_s8 *)"long.csrpl", &selected, &config));
	assert(selected.view.metadata.version == CSRPL_VERSION);
	assert(selected.view.metadata.driving_rules == metadata.driving_rules);
	assert(selected.view.metadata.frame_count == 70001);
	assert(strcmp((const char *)selected.view.metadata.track_name, "LONGTEST1234") == 0);
	assert(strcmp((const char *)config.game_trackname, "LONGTEST") == 0);
	assert(selected.view.inputs[70000] == 5);
	assert(memcmp(config.game_playercarid, "P962", 4) == 0);
	assert(config.game_recordedframes == LEGACY_S16_MAX);
	assert(config.game_framespersec == 20);
	active = 1;
	assert(extended_ghost_matches_track(&selected));
	current.cells[8000].surface = CSTRK_TERRAIN_ICE;
	assert(!extended_ghost_matches_track(&selected));
	current.cells[8000].surface = CSTRK_TERRAIN_GRASS;
	active = 0;
	assert(!extended_ghost_matches_track(&selected));
	extended_ghost_free(&selected);
	assert(selected.bytes == NULL);
	fclose(source);
	source = NULL;
	assert(!extended_ghost_read((const legacy_s8 *)"missing.csrpl", &selected, &config));
	free(replay);
	free(inputs);
	free(track);
	test_classic_unit_ghost();
	cstrk_free(&current);
	puts("Extended ghost input isolation, long frame counts and track identity passed");
	return 0;
}
