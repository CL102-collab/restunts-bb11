#include "ghost_internal.h"
#include "extended/extended_ghost_runtime.h"
#include "ghost.h"
#ifndef RESTUNTS_DOS16
#include <stdlib.h>
#endif
#include "opponent/opponent.h"
#include "camera.h"
#include "externs.h"
#include "fatal.h"
#include "fileio.h"
#include "platform.h"
#include "race/race_resources.h"
#include "residue.h"
#include "track/trackdata_layout.h"
#include "track/track_objects.h"

#include "ghost_line_internal.h"

#ifdef RESTUNTS_SDL3
#define GHOST_LINE_GRID_SIZE EXTENDED_GHOST_LINE_GRID_SIZE
#define GHOST_LINE_MAP_WIDTH extended_ghost_runtime_line_width()
#define GHOST_LINE_MAP_HEIGHT extended_ghost_runtime_line_height()
#else
#define GHOST_LINE_GRID_SIZE TRACK_GRID_SIZE
#define GHOST_LINE_MAP_WIDTH TRACK_GRID_SIZE
#define GHOST_LINE_MAP_HEIGHT TRACK_GRID_SIZE
#endif
#define GHOST_LINE_TILE_COUNT (GHOST_LINE_GRID_SIZE * GHOST_LINE_GRID_SIZE)
#define GHOST_LINE_WORLD_TILE_SIZE (1L << TRACK_WORLD_TILE_SHIFT)
#define GHOST_LINE_WHEEL_POSITION_SCALE                                                            \
	((1L << (TRACK_WORLD_TILE_SHIFT - TRACK_TILE_POSITION_SHIFT)) / CARSTATE_WHEEL_COUNT)
#define GHOST_LINE_LENGTH_ITERATIONS 8U
#define GHOST_LINE_NO_SEGMENT ((legacy_u32) - 1)
#define GHOST_LINE_INITIAL_CAPACITY 1024U
#define GHOST_LINE_MAX_SEGMENTS 131072UL
/* Five renderer units per foot, with 64 position units per renderer unit:
 * 2 metres * 5 * 64 / 0.3048 = 800000 / 381. Keep fractional distance so
 * rounding each cached coordinate never accumulates along the recording. */
#ifdef RESTUNTS_AMIGA
/* Q24 arithmetic keeps the 68000 port independent of disk-based IEEE libraries.
 * Four signed 16-bit wheel coordinates bound cross products below 2^59 in Q24;
 * normalized lengths stay below 2^60. Split products and long division retain
 * that bound without first multiplying a large remainder by the Q24 scale. */
typedef legacy_s64 ghost_line_scalar;
#define GHOST_LINE_FRACTION_BITS 24U
#define GHOST_LINE_ONE (1LL << GHOST_LINE_FRACTION_BITS)
#define GHOST_LINE_VALUE(value) ((ghost_line_scalar)(value) * GHOST_LINE_ONE)
#define GHOST_LINE_SPACING (800000LL * GHOST_LINE_ONE / 381LL)
#define GHOST_LINE_NORMALIZATION_MIN ((GHOST_LINE_ONE + 999999LL) / 1000000LL)
#define GHOST_LINE_SAMPLE_THRESHOLD GHOST_LINE_SPACING
#define GHOST_LINE_SCALAR_MAX 9223372036854775807LL
static ghost_line_scalar ghost_line_multiply(ghost_line_scalar first, ghost_line_scalar second)
{
	ghost_line_scalar remainder = first % GHOST_LINE_ONE;
	return first / GHOST_LINE_ONE * second + remainder * (second / GHOST_LINE_ONE) +
		   remainder * (second % GHOST_LINE_ONE) / GHOST_LINE_ONE;
}
/* Callers divide by a positive scale, nonzero root, or nonzero length. */
static ghost_line_scalar ghost_line_divide(ghost_line_scalar first, ghost_line_scalar second)
{
	ghost_line_scalar quotient = first / second;
	ghost_line_scalar remainder = first % second;
	if (remainder < 0) {
		remainder = -remainder;
	}
	ghost_line_scalar fraction;
	if (remainder <= (GHOST_LINE_SCALAR_MAX / GHOST_LINE_ONE)) {
		fraction = remainder * GHOST_LINE_ONE / second;
	} else {
		fraction = 0;
		for (legacy_u16 bit = 0; bit < GHOST_LINE_FRACTION_BITS; bit++) {
			fraction *= 2;
			if (remainder >= second - remainder) {
				remainder -= second - remainder;
				fraction++;
			} else {
				remainder += remainder;
			}
		}
	}
	return quotient * GHOST_LINE_ONE + (first < 0 ? -fraction : fraction);
}
static legacy_s32 ghost_line_integer(ghost_line_scalar value)
{
	return value / GHOST_LINE_ONE;
}
#else
typedef legacy_f64 ghost_line_scalar;
#define GHOST_LINE_VALUE(value) ((ghost_line_scalar)(value))
#define GHOST_LINE_SPACING (800000.0 / 381.0)
#define GHOST_LINE_NORMALIZATION_MIN 0.000001
#define GHOST_LINE_SAMPLE_THRESHOLD (GHOST_LINE_SPACING - 0.000001)
static ghost_line_scalar ghost_line_multiply(ghost_line_scalar first, ghost_line_scalar second)
{
	return first * second;
}
static ghost_line_scalar ghost_line_divide(ghost_line_scalar first, ghost_line_scalar second)
{
	return first / second;
}
static legacy_s32 ghost_line_integer(ghost_line_scalar value)
{
	return (legacy_s32)value;
}
#endif
#define GHOST_LINE_HALF_WIDTH GHOST_LINE_VALUE(128)
#define GHOST_LINE_SURFACE_LIFT 128
#define GHOST_LINE_MAX_FRAME_DISTANCE GHOST_LINE_VALUE(131072)

struct GHOST_LINE_VECTOR {
	ghost_line_scalar x, y, z;
};

struct GHOST_LINE_POSE {
	struct GHOST_LINE_VECTOR center;
	struct GHOST_LINE_VECTOR right;
	struct GHOST_LINE_VECTOR up;
	ghost_line_scalar speed;
	legacy_u8 has_orientation;
};

#if defined(RESTUNTS_DOS16) || defined(RESTUNTS_AMIGA)
/* Keep the route outside the constrained DOS/Amiga resource arena, with only
 * tile heads and one iterator piece in RAM. */
static legacy_u16 ghost_line_file;
static legacy_s8 ghost_line_temp_name[GHOST_TEMP_NAME_SIZE];
static struct GHOST_RACING_LINE_SEGMENT ghost_line_cached;
static legacy_u32 ghost_line_cached_index = GHOST_LINE_NO_SEGMENT;
legacy_s16 ghost_seek(legacy_u16 file, legacy_s32 offset);
legacy_u16 ghost_create_file(legacy_s8 *name, legacy_s8 kind);
#else
static struct GHOST_RACING_LINE_SEGMENT *ghost_line_segments;
static legacy_u32 ghost_line_capacity;
#endif
static legacy_u32 ghost_line_count;
static legacy_u32 ghost_line_heads[GHOST_LINE_TILE_COUNT];
static struct {
	struct GHOST_LINE_POSE previous;
	struct GHOST_LINE_POSE sample;
	ghost_line_scalar distance;
	ghost_line_scalar midpoint_speed;
	legacy_u8 initialized;
	legacy_u8 failed;
} ghost_line_builder;

void ghost_line_clear(void)
{
#if defined(RESTUNTS_DOS16) || defined(RESTUNTS_AMIGA)
	if (ghost_line_file != 0) {
		(void)dos_file_close(ghost_line_file);
		(void)dos_file_remove(ghost_line_temp_name);
		ghost_line_file = 0;
	}
	ghost_line_cached_index = GHOST_LINE_NO_SEGMENT;
#else
	free(ghost_line_segments);
	ghost_line_segments = 0;
	ghost_line_capacity = 0;
#endif
	ghost_line_count = 0;
	ghost_line_builder.initialized = 0;
	ghost_line_builder.failed = 0;
	for (legacy_u16 tile = 0; tile < GHOST_LINE_TILE_COUNT; tile++) {
		ghost_line_heads[tile] = GHOST_LINE_NO_SEGMENT;
	}
}

/* Normalize before Newton iteration, keeping its input in [1,3]. This avoids
 * an SDL dependency in the standalone ghost tests and remains finite even
 * for invalid recorded coordinates. Eight iterations exceed double precision. */
static ghost_line_scalar ghost_line_length(struct GHOST_LINE_VECTOR vector)
{
	ghost_line_scalar x = vector.x < 0 ? -vector.x : vector.x;
	ghost_line_scalar y = vector.y < 0 ? -vector.y : vector.y;
	ghost_line_scalar z = vector.z < 0 ? -vector.z : vector.z;
	ghost_line_scalar scale = x > y ? x : y;
	if (z > scale) {
		scale = z;
	}
	if (scale == 0) {
		return 0;
	}
	x = ghost_line_divide(x, scale);
	y = ghost_line_divide(y, scale);
	z = ghost_line_divide(z, scale);
	ghost_line_scalar square =
		ghost_line_multiply(x, x) + ghost_line_multiply(y, y) + ghost_line_multiply(z, z);
	ghost_line_scalar root = GHOST_LINE_VALUE(2);
	for (legacy_u16 iteration = 0; iteration < GHOST_LINE_LENGTH_ITERATIONS; iteration++) {
		root = (root + ghost_line_divide(square, root)) / 2;
	}
	return ghost_line_multiply(root, scale);
}

static legacy_s16 ghost_line_normalize(struct GHOST_LINE_VECTOR *vector)
{
	ghost_line_scalar length = ghost_line_length(*vector);
	if (length < GHOST_LINE_NORMALIZATION_MIN) {
		return 0;
	}
	vector->x = ghost_line_divide(vector->x, length);
	vector->y = ghost_line_divide(vector->y, length);
	vector->z = ghost_line_divide(vector->z, length);
	return 1;
}

static struct GHOST_LINE_VECTOR ghost_line_blend(struct GHOST_LINE_VECTOR first,
												 struct GHOST_LINE_VECTOR second,
												 ghost_line_scalar fraction)
{
	struct GHOST_LINE_VECTOR result;
	result.x = first.x + ghost_line_multiply(second.x - first.x, fraction);
	result.y = first.y + ghost_line_multiply(second.y - first.y, fraction);
	result.z = first.z + ghost_line_multiply(second.z - first.z, fraction);
	return result;
}

static struct GHOST_LINE_POSE ghost_line_pose(const struct CARSTATE *car)
{
	struct GHOST_LINE_POSE pose = {0};
	const struct VECTOR *wheel = car->car_wheel_contact_positions;
	/* Wheels are clockwise: front-left, front-right, rear-right, rear-left. */
	for (legacy_u16 index = 0; index < CARSTATE_WHEEL_COUNT; index++) {
		pose.center.x += GHOST_LINE_VALUE(wheel[index].x) * GHOST_LINE_WHEEL_POSITION_SCALE;
		pose.center.y += GHOST_LINE_VALUE(wheel[index].y) * GHOST_LINE_WHEEL_POSITION_SCALE;
		pose.center.z += GHOST_LINE_VALUE(wheel[index].z) * GHOST_LINE_WHEEL_POSITION_SCALE;
	}
	pose.right.x = GHOST_LINE_VALUE((legacy_s32)wheel[1].x + wheel[2].x - wheel[0].x - wheel[3].x);
	pose.right.y = GHOST_LINE_VALUE((legacy_s32)wheel[1].y + wheel[2].y - wheel[0].y - wheel[3].y);
	pose.right.z = GHOST_LINE_VALUE((legacy_s32)wheel[1].z + wheel[2].z - wheel[0].z - wheel[3].z);
	struct GHOST_LINE_VECTOR forward;
	forward.x = GHOST_LINE_VALUE((legacy_s32)wheel[0].x + wheel[1].x - wheel[2].x - wheel[3].x);
	forward.y = GHOST_LINE_VALUE((legacy_s32)wheel[0].y + wheel[1].y - wheel[2].y - wheel[3].y);
	forward.z = GHOST_LINE_VALUE((legacy_s32)wheel[0].z + wheel[1].z - wheel[2].z - wheel[3].z);
	pose.up.x =
		ghost_line_multiply(forward.y, pose.right.z) - ghost_line_multiply(forward.z, pose.right.y);
	pose.up.y =
		ghost_line_multiply(forward.z, pose.right.x) - ghost_line_multiply(forward.x, pose.right.z);
	pose.up.z =
		ghost_line_multiply(forward.x, pose.right.y) - ghost_line_multiply(forward.y, pose.right.x);
	pose.has_orientation = ghost_line_normalize(&pose.right) && ghost_line_normalize(&pose.up);
	if (!pose.has_orientation) {
		pose.right.x = GHOST_LINE_VALUE(1);
		pose.right.y = pose.right.z = 0;
		pose.up.x = pose.up.z = 0;
		pose.up.y = GHOST_LINE_VALUE(1);
	}
	pose.speed = GHOST_LINE_VALUE(car->car_actual_speed);
	return pose;
}

static struct GHOST_LINE_POSE ghost_line_interpolate(const struct GHOST_LINE_POSE *first,
													 const struct GHOST_LINE_POSE *second,
													 ghost_line_scalar fraction)
{
	struct GHOST_LINE_POSE result;
	result.center = ghost_line_blend(first->center, second->center, fraction);
	result.right = ghost_line_blend(first->right, second->right, fraction);
	result.up = ghost_line_blend(first->up, second->up, fraction);
	if (!ghost_line_normalize(&result.right)) {
		result.right = first->right;
	}
	if (!ghost_line_normalize(&result.up)) {
		result.up = first->up;
	}
	result.speed = first->speed + ghost_line_multiply(second->speed - first->speed, fraction);
	result.has_orientation = 1;
	return result;
}

static legacy_s32 ghost_line_round(ghost_line_scalar coordinate)
{
	return ghost_line_integer(
		coordinate + (coordinate < 0 ? -GHOST_LINE_VALUE(1) / 2 : GHOST_LINE_VALUE(1) / 2));
}

static struct VECTORLONG ghost_line_edge(const struct GHOST_LINE_POSE *pose, ghost_line_scalar side)
{
	struct VECTORLONG result;
	result.lx = ghost_line_round(pose->center.x + ghost_line_multiply(pose->right.x, side) +
								 pose->up.x * GHOST_LINE_SURFACE_LIFT);
	result.ly = ghost_line_round(pose->center.y + ghost_line_multiply(pose->right.y, side) +
								 pose->up.y * GHOST_LINE_SURFACE_LIFT);
	result.lz = ghost_line_round(pose->center.z + ghost_line_multiply(pose->right.z, side) +
								 pose->up.z * GHOST_LINE_SURFACE_LIFT);
	return result;
}

static void ghost_line_append(const struct GHOST_LINE_POSE *end, ghost_line_scalar speed)
{
	ghost_line_scalar x = (ghost_line_builder.sample.center.x + end->center.x) / 2;
	ghost_line_scalar z = (ghost_line_builder.sample.center.z + end->center.z) / 2;
	if (x < 0 || z < 0 ||
		x >= GHOST_LINE_VALUE(GHOST_LINE_MAP_WIDTH * GHOST_LINE_WORLD_TILE_SIZE) ||
		z >= GHOST_LINE_VALUE(GHOST_LINE_MAP_HEIGHT * GHOST_LINE_WORLD_TILE_SIZE) ||
		ghost_line_builder.failed) {
		return;
	}
#if defined(RESTUNTS_DOS16) || defined(RESTUNTS_AMIGA)
	if (ghost_line_count == 0) {
		ghost_line_file = ghost_create_file(ghost_line_temp_name, 'L');
	}
	if (ghost_line_file == 0 || ghost_line_count == GHOST_LINE_MAX_SEGMENTS) {
		ghost_line_clear();
		ghost_line_builder.failed = 1;
		return;
	}
	struct GHOST_RACING_LINE_SEGMENT storage;
	struct GHOST_RACING_LINE_SEGMENT *segment = &storage;
#else
	if (ghost_line_count == ghost_line_capacity) {
		legacy_u32 capacity =
			ghost_line_capacity == 0 ? GHOST_LINE_INITIAL_CAPACITY : ghost_line_capacity * 2U;
		struct GHOST_RACING_LINE_SEGMENT *segments = 0;
		if (capacity <= GHOST_LINE_MAX_SEGMENTS) {
			segments = realloc(ghost_line_segments, capacity * sizeof(*segments));
		}
		if (segments == 0) {
			/* An optional visual must not break replay physics or retain a partial
			 * route after an allocation failure or an unreasonable recording. */
			ghost_line_clear();
			ghost_line_builder.failed = 1;
			return;
		}
		ghost_line_segments = segments;
		ghost_line_capacity = capacity;
	}
	struct GHOST_RACING_LINE_SEGMENT *segment = &ghost_line_segments[ghost_line_count];
#endif
	legacy_u16 east = (legacy_u16)(ghost_line_integer(x) / GHOST_LINE_WORLD_TILE_SIZE);
	legacy_u16 south = GHOST_LINE_MAP_HEIGHT - 1U -
					   (legacy_u16)(ghost_line_integer(z) / GHOST_LINE_WORLD_TILE_SIZE);
	legacy_u16 tile = south * GHOST_LINE_GRID_SIZE + east;
	segment->start_left = ghost_line_edge(&ghost_line_builder.sample, -GHOST_LINE_HALF_WIDTH);
	segment->start_right = ghost_line_edge(&ghost_line_builder.sample, GHOST_LINE_HALF_WIDTH);
	segment->end_left = ghost_line_edge(end, -GHOST_LINE_HALF_WIDTH);
	segment->end_right = ghost_line_edge(end, GHOST_LINE_HALF_WIDTH);
	segment->speed = (legacy_u16)ghost_line_integer(speed + GHOST_LINE_VALUE(1) / 2);
	segment->next = ghost_line_heads[tile];
#if defined(RESTUNTS_DOS16) || defined(RESTUNTS_AMIGA)
	if (dos_file_write(ghost_line_file, segment, sizeof(*segment)) != sizeof(*segment)) {
		ghost_line_clear();
		ghost_line_builder.failed = 1;
		return;
	}
#endif
	ghost_line_heads[tile] = ghost_line_count++;
}

void ghost_line_finish(void)
{
	if (ghost_line_builder.initialized && !ghost_line_builder.failed &&
		ghost_line_builder.distance > GHOST_LINE_VALUE(1)) {
		ghost_line_append(&ghost_line_builder.previous,
						  (ghost_line_builder.sample.speed + ghost_line_builder.previous.speed) /
							  2);
		ghost_line_builder.distance = 0;
	}
}

void ghost_line_record(const struct CARSTATE *car)
{
	if (ghost_line_builder.failed) {
		return;
	}
	struct GHOST_LINE_POSE current = ghost_line_pose(car);
	if (!ghost_line_builder.initialized) {
		ghost_line_builder.previous = current;
		ghost_line_builder.sample = current;
		ghost_line_builder.distance = 0;
		ghost_line_builder.initialized = 1;
		return;
	}
	struct GHOST_LINE_POSE *previous = &ghost_line_builder.previous;
	struct GHOST_LINE_VECTOR delta;
	delta.x = current.center.x - previous->center.x;
	delta.y = current.center.y - previous->center.y;
	delta.z = current.center.z - previous->center.z;
	ghost_line_scalar length = ghost_line_length(delta);
	/* Initialized wheel contacts can coincide until the first physics tick.
	 * Adopt their real orientation even if that tick does not move the car. */
	if (!previous->has_orientation && current.has_orientation) {
		previous->right = current.right;
		previous->up = current.up;
		previous->has_orientation = 1;
		if (ghost_line_builder.distance == 0) {
			ghost_line_builder.sample = *previous;
		}
	}
	if (length > GHOST_LINE_MAX_FRAME_DISTANCE) {
		/* Never draw a chord across a teleport or corrupt wheel coordinates. */
		ghost_line_finish();
		ghost_line_builder.sample = current;
		ghost_line_builder.distance = 0;
	} else if (length > 0) {
		ghost_line_scalar consumed = 0;
		while (consumed < length && !ghost_line_builder.failed) {
			ghost_line_scalar remaining = GHOST_LINE_SPACING - ghost_line_builder.distance;
			ghost_line_scalar step = length - consumed < remaining ? length - consumed : remaining;
			if (ghost_line_builder.distance <= GHOST_LINE_SPACING / 2 &&
				ghost_line_builder.distance + step >= GHOST_LINE_SPACING / 2) {
				ghost_line_scalar fraction = ghost_line_divide(
					consumed + GHOST_LINE_SPACING / 2 - ghost_line_builder.distance, length);
				ghost_line_builder.midpoint_speed =
					previous->speed +
					ghost_line_multiply(current.speed - previous->speed, fraction);
			}
			consumed += step;
			ghost_line_builder.distance += step;
			if (ghost_line_builder.distance >= GHOST_LINE_SAMPLE_THRESHOLD) {
				struct GHOST_LINE_POSE sample =
					ghost_line_interpolate(previous, &current, ghost_line_divide(consumed, length));
				ghost_line_append(&sample, ghost_line_builder.midpoint_speed);
				ghost_line_builder.sample = sample;
				ghost_line_builder.distance = 0;
			}
		}
	}
	ghost_line_builder.previous = current;
}

static const struct GHOST_RACING_LINE_SEGMENT *ghost_line_at(legacy_u32 index)
{
	if (index >= ghost_line_count) {
		return 0;
	}
#if defined(RESTUNTS_DOS16) || defined(RESTUNTS_AMIGA)
	if (ghost_line_cached_index != index) {
		if (!ghost_seek(ghost_line_file, (legacy_s32)(index * sizeof(ghost_line_cached))) ||
			dos_file_read(ghost_line_file, &ghost_line_cached, sizeof(ghost_line_cached)) !=
				sizeof(ghost_line_cached)) {
			ghost_line_clear();
			ghost_line_builder.failed = 1;
			return 0;
		}
		ghost_line_cached_index = index;
	}
	return &ghost_line_cached;
#else
	return &ghost_line_segments[index];
#endif
}

const struct GHOST_RACING_LINE_SEGMENT *ghost_racing_line_first(legacy_s16 east, legacy_s16 south)
{
	if (!ghost_is_active() || east < 0 || south < 0 || east >= (legacy_s16)GHOST_LINE_MAP_WIDTH ||
		south >= (legacy_s16)GHOST_LINE_MAP_HEIGHT || ghost_line_count == 0) {
		return 0;
	}
	legacy_u32 index = ghost_line_heads[south * GHOST_LINE_GRID_SIZE + east];
	return ghost_line_at(index);
}

const struct GHOST_RACING_LINE_SEGMENT *
ghost_racing_line_next(const struct GHOST_RACING_LINE_SEGMENT *segment)
{
	if (!ghost_is_active() || segment == 0 || segment->next >= ghost_line_count) {
		return 0;
	}
	return ghost_line_at(segment->next);
}

legacy_u8 ghost_racing_line_color(legacy_u16 current_speed, legacy_u16 recorded_speed)
{
	legacy_s32 difference = (legacy_s32)current_speed - recorded_speed;
	if (difference > 3 * 256) {
		return GHOST_RACING_LINE_RED;
	}
	if (difference < -(3 * 256)) {
		return GHOST_RACING_LINE_GREEN;
	}
	return GHOST_RACING_LINE_YELLOW;
}
