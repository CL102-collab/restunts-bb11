/* Reuse the live renderer/resource fixture, including its state/RNG checks.
 * This test compares visible geometry, rather than storing a renderer golden. */
#define stuntsmain legacy_render_replay_fixture_main
#include "test-render-replay.c"
#undef stuntsmain

#include "../c/cstrk.h"
#include "../c/extended_track.h"
#include "../c/skybox.h"
#include "../c/race_graphics.h"

static unsigned terrain_level(unsigned column)
{
	return column <= 38U ? 0U : column >= 42U ? 4U : column - 38U;
}

static void add_render_piece(struct CSTRK_TRACK *track, unsigned id, unsigned x, unsigned y,
							 unsigned width, unsigned height, unsigned mask, unsigned surface)
{
	struct CSTRK_OBJECT object = {0};
	object.legacy_id = (uint16_t)id;
	object.x = (uint8_t)x;
	object.y = (uint8_t)y;
	object.width = (uint8_t)width;
	object.height = (uint8_t)height;
	object.occupied_mask = (uint8_t)mask;
	object.surface = (uint8_t)surface;
	object.base_height = (uint8_t)terrain_level(x);
	object.slope_x = (int8_t)(terrain_level(x + 1U) - terrain_level(x));
	char error[192];
	if (!cstrk_add_object(track, &object, error, sizeof(error))) {
		fprintf(stderr, "Render fixture piece %u: %s\n", id, error);
		assert(0);
	}
}

static void make_render_track(struct CSTRK_TRACK *track)
{
	char error[192];
	cstrk_init(track);
	assert(cstrk_create(track, 90U, 90U, error, sizeof(error)));
	for (unsigned y = 0; y <= track->height; y++) {
		for (unsigned x = 0; x <= track->width; x++) {
			track->corner_heights[y * (track->width + 1U) + x] = (uint8_t)terrain_level(x);
		}
	}
	for (unsigned y = 36; y < 49; y++) {
		for (unsigned x = 36; x < 48; x++) {
			track->cells[y * track->width + x].surface = (uint8_t)((x / 3U + y / 3U) % 4U);
		}
	}
	track->cells[43U * track->width + 41U].half_water_corner = CSTRK_WATER_NORTHWEST;
	/* The slope runs through all four levels; these rigid stunt meshes need
	 * the complete two-tile approach on both the ground and collision paths. */
	add_render_piece(track, 66U, 39U, 40U, 1U, 1U, 1U, CSTRK_ROAD_ICE);
	add_render_piece(track, 64U, 40U, 42U, 1U, 2U, 3U, CSTRK_ROAD_PAVED);
	add_render_piece(track, 53U, 42U, 40U, 2U, 2U, 11U, CSTRK_ROAD_DIRT);
	add_render_piece(track, 151U, 42U, 41U, 1U, 1U, 1U, CSTRK_ROAD_PAVED);
	add_render_piece(track, 60U, 45U, 40U, 2U, 2U, 6U, CSTRK_ROAD_ICE);
	add_render_piece(track, 151U, 45U, 40U, 1U, 1U, 1U, CSTRK_ROAD_PAVED);
	/* Draw at the far edge as well: all these coordinates exceed the signed
	 * word world range, and the last two pieces touch the 90th map column. */
	add_render_piece(track, 11U, 86U, 44U, 2U, 2U, 11U, CSTRK_ROAD_DIRT);
	add_render_piece(track, 151U, 86U, 45U, 1U, 1U, 1U, CSTRK_ROAD_PAVED);
	add_render_piece(track, 106U, 87U, 40U, 2U, 2U, 11U, CSTRK_ROAD_ICE);
	add_render_piece(track, 151U, 87U, 41U, 1U, 1U, 1U, CSTRK_ROAD_PAVED);
	add_render_piece(track, 67U, 89U, 42U, 1U, 1U, 1U, CSTRK_ROAD_DIRT);
	add_render_piece(track, 64U, 89U, 44U, 1U, 2U, 3U, CSTRK_ROAD_PAVED);
	assert(cstrk_validate(track, error, sizeof(error)));
}

static void activate_render_track(const struct CSTRK_TRACK *track, int with_objects)
{
	struct CSTRK_TRACK view = *track;
	if (!with_objects) {
		view.object_count = 0;
	}
	char error[192];
	if (!extended_track_activate(&view, error, sizeof(error))) {
		fprintf(stderr, "Render fixture activation: %s\n", error);
		assert(0);
	}
	assert(extended_track_width() == 90 && extended_track_height() == 90);
}

static void position_render_target(unsigned column, unsigned north_row)
{
	struct CARSTATE *car = &state.playerstate;
	car->car_position.lx = (legacy_s32)(column * 1024U + 512U) * 64L;
	car->car_position.lz = (legacy_s32)((90U - north_row - 1U) * 1024U + 512U) * 64L;
	car->car_position.ly = (legacy_s32)(terrain_level(column) * 450U + 16U) * 64L;
	car->car_previous_position = car->car_position;
	car->car_rotate = (struct VECTOR){0, 0, 0};
	car->car_crashBmpFlag = CRASH_EVENT_NONE;
	state.game_particles_active = 0;
	assert(car->car_position.lx > 32767L * 64L);
	custom_camera.distance = 2400;
	custom_camera.elevation_angle = 112;
	custom_camera.azimuth_angle = 416;
}

static legacy_u32 *capture_complete_render(size_t *pixel_count)
{
	full_redraw_frames_remaining = 1;
	render_and_check(&state);
	legacy_u8 *legacy = dos_memory_make_pointer(0xA000U, 0);
	legacy_s32 width, height;
	assert(hires_framebuffer(legacy, &width, &height) != NULL);
	assert(width >= 320 && height >= 200);
	*pixel_count = (size_t)width * height;
	legacy_u32 *pixels = malloc(*pixel_count * sizeof(*pixels));
	assert(pixels != NULL);
	/* Preserve distinct indexed colors while including every true-color
	 * companion sample. The indexed-only capture omits enhanced overlays. */
	legacy_u32 palette[256];
	for (unsigned index = 0; index < 256; index++) {
		palette[index] = UINT32_C(0xff000000) | index * UINT32_C(0x010101);
	}
	hires_copy_framebuffer_argb(legacy, palette, pixels, width * sizeof(*pixels));
	return pixels;
}

static size_t check_rendered_objects(const struct CSTRK_TRACK *track, unsigned column, unsigned row,
									 unsigned azimuth)
{
	position_render_target(column, row);
	custom_camera.azimuth_angle = (legacy_s16)azimuth;
	activate_render_track(track, 1);
	size_t object_size, empty_size;
	legacy_u32 *objects = capture_complete_render(&object_size);
	activate_render_track(track, 0);
	legacy_u32 *empty = capture_complete_render(&empty_size);
	assert(object_size == empty_size && object_size >= 320U * 200U);
	size_t changed = 0;
	legacy_u32 colors[4] = {0};
	unsigned color_count = 0;
	for (size_t pixel = 0; pixel < object_size; pixel++) {
		changed += objects[pixel] != empty[pixel];
		unsigned color = 0;
		while (color < color_count && colors[color] != objects[pixel]) {
			color++;
		}
		if (color == color_count && color_count < 4U) {
			colors[color_count++] = objects[pixel];
		}
	}
	assert(color_count >= 4U);
	free(objects);
	free(empty);
	return changed;
}

static unsigned check_rendered_surfaces(const struct CSTRK_TRACK *track)
{
	static const unsigned ids[] = {117U, 121U, 85U, 155U};
	unsigned frames = 0;
	for (unsigned index = 0; index < sizeof(ids) / sizeof(ids[0]); index++) {
		struct CSTRK_OBJECT object = {0};
		object.legacy_id = (uint16_t)ids[index];
		object.x = 44;
		object.y = 40;
		object.width = index < 2U ? 2U : 1U;
		object.height = index < 3U ? 2U : 1U;
		object.occupied_mask = (uint8_t)((1U << (object.width * object.height)) - 1U);
		object.base_height = (uint8_t)terrain_level(object.x);
		struct CSTRK_TRACK isolated = *track;
		isolated.object_count = 1;
		isolated.objects = &object;
		position_render_target(44U, 41U);
		/* Look down onto the driving faces, rather than their unchanged backsides. */
		custom_camera.distance = 4600;
		custom_camera.elevation_angle = 192;
		custom_camera.azimuth_angle = 0;
		legacy_u32 *pixels[3];
		size_t sizes[3];
		for (unsigned surface = 0; surface < 3U; surface++) {
			object.surface = (uint8_t)surface;
			activate_render_track(&isolated, 1);
			pixels[surface] = capture_complete_render(&sizes[surface]);
			frames++;
		}
		activate_render_track(&isolated, 0);
		size_t empty_size;
		legacy_u32 *empty = capture_complete_render(&empty_size);
		assert(empty_size == sizes[0]);
		size_t visible = 0;
		for (size_t pixel = 0; pixel < sizes[0]; pixel++) {
			visible += pixels[0][pixel] != empty[pixel];
		}
		free(empty);
		frames++;
		if (visible <= 16U) {
			fprintf(stderr, "Surface fixture %u shows only %zu object pixels\n", ids[index],
					visible);
		}
		assert(visible > 16U);
		for (unsigned surface = 1; surface < 3U; surface++) {
			assert(sizes[surface] == sizes[0]);
			size_t changed = 0;
			for (size_t pixel = 0; pixel < sizes[0]; pixel++) {
				changed += pixels[0][pixel] != pixels[surface][pixel];
			}
			if (index < 3U) {
				/* Actual corkscrew artwork must show the selected road material. */
				if (changed <= 16U) {
					fprintf(stderr, "Corkscrew %u surface %u changed only %zu pixels\n", ids[index],
							surface, changed);
				}
				assert(changed > 16U);
			} else {
				/* Even imported nonzero surface fields must leave scenery unchanged. */
				assert(changed == 0U);
			}
		}
		if (index < 3U) {
			assert(memcmp(pixels[1], pixels[2], sizes[0] * sizeof(*pixels[0])) != 0);
		}
		for (unsigned surface = 0; surface < 3U; surface++) {
			free(pixels[surface]);
		}
	}
	return frames;
}

#define SHORE_RENDER_COLUMN 44U
#define SHORE_RENDER_ROW 42U
#define SHORE_RENDER_CAMERA_DISTANCE 2400
#define SHORE_RENDER_CAMERA_ELEVATION 192
#define SHORE_RENDER_MIN_CHANGED_PIXELS 16U
#define SHORE_RENDER_SECONDARY_COUNT 3U

static legacy_u16 check_rendered_shore_materials(struct CSTRK_TRACK *track)
{
	struct CSTRK_CELL *cell = &track->cells[SHORE_RENDER_ROW * track->width + SHORE_RENDER_COLUMN];
	struct CSTRK_CELL saved_cell = *cell;
	position_render_target(SHORE_RENDER_COLUMN, SHORE_RENDER_ROW);
	custom_camera.distance = SHORE_RENDER_CAMERA_DISTANCE;
	custom_camera.elevation_angle = SHORE_RENDER_CAMERA_ELEVATION;
	custom_camera.azimuth_angle = 0;
	legacy_u16 frames = 0;
	for (legacy_u8 primary = CSTRK_TERRAIN_GRASS; primary <= CSTRK_TERRAIN_WATER; primary++) {
		cell->surface = primary;
		for (legacy_u8 corner = CSTRK_WATER_NORTHWEST; corner <= CSTRK_WATER_SOUTHWEST; corner++) {
			cell->half_water_corner = corner;
			legacy_u32 *pixels[SHORE_RENDER_SECONDARY_COUNT];
			size_t sizes[SHORE_RENDER_SECONDARY_COUNT];
			legacy_u8 count = 0;
			for (legacy_u8 secondary = CSTRK_TERRAIN_GRASS; secondary <= CSTRK_TERRAIN_WATER;
				 secondary++) {
				if (primary == secondary) {
					continue;
				}
				cell->corner_surface = secondary;
				activate_render_track(track, 0);
				pixels[count] = capture_complete_render(&sizes[count]);
				count++;
				frames++;
			}
			assert(count == SHORE_RENDER_SECONDARY_COUNT);
			for (legacy_u8 variant = 1; variant < count; variant++) {
				assert(sizes[variant] == sizes[0]);
				size_t changed = 0;
				for (size_t pixel = 0; pixel < sizes[0]; pixel++) {
					changed += pixels[0][pixel] != pixels[variant][pixel];
				}
				assert(changed > SHORE_RENDER_MIN_CHANGED_PIXELS);
			}
			for (legacy_u8 variant = 0; variant < count; variant++) {
				free(pixels[variant]);
			}
		}
	}
	*cell = saved_cell;
	return frames;
}

legacy_s16 stuntsmain(legacy_s16 argc, legacy_s8 *argv[])
{
	assert(argc == 1);
	init_main(argc, argv);
	init_div0();
	init_row_tables();
	mainresptr = file_load_resfile("main");
	fontdefptr = file_load_resource(0, "fontdef.fnt");
	fontnptr = file_load_resource(0, "fontn.fnt");
	font_set_fontdef();
	init_polyinfo();
	init_trackdata();
	reset_race_loop_state();
	init_kevinrandom("kevin");
	initialize_replay("DEFAULT");
	gameconfig.game_opponenttype = 0;
	struct CSTRK_TRACK track;
	make_render_track(&track);
	track.horizon = CSTRK_HORIZON_COUNTRY;
	activate_render_track(&track, 1);
	/* The inherited fixture loads replay overlays but omits dashboard sprites.
	 * Release the overlays explicitly and skip the unavailable dashboard cleanup. */
	mmgr_free(sdgameresptr);
	legacy_s8 saved_idle_expired = idle_expired;
	idle_expired = 1;
	free_player_cars();
	idle_expired = saved_idle_expired;
	assert(setup_player_cars_without_dashboard() == 0);
	assert(skybox_resources_loaded != 0 && loaded_skybox_index == CSTRK_HORIZON_COUNTRY);
	visual_clock_ticks = dos_timer_get_realtime_counter();
	visual_clock_frozen = 1;
	unsigned frames = 0;
	for (unsigned enhanced = 0; enhanced < 2U; enhanced++) {
		if ((unsigned)(supersight_enabled != 0) != enhanced) {
			assert(handle_ingame_kb_shortcuts(KEY_F12));
		}
		cameramode = CAMERA_MODE_CUSTOM;
		size_t slope_pixels = check_rendered_objects(&track, 40U, 41U, 416U);
		size_t edge_pixels = check_rendered_objects(&track, 88U, 43U, 768U);
		printf("Extended render %s: %zu slope pixels, %zu edge pixels from track objects\n",
			   enhanced ? "enhanced" : "original", slope_pixels, edge_pixels);
		assert(slope_pixels > 16U && edge_pixels > 16U);
		frames += 4U;
		frames += check_rendered_surfaces(&track);
		frames += check_rendered_shore_materials(&track);
	}
	extended_track_unload();
	cstrk_free(&track);
	printf("Extended 90x90 four-level terrain and rigid object meshes rendered in %u frames.\n",
		   frames);
	call_exitlist();
	return 0;
}
