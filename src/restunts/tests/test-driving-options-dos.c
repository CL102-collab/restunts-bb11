/* Keep the complete game startup and 16-bit memory model in this regression. */
#define run_main_menu_loop driving_options_original_main_menu_loop
#include "../c/restunts.c"
#undef run_main_menu_loop

/* Exercise the actual contact solver, including its file-private wheel state. */
#include "../c/stateply.c"
/* Keep the actual route-confirmation and penalty commit in this DOS regression. */
#include "../c/state.c"
#include "audio_internal.h"
#include "car_speed.h"

static const legacy_s8 *driving_options_failure;

static void driving_options_fill(void *memory, legacy_u8 value, legacy_u16 length)
{
	legacy_u8 *bytes = memory;
	for (legacy_u16 index = 0; index < length; index++) {
		bytes[index] = value;
	}
}

#define CHECK_OPTION(condition, message)                                                           \
	do {                                                                                           \
		if (!(condition)) {                                                                        \
			driving_options_failure = message;                                                     \
			return 1;                                                                              \
		}                                                                                          \
	} while (0)

/* Use the game's allocated far Trackdata buffers, so this also exercises the
 * real DOS memory model and shared startup parser rather than a host fixture. */
static void driving_options_reset_dualway(void)
{
	driving_options_fill(&state, 0, sizeof(state));
	track_pieces_counter = 19;
	for (legacy_s16 piece = 0; piece < track_pieces_counter; piece++) {
		track_primary_route_links[piece] = piece + 1;
		track_alternate_route_links[piece] = -1;
		track_route_columns[piece] = (legacy_s8)piece;
		track_route_rows[piece] = 5;
		track_route_element_ids[piece] = 0;
	}
	trkObjectList[0].ss_multiTileFlag = 0;
	track_primary_route_links[7] = 16;
	track_alternate_route_links[1] = 8;
	track_route_columns[8] = track_route_columns[2];
	track_primary_route_links[18] = 0;
	state.game_startcol = state.game_startcol2 = -1;
	state.game_startrow = state.game_startrow2 = -1;
	state.game_player_route_status = ROUTE_TRACKING_NORMAL;
	framespersec = GAME_FRAME_RATE_NORMAL;
}

static void driving_options_visit_route(legacy_s16 piece)
{
	state.playerstate.car_position.lx = (legacy_s32)track_route_columns[piece] * 65536L;
	state.playerstate.car_position.lz = (29L - track_route_rows[piece]) * 65536L;
	update_player_route_penalty();
}

static legacy_s16 driving_options_check_dualway(legacy_s16 fixed)
{
	for (legacy_s16 direction = 0; direction < 2; direction++) {
		driving_options_reset_dualway();
		state.game_player_confirmed_route = state.game_player_previous_route = direction ? 9 : 3;
		legacy_s16 piece = direction ? 4 : 13;
		while (piece != 0) {
			driving_options_visit_route(piece);
			piece = track_primary_route_links[piece];
		}
		driving_options_visit_route(0);
		CHECK_OPTION(state.playerstate.car_lap_count == 1, "DWS lap completion");
		CHECK_OPTION(state.game_penalty == (fixed ? (direction ? 24 : 18) * 20 : 0),
					 "DWS startup switch reaches actual penalty calculation");
	}
	const legacy_s8 *message =
		fixed ? "DWS penalties: 18/24 seconds\r\n" : "DWS penalties: 0/0 seconds\r\n";
	dos_write_stdout(message, strlen(message));
	return 0;
}

static legacy_s16 driving_options_check(legacy_s16 opt_in)
{
	struct VECTOR first = {100, 200, -100};
	struct VECTOR second = {-100, -200, 100};
	struct VECTOR crossing;
	interpolate_collision_at_z(&first, &second, &crossing, 0);
	CHECK_OPTION(crossing.x == (opt_in ? 0 : 100), "legacy collision switch");
	CHECK_OPTION(scale_cornering_angle(-3, 1) == (opt_in ? -1 : -2), "left corner bias switch");
	CHECK_OPTION(owoot_enabled == opt_in, "OWOOT startup switch");
	CHECK_OPTION(startup_options.skip_intro == opt_in, "skip intro switch");
	CHECK_OPTION(startup_options.sound_disabled == opt_in, "sound startup switch");
	CHECK_OPTION(startup_options.unused_nd_option == opt_in, "legacy nd recognition");
	CHECK_OPTION(startup_options.fps_display_enabled == opt_in && fps_display_enabled == opt_in,
				 "FPS startup option reaches shared renderer");
	CHECK_OPTION(audio_music_enabled == !opt_in && audio_effects_enabled == !opt_in,
				 "sound switch reaches audio engine");
	CHECK_OPTION(stricmp(audiodriverstring, opt_in ? "PC15" : "AD15") == 0,
				 "DOS audio driver selection");

	struct SIMD test_simd = {0};
	test_simd.surface_grip[CAR_SURFACE_GRASS - CAR_SURFACE_PAVED] = 123;
	apply_amiga_grass(&test_simd, "ANSX");
	CHECK_OPTION(test_simd.surface_grip[CAR_SURFACE_GRASS - CAR_SURFACE_PAVED] ==
					 (opt_in ? 128 : 123),
				 "Amiga grass grip switch");

	struct CARSTATE test_car = {0};
	static legacy_s16 drag[64];
	test_simd.num_gears = 5;
	test_simd.car_mass = 15;
	test_simd.idle_rpm = 1000;
	test_simd.downshift_rpm = 2000;
	test_simd.upshift_rpm = 6000;
	test_simd.max_rpm = 12000;
	test_simd.idle_torque = 32;
	test_simd.aerorestable = drag;
	driving_options_fill(test_simd.torque_curve, 32, sizeof(test_simd.torque_curve));
	test_car.car_transmission = TRANSMISSION_MANUAL;
	test_car.car_current_gear = 1;
	test_car.car_currpm = 1000;
	test_car.car_gearratio = 4096;
	test_car.car_gearratioshr8 = 16;
	test_car.car_sumSurfRearWheels = 2;
	test_car.car_sumSurfAllWheels = 4;
	test_car.car_rev_speed = test_car.car_actual_speed = 59000;
	drag[59000 >> 10] = 544;
	framespersec = GAME_FRAME_RATE_NORMAL;
	update_car_speed(INPUT_ACCELERATE_FLAG, PLAYER_CAR_INDEX, &test_car, &test_simd);
	CHECK_OPTION(test_car.car_actual_speed == (opt_in ? 58573U : 60757U),
				 "power gear switch changes force calculation");

	void far *car_resource = file_load_resfile("carterr");
	_memcpy(gameconfig.game_playercarid, "TERR", 4);
	setup_aero_trackdata(car_resource, PLAYER_CAR_INDEX);
	CHECK_OPTION(car_surface_adhesion[PLAYER_CAR_INDEX] == 1,
				 "fand resource automatically enables player adhesion");
	CHECK_OPTION(simd_player.num_gears == 1, "Tesla single forward gear resource");
	_memcpy(gameconfig.game_opponentcarid, "TERR", 4);
	setup_aero_trackdata(car_resource, OPPONENT_CAR_INDEX);
	CHECK_OPTION(car_surface_adhesion[OPPONENT_CAR_INDEX] == 1,
				 "fand resource automatically enables opponent adhesion");
	mmgr_release(car_resource);

	/* Selecting a stock car resets each slot independently. */
	car_resource = file_load_resfile("carvett");
	_memcpy(gameconfig.game_opponentcarid, "VETT", 4);
	setup_aero_trackdata(car_resource, OPPONENT_CAR_INDEX);
	CHECK_OPTION(car_surface_adhesion[OPPONENT_CAR_INDEX] == 0 &&
					 car_surface_adhesion[PLAYER_CAR_INDEX] == 1,
				 "stock opponent selection clears only opponent adhesion");
	_memcpy(gameconfig.game_playercarid, "VETT", 4);
	setup_aero_trackdata(car_resource, PLAYER_CAR_INDEX);
	CHECK_OPTION(car_surface_adhesion[PLAYER_CAR_INDEX] == 0,
				 "stock player selection clears player adhesion");
	mmgr_release(car_resource);
	car_resource = file_load_resfile("carterr");
	_memcpy(gameconfig.game_playercarid, "TERR", 4);
	setup_aero_trackdata(car_resource, PLAYER_CAR_INDEX);
	mmgr_release(car_resource);

	/* A stopped tire stays on a ceiling or a vertical wall for 200 ticks at both
	 * supported simulation rates, independently of the other driving switches. */
	driving_options_fill(&test_car, 0, sizeof(test_car));
	state.game_inputmode = GAME_INPUT_MODE_ACTIVE;
	planindex = 4;
	current_planptr = &planptr[planindex];
	elem_xCenter = elem_zCenter = terrainHeight = 0;
	current_surf_type = CAR_SURFACE_PAVED;
	track_wall_collision_enabled = 0;
	for (legacy_s16 orientation = 0; orientation < 2; orientation++) {
		struct PLANE plane = {0};
		mat_rot_z(&plane.plane_rotation, orientation ? ANGLE_QUARTER_TURN : ANGLE_HALF_TURN);
		car_to_world_rotation = plane.plane_rotation;
		struct VECTOR up = {0, TRIG_FIXED_ONE, 0};
		mat_mul_vector(&up, &car_to_world_rotation, &plane.plane_normal);
		plane.plane_origin.y = 1000;
		*current_planptr = plane;
		for (legacy_s16 rate = 0; rate < 2; rate++) {
			framespersec = rate ? GAME_FRAME_RATE_LOW : GAME_FRAME_RATE_NORMAL;
			struct PLAYER_WHEEL_MOTION motion = {0};
			motion.surface_adhesion = car_surface_adhesion[PLAYER_CAR_INDEX];
			for (legacy_s16 wheel = 0; wheel < 4; wheel++) {
				motion.current[wheel].ly = 1000L * 64;
				for (legacy_s16 tick = 0; tick < 200; tick++) {
					measure_wheel_plane_distance(&motion.current[wheel]);
					motion.previous[wheel] = motion.current[wheel];
					test_car.car_wheel_vertical_speed[wheel] = 10;
					CHECK_OPTION(
						resolve_wheel_plane_contact(&test_car, &motion, wheel, PLAYER_CAR_INDEX),
						"ceiling and wall contact");
					CHECK_OPTION(test_car.car_wheel_vertical_speed[wheel] == 0,
								 "ceiling and wall contact cancels falling velocity");
					CHECK_OPTION(motion.current[wheel].ly == 1000L * 64 &&
									 motion.current[wheel].lx == 0 && motion.current[wheel].lz == 0,
								 "stopped wheel position remains stable");
				}
			}
			/* An unflagged car keeps the original inverted gravity. */
			if (!orientation) {
				motion.surface_adhesion = 0;
				motion.current[0].ly = 999L * 64;
				measure_wheel_plane_distance(&motion.current[0]);
				move_airborne_wheel(&test_car, &motion, 0);
				CHECK_OPTION(motion.current[0].ly < 999L * 64, "default inverted gravity");
			}
		}
	}
	return 0;
}

legacy_s16 run_main_menu_loop(legacy_s16 argc, legacy_s8 *argv[])
{
	legacy_s16 opt_in = argc > 1 && strcmp(argv[1], "optin") == 0;
	init_full_game(argc, argv);
	legacy_s16 result = driving_options_check(opt_in);
	if (result == 0) {
		legacy_s16 fixed = argc > 1 && strcmp(argv[1], "dwsfixed") == 0;
		result = driving_options_check_dualway(fixed);
	}
	shutdown_dos_game();
	const legacy_s8 *message = result ? driving_options_failure : "DOS driving options passed";
	dos_write_stdout(message, strlen(message));
	dos_write_stdout("\r\n", 2);
	return result;
}
