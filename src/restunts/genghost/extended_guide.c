#include "extended_guide.h"
#include "../c/extended_track.h"

#define GUIDE_EXTENDED_POSITION_SCALE 64.0
#define GUIDE_EXTENDED_COORDINATE_LIMIT 32767.0

void extended_genghost_guide_height_error(const struct GENGHOST_GUIDE_POINT *point,
										  const struct CARSTATE *car, struct VECTOR *error)
{
	if (!extended_track_active()) {
		return;
	}
	/* Grade transforms tilt the road's local axes. Include measured height
	 * error when projecting a correction into that tilted car frame. */
	legacy_f64 height =
		((legacy_f64)point->position.ly - car->car_position.ly) / GUIDE_EXTENDED_POSITION_SCALE;
	error->y =
		(legacy_s16)(height < -GUIDE_EXTENDED_COORDINATE_LIMIT	? -GUIDE_EXTENDED_COORDINATE_LIMIT
					 : height > GUIDE_EXTENDED_COORDINATE_LIMIT ? GUIDE_EXTENDED_COORDINATE_LIMIT
																: height);
}
