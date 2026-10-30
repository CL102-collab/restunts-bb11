#include <assert.h>
#include <string.h>

#include "../c/externs.h"
#include "../c/shape2d.h"
#include "../c/shape3d.h"
#include "../c/shape3d_internal.h"

#undef memset

static legacy_u8 record[22] = {0, 0, 0, 4, RENDER_PRIMITIVE_POLYGON, 0};
static legacy_u32 draw_calls;
static legacy_u16 drawn_colors[3];
static legacy_u16 drawn_pattern;

static void retain_draw(legacy_u16 color)
{
	draw_calls++;
	drawn_colors[0] = color;
}

void preRender_default(legacy_u16 color, legacy_u16 count, const struct POINT2D *points)
{
	assert(count == 4 && points != 0);
	retain_draw(color);
}

void preRender_patterned(legacy_u16 pattern, legacy_u16 color, legacy_u16 count,
						 const struct POINT2D *points)
{
	drawn_pattern = pattern;
	preRender_default(color, count, points);
}

void preRender_two_color(legacy_u16 pattern, legacy_u16 color, legacy_u16 alternate_color,
						 legacy_u16 count, const struct POINT2D *points)
{
	drawn_colors[1] = alternate_color;
	preRender_patterned(pattern, color, count, points);
}

void preRender_line(legacy_u16 x1, legacy_u16 y1, legacy_u16 x2, legacy_u16 y2, legacy_u16 color)
{
	(void)x1;
	(void)y1;
	(void)x2;
	(void)y2;
	retain_draw(color);
}

void preRender_sphere(legacy_s16 x, legacy_s16 y, legacy_u16 size, legacy_u16 color)
{
	(void)x;
	(void)y;
	(void)size;
	retain_draw(color);
}

void preRender_wheel(const struct POINT2D *points, legacy_u16 scale, legacy_u16 outer_color,
					 legacy_u16 side_color, legacy_u16 inner_color)
{
	(void)points;
	(void)scale;
	drawn_colors[1] = side_color;
	drawn_colors[2] = inner_color;
	retain_draw(outer_color);
}

void sprite_putpixel_clipped(legacy_s16 x, legacy_s16 y, legacy_s16 color)
{
	(void)x;
	(void)y;
	retain_draw((legacy_u16)color);
}

static void render_material(legacy_u16 material, legacy_u16 primitive)
{
	draw_calls = 0;
	memset(drawn_colors, 0, sizeof(drawn_colors));
	drawn_pattern = 0;
	record[2] = (legacy_u8)material;
	record[4] = (legacy_u8)primitive;
	polyinfo_reset();
	polyinfonumpolys = 1;
	polygon_next_index[POLYINFO_LEGACY_PRIMITIVE_CAPACITY] = 0;
	polyinfoptr = record;
	polygon_record_offsets[0] = 0;
	shape3d_render_queued_primitives();
}

/* These ranges come from the archived executable's contiguous DOS tables,
 * including the two extra color words read by material 255's wheel. */
static legacy_u16 original_extended_color(legacy_u16 material)
{
	if (material == 128U) {
		return 17;
	}
	return (material >= 151U && material <= 153U) || material == 163U || material == 223U ||
		   (material >= 247U && material <= 255U);
}

static void test_original_material_table_boundaries(void)
{
	material_clrlist_ptr_cpy = material_color_list;
	material_clrlist2_ptr_cpy = material_color_list;
	material_patlist_ptr_cpy = material_pattern_list;
	material_patlist2_ptr_cpy = material_pattern2_list;
	for (legacy_u16 material = 128U; material <= 255U; material++) {
		legacy_u16 visible = material == 128U || material == 147U ||
							 (material >= 193U && material <= 222U) ||
							 (material >= 235U && material <= 247U);
		render_material(material, RENDER_PRIMITIVE_POLYGON);
		assert(draw_calls == visible);
		if (visible != 0U) {
			assert(drawn_colors[0] == original_extended_color(material));
		}
		render_material(material, RENDER_PRIMITIVE_POLYGON | RENDER_PRIMITIVE_GHOST_FLAG);
		assert(draw_calls == visible);
		if (visible != 0U) {
			assert(drawn_colors[0] == PRERENDER_GHOST_COLOR);
		}
		static const legacy_u8 colored_primitives[] = {
			RENDER_PRIMITIVE_LINE, RENDER_PRIMITIVE_SPHERE, RENDER_PRIMITIVE_POINT,
			RENDER_PRIMITIVE_WHEEL};
		for (legacy_u32 primitive = 0; primitive < sizeof(colored_primitives); primitive++) {
			render_material(material, colored_primitives[primitive]);
			assert(draw_calls == 1);
			assert(drawn_colors[0] == original_extended_color(material));
			if (colored_primitives[primitive] == RENDER_PRIMITIVE_WHEEL) {
				assert(drawn_colors[1] == original_extended_color(material + 1U));
				assert(drawn_colors[2] == original_extended_color(material + 2U));
			}
		}
	}
}

#define TEST_YELLOW_GRILLE_MATERIAL 24U
#define TEST_STOCK_GRILLE_PATTERN 0x77ddU

static void test_black_grille_keeps_stock_materials_and_holes(void)
{
	material_clrlist_ptr_cpy = material_color_list;
	material_clrlist2_ptr_cpy = material_color_list;
	material_patlist_ptr_cpy = material_pattern_list;
	material_patlist2_ptr_cpy = material_pattern2_list;
	legacy_u16 stock_color = (legacy_u16)material_color_list[SHAPE3D_STOCK_GRILLE_MATERIAL];
	legacy_u16 yellow_color = (legacy_u16)material_color_list[TEST_YELLOW_GRILLE_MATERIAL];
	assert(stock_color != 0 && yellow_color != 0);
	render_material(SHAPE3D_STOCK_GRILLE_MATERIAL, RENDER_PRIMITIVE_POLYGON);
	assert(draw_calls == 1 && drawn_colors[0] == stock_color);
	assert(drawn_pattern == TEST_STOCK_GRILLE_PATTERN);
	render_material(SHAPE3D_STOCK_GRILLE_MATERIAL,
					RENDER_PRIMITIVE_POLYGON | RENDER_PRIMITIVE_BLACK_GRILLE_FLAG);
	assert(draw_calls == 1 && drawn_colors[0] == 0);
	assert(drawn_pattern == TEST_STOCK_GRILLE_PATTERN);
	/* An override is carried by its own queued polygon, never a material-table
	 * mutation that could recolor a stock corkscrew or its yellow markings. */
	render_material(TEST_YELLOW_GRILLE_MATERIAL, RENDER_PRIMITIVE_POLYGON);
	assert(draw_calls == 1 && drawn_colors[0] == yellow_color);
	assert(drawn_pattern == TEST_STOCK_GRILLE_PATTERN);
	render_material(SHAPE3D_STOCK_GRILLE_MATERIAL, RENDER_PRIMITIVE_POLYGON);
	assert(draw_calls == 1 && drawn_colors[0] == stock_color);
	assert(drawn_pattern == TEST_STOCK_GRILLE_PATTERN);
	render_material(SHAPE3D_STOCK_GRILLE_MATERIAL, RENDER_PRIMITIVE_POLYGON |
													   RENDER_PRIMITIVE_BLACK_GRILLE_FLAG |
													   RENDER_PRIMITIVE_GHOST_FLAG);
	assert(draw_calls == 1 && drawn_colors[0] == PRERENDER_GHOST_COLOR);
	assert(material_color_list[SHAPE3D_STOCK_GRILLE_MATERIAL] == stock_color);
	assert(material_color_list[TEST_YELLOW_GRILLE_MATERIAL] == yellow_color);
}

static void test_replacement_material_tables(void)
{
	legacy_s16 colors[256] = {0};
	legacy_s16 secondary_colors[256] = {0};
	legacy_s16 patterns[256] = {0};
	legacy_s16 secondary_patterns[256] = {0};
	colors[129] = 77;
	secondary_colors[129] = 88;
	patterns[129] = 2;
	secondary_patterns[129] = 12345;
	material_clrlist_ptr_cpy = colors;
	material_clrlist2_ptr_cpy = secondary_colors;
	material_patlist_ptr_cpy = patterns;
	material_patlist2_ptr_cpy = secondary_patterns;
	render_material(129, RENDER_PRIMITIVE_POLYGON);
	assert(draw_calls == 1 && drawn_colors[0] == 88 && drawn_colors[1] == 77);
	assert(drawn_pattern == 12345);
	patterns[129] = 1;
	render_material(129, RENDER_PRIMITIVE_POLYGON);
	assert(draw_calls == 1 && drawn_colors[0] == 77 && drawn_pattern == 12345);
	secondary_patterns[129] = 0;
	render_material(129, RENDER_PRIMITIVE_POLYGON);
	assert(draw_calls == 0);
	patterns[129] = 0;
	render_material(129, RENDER_PRIMITIVE_POLYGON);
	assert(draw_calls == 1 && drawn_colors[0] == 77);
}

legacy_int main(void)
{
	test_original_material_table_boundaries();
	test_black_grille_keeps_stock_materials_and_holes();
	test_replacement_material_tables();
	return 0;
}
