/* Compare extended tracks with the original contact and gate calculations.
 * Source inclusion exposes the private response routines without game assets. */
#include <assert.h>
#include <string.h>

#include "../c/stateply.c"
#include "../c/owoot_route.c"
#include "../c/owoot_road.c"
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
#define TEST_ROLL_RECEIVER_COUNT 3U
#define TEST_ROLL_ROUTE_COUNT (TEST_ROLL_RECEIVER_COUNT + 1U)
static struct CSTRK_OBJECT fixture_receivers[TEST_ROLL_RECEIVER_COUNT];
static struct TRKOBJINFO fixture_info;
static struct VECTOR fixture_sections[TEST_POINT_COUNT];
static legacy_s8 fixture_elements[TEST_ROLL_ROUTE_COUNT] = {TEST_OBJECT_ID};
static legacy_s8 fixture_columns[TEST_ROLL_ROUTE_COUNT] = {0};
static legacy_s8 fixture_rows[TEST_ROLL_ROUTE_COUNT] = {TRACK_GRID_LAST_INDEX};
static legacy_s8 fixture_flags[TEST_ROLL_ROUTE_COUNT] = {0};

legacy_int extended_track_active(void)
{
	return fixture_extended;
}

legacy_int extended_track_width(void)
{
	return fixture_track.width;
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
	if (id == TEST_OBJECT_ID) {
		assert(column == fixture_object.x && row == fixture_object.y);
		return &fixture_object;
	}
	assert(id > TEST_OBJECT_ID && id <= TEST_ROLL_ROUTE_COUNT);
	const struct CSTRK_OBJECT *object = &fixture_receivers[id - TEST_OBJECT_ID - 1U];
	assert(column == object->x && row == object->y);
	return object;
}

const struct CSTRK_OBJECT *extended_track_object_at(legacy_int column, legacy_int row)
{
	if (column == fixture_object.x && row >= fixture_object.y &&
		row < fixture_object.y + fixture_object.height) {
		return &fixture_object;
	}
	for (legacy_u16 index = 0; index < TEST_ROLL_RECEIVER_COUNT; index++) {
		if (column == fixture_receivers[index].x && row == fixture_receivers[index].y) {
			return &fixture_receivers[index];
		}
	}
	return NULL;
}

const struct TRKOBJINFO *extended_track_route_info(legacy_s16 column, legacy_s16 row, legacy_u16 id)
{
	(void)column;
	(void)row;
	assert(id < TEST_OBJECT_COUNT);
	return trkObjectList[id].ss_trkObjInfoPtr;
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

legacy_s16 extended_track_elevation(legacy_int column, legacy_int row, legacy_int local_x,
									legacy_int local_y)
{
	(void)column;
	(void)row;
	(void)local_x;
	(void)local_y;
	assert(0 && "Custom jump checks must not borrow ordinary ramp elevation");
	return 0;
}

legacy_u16 owoot_wheel_footprint(const struct CARSTATE *car, legacy_u16 wheel,
								 struct VECTOR output[OWOOT_WHEEL_VERTEX_COUNT])
{
	(void)car;
	(void)wheel;
	(void)output;
	assert(0 && "Custom jump checks must not borrow an ordinary jump footprint");
	return 0;
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

enum {
	TEST_ROLL_COLUMN = 5,
	TEST_ROLL_RAMP_ROW = 5,
	TEST_ROLL_FIRST_RECEIVER_ROW = 10,
	TEST_ROLL_CONTACT_BEFORE_LIP = 40,
	TEST_ROLL_AIRBORNE_DISTANCE = 2 * CSTRK_TILE_WORLD_SIZE,
	TEST_ROLL_SIDE_ESCAPE = TRACK_TILE_HALF_SIZE + 1,
	TEST_ROLL_UPRIGHT_TOLERANCE = ANGLE_FULL_TURN / 64,
	TEST_ROLL_FIRST_TIRE_TILT = ANGLE_FULL_TURN * 50L / 360L,
	TEST_ROLL_SKIPPED_OFFSET = ANGLE_QUARTER_TURN / 8
};

static legacy_s32 roll_fixture_lip_z(void)
{
	return (TRACK_GRID_SIZE - TEST_ROLL_RAMP_ROW - 2) * CSTRK_TILE_WORLD_SIZE;
}

static struct CARSTATE roll_fixture_car(void)
{
	fixture_extended = 1;
	fixture_track.width = fixture_track.height = TRACK_GRID_SIZE;
	memset(&fixture_object, 0, sizeof(fixture_object));
	fixture_object.legacy_id = CSTRK_ROLL_RAMP_FIRST_ID;
	fixture_object.x = TEST_ROLL_COLUMN;
	fixture_object.y = TEST_ROLL_RAMP_ROW;
	fixture_object.width = 1;
	fixture_object.height = 2;
	fixture_object.occupied_mask = 3;
	fixture_object.roll_ramp = 1;
	trkObjectList[TEST_OBJECT_ID].ss_physicalModel = PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT;
	trkObjectList[TEST_OBJECT_ID].ss_multiTileFlag = MULTI_TILE_ROW_EDGE_FLAG;
	static legacy_s16 primary[TEST_ROLL_ROUTE_COUNT], alternate[TEST_ROLL_ROUTE_COUNT];
	track_primary_route_links = primary;
	track_alternate_route_links = alternate;
	track_route_element_ids = fixture_elements;
	track_route_columns = fixture_columns;
	track_route_rows = fixture_rows;
	track_route_traversal_flags = fixture_flags;
	track_pieces_counter = TEST_ROLL_ROUTE_COUNT;
	fixture_elements[0] = TEST_OBJECT_ID;
	fixture_columns[0] = TEST_ROLL_COLUMN;
	fixture_rows[0] = TEST_ROLL_RAMP_ROW;
	for (legacy_u16 index = 0; index < TEST_ROLL_ROUTE_COUNT; index++) {
		primary[index] = index + 1U < TEST_ROLL_ROUTE_COUNT ? index + 1 : TRACK_ROUTE_LINK_NONE;
		alternate[index] = TRACK_ROUTE_LINK_NONE;
		if (index) {
			fixture_receivers[index - 1U] = (struct CSTRK_OBJECT){0};
			fixture_receivers[index - 1U].x = TEST_ROLL_COLUMN;
			fixture_receivers[index - 1U].y = TEST_ROLL_FIRST_RECEIVER_ROW + index - 1U;
			fixture_receivers[index - 1U].width = fixture_receivers[index - 1U].height = 1;
			fixture_elements[index] = TEST_OBJECT_ID + index;
			fixture_columns[index] = TEST_ROLL_COLUMN;
			fixture_rows[index] = TEST_ROLL_FIRST_RECEIVER_ROW + index - 1U;
			trkObjectList[TEST_OBJECT_ID + index].ss_physicalModel = PHYSICAL_MODEL_ROAD;
			trkObjectList[TEST_OBJECT_ID + index].ss_multiTileFlag = 0;
		}
	}
	struct VECTOR origin, contact, normal;
	extended_geometry_object_origin(&fixture_track, &fixture_object, &origin);
	roll_ramp_sample(ROLL_RAMP_LENGTH - TEST_ROLL_CONTACT_BEFORE_LIP, 0, &contact, &normal);
	contact.x += origin.x;
	contact.y += origin.y;
	contact.z += origin.z;
	struct CARSTATE car = {0};
	car.car_position.lx = origin.x * OWOOT_POSITION_SCALE;
	car.car_position.ly =
		(roll_ramp_center_height(ROLL_RAMP_LENGTH) + TEST_WHEEL_HEIGHT) * OWOOT_POSITION_SCALE;
	car.car_position.lz =
		(roll_fixture_lip_z() - TEST_ROLL_CONTACT_BEFORE_LIP) * OWOOT_POSITION_SCALE;
	car.car_previous_position = car.car_position;
	car.car_previous_position.lz += TEST_ROLL_CONTACT_BEFORE_LIP * OWOOT_POSITION_SCALE;
	car.car_rotate.x = ANGLE_HALF_TURN;
	car.car_rotate.z = roll_ramp_bank_angle(ROLL_RAMP_LENGTH - TEST_ROLL_CONTACT_BEFORE_LIP);
	car.car_wheel_contact_positions[0] = contact;
	return car;
}

static void roll_fixture_arm(struct CARSTATE *car)
{
	car->car_sumSurfAllWheels = CAR_SURFACE_PAVED;
	car->car_surfaceWhl[0] = CAR_SURFACE_PAVED;
	assert(owoot_jump_is_valid(car) == 1);
	assert(car->car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_SOURCE] == 1);
}

static legacy_s16 roll_fixture_air(struct CARSTATE *car, legacy_s16 angle)
{
	car->car_sumSurfAllWheels = CAR_WHEEL_CONTACT_NONE;
	car->car_sumSurfFrontWheels = car->car_sumSurfRearWheels = CAR_WHEEL_CONTACT_NONE;
	memset(car->car_surfaceWhl, CAR_WHEEL_CONTACT_NONE, sizeof(car->car_surfaceWhl));
	car->car_previous_position = car->car_position;
	car->car_position.lz =
		(roll_fixture_lip_z() - TEST_ROLL_AIRBORNE_DISTANCE) * OWOOT_POSITION_SCALE;
	car->car_rotate.z = angle;
	return owoot_jump_is_valid(car);
}

static legacy_s16 roll_fixture_land(struct CARSTATE *car, legacy_s16 angle)
{
	car->car_previous_position = car->car_position;
	struct VECTOR origin;
	extended_geometry_object_origin(&fixture_track, &fixture_receivers[0], &origin);
	car->car_position.ly = (origin.y + extended_geometry_object_grade(&fixture_receivers[0], 0, 0) +
							TEST_WHEEL_HEIGHT) *
						   OWOOT_POSITION_SCALE;
	car->car_position.lz =
		((TRACK_GRID_SIZE - TEST_ROLL_FIRST_RECEIVER_ROW) * CSTRK_TILE_WORLD_SIZE -
		 TRACK_TILE_HALF_SIZE) *
		OWOOT_POSITION_SCALE;
	car->car_rotate.z = angle;
	car->car_sumSurfAllWheels = CAR_SURFACE_PAVED;
	car->car_surfaceWhl[0] = CAR_SURFACE_PAVED;
	return owoot_jump_is_valid(car);
}

static void test_roll_launch_corridor(void)
{
	struct CARSTATE car = roll_fixture_car();
	assert(owoot_jump_is_valid(&car) == 0); /* Flying over a ramp cannot arm it. */
	car.car_sumSurfAllWheels = CAR_SURFACE_ICE;
	car.car_surfaceWhl[0] = CAR_SURFACE_ICE;
	legacy_s16 contact_height = car.car_wheel_contact_positions[0].y;
	car.car_wheel_contact_positions[0].y = 0;
	assert(owoot_jump_is_valid(&car) == 0); /* Ice below the grille is not ramp contact. */
	car.car_wheel_contact_positions[0].y = contact_height;
	roll_fixture_arm(&car);
	assert(roll_fixture_air(&car, ANGLE_QUARTER_TURN) == 1);
	/* The first receiving tile need not be the exact touchdown tile. */
	car.car_position.lz = (TRACK_GRID_SIZE - TEST_ROLL_FIRST_RECEIVER_ROW - 1) *
						  CSTRK_TILE_WORLD_SIZE * OWOOT_POSITION_SCALE;
	car.car_previous_position.lz = car.car_position.lz + OWOOT_POSITION_SCALE;
	assert(owoot_jump_is_valid(&car) == 1);
	struct CARSTATE escaped = car;
	escaped.car_position.lx += TEST_ROLL_SIDE_ESCAPE * OWOOT_POSITION_SCALE;
	assert(owoot_jump_is_valid(&escaped) == OWOOT_JUMP_FAILED);
	assert(escaped.car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_SOURCE] == 0);
	escaped = car;
	escaped.car_previous_position.lz = escaped.car_position.lz - OWOOT_POSITION_SCALE;
	assert(owoot_jump_is_valid(&escaped) == OWOOT_JUMP_FAILED);
}

static void test_roll_completion_and_checkpoint_state(void)
{
	struct CARSTATE car = roll_fixture_car();
	roll_fixture_arm(&car);
	assert(roll_fixture_air(&car, ANGLE_QUARTER_TURN) == 1);
	assert(roll_fixture_air(&car, ANGLE_HALF_TURN) == 1);
	struct CARSTATE checkpoint = car;
	assert(roll_fixture_air(&car, ANGLE_THREE_QUARTER_TURN) == 1);
	assert(roll_fixture_air(&car, 0) == 1);
	struct CARSTATE tilted = car;
	assert(roll_fixture_land(&tilted, TEST_CAR_ROLL) == 0);
	/* The first tire may touch while the completed roll still leans, then
	 * suspension settles the wheels-down car onto its receiving road. */
	tilted = car;
	assert(roll_fixture_land(&tilted, TEST_ROLL_FIRST_TIRE_TILT) == 0);
	tilted = car;
	assert(roll_fixture_land(&tilted, ANGLE_QUARTER_TURN) == OWOOT_JUMP_FAILED);
	tilted = car;
	assert(roll_fixture_land(&tilted, ANGLE_HALF_TURN) == OWOOT_JUMP_FAILED);
	tilted = car;
	tilted.car_rotate.y = ANGLE_HALF_TURN;
	assert(roll_fixture_land(&tilted, 0) == OWOOT_JUMP_FAILED);
	assert(roll_fixture_land(&car, 0) == 0);
	assert(car.car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_SOURCE] == 0);
	/* Validation progress belongs to the copied car, rather than a global
	 * phase that could be lost or borrowed during replay seeking. */
	struct CARSTATE other = roll_fixture_car();
	roll_fixture_arm(&other);
	assert(roll_fixture_land(&other, 0) == OWOOT_JUMP_FAILED);
	assert(roll_fixture_air(&checkpoint, ANGLE_THREE_QUARTER_TURN) == 1);
	assert(roll_fixture_air(&checkpoint, 0) == 1);
	assert(roll_fixture_land(&checkpoint, 0) == 0);
	assert(memcmp(&car, &checkpoint, sizeof(car)) == 0);
	/* A completed attempt cannot lend its progress to the next approach. */
	struct CARSTATE retry = roll_fixture_car();
	memcpy(retry.car_reserved_wheel_state, car.car_reserved_wheel_state,
		   sizeof(retry.car_reserved_wheel_state));
	roll_fixture_arm(&retry);
	assert(roll_fixture_air(&retry, retry.car_rotate.z) == 1);
	assert(roll_fixture_land(&retry, 0) == OWOOT_JUMP_FAILED);
	/* Lower-rate updates may cross each milestone between samples. The
	 * final airborne pose is within the same upright tolerance as physics. */
	car = roll_fixture_car();
	roll_fixture_arm(&car);
	const legacy_s16 skipped[] = {ANGLE_QUARTER_TURN + TEST_ROLL_SKIPPED_OFFSET,
								  ANGLE_HALF_TURN + TEST_ROLL_SKIPPED_OFFSET,
								  ANGLE_THREE_QUARTER_TURN + TEST_ROLL_SKIPPED_OFFSET,
								  ANGLE_FULL_TURN - TEST_ROLL_UPRIGHT_TOLERANCE / 2};
	for (legacy_u16 sample = 0; sample < sizeof(skipped) / sizeof(skipped[0]); sample++) {
		assert(roll_fixture_air(&car, skipped[sample]) == 1);
	}
	assert(roll_fixture_land(&car, 0) == 0);
}

static void test_roll_completion_on_cross_slope(void)
{
	struct CARSTATE car = roll_fixture_car();
	fixture_object.slope_x = 1;
	for (legacy_u16 receiver = 0; receiver < TEST_ROLL_RECEIVER_COUNT; receiver++) {
		fixture_receivers[receiver].slope_x = 1;
	}
	/* Facing south on an eastward rise rolls the ground-normal heading
	 * through atan(450/1024), independently of the validator's conversion. */
	legacy_s16 baseline = polarAngle(CSTRK_LEVEL_WORLD_HEIGHT, CSTRK_TILE_WORLD_SIZE);
	car.car_rotate.z += baseline;
	car.car_position.ly += CSTRK_LEVEL_WORLD_HEIGHT / 2 * OWOOT_POSITION_SCALE;
	car.car_wheel_contact_positions[0].y += CSTRK_LEVEL_WORLD_HEIGHT / 2;
	roll_fixture_arm(&car);
	const legacy_s16 poses[] = {ANGLE_QUARTER_TURN + TEST_ROLL_SKIPPED_OFFSET,
								ANGLE_HALF_TURN + TEST_ROLL_SKIPPED_OFFSET,
								ANGLE_THREE_QUARTER_TURN + TEST_ROLL_SKIPPED_OFFSET,
								ANGLE_FULL_TURN - TEST_ROLL_UPRIGHT_TOLERANCE / 2};
	for (legacy_u16 sample = 0; sample < sizeof(poses) / sizeof(poses[0]); sample++) {
		legacy_s16 angle = (poses[sample] + baseline) & ANGLE_MASK;
		assert(roll_fixture_air(&car, angle) == 1);
	}
	assert(roll_fixture_land(&car, baseline) == 0);
}

static void test_roll_shortcuts_are_rejected(void)
{
	struct CARSTATE car = roll_fixture_car();
	roll_fixture_arm(&car);
	assert(roll_fixture_air(&car, car.car_rotate.z) == 1);
	assert(roll_fixture_land(&car, 0) == OWOOT_JUMP_FAILED);
	car = roll_fixture_car();
	roll_fixture_arm(&car);
	assert(roll_fixture_air(&car, ANGLE_QUARTER_TURN) == 1);
	assert(roll_fixture_air(&car, ANGLE_HALF_TURN) == 1);
	assert(roll_fixture_land(&car, 0) == OWOOT_JUMP_FAILED);
	/* Returning upright by unwinding cannot stand in for a full rotation,
	 * even after both side-on poses and the inverted pose were observed. */
	car = roll_fixture_car();
	roll_fixture_arm(&car);
	assert(roll_fixture_air(&car, ANGLE_QUARTER_TURN) == 1);
	assert(roll_fixture_air(&car, ANGLE_HALF_TURN) == 1);
	assert(roll_fixture_air(&car, ANGLE_THREE_QUARTER_TURN) == 1);
	assert(roll_fixture_air(&car, ANGLE_HALF_TURN) == OWOOT_JUMP_FAILED);
	/* A single ambiguous half-turn sample cannot establish its direction. */
	car = roll_fixture_car();
	roll_fixture_arm(&car);
	assert(roll_fixture_air(&car, ANGLE_QUARTER_TURN) == 1);
	assert(roll_fixture_air(&car, ANGLE_THREE_QUARTER_TURN) == OWOOT_JUMP_FAILED);
	/* The final upright transition must happen with every tire unsupported. */
	car = roll_fixture_car();
	roll_fixture_arm(&car);
	assert(roll_fixture_air(&car, ANGLE_QUARTER_TURN) == 1);
	assert(roll_fixture_air(&car, ANGLE_HALF_TURN) == 1);
	assert(roll_fixture_air(&car, ANGLE_THREE_QUARTER_TURN) == 1);
	assert(roll_fixture_land(&car, 0) == OWOOT_JUMP_FAILED);
	/* Supported poses never advance completion. */
	car = roll_fixture_car();
	roll_fixture_arm(&car);
	for (legacy_s16 angle = ANGLE_QUARTER_TURN; angle <= ANGLE_FULL_TURN;
		 angle += ANGLE_QUARTER_TURN) {
		car.car_rotate.z = angle & ANGLE_MASK;
		assert(owoot_jump_is_valid(&car) == 1);
	}
	assert(roll_fixture_air(&car, 0) == 1);
	assert(roll_fixture_land(&car, 0) == OWOOT_JUMP_FAILED);
}

legacy_int main(void)
{
	test_native_wall_response();
	test_native_corkscrew_gates();
	test_sloped_tunnel_portals(0);
	test_sloped_tunnel_portals(1);
	test_roll_launch_corridor();
	test_roll_completion_and_checkpoint_state();
	test_roll_completion_on_cross_slope();
	test_roll_shortcuts_are_rejected();
	return 0;
}
