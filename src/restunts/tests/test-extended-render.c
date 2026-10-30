/* Reuse the live renderer/resource fixture, including its state/RNG checks.
 * This test compares visible geometry, rather than storing a renderer golden. */
#define stuntsmain legacy_render_replay_fixture_main
#include "test-render-replay.c"
#undef stuntsmain

#include "../c/cstrk.h"
#include "../c/extended_track.h"
#include "../c/extended_geometry.h"
#include "../c/skybox.h"
#include "../c/race_graphics.h"
#include "../c/trackdata_layout.h"
#include "../c/shape3d_internal.h"

#define STUNT_RENDER_PART_COUNT 2U
#define STUNT_RENDER_DETAIL_COUNT 2U
#define STUNT_RENDER_AUTHORED_OVERSCAN 2

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
	track->cells[43U * track->width + 41U].corner_surface = CSTRK_TERRAIN_GRASS;
	/* Each one-level incline uses the original full-width hill road or
	 * ramp-to-span substitution. Ordinary stunts remain on flat plateaus. */
	add_render_piece(track, 5U, 39U, 40U, 1U, 1U, 1U, CSTRK_ROAD_ICE);
	add_render_piece(track, 5U, 40U, 42U, 1U, 1U, 1U, CSTRK_ROAD_PAVED);
	add_render_piece(track, 37U, 41U, 44U, 1U, 1U, 1U, CSTRK_ROAD_DIRT);
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

#define RENDER_CAPTURE_OPAQUE_ALPHA UINT32_C(0xff000000)
#define RENDER_CAPTURE_INDEX_RGB_STEP UINT32_C(0x010101)
#define RENDER_CAPTURE_VIDEO_SEGMENT 0xA000U
#define RENDER_CAPTURE_SKY_MARKER LEGACY_U8_MAX
#define RENDER_CAPTURE_SKY_COLOR UINT32_C(0x01000000)

static legacy_u8 render_capture_mark_sky;

static legacy_u32 *capture_complete_render(size_t *pixel_count)
{
	full_redraw_frames_remaining = 1;
	render_and_check(&state);
	legacy_u8 *legacy = dos_memory_make_pointer(RENDER_CAPTURE_VIDEO_SEGMENT, 0);
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
		palette[index] = RENDER_CAPTURE_OPAQUE_ALPHA | index * RENDER_CAPTURE_INDEX_RGB_STEP;
	}
	if (render_capture_mark_sky) {
		// Opaque enhanced highlights can share any RGB value. An alpha marker
		// keeps indexed background coverage distinct from those overlays.
		palette[RENDER_CAPTURE_SKY_MARKER] = RENDER_CAPTURE_SKY_COLOR;
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

#define BACKGROUND_RENDER_POSITION_SCALE 64L
#define BACKGROUND_RENDER_CAR_CLEARANCE 16L
#define BACKGROUND_RENDER_FULL_DETAIL 0
#define BACKGROUND_RENDER_CLOUDLESS_DETAIL 1
#define BACKGROUND_RENDER_ROAD_ID 4U
#define BACKGROUND_RENDER_ROAD_DISTANCE 3U
#define BACKGROUND_RENDER_ERROR_CAPACITY 192U

static legacy_u16 check_extended_background_altitude(void)
{
	static const struct VECTOR rotations[] = {
		{0, 0, 0},
		{ANGLE_QUARTER_TURN, -ANGLE_FULL_TURN / 64, ANGLE_FULL_TURN / 32},
		{ANGLE_THREE_QUARTER_TURN, ANGLE_FULL_TURN / 64, -ANGLE_FULL_TURN / 32}};
	struct GAMESTATE saved = state;
	legacy_s16 saved_camera = cameramode;
	legacy_s8 saved_detail = detail_level;
	struct CSTRK_TRACK track;
	legacy_char error[BACKGROUND_RENDER_ERROR_CAPACITY];
	cstrk_init(&track);
	assert(cstrk_create(&track, CSTRK_MAX_DIMENSION, CSTRK_MAX_DIMENSION, error, sizeof(error)));
	track.horizon = CSTRK_HORIZON_COUNTRY;
	assert(extended_track_activate(&track, error, sizeof(error)));
	cameramode = CAMERA_MODE_COCKPIT;
	struct CARSTATE *car = &state.playerstate;
	car->car_position.lx = (track.width / 2U * CSTRK_TILE_WORLD_SIZE + CSTRK_TILE_WORLD_SIZE / 2U) *
						   BACKGROUND_RENDER_POSITION_SCALE;
	car->car_position.lz = car->car_position.lx;
	car->car_crashBmpFlag = CRASH_EVENT_NONE;
	state.game_particles_active = 0;
	legacy_u16 frames = 0;
	for (size_t view = 0; view < sizeof(rotations) / sizeof(rotations[0]); view++) {
		car->car_rotate = rotations[view];
		car->car_position.ly = BACKGROUND_RENDER_CAR_CLEARANCE * BACKGROUND_RENDER_POSITION_SCALE;
		car->car_previous_position = car->car_position;
		detail_level = BACKGROUND_RENDER_FULL_DETAIL;
		size_t low_size, high_size, cloudless_size;
		legacy_u32 *low = capture_complete_render(&low_size);
		car->car_position.ly +=
			CSTRK_MAX_HEIGHT * CSTRK_LEVEL_WORLD_HEIGHT * BACKGROUND_RENDER_POSITION_SCALE;
		car->car_previous_position = car->car_position;
		legacy_u32 *high = capture_complete_render(&high_size);
		assert(high_size == low_size);
		/* The empty grass field leaves only the distant background visible.
		 * Changing eye altitude must move neither its horizon nor its clouds. */
		assert(memcmp(low, high, low_size * sizeof(*low)) == 0);
		detail_level = BACKGROUND_RENDER_CLOUDLESS_DETAIL;
		legacy_u32 *cloudless = capture_complete_render(&cloudless_size);
		assert(cloudless_size == high_size);
		/* Prove the equality above actually includes visible cloud geometry. */
		assert(memcmp(high, cloudless, high_size * sizeof(*high)) != 0);
		free(cloudless);
		free(high);
		free(low);
		frames += 3U;
	}
	struct CSTRK_OBJECT road = {0};
	road.legacy_id = BACKGROUND_RENDER_ROAD_ID;
	road.x = track.width / 2U;
	road.y = track.height / 2U - BACKGROUND_RENDER_ROAD_DISTANCE;
	road.width = road.height = road.occupied_mask = 1;
	assert(cstrk_add_object(&track, &road, error, sizeof(error)));
	assert(extended_track_activate(&track, error, sizeof(error)));
	car->car_rotate = rotations[0];
	detail_level = BACKGROUND_RENDER_FULL_DETAIL;
	car->car_position.ly = BACKGROUND_RENDER_CAR_CLEARANCE * BACKGROUND_RENDER_POSITION_SCALE;
	car->car_previous_position = car->car_position;
	size_t low_size, high_size;
	legacy_u32 *low = capture_complete_render(&low_size);
	car->car_position.ly +=
		CSTRK_MAX_HEIGHT * CSTRK_LEVEL_WORLD_HEIGHT * BACKGROUND_RENDER_POSITION_SCALE;
	car->car_previous_position = car->car_position;
	legacy_u32 *high = capture_complete_render(&high_size);
	assert(low_size == high_size);
	/* Only the background ignores altitude; the road still uses the real eye position. */
	assert(memcmp(low, high, low_size * sizeof(*low)) != 0);
	free(high);
	free(low);
	frames += 2U;
	state = saved;
	cameramode = saved_camera;
	detail_level = saved_detail;
	extended_track_unload();
	cstrk_free(&track);
	return frames;
}

#define SURFACE_RENDER_COLUMN 44U
#define SURFACE_RENDER_ROW 40U
#define SURFACE_RENDER_TARGET_ROW 41U
#define SURFACE_RENDER_CAMERA_DISTANCE 4600
#define SURFACE_RENDER_CAMERA_ELEVATION 192
#define SURFACE_RENDER_COUNT 3U
#define SURFACE_RENDER_MIN_CHANGED_PIXELS 16U

static legacy_u16 check_rendered_surfaces(const struct CSTRK_TRACK *track)
{
	static const struct {
		legacy_u16 id;
		legacy_u8 width, height;
	} pieces[] = {{117U, 2U, 2U}, {121U, 2U, 2U}, {85U, 1U, 2U}, {64U, 1U, 2U},
				  {70U, 1U, 1U},  {68U, 1U, 1U},  {83U, 1U, 1U}, {155U, 1U, 1U}};
	legacy_u16 frames = 0;
	for (legacy_u16 index = 0; index < sizeof(pieces) / sizeof(pieces[0]); index++) {
		struct CSTRK_OBJECT object = {0};
		object.legacy_id = pieces[index].id;
		object.x = SURFACE_RENDER_COLUMN;
		object.y = SURFACE_RENDER_ROW;
		object.width = pieces[index].width;
		object.height = pieces[index].height;
		object.occupied_mask = (legacy_u8)((1U << (object.width * object.height)) - 1U);
		object.base_height = (legacy_u8)terrain_level(object.x);
		struct CSTRK_TRACK isolated = *track;
		isolated.object_count = 1;
		isolated.objects = &object;
		position_render_target(SURFACE_RENDER_COLUMN, SURFACE_RENDER_TARGET_ROW);
		/* Look down onto the driving faces, rather than their unchanged backsides. */
		custom_camera.distance = SURFACE_RENDER_CAMERA_DISTANCE;
		custom_camera.elevation_angle = SURFACE_RENDER_CAMERA_ELEVATION;
		custom_camera.azimuth_angle = 0;
		legacy_u32 *pixels[SURFACE_RENDER_COUNT];
		size_t sizes[SURFACE_RENDER_COUNT];
		for (legacy_u8 surface = 0; surface < SURFACE_RENDER_COUNT; surface++) {
			object.surface = (legacy_u8)surface;
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
		if (visible <= SURFACE_RENDER_MIN_CHANGED_PIXELS) {
			fprintf(stderr, "Surface fixture %u shows only %zu object pixels\n", pieces[index].id,
					visible);
		}
		assert(visible > SURFACE_RENDER_MIN_CHANGED_PIXELS);
		for (legacy_u8 surface = 1; surface < SURFACE_RENDER_COUNT; surface++) {
			assert(sizes[surface] == sizes[0]);
			size_t changed = 0;
			for (size_t pixel = 0; pixel < sizes[0]; pixel++) {
				changed += pixels[0][pixel] != pixels[surface][pixel];
			}
			if (index + 1U < sizeof(pieces) / sizeof(pieces[0])) {
				/* Driving faces and overlays must show the selected road material. */
				if (changed <= SURFACE_RENDER_MIN_CHANGED_PIXELS) {
					fprintf(stderr, "Road piece %u surface %u changed only %zu pixels\n",
							pieces[index].id, surface, changed);
				}
				assert(changed > SURFACE_RENDER_MIN_CHANGED_PIXELS);
			} else {
				/* Even imported nonzero surface fields must leave scenery unchanged. */
				assert(changed == 0U);
			}
		}
		if (index + 1U < sizeof(pieces) / sizeof(pieces[0])) {
			assert(memcmp(pixels[1], pixels[2], sizes[0] * sizeof(*pixels[0])) != 0);
		}
		for (legacy_u8 surface = 0; surface < SURFACE_RENDER_COUNT; surface++) {
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

#define GRASS_RENDER_CAR_HEIGHT 16L
#define GRASS_RENDER_POSITION_SCALE 64L
#define GRASS_RENDER_VARIANT_COUNT 3U
#define GRASS_RENDER_ERROR_CAPACITY 192U

static legacy_u16 check_ground_grass_color(void)
{
	struct CSTRK_TRACK track;
	legacy_char error[GRASS_RENDER_ERROR_CAPACITY];
	cstrk_init(&track);
	assert(cstrk_create(&track, CSTRK_MAX_DIMENSION, CSTRK_MAX_DIMENSION, error, sizeof(error)));
	track.horizon = CSTRK_HORIZON_COUNTRY;
	for (size_t index = 0; index < (size_t)track.width * track.height; index++) {
		track.cells[index].surface = CSTRK_TERRAIN_WATER;
	}
	position_render_target(SHORE_RENDER_COLUMN, SHORE_RENDER_ROW);
	state.playerstate.car_position.ly = GRASS_RENDER_CAR_HEIGHT * GRASS_RENDER_POSITION_SCALE;
	state.playerstate.car_previous_position = state.playerstate.car_position;
	custom_camera.distance = SHORE_RENDER_CAMERA_DISTANCE;
	custom_camera.elevation_angle = SHORE_RENDER_CAMERA_ELEVATION;
	custom_camera.azimuth_angle = 0;
	activate_render_track(&track, 0);
	size_t water_size;
	legacy_u32 *water = capture_complete_render(&water_size);
	/* This is the original flat-ground fill loaded from the game resources,
	 * rather than a second copy of the extended renderer's material choice. */
	legacy_u32 original_grass = RENDER_CAPTURE_OPAQUE_ALPHA |
								(legacy_u32)skybox.ground_color * RENDER_CAPTURE_INDEX_RGB_STEP;
	struct CSTRK_CELL *cell = &track.cells[SHORE_RENDER_ROW * track.width + SHORE_RENDER_COLUMN];
	legacy_u16 frames = 1;
	for (legacy_u8 variant = 0; variant < GRASS_RENDER_VARIANT_COUNT; variant++) {
		cell->surface = variant == 0 ? CSTRK_TERRAIN_GRASS : CSTRK_TERRAIN_WATER;
		cell->half_water_corner = variant == 0 ? CSTRK_WATER_NONE : CSTRK_WATER_NORTHWEST;
		cell->corner_surface = CSTRK_TERRAIN_GRASS;
		if (variant == GRASS_RENDER_VARIANT_COUNT - 1U) {
			/* The northwest half remains at ground level even when the other
			 * triangle rises. Its color must depend on its own vertices. */
			size_t southeast =
				(SHORE_RENDER_ROW + 1U) * (track.width + 1U) + SHORE_RENDER_COLUMN + 1U;
			track.corner_heights[southeast] = 1;
		}
		activate_render_track(&track, 0);
		size_t grass_size;
		legacy_u32 *grass = capture_complete_render(&grass_size);
		assert(grass_size == water_size);
		size_t original_color_pixels = 0;
		for (size_t pixel = 0; pixel < grass_size; pixel++) {
			if (grass[pixel] != water[pixel] && grass[pixel] == original_grass) {
				original_color_pixels++;
			}
		}
		if (original_color_pixels <= SHORE_RENDER_MIN_CHANGED_PIXELS) {
			fprintf(stderr, "Ground grass variant %u shows only %zu pixels of original color %u\n",
					variant, original_color_pixels, (legacy_u16)skybox.ground_color);
		}
		assert(original_color_pixels > SHORE_RENDER_MIN_CHANGED_PIXELS);
		free(grass);
		frames++;
	}
	free(water);
	extended_track_unload();
	cstrk_free(&track);
	return frames;
}

#define CLASSIC_RENDER_COLUMN 15U
#define CLASSIC_RENDER_ROW 15U
#define CLASSIC_RENDER_CORNER_COUNT 4U
#define CLASSIC_RENDER_PROFILE_COUNT (1U << CLASSIC_RENDER_CORNER_COUNT)
#define CLASSIC_RENDER_INCLINE_VARIANTS 5U
#define CLASSIC_RENDER_RAMP_VARIANT 4U
#define CLASSIC_RENDER_ROAD_SURFACE_STEP 10U
#define CLASSIC_RENDER_SLOPE_MATERIAL_FIRST 102U
#define CLASSIC_RENDER_SLOPE_MATERIAL_LAST 105U
#define CLASSIC_RENDER_LOGICAL_WIDTH 320U
#define CLASSIC_RENDER_TRANSLATED_EDGE_PIXELS 2
#define CLASSIC_RENDER_TERRAIN_PAINTS 7U
#define CLASSIC_RENDER_SOLID_PAINTS 3U
#define CLASSIC_RENDER_PAVED_MATERIAL 19U
#define CLASSIC_RENDER_MARKING_MATERIAL 21U
#define CLASSIC_RENDER_SADDLE_NW_SE 5U
#define CLASSIC_RENDER_SADDLE_NE_SW 10U
#define CLASSIC_RENDER_CAMERA_ELEVATION 192
#define CLASSIC_RENDER_POSITION_SCALE 64L
#define CLASSIC_RENDER_CAR_CLEARANCE 16L

static void position_classic_render_target(legacy_u8 level)
{
	struct CARSTATE *car = &state.playerstate;
	car->car_position.lx =
		(CLASSIC_RENDER_COLUMN * CSTRK_TILE_WORLD_SIZE + CSTRK_TILE_WORLD_SIZE / 2U) *
		CLASSIC_RENDER_POSITION_SCALE;
	car->car_position.lz = ((TRACK_GRID_SIZE - 1U - CLASSIC_RENDER_ROW) * CSTRK_TILE_WORLD_SIZE +
							CSTRK_TILE_WORLD_SIZE / 2U) *
						   CLASSIC_RENDER_POSITION_SCALE;
	car->car_position.ly = (level * CSTRK_LEVEL_WORLD_HEIGHT + CLASSIC_RENDER_CAR_CLEARANCE) *
						   CLASSIC_RENDER_POSITION_SCALE;
	car->car_previous_position = car->car_position;
	car->car_rotate = (struct VECTOR){0, 0, 0};
	car->car_crashBmpFlag = CRASH_EVENT_NONE;
	state.game_particles_active = 0;
	custom_camera.distance = SHORE_RENDER_CAMERA_DISTANCE;
	custom_camera.elevation_angle = CLASSIC_RENDER_CAMERA_ELEVATION;
	custom_camera.azimuth_angle = 0;
}

static legacy_u16 check_classic_hill_rendering(void)
{
	extended_track_unload();
	legacy_u8 saved_elements[TRACKDATA_MAP_SIZE], saved_terrain[TRACKDATA_MAP_SIZE];
	memcpy(saved_elements, track_element_map, sizeof(saved_elements));
	memcpy(saved_terrain, track_terrain_map, sizeof(saved_terrain));
	legacy_u16 frames = 0;
	for (legacy_u8 mask = 0; mask < CLASSIC_RENDER_PROFILE_COUNT; mask++) {
		if (mask == CLASSIC_RENDER_SADDLE_NW_SE || mask == CLASSIC_RENDER_SADDLE_NE_SW) {
			continue;
		}
		struct CSTRK_TRACK track;
		legacy_char error[GRASS_RENDER_ERROR_CAPACITY];
		cstrk_init(&track);
		assert(cstrk_create(&track, TRACK_GRID_SIZE, TRACK_GRID_SIZE, error, sizeof(error)));
		track.horizon = CSTRK_HORIZON_COUNTRY;
		size_t corner = CLASSIC_RENDER_ROW * (track.width + 1U) + CLASSIC_RENDER_COLUMN;
		static const legacy_u8 dx[] = {0, 1, 1, 0}, dy[] = {0, 0, 1, 1};
		for (legacy_u8 index = 0; index < CLASSIC_RENDER_CORNER_COUNT; index++) {
			track.corner_heights[corner + dy[index] * (track.width + 1U) + dx[index]] =
				(mask >> index) & 1U;
		}
		memset(track_element_map, 0, sizeof(saved_elements));
		memset(track_terrain_map, 0, sizeof(saved_terrain));
		for (legacy_u16 y = 0; y < track.height; y++) {
			for (legacy_u16 x = 0; x < track.width; x++) {
				struct CSTRK_TERRAIN_PROFILE profile;
				assert(cstrk_terrain_profile(&track, x, y, &profile));
				track_terrain_map[terrainrows[y] + x] = profile.terrain_id;
			}
		}
		struct CSTRK_TERRAIN_PROFILE central;
		assert(cstrk_terrain_profile(&track, CLASSIC_RENDER_COLUMN, CLASSIC_RENDER_ROW, &central));
		legacy_u8 incline =
			central.terrain_id >= HILL_TERRAIN_FIRST && central.terrain_id < HILL_TERRAIN_END;
		for (legacy_u8 variant = 0; variant < (incline ? CLASSIC_RENDER_INCLINE_VARIANTS : 1U);
			 variant++) {
			track.object_count = 0;
			legacy_u8 original_id = 0;
			if (variant != 0) {
				static const legacy_u8 road_ids[] = {4, 5, 4, 5};
				static const legacy_u8 ramp_ids[] = {39, 36, 38, 37};
				legacy_u8 direction = central.terrain_id - HILL_TERRAIN_FIRST;
				struct CSTRK_OBJECT object = {0};
				object.legacy_id = variant == CLASSIC_RENDER_RAMP_VARIANT ? ramp_ids[direction]
																		  : road_ids[direction];
				object.surface =
					variant == CLASSIC_RENDER_RAMP_VARIANT ? CSTRK_ROAD_PAVED : variant - 1U;
				object.x = CLASSIC_RENDER_COLUMN;
				object.y = CLASSIC_RENDER_ROW;
				object.width = object.height = object.occupied_mask = 1;
				object.base_height = track.corner_heights[corner];
				object.slope_x = track.corner_heights[corner + 1U] - object.base_height;
				object.slope_y =
					track.corner_heights[corner + track.width + 1U] - object.base_height;
				assert(cstrk_add_object(&track, &object, error, sizeof(error)));
				original_id = (legacy_u8)object.legacy_id;
				if (variant != CLASSIC_RENDER_RAMP_VARIANT) {
					original_id += object.surface * CLASSIC_RENDER_ROAD_SURFACE_STEP;
				}
			}
			track_element_map[trackrows[CLASSIC_RENDER_ROW] + CLASSIC_RENDER_COLUMN] = original_id;
			position_classic_render_target(0);
			size_t original_size, extended_size;
			legacy_u32 *original = capture_complete_render(&original_size);
			assert(extended_track_activate(&track, error, sizeof(error)));
			legacy_u32 *extended = capture_complete_render(&extended_size);
			assert(original_size == extended_size);
			size_t differences = 0;
			for (size_t pixel = 0; pixel < original_size; pixel++) {
				differences += original[pixel] != extended[pixel];
			}
			printf("Original hill profile %u, piece %u: %zu of %zu pixels differ\n", mask,
				   original_id, differences, original_size);
			/* Exact native meshes, submission order, and paints retain the
			 * original silhouette and markings in both rasterizers. */
			assert(differences == 0);
			if (variant == 1U || variant == CLASSIC_RENDER_RAMP_VARIANT) {
				struct CSTRK_CELL *cell =
					&track.cells[CLASSIC_RENDER_ROW * track.width + CLASSIC_RENDER_COLUMN];
				struct CSTRK_CELL saved = *cell;
				for (legacy_u8 paint = 0; paint < CLASSIC_RENDER_TERRAIN_PAINTS; paint++) {
					cell->surface = paint < CLASSIC_RENDER_SOLID_PAINTS ? paint + CSTRK_TERRAIN_ICE
																		: CSTRK_TERRAIN_GRASS;
					cell->half_water_corner =
						paint < CLASSIC_RENDER_SOLID_PAINTS
							? CSTRK_WATER_NONE
							: paint - CLASSIC_RENDER_SOLID_PAINTS + CSTRK_WATER_NORTHWEST;
					cell->corner_surface = CSTRK_TERRAIN_DIRT;
					assert(extended_track_activate(&track, error, sizeof(error)));
					size_t painted_size;
					legacy_u32 *painted = capture_complete_render(&painted_size);
					assert(painted_size == original_size);
					legacy_u32 gray =
						RENDER_CAPTURE_OPAQUE_ALPHA |
						(legacy_u32)material_clrlist_ptr[CLASSIC_RENDER_PAVED_MATERIAL] *
							RENDER_CAPTURE_INDEX_RGB_STEP;
					legacy_u32 white =
						RENDER_CAPTURE_OPAQUE_ALPHA |
						(legacy_u32)material_clrlist_ptr[CLASSIC_RENDER_MARKING_MATERIAL] *
							RENDER_CAPTURE_INDEX_RGB_STEP;
					size_t missing_road = 0;
					for (size_t pixel = 0; pixel < original_size; pixel++) {
						if (original[pixel] == gray || original[pixel] == white) {
							missing_road += painted[pixel] != original[pixel];
						}
					}
					if (missing_road) {
						fprintf(stderr,
								"Hill profile %u piece %u terrain paint %u loses %zu road pixels\n",
								mask, original_id, paint, missing_road);
					}
					assert(missing_road == 0);
					free(painted);
					extended_track_unload();
					frames++;
				}
				*cell = saved;
			}
			free(extended);
			extended_track_unload();
			/* Higher native floors replace the original skybox's low-ground
			 * fill. Authored 513/514-unit edge overlaps can change their boundary
			 * shade by two logical pixels; every retained slope interior must
			 * still match the translated original exactly. */
			for (legacy_u8 base = 1; base < CSTRK_MAX_HEIGHT; base++) {
				for (size_t vertex = 0; vertex < (size_t)(track.width + 1U) * (track.height + 1U);
					 vertex++) {
					track.corner_heights[vertex]++;
				}
				if (track.object_count) {
					track.objects[0].base_height++;
				}
				position_classic_render_target(base);
				assert(extended_track_activate(&track, error, sizeof(error)));
				size_t raised_size;
				legacy_u32 *raised = capture_complete_render(&raised_size);
				assert(raised_size == original_size);
				legacy_s32 width, height;
				assert(hires_framebuffer(dos_memory_make_pointer(RENDER_CAPTURE_VIDEO_SEGMENT, 0),
										 &width, &height));
				legacy_u16 edge = (legacy_u16)(width / CLASSIC_RENDER_LOGICAL_WIDTH);
				size_t protected_pixels = 0, changed_pixels = 0;
				for (size_t pixel = 0; pixel < original_size; pixel++) {
					size_t x = pixel % width, y = pixel / width;
					if (x < edge || x + edge >= (size_t)width || y < edge ||
						y + edge >= (size_t)height || original[pixel - edge] != original[pixel] ||
						original[pixel + edge] != original[pixel] ||
						original[pixel - edge * width] != original[pixel] ||
						original[pixel + edge * width] != original[pixel] ||
						original[pixel - edge * width - edge] != original[pixel] ||
						original[pixel - edge * width + edge] != original[pixel] ||
						original[pixel + edge * width - edge] != original[pixel] ||
						original[pixel + edge * width + edge] != original[pixel]) {
						continue;
					}
					legacy_u8 protected = 0;
					for (legacy_u16 material = CLASSIC_RENDER_SLOPE_MATERIAL_FIRST;
						 material <= CLASSIC_RENDER_SLOPE_MATERIAL_LAST; material++) {
						legacy_u32 color = RENDER_CAPTURE_OPAQUE_ALPHA |
										   (legacy_u32)material_clrlist_ptr[material] *
											   RENDER_CAPTURE_INDEX_RGB_STEP;
						protected |= original[pixel] == color;
					}
					if (protected) {
						protected_pixels++;
						if (raised[pixel] != original[pixel]) {
							legacy_u8 native_edge = 0;
							legacy_s16 reach = edge * CLASSIC_RENDER_TRANSLATED_EDGE_PIXELS;
							for (legacy_s16 dy = -reach; dy <= reach && !native_edge; dy++) {
								for (legacy_s16 dx = -reach; dx <= reach && !native_edge; dx++) {
									legacy_s32 nx = (legacy_s32)x + dx, ny = (legacy_s32)y + dy;
									if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
										continue;
									}
									size_t nearby = (size_t)ny * width + nx;
									native_edge = original[nearby] == original[pixel] &&
												  raised[nearby] == original[pixel];
								}
							}
							changed_pixels += !native_edge;
						}
					}
				}
				if (changed_pixels) {
					fprintf(stderr,
							"Raised hill profile %u piece %u base %u changes %zu/%zu native slope "
							"pixels\n",
							mask, original_id, base, changed_pixels, protected_pixels);
				}
				assert(changed_pixels == 0);
				free(raised);
				extended_track_unload();
				frames++;
			}
			for (size_t vertex = 0; vertex < (size_t)(track.width + 1U) * (track.height + 1U);
				 vertex++) {
				track.corner_heights[vertex] -= CSTRK_MAX_HEIGHT - 1U;
			}

			free(original);
			extended_track_unload();
			frames += 2U;
		}
		cstrk_free(&track);
	}
	memcpy(track_element_map, saved_elements, sizeof(saved_elements));
	memcpy(track_terrain_map, saved_terrain, sizeof(saved_terrain));
	return frames;
}

#ifdef RESTUNTS_WRAP_HILL_SHAPES
#define SEAM_RENDER_PAVED_MATERIAL 19U
#define SEAM_RENDER_DIRT_MATERIAL 25U
#define SEAM_RENDER_ICE_MATERIAL 28U

static legacy_u8 seam_capture_hill_shapes;
static legacy_u32 seam_captured_road_parents, seam_captured_marking_vertices;

static legacy_u8 stunt_capture_active, stunt_capture_parts, stunt_capture_details;
static struct SHAPE3D stunt_capture_meshes[STUNT_RENDER_PART_COUNT][STUNT_RENDER_DETAIL_COUNT];
static struct CSTRK_OBJECT stunt_capture_object;
static legacy_u8 stunt_capture_ground;
static struct VECTOR stunt_capture_ground_position, stunt_capture_object_position;

static void stunt_check_supporting_ground(const struct TRANSFORMEDSHAPE3D *instance)
{
	const struct SHAPE3D *shape = instance->shapeptr;
	const legacy_u8 *primitive = shape->shape3d_primitives;
	if (shape->shape3d_numprimitives != 1 || primitive[0] != 4) {
		return;
	}
	const legacy_u8 *indices = primitive + SHAPE3D_PRIMITIVE_HEADER_SIZE + shape->shape3d_numpaints;
	legacy_s16 minimum = CSTRK_TILE_WORLD_SIZE, maximum = -CSTRK_TILE_WORLD_SIZE;
	legacy_s16 cross_minimum = minimum, cross_maximum = maximum;
	for (legacy_u8 vertex = 0; vertex < 4; vertex++) {
		struct VECTOR point;
		shape3d_vertex_read(shape, indices[vertex], &point);
		legacy_s16 old_x = point.x;
		switch (instance->rotvec.z & ANGLE_MASK) {
			case ANGLE_QUARTER_TURN:
				point.x = point.z;
				point.z = -old_x;
				break;
			case ANGLE_HALF_TURN:
				point.x = -point.x;
				point.z = -point.z;
				break;
			case ANGLE_THREE_QUARTER_TURN:
				point.x = -point.z;
				point.z = old_x;
				break;
		}
		legacy_s16 along = stunt_capture_object.slope_x ? point.x : point.z;
		legacy_s16 across = stunt_capture_object.slope_x ? point.z : point.x;
		legacy_s16 expected_height =
			CSTRK_LEVEL_WORLD_HEIGHT / 2 +
			(stunt_capture_object.slope_x * point.x - stunt_capture_object.slope_y * point.z) *
				CSTRK_LEVEL_WORLD_HEIGHT / CSTRK_TILE_WORLD_SIZE;
		if ((along != -CSTRK_TILE_WORLD_SIZE / 2 && along != CSTRK_TILE_WORLD_SIZE / 2) ||
			point.y != expected_height) {
			return;
		}
		if (along < minimum) {
			minimum = along;
		}
		if (along > maximum) {
			maximum = along;
		}
		if (across < cross_minimum) {
			cross_minimum = across;
		}
		if (across > cross_maximum) {
			cross_maximum = across;
		}
	}
	if (maximum - minimum == CSTRK_TILE_WORLD_SIZE) {
		if (cross_maximum == -ROAD_HALF_WIDTH && cross_minimum <= -CSTRK_TILE_WORLD_SIZE / 2) {
			stunt_capture_ground |= 1U;
			stunt_capture_ground_position = instance->pos;
		} else if (cross_minimum == ROAD_HALF_WIDTH && cross_maximum >= CSTRK_TILE_WORLD_SIZE / 2) {
			stunt_capture_ground |= 2U;
			stunt_capture_ground_position = instance->pos;
		}
	}
}

static void stunt_check_submitted_shape(const struct TRANSFORMEDSHAPE3D *instance)
{
	const struct SHAPE3D *shape = instance->shapeptr;
	for (legacy_u8 part = 0; part < STUNT_RENDER_PART_COUNT; part++) {
		for (legacy_u8 detail = 0; detail < STUNT_RENDER_DETAIL_COUNT; detail++) {
			const struct SHAPE3D *expected = &stunt_capture_meshes[part][detail];
			if (shape->shape3d_numverts != expected->shape3d_numverts ||
				shape->shape3d_numprimitives != expected->shape3d_numprimitives) {
				continue;
			}
			legacy_u8 same_footprint = 1;
			for (legacy_u16 vertex = 0; vertex < expected->shape3d_numverts; vertex++) {
				struct VECTOR actual, target;
				shape3d_vertex_read(shape, vertex, &actual);
				shape3d_vertex_read(expected, vertex, &target);
				if (actual.x != target.x || actual.z != target.z) {
					same_footprint = 0;
					break;
				}
			}
			if (!same_footprint) {
				continue;
			}
			assert(instance->rotvec.x == 0 && instance->rotvec.y == 0 && instance->rotvec.z == 0);
			assert(shape->shape3d_numpaints == expected->shape3d_numpaints);
			for (legacy_u16 vertex = 0; vertex < expected->shape3d_numverts; vertex++) {
				struct VECTOR actual, target;
				shape3d_vertex_read(shape, vertex, &actual);
				shape3d_vertex_read(expected, vertex, &target);
				assert(actual.y == target.y);
			}
			stunt_capture_parts |= 1U << part;
			if (part == 0) {
				stunt_capture_details |= 1U << detail;
				stunt_capture_object_position = instance->pos;
			}
		}
	}
}

/* Inspect the actual temporary road mesh submitted by the frame renderer.
 * The classic path goes through the same wrapper without any assertions. */
static void seam_check_submitted_road(const struct SHAPE3D *shape)
{
	const legacy_u8 *primitive = shape->shape3d_primitives;
	struct VECTOR origin = {0};
	legacy_s64 nx = 0, ny = 0, nz = 0;
	legacy_s32 span = 0, rise = 0;
	legacy_u8 road_parent = 0, parent_checked = 0;
	for (legacy_u16 index = 0; index < shape->shape3d_numprimitives; index++) {
		legacy_u16 count = primitive[0];
		if (count < SHAPE3D_PRIMITIVE_POLYGON_FIRST || count > SHAPE3D_PRIMITIVE_POLYGON_LAST) {
			break;
		}
		const legacy_u8 *indices =
			primitive + SHAPE3D_PRIMITIVE_HEADER_SIZE + shape->shape3d_numpaints;
		if ((primitive[1] & SHAPE3D_PRIMITIVE_SKIP_DEPTH_SORT_FLAG) == 0) {
			legacy_u8 material = primitive[SHAPE3D_PRIMITIVE_HEADER_SIZE];
			road_parent = material == SEAM_RENDER_PAVED_MATERIAL ||
						  material == SEAM_RENDER_DIRT_MATERIAL ||
						  material == SEAM_RENDER_ICE_MATERIAL;
			parent_checked = 0;
			nx = ny = nz = 0;
			if (road_parent) {
				struct VECTOR first, last;
				shape3d_vertex_read(shape, indices[0], &origin);
				legacy_s16 minimum_x = origin.x, maximum_x = origin.x;
				legacy_s16 minimum_y = origin.y, maximum_y = origin.y;
				legacy_s16 minimum_z = origin.z, maximum_z = origin.z;
				for (legacy_u16 vertex = 0; vertex < count; vertex++) {
					struct VECTOR point;
					shape3d_vertex_read(shape, indices[vertex], &point);
					if (point.x < minimum_x) {
						minimum_x = point.x;
					}
					if (point.x > maximum_x) {
						maximum_x = point.x;
					}
					if (point.y < minimum_y) {
						minimum_y = point.y;
					}
					if (point.y > maximum_y) {
						maximum_y = point.y;
					}
					if (point.z < minimum_z) {
						minimum_z = point.z;
					}
					if (point.z > maximum_z) {
						maximum_z = point.z;
					}
				}
				for (legacy_u16 vertex = 1; vertex + 1U < count && ny == 0; vertex++) {
					shape3d_vertex_read(shape, indices[vertex], &first);
					shape3d_vertex_read(shape, indices[vertex + 1U], &last);
					legacy_s32 ax = first.x - origin.x, ay = first.y - origin.y,
							   az = first.z - origin.z;
					legacy_s32 bx = last.x - origin.x, by = last.y - origin.y,
							   bz = last.z - origin.z;
					nx = (legacy_s64)ay * bz - (legacy_s64)az * by;
					ny = (legacy_s64)az * bx - (legacy_s64)ax * bz;
					nz = (legacy_s64)ax * by - (legacy_s64)ay * bx;
				}
				span = nx != 0 ? maximum_x - minimum_x : maximum_z - minimum_z;
				rise = maximum_y - minimum_y;
			}
		} else if (road_parent && ny != 0 && (nx != 0 || nz != 0)) {
			if (!parent_checked) {
				assert(span == CSTRK_TILE_WORLD_SIZE);
				assert(rise == CSTRK_LEVEL_WORLD_HEIGHT);
				seam_captured_road_parents++;
				parent_checked = 1;
			}
			for (legacy_u16 vertex = 0; vertex < count; vertex++) {
				struct VECTOR point;
				shape3d_vertex_read(shape, indices[vertex], &point);
				legacy_s64 distance = nx * (point.x - origin.x) + ny * (point.y - origin.y) +
									  nz * (point.z - origin.z);
				if (distance < 0) {
					distance = -distance;
				}
				legacy_s64 vertical = ny < 0 ? -ny : ny;
				/* Integer resource vertices may round by half a world unit,
				 * but painted dashes must not sag below their road plane. */
				assert(distance * 2 <= vertical);
				seam_captured_marking_vertices++;
			}
		}
		primitive = indices + count;
	}
}

legacy_u16 __real_shape3d_transform_and_queue(struct TRANSFORMEDSHAPE3D *instance);
legacy_u16 __wrap_shape3d_transform_and_queue(struct TRANSFORMEDSHAPE3D *instance)
{
	if (seam_capture_hill_shapes) {
		seam_check_submitted_road(instance->shapeptr);
	}
	if (stunt_capture_active) {
		stunt_check_supporting_ground(instance);
		stunt_check_submitted_shape(instance);
	}
	return __real_shape3d_transform_and_queue(instance);
}
#endif

#define SEAM_RENDER_FIRST_COLUMN 38U
#define SEAM_RENDER_CROSS_COLUMN 43U
#define SEAM_RENDER_STEP_COUNT 2U
#define SEAM_RENDER_DIRECTION_COUNT 4U
#define SEAM_RENDER_PROFILE_COUNT 4U
#define SEAM_RENDER_OUTER_PROFILE 1U
#define SEAM_RENDER_INNER_PROFILE 2U
#define SEAM_RENDER_MIXED_PROFILE 3U
#define SEAM_RENDER_ROAD_VARIANTS 4U
#define SEAM_RENDER_ROAD_MATERIAL_FIRST 19U
#define SEAM_RENDER_ROAD_MATERIAL_LAST 30U
#define SEAM_RENDER_SKY_MARKER RENDER_CAPTURE_SKY_MARKER
#define SEAM_RENDER_CAMERA_DISTANCE 2400
#define SEAM_RENDER_CAMERA_ELEVATION 0
#define SEAM_RENDER_GRASS_MATERIAL_FIRST 101U
#define SEAM_RENDER_GRASS_MATERIAL_LAST 105U
#define SEAM_RENDER_HASH_OFFSET UINT64_C(1469598103934665603)
#define SEAM_RENDER_HASH_PRIME UINT64_C(1099511628211)

static legacy_u8 seam_render_level(legacy_u16 coordinate, legacy_u8 base)
{
	return base + (coordinate <= SEAM_RENDER_FIRST_COLUMN ? 0U
				   : coordinate >= SEAM_RENDER_FIRST_COLUMN + SEAM_RENDER_STEP_COUNT
					   ? SEAM_RENDER_STEP_COUNT
					   : coordinate - SEAM_RENDER_FIRST_COLUMN);
}

static legacy_u8 seam_render_terrain_pixel(legacy_u32 pixel)
{
	if (pixel == (RENDER_CAPTURE_OPAQUE_ALPHA |
				  (legacy_u32)skybox.ground_color * RENDER_CAPTURE_INDEX_RGB_STEP)) {
		return 1;
	}
	for (legacy_u16 material = SEAM_RENDER_ROAD_MATERIAL_FIRST;
		 material <= SEAM_RENDER_ROAD_MATERIAL_LAST; material++) {
		if (pixel == (RENDER_CAPTURE_OPAQUE_ALPHA |
					  (legacy_u32)material_clrlist_ptr[material] * RENDER_CAPTURE_INDEX_RGB_STEP)) {
			return 1;
		}
	}
	for (legacy_u16 material = SEAM_RENDER_GRASS_MATERIAL_FIRST;
		 material <= SEAM_RENDER_GRASS_MATERIAL_LAST; material++) {
		if (pixel == (RENDER_CAPTURE_OPAQUE_ALPHA |
					  (legacy_u32)material_clrlist_ptr[material] * RENDER_CAPTURE_INDEX_RGB_STEP)) {
			return 1;
		}
	}
	return 0;
}

static size_t seam_render_interior_sky(const legacy_u32 *pixels, legacy_s32 width,
									   legacy_s32 height)
{
	legacy_u32 sky = render_capture_mark_sky
						 ? RENDER_CAPTURE_SKY_COLOR
						 : RENDER_CAPTURE_OPAQUE_ALPHA |
							   (legacy_u32)skybox.sky_color * RENDER_CAPTURE_INDEX_RGB_STEP;
	size_t gaps = 0, terrain_columns = 0;
	for (legacy_s32 x = 0; x < width; x++) {
		legacy_s32 first = height, last = 0;
		for (legacy_s32 y = 0; y < height; y++) {
			if (seam_render_terrain_pixel(pixels[(size_t)y * width + x])) {
				if (first == height) {
					first = y;
				}
				last = y;
			}
		}
		terrain_columns += first < last;
		// The uniform hill field fills every column below its horizon. Sky
		// between terrain samples exposes a missing join, not open scenery.
		for (legacy_s32 y = first + 1; y < last; y++) {
			gaps += pixels[(size_t)y * width + x] == sky;
		}
	}
	assert(terrain_columns > (size_t)width / 2U);
	return gaps;
}

static legacy_u16 check_consecutive_hill_seams(void)
{
	struct CSTRK_TRACK track;
	legacy_char error[GRASS_RENDER_ERROR_CAPACITY];
	cstrk_init(&track);
	assert(cstrk_create(&track, CSTRK_MAX_DIMENSION, CSTRK_MAX_DIMENSION, error, sizeof(error)));
	track.horizon = CSTRK_HORIZON_COUNTRY;
	legacy_u16 frames = 0;
	legacy_u64 classic_hash = SEAM_RENDER_HASH_OFFSET;
	legacy_s32 saved_scale = hires_render_scale();
	for (legacy_u8 base = 0; base + SEAM_RENDER_STEP_COUNT <= CSTRK_MAX_HEIGHT; base++) {
		for (legacy_u8 direction = 0;
			 direction < SEAM_RENDER_DIRECTION_COUNT * SEAM_RENDER_PROFILE_COUNT; direction++) {
			legacy_u8 rotation = direction % SEAM_RENDER_DIRECTION_COUNT;
			legacy_u8 profile = direction / SEAM_RENDER_DIRECTION_COUNT;
			for (legacy_u16 y = 0; y <= track.height; y++) {
				for (legacy_u16 x = 0; x <= track.width; x++) {
					legacy_u16 u = rotation == 0   ? x
								   : rotation == 1 ? y
								   : rotation == 2 ? track.width - x
												   : track.height - y;
					legacy_u16 v = rotation == 0   ? y
								   : rotation == 1 ? track.width - x
								   : rotation == 2 ? track.height - y
												   : x;
					legacy_u8 first = seam_render_level(u, base);
					legacy_u8 second = seam_render_level(v, base);
					track.corner_heights[(size_t)y * (track.width + 1U) + x] =
						profile == SEAM_RENDER_OUTER_PROFILE   ? (first < second ? first : second)
						: profile == SEAM_RENDER_INNER_PROFILE ? (first > second ? first : second)
															   : first;
					if (x < track.width && y < track.height) {
						struct CSTRK_CELL *cell = &track.cells[(size_t)y * track.width + x];
						cell->surface = profile == SEAM_RENDER_MIXED_PROFILE ? CSTRK_TERRAIN_DIRT
																			 : CSTRK_TERRAIN_GRASS;
						cell->half_water_corner =
							profile == SEAM_RENDER_MIXED_PROFILE
								? CSTRK_WATER_NORTHWEST + (x + y) % SEAM_RENDER_DIRECTION_COUNT
								: CSTRK_WATER_NONE;
						cell->corner_surface = CSTRK_TERRAIN_ICE;
					}
				}
			}
			legacy_u8 corner_profile =
				profile == SEAM_RENDER_OUTER_PROFILE || profile == SEAM_RENDER_INNER_PROFILE;
			legacy_u16 target = SEAM_RENDER_FIRST_COLUMN + corner_profile;
			legacy_u16 cross = corner_profile ? target : SEAM_RENDER_CROSS_COLUMN;
			legacy_u16 column = rotation == 0	? target
								: rotation == 1 ? track.width - cross - 1U
								: rotation == 2 ? track.width - target - 1U
												: cross;
			legacy_u16 row = rotation == 0	 ? cross
							 : rotation == 1 ? target
							 : rotation == 2 ? track.height - cross - 1U
											 : track.height - target - 1U;
			position_render_target(column, row);
			state.playerstate.car_position.ly =
				((base + (corner_profile ? SEAM_RENDER_STEP_COUNT : 0U)) *
					 CSTRK_LEVEL_WORLD_HEIGHT +
				 GRASS_RENDER_CAR_HEIGHT) *
				GRASS_RENDER_POSITION_SCALE;
			state.playerstate.car_previous_position = state.playerstate.car_position;
			/* Retain the camera anchor while hiding its car and shadow. Car paint
			 * above the horizon must not be mistaken for elevated terrain. */
			state.playerstate.car_crashBmpFlag = CRASH_EVENT_WATER;
			custom_camera.distance = SEAM_RENDER_CAMERA_DISTANCE;
			custom_camera.elevation_angle = SEAM_RENDER_CAMERA_ELEVATION;
			legacy_u8 road_variants =
				profile == SEAM_RENDER_OUTER_PROFILE || profile == SEAM_RENDER_INNER_PROFILE
					? 1U
					: SEAM_RENDER_ROAD_VARIANTS;
			for (legacy_u8 road = 0; road < road_variants; road++) {
				track.object_count = 0;
				if (road) {
					for (legacy_u8 step = 0; step < SEAM_RENDER_STEP_COUNT; step++) {
						struct CSTRK_OBJECT object = {0};
						object.legacy_id = rotation % 2U ? 4U : 5U;
						object.surface = road - 1U;
						object.x = column + (rotation == 0 ? step : rotation == 2 ? -step : 0);
						object.y = row + (rotation == 1 ? step : rotation == 3 ? -step : 0);
						object.width = object.height = object.occupied_mask = 1;
						size_t vertex = (size_t)object.y * (track.width + 1U) + object.x;
						object.base_height = track.corner_heights[vertex];
						object.slope_x = track.corner_heights[vertex + 1U] - object.base_height;
						object.slope_y =
							track.corner_heights[vertex + track.width + 1U] - object.base_height;
						assert(cstrk_add_object(&track, &object, error, sizeof(error)));
					}
				}
				activate_render_track(&track, road);
				for (legacy_s32 scale = supersight_enabled ? HIRES_SCALE : 1; scale;
					 scale = supersight_enabled ? scale / 2 : 0) {
					if (supersight_enabled) {
						hires_set_render_scale(scale);
					}
					for (legacy_u8 camera = 0; camera < SEAM_RENDER_DIRECTION_COUNT; camera++) {
						custom_camera.azimuth_angle = camera * ANGLE_QUARTER_TURN;
						// Ice highlights share the real sky color. Give the background
						// a distinct marker so visible ice cannot look like a hole.
						legacy_s16 saved_sky = skybox.sky_color;
						if (supersight_enabled) {
							skybox.sky_color = SEAM_RENDER_SKY_MARKER;
							assert(!seam_render_terrain_pixel(RENDER_CAPTURE_OPAQUE_ALPHA |
															  SEAM_RENDER_SKY_MARKER *
																  RENDER_CAPTURE_INDEX_RGB_STEP));
						}
						render_capture_mark_sky = supersight_enabled != 0;
#ifdef RESTUNTS_WRAP_HILL_SHAPES
						seam_capture_hill_shapes = supersight_enabled && road != 0;
						seam_captured_road_parents = seam_captured_marking_vertices = 0;
#endif
						size_t size;
						legacy_u32 *pixels = capture_complete_render(&size);
#ifdef RESTUNTS_WRAP_HILL_SHAPES
						if (seam_capture_hill_shapes) {
							assert(seam_captured_road_parents != 0);
							assert(seam_captured_marking_vertices != 0);
						}
						seam_capture_hill_shapes = 0;
#endif
						legacy_s32 width, height;
						assert(hires_framebuffer(
								   dos_memory_make_pointer(RENDER_CAPTURE_VIDEO_SEGMENT, 0), &width,
								   &height) != NULL);
						assert(size == (size_t)width * height);
						size_t gaps = seam_render_interior_sky(pixels, width, height);
						skybox.sky_color = saved_sky;
						render_capture_mark_sky = 0;
						if (supersight_enabled && gaps) {
							fprintf(stderr,
									"Consecutive hills base %u direction %u road %u camera %u "
									"scale %d expose %zu interior sky pixels\n",
									base, direction, road, camera, scale, gaps);
						}
						if (supersight_enabled) {
							assert(gaps == 0);
						} else {
							for (size_t pixel = 0; pixel < size; pixel++) {
								classic_hash =
									(classic_hash ^ pixels[pixel]) * SEAM_RENDER_HASH_PRIME;
							}
						}
						free(pixels);
						frames++;
					}
				}
			}
		}
	}
	hires_set_render_scale(saved_scale);
	if (!supersight_enabled) {
		printf("Consecutive hills classic pixel hash: %016" LEGACY_PRIx64 "\n", classic_hash);
	}
	extended_track_unload();
	cstrk_free(&track);
	return frames;
}

#define LOOP_RENDER_COLUMN 44U
#define LOOP_RENDER_ROW 43U
#define LOOP_RENDER_CAMERA_DISTANCE 2700
#define LOOP_RENDER_CAMERA_ELEVATION 96
#define LOOP_RENDER_ASPHALT_MATERIAL CLASSIC_RENDER_PAVED_MATERIAL
#define LOOP_RENDER_OBJECT_ID 65U
#define LOOP_RENDER_LONG_SIDE_CELLS 2U
#define LOOP_RENDER_CAMERA_COUNT 2U
#define LOOP_RENDER_CAR_CLEARANCE 32U
#define LOOP_RENDER_MIN_ASPHALT_PIXELS 500U

static legacy_u16 check_elevated_multitile_road(void)
{
	struct CSTRK_TRACK track;
	legacy_char error[GRASS_RENDER_ERROR_CAPACITY];
	cstrk_init(&track);
	assert(cstrk_create(&track, CSTRK_MAX_DIMENSION, CSTRK_MAX_DIMENSION, error, sizeof(error)));
	legacy_u64 classic_hash = SEAM_RENDER_HASH_OFFSET;
	legacy_u16 frames = 0;
	for (legacy_u8 rotation = 0; rotation < SEAM_RENDER_DIRECTION_COUNT; rotation++) {
		struct CSTRK_OBJECT object = {0};
		object.legacy_id = LOOP_RENDER_OBJECT_ID;
		object.rotation = rotation;
		object.x = LOOP_RENDER_COLUMN;
		object.y = LOOP_RENDER_ROW;
		object.width = (rotation & 1U) ? 1U : LOOP_RENDER_LONG_SIDE_CELLS;
		object.height = (rotation & 1U) ? LOOP_RENDER_LONG_SIDE_CELLS : 1U;
		object.occupied_mask = (1U << (object.width * object.height)) - 1U;
		memset(track.corner_heights, 0, (size_t)(track.width + 1U) * (track.height + 1U));
		track.object_count = 0;
		assert(cstrk_add_object(&track, &object, error, sizeof(error)));
		for (legacy_u8 camera = 0; camera < LOOP_RENDER_CAMERA_COUNT; camera++) {
			legacy_u32 *ground = NULL;
			size_t ground_size = 0;
			for (legacy_u8 base = 0; base <= CSTRK_MAX_HEIGHT; base++) {
				memset(track.corner_heights, base,
					   (size_t)(track.width + 1U) * (track.height + 1U));
				track.objects[0].base_height = base;
				activate_render_track(&track, 1);
				position_render_target(LOOP_RENDER_COLUMN, LOOP_RENDER_ROW);
				struct CARSTATE *car = &state.playerstate;
				car->car_position.lx = (LOOP_RENDER_COLUMN * CSTRK_TILE_WORLD_SIZE +
										object.width * (CSTRK_TILE_WORLD_SIZE / 2U)) *
									   GRASS_RENDER_POSITION_SCALE;
				car->car_position.lz = ((track.height - LOOP_RENDER_ROW) * CSTRK_TILE_WORLD_SIZE -
										object.height * (CSTRK_TILE_WORLD_SIZE / 2U)) *
									   GRASS_RENDER_POSITION_SCALE;
				car->car_position.ly =
					(base * CSTRK_LEVEL_WORLD_HEIGHT + LOOP_RENDER_CAR_CLEARANCE) *
					GRASS_RENDER_POSITION_SCALE;
				car->car_previous_position = car->car_position;
				car->car_crashBmpFlag = CRASH_EVENT_WATER;
				custom_camera.distance = LOOP_RENDER_CAMERA_DISTANCE;
				custom_camera.elevation_angle = LOOP_RENDER_CAMERA_ELEVATION;
				custom_camera.azimuth_angle = (rotation * ANGLE_QUARTER_TURN + ANGLE_HALF_TURN +
											   ANGLE_EIGHTH_TURN + camera * ANGLE_QUARTER_TURN) &
											  ANGLE_MASK;
				size_t size;
				legacy_u32 *pixels = capture_complete_render(&size);
				if (base == 0) {
					ground = pixels;
					ground_size = size;
				} else if (supersight_enabled) {
					assert(size == ground_size);
					legacy_s32 width, height;
					assert(hires_framebuffer(
						dos_memory_make_pointer(RENDER_CAPTURE_VIDEO_SEGMENT, 0), &width, &height));
					legacy_u32 asphalt =
						RENDER_CAPTURE_OPAQUE_ALPHA |
						(legacy_u32)material_clrlist_ptr[LOOP_RENDER_ASPHALT_MATERIAL] *
							RENDER_CAPTURE_INDEX_RGB_STEP;
					size_t protected = 0, covered = 0;
					/* Translating an identical loop and camera by a whole level must
					 * preserve its asphalt. Interior pixels avoid rasterized edges. */
					for (legacy_s32 y = 1; y + 1 < height; y++) {
						for (legacy_s32 x = 1; x + 1 < width; x++) {
							size_t pixel = (size_t)y * width + x;
							if (ground[pixel] == asphalt && ground[pixel - 1U] == asphalt &&
								ground[pixel + 1U] == asphalt && ground[pixel - width] == asphalt &&
								ground[pixel + width] == asphalt) {
								protected++;
								covered += pixels[pixel] != asphalt;
							}
						}
					}
					assert(protected > LOOP_RENDER_MIN_ASPHALT_PIXELS);
					if (covered) {
						fprintf(stderr,
								"Elevated loop rotation %u camera %u level %u covers %zu/%zu "
								"asphalt pixels\n",
								rotation, camera, base, covered, protected);
					}
					assert(covered == 0);
				}
				if (!supersight_enabled) {
					for (size_t pixel = 0; pixel < size; pixel++) {
						classic_hash = (classic_hash ^ pixels[pixel]) * SEAM_RENDER_HASH_PRIME;
					}
				}
				if (base) {
					free(pixels);
				}
				frames++;
			}
			free(ground);
		}
	}
	if (!supersight_enabled) {
		printf("Elevated loops classic pixel hash: %016" LEGACY_PRIx64 "\n", classic_hash);
	}
	extended_track_unload();
	cstrk_free(&track);
	return frames;
}

#define OVERPASS_RENDER_COLUMN 44U
#define OVERPASS_RENDER_ROW 43U
#define OVERPASS_RENDER_LEVEL 2U
#define OVERPASS_RENDER_CAR_CLEARANCE 32U

static legacy_u16 check_overpass_surfaces(void)
{
	struct CSTRK_TRACK track;
	legacy_char error[GRASS_RENDER_ERROR_CAPACITY];
	cstrk_init(&track);
	assert(cstrk_create(&track, CSTRK_MAX_DIMENSION, CSTRK_MAX_DIMENSION, error, sizeof(error)));
	memset(track.corner_heights, OVERPASS_RENDER_LEVEL,
		   (size_t)(track.width + 1U) * (track.height + 1U));
	struct CSTRK_OBJECT object = {0};
	object.legacy_id = CSTRK_OVERPASS_FIRST_ID;
	object.x = OVERPASS_RENDER_COLUMN;
	object.y = OVERPASS_RENDER_ROW;
	object.width = object.height = object.occupied_mask = 1;
	object.base_height = OVERPASS_RENDER_LEVEL;
	assert(cstrk_add_object(&track, &object, error, sizeof(error)));
	legacy_u16 frames = 0;
	for (legacy_u8 rotation = 0; rotation < SEAM_RENDER_DIRECTION_COUNT; rotation++) {
		track.objects[0].rotation = rotation;
		position_render_target(OVERPASS_RENDER_COLUMN, OVERPASS_RENDER_ROW);
		state.playerstate.car_position.ly =
			(OVERPASS_RENDER_LEVEL * CSTRK_LEVEL_WORLD_HEIGHT + OVERPASS_RENDER_CAR_CLEARANCE) *
			GRASS_RENDER_POSITION_SCALE;
		state.playerstate.car_previous_position = state.playerstate.car_position;
		state.playerstate.car_crashBmpFlag = CRASH_EVENT_WATER;
		custom_camera.distance = LOOP_RENDER_CAMERA_DISTANCE;
		custom_camera.elevation_angle = LOOP_RENDER_CAMERA_ELEVATION;
		custom_camera.azimuth_angle = rotation * ANGLE_QUARTER_TURN + ANGLE_EIGHTH_TURN;
		legacy_u32 *pixels[SURFACE_RENDER_COUNT][SURFACE_RENDER_COUNT];
		size_t size = 0;
		for (legacy_u8 upper = CSTRK_ROAD_PAVED; upper <= CSTRK_ROAD_ICE; upper++) {
			for (legacy_u8 lower = CSTRK_ROAD_PAVED; lower <= CSTRK_ROAD_ICE; lower++) {
				track.objects[0].surface = upper;
				track.objects[0].lower_surface = lower;
				activate_render_track(&track, 1);
				size_t count;
				pixels[upper][lower] = capture_complete_render(&count);
				assert(size == 0 || count == size);
				size = count;
				frames++;
			}
		}
		/* Changing one road must visibly affect that road with every material
		 * on the other. The two solid-material changes must occupy disjoint
		 * pixels, proving that neither selector repaints both roads. */
		for (legacy_u8 other = CSTRK_ROAD_PAVED; other <= CSTRK_ROAD_ICE; other++) {
			for (legacy_u8 surface = CSTRK_ROAD_DIRT; surface <= CSTRK_ROAD_ICE; surface++) {
				size_t upper_changed = 0, lower_changed = 0;
				for (size_t pixel = 0; pixel < size; pixel++) {
					upper_changed +=
						pixels[CSTRK_ROAD_PAVED][other][pixel] != pixels[surface][other][pixel];
					lower_changed +=
						pixels[other][CSTRK_ROAD_PAVED][pixel] != pixels[other][surface][pixel];
				}
				assert(upper_changed > SURFACE_RENDER_MIN_CHANGED_PIXELS);
				assert(lower_changed > SURFACE_RENDER_MIN_CHANGED_PIXELS);
			}
		}
		size_t upper_changed = 0, lower_changed = 0;
		for (size_t pixel = 0; pixel < size; pixel++) {
			legacy_u32 base = pixels[CSTRK_ROAD_DIRT][CSTRK_ROAD_DIRT][pixel];
			legacy_u8 upper = pixels[CSTRK_ROAD_ICE][CSTRK_ROAD_DIRT][pixel] != base;
			legacy_u8 lower = pixels[CSTRK_ROAD_DIRT][CSTRK_ROAD_ICE][pixel] != base;
			assert(!upper || !lower);
			assert(pixels[CSTRK_ROAD_ICE][CSTRK_ROAD_ICE][pixel] ==
				   (upper ? pixels[CSTRK_ROAD_ICE][CSTRK_ROAD_DIRT][pixel]
						  : pixels[CSTRK_ROAD_DIRT][CSTRK_ROAD_ICE][pixel]));
			upper_changed += upper;
			lower_changed += lower;
		}
		assert(upper_changed > SURFACE_RENDER_MIN_CHANGED_PIXELS);
		assert(lower_changed > SURFACE_RENDER_MIN_CHANGED_PIXELS);
		for (legacy_u8 upper = CSTRK_ROAD_PAVED; upper <= CSTRK_ROAD_ICE; upper++) {
			for (legacy_u8 lower = CSTRK_ROAD_PAVED; lower <= CSTRK_ROAD_ICE; lower++) {
				free(pixels[upper][lower]);
			}
		}
	}
	extended_track_unload();
	cstrk_free(&track);
	return frames;
}

#define STUNT_RENDER_COLUMN 44U
#define STUNT_RENDER_ROW 43U
#define STUNT_RENDER_LOW_DETAIL 4
#define STUNT_RENDER_CAR_CLEARANCE 32U
#define STUNT_RENDER_CAMERA_DISTANCE 2700
#define STUNT_RENDER_CAMERA_ELEVATION 128

#ifdef RESTUNTS_WRAP_HILL_SHAPES
static void prepare_stunt_capture(const struct CSTRK_OBJECT *object)
{
	stunt_capture_object = *object;
	const struct TRACKOBJECT *definition = &trkObjectList[extended_track_object_id(object)];
	for (legacy_u8 part = 0; part < STUNT_RENDER_PART_COUNT; part++) {
		const struct TRACKOBJECT *render_definition =
			part ? frame_track_object_from_legacy_index(definition->ss_ssOvelay) : definition;
		const struct SHAPE3D *sources[STUNT_RENDER_DETAIL_COUNT] = {
			render_definition->ss_shapePtr, render_definition->ss_loShapePtr
												? render_definition->ss_loShapePtr
												: render_definition->ss_shapePtr};
		for (legacy_u8 detail = 0; detail < STUNT_RENDER_DETAIL_COUNT; detail++) {
			struct SHAPE3D *mesh = &stunt_capture_meshes[part][detail];
			assert(extended_geometry_transform_shape(sources[detail], mesh,
													 render_definition->ss_rotY, object->surface,
													 definition->ss_physicalModel));
			for (legacy_u16 vertex = 0; vertex < mesh->shape3d_numverts; vertex++) {
				struct VECTOR point;
				shape3d_vertex_read(mesh, vertex, &point);
				legacy_s16 along = object->slope_x ? point.x : point.z;
				if (along < -CSTRK_TILE_WORLD_SIZE / 2 &&
					along >= -CSTRK_TILE_WORLD_SIZE / 2 - STUNT_RENDER_AUTHORED_OVERSCAN) {
					along = -CSTRK_TILE_WORLD_SIZE / 2;
				} else if (along > CSTRK_TILE_WORLD_SIZE / 2 &&
						   along <= CSTRK_TILE_WORLD_SIZE / 2 + STUNT_RENDER_AUTHORED_OVERSCAN) {
					along = CSTRK_TILE_WORLD_SIZE / 2;
				}
				if (object->slope_x) {
					point.x = along;
				} else {
					point.z = along;
				}
				/* One aligned tile climbs 450 units over 1024 world units. Its
				 * roof and barriers must retain every authored vertical offset. */
				legacy_s32 distance = CSTRK_TILE_WORLD_SIZE / 2 + object->slope_x * point.x -
									  object->slope_y * point.z;
				legacy_s32 rise = distance * CSTRK_LEVEL_WORLD_HEIGHT;
				point.y += (legacy_s16)((rise + (rise < 0 ? -CSTRK_TILE_WORLD_SIZE / 2
														  : CSTRK_TILE_WORLD_SIZE / 2)) /
										CSTRK_TILE_WORLD_SIZE);
				shape3d_vertex_write(mesh, vertex, &point);
			}
		}
	}
}

static void release_stunt_capture(void)
{
	for (legacy_u8 part = 0; part < STUNT_RENDER_PART_COUNT; part++) {
		for (legacy_u8 detail = 0; detail < STUNT_RENDER_DETAIL_COUNT; detail++) {
			extended_geometry_free_shape(&stunt_capture_meshes[part][detail]);
		}
	}
}
#endif

static legacy_u16 check_sloped_stunt_rendering(void)
{
	static const legacy_u16 ids[] = {CSTRK_TUNNEL_FIRST_ID, CSTRK_SLALOM_FIRST_ID};
	legacy_s8 saved_detail = detail_level;
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	legacy_char error[GRASS_RENDER_ERROR_CAPACITY];
	assert(cstrk_create(&track, CSTRK_MAX_DIMENSION, CSTRK_MAX_DIMENSION, error, sizeof(error)));
	track.horizon = CSTRK_HORIZON_COUNTRY;
	legacy_u16 frames = 0;
	for (legacy_u8 kind = 0; kind < sizeof(ids) / sizeof(ids[0]); kind++) {
#ifdef RESTUNTS_WRAP_HILL_SHAPES
		legacy_u8 captured_details = 0;
#endif
		for (legacy_u8 base = 0; base < CSTRK_MAX_HEIGHT; base++) {
			for (legacy_u8 rotation = 0; rotation < SEAM_RENDER_DIRECTION_COUNT; rotation++) {
				for (legacy_s8 grade = -1; grade <= 1; grade += 2) {
					legacy_u8 across_x = rotation & 1U;
					legacy_u16 slope_coordinate = across_x ? STUNT_RENDER_COLUMN : STUNT_RENDER_ROW;
					for (legacy_u16 y = 0; y <= track.height; y++) {
						for (legacy_u16 x = 0; x <= track.width; x++) {
							legacy_u16 coordinate = across_x ? x : y;
							track.corner_heights[(size_t)y * (track.width + 1U) + x] =
								base + (grade > 0 ? coordinate > slope_coordinate
												  : coordinate <= slope_coordinate);
							if (x < track.width && y < track.height) {
								track.cells[(size_t)y * track.width + x].surface =
									base & 1U ? CSTRK_TERRAIN_DIRT : CSTRK_TERRAIN_GRASS;
							}
						}
					}
					for (legacy_u8 surface = CSTRK_ROAD_PAVED; surface <= CSTRK_ROAD_ICE;
						 surface++) {
						struct CSTRK_OBJECT object = {0};
						object.legacy_id = ids[kind];
						object.x = STUNT_RENDER_COLUMN;
						object.y = STUNT_RENDER_ROW;
						object.rotation = rotation;
						object.surface = surface;
						object.width = object.height = object.occupied_mask = 1;
						object.base_height = base + (grade < 0);
						object.slope_x = across_x ? grade : 0;
						object.slope_y = across_x ? 0 : grade;
						track.object_count = 0;
						assert(cstrk_add_object(&track, &object, error, sizeof(error)));
						activate_render_track(&track, 1);
#ifdef RESTUNTS_WRAP_HILL_SHAPES
						prepare_stunt_capture(extended_track_object_at(object.x, object.y));
#endif
						position_render_target(STUNT_RENDER_COLUMN, STUNT_RENDER_ROW);
						struct CARSTATE *car = &state.playerstate;
						car->car_position.ly =
							(base * CSTRK_LEVEL_WORLD_HEIGHT + CSTRK_LEVEL_WORLD_HEIGHT / 2U +
							 STUNT_RENDER_CAR_CLEARANCE) *
							GRASS_RENDER_POSITION_SCALE;
						car->car_previous_position = car->car_position;
						car->car_crashBmpFlag = CRASH_EVENT_WATER;
						custom_camera.distance = STUNT_RENDER_CAMERA_DISTANCE;
						custom_camera.elevation_angle = STUNT_RENDER_CAMERA_ELEVATION;
						custom_camera.azimuth_angle =
							rotation * ANGLE_QUARTER_TURN + ANGLE_EIGHTH_TURN;
						for (legacy_u8 detail = 0; detail < STUNT_RENDER_DETAIL_COUNT; detail++) {
							detail_level = detail ? STUNT_RENDER_LOW_DETAIL : 0;
							activate_render_track(&track, 1);
#ifdef RESTUNTS_WRAP_HILL_SHAPES
							stunt_capture_parts = stunt_capture_details = 0;
							stunt_capture_ground = 0;
							stunt_capture_active = 1;
#endif
							size_t object_size, empty_size;
							legacy_u32 *objects = capture_complete_render(&object_size);
#ifdef RESTUNTS_WRAP_HILL_SHAPES
							stunt_capture_active = 0;
							if (stunt_capture_parts != (1U << STUNT_RENDER_PART_COUNT) - 1U) {
								fprintf(stderr,
										"Sloped stunt render kind %u base %u rotation %u grade %d "
										"surface %u detail %u mode %d captured parts %u\n",
										kind, base, rotation, grade, surface, detail,
										supersight_enabled, stunt_capture_parts);
							}
							assert(stunt_capture_parts == (1U << STUNT_RENDER_PART_COUNT) - 1U);
							assert(stunt_capture_ground == 3U);
							assert(memcmp(&stunt_capture_ground_position,
										  &stunt_capture_object_position,
										  sizeof(stunt_capture_object_position)) == 0);
							captured_details |= stunt_capture_details;
#endif
							activate_render_track(&track, 0);
							legacy_u32 *empty = capture_complete_render(&empty_size);
							assert(object_size == empty_size);
							size_t changed = 0;
							for (size_t pixel = 0; pixel < object_size; pixel++) {
								changed += objects[pixel] != empty[pixel];
							}
							assert(changed > SURFACE_RENDER_MIN_CHANGED_PIXELS);
							free(objects);
							free(empty);
							frames += 2;
						}
#ifdef RESTUNTS_WRAP_HILL_SHAPES
						release_stunt_capture();
#endif
					}
				}
			}
		}
#ifdef RESTUNTS_WRAP_HILL_SHAPES
		/* Retain the renderer's native detail policy while accepting either
		 * authored mesh. Every selected structure and road overlay is checked. */
		assert(captured_details != 0);
#endif
	}
	detail_level = saved_detail;
	extended_track_unload();
	cstrk_free(&track);
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
		frames += check_extended_background_altitude();
		frames += check_rendered_surfaces(&track);
		frames += check_rendered_shore_materials(&track);
		frames += check_ground_grass_color();
		frames += check_classic_hill_rendering();
		frames += check_consecutive_hill_seams();
		frames += check_elevated_multitile_road();
		frames += check_overpass_surfaces();
		frames += check_sloped_stunt_rendering();
	}
	extended_track_unload();
	cstrk_free(&track);
	printf("Extended 90x90 four-level terrain and native hill meshes rendered in %u frames.\n",
		   frames);
	call_exitlist();
	return 0;
}
