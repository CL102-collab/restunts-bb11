#ifdef RESTUNTS_AMIGA_TRACE
#include <stdio.h>
#define AMIGA_RACE_TRACE(stage) do { puts("Amiga race resources: " stage); fflush(stdout); } while (0)
#else
#define AMIGA_RACE_TRACE(stage) ((void)0)
#endif
#include "dashboard.h"
#include "fileio.h"
#include "memmgr.h"
#include "race_resources.h"
#include "ghost.h"
#include "race_resources_internal.h"
#include "replay_viewer.h"
#include "shape2d.h"
#include "shape3d.h"
#include "ui_dialog.h"
#include "ui_text.h"
#include "track_collision.h"
#include "camera.h"
#include "car_model.h"
#include "skybox.h"
#include "race_graphics.h"
#include "car_audio.h"
#include "audio_control.h"
#include "car_resources.h"
#include "externs.h"
#include "video_pages.h"
#ifdef RESTUNTS_AMIGA
#include "shape2d_internal.h"
#endif
#ifdef RESTUNTS_SDL3
#include "skybox_hires.h"
#endif

#define SKYBOX_RESOURCE_COUNT 5
#define SKYBOX_RESOURCE_NAME_BYTES 9
#define SKYBOX_IMAGE_COUNT 4
#define SKYBOX_NO_IMAGES_FLAG 8U
#define SDGAME2_EFFECT_SHAPE_COUNT 3
#define TRACK_SKYBOX_ELEMENT_INDEX 900U
#define RENDER_WINDOW_WIDTH 320U
#define RENDER_WINDOW_HEIGHT 200U
#define RENDER_WINDOW_LEGACY_ARGUMENT 15U
#define RENDER_WINDOW_PIXEL_BYTES 64000U

enum SKYBOX_MATERIAL_INDEX {
	SKYBOX_GROUND_MATERIAL_INDEX = 16,
	SKYBOX_SKY_MATERIAL_INDEX = 17,
	SKYBOX_WATER_MATERIAL_INDEX = 100
};

static legacy_s8 skybox_resource_names[SKYBOX_RESOURCE_COUNT][SKYBOX_RESOURCE_NAME_BYTES] = {
	"desert", "tropical", "alpine", "city", "country"};

void unload_skybox(void)
{
#ifdef RESTUNTS_SDL3
	skybox_hires_unload();
#endif
	if (skybox_resources_loaded != 0) {
		mmgr_free(skybox_res_ofs);
	}
	skybox_resources_loaded = 0;
}

void free_sdgame2(void)
{
	mmgr_free(sdgame2ptr);
}

void load_sdgame2_shapes(void)
{
	sdgame2ptr = file_load_resource(FILE_RESOURCE_SHAPE2D_ALTERNATE, "sdgame2");
	locate_many_resources(sdgame2ptr, "ex01ex02ex03leftrigh", (legacy_s8 far **)sdgame2shapes);
	for (legacy_s16 i = 0; i < SDGAME2_EFFECT_SHAPE_COUNT; i++) {
		sdgame2_widths[i] = shape2d_get_width((struct SHAPE2D far *)sdgame2shapes[i]);
	}
}

void load_skybox(legacy_s8 skybox_index)
{
	if (((legacy_u8)skybox_index & SKYBOX_NO_IMAGES_FLAG) == 0) {
		if (skybox_resources_loaded != 0 &&
			(legacy_u8)skybox_index == (legacy_u8)loaded_skybox_index) {
			return;
		}

		unload_skybox();
		loaded_skybox_index = skybox_index;
		skybox_resources_loaded = 1;
		skybox_res_ofs = file_load_shape2d_fatal(skybox_resource_names[(legacy_s8)skybox_index]);
		locate_many_resources(skybox_res_ofs, "scensce2sce3sce4", (legacy_s8 far **)skyboxes);

		for (legacy_u16 image_index = 0; image_index < SKYBOX_IMAGE_COUNT; image_index++) {
			skybox.heights[image_index] =
				shape2d_get_height((struct SHAPE2D far *)skyboxes[image_index]);
		}
		legacy_u16 minimum = skybox.heights[0];
		legacy_u16 maximum = skybox.heights[0];
		for (legacy_u16 image_index = 1; image_index < SKYBOX_IMAGE_COUNT; image_index++) {
			if (minimum > skybox.heights[image_index]) {
				minimum = skybox.heights[image_index];
			}
			if (maximum < skybox.heights[image_index]) {
				maximum = skybox.heights[image_index];
			}
		}
		skybox.minimum_height = minimum;
		skybox.maximum_height = maximum;
	}

	skybox.sky_color = material_clrlist_ptr[SKYBOX_SKY_MATERIAL_INDEX];
	skybox.ground_color = material_clrlist_ptr[SKYBOX_GROUND_MATERIAL_INDEX];
	skybox.water_color = material_clrlist_ptr[SKYBOX_WATER_MATERIAL_INDEX];
	meter_needle_color = dialog_fnt_colour;
}

static void setup_car_engine_resources(void)
{
	AMIGA_RACE_TRACE("engine disk check");
	ensure_file_exists(3);
	AMIGA_RACE_TRACE("engine voice file");
	eng1ptr = file_load_resource(FILE_RESOURCE_VOICE, "eng1");
	AMIGA_RACE_TRACE("engine sound file");
	engptr = file_load_resource(FILE_RESOURCE_SOUND_EFFECTS, "eng");
	audio_add_driver_timer();
	AMIGA_RACE_TRACE("engine definition mapping");
	audio_player_engine_channel =
		audio_init_engine(PLAYER_ENGINE_LEGACY_TYPE, &player_engine_definition, eng1ptr, engptr);

	audio_car_state_ready = 0;
	audio_player_car_flags = 0;
	audio_opponent_car_flags = 0;
	if (gameconfig.game_opponenttype != 0) {
		audio_opponent_engine_channel = audio_init_engine(
			OPPONENT_ENGINE_LEGACY_TYPE, &opponent_engine_definition, eng1ptr, engptr);
	}

	audio_car_state_read_index = 0;
	audio_car_state_write_index = 0;
	audio_car_state_interval = 0;
}

static void setup_car_physics_resources(void)
{
	car_resource_name[3] = gameconfig.game_playercarid[0];
	car_resource_name[4] = gameconfig.game_playercarid[1];
	car_resource_name[5] = gameconfig.game_playercarid[2];
	car_resource_name[6] = gameconfig.game_playercarid[3];
	void far *carresptr = file_load_resfile(car_resource_name);
	setup_aero_trackdata(carresptr, 0);
	unload_resource(carresptr);

	if (gameconfig.game_opponenttype != 0) {
		car_resource_name[3] = gameconfig.game_opponentcarid[0];
		car_resource_name[4] = gameconfig.game_opponentcarid[1];
		car_resource_name[5] = gameconfig.game_opponentcarid[2];
		car_resource_name[6] = gameconfig.game_opponentcarid[3];
		carresptr = file_load_resfile(car_resource_name);
		setup_aero_trackdata(carresptr, 1);
		unload_resource(carresptr);

		ensure_file_exists(4);
		load_opponent_data();
	}
}

void ghost_load_simulation_resources(void)
{
	setup_legacy_penalty_route_word();
	shape3d_load_car_shapes(gameconfig.game_playercarid, gameconfig.game_opponentcarid);
	setup_car_physics_resources();
	load_track_collision_resources();
}

void ghost_free_simulation_resources(void)
{
	unload_resource(gameresptr);
	shape3d_free_car_shapes();
}

static legacy_s16 setup_player_cars_impl(legacy_s16 load_dashboard_shapes)
{
	AMIGA_RACE_TRACE("legacy resource layout");
	setup_legacy_penalty_route_word();
	render_window_sprite = 0;
	/* Dump tools retain their linear buffer and original resource layout. */
	if (load_dashboard_shapes != 0) {
		video_pages_begin_race();
	}
	AMIGA_RACE_TRACE("car disk check");
	ensure_file_exists(2);
	AMIGA_RACE_TRACE("car 3D geometry");
	shape3d_load_car_shapes(gameconfig.game_playercarid, ghost_is_active()
															 ? (legacy_s8 *)ghost_car_id()
															 : gameconfig.game_opponentcarid);
	AMIGA_RACE_TRACE("car physics");
	setup_car_physics_resources();

	setup_car_engine_resources();
	AMIGA_RACE_TRACE("LED font");
	fontledresptr = file_load_resource(FILE_RESOURCE_BINARY_FATAL, "fontled.fnt");
	slow_video_mgmt_copy = slow_video_mgmt;
	init_rect_arrays();
	/* Replay dump tools do not render the dashboard. Keep the
	 * car 3D container in its original arena position because later legacy
	 * state still observes that memory layout, but avoid the much larger 2D
	 * dashboard allocation that memory-heavy custom cars cannot afford. */
	if (idle_expired == 0 && load_dashboard_shapes) {
		AMIGA_RACE_TRACE("dashboard");
		setup_car_shapes(DASHBOARD_OPERATION_LOAD);
	}

	if (idle_expired == 0) {
		AMIGA_RACE_TRACE("race overlays");
		sdgameresptr = file_load_resource(FILE_RESOURCE_SHAPE2D_COLLECTION, "sdgame");
		loop_game(REPLAY_LOOP_LOAD_RESOURCES, REPLAY_LOOP_UNUSED_ARGUMENT,
				  REPLAY_LOOP_UNUSED_ARGUMENT);
	}

	AMIGA_RACE_TRACE("track collision");
	load_track_collision_resources();
	AMIGA_RACE_TRACE("race effects");
	load_sdgame2_shapes();
	AMIGA_RACE_TRACE("skybox");
	load_skybox(track_element_map[TRACK_SKYBOX_ELEMENT_INDEX]);
	AMIGA_RACE_TRACE("track 3D geometry");
	if (shape3d_load_all() != 0) {
		return 1;
	}

#ifdef RESTUNTS_AMIGA
	if (load_dashboard_shapes != 0) {
		/* The native planar pages display the previous frame while this one
		 * indexed surface receives both world geometry and dashboard pixels. */
		render_window_sprite = &screen_sprite;
	} else
#endif
	if (video_uses_page_flipping == 0) {
		AMIGA_RACE_TRACE("render framebuffer");
		legacy_u32 window_pixel_bytes = LEGACY_U16_DIV_OR_ZERO(
			RENDER_WINDOW_PIXEL_BYTES,
			LEGACY_U16_WRAP_MUL(video_shape_width_scale, video_buffer_height_divisor));
		if (mmgr_get_res_ofs_diff_scaled() <= window_pixel_bytes) {
			return 1;
		}
		render_window_sprite = sprite_make_wnd(RENDER_WINDOW_WIDTH, RENDER_WINDOW_HEIGHT,
											   RENDER_WINDOW_LEGACY_ARGUMENT);
	}

	followOpponentFlag = 0;
	is_in_replay_copy = -1;
	return 0;
}

legacy_s16 setup_player_cars(void)
{
	return setup_player_cars_impl(1);
}

legacy_s16 setup_player_cars_without_dashboard(void)
{
	return setup_player_cars_impl(0);
}

void free_player_cars(void)
{
	if (video_uses_page_flipping == 0) {
		if (render_window_sprite != 0
#ifdef RESTUNTS_AMIGA
			&& render_window_sprite != &screen_sprite
#endif
		) {
			sprite_free_wnd(render_window_sprite);
		}
	}
	shape3d_free_all();
	unload_skybox();
	free_sdgame2();
	unload_resource(gameresptr);
	if (idle_expired == 0) {
		mmgr_free(sdgameresptr);
		setup_car_shapes(DASHBOARD_OPERATION_UNLOAD);
	}

	mmgr_free(fontledresptr);
	audio_remove_driver_timer();
	mmgr_free(engptr);
	mmgr_free(eng1ptr);
	shape3d_free_car_shapes();
	video_pages_end_race();
}
