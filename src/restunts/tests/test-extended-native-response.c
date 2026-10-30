/* Compare extended tracks with the original contact and gate calculations.
 * Source inclusion exposes the private response routines without game assets. */
#include <assert.h>
#include <string.h>

#include "../c/stateply.c"
#include "../c/owoot_route.c"
#include "../c/trackdata_layout.h"

#undef memcpy

enum {
	TEST_OBJECT_ID = 1,
	TEST_OBJECT_COUNT = 215,
	TEST_POINT_COUNT = 13,
	TEST_ROUTE_VECTOR_COUNT = 3,
	TEST_TUNNEL_PORTAL_COUNT = 2,
	TEST_GATE_SLOPE_COMPONENT =
		(CSTRK_LEVEL_WORLD_HEIGHT * OWOOT_GATE_SCALE + CSTRK_TILE_WORLD_SIZE / 2) /
		CSTRK_TILE_WORLD_SIZE,
	TEST_ROTATION_COUNT = 4,
	TEST_BASE_LEVEL_COUNT = CSTRK_MAX_HEIGHT + 1,
	TEST_FIRST_POINT_X = -300,
	TEST_POINT_X_STEP = 50,
	TEST_WHEEL_HEIGHT = 100,
	TEST_WHEEL_TRAVEL = 1280,
	TEST_WHEEL_DISPLACEMENT = 10,
	TEST_WALL_INDEX = 1,
	TEST_WALL_HEIGHT = 200,
	TEST_CAR_ROLL = ANGLE_QUARTER_TURN / 4,
	TEST_CAR_PITCH = ANGLE_QUARTER_TURN / 8
};

struct TRACKOBJECT trkObjectList[TEST_OBJECT_COUNT];

static legacy_int fixture_extended, fixture_lane_edges;
static struct CSTRK_TRACK fixture_track;
static struct CSTRK_OBJECT fixture_object;
static struct TRKOBJINFO fixture_info;
static struct VECTOR fixture_sections[TEST_POINT_COUNT];
static legacy_s8 fixture_elements[] = {TEST_OBJECT_ID};
static legacy_s8 fixture_columns[] = {0}, fixture_rows[] = {TRACK_GRID_LAST_INDEX};
static legacy_s8 fixture_flags[] = {0};

legacy_int extended_track_active(void)
{
	return fixture_extended;
}

legacy_int extended_track_height(void)
{
	return fixture_track.height;
}

const struct CSTRK_TRACK *extended_track_model(void)
{
	return &fixture_track;
}

const struct CSTRK_OBJECT *extended_track_object_for_id(legacy_int column, legacy_int row,
														legacy_uint id)
{
	assert(column == fixture_object.x && row == fixture_object.y && id == TEST_OBJECT_ID);
	return &fixture_object;
}

legacy_s16 get_track_route_point(legacy_s16 piece, struct VECTOR *output, legacy_s16 index,
								 legacy_s8 *speed)
{
	assert(piece == 0 && index >= 0 && index < TEST_POINT_COUNT);
	(void)speed;
	for (legacy_u16 point = 0; point < TEST_ROUTE_VECTOR_COUNT; point++) {
		output[point] = fixture_sections[index];
		if (fixture_lane_edges && point) {
			legacy_s16 lateral = point == 1U ? -ROAD_HALF_WIDTH : ROAD_HALF_WIDTH;
			legacy_s16 dx = 0, dz = 0;
			if (trkObjectList[TEST_OBJECT_ID].ss_rotY == ANGLE_QUARTER_TURN) {
				dz = lateral;
			} else {
				dx = lateral;
			}
			output[point].x += dx;
			output[point].z += dz;
			legacy_s32 rise = (fixture_object.slope_x * dx - fixture_object.slope_y * dz) *
							  CSTRK_LEVEL_WORLD_HEIGHT;
			rise += rise < 0 ? -CSTRK_TILE_WORLD_SIZE / 2 : CSTRK_TILE_WORLD_SIZE / 2;
			output[point].y += (legacy_s16)(rise / CSTRK_TILE_WORLD_SIZE);
		}
	}
	/* The last storage slot carries only the route flag, not a spatial vector. */
	output[TEST_ROUTE_VECTOR_COUNT].x = 0;
	return index == TEST_POINT_COUNT - 1;
}

void update_crash_state(legacy_s16 event, legacy_s16 car_index)
{
	(void)event;
	(void)car_index;
	assert(0 && "A stationary contact must not crash the fixture car");
}

static struct PLAYER_WHEEL_MOTION wall_response(legacy_int extended, legacy_s16 orientation)
{
	fixture_extended = extended;
	struct CARSTATE car = {0};
	struct PLAYER_WHEEL_MOTION motion = {0};
	motion.travel = TEST_WHEEL_TRAVEL;
	for (legacy_u16 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
		car.car_wheel_contact_positions[wheel].y = TEST_WHEEL_HEIGHT;
		motion.previous[wheel].ly = TEST_WHEEL_HEIGHT * OWOOT_POSITION_SCALE;
		motion.current[wheel] = motion.previous[wheel];
		motion.current[wheel].lx = TEST_WHEEL_DISPLACEMENT * OWOOT_POSITION_SCALE;
		motion.current[wheel].lz = TEST_WHEEL_DISPLACEMENT * OWOOT_POSITION_SCALE;
	}
	car_working_roll = TEST_CAR_ROLL;
	car_working_pitch = TEST_CAR_PITCH;
	car_working_yaw = 0;
	wallindex = TEST_WALL_INDEX;
	wallStartX = wallStartZ = 0;
	wallOrientation = orientation;
	wallHeight = TEST_WALL_HEIGHT;
	elRdWallRelated = 0;
	nextPosAndNormalIP = TEST_WHEEL_HEIGHT;
	assert(resolve_wheel_wall_collision(&car, &motion, 0, PLAYER_CAR_INDEX));
	assert(car.car_sound_flags == PLAYER_PHYSICS_WALL_SOUND_FLAG);
	return motion;
}

static void test_native_wall_response(void)
{
	legacy_int has_vertical_response = 0;
	for (legacy_u16 rotation = 0; rotation < TEST_ROTATION_COUNT; rotation++) {
		legacy_s16 orientation = rotation * ANGLE_QUARTER_TURN;
		struct PLAYER_WHEEL_MOTION original = wall_response(0, orientation);
		struct PLAYER_WHEEL_MOTION extended = wall_response(1, orientation);
		for (legacy_u16 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
			assert(original.current[wheel].lx == extended.current[wheel].lx);
			assert(original.current[wheel].ly == extended.current[wheel].ly);
			assert(original.current[wheel].lz == extended.current[wheel].lz);
			has_vertical_response |= original.current[wheel].ly != original.previous[wheel].ly;
		}
	}
	/* A yaw-only response would flatten this contact while the car is tilted. */
	assert(has_vertical_response);
}

static void assert_same_point(const struct OWOOT_POINT *original,
							  const struct OWOOT_POINT *extended)
{
	assert(original->x == extended->x);
	assert(original->y == extended->y);
	assert(original->z == extended->z);
}

static void test_native_corkscrew_gates(void)
{
	static const legacy_s16 sides[TEST_POINT_COUNT] = {0,  -58, -100, -115, -100, -58, 0,
													   58, 100, 115,  100,	58,	  0};
	static const legacy_s16 heights[TEST_POINT_COUNT] = {0,	  17,  61,	119, 177, 219, 235,
														 219, 177, 119, 61,	 17,  0};
	fixture_track.version = CSTRK_VERSION;
	fixture_track.height = TRACK_GRID_SIZE;
	fixture_object.legacy_id = TEST_OBJECT_ID;
	fixture_object.y = TRACK_GRID_LAST_INDEX;
	fixture_object.width = fixture_object.height = 1;
	fixture_info.route_point_count = TEST_POINT_COUNT;
	trkObjectList[TEST_OBJECT_ID].ss_physicalModel = PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT;
	trkObjectList[TEST_OBJECT_ID].ss_trkObjInfoPtr = &fixture_info;
	track_route_element_ids = fixture_elements;
	track_route_columns = fixture_columns;
	track_route_rows = fixture_rows;
	track_route_traversal_flags = fixture_flags;
	for (legacy_u16 level = 0; level < TEST_BASE_LEVEL_COUNT; level++) {
		fixture_object.base_height = level;
		struct VECTOR origin;
		extended_geometry_object_origin(&fixture_track, &fixture_object, &origin);
		for (legacy_u16 rotation = 0; rotation < TEST_ROTATION_COUNT; rotation++) {
			struct MATRIX yaw;
			mat_rot_y(&yaw, rotation * ANGLE_QUARTER_TURN);
			for (legacy_u16 point = 0; point < TEST_POINT_COUNT; point++) {
				struct VECTOR local = {TEST_FIRST_POINT_X + point * TEST_POINT_X_STEP,
									   heights[point], sides[point]};
				mat_mul_vector(&local, &yaw, &fixture_sections[point]);
				fixture_sections[point].x += origin.x;
				fixture_sections[point].y += origin.y;
				fixture_sections[point].z += origin.z;
			}
			extended_geometry_set_context(origin.x * OWOOT_POSITION_SCALE,
										  origin.z * OWOOT_POSITION_SCALE);
			for (legacy_s16 point = 1; point < TEST_POINT_COUNT - 1; point++) {
				fixture_extended = 0;
				struct OWOOT_GATE original = {0};
				original.center = route_vector(&fixture_sections[point]);
				struct OWOOT_POINT previous = route_vector(&fixture_sections[point - 1]);
				struct OWOOT_POINT next = route_vector(&fixture_sections[point + 1]);
				original.tangent = route_delta(&next, &previous);
				struct OWOOT_GATE extended = original;
				fixture_extended = 0;
				assert(route_corkscrew_gate(0, point, &original));
				fixture_extended = 1;
				assert(route_corkscrew_gate(0, point, &extended));
				assert_same_point(&original.center, &extended.center);
				assert_same_point(&original.tangent, &extended.tangent);
				assert_same_point(&original.edge, &extended.edge);
				assert_same_point(&original.normal, &extended.normal);
				assert(original.lateral_limit == extended.lateral_limit);
				assert(original.normal_limit == extended.normal_limit);
				assert(original.kind == extended.kind);
			}
		}
	}
}

static void test_sloped_tunnel_portals(legacy_u8 tunnel_slalom)
{
	static const legacy_s8 slopes[TEST_ROTATION_COUNT][2] = {{0, -1}, {-1, 0}, {0, 1}, {1, 0}};
	fixture_extended = fixture_lane_edges = 1;
	fixture_track.width = fixture_track.height = TRACK_GRID_SIZE;
	fixture_track.version = CSTRK_VERSION;
	fixture_object.legacy_id = TEST_OBJECT_ID;
	fixture_object.tunnel_slalom = tunnel_slalom;
	fixture_object.x = 0;
	fixture_object.y = TRACK_GRID_LAST_INDEX;
	fixture_object.width = fixture_object.height = 1;
	fixture_info.route_point_count = TEST_TUNNEL_PORTAL_COUNT;
	track_route_element_ids = fixture_elements;
	track_route_columns = fixture_columns;
	track_route_rows = fixture_rows;
	track_route_traversal_flags = fixture_flags;
	trkObjectList[TEST_OBJECT_ID].ss_physicalModel =
		tunnel_slalom ? PHYSICAL_MODEL_SLALOM : PHYSICAL_MODEL_TUNNEL;
	trkObjectList[TEST_OBJECT_ID].ss_trkObjInfoPtr = &fixture_info;
	assert(route_has_finite_portal(0));
	assert(route_gate_count(0) == TEST_TUNNEL_PORTAL_COUNT);
	for (legacy_u8 direction = 0; direction < TEST_ROTATION_COUNT; direction++) {
		legacy_u8 along_x = direction & 1U;
		trkObjectList[TEST_OBJECT_ID].ss_rotY = along_x ? ANGLE_QUARTER_TURN : 0;
		for (legacy_u8 crosswise = 0; crosswise < TEST_TUNNEL_PORTAL_COUNT; crosswise++) {
			legacy_u8 slope_direction = (direction + crosswise) % TEST_ROTATION_COUNT;
			fixture_object.slope_x = slopes[slope_direction][0];
			fixture_object.slope_y = slopes[slope_direction][1];
			for (legacy_u8 base = 0; base < CSTRK_MAX_HEIGHT; base++) {
				fixture_object.base_height =
					base + (fixture_object.slope_x < 0 || fixture_object.slope_y < 0);
				struct VECTOR origin;
				extended_geometry_object_origin(&fixture_track, &fixture_object, &origin);
				extended_geometry_set_context(origin.x * OWOOT_POSITION_SCALE,
											  origin.z * OWOOT_POSITION_SCALE);
				for (legacy_s16 portal = 0; portal < TEST_TUNNEL_PORTAL_COUNT; portal++) {
					legacy_s16 coordinate =
						portal ? CSTRK_TILE_WORLD_SIZE / 2 : -CSTRK_TILE_WORLD_SIZE / 2;
					legacy_s16 local_x = along_x ? coordinate : 0;
					legacy_s16 local_z = along_x ? 0 : coordinate;
					legacy_s32 floor =
						fixture_object.base_height * CSTRK_LEVEL_WORLD_HEIGHT +
						(fixture_object.slope_x * (CSTRK_TILE_WORLD_SIZE / 2 + local_x) +
						 fixture_object.slope_y * (CSTRK_TILE_WORLD_SIZE / 2 - local_z)) *
							CSTRK_LEVEL_WORLD_HEIGHT / CSTRK_TILE_WORLD_SIZE;
					fixture_sections[portal] =
						(struct VECTOR){origin.x + local_x, (legacy_s16)floor, origin.z + local_z};
				}
				for (legacy_s16 portal = 0; portal < TEST_TUNNEL_PORTAL_COUNT; portal++) {
					struct OWOOT_GATE gate = {0};
					route_gate(0, portal, &gate);
					assert(gate.center.x == fixture_sections[portal].x);
					assert(gate.center.z == fixture_sections[portal].z);
					assert(gate.center.y == fixture_sections[portal].y + TUNNEL_HEIGHT / 2);
					assert(gate.kind == OWOOT_GATE_TUNNEL);
					assert(gate.tangent.x == (along_x ? OWOOT_GATE_SCALE : 0));
					assert(gate.tangent.y == 0);
					assert(gate.tangent.z == (along_x ? 0 : OWOOT_GATE_SCALE));
					assert(gate.edge.x == (along_x ? 0 : OWOOT_GATE_SCALE));
					assert(gate.edge.y == 0);
					assert(gate.edge.z == (along_x ? -OWOOT_GATE_SCALE : 0));
					assert(gate.normal.x == -fixture_object.slope_x * TEST_GATE_SLOPE_COMPONENT);
					assert(gate.normal.y == OWOOT_GATE_SCALE);
					assert(gate.normal.z == fixture_object.slope_y * TEST_GATE_SLOPE_COMPONENT);
					assert(gate.lateral_limit == ROAD_HALF_WIDTH * OWOOT_GATE_SCALE);
					assert(gate.normal_limit == TUNNEL_HEIGHT / 2 * OWOOT_GATE_SCALE);
				}
			}
		}
	}
	fixture_lane_edges = 0;
}

legacy_int main(void)
{
	test_native_wall_response();
	test_native_corkscrew_gates();
	test_sloped_tunnel_portals(0);
	test_sloped_tunnel_portals(1);
	return 0;
}
