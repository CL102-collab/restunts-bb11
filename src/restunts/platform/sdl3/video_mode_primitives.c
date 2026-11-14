#include "video_mode_primitives.h"
#include "video_mode.h"
#include "video_mode_materials.h"
#include "video_mode_colors.h"
#include "../../c/externs.h"
#include "../../c/shape2d/shape2d_internal.h"
#include "../../c/shape3d/shape3d.h"
#include "../../c/shape3d/shape3d_pipeline_internal.h"

enum VIDEO_MODE_MATERIAL_TABLE {
	VIDEO_MODE_MATERIAL_PRIMARY,
	VIDEO_MODE_MATERIAL_SECONDARY,
	VIDEO_MODE_MATERIAL_PATTERN_TYPE,
	VIDEO_MODE_MATERIAL_PATTERN_BITS,
	VIDEO_MODE_MATERIAL_TABLE_COUNT
};

enum VIDEO_MODE_PRIMITIVE_COLOR {
	VIDEO_MODE_PRIMITIVE_PRIMARY,
	VIDEO_MODE_PRIMITIVE_SECONDARY,
	VIDEO_MODE_PRIMITIVE_TERTIARY,
	VIDEO_MODE_PRIMITIVE_COLOR_COUNT
};

struct VIDEO_MODE_MATERIALS {
	const legacy_u16 *table[VIDEO_MODE_MATERIAL_TABLE_COUNT];
	legacy_u8 cga;
};

static legacy_u16 video_mode_material_value(const struct VIDEO_MODE_MATERIALS *materials,
											enum VIDEO_MODE_MATERIAL_TABLE table, legacy_u16 index)
{
	/* Resource materials are bytes, and wheel colors read two entries further.
	 * Reproduce adjacent native tables without depending on C object layout. */
	if (index >= VIDEO_MODE_MATERIAL_COUNT) {
		index -= VIDEO_MODE_MATERIAL_COUNT;
		if (table == VIDEO_MODE_MATERIAL_PRIMARY ||
			(table == VIDEO_MODE_MATERIAL_SECONDARY && materials->cga)) {
			table = VIDEO_MODE_MATERIAL_PATTERN_TYPE;
		} else if (table == VIDEO_MODE_MATERIAL_PATTERN_TYPE) {
			table = VIDEO_MODE_MATERIAL_PATTERN_BITS;
		} else {
			return 0;
		}
	}
	if (table >= VIDEO_MODE_MATERIAL_TABLE_COUNT || index >= VIDEO_MODE_MATERIAL_COUNT) {
		return 0;
	}
	return materials->table[table][index];
}

static void video_mode_draw_primitive(const legacy_u8 *record, struct POINT2D *points,
									  const struct VIDEO_MODE_MATERIALS *materials)
{
	legacy_u16 type = record[POLYINFO_TYPE_OFFSET] & ~RENDER_PRIMITIVE_VISUAL_FLAGS;
	legacy_u16 material = record[POLYINFO_MATERIAL_OFFSET];
	legacy_u16 ghost = record[POLYINFO_TYPE_OFFSET] & RENDER_PRIMITIVE_GHOST_FLAG;
	legacy_u16 colors[VIDEO_MODE_PRIMITIVE_COLOR_COUNT];
	colors[VIDEO_MODE_PRIMITIVE_PRIMARY] =
		video_mode_material_value(materials, VIDEO_MODE_MATERIAL_PRIMARY, material);
	colors[VIDEO_MODE_PRIMITIVE_SECONDARY] =
		video_mode_material_value(materials, VIDEO_MODE_MATERIAL_SECONDARY, material);
	colors[VIDEO_MODE_PRIMITIVE_TERTIARY] = 0;
	if (type == RENDER_PRIMITIVE_WHEEL) {
		colors[VIDEO_MODE_PRIMITIVE_SECONDARY] =
			video_mode_material_value(materials, VIDEO_MODE_MATERIAL_PRIMARY, material + 1U);
		colors[VIDEO_MODE_PRIMITIVE_TERTIARY] =
			video_mode_material_value(materials, VIDEO_MODE_MATERIAL_PRIMARY, material + 2U);
	}
	if (ghost) {
		for (legacy_u16 index = 0; index < VIDEO_MODE_PRIMITIVE_COLOR_COUNT; index++) {
			colors[index] = 0;
		}
	} else if (type == RENDER_PRIMITIVE_POLYGON &&
			   (record[POLYINFO_TYPE_OFFSET] & RENDER_PRIMITIVE_BLACK_GRILLE_FLAG) != 0U) {
		colors[VIDEO_MODE_PRIMITIVE_PRIMARY] = 0;
	}
	legacy_u16 point_color = type == RENDER_PRIMITIVE_POINT;
	if (type == RENDER_PRIMITIVE_SPHERE) {
		legacy_u16 size = polyinfo_read_word(record, POLYINFO_SPHERE_SIZE_WORD);
		legacy_u16 height = LEGACY_U16_WRAP_ADD(LEGACY_U16_WRAP_SUB(size, size >> 2), size >> 4);
		point_color = height == 1U;
	}
	sdl3_video_mode_primitive_capture(colors);
	if (materials->cga && point_color) {
		sdl3_video_mode_point_begin();
	}
	legacy_u16 color = ghost ? PRERENDER_GHOST_COLOR : SDL3_VIDEO_NATIVE_PRIMARY;
	if (type == RENDER_PRIMITIVE_POLYGON) {
		legacy_u16 pattern_type =
			video_mode_material_value(materials, VIDEO_MODE_MATERIAL_PATTERN_TYPE, material);
		legacy_u16 pattern =
			video_mode_material_value(materials, VIDEO_MODE_MATERIAL_PATTERN_BITS, material);
		legacy_u16 count = record[POLYINFO_VERTEX_COUNT_OFFSET];
		polyinfo_read_points(record, points, count);
		if (ghost) {
			if (pattern_type <= SHAPE3D_PATTERN_TWO_COLOR &&
				(pattern_type != SHAPE3D_PATTERN_MASKED || pattern != 0U)) {
				preRender_default(color, count, points);
			}
		} else if (pattern_type == SHAPE3D_PATTERN_SOLID) {
			preRender_default(color, count, points);
		} else if (pattern_type == SHAPE3D_PATTERN_MASKED && pattern != 0U) {
			if (materials->cga) {
				/* The CGA mask uses its low byte on even rows; the shared
				 * byte rasterizer uses the high byte on those rows. */
				pattern =
					(legacy_u16)((pattern << LEGACY_BYTE_BITS) | (pattern >> LEGACY_BYTE_BITS));
			}
			preRender_patterned(pattern, color, count, points);
		} else if (pattern_type == SHAPE3D_PATTERN_TWO_COLOR) {
			preRender_two_color(pattern, SDL3_VIDEO_NATIVE_SECONDARY, color, count, points);
		}
	} else if (type == RENDER_PRIMITIVE_LINE) {
		preRender_line(polyinfo_read_word(record, POLYINFO_START_X_WORD),
					   polyinfo_read_word(record, POLYINFO_START_Y_WORD),
					   polyinfo_read_word(record, POLYINFO_END_X_WORD),
					   polyinfo_read_word(record, POLYINFO_END_Y_WORD), color);
	} else if (type == RENDER_PRIMITIVE_POINT) {
		legacy_u16 x = polyinfo_read_word(record, POLYINFO_START_X_WORD);
		legacy_u16 y = polyinfo_read_word(record, POLYINFO_START_Y_WORD);
		if (ghost) {
			preRender_line(x, y, x, y, color);
		} else {
			sprite_putpixel_clipped(LEGACY_S16_FROM_BITS(x), LEGACY_S16_FROM_BITS(y), color);
		}
	} else if (type == RENDER_PRIMITIVE_SPHERE) {
		preRender_sphere(LEGACY_S16_FROM_BITS(polyinfo_read_word(record, POLYINFO_START_X_WORD)),
						 LEGACY_S16_FROM_BITS(polyinfo_read_word(record, POLYINFO_START_Y_WORD)),
						 polyinfo_read_word(record, POLYINFO_SPHERE_SIZE_WORD), color);
	} else if (type == RENDER_PRIMITIVE_WHEEL) {
		polyinfo_read_points(record, points, WHEEL_SOURCE_POINT_COUNT);
		preRender_wheel(points, WHEEL_INNER_RADIUS_SCALE, color,
						ghost ? PRERENDER_GHOST_COLOR : SDL3_VIDEO_NATIVE_SECONDARY,
						ghost ? PRERENDER_GHOST_COLOR : SDL3_VIDEO_NATIVE_TERTIARY);
	}
	if (materials->cga && point_color) {
		sdl3_video_mode_point_end();
	}
}

void sdl3_video_mode_render_primitive(const legacy_u8 *record, struct POINT2D *points)
{
	struct VIDEO_MODE_MATERIALS materials;
	materials.cga = sdl3_video_mode_selected == SDL3_VIDEO_MODE_CGA;
	materials.table[VIDEO_MODE_MATERIAL_PRIMARY] =
		materials.cga ? video_mode_cga_primary : video_mode_ega_primary;
	materials.table[VIDEO_MODE_MATERIAL_SECONDARY] =
		materials.cga ? video_mode_cga_secondary : video_mode_ega_secondary;
	materials.table[VIDEO_MODE_MATERIAL_PATTERN_TYPE] =
		materials.cga ? video_mode_cga_pattern_types : video_mode_ega_pattern_types;
	materials.table[VIDEO_MODE_MATERIAL_PATTERN_BITS] =
		materials.cga ? video_mode_cga_pattern_bits : video_mode_ega_pattern_bits;
	void (*saved_sprite)(legacy_s16 *, legacy_s16 *, legacy_u16, legacy_u16, legacy_u16) =
		spritefunc;
	void (*saved_image)(legacy_u16, legacy_u16, legacy_u16, legacy_u16, legacy_u16) = imagefunc;
	legacy_u16 saved_pattern = raster_fill_pattern;
	legacy_u16 saved_alternate = raster_alternate_color;
	video_mode_draw_primitive(record, points, &materials);
	spritefunc = saved_sprite;
	imagefunc = saved_image;
	raster_fill_pattern = saved_pattern;
	raster_alternate_color = saved_alternate;
}

void sdl3_video_mode_material_begin(legacy_u16 material)
{
	if (material >= VIDEO_MODE_MATERIAL_COUNT) {
		material = 0;
	}
	sdl3_video_mode_direct_begin(video_mode_ega_primary[material],
								 video_mode_cga_primary[material]);
}
