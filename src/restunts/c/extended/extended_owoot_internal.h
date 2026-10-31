#ifndef RESTUNTS_EXTENDED_OWOOT_INTERNAL_H
#define RESTUNTS_EXTENDED_OWOOT_INTERNAL_H

#include "extended_track.h"
#include "../owoot/owoot_road.h"

#define OWOOT_POSITION_SCALE 64L
#define OWOOT_ROUTE_SUBTYPE_MASK 15U
#define OWOOT_ROUTE_REVERSE_FLAG 16U
#define OWOOT_ROUTE_VECTOR_COUNT 3U
#define OWOOT_GATE_SCALE 256L
#define OWOOT_FRACTION_BITS 10U
#define OWOOT_FRACTION_SCALE (1L << OWOOT_FRACTION_BITS)
#define OWOOT_CORKSCREW_GATE_COUNT 5
#define OWOOT_SLALOM_GATE_COUNT 2
#define OWOOT_JUMP_CONNECTION 1
#define OWOOT_TILE_SIZE TRACK_TILE_SIZE
#define OWOOT_GAP_HALF_WIDTH (OWOOT_TILE_SIZE / 2L)
#define OWOOT_SINGLE_GAP_DISTANCE (2L * OWOOT_TILE_SIZE)
#define OWOOT_PIPE_HEIGHT 235
#define OWOOT_PIPE_HALF_WIDTH 115
#define OWOOT_PIPE_ENTRANCE_HEIGHT 171
#define OWOOT_PIPE_ENTRANCE_HALF_WIDTH 115

enum OWOOT_SCRATCH_INDEX {
	OWOOT_SCRATCH_ROUTE_DIRECTION,
	OWOOT_SCRATCH_JUMP_SOURCE,
	OWOOT_SCRATCH_JUMP_TARGET,
	OWOOT_SCRATCH_JUMP_LAUNCH
};

struct OWOOT_POINT {
	legacy_s32 x, y, z;
};

struct OWOOT_BOUNDS {
	legacy_s32 minimum_x, maximum_x, minimum_z, maximum_z;
};

struct OWOOT_TIRES {
	struct VECTOR vertices[CARSTATE_WHEEL_COUNT][OWOOT_WHEEL_VERTEX_COUNT];
	legacy_u16 counts[CARSTATE_WHEEL_COUNT];
};

enum OWOOT_GATE_KIND {
	OWOOT_GATE_FLAT_LANE,
	OWOOT_GATE_ROAD_SURFACE,
	OWOOT_GATE_TUNNEL,
	OWOOT_GATE_CORKSCREW_PHASE,
	OWOOT_GATE_PIPE
};

struct OWOOT_GATE {
	struct OWOOT_POINT center, tangent, edge, normal;
	legacy_s32 lateral_limit, normal_limit;
	legacy_u16 kind;
};

legacy_s32 route_abs(legacy_s32 value);

struct OWOOT_POINT route_position(const struct VECTORLONG *position);

struct OWOOT_POINT route_vector(const struct VECTOR *vector);

struct OWOOT_POINT route_delta(const struct OWOOT_POINT *first, const struct OWOOT_POINT *second);

legacy_s32 route_dot(const struct OWOOT_POINT *first, const struct OWOOT_POINT *second);

legacy_s32 route_maximum_component(const struct OWOOT_POINT *vector);

void route_normalize(struct OWOOT_POINT *vector);

const struct TRACKOBJECT *route_object(legacy_s16 piece);

legacy_s16 route_is_roll_ramp(legacy_s16 piece);

const struct TRKOBJINFO *route_info(legacy_s16 piece);

void route_bounds(legacy_s16 piece, struct OWOOT_BOUNDS *bounds);

legacy_s16 route_contains(const struct OWOOT_BOUNDS *bounds, const struct OWOOT_POINT *position);

legacy_s16 route_has_finite_portal(legacy_s16 piece);

legacy_s16 route_owns_position(legacy_s16 piece, const struct OWOOT_POINT *position);

const struct CSTRK_OBJECT *route_extended_object(legacy_s16 piece);

struct OWOOT_POINT route_extended_origin(const struct CSTRK_OBJECT *placed);

void route_gate(legacy_s16 piece, legacy_s16 index, struct OWOOT_GATE *gate);

legacy_s32 route_gate_distance(const struct OWOOT_GATE *gate, const struct OWOOT_POINT *position);

legacy_s16 route_swept_gate(const struct OWOOT_GATE *gate, const struct OWOOT_TIRES *tires,
							const struct OWOOT_POINT *previous, const struct OWOOT_POINT *current,
							legacy_s16 direction, legacy_s16 minimum_fraction);

legacy_s16 route_previous_tire_before(const struct OWOOT_GATE *gate,
									  const struct OWOOT_TIRES *tires,
									  const struct OWOOT_POINT *motion, legacy_s16 direction);

#endif
