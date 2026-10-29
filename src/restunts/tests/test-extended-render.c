/* Reuse the live renderer/resource fixture, including its state/RNG checks.
 * This test compares visible geometry, rather than storing a renderer golden. */
#define stuntsmain legacy_render_replay_fixture_main
#include "test-render-replay.c"
#undef stuntsmain

#include "../c/cstrk.h"
#include "../c/extended_track.h"
#include "../c/skybox.h"
#include "../c/race_graphics.h"
#include "../c/trackdata_layout.h"

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
		frames += check_ground_grass_color();
		frames += check_classic_hill_rendering();
	}
	extended_track_unload();
	cstrk_free(&track);
	printf("Extended 90x90 four-level terrain and native hill meshes rendered in %u frames.\n",
		   frames);
	call_exitlist();
	return 0;
}
