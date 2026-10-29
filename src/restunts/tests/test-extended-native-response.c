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

static legacy_int fixture_extended;
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
	for (legacy_u16 point = 0; point < ROUTE_GEOMETRY_POINT_COUNT; point++) {
		output[point] = fixture_sections[index];
	}
	return index == TEST_POINT_COUNT - 1;
}

void update_crash_state(legacy_s16 event, legacy_s16 car_index)
{
	(void)event;
	(void)car_index;
	assert(0 && "A stationary contact must not crash the fixture car");
}

static struct PLAYER_WHEEL_MOTION wall_response(legacy_int extended, legacy_s16 orientation,
												legacy_u8 level)
{
	fixture_extended = extended;
	struct CARSTATE car = {0};
	struct PLAYER_WHEEL_MOTION motion = {0};
	motion.travel = TEST_WHEEL_TRAVEL;
	for (legacy_u16 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
		legacy_s16 world_height = level * CSTRK_LEVEL_WORLD_HEIGHT + TEST_WHEEL_HEIGHT;
		car.car_wheel_contact_positions[wheel].y = world_height;
		motion.previous[wheel].ly = world_height * OWOOT_POSITION_SCALE;
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
		struct PLAYER_WHEEL_MOTION ground = wall_response(0, orientation, 0);
		for (legacy_u8 level = 0; level < TEST_BASE_LEVEL_COUNT; level++) {
			struct PLAYER_WHEEL_MOTION original = wall_response(0, orientation, level);
			struct PLAYER_WHEEL_MOTION extended = wall_response(1, orientation, level);
			legacy_s32 height_offset = level * CSTRK_LEVEL_WORLD_HEIGHT * OWOOT_POSITION_SCALE;
			for (legacy_u16 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
				assert(original.current[wheel].lx == extended.current[wheel].lx);
				assert(original.current[wheel].ly == extended.current[wheel].ly);
				assert(original.current[wheel].lz == extended.current[wheel].lz);
				assert(original.current[wheel].lx == ground.current[wheel].lx);
				assert(original.current[wheel].ly - height_offset == ground.current[wheel].ly);
				assert(original.current[wheel].lz == ground.current[wheel].lz);
				has_vertical_response |= original.current[wheel].ly != original.previous[wheel].ly;
			}
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

legacy_int main(void)
{
	test_native_wall_response();
	test_native_corkscrew_gates();
	return 0;
}
