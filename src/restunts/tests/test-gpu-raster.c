#include "../c/renderer/gpu_raster.h"
#include "../c/platform.h"
#include "../c/shape3d/shape3d.h"
#include "../c/shape2d/shape2d_internal.h"
#include "../c/video/video_framebuffer.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_WIDTH 32
#define TEST_HEIGHT 24
#define TEST_PIXELS (TEST_WIDTH * TEST_HEIGHT)
#define TEST_COMMANDS 8U
#define TEST_EDGES 32U
#define TEST_LEFT 4
#define TEST_RIGHT (TEST_WIDTH - TEST_LEFT)
#define TEST_TOP 4
#define TEST_BOTTOM (TEST_HEIGHT - TEST_TOP)
#define TEST_BACKGROUND 3U
#define TEST_FIRST 7U
#define TEST_SECOND 9U
#define TEST_DECAL 11U
#define TEST_MASK 0xAAAAU
#define TEST_FAR 1.0
#define TEST_NEAR 2.0
#define TEST_FRONT 3.0
#define TEST_UNAVAILABLE 77
#define TEST_RECTANGLE_VERTICES 4U
#define TEST_CONCAVE_VERTICES 8U
#define TEST_SUPPORT_DEPTH 0.5
#define TEST_CLASSIC_CASES 10U
#define TEST_GHOST_COLOR 256U
#define TEST_SCREEN_SEGMENT 0xA000U
#define TEST_SCREEN_WIDTH (HIRES_WIDTH / HIRES_SCALE)
#define TEST_SCREEN_HEIGHT (HIRES_HEIGHT / HIRES_SCALE)

static struct GPU_RASTER_COMMAND commands[TEST_COMMANDS];
static struct GPU_RASTER_EDGE edges[TEST_EDGES];
static size_t command_count, edge_count;
static legacy_u32 output[TEST_PIXELS * GPU_RASTER_OUTPUT_WORDS];

static legacy_u32 depth_word(legacy_f32 depth)
{
	legacy_u32 word;
	memcpy(&word, &depth, sizeof(word));
	return word;
}

static legacy_f32 output_depth(legacy_u32 pixel)
{
	legacy_f32 depth;
	memcpy(&depth, &output[pixel * GPU_RASTER_OUTPUT_WORDS + 1U], sizeof(depth));
	return depth;
}

static void begin_scene(legacy_f32 depth, legacy_u32 family)
{
	memset(commands, 0, sizeof(commands));
	memset(edges, 0, sizeof(edges));
	command_count = edge_count = 0;
	for (legacy_u32 index = 0; index < TEST_PIXELS; index++) {
		output[index * GPU_RASTER_OUTPUT_WORDS] = TEST_BACKGROUND;
		output[index * GPU_RASTER_OUTPUT_WORDS + 1U] = depth_word(depth);
		output[index * GPU_RASTER_OUTPUT_WORDS + 2U] = family;
	}
}

static struct GPU_RASTER_COMMAND *rectangle(legacy_u16 color, legacy_u32 family, legacy_f64 depth)
{
	assert(command_count < TEST_COMMANDS && edge_count + 2U <= TEST_EDGES);
	struct GPU_RASTER_COMMAND *command = &commands[command_count++];
	command->material = (struct HIRES_BACKEND_MATERIAL){family, color, 0, 0, HIRES_PAINT_SOLID, 0};
	command->first_edge = (legacy_u32)edge_count;
	command->hires_edge_count = 2;
	command->top = TEST_TOP;
	command->bottom = TEST_BOTTOM;
	command->planar = 1;
	command->plane.origin = depth;
	edges[edge_count++] =
		(struct GPU_RASTER_EDGE){TEST_LEFT, TEST_TOP, depth, 0, 0, TEST_TOP, TEST_BOTTOM};
	edges[edge_count++] =
		(struct GPU_RASTER_EDGE){TEST_RIGHT, TEST_TOP, depth, 0, 0, TEST_TOP, TEST_BOTTOM};
	return command;
}

static void dispatch(legacy_u32 mode, legacy_s32 incremental,
					 const struct HIRES_DEPTH_PLANE *ground)
{
	assert(gpu_raster_submit(TEST_WIDTH, TEST_HEIGHT, 0, TEST_WIDTH, 0, TEST_HEIGHT, mode, commands,
							 command_count, edges, edge_count, ground, incremental, output));
}

static void assert_rectangle(legacy_u16 color, legacy_u32 family, legacy_f32 depth)
{
	for (legacy_u32 y = 0; y < TEST_HEIGHT; y++) {
		for (legacy_u32 x = 0; x < TEST_WIDTH; x++) {
			legacy_u32 pixel = y * TEST_WIDTH + x;
			legacy_u32 inside =
				x >= TEST_LEFT && x < TEST_RIGHT && y >= TEST_TOP && y < TEST_BOTTOM;
			assert(output[pixel * GPU_RASTER_OUTPUT_WORDS] ==
				   (inside ? GPU_RASTER_TOUCHED | color : TEST_BACKGROUND));
			if (inside) {
				assert(output_depth(pixel) == depth);
				assert(output[pixel * GPU_RASTER_OUTPUT_WORDS + 2U] == family);
			}
		}
	}
}

static void test_opaque_and_painter_order(void)
{
	for (legacy_s32 reverse = 0; reverse <= 1; reverse++) {
		begin_scene(0, 0);
		rectangle(reverse ? TEST_SECOND : TEST_FIRST, reverse ? 2U : 1U,
				  reverse ? TEST_NEAR : TEST_FAR);
		rectangle(reverse ? TEST_FIRST : TEST_SECOND, reverse ? 1U : 2U,
				  reverse ? TEST_FAR : TEST_NEAR);
		dispatch(GPU_RASTER_HIRES, 0, NULL);
		assert_rectangle(TEST_SECOND, 2U, TEST_NEAR);
	}
	begin_scene(0, 0);
	rectangle(TEST_SECOND, 2U, TEST_NEAR);
	rectangle(TEST_FIRST, 1U, TEST_FAR);
	dispatch(GPU_RASTER_CLASSIC, 0, NULL);
	assert_rectangle(TEST_FIRST, 0U, 0);
}

static void test_patterns_and_attached_support(void)
{
	begin_scene(0, 0);
	rectangle(TEST_FIRST, 1U, TEST_FAR);
	struct GPU_RASTER_COMMAND *masked = rectangle(TEST_SECOND, 2U, TEST_NEAR);
	masked->material.mode = HIRES_PAINT_PATTERN;
	masked->material.pattern = TEST_MASK;
	dispatch(GPU_RASTER_HIRES, 0, NULL);
	for (legacy_u32 y = TEST_TOP; y < TEST_BOTTOM; y++) {
		for (legacy_u32 x = TEST_LEFT; x < TEST_RIGHT; x++) {
			legacy_u16 color = (x & 1U) == 0 ? TEST_SECOND : TEST_FIRST;
			assert(output[(y * TEST_WIDTH + x) * GPU_RASTER_OUTPUT_WORDS] ==
				   (GPU_RASTER_TOUCHED | color));
		}
	}
	begin_scene(0, 0);
	rectangle(TEST_FIRST, 1U, TEST_NEAR);
	struct GPU_RASTER_COMMAND *decal = rectangle(TEST_DECAL, 1U, TEST_FAR);
	decal->material.flags = HIRES_BACKEND_DECAL;
	dispatch(GPU_RASTER_HIRES, 0, NULL);
	assert_rectangle(TEST_DECAL, 1U, TEST_NEAR);
	begin_scene(TEST_NEAR, 1U);
	decal = rectangle(TEST_DECAL, 1U, TEST_FAR);
	decal->material.flags = HIRES_BACKEND_DECAL;
	dispatch(GPU_RASTER_HIRES, 1, NULL);
	assert_rectangle(TEST_DECAL, 1U, TEST_NEAR);
}

static void test_ground_clipping(void)
{
	struct HIRES_DEPTH_PLANE ground = {0, 0, TEST_NEAR};
	begin_scene(0, 0);
	struct GPU_RASTER_COMMAND *line = rectangle(TEST_DECAL, 1U, TEST_FAR);
	line->material.flags = HIRES_BACKEND_GROUND_CLIP;
	dispatch(GPU_RASTER_HIRES, 0, &ground);
	for (legacy_u32 index = 0; index < TEST_PIXELS; index++) {
		assert(output[index * GPU_RASTER_OUTPUT_WORDS] == TEST_BACKGROUND);
	}
	begin_scene(0, 0);
	line = rectangle(TEST_DECAL, 1U, TEST_FRONT);
	line->material.flags = HIRES_BACKEND_GROUND_CLIP;
	dispatch(GPU_RASTER_HIRES, 0, &ground);
	assert_rectangle(TEST_DECAL, 1U, TEST_FRONT);
}

static void prepare_sprite(struct SPRITE *sprite, legacy_u8 *rows)
{
	legacy_u8 *screen = dos_memory_make_pointer(TEST_SCREEN_SEGMENT, 0);
	memset(sprite, 0, sizeof(*sprite));
	memset(screen, TEST_BACKGROUND, TEST_SCREEN_WIDTH * TEST_SCREEN_HEIGHT);
	sprite->sprite_bitmapptr = (struct SHAPE2D *)screen;
	sprite->sprite_lineofs = rows;
	sprite->sprite_right = TEST_WIDTH;
	sprite->sprite_pitch = sprite->sprite_buffer_width = TEST_SCREEN_WIDTH;
	sprite->sprite_raster_right = TEST_WIDTH;
	sprite->sprite_bottom = TEST_HEIGHT;
	for (legacy_u32 y = 0; y < TEST_HEIGHT; y++) {
		LEGACY_WRITE_U16_LE(rows + y * LEGACY_WORD_BYTES, y * TEST_SCREEN_WIDTH);
	}
}

static void test_classic_adapter(void)
{
	legacy_u8 *screen = dos_memory_make_pointer(TEST_SCREEN_SEGMENT, 0);
	legacy_u8 rows[TEST_HEIGHT * LEGACY_WORD_BYTES];
	struct SPRITE saved = drawing_sprite;
	prepare_sprite(&drawing_sprite, rows);
	const struct POINT2D points[] = {{TEST_LEFT, TEST_TOP},
									 {TEST_RIGHT, TEST_TOP},
									 {TEST_RIGHT, TEST_BOTTOM},
									 {TEST_LEFT, TEST_BOTTOM}};
	assert(!gpu_raster_classic_line(0, 0, 0, 0, TEST_FIRST));
	gpu_raster_classic_begin();
	assert(gpu_raster_classic_active());
	assert(gpu_raster_classic_polygon(TEST_FIRST, 0, 0, HIRES_PAINT_SOLID, TEST_RECTANGLE_VERTICES,
									  points, 0));
	assert(gpu_raster_classic_line(0, 0, TEST_WIDTH - 1, TEST_HEIGHT - 1, TEST_SECOND));
	assert(gpu_raster_classic_span(0, TEST_LEFT - 1, TEST_HEIGHT - 1, TEST_DECAL));
	assert(gpu_raster_classic_line(0, TEST_HEIGHT - 2, 0, TEST_HEIGHT - 2, TEST_DECAL));
	assert(screen[TEST_TOP * TEST_SCREEN_WIDTH + TEST_LEFT] == TEST_BACKGROUND);
	gpu_raster_classic_end();
	assert(!gpu_raster_classic_active());
	assert(!gpu_raster_classic_line(0, 0, 0, 0, TEST_FIRST));
	assert(screen[TEST_TOP * TEST_SCREEN_WIDTH + TEST_RIGHT - 1] == TEST_FIRST);
	assert(screen[0] == TEST_SECOND);
	assert(screen[(TEST_HEIGHT - 1) * TEST_SCREEN_WIDTH + TEST_WIDTH - 1] == TEST_SECOND);
	assert(screen[(TEST_HEIGHT - 1) * TEST_SCREEN_WIDTH + TEST_LEFT - 1] == TEST_DECAL);
	assert(screen[(TEST_HEIGHT - 2) * TEST_SCREEN_WIDTH] == TEST_DECAL);
	assert(screen[TEST_SCREEN_WIDTH + TEST_WIDTH - 1] == TEST_BACKGROUND);
	drawing_sprite = saved;
}

static void classic_fixture(legacy_u32 scene)
{
	const struct POINT2D rectangle[] = {{2, 2}, {8, 2}, {8, 8}, {2, 8}};
	const struct POINT2D descending[] = {{10, 20}, {20, 10}};
	const struct POINT2D point[] = {{5, 4}};
	const struct POINT2D triangle[] = {{3, 2}, {28, 9}, {8, 20}};
	const struct POINT2D clipped[] = {{-12, -8}, {20, 5}, {40, 28}, {2, 18}};
	switch (scene) {
		case 0:
			preRender_default(TEST_FIRST, TEST_RECTANGLE_VERTICES, rectangle);
			break;
		case 1:
			preRender_default(TEST_FIRST, 2U, descending);
			break;
		case 2:
			preRender_patterned(TEST_MASK, TEST_FIRST, 1U, point);
			break;
		case 3:
			preRender_line(0, 0, 6, 1, TEST_FIRST);
			break;
		case 4:
			preRender_line(-8, -2, 30, 10, TEST_FIRST);
			break;
		case 5:
			preRender_default(TEST_FIRST, 3U, triangle);
			break;
		case 6:
			preRender_default(TEST_FIRST, TEST_RECTANGLE_VERTICES, clipped);
			break;
		case 7:
			preRender_patterned(TEST_MASK, TEST_FIRST, TEST_RECTANGLE_VERTICES, rectangle);
			break;
		case 8:
			preRender_two_color(TEST_MASK, TEST_SECOND, TEST_FIRST, TEST_RECTANGLE_VERTICES,
								rectangle);
			break;
		case 9:
			preRender_default(TEST_GHOST_COLOR, 1U, point);
			break;
	}
}

static void test_classic_software_parity(void)
{
	legacy_u8 *screen = dos_memory_make_pointer(TEST_SCREEN_SEGMENT, 0);
	legacy_u8 rows[TEST_HEIGHT * LEGACY_WORD_BYTES];
	struct SPRITE saved = drawing_sprite;
	prepare_sprite(&drawing_sprite, rows);
	size_t pixels = TEST_SCREEN_WIDTH * TEST_SCREEN_HEIGHT;
	legacy_u8 *reference = malloc(pixels);
	assert(reference != NULL);
	for (legacy_u32 scene = 0; scene < TEST_CLASSIC_CASES; scene++) {
		for (legacy_s32 gpu = 0; gpu <= 1; gpu++) {
			memset(screen, TEST_BACKGROUND, pixels);
			if (gpu) {
				gpu_raster_classic_begin();
			}
			classic_fixture(scene);
			if (gpu) {
				gpu_raster_classic_end();
				for (size_t pixel = 0; pixel < pixels; pixel++) {
					if (screen[pixel] != reference[pixel]) {
						fprintf(stderr, "Classic case %u pixel %zu,%zu: CPU=%u GPU=%u\n", scene,
								pixel % TEST_SCREEN_WIDTH, pixel / TEST_SCREEN_WIDTH,
								reference[pixel], screen[pixel]);
						assert(screen[pixel] == reference[pixel]);
					}
				}
			} else {
				memcpy(reference, screen, pixels);
			}
		}
	}
	free(reference);
	drawing_sprite = saved;
}

static void test_companion_readback(void)
{
	legacy_u8 *screen = dos_memory_make_pointer(TEST_SCREEN_SEGMENT, 0);
	legacy_u8 rows[TEST_HEIGHT * LEGACY_WORD_BYTES];
	struct SPRITE sprite;
	prepare_sprite(&sprite, rows);
	hires_set_enabled(1);
	hires_set_render_scale(HIRES_MINIMUM_SCALE);
	assert(hires_begin(&sprite));
	hires_depth_begin(0, TEST_WIDTH, 0, TEST_HEIGHT);
	struct HIRES_RASTER_TARGET target;
	assert(hires_raster_prepare(&target));
	begin_scene(0, 0);
	rectangle(TEST_FIRST, 1U, TEST_NEAR);
	assert(gpu_raster_hires(&target, commands, command_count, edges, edge_count, NULL, 0));
	assert(target.inverse_depth[TEST_TOP * target.width + TEST_LEFT] == TEST_NEAR);
	assert(target.depth_family[TEST_TOP * target.width + TEST_LEFT] == 1U);
	hires_end();
	legacy_s32 width, height;
	const legacy_u8 *pixels = video_framebuffer(screen, &width, &height);
	assert(pixels != NULL);
	assert(pixels[TEST_TOP * width + TEST_LEFT] == TEST_FIRST);
	assert(pixels[0] == TEST_BACKGROUND);
	hires_shutdown();
}

static void backend_fixture(legacy_s32 device, enum GRAPHICS_RENDERER renderer, legacy_u8 *colors,
							legacy_f32 *depths, legacy_u32 *families)
{
	legacy_char error[GRAPHICS_ERROR_CAPACITY];
	assert(graphics_select_renderer(
		renderer, renderer == GRAPHICS_RENDERER_SOFTWARE ? GRAPHICS_DEVICE_DEFAULT : device, error,
		sizeof(error)));
	legacy_u8 *screen = dos_memory_make_pointer(TEST_SCREEN_SEGMENT, 0);
	legacy_u8 rows[TEST_HEIGHT * LEGACY_WORD_BYTES];
	struct SPRITE sprite;
	prepare_sprite(&sprite, rows);
	hires_set_enabled(1);
	hires_set_render_scale(HIRES_MINIMUM_SCALE);
	assert(hires_begin(&sprite));
	hires_depth_begin(0, TEST_WIDTH, 0, TEST_HEIGHT);
	struct HIRES_RASTER_TARGET target;
	assert(hires_raster_prepare(&target));
	hires_backend_begin(NULL);
	const struct HIRES_BACKEND_VERTEX backing[] = {{0, 0, TEST_FAR},
												   {TEST_WIDTH, 0, TEST_FAR},
												   {TEST_WIDTH, TEST_HEIGHT, TEST_FAR},
												   {0, TEST_HEIGHT, TEST_FAR}};
	struct HIRES_BACKEND_MATERIAL paint = {2U, TEST_BACKGROUND, 0, 0, HIRES_PAINT_SOLID, 0};
	hires_backend_polygon(backing, TEST_RECTANGLE_VERTICES, &paint);
	const struct HIRES_BACKEND_VERTEX concave[] = {
		{4, 4, TEST_NEAR},	 {28, 4, TEST_NEAR},  {28, 20, TEST_NEAR}, {20, 20, TEST_NEAR},
		{20, 12, TEST_NEAR}, {12, 12, TEST_NEAR}, {12, 20, TEST_NEAR}, {4, 20, TEST_NEAR}};
	paint = (struct HIRES_BACKEND_MATERIAL){
		1U, TEST_FIRST, TEST_SECOND, TEST_MASK, HIRES_PAINT_ALTERNATE, 0};
	hires_backend_polygon(concave, TEST_CONCAVE_VERTICES, &paint);
	const struct HIRES_BACKEND_VERTEX decal[] = {{8, 8, TEST_SUPPORT_DEPTH},
												 {24, 8, TEST_SUPPORT_DEPTH},
												 {24, 16, TEST_SUPPORT_DEPTH},
												 {8, 16, TEST_SUPPORT_DEPTH}};
	paint = (struct HIRES_BACKEND_MATERIAL){1U, TEST_DECAL,		   0,
											0,	HIRES_PAINT_SOLID, HIRES_BACKEND_DECAL};
	hires_backend_polygon(decal, TEST_RECTANGLE_VERTICES, &paint);
	const struct HIRES_BACKEND_VERTEX mask[] = {
		{-4, 2, TEST_FRONT}, {12, 2, TEST_FRONT}, {12, 10, TEST_FRONT}, {-4, 10, TEST_FRONT}};
	paint = (struct HIRES_BACKEND_MATERIAL){3U, TEST_SECOND, 0, TEST_MASK, HIRES_PAINT_PATTERN, 0};
	hires_backend_polygon(mask, TEST_RECTANGLE_VERTICES, &paint);
	(void)hires_backend_end(&target, 0);
	hires_end();
	legacy_s32 width, height;
	const legacy_u8 *pixels = video_framebuffer(screen, &width, &height);
	assert(pixels != NULL && width == TEST_SCREEN_WIDTH && height == TEST_SCREEN_HEIGHT);
	memcpy(colors, pixels, TEST_SCREEN_WIDTH * TEST_SCREEN_HEIGHT);
	memcpy(depths, target.inverse_depth, TEST_SCREEN_WIDTH * TEST_SCREEN_HEIGHT * sizeof(*depths));
	memcpy(families, target.depth_family,
		   TEST_SCREEN_WIDTH * TEST_SCREEN_HEIGHT * sizeof(*families));
	hires_shutdown();
}

static void test_software_backend_parity(legacy_s32 device)
{
	size_t pixels = TEST_SCREEN_WIDTH * TEST_SCREEN_HEIGHT;
	legacy_u8 *colors = malloc(pixels * 2U);
	legacy_f32 *depths = malloc(pixels * 2U * sizeof(*depths));
	legacy_u32 *families = malloc(pixels * 2U * sizeof(*families));
	assert(colors != NULL && depths != NULL && families != NULL);
	backend_fixture(device, GRAPHICS_RENDERER_SOFTWARE, colors, depths, families);
	backend_fixture(device, GRAPHICS_RENDERER_NATIVE, colors + pixels, depths + pixels,
					families + pixels);
	assert(memcmp(colors, colors + pixels, pixels) == 0);
	assert(memcmp(depths, depths + pixels, pixels * sizeof(*depths)) == 0);
	assert(memcmp(families, families + pixels, pixels * sizeof(*families)) == 0);
	hires_backend_shutdown();
	free(colors);
	free(depths);
	free(families);
}

legacy_int main(void)
{
	assert(graphics_renderer() == GRAPHICS_RENDERER_SOFTWARE);
	legacy_char error[GRAPHICS_ERROR_CAPACITY];
	legacy_u32 unavailable_input = 0, unavailable_output = 0;
	assert(!graphics_gpu_dispatch(&unavailable_input, 1, &unavailable_output, 1, 1, error,
								  sizeof(error)));
	assert(error[0] != 0 && unavailable_output == 0);
	struct GRAPHICS_DEVICE devices[GRAPHICS_DEVICE_CAPACITY];
	legacy_u32 count = graphics_enumerate_devices(devices, GRAPHICS_DEVICE_CAPACITY);
	if (count == 0) {
		puts("No " GRAPHICS_RENDERER_NATIVE_LABEL " compute device is available; "
			 "GPU raster test skipped.");
		return TEST_UNAVAILABLE;
	}
	if (!graphics_select_renderer(GRAPHICS_RENDERER_NATIVE, devices[0].index, error,
								  sizeof(error))) {
		fprintf(stderr,
				"Cannot initialize enumerated " GRAPHICS_RENDERER_NATIVE_LABEL " device: %s\n",
				error);
		return 1;
	}
	test_opaque_and_painter_order();
	test_patterns_and_attached_support();
	test_ground_clipping();
	test_classic_adapter();
	test_classic_software_parity();
	test_companion_readback();
	test_software_backend_parity(devices[0].index);
	gpu_raster_shutdown();
	graphics_shutdown();
	puts(GRAPHICS_RENDERER_NATIVE_LABEL
		 " shader dispatch, coverage, depth, patterns, classic batches, software parity and "
		 "CPU companion "
		 "readback passed.");
	return 0;
}
