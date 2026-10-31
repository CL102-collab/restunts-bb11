#include <stddef.h>
#include <limits.h>
#if defined(RESTUNTS_SDL3)
#include <stdlib.h>
#include <string.h>
#endif
#include "../externs.h"
#include "../fileio.h"
#include "../legacy.h"
#include "../memmgr.h"
#include "../shape3d/shape3d.h"
#include "../shape2d/shape2d_internal.h"
#include "../shape3d/shape3d_internal.h"
#include "../shape2d/shape2d.h"
#include "../math_internal.h"
#include "../car/car_model.h"
#include "../projection.h"
#include "../residue.h"
#if defined(RESTUNTS_SDL3)
#undef memcpy
#include "../fatal.h"
#include "../hires/hires.h"
#include "../hires/shape3d_hires.h"
#endif

#include "../shape3d/shape3d_pipeline_internal.h"

#ifdef RESTUNTS_SDL3
static void shape3d_render_hires_primitive(legacy_u32 index,
										   const struct HYPERVISION_SCENE_PRIMITIVE *primitive)
{
	legacy_u16 material = primitive->material;
	legacy_u16 type = primitive->type & ~RENDER_PRIMITIVE_VISUAL_FLAGS;
	legacy_u16 pattern_type = SHAPE3D_PATTERN_SOLID;
	legacy_u16 pattern = 0;
	legacy_u16 color = shape3d_material_value(material_clrlist_ptr_cpy, material);
	legacy_u16 second_color = 0;
	legacy_u16 third_color = 0;
	if (type == RENDER_PRIMITIVE_POLYGON) {
		pattern_type = shape3d_material_value(material_patlist_ptr_cpy, material);
		pattern = shape3d_material_value(material_patlist2_ptr_cpy, material);
		if (pattern_type > SHAPE3D_PATTERN_TWO_COLOR ||
			(pattern_type == SHAPE3D_PATTERN_MASKED && pattern == 0U)) {
			return;
		}
		second_color = shape3d_material_value(material_clrlist2_ptr_cpy, material);
	} else if (type == RENDER_PRIMITIVE_WHEEL) {
		second_color = shape3d_material_value(material_clrlist_ptr_cpy, material + 1U);
		third_color = shape3d_material_value(material_clrlist_ptr_cpy, material + 2U);
	}
	shape3d_hires_render(index, primitive->type, color, second_color, third_color, pattern_type,
						 pattern);
}

legacy_u8 shape3d_hires_render_scene(void)
{
	if (hires_begin(&drawing_sprite) != 0) {
		/* Enhanced presentation has its own depth rasterizer. Drawing the
		 * original 320x200 polygons as well only repeats the complete scene. */
		shape3d_hires_batch_begin();
		for (legacy_u32 index = 0; index < hypervision_scene_count; index++) {
			shape3d_render_hires_primitive(index, &hypervision_scene[index]);
		}
		shape3d_hires_batch_end();
		shape3d_hires_draw_shadows();
		hires_end();
		polyinfo_reset();
		return 1;
	}
	return 0;
}

legacy_u32 shape3d_hires_scene_count(void)
{
	return hires_enabled() ? hypervision_scene_count : polyinfonumpolys;
}
void shape3d_hires_set_half_scale(legacy_s16 half_scale)
{
	shape3d_hires_set_model_scale(half_scale != 0 ? 0.5 : 1);
}
#endif
