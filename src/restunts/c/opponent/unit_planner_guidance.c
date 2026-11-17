#include "unit_planner_internal.h"

#include <math.h>

#define UNIT_STUNT_LOOKAHEAD_BASE 160.0
#define UNIT_STUNT_LOOKAHEAD_SCALE 1.3
#define UNIT_STUNT_LOOKAHEAD_MAXIMUM 260.0
#define UNIT_STUNT_LOOKAHEAD_SAMPLES 16U

void unit_planner_protected_target(const struct UNIT_ROUTE_POINT *points, legacy_u32 count,
								   const struct UNIT_REQUEST *request, struct UNIT_PLAN *plan)
{
	const struct UNIT_ROUTE_POINT *current = &points[request->cursor];
	legacy_f64 x = current->x, y = current->y, z = current->z;
	legacy_u8 height_known = !current->height_unspecified;
	if (request->cursor && points[request->cursor - 1U].route_index == current->route_index) {
		const struct UNIT_ROUTE_POINT *previous = &points[request->cursor - 1U];
		legacy_f64 dx = x - previous->x, dy = y - previous->y, dz = z - previous->z;
		legacy_f64 squared = dx * dx + dy * dy + dz * dz;
		legacy_f64 fraction = squared > UNIT_PLANNER_MIN_DISTANCE
								  ? unit_planner_clamp(((request->unit.x - previous->x) * dx +
														(request->unit.y - previous->y) * dy +
														(request->unit.z - previous->z) * dz) /
														   squared,
													   0.0, 1.0)
								  : 1.0;
		x = previous->x + fraction * dx;
		y = previous->y + fraction * dy;
		z = previous->z + fraction * dz;
	}
	legacy_f64 remaining =
		fmin(UNIT_STUNT_LOOKAHEAD_MAXIMUM,
			 UNIT_STUNT_LOOKAHEAD_BASE + request->unit.speed * UNIT_STUNT_LOOKAHEAD_SCALE);
	/* Project only onto the authoritative incoming segment, then walk forward
	 * in 3-D arc order. Ground-nearest selection can jump between the overlapping
	 * halves of a loop. Targeting the next vertex alone instead makes pursuit
	 * distance collapse and demands abrupt yaw while the car is banked. */
	for (legacy_u32 sample = 0U, index = request->cursor;
		 index < count && sample < UNIT_STUNT_LOOKAHEAD_SAMPLES; index++, sample++) {
		const struct UNIT_ROUTE_POINT *next = &points[index];
		if (next->route_index != current->route_index && next->stunt != UNIT_STUNT_NONE &&
			next->stunt != UNIT_STUNT_RAMP) {
			break;
		}
		/* Keep looking into the adjoining road so a close final stunt vertex
		 * cannot reverse the steering demand before the car has aligned with
		 * its exit. An unspecified ramp height is not its physical surface:
		 * use horizontal pursuit there, as the native ghost driver does. */
		legacy_u8 use_height = height_known && !next->height_unspecified;
		legacy_f64 dx = next->x - x, dy = use_height ? next->y - y : 0.0, dz = next->z - z;
		legacy_f64 span = sqrt(dx * dx + dy * dy + dz * dz);
		legacy_f64 fraction = span > UNIT_PLANNER_MIN_DISTANCE ? fmin(1.0, remaining / span) : 1.0;
		plan->target_x = x + fraction * dx;
		plan->target_y = use_height ? y + fraction * dy : request->unit.y;
		plan->target_z = z + fraction * dz;
		if (span >= remaining) {
			break;
		}
		remaining -= span;
		x = next->x;
		y = next->y;
		z = next->z;
		height_known = !next->height_unspecified;
	}
}
