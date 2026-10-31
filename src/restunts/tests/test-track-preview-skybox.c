#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <SDL3/SDL.h>
#include "../c/externs.h"
#include "../c/frame_internal.h"
#include "../c/hires/hires.h"
#include "../c/menu/menu_internal.h"
#include "../c/platform.h"
#include "../c/projection.h"
#include "../c/shape2d/shape2d.h"
#include "../c/shape3d/shape3d.h"
#include "../c/track/track_objects.h"
#include "../c/track/trackdata_layout.h"
#include "../c/skybox.h"
#include "../c/hires/skybox_hires.h"

#define SKY_HEIGHT 8
#define HORIZON 80
#define LEGACY_SKY_COLOR 4
#define PREVIEW_SKYBOX_RASTER_PASSES 2U
#define PREVIEW_MAX_RASTER_PASSES (PREVIEW_SKYBOX_RASTER_PASSES + 1U)
#define PREVIEW_TEST_VERTEX_COUNT 4U
#define PREVIEW_TEST_PAINT_COUNT 2U
#define PREVIEW_TEST_HALF_PANEL 160
#define PREVIEW_TEST_OFFSET 80
#define PREVIEW_TEST_NEAR_DISTANCE 400
#define PREVIEW_TEST_FAR_DISTANCE (PREVIEW_TEST_NEAR_DISTANCE * 2)
#define PREVIEW_TEST_NEAR_COLOR 7
#define PREVIEW_TEST_FAR_COLOR 8
#define PREVIEW_TEST_NEAR_OBJECT 1U
#define PREVIEW_TEST_FAR_OBJECT 2U
#define PREVIEW_TEST_NEAR_COLUMN 0U
#define PREVIEW_TEST_FAR_COLUMN 1U
#define PREVIEW_TEST_NEAR_ROW 0U
#define PREVIEW_TEST_FAR_ROW 1U
#define PREVIEW_TEST_NEAR_SAMPLE_X 80
#define PREVIEW_TEST_OVERLAP_SAMPLE_X 160
#define PREVIEW_TEST_FAR_SAMPLE_X 200

static struct SPRITE preview, screen_target;
static legacy_u8 preview_rows[200 * 2], screen_rows[200 * 2];
static legacy_u8 empty_track[900];
static legacy_u8 *screen;

static legacy_u8 fixture_color(legacy_s32 x, legacy_s32 y)
{
	return 17 + (x + y * HIRES_WIDTH) % 63;
}

static void write_fixture(void)
{
	SDL_Surface *image =
		SDL_CreateSurface(HIRES_WIDTH, SKY_HEIGHT * HIRES_SCALE, SDL_PIXELFORMAT_RGB24);
	assert(image != NULL);
	for (legacy_s32 y = 0; y < image->h; y++) {
		for (legacy_s32 x = 0; x < image->w; x++) {
			legacy_u8 shade = (fixture_color(x, y) - 16U) * 255U / 63U;
			assert(SDL_WriteSurfacePixel(image, x, y, shade, shade, shade, 255));
		}
	}
	assert(SDL_SavePNG(image, "skyboxes/country-sce3.png"));
	SDL_DestroySurface(image);
}

static void prepare_sprite(struct SPRITE *sprite, legacy_u8 *rows, legacy_u16 segment,
						   legacy_u16 header_size)
{
	sprite->sprite_bitmapptr = dos_memory_make_pointer(segment, 0);
	sprite->sprite_lineofs = rows;
	sprite->sprite_right = sprite->sprite_pitch = sprite->sprite_buffer_width = 320;
	sprite->sprite_raster_right = 320;
	sprite->sprite_bottom = 200;
	for (legacy_u32 row = 0; row < 200; row++) {
		LEGACY_WRITE_U16_LE(rows + row * 2, header_size + row * 320);
	}
	if (header_size != 0) {
		memset(sprite->sprite_bitmapptr, 0, header_size);
		sprite->sprite_bitmapptr->width = 320;
		sprite->sprite_bitmapptr->height = 200;
	}
}

static void assert_frame(legacy_s32 enhanced)
{
	legacy_s32 width, height;
	const legacy_u8 *pixels = hires_framebuffer(screen, &width, &height);
	legacy_s32 scale = enhanced ? hires_render_scale() : HIRES_MINIMUM_SCALE;
	legacy_s32 source_step = HIRES_SCALE / scale;
	assert(width == 320 * scale && height == 200 * scale);
	for (legacy_s32 y = 0; y < height; y++) {
		for (legacy_s32 x = 0; x < width; x++) {
			legacy_u8 expected = skybox.sky_color;
			if (y >= HORIZON * scale) {
				expected = skybox.ground_color;
			} else if (y >= (HORIZON - SKY_HEIGHT) * scale) {
				expected = enhanced && scale != HIRES_MINIMUM_SCALE
							   ? fixture_color(x * source_step + source_step / 2,
											   (y - (HORIZON - SKY_HEIGHT) * scale) * source_step +
												   source_step / 2)
							   : LEGACY_SKY_COLOR;
			}
			assert(pixels[y * width + x] == expected);
		}
	}
}

static void draw_preview_with_bounded_passes(void)
{
	/* Start without a cached target so generations count completed raster
	 * passes, excluding invalidations of artwork from the previous preview. */
	hires_forget(preview.sprite_bitmapptr);
	legacy_u32 generation = hires_generation();
	draw_track_preview();
	/* Loading artwork may draw the horizon strips before the scene. The
	 * number of full raster passes must not grow with the track's cell count. */
	assert(hires_generation() - generation <= PREVIEW_MAX_RASTER_PASSES);
}

static void test_preview(legacy_s32 enhanced, legacy_s32 scale)
{
	hires_set_enabled(enhanced);
	if (enhanced) {
		hires_set_render_scale(scale);
	}
	loaded_skybox_index = 4;
	sprite_select_target(&preview);
	sprite_clear_target((legacy_u8)skybox.ground_color);
	draw_preview_with_bounded_passes();
	/* The track menu releases the source artwork before presenting its window. */
	unload_skybox();
	sprite_select_target(&screen_target);
	sprite_clear_target(0);
	/* Cover both initial menu dissolve and later immediate window refresh. */
	for (legacy_u16 phase = 0; phase < 4; phase++) {
		sprite_draw_dissolve_phase(preview.sprite_bitmapptr, phase);
	}
	assert_frame(enhanced);
	sprite_clear_target(0);
	sprite_putimage(preview.sprite_bitmapptr);
	assert_frame(enhanced);
	assert(screen[(HORIZON - SKY_HEIGHT) * 320] == LEGACY_SKY_COLOR);
}

static void test_populated_preview(legacy_s32 scale)
{
	legacy_u8 elements[TRACK_GRID_SIZE * TRACK_GRID_SIZE] = {0};
	legacy_u8 vertices[PREVIEW_TEST_VERTEX_COUNT * SHAPE3D_VERTEX_SIZE];
	legacy_u8 primitive[] = {PREVIEW_TEST_VERTEX_COUNT, 0, 0, 1, 0, 1, 2, 3, 0, 0};
	legacy_u8 visibility[SHAPE3D_VISIBILITY_MASK_SIZE];
	legacy_u8 front_facing[SHAPE3D_VISIBILITY_MASK_SIZE] = {0};
	legacy_s16 colors[PREVIEW_TEST_PAINT_COUNT] = {PREVIEW_TEST_NEAR_COLOR, PREVIEW_TEST_FAR_COLOR};
	legacy_s16 patterns[PREVIEW_TEST_PAINT_COUNT] = {0};
	const struct VECTOR panel[PREVIEW_TEST_VERTEX_COUNT] = {
		{-PREVIEW_TEST_HALF_PANEL, -PREVIEW_TEST_HALF_PANEL, 0},
		{-PREVIEW_TEST_HALF_PANEL, PREVIEW_TEST_HALF_PANEL, 0},
		{PREVIEW_TEST_HALF_PANEL, PREVIEW_TEST_HALF_PANEL, 0},
		{PREVIEW_TEST_HALF_PANEL, -PREVIEW_TEST_HALF_PANEL, 0}};
	struct SHAPE3D shape = {PREVIEW_TEST_VERTEX_COUNT,
							vertices,
							1,
							PREVIEW_TEST_PAINT_COUNT,
							primitive,
							visibility,
							front_facing};
	memset(visibility, LEGACY_U8_MAX, sizeof(visibility));
	for (legacy_u16 index = 0; index < PREVIEW_TEST_VERTEX_COUNT; index++) {
		shape3d_vertex_write(&shape, index, &panel[index]);
	}
	for (legacy_u16 row = 0; row < TRACK_GRID_SIZE; row++) {
		trackrows[row] = terrainrows[row] = row * TRACK_GRID_SIZE;
	}
	track_column_centers[PREVIEW_TEST_NEAR_COLUMN] = -PREVIEW_TEST_OFFSET;
	track_column_centers[PREVIEW_TEST_FAR_COLUMN] = PREVIEW_TEST_OFFSET;
	track_row_centers[PREVIEW_TEST_NEAR_ROW] = PREVIEW_TEST_NEAR_DISTANCE;
	track_row_centers[PREVIEW_TEST_FAR_ROW] = PREVIEW_TEST_FAR_DISTANCE;
	memset(&trkObjectList[PREVIEW_TEST_NEAR_OBJECT], 0, sizeof(struct TRACKOBJECT));
	memset(&trkObjectList[PREVIEW_TEST_FAR_OBJECT], 0, sizeof(struct TRACKOBJECT));
	trkObjectList[PREVIEW_TEST_NEAR_OBJECT].ss_loShapePtr = &shape;
	trkObjectList[PREVIEW_TEST_FAR_OBJECT].ss_loShapePtr = &shape;
	trkObjectList[PREVIEW_TEST_FAR_OBJECT].ss_surfaceType = 1;
	elements[trackrows[PREVIEW_TEST_NEAR_ROW] + PREVIEW_TEST_NEAR_COLUMN] =
		PREVIEW_TEST_NEAR_OBJECT;
	elements[trackrows[PREVIEW_TEST_FAR_ROW] + PREVIEW_TEST_FAR_COLUMN] = PREVIEW_TEST_FAR_OBJECT;
	track_element_map = elements;
	material_clrlist_ptr_cpy = material_clrlist2_ptr_cpy = colors;
	material_patlist_ptr_cpy = material_patlist2_ptr_cpy = patterns;
	hires_set_enabled(1);
	hires_set_render_scale(scale);
	sprite_select_target(&preview);
	sprite_clear_target((legacy_u8)skybox.ground_color);
	draw_preview_with_bounded_passes();
	sprite_select_target(&screen_target);
	sprite_clear_target(0);
	sprite_putimage(preview.sprite_bitmapptr);
	legacy_s32 width, height;
	const legacy_u8 *pixels = hires_framebuffer(screen, &width, &height);
	assert(width == HIRES_WIDTH / HIRES_SCALE * scale &&
		   height == HIRES_HEIGHT / HIRES_SCALE * scale);
	/* Distinct cells must both survive the batch. The farther panel is queued
	 * later but cannot cover the nearer panel where their projections overlap. */
	assert(pixels[HORIZON * scale * width + PREVIEW_TEST_NEAR_SAMPLE_X * scale] ==
		   PREVIEW_TEST_NEAR_COLOR);
	assert(pixels[HORIZON * scale * width + PREVIEW_TEST_OVERLAP_SAMPLE_X * scale] ==
		   PREVIEW_TEST_NEAR_COLOR);
	assert(pixels[HORIZON * scale * width + PREVIEW_TEST_FAR_SAMPLE_X * scale] ==
		   PREVIEW_TEST_FAR_COLOR);
	assert(shape3d_queued_primitive_count() == 0);
	track_element_map = empty_track;
}

legacy_int main(void)
{
	assert(SDL_Init(0));
	assert(SDL_CreateDirectory("fixtures"));
	assert(chdir("fixtures") == 0);
	assert(SDL_CreateDirectory("skyboxes"));
	write_fixture();
	legacy_u8 palette[768] = {0};
	for (legacy_u32 index = 0; index < 64; index++) {
		memset(palette + (16 + index) * 3, index, 3);
	}
	skybox_hires_set_palette(palette);
	prepare_sprite(&preview, preview_rows, 0x5000, SHAPE2D_HEADER_SIZE);
	prepare_sprite(&screen_target, screen_rows, 0xA000, 0);
	screen = (legacy_u8 *)screen_target.sprite_bitmapptr;
	struct SHAPE2D *strip = dos_memory_make_pointer(0x3000, 0);
	memset(strip, 0, SHAPE2D_HEADER_SIZE);
	strip->width = 320;
	strip->height = SKY_HEIGHT;
	memset((legacy_u8 *)strip + SHAPE2D_HEADER_SIZE, LEGACY_SKY_COLOR, 320 * SKY_HEIGHT);
	for (legacy_u32 index = 0; index < 4; index++) {
		skyboxes[index] = strip;
		skybox.heights[index] = SKY_HEIGHT;
	}
	skybox.minimum_height = skybox.maximum_height = SKY_HEIGHT;
	skybox.sky_color = 3;
	skybox.ground_color = 6;
	track_element_map = track_terrain_map = empty_track;
	track_preview_camera_x = track_preview_camera_y = track_preview_camera_z = 0;
	track_preview_target_x = track_preview_target_y = 0;
	track_preview_target_z = 1000;
	track_preview_horizon_vector.x = track_preview_horizon_vector.y = 0;
	track_preview_horizon_vector.z = 1000;
	projection_center_x = projection_focal_length_x = projection_focal_length_y = 160;
	projection_center_y = HORIZON;
	test_preview(0, HIRES_SCALE);
	test_preview(1, HIRES_SCALE);
	test_preview(1, HIRES_MEDIUM_SCALE);
	test_preview(1, HIRES_MINIMUM_SCALE);
	test_preview(1, HIRES_MEDIUM_SCALE);
	test_preview(1, HIRES_SCALE);
	test_preview(0, HIRES_SCALE);
	test_populated_preview(HIRES_SCALE);
	test_populated_preview(HIRES_MEDIUM_SCALE);
	test_populated_preview(HIRES_MINIMUM_SCALE);
	hires_shutdown();
	assert(remove("skyboxes/country-sce3.png") == 0);
	assert(rmdir("skyboxes") == 0);
	assert(chdir("..") == 0);
	assert(rmdir("fixtures") == 0);
	SDL_Quit();
	puts("Track preview skybox and menu presentation tests passed.");
	return 0;
}
