#ifndef RESTUNTS_HIRES_SHOWROOM_H
#define RESTUNTS_HIRES_SHOWROOM_H

#include "presentation.h"
#include "../math.h"

struct TRANSFORMEDSHAPE3D;
struct SHAPE2D;

struct HIRES_SHOWROOM {
	struct VECTOR shadow_position;
	struct PRESENTATION_CLOCK presentation_clock;
	legacy_u64 rotation_time;
	legacy_u64 input_time;
	legacy_s16 input_delta;
	legacy_u8 portrait_dirty, background_dirty;
};

void hires_showroom_initialize(struct HIRES_SHOWROOM *showroom);
void hires_showroom_load_car(struct HIRES_SHOWROOM *showroom,
							 const struct TRANSFORMEDSHAPE3D *transformed);
void hires_showroom_reset_clock(struct HIRES_SHOWROOM *showroom);
legacy_u8 hires_showroom_visual_rotation(struct HIRES_SHOWROOM *showroom, legacy_s16 rotation,
										 legacy_u8 start, legacy_s16 *visual_rotation);
void hires_showroom_shadows(struct HIRES_SHOWROOM *showroom, struct TRANSFORMEDSHAPE3D *transformed,
							legacy_s16 visual_rotation);
void hires_showroom_car_bounds(struct RECTANGLE *rectangle);
void hires_showroom_background_bounds(struct HIRES_SHOWROOM *showroom, struct RECTANGLE *rectangle);
void hires_showroom_defer_render(legacy_u8 *deferred);
void hires_showroom_background(struct HIRES_SHOWROOM *showroom, const struct SHAPE2D *background);
legacy_u8 hires_showroom_redraw_portrait(const struct HIRES_SHOWROOM *showroom, legacy_u8 redraw);
void hires_showroom_portrait(struct HIRES_SHOWROOM *showroom, legacy_u16 opponent,
							 struct RECTANGLE *rectangle);
void hires_showroom_record_rotation(struct HIRES_SHOWROOM *showroom);
legacy_u8 hires_showroom_input(struct HIRES_SHOWROOM *showroom, legacy_s16 rotation_delta,
							   legacy_s16 *input_delta);
void hires_showroom_shortcut(struct HIRES_SHOWROOM *showroom, legacy_u16 input, legacy_u8 opponent);
void hires_showroom_release(void);

#endif
