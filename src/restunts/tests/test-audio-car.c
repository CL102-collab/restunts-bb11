#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../c/audio_car.c"
#undef printf

legacy_s8 audio_car_state_ready, audio_player_car_flags, audio_opponent_car_flags;
legacy_s16 audio_player_engine_channel, audio_opponent_engine_channel;
legacy_s16 audio_car_state_read_index, audio_car_state_write_index;
legacy_s16 camera_track_height_offset;
legacy_u8 audio_previous_replay_mode;
struct AUDIO_CAR_STATE *audio_car_state_records;
static legacy_u64 trace_hash = UINT64_C(1469598103934665603);
static struct AUDIO_CAR_STATE records[AUDIO_CAR_STATE_RECORD_COUNT];
static struct VECTOR cameras[2];
static struct CARSTATE test_ghost_car;
static struct GHOST_CAMERA_STATE test_ghost_camera;
static legacy_s16 test_ghost_active;
static legacy_u32 audio_channel_calls[2];
static legacy_u32 ghost_motion_calls;
static legacy_s16 freeze_ghost_motion;

struct CARSTATE *ghost_car_state(void)
{
	return test_ghost_active != 0 && gameconfig.game_opponenttype == 0 ? &test_ghost_car : 0;
}

const struct GHOST_CAMERA_STATE *ghost_camera_state(void)
{
	return test_ghost_active != 0 && gameconfig.game_opponenttype == 0 ? &test_ghost_camera : 0;
}

void ghost_adjust_camera_motion(struct VECTOR *previous, const struct VECTOR *current)
{
	assert(test_ghost_active != 0);
	ghost_motion_calls++;
	if (freeze_ghost_motion != 0) {
		*previous = *current;
	}
}

static void trace_word(legacy_u16 value)
{
	trace_hash = (trace_hash ^ (value & 255U)) * UINT64_C(1099511628211);
	trace_hash = (trace_hash ^ (value >> 8)) * UINT64_C(1099511628211);
}
static void trace_audio_channel(legacy_s16 channel)
{
	assert(channel == audio_player_engine_channel || channel == audio_opponent_engine_channel);
	audio_channel_calls[channel == audio_opponent_engine_channel]++;
	trace_word(channel);
}
void audio_start_engine(legacy_s16 channel)
{
	trace_word(1);
	trace_audio_channel(channel);
}
void audio_stop_engine(legacy_s16 channel)
{
	trace_word(2);
	trace_audio_channel(channel);
}
void audio_play_paved_skid(legacy_s16 channel)
{
	trace_word(3);
	trace_audio_channel(channel);
}
void audio_play_offroad_skid(legacy_s16 channel)
{
	trace_word(4);
	trace_audio_channel(channel);
}
void audio_stop_skid_sound(legacy_s16 channel)
{
	trace_word(5);
	trace_audio_channel(channel);
}
void audio_reset_channels(void)
{
	trace_word(6);
}
static void record_result(void)
{
	const legacy_u8 *bytes = (const legacy_u8 *)records;
	trace_word(audio_car_state_ready);
	trace_word(audio_player_car_flags);
	trace_word(audio_opponent_car_flags);
	trace_word(audio_car_state_read_index);
	trace_word(audio_car_state_write_index);
	trace_word(audio_previous_replay_mode);
	for (legacy_u32 i = 0; i < sizeof(records); i++) {
		trace_word(bytes[i]);
	}
}
static void reset_audio_car(legacy_u32 index)
{
	test_ghost_active = 0;
	ghost_motion_calls = 0;
	memset(audio_channel_calls, 0, sizeof(audio_channel_calls));
	memset(&state, 0, sizeof(state));
	memset(records, 0x5a, sizeof(records));
	audio_car_state_records = records;
	audio_car_state_read_index = 7;
	audio_car_state_write_index = 39;
	audio_player_engine_channel = 17;
	audio_opponent_engine_channel = 21;
	audio_car_state_ready = index % 2U;
	audio_previous_replay_mode = index % 3U;
	state.playerstate.car_previous_position.lx = 0x7ffffff0L;
	state.playerstate.car_previous_position.ly = -0x12345L;
	state.playerstate.car_previous_position.lz = 0x87654L;
	state.playerstate.car_position.lx = -0x7fffffffL;
	state.playerstate.car_position.ly = 0x10002L;
	state.playerstate.car_position.lz = -1;
	state.opponentstate.car_previous_position.lx = -1;
	state.opponentstate.car_previous_position.ly = -33;
	state.opponentstate.car_previous_position.lz = 32;
	state.opponentstate.car_position.lx = 12345;
	state.opponentstate.car_position.ly = 23456;
	state.opponentstate.car_position.lz = -34567;
	state.playerstate.car_currpm = 32767;
	state.opponentstate.car_currpm = -32768;
	cameras[0].x = 32760;
	cameras[0].y = -32760;
	cameras[0].z = 13;
	cameras[1].x = -27;
	cameras[1].y = 32760;
	cameras[1].z = -40;
	trackside_camera_positions = cameras;
	camera_track_height_offset = 32760;
	state.game_follow_camera_position[0] = cameras[0];
	state.game_follow_camera_position[1] = cameras[1];
	state.game_player_camera_previous = cameras[1];
	state.game_opponent_camera_previous = cameras[0];
	state.game_trackside_camera_index[0] = 0;
	state.game_trackside_camera_index[1] = 1;
	trace_word(index);
}
static void test_recording_modes(void)
{
	legacy_u32 index = 0;
	for (legacy_u32 mode = 0; mode < 6U; mode++) {
		for (legacy_u32 opponent = 0; opponent < 2U; opponent++) {
			for (legacy_u32 follow = 0; follow <= opponent; follow++) {
				for (legacy_u32 flags = 0; flags < 16U; flags++) {
					reset_audio_car(index++);
					gameconfig.game_opponenttype = opponent;
					followOpponentFlag = follow;
					cameramode = mode;
					is_in_replay = 0;
					audio_player_car_flags = flags;
					audio_opponent_car_flags = flags ^ 7U;
					state.playerstate.car_sound_flags = flags ^ 15U;
					state.opponentstate.car_sound_flags = flags;
					audio_carstate();
					record_result();
					audio_carstate();
					record_result();
				}
			}
		}
	}
}
static void test_replay_shutdown(void)
{
	legacy_u32 index = 400;
	for (legacy_u32 replay = 1; replay <= 2U; replay++) {
		for (legacy_u32 opponent = 0; opponent < 2U; opponent++) {
			for (legacy_u32 flags = 0; flags < 16U; flags++) {
				for (legacy_u32 ready = 0; ready < 2U; ready++) {
					reset_audio_car(index++);
					gameconfig.game_opponenttype = opponent;
					is_in_replay = replay;
					audio_player_car_flags = flags;
					audio_opponent_car_flags = flags ^ 7U;
					audio_car_state_ready = ready;
					audio_carstate();
					record_result();
					audio_carstate();
					record_result();
				}
			}
		}
	}
}
static void assert_audio_vector(const struct VECTOR *actual, const struct VECTOR *expected)
{
	assert(LEGACY_READ_S16_LE((const legacy_u8 *)&actual->x) == expected->x);
	assert(LEGACY_READ_S16_LE((const legacy_u8 *)&actual->y) == expected->y);
	assert(LEGACY_READ_S16_LE((const legacy_u8 *)&actual->z) == expected->z);
}

static void test_ghost_camera_audio(void)
{
	static const struct VECTOR ghost_previous[CAMERA_MODE_COUNT] = {
		{3, 3, 3}, {39, 43, 52}, {3, 3, 3}, {99, 308, 297}};
	static const struct VECTOR ghost_current[CAMERA_MODE_COUNT] = {
		{5, 5, 5}, {48, 57, 66}, {5, 5, 5}, {98, 307, 296}};
	static const struct VECTOR player_previous[CAMERA_MODE_COUNT] = {
		{0, 0, 0}, {9, 13, 22}, {0, 0, 0}, {299, 508, 497}};
	static const struct VECTOR player_current[CAMERA_MODE_COUNT] = {
		{0, 0, 0}, {18, 27, 36}, {0, 0, 0}, {298, 507, 496}};
	static const struct VECTOR untouched = {0x5a5a, 0x5a5a, 0x5a5a};

	for (legacy_u32 mode = 0; mode < CAMERA_MODE_COUNT; mode++) {
		/* Also cover returning to the player and losing the cached ghost while
		 * the view flag is still set: neither may use the empty opponent state. */
		for (legacy_u32 view = 0; view < 3; view++) {
			for (freeze_ghost_motion = 0; freeze_ghost_motion < 2; freeze_ghost_motion++) {
				reset_audio_car(0);
				memset(&test_ghost_car, 0, sizeof(test_ghost_car));
				memset(&test_ghost_camera, 0, sizeof(test_ghost_camera));
				gameconfig.game_opponenttype = 0;
				is_in_replay = 0;
				cameramode = mode;
				followOpponentFlag = view != 1;
				test_ghost_active = view != 2;
				audio_player_car_flags = 0;
				audio_opponent_car_flags = 0;
				state.playerstate.car_sound_flags = CAR_SOUND_ENGINE_ACTIVE_FLAG;
				test_ghost_car.car_sound_flags =
					CAR_SOUND_ENGINE_ACTIVE_FLAG | CAR_SOUND_SKID_PAVED_FLAG;
				state.playerstate.car_previous_position = (struct VECTORLONG){64, 128, 192};
				state.playerstate.car_position = (struct VECTORLONG){128, 192, 256};
				test_ghost_car.car_previous_position = (struct VECTORLONG){256, 320, 384};
				test_ghost_car.car_position = (struct VECTORLONG){448, 512, 576};
				state.game_follow_camera_position[0] = (struct VECTOR){20, 30, 40};
				state.game_player_camera_previous = (struct VECTOR){10, 15, 25};
				test_ghost_camera.follow_position = (struct VECTOR){50, 60, 70};
				test_ghost_camera.previous_position = (struct VECTOR){40, 45, 55};
				test_ghost_camera.trackside_index = 1;
				cameras[0] = (struct VECTOR){300, 400, 500};
				cameras[1] = (struct VECTOR){100, 200, 300};
				camera_track_height_offset = 20;
				audio_carstate();

				const struct AUDIO_CAR_STATE *record = &records[39];
				struct VECTOR expected_previous =
					view == 0 ? ghost_previous[mode] : player_previous[mode];
				if (view == 0 && freeze_ghost_motion != 0) {
					expected_previous = ghost_current[mode];
					/* The player moved one coordinate unit on each axis this tick. */
					expected_previous.x++;
					expected_previous.y++;
					expected_previous.z++;
				}
				assert_audio_vector(&record->player_previous, &expected_previous);
				assert_audio_vector(&record->player_current,
									view == 0 ? &ghost_current[mode] : &player_current[mode]);
				assert(LEGACY_READ_S16_LE((const legacy_u8 *)&record->player_rpm) ==
					   state.playerstate.car_currpm);
				assert_audio_vector(&record->opponent_previous, &untouched);
				assert_audio_vector(&record->opponent_current, &untouched);
				assert(record->opponent_rpm == 0x5a5a);
				assert(audio_channel_calls[0] == 1);
				assert(audio_channel_calls[1] == 0);
				assert(audio_opponent_car_flags == 0);
				assert(audio_car_state_write_index == 0);
				assert(ghost_motion_calls == (view == 0 ? 1U : 0U));
			}
		}
	}
}

int main(void)
{
	test_recording_modes();
	test_replay_shutdown();
	assert(trace_hash == UINT64_C(0xafadd350106660b7));
	test_ghost_camera_audio();
	printf("test-audio-car: passed\n");
	return 0;
}
