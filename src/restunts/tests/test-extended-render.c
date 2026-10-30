/* Reuse the live renderer/resource fixture, including its state/RNG checks.
 * This test compares visible geometry, rather than storing a renderer golden. */
#define stuntsmain legacy_render_replay_fixture_main
#include "test-render-replay.c"
#undef stuntsmain

#include "../c/cstrk.h"
#include "../c/extended_track.h"
#include "../c/extended_geometry.h"
#include "../c/extended_frame.h"
#include "../c/frame_render_internal.h"
#include "../c/skybox.h"
#include "../c/race_graphics.h"
#include "../c/trackdata_layout.h"
#include "../c/shape3d_internal.h"
#include "../c/shape3d_hires.h"
#include "../c/projection.h"
#include "../c/roll_ramp.h"
#include "../c/owoot_road.h"

#define STUNT_RENDER_PART_COUNT 2U
#define STUNT_RENDER_DETAIL_COUNT 2U
#define STUNT_RENDER_AUTHORED_OVERSCAN 2

#define ROLL_RAMP_RENDER_PRECISE_TOLERANCE .00000001
#define ROLL_RAMP_RENDER_TERRAIN_COUNT 4U
#define ROLL_RAMP_RENDER_MIN_PIXELS 64U

#define COMBINED_STUNT_RENDER_PART_COUNT 3U
#define COMBINED_STUNT_RENDER_VARIANT_COUNT 3U
#define COMBINED_STUNT_RENDER_COMBINED_VARIANT 2U
#define COMBINED_STUNT_RENDER_SLOPE_COUNT 2U
#define COMBINED_STUNT_RENDER_ROUNDING_TOLERANCE .5
#define COMBINED_STUNT_RENDER_PRECISE_TOLERANCE .00000001

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

#define FENCE_RENDER_SIDE_COUNT 4U
#define FENCE_RENDER_MODEL_COUNT 2U
#define FENCE_RENDER_DETAIL_COUNT 2U
#define FENCE_RENDER_STRAIGHT_CODE 214U
#define FENCE_RENDER_POSITION_SCALE 64L
#define FENCE_RENDER_CAMERA_CLEARANCE (2 * CSTRK_TILE_WORLD_SIZE)
#define FENCE_RENDER_CAR_CLEARANCE 16
#define FENCE_RENDER_CORNER_OBJECT_ID 53U
#define FENCE_RENDER_CORNER_OBJECT_MASK 11U
#define FENCE_RENDER_CAPTURE 1U
#define FENCE_RENDER_HIDE 2U

static legacy_u8 fence_capture_mode;
static const struct CSTRK_TRACK *fence_capture_track;
static const struct SHAPE3D
	*fence_capture_models[FENCE_RENDER_MODEL_COUNT][FENCE_RENDER_DETAIL_COUNT];
static struct VECTORLONG fence_capture_camera;
static legacy_u8 fence_capture_seen[FENCE_RENDER_SIDE_COUNT][CSTRK_MAX_DIMENSION];

static struct VECTOR fence_rotate_point(struct VECTOR point, legacy_u8 rotation)
{
	legacy_s16 x = point.x;
	switch (rotation) {
		case 1:
			point.x = point.z;
			point.z = -x;
			break;
		case 2:
			point.x = -point.x;
			point.z = -point.z;
			break;
		case 3:
			point.x = -point.z;
			point.z = x;
			break;
	}
	return point;
}

static legacy_u8 fence_matches_model(const struct SHAPE3D *shape, const struct SHAPE3D *source)
{
	if (!source || shape->shape3d_numverts != source->shape3d_numverts ||
		shape->shape3d_numprimitives != source->shape3d_numprimitives ||
		shape->shape3d_numpaints != source->shape3d_numpaints) {
		return 0;
	}
	const legacy_u8 *actual = shape->shape3d_primitives, *expected = source->shape3d_primitives;
	for (legacy_u16 primitive = 0; primitive < source->shape3d_numprimitives; primitive++) {
		size_t size = SHAPE3D_PRIMITIVE_HEADER_SIZE + source->shape3d_numpaints + expected[0];
		if (memcmp(actual, expected, size) != 0) {
			return 0;
		}
		actual += size;
		expected += size;
	}
	return 1;
}

static legacy_s32 fence_expected_edge_height(legacy_s32 x, legacy_s32 z)
{
	const struct CSTRK_TRACK *track = fence_capture_track;
	legacy_s32 stride = track->width + 1U;
	legacy_s32 along, count, first, step;
	if (x == 0 || x == track->width * CSTRK_TILE_WORLD_SIZE) {
		along = track->height * CSTRK_TILE_WORLD_SIZE - z;
		count = track->height;
		first = x == 0 ? 0 : track->width;
		step = stride;
	} else {
		assert(z == 0 || z == track->height * CSTRK_TILE_WORLD_SIZE);
		along = x;
		count = track->width;
		first = z == 0 ? track->height * stride : 0;
		step = 1;
	}
	legacy_s32 cell = along / CSTRK_TILE_WORLD_SIZE;
	if (cell == count) {
		cell--;
	}
	legacy_s32 fraction = along - cell * CSTRK_TILE_WORLD_SIZE;
	legacy_s32 height =
		track->corner_heights[first + cell * step] * (CSTRK_TILE_WORLD_SIZE - fraction) +
		track->corner_heights[first + (cell + 1) * step] * fraction;
	return height * CSTRK_LEVEL_WORLD_HEIGHT / CSTRK_TILE_WORLD_SIZE;
}

static legacy_u8 fence_check_submitted_shape(const struct TRANSFORMEDSHAPE3D *instance)
{
	const struct SHAPE3D *shape = instance->shapeptr;
	const struct CSTRK_TRACK *track = fence_capture_track;
	for (legacy_u8 model = 0; model < FENCE_RENDER_MODEL_COUNT; model++) {
		for (legacy_u8 detail = 0; detail < FENCE_RENDER_DETAIL_COUNT; detail++) {
			const struct SHAPE3D *source = fence_capture_models[model][detail];
			if (!fence_matches_model(shape, source)) {
				continue;
			}
			if (fence_capture_mode == FENCE_RENDER_HIDE) {
				return 1;
			}
			legacy_u8 rotation = 0;
			for (; rotation < FENCE_RENDER_SIDE_COUNT; rotation++) {
				legacy_u8 matching = 1;
				for (legacy_u16 vertex = 0; vertex < shape->shape3d_numverts; vertex++) {
					struct VECTOR point, expected;
					shape3d_vertex_read(shape, vertex, &point);
					shape3d_vertex_read(source, vertex, &expected);
					expected = fence_rotate_point(expected, rotation);
					matching &= point.x == expected.x && point.z == expected.z;
				}
				if (matching) {
					break;
				}
			}
			assert(rotation < FENCE_RENDER_SIDE_COUNT);
			assert(instance->rotvec.x == 0 && instance->rotvec.y == 0 && instance->rotvec.z == 0);
			legacy_s32 origin_x = instance->pos.x + fence_capture_camera.lx;
			legacy_s32 origin_z = instance->pos.z + fence_capture_camera.lz;
			legacy_s32 column = origin_x / CSTRK_TILE_WORLD_SIZE;
			legacy_s32 row = track->height - 1 - origin_z / CSTRK_TILE_WORLD_SIZE;
			assert(column >= 0 && column < track->width && row >= 0 && row < track->height);
			for (legacy_u8 arm = 0; arm <= model; arm++) {
				legacy_u8 side = (rotation + arm) % FENCE_RENDER_SIDE_COUNT;
				assert(side == 0   ? row == 0
					   : side == 1 ? column == (legacy_s32)track->width - 1
					   : side == 2 ? row == (legacy_s32)track->height - 1
								   : column == 0);
				legacy_s32 segment = (side & 1U) ? row : column;
				assert(++fence_capture_seen[side][segment] == 1);
			}
			legacy_u16 checked = 0;
			for (legacy_u16 vertex = 0; vertex < shape->shape3d_numverts; vertex++) {
				struct VECTOR point, expected;
				shape3d_vertex_read(shape, vertex, &point);
				shape3d_vertex_read(source, vertex, &expected);
				legacy_s32 x = origin_x + point.x, z = origin_z + point.z;
				if (x == 0 || x == track->width * CSTRK_TILE_WORLD_SIZE || z == 0 ||
					z == track->height * CSTRK_TILE_WORLD_SIZE) {
					assert(point.y + instance->pos.y + fence_capture_camera.ly ==
						   expected.y + fence_expected_edge_height(x, z));
					checked++;
				}
			}
			assert(checked > 0);
			return 1;
		}
	}
	return 0;
}

static legacy_u8 seam_capture_hill_shapes;
static legacy_u32 seam_captured_road_parents, seam_captured_marking_vertices;

#define PLANT_RENDER_PALM_ID 151U
#define PLANT_RENDER_CACTUS_ID 152U
#define PLANT_RENDER_PINE_ID 153U
#define PLANT_RENDER_DIRECTION_COUNT 4U
#define PLANT_RENDER_SLOPE_COUNT 5U
#define PLANT_RENDER_ROUNDING_TOLERANCE .5
#define PLANT_RENDER_PRECISE_TOLERANCE .00000001

static legacy_u8 roll_ramp_capture_active, roll_ramp_capture_calls;
static struct CSTRK_OBJECT roll_ramp_capture_object;
static struct SHAPE3D roll_ramp_capture_source;

static void check_roll_ramp_submission(const struct TRANSFORMEDSHAPE3D *instance,
									   const struct SHAPE3D_HIRES_VECTOR *vertices)
{
	const struct CSTRK_OBJECT *object = &roll_ramp_capture_object;
	const struct SHAPE3D *mesh = instance->shapeptr;
	assert(mesh->shape3d_numverts == roll_ramp_capture_source.shape3d_numverts);
	assert(mesh->shape3d_numprimitives == roll_ramp_capture_source.shape3d_numprimitives);
	assert(instance->rotvec.z == 0);
	struct VECTOR origin;
	extended_geometry_object_origin(extended_track_model(), object, &origin);
	assert(memcmp(&instance->pos, &origin, sizeof(origin)) == 0);
	for (legacy_u16 vertex = 0; vertex < mesh->shape3d_numverts; vertex++) {
		struct VECTOR source, packed;
		shape3d_vertex_read(&roll_ramp_capture_source, vertex, &source);
		shape3d_vertex_read(mesh, vertex, &packed);
		legacy_s16 x = source.x, z = source.z;
		switch (roll_ramp_orientation(object)) {
			case ANGLE_QUARTER_TURN:
				source.x = z;
				source.z = -x;
				break;
			case ANGLE_HALF_TURN:
				source.x = -x;
				source.z = -z;
				break;
			case ANGLE_THREE_QUARTER_TURN:
				source.x = -z;
				source.z = x;
				break;
		}
		legacy_f64 grade = ((legacy_f64)object->slope_x *
								(object->width * (CSTRK_TILE_WORLD_SIZE / 2) + source.x) +
							(legacy_f64)object->slope_y *
								(object->height * (CSTRK_TILE_WORLD_SIZE / 2) - source.z)) *
						   CSTRK_LEVEL_WORLD_HEIGHT / CSTRK_TILE_WORLD_SIZE;
		assert(packed.x == source.x && packed.z == source.z);
		legacy_f64 rounding = packed.y - source.y - grade;
		assert(rounding >= -.5 && rounding <= .5);
		if (vertices) {
			assert(vertices[vertex].x == source.x && vertices[vertex].z == source.z);
			legacy_f64 error = vertices[vertex].y - source.y - grade;
			assert(error >= -ROLL_RAMP_RENDER_PRECISE_TOLERANCE &&
				   error <= ROLL_RAMP_RENDER_PRECISE_TOLERANCE);
		}
	}
	const legacy_u8 *actual = mesh->shape3d_primitives;
	const legacy_u8 *expected = roll_ramp_capture_source.shape3d_primitives;
	for (legacy_u16 face = 0; face < mesh->shape3d_numprimitives; face++) {
		legacy_u16 bytes = SHAPE3D_PRIMITIVE_HEADER_SIZE + 1U + expected[0];
		assert(memcmp(actual, expected, bytes) == 0);
		actual += bytes;
		expected += bytes;
	}
	roll_ramp_capture_calls++;
}

static legacy_u8 plant_capture_active, plant_capture_calls, plant_capture_precise;
static struct CSTRK_OBJECT plant_capture_object;
static struct SHAPE3D plant_capture_source;
static legacy_f64 plant_capture_lift;

/* Check the renderer submission: rigid upper geometry, roots on the terrain,
 * and a trunk at least as tall as its flat-ground model on the uphill side. */
static void plant_check_vertices(const struct TRANSFORMEDSHAPE3D *instance,
								 const struct SHAPE3D_HIRES_VECTOR *vertices)
{
	const struct SHAPE3D *mesh = instance->shapeptr;
	assert(mesh->shape3d_numverts == plant_capture_source.shape3d_numverts);
	assert(instance->rotvec.z == 0);
	legacy_u8 sloped = plant_capture_object.slope_x || plant_capture_object.slope_y;
	legacy_u8 upright = supersight_enabled && sloped;
	legacy_u16 roots = 0, upper = 0;
	legacy_f64 tolerance =
		vertices ? PLANT_RENDER_PRECISE_TOLERANCE : PLANT_RENDER_ROUNDING_TOLERANCE;
	legacy_f64 lift = 0;
	for (legacy_u16 vertex = 0; vertex < mesh->shape3d_numverts; vertex++) {
		struct VECTOR source, packed;
		shape3d_vertex_read(&plant_capture_source, vertex, &source);
		shape3d_vertex_read(mesh, vertex, &packed);
		legacy_f64 x = vertices ? vertices[vertex].x : packed.x;
		legacy_f64 y = vertices ? vertices[vertex].y : packed.y;
		legacy_f64 z = vertices ? vertices[vertex].z : packed.z;
		assert(x == source.x && z == source.z);
		/* This is the supporting plane in absolute world height, independent
		 * of the render transform's lowest-corner origin. */
		legacy_f64 ground = plant_capture_object.base_height * CSTRK_LEVEL_WORLD_HEIGHT +
							(plant_capture_object.slope_x * (CSTRK_TILE_WORLD_SIZE / 2 + x) +
							 plant_capture_object.slope_y * (CSTRK_TILE_WORLD_SIZE / 2 - z)) *
								CSTRK_LEVEL_WORLD_HEIGHT / CSTRK_TILE_WORLD_SIZE;
		legacy_f64 relative_ground = ground - instance->pos.y;
		if (upright && source.y > 0) {
			legacy_f64 offset = y - source.y;
			if (!upper) {
				lift = offset;
			}
			assert(offset >= lift - tolerance && offset <= lift + tolerance);
			assert(y - relative_ground >= source.y - tolerance);
			upper++;
		} else {
			legacy_f64 error = y - source.y - relative_ground;
			assert(error >= -tolerance && error <= tolerance);
			roots += source.y == 0;
		}
		if (vertices) {
			legacy_f64 error = y - packed.y;
			assert(error >= -PLANT_RENDER_ROUNDING_TOLERANCE &&
				   error <= PLANT_RENDER_ROUNDING_TOLERANCE);
		}
	}
	assert(roots > 0);
	if (upright) {
		assert(upper > 0);
		plant_capture_lift = lift;
	}
}

static void plant_check_submission(const struct TRANSFORMEDSHAPE3D *instance,
								   const struct SHAPE3D_HIRES_VECTOR *vertices)
{
	plant_check_vertices(instance, NULL);
	if (vertices) {
		plant_check_vertices(instance, vertices);
	}
	plant_capture_precise = vertices != NULL;
	plant_capture_calls++;
}

static legacy_u8 combined_stunt_capture_active, combined_stunt_capture_parts;
static legacy_u8 combined_stunt_capture_calls, combined_stunt_capture_precise;
static struct CSTRK_OBJECT combined_stunt_capture_object;
static struct SHAPE3D combined_stunt_capture_meshes[COMBINED_STUNT_RENDER_PART_COUNT];
static struct VECTOR combined_stunt_capture_position;

static void combined_stunt_check_submission(const struct TRANSFORMEDSHAPE3D *instance,
											const struct SHAPE3D_HIRES_VECTOR *vertices)
{
	const struct SHAPE3D *mesh = instance->shapeptr;
	legacy_u8 matched = COMBINED_STUNT_RENDER_PART_COUNT;
	for (legacy_u8 part = 0; part < COMBINED_STUNT_RENDER_PART_COUNT; part++) {
		const struct SHAPE3D *source = &combined_stunt_capture_meshes[part];
		if (source->shape3d_numverts != mesh->shape3d_numverts ||
			source->shape3d_numprimitives != mesh->shape3d_numprimitives) {
			continue;
		}
		assert(source->shape3d_numpaints == 1 && mesh->shape3d_numpaints == 1);
		const legacy_u8 *primitive = source->shape3d_primitives;
		size_t bytes = 0;
		for (legacy_u16 face = 0; face < source->shape3d_numprimitives; face++) {
			legacy_u16 count = primitive[0];
			assert(count >= SHAPE3D_PRIMITIVE_POINT && count <= SHAPE3D_PRIMITIVE_POLYGON_LAST);
			size_t size = SHAPE3D_PRIMITIVE_HEADER_SIZE + 1U + count;
			bytes += size;
			primitive += size;
		}
		if (memcmp(source->shape3d_primitives, mesh->shape3d_primitives, bytes) == 0) {
			matched = part;
			break;
		}
	}
	assert(matched < COMBINED_STUNT_RENDER_PART_COUNT);
	assert((combined_stunt_capture_parts & (1U << matched)) == 0);
	combined_stunt_capture_parts |= 1U << matched;
	assert(instance->rotvec.x == 0 && instance->rotvec.y == 0 && instance->rotvec.z == 0);
	if (combined_stunt_capture_calls) {
		assert(memcmp(&combined_stunt_capture_position, &instance->pos, sizeof(instance->pos)) ==
			   0);
	} else {
		combined_stunt_capture_position = instance->pos;
	}
	const struct CSTRK_OBJECT *object = &combined_stunt_capture_object;
	const struct SHAPE3D *source = &combined_stunt_capture_meshes[matched];
	for (legacy_u16 vertex = 0; vertex < mesh->shape3d_numverts; vertex++) {
		struct VECTOR authored, packed;
		shape3d_vertex_read(source, vertex, &authored);
		shape3d_vertex_read(mesh, vertex, &packed);
		legacy_s16 *along = object->slope_x ? &authored.x : &authored.z;
		if (object->slope_x || object->slope_y) {
			legacy_s16 half = CSTRK_TILE_WORLD_SIZE / 2;
			if (*along > half && *along <= half + STUNT_RENDER_AUTHORED_OVERSCAN) {
				*along = half;
			} else if (*along < -half && *along >= -half - STUNT_RENDER_AUTHORED_OVERSCAN) {
				*along = -half;
			}
		}
		assert(packed.x == authored.x && packed.z == authored.z);
		/* All three authored parts must retain their height above the same
		 * world support plane, including the roof and the two barriers. */
		legacy_f64 ground = object->base_height * CSTRK_LEVEL_WORLD_HEIGHT +
							(object->slope_x * (CSTRK_TILE_WORLD_SIZE / 2 + (legacy_f64)packed.x) +
							 object->slope_y * (CSTRK_TILE_WORLD_SIZE / 2 - (legacy_f64)packed.z)) *
								CSTRK_LEVEL_WORLD_HEIGHT / CSTRK_TILE_WORLD_SIZE;
		legacy_f64 expected = authored.y + ground - instance->pos.y;
		legacy_f64 error = packed.y - expected;
		assert(error >= -COMBINED_STUNT_RENDER_ROUNDING_TOLERANCE &&
			   error <= COMBINED_STUNT_RENDER_ROUNDING_TOLERANCE);
		if (vertices) {
			assert(vertices[vertex].x == packed.x && vertices[vertex].z == packed.z);
			error = vertices[vertex].y - expected;
			assert(error >= -COMBINED_STUNT_RENDER_PRECISE_TOLERANCE &&
				   error <= COMBINED_STUNT_RENDER_PRECISE_TOLERANCE);
		}
	}
	combined_stunt_capture_calls++;
	combined_stunt_capture_precise += vertices != NULL;
}

static legacy_u8 stunt_capture_active, stunt_capture_parts, stunt_capture_details;
static legacy_u8 slope_occlusion_hide_terrain;
static legacy_u8 terrain_coverage_capture;
static legacy_f64 terrain_coverage_area;
#define TERRAIN_EDGE_ENDPOINT_COUNT 2U
static legacy_u8 terrain_edge_capture;
static legacy_u16 terrain_flat_count, terrain_base_count;
static struct SHAPE3D_HIRES_VECTOR terrain_flat_vertices[SHAPE3D_PRIMITIVE_POLYGON_FIRST];
static struct SHAPE3D_HIRES_VECTOR terrain_base_vertices[TERRAIN_EDGE_ENDPOINT_COUNT];

#define GENERATED_BOUNDS_PREFIX_COUNT 8U
#define GENERATED_BOUNDS_MATERIAL 101U
#define GENERATED_BOUNDS_ALWAYS_VISIBLE 1U
#define GENERATED_BOUNDS_MIN_VISIBLE_PIXELS 100U
#define GENERATED_BOUNDS_TARGET_TILE 15U
#define GENERATED_BOUNDS_ROAD_OFFSET 3U

enum GENERATED_BOUNDS_MODE {
	GENERATED_BOUNDS_DISABLED,
	GENERATED_BOUNDS_EMPTY,
	GENERATED_BOUNDS_TRIANGLES,
	GENERATED_BOUNDS_POLYGON
};
static legacy_u8 generated_bounds_mode, generated_bounds_count, generated_bounds_injected;
static void inject_generated_bounds_polygon(void);

static legacy_u8 generated_bounds_skip_scene(void)
{
	if (!generated_bounds_mode) {
		return 0;
	}
	if (!generated_bounds_injected) {
		generated_bounds_injected = 1;
		if (generated_bounds_mode != GENERATED_BOUNDS_EMPTY) {
			inject_generated_bounds_polygon();
		}
	}
	return 1;
}

static legacy_u8 native_slope_capture_active, native_slope_capture_parts;
static legacy_u16 native_slope_capture_markings;
static const struct TRACKOBJECT *native_slope_capture_definitions[STUNT_RENDER_PART_COUNT];
static struct SHAPE3D stunt_capture_meshes[STUNT_RENDER_PART_COUNT][STUNT_RENDER_DETAIL_COUNT];
static struct CSTRK_OBJECT stunt_capture_object;
static legacy_u8 stunt_capture_ground;
static struct VECTOR stunt_capture_ground_position, stunt_capture_object_position;

static void stunt_check_supporting_ground(const struct TRANSFORMEDSHAPE3D *instance)
{
	const struct SHAPE3D *shape = instance->shapeptr;
	const legacy_u8 *primitive = shape->shape3d_primitives;
	if (shape->shape3d_numprimitives != 1 || primitive[0] < SHAPE3D_PRIMITIVE_POLYGON_FIRST ||
		primitive[0] > SHAPE3D_POLYGON_MAX_VERTICES) {
		return;
	}
	const legacy_u8 *indices = primitive + SHAPE3D_PRIMITIVE_HEADER_SIZE + shape->shape3d_numpaints;
	legacy_s16 minimum = CSTRK_TILE_WORLD_SIZE, maximum = -CSTRK_TILE_WORLD_SIZE;
	legacy_s16 cross_minimum = minimum, cross_maximum = maximum;
	for (legacy_u8 vertex = 0; vertex < primitive[0]; vertex++) {
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
		if (along < -CSTRK_TILE_WORLD_SIZE / 2 || along > CSTRK_TILE_WORLD_SIZE / 2 ||
			abs(point.y - expected_height) > 1) {
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
	if (maximum > minimum) {
		if (cross_maximum <= -ROAD_HALF_WIDTH && cross_minimum <= -CSTRK_TILE_WORLD_SIZE / 2) {
			stunt_capture_ground |= 1U;
			stunt_capture_ground_position = instance->pos;
		} else if (cross_minimum >= ROAD_HALF_WIDTH && cross_maximum >= CSTRK_TILE_WORLD_SIZE / 2) {
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

#define SLOPE_OCCLUSION_GROUND_MATERIAL 16U
#define SLOPE_OCCLUSION_HILL_MATERIAL_FIRST 101U
#define SLOPE_OCCLUSION_HILL_MATERIAL_LAST 105U

static legacy_u8 slope_occlusion_is_terrain(const struct SHAPE3D *shape)
{
	if (shape->shape3d_numprimitives != 1U) {
		return 0;
	}
	const legacy_u8 *primitive = shape->shape3d_primitives;
	if (primitive[0] < SHAPE3D_PRIMITIVE_POLYGON_FIRST ||
		primitive[0] > SHAPE3D_POLYGON_MAX_VERTICES) {
		return 0;
	}
	legacy_u8 material = primitive[SHAPE3D_PRIMITIVE_HEADER_SIZE];
	return material == SLOPE_OCCLUSION_GROUND_MATERIAL ||
		   (material >= SLOPE_OCCLUSION_HILL_MATERIAL_FIRST &&
			material <= SLOPE_OCCLUSION_HILL_MATERIAL_LAST);
}

static legacy_u8 native_slope_marking_vertex(const struct SHAPE3D *shape, legacy_u16 vertex)
{
	const legacy_u8 *primitive = shape->shape3d_primitives;
	for (legacy_u16 face = 0; face < shape->shape3d_numprimitives; face++) {
		legacy_u16 count = primitive[0];
		if (count < SHAPE3D_PRIMITIVE_POLYGON_FIRST || count > SHAPE3D_PRIMITIVE_POLYGON_LAST) {
			break;
		}
		const legacy_u8 *indices =
			primitive + SHAPE3D_PRIMITIVE_HEADER_SIZE + shape->shape3d_numpaints;
		if ((primitive[1] & SHAPE3D_PRIMITIVE_SKIP_DEPTH_SORT_FLAG) &&
			primitive[SHAPE3D_PRIMITIVE_HEADER_SIZE] == CLASSIC_RENDER_MARKING_MATERIAL) {
			for (legacy_u16 point = 0; point < count; point++) {
				if (indices[point] == vertex) {
					return 1;
				}
			}
		}
		primitive = indices + count;
	}
	return 0;
}

static legacy_u8 render_capture_skip_shape(struct TRANSFORMEDSHAPE3D *instance)
{
	if (slope_occlusion_hide_terrain && slope_occlusion_is_terrain(instance->shapeptr)) {
		return 1;
	}
	if (native_slope_capture_active) {
		for (legacy_u8 part = 0; part < STUNT_RENDER_PART_COUNT; part++) {
			const struct TRACKOBJECT *definition = native_slope_capture_definitions[part];
			if (!definition) {
				continue;
			}
			const struct SHAPE3D *sources[] = {definition->ss_shapePtr, definition->ss_loShapePtr};
			for (legacy_u8 detail = 0; detail < STUNT_RENDER_DETAIL_COUNT; detail++) {
				const struct SHAPE3D *source = sources[detail];
				const struct SHAPE3D *actual = instance->shapeptr;
				if (!source || actual->shape3d_primitives != source->shape3d_primitives ||
					actual->shape3d_numverts != source->shape3d_numverts) {
					continue;
				}
				assert(instance->rotvec.z == definition->ss_rotY);
				for (legacy_u16 vertex = 0; vertex < source->shape3d_numverts; vertex++) {
					struct VECTOR target, point;
					shape3d_vertex_read(source, vertex, &target);
					shape3d_vertex_read(actual, vertex, &point);
					assert(abs(point.x - target.x) <= STUNT_RENDER_AUTHORED_OVERSCAN);
					assert(abs(point.z - target.z) <= STUNT_RENDER_AUTHORED_OVERSCAN);
					if (native_slope_marking_vertex(source, vertex)) {
						struct MATRIX yaw;
						struct VECTOR world;
						mat_rot_y(&yaw, definition->ss_rotY);
						mat_mul_vector(&point, &yaw, &world);
						/* This north-facing native slope rises exactly one level
						 * over its grid span. Dashes are draped onto that plane,
						 * correcting known errors in their authored Y values. */
						legacy_s32 height =
							(world.z + CSTRK_TILE_WORLD_SIZE / 2) * CSTRK_LEVEL_WORLD_HEIGHT;
						legacy_s32 error = world.y * CSTRK_TILE_WORLD_SIZE - height;
						assert(abs(error) * 2 <= CSTRK_TILE_WORLD_SIZE);
						native_slope_capture_markings++;
					} else {
						assert(point.y == target.y);
					}
				}
				native_slope_capture_parts |= 1U << part;
			}
		}
	}
	if (fence_capture_mode && fence_check_submitted_shape(instance) &&
		fence_capture_mode == FENCE_RENDER_HIDE) {
		return 1;
	}
	if (seam_capture_hill_shapes) {
		seam_check_submitted_road(instance->shapeptr);
	}
	if (stunt_capture_active) {
		stunt_check_supporting_ground(instance);
		stunt_check_submitted_shape(instance);
	}
	return 0;
}

/* Measure the submitted terrain rather than pixels hidden behind a floating
 * road. Area in XZ is invariant under the terrain's quarter-turn yaw. */
static void capture_terrain_coverage(const struct TRANSFORMEDSHAPE3D *instance,
									 const struct SHAPE3D_HIRES_VECTOR *vertices)
{
	const struct SHAPE3D *shape = instance->shapeptr;
	const legacy_u8 *primitive = shape->shape3d_primitives;
	for (legacy_u16 face = 0; face < shape->shape3d_numprimitives; face++) {
		legacy_u16 count = primitive[0];
		if (count < SHAPE3D_PRIMITIVE_POLYGON_FIRST || count > SHAPE3D_POLYGON_MAX_VERTICES) {
			break;
		}
		const legacy_u8 *indices =
			primitive + SHAPE3D_PRIMITIVE_HEADER_SIZE + shape->shape3d_numpaints;
		struct SHAPE3D_HIRES_VECTOR points[SHAPE3D_POLYGON_MAX_VERTICES];
		legacy_f64 minimum = 0, maximum = 0;
		for (legacy_u16 vertex = 0; vertex < count; vertex++) {
			struct SHAPE3D_HIRES_VECTOR point;
			if (vertices) {
				point = vertices[indices[vertex]];
			} else {
				struct VECTOR packed;
				shape3d_vertex_read(shape, indices[vertex], &packed);
				point = (struct SHAPE3D_HIRES_VECTOR){packed.x, packed.y, packed.z};
			}
			legacy_f64 previous_x = point.x;
			switch (instance->rotvec.z & ANGLE_MASK) {
				case ANGLE_QUARTER_TURN:
					point.x = point.z;
					point.z = -previous_x;
					break;
				case ANGLE_HALF_TURN:
					point.x = -point.x;
					point.z = -point.z;
					break;
				case ANGLE_THREE_QUARTER_TURN:
					point.x = -point.z;
					point.z = previous_x;
					break;
			}
			point.x += instance->pos.x;
			point.y += instance->pos.y;
			point.z += instance->pos.z;
			points[vertex] = point;
			if (!vertex || point.y < minimum) {
				minimum = point.y;
			}
			if (!vertex || point.y > maximum) {
				maximum = point.y;
			}
		}
		legacy_f64 area = 0;
		for (legacy_u16 vertex = 0; vertex < count; vertex++) {
			const struct SHAPE3D_HIRES_VECTOR *a = &points[vertex];
			const struct SHAPE3D_HIRES_VECTOR *b = &points[(vertex + 1U) % count];
			area += a->x * b->z - b->x * a->z;
		}
		terrain_coverage_area += area < 0 ? -area : area;
		if (terrain_edge_capture) {
			if (minimum == maximum) {
				assert(count == SHAPE3D_PRIMITIVE_POLYGON_FIRST);
				assert(!terrain_flat_count);
				memcpy(terrain_flat_vertices, points, sizeof(terrain_flat_vertices));
				terrain_flat_count = count;
			} else {
				for (legacy_u16 vertex = 0; vertex < count; vertex++) {
					if (points[vertex].y == minimum) {
						assert(terrain_base_count < TERRAIN_EDGE_ENDPOINT_COUNT);
						terrain_base_vertices[terrain_base_count++] = points[vertex];
					}
				}
			}
		}
		primitive = indices + count;
	}
}

legacy_u16 __real_shape3d_transform_and_queue(struct TRANSFORMEDSHAPE3D *instance);
legacy_u16 __wrap_shape3d_transform_and_queue(struct TRANSFORMEDSHAPE3D *instance)
{
	if (roll_ramp_capture_active) {
		check_roll_ramp_submission(instance, NULL);
		return 0;
	}
	if (combined_stunt_capture_active) {
		combined_stunt_check_submission(instance, NULL);
		return 0;
	}
	if (plant_capture_active) {
		plant_check_submission(instance, NULL);
		return 0;
	}
	if (terrain_coverage_capture) {
		capture_terrain_coverage(instance, NULL);
		return 0;
	}
	if (generated_bounds_skip_scene()) {
		return 0;
	}
	if (render_capture_skip_shape(instance)) {
		return 0;
	}
	return __real_shape3d_transform_and_queue(instance);
}

legacy_u16 __real_shape3d_hires_transform_and_queue(struct TRANSFORMEDSHAPE3D *instance,
													const struct SHAPE3D_HIRES_VECTOR *vertices);
static void queue_generated_bounds_face(const struct SHAPE3D_HIRES_VECTOR *vertices,
										legacy_u16 count)
{
	legacy_u8 bytes[SHAPE3D_POLYGON_MAX_VERTICES * SHAPE3D_VERTEX_SIZE];
	legacy_u8 primitive[SHAPE3D_POLYGON_MAX_VERTICES + 5U] = {0};
	legacy_u8 masks[SHAPE3D_VISIBILITY_MASK_SIZE] = {255, 255, 255, 255};
	legacy_u8 front[SHAPE3D_VISIBILITY_MASK_SIZE] = {0};
	struct SHAPE3D shape = {count, bytes, 1, 1, primitive, masks, front};
	primitive[0] = (legacy_u8)count;
	primitive[1] = GENERATED_BOUNDS_ALWAYS_VISIBLE;
	primitive[2] = GENERATED_BOUNDS_MATERIAL;
	for (legacy_u16 vertex = 0; vertex < count; vertex++) {
		struct VECTOR fallback = {(legacy_s16)vertices[vertex].x, (legacy_s16)vertices[vertex].y,
								  (legacy_s16)vertices[vertex].z};
		shape3d_vertex_write(&shape, vertex, &fallback);
		primitive[SHAPE3D_PRIMITIVE_HEADER_SIZE + 1U + vertex] = (legacy_u8)vertex;
	}
	struct TRANSFORMEDSHAPE3D instance = {0};
	instance.shapeptr = &shape;
	instance.rectptr = &frame_sorted_shapes_rect;
	instance.culling_distance = FRAME_DEFAULT_TRANSFORM_DISTANCE;
	instance.ts_flags = FRAME_TRANSFORM_FLAGS_DEFAULT | SHAPE3D_NO_SHADOW_RECEIVE_FLAG;
	(void)__real_shape3d_hires_transform_and_queue(&instance, vertices);
}

static void inject_generated_bounds_polygon(void)
{
	/* A clipped terrain remainder can list eight invisible boundary vertices
	 * before its visible corner. These are polygon corners, not a resource's
	 * eight enclosing box corners. Keep a fractional displacement as well. */
	static const struct SHAPE3D_HIRES_VECTOR view[SHAPE3D_POLYGON_MAX_VERTICES] = {
		{-2000.125, -100, 1000}, {-2300.125, -93, 1000}, {-2500.125, -67, 1000},
		{-2600.125, -27, 1000},	 {-2600.125, 27, 1000},	 {-2500.125, 67, 1000},
		{-2300.125, 93, 1000},	 {-2000.125, 100, 1000}, {300.125, 100, 1000},
		{300.125, -100, 1000}};
	struct SHAPE3D_HIRES_VECTOR vertices[SHAPE3D_POLYGON_MAX_VERTICES];
	for (legacy_u16 vertex = 0; vertex < generated_bounds_count; vertex++) {
		if (vertex < GENERATED_BOUNDS_PREFIX_COUNT) {
			assert(shape3d_hires_clip_flags(&view[vertex]) & SHAPE3D_RECT_CLIP_LEFT);
		} else {
			assert(shape3d_hires_clip_flags(&view[vertex]) == 0);
		}
		/* Transform the camera-space fixture back to camera-relative world
		 * coordinates. Both polygon and triangle reference use this same map. */
		vertices[vertex] = (struct SHAPE3D_HIRES_VECTOR){
			(view[vertex].x * mat_temp.m._11 + view[vertex].y * mat_temp.m._21 +
			 view[vertex].z * mat_temp.m._31) /
				TRIG_FIXED_ONE,
			(view[vertex].x * mat_temp.m._12 + view[vertex].y * mat_temp.m._22 +
			 view[vertex].z * mat_temp.m._32) /
				TRIG_FIXED_ONE,
			(view[vertex].x * mat_temp.m._13 + view[vertex].y * mat_temp.m._23 +
			 view[vertex].z * mat_temp.m._33) /
				TRIG_FIXED_ONE};
	}
	if (generated_bounds_mode == GENERATED_BOUNDS_TRIANGLES) {
		for (legacy_u16 vertex = 1; vertex + 1U < generated_bounds_count; vertex++) {
			const struct SHAPE3D_HIRES_VECTOR triangle[] = {vertices[0], vertices[vertex],
															vertices[vertex + 1U]};
			queue_generated_bounds_face(triangle, SHAPE3D_PRIMITIVE_POLYGON_FIRST);
		}
	} else {
		queue_generated_bounds_face(vertices, generated_bounds_count);
	}
}

legacy_u16 __wrap_shape3d_hires_transform_and_queue(struct TRANSFORMEDSHAPE3D *instance,
													const struct SHAPE3D_HIRES_VECTOR *vertices)
{
	if (roll_ramp_capture_active) {
		check_roll_ramp_submission(instance, vertices);
		return 0;
	}
	if (combined_stunt_capture_active) {
		combined_stunt_check_submission(instance, vertices);
		return 0;
	}
	if (plant_capture_active) {
		plant_check_submission(instance, vertices);
		return 0;
	}
	if (terrain_coverage_capture) {
		capture_terrain_coverage(instance, vertices);
		return 0;
	}
	if (generated_bounds_skip_scene()) {
		return 0;
	}
	if (render_capture_skip_shape(instance)) {
		return 0;
	}
	return __real_shape3d_hires_transform_and_queue(instance, vertices);
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
		if (part && !definition->ss_ssOvelay) {
			break;
		}
		const struct TRACKOBJECT *render_definition =
			part ? frame_track_object_from_legacy_index(definition->ss_ssOvelay) : definition;
		const struct SHAPE3D *sources[STUNT_RENDER_DETAIL_COUNT] = {
			render_definition->ss_shapePtr, render_definition->ss_loShapePtr
												? render_definition->ss_loShapePtr
												: render_definition->ss_shapePtr};
		for (legacy_u8 detail = 0; detail < STUNT_RENDER_DETAIL_COUNT; detail++) {
			struct SHAPE3D *mesh = &stunt_capture_meshes[part][detail];
			legacy_u8 surface = part && definition->ss_physicalModel == PHYSICAL_MODEL_OVERPASS
									? object->lower_surface
									: object->surface;
			assert(extended_geometry_transform_shape(sources[detail], mesh,
													 render_definition->ss_rotY, surface,
													 definition->ss_physicalModel));
			for (legacy_u16 vertex = 0; vertex < mesh->shape3d_numverts; vertex++) {
				struct VECTOR point;
				shape3d_vertex_read(mesh, vertex, &point);
				legacy_s16 along = object->slope_x ? point.x : point.z;
				legacy_s16 half_span = (object->slope_x ? object->width : object->height) *
									   (CSTRK_TILE_WORLD_SIZE / 2);
				if (along < -half_span && along >= -half_span - STUNT_RENDER_AUTHORED_OVERSCAN) {
					along = -half_span;
				} else if (along > half_span &&
						   along <= half_span + STUNT_RENDER_AUTHORED_OVERSCAN) {
					along = half_span;
				}
				if (object->slope_x) {
					point.x = along;
				} else {
					point.z = along;
				}
				/* One aligned tile climbs 450 units over 1024 world units. Its
				 * roof and barriers must retain every authored vertical offset. */
				legacy_s32 distance =
					object->slope_x * (object->width * (CSTRK_TILE_WORLD_SIZE / 2) + point.x) +
					object->slope_y * (object->height * (CSTRK_TILE_WORLD_SIZE / 2) - point.z);
				if (object->slope_x < 0) {
					distance -= object->slope_x * object->width * CSTRK_TILE_WORLD_SIZE;
				}
				if (object->slope_y < 0) {
					distance -= object->slope_y * object->height * CSTRK_TILE_WORLD_SIZE;
				}
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

#ifdef RESTUNTS_WRAP_HILL_SHAPES
static legacy_u16 check_extended_fences(void)
{
	static const legacy_u16 sizes[][2] = {{1, 1}, {1, 7}, {9, 1}, {12, 8}, {90, 70}};
	static const legacy_u8 views[][2] = {{0, 0}, {2, 0}, {2, 2}, {0, 2},
										 {1, 0}, {2, 1}, {1, 2}, {0, 1}};
	struct GAMESTATE saved_state = state;
	legacy_s16 saved_camera = cameramode;
	legacy_s8 saved_detail = detail_level;
	cameramode = CAMERA_MODE_FOLLOW;
	detail_level = 0;
	for (legacy_u8 model = 0; model < FENCE_RENDER_MODEL_COUNT; model++) {
		const struct TRACKOBJECT *object =
			frame_track_object_from_legacy_index(FENCE_RENDER_STRAIGHT_CODE + model);
		fence_capture_models[model][0] = object->ss_shapePtr;
		fence_capture_models[model][1] = object->ss_loShapePtr;
	}
	legacy_u16 frames = 0;
	for (size_t size = 0; size < sizeof(sizes) / sizeof(sizes[0]); size++) {
		struct CSTRK_TRACK track;
		legacy_char error[GRASS_RENDER_ERROR_CAPACITY];
		cstrk_init(&track);
		assert(cstrk_create(&track, sizes[size][0], sizes[size][1], error, sizeof(error)));
		track.horizon = CSTRK_HORIZON_COUNTRY;
		for (legacy_u16 y = 0; y <= track.height; y++) {
			for (legacy_u16 x = 0; x <= track.width; x++) {
				legacy_u16 level = x > y ? x : y;
				track.corner_heights[y * (track.width + 1U) + x] =
					size == 3U || level > (legacy_s16)CSTRK_MAX_HEIGHT
						? (legacy_s16)CSTRK_MAX_HEIGHT
						: level;
			}
		}
		if (size == 3U) {
			/* A corner object covers several perimeter cells, including continuations. */
			struct CSTRK_OBJECT object = {0};
			object.legacy_id = FENCE_RENDER_CORNER_OBJECT_ID;
			object.x = track.width - 2U;
			object.y = track.height - 2U;
			object.width = object.height = 2U;
			object.occupied_mask = FENCE_RENDER_CORNER_OBJECT_MASK;
			object.base_height = CSTRK_MAX_HEIGHT;
			assert(cstrk_add_object(&track, &object, error, sizeof(error)));
		}
		assert(extended_track_activate(&track, error, sizeof(error)));
		fence_capture_track = &track;
		for (size_t view = 0; view < sizeof(views) / sizeof(views[0]); view++) {
			legacy_u16 column = views[view][0] * (track.width - 1U) / 2U;
			legacy_u16 row = views[view][1] * (track.height - 1U) / 2U;
			fence_capture_camera.lx = column * CSTRK_TILE_WORLD_SIZE + CSTRK_TILE_WORLD_SIZE / 2;
			fence_capture_camera.lz =
				(track.height - row) * CSTRK_TILE_WORLD_SIZE - CSTRK_TILE_WORLD_SIZE / 2;
			fence_capture_camera.ly =
				CSTRK_MAX_HEIGHT * CSTRK_LEVEL_WORLD_HEIGHT + FENCE_RENDER_CAMERA_CLEARANCE;
			state.game_follow_camera_position[0] =
				(struct VECTOR){LEGACY_S16_FROM_BITS((legacy_u16)fence_capture_camera.lx),
								(legacy_s16)fence_capture_camera.ly,
								LEGACY_S16_FROM_BITS((legacy_u16)fence_capture_camera.lz)};
			struct CARSTATE *car = &state.playerstate;
			car->car_position =
				(struct VECTORLONG){fence_capture_camera.lx * FENCE_RENDER_POSITION_SCALE,
									FENCE_RENDER_CAR_CLEARANCE * FENCE_RENDER_POSITION_SCALE,
									fence_capture_camera.lz * FENCE_RENDER_POSITION_SCALE};
			car->car_previous_position = car->car_position;
			car->car_rotate = (struct VECTOR){0};
			car->car_crashBmpFlag = CRASH_EVENT_NONE;
			state.game_particles_active = 0;
			memset(fence_capture_seen, 0, sizeof(fence_capture_seen));
			fence_capture_mode = FENCE_RENDER_CAPTURE;
			size_t pixel_count;
			legacy_u32 *visible = capture_complete_render(&pixel_count);
			assert(row != 0 || fence_capture_seen[0][column] == 1);
			assert(column != track.width - 1U || fence_capture_seen[1][row] == 1);
			assert(row != track.height - 1U || fence_capture_seen[2][column] == 1);
			assert(column != 0 || fence_capture_seen[3][row] == 1);
			if (size == 3U && view == 0) {
				fence_capture_mode = FENCE_RENDER_HIDE;
				size_t hidden_count;
				legacy_u32 *hidden = capture_complete_render(&hidden_count);
				assert(pixel_count == hidden_count);
				/* Suppress only the matched fence meshes; real pixels must disappear. */
				assert(memcmp(visible, hidden, pixel_count * sizeof(*visible)) != 0);
				free(hidden);
				frames++;
			}
			fence_capture_mode = 0;
			free(visible);
			frames++;
		}
		extended_track_unload();
		cstrk_free(&track);
	}
	state = saved_state;
	cameramode = saved_camera;
	detail_level = saved_detail;
	return frames;
}
#endif

#define SLOPE_OCCLUSION_MAP_SIZE 30U
#define SLOPE_OCCLUSION_CAMERA_ELEVATION 192
#define SLOPE_OCCLUSION_MIN_ROAD_PIXELS 100U
#define SLOPE_OCCLUSION_CAMERA_COUNT 4U
#define SLOPE_OCCLUSION_PLATEAU_COLUMN 10
#define SLOPE_OCCLUSION_GRAZING_DISTANCE_TILES 2U
#define SLOPE_OCCLUSION_MIN_GRAZING_ROAD_PIXELS 50U

/* Fence colors can also be road paints. The bare reference must specifically
 * contain grass, so patterned fence cutouts cannot count as interior ground. */
static legacy_u8 slope_occlusion_grass_pixel(legacy_u32 pixel)
{
	for (legacy_u16 material = SLOPE_OCCLUSION_HILL_MATERIAL_FIRST;
		 material <= SLOPE_OCCLUSION_HILL_MATERIAL_LAST; material++) {
		if (pixel == (RENDER_CAPTURE_OPAQUE_ALPHA |
					  (legacy_u32)material_clrlist_ptr[material] * RENDER_CAPTURE_INDEX_RGB_STEP)) {
			return 1;
		}
	}
	return pixel == (RENDER_CAPTURE_OPAQUE_ALPHA |
					 (legacy_u32)skybox.ground_color * RENDER_CAPTURE_INDEX_RGB_STEP) ||
		   pixel == (RENDER_CAPTURE_OPAQUE_ALPHA |
					 (legacy_u32)material_clrlist_ptr[SLOPE_OCCLUSION_GROUND_MATERIAL] *
						 RENDER_CAPTURE_INDEX_RGB_STEP);
}

static legacy_u16 check_sloped_road_occlusion(void)
{
#ifdef RESTUNTS_WRAP_HILL_SHAPES
	/* Match the user's slope.cstrk: a native NW big turn with a gas station
	 * at its free origin, and a quarter-turned chicane sharing an office. */
	static const struct {
		legacy_u16 road, scenery;
		legacy_u8 x, y, rotation, base;
		legacy_u64 classic_hash[SLOPE_OCCLUSION_CAMERA_COUNT];
	} cases[] = {{10U,
				  157U,
				  7U,
				  0U,
				  0U,
				  3U,
				  {UINT64_C(0xcc118d2116fe0482), UINT64_C(0x1db1a46355253bf2),
				   UINT64_C(0x9f43712c643b5fd3), UINT64_C(0x809724c1d4d470dc)}},
				 {62U,
				  165U,
				  8U,
				  9U,
				  3U,
				  2U,
				  {UINT64_C(0xaede942db316e74e), UINT64_C(0xd9421d370c475a6f),
				   UINT64_C(0xe952dafd425530a1), UINT64_C(0xfcceeaaf6f139a60)}}};
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	legacy_char error[GRASS_RENDER_ERROR_CAPACITY];
	assert(cstrk_create(&track, SLOPE_OCCLUSION_MAP_SIZE, SLOPE_OCCLUSION_MAP_SIZE, error,
						sizeof(error)));
	track.horizon = CSTRK_HORIZON_COUNTRY;
	for (legacy_u16 y = 0; y <= track.height; y++) {
		for (legacy_u16 x = 0; x <= track.width; x++) {
			legacy_s16 height = SLOPE_OCCLUSION_PLATEAU_COLUMN - (legacy_s16)x;
			track.corner_heights[(size_t)y * (track.width + 1U) + x] =
				height < 0								? 0
				: height > (legacy_s16)CSTRK_MAX_HEIGHT ? (legacy_s16)CSTRK_MAX_HEIGHT
														: height;
		}
	}
	legacy_s32 saved_scale = hires_render_scale();
	legacy_s8 saved_camera = cameramode;
	legacy_u16 frames = 0;
	for (legacy_u8 kind = 0; kind < sizeof(cases) / sizeof(cases[0]); kind++) {
		struct CSTRK_OBJECT road = {0};
		road.legacy_id = cases[kind].road;
		road.x = cases[kind].x;
		road.y = cases[kind].y;
		road.rotation = cases[kind].rotation;
		road.width = road.height = 2;
		road.occupied_mask = 6;
		road.base_height = cases[kind].base;
		road.slope_x = -1;
		struct CSTRK_OBJECT scenery = road;
		scenery.legacy_id = cases[kind].scenery;
		scenery.rotation = 0;
		scenery.width = scenery.height = scenery.occupied_mask = 1;
		track.object_count = 0;
		assert(cstrk_add_object(&track, &road, error, sizeof(error)));
		assert(cstrk_add_object(&track, &scenery, error, sizeof(error)));
		assert(extended_track_activate(&track, error, sizeof(error)));
		for (legacy_s32 scale = supersight_enabled ? HIRES_SCALE : HIRES_MINIMUM_SCALE; scale;
			 scale = supersight_enabled ? scale / 2 : 0) {
			if (supersight_enabled) {
				hires_set_render_scale(scale);
			}
			legacy_u8 camera_count = SLOPE_OCCLUSION_CAMERA_COUNT + (supersight_enabled != 0);
			for (legacy_u8 camera = 0; camera < camera_count; camera++) {
				struct CARSTATE *car = &state.playerstate;
				car->car_rotate = (struct VECTOR){0, 0, 0};
				state.game_particles_active = 0;
				car->car_position.lx = (road.x * CSTRK_TILE_WORLD_SIZE + CSTRK_TILE_WORLD_SIZE) *
									   GRASS_RENDER_POSITION_SCALE;
				car->car_position.lz =
					((track.height - road.y) * CSTRK_TILE_WORLD_SIZE - CSTRK_TILE_WORLD_SIZE) *
					GRASS_RENDER_POSITION_SCALE;
				car->car_position.ly = ((road.base_height - 1U) * CSTRK_LEVEL_WORLD_HEIGHT +
										STUNT_RENDER_CAR_CLEARANCE) *
									   GRASS_RENDER_POSITION_SCALE;
				car->car_previous_position = car->car_position;
				car->car_crashBmpFlag = CRASH_EVENT_WATER;
				custom_camera.distance = STUNT_RENDER_CAMERA_DISTANCE;
				custom_camera.elevation_angle = SLOPE_OCCLUSION_CAMERA_ELEVATION;
				custom_camera.azimuth_angle = ANGLE_EIGHTH_TURN + camera * ANGLE_QUARTER_TURN;
				cameramode = CAMERA_MODE_CUSTOM;
				legacy_u16 minimum_road_pixels = SLOPE_OCCLUSION_MIN_ROAD_PIXELS;
				if (camera == SLOPE_OCCLUSION_CAMERA_COUNT) {
					/* A low view across the hillside exposes fractional cracks that
					 * the overhead road-occlusion views can hide. */
					cameramode = CAMERA_MODE_COCKPIT;
					car->car_position.lx =
						(SLOPE_OCCLUSION_PLATEAU_COLUMN + SLOPE_OCCLUSION_GRAZING_DISTANCE_TILES) *
						CSTRK_TILE_WORLD_SIZE * GRASS_RENDER_POSITION_SCALE;
					car->car_position.ly = 0;
					car->car_position.lz = ((track.height - road.y - 1U) * CSTRK_TILE_WORLD_SIZE +
											CSTRK_TILE_WORLD_SIZE / 2U) *
										   GRASS_RENDER_POSITION_SCALE;
					car->car_previous_position = car->car_position;
					car->car_rotate = (struct VECTOR){ANGLE_QUARTER_TURN, 0, 0};
					minimum_road_pixels = SLOPE_OCCLUSION_MIN_GRAZING_ROAD_PIXELS;
				}
				legacy_s16 saved_sky = skybox.sky_color;
				if (supersight_enabled) {
					skybox.sky_color = SEAM_RENDER_SKY_MARKER;
					render_capture_mark_sky = 1;
				}
				size_t reference_size, actual_size;
				slope_occlusion_hide_terrain = 1;
				legacy_u32 *reference = capture_complete_render(&reference_size);
				slope_occlusion_hide_terrain = 0;
				legacy_u32 *actual = capture_complete_render(&actual_size);
				assert(actual_size == reference_size);
				legacy_s32 width, height;
				assert(hires_framebuffer(dos_memory_make_pointer(RENDER_CAPTURE_VIDEO_SEGMENT, 0),
										 &width, &height));
				if (supersight_enabled) {
					struct CSTRK_TRACK bare = track;
					bare.object_count = 0;
					assert(extended_track_activate(&bare, error, sizeof(error)));
					size_t ground_size;
					legacy_u32 *ground = capture_complete_render(&ground_size);
					assert(ground_size == actual_size);
					assert(extended_track_activate(&track, error, sizeof(error)));
					size_t gaps = 0;
					for (legacy_s32 y = 1; y + 1 < height; y++) {
						for (legacy_s32 x = 1; x + 1 < width; x++) {
							size_t pixel = (size_t)y * width + x;
							/* A one-pixel interior margin excludes the open map and
							 * hill silhouettes. A distinct sky marker distinguishes
							 * gaps from enhanced highlights sharing the sky's RGB. */
							if (actual[pixel] == RENDER_CAPTURE_SKY_COLOR &&
								slope_occlusion_grass_pixel(ground[pixel]) &&
								slope_occlusion_grass_pixel(ground[pixel - 1U]) &&
								slope_occlusion_grass_pixel(ground[pixel + 1U]) &&
								slope_occlusion_grass_pixel(ground[pixel - width]) &&
								slope_occlusion_grass_pixel(ground[pixel + width]) &&
								slope_occlusion_grass_pixel(ground[pixel - width - 1U]) &&
								slope_occlusion_grass_pixel(ground[pixel - width + 1U]) &&
								slope_occlusion_grass_pixel(ground[pixel + width - 1U]) &&
								slope_occlusion_grass_pixel(ground[pixel + width + 1U])) {
								gaps++;
							}
						}
					}
					if (gaps) {
						fprintf(
							stderr,
							"Sloped road %u camera %u scale %d exposes %zu interior sky pixels\n",
							road.legacy_id, camera, scale, gaps);
					}
					assert(gaps == 0);
					free(ground);
					frames++;
				}
				skybox.sky_color = saved_sky;
				render_capture_mark_sky = 0;
				legacy_u32 asphalt =
					RENDER_CAPTURE_OPAQUE_ALPHA |
					(legacy_u32)material_clrlist_ptr[CLASSIC_RENDER_PAVED_MATERIAL] *
						RENDER_CAPTURE_INDEX_RGB_STEP;
				size_t visible = 0, covered = 0;
				for (legacy_s32 y = 1; y + 1 < height; y++) {
					for (legacy_s32 x = 1; x + 1 < width; x++) {
						size_t pixel = (size_t)y * width + x;
						if (reference[pixel] == asphalt && reference[pixel - 1U] == asphalt &&
							reference[pixel + 1U] == asphalt &&
							reference[pixel - width] == asphalt &&
							reference[pixel + width] == asphalt) {
							visible++;
							covered += actual[pixel] != asphalt;
						}
					}
				}
				if ((supersight_enabled && covered) || visible < minimum_road_pixels) {
					fprintf(stderr,
							"Sloped road %u camera %u mode %d has %zu/%zu covered asphalt pixels\n",
							road.legacy_id, camera, supersight_enabled, covered, visible);
				}
				assert(visible >= minimum_road_pixels);
				if (supersight_enabled) {
					assert(covered == 0);
				} else {
					legacy_u64 hash = SEAM_RENDER_HASH_OFFSET;
					for (size_t pixel = 0; pixel < actual_size; pixel++) {
						hash = (hash ^ actual[pixel]) * SEAM_RENDER_HASH_PRIME;
					}
					/* Independently captured from committed renderer 677be9c5. */
					assert(hash == cases[kind].classic_hash[camera]);
					printf("Sloped road classic ID %u camera %u hash %016" LEGACY_PRIx64
						   " covered %zu/%zu\n",
						   road.legacy_id, camera, hash, covered, visible);
				}
				free(reference);
				free(actual);
				frames += 2;
			}
		}
	}
	hires_set_render_scale(saved_scale);
	cameramode = saved_camera;
	extended_track_unload();
	cstrk_free(&track);
	return frames;
#else
	return 0;
#endif
}

#define CORNER_FILL_MAP_SIZE 30U
#define CORNER_FILL_ANCHOR 10U
#define CORNER_FILL_FOOTPRINT 2U
#define CORNER_FILL_HEIGHT 2U
#define CORNER_FILL_ROTATIONS 4U
#define CORNER_FILL_DETAIL_COUNT 2U
#define CORNER_FILL_BASE_LEVELS (CSTRK_MAX_HEIGHT - CORNER_FILL_HEIGHT + 1U)
#define CORNER_FILL_ROAD 11U
/* Doubled area can change by at most perimeter * two-unit authored overhang. */
#define CORNER_FILL_AREA_TOLERANCE (8.0 * STUNT_RENDER_AUTHORED_OVERSCAN * CSTRK_TILE_WORLD_SIZE)

#ifdef RESTUNTS_WRAP_HILL_SHAPES
static void corner_fill_rotate(legacy_u16 *x, legacy_u16 *y, legacy_u16 extent, legacy_u8 rotation)
{
	for (legacy_u8 turn = 0; turn < rotation; turn++) {
		legacy_u16 previous_x = *x;
		*x = extent - *y;
		*y = previous_x;
	}
}

static legacy_f64 capture_corner_terrain(const struct CSTRK_TRACK *track, legacy_u16 x,
										 legacy_u16 y, legacy_u8 detail)
{
	legacy_char error[GRASS_RENDER_ERROR_CAPACITY];
	assert(extended_track_activate(track, error, sizeof(error)));
	struct FRAME_TILE tile = {0};
	tile.east = (legacy_s8)x;
	tile.south = (legacy_s8)y;
	tile.detail = (legacy_s8)detail;
	const struct FRAME_CAMERA camera = {0};
	terrain_coverage_area = 0;
	terrain_coverage_capture = 1;
	assert(extended_frame_draw_terrain(&tile, &camera, 0) == 0);
	terrain_coverage_capture = 0;
	return terrain_coverage_area;
}
#endif

static legacy_u16 check_terrain_below_graded_turn(void)
{
#ifdef RESTUNTS_WRAP_HILL_SHAPES
	if (!supersight_enabled) {
		return 0;
	}
	/* The occupied diagonal follows a straight eastward slope. Its free NE
	 * corner falls below that plane, making an outer hill corner. The road's
	 * projected footprint must not erase either of the lower terrain faces. */
	for (legacy_u8 base = 0; base < CORNER_FILL_BASE_LEVELS; base++) {
		for (legacy_u8 rotation = 0; rotation < CORNER_FILL_ROTATIONS; rotation++) {
			struct CSTRK_TRACK track;
			cstrk_init(&track);
			legacy_char error[GRASS_RENDER_ERROR_CAPACITY];
			assert(cstrk_create(&track, CORNER_FILL_MAP_SIZE, CORNER_FILL_MAP_SIZE, error,
								sizeof(error)));
			for (legacy_u16 y = 0; y <= track.height; y++) {
				for (legacy_u16 x = 0; x <= track.width; x++) {
					legacy_s16 height = (legacy_s16)x - (legacy_s16)CORNER_FILL_ANCHOR;
					if (height < 0) {
						height = 0;
					} else if (height > (legacy_s16)CORNER_FILL_HEIGHT) {
						height = CORNER_FILL_HEIGHT;
					}
					if (y <= CORNER_FILL_ANCHOR && height > 1) {
						height = 1;
					}
					legacy_u16 rx = x, ry = y;
					corner_fill_rotate(&rx, &ry, CORNER_FILL_MAP_SIZE, rotation);
					track.corner_heights[(size_t)ry * (track.width + 1U) + rx] =
						(legacy_u8)(height + base);
				}
			}
			struct CSTRK_OBJECT road = {0};
			road.legacy_id = CORNER_FILL_ROAD;
			legacy_u16 x = CORNER_FILL_ANCHOR, y = CORNER_FILL_ANCHOR;
			corner_fill_rotate(&x, &y, CORNER_FILL_MAP_SIZE - CORNER_FILL_FOOTPRINT, rotation);
			road.x = (legacy_u8)x;
			road.y = (legacy_u8)y;
			road.rotation = rotation;
			road.surface = CSTRK_ROAD_ICE;
			road.width = road.height = CORNER_FILL_FOOTPRINT;
			road.occupied_mask = rotation & 1U ? 6U : 9U;
			road.base_height = base + (rotation < CORNER_FILL_HEIGHT ? 0U : CORNER_FILL_HEIGHT);
			static const legacy_s8 slopes[][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
			road.slope_x = slopes[rotation][0];
			road.slope_y = slopes[rotation][1];
			assert(cstrk_add_object(&track, &road, error, sizeof(error)));
			x = CORNER_FILL_ANCHOR + 1U;
			y = CORNER_FILL_ANCHOR;
			corner_fill_rotate(&x, &y, CORNER_FILL_MAP_SIZE - 1U, rotation);
			struct CSTRK_TRACK bare = track;
			bare.object_count = 0;
			for (legacy_u8 detail = 0; detail < CORNER_FILL_DETAIL_COUNT; detail++) {
				terrain_edge_capture = 1;
				terrain_flat_count = terrain_base_count = 0;
				legacy_f64 reference = capture_corner_terrain(&bare, x, y, detail);
				terrain_edge_capture = 0;
				assert(terrain_flat_count == SHAPE3D_PRIMITIVE_POLYGON_FIRST);
				assert(terrain_base_count == TERRAIN_EDGE_ENDPOINT_COUNT);
				/* The authored hill diagonal overhangs the grid by two units.
				 * Its low supporting triangle must share both exact endpoints. */
				for (legacy_u16 endpoint = 0; endpoint < terrain_base_count; endpoint++) {
					legacy_u8 shared = 0;
					for (legacy_u16 vertex = 0; vertex < terrain_flat_count; vertex++) {
						const struct SHAPE3D_HIRES_VECTOR *a = &terrain_base_vertices[endpoint];
						const struct SHAPE3D_HIRES_VECTOR *b = &terrain_flat_vertices[vertex];
						shared |= a->x == b->x && a->y == b->y && a->z == b->z;
					}
					assert(shared);
				}
				legacy_f64 actual = capture_corner_terrain(&track, x, y, detail);
				assert(reference >= 2.0 * CSTRK_TILE_WORLD_SIZE * CSTRK_TILE_WORLD_SIZE);
				legacy_f64 missing = reference - actual;
				if (missing > CORNER_FILL_AREA_TOLERANCE) {
					fprintf(stderr,
							"Graded turn rotation %u base %u detail %u removes %.0f doubled "
							"terrain area\n",
							rotation, base, detail, missing);
				}
				assert(missing <= CORNER_FILL_AREA_TOLERANCE);
			}
			extended_track_unload();
			cstrk_free(&track);
		}
	}
#endif
	return 0;
}

static legacy_u16 check_generated_terrain_bounds(void)
{
#ifdef RESTUNTS_WRAP_HILL_SHAPES
	if (!supersight_enabled) {
		return 0;
	}
	struct GAMESTATE saved_state = state;
	legacy_s8 saved_camera = cameramode;
	legacy_s32 saved_scale = hires_render_scale();
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	legacy_char error[GRASS_RENDER_ERROR_CAPACITY];
	assert(cstrk_create(&track, CORNER_FILL_MAP_SIZE, CORNER_FILL_MAP_SIZE, error, sizeof(error)));
	track.horizon = CSTRK_HORIZON_COUNTRY;
	struct CSTRK_OBJECT marker = {0};
	marker.legacy_id = 4;
	marker.x = GENERATED_BOUNDS_TARGET_TILE;
	marker.y = GENERATED_BOUNDS_TARGET_TILE - GENERATED_BOUNDS_ROAD_OFFSET;
	marker.width = marker.height = marker.occupied_mask = 1;
	assert(cstrk_add_object(&track, &marker, error, sizeof(error)));
	assert(extended_track_activate(&track, error, sizeof(error)));
	struct CARSTATE *car = &state.playerstate;
	car->car_position.lx =
		(GENERATED_BOUNDS_TARGET_TILE * CSTRK_TILE_WORLD_SIZE + CSTRK_TILE_WORLD_SIZE / 2U) *
		GRASS_RENDER_POSITION_SCALE;
	car->car_position.lz =
		((track.height - GENERATED_BOUNDS_TARGET_TILE - 1U) * CSTRK_TILE_WORLD_SIZE +
		 CSTRK_TILE_WORLD_SIZE / 2U) *
		GRASS_RENDER_POSITION_SCALE;
	car->car_position.ly = 0;
	car->car_previous_position = car->car_position;
	car->car_rotate = (struct VECTOR){0, 0, 0};
	car->car_crashBmpFlag = CRASH_EVENT_WATER;
	state.game_particles_active = 0;
	cameramode = CAMERA_MODE_COCKPIT;
	legacy_u32 grass =
		RENDER_CAPTURE_OPAQUE_ALPHA |
		(legacy_u32)material_clrlist_ptr[GENERATED_BOUNDS_MATERIAL] * RENDER_CAPTURE_INDEX_RGB_STEP;
	legacy_u16 frames = 0;
	for (legacy_s32 scale = HIRES_SCALE; scale; scale /= 2) {
		hires_set_render_scale(scale);
		for (generated_bounds_count = SHAPE3D_POLYGON_MAX_VERTICES - 1U;
			 generated_bounds_count <= SHAPE3D_POLYGON_MAX_VERTICES; generated_bounds_count++) {
			size_t sizes[GENERATED_BOUNDS_POLYGON + 1U];
			legacy_u32 *pixels[GENERATED_BOUNDS_POLYGON + 1U] = {0};
			for (legacy_u8 mode = GENERATED_BOUNDS_EMPTY; mode <= GENERATED_BOUNDS_POLYGON;
				 mode++) {
				generated_bounds_mode = mode;
				generated_bounds_injected = 0;
				pixels[mode] = capture_complete_render(&sizes[mode]);
				assert(generated_bounds_injected);
				assert(sizes[mode] == sizes[GENERATED_BOUNDS_EMPTY]);
				frames++;
			}
			legacy_s32 width = hires_render_width(), height = hires_render_height();
			size_t visible = 0, missing = 0;
			for (legacy_s32 y = 1; y + 1 < height; y++) {
				for (legacy_s32 x = 1; x + 1 < width; x++) {
					size_t pixel = (size_t)y * width + x;
					const legacy_u32 *reference = pixels[GENERATED_BOUNDS_TRIANGLES];
					if (reference[pixel] == grass && reference[pixel - 1U] == grass &&
						reference[pixel + 1U] == grass && reference[pixel - width] == grass &&
						reference[pixel + width] == grass &&
						pixels[GENERATED_BOUNDS_EMPTY][pixel] != grass) {
						visible++;
						missing += pixels[GENERATED_BOUNDS_POLYGON][pixel] != grass;
					}
				}
			}
			if (missing || visible < GENERATED_BOUNDS_MIN_VISIBLE_PIXELS) {
				fprintf(stderr, "Generated %u-gon scale %d loses %zu/%zu visible pixels\n",
						generated_bounds_count, scale, missing, visible);
			}
			assert(visible >= GENERATED_BOUNDS_MIN_VISIBLE_PIXELS);
			assert(missing == 0);
			for (legacy_u8 mode = GENERATED_BOUNDS_EMPTY; mode <= GENERATED_BOUNDS_POLYGON;
				 mode++) {
				free(pixels[mode]);
			}
		}
	}
	generated_bounds_mode = GENERATED_BOUNDS_DISABLED;
	hires_set_render_scale(saved_scale);
	cameramode = saved_camera;
	state = saved_state;
	extended_track_unload();
	cstrk_free(&track);
	return frames;
#else
	return 0;
#endif
}

#define NATIVE_SLOPE_RENDER_COLUMN 0U
#define NATIVE_SLOPE_RENDER_ROW 15U
#define NATIVE_SLOPE_RENDER_REVERSE 2U
#define NATIVE_SLOPE_RENDER_BASE 1U

static legacy_u16 check_reversed_native_slope_rendering(void)
{
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	legacy_char error[GRASS_RENDER_ERROR_CAPACITY];
	assert(cstrk_create(&track, SLOPE_OCCLUSION_MAP_SIZE, SLOPE_OCCLUSION_MAP_SIZE, error,
						sizeof(error)));
	track.horizon = CSTRK_HORIZON_COUNTRY;
	for (legacy_u16 y = 0; y <= track.height; y++) {
		legacy_s16 height = NATIVE_SLOPE_RENDER_ROW + NATIVE_SLOPE_RENDER_BASE - (legacy_s16)y;
		for (legacy_u16 x = 0; x <= track.width; x++) {
			track.corner_heights[(size_t)y * (track.width + 1U) + x] =
				height < 0								? 0
				: height > (legacy_s16)CSTRK_MAX_HEIGHT ? (legacy_s16)CSTRK_MAX_HEIGHT
														: height;
		}
	}
	struct CSTRK_OBJECT road = {0};
	road.legacy_id = 4;
	road.x = NATIVE_SLOPE_RENDER_COLUMN;
	road.y = NATIVE_SLOPE_RENDER_ROW;
	road.rotation = NATIVE_SLOPE_RENDER_REVERSE;
	road.width = road.height = road.occupied_mask = 1;
	road.base_height = NATIVE_SLOPE_RENDER_BASE;
	road.slope_y = -1;
	assert(cstrk_add_object(&track, &road, error, sizeof(error)));
	assert(extended_track_activate(&track, error, sizeof(error)));
	const struct CSTRK_OBJECT *placed = &extended_track_model()->objects[0];
	const struct TRACKOBJECT *definition = &trkObjectList[extended_track_object_id(placed)];
#ifdef RESTUNTS_WRAP_HILL_SHAPES
	native_slope_capture_definitions[0] = definition;
	native_slope_capture_definitions[1] =
		definition->ss_ssOvelay ? frame_track_object_from_legacy_index(definition->ss_ssOvelay)
								: NULL;
	native_slope_capture_active = supersight_enabled != 0;
	native_slope_capture_parts = 0;
	native_slope_capture_markings = 0;
#endif
	struct CARSTATE *car = &state.playerstate;
	car->car_position.lx =
		(road.x * CSTRK_TILE_WORLD_SIZE + CSTRK_TILE_WORLD_SIZE / 2U) * GRASS_RENDER_POSITION_SCALE;
	car->car_position.lz =
		((track.height - road.y) * CSTRK_TILE_WORLD_SIZE - CSTRK_TILE_WORLD_SIZE / 2U) *
		GRASS_RENDER_POSITION_SCALE;
	car->car_position.ly =
		(CSTRK_LEVEL_WORLD_HEIGHT / 2U + STUNT_RENDER_CAR_CLEARANCE) * GRASS_RENDER_POSITION_SCALE;
	car->car_previous_position = car->car_position;
	car->car_rotate = (struct VECTOR){0, 0, 0};
	car->car_crashBmpFlag = CRASH_EVENT_WATER;
	state.game_particles_active = 0;
	custom_camera.distance = STUNT_RENDER_CAMERA_DISTANCE;
	custom_camera.elevation_angle = SLOPE_OCCLUSION_CAMERA_ELEVATION;
	custom_camera.azimuth_angle = ANGLE_EIGHTH_TURN;
	size_t size;
	legacy_u32 *pixels = capture_complete_render(&size);
#ifdef RESTUNTS_WRAP_HILL_SHAPES
	native_slope_capture_active = 0;
	if (supersight_enabled) {
		legacy_u8 expected = definition->ss_ssOvelay ? (1U << STUNT_RENDER_PART_COUNT) - 1U : 1U;
		assert(native_slope_capture_parts == expected);
		assert(native_slope_capture_markings > 0);
	}
#endif
	if (!supersight_enabled) {
		legacy_u64 hash = SEAM_RENDER_HASH_OFFSET;
		for (size_t pixel = 0; pixel < size; pixel++) {
			hash = (hash ^ pixels[pixel]) * SEAM_RENDER_HASH_PRIME;
		}
		/* Captured independently with committed renderer 677be9c5. */
		assert(hash == UINT64_C(0xa17912631402ffef));
		printf("Reversed native road classic pixel hash: %016" LEGACY_PRIx64 "\n", hash);
	}
	free(pixels);
	extended_track_unload();
	cstrk_free(&track);
	return 1;
}

#define GENERIC_RENDER_SLOPE_ORIGIN STUNT_RENDER_COLUMN
#define GENERIC_RENDER_HALF_TURN_COUNT 2U

static legacy_u16 check_general_sloped_rendering(void)
{
	static const struct {
		legacy_u16 id;
		legacy_u8 width, height, mask;
	} cases[] = {{7U, 1U, 1U, 1U},	{11U, 2U, 2U, 9U}, {53U, 2U, 2U, 9U},  {106U, 2U, 2U, 9U},
				 {64U, 1U, 2U, 3U}, {68U, 1U, 1U, 1U}, {101U, 1U, 1U, 1U}, {159U, 1U, 1U, 1U},
				 {38U, 1U, 1U, 1U}, {66U, 1U, 1U, 1U}, {115U, 1U, 1U, 1U}};
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	legacy_char error[GRASS_RENDER_ERROR_CAPACITY];
	assert(cstrk_create(&track, CSTRK_MAX_DIMENSION, CSTRK_MAX_DIMENSION, error, sizeof(error)));
	track.horizon = CSTRK_HORIZON_COUNTRY;
	legacy_u16 frames = 0;
	for (legacy_u16 kind = 0; kind < sizeof(cases) / sizeof(cases[0]); kind++) {
		for (legacy_u8 direction = 0; direction < GENERIC_RENDER_HALF_TURN_COUNT; direction++) {
			legacy_s8 grade = direction ? -1 : 1;
			for (legacy_u16 y = 0; y <= track.height; y++) {
				for (legacy_u16 x = 0; x <= track.width; x++) {
					legacy_s16 level = 2 + grade * ((legacy_s16)x - GENERIC_RENDER_SLOPE_ORIGIN);
					track.corner_heights[(size_t)y * (track.width + 1U) + x] =
						level < 0							   ? 0
						: level > (legacy_s16)CSTRK_MAX_HEIGHT ? (legacy_s16)CSTRK_MAX_HEIGHT
															   : level;
					if (x < track.width && y < track.height) {
						track.cells[(size_t)y * track.width + x].surface = CSTRK_TERRAIN_DIRT;
					}
				}
			}
			struct CSTRK_OBJECT object = {0};
			object.legacy_id = cases[kind].id;
			object.x = STUNT_RENDER_COLUMN;
			object.y = STUNT_RENDER_ROW;
			object.rotation = direction * GENERIC_RENDER_HALF_TURN_COUNT;
			object.width = cases[kind].width;
			object.height = cases[kind].height;
			object.occupied_mask = cases[kind].mask;
			object.slope_x = grade;
			object.base_height = 2;
			track.object_count = 0;
			assert(cstrk_add_object(&track, &object, error, sizeof(error)));
			activate_render_track(&track, 1);
			const struct CSTRK_OBJECT *placed = &extended_track_model()->objects[0];
			assert(placed && extended_geometry_uses_grade(placed));
#ifdef RESTUNTS_WRAP_HILL_SHAPES
			prepare_stunt_capture(placed);
			stunt_capture_parts = stunt_capture_details = stunt_capture_ground = 0;
			stunt_capture_active = 1;
#endif
			position_render_target(STUNT_RENDER_COLUMN, STUNT_RENDER_ROW);
			state.playerstate.car_position.ly =
				(2 * CSTRK_LEVEL_WORLD_HEIGHT + STUNT_RENDER_CAR_CLEARANCE) *
				GRASS_RENDER_POSITION_SCALE;
			state.playerstate.car_previous_position = state.playerstate.car_position;
			state.playerstate.car_crashBmpFlag = CRASH_EVENT_WATER;
			custom_camera.distance = STUNT_RENDER_CAMERA_DISTANCE;
			custom_camera.elevation_angle = STUNT_RENDER_CAMERA_ELEVATION;
			custom_camera.azimuth_angle =
				ANGLE_EIGHTH_TURN + direction * GENERIC_RENDER_HALF_TURN_COUNT * ANGLE_QUARTER_TURN;
			size_t size;
			legacy_u32 *pixels = capture_complete_render(&size);
			assert(size > 0);
			free(pixels);
#ifdef RESTUNTS_WRAP_HILL_SHAPES
			stunt_capture_active = 0;
			const struct TRACKOBJECT *definition = &trkObjectList[extended_track_object_id(placed)];
			legacy_u8 expected =
				definition->ss_ssOvelay ? (1U << STUNT_RENDER_PART_COUNT) - 1U : 1U;
			if (stunt_capture_parts != expected) {
				fprintf(stderr, "General slope render ID %u grade %d parts %u expected %u\n",
						object.legacy_id, grade, stunt_capture_parts, expected);
			}
			assert(stunt_capture_parts == expected);
			release_stunt_capture();
#endif
			frames++;
		}
	}
	extended_track_unload();
	cstrk_free(&track);
	return frames;
}

#ifdef RESTUNTS_WRAP_HILL_SHAPES
static void check_upright_sloped_plants(void)
{
	static const legacy_u16 ids[] = {PLANT_RENDER_PALM_ID, PLANT_RENDER_CACTUS_ID,
									 PLANT_RENDER_PINE_ID};
	static const legacy_s8 slopes[PLANT_RENDER_SLOPE_COUNT][2] = {
		{0, 0}, {1, 0}, {0, 1}, {-1, 0}, {0, -1}};
	static const legacy_u8 bases[] = {0, CSTRK_MAX_HEIGHT - 1U};
	struct CSTRK_TRACK track;
	legacy_char error[GRASS_RENDER_ERROR_CAPACITY];
	cstrk_init(&track);
	assert(cstrk_create(&track, CSTRK_MAX_DIMENSION, CSTRK_MAX_DIMENSION, error, sizeof(error)));
	const struct FRAME_CAMERA camera = {0};
	legacy_u16 submissions = 0;
	for (legacy_u16 kind = 0; kind < sizeof(ids) / sizeof(ids[0]); kind++) {
		for (legacy_u8 rotation = 0; rotation < PLANT_RENDER_DIRECTION_COUNT; rotation++) {
			for (legacy_u8 slope = 0; slope < PLANT_RENDER_SLOPE_COUNT; slope++) {
				for (legacy_u8 base = 0; base < sizeof(bases) / sizeof(bases[0]); base++) {
					legacy_s8 slope_x = slopes[slope][0], slope_y = slopes[slope][1];
					for (legacy_u16 y = 0; y <= track.height; y++) {
						for (legacy_u16 x = 0; x <= track.width; x++) {
							legacy_u8 raised = slope_x > 0	 ? x > STUNT_RENDER_COLUMN
											   : slope_x < 0 ? x <= STUNT_RENDER_COLUMN
											   : slope_y > 0 ? y > STUNT_RENDER_ROW
											   : slope_y < 0 ? y <= STUNT_RENDER_ROW
															 : 0;
							track.corner_heights[(size_t)y * (track.width + 1U) + x] =
								bases[base] + raised;
						}
					}
					struct CSTRK_OBJECT object = {0};
					object.legacy_id = ids[kind];
					object.x = STUNT_RENDER_COLUMN;
					object.y = STUNT_RENDER_ROW;
					object.rotation = rotation;
					object.width = object.height = object.occupied_mask = 1;
					object.base_height = bases[base] + (slope_x < 0 || slope_y < 0);
					object.slope_x = slope_x;
					object.slope_y = slope_y;
					track.object_count = 0;
					assert(cstrk_add_object(&track, &object, error, sizeof(error)));
					activate_render_track(&track, 1);
					const struct CSTRK_OBJECT *placed = &extended_track_model()->objects[0];
					const struct TRACKOBJECT *definition =
						&trkObjectList[extended_track_object_id(placed)];
					legacy_f64 lifts[STUNT_RENDER_DETAIL_COUNT] = {0};
					for (legacy_u8 detail = 0; detail < STUNT_RENDER_DETAIL_COUNT; detail++) {
						const struct SHAPE3D *source = detail && definition->ss_loShapePtr
														   ? definition->ss_loShapePtr
														   : definition->ss_shapePtr;
						assert(extended_geometry_transform_shape(
							source, &plant_capture_source, definition->ss_rotY, CSTRK_ROAD_PAVED,
							definition->ss_physicalModel));
						plant_capture_object = *placed;
						plant_capture_calls = plant_capture_precise = 0;
						plant_capture_lift = 0;
						struct FRAME_TILE tile = {0};
						tile.east = STUNT_RENDER_COLUMN;
						tile.south = STUNT_RENDER_ROW;
						tile.detail = detail;
						plant_capture_active = 1;
						assert(!extended_frame_draw_objects(&tile, &camera, 0));
						plant_capture_active = 0;
						assert(plant_capture_calls == 1);
						assert(plant_capture_precise == (supersight_enabled && slope != 0));
						lifts[detail] = plant_capture_lift;
						extended_geometry_free_shape(&plant_capture_source);
						submissions++;
					}
					/* A detail switch must not shift the canopy's elevation. */
					assert(lifts[0] == lifts[1]);
				}
			}
		}
	}
	extended_track_unload();
	cstrk_free(&track);
	printf("Upright plant geometry: %u submissions in %s mode.\n", submissions,
		   supersight_enabled ? "HyperVision" : "classic");
}
#endif

static void combined_stunt_render_terrain(struct CSTRK_TRACK *track, legacy_s8 slope_x,
										  legacy_s8 slope_y, legacy_u8 base)
{
	for (legacy_u16 y = 0; y <= track->height; y++) {
		for (legacy_u16 x = 0; x <= track->width; x++) {
			legacy_u8 raised = slope_x > 0	 ? x > STUNT_RENDER_COLUMN
							   : slope_x < 0 ? x <= STUNT_RENDER_COLUMN
							   : slope_y > 0 ? y > STUNT_RENDER_ROW
							   : slope_y < 0 ? y <= STUNT_RENDER_ROW
											 : 0;
			track->corner_heights[(size_t)y * (track->width + 1U) + x] = base + raised;
		}
	}
}

#ifdef RESTUNTS_WRAP_HILL_SHAPES
static void prepare_combined_stunt_capture(const struct CSTRK_OBJECT *object, legacy_u8 detail)
{
	const struct TRACKOBJECT *slalom = &trkObjectList[extended_track_object_id(object)];
	struct TRACKOBJECT tunnel = trkObjectList[CSTRK_TUNNEL_FIRST_ID];
	struct TRACKOBJECT floor = *frame_track_object_from_legacy_index(tunnel.ss_ssOvelay);
	tunnel.ss_rotY = floor.ss_rotY = slalom->ss_rotY;
	if (!floor.ss_loShapePtr) {
		floor.ss_loShapePtr = floor.ss_shapePtr;
	}
	const struct TRACKOBJECT *parts[COMBINED_STUNT_RENDER_PART_COUNT] = {&tunnel, slalom, &floor};
	for (legacy_u8 part = 0; part < COMBINED_STUNT_RENDER_PART_COUNT; part++) {
		const struct SHAPE3D *source =
			detail ? parts[part]->ss_loShapePtr : parts[part]->ss_shapePtr;
		assert(source != NULL);
		assert(extended_geometry_transform_shape(source, &combined_stunt_capture_meshes[part],
												 slalom->ss_rotY, object->surface,
												 PHYSICAL_MODEL_SLALOM));
	}
	combined_stunt_capture_object = *object;
	combined_stunt_capture_parts = combined_stunt_capture_calls = combined_stunt_capture_precise =
		0;
}

static void check_combined_stunt_meshes(void)
{
	static const legacy_s8 slopes[][2] = {{0, 0}, {1, 0}, {0, 1}, {-1, 0}, {0, -1}};
	struct CSTRK_TRACK track;
	legacy_char error[GRASS_RENDER_ERROR_CAPACITY];
	cstrk_init(&track);
	assert(cstrk_create(&track, CSTRK_MAX_DIMENSION, CSTRK_MAX_DIMENSION, error, sizeof(error)));
	const struct FRAME_CAMERA camera = {0};
	legacy_u16 cases = 0;
	for (legacy_u8 rotation = 0; rotation < SEAM_RENDER_DIRECTION_COUNT; rotation++) {
		legacy_u8 base = rotation & 1U ? CSTRK_MAX_HEIGHT - 1U : 0;
		for (legacy_u8 slope = 0; slope < sizeof(slopes) / sizeof(slopes[0]); slope++) {
			combined_stunt_render_terrain(&track, slopes[slope][0], slopes[slope][1], base);
			for (legacy_u8 surface = CSTRK_ROAD_PAVED; surface <= CSTRK_ROAD_ICE; surface++) {
				struct CSTRK_OBJECT object = {0};
				object.legacy_id = CSTRK_SLALOM_FIRST_ID;
				object.x = STUNT_RENDER_COLUMN;
				object.y = STUNT_RENDER_ROW;
				object.rotation = rotation;
				object.surface = surface;
				object.width = object.height = object.occupied_mask = 1;
				object.tunnel_slalom = 1;
				object.slope_x = slopes[slope][0];
				object.slope_y = slopes[slope][1];
				object.base_height = base + (object.slope_x < 0 || object.slope_y < 0);
				track.object_count = 0;
				assert(cstrk_add_object(&track, &object, error, sizeof(error)));
				activate_render_track(&track, 1);
				const struct CSTRK_OBJECT *placed = &extended_track_model()->objects[0];
				for (legacy_u8 detail = 0; detail < STUNT_RENDER_DETAIL_COUNT; detail++) {
					prepare_combined_stunt_capture(placed, detail);
					struct FRAME_TILE tile = {0};
					tile.east = STUNT_RENDER_COLUMN;
					tile.south = STUNT_RENDER_ROW;
					tile.detail = detail;
					combined_stunt_capture_active = 1;
					assert(!extended_frame_draw_objects(&tile, &camera, 0));
					combined_stunt_capture_active = 0;
					assert(combined_stunt_capture_parts ==
						   (1U << COMBINED_STUNT_RENDER_PART_COUNT) - 1U);
					assert(combined_stunt_capture_calls == COMBINED_STUNT_RENDER_PART_COUNT);
					assert(combined_stunt_capture_precise ==
						   (supersight_enabled && slope ? COMBINED_STUNT_RENDER_PART_COUNT : 0));
					for (legacy_u8 part = 0; part < COMBINED_STUNT_RENDER_PART_COUNT; part++) {
						extended_geometry_free_shape(&combined_stunt_capture_meshes[part]);
					}
					cases++;
				}
			}
		}
	}
	extended_track_unload();
	cstrk_free(&track);
	printf("Tunnel with slalom: %u three-part submissions in %s mode.\n", cases,
		   supersight_enabled ? "HyperVision" : "classic");
}
#endif

static legacy_u16 check_combined_stunt_rendering(void)
{
	struct GAMESTATE saved = state;
	legacy_s8 saved_detail = detail_level;
	struct CSTRK_TRACK track;
	legacy_char error[GRASS_RENDER_ERROR_CAPACITY];
	cstrk_init(&track);
	assert(cstrk_create(&track, CSTRK_MAX_DIMENSION, CSTRK_MAX_DIMENSION, error, sizeof(error)));
	track.horizon = CSTRK_HORIZON_COUNTRY;
	legacy_u16 frames = 0;
	for (legacy_u8 sloped = 0; sloped < COMBINED_STUNT_RENDER_SLOPE_COUNT; sloped++) {
		combined_stunt_render_terrain(&track, sloped, 0, 0);
		position_render_target(STUNT_RENDER_COLUMN, STUNT_RENDER_ROW);
		state.playerstate.car_position.ly =
			(sloped * (CSTRK_LEVEL_WORLD_HEIGHT / 2) + STUNT_RENDER_CAR_CLEARANCE) *
			GRASS_RENDER_POSITION_SCALE;
		state.playerstate.car_previous_position = state.playerstate.car_position;
		state.playerstate.car_crashBmpFlag = CRASH_EVENT_WATER;
		custom_camera.distance = CSTRK_TILE_WORLD_SIZE;
		custom_camera.elevation_angle = 0;
		custom_camera.azimuth_angle = 0;
		for (legacy_u8 detail = 0; detail < STUNT_RENDER_DETAIL_COUNT; detail++) {
			detail_level = detail ? STUNT_RENDER_LOW_DETAIL : 0;
			legacy_u32 *pixels[COMBINED_STUNT_RENDER_VARIANT_COUNT];
			size_t sizes[COMBINED_STUNT_RENDER_VARIANT_COUNT];
			for (legacy_u8 variant = 0; variant < COMBINED_STUNT_RENDER_VARIANT_COUNT; variant++) {
				struct CSTRK_OBJECT object = {0};
				object.legacy_id = variant ? CSTRK_SLALOM_FIRST_ID : CSTRK_TUNNEL_FIRST_ID;
				object.x = STUNT_RENDER_COLUMN;
				object.y = STUNT_RENDER_ROW;
				object.width = object.height = object.occupied_mask = 1;
				object.slope_x = sloped;
				object.tunnel_slalom = variant == COMBINED_STUNT_RENDER_COMBINED_VARIANT;
				track.object_count = 0;
				assert(cstrk_add_object(&track, &object, error, sizeof(error)));
				activate_render_track(&track, 1);
				pixels[variant] = capture_complete_render(&sizes[variant]);
				frames++;
			}
			for (legacy_u8 variant = 0; variant < COMBINED_STUNT_RENDER_COMBINED_VARIANT;
				 variant++) {
				assert(sizes[variant] == sizes[COMBINED_STUNT_RENDER_COMBINED_VARIANT]);
				size_t changed = 0;
				for (size_t pixel = 0; pixel < sizes[variant]; pixel++) {
					changed += pixels[variant][pixel] !=
							   pixels[COMBINED_STUNT_RENDER_COMBINED_VARIANT][pixel];
				}
				if (changed <= SURFACE_RENDER_MIN_CHANGED_PIXELS) {
					fprintf(stderr,
							"Tunnel with slalom pixels mode %d slope %u detail %u baseline %u "
							"changed %zu\n",
							supersight_enabled, sloped, detail, variant, changed);
				}
				assert(changed > SURFACE_RENDER_MIN_CHANGED_PIXELS);
			}
			for (legacy_u8 variant = 0; variant < COMBINED_STUNT_RENDER_VARIANT_COUNT; variant++) {
				free(pixels[variant]);
			}
		}
	}
	state = saved;
	detail_level = saved_detail;
	extended_track_unload();
	cstrk_free(&track);
	return frames;
}

static void check_roll_ramp_mesh_geometry(void)
{
	struct VECTOR entry, lip, normal;
	roll_ramp_sample(0, 0, &entry, &normal);
	assert(entry.x == 0 && entry.y == 0 && entry.z == ROLL_RAMP_HALF_LENGTH);
	assert(normal.x == 0 && normal.y > 0 && normal.z == 0);
	roll_ramp_sample(ROLL_RAMP_LENGTH, 0, &lip, &normal);
	assert(lip.x == 0 && lip.z == -ROLL_RAMP_HALF_LENGTH);
	assert(lip.y > ROLL_RAMP_RISE_PER_TILE && normal.x < 0 && normal.y > 0);
	assert(roll_ramp_bank_angle(ROLL_RAMP_LENGTH) == ROLL_RAMP_LIP_BANK);
	assert(roll_ramp_bank_angle(ROLL_RAMP_LENGTH - ROLL_RAMP_ENTRY_EASE) < ROLL_RAMP_LIP_BANK);
	struct SHAPE3D mesh;
	assert(roll_ramp_create_mesh(&mesh, CSTRK_ROAD_PAVED));
	assert(mesh.shape3d_numverts <= LEGACY_U8_MAX);
	const legacy_u8 *primitive = mesh.shape3d_primitives;
	legacy_u16 concrete = 0, grille = 0;
	for (legacy_u16 triangle = 0; triangle < roll_ramp_road_triangle_count(); triangle++) {
		struct VECTOR points[OWOOT_ROAD_TRIANGLE_VERTEX_COUNT];
		assert(roll_ramp_road_triangle(triangle, points));
		assert(primitive[0] == OWOOT_ROAD_TRIANGLE_VERTEX_COUNT);
		for (legacy_u16 vertex = 0; vertex < OWOOT_ROAD_TRIANGLE_VERTEX_COUNT; vertex++) {
			struct VECTOR actual;
			shape3d_vertex_read(&mesh, primitive[SHAPE3D_PRIMITIVE_HEADER_SIZE + 1U + vertex],
								&actual);
			assert(memcmp(&actual, &points[vertex], sizeof(actual)) == 0);
			assert(actual.z >= -ROLL_RAMP_HALF_LENGTH && actual.z <= ROLL_RAMP_HALF_LENGTH);
		}
		legacy_s32 normal_y =
			(legacy_s32)(points[1].z - points[0].z) * (points[2].x - points[0].x) -
			(legacy_s32)(points[1].x - points[0].x) * (points[2].z - points[0].z);
		assert(normal_y > 0);
		grille += primitive[SHAPE3D_PRIMITIVE_HEADER_SIZE] == ROLL_RAMP_GRILLE_MATERIAL;
		primitive += SHAPE3D_PRIMITIVE_HEADER_SIZE + 1U + primitive[0];
	}
	for (legacy_u16 face = roll_ramp_road_triangle_count(); face < mesh.shape3d_numprimitives;
		 face++) {
		concrete += primitive[SHAPE3D_PRIMITIVE_HEADER_SIZE] == ROLL_RAMP_CONCRETE_MATERIAL ||
					primitive[SHAPE3D_PRIMITIVE_HEADER_SIZE] == ROLL_RAMP_CONCRETE_LIGHT_MATERIAL;
		primitive += SHAPE3D_PRIMITIVE_HEADER_SIZE + 1U + primitive[0];
	}
	assert(grille == ROLL_RAMP_ROAD_TRIANGLE_COUNT / 2U && concrete > 0);
	assert(material_patlist_ptr_cpy[ROLL_RAMP_GRILLE_MATERIAL] != 0);
	assert(material_patlist2_ptr_cpy[ROLL_RAMP_GRILLE_MATERIAL] != 0);
	for (legacy_u16 index = 0; index < roll_ramp_support_box_count(); index++) {
		struct ROLL_RAMP_BOX box;
		assert(roll_ramp_support_box(index, &box));
		assert(box.minimum.z >= -ROLL_RAMP_HALF_LENGTH && box.maximum.z <= 0);
		assert(box.minimum.y >= 0 && box.maximum.y > box.minimum.y);
		if (box.minimum.x < 0 && box.maximum.x > 0) {
			legacy_f64 lowest = ROLL_RAMP_LENGTH;
			for (legacy_u16 triangle = ROLL_RAMP_ROAD_TRIANGLE_COUNT / 2U + 1U;
				 triangle < ROLL_RAMP_ROAD_TRIANGLE_COUNT; triangle += 2U) {
				struct VECTOR points[OWOOT_ROAD_TRIANGLE_VERTEX_COUNT];
				assert(roll_ramp_road_triangle(triangle, points));
				/* Vertices zero and two are the actual low edge of each
				 * grille segment, independent of the continuous sampler. */
				const struct VECTOR *first = &points[0], *second = &points[2];
				legacy_s16 start = first->z < box.maximum.z ? first->z : box.maximum.z;
				legacy_s16 end = second->z > box.minimum.z ? second->z : box.minimum.z;
				if (start < end) {
					continue;
				}
				for (legacy_u16 side = 0; side < 2U; side++) {
					legacy_s16 z = side ? end : start;
					legacy_f64 height = first->y + (legacy_f64)(second->y - first->y) *
													   (z - first->z) / (second->z - first->z);
					if (height < lowest) {
						lowest = height;
					}
				}
			}
			assert(box.maximum.y <= lowest);
			assert(lowest - box.maximum.y < 1);
		}
	}
	roll_ramp_free_mesh(&mesh);
	assert(mesh.shape3d_vertex_bytes == NULL);
}

#ifdef RESTUNTS_WRAP_HILL_SHAPES
static void check_roll_ramp_submissions(void)
{
	struct CSTRK_TRACK track;
	legacy_char error[GRASS_RENDER_ERROR_CAPACITY];
	cstrk_init(&track);
	assert(cstrk_create(&track, CSTRK_MAX_DIMENSION, CSTRK_MAX_DIMENSION, error, sizeof(error)));
	const struct FRAME_CAMERA camera = {0};
	legacy_u16 submissions = 0;
	for (legacy_u8 rotation = 0; rotation < SEAM_RENDER_DIRECTION_COUNT; rotation++) {
		for (legacy_u8 terrain = 0; terrain < ROLL_RAMP_RENDER_TERRAIN_COUNT; terrain++) {
			struct CSTRK_OBJECT object = {0};
			object.legacy_id = CSTRK_ROLL_RAMP_FIRST_ID;
			object.x = STUNT_RENDER_COLUMN;
			object.y = STUNT_RENDER_ROW;
			object.rotation = rotation;
			object.width = rotation % 2U ? 2U : 1U;
			object.height = rotation % 2U ? 1U : 2U;
			object.occupied_mask = 3;
			object.roll_ramp = 1;
			object.base_height = terrain == 1U ? 2U : 0;
			object.slope_x = terrain == 2U;
			object.slope_y = terrain == 3U;
			for (legacy_u16 y = 0; y <= track.height; y++) {
				for (legacy_u16 x = 0; x <= track.width; x++) {
					legacy_s16 along =
						object.slope_x ? (legacy_s16)x - object.x : (legacy_s16)y - object.y;
					legacy_s16 span = object.slope_x ? object.width : object.height;
					legacy_s16 rise = along < 0 ? 0 : along > span ? span : along;
					track.corner_heights[(size_t)y * (track.width + 1U) + x] =
						object.base_height + ((object.slope_x || object.slope_y) ? rise : 0);
				}
			}
			for (legacy_u8 surface = CSTRK_ROAD_PAVED; surface <= CSTRK_ROAD_ICE; surface++) {
				object.surface = surface;
				track.object_count = 0;
				assert(cstrk_add_object(&track, &object, error, sizeof(error)));
				activate_render_track(&track, 1);
				roll_ramp_capture_object = *extended_track_object_origin_at(object.x, object.y);
				assert(roll_ramp_create_mesh(&roll_ramp_capture_source, surface));
				for (legacy_u8 detail = 0; detail < STUNT_RENDER_DETAIL_COUNT; detail++) {
					struct FRAME_TILE tile = {0};
					tile.east = object.x;
					tile.south = object.y;
					tile.detail = detail;
					roll_ramp_capture_calls = 0;
					roll_ramp_capture_active = 1;
					assert(!extended_frame_draw_objects(&tile, &camera, 0));
					roll_ramp_capture_active = 0;
					assert(roll_ramp_capture_calls == 1);
					submissions++;
				}
				roll_ramp_free_mesh(&roll_ramp_capture_source);
			}
		}
	}
	extended_track_unload();
	cstrk_free(&track);
	printf("Roll ramp geometry: %u submissions in %s mode.\n", submissions,
		   supersight_enabled ? "HyperVision" : "classic");
}
#endif

static legacy_u16 check_roll_ramp_pixels(void)
{
	struct CSTRK_TRACK track;
	legacy_char error[GRASS_RENDER_ERROR_CAPACITY];
	cstrk_init(&track);
	assert(cstrk_create(&track, CSTRK_MAX_DIMENSION, CSTRK_MAX_DIMENSION, error, sizeof(error)));
	track.horizon = CSTRK_HORIZON_COUNTRY;
	struct CSTRK_OBJECT object = {0};
	object.legacy_id = CSTRK_ROLL_RAMP_FIRST_ID;
	object.x = STUNT_RENDER_COLUMN;
	object.y = STUNT_RENDER_ROW;
	object.width = 1;
	object.height = 2;
	object.occupied_mask = 3;
	object.roll_ramp = 1;
	assert(cstrk_add_object(&track, &object, error, sizeof(error)));
	position_render_target(STUNT_RENDER_COLUMN, STUNT_RENDER_ROW);
	state.playerstate.car_position.ly = STUNT_RENDER_CAR_CLEARANCE * GRASS_RENDER_POSITION_SCALE;
	state.playerstate.car_previous_position = state.playerstate.car_position;
	state.playerstate.car_crashBmpFlag = CRASH_EVENT_WATER;
	custom_camera.distance = STUNT_RENDER_CAMERA_DISTANCE;
	custom_camera.elevation_angle = STUNT_RENDER_CAMERA_ELEVATION;
	custom_camera.azimuth_angle = ANGLE_EIGHTH_TURN;
	activate_render_track(&track, 1);
	size_t ramp_size, empty_size;
	legacy_u32 *ramp = capture_complete_render(&ramp_size);
	activate_render_track(&track, 0);
	legacy_u32 *empty = capture_complete_render(&empty_size);
	assert(ramp_size == empty_size);
	size_t changed = 0;
	for (size_t pixel = 0; pixel < ramp_size; pixel++) {
		changed += ramp[pixel] != empty[pixel];
	}
	assert(changed > ROLL_RAMP_RENDER_MIN_PIXELS);
	free(ramp);
	free(empty);
	extended_track_unload();
	cstrk_free(&track);
	return 2;
}

#define HIGHWAY_RENDER_ID 111U
#define HIGHWAY_RENDER_REVERSED_ID 113U
#define HIGHWAY_RENDER_BASE_HEIGHT 2
#define HIGHWAY_RENDER_ASPHALT_MATERIAL 19U
#define HIGHWAY_RENDER_MARKING_MATERIAL 21U
#define HIGHWAY_RENDER_GRADE_COUNT 3U

static void check_highway_transition_paints(void)
{
	static const legacy_u8 road_materials[SURFACE_RENDER_COUNT] = {19U, 25U, 28U};
	static const legacy_u8 marking_materials[SURFACE_RENDER_COUNT] = {21U, 27U, 30U};
	const struct TRACKOBJECT *definition = &trkObjectList[HIGHWAY_RENDER_ID];
	const struct SHAPE3D *sources[STUNT_RENDER_DETAIL_COUNT] = {
		definition->ss_shapePtr,
		definition->ss_loShapePtr ? definition->ss_loShapePtr : definition->ss_shapePtr};
	for (legacy_u8 detail = 0; detail < STUNT_RENDER_DETAIL_COUNT; detail++) {
		const struct SHAPE3D *source = sources[detail];
		assert(source != NULL && source->shape3d_numpaints > CSTRK_ROAD_ICE);
		for (legacy_u8 surface = CSTRK_ROAD_PAVED; surface <= CSTRK_ROAD_ICE; surface++) {
			struct SHAPE3D transformed;
			assert(extended_geometry_transform_shape(source, &transformed, 0, surface,
													 PHYSICAL_MODEL_HIGHWAY_ENTRANCE));
			assert(transformed.shape3d_numpaints == 1U);
			const legacy_u8 *original = source->shape3d_primitives;
			const legacy_u8 *painted = transformed.shape3d_primitives;
			legacy_u16 roads = 0, structures = 0, animated = 0;
			for (legacy_u16 face = 0; face < source->shape3d_numprimitives; face++) {
				legacy_u8 count = original[0];
				assert(count >= SHAPE3D_PRIMITIVE_POLYGON_FIRST &&
					   count <= SHAPE3D_PRIMITIVE_POLYGON_LAST);
				legacy_u8 material = original[SHAPE3D_PRIMITIVE_HEADER_SIZE];
				legacy_u8 expected = original[SHAPE3D_PRIMITIVE_HEADER_SIZE + surface];
				if (material == HIGHWAY_RENDER_ASPHALT_MATERIAL ||
					material == HIGHWAY_RENDER_MARKING_MATERIAL) {
					/* The real gwro/zgwr road has four identical paint columns.
					 * The varying columns belong to the sign, not road surfaces. */
					for (legacy_u16 paint = 0; paint < source->shape3d_numpaints; paint++) {
						assert(original[SHAPE3D_PRIMITIVE_HEADER_SIZE + paint] == material);
					}
					if (supersight_enabled != 0) {
						expected = material == HIGHWAY_RENDER_ASPHALT_MATERIAL
									   ? road_materials[surface]
									   : marking_materials[surface];
					}
					roads++;
				} else {
					structures++;
					for (legacy_u16 paint = 1; paint < source->shape3d_numpaints; paint++) {
						animated += original[SHAPE3D_PRIMITIVE_HEADER_SIZE + paint] != material;
					}
				}
				assert(painted[0] == original[0] && painted[1] == original[1]);
				assert(painted[SHAPE3D_PRIMITIVE_HEADER_SIZE] == expected);
				assert(memcmp(painted + SHAPE3D_PRIMITIVE_HEADER_SIZE + 1U,
							  original + SHAPE3D_PRIMITIVE_HEADER_SIZE + source->shape3d_numpaints,
							  count) == 0);
				original += SHAPE3D_PRIMITIVE_HEADER_SIZE + source->shape3d_numpaints + count;
				painted += SHAPE3D_PRIMITIVE_HEADER_SIZE + 1U + count;
			}
			assert(roads != 0U && structures != 0U && animated != 0U);
			extended_geometry_free_shape(&transformed);
		}
	}
}

static legacy_u16 check_highway_transition_surfaces(void)
{
	static const legacy_s8 grades[HIGHWAY_RENDER_GRADE_COUNT] = {0, -1, 1};
	struct GAMESTATE saved = state;
	legacy_s16 saved_camera = cameramode;
	legacy_s8 saved_detail = detail_level;
	struct CSTRK_TRACK track;
	cstrk_init(&track);
	legacy_char error[GRASS_RENDER_ERROR_CAPACITY];
	assert(cstrk_create(&track, CSTRK_MAX_DIMENSION, CSTRK_MAX_DIMENSION, error, sizeof(error)));
	track.horizon = CSTRK_HORIZON_COUNTRY;
	struct CSTRK_OBJECT object = {0};
	object.x = SURFACE_RENDER_COLUMN;
	object.y = SURFACE_RENDER_ROW;
	object.width = object.height = object.occupied_mask = 1;
	object.base_height = HIGHWAY_RENDER_BASE_HEIGHT;
	track.objects = &object;
	track.object_count = 1;
	legacy_u16 frames = 0;
	for (legacy_u8 grade = 0; grade < HIGHWAY_RENDER_GRADE_COUNT; grade++) {
		object.legacy_id = grades[grade] < 0 ? HIGHWAY_RENDER_REVERSED_ID : HIGHWAY_RENDER_ID;
		object.slope_y = grades[grade];
		for (legacy_u16 y = 0; y <= track.height; y++) {
			legacy_s16 level =
				HIGHWAY_RENDER_BASE_HEIGHT + grades[grade] * ((legacy_s16)y - SURFACE_RENDER_ROW);
			legacy_u8 height = level < 0							  ? 0
							   : level > (legacy_s16)CSTRK_MAX_HEIGHT ? CSTRK_MAX_HEIGHT
																	  : (legacy_u8)level;
			for (legacy_u16 x = 0; x <= track.width; x++) {
				track.corner_heights[(size_t)y * (track.width + 1U) + x] = height;
			}
		}
		for (legacy_u8 detail = 0; detail < STUNT_RENDER_DETAIL_COUNT; detail++) {
			detail_level = detail ? STUNT_RENDER_LOW_DETAIL : 0;
			legacy_u32 *pixels[SURFACE_RENDER_COUNT];
			size_t sizes[SURFACE_RENDER_COUNT];
			for (legacy_u8 surface = CSTRK_ROAD_PAVED; surface <= CSTRK_ROAD_ICE; surface++) {
				object.surface = surface;
				activate_render_track(&track, 1);
				if (grade == 0 && detail == 0 && surface == CSTRK_ROAD_PAVED) {
					check_highway_transition_paints();
				}
				position_render_target(SURFACE_RENDER_COLUMN, SURFACE_RENDER_ROW);
				state.playerstate.car_position.ly =
					(HIGHWAY_RENDER_BASE_HEIGHT * CSTRK_LEVEL_WORLD_HEIGHT +
					 STUNT_RENDER_CAR_CLEARANCE) *
					GRASS_RENDER_POSITION_SCALE;
				state.playerstate.car_previous_position = state.playerstate.car_position;
				state.playerstate.car_crashBmpFlag = CRASH_EVENT_WATER;
				custom_camera.distance = SURFACE_RENDER_CAMERA_DISTANCE;
				custom_camera.elevation_angle = SURFACE_RENDER_CAMERA_ELEVATION;
				custom_camera.azimuth_angle = 0;
				pixels[surface] = capture_complete_render(&sizes[surface]);
				frames++;
			}
			for (legacy_u8 surface = CSTRK_ROAD_DIRT; surface <= CSTRK_ROAD_ICE; surface++) {
				assert(sizes[surface] == sizes[CSTRK_ROAD_PAVED]);
				if (supersight_enabled != 0) {
					size_t changed = 0;
					for (size_t pixel = 0; pixel < sizes[surface]; pixel++) {
						changed += pixels[surface][pixel] != pixels[CSTRK_ROAD_PAVED][pixel];
					}
					assert(changed > SURFACE_RENDER_MIN_CHANGED_PIXELS);
				}
			}
			for (legacy_u8 surface = CSTRK_ROAD_PAVED; surface <= CSTRK_ROAD_ICE; surface++) {
				free(pixels[surface]);
			}
		}
	}
	state = saved;
	cameramode = saved_camera;
	detail_level = saved_detail;
	extended_track_unload();
	track.objects = NULL;
	track.object_count = 0;
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
#ifdef RESTUNTS_WRAP_HILL_SHAPES
		frames += check_extended_fences();
#endif
		frames += check_rendered_surfaces(&track);
		frames += check_highway_transition_surfaces();
		frames += check_rendered_shore_materials(&track);
		frames += check_ground_grass_color();
		frames += check_classic_hill_rendering();
		frames += check_consecutive_hill_seams();
		frames += check_elevated_multitile_road();
		frames += check_overpass_surfaces();
		frames += check_sloped_stunt_rendering();
		frames += check_general_sloped_rendering();
		check_roll_ramp_mesh_geometry();
		frames += check_roll_ramp_pixels();
		frames += check_combined_stunt_rendering();
#ifdef RESTUNTS_WRAP_HILL_SHAPES
		check_combined_stunt_meshes();
		check_upright_sloped_plants();
		check_roll_ramp_submissions();
#endif
		frames += check_sloped_road_occlusion();
		frames += check_terrain_below_graded_turn();
		frames += check_generated_terrain_bounds();
		frames += check_reversed_native_slope_rendering();
	}
	extended_track_unload();
	cstrk_free(&track);
	printf("Extended 90x90 four-level terrain and native hill meshes rendered in %u frames.\n",
		   frames);
	call_exitlist();
	return 0;
}
