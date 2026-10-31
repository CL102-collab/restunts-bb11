#include <assert.h>
#include <float.h>
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL_stdinc.h>
#include "../c/externs.h"
#include "../c/hires.h"
#include "../c/platform.h"
#include "../c/projection.h"
#include "../c/shape2d.h"
#include "../c/shape3d_hires.h"
#include "../c/shape3d_internal.h"

#undef memcpy

extern legacy_u8 primidxcounttab[SHAPE3D_PRIMITIVE_TYPE_COUNT];

static struct SPRITE target;
static legacy_u8 rows[200 * 2];
static legacy_u8 *screen;
static struct RECTANGLE bounds;

static void reset_target(void)
{
	hires_shutdown();
	memset(screen, 3, 320 * 200);
	hires_set_enabled(1);
	shape3d_hires_reset();
	assert(hires_begin(&target));
}

static const legacy_u8 *pixels(void)
{
	legacy_s32 width;
	legacy_s32 height;
	const legacy_u8 *result = hires_framebuffer(screen, &width, &height);
	assert(width == hires_render_width() && height == hires_render_height());
	return result;
}

static legacy_u32 count_color(legacy_u8 color)
{
	const legacy_u8 *image = pixels();
	legacy_u32 count = 0;
	legacy_s32 width = hires_render_width();
	legacy_s32 height = hires_render_height();
	legacy_s32 scale = hires_render_scale();
	for (legacy_u32 index = 0; index < (legacy_u32)(width * height); index++) {
		count += image[index] == color;
		if (image[index] == color) {
			legacy_s32 x = (index % width) / scale;
			legacy_s32 y = (index / width) / scale;
			assert(x >= bounds.left && x < bounds.right);
			assert(y >= bounds.top && y < bounds.bottom);
		}
	}
	return count;
}

static void queue_flagged(legacy_u8 type, legacy_u32 count,
						  const struct SHAPE3D_HIRES_VECTOR *vertices, legacy_u16 flags)
{
	legacy_u8 indices[10];
	assert(count <= sizeof(indices));
	for (legacy_u32 index = 0; index < count; index++) {
		indices[index] = (legacy_u8)index;
	}
	shape3d_hires_queue(0, type, count, indices, vertices, flags);
	bounds.left = 320;
	bounds.top = 200;
	bounds.right = 0;
	bounds.bottom = 0;
	shape3d_hires_update_bounds(0, type, &bounds);
}

static void queue(legacy_u8 type, legacy_u32 count, const struct SHAPE3D_HIRES_VECTOR *vertices)
{
	queue_flagged(type, count, vertices, 0);
}

#define DECAL_TEST_INTERIOR_WIDTH 0.5
#define DECAL_TEST_MAXIMUM_BORDER_WIDTH 2U
#define DECAL_TEST_CENTER_Y (HIRES_HEIGHT / 2)
#define LINE_TEST_FAR_MAXIMUM_WIDTH 2U
#define LINE_TEST_CLIPPED_FAR_SAMPLE_X 739
#define DECAL_TEST_BORDER_SAMPLE_OFFSET (HIRES_SAMPLE_CENTER_OFFSET / 2)

/* Attached road dashes are polygons. Nearby markings retain their weight;
 * distant subpixel markings can miss pixel centers or have gaps. */
static void test_attached_polygon_weight(void)
{
	const legacy_f64 directions[][2] = {{1, 0}, {0, 1}, {0.8, 0.6}};
	const legacy_f64 depths[] = {600, 1000, 6400};
	legacy_u32 near_coverage[4][2];
	legacy_u32 distance_coverage[3] = {0};
	for (legacy_u32 direction = 0; direction < 3; direction++) {
		for (legacy_u32 distance = 0; distance < 3; distance++) {
			for (legacy_u32 translation = 0; translation < 4; translation++) {
				legacy_f64 depth = depths[distance];
				legacy_f64 shift = translation / 4.0;
				legacy_f64 tangent_x = directions[direction][0];
				legacy_f64 tangent_y = directions[direction][1];
				struct SHAPE3D_HIRES_VECTOR marking[4];
				for (legacy_u32 vertex = 0; vertex < 4; vertex++) {
					legacy_f64 along = (vertex < 2 ? -1 : 1) * depth / 16;
					legacy_f64 across = (vertex == 0 || vertex == 3 ? -1 : 1) * 1.5;
					marking[vertex].x =
						tangent_x * along - tangent_y * across + shift * depth / 640;
					marking[vertex].y =
						-(tangent_y * along + tangent_x * across + shift * depth / 640);
					marking[vertex].z = depth;
				}
				for (legacy_u32 winding = 0; winding < 2; winding++) {
					struct SHAPE3D_HIRES_VECTOR ordered[4];
					for (legacy_u32 vertex = 0; vertex < 4; vertex++) {
						ordered[vertex] = marking[winding ? 3 - vertex : vertex];
					}
					reset_target();
					queue_flagged(RENDER_PRIMITIVE_POLYGON, 4, ordered, 3);
					shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 8, 0, 0, 0, 0);
					hires_end();
					const legacy_u8 *image = pixels();
					legacy_u32 coverage = count_color(8);
					distance_coverage[distance] += coverage;
					if (distance != 2) {
						assert(coverage >= 80);
					}
					assert(coverage <= 84 * (HIRES_SCALE + 1));
					if (distance == 0) {
						assert(coverage >= 80 * (HIRES_SCALE - 1));
						near_coverage[translation][winding] = coverage;
					} else if (distance == 2) {
						assert(coverage <= 84 * DECAL_TEST_MAXIMUM_BORDER_WIDTH);
						assert(coverage < near_coverage[translation][winding]);
					}
					for (legacy_s32 along = -30; along <= 30; along++) {
						legacy_s32 x = (legacy_s32)(640 + tangent_x * along + shift);
						legacy_s32 y = (legacy_s32)(400 + tangent_y * along + shift);
						if (distance == 2) {
							/* Edge quads may cover an extra sample compared with the old
							 * distance-based border. Keep their interior visible while
							 * bounding the complete distant stroke above. */
							legacy_f64 offset_x = x + HIRES_SAMPLE_CENTER_OFFSET - (640 + shift);
							legacy_f64 offset_y = y + HIRES_SAMPLE_CENTER_OFFSET - (400 + shift);
							legacy_f64 across = offset_x * tangent_y - offset_y * tangent_x;
							if (SDL_fabs(across) < DECAL_TEST_INTERIOR_WIDTH / 2) {
								assert(image[y * HIRES_WIDTH + x] == 8);
							}
						} else {
							assert(image[y * HIRES_WIDTH + x] == 8);
						}
					}
				}
			}
		}
	}
	assert(distance_coverage[0] > distance_coverage[1]);
	assert(distance_coverage[1] > distance_coverage[2]);
}

static void test_attached_polygon_midrange_weight(void)
{
	/* Keep the projected dash length and subpixel phase fixed. These distances
	 * cover the near cap, mid-range falloff, and distant visibility floor. */
	const legacy_f64 depths[] = {200, 250, 300, 400, 1000, 6400};
	legacy_u32 widths[SDL_arraysize(depths)] = {0};
	for (legacy_u32 distance = 0; distance < sizeof(depths) / sizeof(depths[0]); distance++) {
		legacy_f64 depth = depths[distance];
		legacy_f64 shift = depth * 0.375 / 640;
		const struct SHAPE3D_HIRES_VECTOR marking[] = {{-depth / 16 + shift, -0.25 - shift, depth},
													   {depth / 16 + shift, -0.25 - shift, depth},
													   {depth / 16 + shift, 0.25 - shift, depth},
													   {-depth / 16 + shift, 0.25 - shift, depth}};
		reset_target();
		queue_flagged(RENDER_PRIMITIVE_POLYGON, 4, marking, 3);
		shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 8, 0, 0, 0, 0);
		hires_end();
		const legacy_u8 *image = pixels();
		for (legacy_s32 x = 610; x <= 670; x++) {
			legacy_u32 coverage = 0;
			for (legacy_s32 y = 395; y <= 405; y++) {
				coverage += image[y * HIRES_WIDTH + x] == 8;
			}
			assert(coverage >= 1 && coverage <= HIRES_SCALE);
			assert(widths[distance] == 0 || widths[distance] == coverage);
			widths[distance] = coverage;
			assert(image[400 * HIRES_WIDTH + x] == 8);
		}
		if (distance != 0) {
			assert(widths[distance] <= widths[distance - 1U]);
		}
	}
	assert(widths[0] > widths[SDL_arraysize(depths) - 1U]);
}

static void test_attached_polygon_weight_follows_projection(void)
{
	static legacy_u8 reference[HIRES_WIDTH * HIRES_HEIGHT];
	/* A fractional translation distinguishes this medium-distance stroke
	 * from both the full near width and the minimum distant width. */
	const struct SHAPE3D_HIRES_VECTOR marking[] = {{-18.57421875, -0.42578125, 300},
												   {18.92578125, -0.42578125, 300},
												   {18.92578125, 0.07421875, 300},
												   {-18.57421875, 0.07421875, 300}};
	reset_target();
	queue_flagged(RENDER_PRIMITIVE_POLYGON, 4, marking, 3);
	shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 8, 0, 0, 0, 0);
	hires_end();
	assert(count_color(8) >= 160 && count_color(8) < 200);
	memcpy(reference, pixels(), sizeof(reference));

	/* Equal focal-length/depth ratios preserve both geometry and weight. */
	struct SHAPE3D_HIRES_VECTOR equivalent[4];
	for (legacy_u32 vertex = 0; vertex < 4; vertex++) {
		equivalent[vertex] = marking[vertex];
		equivalent[vertex].z *= 2;
	}
	projection_focal_length_x = projection_focal_length_y = 320;
	reset_target();
	queue_flagged(RENDER_PRIMITIVE_POLYGON, 4, equivalent, 3);
	/* Width must use the projection captured when the primitive was queued. */
	projection_focal_length_x = projection_focal_length_y = 160;
	shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 8, 0, 0, 0, 0);
	hires_end();
	assert(memcmp(reference, pixels(), sizeof(reference)) == 0);

	/* Equivalent authored model sizes retain the same displayed weight. */
	const legacy_f64 scales[] = {20, 0.5};
	for (legacy_u32 model = 0; model < 2; model++) {
		legacy_f64 scale = scales[model];
		for (legacy_u32 vertex = 0; vertex < 4; vertex++) {
			equivalent[vertex].x = marking[vertex].x * scale;
			equivalent[vertex].y = marking[vertex].y * scale;
			equivalent[vertex].z = marking[vertex].z * scale;
		}
		reset_target();
		shape3d_hires_set_model_scale(scale);
		queue_flagged(RENDER_PRIMITIVE_POLYGON, 4, equivalent, 3);
		shape3d_hires_set_model_scale(1);
		shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 8, 0, 0, 0, 0);
		hires_end();
		assert(memcmp(reference, pixels(), sizeof(reference)) == 0);
	}
}

static void test_attached_polygon_varies_along_depth(void)
{
	/* Perspective narrows the far end of a dash on a sloping road. Offset
	 * its projected center so the far tip needs added border coverage to reach
	 * the sample row: its original half-width is 0.15 pixels at that end. */
	struct SHAPE3D_HIRES_VECTOR marking[] = {
		{-100, -1.5, 1600}, {-100, 1.5, 1600}, {400, 1.5, 6400}, {400, -1.5, 6400}};
	for (legacy_u32 vertex = 0; vertex < SDL_arraysize(marking); vertex++) {
		marking[vertex].y -= marking[vertex].z * DECAL_TEST_BORDER_SAMPLE_OFFSET /
							 (projection_focal_length_y * HIRES_SCALE);
	}
	reset_target();
	queue_flagged(RENDER_PRIMITIVE_POLYGON, 4, marking, 3);
	shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 8, 0, 0, 0, 0);
	hires_end();
	const legacy_u8 *image = pixels();
	assert(count_color(8) >= 80);
	for (legacy_s32 x = 605; x <= 675; x++) {
		legacy_u32 coverage = 0;
		for (legacy_s32 y = 397; y <= 403; y++) {
			coverage += image[y * HIRES_WIDTH + x] == 8;
		}
		assert(coverage >= 1);
		assert(image[400 * HIRES_WIDTH + x] == 8);
	}
}

static void test_polygon_weight_preserves_surfaces(void)
{
	static legacy_u8 original[HIRES_WIDTH * HIRES_HEIGHT];
	const struct SHAPE3D_HIRES_VECTOR panel[] = {
		{-50, -20, 200}, {50, -20, 200}, {50, 20, 200}, {-50, 20, 200}};
	for (legacy_u16 attached = 0; attached < 2; attached++) {
		reset_target();
		queue_flagged(RENDER_PRIMITIVE_POLYGON, 4, panel, attached ? 3 : 0);
		shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 8, 0, 0, 0, 0);
		hires_end();
		assert(count_color(8) != 0);
		if (!attached) {
			memcpy(original, pixels(), sizeof(original));
		} else {
			assert(memcmp(original, pixels(), sizeof(original)) == 0);
		}
	}

	/* Unattached, subpixel road surfaces must not turn into thick strokes. */
	const struct SHAPE3D_HIRES_VECTOR thin[] = {
		{-400, -1.5, 6400}, {400, -1.5, 6400}, {400, 1.5, 6400}, {-400, 1.5, 6400}};
	reset_target();
	queue(RENDER_PRIMITIVE_POLYGON, 4, thin);
	shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 8, 0, 0, 0, 0);
	hires_end();
	assert(count_color(8) == 0);
}

static void test_attached_polygon_clipping(void)
{
	/* Offset the subpixel dash so only its added border reaches the sample
	 * row, exercising border clipping at both ends and at the viewport top. */
	struct SHAPE3D_HIRES_VECTOR marking[] = {
		{-7000, -1.5, 6400}, {7000, -1.5, 6400}, {7000, 1.5, 6400}, {-7000, 1.5, 6400}};
	for (legacy_u32 vertex = 0; vertex < SDL_arraysize(marking); vertex++) {
		marking[vertex].y -= marking[vertex].z * DECAL_TEST_BORDER_SAMPLE_OFFSET /
							 (projection_focal_length_y * HIRES_SCALE);
	}
	reset_target();
	queue_flagged(RENDER_PRIMITIVE_POLYGON, 4, marking, 3);
	shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 8, 0, 0, 0, 0);
	hires_end();
	const legacy_u8 *image = pixels();
	assert(count_color(8) == HIRES_WIDTH);
	legacy_s32 sample_row = projection_center_y * HIRES_SCALE;
	assert(image[sample_row * HIRES_WIDTH] == 8);
	assert(image[sample_row * HIRES_WIDTH + HIRES_WIDTH - 1] == 8);

	target.sprite_raster_left = 150;
	target.sprite_raster_right = 170;
	target.sprite_top = 100;
	target.sprite_bottom = 110;
	reset_target();
	queue_flagged(RENDER_PRIMITIVE_POLYGON, 4, marking, 3);
	shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 8, 0, 0, 0, 0);
	hires_end();
	image = pixels();
	assert(count_color(8) == 80);
	for (legacy_s32 y = 0; y < HIRES_HEIGHT; y++) {
		for (legacy_s32 x = 0; x < HIRES_WIDTH; x++) {
			if (x < 600 || x >= 680 || y < 400 || y >= 440) {
				assert(image[y * HIRES_WIDTH + x] == 3);
			}
		}
	}
	target.sprite_raster_left = 0;
	target.sprite_raster_right = 320;
	target.sprite_top = 0;
	target.sprite_bottom = 200;
}

static void test_attached_polygon_occlusion(void)
{
	const struct SHAPE3D_HIRES_VECTOR parent[] = {
		{-5, -2.5, 80}, {5, -2.5, 80}, {5, 2.5, 80}, {-5, 2.5, 80}};
	/* Attached paint can be authored slightly behind its supporting surface. */
	const struct SHAPE3D_HIRES_VECTOR marking[] = {
		{-5, -0.025, 80.5}, {5, -0.025, 80.5}, {5, 0.025, 80.5}, {-5, 0.025, 80.5}};
	const struct SHAPE3D_HIRES_VECTOR nearer[] = {
		{-3.75, -1.25, 40}, {0, -1.25, 40}, {0, 1.25, 40}, {-3.75, 1.25, 40}};
	const legacy_u8 indices[] = {0, 1, 2, 3};
	for (legacy_s32 batched = 0; batched < 2; batched++) {
		for (legacy_u32 order = 0; order < 2; order++) {
			reset_target();
			if (batched) {
				shape3d_hires_batch_begin();
			}
			queue(RENDER_PRIMITIVE_POLYGON, 4, parent);
			shape3d_hires_queue(1, RENDER_PRIMITIVE_POLYGON, 4, indices, marking, 3);
			shape3d_hires_update_bounds(1, RENDER_PRIMITIVE_POLYGON, &bounds);
			shape3d_hires_queue(2, RENDER_PRIMITIVE_POLYGON, 4, indices, nearer, 0);
			shape3d_hires_update_bounds(2, RENDER_PRIMITIVE_POLYGON, &bounds);
			if (order == 0) {
				shape3d_hires_render(2, RENDER_PRIMITIVE_POLYGON, 9, 0, 0, 0, 0);
			}
			shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 7, 0, 0, 0, 0);
			shape3d_hires_render(1, RENDER_PRIMITIVE_POLYGON, 8, 0, 0, 0, 0);
			if (order != 0) {
				shape3d_hires_render(2, RENDER_PRIMITIVE_POLYGON, 9, 0, 0, 0, 0);
			}
			if (batched) {
				shape3d_hires_batch_end();
			}
			hires_end();
			const legacy_u8 *image = pixels();
			assert(count_color(8) != 0);
			/* Check the interior of the cheaper edge-quad stroke, including the
			 * exact left/right boundary where an independent nearer panel covers it. */
			for (legacy_s32 y = DECAL_TEST_CENTER_Y - 1; y <= DECAL_TEST_CENTER_Y; y++) {
				assert(image[y * HIRES_WIDTH + 639] == 9);
				assert(image[y * HIRES_WIDTH + 640] == 8);
				assert(image[y * HIRES_WIDTH + 660] == 8);
			}
		}
	}
}

static void test_attached_polygon_preserves_interior_depth(void)
{
	/* This sloping dash projects to x 639.8..640.8. Its true inverse depth at
	 * pixel center 640.5 is 0.0093, behind the independent panel at 0.0095. */
	const struct SHAPE3D_HIRES_VECTOR marking[] = {{-0.2 / 6.4, -20 / 6.4, 100},
												   {0.8 / 5.76, -20 / 5.76, 1 / 0.009},
												   {0.8 / 5.76, 20 / 5.76, 1 / 0.009},
												   {-0.2 / 6.4, 20 / 6.4, 100}};
	const struct SHAPE3D_HIRES_VECTOR nearer[] = {{-2 / 6.08, -20 / 6.08, 1 / 0.0095},
												  {2 / 6.08, -20 / 6.08, 1 / 0.0095},
												  {2 / 6.08, 20 / 6.08, 1 / 0.0095},
												  {-2 / 6.08, 20 / 6.08, 1 / 0.0095}};
	const legacy_u8 indices[] = {0, 1, 2, 3};
	for (legacy_s32 batched = 0; batched < 2; batched++) {
		for (legacy_u32 order = 0; order < 2; order++) {
			reset_target();
			if (batched) {
				shape3d_hires_batch_begin();
			}
			queue_flagged(RENDER_PRIMITIVE_POLYGON, 4, marking, 3);
			shape3d_hires_queue(1, RENDER_PRIMITIVE_POLYGON, 4, indices, nearer, 0);
			shape3d_hires_update_bounds(1, RENDER_PRIMITIVE_POLYGON, &bounds);
			if (order == 0) {
				shape3d_hires_render(1, RENDER_PRIMITIVE_POLYGON, 9, 0, 0, 0, 0);
			}
			shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 8, 0, 0, 0, 0);
			if (order != 0) {
				shape3d_hires_render(1, RENDER_PRIMITIVE_POLYGON, 9, 0, 0, 0, 0);
			}
			if (batched) {
				shape3d_hires_batch_end();
			}
			hires_end();
			const legacy_u8 *image = pixels();
			assert(count_color(9) != 0);
			for (legacy_s32 y = 385; y < 415; y++) {
				assert(image[y * HIRES_WIDTH + 640] == 9);
			}
		}
	}
}

static void test_projection_and_subpixel_edges(void)
{
	struct SHAPE3D_HIRES_VECTOR vector = {1, 1, 300};
	struct SHAPE3D_HIRES_POINT point;
	shape3d_hires_project(&vector, &point);
	assert(point.x > 642 && point.x < 643);
	assert(point.y > 397 && point.y < 398);
	const struct SHAPE3D_HIRES_VECTOR triangle[] = {
		{-100, -60, 300}, {100, -60, 300}, {-100, 60, 300}};
	reset_target();
	queue(RENDER_PRIMITIVE_POLYGON, 3, triangle);
	shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 7, 0, 0, 0, 0);
	hires_end();
	assert(count_color(7) > 40000);
	const legacy_u8 *image = pixels();
	legacy_u32 partial_blocks = 0;
	for (legacy_s32 y = 0; y < HIRES_HEIGHT; y += HIRES_SCALE) {
		for (legacy_s32 x = 0; x < HIRES_WIDTH; x += HIRES_SCALE) {
			legacy_u32 colored = 0;
			for (legacy_s32 row = 0; row < HIRES_SCALE; row++) {
				for (legacy_s32 column = 0; column < HIRES_SCALE; column++) {
					colored += image[(y + row) * HIRES_WIDTH + x + column] == 7;
				}
			}
			partial_blocks += colored != 0 && colored != HIRES_SCALE * HIRES_SCALE;
		}
	}
	assert(partial_blocks > 100);
	/* The detailed pass leaves the legacy image available for compatibility. */
	for (legacy_u32 index = 0; index < 320 * 200; index++) {
		assert(screen[index] == 3);
	}
}

static void test_near_plane_and_screen_clipping(void)
{
	const struct SHAPE3D_HIRES_VECTOR triangle[] = {{-10, -10, 1}, {50, -30, 100}, {0, 50, 100}};
	reset_target();
	queue(RENDER_PRIMITIVE_POLYGON, 3, triangle);
	shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 9, 0, 0, 0, 0);
	hires_end();
	assert(count_color(9) > 100000);

	const struct SHAPE3D_HIRES_VECTOR line[] = {{-32768, 0, 12}, {32767, 0, 12}};
	reset_target();
	queue(RENDER_PRIMITIVE_LINE, 2, line);
	shape3d_hires_render(0, RENDER_PRIMITIVE_LINE, 10, 0, 0, 0, 0);
	hires_end();
	assert(count_color(10) == HIRES_WIDTH * HIRES_SCALE);
	assert(pixels()[400 * HIRES_WIDTH] == 10);
	assert(pixels()[400 * HIRES_WIDTH + HIRES_WIDTH - 1] == 10);
}

static legacy_u32 colored_column(legacy_s32 x, legacy_u8 color)
{
	const legacy_u8 *image = pixels();
	legacy_u32 count = 0;
	for (legacy_s32 y = 0; y < HIRES_HEIGHT; y++) {
		count += image[y * HIRES_WIDTH + x] == color;
	}
	return count;
}

/* Nearby model lines keep their weight in every direction, while their
 * centerlines still move in high-resolution increments. */
static void test_line_weight(void)
{
	static legacy_u8 forward[HIRES_WIDTH * HIRES_HEIGHT];
	const struct SHAPE3D_HIRES_VECTOR lines[][2] = {{{-5, 0, 80}, {5, 0, 80}},
													{{0, -5, 80}, {0, 5, 80}},
													{{-5, -5, 80}, {5, 5, 80}},
													{{0, 0, 80}, {0, 0, 80}}};
	for (legacy_u32 direction = 0; direction < 4; direction++) {
		for (legacy_u32 reverse = 0; reverse < 2; reverse++) {
			const struct SHAPE3D_HIRES_VECTOR line[] = {lines[direction][reverse],
														lines[direction][1 - reverse]};
			reset_target();
			queue(RENDER_PRIMITIVE_LINE, 2, line);
			shape3d_hires_render(0, RENDER_PRIMITIVE_LINE, 10, 0, 0, 0, 0);
			hires_end();
			const legacy_u8 *image = pixels();
			assert(image[400 * HIRES_WIDTH + 640] == 10);
			legacy_u32 coverage = count_color(10);
			if (direction < 2) {
				assert(coverage > 80 * 3 && coverage < 90 * HIRES_SCALE);
			} else if (direction == 2) {
				assert(coverage > 80 * HIRES_SCALE);
				assert(image[400 * HIRES_WIDTH + 645] == 3);
			} else {
				assert(coverage >= 1 && coverage <= HIRES_SCALE * HIRES_SCALE);
			}
			if (reverse == 0) {
				memcpy(forward, image, sizeof(forward));
			} else {
				assert(memcmp(forward, image, sizeof(forward)) == 0);
			}
		}
	}

	/* Points and collapsed polygon edges retain their subpixel detail. */
	for (legacy_u32 type = 0; type < 2; type++) {
		legacy_u8 primitive = type == 0 ? RENDER_PRIMITIVE_POINT : RENDER_PRIMITIVE_POLYGON;
		reset_target();
		queue(primitive, type == 0 ? 1 : 2, lines[3]);
		shape3d_hires_render(0, primitive, 10, 0, 0, 0, 0);
		hires_end();
		assert(count_color(10) == 1);
	}
}

static void test_line_weight_follows_projection(void)
{
	static legacy_u8 close[HIRES_WIDTH * HIRES_HEIGHT];
	static legacy_u8 middle[HIRES_WIDTH * HIRES_HEIGHT];
	const legacy_f64 depths[] = {100, 200, 600, 6400};
	legacy_u32 widths[4];
	for (legacy_u32 distance = 0; distance < 4; distance++) {
		/* Keep the projected endpoints fixed while moving the line away. */
		legacy_f64 depth = depths[distance];
		const struct SHAPE3D_HIRES_VECTOR line[] = {{-depth / 16, 0, depth},
													{depth / 16, 0, depth}};
		reset_target();
		queue(RENDER_PRIMITIVE_LINE, 2, line);
		shape3d_hires_render(0, RENDER_PRIMITIVE_LINE, 10, 0, 0, 0, 0);
		hires_end();
		assert(count_color(10) != 0);
		widths[distance] = colored_column(640, 10);
		if (distance == 0) {
			memcpy(close, pixels(), sizeof(close));
		} else if (distance == 1) {
			/* Close and medium-close lines retain the same full-weight stroke. */
			assert(memcmp(close, pixels(), sizeof(close)) == 0);
		} else if (distance == 2) {
			memcpy(middle, pixels(), sizeof(middle));
		}
	}
	assert(widths[0] == HIRES_SCALE);
	assert(widths[1] == HIRES_SCALE);
	assert(widths[1] > widths[2] && widths[2] > widths[3]);
	assert(widths[3] == 1);

	/* Equal focal-length/depth ratios must preserve both geometry and weight. */
	const struct SHAPE3D_HIRES_VECTOR equivalent[] = {{-37.5, 0, 1200}, {37.5, 0, 1200}};
	projection_focal_length_x = projection_focal_length_y = 320;
	reset_target();
	queue(RENDER_PRIMITIVE_LINE, 2, equivalent);
	/* Projection belongs to the queued shape, even if a later view changes it. */
	projection_focal_length_x = projection_focal_length_y = 160;
	shape3d_hires_render(0, RENDER_PRIMITIVE_LINE, 10, 0, 0, 0, 0);
	hires_end();
	assert(memcmp(middle, pixels(), sizeof(middle)) == 0);

	/* Equivalent authored model sizes must not change their displayed weight. */
	const legacy_f64 scales[] = {20, 0.5};
	for (legacy_u32 model = 0; model < 2; model++) {
		legacy_f64 scale = scales[model];
		const struct SHAPE3D_HIRES_VECTOR line[] = {{-37.5 * scale, 0, 600 * scale},
													{37.5 * scale, 0, 600 * scale}};
		reset_target();
		shape3d_hires_set_model_scale(scale);
		queue(RENDER_PRIMITIVE_LINE, 2, line);
		shape3d_hires_set_model_scale(1);
		shape3d_hires_render(0, RENDER_PRIMITIVE_LINE, 10, 0, 0, 0, 0);
		hires_end();
		assert(memcmp(middle, pixels(), sizeof(middle)) == 0);
	}
}

static void test_line_weight_varies_along_depth(void)
{
	const struct SHAPE3D_HIRES_VECTOR sloping[] = {{-29.296875, 0, 187.5}, {375, 0, 2400}};
	for (legacy_u32 reverse = 0; reverse < 2; reverse++) {
		const struct SHAPE3D_HIRES_VECTOR line[] = {sloping[reverse], sloping[1 - reverse]};
		reset_target();
		queue(RENDER_PRIMITIVE_LINE, 2, line);
		shape3d_hires_render(0, RENDER_PRIMITIVE_LINE, 10, 0, 0, 0, 0);
		hires_end();
		assert(count_color(10) != 0);
		legacy_u32 near_width = colored_column(550, 10);
		legacy_u32 middle_width = colored_column(660, 10);
		legacy_u32 far_width = colored_column(730, 10);
		assert(near_width == HIRES_SCALE);
		/* A tapered quad can round adjacent far widths to the same sample
		 * count; it must still narrow and never widen with distance. */
		assert(near_width >= middle_width && middle_width >= far_width);
		assert(near_width > far_width);
		assert(far_width >= 1 && far_width <= LINE_TEST_FAR_MAXIMUM_WIDTH);
	}
}

static void test_line_weight_after_clipping(void)
{
	const struct SHAPE3D_HIRES_VECTOR far_line[] = {{-6410, -400, 6400}, {-6410, 400, 6400}};
	reset_target();
	queue(RENDER_PRIMITIVE_LINE, 2, far_line);
	shape3d_hires_render(0, RENDER_PRIMITIVE_LINE, 10, 0, 0, 0, 0);
	hires_end();
	/* A distant centerline outside the image must not inherit the near width. */
	assert(count_color(10) == 0);

	const struct SHAPE3D_HIRES_VECTOR crossing[] = {{-2, 0, 1}, {1000, 0, 6400}};
	for (legacy_u32 reverse = 0; reverse < 2; reverse++) {
		const struct SHAPE3D_HIRES_VECTOR line[] = {crossing[reverse], crossing[1 - reverse]};
		reset_target();
		queue(RENDER_PRIMITIVE_LINE, 2, line);
		shape3d_hires_render(0, RENDER_PRIMITIVE_LINE, 10, 0, 0, 0, 0);
		hires_end();
		assert(count_color(10) != 0);
		assert(colored_column(640, 10) == HIRES_SCALE);
		assert(colored_column(LINE_TEST_CLIPPED_FAR_SAMPLE_X, 10) >= 1);
		assert(colored_column(LINE_TEST_CLIPPED_FAR_SAMPLE_X, 10) <= LINE_TEST_FAR_MAXIMUM_WIDTH);
	}
}

static void test_steep_line_stroke_has_no_holes(void)
{
	/* The rounded centerline can wander from the exact projected segment.
	 * This interior stroke pixel must survive in both endpoint orders. */
	const struct SHAPE3D_HIRES_VECTOR steep[] = {{0.4625, -3.525, 80}, {0.6875, 0.11875, 80}};
	for (legacy_u32 reverse = 0; reverse < 2; reverse++) {
		const struct SHAPE3D_HIRES_VECTOR line[] = {steep[reverse], steep[1 - reverse]};
		reset_target();
		queue(RENDER_PRIMITIVE_LINE, 2, line);
		shape3d_hires_render(0, RENDER_PRIMITIVE_LINE, 10, 0, 0, 0, 0);
		hires_end();
		assert(count_color(10) != 0);
		assert(pixels()[416 * HIRES_WIDTH + 642] == 10);
	}
}

static void test_line_stroke_clipping(void)
{
	const struct SHAPE3D_HIRES_VECTOR edges[][2] = {{{-80.125, -5, 80}, {-80.125, 5, 80}},
													{{80, -5, 80}, {80, 5, 80}},
													{{-5, 50.125, 80}, {5, 50.125, 80}},
													{{-5, -50, 80}, {5, -50, 80}}};
	for (legacy_u32 edge = 0; edge < 4; edge++) {
		reset_target();
		queue(RENDER_PRIMITIVE_LINE, 2, edges[edge]);
		shape3d_hires_render(0, RENDER_PRIMITIVE_LINE, 10, 0, 0, 0, 0);
		hires_end();
		/* The centerline is outside the image; its stroke still reaches in. */
		assert(count_color(10) > 0 && count_color(10) < 80 * HIRES_SCALE);
	}

	const struct SHAPE3D_HIRES_VECTOR clipped[][2] = {{{-5.125, -7.5, 80}, {-5.125, 7.5, 80}},
													  {{-7.5, 5.125, 80}, {7.5, 5.125, 80}}};
	target.sprite_raster_left = 150;
	target.sprite_raster_right = 170;
	target.sprite_top = 90;
	target.sprite_bottom = 110;
	for (legacy_u32 direction = 0; direction < 2; direction++) {
		reset_target();
		queue(RENDER_PRIMITIVE_LINE, 2, clipped[direction]);
		shape3d_hires_render(0, RENDER_PRIMITIVE_LINE, 10, 0, 0, 0, 0);
		hires_end();
		assert(count_color(10) > 0 && count_color(10) < 20 * HIRES_SCALE * HIRES_SCALE);
		const legacy_u8 *image = pixels();
		for (legacy_s32 y = 0; y < HIRES_HEIGHT; y++) {
			for (legacy_s32 x = 0; x < HIRES_WIDTH; x++) {
				if (x < 600 || x >= 680 || y < 360 || y >= 440) {
					assert(image[y * HIRES_WIDTH + x] == 3);
				}
			}
		}
	}
	target.sprite_raster_left = 0;
	target.sprite_raster_right = 320;
	target.sprite_top = 0;
	target.sprite_bottom = 200;
}

static void test_line_stroke_occlusion(void)
{
	const struct SHAPE3D_HIRES_VECTOR panel[] = {
		{-3.75, -1.25, 40}, {0, -1.25, 40}, {0, 1.25, 40}, {-3.75, 1.25, 40}};
	const struct SHAPE3D_HIRES_VECTOR line[] = {{-2.5, 0, 80}, {2.5, 0, 80}};
	const legacy_u8 indices[] = {0, 1};
	for (legacy_u32 order = 0; order < 2; order++) {
		reset_target();
		queue(RENDER_PRIMITIVE_POLYGON, 4, panel);
		shape3d_hires_queue(1, RENDER_PRIMITIVE_LINE, 2, indices, line, 0);
		shape3d_hires_update_bounds(1, RENDER_PRIMITIVE_LINE, &bounds);
		if (order == 0) {
			shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 7, 0, 0, 0, 0);
		}
		shape3d_hires_render(1, RENDER_PRIMITIVE_LINE, 8, 0, 0, 0, 0);
		if (order != 0) {
			shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 7, 0, 0, 0, 0);
		}
		hires_end();
		for (legacy_s32 y = 398; y < 402; y++) {
			assert(pixels()[y * HIRES_WIDTH + 639] == 7);
			assert(pixels()[y * HIRES_WIDTH + 640] == 8);
		}
		assert(count_color(8) > 0);
		assert(colored_column(650, 8) == HIRES_SCALE);
	}
}

static void test_materials_and_rounded_primitives(void)
{
	const struct SHAPE3D_HIRES_VECTOR rectangle[] = {
		{-50, -50, 200}, {50, -50, 200}, {50, 50, 200}, {-50, 50, 200}};
	reset_target();
	queue(RENDER_PRIMITIVE_POLYGON, 4, rectangle);
	shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 5, 6, 0, 2, 0xAA55);
	hires_end();
	assert(count_color(5) == 51200);
	assert(count_color(6) == 51200);
	assert(pixels()[240 * HIRES_WIDTH + 480] == 5);
	assert(pixels()[240 * HIRES_WIDTH + 481] == 6);

	reset_target();
	queue(RENDER_PRIMITIVE_POLYGON, 4, rectangle);
	shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON | RENDER_PRIMITIVE_GHOST_FLAG, 5, 6, 0, 0, 0);
	hires_end();
	assert(count_color(0) == 51200);
	assert(count_color(5) == 0);

	const struct SHAPE3D_HIRES_VECTOR sphere[] = {{0, 0, 300}, {20, 0, 300}};
	reset_target();
	queue(RENDER_PRIMITIVE_SPHERE, 2, sphere);
	shape3d_hires_render(0, RENDER_PRIMITIVE_SPHERE, 11, 0, 0, 0, 0);
	hires_end();
	assert(count_color(11) > 1000);
	assert(pixels()[400 * HIRES_WIDTH + 640] == 11);
	assert(pixels()[380 * HIRES_WIDTH + 640] == 3);

	const struct SHAPE3D_HIRES_VECTOR wheel[] = {{-20, 0, 400}, {0, 0, 400},  {-20, 20, 400},
												 {0, 0, 420},	{20, 0, 420}, {0, 20, 420}};
	reset_target();
	queue(RENDER_PRIMITIVE_WHEEL, 6, wheel);
	shape3d_hires_render(0, RENDER_PRIMITIVE_WHEEL, 11, 12, 13, 0, 0);
	hires_end();
	assert(count_color(11) != 0);
	assert(count_color(12) != 0);
	assert(count_color(13) != 0);
}

#define TEST_STOCK_GRILLE_COLOR 28U
#define TEST_STOCK_GRILLE_PATTERN 0x77ddU

static void test_black_grille_preserves_stock_pixel_coverage(void)
{
	const struct SHAPE3D_HIRES_VECTOR rectangle[] = {
		{-50, -50, 200}, {50, -50, 200}, {50, 50, 200}, {-50, 50, 200}};
	static legacy_u8 reference[HIRES_WIDTH * HIRES_HEIGHT];
	reset_target();
	queue(RENDER_PRIMITIVE_POLYGON, 4, rectangle);
	shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, TEST_STOCK_GRILLE_COLOR, 0, 0,
						 HIRES_PAINT_PATTERN, TEST_STOCK_GRILLE_PATTERN);
	hires_end();
	legacy_u32 covered = count_color(TEST_STOCK_GRILLE_COLOR);
	assert(covered > 0);
	memcpy(reference, pixels(), sizeof(reference));

	reset_target();
	queue(RENDER_PRIMITIVE_POLYGON, 4, rectangle);
	shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON | RENDER_PRIMITIVE_BLACK_GRILLE_FLAG,
						 TEST_STOCK_GRILLE_COLOR, 0, 0, HIRES_PAINT_PATTERN,
						 TEST_STOCK_GRILLE_PATTERN);
	hires_end();
	assert(count_color(0) == covered);
	assert(count_color(TEST_STOCK_GRILLE_COLOR) == 0);
	const legacy_u8 *image = pixels();
	for (legacy_u32 index = 0; index < sizeof(reference); index++) {
		/* Exact coverage, including every transparent hole, remains native;
		 * only the covered stock gray pixels change to palette black. */
		legacy_u8 expected = reference[index] == TEST_STOCK_GRILLE_COLOR ? 0 : reference[index];
		assert(image[index] == expected);
	}

	reset_target();
	queue(RENDER_PRIMITIVE_POLYGON, 4, rectangle);
	shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, TEST_STOCK_GRILLE_COLOR, 0, 0,
						 HIRES_PAINT_PATTERN, TEST_STOCK_GRILLE_PATTERN);
	hires_end();
	assert(memcmp(reference, pixels(), sizeof(reference)) == 0);

	/* A ghost keeps its own grille mask even if an instance also requests
	 * the black stock-grille override. */
	reset_target();
	queue(RENDER_PRIMITIVE_POLYGON, 4, rectangle);
	shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON | RENDER_PRIMITIVE_GHOST_FLAG,
						 TEST_STOCK_GRILLE_COLOR, 0, 0, HIRES_PAINT_PATTERN,
						 TEST_STOCK_GRILLE_PATTERN);
	hires_end();
	memcpy(reference, pixels(), sizeof(reference));
	reset_target();
	queue(RENDER_PRIMITIVE_POLYGON, 4, rectangle);
	shape3d_hires_render(
		0,
		RENDER_PRIMITIVE_POLYGON | RENDER_PRIMITIVE_GHOST_FLAG | RENDER_PRIMITIVE_BLACK_GRILLE_FLAG,
		TEST_STOCK_GRILLE_COLOR, 0, 0, HIRES_PAINT_PATTERN, TEST_STOCK_GRILLE_PATTERN);
	hires_end();
	assert(memcmp(reference, pixels(), sizeof(reference)) == 0);
}

#define TEST_ROUND_REFERENCE_POINTS 64U
#define TEST_ROUND_OUTLINE_ERROR 0.25
#define TEST_ROUND_PHASE (3.0 / 8.0)
#define TEST_ROUND_INNER_SCALE (9472.0 / TRIG_FIXED_ONE)
#define TEST_ROUND_SPHERE_ASPECT (13.0 / 16.0)
#define TEST_ROUND_SMALL_RADIUS 2.0
#define TEST_ROUND_MEDIUM_RADIUS 8.0
#define TEST_ROUND_LARGE_RADIUS 32.0
#define TEST_ROUND_NEAR_RADIUS 96.0
#define TEST_ROUND_COLOR 10U
#define TEST_ROUND_RIM_COLOR 11U
#define TEST_ROUND_HUB_COLOR 12U
#define TEST_ROUND_BACKGROUND_COLOR 3U

enum TEST_ROUND_KIND {
	TEST_ROUND_SPHERE,
	TEST_ROUND_WHEEL,
	TEST_ROUND_SKEWED_WHEEL,
	TEST_ROUND_KIND_COUNT
};

static void round_fixture_axes(legacy_s32 kind, legacy_f64 radius, legacy_f64 *first_x,
							   legacy_f64 *first_y, legacy_f64 *second_x, legacy_f64 *second_y)
{
	*first_x = radius;
	*first_y = kind == TEST_ROUND_SKEWED_WHEEL ? radius / 4 : 0;
	*second_x = kind == TEST_ROUND_SKEWED_WHEEL ? radius / 2 : 0;
	*second_y = kind == TEST_ROUND_SPHERE ? radius * TEST_ROUND_SPHERE_ASPECT : radius;
}

static void draw_round_fixture(legacy_s32 kind, legacy_f64 radius, legacy_s32 background,
							   legacy_s32 ghost, legacy_s32 batched)
{
	legacy_f64 depth = projection_focal_length_x * HIRES_SCALE;
	struct SHAPE3D_HIRES_VECTOR vertices[6];
	vertices[0] = (struct SHAPE3D_HIRES_VECTOR){TEST_ROUND_PHASE, -TEST_ROUND_PHASE, depth};
	legacy_u8 type;
	legacy_u32 vertex_count;
	if (kind == TEST_ROUND_SPHERE) {
		vertices[1] = vertices[0];
		vertices[1].x += radius * 2;
		type = RENDER_PRIMITIVE_SPHERE;
		vertex_count = 2;
	} else {
		legacy_f64 first_x, first_y, second_x, second_y;
		round_fixture_axes(kind, radius, &first_x, &first_y, &second_x, &second_y);
		vertices[1] = (struct SHAPE3D_HIRES_VECTOR){TEST_ROUND_PHASE + first_x,
													-TEST_ROUND_PHASE - first_y, depth};
		vertices[2] = (struct SHAPE3D_HIRES_VECTOR){TEST_ROUND_PHASE + second_x,
													-TEST_ROUND_PHASE - second_y, depth};
		/* A coplanar wheel isolates outline/rim coverage from tread depth. */
		for (legacy_u32 vertex = 0; vertex < 3; vertex++) {
			vertices[vertex + 3] = vertices[vertex];
		}
		type = RENDER_PRIMITIVE_WHEEL;
		vertex_count = 6;
	}
	reset_target();
	shape3d_hires_begin_shape(0, background ? SHAPE3D_HIRES_DEPTH_BACKGROUND
											: SHAPE3D_HIRES_DEPTH_SORTED);
	queue(type, vertex_count, vertices);
	if (batched) {
		/* A full-screen background makes even tiny fixtures use worker bands. */
		legacy_f64 half_width = HIRES_WIDTH / 2;
		legacy_f64 half_height = HIRES_HEIGHT / 2;
		const struct SHAPE3D_HIRES_VECTOR panel[] = {{-half_width, -half_height, depth},
													 {half_width, -half_height, depth},
													 {half_width, half_height, depth},
													 {-half_width, half_height, depth}};
		const legacy_u8 indices[] = {0, 1, 2, 3};
		shape3d_hires_begin_shape(1, SHAPE3D_HIRES_DEPTH_BACKGROUND);
		shape3d_hires_queue(1, RENDER_PRIMITIVE_POLYGON, 4, indices, panel, 0);
		shape3d_hires_batch_begin();
		shape3d_hires_render(1, RENDER_PRIMITIVE_POLYGON, TEST_ROUND_BACKGROUND_COLOR, 0, 0, 0, 0);
	}
	shape3d_hires_render(0, type | (ghost ? RENDER_PRIMITIVE_GHOST_FLAG : 0), TEST_ROUND_COLOR,
						 TEST_ROUND_RIM_COLOR, TEST_ROUND_HUB_COLOR, 0, 0);
	if (batched) {
		shape3d_hires_batch_end();
	}
	hires_end();
}

/* Measure distance to the old 64-point outline independently of scan conversion. */
static legacy_f64 round_outline_distance_squared(legacy_s32 kind, legacy_f64 radius, legacy_f64 x,
												 legacy_f64 y)
{
	legacy_f64 first_x, first_y, second_x, second_y;
	round_fixture_axes(kind, radius, &first_x, &first_y, &second_x, &second_y);
	legacy_f64 minimum = DBL_MAX;
	for (legacy_u32 index = 0; index < TEST_ROUND_REFERENCE_POINTS; index++) {
		legacy_u32 next = (index + 1) % TEST_ROUND_REFERENCE_POINTS;
		legacy_u16 first_angle =
			(legacy_u16)(index * ANGLE_FULL_TURN / TEST_ROUND_REFERENCE_POINTS);
		legacy_u16 last_angle = (legacy_u16)(next * ANGLE_FULL_TURN / TEST_ROUND_REFERENCE_POINTS);
		legacy_f64 first_cosine = cos_fast(first_angle) / (legacy_f64)TRIG_FIXED_ONE;
		legacy_f64 first_sine = sin_fast(first_angle) / (legacy_f64)TRIG_FIXED_ONE;
		legacy_f64 last_cosine = cos_fast(last_angle) / (legacy_f64)TRIG_FIXED_ONE;
		legacy_f64 last_sine = sin_fast(last_angle) / (legacy_f64)TRIG_FIXED_ONE;
		legacy_f64 start_x = first_x * first_cosine + second_x * first_sine;
		legacy_f64 start_y = first_y * first_cosine + second_y * first_sine;
		legacy_f64 dx = first_x * last_cosine + second_x * last_sine - start_x;
		legacy_f64 dy = first_y * last_cosine + second_y * last_sine - start_y;
		legacy_f64 offset_x = x - start_x;
		legacy_f64 offset_y = y - start_y;
		legacy_f64 length_squared = dx * dx + dy * dy;
		legacy_f64 fraction =
			length_squared == 0 ? 0 : (offset_x * dx + offset_y * dy) / length_squared;
		if (fraction < 0) {
			fraction = 0;
		} else if (fraction > 1) {
			fraction = 1;
		}
		offset_x -= dx * fraction;
		offset_y -= dy * fraction;
		legacy_f64 distance = offset_x * offset_x + offset_y * offset_y;
		if (distance < minimum) {
			minimum = distance;
		}
	}
	return minimum;
}

static void test_roundness_preserves_near_and_bounds_small_shapes(void)
{
	static legacy_u8 reference[HIRES_WIDTH * HIRES_HEIGHT];
	const legacy_f64 radii[] = {TEST_ROUND_SMALL_RADIUS, TEST_ROUND_MEDIUM_RADIUS,
								TEST_ROUND_LARGE_RADIUS, TEST_ROUND_NEAR_RADIUS};
	legacy_u32 changed_small_pixels = 0;
	for (legacy_s32 kind = 0; kind < TEST_ROUND_KIND_COUNT; kind++) {
		for (legacy_u32 size = 0; size < sizeof(radii) / sizeof(radii[0]); size++) {
			legacy_f64 radius = radii[size];
			for (legacy_s32 ghost = 0; ghost < 2; ghost++) {
				/* Background geometry always retains the original 64-point perimeter. */
				draw_round_fixture(kind, radius, 1, ghost, 0);
				memcpy(reference, pixels(), sizeof(reference));
				draw_round_fixture(kind, radius, 0, ghost, 0);
				const legacy_u8 *image = pixels();
				if (radius == TEST_ROUND_NEAR_RADIUS) {
					assert(memcmp(reference, image, sizeof(reference)) == 0);
					continue;
				}
				for (legacy_s32 y = 0; y < HIRES_HEIGHT; y++) {
					for (legacy_s32 x = 0; x < HIRES_WIDTH; x++) {
						legacy_u32 offset = y * HIRES_WIDTH + x;
						if (reference[offset] == image[offset]) {
							continue;
						}
						changed_small_pixels++;
						legacy_f64 sample_x = x + HIRES_SAMPLE_CENTER_OFFSET -
											  projection_center_x * HIRES_SCALE - TEST_ROUND_PHASE;
						legacy_f64 sample_y = y + HIRES_SAMPLE_CENTER_OFFSET -
											  projection_center_y * HIRES_SCALE - TEST_ROUND_PHASE;
						legacy_f64 distance =
							round_outline_distance_squared(kind, radius, sample_x, sample_y);
						if (kind != TEST_ROUND_SPHERE) {
							legacy_f64 inner = round_outline_distance_squared(
								kind, radius * TEST_ROUND_INNER_SCALE, sample_x, sample_y);
							if (inner < distance) {
								distance = inner;
							}
						}
						assert(distance <= TEST_ROUND_OUTLINE_ERROR * TEST_ROUND_OUTLINE_ERROR);
					}
				}
			}
		}
	}
	assert(changed_small_pixels != 0);
}

static void test_roundness_matches_worker_bands(void)
{
	static legacy_u8 reference[HIRES_WIDTH * HIRES_HEIGHT];
	const legacy_char *settings[] = {"0", "1", "2"};
	const legacy_char *original = SDL_getenv("RESTUNTS_RENDER_WORKERS");
	legacy_char *saved = original != NULL ? SDL_strdup(original) : NULL;
	assert(original == NULL || saved != NULL);
	for (legacy_s32 kind = 0; kind < TEST_ROUND_KIND_COUNT; kind++) {
		for (legacy_s32 ghost = 0; ghost < 2; ghost++) {
			draw_round_fixture(kind, TEST_ROUND_LARGE_RADIUS, 0, ghost, 0);
			memcpy(reference, pixels(), sizeof(reference));
			for (legacy_u32 workers = 0; workers < sizeof(settings) / sizeof(settings[0]);
				 workers++) {
				assert(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "RESTUNTS_RENDER_WORKERS",
												  settings[workers], true));
				draw_round_fixture(kind, TEST_ROUND_LARGE_RADIUS, 0, ghost, 1);
				assert(memcmp(reference, pixels(), sizeof(reference)) == 0);
			}
		}
	}
	if (saved != NULL) {
		assert(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "RESTUNTS_RENDER_WORKERS", saved,
										  true));
		SDL_free(saved);
	} else {
		assert(SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "RESTUNTS_RENDER_WORKERS"));
	}
}

static void test_disabled_and_reset(void)
{
	const struct SHAPE3D_HIRES_VECTOR point[] = {{0, 0, 100}};
	reset_target();
	queue(RENDER_PRIMITIVE_POINT, 1, point);
	shape3d_hires_reset();
	shape3d_hires_render(0, RENDER_PRIMITIVE_POINT, 17, 0, 0, 0, 0);
	hires_end();
	assert(count_color(17) == 0);
	hires_set_enabled(0);
	queue(RENDER_PRIMITIVE_POINT, 1, point);
	hires_set_enabled(1);
	assert(hires_begin(&target));
	shape3d_hires_render(0, RENDER_PRIMITIVE_POINT, 17, 0, 0, 0, 0);
	hires_end();
	assert(count_color(17) == 0);
}

/* Exercise the production transform and culling gates, which the direct raster
 * tests above intentionally bypass. Thin road and body panels must survive
 * until their fractional coordinates reach the high-resolution rasterizer. */
static legacy_u8 scene_vertices[10U * SHAPE3D_VERTEX_SIZE];
static legacy_u8 scene_primitives[64];
static legacy_u8 scene_visibility[16];
static legacy_u8 scene_front_facing[16];
static legacy_u8 scene_polyinfo[POLYINFO_SUPERSIGHT_DATA_SIZE];
static legacy_s16 scene_colors[] = {7, 8, 9, 10};
static legacy_s16 scene_patterns[4];
static struct SHAPE3D scene_shape;
static struct TRANSFORMEDSHAPE3D scene_instance;

static void prepare_scene(const struct VECTOR *vertices, legacy_u32 count,
						  const legacy_u8 *primitives, legacy_u32 primitive_size,
						  legacy_s32 high_resolution)
{
	hires_shutdown();
	memset(screen, 3, 320 * 200);
	hires_set_enabled(high_resolution);
	sprite_select_target(&target);
	memset(&scene_shape, 0, sizeof(scene_shape));
	memset(&scene_instance, 0, sizeof(scene_instance));
	memset(scene_primitives, 0, sizeof(scene_primitives));
	memset(scene_visibility, 255, sizeof(scene_visibility));
	memcpy(scene_primitives, primitives, primitive_size);
	scene_shape.shape3d_numverts = (legacy_u16)count;
	scene_shape.shape3d_vertex_bytes = scene_vertices;
	scene_shape.shape3d_numpaints = 1;
	for (legacy_u32 offset = 0;
		 offset < primitive_size && primitives[offset] != SHAPE3D_PRIMITIVE_EMPTY;) {
		legacy_u8 type = primitives[offset];
		assert(type < SHAPE3D_PRIMITIVE_TYPE_COUNT);
		offset +=
			SHAPE3D_PRIMITIVE_HEADER_SIZE + scene_shape.shape3d_numpaints + primidxcounttab[type];
		assert(offset <= primitive_size);
		scene_shape.shape3d_numprimitives++;
	}
	scene_shape.shape3d_primitives = scene_primitives;
	scene_shape.shape3d_visibility_masks = scene_visibility;
	scene_shape.shape3d_front_facing_masks = scene_front_facing;
	for (legacy_u32 index = 0; index < count; index++) {
		shape3d_vertex_write(&scene_shape, (legacy_u16)index, &vertices[index]);
	}
	scene_instance.shapeptr = &scene_shape;
	scene_instance.rectptr = &bounds;
	scene_instance.ts_flags = 10; /* Pretransformed translation and bounding rectangle. */
	scene_instance.culling_distance = 1024;
	bounds.left = 320;
	bounds.top = 200;
	bounds.right = bounds.bottom = 0;
	polyinfoptr = scene_polyinfo;
	struct RECTANGLE clip = {0, 320, 0, 200};
	select_cliprect_rotate(0, 0, 0, &clip, 0);
	material_clrlist_ptr_cpy = scene_colors;
	material_clrlist2_ptr_cpy = scene_colors;
	material_patlist_ptr_cpy = scene_patterns;
	material_patlist2_ptr_cpy = scene_patterns;
}

static void test_visible_line_overhang_survives_culling(void)
{
	/* These centerlines project just outside the left and top viewport edges. */
	const struct VECTOR lines[][2] = {{{-121, -10, 120}, {-121, 10, 120}},
									  {{-10, 75, 119}, {10, 75, 119}}};
	const legacy_u8 primitive[] = {2, 0, 0, 0, 1, 0, 0};
	projection_focal_length_x = projection_focal_length_y = 159;
	for (legacy_u32 edge = 0; edge < 2; edge++) {
		prepare_scene(lines[edge], 2, primitive, sizeof(primitive), 1);
		assert(shape3d_transform_and_queue(&scene_instance) == 0);
		assert(shape3d_queued_primitive_count() == 1);
		shape3d_render_queued_primitives();
		assert(count_color(7) != 0);
		assert(pixels()[edge == 0 ? 400 * HIRES_WIDTH : 640] == 7);
	}
	projection_focal_length_x = projection_focal_length_y = 160;
}

#define ROUND_CULL_VERTEX_CAPACITY (SHAPE3D_WHEEL_RIM_VERTEX_COUNT * 2U)
#define ROUND_CULL_OUTSIDE_SHIFT 200

static void test_visible_round_overhang_survives_culling(void)
{
	/* Every defining point is offscreen, but a sphere or wheel extends in
	 * both directions from its center. Its visible rim must survive culling. */
	static const struct VECTOR models[][ROUND_CULL_VERTEX_CAPACITY] = {
		{{-110, 0, 100}, {-150, 0, 100}},
		{{-110, 0, 100},
		 {-150, 0, 100},
		 {-110, 20, 100},
		 {-121, 0, 110},
		 {-165, 0, 110},
		 {-121, 22, 110}}};
	static const legacy_u16 counts[] = {2, ROUND_CULL_VERTEX_CAPACITY};
	static const legacy_u8 primitives[][SHAPE3D_PRIMITIVE_HEADER_SIZE + 1U +
										ROUND_CULL_VERTEX_CAPACITY +
										SHAPE3D_PRIMITIVE_HEADER_SIZE] = {
		{SHAPE3D_PRIMITIVE_SPHERE, 0, 0, 0, 1, 0, 0},
		{SHAPE3D_PRIMITIVE_WHEEL, 0, 0, 0, 1, 2, 3, 4, 5, 0, 0}};
	for (legacy_u32 kind = 0; kind < SDL_arraysize(models); kind++) {
		for (legacy_s32 side = 0; side < 2; side++) {
			for (legacy_s32 outside = 0; outside < 2; outside++) {
				struct VECTOR model[ROUND_CULL_VERTEX_CAPACITY];
				for (legacy_u32 vertex = 0; vertex < counts[kind]; vertex++) {
					model[vertex] = models[kind][vertex];
					model[vertex].x -= outside * ROUND_CULL_OUTSIDE_SHIFT;
					if (side != 0) {
						model[vertex].x = -model[vertex].x;
					}
				}
				prepare_scene(model, counts[kind], primitives[kind], sizeof(primitives[kind]), 1);
				assert(shape3d_transform_and_queue(&scene_instance) ==
					   (outside ? LEGACY_U16_MAX : 0));
				assert(shape3d_queued_primitive_count() == (outside ? 0U : 1U));
				shape3d_render_queued_primitives();
				legacy_u32 coverage = count_color(7) + count_color(8) + count_color(9);
				assert((coverage != 0) == !outside);
			}
		}
	}
}

static void test_line_weight_in_half_scale_view(void)
{
	static legacy_u8 full_scale[HIRES_WIDTH * HIRES_HEIGHT];
	const struct VECTOR line[] = {{-100, 0, 1600}, {100, 0, 1600}};
	const legacy_u8 primitive[] = {2, 0, 0, 0, 1, 0, 0};
	for (legacy_s16 half_scale = 0; half_scale < 2; half_scale++) {
		prepare_scene(line, 2, primitive, sizeof(primitive), 1);
		struct RECTANGLE clip = {0, 320, 0, 200};
		select_cliprect_rotate(0, 0, 0, &clip, half_scale);
		assert(shape3d_transform_and_queue(&scene_instance) == 0);
		shape3d_render_queued_primitives();
		assert(count_color(7) != 0);
		if (half_scale == 0) {
			memcpy(full_scale, pixels(), sizeof(full_scale));
		} else {
			assert(memcmp(full_scale, pixels(), sizeof(full_scale)) == 0);
		}
	}
}

static void test_thin_polygon_and_attached_detail(void)
{
	const struct VECTOR road[] = {
		{-100, -10, 2000}, {-100, -10, 4000}, {100, -10, 4000}, {100, -10, 2000}};
	/* Rejecting the road must also discard its dependent line. Place the line
	 * along a side edge so its thicker stroke leaves some road visible. */
	const legacy_u8 primitives[] = {4, 0, 0, 0, 1, 2, 3, 2, 2, 1, 0, 1, 0, 0};
	prepare_scene(road, 4, primitives, sizeof(primitives), 0);
	assert(shape3d_transform_and_queue(&scene_instance) == LEGACY_U16_MAX);
	assert(shape3d_queued_primitive_count() == 0);

	prepare_scene(road, 4, primitives, sizeof(primitives), 1);
	assert(shape3d_transform_and_queue(&scene_instance) == 0);
	assert(shape3d_queued_primitive_count() == 2);
	shape3d_render_queued_primitives();
	assert(count_color(7) > 0);
	assert(count_color(8) > 0);
	assert(pixels()[402 * HIRES_WIDTH + 640] == 7);
}

static void test_full_polygon_winding(void)
{
	const struct VECTOR panel[] = {
		{-30, -20, 200}, {-30, 0, 200}, {-30, 20, 200}, {30, 20, 200}, {30, -20, 200}};
	const legacy_u8 forward[] = {5, 0, 0, 0, 1, 2, 3, 4, 0, 0};
	const legacy_u8 reverse[] = {5, 0, 0, 4, 3, 2, 1, 0, 0, 0};
	prepare_scene(panel, 5, forward, sizeof(forward), 0);
	assert(shape3d_transform_and_queue(&scene_instance) == LEGACY_U16_MAX);
	assert(shape3d_queued_primitive_count() == 0);

	prepare_scene(panel, 5, forward, sizeof(forward), 1);
	assert(shape3d_transform_and_queue(&scene_instance) == 0);
	assert(shape3d_queued_primitive_count() == 1);
	shape3d_render_queued_primitives();
	assert(count_color(7) > 20000);
	assert(pixels()[400 * HIRES_WIDTH + 640] == 7);

	prepare_scene(panel, 5, reverse, sizeof(reverse), 1);
	assert(shape3d_transform_and_queue(&scene_instance) == LEGACY_U16_MAX);
	assert(shape3d_queued_primitive_count() == 0);
	shape3d_render_queued_primitives();
	assert(count_color(7) == 0);
}

static void test_clipped_polygon_visibility(void)
{
	const struct VECTOR triangle[] = {{-10, -10, 1}, {0, 50, 100}, {50, -30, 100}};
	const legacy_u8 forward[] = {3, 0, 0, 0, 1, 2, 0, 0};
	const legacy_u8 reverse[] = {3, 0, 0, 2, 1, 0, 0, 0};
	prepare_scene(triangle, 3, forward, sizeof(forward), 1);
	assert(shape3d_transform_and_queue(&scene_instance) == 0);
	assert(shape3d_queued_primitive_count() == 1);
	shape3d_render_queued_primitives();
	assert(count_color(7) > 100000);

	prepare_scene(triangle, 3, reverse, sizeof(reverse), 1);
	assert(shape3d_transform_and_queue(&scene_instance) == LEGACY_U16_MAX);
	assert(shape3d_queued_primitive_count() == 0);
}

static void test_wheel_face_and_sort_depth(void)
{
	const struct VECTOR wheel[] = {{0, -1, 400}, {-20, -1, 400}, {0, 0, 400},
								   {5, -1, 420}, {-15, -1, 420}, {5, 0, 420}};
	const legacy_u8 primitive[] = {12, 0, 0, 0, 1, 2, 3, 4, 5, 0, 0};
	prepare_scene(wheel, 6, primitive, sizeof(primitive), 0);
	assert(shape3d_transform_and_queue(&scene_instance) == 0);
	assert(shape3d_queued_primitive_count() == 1);
	assert(LEGACY_READ_U16_LE(scene_polyinfo) == 420);

	prepare_scene(wheel, 6, primitive, sizeof(primitive), 1);
	assert(shape3d_transform_and_queue(&scene_instance) == 0);
	assert(shape3d_queued_primitive_count() == 1);
	assert(polyinfonumpolys == 0 && polyinfoptrnext == 0);
	shape3d_render_queued_primitives();
	assert(count_color(7) + count_color(8) + count_color(9) != 0);
}

static void test_crossing_surfaces_use_pixel_depth(void)
{
	const struct VECTOR panels[] = {{-60, -30, 100}, {-60, 30, 100}, {60, 30, 300}, {60, -30, 300},
									{-60, -30, 300}, {-60, 30, 300}, {60, 30, 100}, {60, -30, 100}};
	const legacy_u8 forward[] = {4, 0, 0, 0, 1, 2, 3, 4, 0, 1, 4, 5, 6, 7, 0, 0};
	const legacy_u8 reverse[] = {4, 0, 1, 4, 5, 6, 7, 4, 0, 0, 0, 1, 2, 3, 0, 0};
	for (legacy_u32 order = 0; order < 2; order++) {
		prepare_scene(panels, 8, order == 0 ? forward : reverse, sizeof(forward), 1);
		assert(shape3d_transform_and_queue(&scene_instance) == 0);
		assert(shape3d_queued_primitive_count() == 2);
		shape3d_render_queued_primitives();
		/* The equal average depths cannot order these intersecting panels:
		 * each is nearer on a different side of the image. */
		assert(pixels()[400 * HIRES_WIDTH + 600] == 7);
		assert(pixels()[400 * HIRES_WIDTH + 680] == 8);
	}
}

/* Different track elements can overlap even when their tile centers have
 * the opposite painter order. Preserve depth across every shape boundary. */
static void queue_polygon_shape(legacy_u32 index, legacy_s32 depth_mode,
								const struct SHAPE3D_HIRES_VECTOR *vertices)
{
	const legacy_u8 indices[] = {0, 1, 2, 3};
	shape3d_hires_begin_shape(index, depth_mode);
	shape3d_hires_queue(index, RENDER_PRIMITIVE_POLYGON, 4, indices, vertices, 0);
}

static void test_separate_shapes_use_pixel_depth(void)
{
	const struct SHAPE3D_HIRES_VECTOR panels[][4] = {
		{{-60, -30, 100}, {-60, 30, 100}, {60, 30, 300}, {60, -30, 300}},
		{{-60, -30, 300}, {-60, 30, 300}, {60, 30, 100}, {60, -30, 100}}};
	const legacy_s32 modes[] = {SHAPE3D_HIRES_DEPTH_ORDERED, SHAPE3D_HIRES_DEPTH_SORTED};
	for (legacy_u32 first_mode = 0; first_mode < 2; first_mode++) {
		for (legacy_u32 second_mode = 0; second_mode < 2; second_mode++) {
			for (legacy_u32 order = 0; order < 2; order++) {
				reset_target();
				queue_polygon_shape(0, modes[first_mode], panels[0]);
				queue_polygon_shape(1, modes[second_mode], panels[1]);
				for (legacy_u32 pass = 0; pass < 2; pass++) {
					legacy_u32 index = pass ^ order;
					shape3d_hires_render(index, RENDER_PRIMITIVE_POLYGON, (legacy_u16)(7 + index),
										 0, 0, 0, 0);
				}
				hires_end();
				assert(pixels()[400 * HIRES_WIDTH + 600] == 7);
				assert(pixels()[400 * HIRES_WIDTH + 680] == 8);
			}
		}
	}
}

static void test_separate_shapes_preserve_authored_overlays(void)
{
	const struct SHAPE3D_HIRES_VECTOR parent[] = {
		{-5, -2.5, 80}, {5, -2.5, 80}, {5, 2.5, 80}, {-5, 2.5, 80}};
	const struct SHAPE3D_HIRES_VECTOR marking[] = {
		{-5, -0.25, 80.5}, {5, -0.25, 80.5}, {5, 0.25, 80.5}, {-5, 0.25, 80.5}};
	const struct SHAPE3D_HIRES_VECTOR middle[] = {
		{-5, -2.5, 80.25}, {5, -2.5, 80.25}, {5, 2.5, 80.25}, {-5, 2.5, 80.25}};
	const struct SHAPE3D_HIRES_VECTOR nearer[] = {
		{-2.5, -1.25, 40}, {0, -1.25, 40}, {0, 1.25, 40}, {-2.5, 1.25, 40}};
	const legacy_u8 indices[] = {0, 1, 2, 3};
	const legacy_s32 modes[] = {SHAPE3D_HIRES_DEPTH_ORDERED, SHAPE3D_HIRES_DEPTH_SORTED};
	for (legacy_s32 batched = 0; batched < 2; batched++) {
		for (legacy_u32 mode = 0; mode < 2; mode++) {
			for (legacy_u32 order = 0; order < 2; order++) {
				reset_target();
				if (batched) {
					shape3d_hires_batch_begin();
				}
				queue_polygon_shape(0, modes[mode], parent);
				/* Both former ordering modes use an explicit attachment to retain
				 * paint authored behind its own support. */
				shape3d_hires_queue(1, RENDER_PRIMITIVE_POLYGON, 4, indices, marking,
									SHAPE3D_PRIMITIVE_SKIP_DEPTH_SORT_FLAG);
				queue_polygon_shape(2, SHAPE3D_HIRES_DEPTH_SORTED, middle);
				queue_polygon_shape(3, SHAPE3D_HIRES_DEPTH_SORTED, nearer);
				if (order == 0) {
					shape3d_hires_render(2, RENDER_PRIMITIVE_POLYGON, 9, 0, 0, 0, 0);
					shape3d_hires_render(3, RENDER_PRIMITIVE_POLYGON, 10, 0, 0, 0, 0);
				}
				shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 7, 0, 0, 0, 0);
				shape3d_hires_render(1, RENDER_PRIMITIVE_POLYGON, 8, 0, 0, 0, 0);
				if (order != 0) {
					shape3d_hires_render(2, RENDER_PRIMITIVE_POLYGON, 9, 0, 0, 0, 0);
					shape3d_hires_render(3, RENDER_PRIMITIVE_POLYGON, 10, 0, 0, 0, 0);
				}
				if (batched) {
					shape3d_hires_batch_end();
				}
				hires_end();
				/* Paint behind its own support stays visible, yet retains the
				 * support's depth against a surface between the road and paint. */
				assert(pixels()[400 * HIRES_WIDTH + 660] == 8);
				assert(pixels()[390 * HIRES_WIDTH + 660] == 7);
				assert(pixels()[400 * HIRES_WIDTH + 620] == 10);
			}
		}
	}
}

static void test_unattached_polygons_keep_nearest_surface(void)
{
	const struct SHAPE3D_HIRES_VECTOR parent[] = {
		{-5, -2.5, 80}, {5, -2.5, 80}, {5, 2.5, 80}, {-5, 2.5, 80}};
	const struct SHAPE3D_HIRES_VECTOR nearer[] = {
		{-2.5, -1.25, 40}, {2.5, -1.25, 40}, {2.5, 1.25, 40}, {-2.5, 1.25, 40}};
	const struct SHAPE3D_HIRES_VECTOR marking[] = {
		{-5, -0.25, 80.5}, {5, -0.25, 80.5}, {5, 0.25, 80.5}, {-5, 0.25, 80.5}};
	const struct SHAPE3D_HIRES_VECTOR middle[] = {
		{-3.75, -1.875, 60}, {3.75, -1.875, 60}, {3.75, 1.875, 60}, {-3.75, 1.875, 60}};
	const legacy_u8 indices[] = {0, 1, 2, 3};
	for (legacy_u32 order = 0; order < 2; order++) {
		reset_target();
		queue_polygon_shape(0, SHAPE3D_HIRES_DEPTH_ORDERED, parent);
		shape3d_hires_queue(1, RENDER_PRIMITIVE_POLYGON, 4, indices, nearer, 0);
		shape3d_hires_queue(2, RENDER_PRIMITIVE_POLYGON, 4, indices, marking, 0);
		queue_polygon_shape(3, SHAPE3D_HIRES_DEPTH_SORTED, middle);
		if (order == 0) {
			shape3d_hires_render(3, RENDER_PRIMITIVE_POLYGON, 10, 0, 0, 0, 0);
		}
		shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 7, 0, 0, 0, 0);
		shape3d_hires_render(1, RENDER_PRIMITIVE_POLYGON, 8, 0, 0, 0, 0);
		shape3d_hires_render(2, RENDER_PRIMITIVE_POLYGON, 9, 0, 0, 0, 0);
		if (order != 0) {
			shape3d_hires_render(3, RENDER_PRIMITIVE_POLYGON, 10, 0, 0, 0, 0);
		}
		hires_end();
		/* Unattached geometry uses its own depth even when its model had
		 * painter ordering. A rear panel cannot repaint the nearer surface. */
		assert(pixels()[400 * HIRES_WIDTH + 660] == 8);
		assert(pixels()[390 * HIRES_WIDTH + 660] == 8);
	}
}

static void test_scene_depth_resets_between_draws(void)
{
	const struct SHAPE3D_HIRES_VECTOR nearer[] = {
		{-5, -2.5, 80}, {5, -2.5, 80}, {5, 2.5, 80}, {-5, 2.5, 80}};
	const struct SHAPE3D_HIRES_VECTOR farther[] = {
		{-10, -5, 160}, {10, -5, 160}, {10, 5, 160}, {-10, 5, 160}};
	reset_target();
	queue_polygon_shape(0, SHAPE3D_HIRES_DEPTH_SORTED, nearer);
	queue_polygon_shape(1, SHAPE3D_HIRES_DEPTH_SORTED, farther);
	shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 7, 0, 0, 0, 0);
	hires_end();
	assert(pixels()[400 * HIRES_WIDTH + 640] == 7);
	assert(hires_begin(&target));
	shape3d_hires_render(1, RENDER_PRIMITIVE_POLYGON, 8, 0, 0, 0, 0);
	hires_end();
	assert(pixels()[400 * HIRES_WIDTH + 640] == 8);
	assert(hires_begin(&target));
	shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 7, 0, 0, 0, 0);
	shape3d_hires_reset();
	queue_polygon_shape(0, SHAPE3D_HIRES_DEPTH_SORTED, farther);
	shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 8, 0, 0, 0, 0);
	hires_end();
	assert(pixels()[400 * HIRES_WIDTH + 640] == 8);
}

static void test_separate_shapes_preserve_ghost_holes(void)
{
	const struct SHAPE3D_HIRES_VECTOR ghost[] = {
		{-5, -2.5, 80}, {5, -2.5, 80}, {5, 2.5, 80}, {-5, 2.5, 80}};
	const struct SHAPE3D_HIRES_VECTOR farther[] = {
		{-10, -5, 160}, {10, -5, 160}, {10, 5, 160}, {-10, 5, 160}};
	for (legacy_u32 order = 0; order < 2; order++) {
		reset_target();
		queue_polygon_shape(0, SHAPE3D_HIRES_DEPTH_SORTED, ghost);
		queue_polygon_shape(1, SHAPE3D_HIRES_DEPTH_ORDERED, farther);
		if (order == 0) {
			shape3d_hires_render(1, RENDER_PRIMITIVE_POLYGON, 7, 0, 0, 0, 0);
		}
		shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON | RENDER_PRIMITIVE_GHOST_FLAG, 8, 0, 0, 0,
							 0);
		if (order != 0) {
			shape3d_hires_render(1, RENDER_PRIMITIVE_POLYGON, 7, 0, 0, 0, 0);
		}
		hires_end();
		const legacy_u8 *image = pixels();
		for (legacy_s32 y = 380; y < 420; y++) {
			for (legacy_s32 x = 600; x < 680; x++) {
				legacy_u32 bit = ((y & 1) == 0 ? 8U : 0U) + 7U - (x & 7);
				legacy_u8 expected = (PRERENDER_BLACK_GRILLE_PATTERN & (1U << bit)) != 0 ? 0 : 7;
				assert(image[y * HIRES_WIDTH + x] == expected);
			}
		}
	}
}

static void test_background_shapes_do_not_occlude_scene(void)
{
	const struct SHAPE3D_HIRES_VECTOR background[] = {
		{-5, -2.5, 80}, {5, -2.5, 80}, {5, 2.5, 80}, {-5, 2.5, 80}};
	const struct SHAPE3D_HIRES_VECTOR farther[] = {
		{-10, -5, 160}, {0, -5, 160}, {0, 5, 160}, {-10, 5, 160}};
	const legacy_s32 modes[] = {SHAPE3D_HIRES_DEPTH_ORDERED, SHAPE3D_HIRES_DEPTH_SORTED};
	for (legacy_u32 mode = 0; mode < 2; mode++) {
		reset_target();
		/* Clouds use an artificial distance and must not hide world geometry. */
		queue_polygon_shape(0, SHAPE3D_HIRES_DEPTH_BACKGROUND, background);
		queue_polygon_shape(1, modes[mode], farther);
		shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 7, 0, 0, 0, 0);
		shape3d_hires_render(1, RENDER_PRIMITIVE_POLYGON, 8, 0, 0, 0, 0);
		hires_end();
		assert(pixels()[400 * HIRES_WIDTH + 620] == 8);
		assert(pixels()[400 * HIRES_WIDTH + 660] == 7);
	}
}

/* Opposite map corners are more than 32767 units apart after a diagonal
 * camera rotation, even though each world coordinate fits in a signed word. */
static void test_far_diagonal_geometry(void)
{
	const struct VECTOR panels[] = {
		{-2000, -1000, -1200}, {-2000, 1000, -1200}, {2000, 1000, 1200},  {2000, -1000, 1200},
		{-2000, -1000, 1200},  {-2000, 1000, 1200},	 {2000, 1000, -1200}, {2000, -1000, -1200}};
	const legacy_u8 forward[] = {4, 0, 0, 0, 1, 2, 3, 4, 0, 1, 4, 5, 6, 7, 0, 0};
	const legacy_u8 reverse[] = {4, 0, 1, 4, 5, 6, 7, 4, 0, 0, 0, 1, 2, 3, 0, 0};
	for (legacy_u32 order = 0; order < 2; order++) {
		prepare_scene(panels, 8, order == 0 ? forward : reverse, sizeof(forward), 1);
		struct RECTANGLE clip = {0, 320, 0, 200};
		select_cliprect_rotate(0, 0, -128, &clip, 0);
		scene_instance.ts_flags = 8;
		scene_instance.pos = (struct VECTOR){30000, 0, 30000};
		scene_instance.rotvec.z = 128;
		assert(shape3d_transform_and_queue(&scene_instance) == 0);
		assert(shape3d_queued_primitive_count() == 2);
		shape3d_render_queued_primitives();
		assert(pixels()[400 * HIRES_WIDTH + 620] == 7);
		assert(pixels()[400 * HIRES_WIDTH + 660] == 8);
	}
}

static void test_far_primitive_depth_visibility(void)
{
	const struct VECTOR panels[] = {
		{-2000, -1000, 1200},  {-2000, 1000, 1200},	 {2000, 1000, 1200},  {2000, -1000, 1200},
		{-2000, -1000, -1200}, {-2000, 1000, -1200}, {2000, 1000, -1200}, {2000, -1000, -1200}};
	const legacy_u8 forward[] = {4, 0, 0, 0, 1, 2, 3, 4, 0, 1, 4, 5, 6, 7, 0, 0};
	const legacy_u8 reverse[] = {4, 0, 1, 4, 5, 6, 7, 4, 0, 0, 0, 1, 2, 3, 0, 0};
	for (legacy_u32 order = 0; order < 2; order++) {
		prepare_scene(panels, 8, order == 0 ? forward : reverse, sizeof(forward), 1);
		struct RECTANGLE clip = {0, 320, 0, 200};
		select_cliprect_rotate(0, 0, -128, &clip, 0);
		scene_instance.ts_flags = 8;
		scene_instance.pos = (struct VECTOR){23000, 0, 23000};
		scene_instance.rotvec.z = 128;
		assert(shape3d_transform_and_queue(&scene_instance) == 0);
		assert(shape3d_queued_primitive_count() == 2);
		/* Their depths straddle the signed 16-bit boundary. Visibility must
		 * select the near panel without constructing a legacy painter queue. */
		assert(polyinfonumpolys == 0 && polyinfoptrnext == 0);
		shape3d_render_queued_primitives();
		assert(pixels()[400 * HIRES_WIDTH + 640] == 8);
	}
}

static void test_precise_scene_origin(void)
{
	static legacy_u8 reference[HIRES_WIDTH * HIRES_HEIGHT];
	const struct VECTOR panel[] = {
		{-1000, -500, 0}, {-1000, 500, 0}, {1000, 500, 0}, {1000, -500, 0}};
	const struct VECTOR position = {18000, -8000, 18000};
	const legacy_u8 primitive[] = {4, 0, 0, 0, 1, 2, 3, 0, 0};
	const legacy_s16 distant_scale = 16;
	for (legacy_s16 precise = 0; precise < 2; precise++) {
		for (legacy_s16 pass = 0; pass < 3; pass++) {
			legacy_s16 scale = pass == 1 ? distant_scale : 1;
			struct VECTOR packed[SDL_arraysize(panel)];
			struct SHAPE3D_HIRES_VECTOR vertices[SDL_arraysize(panel)];
			for (legacy_u16 vertex = 0; vertex < SDL_arraysize(panel); vertex++) {
				packed[vertex] = (struct VECTOR){panel[vertex].x * scale, panel[vertex].y * scale,
												 panel[vertex].z * scale};
				vertices[vertex] = (struct SHAPE3D_HIRES_VECTOR){(panel[vertex].x + 0.125) * scale,
																 (panel[vertex].y + 0.25) * scale,
																 (panel[vertex].z + 0.5) * scale};
			}
			prepare_scene(packed, SDL_arraysize(panel), primitive, sizeof(primitive), 1);
			struct RECTANGLE clip = {0, 320, 0, 200};
			select_cliprect_rotate(0, 0, -ANGLE_QUARTER_TURN / 2, &clip, 0);
			scene_instance.ts_flags = 8;
			scene_instance.pos = position;
			if (pass == 1) {
				/* Every precise origin component exceeds a full 16-bit word. A
				 * diagonal view also verifies that the offset precedes rotation. */
				scene_instance.pos = (struct VECTOR){100, 100, 100};
				struct SHAPE3D_HIRES_VECTOR origin = {
					(legacy_f64)position.x * scale - scene_instance.pos.x,
					(legacy_f64)position.y * scale - scene_instance.pos.y,
					(legacy_f64)position.z * scale - scene_instance.pos.z};
				shape3d_hires_set_origin(&origin);
			} else {
				shape3d_hires_set_origin(NULL);
			}
			assert((precise ? shape3d_hires_transform_and_queue(&scene_instance, vertices)
							: shape3d_transform_and_queue(&scene_instance)) == 0);
			assert(shape3d_queued_primitive_count() == 1);
			shape3d_render_queued_primitives();
			assert(count_color(7) != 0);
			if (pass == 0) {
				memcpy(reference, pixels(), sizeof(reference));
			} else {
				/* Moving the same scene farther away with matching geometry must
				 * preserve its pixels; resetting restores the original near view. */
				assert(memcmp(reference, pixels(), sizeof(reference)) == 0);
			}
		}
	}
}

static void test_body_panel_occludes_wheel(void)
{
	const struct VECTOR model[] = {
		{-100, -50, 500}, {-100, 50, 500}, {100, 50, 200}, {100, -50, 200}, {50, 0, 349},
		{30, 0, 349},	  {50, 20, 349},   {50, 0, 365},   {30, 0, 365},	{50, 20, 365}};
	const legacy_u8 primitives[] = {4, 0, 0, 0, 1, 2, 3, 12, 0, 1, 4, 5, 6, 7, 8, 9, 0, 0};
	prepare_scene(model, 10, primitives, sizeof(primitives), 1);
	assert(shape3d_transform_and_queue(&scene_instance) == 0);
	assert(shape3d_queued_primitive_count() == 2);
	/* The body averages 350 while the wheel is 349. At the wheel's screen
	 * position the sloping body is actually nearer, at approximately 288. */
	assert(polyinfonumpolys == 0 && polyinfoptrnext == 0);
	shape3d_render_queued_primitives();
	assert(pixels()[400 * HIRES_WIDTH + 732] == 7);
	assert(count_color(8) + count_color(9) + count_color(10) == 0);
}

/* A road split into tiles must retain its continuous interior, even when the
 * tiles use different origins and model orientations. */
static void draw_joined_road(legacy_s32 split, legacy_s16 roll, legacy_s16 pitch, legacy_s16 yaw,
							 legacy_s16 depth, legacy_s16 half_scale, legacy_s16 reverse_order,
							 legacy_s16 turn)
{
	const struct VECTOR road[] = {
		{-200, 0, -1024}, {-200, 0, 1024}, {200, 0, 1024}, {200, 0, -1024}};
	const legacy_u8 primitive[] = {4, 1, 0, 0, 1, 2, 3, 0, 0};
	prepare_scene(road, 4, primitive, sizeof(primitive), 1);
	struct RECTANGLE clip = {0, 320, 0, 200};
	select_cliprect_rotate(roll, pitch, yaw, &clip, half_scale);
	scene_instance.ts_flags = 8;
	scene_instance.pos = (struct VECTOR){17, -233, depth};
	if (!split) {
		assert(shape3d_transform_and_queue(&scene_instance) == 0);
	} else {
		for (legacy_s16 index = 0; index < 2; index++) {
			legacy_s16 tile = reverse_order ? 1 - index : index;
			legacy_s16 rotation = tile ? turn : 0;
			struct MATRIX inverse;
			mat_rot_y(&inverse, -rotation);
			for (legacy_u16 vertex = 0; vertex < 4; vertex++) {
				struct VECTOR local = road[vertex];
				local.z /= 2;
				struct VECTOR rotated;
				mat_mul_vector(&local, &inverse, &rotated);
				shape3d_vertex_write(&scene_shape, vertex, &rotated);
			}
			scene_instance.rotvec.z = rotation;
			scene_instance.pos.z = depth + (tile ? 512 : -512) / (half_scale ? 2 : 1);
			assert(shape3d_transform_and_queue(&scene_instance) == 0);
		}
	}
	shape3d_render_queued_primitives();
	assert(count_color(7) > 1000);
}

static void test_joined_track_surfaces(void)
{
	static legacy_u8 continuous[HIRES_WIDTH * HIRES_HEIGHT];
	static legacy_u8 interior[HIRES_WIDTH * HIRES_HEIGHT];
	static legacy_u8 tiled[HIRES_WIDTH * HIRES_HEIGHT];
	static const legacy_s16 views[][4] = {
		{0, 0, 0, 1600}, {11, 17, 37, 1600}, {-19, -13, -71, 1600}, {7, 23, 9, 650}};
	for (legacy_u32 view = 0; view < sizeof(views) / sizeof(views[0]); view++) {
		for (legacy_s16 half_scale = 0; half_scale < 2; half_scale++) {
			draw_joined_road(0, views[view][0], views[view][1], views[view][2], views[view][3],
							 half_scale, 0, 0);
			memcpy(continuous, pixels(), sizeof(continuous));
			/* Splitting an outer edge can move an exact pixel-center tie by a
			 * rounding bit. Check the uniform interior on both sides of the
			 * silhouette; every tile join within the road must stay covered. */
			memset(interior, 0, sizeof(interior));
			for (legacy_s32 y = 1; y < HIRES_HEIGHT - 1; y++) {
				for (legacy_s32 x = 1; x < HIRES_WIDTH - 1; x++) {
					legacy_s32 offset = y * HIRES_WIDTH + x;
					legacy_u8 color = continuous[offset];
					legacy_s32 uniform = 1;
					for (legacy_s32 row = -1; row <= 1; row++) {
						for (legacy_s32 column = -1; column <= 1; column++) {
							uniform &= continuous[offset + row * HIRES_WIDTH + column] == color;
						}
					}
					interior[offset] = uniform ? color : 0;
				}
			}
			for (legacy_s16 turn = 0; turn < ANGLE_FULL_TURN; turn += ANGLE_QUARTER_TURN) {
				for (legacy_s16 order = 0; order < 2; order++) {
					draw_joined_road(1, views[view][0], views[view][1], views[view][2],
									 views[view][3], half_scale, order, turn);
					const legacy_u8 *image = pixels();
					if (turn == 0 && order == 0) {
						memcpy(tiled, image, sizeof(tiled));
					} else {
						assert(memcmp(tiled, image, sizeof(tiled)) == 0);
					}
					for (legacy_u32 pixel = 0; pixel < sizeof(interior); pixel++) {
						assert(interior[pixel] == 0 || image[pixel] == interior[pixel]);
					}
				}
			}
		}
	}
}

static void shared_edge_queue(const struct SHAPE3D_HIRES_VECTOR *vertices, const legacy_u8 *indices,
							  legacy_u16 index)
{
	shape3d_hires_queue(index, RENDER_PRIMITIVE_POLYGON, 3, indices, vertices, 0);
}

static void test_shared_edge_pixel_coverage(void)
{
	/* The diagonal projects through pixel centers; its two traversal
	 * directions previously rounded to different columns on some rows. */
	const struct SHAPE3D_HIRES_VECTOR rectangle[] = {
		{-455, 200, 640}, {315, 46, 640}, {-455, 46, 640}, {315, 200, 640}};
	const legacy_u8 forward[][3] = {{0, 1, 2}, {1, 0, 3}};
	const legacy_u8 reverse[][3] = {{2, 1, 0}, {3, 0, 1}};
	for (legacy_u32 winding = 0; winding < 2; winding++) {
		reset_target();
		shape3d_hires_begin_shape(0, 0);
		const legacy_u8(*indices)[3] = winding == 0 ? forward : reverse;
		shared_edge_queue(rectangle, indices[0], 0);
		shared_edge_queue(rectangle, indices[1], 1);
		shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 7, 0, 0, 0, 0);
		shape3d_hires_render(1, RENDER_PRIMITIVE_POLYGON, 7, 0, 0, 0, 0);
		hires_end();
		const legacy_u8 *image = pixels();
		for (legacy_s32 y = 200; y < 354; y++) {
			for (legacy_s32 x = 185; x < 955; x++) {
				assert(image[y * HIRES_WIDTH + x] == 7);
			}
		}
	}
}

static void test_shared_edge_near_clipping(void)
{
	/* Every clipped corner lands exactly on the named pixel center. Reversing
	 * the near-plane crossing must preserve that covered pixel at every scale. */
	const struct {
		legacy_s32 scale;
		legacy_f64 far_x, far_y;
		legacy_s32 sample_x, sample_y;
	} cases[] = {{HIRES_SCALE, -4962, 5556, 113, 168},
				 {HIRES_SCALE / 2, -4974, 5544, 56, 84},
				 {1, -4950, 5520, 28, 42}};
	const legacy_u8 indices[][3] = {{0, 1, 2}, {2, 1, 0}};
	for (legacy_u32 scale = 0; scale < SDL_arraysize(cases); scale++) {
		const struct SHAPE3D_HIRES_VECTOR triangle[] = {
			{-6, 0, 11}, {cases[scale].far_x, cases[scale].far_y, 1291}, {2, 0, 757}};
		for (legacy_u32 winding = 0; winding < 2; winding++) {
			reset_target();
			hires_end();
			hires_set_render_scale(cases[scale].scale);
			assert(hires_begin(&target));
			shape3d_hires_begin_shape(0, 0);
			shared_edge_queue(triangle, indices[winding], 0);
			shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 7, 0, 0, 0, 0);
			hires_end();
			assert(pixels()[cases[scale].sample_y * hires_render_width() + cases[scale].sample_x] ==
				   7);
		}
	}
}

/* A concave outline has four active edges below the notch. The left edge
 * ends and restarts exactly where the notch begins, and the final vertex
 * repeats the first. Pixel-center ties must survive clipping and batching. */
static void test_concave_polygon_scanlines(void)
{
	enum {
		CONCAVE_LEFT = 180,
		CONCAVE_RIGHT = 1060,
		CONCAVE_TOP = 63,
		CONCAVE_BOTTOM = 703,
		CONCAVE_NOTCH_LEFT = 420,
		CONCAVE_NOTCH_RIGHT = 820,
		CONCAVE_NOTCH_TOP = 383,
		CONCAVE_COLOR = 7,
		CONCAVE_BACKGROUND = 3,
		CONCAVE_CLIP_LEFT = 40,
		CONCAVE_CLIP_RIGHT = 240,
		CONCAVE_CLIP_TOP = 25,
		CONCAVE_CLIP_BOTTOM = 175,
		CONCAVE_VERTEX_COUNT = 10
	};
	const legacy_s32 outline[CONCAVE_VERTEX_COUNT][2] = {{CONCAVE_LEFT, CONCAVE_TOP},
														 {CONCAVE_RIGHT, CONCAVE_TOP},
														 {CONCAVE_RIGHT, CONCAVE_BOTTOM},
														 {CONCAVE_NOTCH_RIGHT, CONCAVE_BOTTOM},
														 {CONCAVE_NOTCH_RIGHT, CONCAVE_NOTCH_TOP},
														 {CONCAVE_NOTCH_LEFT, CONCAVE_NOTCH_TOP},
														 {CONCAVE_NOTCH_LEFT, CONCAVE_BOTTOM},
														 {CONCAVE_LEFT, CONCAVE_BOTTOM},
														 {CONCAVE_LEFT, CONCAVE_NOTCH_TOP},
														 {CONCAVE_LEFT, CONCAVE_TOP}};
	struct SHAPE3D_HIRES_VECTOR vertices[CONCAVE_VERTEX_COUNT];
	legacy_u8 indices[CONCAVE_VERTEX_COUNT];
	for (legacy_s32 collapsed = 0; collapsed < 2; collapsed++) {
		for (legacy_u32 index = 0; index < CONCAVE_VERTEX_COUNT; index++) {
			vertices[index].x =
				outline[index][0] + HIRES_SAMPLE_CENTER_OFFSET - projection_center_x * HIRES_SCALE;
			vertices[index].y =
				projection_center_y * HIRES_SCALE -
				((collapsed ? CONCAVE_TOP : outline[index][1]) + HIRES_SAMPLE_CENTER_OFFSET);
			vertices[index].z = projection_focal_length_x * HIRES_SCALE;
		}
		for (legacy_s32 winding = 0; winding < 2; winding++) {
			for (legacy_u32 index = 0; index < CONCAVE_VERTEX_COUNT; index++) {
				indices[index] = (legacy_u8)(winding ? CONCAVE_VERTEX_COUNT - 1 - index : index);
			}
			for (legacy_s32 clipped = 0; clipped < 2; clipped++) {
				target.sprite_raster_left = clipped ? CONCAVE_CLIP_LEFT : 0;
				target.sprite_raster_right =
					clipped ? CONCAVE_CLIP_RIGHT : HIRES_WIDTH / HIRES_SCALE;
				target.sprite_top = clipped ? CONCAVE_CLIP_TOP : 0;
				target.sprite_bottom = clipped ? CONCAVE_CLIP_BOTTOM : HIRES_HEIGHT / HIRES_SCALE;
				for (legacy_s32 batched = 0; batched < 2; batched++) {
					reset_target();
					shape3d_hires_queue(0, RENDER_PRIMITIVE_POLYGON, CONCAVE_VERTEX_COUNT, indices,
										vertices, 0);
					if (batched) {
						shape3d_hires_batch_begin();
					}
					shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, CONCAVE_COLOR, 0, 0, 0, 0);
					if (batched) {
						shape3d_hires_batch_end();
					}
					hires_end();
					const legacy_u8 *image = pixels();
					for (legacy_s32 y = 0; y < HIRES_HEIGHT; y++) {
						for (legacy_s32 x = 0; x < HIRES_WIDTH; x++) {
							legacy_s32 covered = !collapsed && x >= CONCAVE_LEFT &&
												 x < CONCAVE_RIGHT && y >= CONCAVE_TOP &&
												 y < CONCAVE_BOTTOM &&
												 (y < CONCAVE_NOTCH_TOP || x < CONCAVE_NOTCH_LEFT ||
												  x >= CONCAVE_NOTCH_RIGHT) &&
												 x >= target.sprite_raster_left * HIRES_SCALE &&
												 x < target.sprite_raster_right * HIRES_SCALE &&
												 y >= target.sprite_top * HIRES_SCALE &&
												 y < target.sprite_bottom * HIRES_SCALE;
							assert(image[y * HIRES_WIDTH + x] ==
								   (covered ? CONCAVE_COLOR : CONCAVE_BACKGROUND));
						}
					}
				}
			}
		}
	}
	target.sprite_raster_left = target.sprite_top = 0;
	target.sprite_raster_right = HIRES_WIDTH / HIRES_SCALE;
	target.sprite_bottom = HIRES_HEIGHT / HIRES_SCALE;
}

static void test_hypervision_full_scene_queue(void)
{
	/* A dense scene exceeds both the old byte buffer and every 16-bit index.
	 * Three out-of-order points per shape also grow the queue mid-shape. */
	const struct VECTOR vertices[] = {{0, 0, 1000}, {0, 0, 2000}, {0, 0, 1500}};
	const legacy_u8 primitives[] = {1, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 2, 0, 0};
	const legacy_u32 shape_count = 23334;
	const legacy_u32 count = shape_count * 3U;
	prepare_scene(vertices, 3, primitives, sizeof(primitives), 1);
	polyinfo_set_supersight(1);
	for (legacy_u32 index = 0; index < shape_count; index++) {
		scene_instance.pos.x = index == 0 ? -200 : index + 1U == shape_count ? 200 : 0;
		if (index + 1U == shape_count) {
			scene_primitives[2] = scene_primitives[6] = scene_primitives[10] = 1;
		}
		assert(shape3d_transform_and_queue(&scene_instance) == 0);
	}
	assert(shape3d_queued_primitive_count() == count);
	/* Enhanced commands exceed the old 16-bit limits without serializing
	 * or sorting any classic polygon records. */
	assert(count > LEGACY_U16_MAX);
	assert(polyinfonumpolys == 0 && polyinfoptrnext == 0);
	shape3d_render_queued_primitives();
	assert(shape3d_queued_primitive_count() == 0);
	assert(count_color(7) != 0);
	assert(count_color(8) != 0);

	/* Family 65536 must stay distinct from the zero/empty depth marker. */
	reset_target();
	const struct SHAPE3D_HIRES_VECTOR wide_point[] = {{100, 100, 1000}};
	const legacy_u8 wide_indices[] = {0};
	shape3d_hires_begin_shape(LEGACY_U16_MAX, 1);
	shape3d_hires_queue(LEGACY_U16_MAX, RENDER_PRIMITIVE_POINT, 1, wide_indices, wide_point, 0);
	bounds.left = 320;
	bounds.top = 200;
	bounds.right = bounds.bottom = 0;
	shape3d_hires_update_bounds(LEGACY_U16_MAX, RENDER_PRIMITIVE_POINT, &bounds);
	shape3d_hires_render(LEGACY_U16_MAX, RENDER_PRIMITIVE_POINT, 9, 0, 0, 0, 0);
	hires_end();
	assert(count_color(9) != 0);

	/* F12 restores the exact legacy limit even after native queues have grown.
	 * A new target buffer must also be safe when reusing the cached allocation. */
	polyinfo_set_supersight(0);
	const legacy_u8 point[] = {1, 0, 0, 0, 0, 0};
	prepare_scene(vertices, 1, point, sizeof(point), 0);
	for (legacy_u32 index = 1; index < POLYINFO_LEGACY_PRIMITIVE_CAPACITY; index++) {
		assert(shape3d_transform_and_queue(&scene_instance) == 0);
	}
	assert(shape3d_transform_and_queue(&scene_instance) == 1);
	assert(shape3d_queued_primitive_count() == POLYINFO_LEGACY_PRIMITIVE_CAPACITY);
	prepare_scene(vertices, 1, point, sizeof(point), 1);
	polyinfo_set_supersight(1);
	for (legacy_u32 index = 0; index < 2000; index++) {
		assert(shape3d_transform_and_queue(&scene_instance) == 0);
	}
	assert(shape3d_queued_primitive_count() == 2000);
	assert(polyinfonumpolys == 0 && polyinfoptrnext == 0);
	shape3d_render_queued_primitives();
	assert(count_color(7) != 0);
	polyinfo_set_supersight(0);
}

/* Every primitive is queued before recording the immutable batch. The wide
 * surfaces provide enough work to exercise workers, while crossing depths,
 * authored overlays and patterned holes expose ordering or band-edge errors. */
static legacy_s32 draw_batch_scene(legacy_s32 batched, legacy_s32 ordered)
{
	static const struct SHAPE3D_HIRES_VECTOR vertices[][6] = {
		{{-2000, -1300, 2000}, {2000, -1300, 2000}, {2000, 1300, 2000}, {-2000, 1300, 2000}},
		{{-100, -60, 100}, {-100, 60, 100}, {100, 60, 300}, {100, -60, 300}},
		{{-100, -60, 300}, {-100, 60, 300}, {100, 60, 100}, {100, -60, 100}},
		{{-45, -40, 80}, {45, -40, 80}, {45, 40, 80}, {-45, 40, 80}},
		{{-43, 9.9, 80.5}, {43, 9.9, 80.5}, {43, 10.1, 80.5}, {-43, 10.1, 80.5}},
		{{10, -25, 60}, {40, -25, 60}, {40, 25, 60}, {10, 25, 60}},
		{{-20, -10, 40}, {-10, -10, 40}, {-20, 0, 40}, {-10, -10, 42}, {0, -10, 42}, {-10, 0, 42}},
		{{15, 10, 40}, {19, 10, 40}},
		{{-70, -45, 70}, {70, 45, 140}},
		{{-2, 1, 3}, {30, 20, 60}},
		{{-20, -0.1, 30}, {20, -0.1, 30}, {20, 0.1, 30}, {-20, 0.1, 30}},
		{{0.025, 0.016, 20}},
		/* The point rounds inward from the left edge. The line straddles
		 * the top edge while retaining coverage at the first pixel center. */
		{{-100.0390625, 46.875, 100}},
		{{-84.375, 62.4609375, 100}, {-81.25, 62.4609375, 100}}};
	static const legacy_u8 types[] = {
		RENDER_PRIMITIVE_POLYGON, RENDER_PRIMITIVE_POLYGON,
		RENDER_PRIMITIVE_POLYGON, RENDER_PRIMITIVE_POLYGON,
		RENDER_PRIMITIVE_POLYGON, RENDER_PRIMITIVE_POLYGON | RENDER_PRIMITIVE_GHOST_FLAG,
		RENDER_PRIMITIVE_WHEEL,	  RENDER_PRIMITIVE_SPHERE,
		RENDER_PRIMITIVE_LINE,	  RENDER_PRIMITIVE_LINE,
		RENDER_PRIMITIVE_POLYGON, RENDER_PRIMITIVE_POINT,
		RENDER_PRIMITIVE_POINT,	  RENDER_PRIMITIVE_POLYGON};
	static const legacy_u8 counts[] = {4, 4, 4, 4, 4, 4, 6, 2, 2, 2, 4, 1, 1, 2};
	static const legacy_u16 paints[][5] = {
		{4, 0, 0, 0, 0},  {5, 6, 0, 2, 0xAA55}, {7, 0, 0, 0, 0},	{8, 0, 0, 0, 0},
		{9, 0, 0, 0, 0},  {0, 0, 0, 0, 0},		{10, 11, 12, 0, 0}, {13, 0, 0, 0, 0},
		{14, 0, 0, 0, 0}, {15, 0, 0, 0, 0},		{16, 0, 0, 0, 0},	{17, 0, 0, 0, 0},
		{18, 0, 0, 0, 0}, {19, 0, 0, 0, 0}};
	const legacy_u8 indices[] = {0, 1, 2, 3, 4, 5};
	shape3d_hires_reset();
	for (legacy_u32 index = 0; index < sizeof(counts); index++) {
		legacy_s32 mode = index == 0			  ? SHAPE3D_HIRES_DEPTH_BACKGROUND
						  : index == 3 && ordered ? SHAPE3D_HIRES_DEPTH_ORDERED
												  : SHAPE3D_HIRES_DEPTH_SORTED;
		if (index != 4) {
			shape3d_hires_begin_shape(index, mode);
		}
		legacy_u16 flags = index == 4	 ? SHAPE3D_PRIMITIVE_SKIP_DEPTH_SORT_FLAG
						   : index == 10 ? 3
										 : 0;
		shape3d_hires_queue(index, types[index] & ~RENDER_PRIMITIVE_GHOST_FLAG, counts[index],
							indices, vertices[index], flags);
	}
	if (batched) {
		shape3d_hires_batch_begin();
	}
	for (legacy_u32 pass = 0; pass < sizeof(counts); pass++) {
		/* Reverse the intersecting surfaces without moving decals before
		 * their supporting geometry. Each order has its own serial oracle. */
		legacy_u32 index = !ordered && (pass == 1 || pass == 2) ? 3 - pass : pass;
		const legacy_u16 *paint = paints[index];
		shape3d_hires_render(index, types[index], paint[0], paint[1], paint[2], paint[3], paint[4]);
	}
	return batched ? shape3d_hires_batch_end() : 0;
}

static void test_parallel_batches_match_serial(void)
{
	static legacy_u8 reference[HIRES_WIDTH * HIRES_HEIGHT];
	const legacy_char *settings[] = {"0", "1", "2"};
	const legacy_char *original_setting = SDL_getenv("RESTUNTS_RENDER_WORKERS");
	legacy_char *saved_setting = original_setting != NULL ? SDL_strdup(original_setting) : NULL;
	assert(original_setting == NULL || saved_setting != NULL);
	projection_center_x = 160;
	projection_center_y = 100;
	projection_focal_length_x = projection_focal_length_y = 160;
	for (legacy_s32 clipped = 0; clipped < 2; clipped++) {
		target.sprite_raster_left = clipped ? 13 : 0;
		target.sprite_raster_right = clipped ? 307 : 320;
		target.sprite_top = clipped ? 7 : 0;
		target.sprite_bottom = clipped ? 193 : 200;
		bounds = (struct RECTANGLE){target.sprite_raster_left, target.sprite_raster_right,
									target.sprite_top, target.sprite_bottom};
		reset_target();
		assert(draw_batch_scene(0, !clipped) == 0);
		hires_end();
		/* Require coverage from the pattern, ghost and every primitive kind;
		 * otherwise an empty or fully occluded fixture could compare equal. */
		assert(count_color(0) != 0);
		for (legacy_u8 color = 4; color <= 17; color++) {
			assert(count_color(color) != 0);
		}
		if (!clipped) {
			assert(pixels()[100 * HIRES_WIDTH] == 18);
			assert(pixels()[100] == 19);
		}
		memcpy(reference, pixels(), sizeof(reference));
		for (legacy_s32 workers = 0; workers < 3; workers++) {
			assert(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "RESTUNTS_RENDER_WORKERS",
											  settings[workers], true));
			reset_target();
			for (legacy_s32 iteration = 0; iteration < 3; iteration++) {
				if (iteration != 0) {
					if (iteration == 2) {
						/* F12 releases surfaces and workers, then both must be
						 * ready again when SuperSight is enabled. */
						hires_set_enabled(0);
						hires_set_enabled(1);
					}
					assert(hires_begin(&target));
				}
#if defined(__DJGPP__)
				assert(draw_batch_scene(1, !clipped) == 0);
#else
				assert(draw_batch_scene(1, !clipped) == workers);
#endif
				hires_end();
				assert(memcmp(reference, pixels(), sizeof(reference)) == 0);
			}
		}
	}
	target.sprite_raster_left = target.sprite_top = 0;
	target.sprite_raster_right = 320;
	target.sprite_bottom = 200;
	reset_target();
	shape3d_hires_batch_begin();
	assert(shape3d_hires_batch_end() == 0);
	const struct SHAPE3D_HIRES_VECTOR point[] = {{0, 0, 100}};
	queue(RENDER_PRIMITIVE_POINT, 1, point);
	shape3d_hires_batch_begin();
	shape3d_hires_render(0, RENDER_PRIMITIVE_POINT, 17, 0, 0, 0, 0);
	/* Tiny previews should bypass worker synchronization even when enabled. */
	assert(shape3d_hires_batch_end() == 0);
	hires_end();
	assert(count_color(17) != 0);
	hires_shutdown();
	if (saved_setting != NULL) {
		assert(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "RESTUNTS_RENDER_WORKERS",
										  saved_setting, true));
		SDL_free(saved_setting);
	} else {
		assert(SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "RESTUNTS_RENDER_WORKERS"));
	}
}

/* Resolution changes reproject geometry and shrink the raster workload while
 * preserving logical clipping, every primitive family and joined-worker output. */
static void test_render_scale_roundtrip(void)
{
	static legacy_u8 reference[HIRES_WIDTH * HIRES_HEIGHT];
	static legacy_u8 full_resolution[HIRES_WIDTH * HIRES_HEIGHT];
	const legacy_s32 scales[] = {HIRES_SCALE, HIRES_SCALE / 2, 1, HIRES_SCALE};
	const legacy_char *settings[] = {"0", "2"};
	const legacy_char *original_setting = SDL_getenv("RESTUNTS_RENDER_WORKERS");
	legacy_char *saved_setting = original_setting != NULL ? SDL_strdup(original_setting) : NULL;
	assert(original_setting == NULL || saved_setting != NULL);
	projection_center_x = 160;
	projection_center_y = 100;
	projection_focal_length_x = projection_focal_length_y = 160;
	target.sprite_raster_left = 13;
	target.sprite_raster_right = 307;
	target.sprite_top = 7;
	target.sprite_bottom = 193;
	for (legacy_u32 setting = 0; setting < SDL_arraysize(settings); setting++) {
		assert(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "RESTUNTS_RENDER_WORKERS",
										  settings[setting], true));
		reset_target();
		hires_end();
		for (legacy_u32 step = 0; step < SDL_arraysize(scales); step++) {
			legacy_s32 scale = scales[step];
			hires_set_render_scale(scale);
			assert(hires_enabled());
			legacy_s32 width = hires_render_width();
			legacy_s32 height = hires_render_height();
			assert(width == 320 * scale && height == 200 * scale);
			const struct SHAPE3D_HIRES_VECTOR vertex = {13.25, -7.75, 320};
			struct SHAPE3D_HIRES_POINT point;
			shape3d_hires_project(&vertex, &point);
			assert(point.x == (160 + vertex.x / 2) * scale);
			assert(point.y == (100 - vertex.y / 2) * scale);
			/* Compare binary64 values even when x87 evaluates expressions more precisely. */
			volatile legacy_f64 expected_inverse_z = 1.0 / vertex.z;
			assert(point.inverse_z == expected_inverse_z);
			assert(hires_begin(&target));
			assert(draw_batch_scene(0, 1) == 0);
			hires_end();
			const legacy_u8 *image = pixels();
			legacy_u32 drawn = 0;
			for (legacy_s32 y = 0; y < height; y++) {
				for (legacy_s32 x = 0; x < width; x++) {
					legacy_s32 inside = x >= target.sprite_raster_left * scale &&
										x < target.sprite_raster_right * scale &&
										y >= target.sprite_top * scale &&
										y < target.sprite_bottom * scale;
					assert((image[y * width + x] != 3) == inside);
					drawn += inside;
				}
			}
			/* One quarter, then one sixteenth as many destination samples. */
			assert(drawn == (legacy_u32)(target.sprite_raster_right - target.sprite_raster_left) *
								(target.sprite_bottom - target.sprite_top) * scale * scale);
			memcpy(reference, image, (size_t)width * height);
			if (step == 0) {
				memcpy(full_resolution, image, sizeof(full_resolution));
			} else if (step == SDL_arraysize(scales) - 1) {
				assert(memcmp(full_resolution, image, sizeof(full_resolution)) == 0);
			}
			assert(hires_begin(&target));
			legacy_s32 workers = draw_batch_scene(1, 1);
#if defined(__DJGPP__)
			assert(workers == 0);
#else
			assert(workers == (setting == 0 ? 0 : 2));
#endif
			hires_end();
			assert(memcmp(reference, pixels(), (size_t)width * height) == 0);
		}
	}
	hires_shutdown();
	if (saved_setting != NULL) {
		assert(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "RESTUNTS_RENDER_WORKERS",
										  saved_setting, true));
		SDL_free(saved_setting);
	} else {
		assert(SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "RESTUNTS_RENDER_WORKERS"));
	}
	target.sprite_raster_left = target.sprite_top = 0;
	target.sprite_raster_right = 320;
	target.sprite_bottom = 200;
}

#define GROUND_DETAIL_CAMERA_HEIGHT 16
#define GROUND_DETAIL_FOCAL_LENGTH 160
#define GROUND_DETAIL_FOOT_OFFSET 1.2
#define GROUND_DETAIL_FAR_OFFSET 1.3
#define GROUND_DETAIL_TOP_OFFSET -4.0
#define GROUND_DETAIL_HALF_LENGTH 8.0
#define GROUND_DETAIL_SUPPORT_MARGIN 2.0
#define GROUND_DETAIL_SAMPLE_RADIUS 2
#define GROUND_DETAIL_STROKE_SCALE 32.0
#define GROUND_DETAIL_BACKGROUND_COLOR 3
#define GROUND_DETAIL_SUPPORT_COLOR 7
#define GROUND_DETAIL_COLOR 8

enum GROUND_DETAIL_PLANE { GROUND_DETAIL_NO_PLANE, GROUND_DETAIL_PLANE, GROUND_DETAIL_RESET };

/* Rotating the camera by a quarter turn makes the ground boundary vertical.
 * The same fixture then verifies that clipping follows the camera plane. */
static struct SHAPE3D_HIRES_VECTOR ground_detail_vertex(legacy_f64 along, legacy_f64 down,
														legacy_f64 depth, legacy_s32 rolled,
														legacy_s32 scale)
{
	legacy_f64 focal = GROUND_DETAIL_FOCAL_LENGTH * scale;
	struct SHAPE3D_HIRES_VECTOR result = {(rolled ? down : along) * depth / focal,
										  -(rolled ? along : down) * depth / focal, depth};
	return result;
}

static void draw_ground_detail(legacy_s32 scale, legacy_s32 rolled, legacy_u8 type,
							   legacy_s32 supported, legacy_s32 plane, legacy_s32 batched)
{
	reset_target();
	hires_end();
	hires_set_render_scale(scale);
	assert(hires_begin(&target));
	memset(&mat_temp, 0, sizeof(mat_temp));
	mat_temp.m._33 = TRIG_FIXED_ONE;
	if (rolled) {
		mat_temp.m._12 = -TRIG_FIXED_ONE;
		mat_temp.m._21 = TRIG_FIXED_ONE;
	} else {
		mat_temp.m._11 = mat_temp.m._22 = TRIG_FIXED_ONE;
	}
	const struct VECTOR camera = {0, GROUND_DETAIL_CAMERA_HEIGHT, 0};
	if (plane != GROUND_DETAIL_NO_PLANE) {
		shape3d_hires_ground_begin(&camera);
	}
	if (plane == GROUND_DETAIL_RESET) {
		shape3d_hires_reset();
	}
	legacy_f64 depth = GROUND_DETAIL_CAMERA_HEIGHT * GROUND_DETAIL_FOCAL_LENGTH * scale /
					   GROUND_DETAIL_FOOT_OFFSET;
	legacy_f64 near_depth =
		GROUND_DETAIL_CAMERA_HEIGHT * GROUND_DETAIL_FOCAL_LENGTH * scale / GROUND_DETAIL_FAR_OFFSET;
	const legacy_u8 indices[] = {0, 1, 2, 3};
	legacy_u32 index = 0;
	if (supported) {
		const struct SHAPE3D_HIRES_VECTOR support[] = {
			ground_detail_vertex(-GROUND_DETAIL_HALF_LENGTH, -GROUND_DETAIL_SUPPORT_MARGIN, depth,
								 rolled, scale),
			ground_detail_vertex(GROUND_DETAIL_HALF_LENGTH, -GROUND_DETAIL_SUPPORT_MARGIN, depth,
								 rolled, scale),
			ground_detail_vertex(GROUND_DETAIL_HALF_LENGTH, GROUND_DETAIL_SUPPORT_MARGIN * 2, depth,
								 rolled, scale),
			ground_detail_vertex(-GROUND_DETAIL_HALF_LENGTH, GROUND_DETAIL_SUPPORT_MARGIN * 2,
								 depth, rolled, scale)};
		shape3d_hires_queue(index++, RENDER_PRIMITIVE_POLYGON, SDL_arraysize(support), indices,
							support, 0);
	}
	struct SHAPE3D_HIRES_VECTOR detail[4];
	legacy_u16 count;
	if (type == RENDER_PRIMITIVE_POLYGON) {
		detail[0] = ground_detail_vertex(-GROUND_DETAIL_HALF_LENGTH, GROUND_DETAIL_FOOT_OFFSET,
										 depth, rolled, scale);
		detail[1] = ground_detail_vertex(GROUND_DETAIL_HALF_LENGTH, GROUND_DETAIL_FOOT_OFFSET,
										 depth, rolled, scale);
		detail[2] = ground_detail_vertex(GROUND_DETAIL_HALF_LENGTH, GROUND_DETAIL_FAR_OFFSET,
										 near_depth, rolled, scale);
		detail[3] = ground_detail_vertex(-GROUND_DETAIL_HALF_LENGTH, GROUND_DETAIL_FAR_OFFSET,
										 near_depth, rolled, scale);
		count = SDL_arraysize(detail);
	} else {
		detail[0] = ground_detail_vertex(0, GROUND_DETAIL_FOOT_OFFSET, depth, rolled, scale);
		detail[1] = ground_detail_vertex(0, GROUND_DETAIL_TOP_OFFSET, depth, rolled, scale);
		count = type == RENDER_PRIMITIVE_POINT ? 1 : 2;
		/* Exercise widened strokes as well as their continuous pixel spine. */
		shape3d_hires_set_model_scale(GROUND_DETAIL_STROKE_SCALE);
	}
	shape3d_hires_queue(index, type, count, indices, detail,
						SHAPE3D_PRIMITIVE_SKIP_DEPTH_SORT_FLAG);
	shape3d_hires_set_model_scale(1);
	if (batched) {
		shape3d_hires_batch_begin();
	}
	if (supported) {
		shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, GROUND_DETAIL_SUPPORT_COLOR, 0, 0, 0, 0);
	}
	shape3d_hires_render(index, type, GROUND_DETAIL_COLOR, 0, 0, 0, 0);
	if (batched) {
		shape3d_hires_batch_end();
	}
	hires_end();
}

/* Quad strokes can cover either side of an integer centerline. Select an
 * actually covered sample below the footprint before testing its occlusion. */
static legacy_s32 ground_detail_sample(const legacy_u8 *image, legacy_s32 width, legacy_s32 rolled,
									   legacy_s32 *x, legacy_s32 *y)
{
	for (legacy_s32 offset = -GROUND_DETAIL_SAMPLE_RADIUS; offset <= GROUND_DETAIL_SAMPLE_RADIUS;
		 offset++) {
		legacy_s32 sample_x = *x + (rolled ? 0 : offset);
		legacy_s32 sample_y = *y + (rolled ? offset : 0);
		if (image[sample_y * width + sample_x] == GROUND_DETAIL_COLOR) {
			*x = sample_x;
			*y = sample_y;
			return 1;
		}
	}
	return 0;
}

/* Generated detail can reach a pixel below a model's ground-level footprint.
 * Grass has no stored depth family there, but an existing surface must retain
 * attached paint and ordinary polygon fills must remain unchanged. */
static void test_ground_occludes_generated_detail(void)
{
	static legacy_u8 reference[HIRES_WIDTH * HIRES_HEIGHT];
	static legacy_u8 without_plane[HIRES_WIDTH * HIRES_HEIGHT];
	const legacy_u8 types[] = {RENDER_PRIMITIVE_LINE, RENDER_PRIMITIVE_POINT,
							   RENDER_PRIMITIVE_POLYGON};
	const struct MATRIX saved_view = mat_temp;
	projection_focal_length_x = projection_focal_length_y = GROUND_DETAIL_FOCAL_LENGTH;
	for (legacy_s32 scale = HIRES_MINIMUM_SCALE; scale <= HIRES_SCALE; scale *= 2) {
		for (legacy_s32 rolled = 0; rolled < 2; rolled++) {
			for (legacy_u32 kind = 0; kind < SDL_arraysize(types); kind++) {
				for (legacy_s32 supported = 0; supported < 2; supported++) {
					for (legacy_s32 plane = GROUND_DETAIL_NO_PLANE; plane <= GROUND_DETAIL_RESET;
						 plane++) {
						for (legacy_s32 batched = 0; batched < 2; batched++) {
							draw_ground_detail(scale, rolled, types[kind], supported, plane,
											   batched);
							const legacy_u8 *image = pixels();
							legacy_s32 width = hires_render_width();
							legacy_s32 center_x = projection_center_x * scale;
							legacy_s32 center_y = projection_center_y * scale;
							legacy_s32 foot = (legacy_s32)GROUND_DETAIL_FOOT_OFFSET;
							legacy_s32 x = center_x + (rolled ? foot : 0);
							legacy_s32 y = center_y + (rolled ? 0 : foot);
							const legacy_u8 *sample_image =
								plane == GROUND_DETAIL_NO_PLANE ? image : without_plane;
							assert(ground_detail_sample(sample_image, width, rolled, &x, &y));
							legacy_u8 expected = !supported && plane == GROUND_DETAIL_PLANE
													 ? GROUND_DETAIL_BACKGROUND_COLOR
													 : GROUND_DETAIL_COLOR;
							assert(image[y * width + x] == expected);
							if (supported) {
								/* This core fill lies behind ground but has no added coverage. */
								legacy_s32 interior =
									(legacy_s32)(GROUND_DETAIL_SUPPORT_MARGIN + 1);
								x = center_x + (rolled ? interior : 0);
								y = center_y + (rolled ? 0 : interior);
								assert(image[y * width + x] == GROUND_DETAIL_SUPPORT_COLOR);
							}
							size_t size = (size_t)width * hires_render_height();
							if (!batched) {
								memcpy(reference, image, size);
								if (plane == GROUND_DETAIL_NO_PLANE) {
									memcpy(without_plane, image, size);
								} else if (plane == GROUND_DETAIL_RESET) {
									assert(memcmp(without_plane, image, size) == 0);
								}
							} else {
								assert(memcmp(reference, image, size) == 0);
							}
						}
					}
				}
			}
		}
	}
	mat_temp = saved_view;
	hires_set_render_scale(HIRES_SCALE);
}

#define SHADOW_TEST_OFFSET_LIMIT_SCALE 0.7
#define SHADOW_TEST_PIXEL_TOLERANCE 0.5
#define SHADOW_TEST_DIRECTION_TOLERANCE 1.0
#define SHADOW_TEST_LOW_LIGHT_COTANGENT 1.732051
#define SHADOW_TEST_DIAGONAL_COMPONENT 0.707107

struct SHADOW_TEST_IMAGE {
	const legacy_u32 *pixels;
	legacy_f64 centroid_x, centroid_z;
	legacy_f64 half_width, half_length;
	legacy_f64 light_x, light_z;
};
static struct SHADOW_TEST_IMAGE shadow_image;

/* A camera directly above the car makes the world footprint measurable
 * without depending on the shadow implementation's projection helpers. */
static void begin_shadow_scene_at_scale(legacy_s16 car_height, legacy_s16 heading, legacy_s32 scale)
{
	const struct VECTOR camera = {0, 400, 0};
	const struct VECTOR car = {0, (legacy_s16)(car_height - camera.y), 0};
	reset_target();
	if (scale != HIRES_SCALE) {
		hires_end();
		hires_set_render_scale(scale);
		assert(hires_begin(&target));
	}
	memset(&mat_temp, 0, sizeof(mat_temp));
	mat_temp.m._11 = 16384;
	mat_temp.m._23 = 16384;
	mat_temp.m._32 = -16384;
	projection_center_x = 160;
	projection_center_y = 100;
	projection_focal_length_x = projection_focal_length_y = 160;
	hires_depth_begin(0, hires_render_width(), 0, hires_render_height());
	shape3d_hires_shadows_begin(&camera);
	shape3d_hires_shadow_car(&car, heading, 40, 75);
	shadow_image.half_width = 40;
	shadow_image.half_length = 75;
	shadow_image.light_x = 0;
	shadow_image.light_z = 1;
}

static void begin_shadow_scene(legacy_s16 car_height, legacy_s16 heading)
{
	begin_shadow_scene_at_scale(car_height, heading, HIRES_SCALE);
}

static void queue_shadow_receiver(legacy_u32 index, legacy_s16 height, legacy_s32 receiver,
								  legacy_f64 right)
{
	const legacy_u8 indices[] = {0, 1, 2, 3};
	const struct SHAPE3D_HIRES_VECTOR plane[] = {{-150, -150, 400 - height},
												 {right, -150, 400 - height},
												 {right, 150, 400 - height},
												 {-150, 150, 400 - height}};
	shape3d_hires_begin_shape(index, SHAPE3D_HIRES_DEPTH_SORTED);
	shape3d_hires_set_shadow_receiver(receiver);
	shape3d_hires_queue(index, RENDER_PRIMITIVE_POLYGON, 4, indices, plane, 0);
}

static legacy_u32 finish_shadow_scene(legacy_s16 receiver_height, legacy_s16 heading,
									  legacy_u8 excluded_color)
{
	static legacy_u8 indexed[HIRES_WIDTH * HIRES_HEIGHT];
	legacy_s32 width = hires_render_width();
	legacy_s32 height = hires_render_height();
	legacy_s32 scale = hires_render_scale();
	legacy_u32 palette[256];
	for (legacy_u32 index = 0; index < 256; index++) {
		palette[index] = 0xFFC0C0C0U;
	}
	palette[15] = 0xFFFFFFFFU;
	memcpy(indexed, pixels(), (size_t)width * height);
	shape3d_hires_draw_shadows();
	hires_end();
	assert(memcmp(indexed, pixels(), (size_t)width * height) == 0);
	const legacy_u32 *image = hires_framebuffer_argb(screen, palette);
	legacy_u32 changed = 0;
	shadow_image.pixels = image;
	shadow_image.centroid_x = shadow_image.centroid_z = 0;
	legacy_f64 weight_total = 0;
	for (legacy_s32 y = 0; image != NULL && y < height; y++) {
		for (legacy_s32 x = 0; x < width; x++) {
			legacy_u32 offset = y * width + x;
			if (image[offset] == palette[indexed[offset]]) {
				continue;
			}
			assert(indexed[offset] != excluded_color);
			assert((image[offset] & 0xFFFFFFU) < (palette[indexed[offset]] & 0xFFFFFFU));
			/* The light can extend the footprint only along its world direction,
			 * independently of the rotating car's silhouette. */
			legacy_f64 world_x = (x + 0.5 - projection_center_x * scale) * (400 - receiver_height) /
								 (projection_focal_length_x * scale);
			legacy_f64 world_z = (projection_center_y * scale - y - 0.5) * (400 - receiver_height) /
								 (projection_focal_length_y * scale);
			legacy_f64 half_x =
				(heading & 256) == 0 ? shadow_image.half_width : shadow_image.half_length;
			legacy_f64 half_z =
				(heading & 256) == 0 ? shadow_image.half_length : shadow_image.half_width;
			legacy_f64 extension = SDL_min(half_x, half_z) * SHADOW_TEST_OFFSET_LIMIT_SCALE;
			legacy_f64 extend_x = extension * shadow_image.light_x;
			legacy_f64 extend_z = extension * shadow_image.light_z;
			assert(world_x >= -half_x + SDL_min(0, extend_x) - SHADOW_TEST_PIXEL_TOLERANCE &&
				   world_x <= half_x + SDL_max(0, extend_x) + SHADOW_TEST_PIXEL_TOLERANCE);
			assert(world_z >= -half_z + SDL_min(0, extend_z) - SHADOW_TEST_PIXEL_TOLERANCE &&
				   world_z <= half_z + SDL_max(0, extend_z) + SHADOW_TEST_PIXEL_TOLERANCE);
			legacy_f64 weight = (palette[indexed[offset]] & 255U) - (image[offset] & 255U);
			shadow_image.centroid_x += world_x * weight;
			shadow_image.centroid_z += world_z * weight;
			weight_total += weight;
			changed++;
		}
	}
	if (weight_total > 0) {
		shadow_image.centroid_x /= weight_total;
		shadow_image.centroid_z /= weight_total;
	}
	return changed;
}

static void test_car_shadow_stays_below_and_shrinks(void)
{
	const legacy_s16 heights[] = {0, 48, 128};
	for (legacy_s16 heading = 0; heading <= 256; heading += 256) {
		legacy_u32 previous = HIRES_WIDTH * HIRES_HEIGHT;
		for (legacy_u32 height = 0; height < sizeof(heights) / sizeof(heights[0]); height++) {
			begin_shadow_scene(heights[height], heading);
			queue_shadow_receiver(0, 0, 1, 150);
			shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 8, 0, 0, 0, 0);
			legacy_u32 coverage = finish_shadow_scene(0, heading, 0);
			assert(coverage > 100);
			assert(coverage < previous);
			previous = coverage;
		}
	}
}

/* A narrow open-wheel body, four cylinders and four suspension lines leave
 * deliberate gaps that a generic rounded rectangle cannot reproduce. */
static struct SHAPE3D shadow_model;
static legacy_u8 shadow_model_vertices[36 * SHAPE3D_VERTEX_SIZE];
static legacy_u8 shadow_model_primitives[7 + 4 * 9 + 4 * 5 + 2];

static void prepare_shadow_model(legacy_s16 scale)
{
	struct VECTOR vertices[36] = {{-12, 24, -60}, {12, 24, -60}, {12, 24, 60}, {-12, 24, 60}};
	memset(&shadow_model, 0, sizeof(shadow_model));
	shadow_model.shape3d_numverts = 36;
	shadow_model.shape3d_numprimitives = 9;
	shadow_model.shape3d_numpaints = 1;
	shadow_model.shape3d_vertex_bytes = shadow_model_vertices;
	shadow_model.shape3d_primitives = shadow_model_primitives;
	const legacy_u8 body[] = {4, 0, 0, 0, 1, 2, 3};
	memcpy(shadow_model_primitives, body, sizeof(body));
	legacy_u32 offset = sizeof(body);
	for (legacy_u32 wheel = 0; wheel < 4; wheel++) {
		legacy_s16 side = (wheel & 1) != 0 ? 1 : -1;
		legacy_s16 z = wheel < 2 ? -43 : 43;
		legacy_u8 base = (legacy_u8)(4 + wheel * 6);
		vertices[base] = (struct VECTOR){(legacy_s16)(side * 28), 12, z};
		vertices[base + 1] = (struct VECTOR){(legacy_s16)(side * 28), 12, (legacy_s16)(z + 12)};
		vertices[base + 2] = (struct VECTOR){(legacy_s16)(side * 28), 24, z};
		vertices[base + 3] = (struct VECTOR){(legacy_s16)(side * 40), 12, z};
		vertices[base + 4] = (struct VECTOR){(legacy_s16)(side * 40), 12, (legacy_s16)(z + 12)};
		vertices[base + 5] = (struct VECTOR){(legacy_s16)(side * 40), 24, z};
		shadow_model_primitives[offset++] = 12;
		shadow_model_primitives[offset++] = 0;
		shadow_model_primitives[offset++] = 0;
		for (legacy_u32 vertex = 0; vertex < 6; vertex++) {
			shadow_model_primitives[offset++] = (legacy_u8)(base + vertex);
		}
		base = (legacy_u8)(28 + wheel * 2);
		vertices[base] =
			(struct VECTOR){(legacy_s16)(side * 10), 12, (legacy_s16)(z < 0 ? -37 : 37)};
		vertices[base + 1] = (struct VECTOR){(legacy_s16)(side * 28), 12, z};
	}
	for (legacy_u32 wheel = 0; wheel < 4; wheel++) {
		shadow_model_primitives[offset++] = 2;
		shadow_model_primitives[offset++] = 0;
		shadow_model_primitives[offset++] = 0;
		shadow_model_primitives[offset++] = (legacy_u8)(28 + wheel * 2);
		shadow_model_primitives[offset++] = (legacy_u8)(29 + wheel * 2);
	}
	shadow_model_primitives[offset++] = 0;
	shadow_model_primitives[offset] = 0;
	for (legacy_u16 vertex = 0; vertex < 36; vertex++) {
		vertices[vertex].x *= scale;
		vertices[vertex].y *= scale;
		vertices[vertex].z *= scale;
		shape3d_vertex_write(&shadow_model, vertex, &vertices[vertex]);
	}
}

static legacy_u32 render_shadow_model(legacy_s16 heading, legacy_s16 scale)
{
	begin_shadow_scene(0, heading);
	shape3d_hires_shadow_model(&shadow_model);
	shadow_image.half_width = 40 * scale;
	shadow_image.half_length = 60 * scale;
	queue_shadow_receiver(0, 0, 1, 150);
	shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 8, 0, 0, 0, 0);
	return finish_shadow_scene(0, heading, 0);
}

static legacy_u32 shadow_samples_near(legacy_f64 world_x, legacy_f64 world_z)
{
	legacy_s32 center_x = (legacy_s32)(640 + world_x * 1.6);
	legacy_s32 center_y = (legacy_s32)(400 - world_z * 1.6);
	legacy_u32 changed = 0;
	for (legacy_s32 y = center_y - 1; y <= center_y + 1; y++) {
		for (legacy_s32 x = center_x - 1; x <= center_x + 1; x++) {
			changed += shadow_image.pixels[y * HIRES_WIDTH + x] != 0xFFC0C0C0U;
		}
	}
	return changed;
}

static void test_car_shadow_model_details_and_north_light(void)
{
	prepare_shadow_model(1);
	shape3d_hires_shadow_models_reset();
	for (legacy_s16 heading = 0; heading < 1024; heading += 256) {
		assert(render_shadow_model(heading, 1) > 100);
		/* Car heading changes the silhouette, never the sun's direction. */
		assert(shadow_image.centroid_x > -0.5 && shadow_image.centroid_x < 0.5);
		assert(shadow_image.centroid_z > 3 && shadow_image.centroid_z < 20);
	}
	assert(render_shadow_model(0, 1) > 100);
	legacy_f64 north = shadow_image.centroid_z;
	assert(shadow_samples_near(0, north) == 9);
	for (legacy_s16 side = -1; side <= 1; side += 2) {
		for (legacy_s16 end = -1; end <= 1; end += 2) {
			assert(shadow_samples_near(side * 34, end * 43 + north) == 9);
			assert(shadow_samples_near(side * 20, end * 40 + north) > 0);
			assert(shadow_samples_near(side * 20, end * 20 + north) == 0);
		}
	}
}

static void test_car_shadow_model_size_and_cache_reset(void)
{
	prepare_shadow_model(1);
	shape3d_hires_shadow_models_reset();
	legacy_u32 small = render_shadow_model(0, 1);
	/* Reloading a different car may reuse both the shape and vertex addresses.
	 * Its geometry must supersede the cached mask after resource invalidation. */
	prepare_shadow_model(2);
	shape3d_hires_shadow_models_reset();
	legacy_u32 large = render_shadow_model(0, 2);
	assert(large > small * 3.6 && large < small * 4.4);
	assert(shadow_samples_near(68, 86 + shadow_image.centroid_z) == 9);
	shape3d_hires_shadow_models_reset();
}

static void test_car_shadow_receiver_height(void)
{
	/* Elevated track under a car receives a shadow; a roof above it does
	 * not. Nor should a car high in the air darken remote terrain. */
	const legacy_s16 car_heights[] = {96, 24, 300};
	const legacy_s16 surface_heights[] = {64, 64, 0};
	for (legacy_u32 scene = 0; scene < 3; scene++) {
		begin_shadow_scene(car_heights[scene], 0);
		queue_shadow_receiver(0, surface_heights[scene], 1, 150);
		shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 8, 0, 0, 0, 0);
		legacy_u32 coverage = finish_shadow_scene(surface_heights[scene], 0, 0);
		assert(scene == 0 ? coverage > 100 : coverage == 0);
	}
}

static void test_car_shadow_excludes_car_geometry(void)
{
	begin_shadow_scene(24, 0);
	/* An excluded car panel covers the left half of the footprint, even
	 * though it lies below the caster. The next shape is ordinary road. */
	queue_shadow_receiver(0, 8, 0, 0);
	const struct SHAPE3D_HIRES_VECTOR road[] = {
		{-150, -150, 400}, {150, -150, 400}, {150, 150, 400}, {-150, 150, 400}};
	queue_polygon_shape(1, SHAPE3D_HIRES_DEPTH_SORTED, road);
	shape3d_hires_render(1, RENDER_PRIMITIVE_POLYGON, 8, 0, 0, 0, 0);
	shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 9, 0, 0, 0, 0);
	assert(pixels()[400 * HIRES_WIDTH + 620] == 9);
	assert(pixels()[400 * HIRES_WIDTH + 660] == 8);
	assert(finish_shadow_scene(0, 0, 9) > 100);
}

static void test_car_shadow_ground_fallback_and_reset(void)
{
	/* The ordinary flat ground has no queued depth surface. */
	begin_shadow_scene(24, 0);
	assert(finish_shadow_scene(0, 0, 0) > 100);
	begin_shadow_scene(24, 0);
	shape3d_hires_reset();
	assert(finish_shadow_scene(0, 0, 0) == 0);

	begin_shadow_scene(24, 0);
	hires_set_enabled(0);
	shape3d_hires_draw_shadows();
	hires_end();
	for (legacy_u32 index = 0; index < 320 * 200; index++) {
		assert(screen[index] == 3);
	}
	legacy_u32 palette[256] = {0};
	assert(hires_framebuffer_argb(screen, palette) == NULL);
	hires_set_enabled(1);
}

/* Collecting an occluder without drawing it models a road hidden from the
 * camera: its shadow visibility must not depend on camera depth coverage. */
enum SHADOW_OCCLUSION_TEST_CONSTANTS {
	SHADOW_TEST_CAMERA_HEIGHT = 400,
	SHADOW_TEST_CAR_HEIGHT = 128,
	SHADOW_TEST_ROAD_HEIGHT = 64,
	SHADOW_TEST_HALF_SURFACE = 150,
	SHADOW_TEST_SAMPLE_X = 8,
	SHADOW_TEST_SAMPLE_Z = 24,
	SHADOW_TEST_BASE_CHANNEL = 192,
	SHADOW_TEST_WHITE_INDEX = 15,
	SHADOW_TEST_SURFACE_COLOR = 8,
	SHADOW_TEST_CAR_COLOR = 9,
	SHADOW_TEST_QUAD_VERTICES = 4,
	SHADOW_TEST_CHANNEL_ROUNDING = 1,
	SHADOW_TEST_MASKED_MATERIAL = 1,
	SHADOW_TEST_ORDERED_FLAG = 1,
	SHADOW_TEST_DOUBLE_SIDED_FLAG = 1
};

#define SHADOW_TEST_BASE_COLOR 0xFFC0C0C0U

static const legacy_u32 *shadow_test_argb(void)
{
	legacy_u32 palette[LEGACY_U8_MAX + 1];
	for (legacy_u32 index = 0; index < SDL_arraysize(palette); index++) {
		palette[index] = SHADOW_TEST_BASE_COLOR;
	}
	palette[SHADOW_TEST_WHITE_INDEX] = LEGACY_U32_MAX;
	return hires_framebuffer_argb(screen, palette);
}

static void collect_shadow_road(legacy_s16 left_height, legacy_s16 right_height, legacy_f64 right,
								legacy_s32 grille)
{
	const struct SHAPE3D_HIRES_VECTOR road[] = {
		{-SHADOW_TEST_HALF_SURFACE, -SHADOW_TEST_HALF_SURFACE,
		 SHADOW_TEST_CAMERA_HEIGHT - left_height},
		{right, -SHADOW_TEST_HALF_SURFACE, SHADOW_TEST_CAMERA_HEIGHT - right_height},
		{right, SHADOW_TEST_HALF_SURFACE, SHADOW_TEST_CAMERA_HEIGHT - right_height},
		{-SHADOW_TEST_HALF_SURFACE, SHADOW_TEST_HALF_SURFACE,
		 SHADOW_TEST_CAMERA_HEIGHT - left_height}};
	shape3d_hires_shadow_polygon(road, SDL_arraysize(road), grille);
}

static legacy_s32 shadow_darkening_at(legacy_f64 world_x, legacy_f64 world_z, legacy_s16 height)
{
	if (shadow_image.pixels == NULL) {
		return 0;
	}
	legacy_f64 focal = projection_focal_length_x * HIRES_SCALE;
	legacy_f64 depth = SHADOW_TEST_CAMERA_HEIGHT - height;
	legacy_s32 x = (legacy_s32)(projection_center_x * HIRES_SCALE + world_x * focal / depth);
	legacy_s32 y = (legacy_s32)(projection_center_y * HIRES_SCALE - world_z * focal / depth);
	return SHADOW_TEST_BASE_CHANNEL - (shadow_image.pixels[y * HIRES_WIDTH + x] & LEGACY_U8_MAX);
}

static void assert_shadow_fraction(legacy_s32 full, legacy_s32 partial, legacy_s32 denominator)
{
	assert(full > 0 && partial > 0);
	legacy_s32 difference = full - partial * denominator;
	legacy_s32 tolerance = SHADOW_TEST_CHANNEL_ROUNDING * denominator;
	assert(difference >= -tolerance && difference <= tolerance);
}

enum SHADOW_LIGHT_TEST_CONSTANTS {
	SHADOW_LIGHT_GROUND_HEIGHT = 0,
	SHADOW_LIGHT_LOW_CAR_HEIGHT = 24,
	SHADOW_LIGHT_HALF_WIDTH = 40,
	SHADOW_LIGHT_HALF_LENGTH = 75,
	SHADOW_LIGHT_MODEL_HALF_LENGTH = 60,
	SHADOW_LIGHT_FALLBACK = 0,
	SHADOW_LIGHT_MODEL
};

struct SHADOW_LIGHT_TEST_DIRECTION {
	legacy_s16 heading;
	legacy_f64 x, z;
};

static const struct SHADOW_LIGHT_TEST_DIRECTION shadow_light_directions[] = {
	{ANGLE_QUARTER_TURN, 1, 0},
	{-ANGLE_QUARTER_TURN, -1, 0},
	{ANGLE_HALF_TURN, 0, -1},
	{ANGLE_EIGHTH_TURN, SHADOW_TEST_DIAGONAL_COMPONENT, SHADOW_TEST_DIAGONAL_COMPONENT},
	{-ANGLE_EIGHTH_TURN, -SHADOW_TEST_DIAGONAL_COMPONENT, SHADOW_TEST_DIAGONAL_COMPONENT},
	{ANGLE_HALF_TURN + ANGLE_EIGHTH_TURN, -SHADOW_TEST_DIAGONAL_COMPONENT,
	 -SHADOW_TEST_DIAGONAL_COMPONENT}};

static void begin_directed_shadow_scene(legacy_s16 height, legacy_s16 heading,
										const struct SHADOW_LIGHT_TEST_DIRECTION *light,
										legacy_s32 model)
{
	const struct VECTOR camera = {0, SHADOW_TEST_CAMERA_HEIGHT, 0};
	const struct VECTOR car = {0, (legacy_s16)(height - camera.y), 0};
	begin_shadow_scene(height, heading);
	shape3d_hires_shadows_begin(&camera);
	shape3d_hires_set_shadow_light(light->heading, SHADOW_TEST_LOW_LIGHT_COTANGENT);
	shape3d_hires_shadow_car(&car, heading, SHADOW_LIGHT_HALF_WIDTH, SHADOW_LIGHT_HALF_LENGTH);
	shadow_image.light_x = light->x;
	shadow_image.light_z = light->z;
	if (model == SHADOW_LIGHT_MODEL) {
		shape3d_hires_shadow_model(&shadow_model);
		shadow_image.half_length = SHADOW_LIGHT_MODEL_HALF_LENGTH;
	}
}

static void test_car_shadow_light_direction_and_reset(void)
{
	prepare_shadow_model(1);
	shape3d_hires_shadow_models_reset();
	begin_shadow_scene(SHADOW_LIGHT_GROUND_HEIGHT, 0);
	legacy_u32 default_coverage = finish_shadow_scene(SHADOW_LIGHT_GROUND_HEIGHT, 0, 0);
	legacy_f64 default_x = shadow_image.centroid_x;
	legacy_f64 default_z = shadow_image.centroid_z;
	const legacy_s16 heights[] = {SHADOW_LIGHT_GROUND_HEIGHT, SHADOW_LIGHT_LOW_CAR_HEIGHT};
	for (legacy_s32 model = SHADOW_LIGHT_FALLBACK; model <= SHADOW_LIGHT_MODEL; model++) {
		for (legacy_u32 direction = 0; direction < SDL_arraysize(shadow_light_directions);
			 direction++) {
			const struct SHADOW_LIGHT_TEST_DIRECTION *light = &shadow_light_directions[direction];
			for (legacy_s16 heading = 0; heading < ANGLE_FULL_TURN; heading += ANGLE_QUARTER_TURN) {
				for (legacy_u32 height = 0; height < SDL_arraysize(heights); height++) {
					begin_directed_shadow_scene(heights[height], heading, light, model);
					assert(finish_shadow_scene(SHADOW_LIGHT_GROUND_HEIGHT, heading, 0) > 0);
					legacy_f64 along =
						shadow_image.centroid_x * light->x + shadow_image.centroid_z * light->z;
					legacy_f64 across =
						shadow_image.centroid_x * light->z - shadow_image.centroid_z * light->x;
					assert(along > default_z);
					assert(across > -SHADOW_TEST_DIRECTION_TOLERANCE &&
						   across < SHADOW_TEST_DIRECTION_TOLERANCE);
				}
			}
		}
	}
	/* Entering a driving scene restores both the southern sun direction and
	 * its steep elevation, even after a low showroom light was selected. */
	begin_shadow_scene(SHADOW_LIGHT_GROUND_HEIGHT, 0);
	assert(finish_shadow_scene(SHADOW_LIGHT_GROUND_HEIGHT, 0, 0) == default_coverage);
	assert(shadow_image.centroid_x == default_x && shadow_image.centroid_z == default_z);
	shape3d_hires_shadow_models_reset();
}

static void test_car_shadow_directed_light_occlusion(void)
{
	/* Exercise positive and negative displacement through the general plane
	 * intersection path, including ramps and half-strength grille surfaces. */
	for (legacy_u32 direction = 0; direction < SDL_arraysize(shadow_light_directions);
		 direction++) {
		const struct SHADOW_LIGHT_TEST_DIRECTION *light = &shadow_light_directions[direction];
		begin_directed_shadow_scene(SHADOW_TEST_CAR_HEIGHT, 0, light, SHADOW_LIGHT_FALLBACK);
		assert(finish_shadow_scene(SHADOW_LIGHT_GROUND_HEIGHT, 0, 0) > 0);
		legacy_f64 sample_x = shadow_image.centroid_x;
		legacy_f64 sample_z = shadow_image.centroid_z;
		legacy_s32 full = shadow_darkening_at(sample_x, sample_z, SHADOW_LIGHT_GROUND_HEIGHT);
		assert(full > 0);
		for (legacy_s32 grille = 0; grille <= 1; grille++) {
			begin_directed_shadow_scene(SHADOW_TEST_CAR_HEIGHT, 0, light, SHADOW_LIGHT_FALLBACK);
			collect_shadow_road(SHADOW_TEST_ROAD_HEIGHT / 2,
								SHADOW_TEST_ROAD_HEIGHT + SHADOW_TEST_ROAD_HEIGHT / 2,
								SHADOW_TEST_HALF_SURFACE, grille);
			legacy_u32 coverage = finish_shadow_scene(SHADOW_LIGHT_GROUND_HEIGHT, 0, 0);
			if (grille) {
				assert(coverage > 0);
				assert_shadow_fraction(
					full, shadow_darkening_at(sample_x, sample_z, SHADOW_LIGHT_GROUND_HEIGHT), 2);
			} else {
				assert(coverage == 0);
			}
		}
	}
}

enum PROJECTED_SHADOW_TEST_CONSTANTS {
	PROJECTED_SHADOW_PANEL_HALF_SIZE = 4,
	PROJECTED_SHADOW_ROOF_HEIGHT = 24,
	PROJECTED_SHADOW_MODEL_GROUND = 37,
	PROJECTED_SHADOW_LOWER_PLANE = -12,
	PROJECTED_SHADOW_QUAD_VERTICES = 4,
	PROJECTED_SHADOW_PANEL_COUNT = 2,
	PROJECTED_SHADOW_SAMPLE_RADIUS = 1,
	PROJECTED_SHADOW_WHEEL_CONTACT_X = 34,
	PROJECTED_SHADOW_WHEEL_CONTACT_Z = 43,
	PROJECTED_SHADOW_FOCAL_LENGTH = 160,
	PROJECTED_SHADOW_SPHERE_DIAMETER = 32,
	PROJECTED_SHADOW_SPHERE_CENTER_Y = 24,
	PROJECTED_SHADOW_SPHERE_OUTSIDE_SIDE = 24,
	PROJECTED_SHADOW_SPHERE_LOW_PLANE = -32,
	PROJECTED_SHADOW_SPHERE_EXTENDED_X = 132
};
#define PROJECTED_SHADOW_HASH_INITIAL UINT64_C(1469598103934665603)
#define PROJECTED_SHADOW_HASH_PRIME UINT64_C(1099511628211)

static struct SHAPE3D projected_shadow_model;
static legacy_u8 projected_shadow_vertices[PROJECTED_SHADOW_PANEL_COUNT *
										   PROJECTED_SHADOW_QUAD_VERTICES * SHAPE3D_VERTEX_SIZE];

static void prepare_projected_shadow_panels(legacy_s16 ground_height)
{
	static legacy_u8 primitives[] = {SHAPE3D_PRIMITIVE_POLYGON_FIRST + 1,
									 0,
									 0,
									 0,
									 1,
									 2,
									 3,
									 SHAPE3D_PRIMITIVE_POLYGON_FIRST + 1,
									 0,
									 0,
									 4,
									 5,
									 6,
									 7,
									 0,
									 0};
	const legacy_s16 corners[][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
	memset(&projected_shadow_model, 0, sizeof(projected_shadow_model));
	projected_shadow_model.shape3d_numverts =
		PROJECTED_SHADOW_PANEL_COUNT * PROJECTED_SHADOW_QUAD_VERTICES;
	projected_shadow_model.shape3d_numprimitives = PROJECTED_SHADOW_PANEL_COUNT;
	projected_shadow_model.shape3d_numpaints = 1;
	projected_shadow_model.shape3d_vertex_bytes = projected_shadow_vertices;
	projected_shadow_model.shape3d_primitives = primitives;
	for (legacy_u16 panel = 0; panel < PROJECTED_SHADOW_PANEL_COUNT; panel++) {
		for (legacy_u16 corner = 0; corner < SDL_arraysize(corners); corner++) {
			const struct VECTOR vertex = {corners[corner][0] * PROJECTED_SHADOW_PANEL_HALF_SIZE,
										  ground_height + panel * PROJECTED_SHADOW_ROOF_HEIGHT,
										  corners[corner][1] * PROJECTED_SHADOW_PANEL_HALF_SIZE};
			shape3d_vertex_write(&projected_shadow_model,
								 panel * PROJECTED_SHADOW_QUAD_VERTICES + corner, &vertex);
		}
	}
}

static void begin_projected_shadow_scene(legacy_s16 heading, legacy_s16 light_heading,
										 legacy_u16 focal_y)
{
	const struct VECTOR camera = {0, SHADOW_TEST_CAMERA_HEIGHT, 0};
	const struct VECTOR car = {0, -SHADOW_TEST_CAMERA_HEIGHT, 0};
	begin_shadow_scene(SHADOW_LIGHT_GROUND_HEIGHT, heading);
	projection_focal_length_y = focal_y;
	shape3d_hires_shadows_begin(&camera);
	shape3d_hires_set_shadow_light(light_heading, SHADOW_TEST_LOW_LIGHT_COTANGENT);
	shape3d_hires_shadow_car(&car, heading, SHADOW_LIGHT_HALF_WIDTH, SHADOW_LIGHT_HALF_LENGTH);
}

static legacy_u64 finish_projected_shadow_scene(void)
{
	shape3d_hires_draw_shadows();
	hires_end();
	shadow_image.pixels = shadow_test_argb();
	assert(shadow_image.pixels != NULL);
	legacy_u64 hash = PROJECTED_SHADOW_HASH_INITIAL;
	for (legacy_u32 pixel = 0; pixel < HIRES_WIDTH * HIRES_HEIGHT; pixel++) {
		hash = (hash ^ shadow_image.pixels[pixel]) * PROJECTED_SHADOW_HASH_PRIME;
	}
	return hash;
}

static legacy_u32 projected_shadow_samples_near(legacy_f64 world_x, legacy_f64 world_z)
{
	legacy_s32 center_x =
		(legacy_s32)(projection_center_x * HIRES_SCALE +
					 world_x * projection_focal_length_x * HIRES_SCALE / SHADOW_TEST_CAMERA_HEIGHT);
	legacy_s32 center_y =
		(legacy_s32)(projection_center_y * HIRES_SCALE -
					 world_z * projection_focal_length_y * HIRES_SCALE / SHADOW_TEST_CAMERA_HEIGHT);
	legacy_u32 count = 0;
	for (legacy_s32 y = center_y - PROJECTED_SHADOW_SAMPLE_RADIUS;
		 y <= center_y + PROJECTED_SHADOW_SAMPLE_RADIUS; y++) {
		for (legacy_s32 x = center_x - PROJECTED_SHADOW_SAMPLE_RADIUS;
			 x <= center_x + PROJECTED_SHADOW_SAMPLE_RADIUS; x++) {
			assert(x >= 0 && x < HIRES_WIDTH && y >= 0 && y < HIRES_HEIGHT);
			count += shadow_image.pixels[y * HIRES_WIDTH + x] != SHADOW_TEST_BASE_COLOR;
		}
	}
	return count;
}

static void test_projected_car_shadow_wheel_contacts(void)
{
	prepare_shadow_model(1);
	shape3d_hires_shadow_models_reset();
	for (legacy_s16 heading = 0; heading < ANGLE_FULL_TURN; heading += ANGLE_EIGHTH_TURN) {
		begin_projected_shadow_scene(heading, ANGLE_EIGHTH_TURN, PROJECTED_SHADOW_FOCAL_LENGTH);
		shape3d_hires_shadow_projected_model(&shadow_model,
											 shape3d_car_ground_height(&shadow_model));
		finish_projected_shadow_scene();
		legacy_f64 cosine = cos_fast(heading) / (legacy_f64)TRIG_FIXED_ONE;
		legacy_f64 sine = sin_fast(heading) / (legacy_f64)TRIG_FIXED_ONE;
		for (legacy_s16 side = -1; side <= 1; side += 2) {
			for (legacy_s16 end = -1; end <= 1; end += 2) {
				legacy_f64 x = side * PROJECTED_SHADOW_WHEEL_CONTACT_X;
				legacy_f64 z = end * PROJECTED_SHADOW_WHEEL_CONTACT_Z;
				assert(projected_shadow_samples_near(x * cosine + z * sine,
													 -x * sine + z * cosine) > 0);
			}
		}
	}
}

static void test_projected_car_shadow_height_and_cache(void)
{
	prepare_projected_shadow_panels(SHADOW_LIGHT_GROUND_HEIGHT);
	shape3d_hires_shadow_models_reset();
	begin_shadow_scene(SHADOW_LIGHT_GROUND_HEIGHT, 0);
	shape3d_hires_shadow_model(&projected_shadow_model);
	legacy_u64 racing = finish_projected_shadow_scene();
	legacy_u64 projected = 0;
	/* The ground panel remains attached while the identical roof panel moves
	 * along the fixed light direction, including when the car turns diagonally. */
	for (legacy_s16 heading = 0; heading < ANGLE_FULL_TURN; heading += ANGLE_EIGHTH_TURN) {
		for (legacy_u32 direction = 0; direction < SDL_arraysize(shadow_light_directions);
			 direction++) {
			const struct SHADOW_LIGHT_TEST_DIRECTION *light = &shadow_light_directions[direction];
			begin_projected_shadow_scene(heading, light->heading, PROJECTED_SHADOW_FOCAL_LENGTH);
			shape3d_hires_shadow_projected_model(&projected_shadow_model,
												 SHADOW_LIGHT_GROUND_HEIGHT);
			legacy_u64 image = finish_projected_shadow_scene();
			if (heading == 0 && direction == 0) {
				projected = image;
			}
			legacy_f64 distance = PROJECTED_SHADOW_ROOF_HEIGHT * SHADOW_TEST_LOW_LIGHT_COTANGENT;
			assert(projected_shadow_samples_near(0, 0) > 0);
			assert(projected_shadow_samples_near(distance * light->x, distance * light->z) > 0);
			assert(projected_shadow_samples_near(distance * light->x / 2,
												 distance * light->z / 2) == 0);
		}
	}
	/* Reusing the same shape and vertex pointers must not reuse a mask for
	 * another projection plane, light, car heading, or the racing renderer. */
	begin_projected_shadow_scene(0, ANGLE_QUARTER_TURN, PROJECTED_SHADOW_FOCAL_LENGTH);
	shape3d_hires_shadow_projected_model(&projected_shadow_model, PROJECTED_SHADOW_LOWER_PLANE);
	finish_projected_shadow_scene();
	assert(projected_shadow_samples_near(0, 0) == 0);
	assert(projected_shadow_samples_near(
			   -PROJECTED_SHADOW_LOWER_PLANE * SHADOW_TEST_LOW_LIGHT_COTANGENT, 0) > 0);
	begin_projected_shadow_scene(0, ANGLE_QUARTER_TURN, PROJECTED_SHADOW_FOCAL_LENGTH);
	shape3d_hires_shadow_projected_model(&projected_shadow_model, SHADOW_LIGHT_GROUND_HEIGHT);
	assert(finish_projected_shadow_scene() == projected);
	begin_shadow_scene(SHADOW_LIGHT_GROUND_HEIGHT, 0);
	shape3d_hires_shadow_model(&projected_shadow_model);
	assert(finish_projected_shadow_scene() == racing);
	/* Custom model origins can sit above or below the authored tire plane. */
	const legacy_s16 ground_heights[] = {PROJECTED_SHADOW_MODEL_GROUND,
										 -PROJECTED_SHADOW_MODEL_GROUND};
	for (legacy_u32 index = 0; index < SDL_arraysize(ground_heights); index++) {
		prepare_projected_shadow_panels(ground_heights[index]);
		shape3d_hires_shadow_models_reset();
		begin_projected_shadow_scene(0, ANGLE_QUARTER_TURN, PROJECTED_SHADOW_FOCAL_LENGTH);
		assert(shape3d_car_ground_height(&projected_shadow_model) == ground_heights[index]);
		shape3d_hires_shadow_projected_model(&projected_shadow_model, ground_heights[index]);
		assert(finish_projected_shadow_scene() == projected);
	}
}

static void test_projected_car_shadow_sphere_contacts(void)
{
	static legacy_u8 primitives[] = {SHAPE3D_PRIMITIVE_SPHERE, 0, 0, 0, 1, 0, 0};
	const struct VECTOR center = {0, PROJECTED_SHADOW_SPHERE_CENTER_Y, 0};
	const struct VECTOR control = {
		0, PROJECTED_SHADOW_SPHERE_CENTER_Y + PROJECTED_SHADOW_SPHERE_DIAMETER, 0};
	memset(&projected_shadow_model, 0, sizeof(projected_shadow_model));
	projected_shadow_model.shape3d_numverts = 2;
	projected_shadow_model.shape3d_numprimitives = 1;
	projected_shadow_model.shape3d_numpaints = 1;
	projected_shadow_model.shape3d_vertex_bytes = projected_shadow_vertices;
	projected_shadow_model.shape3d_primitives = primitives;
	shape3d_vertex_write(&projected_shadow_model, 0, &center);
	shape3d_vertex_write(&projected_shadow_model, 1, &control);
	shape3d_hires_shadow_models_reset();
	const legacy_u16 focal_lengths[] = {PROJECTED_SHADOW_FOCAL_LENGTH,
										PROJECTED_SHADOW_FOCAL_LENGTH / 2};
	for (legacy_u32 focal = 0; focal < SDL_arraysize(focal_lengths); focal++) {
		for (legacy_s16 heading = 0; heading < ANGLE_FULL_TURN; heading += ANGLE_EIGHTH_TURN) {
			begin_projected_shadow_scene(heading, ANGLE_QUARTER_TURN, focal_lengths[focal]);
			shape3d_hires_shadow_projected_model(
				&projected_shadow_model, shape3d_car_ground_height(&projected_shadow_model));
			finish_projected_shadow_scene();
			assert(projected_shadow_samples_near(0, 0) > 0);
			/* The control vertex describes a diameter; the transverse shadow
			 * must stay within the rendered wheel width at either aspect. */
			assert(projected_shadow_samples_near(0, PROJECTED_SHADOW_SPHERE_OUTSIDE_SIDE) == 0);
		}
	}
	/* Keep the shape, plane and local light identical while the sphere's
	 * rendered vertical aspect changes; the cached silhouette must rebuild. */
	for (legacy_u32 focal = 0; focal < SDL_arraysize(focal_lengths); focal++) {
		begin_projected_shadow_scene(0, ANGLE_QUARTER_TURN, focal_lengths[focal]);
		shape3d_hires_shadow_projected_model(&projected_shadow_model,
											 PROJECTED_SHADOW_SPHERE_LOW_PLANE);
		finish_projected_shadow_scene();
		assert((projected_shadow_samples_near(PROJECTED_SHADOW_SPHERE_EXTENDED_X, 0) > 0) ==
			   (focal != 0));
	}
	shape3d_hires_shadow_models_reset();
}

static void test_car_shadow_opaque_road_and_partial_edge(void)
{
	begin_shadow_scene(SHADOW_TEST_CAR_HEIGHT, 0);
	queue_shadow_receiver(0, 0, 1, SHADOW_TEST_HALF_SURFACE);
	shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, SHADOW_TEST_SURFACE_COLOR, 0, 0, 0, 0);
	legacy_u32 baseline = finish_shadow_scene(0, 0, 0);
	legacy_s32 right = shadow_darkening_at(SHADOW_TEST_SAMPLE_X, SHADOW_TEST_SAMPLE_Z, 0);
	assert(baseline > 0 && right > 0);
	for (legacy_s32 fallback = 0; fallback < 2; fallback++) {
		begin_shadow_scene(SHADOW_TEST_CAR_HEIGHT, 0);
		collect_shadow_road(SHADOW_TEST_ROAD_HEIGHT, SHADOW_TEST_ROAD_HEIGHT,
							SHADOW_TEST_HALF_SURFACE, 0);
		if (!fallback) {
			queue_shadow_receiver(0, 0, 1, SHADOW_TEST_HALF_SURFACE);
			shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, SHADOW_TEST_SURFACE_COLOR, 0, 0, 0,
								 0);
		}
		assert(finish_shadow_scene(0, 0, 0) == 0);
	}
	begin_shadow_scene(SHADOW_TEST_CAR_HEIGHT, 0);
	collect_shadow_road(SHADOW_TEST_ROAD_HEIGHT, SHADOW_TEST_ROAD_HEIGHT, 0, 0);
	queue_shadow_receiver(0, 0, 1, SHADOW_TEST_HALF_SURFACE);
	shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, SHADOW_TEST_SURFACE_COLOR, 0, 0, 0, 0);
	legacy_u32 partial = finish_shadow_scene(0, 0, 0);
	assert(partial > 0 && partial < baseline);
	assert(shadow_darkening_at(-SHADOW_TEST_SAMPLE_X, SHADOW_TEST_SAMPLE_Z, 0) == 0);
	assert(shadow_darkening_at(SHADOW_TEST_SAMPLE_X, SHADOW_TEST_SAMPLE_Z, 0) == right);
	/* A sloping deck blocks the entire footprint, independent of which
	 * triangle contains a particular sample. A roof above the car does not. */
	const legacy_s16 left_heights[] = {SHADOW_TEST_ROAD_HEIGHT / 2,
									   SHADOW_TEST_CAR_HEIGHT + SHADOW_TEST_ROAD_HEIGHT};
	const legacy_s16 right_heights[] = {SHADOW_TEST_ROAD_HEIGHT + SHADOW_TEST_ROAD_HEIGHT / 2,
										SHADOW_TEST_CAR_HEIGHT + SHADOW_TEST_ROAD_HEIGHT};
	for (legacy_u32 scene = 0; scene < SDL_arraysize(left_heights); scene++) {
		begin_shadow_scene(SHADOW_TEST_CAR_HEIGHT, 0);
		collect_shadow_road(left_heights[scene], right_heights[scene], SHADOW_TEST_HALF_SURFACE, 0);
		queue_shadow_receiver(0, 0, 1, SHADOW_TEST_HALF_SURFACE);
		shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, SHADOW_TEST_SURFACE_COLOR, 0, 0, 0, 0);
		assert(finish_shadow_scene(0, 0, 0) == (scene == 0 ? 0 : baseline));
	}
}

static void test_car_shadow_grille_transmission(void)
{
	legacy_s32 full[2];
	const legacy_s16 receiver_heights[] = {0, SHADOW_TEST_ROAD_HEIGHT};
	for (legacy_u32 receiver = 0; receiver < SDL_arraysize(receiver_heights); receiver++) {
		legacy_s16 height = receiver_heights[receiver];
		for (legacy_s32 grille = 0; grille < 2; grille++) {
			begin_shadow_scene(SHADOW_TEST_CAR_HEIGHT, 0);
			if (grille || height == SHADOW_TEST_ROAD_HEIGHT) {
				collect_shadow_road(SHADOW_TEST_ROAD_HEIGHT, SHADOW_TEST_ROAD_HEIGHT,
									SHADOW_TEST_HALF_SURFACE, grille);
			}
			queue_shadow_receiver(0, height, 1, SHADOW_TEST_HALF_SURFACE);
			shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, SHADOW_TEST_SURFACE_COLOR, 0, 0, 0,
								 0);
			assert(finish_shadow_scene(height, 0, 0) > 0);
			legacy_s32 sample =
				shadow_darkening_at(SHADOW_TEST_SAMPLE_X, SHADOW_TEST_SAMPLE_Z, height);
			if (!grille) {
				full[receiver] = sample;
			} else {
				assert_shadow_fraction(full[receiver], sample, 2);
			}
		}
	}
	/* Duplicate faces and triangles at the same height are one layer.
	 * A genuinely separate grille transmits half of the remaining shadow. */
	for (legacy_s32 layers = 1; layers <= 2; layers++) {
		begin_shadow_scene(SHADOW_TEST_CAR_HEIGHT, 0);
		for (legacy_s32 layer = 1; layer <= layers; layer++) {
			legacy_s16 height = (legacy_s16)(SHADOW_TEST_ROAD_HEIGHT / layer);
			collect_shadow_road(height, height, SHADOW_TEST_HALF_SURFACE, 1);
			collect_shadow_road(height, height, SHADOW_TEST_HALF_SURFACE, 1);
		}
		queue_shadow_receiver(0, 0, 1, SHADOW_TEST_HALF_SURFACE);
		shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, SHADOW_TEST_SURFACE_COLOR, 0, 0, 0, 0);
		assert(finish_shadow_scene(0, 0, 0) > 0);
		assert_shadow_fraction(full[0],
							   shadow_darkening_at(SHADOW_TEST_SAMPLE_X, SHADOW_TEST_SAMPLE_Z, 0),
							   1 << layers);
	}
	begin_shadow_scene(SHADOW_TEST_CAR_HEIGHT, 0);
	collect_shadow_road(SHADOW_TEST_ROAD_HEIGHT, SHADOW_TEST_ROAD_HEIGHT, SHADOW_TEST_HALF_SURFACE,
						1);
	queue_shadow_receiver(0, SHADOW_TEST_ROAD_HEIGHT / 2, 0, 0);
	queue_shadow_receiver(1, 0, 1, SHADOW_TEST_HALF_SURFACE);
	shape3d_hires_render(1, RENDER_PRIMITIVE_POLYGON, SHADOW_TEST_SURFACE_COLOR, 0, 0, 0, 0);
	shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, SHADOW_TEST_CAR_COLOR, 0, 0, 0, 0);
	assert(finish_shadow_scene(0, 0, SHADOW_TEST_CAR_COLOR) > 0);
	assert(shadow_darkening_at(-SHADOW_TEST_SAMPLE_X, SHADOW_TEST_SAMPLE_Z, 0) == 0);
	assert_shadow_fraction(full[0],
						   shadow_darkening_at(SHADOW_TEST_SAMPLE_X, SHADOW_TEST_SAMPLE_Z, 0), 2);
}

static void test_car_shadow_collects_camera_hidden_roads(void)
{
	const legacy_s16 relative_height = SHADOW_TEST_ROAD_HEIGHT - SHADOW_TEST_CAMERA_HEIGHT;
	const struct VECTOR road[] = {
		{-SHADOW_TEST_HALF_SURFACE, relative_height, -SHADOW_TEST_HALF_SURFACE},
		{SHADOW_TEST_HALF_SURFACE, relative_height, -SHADOW_TEST_HALF_SURFACE},
		{SHADOW_TEST_HALF_SURFACE, relative_height, SHADOW_TEST_HALF_SURFACE},
		{-SHADOW_TEST_HALF_SURFACE, relative_height, SHADOW_TEST_HALF_SURFACE}};
	const legacy_u8 primitive[] = {SHADOW_TEST_QUAD_VERTICES, 0, 0, 0, 1, 2, 3, 0, 0};
	legacy_s16 grille_patterns[SDL_arraysize(scene_patterns)] = {
		(legacy_s16)PRERENDER_BLACK_GRILLE_PATTERN};
	const legacy_u8 flags[] = {0, 0, SHAPE3D_NO_SHADOW_RECEIVE_FLAG, SHAPE3D_GHOST_FLAG,
							   SHAPE3D_BACKGROUND_FLAG};
	legacy_s32 transmitted = 0;
	for (legacy_u32 scene = 0; scene < SDL_arraysize(flags); scene++) {
		prepare_scene(road, SDL_arraysize(road), primitive, sizeof(primitive), 1);
		begin_shadow_scene(SHADOW_TEST_CAR_HEIGHT, 0);
		/* Force rejection by the ordinary visibility gate after transforming
		 * the model. Occluder collection must still process the road. */
		memset(scene_visibility, 0, sizeof(scene_visibility));
		scene_instance.ts_flags |= flags[scene];
		scene_patterns[0] = scene == 0 ? 0 : SHADOW_TEST_MASKED_MATERIAL;
		material_patlist2_ptr_cpy = grille_patterns;
		assert(shape3d_transform_and_queue(&scene_instance) == LEGACY_U16_MAX);
		assert(shape3d_queued_primitive_count() == 0);
		legacy_u32 coverage = finish_shadow_scene(0, 0, 0);
		legacy_s32 sample = shadow_darkening_at(SHADOW_TEST_SAMPLE_X, SHADOW_TEST_SAMPLE_Z, 0);
		if (scene == 0) {
			assert(coverage == 0);
		} else if (scene == 1) {
			assert(coverage > 0 && sample > 0);
			transmitted = sample;
		} else {
			assert_shadow_fraction(sample, transmitted, 2);
		}
	}
	scene_patterns[0] = 0;
	material_patlist2_ptr_cpy = scene_patterns;
}

/* A camera underneath a bridge still sees its lower shadow receiver. The
 * deck behind the near plane must remain an occluder although none is queued. */
static void test_car_shadow_collects_behind_camera_road(void)
{
	const legacy_s16 camera_height = SHADOW_TEST_ROAD_HEIGHT + SHADOW_TEST_ROAD_HEIGHT / 2;
	const legacy_s16 road_height = SHADOW_TEST_CAR_HEIGHT - SHADOW_TEST_ROAD_HEIGHT / 4;
	const legacy_s16 relative_height = road_height - camera_height;
	const struct VECTOR road[] = {
		{-SHADOW_TEST_HALF_SURFACE, relative_height, -SHADOW_TEST_HALF_SURFACE},
		{SHADOW_TEST_HALF_SURFACE, relative_height, -SHADOW_TEST_HALF_SURFACE},
		{SHADOW_TEST_HALF_SURFACE, relative_height, SHADOW_TEST_HALF_SURFACE},
		{-SHADOW_TEST_HALF_SURFACE, relative_height, SHADOW_TEST_HALF_SURFACE}};
	const legacy_u8 primitive[] = {SHADOW_TEST_QUAD_VERTICES, 0, 0, 0, 1, 2, 3, 0, 0};
	const struct VECTOR camera = {0, camera_height, 0};
	const struct VECTOR car = {0, SHADOW_TEST_CAR_HEIGHT - camera_height, 0};
	for (legacy_s32 blocker = 0; blocker < 2; blocker++) {
		prepare_scene(road, SDL_arraysize(road), primitive, sizeof(primitive), 1);
		begin_shadow_scene(SHADOW_TEST_CAR_HEIGHT, 0);
		shape3d_hires_shadows_begin(&camera);
		shape3d_hires_shadow_car(&car, 0, (legacy_s16)shadow_image.half_width,
								 (legacy_s16)shadow_image.half_length);
		if (blocker) {
			assert(shape3d_transform_and_queue(&scene_instance) == LEGACY_U16_MAX);
			assert(shape3d_queued_primitive_count() == 0);
		}
		legacy_u32 coverage = finish_shadow_scene(SHADOW_TEST_CAMERA_HEIGHT - camera_height, 0, 0);
		assert(blocker ? coverage == 0 : coverage > 0);
	}
}

enum SHADOW_CONTACT_TEST_CONSTANTS {
	SHADOW_CONTACT_CAMERA_DISTANCE = 400,
	SHADOW_CONTACT_CAMERA_ROLL = 23,
	SHADOW_CONTACT_CAMERA_PITCH = -128,
	SHADOW_CONTACT_CAMERA_YAW = 17,
	SHADOW_CONTACT_NO_BLOCKER = 0,
	SHADOW_CONTACT_SOLID,
	SHADOW_CONTACT_GRILLE
};
#define SHADOW_CONTACT_DEPTH_ERROR 1e-7

static struct SHAPE3D_HIRES_VECTOR shadow_contact_view(legacy_f64 x, legacy_f64 y, legacy_f64 z)
{
	return (struct SHAPE3D_HIRES_VECTOR){
		(x * mat_temp.m._11 + y * mat_temp.m._12 + z * mat_temp.m._13) / TRIG_FIXED_ONE,
		(x * mat_temp.m._21 + y * mat_temp.m._22 + z * mat_temp.m._23) / TRIG_FIXED_ONE,
		(x * mat_temp.m._31 + y * mat_temp.m._32 + z * mat_temp.m._33) / TRIG_FIXED_ONE};
}

static legacy_s32 render_contact_shadow(legacy_s32 material, legacy_s32 lower,
										legacy_f64 depth_error)
{
	const struct VECTOR camera = {0, SHADOW_TEST_CAMERA_HEIGHT, -SHADOW_CONTACT_CAMERA_DISTANCE};
	const struct VECTOR car = {0, SHADOW_TEST_ROAD_HEIGHT - SHADOW_TEST_CAMERA_HEIGHT,
							   SHADOW_CONTACT_CAMERA_DISTANCE};
	begin_shadow_scene(SHADOW_TEST_ROAD_HEIGHT, 0);
	mat_temp = *mat_rot_zxy(SHADOW_CONTACT_CAMERA_ROLL, SHADOW_CONTACT_CAMERA_PITCH,
							SHADOW_CONTACT_CAMERA_YAW, MATRIX_ROTATION_ORDER_YXZ);
	shape3d_hires_shadows_begin(&camera);
	shape3d_hires_shadow_car(&car, 0, (legacy_s16)shadow_image.half_width,
							 (legacy_s16)shadow_image.half_length);
	const legacy_s16 corners[][2] = {{-SHADOW_TEST_HALF_SURFACE, -SHADOW_TEST_HALF_SURFACE},
									 {SHADOW_TEST_HALF_SURFACE, -SHADOW_TEST_HALF_SURFACE},
									 {SHADOW_TEST_HALF_SURFACE, SHADOW_TEST_HALF_SURFACE},
									 {-SHADOW_TEST_HALF_SURFACE, SHADOW_TEST_HALF_SURFACE}};
	struct SHAPE3D_HIRES_VECTOR road[SHADOW_TEST_QUAD_VERTICES];
	struct SHAPE3D_HIRES_VECTOR receiver[SHADOW_TEST_QUAD_VERTICES];
	for (legacy_u32 vertex = 0; vertex < SDL_arraysize(corners); vertex++) {
		road[vertex] = shadow_contact_view(corners[vertex][0], car.y, corners[vertex][1] + car.z);
		receiver[vertex] =
			shadow_contact_view(corners[vertex][0], (lower ? -camera.y : car.y) + depth_error,
								corners[vertex][1] + car.z);
	}
	if (material != SHADOW_CONTACT_NO_BLOCKER) {
		shape3d_hires_shadow_polygon(road, SDL_arraysize(road), material == SHADOW_CONTACT_GRILLE);
	}
	queue_polygon_shape(0, SHAPE3D_HIRES_DEPTH_SORTED, receiver);
	shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, SHADOW_TEST_SURFACE_COLOR, 0, 0, 0, 0);
	shape3d_hires_draw_shadows();
	hires_end();
	const legacy_u32 *image = shadow_test_argb();
	if (image == NULL) {
		return 0;
	}
	struct SHAPE3D_HIRES_VECTOR sample = shadow_contact_view(
		SHADOW_TEST_SAMPLE_X, lower ? -camera.y : car.y, car.z + SHADOW_TEST_SAMPLE_Z);
	struct SHAPE3D_HIRES_POINT point;
	shape3d_hires_project(&sample, &point);
	legacy_s32 x = (legacy_s32)point.x;
	legacy_s32 y = (legacy_s32)point.y;
	assert(x >= 0 && x < HIRES_WIDTH && y >= 0 && y < HIRES_HEIGHT);
	return SHADOW_TEST_BASE_CHANNEL - (image[y * HIRES_WIDTH + x] & LEGACY_U8_MAX);
}

static void test_car_shadow_contact_plane_precision(void)
{
	/* The rounded camera matrix and reciprocal depth buffer reconstruct a
	 * contact point on either side of the exact surface plane. Neither tiny
	 * error may remove the road's shadow or bypass grille attenuation. */
	const legacy_f64 errors[] = {-SHADOW_CONTACT_DEPTH_ERROR, 0, SHADOW_CONTACT_DEPTH_ERROR};
	for (legacy_s32 lower = 0; lower < 2; lower++) {
		legacy_s32 full = render_contact_shadow(SHADOW_CONTACT_NO_BLOCKER, lower, 0);
		assert(full > 0);
		for (legacy_u32 error = 0; error < SDL_arraysize(errors); error++) {
			legacy_s32 solid = render_contact_shadow(SHADOW_CONTACT_SOLID, lower, errors[error]);
			assert(solid == (lower ? 0 : full));
			legacy_s32 grille = render_contact_shadow(SHADOW_CONTACT_GRILLE, lower, errors[error]);
			assert_shadow_fraction(full, grille, 2);
		}
	}
}

static const legacy_u32 *render_warped_shadow(legacy_s32 ordered, legacy_s32 lower,
											  legacy_s32 grille, legacy_s32 collect)
{
	const legacy_s16 relative_height = SHADOW_TEST_ROAD_HEIGHT - SHADOW_TEST_CAMERA_HEIGHT;
	const struct VECTOR road[] = {
		{-SHADOW_TEST_HALF_SURFACE, relative_height, -SHADOW_TEST_HALF_SURFACE},
		{SHADOW_TEST_HALF_SURFACE, relative_height, -SHADOW_TEST_HALF_SURFACE},
		{SHADOW_TEST_HALF_SURFACE, relative_height, SHADOW_TEST_HALF_SURFACE},
		{-SHADOW_TEST_HALF_SURFACE, relative_height + SHADOW_TEST_ROAD_HEIGHT / 4,
		 SHADOW_TEST_HALF_SURFACE}};
	const legacy_u8 primitive[] = {
		SHADOW_TEST_QUAD_VERTICES, SHADOW_TEST_DOUBLE_SIDED_FLAG, 0, 0, 1, 2, 3, 0, 0};
	const struct VECTOR camera = {0, SHADOW_TEST_CAMERA_HEIGHT, 0};
	const struct VECTOR car = {0, SHADOW_TEST_CAR_HEIGHT - SHADOW_TEST_CAMERA_HEIGHT, 0};
	legacy_s16 grille_patterns[SDL_arraysize(scene_patterns)] = {
		(legacy_s16)PRERENDER_BLACK_GRILLE_PATTERN};
	prepare_scene(road, SDL_arraysize(road), primitive, sizeof(primitive), 1);
	begin_shadow_scene(SHADOW_TEST_CAR_HEIGHT, 0);
	shape3d_hires_shadows_begin(&camera);
	if (collect) {
		shape3d_hires_shadow_car(&car, 0, (legacy_s16)shadow_image.half_width,
								 (legacy_s16)shadow_image.half_length);
	}
	if (lower) {
		memset(scene_visibility, 0, sizeof(scene_visibility));
	}
	if (ordered) {
		scene_instance.ts_flags |= SHADOW_TEST_ORDERED_FLAG;
	}
	scene_patterns[0] = grille ? SHADOW_TEST_MASKED_MATERIAL : 0;
	material_patlist2_ptr_cpy = grille_patterns;
	assert(shape3d_transform_and_queue(&scene_instance) == (lower ? LEGACY_U16_MAX : 0));
	assert(shape3d_queued_primitive_count() == (lower ? 0 : 1));
	if (!collect) {
		/* The reference draws the same receiver before there is a caster
		 * to collect blockers for, retaining the original shadow footprint. */
		shape3d_hires_shadow_car(&car, 0, (legacy_s16)shadow_image.half_width,
								 (legacy_s16)shadow_image.half_length);
	}
	if (lower) {
		queue_shadow_receiver(0, 0, 1, SHADOW_TEST_HALF_SURFACE);
	}
	/* Draw the collected receiver without grille holes so every pixel can
	 * compare attenuation independently of the screen-space material mask. */
	shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, SHADOW_TEST_SURFACE_COLOR, 0, 0, 0, 0);
	shape3d_hires_draw_shadows();
	hires_end();
	scene_patterns[0] = 0;
	material_patlist2_ptr_cpy = scene_patterns;
	return shadow_test_argb();
}

static void test_car_shadow_warped_road_preserves_receiver(void)
{
	static legacy_u32 reference[HIRES_WIDTH * HIRES_HEIGHT];
	/* The fourth corner is raised, so the polygon's averaged occlusion
	 * plane differs from the rasterized receiver by several world units. */
	for (legacy_s32 ordered = 0; ordered < 2; ordered++) {
		for (legacy_s32 lower = 0; lower < 2; lower++) {
			const legacy_u32 *image = render_warped_shadow(ordered, lower, 0, 0);
			assert(image != NULL);
			memcpy(reference, image, sizeof(reference));
			for (legacy_s32 grille = 0; grille < 2; grille++) {
				image = render_warped_shadow(ordered, lower, grille, 1);
				legacy_u32 shadowed = 0;
				for (legacy_u32 pixel = 0; pixel < SDL_arraysize(reference); pixel++) {
					legacy_s32 full = SHADOW_TEST_BASE_CHANNEL - (reference[pixel] & LEGACY_U8_MAX);
					legacy_s32 actual =
						image == NULL ? 0
									  : SHADOW_TEST_BASE_CHANNEL - (image[pixel] & LEGACY_U8_MAX);
					if (grille) {
						legacy_s32 difference = full - actual * 2;
						assert(difference >= -SHADOW_TEST_CHANNEL_ROUNDING * 2 &&
							   difference <= SHADOW_TEST_CHANNEL_ROUNDING * 2);
					} else {
						assert(actual == (lower ? 0 : full));
					}
					shadowed += full > 0;
				}
				assert(shadowed > 0);
			}
		}
	}
}

static void test_car_shadow_render_scales(void)
{
	const legacy_s32 scales[] = {HIRES_SCALE, HIRES_MEDIUM_SCALE, HIRES_MINIMUM_SCALE,
								 HIRES_MEDIUM_SCALE, HIRES_SCALE};
	legacy_u32 coverage_by_scale[HIRES_SCALE + 1] = {0};
	prepare_shadow_model(1);
	for (legacy_u32 step = 0; step < SDL_arraysize(scales); step++) {
		legacy_s32 scale = scales[step];
		begin_shadow_scene_at_scale(0, 0, scale);
		shape3d_hires_shadow_model(&shadow_model);
		shadow_image.half_width = 40;
		shadow_image.half_length = 60;
		queue_shadow_receiver(0, 0, 1, 150);
		shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 8, 0, 0, 0, 0);
		legacy_u32 coverage = finish_shadow_scene(0, 0, 7);
		if (scale == HIRES_MINIMUM_SCALE) {
			assert(coverage == 0 && shadow_image.pixels == NULL);
		} else {
			assert(coverage != 0);
			if (coverage_by_scale[scale] != 0) {
				assert(coverage == coverage_by_scale[scale]);
			}
			coverage_by_scale[scale] = coverage;
		}
	}
	assert(coverage_by_scale[HIRES_MEDIUM_SCALE] < coverage_by_scale[HIRES_SCALE]);
	/* A shadow queued before a scale change must not draw at the minimum. */
	begin_shadow_scene(0, 0);
	hires_end();
	hires_set_render_scale(HIRES_MINIMUM_SCALE);
	assert(hires_begin(&target));
	hires_depth_begin(0, hires_render_width(), 0, hires_render_height());
	queue_shadow_receiver(0, 0, 1, 150);
	shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 8, 0, 0, 0, 0);
	assert(finish_shadow_scene(0, 0, 7) == 0);
	/* Conversely, the minimum must not queue a car to appear after recovery. */
	begin_shadow_scene_at_scale(0, 0, HIRES_MINIMUM_SCALE);
	shape3d_hires_shadow_model(&shadow_model);
	hires_end();
	hires_set_render_scale(HIRES_MEDIUM_SCALE);
	assert(hires_begin(&target));
	hires_depth_begin(0, hires_render_width(), 0, hires_render_height());
	queue_shadow_receiver(0, 0, 1, 150);
	shape3d_hires_render(0, RENDER_PRIMITIVE_POLYGON, 8, 0, 0, 0, 0);
	assert(finish_shadow_scene(0, 0, 7) == 0);
	hires_set_render_scale(HIRES_SCALE);
}

legacy_int main(void)
{
	screen = dos_memory_make_pointer(0xA000, 0);
	target.sprite_bitmapptr = (struct SHAPE2D *)screen;
	target.sprite_lineofs = rows;
	target.sprite_right = 320;
	target.sprite_bottom = 200;
	target.sprite_pitch = 320;
	target.sprite_buffer_width = 320;
	target.sprite_raster_right = 320;
	for (legacy_u32 row = 0; row < 200; row++) {
		LEGACY_WRITE_U16_LE(rows + row * 2, row * 320);
	}
	projection_center_x = 160;
	projection_center_y = 100;
	projection_focal_length_x = 160;
	projection_focal_length_y = 160;
	test_projection_and_subpixel_edges();
	test_attached_polygon_weight();
	test_attached_polygon_midrange_weight();
	test_attached_polygon_weight_follows_projection();
	test_attached_polygon_varies_along_depth();
	test_polygon_weight_preserves_surfaces();
	test_attached_polygon_clipping();
	test_attached_polygon_occlusion();
	test_attached_polygon_preserves_interior_depth();
	test_line_weight_follows_projection();
	test_line_weight_varies_along_depth();
	test_line_weight_after_clipping();
	test_line_weight();
	test_near_plane_and_screen_clipping();
	test_steep_line_stroke_has_no_holes();
	test_line_stroke_clipping();
	test_line_stroke_occlusion();
	test_materials_and_rounded_primitives();
	test_black_grille_preserves_stock_pixel_coverage();
	test_roundness_preserves_near_and_bounds_small_shapes();
	test_roundness_matches_worker_bands();
	test_disabled_and_reset();
	test_visible_line_overhang_survives_culling();
	test_visible_round_overhang_survives_culling();
	test_line_weight_in_half_scale_view();
	test_thin_polygon_and_attached_detail();
	test_full_polygon_winding();
	test_clipped_polygon_visibility();
	test_wheel_face_and_sort_depth();
	test_crossing_surfaces_use_pixel_depth();
	test_separate_shapes_use_pixel_depth();
	test_separate_shapes_preserve_authored_overlays();
	test_unattached_polygons_keep_nearest_surface();
	test_scene_depth_resets_between_draws();
	test_separate_shapes_preserve_ghost_holes();
	test_background_shapes_do_not_occlude_scene();
	test_far_diagonal_geometry();
	test_precise_scene_origin();
	test_far_primitive_depth_visibility();
	test_hypervision_full_scene_queue();
	test_body_panel_occludes_wheel();
	test_joined_track_surfaces();
	test_shared_edge_pixel_coverage();
	test_shared_edge_near_clipping();
	test_concave_polygon_scanlines();
	test_parallel_batches_match_serial();
	test_render_scale_roundtrip();
	test_ground_occludes_generated_detail();
	test_car_shadow_stays_below_and_shrinks();
	test_car_shadow_model_details_and_north_light();
	test_car_shadow_model_size_and_cache_reset();
	test_car_shadow_light_direction_and_reset();
	test_car_shadow_directed_light_occlusion();
	test_projected_car_shadow_wheel_contacts();
	test_projected_car_shadow_height_and_cache();
	test_projected_car_shadow_sphere_contacts();
	test_car_shadow_receiver_height();
	test_car_shadow_excludes_car_geometry();
	test_car_shadow_ground_fallback_and_reset();
	test_car_shadow_opaque_road_and_partial_edge();
	test_car_shadow_grille_transmission();
	test_car_shadow_collects_camera_hidden_roads();
	test_car_shadow_collects_behind_camera_road();
	test_car_shadow_contact_plane_precision();
	test_car_shadow_warped_road_preserves_receiver();
	test_car_shadow_render_scales();
	hires_shutdown();
	puts("High-resolution 3D projection and raster tests passed.");
	return 0;
}
