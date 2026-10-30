#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../c/carsetup.c"
#include "../c/stateply.c"
#include "../c/trackdata_layout.h"
#include "../c/track_objects.h"

#undef memcpy
#undef printf

static struct PLANE planes[TRACK_PLAN_RESOURCE_COUNT];
static struct TRACK_WALL walls[TRACK_WALL_RESOURCE_COUNT];
static legacy_u8 elements[TRACKDATA_MAP_SIZE];
static legacy_u8 terrain[TRACKDATA_MAP_SIZE];
static legacy_s8 signs[TRACKDATA_MAP_SIZE];

void fatal_error(const legacy_s8 *format, ...)
{
	(void)format;
	assert(0);
}

void update_crash_state(legacy_s16 event, legacy_s16 car_index)
{
	(void)event;
	(void)car_index;
	assert(0);
}

void owoot_update_player(struct CARSTATE *car, legacy_s16 car_index)
{
	(void)car;
	(void)car_index;
}

void emit_crash_particles(legacy_s16 kind, legacy_s16 angle, legacy_s16 speed)
{
	(void)kind;
	(void)angle;
	(void)speed;
	assert(0);
}

static void make_capability_resource(legacy_u8 resource[15], legacy_u8 value)
{
	memset(resource, 0, 15);
	resource_file_set_size(resource, 15);
	LEGACY_WRITE_U16_LE(resource + RESOURCE_FILE_COUNT_OFFSET, 1);
	memcpy(resource + RESOURCE_FILE_DIRECTORY_OFFSET, "fand", 4);
	resource_file_set_offset(resource, 1, 0, 0);
	resource[14] = value;
}

static void test_resource_capability(void)
{
	legacy_u8 resource[15];
	reset_car_capabilities();
	assert(car_surface_adhesion[PLAYER_CAR_INDEX] == 0);
	assert(car_surface_adhesion[OPPONENT_CAR_INDEX] == 0);
	setup_car_surface_adhesion(0, PLAYER_CAR_INDEX);
	assert(car_surface_adhesion[PLAYER_CAR_INDEX] == 0);

	/* A car's resource alone enables its capability for either driver. */
	make_capability_resource(resource, 1);
	setup_car_surface_adhesion(resource, PLAYER_CAR_INDEX);
	assert(car_surface_adhesion[PLAYER_CAR_INDEX] == 1);
	assert(car_surface_adhesion[OPPONENT_CAR_INDEX] == 0);
	setup_car_surface_adhesion(resource, OPPONENT_CAR_INDEX);
	assert(car_surface_adhesion[OPPONENT_CAR_INDEX] == 1);

	/* Only the exact supported flag value enables adhesion. A new selection
	 * must clear the previous car's capability without changing the other. */
	static const legacy_u8 inactive_values[] = {0, 2, 255};
	for (legacy_u16 index = 0; index < sizeof(inactive_values); index++) {
		resource[14] = inactive_values[index];
		setup_car_surface_adhesion(resource, PLAYER_CAR_INDEX);
		assert(car_surface_adhesion[PLAYER_CAR_INDEX] == 0);
		assert(car_surface_adhesion[OPPONENT_CAR_INDEX] == 1);
	}
	memcpy(resource + RESOURCE_FILE_DIRECTORY_OFFSET, "gnam", 4);
	setup_car_surface_adhesion(resource, OPPONENT_CAR_INDEX);
	assert(car_surface_adhesion[OPPONENT_CAR_INDEX] == 0);

	/* Empty or out-of-file flag payloads cannot activate the capability. */
	make_capability_resource(resource, 1);
	resource_file_set_size(resource, 14);
	setup_car_surface_adhesion(resource, PLAYER_CAR_INDEX);
	assert(car_surface_adhesion[PLAYER_CAR_INDEX] == 0);
	resource_file_set_size(resource, 15);
	resource_file_set_offset(resource, 1, 0, 1);
	setup_car_surface_adhesion(resource, PLAYER_CAR_INDEX);
	assert(car_surface_adhesion[PLAYER_CAR_INDEX] == 0);

	legacy_u8 oversized[16];
	make_capability_resource(oversized, 1);
	oversized[15] = 0;
	resource_file_set_size(oversized, sizeof(oversized));
	setup_car_surface_adhesion(oversized, PLAYER_CAR_INDEX);
	assert(car_surface_adhesion[PLAYER_CAR_INDEX] == 0);

	/* Reinitializing the game/replay clears both slots before car selection. */
	make_capability_resource(resource, 1);
	setup_car_surface_adhesion(resource, PLAYER_CAR_INDEX);
	setup_car_surface_adhesion(resource, OPPONENT_CAR_INDEX);
	reset_car_capabilities();
	assert(car_surface_adhesion[PLAYER_CAR_INDEX] == 0);
	assert(car_surface_adhesion[OPPONENT_CAR_INDEX] == 0);
}

/* The optional versioned settings follow the legacy one-byte fand flag. */
static void make_settings_resource(legacy_u8 resource[29], legacy_u16 max_distance,
								   legacy_u16 min_alignment, legacy_u8 surface_mask)
{
	memset(resource, 0, 29);
	resource_file_set_size(resource, 29);
	LEGACY_WRITE_U16_LE(resource + RESOURCE_FILE_COUNT_OFFSET, 2);
	memcpy(resource + RESOURCE_FILE_DIRECTORY_OFFSET, "fandadhs", 8);
	resource_file_set_offset(resource, 2, 0, 0);
	resource_file_set_offset(resource, 2, 1, 1);
	resource[22] = 1;
	resource[23] = 1;
	LEGACY_WRITE_U16_LE(resource + 24, max_distance);
	LEGACY_WRITE_U16_LE(resource + 26, min_alignment);
	resource[28] = surface_mask;
}

static void assert_settings(legacy_s16 car_index, legacy_u16 max_distance, legacy_u16 min_alignment,
							legacy_u8 surface_mask)
{
	assert(car_surface_adhesion_settings[car_index].max_distance == max_distance);
	assert(car_surface_adhesion_settings[car_index].min_alignment == min_alignment);
	assert(car_surface_adhesion_settings[car_index].surface_mask == surface_mask);
}

static void test_resource_settings(void)
{
	legacy_u8 resource[30];
	legacy_u8 legacy_resource[15];
	reset_car_capabilities();
	assert_settings(PLAYER_CAR_INDEX, 12, 8192, 15);
	assert_settings(OPPONENT_CAR_INDEX, 12, 8192, 15);

	make_settings_resource(resource, 24, 14189, 5);
	setup_car_surface_adhesion(resource, PLAYER_CAR_INDEX);
	assert(car_surface_adhesion[PLAYER_CAR_INDEX] == 1);
	assert_settings(PLAYER_CAR_INDEX, 24, 14189, 5);
	assert_settings(OPPONENT_CAR_INDEX, 12, 8192, 15);
	make_settings_resource(resource, 4, 0, 2);
	setup_car_surface_adhesion(resource, OPPONENT_CAR_INDEX);
	assert(car_surface_adhesion[OPPONENT_CAR_INDEX] == 1);
	assert_settings(OPPONENT_CAR_INDEX, 4, 0, 2);
	assert_settings(PLAYER_CAR_INDEX, 24, 14189, 5);

	/* Selecting a legacy car restores its defaults without leaking settings
	 * between drivers, even if the preceding car customized every field. */
	make_capability_resource(legacy_resource, 1);
	setup_car_surface_adhesion(legacy_resource, PLAYER_CAR_INDEX);
	assert(car_surface_adhesion[PLAYER_CAR_INDEX] == 1);
	assert_settings(PLAYER_CAR_INDEX, 12, 8192, 15);
	assert_settings(OPPONENT_CAR_INDEX, 4, 0, 2);
	setup_car_surface_adhesion(0, OPPONENT_CAR_INDEX);
	assert(car_surface_adhesion[OPPONENT_CAR_INDEX] == 0);
	assert_settings(OPPONENT_CAR_INDEX, 12, 8192, 15);

	/* Directory order need not match data order. */
	make_settings_resource(resource, 511, 16384, 15);
	memcpy(resource + RESOURCE_FILE_DIRECTORY_OFFSET, "adhsfand", 8);
	resource_file_set_offset(resource, 2, 0, 1);
	resource_file_set_offset(resource, 2, 1, 0);
	setup_car_surface_adhesion(resource, PLAYER_CAR_INDEX);
	assert(car_surface_adhesion[PLAYER_CAR_INDEX] == 1);
	assert_settings(PLAYER_CAR_INDEX, 511, 16384, 15);

	/* Zero means exact contact, any tire-facing angle, and no allowed material.
	 * These are valid settings, rather than absent values or an enable flag. */
	make_settings_resource(resource, 0, 0, 0);
	setup_car_surface_adhesion(resource, OPPONENT_CAR_INDEX);
	assert(car_surface_adhesion[OPPONENT_CAR_INDEX] == 1);
	assert_settings(OPPONENT_CAR_INDEX, 0, 0, 0);
	reset_car_capabilities();
	assert_settings(PLAYER_CAR_INDEX, 12, 8192, 15);
	assert_settings(OPPONENT_CAR_INDEX, 12, 8192, 15);

	/* Settings alone cannot enable adhesion. */
	make_settings_resource(resource, 24, 8192, 15);
	memcpy(resource + RESOURCE_FILE_DIRECTORY_OFFSET, "gnam", 4);
	setup_car_surface_adhesion(resource, PLAYER_CAR_INDEX);
	assert(car_surface_adhesion[PLAYER_CAR_INDEX] == 0);

	make_settings_resource(resource, 7, 14189, 2);
	setup_car_surface_adhesion(resource, OPPONENT_CAR_INDEX);

	/* A malformed extension must fail closed instead of silently using the
	 * legacy behavior, or keeping settings left over from the preceding car. */
	for (legacy_u16 invalid = 0; invalid < 8; invalid++) {
		make_settings_resource(resource, 24, 8192, 15);
		setup_car_surface_adhesion(resource, PLAYER_CAR_INDEX);
		assert(car_surface_adhesion[PLAYER_CAR_INDEX] == 1);
		switch (invalid) {
			case 0:
				resource[23] = 0;
				break;
			case 1:
				resource[23] = 2;
				break;
			case 2:
				LEGACY_WRITE_U16_LE(resource + 24, 512);
				break;
			case 3:
				LEGACY_WRITE_U16_LE(resource + 26, 16385);
				break;
			case 4:
				resource[28] = 16;
				break;
			case 5:
				resource_file_set_size(resource, 28);
				break;
			case 6:
				resource[29] = 0;
				resource_file_set_size(resource, 30);
				break;
			case 7:
				resource_file_set_offset(resource, 2, 1, 8);
				break;
		}
		setup_car_surface_adhesion(resource, PLAYER_CAR_INDEX);
		assert(car_surface_adhesion[PLAYER_CAR_INDEX] == 0);
		assert_settings(PLAYER_CAR_INDEX, 12, 8192, 15);
		assert(car_surface_adhesion[OPPONENT_CAR_INDEX] == 1);
		assert_settings(OPPONENT_CAR_INDEX, 7, 14189, 2);
	}

	/* Duplicate extension identifiers are ambiguous even if both agree. */
	legacy_u8 duplicate[43];
	memset(duplicate, 0, sizeof(duplicate));
	resource_file_set_size(duplicate, sizeof(duplicate));
	LEGACY_WRITE_U16_LE(duplicate + RESOURCE_FILE_COUNT_OFFSET, 3);
	memcpy(duplicate + RESOURCE_FILE_DIRECTORY_OFFSET, "fandadhsadhs", 12);
	resource_file_set_offset(duplicate, 3, 0, 0);
	resource_file_set_offset(duplicate, 3, 1, 1);
	resource_file_set_offset(duplicate, 3, 2, 7);
	duplicate[30] = 1;
	make_settings_resource(resource, 24, 8192, 15);
	memcpy(duplicate + 31, resource + 23, 6);
	memcpy(duplicate + 37, resource + 23, 6);
	setup_car_surface_adhesion(duplicate, PLAYER_CAR_INDEX);
	assert(car_surface_adhesion[PLAYER_CAR_INDEX] == 0);
	assert_settings(PLAYER_CAR_INDEX, 12, 8192, 15);
	reset_car_capabilities();
}

static void initialize_plane(legacy_s16 roll)
{
	memset(planes, 0, sizeof(planes));
	planptr = planes;
	planindex = 4;
	current_planptr = &planes[planindex];
	mat_rot_z(&planes[planindex].plane_rotation, roll);
	car_to_world_rotation = planes[planindex].plane_rotation;
	struct VECTOR up = {0, 8192, 0};
	mat_mul_vector(&up, &car_to_world_rotation, &planes[planindex].plane_normal);
	planes[planindex].plane_origin.y = 1000;
	elem_xCenter = elem_zCenter = terrainHeight = 0;
	current_surf_type = CAR_SURFACE_PAVED;
	track_wall_collision_enabled = 0;
	framespersec = GAME_FRAME_RATE_NORMAL;
	state.game_inputmode = GAME_INPUT_MODE_ACTIVE;
}

static void test_selected_car_capability(void)
{
	struct CARSTATE car = {0};
	struct SIMD simd = {0};
	struct PLAYER_WHEEL_MOTION motion = {0};
	car.car_sumSurfAllWheels = 4;
	car_working_x = car_working_z = 10000L * 64;
	car_working_y = 1000L * 64;
	car_working_pitch = car_working_yaw = 0;
	car_working_roll = ANGLE_HALF_TURN;
	framespersec = GAME_FRAME_RATE_NORMAL;
	legacy_u8 resource[15];
	make_capability_resource(resource, 1);
	reset_car_capabilities();
	setup_car_surface_adhesion(resource, PLAYER_CAR_INDEX);
	prepare_wheel_motion(&car, &simd, &motion, PLAYER_CAR_INDEX);
	assert(motion.surface_adhesion == 1);
	assert(motion.car_index == PLAYER_CAR_INDEX);
	assert(motion.inverted_adjustment == 0);
	assert(motion.current[0].ly == car_working_y + PLAYER_PHYSICS_SUSPENSION_TRAVEL_LIMIT);
	prepare_wheel_motion(&car, &simd, &motion, OPPONENT_CAR_INDEX);
	assert(motion.surface_adhesion == 0);
	assert(motion.car_index == OPPONENT_CAR_INDEX);
	assert(motion.inverted_adjustment == -PLAYER_PHYSICS_INVERTED_WHEEL_ADJUSTMENT);
	assert(motion.current[0].ly == car_working_y + PLAYER_PHYSICS_SUSPENSION_TRAVEL_LIMIT -
									   PLAYER_PHYSICS_INVERTED_WHEEL_ADJUSTMENT);
	setup_car_surface_adhesion(resource, OPPONENT_CAR_INDEX);
	prepare_wheel_motion(&car, &simd, &motion, OPPONENT_CAR_INDEX);
	assert(motion.surface_adhesion == 1);
	assert(motion.inverted_adjustment == 0);
	reset_car_capabilities();
}

static void assert_configured_contact(struct PLAYER_WHEEL_MOTION *motion, legacy_s16 distance,
									  legacy_s16 relative_roll, legacy_s16 surface,
									  legacy_s16 expected)
{
	initialize_plane(ANGLE_HALF_TURN);
	mat_rot_z(&car_to_world_rotation, ANGLE_HALF_TURN + relative_roll);
	current_surf_type = surface;
	struct CARSTATE car = {0};
	motion->current[0] = (struct VECTORLONG){0, (1000L - distance) * 64, 0};
	measure_wheel_plane_distance(&motion->current[0]);
	assert(nextPosAndNormalIP == distance);
	assert(hold_wheel_to_surface(&car, motion, 0) == expected);
	if (expected) {
		assert(nextPosAndNormalIP == 0);
		assert(motion->current[0].lx == 0);
		assert(motion->current[0].ly == 1000L * 64);
		assert(motion->current[0].lz == 0);
	}
}

static void test_configured_contacts(void)
{
	legacy_u8 resource[29];
	struct PLAYER_WHEEL_MOTION player = {0};
	struct PLAYER_WHEEL_MOTION opponent = {0};
	player.surface_adhesion = opponent.surface_adhesion = 1;
	opponent.car_index = OPPONENT_CAR_INDEX;
	reset_car_capabilities();

	/* A smaller capture radius replaces the old twelve-unit tolerance. The
	 * other driver's larger radius must also work beyond that old tolerance. */
	make_settings_resource(resource, 4, 8192, 15);
	setup_car_surface_adhesion(resource, PLAYER_CAR_INDEX);
	make_settings_resource(resource, 24, 8192, 15);
	setup_car_surface_adhesion(resource, OPPONENT_CAR_INDEX);
	assert_configured_contact(&player, 4, 0, CAR_SURFACE_PAVED, 1);
	assert_configured_contact(&player, 5, 0, CAR_SURFACE_PAVED, 0);
	assert_configured_contact(&player, 13, 0, CAR_SURFACE_PAVED, 0);
	assert_configured_contact(&opponent, 13, 0, CAR_SURFACE_PAVED, 1);
	assert_configured_contact(&opponent, 24, 0, CAR_SURFACE_PAVED, 1);
	assert_configured_contact(&opponent, 25, 0, CAR_SURFACE_PAVED, 0);
	assert_configured_contact(&opponent, -1, 0, CAR_SURFACE_PAVED, 0);

	make_settings_resource(resource, 0, 8192, 15);
	setup_car_surface_adhesion(resource, PLAYER_CAR_INDEX);
	assert_configured_contact(&player, 0, 0, CAR_SURFACE_PAVED, 1);
	assert_configured_contact(&player, 1, 0, CAR_SURFACE_PAVED, 0);
	make_settings_resource(resource, 511, 8192, 15);
	setup_car_surface_adhesion(resource, PLAYER_CAR_INDEX);
	assert_configured_contact(&player, 511, 0, CAR_SURFACE_PAVED, 1);
	assert_configured_contact(&player, 512, 0, CAR_SURFACE_PAVED, 0);

	/* A 45-degree relative tire orientation fits the legacy 60-degree limit,
	 * but fails a resource specifying about 30 degrees (Q14 cosine 14189). */
	make_settings_resource(resource, 24, 14189, 15);
	setup_car_surface_adhesion(resource, PLAYER_CAR_INDEX);
	assert_configured_contact(&player, 1, 0, CAR_SURFACE_PAVED, 1);
	assert_configured_contact(&player, 1, ANGLE_QUARTER_TURN / 2, CAR_SURFACE_PAVED, 0);
	assert_configured_contact(&opponent, 1, ANGLE_QUARTER_TURN / 2, CAR_SURFACE_PAVED, 1);
	assert_configured_contact(&opponent, 1, ANGLE_QUARTER_TURN, CAR_SURFACE_PAVED, 0);
	make_settings_resource(resource, 24, 0, 15);
	setup_car_surface_adhesion(resource, PLAYER_CAR_INDEX);
	assert_configured_contact(&player, 1, ANGLE_QUARTER_TURN, CAR_SURFACE_PAVED, 1);
	assert_configured_contact(&player, 1, ANGLE_HALF_TURN, CAR_SURFACE_PAVED, 0);
	make_settings_resource(resource, 24, 16384, 15);
	setup_car_surface_adhesion(resource, PLAYER_CAR_INDEX);
	assert_configured_contact(&player, 1, 0, CAR_SURFACE_PAVED, 1);
	assert_configured_contact(&player, 1, ANGLE_QUARTER_TURN / 2, CAR_SURFACE_PAVED, 0);

	/* Fixed-point sine/cosine vectors at 45 degrees are slightly shorter than
	 * one. A zero-angle setting must still accept identical orientations. */
	initialize_plane(ANGLE_QUARTER_TURN / 2);
	struct CARSTATE car = {0};
	player.current[0] = (struct VECTORLONG){0, 1000L * 64, 0};
	measure_wheel_plane_distance(&player.current[0]);
	assert(hold_wheel_to_surface(&car, &player, 0));
	mat_rot_z(&car_to_world_rotation, ANGLE_QUARTER_TURN / 2 + 1);
	measure_wheel_plane_distance(&player.current[0]);
	assert(!hold_wheel_to_surface(&car, &player, 0));

	/* Every material bit gates its own surface, without allowing water or
	 * unknown material identifiers through even when every bit is set. */
	for (legacy_s16 allowed = CAR_SURFACE_PAVED; allowed <= CAR_SURFACE_GRASS; allowed++) {
		make_settings_resource(resource, 24, 8192,
							   (legacy_u8)(1U << (allowed - CAR_SURFACE_PAVED)));
		setup_car_surface_adhesion(resource, PLAYER_CAR_INDEX);
		for (legacy_s16 surface = CAR_SURFACE_PAVED; surface <= CAR_SURFACE_GRASS; surface++) {
			assert_configured_contact(&player, 1, 0, surface, surface == allowed);
		}
	}
	make_settings_resource(resource, 24, 8192, 0);
	setup_car_surface_adhesion(resource, PLAYER_CAR_INDEX);
	for (legacy_s16 surface = CAR_SURFACE_PAVED; surface <= CAR_SURFACE_GRASS; surface++) {
		assert_configured_contact(&player, 0, 0, surface, 0);
	}
	make_settings_resource(resource, 24, 8192, 15);
	setup_car_surface_adhesion(resource, PLAYER_CAR_INDEX);
	assert_configured_contact(&player, 1, 0, CAR_SURFACE_WATER, 0);
	assert_configured_contact(&player, 1, 0, CAR_SURFACE_PAVED - 1, 0);
	assert_configured_contact(&player, 1, 0, CAR_SURFACE_GRASS + 1, 0);
	reset_car_capabilities();
}

static void test_stopped_inverted_and_vertical_contacts(void)
{
	static const legacy_s16 rolls[] = {ANGLE_HALF_TURN, ANGLE_QUARTER_TURN};
	static const legacy_u16 rates[] = {GAME_FRAME_RATE_NORMAL, GAME_FRAME_RATE_LOW};
	static const legacy_u32 fractions[] = {0, 21845UL, PHYSICS_FRACTION_ONE};
	for (legacy_u16 orientation = 0; orientation < 2; orientation++) {
		for (legacy_u16 rate = 0; rate < 2; rate++) {
			for (legacy_u16 fraction = 0; fraction < 3; fraction++) {
				initialize_plane(rolls[orientation]);
				framespersec = rates[rate];
				player_motion_fraction = fractions[fraction];
				struct CARSTATE car = {0};
				struct PLAYER_WHEEL_MOTION motion = {0};
				motion.surface_adhesion = 1;
				for (legacy_s16 wheel = 0; wheel < 4; wheel++) {
					motion.current[wheel].ly = 1000L * 64;
					for (legacy_u16 tick = 0; tick < 200; tick++) {
						measure_wheel_plane_distance(&motion.current[wheel]);
						motion.previous[wheel] = motion.current[wheel];
						car.car_wheel_vertical_speed[wheel] = 10;
						assert(resolve_wheel_plane_contact(&car, &motion, wheel, PLAYER_CAR_INDEX));
						assert(car.car_wheel_vertical_speed[wheel] == 0);
						assert(motion.contact_distances[wheel] == 0);
						assert(nextPosAndNormalIP == 0);
						assert(motion.current[wheel].lx == 0);
						assert(motion.current[wheel].ly == 1000L * 64);
						assert(motion.current[wheel].lz == 0);
					}
				}
			}
		}
	}
	player_motion_fraction = 0;
}

static void test_contact_releases_and_stock_gravity(void)
{
	initialize_plane(ANGLE_HALF_TURN);
	struct CARSTATE car = {0};
	struct PLAYER_WHEEL_MOTION motion = {0};
	motion.surface_adhesion = 1;
	motion.current[0].ly = (1000L - PLAYER_PHYSICS_CONTACT_DISTANCE_LIMIT) * 64;
	measure_wheel_plane_distance(&motion.current[0]);
	assert(hold_wheel_to_surface(&car, &motion, 0));
	assert(motion.current[0].ly == 1000L * 64);

	/* Past the contact tolerance an inverted car falls normally. */
	motion.current[0].ly = (1000L - PLAYER_PHYSICS_CONTACT_DISTANCE_LIMIT - 1) * 64;
	measure_wheel_plane_distance(&motion.current[0]);
	assert(!hold_wheel_to_surface(&car, &motion, 0));
	legacy_s32 previous_y = motion.current[0].ly;
	move_airborne_wheel(&car, &motion, 0);
	assert(motion.current[0].ly < previous_y);
	assert(car.car_surfaceWhl[0] == CAR_WHEEL_CONTACT_NONE);

	/* With no capability, a stopped inverted wheel keeps the legacy fall. */
	motion.surface_adhesion = 0;
	car.car_wheel_vertical_speed[0] = 0;
	motion.current[0].ly = 999L * 64;
	measure_wheel_plane_distance(&motion.current[0]);
	assert(!hold_wheel_to_surface(&car, &motion, 0));
	move_airborne_wheel(&car, &motion, 0);
	assert(motion.current[0].ly == 999L * 64 - wheel_gravity_steps[0]);

	/* Neither a roof impact nor water may become a driving contact. */
	motion.surface_adhesion = 1;
	mat_rot_z(&car_to_world_rotation, 0);
	nextPosAndNormalIP = 1;
	assert(!hold_wheel_to_surface(&car, &motion, 0));
	car_to_world_rotation = planes[planindex].plane_rotation;
	current_surf_type = CAR_SURFACE_WATER;
	assert(!hold_wheel_to_surface(&car, &motion, 0));
}

static void test_finite_loop_surface(void)
{
	memset(elements, 0, sizeof(elements));
	memset(terrain, 0, sizeof(terrain));
	memset(planes, 0, sizeof(planes));
	memset(signs, -1, sizeof(signs));
	roadside_sign_indices_by_tile = signs;
	track_element_map = elements;
	track_terrain_map = terrain;
	planptr = planes;
	wallptr = walls;
	for (legacy_s16 index = 0; index < TRACK_GRID_SIZE; index++) {
		trackrows[index] = index * TRACK_GRID_SIZE;
		terrainrows[index] = (TRACK_GRID_LAST_INDEX - index) * TRACK_GRID_SIZE;
		track_column_centers[index] = index * 1024 + 512;
		terraincenterpos[index] = index * 1024 + 512;
		track_column_positions[index] = index * 1024;
		terrainpos[index] = index * 1024;
	}
	elements[terrainrows[10] + 10] = 1;
	trkObjectList[1].ss_physicalModel = PHYSICAL_MODEL_LOOP;
	trkObjectList[1].ss_rotY = 0;
	trkObjectList[1].ss_multiTileFlag = 0;
	trkObjectList[1].ss_surfaceType = 0;
	/* The original loop's six facets on both mirrored halves. */
	static const struct VECTOR origins[] = {{-367, 135, 224}, {-334, 299, 389}, {-300, 524, 449},
											{-266, 749, 389}, {-233, 914, 225}, {-200, 975, 0}};
	static const struct VECTOR normals[] = {{0, 7016, -4228},  {0, 5810, -5774},
											{0, 2110, -7915},  {0, -2110, -7915},
											{0, -5774, -5810}, {0, -7906, -2143}};
	for (legacy_s16 half = 0; half < 2; half++) {
		for (legacy_s16 facet = 0; facet < 6; facet++) {
			legacy_s16 index = (half == 0 ? 180 : 204) + facet * 4;
			planes[index].plane_origin = origins[facet];
			planes[index].plane_normal = normals[facet];
			if (half != 0) {
				planes[index].plane_origin.x = -planes[index].plane_origin.x;
				planes[index].plane_origin.z = -planes[index].plane_origin.z;
				planes[index].plane_normal.z = -planes[index].plane_normal.z;
			}
			mat_rot_x(&planes[index].plane_rotation,
					  polarAngle(planes[index].plane_normal.z, planes[index].plane_normal.y));
		}
	}
	car_to_world_rotation = planes[200].plane_rotation;

	struct CARSTATE car = {0};
	struct PLAYER_WHEEL_MOTION motion = {0};
	motion.surface_adhesion = 1;
	motion.current[0] = (struct VECTORLONG){(10752L - 180) * 64, 977L * 64, 10752L * 64};
	look_up_wheel_surface(&car, &motion, 0);
	assert(planindex == 200);
	assert(hold_wheel_to_surface(&car, &motion, 0));

	/* Leaving the loop lane selects the distant ground, releasing the fan. */
	motion.current[0].lx += 600L * 64;
	look_up_wheel_surface(&car, &motion, 0);
	assert(planindex == 0);
	assert(!hold_wheel_to_surface(&car, &motion, 0));
	legacy_s32 previous_y = motion.current[0].ly;
	move_airborne_wheel(&car, &motion, 0);
	assert(motion.current[0].ly < previous_y);
}

static void configure_loop_car(struct CARSTATE *car, struct SIMD *simd, legacy_s16 height,
							   legacy_s16 depth, legacy_s16 pitch, legacy_s16 car_index)
{
	memset(car, 0, sizeof(*car));
	memset(simd, 0, sizeof(*simd));
	for (legacy_s16 wheel = 0; wheel < 4; wheel++) {
		simd->wheel_coords[wheel].x = (wheel == 0 || wheel == 3 ? -20 : 20) * 64;
		simd->wheel_coords[wheel].z = (wheel < 2 ? 30 : -30) * 64;
		car->car_surfaceWhl[wheel] = CAR_SURFACE_PAVED;
	}
	car->car_position = (struct VECTORLONG){10752L * 64, height * 64L, (10752L + depth) * 64};
	car->car_rotate.y = pitch;
	car->car_sumSurfAllWheels = 4;
	legacy_u8 resource[15];
	make_capability_resource(resource, 1);
	reset_car_capabilities();
	setup_car_surface_adhesion(resource, car_index);
	player_motion_fraction = 0;
	state.game_inputmode = GAME_INPUT_MODE_ACTIVE;
}

static void test_full_stopped_loop_update(void)
{
	static const legacy_u16 rates[] = {GAME_FRAME_RATE_NORMAL, GAME_FRAME_RATE_LOW};
	for (legacy_u16 rate = 0; rate < 2; rate++) {
		for (legacy_s16 car_index = PLAYER_CAR_INDEX; car_index <= OPPONENT_CAR_INDEX;
			 car_index++) {
			test_finite_loop_surface();
			struct CARSTATE car;
			struct CARSTATE other = {0};
			struct SIMD simd;
			struct SIMD other_simd = {0};
			configure_loop_car(&car, &simd, 942, 98, 469, car_index);
			framespersec = rates[rate];
			struct VECTORLONG start = car.car_position;
			for (legacy_s16 tick = 0; tick < 200; tick++) {
				update_player_state(&car, &simd, &other, &other_simd, car_index);
				assert(car.car_sumSurfAllWheels == 4);
				assert(car.car_crashBmpFlag == CRASH_EVENT_NONE);
				assert(car.car_actual_speed == CAR_SPEED_STOPPED);
				/* Include suspension and pose reconstruction: bounded fixed-point
				 * rounding cannot become falling over 10/20 seconds at either rate. */
				assert(car.car_position.ly > start.ly - 2 * 64);
				assert(car.car_position.ly < start.ly + 6 * 64);
				assert(car.car_position.lz > start.lz - 6 * 64);
				assert(car.car_position.lz < start.lz + 2 * 64);
				struct VECTOR up = {0, TRIG_FIXED_ONE, 0};
				struct VECTOR world_up;
				struct MATRIX rotation = *mat_rot_zxy(-car.car_rotate.z, -car.car_rotate.y,
													  -car.car_rotate.x, MATRIX_ROTATION_ORDER_ZXY);
				mat_mul_vector(&up, &rotation, &world_up);
				assert(world_up.y < -TRIG_FIXED_ONE / 2);
			}
		}
	}
}

static void test_full_moving_loop_facets(void)
{
	static const legacy_u16 rates[] = {GAME_FRAME_RATE_NORMAL, GAME_FRAME_RATE_LOW};
	static const legacy_u16 speeds[] = {30 * 256, 60 * 256, 120 * 256};
	for (legacy_u16 rate = 0; rate < 2; rate++) {
		for (legacy_u16 speed = 0; speed < 3; speed++) {
			test_finite_loop_surface();
			struct CARSTATE car;
			struct CARSTATE other = {0};
			struct SIMD simd;
			struct SIMD other_simd = {0};
			configure_loop_car(&car, &simd, 834, 294, 384, PLAYER_CAR_INDEX);
			framespersec = rates[rate];
			car.car_actual_speed = speeds[speed];
			legacy_s16 tick;
			for (tick = 0; tick < 160 && car.car_position.lz > (10752L - 280) * 64; tick++) {
				update_player_state(&car, &simd, &other, &other_simd, PLAYER_CAR_INDEX);
				assert(car.car_sumSurfAllWheels == 4);
				assert(car.car_crashBmpFlag == CRASH_EVENT_NONE);
			}
			assert(tick < 160);
			assert(car.car_position.lz < (10752L - 280) * 64);
		}
	}
}

legacy_int main(void)
{
	test_resource_capability();
	test_resource_settings();
	test_selected_car_capability();
	test_configured_contacts();
	test_stopped_inverted_and_vertical_contacts();
	test_contact_releases_and_stock_gravity();
	test_finite_loop_surface();
	test_full_stopped_loop_update();
	test_full_moving_loop_facets();
	puts("car surface adhesion tests passed");
	return 0;
}
