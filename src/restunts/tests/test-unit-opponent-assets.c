#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../c/memmgr.h"
#include "../c/opponent/opponent.h"
#include "../c/opponent/unit_opponent_assets.h"
#include "../c/opponent/unit_portrait_data.h"
#include "../c/shape2d/shape2d.h"
#include "../c/ui/ui_text.h"

#define TEST_GUARD_BYTES 16U
#define TEST_GUARD_VALUE 165U
#define TEST_DRAW_CAPACITY 16U
#define TEST_TEXT_CAPACITY 128U
#define TEST_BIO_CAPACITY 1024U
#define TEST_GLYPH_WIDTH 4U
#define TEST_WIDE_SPACE_WIDTH 5U
#define TEST_GLYPH_HEIGHT 8
#define TEST_BIO_LEFT 12
#define TEST_BIO_WIDTH 180
#define TEST_BIO_BACKGROUND_TOP 105
#define TEST_BIO_PAGE_TOP 166
#define TEST_BIO_METADATA_COUNT 5U
#define TEST_BIO_MAX_LINES ((TEST_BIO_PAGE_TOP - TEST_BIO_BACKGROUND_TOP) / TEST_GLYPH_HEIGHT)
#define TEST_BIO_INDENT_SPACES 4U
#define TEST_BIO_NORMAL_PAGES 2U
#define TEST_BIO_WIDE_SPACE_PAGES 3U
#define TEST_PORTRAIT_DIGIT_COLOR 39U

struct TEST_DRAW {
	legacy_s8 text[TEST_TEXT_CAPACITY];
	legacy_s16 x;
	legacy_s16 y;
};

static legacy_u8 *allocation;
static legacy_s32 allocation_size;
static legacy_u16 allocation_count;
static legacy_u16 release_count;
static struct TEST_DRAW draws[TEST_DRAW_CAPACITY];
static legacy_u16 draw_count;
static legacy_u16 test_space_width = TEST_GLYPH_WIDTH;
legacy_s16 font_glyph_height = TEST_GLYPH_HEIGHT;

void *mmgr_alloc_resbytes(const legacy_s8 *name, legacy_s32 size)
{
	assert(allocation == NULL);
	assert(strcmp((const legacy_char *)name, "unitpic") == 0);
	assert(size == SHAPE2D_HEADER_SIZE + sizeof(unit_portrait_pixels));
	allocation_size = size;
	allocation = malloc((size_t)size + TEST_GUARD_BYTES * 2U);
	assert(allocation != NULL);
	memset(allocation, TEST_GUARD_VALUE, (size_t)size + TEST_GUARD_BYTES * 2U);
	allocation_count++;
	return allocation + TEST_GUARD_BYTES;
}

void mmgr_release(void *pointer)
{
	assert(allocation != NULL && pointer == allocation + TEST_GUARD_BYTES);
	for (legacy_u16 index = 0; index < TEST_GUARD_BYTES; index++) {
		assert(allocation[index] == TEST_GUARD_VALUE);
		assert(allocation[TEST_GUARD_BYTES + allocation_size + index] == TEST_GUARD_VALUE);
	}
	free(allocation);
	allocation = NULL;
	release_count++;
}

legacy_s16 font_text_width(const legacy_s8 *text)
{
	legacy_u16 width = 0;
	while (*text != 0) {
		width += *text++ == ' ' ? test_space_width : TEST_GLYPH_WIDTH;
	}
	return (legacy_s16)width;
}

void font_draw_text(const legacy_s8 *text, legacy_s16 x, legacy_s16 y)
{
	assert(draw_count < TEST_DRAW_CAPACITY);
	assert(strlen((const legacy_char *)text) < sizeof(draws[draw_count].text));
	strcpy((legacy_char *)draws[draw_count].text, (const legacy_char *)text);
	draws[draw_count].x = x;
	draws[draw_count].y = y;
	draw_count++;
}

static void assert_portrait_pixels(const struct SHAPE2D *portrait, legacy_u8 numbered)
{
	assert(portrait->width == UNIT_PORTRAIT_WIDTH);
	assert(portrait->height == UNIT_PORTRAIT_HEIGHT);
	legacy_u8 expected[sizeof(unit_portrait_pixels)];
	memcpy(expected, unit_portrait_pixels, sizeof(expected));
	if (!numbered) {
		legacy_u16 changed = 0;
		for (legacy_u16 row = 0; row < UNIT_PORTRAIT_DIGIT_HEIGHT; row++) {
			for (legacy_u16 column = 0; column < UNIT_PORTRAIT_DIGIT_WIDTH; column++) {
				legacy_u16 offset = (UNIT_PORTRAIT_DIGIT_Y + row) * UNIT_PORTRAIT_WIDTH +
									UNIT_PORTRAIT_DIGIT_X + column;
				legacy_u8 background =
					unit_portrait_result_patch[row * UNIT_PORTRAIT_DIGIT_WIDTH + column];
				if (expected[offset] != background) {
					assert(expected[offset] == TEST_PORTRAIT_DIGIT_COLOR);
					changed++;
				}
				expected[offset] = background;
			}
		}
		assert(changed != 0);
	}
	assert(memcmp((const legacy_u8 *)portrait + SHAPE2D_HEADER_SIZE, expected, sizeof(expected)) ==
		   0);
}

static void test_portrait_ownership(void)
{
	struct SHAPE2D originals[OPPONENT_LEGACY_LAST + 1U];
	legacy_s8 *resources[OPPONENT_LEGACY_LAST + 1U];
	memset(originals, 0, sizeof(originals));
	for (legacy_u16 index = 0; index <= OPPONENT_LEGACY_LAST; index++) {
		originals[index].width = UNIT_PORTRAIT_WIDTH;
		originals[index].height = UNIT_PORTRAIT_HEIGHT;
		originals[index].position_x = index;
		originals[index].position_y = OPPONENT_LEGACY_LAST - index;
		resources[index] = (legacy_s8 *)&originals[index];
		assert(unit_opponent_menu_portrait((legacy_u8)index, resources) == &originals[index]);
	}
	/* Only seven original entries exist; future IDs must not read beyond them. */
	for (legacy_u16 opponent = OPPONENT_UNIT + 1U; opponent <= LEGACY_U8_MAX; opponent++) {
		assert(unit_opponent_menu_portrait((legacy_u8)opponent, resources) == &originals[0]);
	}
	assert(allocation_count == 0);
	unit_opponent_portrait_release();
	assert(release_count == 0);
	struct SHAPE2D *portrait = unit_opponent_result_portrait();
	assert(allocation_count == 1);
	assert_portrait_pixels(portrait, 0);
	assert(unit_opponent_result_portrait() == portrait);
	assert_portrait_pixels(portrait, 0);
	assert(unit_opponent_menu_portrait(OPPONENT_UNIT, resources) == portrait);
	assert(memcmp(portrait, &originals[OPPONENT_LEGACY_LAST], SHAPE2D_HEADER_SIZE) == 0);
	assert_portrait_pixels(portrait, 1);
	/* Result screens remove only the number, even after the menu cached it. */
	assert(unit_opponent_result_portrait() == portrait);
	assert_portrait_pixels(portrait, 0);
	assert(unit_opponent_result_portrait() == portrait);
	assert_portrait_pixels(portrait, 0);
	for (legacy_u16 index = 0; index <= OPPONENT_LEGACY_LAST; index++) {
		assert(unit_opponent_menu_portrait((legacy_u8)index, resources) == &originals[index]);
	}
	assert_portrait_pixels(portrait, 0);
	assert(unit_opponent_menu_portrait(OPPONENT_UNIT, resources) == portrait);
	assert_portrait_pixels(portrait, 1);
	/* A new menu layout updates geometry without reallocating cached pixels. */
	originals[OPPONENT_LEGACY_LAST].position_x++;
	originals[OPPONENT_LEGACY_LAST].plane_flags[0] = 1;
	assert(unit_opponent_menu_portrait(OPPONENT_UNIT, resources) == portrait);
	assert(memcmp(portrait, &originals[OPPONENT_LEGACY_LAST], SHAPE2D_HEADER_SIZE) == 0);
	assert_portrait_pixels(portrait, 1);
	assert(allocation_count == 1);
	unit_opponent_portrait_release();
	assert(release_count == 1 && allocation == NULL);
	unit_opponent_portrait_release();
	assert(release_count == 1);
	/* A later result screen starts with fresh geometry and the same portrait. */
	portrait = unit_opponent_result_portrait();
	assert(allocation_count == 2);
	assert_portrait_pixels(portrait, 0);
	assert(portrait->position_x == 0 && portrait->position_y == 0);
	assert(portrait->plane_flags[0] == 0);
	unit_opponent_portrait_release();
	assert(release_count == allocation_count && allocation == NULL);
}

static legacy_u16 assert_biography_layout(void)
{
	static const legacy_char *metadata[] = {"Unitree Humanoid Bot", "Age: 1 Height: 6'0",
											"Years Experience: 97", "Expertise: All",
											"Problems: None"};
	static const legacy_s16 metadata_y[] = {33, 57, 65, 73, 89};
	static const legacy_char expected_background[] =
		"Background: When the last human who has ever played Stunts has died at the age of 67 "
		"in 2055 after being hit by a car with full self driving enabled, no-one played Stunts "
		"anymore. That's why it was needed to create AI pipsqueaks who could compete against "
		"each other in competitions, organized exclusively for AI. Since their knowledge is "
		"shared between each other, it isn't really a competition anymore, but a distributed "
		"system for optimizing parameters to approximate a function that maps inputs to outputs.";
	assert(strcmp((const legacy_char *)unit_opponent_background(), expected_background) == 0);
	draw_count = 0;
	legacy_u16 pages = unit_opponent_draw_biography(0);
	assert(pages > 1);
	legacy_char reconstructed[TEST_BIO_CAPACITY] = {0};
	for (legacy_u16 page = 0; page < pages; page++) {
		draw_count = 0;
		assert(unit_opponent_draw_biography(page) == pages);
		assert(draw_count > TEST_BIO_METADATA_COUNT + 1U);
		assert(draw_count <= TEST_BIO_METADATA_COUNT + TEST_BIO_MAX_LINES + 1U);
		struct TEST_DRAW previous_draws[TEST_DRAW_CAPACITY];
		legacy_u16 previous_count = draw_count;
		memcpy(previous_draws, draws, draw_count * sizeof(draws[0]));
		draw_count = 0;
		assert(unit_opponent_draw_biography(page) == pages);
		assert(draw_count == previous_count);
		assert(memcmp(previous_draws, draws, draw_count * sizeof(draws[0])) == 0);
		for (legacy_u16 index = 0; index < TEST_BIO_METADATA_COUNT; index++) {
			assert(strcmp((const legacy_char *)draws[index].text, metadata[index]) == 0);
			assert(draws[index].y == metadata_y[index]);
			assert(draws[index].x == TEST_BIO_LEFT);
		}
		for (legacy_u16 index = 0; index < draw_count; index++) {
			assert(draws[index].x - TEST_BIO_LEFT + font_text_width(draws[index].text) <=
				   TEST_BIO_WIDTH);
		}
		for (legacy_u16 index = TEST_BIO_METADATA_COUNT; index + 1U < draw_count; index++) {
			const legacy_char *line = (const legacy_char *)draws[index].text;
			size_t length = strlen(line);
			assert(length > 0 && line[0] != ' ' && line[length - 1U] != ' ');
			legacy_s16 margin = page == 0 && index == TEST_BIO_METADATA_COUNT
									? 0
									: (legacy_s16)(TEST_BIO_INDENT_SPACES * test_space_width);
			assert(draws[index].x == TEST_BIO_LEFT + margin);
			assert(draws[index].y ==
				   TEST_BIO_BACKGROUND_TOP +
					   (legacy_s16)(index - TEST_BIO_METADATA_COUNT) * font_glyph_height);
			assert(draws[index].y + font_glyph_height <= TEST_BIO_PAGE_TOP);
			assert(strlen(reconstructed) + length + 2U <= sizeof(reconstructed));
			if (reconstructed[0] != 0) {
				strcat(reconstructed, " ");
			}
			strcat(reconstructed, line);
		}
		legacy_char label[TEST_TEXT_CAPACITY];
		snprintf(label, sizeof(label), "Bio %u/%u  Up/Down", (legacy_uint)(page + 1U),
				 (legacy_uint)pages);
		assert(strcmp((const legacy_char *)draws[draw_count - 1U].text, label) == 0);
		assert(draws[draw_count - 1U].y == TEST_BIO_PAGE_TOP);
		assert(draws[draw_count - 1U].x == TEST_BIO_LEFT);
	}
	assert(strcmp(reconstructed, expected_background) == 0);
	return pages;
}

static void test_biography(void)
{
	assert(assert_biography_layout() == TEST_BIO_NORMAL_PAGES);
	/* Indentation must measure spaces, including with a proportional font. */
	test_space_width = TEST_WIDE_SPACE_WIDTH;
	assert(assert_biography_layout() == TEST_BIO_WIDE_SPACE_PAGES);
	test_space_width = TEST_GLYPH_WIDTH;
}

legacy_int main(void)
{
	test_portrait_ownership();
	test_biography();
	puts("UNIT opponent assets tests passed");
	return 0;
}
