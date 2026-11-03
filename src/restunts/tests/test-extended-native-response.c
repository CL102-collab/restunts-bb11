/* Compare extended tracks with the original contact and gate calculations.
 * Source inclusion exposes the private response routines without game assets. */
#include <assert.h>
#include <string.h>

#include "../c/stateply/stateply.c"
#include "../c/stateply/stateply_motion.c"
#include "../c/stateply/stateply_contact.c"
#undef PLAYER_PHYSICS_WHEEL_COUNT
#include "../c/extended/extended_stateply.c"
#include "../c/owoot/owoot_route.c"
#include "../c/extended/extended_owoot_route.c"
#include "../c/extended/extended_owoot_gates.c"
#include "../c/extended/extended_owoot_jump.c"
#include "../c/owoot/owoot_road.c"
#include "../c/track/trackdata_layout.h"
#include "../c/extended/pine_trees.h"

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
struct CAR_SURFACE_ADHESION_SETTINGS car_surface_adhesion_settings[2];

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

/* This fixture supplies ground planes directly, without a selected mesh. */
legacy_s16 track_selected_plane_contact_fixed(const struct VECTORLONG *position, legacy_s32 *height,
											  legacy_s16 *distance)
{
	(void)position;
	(void)height;
	(void)distance;
	return 0;
}

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

legacy_uint extended_track_object_id(const struct CSTRK_OBJECT *object)
{
	return object->legacy_id;
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

enum {
	TEST_ADHESION_FRACTIONAL_COORDINATE = 13,
	TEST_ADHESION_WHOLE_GAP = 3,
	TEST_ADHESION_CONTACT_STEPS = 32
};

static void test_fan_contacts_follow_exact_slopes(void)
{
	static const legacy_s8 slopes[TEST_ROTATION_COUNT][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
	static const legacy_s32 gaps[] = {0, OWOOT_POSITION_SCALE / 2,
									  TEST_ADHESION_WHOLE_GAP * OWOOT_POSITION_SCALE +
										  OWOOT_POSITION_SCALE / 2};
	fixture_extended = 1;
	state.game_inputmode = GAME_INPUT_MODE_ACTIVE;
	elem_xCenter = elem_zCenter = 0;
	terrainHeight = TEST_WHEEL_HEIGHT;
	planindex = EXTENDED_GEOMETRY_PLANE_INDEX;
	current_surf_type = CAR_SURFACE_PAVED;
	struct PLANE plane = {0};
	current_planptr = &plane;
	for (legacy_u8 direction = 0; direction < TEST_ROTATION_COUNT; direction++) {
		plane = (struct PLANE){0};
		plane.plane_normal.y = TRIG_FIXED_ONE / 2;
		mat_rot_y(&plane.plane_rotation, 0);
		struct CSTRK_OBJECT object = {0};
		object.width = object.height = 1;
		object.slope_x = slopes[direction][0];
		object.slope_y = slopes[direction][1];
		extended_geometry_grade_plane(&object, &plane);
		car_to_world_rotation = plane.plane_rotation;
		for (legacy_s16 car_index = PLAYER_CAR_INDEX; car_index <= OPPONENT_CAR_INDEX;
			 car_index++) {
			car_surface_adhesion_settings[car_index] = (struct CAR_SURFACE_ADHESION_SETTINGS){
				CAR_ADHESION_DEFAULT_DISTANCE, CAR_ADHESION_DEFAULT_ALIGNMENT,
				CAR_ADHESION_SURFACE_MASK};
			struct CARSTATE car = {0};
			struct PLAYER_WHEEL_MOTION motion = {0};
			motion.surface_adhesion = 1;
			motion.car_index = car_index;
			for (legacy_u8 gap = 0; gap < sizeof(gaps) / sizeof(gaps[0]); gap++) {
				struct VECTORLONG position = {TEST_WHEEL_DISPLACEMENT * OWOOT_POSITION_SCALE +
												  TEST_ADHESION_FRACTIONAL_COORDINATE,
											  TEST_WHEEL_HEIGHT * OWOOT_POSITION_SCALE,
											  TEST_WHEEL_DISPLACEMENT * OWOOT_POSITION_SCALE -
												  TEST_ADHESION_FRACTIONAL_COORDINATE};
				for (legacy_u8 step = 0; step < TEST_ADHESION_CONTACT_STEPS; step++) {
					position.lx += TEST_ADHESION_FRACTIONAL_COORDINATE;
					position.lz -= TEST_ADHESION_FRACTIONAL_COORDINATE;
					struct PLAYER_GROUND_CONTACT support;
					assert(extended_ground_contact(&position, &support, GAME_INPUT_MODE_ACTIVE));
					position.ly = support.height;
					motion.current[0] = position;
					motion.current[0].ly += gaps[gap];
					measure_wheel_plane_distance(&motion.current[0]);
					assert(nextPosAndNormalIP >= 0);
					if (gaps[gap] < OWOOT_POSITION_SCALE) {
						assert(nextPosAndNormalIP == 0);
					}
					assert(hold_wheel_to_surface(&car, &motion, 0));
					assert(motion.current[0].lx == position.lx);
					assert(motion.current[0].ly == position.ly);
					assert(motion.current[0].lz == position.lz);
					assert(nextPosAndNormalIP == 0);
				}
			}
		}
	}
	current_planptr = NULL;
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
	TEST_ROLL_SKIPPED_OFFSET = ANGLE_QUARTER_TURN / 8,
	TEST_ROLL_CENTER_EDGE_OFFSET = 10,
	TEST_ROLL_CONTACT_EDGE_OFFSET = 30
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

static void roll_fixture_touchdown(struct CARSTATE *car, legacy_s16 angle)
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
	car->car_wheel_contact_positions[0] = (struct VECTOR){
		(legacy_s16)(car->car_position.lx / OWOOT_POSITION_SCALE),
		(legacy_s16)(car->car_position.ly / OWOOT_POSITION_SCALE - TEST_WHEEL_HEIGHT),
		(legacy_s16)(car->car_position.lz / OWOOT_POSITION_SCALE)};
}

static legacy_s16 roll_fixture_land(struct CARSTATE *car, legacy_s16 angle)
{
	roll_fixture_touchdown(car, angle);
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
	assert(extended_owoot_barrel_roll_complete(&car) == 0);
	roll_fixture_arm(&car);
	assert(roll_fixture_air(&car, ANGLE_QUARTER_TURN) == 1);
	assert(roll_fixture_air(&car, ANGLE_HALF_TURN) == 1);
	assert(extended_owoot_barrel_roll_complete(&car) == 0);
	struct CARSTATE checkpoint = car;
	assert(roll_fixture_air(&car, ANGLE_THREE_QUARTER_TURN) == 1);
	assert(roll_fixture_air(&car, 0) == 1);
	assert(extended_owoot_barrel_roll_complete(&car) == 1);
	struct CARSTATE tilted = car;
	assert(roll_fixture_land(&tilted, TEST_CAR_ROLL) == 0);
	/* The first tire may touch while the completed roll still leans, then
	 * suspension settles the wheels-down car onto its receiving road. */
	tilted = car;
	assert(roll_fixture_land(&tilted, TEST_ROLL_FIRST_TIRE_TILT) == 0);
	/* Native collision response owns landing orientation. A noncrashed car
	 * with real receiving-road contact does not need another OWOOT tilt test. */
	tilted = car;
	assert(roll_fixture_land(&tilted, ANGLE_QUARTER_TURN) == 0);
	tilted = car;
	assert(roll_fixture_land(&tilted, ANGLE_HALF_TURN) == 0);
	tilted = car;
	tilted.car_rotate.y = ANGLE_HALF_TURN;
	assert(roll_fixture_land(&tilted, 0) == 0);
	assert(roll_fixture_land(&car, 0) == 0);
	assert(car.car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_SOURCE] == 0);
	assert(extended_owoot_barrel_roll_complete(&car) == 0);
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

static void roll_fixture_complete_airborne(struct CARSTATE *car)
{
	roll_fixture_arm(car);
	assert(roll_fixture_air(car, ANGLE_QUARTER_TURN) == 1);
	assert(roll_fixture_air(car, ANGLE_HALF_TURN) == 1);
	assert(roll_fixture_air(car, ANGLE_THREE_QUARTER_TURN) == 1);
	assert(roll_fixture_air(car, 0) == 1);
}

static void test_roll_native_landing_contact(void)
{
	for (legacy_u16 trailing = 0; trailing < 2U; trailing++) {
		struct CARSTATE car = roll_fixture_car();
		roll_fixture_complete_airborne(&car);
		/* The first tire reaches the receiver before the center. The last
		 * tire can likewise remain on its runout after the center exits. */
		legacy_s16 piece = trailing ? TEST_ROLL_ROUTE_COUNT - 1U : 1;
		struct OWOOT_BOUNDS bounds;
		route_bounds(piece, &bounds);
		legacy_s32 edge = trailing ? bounds.minimum_z : bounds.maximum_z;
		legacy_s16 direction = trailing ? -1 : 1;
		car.car_position.lz =
			(edge + direction * TEST_ROLL_CENTER_EDGE_OFFSET) * OWOOT_POSITION_SCALE;
		car.car_previous_position = car.car_position;
		car.car_previous_position.lz += OWOOT_POSITION_SCALE;
		car.car_sumSurfAllWheels = CAR_SURFACE_PAVED;
		car.car_surfaceWhl[0] = CAR_SURFACE_PAVED;
		car.car_wheel_contact_positions[0] =
			(struct VECTOR){(legacy_s16)(car.car_position.lx / OWOOT_POSITION_SCALE), 0,
							(legacy_s16)(edge - direction * TEST_ROLL_CONTACT_EDGE_OFFSET)};
		assert(owoot_jump_is_valid(&car) == 0);
		assert(car.car_reserved_wheel_state[OWOOT_SCRATCH_JUMP_SOURCE] == 0);
	}
	/* An actual receiving-road contact is required, even after a complete roll. */
	struct CARSTATE car = roll_fixture_car();
	roll_fixture_complete_airborne(&car);
	struct CARSTATE grass = car;
	grass.car_sumSurfAllWheels = CAR_SURFACE_GRASS;
	grass.car_surfaceWhl[0] = CAR_SURFACE_GRASS;
	assert(owoot_jump_is_valid(&grass) == OWOOT_JUMP_FAILED);
	struct CARSTATE unrelated = car;
	unrelated.car_sumSurfAllWheels = CAR_SURFACE_PAVED;
	unrelated.car_surfaceWhl[0] = CAR_SURFACE_PAVED;
	assert(owoot_jump_is_valid(&unrelated) == OWOOT_JUMP_FAILED);
	/* Completing the roll does not exempt a still-airborne car from the corridor. */
	car.car_position.lx += TEST_ROLL_SIDE_ESCAPE * OWOOT_POSITION_SCALE;
	assert(owoot_jump_is_valid(&car) == OWOOT_JUMP_FAILED);
}

static void test_roll_completed_attempt_is_retained(void)
{
	struct CARSTATE car = roll_fixture_car();
	roll_fixture_complete_airborne(&car);
	/* A native landing response can reverse motion during first contact. */
	struct CARSTATE backwards = car;
	roll_fixture_touchdown(&backwards, 0);
	backwards.car_previous_position.lz = backwards.car_position.lz - OWOOT_POSITION_SCALE;
	assert(owoot_jump_is_valid(&backwards) == 0);
	/* A complete airborne turn remains earned if rotation later reverses. */
	assert(roll_fixture_air(&car, ANGLE_FULL_TURN - TEST_ROLL_SKIPPED_OFFSET) == 1);
	assert(roll_fixture_land(&car, ANGLE_HALF_TURN) == 0);
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

enum {
	TEST_PINE_COLUMN = 5,
	TEST_PINE_ROW = 5,
	TEST_PINE_CROWN_RADIUS = 60,
	TEST_PINE_SCATTER_MIN_SCALE = 96,
	TEST_PINE_MIN_TRUNK_DISTANCE = 80,
	TEST_PINE_SCATTER_MAX_SCALE = 224,
	TEST_PINE_TRUNK_WIDTH = 32,
	TEST_PINE_TRUNK_HEIGHT = 128,
	TEST_PINE_TRUNK_RADIUS = 136,
	TEST_PINE_CAR_EXTENT = 8,
	TEST_PINE_CAR_RADIUS = 16,
	TEST_PINE_CLEAR_GUARD = 30000,
	TEST_PINE_SLOPE_COUNT = 5,
	TEST_PINE_SIGNED_FIRST_WIDTH = -33,
	TEST_PINE_SIGNED_FIRST_HEIGHT = 129,
	TEST_PINE_SIGNED_SECOND_WIDTH = 35,
	TEST_PINE_SIGNED_SECOND_RADIUS = -137
};

static void test_pine_scatter(legacy_u8 count, const struct PINE_TREE_INSTANCE *expected)
{
	struct CSTRK_OBJECT object = {0};
	object.legacy_id = CSTRK_PINE_TREE_ID;
	object.pine_count = count;
	for (legacy_u8 index = 0; index < count; index++) {
		struct PINE_TREE_INSTANCE tree, repeated;
		assert(pine_trees_instance(&object, index, &tree));
		assert(memcmp(&tree, &expected[index], sizeof(tree)) == 0);
		assert(pine_trees_instance(&object, index, &repeated));
		assert(memcmp(&tree, &repeated, sizeof(tree)) == 0);
		for (legacy_u8 rotation = 1; rotation < TEST_ROTATION_COUNT; rotation++) {
			object.rotation = rotation;
			struct PINE_TREE_INSTANCE turned;
			assert(pine_trees_instance(&object, index, &turned));
			assert(turned.scale == tree.scale);
			if (rotation == 1) {
				assert(turned.x == tree.z && turned.z == -tree.x);
			} else if (rotation == 2) {
				assert(turned.x == -tree.x && turned.z == -tree.z);
			} else {
				assert(turned.x == -tree.z && turned.z == tree.x);
			}
		}
		object.rotation = 0;
	}
	struct PINE_TREE_INSTANCE tree;
	assert(!pine_trees_instance(&object, count, &tree));

	/* Adjacent tiles must not repeat the same trunk positions or size sequence. */
	for (legacy_u8 axis = 0; axis < 2; axis++) {
		object.x = axis == 0;
		object.y = axis != 0;
		legacy_u8 different_positions = 0, different_sizes = 0;
		for (legacy_u8 index = 0; index < count; index++) {
			assert(pine_trees_instance(&object, index, &tree));
			different_positions |= tree.x != expected[index].x || tree.z != expected[index].z;
			different_sizes |= tree.scale != expected[index].scale;
		}
		assert(different_positions && different_sizes);
	}

	/* Even the largest jitter and crowns remain inside their own tile. */
	for (legacy_u8 row = 0; row < CSTRK_MAX_DIMENSION; row++) {
		object.y = row;
		for (legacy_u8 column = 0; column < CSTRK_MAX_DIMENSION; column++) {
			object.x = column;
			struct PINE_TREE_INSTANCE placed[CSTRK_PINE_GROUP_LARGE_COUNT];
			for (legacy_u8 index = 0; index < count; index++) {
				assert(pine_trees_instance(&object, index, &tree));
				legacy_s32 x = tree.x < 0 ? -tree.x : tree.x;
				legacy_s32 z = tree.z < 0 ? -tree.z : tree.z;
				assert(tree.scale >= TEST_PINE_SCATTER_MIN_SCALE &&
					   tree.scale <= TEST_PINE_SCATTER_MAX_SCALE);
				assert(x * PINE_TREE_SCALE_ONE + TEST_PINE_CROWN_RADIUS * tree.scale <
					   CSTRK_TILE_WORLD_SIZE / 2 * PINE_TREE_SCALE_ONE);
				assert(z * PINE_TREE_SCALE_ONE + TEST_PINE_CROWN_RADIUS * tree.scale <
					   CSTRK_TILE_WORLD_SIZE / 2 * PINE_TREE_SCALE_ONE);
				for (legacy_u8 other = 0; other < index; other++) {
					legacy_s32 dx = tree.x - placed[other].x;
					legacy_s32 dz = tree.z - placed[other].z;
					assert(dx * dx + dz * dz >=
						   TEST_PINE_MIN_TRUNK_DISTANCE * TEST_PINE_MIN_TRUNK_DISTANCE);
				}
				placed[index] = tree;
			}
		}
	}
}

static void test_pine_group_scatter(void)
{
	/* Fixed samples also guard the editor's independently implemented recipe. */
	static const struct PINE_TREE_INSTANCE six[CSTRK_PINE_GROUP_SMALL_COUNT] = {
		{-302, 349, 155}, {17, 433, 186},	{344, 241, 139},
		{117, -109, 215}, {-30, -313, 145}, {-360, -121, 168}};
	static const struct PINE_TREE_INSTANCE ten[CSTRK_PINE_GROUP_LARGE_COUNT] = {
		{-266, 283, 131}, {-28, 358, 170},	 {250, 230, 147},  {-152, 147, 119}, {85, 133, 177},
		{306, 12, 144},	  {-427, -125, 157}, {-29, -219, 105}, {178, -332, 164}, {-211, -400, 141}};
	test_pine_scatter(CSTRK_PINE_GROUP_SMALL_COUNT, six);
	test_pine_scatter(CSTRK_PINE_GROUP_LARGE_COUNT, ten);
}

static void test_pine_group_collision_centers(void)
{
	static const legacy_s8 slopes[TEST_PINE_SLOPE_COUNT][2] = {
		{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}};
	static const legacy_u8 counts[] = {CSTRK_PINE_GROUP_SMALL_COUNT, CSTRK_PINE_GROUP_LARGE_COUNT};
	fixture_extended = 1;
	fixture_track.width = fixture_track.height = TRACK_GRID_SIZE;
	trkObjectList[CSTRK_PINE_TREE_ID].ss_physicalModel = PHYSICAL_MODEL_PINE_TREE;
	for (legacy_u8 group = 0; group < sizeof(counts) / sizeof(counts[0]); group++) {
		for (legacy_u8 rotation = 0; rotation < TEST_ROTATION_COUNT; rotation++) {
			for (legacy_u8 slope = 0; slope < TEST_PINE_SLOPE_COUNT; slope++) {
				fixture_object = (struct CSTRK_OBJECT){0};
				fixture_object.legacy_id = CSTRK_PINE_TREE_ID;
				fixture_object.x = TEST_PINE_COLUMN;
				fixture_object.y = TEST_PINE_ROW;
				fixture_object.width = fixture_object.height = 1;
				fixture_object.occupied_mask = 1;
				fixture_object.rotation = rotation;
				fixture_object.base_height = 1;
				fixture_object.slope_x = slopes[slope][0];
				fixture_object.slope_y = slopes[slope][1];
				fixture_object.pine_count = counts[group];
				struct VECTOR origin;
				extended_geometry_object_origin(&fixture_track, &fixture_object, &origin);
				struct VECTOR positions[CSTRK_PINE_GROUP_LARGE_COUNT + 1U] = {{0}};
				positions[counts[group]].x = TEST_PINE_CLEAR_GUARD;
				assert(get_track_collision_points(TEST_PINE_COLUMN, TEST_PINE_ROW, positions) ==
					   counts[group]);
				assert(positions[counts[group]].x == TEST_PINE_CLEAR_GUARD);
				for (legacy_u8 index = 0; index < counts[group]; index++) {
					struct PINE_TREE_INSTANCE tree;
					assert(pine_trees_instance(&fixture_object, index, &tree));
					assert(positions[index].x == LEGACY_S16_WRAP_ADD(origin.x, tree.x));
					assert(positions[index].z == LEGACY_S16_WRAP_ADD(origin.z, tree.z));
					assert(positions[index].y == origin.y + extended_geometry_object_grade(
																&fixture_object, tree.x, tree.z));
					assert(positions[index].x != origin.x || positions[index].z != origin.z);
				}
				/* The unmodified single pine retains its one centered collision point. */
				fixture_object.pine_count = 0;
				assert(get_track_collision_points(TEST_PINE_COLUMN, TEST_PINE_ROW, positions) == 1);
				assert(positions[0].x == origin.x && positions[0].z == origin.z);
				assert(positions[0].y ==
					   origin.y + extended_geometry_object_grade(&fixture_object, 0, 0));
			}
		}
	}
}

static void test_pine_group_collision_bounds(void)
{
	struct POINT2D native_bounds[2];
	native_bounds[0] = track_auxiliary_obstacle_bounds[0];
	native_bounds[1] = track_auxiliary_obstacle_bounds[1];
	track_auxiliary_obstacle_bounds[0] =
		(struct POINT2D){TEST_PINE_TRUNK_WIDTH, TEST_PINE_TRUNK_HEIGHT};
	track_auxiliary_obstacle_bounds[1] =
		(struct POINT2D){TEST_PINE_TRUNK_WIDTH, TEST_PINE_TRUNK_RADIUS};
	fixture_object.rotation = fixture_object.slope_x = fixture_object.slope_y = 0;
	fixture_object.pine_count = CSTRK_PINE_GROUP_LARGE_COUNT;
	struct VECTOR positions[CSTRK_PINE_GROUP_LARGE_COUNT];
	assert(get_track_collision_points(TEST_PINE_COLUMN, TEST_PINE_ROW, positions) ==
		   CSTRK_PINE_GROUP_LARGE_COUNT);
	struct POINT2D car_bounds[2] = {{TEST_PINE_CAR_EXTENT, TEST_PINE_CAR_EXTENT},
									{TEST_PINE_CAR_EXTENT, TEST_PINE_CAR_RADIUS}};
	struct VECTOR car_pose[2] = {{0}}, obstacle_pose[2] = {{0}};
	for (legacy_u8 index = 0; index < CSTRK_PINE_GROUP_LARGE_COUNT; index++) {
		struct POINT2D bounds[2];
		bounds[0] = track_auxiliary_obstacle_bounds[0];
		bounds[1] = track_auxiliary_obstacle_bounds[1];
		extended_track_collision_bounds(TEST_PINE_COLUMN, TEST_PINE_ROW, index, bounds);
		struct PINE_TREE_INSTANCE tree;
		assert(pine_trees_instance(&fixture_object, index, &tree));
		assert(bounds[0].px ==
			   TEST_PINE_TRUNK_WIDTH * tree.scale / (legacy_s32)PINE_TREE_SCALE_ONE);
		assert(bounds[0].py ==
			   TEST_PINE_TRUNK_HEIGHT * tree.scale / (legacy_s32)PINE_TREE_SCALE_ONE);
		assert(bounds[1].py ==
			   TEST_PINE_TRUNK_RADIUS * tree.scale / (legacy_s32)PINE_TREE_SCALE_ONE);
		obstacle_pose[0] = positions[index];
		car_pose[0] = positions[index];
		assert(car_collision_boxes_overlap(car_bounds, car_pose, bounds, obstacle_pose));
		extended_geometry_object_origin(&fixture_track, &fixture_object, &car_pose[0]);
		assert(!car_collision_boxes_overlap(car_bounds, car_pose, bounds, obstacle_pose));
	}
	struct POINT2D bounds[2];
	bounds[0] = track_auxiliary_obstacle_bounds[0];
	bounds[1] = track_auxiliary_obstacle_bounds[1];
	fixture_object.pine_count = 0;
	extended_track_collision_bounds(TEST_PINE_COLUMN, TEST_PINE_ROW, 0, bounds);
	assert(memcmp(bounds, track_auxiliary_obstacle_bounds, sizeof(bounds)) == 0);
	/* Signed, asymmetric bounds round toward zero, including negative values. */
	fixture_object.pine_count = CSTRK_PINE_GROUP_LARGE_COUNT;
	bounds[0] = (struct POINT2D){TEST_PINE_SIGNED_FIRST_WIDTH, TEST_PINE_SIGNED_FIRST_HEIGHT};
	bounds[1] = (struct POINT2D){TEST_PINE_SIGNED_SECOND_WIDTH, TEST_PINE_SIGNED_SECOND_RADIUS};
	struct PINE_TREE_INSTANCE first;
	assert(pine_trees_instance(&fixture_object, 0, &first));
	extended_track_collision_bounds(TEST_PINE_COLUMN, TEST_PINE_ROW, 0, bounds);
	assert(bounds[0].px ==
		   TEST_PINE_SIGNED_FIRST_WIDTH * first.scale / (legacy_s32)PINE_TREE_SCALE_ONE);
	assert(bounds[0].py ==
		   TEST_PINE_SIGNED_FIRST_HEIGHT * first.scale / (legacy_s32)PINE_TREE_SCALE_ONE);
	assert(bounds[1].px ==
		   TEST_PINE_SIGNED_SECOND_WIDTH * first.scale / (legacy_s32)PINE_TREE_SCALE_ONE);
	assert(bounds[1].py ==
		   TEST_PINE_SIGNED_SECOND_RADIUS * first.scale / (legacy_s32)PINE_TREE_SCALE_ONE);
	fixture_object.pine_count = 0;
	track_auxiliary_obstacle_bounds[0] = native_bounds[0];
	track_auxiliary_obstacle_bounds[1] = native_bounds[1];
}

legacy_int main(void)
{
	test_native_wall_response();
	test_fan_contacts_follow_exact_slopes();
	test_native_corkscrew_gates();
	test_sloped_tunnel_portals(0);
	test_sloped_tunnel_portals(1);
	test_roll_launch_corridor();
	test_roll_completion_and_checkpoint_state();
	test_roll_native_landing_contact();
	test_roll_completed_attempt_is_retained();
	test_roll_completion_on_cross_slope();
	test_roll_shortcuts_are_rejected();
	test_pine_group_scatter();
	test_pine_group_collision_centers();
	test_pine_group_collision_bounds();
	return 0;
}
