#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../genghost/simulation.h"
#include "../genghost/guide.h"
#include "../c/externs.h"
#include "../c/game_input.h"

#undef memcpy
#undef strlen
#undef printf

/* Resource-backed integration check. Run from a game-data directory:
 * test-genghost-simulation source.rpl normalized.rpl [physics switches]
 * repldump normalized.rpl 1 [the same physics switches]
 * Compare normalized.rpl.states with normalized.BNI: every serialized state
 * must match. Source 10 Hz inputs are deliberately normalized to 20 Hz so this
 * also exercises extraction without importing the source recording's timing. */
legacy_s16 stuntsmain(legacy_s16 argc, legacy_s8 *argv[])
{
	if (argc < 3) {
		fprintf(stderr,
				"Usage: test-genghost-simulation SOURCE.RPL OUTPUT.RPL [physics options]\n");
		return 1;
	}
	unsigned char recording[REPLAY_INPUT_OFFSET + GENGHOST_MAX_FRAMES + 1];
	FILE *source = fopen((const char *)argv[1], "rb");
	assert(source != NULL);
	size_t size = fread(recording, 1, sizeof(recording), source);
	assert(!ferror(source) && fclose(source) == 0);
	assert(size >= REPLAY_INPUT_OFFSET);
	struct GAMEINFO config;
	replay_gameinfo_decode(&config, recording);
	assert(config.game_opponenttype == 0);
	assert(config.game_recordedframes <= GENGHOST_MAX_FRAMES);
	assert(size == REPLAY_INPUT_OFFSET + config.game_recordedframes);
	unsigned int count = config.game_recordedframes;
	const unsigned char *inputs = recording + REPLAY_INPUT_OFFSET;
	assert(genghost_sim_init(recording + REPLAY_GAMEINFO_SIZE,
							 (const char *)config.game_playercarid, config.game_playertransmission,
							 argc, (char **)argv) == 0);

	/* A discarded branch changes collision caches, RNG (on impact), retained
	 * legacy stack words and penalty state. Restoring must undo all of it. */
	struct GENGHOST_SNAPSHOT checkpoint, expected, actual;
	for (unsigned int frame = 0; frame < 137; frame++) {
		assert(genghost_sim_step(frame < count ? inputs[frame] : INPUT_ACCELERATE_FLAG) >= 0);
	}
	genghost_sim_capture(&checkpoint);
	for (unsigned int frame = 137; frame < 377; frame++) {
		assert(genghost_sim_step(frame < count ? inputs[frame] : INPUT_ACCELERATE_FLAG) >= 0);
	}
	genghost_sim_capture(&expected);
	genghost_sim_restore(&checkpoint);
	for (unsigned int frame = 0; frame < 911; frame++) {
		assert(genghost_sim_step(INPUT_ACCELERATE_FLAG | INPUT_STEER_LEFT_FLAG) >= 0);
	}
	genghost_sim_restore(&checkpoint);
	for (unsigned int frame = 137; frame < 377; frame++) {
		assert(genghost_sim_step(frame < count ? inputs[frame] : INPUT_ACCELERATE_FLAG) >= 0);
	}
	genghost_sim_capture(&actual);
	assert(memcmp(&expected, &actual, sizeof(expected)) == 0);

	char track_name[REPLAY_TRACK_NAME_SIZE + 1];
	memcpy(track_name, config.game_trackname, REPLAY_TRACK_NAME_SIZE);
	track_name[REPLAY_TRACK_NAME_SIZE] = 0;
	assert(genghost_sim_write_replay((const char *)argv[2], track_name, inputs, count) == 0);
	char *state_path = malloc(strlen((const char *)argv[2]) + sizeof(".states"));
	assert(state_path != NULL);
	sprintf(state_path, "%s.states", (const char *)argv[2]);
	FILE *output = fopen(state_path, "wb");
	assert(output != NULL);
	free(state_path);
	struct GENGHOST_GUIDE_POINT *guide = calloc(count, sizeof(*guide));
	assert(guide != NULL);
	unsigned char serialized[GAMESTATE_SERIALIZED_SIZE];
	LEGACY_WRITE_U16_LE(serialized, count);
	assert(fwrite(serialized, 1, 2, output) == 2);
	genghost_sim_reset();
	for (unsigned int frame = 0; frame < count; frame++) {
		genghost_guide_capture(&guide[frame], inputs[frame]);
		assert(genghost_sim_step(inputs[frame]) >= 0);
		assert(gamestate_serialize(serialized, &state) == sizeof(serialized));
		assert(fwrite(serialized, 1, sizeof(serialized), output) == sizeof(serialized));
	}
	assert(fclose(output) == 0);
	genghost_sim_capture(&expected);
	genghost_sim_reset();
	for (unsigned int frame = 0; frame < count; frame++) {
		assert(genghost_sim_step(inputs[frame]) >= 0);
	}
	genghost_sim_capture(&actual);
	assert(memcmp(&expected, &actual, sizeof(expected)) == 0);
	/* Following an unmodified demonstrated trajectory must preserve every input
	 * and the complete final simulator state, including through jumps and slides. */
	genghost_sim_reset();
	unsigned int cursor = 0;
	for (unsigned int frame = 0; frame < count; ++frame) {
		unsigned char guided = genghost_guide_step(guide, count, &cursor, 0.5);
		assert(guided == inputs[frame]);
		assert(cursor == frame + 1U);
		assert(genghost_sim_step(guided) >= 0);
	}
	genghost_sim_capture(&actual);
	assert(memcmp(&expected, &actual, sizeof(expected)) == 0);
	free(guide);
	struct GENGHOST_RESULT result;
	genghost_sim_result(&result);
	printf(
		"Snapshot branching and replay reset agree for %u frames: finish=%d penalty=%u score=%u.\n",
		count, result.finished, result.penalty_frames, result.total_frames);
	return 0;
}
