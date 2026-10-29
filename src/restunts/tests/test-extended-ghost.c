#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../c/extended_ghost.h"
#include "../c/extended_track.h"
#include "../c/platform.h"

static FILE *source;
static struct CSTRK_TRACK current;
static int active;

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
	memcpy(metadata.track_name, "LONGTEST", 8);
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
	cstrk_free(&current);
	puts("Extended ghost input isolation, long frame counts and track identity passed");
	return 0;
}
