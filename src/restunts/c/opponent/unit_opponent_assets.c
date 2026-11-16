#include "unit_opponent_assets.h"
#include "opponent.h"
#include "unit_portrait_data.h"
#include "../memmgr.h"
#include "../shape2d/shape2d.h"
#include "../ui/ui_text.h"
#include <string.h>

#define UNIT_BIO_LEFT 12
#define UNIT_BIO_TITLE_TOP 33
#define UNIT_BIO_AGE_TOP 51
#define UNIT_BIO_EXPERIENCE_TOP 59
#define UNIT_BIO_EXPERTISE_TOP 67
#define UNIT_BIO_PROBLEMS_TOP 83
#define UNIT_BIO_BACKGROUND_TOP 101
#define UNIT_BIO_LINE_WIDTH 180
#define UNIT_BIO_LINES_PER_PAGE 8U
#define UNIT_BIO_LINE_CAPACITY 80U
#define UNIT_BIO_PAGE_TOP 166

static struct SHAPE2D *unit_portrait;
static const legacy_s8 unit_background[] =
	"Background: When the last human who has ever played Stunts has died at the age of 67 "
	"in 2055 after being hit by a car with full self driving enabled, no-one played Stunts "
	"anymore. That's why it was needed to create AI pipsqueaks who could compete against "
	"each other in competitions, organized exclusively for AI. Since their knowledge is "
	"shared between each other, it isn't really a competition anymore, but a distributed "
	"system for optimizing parameters to approximate a function that maps inputs to outputs.";

static struct SHAPE2D *unit_opponent_load_portrait(const struct SHAPE2D *original)
{
	if (unit_portrait == 0) {
		unit_portrait = (struct SHAPE2D *)mmgr_alloc_resbytes(
			"unitpic", SHAPE2D_HEADER_SIZE + sizeof(unit_portrait_pixels));
		memset(unit_portrait, 0, SHAPE2D_HEADER_SIZE);
		LEGACY_WRITE_U16_LE((legacy_u8 *)&unit_portrait->width, UNIT_PORTRAIT_WIDTH);
		LEGACY_WRITE_U16_LE((legacy_u8 *)&unit_portrait->height, UNIT_PORTRAIT_HEIGHT);
		memcpy((legacy_u8 *)unit_portrait + SHAPE2D_HEADER_SIZE, unit_portrait_pixels,
			   sizeof(unit_portrait_pixels));
	}
	if (original != 0) {
		memcpy(unit_portrait, original, SHAPE2D_HEADER_SIZE);
	}
	/* The shared portrait keeps its selection number only in opponent menus. */
	legacy_u8 *pixels = (legacy_u8 *)unit_portrait + SHAPE2D_HEADER_SIZE;
	for (legacy_u16 row = 0; row < UNIT_PORTRAIT_DIGIT_HEIGHT; row++) {
		legacy_u16 offset =
			(UNIT_PORTRAIT_DIGIT_Y + row) * UNIT_PORTRAIT_WIDTH + UNIT_PORTRAIT_DIGIT_X;
		const legacy_u8 *source =
			original != 0 ? unit_portrait_pixels + offset
						  : unit_portrait_result_patch + row * UNIT_PORTRAIT_DIGIT_WIDTH;
		memcpy(pixels + offset, source, UNIT_PORTRAIT_DIGIT_WIDTH);
	}
	return unit_portrait;
}

struct SHAPE2D *unit_opponent_menu_portrait(legacy_u8 opponent, legacy_s8 **originals)
{
	if (opponent == OPPONENT_UNIT) {
		return unit_opponent_load_portrait((const struct SHAPE2D *)originals[OPPONENT_LEGACY_LAST]);
	}
	return (struct SHAPE2D *)originals[opponent <= OPPONENT_LEGACY_LAST ? opponent : 0];
}

struct SHAPE2D *unit_opponent_result_portrait(void)
{
	return unit_opponent_load_portrait(0);
}

void unit_opponent_portrait_release(void)
{
	if (unit_portrait != 0) {
		mmgr_release(unit_portrait);
		unit_portrait = 0;
	}
}

const legacy_s8 *unit_opponent_background(void)
{
	return unit_background;
}

static void unit_bio_next_line(const legacy_s8 **source, legacy_s8 *line)
{
	const legacy_s8 *start = *source;
	legacy_u16 length = 0;
	legacy_u16 last_space = 0;
	while (start[length] != 0 && length + 1U < UNIT_BIO_LINE_CAPACITY) {
		line[length] = start[length];
		line[length + 1U] = 0;
		if (font_text_width(line) > UNIT_BIO_LINE_WIDTH) {
			break;
		}
		if (start[length] == ' ') {
			last_space = length;
		}
		length++;
	}
	if (start[length] != 0 && last_space != 0) {
		length = last_space;
	}
	/* One oversized glyph must still advance; normal game fonts fit easily. */
	if (length == 0 && start[0] != 0) {
		line[0] = start[0];
		length = 1;
	}
	line[length] = 0;
	*source = start + length;
	while (**source == ' ') {
		(*source)++;
	}
}

legacy_u16 unit_opponent_draw_biography(legacy_u16 page)
{
	font_draw_text("Unitree Humanoid Bot", UNIT_BIO_LEFT, UNIT_BIO_TITLE_TOP);
	font_draw_text("Age: 1 Height: 6'0", UNIT_BIO_LEFT, UNIT_BIO_AGE_TOP);
	font_draw_text("Years Experience: 97", UNIT_BIO_LEFT, UNIT_BIO_EXPERIENCE_TOP);
	font_draw_text("Expertise: All", UNIT_BIO_LEFT, UNIT_BIO_EXPERTISE_TOP);
	font_draw_text("Problems: None", UNIT_BIO_LEFT, UNIT_BIO_PROBLEMS_TOP);
	const legacy_s8 *source = unit_background;
	legacy_s8 line[UNIT_BIO_LINE_CAPACITY];
	legacy_u16 index = 0;
	while (*source != 0) {
		unit_bio_next_line(&source, line);
		if (index / UNIT_BIO_LINES_PER_PAGE == page) {
			font_draw_text(line, UNIT_BIO_LEFT,
						   UNIT_BIO_BACKGROUND_TOP +
							   (legacy_s16)(index % UNIT_BIO_LINES_PER_PAGE) * font_glyph_height);
		}
		index++;
	}
	legacy_u16 pages = (index + UNIT_BIO_LINES_PER_PAGE - 1U) / UNIT_BIO_LINES_PER_PAGE;
	legacy_s8 label[] = "Bio 0/0  Up/Down";
	label[4] = (legacy_s8)('0' + page + 1U);
	label[6] = (legacy_s8)('0' + pages);
	font_draw_text(label, UNIT_BIO_LEFT, UNIT_BIO_PAGE_TOP);
	return pages;
}
