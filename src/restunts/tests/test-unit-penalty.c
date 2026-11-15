/* Reuse the authored native branch/finish fixture and legacy fingerprint.
 * This target uses real UNIT observation instead of inactive runtime stubs. */
#define RESTUNTS_EXTENDED_REPLAY_TEST_STUBS_H
#define main native_penalty_fixture_main
#include "test-penalty-routing.c"
#undef main
#include "../c/extended/extended_replay_runtime.h"
#include "../c/extended/extended_statecrs.h"
#include "../c/crash_state.h"

#define TEST_UNIT_PENALTY_FRAMES 360U
#define TEST_UNIT_LARGE_PENALTY 70000U
#define TEST_UNIT_FINISH_FRAME 1000U
#define TEST_UNIT_PLAYER_PENALTY 80000U
#define TEST_UNIT_WORD_POSITION_SCALE 64
#define TEST_UNIT_GEOMETRY_POINT_COUNT 2
#define TEST_UNIT_TILE_POSITION_SCALE 65536L

struct GAMEINFO gameconfig;
struct EXTENDED_REPLAY_CLOCKS extended_replay_clocks;
legacy_s8 start_finish_column, start_finish_row;
/* Recovery tests use cardinal directions, which do not read this table. */
legacy_u8 atantable[257];
static legacy_int runtime_active;

legacy_int extended_replay_is_active(void)
{
	return runtime_active;
}
legacy_u32 extended_replay_frame(void)
{
	return extended_replay_clocks.frame;
}
legacy_int extended_track_active(void)
{
	return 0;
}
legacy_int extended_track_width(void)
{
	return 30;
}
legacy_int extended_track_height(void)
{
	return 30;
}
const struct CSTRK_OBJECT *extended_track_object_at(legacy_int column, legacy_int row)
{
	(void)column;
	(void)row;
	assert(0);
	return NULL;
}
legacy_uint extended_track_object_id(const struct CSTRK_OBJECT *object)
{
	(void)object;
	assert(0);
	return 0;
}
/* A straight northbound segment for native wrong-way direction recovery. */
legacy_s16 get_track_route_point(legacy_s16 piece, struct VECTOR *output, legacy_s16 point,
								 legacy_s8 *speed)
{
	(void)piece;
	(void)speed;
	assert(point >= 0 && point < TEST_UNIT_GEOMETRY_POINT_COUNT);
	memset(output, 0, ROUTE_GEOMETRY_POINT_COUNT * sizeof(*output));
	output->z = (point + 1) * TEST_UNIT_WORD_POSITION_SCALE;
	return point == TEST_UNIT_GEOMETRY_POINT_COUNT - 1;
}
static void reset_unit(void)
{
	reset_dualway_route();
	memset(&extended_replay_clocks, 0, sizeof(extended_replay_clocks));
	memset(&gameconfig, 0, sizeof(gameconfig));
	gameconfig.game_opponenttype = CSRPL_UNIT_OPPONENT_TYPE;
	start_finish_column = columns[0];
	start_finish_row = rows[0];
	runtime_active = 1;
	configure_switching("--dws:off");
	extended_replay_clocks.penalty = TEST_UNIT_PLAYER_PENALTY;
	penalty_time = 17;
	show_penalty_counter = 23;
}
static void observe_piece(legacy_s16 piece)
{
	state.opponentstate.car_position.lx = (legacy_s32)columns[piece] * 65536L;
	state.opponentstate.car_position.lz = (29L - rows[piece]) * 65536L;
	struct GAMESTATE before = state;
	struct LEGACY_EXECUTION_RESIDUE residue = legacy_execution_residue;
	extended_unit_penalty_update();
	assert(memcmp(&state, &before, sizeof(state)) == 0);
	assert(memcmp(&residue, &legacy_execution_residue, sizeof(residue)) == 0);
	assert(penalty_time == 17 && show_penalty_counter == 23);
	assert(extended_replay_clocks.penalty == TEST_UNIT_PLAYER_PENALTY);
}
static void test_independent_confirmation_and_restore(void)
{
	reset_unit();
	observe_piece(1);
	assert(extended_replay_clocks.unit_penalty.route.confirmed_route == 1);
	assert(extended_unit_penalty_shortcuts_allowed());
	observe_piece(6);
	assert(extended_unit_penalty_total() == 0);
	assert(extended_unit_penalty_recovery_required());
	struct EXTENDED_REPLAY_CLOCKS saved = extended_replay_clocks;
	observe_piece(7);
	assert(extended_unit_penalty_total() == 0);
	observe_piece(16);
	assert(extended_unit_penalty_total() == TEST_UNIT_PENALTY_FRAMES);
	assert(!extended_unit_penalty_shortcuts_allowed());
	struct EXTENDED_REPLAY_CLOCKS final = extended_replay_clocks;
	extended_replay_clocks = saved;
	observe_piece(7);
	observe_piece(16);
	assert(memcmp(&final, &extended_replay_clocks, sizeof(final)) == 0);
	assert(state.game_player_confirmed_route == 3 && state.game_penalty == 0);
}
static void test_finish_and_saturation(void)
{
	reset_unit();
	observe_piece(1);
	observe_piece(6);
	struct EXTENDED_REPLAY_CLOCKS saved = extended_replay_clocks;
	extended_replay_clocks.unit_penalty.frames = TEST_UNIT_LARGE_PENALTY;
	observe_piece(7);
	observe_piece(16);
	legacy_u32 total = TEST_UNIT_LARGE_PENALTY + TEST_UNIT_PENALTY_FRAMES;
	assert(extended_unit_penalty_total() == total);
	extended_replay_clocks.frame = TEST_UNIT_FINISH_FRAME;
	extended_crash_record_finish(OPPONENT_CAR_INDEX);
	assert(extended_replay_clocks.opponent_finish == TEST_UNIT_FINISH_FRAME + total);
	assert((legacy_u16)extended_crash_opponent_finish_time(TEST_UNIT_FINISH_FRAME) ==
		   (legacy_u16)(TEST_UNIT_FINISH_FRAME + total));
	gameconfig.game_opponenttype = CSRPL_CLASSIC_OPPONENT_LAST;
	extended_crash_record_finish(OPPONENT_CAR_INDEX);
	assert(extended_replay_clocks.opponent_finish == TEST_UNIT_FINISH_FRAME);
	assert(extended_crash_opponent_finish_time(TEST_UNIT_FINISH_FRAME) == TEST_UNIT_FINISH_FRAME);
	gameconfig.game_opponenttype = CSRPL_UNIT_OPPONENT_TYPE;
	extended_replay_clocks = saved;
	extended_replay_clocks.unit_penalty.frames = LEGACY_U32_MAX - 1U;
	observe_piece(7);
	observe_piece(16);
	assert(extended_unit_penalty_total() == LEGACY_U32_MAX);
	extended_replay_clocks.frame = TEST_UNIT_FINISH_FRAME;
	extended_crash_record_finish(OPPONENT_CAR_INDEX);
	assert(extended_replay_clocks.opponent_finish == LEGACY_U32_MAX);
}
static void test_legal_and_outside_route(void)
{
	for (legacy_s16 branch = 0; branch < 2; branch++) {
		reset_unit();
		legacy_s16 piece = 1;
		while (piece) {
			observe_piece(piece);
			piece = branch && piece == 1 ? 8 : primary[piece];
		}
		observe_piece(0);
		assert(extended_unit_penalty_total() == 0);
		assert(extended_replay_clocks.unit_penalty.route.laps == 1);
	}
	reset_unit();
	observe_piece(1);
	state.opponentstate.car_position.lx = -65536L;
	extended_unit_penalty_update();
	assert(extended_unit_penalty_recovery_required());
	assert(extended_unit_penalty_total() == 0);
	observe_piece(2);
	assert(!extended_unit_penalty_recovery_required());
}
/* Returning to an already confirmed footprint leaves the native pending
 * counter intact until the next piece. It no longer means UNIT is off route. */
static void test_recovery_after_returning_to_confirmed_piece(void)
{
	reset_unit();
	observe_piece(1);
	state.opponentstate.car_position.lz -= TEST_UNIT_TILE_POSITION_SCALE;
	extended_unit_penalty_update();
	assert(extended_unit_penalty_recovery_required());
	observe_piece(1);
	observe_piece(1);
	const struct RACE_PENALTY_STATE *rules = &extended_replay_clocks.unit_penalty.route;
	assert(rules->status == ROUTE_TRACKING_NORMAL);
	assert(rules->confirmed_route == 1 && rules->previous_route == 1);
	assert(rules->last_skipped == 0 && rules->confirmations == ROUTE_CONFIRMATION_INITIAL);
	assert(!extended_unit_penalty_recovery_required());
	assert(extended_unit_penalty_shortcuts_allowed());
	observe_piece(2);
	assert(!extended_unit_penalty_recovery_required() && !extended_unit_penalty_total());

	/* A cached footprint can also report zero distance while its skipped
	 * pieces remain unconfirmed; that case still requires recovery. */
	reset_unit();
	observe_piece(1);
	observe_piece(6);
	observe_piece(6);
	assert(rules->last_skipped == 0 && rules->confirmations == ROUTE_CONFIRMATION_INITIAL);
	assert(rules->confirmed_route == 1 && rules->previous_route == 6);
	assert(extended_unit_penalty_recovery_required());
	assert(!extended_unit_penalty_shortcuts_allowed());
	observe_piece(7);
	observe_piece(16);
	observe_piece(16);
	assert(!extended_unit_penalty_recovery_required());
	assert(extended_unit_penalty_total() == TEST_UNIT_PENALTY_FRAMES);
	assert(!extended_unit_penalty_shortcuts_allowed());
}
static void test_collision_motion_and_finished_freeze(void)
{
	reset_unit();
	observe_piece(1);
	state.opponentstate.car_crashBmpFlag = CRASH_EVENT_COLLISION;
	observe_piece(6);
	observe_piece(7);
	observe_piece(16);
	assert(extended_unit_penalty_total() == TEST_UNIT_PENALTY_FRAMES);
	struct EXTENDED_UNIT_PENALTY saved = extended_replay_clocks.unit_penalty;
	state.opponentstate.car_crashBmpFlag = CRASH_EVENT_FINISH;
	observe_piece(0);
	assert(memcmp(&saved, &extended_replay_clocks.unit_penalty, sizeof(saved)) == 0);
}

static void test_direction_recovery(void)
{
	reset_unit();
	observe_piece(1);
	struct RACE_PENALTY_STATE *rules = &extended_replay_clocks.unit_penalty.route;
	rules->status = ROUTE_TRACKING_WRONG_WAY;
	rules->previous_route = 4;
	struct CARSTATE car = {0};
	car.car_rotate.x = ANGLE_HALF_TURN;
	race_penalty_recover_direction(rules, &car);
	assert(rules->status == ROUTE_TRACKING_WRONG_WAY);
	car.car_rotate.x = 0;
	race_penalty_recover_direction(rules, &car);
	assert(rules->status == ROUTE_TRACKING_NORMAL);
	assert(rules->confirmations == ROUTE_CONFIRMATION_INITIAL);
}
legacy_int main(void)
{
	native_penalty_fixture_main();
	test_independent_confirmation_and_restore();
	test_finish_and_saturation();
	test_legal_and_outside_route();
	test_direction_recovery();
	test_recovery_after_returning_to_confirmed_piece();
	test_collision_motion_and_finished_freeze();
	puts("UNIT independent native penalties, recovery, snapshots and finish totals passed.");
	return 0;
}
