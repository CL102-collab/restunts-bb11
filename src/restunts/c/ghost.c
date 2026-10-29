#include "ghost.h"
#ifndef RESTUNTS_DOS16
#include <stdlib.h>
#endif
#include "opponent.h"
#include "camera.h"
#include "externs.h"
#include "fatal.h"
#include "fileio.h"
#include "platform.h"
#include "race_resources.h"
#include "residue.h"
#include "trackdata_layout.h"
#include "track_objects.h"
#ifdef RESTUNTS_SDL3
#include "frame_interpolation.h"
#include "extended_ghost.h"
#include "extended_track.h"
#include "extended_replay_runtime.h"
#endif

#define GHOST_TEMP_NAME_SIZE 13U
#define GHOST_TEMP_NAME_DIGITS 6U
#define GHOST_TEMP_NAME_DIGIT_OFFSET 2U
#define GHOST_TEMP_NAME_CANDIDATE_COUNT 1000000UL
#define GHOST_REPLAY_PATH_OVERHEAD (sizeof(".rpl") + sizeof("/") - 1U)
#define GHOST_COPY_BUFFER_SIZE 256U

struct GHOST_POSE {
	struct CARSTATE car;
	struct GHOST_CAMERA_STATE camera;
};

static struct GAMEINFO ghost_config;
static struct GHOST_POSE ghost_pose;
static struct SIMD ghost_simd;
static legacy_u16 ghost_file;
static legacy_s8 ghost_temp_name[GHOST_TEMP_NAME_SIZE];
static legacy_s16 ghost_active;
static legacy_s16 ghost_cleanup_registered;
static legacy_s16 ghost_pose_valid;
#ifdef RESTUNTS_SDL3
static legacy_u32 ghost_pose_frame;
static struct EXTENDED_GHOST_REPLAY ghost_extended;
#else
static legacy_u16 ghost_pose_frame;
#endif
static legacy_u32 ghost_live_frame;
static legacy_u16 ghost_live_frame_rate;
#ifdef RESTUNTS_SDL3
static struct {
	struct GHOST_POSE lower;
	struct GHOST_POSE upper;
	legacy_u32 lower_frame;
	legacy_u32 upper_frame;
	legacy_u8 valid;
} ghost_render_cache;
#endif

#ifdef RESTUNTS_SDL3
#define GHOST_LINE_GRID_SIZE EXTENDED_TRACK_MAX_DIMENSION
#define GHOST_LINE_MAP_WIDTH extended_track_width()
#define GHOST_LINE_MAP_HEIGHT extended_track_height()
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
static legacy_s16 ghost_seek(legacy_u16 file, legacy_s32 offset);
static legacy_u16 ghost_create_file(legacy_s8 *name, legacy_s8 kind);
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

static void ghost_line_clear(void)
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

static void ghost_line_finish(void)
{
	if (ghost_line_builder.initialized && !ghost_line_builder.failed &&
		ghost_line_builder.distance > GHOST_LINE_VALUE(1)) {
		ghost_line_append(&ghost_line_builder.previous,
						  (ghost_line_builder.sample.speed + ghost_line_builder.previous.speed) /
							  2);
		ghost_line_builder.distance = 0;
	}
}

static void ghost_line_record(const struct CARSTATE *car)
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

static legacy_s16 ghost_seek(legacy_u16 file, legacy_s32 offset)
{
	(void)dos_file_error();
	(void)dos_file_seek(file, offset, DOS_FILE_SEEK_BEGIN);
	return dos_file_error() == 0;
}

void ghost_end_race(void)
{
	if (ghost_active != 0 && gameconfig.game_opponenttype == 0) {
		followOpponentFlag = 0;
	}
	ghost_active = 0;
	ghost_pose_valid = 0;
#ifdef RESTUNTS_SDL3
	ghost_render_cache.valid = 0;
#endif
	ghost_line_clear();
}

void ghost_clear(void)
{
	ghost_end_race();
#ifdef RESTUNTS_SDL3
	extended_ghost_free(&ghost_extended);
#endif
	if (ghost_file != 0) {
		(void)dos_file_close(ghost_file);
		ghost_file = 0;
	}
	if (ghost_temp_name[0] != 0) {
		(void)dos_file_remove(ghost_temp_name);
		ghost_temp_name[0] = 0;
	}
}

static void far ghost_cleanup(void)
{
	ghost_clear();
}

legacy_s16 ghost_is_selected(void)
{
	return ghost_file != 0;
}

legacy_s16 ghost_is_active(void)
{
	return ghost_active != 0 && gameconfig.game_opponenttype == 0;
}

struct CARSTATE *ghost_car_state(void)
{
	return ghost_is_active() && ghost_pose_valid ? &ghost_pose.car : 0;
}

const struct GHOST_CAMERA_STATE *ghost_camera_state(void)
{
	return ghost_is_active() && ghost_pose_valid ? &ghost_pose.camera : 0;
}

static legacy_u32 ghost_recorded_frames(void)
{
#ifdef RESTUNTS_SDL3
	if (ghost_extended.bytes) {
		return ghost_extended.view.metadata.frame_count;
	}
#endif
	return ghost_config.game_recordedframes;
}

static legacy_u32 ghost_pose_offset(void)
{
#ifdef RESTUNTS_SDL3
	if (ghost_extended.bytes) {
		return 0;
	}
#endif
	return replay_file_size(ghost_config.game_recordedframes);
}

static legacy_s16 ghost_previous_camera_coordinate(legacy_s16 previous, legacy_s16 current)
{
	legacy_s32 delta = LEGACY_S16_WRAP_SUB(current, previous);
	delta = delta * ghost_config.game_framespersec / ghost_live_frame_rate;
	return LEGACY_S16_WRAP_SUB(current, LEGACY_S16_FROM_BITS((legacy_u16)delta));
}

void ghost_adjust_camera_motion(struct VECTOR *previous, const struct VECTOR *current)
{
	if (!ghost_is_active() || !ghost_pose_valid || ghost_live_frame_rate == 0) {
		return;
	}
	if (ghost_pose_frame == 0 || (legacy_u64)ghost_live_frame * ghost_config.game_framespersec >
									 (legacy_u64)ghost_recorded_frames() * ghost_live_frame_rate) {
		*previous = *current;
		return;
	}
	previous->x = ghost_previous_camera_coordinate(previous->x, current->x);
	previous->y = ghost_previous_camera_coordinate(previous->y, current->y);
	previous->z = ghost_previous_camera_coordinate(previous->z, current->z);
}

const struct SIMD *ghost_car_simd(void)
{
	return ghost_is_active() ? &ghost_simd : 0;
}

legacy_u8 ghost_car_material(void)
{
	return (legacy_u8)ghost_config.game_playermaterial;
}

const legacy_s8 *ghost_car_id(void)
{
	return ghost_config.game_playercarid;
}

/* The DOS create API truncates existing files, so only use an unused private
 * name. No persistent arena allocation may pin the opponent menu's resources. */
static legacy_u16 ghost_create_file(legacy_s8 *name, legacy_s8 kind)
{
	for (legacy_u32 candidate = 0; candidate < GHOST_TEMP_NAME_CANDIDATE_COUNT; candidate++) {
		legacy_u32 digits = candidate;
		strcpy(name, "GH000000.TMP");
		name[1] = kind;
		for (legacy_u16 index = 0; index < GHOST_TEMP_NAME_DIGITS; index++) {
			name[GHOST_TEMP_NAME_DIGIT_OFFSET + GHOST_TEMP_NAME_DIGITS - 1U - index] =
				(legacy_s8)('0' + digits % 10UL);
			digits /= 10UL;
		}
		if (dos_file_find_first(name) == 0) {
			return dos_file_open(name, DOS_FILE_CREATE);
		}
	}
	return 0;
}

static legacy_s16 ghost_valid_car_id(const legacy_s8 *id)
{
	for (legacy_u16 index = 0; index < REPLAY_CAR_ID_SIZE; index++) {
		legacy_u8 character = (legacy_u8)id[index];
		if (character <= ' ' || character == '/' || character == '\\' || character == ':' ||
			character == '*' || character == '?' || character == '"' || character == '<' ||
			character == '>' || character == '|') {
			return 0;
		}
	}
	return 1;
}

static legacy_s16 ghost_valid_config(const struct GAMEINFO *config)
{
	if (config->game_framespersec != GAME_FRAME_RATE_LOW &&
		config->game_framespersec != GAME_FRAME_RATE_NORMAL) {
		return 0;
	}
	if (config->game_recordedframes > TRACKDATA_REPLAY_INPUT_BUFFER_SIZE ||
		config->game_opponenttype < 0 || config->game_opponenttype > (legacy_s16)OPPONENT_LAST ||
		config->game_playertransmission < TRANSMISSION_MANUAL ||
		config->game_playertransmission > TRANSMISSION_AUTOMATIC ||
		!ghost_valid_car_id(config->game_playercarid)) {
		return 0;
	}
	return config->game_opponenttype == 0 || ghost_valid_car_id(config->game_opponentcarid);
}

static legacy_s16 ghost_copy_input(legacy_u16 source, legacy_u16 destination, legacy_u16 length)
{
	legacy_u8 buffer[GHOST_COPY_BUFFER_SIZE];
	while (length != 0) {
		legacy_u16 count = length > sizeof(buffer) ? sizeof(buffer) : length;
		if (dos_file_read(source, buffer, count) != count ||
			dos_file_write(destination, buffer, count) != count) {
			return 0;
		}
		length -= count;
	}
	return 1;
}

legacy_s16 ghost_select_replay(const legacy_s8 *directory, const legacy_s8 *name)
{
	legacy_s8 path[REPLAY_FILENAME_SIZE];
	legacy_u16 directory_length = directory == 0 ? 0 : strlen(directory);
	if ((legacy_u32)directory_length + strlen(name) + GHOST_REPLAY_PATH_OVERHEAD > sizeof(path)) {
		return 1;
	}
	file_build_path(directory, name, ".rpl", path);
	legacy_u16 source = dos_file_open(path, DOS_FILE_OPEN_EXISTING);
	if (source == 0) {
		return 1;
	}
	legacy_u8 header[REPLAY_INPUT_OFFSET];
	struct GAMEINFO config;
	if (dos_file_read(source, header, sizeof(header)) != sizeof(header)) {
		(void)dos_file_close(source);
		return 1;
	}
	replay_gameinfo_decode(&config, header);
	if (!ghost_valid_config(&config)) {
		(void)dos_file_close(source);
		return 1;
	}
	config.game_trackname[REPLAY_TRACK_NAME_SIZE - 1U] = 0;
	legacy_s8 temp_name[GHOST_TEMP_NAME_SIZE];
	legacy_u16 destination = ghost_create_file(temp_name, 'H');
	if (destination == 0) {
		(void)dos_file_close(source);
		return 1;
	}
	legacy_s16 copied = dos_file_write(destination, header, sizeof(header)) == sizeof(header) &&
						ghost_copy_input(source, destination, config.game_recordedframes);
	(void)dos_file_close(source);
	if (!copied) {
		(void)dos_file_close(destination);
		(void)dos_file_remove(temp_name);
		return 1;
	}

	/* Preserve the previous track in the header scratch space until setup and
	 * storage both succeed. Track setup canonicalizes some large tile IDs. */
	for (legacy_u16 index = 0; index < REPLAY_TRACK_SIZE; index++) {
		legacy_u8 previous = track_element_map[index];
		track_element_map[index] = header[REPLAY_GAMEINFO_SIZE + index];
		header[REPLAY_GAMEINFO_SIZE + index] = previous;
	}
	if (track_setup() != 0 || !ghost_seek(destination, REPLAY_GAMEINFO_SIZE) ||
		dos_file_write(destination, track_element_map, REPLAY_TRACK_SIZE) != REPLAY_TRACK_SIZE) {
		for (legacy_u16 index = 0; index < REPLAY_TRACK_SIZE; index++) {
			track_element_map[index] = header[REPLAY_GAMEINFO_SIZE + index];
		}
		(void)track_setup();
		(void)dos_file_close(destination);
		(void)dos_file_remove(temp_name);
		return 1;
	}

	ghost_clear();
	ghost_file = destination;
	strcpy(ghost_temp_name, temp_name);
	ghost_config = config;
	if (!ghost_cleanup_registered) {
		add_exit_handler(ghost_cleanup);
		ghost_cleanup_registered = 1;
	}
	for (legacy_u16 index = 0; index < REPLAY_TRACK_NAME_SIZE; index++) {
		gameconfig.game_trackname[index] = ghost_config.game_trackname[index];
	}
	return 0;
}

#ifdef RESTUNTS_SDL3
legacy_s16 ghost_select_replay_format(const legacy_s8 *directory, const legacy_s8 *name,
									  const legacy_s8 *extension)
{
	if (strcmp(extension, ".csrpl") != 0) {
		uint8_t *previous = NULL;
		size_t previous_size = 0;
		if (extended_track_active()) {
			if (!cstrk_encode(extended_track_model(), &previous, &previous_size, NULL, 0)) {
				return 1;
			}
			extended_track_unload();
		}
		legacy_s16 result = ghost_select_replay(directory, name);
		if (result != 0 && previous) {
			(void)extended_track_load(previous, previous_size, NULL, 0);
			(void)track_setup();
		} else if (result == 0) {
			extended_replay_runtime_clear();
		}
		free(previous);
		return result;
	}
	legacy_s8 path[REPLAY_FILENAME_SIZE];
	if ((directory ? strlen(directory) : 0) + strlen(name) + sizeof(".csrpl/") > sizeof(path)) {
		return 1;
	}
	file_build_path(directory, name, ".csrpl", path);
	struct EXTENDED_GHOST_REPLAY candidate = {0};
	struct GAMEINFO config;
	if (!extended_ghost_read(path, &candidate, &config)) {
		return 1;
	}
	struct GAMEINFO checked = config;
	checked.game_recordedframes = 0;
	if (!ghost_valid_config(&checked)) {
		extended_ghost_free(&candidate);
		return 1;
	}
	legacy_s8 temp_name[GHOST_TEMP_NAME_SIZE];
	legacy_u16 destination = ghost_create_file(temp_name, 'H');
	if (!destination) {
		extended_ghost_free(&candidate);
		return 1;
	}
	uint8_t *previous = NULL;
	size_t previous_size = 0;
	legacy_u8 legacy_track[REPLAY_TRACK_SIZE];
	if (extended_track_active()) {
		if (!cstrk_encode(extended_track_model(), &previous, &previous_size, NULL, 0)) {
			dos_file_close(destination);
			dos_file_remove(temp_name);
			extended_ghost_free(&candidate);
			return 1;
		}
	} else {
		memcpy(legacy_track, track_element_map, sizeof(legacy_track));
	}
	int loaded = extended_track_load(candidate.view.track_data, candidate.view.track_size, NULL, 0);
	if (!loaded || track_setup() != 0) {
		if (loaded) {
			if (previous) {
				(void)extended_track_load(previous, previous_size, NULL, 0);
			} else {
				extended_track_unload();
				memcpy(track_element_map, legacy_track, sizeof(legacy_track));
				init_row_tables();
			}
			(void)track_setup();
		}
		free(previous);
		dos_file_close(destination);
		dos_file_remove(temp_name);
		extended_ghost_free(&candidate);
		return 1;
	}
	free(previous);
	ghost_clear();
	ghost_file = destination;
	strcpy(ghost_temp_name, temp_name);
	ghost_extended = candidate;
	ghost_config = config;
	extended_replay_runtime_clear();
	if (!ghost_cleanup_registered) {
		add_exit_handler(ghost_cleanup);
		ghost_cleanup_registered = 1;
	}
	memcpy(gameconfig.game_trackname, config.game_trackname, REPLAY_TRACK_NAME_SIZE);
	file_warn_replay_version(ghost_extended.view.metadata.version);
	file_warn_replay_driving_rules(ghost_extended.view.metadata.driving_rules);
	return 0;
}
#endif

void ghost_check_track(void)
{
	if (!ghost_is_selected()) {
		return;
	}
	for (legacy_u16 index = 0; index < REPLAY_TRACK_NAME_SIZE; index++) {
		if (gameconfig.game_trackname[index] != ghost_config.game_trackname[index]) {
			ghost_clear();
			return;
		}
	}
#ifdef RESTUNTS_SDL3
	if (ghost_extended.bytes) {
		if (!extended_ghost_matches_track(&ghost_extended)) {
			ghost_clear();
		}
		return;
	}
	if (extended_track_active()) {
		ghost_clear();
		return;
	}
#endif
	if (!ghost_seek(ghost_file, REPLAY_GAMEINFO_SIZE)) {
		ghost_clear();
		return;
	}
	legacy_u8 buffer[GHOST_COPY_BUFFER_SIZE];
	legacy_u16 offset = 0;
	while (offset < REPLAY_TRACK_SIZE) {
		legacy_u16 remaining = REPLAY_TRACK_SIZE - offset;
		legacy_u16 count = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
		if (dos_file_read(ghost_file, buffer, count) != count) {
			ghost_clear();
			return;
		}
		for (legacy_u16 index = 0; index < count; index++) {
			if (buffer[index] != track_element_map[offset + index]) {
				ghost_clear();
				return;
			}
		}
		offset += count;
	}
}

/* A replay stores controls rather than positions. Reconstruct its original
 * race (including its AI) before starting the independent live race. Poses are
 * kept on disk so the full recording buffer fits conventional DOS memory. */
legacy_s16 ghost_prepare_race(void)
{
	ghost_end_race();
	ghost_check_track();
	if (!ghost_is_selected() || gameconfig.game_opponenttype != 0) {
		return 0;
	}
#ifdef RESTUNTS_SDL3
	if (ghost_extended.bytes &&
		ghost_recorded_frames() > (legacy_u32)LEGACY_S32_MAX / sizeof(struct GHOST_POSE) - 1U) {
		return 1;
	}
	if (!ghost_extended.bytes)
#endif
		if (!ghost_seek(ghost_file, REPLAY_INPUT_OFFSET) ||
			dos_file_read(ghost_file, replay_input_buffer, ghost_config.game_recordedframes) !=
				ghost_config.game_recordedframes) {
			return 1;
		}
	if (!ghost_seek(ghost_file, (legacy_s32)ghost_pose_offset())) {
		return 1;
	}
	struct GAMEINFO saved_config = gameconfig;
	struct GAMESTATE saved_state = state;
	struct LEGACY_EXECUTION_RESIDUE saved_residue = legacy_execution_residue;
	legacy_s16 saved_render_headings = legacy_render_player_headings_active;
	legacy_u16 saved_frame_rate = framespersec;
	legacy_u8 saved_mode = game_replay_mode;
	legacy_s8 saved_is_in_replay = is_in_replay;
	legacy_s8 saved_recording_flags = replay_recording_flags;
	legacy_u16 saved_elapsed_time1 = elapsed_time1;
	legacy_u16 saved_elapsed_time2 = elapsed_time2;
	legacy_s16 saved_checkpoint_interval = checkpoint_frame_interval;
	legacy_s16 saved_timer_ticks = timer_ticks_per_frame;
	legacy_s8 *saved_steering_table = steerWhlRespTable_ptr;
	legacy_u8 saved_frame_countdown = frame_callback_countdown;
	legacy_u8 saved_slow_countdown = slow_replay_countdown;
	legacy_u8 saved_exit_request = race_exit_request;
	legacy_u8 saved_start_sequence = race_start_sequence_state;
	legacy_s16 saved_flag_animation = start_flag_animation;
	legacy_s8 seed[GAMESTATE_RANDOM_SEED_SIZE];
	get_kevinrandom_seed(seed);
#ifdef RESTUNTS_SDL3
	struct EXTENDED_REPLAY_SUSPENDED saved_extended;
	extended_replay_runtime_suspend(&saved_extended);
	if (ghost_extended.bytes && !extended_replay_runtime_load(&ghost_extended.view)) {
		extended_replay_runtime_resume(&saved_extended);
		return 1;
	}
#endif
	gameconfig = ghost_config;
	game_replay_mode = REPLAY_MODE_PLAYBACK;
	/* Direct impact sounds and result totals have separate replay guards. */
	is_in_replay = 1;
	replay_recording_flags = REPLAY_RECORDING_RESTARTABLE_FLAG;
	framespersec = ghost_config.game_framespersec;
	struct LEGACY_EXECUTION_RESIDUE empty_residue = {0};
	legacy_execution_residue = empty_residue;
	legacy_render_player_headings_active = 0;
	ghost_load_simulation_resources();
	ghost_simd = simd_player;
	init_game_state(GAMESTATE_INIT_RESET_CHECKPOINTS);
	legacy_s16 succeeded = 1;
	for (legacy_u32 frame = 0; frame <= ghost_recorded_frames(); frame++) {
		if (frame != 0) {
			update_gamestate_silent();
		}
		ghost_pose.car = state.playerstate;
		ghost_line_record(&state.playerstate);
		ghost_pose.camera.follow_position = state.game_follow_camera_position[PLAYER_CAR_INDEX];
		ghost_pose.camera.previous_position = state.game_player_camera_previous;
		ghost_pose.camera.trackside_index = state.game_trackside_camera_index[PLAYER_CAR_INDEX];
#ifdef RESTUNTS_SDL3
		ghost_pose.camera.frame =
			ghost_extended.bytes ? extended_replay_frame() : (legacy_u16)state.game_frame;
		ghost_pose.camera.crash_frame = ghost_extended.bytes ? extended_replay_clocks.player_end
															 : (legacy_u16)state.game_pEndFrame;
#else
		ghost_pose.camera.frame = state.game_frame;
		ghost_pose.camera.crash_frame = state.game_pEndFrame;
#endif
		if (dos_file_write(ghost_file, &ghost_pose, sizeof(ghost_pose)) != sizeof(ghost_pose)) {
			succeeded = 0;
			break;
		}
	}
	ghost_free_simulation_resources();
	gameconfig = saved_config;
	state = saved_state;
	framespersec = saved_frame_rate;
	game_replay_mode = saved_mode;
	is_in_replay = saved_is_in_replay;
	replay_recording_flags = saved_recording_flags;
	elapsed_time1 = saved_elapsed_time1;
	elapsed_time2 = saved_elapsed_time2;
	checkpoint_frame_interval = saved_checkpoint_interval;
	timer_ticks_per_frame = saved_timer_ticks;
	steerWhlRespTable_ptr = saved_steering_table;
	frame_callback_countdown = saved_frame_countdown;
	slow_replay_countdown = saved_slow_countdown;
	race_exit_request = saved_exit_request;
	race_start_sequence_state = saved_start_sequence;
	start_flag_animation = saved_flag_animation;
	legacy_execution_residue = saved_residue;
	legacy_render_player_headings_active = saved_render_headings;
	init_kevinrandom(seed);
#ifdef RESTUNTS_SDL3
	extended_replay_runtime_resume(&saved_extended);
#endif
	if (!succeeded) {
		ghost_line_clear();
		return 1;
	}
	ghost_line_finish();
	ghost_active = 1;
	ghost_update(0, framespersec);
	return ghost_pose_valid ? 0 : 1;
}

void ghost_update(legacy_u32 frame, legacy_u16 live_frame_rate)
{
	if (!ghost_is_active() || live_frame_rate == 0) {
		return;
	}
	legacy_u64 scaled_frame = (legacy_u64)frame * ghost_config.game_framespersec / live_frame_rate;
	legacy_u32 target =
		scaled_frame > ghost_recorded_frames() ? ghost_recorded_frames() : (legacy_u32)scaled_frame;
	if (ghost_pose_valid && target == ghost_pose_frame) {
		ghost_live_frame = frame;
		ghost_live_frame_rate = live_frame_rate;
		return;
	}
	legacy_u32 offset = ghost_pose_offset() + target * sizeof(struct GHOST_POSE);
	if (!ghost_seek(ghost_file, (legacy_s32)offset) ||
		dos_file_read(ghost_file, &ghost_pose, sizeof(ghost_pose)) != sizeof(ghost_pose)) {
		ghost_end_race();
		return;
	}
	ghost_pose.car.car_sound_flags = CAR_SOUND_NONE;
	ghost_pose_frame = target;
	ghost_pose_valid = 1;
	ghost_live_frame = frame;
	ghost_live_frame_rate = live_frame_rate;
}

#ifdef RESTUNTS_SDL3
static legacy_s16 ghost_read_render_pose(legacy_u32 frame, struct GHOST_POSE *pose)
{
	if (ghost_pose_valid != 0 && ghost_pose_frame == frame) {
		*pose = ghost_pose;
		return 1;
	}
	if (ghost_render_cache.valid != 0) {
		if (ghost_render_cache.lower_frame == frame) {
			*pose = ghost_render_cache.lower;
			return 1;
		}
		if (ghost_render_cache.upper_frame == frame) {
			*pose = ghost_render_cache.upper;
			return 1;
		}
	}
	legacy_u32 offset = ghost_pose_offset() + (legacy_u32)frame * sizeof(struct GHOST_POSE);
	return ghost_seek(ghost_file, (legacy_s32)offset) &&
		   dos_file_read(ghost_file, pose, sizeof(*pose)) == sizeof(*pose);
}

legacy_s16 ghost_sample_render_pose(legacy_u32 live_frame, legacy_u16 live_frame_rate,
									legacy_u32 lag, struct CARSTATE *car,
									struct GHOST_CAMERA_STATE *camera)
{
	if (!ghost_is_active() || !ghost_pose_valid || live_frame_rate == 0 || car == 0 ||
		camera == 0) {
		return 0;
	}
	legacy_u64 live_time = (legacy_u64)live_frame * FRAME_INTERPOLATION_ONE;
	live_time = live_time > lag ? live_time - lag : 0;
	legacy_u64 source_time = live_time * ghost_config.game_framespersec / live_frame_rate;
	legacy_u64 end_time = (legacy_u64)ghost_recorded_frames() * FRAME_INTERPOLATION_ONE;
	if (source_time > end_time) {
		source_time = end_time;
	}
	legacy_u32 lower_frame = (legacy_u32)(source_time / FRAME_INTERPOLATION_ONE);
	legacy_u32 fraction = (legacy_u32)(source_time % FRAME_INTERPOLATION_ONE);
	legacy_u32 upper_frame = lower_frame + (fraction != 0);
	struct GHOST_POSE lower, upper;
	if (!ghost_read_render_pose(lower_frame, &lower)) {
		return 0;
	}
	if (lower_frame == upper_frame) {
		upper = lower;
	} else if (!ghost_read_render_pose(upper_frame, &upper)) {
		return 0;
	}
	/* Commit a complete bracket only. Failed reads leave the authoritative pose
	 * and the caller's output untouched, so presentation can fall back safely. */
	ghost_render_cache.lower = lower;
	ghost_render_cache.upper = upper;
	ghost_render_cache.lower_frame = lower_frame;
	ghost_render_cache.upper_frame = upper_frame;
	ghost_render_cache.valid = 1;
	if (lower.car.car_crashBmpFlag != upper.car.car_crashBmpFlag ||
		upper.car.car_crashBmpFlag != ghost_pose.car.car_crashBmpFlag) {
		/* A known future impact must not move the rendered car before the live
		 * ghost confirms it. A newly confirmed impact snaps body and camera together. */
		*car = ghost_pose.car;
		*camera = ghost_pose.camera;
	} else {
		struct CARSTATE blended;
		frame_interpolate_car(&blended, &upper.car, &lower.car, fraction);
		*car = ghost_pose.car;
		car->car_position = blended.car_position;
		car->car_rotate = blended.car_rotate;
		car->car_steeringAngle = blended.car_steeringAngle;
		for (legacy_u16 wheel = 0; wheel < CARSTATE_WHEEL_COUNT; wheel++) {
			car->car_suspension_deflection[wheel] = blended.car_suspension_deflection[wheel];
		}
		*camera = ghost_pose.camera;
		if (lower.camera.trackside_index == upper.camera.trackside_index &&
			upper.camera.trackside_index == ghost_pose.camera.trackside_index) {
			frame_interpolate_vector(&camera->follow_position, &upper.camera.follow_position,
									 &lower.camera.follow_position, fraction);
			frame_interpolate_vector(&camera->previous_position, &upper.camera.previous_position,
									 &lower.camera.previous_position, fraction);
		}
	}
	car->car_sound_flags = CAR_SOUND_NONE;
	/* Cached future poses supply geometry only; visible events keep their
	 * authoritative timing, including the ghost-view elapsed/crash overlay. */
	car->car_crashBmpFlag = ghost_pose.car.car_crashBmpFlag;
	camera->frame = ghost_pose.camera.frame;
	camera->crash_frame = ghost_pose.camera.crash_frame;
	return 1;
}
#endif
