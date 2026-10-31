#include <assert.h>
#include <string.h>

#include "../c/replay/replay.h"

#define TEST_CAR_ID_SIZE 4U
#define TEST_TRACK_NAME_SIZE 9U
#define TEST_PLAYER_MATERIAL 129U
#define TEST_OPPONENT_CAR_PATTERN 85U
#define TEST_FRAMES_PER_SECOND 4660U
#define TEST_RECORDED_FRAME_COUNT 43981U
#define TEST_FRAMES_PER_SECOND_LOW_BYTE 52U
#define TEST_FRAMES_PER_SECOND_HIGH_BYTE 18U
#define TEST_RECORDED_FRAME_COUNT_LOW_BYTE 205U
#define TEST_RECORDED_FRAME_COUNT_HIGH_BYTE 171U
#define TEST_EXPECTED_REPLAY_INPUT_OFFSET 1828U
#define TEST_SAMPLE_RECORDED_FRAMES 244U
#define TEST_SAMPLE_REPLAY_FILE_SIZE 2072UL
#define TEST_MAXIMUM_REPLAY_FILE_SIZE 67363UL
#define TEST_STORAGE_PREFIX_SIZE 1U

static void fill_gameinfo(struct GAMEINFO *gameinfo)
{
	static const legacy_u8 opponent_car[TEST_CAR_ID_SIZE] = {LEGACY_U8_MAX, 0U, LEGACY_U8_SIGN_BIT,
															 TEST_OPPONENT_CAR_PATTERN};
	static const legacy_u8 track_name[TEST_TRACK_NAME_SIZE] = {
		'T', 'R', 'A', 'C', 'K', 0U, LEGACY_S8_MAX, LEGACY_U8_SIGN_BIT, LEGACY_U8_MAX};

	static const legacy_u8 player_car[TEST_CAR_ID_SIZE] = {'A', 'B', 'C', 'D'};
	for (legacy_u16 index = 0U; index < TEST_CAR_ID_SIZE; index++) {
		gameinfo->game_playercarid[index] = LEGACY_S8_FROM_BITS(player_car[index]);
		gameinfo->game_opponentcarid[index] = LEGACY_S8_FROM_BITS(opponent_car[index]);
	}
	gameinfo->game_playermaterial = LEGACY_S8_FROM_BITS(TEST_PLAYER_MATERIAL);
	gameinfo->game_playertransmission = LEGACY_S8_MAX;
	gameinfo->game_opponenttype = 6;
	gameinfo->game_opponentmaterial = 3;
	gameinfo->game_opponenttransmission = 1;
	for (legacy_u16 index = 0U; index < TEST_TRACK_NAME_SIZE; index++) {
		gameinfo->game_trackname[index] = LEGACY_S8_FROM_BITS(track_name[index]);
	}
	gameinfo->game_framespersec = TEST_FRAMES_PER_SECOND;
	gameinfo->game_recordedframes = TEST_RECORDED_FRAME_COUNT;
}

static void assert_gameinfo_equal(const struct GAMEINFO *left, const struct GAMEINFO *right)
{
	for (legacy_u16 index = 0U; index < TEST_CAR_ID_SIZE; index++) {
		assert(left->game_playercarid[index] == right->game_playercarid[index]);
		assert(left->game_opponentcarid[index] == right->game_opponentcarid[index]);
	}
	assert(left->game_playermaterial == right->game_playermaterial);
	assert(left->game_playertransmission == right->game_playertransmission);
	assert(left->game_opponenttype == right->game_opponenttype);
	assert(left->game_opponentmaterial == right->game_opponentmaterial);
	assert(left->game_opponenttransmission == right->game_opponenttransmission);
	for (legacy_u16 index = 0U; index < TEST_TRACK_NAME_SIZE; index++) {
		assert(left->game_trackname[index] == right->game_trackname[index]);
	}
	assert(left->game_framespersec == right->game_framespersec);
	assert(left->game_recordedframes == right->game_recordedframes);
}

static void test_replay_ui_math(void)
{
	assert(replay_timeline_position(0U, 0U, 110U) == 0U);
	assert(replay_timeline_position(50U, 100U, 110U) == 55U);
	assert(replay_timeline_position(100U, 100U, 110U) == 110U);
	assert(replay_timeline_position(101U, 100U, 110U) == 110U);

	assert(replay_rewind_interpolate(1U, 99U, 100U) == 0U);
	assert(replay_rewind_interpolate(100U, 50U, 100U) == 50U);
	assert(replay_rewind_interpolate(12000U, 50U, 100U) == 6000U);
	assert(replay_rewind_interpolate(5U, 1U, 0U) == 0U);

	for (legacy_u16 amount = 1U; amount <= 100U; amount++) {
		for (legacy_u16 distance = 1U; distance <= 100U; distance++) {
			for (legacy_u16 remaining = 0U; remaining <= distance; remaining++) {
				assert(replay_rewind_interpolate(amount, remaining, distance) <= amount);
			}
		}
	}
}

static void test_replay_file_size(void)
{
	assert(REPLAY_INPUT_OFFSET == TEST_EXPECTED_REPLAY_INPUT_OFFSET);
	assert(replay_file_size(0U) == TEST_EXPECTED_REPLAY_INPUT_OFFSET);
	assert(replay_file_size(TEST_SAMPLE_RECORDED_FRAMES) == TEST_SAMPLE_REPLAY_FILE_SIZE);
	assert(replay_file_size(LEGACY_U16_MAX) == TEST_MAXIMUM_REPLAY_FILE_SIZE);
}

int main(void)
{
	static const legacy_u8 expected[REPLAY_GAMEINFO_SIZE] = {'A',
															 'B',
															 'C',
															 'D',
															 TEST_PLAYER_MATERIAL,
															 LEGACY_S8_MAX,
															 6U,
															 LEGACY_U8_MAX,
															 0U,
															 LEGACY_U8_SIGN_BIT,
															 TEST_OPPONENT_CAR_PATTERN,
															 3U,
															 1U,
															 'T',
															 'R',
															 'A',
															 'C',
															 'K',
															 0U,
															 LEGACY_S8_MAX,
															 LEGACY_U8_SIGN_BIT,
															 LEGACY_U8_MAX,
															 TEST_FRAMES_PER_SECOND_LOW_BYTE,
															 TEST_FRAMES_PER_SECOND_HIGH_BYTE,
															 TEST_RECORDED_FRAME_COUNT_LOW_BYTE,
															 TEST_RECORDED_FRAME_COUNT_HIGH_BYTE};

	legacy_u8 storage[REPLAY_GAMEINFO_SIZE + TEST_STORAGE_PREFIX_SIZE];
	legacy_u8 *bytes = storage + TEST_STORAGE_PREFIX_SIZE;
	struct GAMEINFO encoded;
	fill_gameinfo(&encoded);
	replay_gameinfo_encode(bytes, &encoded);
	assert(memcmp(bytes, expected, REPLAY_GAMEINFO_SIZE) == 0);

	struct GAMEINFO decoded;
	memset(&decoded, 0, sizeof(decoded));
	replay_gameinfo_decode(&decoded, bytes);
	assert_gameinfo_equal(&encoded, &decoded);
	test_replay_file_size();
	test_replay_ui_math();
	return 0;
}
