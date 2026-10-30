#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../c/owoot.c"
#include "../c/owoot_wheels.c"

static legacy_s16 route_valid = 1;
static legacy_s16 jump_valid = 0;
static legacy_s16 road_wheel = -1;
static legacy_u32 road_queries;
static legacy_u32 expected_contact_mask;
static legacy_u32 route_queries;
static legacy_u32 jump_queries;
static legacy_u32 crashes;

legacy_s16 owoot_route_is_valid(struct CARSTATE *car, legacy_s16 allowed_jump)
{
	(void)car;
	assert(allowed_jump == jump_valid);
	route_queries++;
	return route_valid;
}

legacy_s16 owoot_jump_is_valid(struct CARSTATE *car)
{
	(void)car;
	jump_queries++;
	return jump_valid;
}

legacy_s16 track_road_overlaps_wheel(const struct VECTOR *vertices, legacy_u16 ring_count,
									 legacy_s16 road_contact)
{
	(void)vertices;
	assert(ring_count == 16);
	assert((legacy_u32)road_contact == ((expected_contact_mask >> (road_queries % 4U)) & 1U));
	return (legacy_s16)road_queries++ == road_wheel;
}

void update_crash_state(legacy_s16 event, legacy_s16 car_index)
{
	assert(event == CRASH_EVENT_COLLISION);
	assert(car_index == PLAYER_CAR_INDEX);
	state.playerstate.car_crashBmpFlag = (legacy_s8)event;
	crashes++;
}

void fatal_error(const legacy_s8 *format, ...)
{
	(void)format;
	abort();
}

static void set_mode(const legacy_char *option)
{
	legacy_s8 *arguments[] = {(legacy_s8 *)"restunts.exe", (legacy_s8 *)option};
	configure_owoot(option ? 2 : 1, arguments);
}

static void reset_player(void)
{
	memset(&state, 0, sizeof(state));
	state.game_inputmode = GAME_INPUT_MODE_ACTIVE;
	road_queries = route_queries = jump_queries = crashes = 0;
	expected_contact_mask = 0;
	route_valid = 1;
	jump_valid = 0;
	road_wheel = -1;
}

static void test_owoot_options(void)
{
	static const struct {
		const legacy_char *option;
		legacy_s16 expected_enabled;
	} cases[] = {{NULL, 0},		  {"--owoot", 1},	{"--OWOOT", 1},
				 {"--OwOoT", 1},  {"/owoot", 0},	{"/OWOOT", 0},
				 {"owoot", 0},	  {"-owoot", 0},	{"--owoot-extra", 0},
				 {"--owoot ", 0}, {"--owoot:on", 0}};
	for (legacy_u16 index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		set_mode("--owoot");
		set_mode(cases[index].option);
		assert(owoot_enabled == cases[index].expected_enabled);
	}
	legacy_s8 *arguments[] = {(legacy_s8 *)"restunts", (legacy_s8 *)"--owoot",
							  (legacy_s8 *)"--nointro", (legacy_s8 *)"--OWOOT"};
	configure_owoot(sizeof(arguments) / sizeof(arguments[0]), arguments);
	assert(owoot_enabled);
	legacy_s8 *executable_only[] = {(legacy_s8 *)"--owoot"};
	configure_owoot(sizeof(executable_only) / sizeof(executable_only[0]), executable_only);
	assert(!owoot_enabled);
}

static void test_switch_and_enforcement(void)
{
	reset_player();
	set_mode("--OWOOT");
	assert(owoot_enabled);
	set_mode("--owoot-extra");
	assert(!owoot_enabled);
	set_mode(0);
	owoot_update_player(&state.playerstate, PLAYER_CAR_INDEX);
	assert(crashes == 0 && route_queries == 0);
	set_mode("--owoot");
	owoot_update_player(&state.opponentstate, OPPONENT_CAR_INDEX);
	assert(crashes == 0 && route_queries == 0);
	state.game_inputmode = GAME_INPUT_MODE_INTRO;
	owoot_update_player(&state.playerstate, PLAYER_CAR_INDEX);
	assert(crashes == 0 && route_queries == 0);
	state.game_inputmode = GAME_INPUT_MODE_ACTIVE;
	road_wheel = 3;
	expected_contact_mask = 7;
	state.playerstate.car_surfaceWhl[0] = CAR_SURFACE_PAVED;
	state.playerstate.car_surfaceWhl[1] = CAR_SURFACE_DIRT;
	state.playerstate.car_surfaceWhl[2] = CAR_SURFACE_ICE;
	state.playerstate.car_surfaceWhl[3] = CAR_SURFACE_GRASS;
	is_in_replay = 1;
	owoot_update_player(&state.playerstate, PLAYER_CAR_INDEX);
	assert(crashes == 0 && road_queries == 4);
	assert(route_queries == 1 && jump_queries == 1);
	road_wheel = -1;
	owoot_update_player(&state.playerstate, PLAYER_CAR_INDEX);
	assert(crashes == 1);
	owoot_update_player(&state.playerstate, PLAYER_CAR_INDEX);
	assert(crashes == 1);

	reset_player();
	jump_valid = 1;
	owoot_update_player(&state.playerstate, PLAYER_CAR_INDEX);
	assert(crashes == 0 && road_queries == 0);
	route_valid = 0;
	owoot_update_player(&state.playerstate, PLAYER_CAR_INDEX);
	assert(crashes == 1);
}

static void test_failed_stunt_cannot_use_road_fallback(void)
{
	reset_player();
	set_mode("--owoot");
	/* Ordinary road contact remains valid after an ordinary jump ends. */
	road_wheel = 0;
	expected_contact_mask = 1;
	state.playerstate.car_surfaceWhl[0] = CAR_SURFACE_PAVED;
	owoot_update_player(&state.playerstate, PLAYER_CAR_INDEX);
	assert(crashes == 0 && road_queries == 1 && route_queries == 1);
	/* An incomplete stunt at that same paved landing is a mandatory failure,
	 * even if its compiled route and every ordinary road check would pass. */
	road_queries = route_queries = jump_queries = 0;
	jump_valid = OWOOT_JUMP_FAILED;
	owoot_update_player(&state.playerstate, PLAYER_CAR_INDEX);
	assert(crashes == 1 && jump_queries == 1);
	assert(road_queries == 0 && route_queries == 0);
}

static void load_test_wheels(void)
{
	legacy_u8 shape[SHAPE3D_HEADER_SIZE + 32 * SHAPE3D_VERTEX_SIZE] = {0};
	shape[0] = 32;
	for (legacy_u32 wheel = 0; wheel < 4; wheel++) {
		for (legacy_u32 side = 0; side < 2; side++) {
			for (legacy_u32 point = 0; point < 3; point++) {
				legacy_u32 index = 8 + wheel * 6 + side * 3 + point;
				legacy_u8 *vertex = shape + SHAPE3D_HEADER_SIZE + index * SHAPE3D_VERTEX_SIZE;
				legacy_s16 x = (wheel % 2 ? 40 : -40) + (side ? 4 : -4);
				legacy_s16 y = point == 1 ? 16 : 8;
				legacy_s16 z = (wheel < 2 ? 60 : -60) + (point == 2 ? 8 : 0);
				LEGACY_WRITE_U16_LE(vertex, x);
				LEGACY_WRITE_U16_LE(vertex + 2, y);
				LEGACY_WRITE_U16_LE(vertex + 4, z);
			}
		}
	}
	owoot_read_wheel_shape(shape);
}

static void bounds(const struct VECTOR *points, legacy_s16 *minimum, legacy_s16 *maximum)
{
	*minimum = *maximum = points[0].x;
	for (legacy_u32 i = 1; i < 32; i++) {
		if (points[i].x < *minimum) {
			*minimum = points[i].x;
		}
		if (points[i].x > *maximum) {
			*maximum = points[i].x;
		}
	}
}

static void test_model_wheel_projection(void)
{
	reset_player();
	set_mode("--owoot");
	load_test_wheels();
	struct VECTOR points[OWOOT_WHEEL_VERTEX_COUNT];
	struct CARSTATE *car = &state.playerstate;
	car->car_position.lx = 1000L * 64;
	car->car_position.lz = 2000L * 64;
	assert(owoot_wheel_footprint(car, 0, points) == 32);
	legacy_s16 min_x, max_x;
	bounds(points, &min_x, &max_x);
	assert(min_x == 956 && max_x == 964);
	assert(points[0].y == 16);
	/* Opposite-facing far-rim controls must not twist the cylinder's tread. */
	wheel_controls[0][4].y = 0;
	wheel_controls[0][5].z = 52;
	owoot_wheel_footprint(car, 0, points);
	for (legacy_u32 i = 0; i < 16; i++) {
		assert(points[i + 16].x - points[i].x == 8);
		assert(points[i + 16].y == points[i].y);
		assert(points[i + 16].z == points[i].z);
	}
	car->car_suspension_deflection[0] = 128;
	owoot_wheel_footprint(car, 0, points);
	assert(points[0].y == 14);
	car->car_steeringAngle = ANGLE_HALF_TURN;
	owoot_wheel_footprint(car, 0, points);
	bounds(points, &min_x, &max_x);
	assert(min_x == 952 && max_x == 968);
	car->car_rotate.z = ANGLE_QUARTER_TURN;
	owoot_wheel_footprint(car, 2, points);
	bounds(points, &min_x, &max_x);
	assert(max_x - min_x == 16);
}

static void test_steering_control_rounding(void)
{
	reset_player();
	set_mode("--owoot");
	load_test_wheels();
	for (legacy_u16 point = 0; point < 3; point++) {
		wheel_controls[0][point].x = -45;
	}
	struct CARSTATE *car = &state.playerstate;
	car->car_position.lx = 1000L * 64;
	car->car_position.lz = 2000L * 64;
	car->car_steeringAngle = 128;
	struct VECTOR points[OWOOT_WHEEL_VERTEX_COUNT];
	owoot_wheel_footprint(car, 0, points);
	/* These cardinal points match the renderer's transformed controls.
	 * The odd negative axle centre rounds down; the two rim centres move
	 * in the same X/Z direction. A conventional rotation gives a different
	 * edge because the original steering transform adds both sine terms. */
	assert(points[0].x == 963 && points[0].y == 16 && points[0].z == 2062);
	assert(points[4].x == 960 && points[4].y == 8 && points[4].z == 2055);
	assert(points[16].x == 954 && points[16].y == 16 && points[16].z == 2058);
	assert(points[20].x == 951 && points[20].y == 8 && points[20].z == 2051);
	car->car_suspension_deflection[0] = -127;
	owoot_wheel_footprint(car, 0, points);
	assert(points[0].y == 17);
}

int main(void)
{
	test_model_wheel_projection();
	test_steering_control_rounding();
	test_owoot_options();
	test_switch_and_enforcement();
	test_failed_stunt_cannot_use_road_fallback();
	puts("OWOOT mode and wheel tests passed");
	return 0;
}
