#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../genghost/maneuvers.h"
#include "../genghost/simulation.h"
#include "../c/externs.h"
#include "../c/cstrk.h"
#include "../c/game_input.h"
#include "../c/memmgr.h"
#include "../c/track_objects.h"
#include "../c/trackdata_layout.h"
#include "../c/track_collision.h"

#undef memcpy
#undef printf

#define EXTENDED_MANEUVER_POINT_COUNT 4U
#define EXTENDED_MANEUVER_ROUTE_COUNT 3U
#define EXTENDED_MANEUVER_FIRST_COLUMN 5U
#define EXTENDED_MANEUVER_FIRST_ROW 5U
#define EXTENDED_MANEUVER_BODY_HALF_WIDTH 30
#define EXTENDED_MANEUVER_BODY_HALF_LENGTH 60
#define EXTENDED_MANEUVER_TEST_SPEED 100U
#define EXTENDED_MANEUVER_POSITION_SCALE 64L
#define EXTENDED_MANEUVER_FLIGHT_STEP 4L
#define EXTENDED_MANEUVER_ROLL_VARIANTS 3U
#define EXTENDED_MANEUVER_ROAD_ID 4U

static void prepare_route(struct GENGHOST_ROUTE *route,
						  struct GENGHOST_WAYPOINT points[EXTENDED_MANEUVER_POINT_COUNT],
						  legacy_int pieces[EXTENDED_MANEUVER_ROUTE_COUNT])
{
	memset(route, 0, sizeof(*route));
	memset(points, 0, EXTENDED_MANEUVER_POINT_COUNT * sizeof(*points));
	for (legacy_uint piece = 0; piece < EXTENDED_MANEUVER_ROUTE_COUNT; piece++) {
		track_route_columns[piece] = EXTENDED_MANEUVER_FIRST_COLUMN;
		track_route_rows[piece] = EXTENDED_MANEUVER_FIRST_ROW;
		track_route_element_ids[piece] = EXTENDED_MANEUVER_ROAD_ID;
		track_route_traversal_flags[piece] = 0;
		pieces[piece] = piece;
	}
	for (legacy_uint point = 0; point < EXTENDED_MANEUVER_POINT_COUNT; point++) {
		points[point].distance = point * TRACK_TILE_SIZE;
		points[point].piece = point < 2U ? point : point - 1U;
		points[point].piece_index = points[point].piece;
		points[point].physical_model = PHYSICAL_MODEL_ROAD;
		points[point].speed = EXTENDED_MANEUVER_TEST_SPEED;
	}
	route->pieces = pieces;
	route->piece_count = EXTENDED_MANEUVER_ROUTE_COUNT;
	route->points = points;
	route->point_count = EXTENDED_MANEUVER_POINT_COUNT;
	route->length = points[EXTENDED_MANEUVER_POINT_COUNT - 1U].distance;
}

static void test_barrel_proposals(void)
{
	struct GENGHOST_ROUTE route;
	struct GENGHOST_WAYPOINT points[EXTENDED_MANEUVER_POINT_COUNT];
	legacy_int pieces[EXTENDED_MANEUVER_ROUTE_COUNT];
	prepare_route(&route, points, pieces);
	track_route_element_ids[1] = CSTRK_ROLL_RAMP_FIRST_ID;
	points[1].physical_model = points[2].physical_model = PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT;
	points[1].roll_ramp = points[2].roll_ramp = 1;
	state.playerstate.car_actual_speed = EXTENDED_MANEUVER_TEST_SPEED << LEGACY_BYTE_BITS;
	state.playerstate.car_sumSurfAllWheels = CARSTATE_WHEEL_COUNT;
	struct GENGHOST_POLICY policy;
	genghost_policy_default(&policy);
	struct GAMESTATE before = state;
	for (legacy_uint owoot = 0; owoot <= 1U; owoot++) {
		struct GENGHOST_DRIVER driver;
		genghost_driver_reset(&driver);
		struct GENGHOST_MANEUVER actions[GENGHOST_MANEUVER_CAPACITY];
		size_t count = genghost_maneuvers(&route, &driver, &policy, 0, 0, owoot, actions,
										  GENGHOST_MANEUVER_CAPACITY);
		legacy_uint barrels = 0;
		for (size_t index = 0; index < count; index++) {
			const struct GENGHOST_MANEUVER *action = &actions[index];
			if (action->kind == GENGHOST_MANEUVER_BARREL_ROLL) {
				barrels++;
				assert(action->road_only);
				assert(action->gene.stunt_mode == GENGHOST_STUNT_BARREL);
				assert(action->gene.line_mode == 0 && action->gene.shortcut_skip == 0);
				assert(action->gene.lateral_offset == 0.0);
				assert(action->approach_speed > 0.0);
				assert(action->horizon_frames > GENGHOST_FRAME_RATE);
			}
			if (action->approach.roll_ramp) {
				assert(action->kind != GENGHOST_MANEUVER_STUNT);
				assert(action->kind != GENGHOST_MANEUVER_STUNT_ALTERNATE);
				assert(action->kind != GENGHOST_MANEUVER_LAUNCH);
			}
		}
		assert(barrels == EXTENDED_MANEUVER_ROLL_VARIANTS);
		assert(memcmp(&before, &state, sizeof(before)) == 0);
	}
}

static void test_combined_slalom_roof(void)
{
	struct GENGHOST_ROUTE route;
	struct GENGHOST_WAYPOINT points[EXTENDED_MANEUVER_POINT_COUNT];
	legacy_int pieces[EXTENDED_MANEUVER_ROUTE_COUNT];
	prepare_route(&route, points, pieces);
	track_route_element_ids[1] = CSTRK_SLALOM_FIRST_ID;
	points[1].physical_model = points[2].physical_model = PHYSICAL_MODEL_SLALOM;
	legacy_f64 origin_x, origin_z;
	legacy_s16 orientation;
	genghost_route_piece_frame(1, &origin_x, &origin_z, &orientation);
	assert(orientation == 0);
	points[1].x = points[2].x = origin_x;
	points[1].z = origin_z + SLALOM_POLE_FAR_Z;
	points[2].z = origin_z - SLALOM_POLE_FAR_Z;
	state.playerstate.car_position.lx = (legacy_s32)(origin_x * EXTENDED_MANEUVER_POSITION_SCALE);
	state.playerstate.car_position.lz = (legacy_s32)(origin_z * EXTENDED_MANEUVER_POSITION_SCALE);
	state.playerstate.car_previous_position = state.playerstate.car_position;
	state.playerstate.car_previous_position.lz +=
		EXTENDED_MANEUVER_FLIGHT_STEP * EXTENDED_MANEUVER_POSITION_SCALE;
	state.playerstate.car_actual_speed = EXTENDED_MANEUVER_TEST_SPEED << LEGACY_BYTE_BITS;
	state.playerstate.car_sumSurfAllWheels = CAR_WHEEL_CONTACT_NONE;
	simd_player.collide_points[0].px = EXTENDED_MANEUVER_BODY_HALF_WIDTH;
	simd_player.collide_points[1].px = EXTENDED_MANEUVER_BODY_HALF_LENGTH;
	struct GENGHOST_POLICY policy;
	genghost_policy_default(&policy);
	struct GAMESTATE before = state;
	for (legacy_uint combined = 0; combined <= 1U; combined++) {
		points[1].tunnel_slalom = points[2].tunnel_slalom = combined;
		struct GENGHOST_DRIVER driver;
		genghost_driver_reset(&driver);
		struct GENGHOST_MANEUVER actions[GENGHOST_MANEUVER_CAPACITY];
		size_t count = genghost_maneuvers(&route, &driver, &policy, 0, 0, 0, actions,
										  GENGHOST_MANEUVER_CAPACITY);
		legacy_uint centers = 0, jumps = 0;
		for (size_t index = 0; index < count; index++) {
			centers += actions[index].kind == GENGHOST_MANEUVER_SLALOM_CENTER;
			jumps += actions[index].kind == GENGHOST_MANEUVER_SLALOM_JUMP;
		}
		assert(centers == 1U);
		assert(jumps == (combined ? 0U : 1U));
		assert(memcmp(&before, &state, sizeof(before)) == 0);
	}
}

int main(void)
{
	mmgr_init_conventional_arena();
	init_row_tables();
	init_trackdata();
	test_barrel_proposals();
	test_combined_slalom_roof();
	puts("Extended maneuvers preserve barrel approaches and combined-slalom roof clearance.");
	return 0;
}
