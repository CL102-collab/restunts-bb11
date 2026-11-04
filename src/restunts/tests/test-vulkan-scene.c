#include "../c/renderer/graphics_backend.h"
#include "../c/renderer/vulkan_scene.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_UNAVAILABLE 77
#define TEST_WIDTH 64
#define TEST_HEIGHT 64
#define TEST_RESIZED_WIDTH 96
#define TEST_RESIZED_HEIGHT 80
#define TEST_ROAD_WIDTH 1280
#define TEST_ROAD_HEIGHT 800
#define TEST_ROAD_SAMPLE_X 640
#define TEST_ROAD_SAMPLE_Y 600
#define TEST_GRASS_TRIANGLE 934U
#define TEST_ROAD_TRIANGLE 936U
#define TEST_ROAD_COLOR 28U
#define TEST_GRASS_COLOR 99U
#define TEST_CLIPPED_GRASS_TRIANGLES 3U
#define TEST_CLIPPED_TRIANGLES 8U
#define TEST_CLIPPED_SAMPLE_X 800
#define TEST_CLIPPED_SAMPLE_Y 698
#define TEST_QUAD_VERTICES 6U
#define TEST_TRIANGLE_VERTICES 3U
#define TEST_RING_SUBMISSIONS 19U
#define TEST_SOLID 0U
#define TEST_PATTERN 1U
#define TEST_ALTERNATE 2U
#define TEST_SHADOW 3U
#define TEST_SHADOW_ALPHA 128U
#define TEST_BACKGROUND 1U
#define TEST_DECAL 2U
#define TEST_RED 1U
#define TEST_GREEN 2U
#define TEST_BLUE 3U
#define TEST_OPAQUE_ALPHA 255U
#define TEST_PATTERN_BITS 0xAAAAU

static legacy_u32 palette[VULKAN_SCENE_PALETTE_COLORS];
static legacy_u32 pixels[TEST_ROAD_WIDTH * TEST_ROAD_HEIGHT];
static legacy_char error[GRAPHICS_ERROR_CAPACITY];

static void require(legacy_s32 success)
{
	if (!success) {
		fprintf(stderr, "Vulkan scene operation failed: %s\n", error);
		abort();
	}
}

static legacy_u32 rgba(legacy_u32 argb)
{
	legacy_u8 bytes[4] = {(legacy_u8)(argb >> 16U), (legacy_u8)(argb >> 8U), (legacy_u8)argb,
						  (legacy_u8)(argb >> 24U)};
	legacy_u32 result;
	memcpy(&result, bytes, sizeof(result));
	return result;
}

static void expect_pixel(legacy_s32 x, legacy_s32 y, legacy_s32 width, legacy_u32 expected)
{
	legacy_u32 actual = pixels[(size_t)y * width + x];
	if (actual != expected) {
		fprintf(stderr, "Pixel (%d,%d): expected %08x, got %08x\n", x, y, (legacy_uint)expected,
				(legacy_uint)actual);
		abort();
	}
}

static struct VULKAN_SCENE_VERTEX vertex(legacy_f32 x, legacy_f32 y, legacy_f32 depth,
										 legacy_u32 color, legacy_u32 mode, legacy_u32 flags)
{
	struct VULKAN_SCENE_VERTEX result = {x,	   y,	 depth, color, TEST_GREEN, TEST_PATTERN_BITS,
										 mode, flags};
	return result;
}

static void quad(struct VULKAN_SCENE_VERTEX *vertices, legacy_f32 left, legacy_f32 top,
				 legacy_f32 right, legacy_f32 bottom, legacy_f32 depth, legacy_u32 color,
				 legacy_u32 mode, legacy_u32 flags)
{
	vertices[0] = vertex(left, top, depth, color, mode, flags);
	vertices[1] = vertex(right, top, depth, color, mode, flags);
	vertices[2] = vertex(right, bottom, depth, color, mode, flags);
	vertices[3] = vertices[0];
	vertices[4] = vertices[2];
	vertices[5] = vertex(left, bottom, depth, color, mode, flags);
}

static void submit(const struct VULKAN_SCENE_VERTEX *vertices, size_t count, legacy_s32 preserve,
				   legacy_s32 classic)
{
	require(vulkan_scene_submit(vertices, count, TEST_WIDTH, TEST_HEIGHT, 0, TEST_WIDTH, 0,
								TEST_HEIGHT, preserve, classic, error, sizeof(error)));
}

static void readback(legacy_s32 width, legacy_s32 height)
{
	memset(pixels, 0xCD, sizeof(pixels));
	require(vulkan_scene_readback(pixels, width, height, error, sizeof(error)));
}

static void test_triangle_and_clear(void)
{
	struct VULKAN_SCENE_VERTEX vertices[TEST_TRIANGLE_VERTICES] = {
		vertex(2, 2, 0.5f, TEST_RED, TEST_SOLID, 0), vertex(30, 2, 0.5f, TEST_RED, TEST_SOLID, 0),
		vertex(2, 30, 0.5f, TEST_RED, TEST_SOLID, 0)};
	submit(vertices, TEST_TRIANGLE_VERTICES, 0, 0);
	readback(TEST_WIDTH, TEST_HEIGHT);
	expect_pixel(4, 4, TEST_WIDTH, rgba(palette[TEST_RED]));
	expect_pixel(40, 40, TEST_WIDTH, 0);
	submit(NULL, 0, 0, 0);
	readback(TEST_WIDTH, TEST_HEIGHT);
	for (size_t pixel = 0; pixel < TEST_WIDTH * TEST_HEIGHT; pixel++) {
		assert(pixels[pixel] == 0);
	}
}

static void test_depth_and_painter_order(void)
{
	struct VULKAN_SCENE_VERTEX vertices[TEST_QUAD_VERTICES * 2U];
	quad(vertices, 4, 4, 28, 28, 0.8f, TEST_RED, TEST_SOLID, 0);
	quad(vertices + TEST_QUAD_VERTICES, 4, 4, 28, 28, 0.2f, TEST_BLUE, TEST_SOLID, 0);
	submit(vertices, TEST_QUAD_VERTICES * 2U, 0, 0);
	readback(TEST_WIDTH, TEST_HEIGHT);
	expect_pixel(10, 10, TEST_WIDTH, rgba(palette[TEST_RED]));
	submit(vertices, TEST_QUAD_VERTICES * 2U, 0, 1);
	readback(TEST_WIDTH, TEST_HEIGHT);
	expect_pixel(10, 10, TEST_WIDTH, rgba(palette[TEST_BLUE]));
	quad(vertices, 4, 4, 28, 28, 0.2f, TEST_BLUE, TEST_SOLID, 0);
	quad(vertices + TEST_QUAD_VERTICES, 4, 4, 28, 28, 0.8f, TEST_RED, TEST_SOLID, 0);
	submit(vertices, TEST_QUAD_VERTICES * 2U, 0, 0);
	readback(TEST_WIDTH, TEST_HEIGHT);
	expect_pixel(10, 10, TEST_WIDTH, rgba(palette[TEST_RED]));
}

static void test_coplanar_road_order_and_palette_replay(void)
{
	/* Actual road/grass geometry from the full HyperVision camera. At (640.5,600.5),
	 * their interpolated inverse depths differ by only 3.9e-10. Huge offscreen
	 * vertices amplify clipping/interpolation rounding, so later road must win. */
	struct VULKAN_SCENE_VERTEX vertices[(TEST_ROAD_TRIANGLE + 1U) * TEST_TRIANGLE_VERTICES];
	struct VULKAN_SCENE_VERTEX *grass = vertices + TEST_GRASS_TRIANGLE * TEST_TRIANGLE_VERTICES;
	struct VULKAN_SCENE_VERTEX *road = vertices + TEST_ROAD_TRIANGLE * TEST_TRIANGLE_VERTICES;
	memset(vertices, 0, sizeof(vertices));
	grass[0] = vertex(-41450.0f, 1123.3333740234375f, 0.0833333358168602f, TEST_GRASS_COLOR,
					  TEST_SOLID, 0);
	grass[1] = vertex(1246.970947265625f, 412.0055236816406f, 0.0013831258984282613f,
					  TEST_GRASS_COLOR, TEST_SOLID, 0);
	grass[2] =
		vertex(37210.0f, 1123.3333740234375f, 0.0833333358168602f, TEST_GRASS_COLOR, TEST_SOLID, 0);
	road[0] =
		vertex(-11320.0f, 1123.3333740234375f, 0.0833333358168602f, TEST_ROAD_COLOR, TEST_SOLID, 0);
	road[1] = vertex(746.0082397460938f, 411.9067077636719f, 0.0013717421097680926f,
					 TEST_ROAD_COLOR, TEST_SOLID, 0);
	road[2] =
		vertex(7080.0f, 1123.3333740234375f, 0.0833333358168602f, TEST_ROAD_COLOR, TEST_SOLID, 0);
	require(vulkan_scene_submit(vertices, (TEST_ROAD_TRIANGLE + 1U) * TEST_TRIANGLE_VERTICES,
								TEST_ROAD_WIDTH, TEST_ROAD_HEIGHT, 0, TEST_ROAD_WIDTH, 0,
								TEST_ROAD_HEIGHT, 0, 0, error, sizeof(error)));
	readback(TEST_ROAD_WIDTH, TEST_ROAD_HEIGHT);
	expect_pixel(TEST_ROAD_SAMPLE_X, TEST_ROAD_SAMPLE_Y, TEST_ROAD_WIDTH,
				 rgba(palette[TEST_ROAD_COLOR]));
	/* Preserved batches and shadows must retain the coplanar surface ordering. */
	require(vulkan_scene_submit(vertices, (TEST_GRASS_TRIANGLE + 1U) * TEST_TRIANGLE_VERTICES,
								TEST_ROAD_WIDTH, TEST_ROAD_HEIGHT, 0, TEST_ROAD_WIDTH, 0,
								TEST_ROAD_HEIGHT, 0, 0, error, sizeof(error)));
	struct VULKAN_SCENE_VERTEX shadow[TEST_TRIANGLE_VERTICES];
	memset(shadow, 0, sizeof(shadow));
	for (size_t index = 0; index < TEST_TRIANGLE_VERTICES; index++) {
		shadow[index].mode = TEST_SHADOW;
	}
	require(vulkan_scene_submit(shadow, TEST_TRIANGLE_VERTICES, TEST_ROAD_WIDTH, TEST_ROAD_HEIGHT,
								0, TEST_ROAD_WIDTH, 0, TEST_ROAD_HEIGHT, 1, 0, error,
								sizeof(error)));
	require(vulkan_scene_submit(road, TEST_TRIANGLE_VERTICES, TEST_ROAD_WIDTH, TEST_ROAD_HEIGHT, 0,
								TEST_ROAD_WIDTH, 0, TEST_ROAD_HEIGHT, 1, 0, error, sizeof(error)));
	readback(TEST_ROAD_WIDTH, TEST_ROAD_HEIGHT);
	expect_pixel(TEST_ROAD_SAMPLE_X, TEST_ROAD_SAMPLE_Y, TEST_ROAD_WIDTH,
				 rgba(palette[TEST_ROAD_COLOR]));
	legacy_u32 original = palette[TEST_ROAD_COLOR];
	palette[TEST_ROAD_COLOR] = 0xff2468acU;
	vulkan_scene_palette(palette);
	require(vulkan_scene_refresh_palette(error, sizeof(error)));
	readback(TEST_ROAD_WIDTH, TEST_ROAD_HEIGHT);
	expect_pixel(TEST_ROAD_SAMPLE_X, TEST_ROAD_SAMPLE_Y, TEST_ROAD_WIDTH,
				 rgba(palette[TEST_ROAD_COLOR]));
	palette[TEST_ROAD_COLOR] = original;
	vulkan_scene_palette(palette);
}

static void test_clipped_coplanar_depth_planes(void)
{
	/* Viewport-clipped versions of the same ground and road. Recovering each
	 * skinny triangle's plane independently made the road stripe with grass. */
	static const legacy_f32 coordinates[TEST_CLIPPED_TRIANGLES * TEST_TRIANGLE_VERTICES][3] = {
		{1280.0f, 800.0f, 0.04608295112848282f},
		{0.0f, 800.0f, 0.04608295112848282f},
		{0.0f, 432.77996826171875f, 0.003776493016630411f},
		{1280.0f, 800.0f, 0.04608295112848282f},
		{0.0f, 432.77996826171875f, 0.003776493016630411f},
		{1246.970947265625f, 412.0055236816406f, 0.0013831258984282613f},
		{1280.0f, 800.0f, 0.04608295112848282f},
		{1246.970947265625f, 412.0055236816406f, 0.0013831258984282613f},
		{1280.0f, 412.6588134765625f, 0.0014583902666345239f},
		{0.0f, 455.89227294921875f, 0.006439202930778265f},
		{0.0f, 438.706787109375f, 0.004459308926016092f},
		{443.1275634765625f, 411.9067077636719f, 0.0013717421097680926f},
		{0.0f, 455.89227294921875f, 0.006439202930778265f},
		{443.1275634765625f, 411.9067077636719f, 0.0013717421097680926f},
		{746.0082397460938f, 411.9067077636719f, 0.0013717421097680926f},
		{1280.0f, 800.0f, 0.04608295112848282f},
		{0.0f, 800.0f, 0.04608295112848282f},
		{0.0f, 455.89227294921875f, 0.006439202930778265f},
		{1280.0f, 800.0f, 0.04608295112848282f},
		{0.0f, 455.89227294921875f, 0.006439202930778265f},
		{746.0082397460938f, 411.9067077636719f, 0.0013717421097680926f},
		{1280.0f, 800.0f, 0.04608295112848282f},
		{746.0082397460938f, 411.9067077636719f, 0.0013717421097680926f},
		{1280.0f, 471.8840637207031f, 0.00828157365322113f},
	};
	struct VULKAN_SCENE_VERTEX vertices[TEST_CLIPPED_TRIANGLES * TEST_TRIANGLE_VERTICES];
	for (size_t index = 0; index < TEST_CLIPPED_TRIANGLES * TEST_TRIANGLE_VERTICES; index++) {
		legacy_u32 color = index < TEST_CLIPPED_GRASS_TRIANGLES * TEST_TRIANGLE_VERTICES
							   ? TEST_GRASS_COLOR
							   : TEST_ROAD_COLOR;
		vertices[index] = vertex(coordinates[index][0], coordinates[index][1],
								 coordinates[index][2], color, TEST_SOLID, 0);
	}
	require(vulkan_scene_submit(vertices, TEST_CLIPPED_TRIANGLES * TEST_TRIANGLE_VERTICES,
								TEST_ROAD_WIDTH, TEST_ROAD_HEIGHT, 0, TEST_ROAD_WIDTH, 0,
								TEST_ROAD_HEIGHT, 0, 0, error, sizeof(error)));
	readback(TEST_ROAD_WIDTH, TEST_ROAD_HEIGHT);
	expect_pixel(TEST_CLIPPED_SAMPLE_X, TEST_CLIPPED_SAMPLE_Y, TEST_ROAD_WIDTH,
				 rgba(palette[TEST_ROAD_COLOR]));
	/* Representatives must survive preserved batches and palette-only replay. */
	require(vulkan_scene_submit(vertices, TEST_CLIPPED_GRASS_TRIANGLES * TEST_TRIANGLE_VERTICES,
								TEST_ROAD_WIDTH, TEST_ROAD_HEIGHT, 0, TEST_ROAD_WIDTH, 0,
								TEST_ROAD_HEIGHT, 0, 0, error, sizeof(error)));
	require(vulkan_scene_submit(vertices + TEST_CLIPPED_GRASS_TRIANGLES * TEST_TRIANGLE_VERTICES,
								(TEST_CLIPPED_TRIANGLES - TEST_CLIPPED_GRASS_TRIANGLES) *
									TEST_TRIANGLE_VERTICES,
								TEST_ROAD_WIDTH, TEST_ROAD_HEIGHT, 0, TEST_ROAD_WIDTH, 0,
								TEST_ROAD_HEIGHT, 1, 0, error, sizeof(error)));
	readback(TEST_ROAD_WIDTH, TEST_ROAD_HEIGHT);
	expect_pixel(TEST_CLIPPED_SAMPLE_X, TEST_CLIPPED_SAMPLE_Y, TEST_ROAD_WIDTH,
				 rgba(palette[TEST_ROAD_COLOR]));
	legacy_u32 original = palette[TEST_ROAD_COLOR];
	palette[TEST_ROAD_COLOR] = 0xff123456U;
	vulkan_scene_palette(palette);
	require(vulkan_scene_refresh_palette(error, sizeof(error)));
	readback(TEST_ROAD_WIDTH, TEST_ROAD_HEIGHT);
	expect_pixel(TEST_CLIPPED_SAMPLE_X, TEST_CLIPPED_SAMPLE_Y, TEST_ROAD_WIDTH,
				 rgba(palette[TEST_ROAD_COLOR]));
	palette[TEST_ROAD_COLOR] = original;
	vulkan_scene_palette(palette);
}

static void test_mask_and_alternate(void)
{
	struct VULKAN_SCENE_VERTEX vertices[TEST_QUAD_VERTICES * 2U];
	quad(vertices, 2, 2, 30, 30, 0.2f, TEST_BLUE, TEST_SOLID, 0);
	quad(vertices + TEST_QUAD_VERTICES, 2, 2, 30, 30, 0.8f, TEST_RED, TEST_PATTERN, 0);
	submit(vertices, TEST_QUAD_VERTICES * 2U, 0, 0);
	readback(TEST_WIDTH, TEST_HEIGHT);
	expect_pixel(4, 8, TEST_WIDTH, rgba(palette[TEST_RED]));
	expect_pixel(5, 8, TEST_WIDTH, rgba(palette[TEST_BLUE]));
	/* A masked hole must retain the underlying depth as well as its color. */
	quad(vertices, 2, 2, 30, 30, 0.5f, TEST_GREEN, TEST_SOLID, 0);
	submit(vertices, TEST_QUAD_VERTICES, 1, 0);
	readback(TEST_WIDTH, TEST_HEIGHT);
	expect_pixel(4, 8, TEST_WIDTH, rgba(palette[TEST_RED]));
	expect_pixel(5, 8, TEST_WIDTH, rgba(palette[TEST_GREEN]));
	quad(vertices, 2, 2, 30, 30, 0.4f, TEST_RED, TEST_ALTERNATE, 0);
	submit(vertices, TEST_QUAD_VERTICES, 0, 0);
	readback(TEST_WIDTH, TEST_HEIGHT);
	expect_pixel(4, 8, TEST_WIDTH, rgba(palette[TEST_GREEN]));
	expect_pixel(5, 8, TEST_WIDTH, rgba(palette[TEST_RED]));
}

static void test_preserved_color_depth_and_clip(void)
{
	struct VULKAN_SCENE_VERTEX vertices[TEST_QUAD_VERTICES * 2U];
	quad(vertices, 4, 4, 28, 28, 0.6f, TEST_RED, TEST_SOLID, 0);
	submit(vertices, TEST_QUAD_VERTICES, 0, 0);
	quad(vertices, 4, 4, 28, 28, 0.3f, TEST_BLUE, TEST_SOLID, 0);
	quad(vertices + TEST_QUAD_VERTICES, 40, 40, 60, 60, 0.4f, TEST_GREEN, TEST_SOLID, 0);
	submit(vertices, TEST_QUAD_VERTICES * 2U, 1, 0);
	readback(TEST_WIDTH, TEST_HEIGHT);
	expect_pixel(10, 10, TEST_WIDTH, rgba(palette[TEST_RED]));
	expect_pixel(45, 45, TEST_WIDTH, rgba(palette[TEST_GREEN]));
	submit(NULL, 0, 1, 0);
	readback(TEST_WIDTH, TEST_HEIGHT);
	expect_pixel(10, 10, TEST_WIDTH, rgba(palette[TEST_RED]));
	expect_pixel(45, 45, TEST_WIDTH, rgba(palette[TEST_GREEN]));
	quad(vertices, -4, -4, 70, 70, 0.5f, TEST_BLUE, TEST_SOLID, 0);
	require(vulkan_scene_submit(vertices, TEST_QUAD_VERTICES, TEST_WIDTH, TEST_HEIGHT, 8, 24, 12,
								28, 0, 0, error, sizeof(error)));
	readback(TEST_WIDTH, TEST_HEIGHT);
	expect_pixel(10, 14, TEST_WIDTH, rgba(palette[TEST_BLUE]));
	expect_pixel(7, 14, TEST_WIDTH, 0);
	expect_pixel(24, 14, TEST_WIDTH, 0);
	expect_pixel(10, 11, TEST_WIDTH, 0);
	expect_pixel(10, 28, TEST_WIDTH, 0);
}

static void test_background_and_decals(void)
{
	struct VULKAN_SCENE_VERTEX vertices[TEST_QUAD_VERTICES * 2U];
	quad(vertices, 4, 4, 28, 28, 0.2f, TEST_RED, TEST_SOLID, 0);
	quad(vertices + TEST_QUAD_VERTICES, 4, 4, 28, 28, 0.9f, TEST_BLUE, TEST_SOLID, TEST_BACKGROUND);
	submit(vertices, TEST_QUAD_VERTICES * 2U, 0, 0);
	readback(TEST_WIDTH, TEST_HEIGHT);
	expect_pixel(10, 10, TEST_WIDTH, rgba(palette[TEST_RED]));
	quad(vertices, 4, 4, 28, 28, 0.5f, TEST_RED, TEST_SOLID, 0);
	quad(vertices + TEST_QUAD_VERTICES, 4, 4, 28, 28, 0.5f, TEST_BLUE, TEST_SOLID, TEST_DECAL);
	submit(vertices, TEST_QUAD_VERTICES * 2U, 0, 0);
	readback(TEST_WIDTH, TEST_HEIGHT);
	expect_pixel(10, 10, TEST_WIDTH, rgba(palette[TEST_BLUE]));
}

static void test_shadow_blend_and_depth(void)
{
	struct VULKAN_SCENE_VERTEX vertices[TEST_QUAD_VERTICES];
	quad(vertices, 4, 4, 28, 28, 0.5f, TEST_RED, TEST_SOLID, 0);
	submit(vertices, TEST_QUAD_VERTICES, 0, 0);
	quad(vertices, 4, 4, 40, 40, 0.8f, 0, TEST_SHADOW, TEST_DECAL);
	for (size_t index = 0; index < TEST_QUAD_VERTICES; index++) {
		vertices[index].alternate = TEST_SHADOW_ALPHA;
	}
	submit(vertices, TEST_QUAD_VERTICES, 1, 0);
	readback(TEST_WIDTH, TEST_HEIGHT);
	legacy_u8 color[4];
	memcpy(color, &pixels[10 * TEST_WIDTH + 10], sizeof(color));
	assert(color[0] >= 126U && color[0] <= 129U);
	assert(color[1] == 0 && color[2] == 0 && color[3] == TEST_OPAQUE_ALPHA);
	memcpy(color, &pixels[34 * TEST_WIDTH + 34], sizeof(color));
	assert(color[0] == 0 && color[1] == 0 && color[2] == 0);
	assert(color[3] >= 127U && color[3] <= 129U);
	/* A blend affects color, never the opaque geometry's depth ownership. */
	quad(vertices, 4, 4, 28, 28, 0.6f, TEST_GREEN, TEST_SOLID, 0);
	submit(vertices, TEST_QUAD_VERTICES, 1, 0);
	readback(TEST_WIDTH, TEST_HEIGHT);
	expect_pixel(10, 10, TEST_WIDTH, rgba(palette[TEST_GREEN]));
}

static void test_palette_refresh_preserves_batches(void)
{
	struct VULKAN_SCENE_VERTEX vertices[TEST_QUAD_VERTICES];
	legacy_u32 original_red = palette[TEST_RED], original_green = palette[TEST_GREEN];
	quad(vertices, 4, 4, 28, 28, 0.5f, TEST_RED, TEST_SOLID, 0);
	submit(vertices, TEST_QUAD_VERTICES, 0, 0);
	quad(vertices, 40, 40, 60, 60, 0.4f, TEST_GREEN, TEST_SOLID, 0);
	submit(vertices, TEST_QUAD_VERTICES, 1, 0);
	palette[TEST_RED] = 0xff2468acU;
	palette[TEST_GREEN] = 0xff123456U;
	vulkan_scene_palette(palette);
	readback(TEST_WIDTH, TEST_HEIGHT);
	expect_pixel(10, 10, TEST_WIDTH, rgba(original_red));
	expect_pixel(45, 45, TEST_WIDTH, rgba(original_green));
	require(vulkan_scene_refresh_palette(error, sizeof(error)));
	readback(TEST_WIDTH, TEST_HEIGHT);
	expect_pixel(10, 10, TEST_WIDTH, rgba(palette[TEST_RED]));
	expect_pixel(45, 45, TEST_WIDTH, rgba(palette[TEST_GREEN]));
	/* An empty new frame retires cached geometry, including preserve batches. */
	submit(NULL, 0, 0, 0);
	palette[TEST_RED] = original_red;
	palette[TEST_GREEN] = original_green;
	vulkan_scene_palette(palette);
	require(vulkan_scene_refresh_palette(error, sizeof(error)));
	readback(TEST_WIDTH, TEST_HEIGHT);
	expect_pixel(10, 10, TEST_WIDTH, 0);
	expect_pixel(45, 45, TEST_WIDTH, 0);
}

static void test_palette_resize_and_ring_reuse(void)
{
	struct VULKAN_SCENE_VERTEX vertices[TEST_QUAD_VERTICES];
	legacy_u32 original = palette[TEST_RED];
	quad(vertices, 4, 4, 28, 28, 0.5f, TEST_RED, TEST_SOLID, 0);
	submit(vertices, TEST_QUAD_VERTICES, 0, 0);
	palette[TEST_RED] = 0xff2468acU;
	vulkan_scene_palette(palette);
	/* Updating the palette must not recolor previously rasterized RGBA pixels. */
	readback(TEST_WIDTH, TEST_HEIGHT);
	expect_pixel(10, 10, TEST_WIDTH, rgba(original));
	submit(vertices, TEST_QUAD_VERTICES, 0, 0);
	readback(TEST_WIDTH, TEST_HEIGHT);
	expect_pixel(10, 10, TEST_WIDTH, rgba(palette[TEST_RED]));
	quad(vertices, 72, 60, 92, 76, 0.5f, TEST_GREEN, TEST_SOLID, 0);
	require(vulkan_scene_submit(vertices, TEST_QUAD_VERTICES, TEST_RESIZED_WIDTH,
								TEST_RESIZED_HEIGHT, 0, TEST_RESIZED_WIDTH, 0, TEST_RESIZED_HEIGHT,
								0, 0, error, sizeof(error)));
	readback(TEST_RESIZED_WIDTH, TEST_RESIZED_HEIGHT);
	expect_pixel(80, 68, TEST_RESIZED_WIDTH, rgba(palette[TEST_GREEN]));
	expect_pixel(10, 10, TEST_RESIZED_WIDTH, 0);
	for (legacy_u32 submission = 0; submission < TEST_RING_SUBMISSIONS; submission++) {
		legacy_u32 color = TEST_RED + submission % 3U;
		quad(vertices, 4, 4, 28, 28, 0.5f, color, TEST_SOLID, 0);
		submit(vertices, TEST_QUAD_VERTICES, 0, 0);
	}
	readback(TEST_WIDTH, TEST_HEIGHT);
	expect_pixel(10, 10, TEST_WIDTH, rgba(palette[TEST_RED + (TEST_RING_SUBMISSIONS - 1U) % 3U]));
	palette[TEST_RED] = original;
	vulkan_scene_palette(palette);
}

legacy_int main(void)
{
	struct GRAPHICS_DEVICE devices[GRAPHICS_DEVICE_CAPACITY];
	if (graphics_enumerate_devices(devices, GRAPHICS_DEVICE_CAPACITY) == 0) {
		puts("No Vulkan device is available; hardware scene test skipped.");
		return TEST_UNAVAILABLE;
	}
	require(
		graphics_select_renderer(GRAPHICS_RENDERER_VULKAN, devices[0].index, error, sizeof(error)));
	struct VULKAN_SCENE_HANDLES handles;
	require(vulkan_scene_get_handles(&handles, error, sizeof(error)));
	assert(handles.instance != NULL && handles.physical_device != NULL && handles.device != NULL);
	assert(handles.image != 0 && handles.texture_width == VULKAN_SCENE_TEXTURE_WIDTH &&
		   handles.texture_height == VULKAN_SCENE_TEXTURE_HEIGHT);
	for (legacy_u32 color = 0; color < VULKAN_SCENE_PALETTE_COLORS; color++) {
		palette[color] = 0xff000000U;
	}
	palette[TEST_RED] = 0xffff0000U;
	palette[TEST_GREEN] = 0xff00ff00U;
	palette[TEST_BLUE] = 0xff0000ffU;
	palette[TEST_GRASS_COLOR] = 0xff00a000U;
	palette[TEST_ROAD_COLOR] = 0xff808080U;
	vulkan_scene_palette(palette);
	test_triangle_and_clear();
	test_depth_and_painter_order();
	test_coplanar_road_order_and_palette_replay();
	test_clipped_coplanar_depth_planes();
	test_mask_and_alternate();
	test_preserved_color_depth_and_clip();
	test_background_and_decals();
	test_shadow_blend_and_depth();
	test_palette_refresh_preserves_batches();
	test_palette_resize_and_ring_reuse();
	require(vulkan_scene_shutdown());
	graphics_shutdown();
	puts("Vulkan hardware triangle coverage, depth, coplanar road order, patterns, shadows, "
		 "palette, clip, "
		 "preservation, resize "
		 "and asynchronous ring reuse passed.");
	return 0;
}
