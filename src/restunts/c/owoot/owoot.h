#ifndef RESTUNTS_OWOOT_H
#define RESTUNTS_OWOOT_H

#include "../math.h"

struct CARSTATE;

extern legacy_s16 owoot_enabled;

#define OWOOT_WHEEL_RIM_COUNT 2U
#define OWOOT_WHEEL_RING_COUNT 16U
#define OWOOT_WHEEL_VERTEX_COUNT (OWOOT_WHEEL_RIM_COUNT * OWOOT_WHEEL_RING_COUNT)

void configure_owoot(legacy_s16 argc, legacy_s8 *argv[]);
void owoot_read_wheel_shape(const legacy_u8 far *shape);
void owoot_load_player_wheels(void);
/* Two ordered 16-point rims, with corresponding points joined by the tire tread. */
legacy_u16 owoot_wheel_footprint(const struct CARSTATE *car, legacy_u16 wheel,
								 struct VECTOR output[OWOOT_WHEEL_VERTEX_COUNT]);
void owoot_update_player(struct CARSTATE *car, legacy_s16 car_index);

#endif
