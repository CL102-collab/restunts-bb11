#include "track_geometry.h"
#include "../extended/cstrk.h"
#include "../track/track_objects.h"
#include "../track/trackdata_layout.h"
#include "../game_input.h"

enum EXTENDED_GEOMETRY_ROAD_MATERIAL {
	EXTENDED_MATERIAL_PAVED = 19,
	EXTENDED_MATERIAL_PAVED_MARKING = 21,
	EXTENDED_MATERIAL_SHADED_PAVED = 22,
	EXTENDED_MATERIAL_SHADED_MARKING = 24,
	EXTENDED_MATERIAL_DIRT = 25,
	EXTENDED_MATERIAL_DIRT_MARKING = 27,
	EXTENDED_MATERIAL_ICE = 28,
	EXTENDED_MATERIAL_ICE_MARKING = 30
};

static legacy_u8 geometry_hypervision_materials_enabled(void)
{
#ifdef RESTUNTS_HEADLESS
	return 0;
#else
	return supersight_enabled != 0;
#endif
}

legacy_u8 hires_track_geometry_material(const struct SHAPE3D *source, const legacy_u8 *primitive,
										legacy_u16 surface, legacy_s16 physical_model)
{
	legacy_u16 paint = surface < source->shape3d_numpaints ? surface : 0;
	legacy_u8 material = primitive[2U + paint];
	/* Paint columns can animate a sign while every road face stays paved.
 * HyperVision remaps these unchanged faces while retaining native variants. */
	if (surface != CSTRK_ROAD_PAVED &&
		(source->shape3d_numpaints <= CSTRK_ROAD_ICE ||
		 (geometry_hypervision_materials_enabled() && material == primitive[2U]))) {
		legacy_u8 road_material =
			surface == CSTRK_ROAD_DIRT ? EXTENDED_MATERIAL_DIRT : EXTENDED_MATERIAL_ICE;
		if (material == EXTENDED_MATERIAL_PAVED) {
			material = road_material;
		} else if (material == EXTENDED_MATERIAL_PAVED_MARKING) {
			material = surface == CSTRK_ROAD_DIRT ? EXTENDED_MATERIAL_DIRT_MARKING
												  : EXTENDED_MATERIAL_ICE_MARKING;
		} else if ((physical_model == PHYSICAL_MODEL_CORKSCREW_LEFT_RIGHT ||
					physical_model == PHYSICAL_MODEL_LOOP ||
					physical_model == PHYSICAL_MODEL_PIPE_ENTRANCE ||
					physical_model == PHYSICAL_MODEL_PIPE ||
					physical_model == PHYSICAL_MODEL_HALF_PIPE ||
					physical_model == PHYSICAL_MODEL_OVERPASS) &&
				   material >= EXTENDED_MATERIAL_SHADED_PAVED &&
				   material <= EXTENDED_MATERIAL_SHADED_MARKING) {
			/* Curved driving faces use a shaded road pair and separate
		 * markings. Their structural framing retains its own paint. */
			material = road_material + material - EXTENDED_MATERIAL_SHADED_PAVED;
		} else if ((physical_model == PHYSICAL_MODEL_ELEVATED_ROAD ||
					physical_model == PHYSICAL_MODEL_ELEVATED_SPAN) &&
				   material >= EXTENDED_MATERIAL_SHADED_PAVED &&
				   material < EXTENDED_MATERIAL_SHADED_MARKING) {
			material = road_material;
		}
	}
	return material;
}
