#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../c/externs.h"
#include "../c/race_route.h"
#include "../c/residue.h"
#include "../c/track_objects.h"

struct GAMESTATE state;
legacy_u16 framespersec = 20;
legacy_s16 penalty_time;
legacy_s8 show_penalty_counter;
struct LEGACY_EXECUTION_RESIDUE legacy_execution_residue;
struct TRACKOBJECT trkObjectList[215];
legacy_s16 track_pieces_counter;
legacy_s16 far *track_primary_route_links;
legacy_s16 far *track_alternate_route_links;
legacy_s8 far *track_route_columns;
legacy_s8 far *track_route_rows;
legacy_s8 far *track_route_element_ids;
legacy_s8 far *replay_input_buffer;
legacy_s8 far *track_and_directory_backup;

static legacy_s16 primary[904], alternate[904];
static legacy_s8 columns[904], rows[904], elements[904];
static legacy_s8 replay[12000], backup[1964];
static legacy_u32 random_state = 1;

static legacy_u16 random_word(void)
{
	random_state = random_state * 1664525UL + 1013904223UL;
	return (legacy_u16)(random_state >> 16);
}

static void reset_route(legacy_s16 column, legacy_s16 row)
{
	memset(&state, 0, sizeof(state));
	memset(trkObjectList, 0, sizeof(trkObjectList));
	memset(columns, 0, sizeof(columns));
	memset(rows, 0, sizeof(rows));
	memset(elements, 0, sizeof(elements));
	memset(replay, 0, sizeof(replay));
	memset(backup, 0, sizeof(backup));
	track_primary_route_links = primary;
	track_alternate_route_links = alternate;
	track_route_columns = columns;
	track_route_rows = rows;
	track_route_element_ids = elements;
	replay_input_buffer = replay;
	track_and_directory_backup = backup;
	track_pieces_counter = 12;
	legacy_execution_residue.penalty_route_word = -1;
	for (legacy_u32 i = 0; i < 904; i++) {
		primary[i] = -1;
		alternate[i] = -1;
	}
	state.game_startcol = state.game_startcol2 = -1;
	state.game_startrow = state.game_startrow2 = -1;
	state.playerstate.car_position.lx = (legacy_s32)column * 65536L;
	state.playerstate.car_position.lz = (29L - row) * 65536L;
}

static void test_route_boundaries(void)
{
	reset_route(30, 5);
	legacy_s16 penalty = 99;
	legacy_s16 current = 0;
	assert(detect_penalty(&current, &penalty) == 1);
	assert(penalty == PENALTY_ROUTE_OUTSIDE_TRACK);
	assert(current == 0 && state.game_startcol == -1);
	reset_route(5, 5);
	primary[0] = 1;
	columns[1] = rows[1] = 5;
	assert(detect_penalty(&current, &penalty) == 1);
	assert(current == 1 && penalty == 0);
	assert(detect_penalty(&current, &penalty) == 0);
	assert(current == 1 && penalty == 0);
	reset_route(5, 5);
	current = 0;
	columns[900] = 5;
	backup[1963] = 5;
	assert(detect_penalty(&current, &penalty) == 1);
	assert(current == -1 && penalty == 0);
}

static void test_branch_and_finish(void)
{
	reset_route(6, 6);
	primary[0] = 1;
	alternate[0] = 3;
	primary[1] = 1;
	primary[3] = 4;
	columns[4] = rows[4] = 5;
	elements[4] = 1;
	trkObjectList[1].ss_multiTileFlag = 3;
	legacy_s16 penalty;
	legacy_s16 current = 0;
	assert(detect_penalty(&current, &penalty) == 1);
	assert(current == 4 && penalty == 0);
	assert(state.game_startcol == 5 && state.game_startcol2 == 6);
	assert(state.game_startrow == 5 && state.game_startrow2 == 6);
	reset_route(5, 5);
	current = 1;
	primary[1] = 0;
	primary[0] = 2;
	columns[2] = rows[2] = 5;
	assert(detect_penalty(&current, &penalty) == 1);
	assert(current == 2 && penalty == PENALTY_ROUTE_FINISH_REACHED);
}

static void configure_switching(const legacy_char *option)
{
	legacy_s8 *arguments[] = {(legacy_s8 *)"restunts", (legacy_s8 *)option};
	configure_dual_way_switching(option != NULL ? 2 : 1, arguments);
}

static void place_on_route(legacy_s16 piece)
{
	state.playerstate.car_position.lx = (legacy_s32)columns[piece] * 65536L;
	state.playerstate.car_position.lz = (29L - rows[piece]) * 65536L;
}

/* Two legal alternatives share a split footprint (2/8), then rejoin at 16.
 * The finish is 0. This exercises the actual runtime route graph rather than
 * relying on route IDs alone to decide whether a branch was driven. */
static void reset_dualway_route(void)
{
	reset_route(3, 5);
	track_pieces_counter = 19;
	for (legacy_s16 index = 0; index < track_pieces_counter; index++) {
		primary[index] = index + 1;
		columns[index] = (legacy_s8)index;
		rows[index] = 5;
	}
	primary[7] = 16;
	alternate[1] = 8;
	columns[8] = columns[2];
	primary[18] = 0;
	state.game_player_confirmed_route = 3;
	state.game_player_previous_route = 3;
	state.game_player_route_status = ROUTE_TRACKING_NORMAL;
}

static void test_switching_penalty_time(void)
{
	for (legacy_s16 fixed = 0; fixed < 2; fixed++) {
		configure_switching(fixed ? "--dws:off" : "--dws:on");
		reset_dualway_route();
		for (legacy_s16 piece = 13; piece <= 18; piece++) {
			place_on_route(piece);
			update_player_route_penalty();
			if (fixed && piece < 16) {
				assert(state.game_player_confirmed_route == 3);
				assert(state.game_player_route_status == ROUTE_TRACKING_OUTSIDE_TRACK);
			}
		}
		assert(state.game_penalty == (fixed ? 18 * 20 : 0));
		place_on_route(0);
		update_player_route_penalty();
		assert(state.playerstate.car_lap_count == 1);
		assert(state.game_penalty == (fixed ? 18 * 20 : 0));
	}
	/* The opposite switch skips a longer section of the chosen branch. */
	for (legacy_s16 fixed = 0; fixed < 2; fixed++) {
		configure_switching(fixed ? "--dws:off" : "--dws:on");
		reset_dualway_route();
		state.game_player_confirmed_route = state.game_player_previous_route = 9;
		legacy_s16 piece = 4;
		while (piece != 0) {
			place_on_route(piece);
			update_player_route_penalty();
			piece = primary[piece];
		}
		place_on_route(0);
		update_player_route_penalty();
		assert(state.playerstate.car_lap_count == 1);
		assert(state.game_penalty == (fixed ? 24 * 20 : 0));
	}
}

static void test_fixed_legal_routes(void)
{
	configure_switching("--dws:off");
	for (legacy_s16 branch = 0; branch < 2; branch++) {
		reset_dualway_route();
		state.game_player_confirmed_route = 0;
		state.game_player_previous_route = 0;
		legacy_s16 piece = 1;
		while (piece != 0) {
			place_on_route(piece);
			update_player_route_penalty();
			piece = branch && piece == 1 ? 8 : primary[piece];
		}
		place_on_route(0);
		update_player_route_penalty();
		assert(state.playerstate.car_lap_count == 1);
		assert(state.game_penalty == 0);
	}
	/* A second split whose primary reaches finish must still explore its
	 * pending legal alternative. A nested split is legal on that path too. */
	reset_dualway_route();
	primary[3] = 0;
	alternate[3] = 8;
	alternate[9] = 11;
	primary[11] = 12;
	place_on_route(12);
	legacy_s16 current = 3, penalty;
	assert(detect_penalty(&current, &penalty) == 1);
	assert(current == 12 && penalty >= 0);
	/* Ordinary missed-piece penalties and the last-piece finish exception
	 * continue to use the original chain length and seconds-per-piece. */
	reset_dualway_route();
	state.game_player_confirmed_route = state.game_player_previous_route = 1;
	for (legacy_s16 piece = 6; piece <= 7; piece++) {
		place_on_route(piece);
		update_player_route_penalty();
	}
	place_on_route(16);
	update_player_route_penalty();
	assert(state.game_penalty == 18 * 20);
	reset_dualway_route();
	state.game_player_confirmed_route = state.game_player_previous_route = 17;
	place_on_route(0);
	update_player_route_penalty();
	assert(state.game_penalty == 3 * 20);
	assert(state.playerstate.car_lap_count == 1);
	/* Going backwards remains a wrong-way event rather than a free switch. */
	reset_dualway_route();
	state.game_player_confirmed_route = state.game_player_previous_route = 5;
	place_on_route(4);
	update_player_route_penalty();
	assert(state.game_player_route_status == ROUTE_TRACKING_WRONG_WAY);
	assert(state.game_penalty == 0);
	/* A sentinel's aliased memory must not turn into an extra legal path. */
	reset_route(5, 5);
	columns[900] = backup[1963] = 5;
	legacy_execution_residue.penalty_route_word = 2;
	current = 0;
	assert(detect_penalty(&current, &penalty) == 1);
	assert(current == 0 && penalty == PENALTY_ROUTE_OUTSIDE_TRACK);
}

static void test_switching_options(void)
{
	static const legacy_char *options[] = {NULL,	  "--dws:on", "--DWS:OFF", "--dws:offx",
										   "dws:off", "--dws:",	  "--dws:off "};
	for (legacy_u16 index = 0; index < sizeof(options) / sizeof(options[0]); index++) {
		configure_switching(options[index]);
		reset_dualway_route();
		place_on_route(13);
		legacy_s16 current = 3, penalty;
		assert(detect_penalty(&current, &penalty) == 1);
		assert(current == (index == 2 ? 3 : 13));
		assert(penalty ==
			   (index == 2 ? PENALTY_ROUTE_OUTSIDE_TRACK : PENALTY_ROUTE_FINISH_REACHED));
	}
	legacy_s8 *arguments[] = {(legacy_s8 *)"restunts", (legacy_s8 *)"--dws:off",
							  (legacy_s8 *)"--DWS:ON"};
	configure_dual_way_switching(3, arguments);
	reset_dualway_route();
	place_on_route(13);
	legacy_s16 current = 3, penalty;
	assert(detect_penalty(&current, &penalty) == 1);
	assert(current == 13 && penalty == PENALTY_ROUTE_FINISH_REACHED);
	arguments[1] = (legacy_s8 *)"--dws:on";
	arguments[2] = (legacy_s8 *)"--dws:off";
	configure_dual_way_switching(3, arguments);
	reset_dualway_route();
	place_on_route(13);
	current = 3;
	assert(detect_penalty(&current, &penalty) == 1);
	assert(current == 3 && penalty == PENALTY_ROUTE_OUTSIDE_TRACK);
}

static legacy_u32 route_fingerprint(void)
{
	legacy_s16 penalty;
	legacy_u32 hash = 2166136261UL;
	legacy_s16 current;
	for (legacy_u32 sample = 0; sample < 8192; sample++) {
		legacy_s16 column = random_word() % 34 - 2;
		legacy_s16 row = random_word() % 34 - 2;
		reset_route(column, row);
		for (legacy_u32 i = 0; i < 12; i++) {
			primary[i] = random_word() % 15 - 2;
			alternate[i] = random_word() % 13 - 1;
			columns[i] = random_word() % 30;
			rows[i] = random_word() % 30;
			elements[i] = random_word() % 4;
			trkObjectList[i % 4].ss_multiTileFlag = i % 4;
		}
		columns[900] = random_word() % 30;
		backup[1963] = random_word() % 30;
		replay[11999] = random_word() % 4;
		primary[900] = random_word() % 13 - 1;
		legacy_execution_residue.penalty_route_word = random_word() % 13 - 1;
		current = random_word() % 13 - 1;
		penalty = LEGACY_S16_FROM_BITS(random_word());
		legacy_s16 result = detect_penalty(&current, &penalty);
		hash = (hash ^ (legacy_u16)result) * 16777619UL;
		hash = (hash ^ (legacy_u16)current) * 16777619UL;
		hash = (hash ^ (legacy_u16)penalty) * 16777619UL;
		hash = (hash ^ (legacy_u8)state.game_startcol) * 16777619UL;
		hash = (hash ^ (legacy_u8)state.game_startcol2) * 16777619UL;
		hash = (hash ^ (legacy_u8)state.game_startrow) * 16777619UL;
		hash = (hash ^ (legacy_u8)state.game_startrow2) * 16777619UL;
	}
	return hash;
}

int main(void)
{
	test_route_boundaries();
	test_branch_and_finish();
	test_switching_penalty_time();
	test_fixed_legal_routes();
	test_switching_options();
	configure_switching(NULL);
#ifndef PENALTY_ROUTING_FEATURE_ONLY
	legacy_u32 hash = route_fingerprint();
#ifdef PHYSICS_RECORD_BASELINE
	fprintf(stdout, "%08" LEGACY_PRIx32 "\n", hash);
#else
	/* Deterministic inputs draw each random word once before signed conversion. */
	assert(hash == 0xbfbfb6ddUL);
#endif
#endif
#ifndef PHYSICS_RECORD_BASELINE
	puts("Dual-Way Switching penalty routing tests passed (on/default: 0s; off: 18s/24s).");
#endif
	return 0;
}
