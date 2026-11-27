#define _GNU_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../c/extended/extended_track.h"
#include "../c/fileio.h"
#include "../c/game_input.h"
#include "../c/memmgr.h"
#include "../c/replay/multi_replay.h"
#include "../c/replay/multi_replay_capture.h"
#include "../c/replay/multi_replay_manifest.h"
#include "../c/restunts.h"
#include "../c/shape3d/shape3d.h"
#include "../c/video/video_pages.h"

#undef memcpy
#undef strcmp
#undef strlen

#define TEST_SHORT_BASENAME "short:race\\view"
#define TEST_SHORT_FRAMES 25U
#define TEST_LONG_FRAMES 65540U
#define TEST_ACCELERATE_FRAMES 100U
#define TEST_SHORT_END (TEST_SHORT_FRAMES * MULTI_REPLAY_FRAME_RATE / GAME_FRAME_RATE_LOW)
#define TEST_ERROR_SIZE 192U
#define TEST_TRACK_MISMATCH_BYTE (REPLAY_TRACK_SIZE - 1U)

static void write_bytes(const legacy_char *path, const void *bytes, size_t size)
{
	FILE *file = fopen(path, "wb");
	assert(file != NULL);
	assert(fwrite(bytes, 1U, size, file) == size);
	assert(fclose(file) == 0);
}

static void write_legacy_replay(const legacy_char *path, const struct MULTI_REPLAY_SOURCE *source,
								legacy_int mismatch)
{
	struct GAMEINFO config = {0};
	memcpy(config.game_playercarid, "PMIN", REPLAY_CAR_ID_SIZE);
	memcpy(config.game_trackname, source->track_name, REPLAY_TRACK_NAME_SIZE);
	config.game_playertransmission = TRANSMISSION_AUTOMATIC;
	config.game_opponentcarid[0] = -1;
	config.game_framespersec = GAME_FRAME_RATE_LOW;
	config.game_recordedframes = TEST_SHORT_FRAMES;

	legacy_u8 bytes[REPLAY_INPUT_OFFSET + TEST_SHORT_FRAMES];
	replay_gameinfo_encode(bytes, &config);
	memcpy(bytes + REPLAY_GAMEINFO_SIZE, source->track_data, REPLAY_TRACK_SIZE);
	if (mismatch) {
		bytes[REPLAY_GAMEINFO_SIZE + TEST_TRACK_MISMATCH_BYTE] ^= 1U;
	}
	memset(bytes + REPLAY_INPUT_OFFSET, INPUT_ACCELERATE_FLAG, TEST_SHORT_FRAMES);
	write_bytes(path, bytes, sizeof(bytes));
}

static void write_extended_replay(const legacy_char *path, const struct MULTI_REPLAY_SOURCE *source)
{
	struct CSRPL_METADATA metadata = {0};
	metadata.version = CSRPL_VERSION;
	metadata.flags = CSRPL_FLAG_CLASSIC_TRACK;
	metadata.frame_count = TEST_LONG_FRAMES;
	metadata.frames_per_second = GAME_FRAME_RATE_NORMAL;
	metadata.player_transmission = TRANSMISSION_AUTOMATIC;
	metadata.opponent_type = CSRPL_CLASSIC_OPPONENT_LAST;
	metadata.opponent_transmission = TRANSMISSION_AUTOMATIC;
	memcpy(metadata.player_car_id, "JAGU", CSRPL_CAR_ID_SIZE);
	memcpy(metadata.opponent_car_id, "COUN", CSRPL_CAR_ID_SIZE);
	memcpy(metadata.track_name, source->track_name, CSRPL_TRACK_NAME_SIZE);

	legacy_u8 *inputs = calloc(TEST_LONG_FRAMES, sizeof(*inputs));
	assert(inputs != NULL);
	memset(inputs, INPUT_ACCELERATE_FLAG, TEST_ACCELERATE_FRAMES);
	legacy_u8 *bytes = NULL;
	size_t length = 0;
	assert(csrpl_encode(&metadata, source->track_data, source->track_size, inputs, TEST_LONG_FRAMES,
						&bytes, &length, NULL, 0));
	write_bytes(path, bytes, length);
	free(bytes);
	free(inputs);
}

static void write_manifest(const legacy_char *path, const legacy_char *first,
						   const legacy_char *second)
{
	FILE *file = fopen(path, "wb");
	assert(file != NULL);
	assert(fputs(MULTI_REPLAY_MANIFEST_HEADER, file) >= 0);
	size_t first_size = strlen(first) + 1U;
	size_t second_size = strlen(second) + 1U;
	assert(fwrite(first, 1U, first_size, file) == first_size);
	assert(fwrite(second, 1U, second_size, file) == second_size);
	assert(fclose(file) == 0);
}

static void test_target_cycle(void)
{
	assert(multi_replay_target_index() == 0U);
	assert(strcmp((const legacy_char *)replay_filename, TEST_SHORT_BASENAME) == 0);
	multi_replay_cycle_target(1);
	assert(multi_replay_target_index() == 1U);
	assert(strcmp((const legacy_char *)replay_filename, "longer") == 0);
	multi_replay_cycle_target(1);
	assert(multi_replay_target_index() == 2U);
	assert(strcmp((const legacy_char *)replay_filename, "longer") == 0);
	multi_replay_cycle_target(1);
	assert(multi_replay_target_index() == 0U);
	assert(strcmp((const legacy_char *)replay_filename, TEST_SHORT_BASENAME) == 0);
	multi_replay_cycle_target(-1);
	assert(multi_replay_target_index() == 2U);
	assert(strcmp((const legacy_char *)replay_filename, "longer") == 0);
	multi_replay_cycle_target(-1);
	assert(multi_replay_target_index() == 1U);
}

static void test_timeline(void)
{
	assert(multi_replay_recorded_frames() == TEST_LONG_FRAMES);
	multi_replay_seek(2U);
	assert(multi_replay_car(0U)->camera.frame == 1U);
	assert(multi_replay_car(1U)->camera.frame == 2U);
	assert(multi_replay_car(2U)->camera.frame == 2U);
	assert(multi_replay_queue_frame());
	assert(multi_replay_input_frame() == 3U && multi_replay_frame() == 2U);
	multi_replay_present_input_frame();
	assert(multi_replay_car(0U)->camera.frame == 1U);
	assert(multi_replay_car(1U)->camera.frame == 3U);

	multi_replay_seek(TEST_SHORT_END);
	struct CARSTATE last_short_pose = multi_replay_car(0U)->state;
	assert(multi_replay_car(0U)->camera.frame == TEST_SHORT_FRAMES);
	multi_replay_seek(LEGACY_U32_MAX);
	assert(multi_replay_frame() == TEST_LONG_FRAMES);
	assert(multi_replay_car(0U)->camera.frame == TEST_SHORT_FRAMES);
	assert(memcmp(&multi_replay_car(0U)->state, &last_short_pose, sizeof(last_short_pose)) == 0);
	assert(multi_replay_car(1U)->camera.frame == TEST_LONG_FRAMES);
	assert(multi_replay_car(2U)->camera.frame == TEST_LONG_FRAMES);
	assert(!multi_replay_queue_frame());
	multi_replay_seek(0U);
	assert(multi_replay_car(0U)->camera.frame == 0U);
	assert(multi_replay_car(2U)->camera.frame == 0U);

	/* Playback and seeking leave the selected camera/car unchanged. */
	assert(multi_replay_target_index() == 1U);
}

legacy_s16 stuntsmain(legacy_s16 argc, legacy_s8 *argv[])
{
	init_main(argc, argv);
	mouse_draw_opaque_check();
	video_pages_initialize();
	init_div0();
	init_row_tables();
	mainresptr = file_load_resfile("main");
	fontdefptr = file_load_resource(FILE_RESOURCE_BINARY_FATAL, "fontdef.fnt");
	fontnptr = file_load_resource(FILE_RESOURCE_BINARY_FATAL, "fontn.fnt");
	init_polyinfo();
	init_trackdata();
	reset_race_loop_state();
	init_kevinrandom("kevin");
	cvxptr = mmgr_alloc_resbytes("cvx", sizeof(struct GAMESTATE) * GAMESTATE_CHECKPOINT_COUNT);

	legacy_char directory[] = "/tmp/restunts-multi-replay-XXXXXX";
	assert(mkdtemp(directory) != NULL);
	legacy_char short_path[REPLAY_FILENAME_SIZE], long_path[REPLAY_FILENAME_SIZE];
	legacy_char mismatch_path[REPLAY_FILENAME_SIZE], manifest[REPLAY_FILENAME_SIZE];
	snprintf(short_path, sizeof(short_path), "%s/" TEST_SHORT_BASENAME ".rpl", directory);
	snprintf(long_path, sizeof(long_path), "%s/longer.csrpl", directory);
	snprintf(mismatch_path, sizeof(mismatch_path), "%s/mismatch\ntrack.rpl", directory);
	snprintf(manifest, sizeof(manifest), "%s/selection", directory);

	legacy_char error[TEST_ERROR_SIZE] = {0};
	struct MULTI_REPLAY_SOURCE source = {0};
	const legacy_s8 *default_replay = file_find("DEFAULT.RPL");
	assert(default_replay != NULL);
	assert(multi_replay_source_read(default_replay, &source, error, sizeof(error)));
	write_legacy_replay(short_path, &source, 0);
	write_legacy_replay(mismatch_path, &source, 1);
	write_extended_replay(long_path, &source);
	multi_replay_source_free(&source);

	write_manifest(manifest, short_path, long_path);
	assert(multi_replay_open((const legacy_s8 *)manifest, error, sizeof(error)));
	assert(access(manifest, F_OK) != 0);
	assert(multi_replay_is_active());
	assert(multi_replay_car_count() == 3U);
	assert(multi_replay_car(3U) == NULL);
	test_target_cycle();
	test_timeline();
	multi_replay_close();

	write_manifest(manifest, short_path, mismatch_path);
	assert(!multi_replay_open((const legacy_s8 *)manifest, error, sizeof(error)));
	assert(strstr(error, "same track") != NULL);
	assert(!multi_replay_is_active());
	assert(multi_replay_car_count() == 0U);
	assert(access(manifest, F_OK) != 0);

	extended_track_unload();
	mmgr_release(cvxptr);
	assert(unlink(short_path) == 0);
	assert(unlink(long_path) == 0);
	assert(unlink(mismatch_path) == 0);
	assert(rmdir(directory) == 0);
	return 0;
}
