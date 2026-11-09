#include "extended_checkpoint.h"
#include "checkpoint_shape.h"

#ifdef RESTUNTS_SDL3

#define CHECKPOINT_TILE_CENTER (CSTRK_TILE_WORLD_SIZE / 2L)
#define CHECKPOINT_BYTE_BITS 8U
#define CHECKPOINT_QUARTER_TURN_MASK 3U
#define CHECKPOINT_BLUE_INDEX 0U
#define CHECKPOINT_RED_INDEX 1U

struct CHECKPOINT_INTERSECTION {
	legacy_s64 numerator, denominator;
	legacy_s32 previous_depth, current_depth;
};

static void checkpoint_local(const struct CSTRK_TRACK *track, const struct CSTRK_OBJECT *object,
							 const struct VECTORLONG *position, legacy_u16 rotation,
							 legacy_s64 *lateral, legacy_s64 *height, legacy_s32 *depth)
{
	legacy_s32 center_x =
		((legacy_s32)object->x * CSTRK_TILE_WORLD_SIZE + object->width * CHECKPOINT_TILE_CENTER) *
		EXTENDED_CHECKPOINT_POSITION_SCALE;
	legacy_s32 center_z = ((legacy_s32)(track->height - object->y) * CSTRK_TILE_WORLD_SIZE -
						   object->height * CHECKPOINT_TILE_CENTER) *
						  EXTENDED_CHECKPOINT_POSITION_SCALE;
	legacy_s32 dx = position->lx - center_x, dz = position->lz - center_z;
	switch (rotation & CHECKPOINT_QUARTER_TURN_MASK) {
		case 1U:
			*lateral = -dz;
			*depth = dx;
			break;
		case 2U:
			*lateral = -dx;
			*depth = -dz;
			break;
		case 3U:
			*lateral = dz;
			*depth = -dx;
			break;
		default:
			*lateral = dx;
			*depth = dz;
			break;
	}
	/* Keep the support height rational until aperture comparison, avoiding
	 * word wrapping and premature rounding on high or graded extended roads. */
	legacy_s64 support_x =
		dx + object->width * CHECKPOINT_TILE_CENTER * EXTENDED_CHECKPOINT_POSITION_SCALE;
	legacy_s64 support_y =
		object->height * CHECKPOINT_TILE_CENTER * EXTENDED_CHECKPOINT_POSITION_SCALE - dz;
	*height =
		((legacy_s64)position->ly - (legacy_s64)object->base_height * CSTRK_LEVEL_WORLD_HEIGHT *
										EXTENDED_CHECKPOINT_POSITION_SCALE) *
			CSTRK_TILE_WORLD_SIZE -
		(object->slope_x * support_x + object->slope_y * support_y) * CSTRK_LEVEL_WORLD_HEIGHT;
}

static legacy_s16 checkpoint_intersection(const struct CSTRK_TRACK *track,
										  const struct CSTRK_OBJECT *object,
										  const struct VECTORLONG *previous,
										  const struct VECTORLONG *current, legacy_u16 rotation,
										  struct CHECKPOINT_INTERSECTION *hit)
{
	legacy_s64 previous_x, previous_y, current_x, current_y;
	checkpoint_local(track, object, previous, rotation, &previous_x, &previous_y,
					 &hit->previous_depth);
	checkpoint_local(track, object, current, rotation, &current_x, &current_y, &hit->current_depth);
	if (!((hit->previous_depth < 0 && hit->current_depth >= 0) ||
		  (hit->previous_depth > 0 && hit->current_depth <= 0))) {
		return 0;
	}
	hit->numerator =
		hit->previous_depth < 0 ? -(legacy_s64)hit->previous_depth : hit->previous_depth;
	legacy_s64 end_depth =
		hit->current_depth < 0 ? -(legacy_s64)hit->current_depth : hit->current_depth;
	hit->denominator = hit->numerator + end_depth;
	legacy_s64 before = hit->denominator - hit->numerator;
	legacy_s64 y = previous_y * before + current_y * hit->numerator;
	legacy_s64 height_scale =
		(legacy_s64)EXTENDED_CHECKPOINT_POSITION_SCALE * CSTRK_TILE_WORLD_SIZE * hit->denominator;
	if (y < 0 || y >= CHECKPOINT_GATE_SIGN_BOTTOM * height_scale) {
		return 0;
	}
	legacy_s16 opening = y < CHECKPOINT_GATE_LOWER_POLE_HEIGHT * height_scale
							 ? CHECKPOINT_GATE_INNER_HALF_WIDTH
							 : CHECKPOINT_GATE_UPPER_INNER_HALF_WIDTH;
	legacy_s64 x = previous_x * before + current_x * hit->numerator;
	legacy_s64 half_width =
		(legacy_s64)opening * EXTENDED_CHECKPOINT_POSITION_SCALE * hit->denominator;
	return x > -half_width && x < half_width;
}

legacy_s16 extended_checkpoint_crossed(const struct CSTRK_TRACK *track,
									   const struct CSTRK_OBJECT *object,
									   const struct VECTORLONG *previous,
									   const struct VECTORLONG *current)
{
	struct CHECKPOINT_INTERSECTION hit;
	return track && object && previous && current &&
		   checkpoint_intersection(track, object, previous, current, object->rotation, &hit);
}

#if !defined(RESTUNTS_HEADLESS) || defined(RESTUNTS_EXTENDED_REPLAY)
#include "extended_track.h"
#include "extended_replay_runtime.h"
#include "../externs.h"
#include "../crash_state.h"

static const struct CSTRK_OBJECT *checkpoint_objects;
static legacy_u32 checkpoint_object_count;
static legacy_u16 checkpoint_indices[CSTRK_MAX_OBJECTS];
static legacy_u16 checkpoint_count;
static legacy_u16 checkpoint_totals[EXTENDED_CHECKPOINT_COLOR_COUNT];

static struct CARSTATE *checkpoint_car(legacy_s16 car_index)
{
	return car_index == PLAYER_CAR_INDEX ? &state.playerstate : &state.opponentstate;
}

static legacy_s16 checkpoint_prepare(void)
{
	if (!extended_track_active() || !extended_replay_is_active()) {
		return 0;
	}
	const struct CSTRK_TRACK *track = extended_track_model();
	struct EXTENDED_CHECKPOINT_PROGRESS *progress = &extended_replay_clocks.checkpoints;
	if (!progress->initialized || checkpoint_objects != track->objects ||
		checkpoint_object_count != track->object_count) {
		checkpoint_count = 0;
		checkpoint_totals[CHECKPOINT_BLUE_INDEX] = 0;
		checkpoint_totals[CHECKPOINT_RED_INDEX] = 0;
		for (legacy_u32 index = 0; index < track->object_count; index++) {
			legacy_u8 colors = cstrk_checkpoint_colors(track->objects[index].legacy_id);
			if (colors) {
				checkpoint_indices[checkpoint_count++] = (legacy_u16)index;
				for (legacy_u16 color = 0; color < EXTENDED_CHECKPOINT_COLOR_COUNT; color++) {
					if (colors & (1U << color)) {
						checkpoint_totals[color]++;
					}
				}
			}
		}
		checkpoint_objects = track->objects;
		checkpoint_object_count = track->object_count;
	}
	if (!progress->initialized) {
		for (legacy_u16 car = 0; car < EXTENDED_CHECKPOINT_CAR_COUNT; car++) {
			progress->remaining[car][CHECKPOINT_BLUE_INDEX] =
				checkpoint_totals[CHECKPOINT_BLUE_INDEX];
			progress->remaining[car][CHECKPOINT_RED_INDEX] =
				checkpoint_totals[CHECKPOINT_RED_INDEX];
			progress->previous[car] = checkpoint_car((legacy_s16)car)->car_position;
		}
		progress->initialized = 1;
	}
	return checkpoint_count != 0;
}

legacy_s16 extended_checkpoint_passed(const struct CSTRK_OBJECT *object, legacy_s16 car_index)
{
	if (car_index < 0 || (legacy_u16)car_index >= EXTENDED_CHECKPOINT_CAR_COUNT || !object ||
		!checkpoint_prepare()) {
		return 0;
	}
	const struct CSTRK_TRACK *track = extended_track_model();
	for (legacy_u16 gate = 0; gate < checkpoint_count; gate++) {
		legacy_u16 index = checkpoint_indices[gate];
		if (&track->objects[index] == object) {
			return (extended_replay_clocks.checkpoints
						.visited[car_index][index / CHECKPOINT_BYTE_BITS] &
					(1U << (index % CHECKPOINT_BYTE_BITS))) != 0;
		}
	}
	return 0;
}

static void checkpoint_observe(legacy_s16 car_index, const struct CHECKPOINT_INTERSECTION *finish)
{
	const struct CSTRK_TRACK *track = extended_track_model();
	struct EXTENDED_CHECKPOINT_PROGRESS *progress = &extended_replay_clocks.checkpoints;
	const struct VECTORLONG *current = &checkpoint_car(car_index)->car_position;
	for (legacy_u16 gate = 0; gate < checkpoint_count; gate++) {
		legacy_u16 index = checkpoint_indices[gate];
		legacy_u8 mask = (legacy_u8)(1U << (index % CHECKPOINT_BYTE_BITS));
		legacy_u8 *visited = &progress->visited[car_index][index / CHECKPOINT_BYTE_BITS];
		if (*visited & mask) {
			continue;
		}
		const struct CSTRK_OBJECT *object = &track->objects[index];
		struct CHECKPOINT_INTERSECTION hit;
		if (checkpoint_intersection(track, object, &progress->previous[car_index], current,
									object->rotation, &hit) &&
			(!finish ||
			 hit.numerator * finish->denominator < finish->numerator * hit.denominator)) {
			*visited |= mask;
			legacy_u8 colors = cstrk_checkpoint_colors(object->legacy_id);
			for (legacy_u16 color = 0; color < EXTENDED_CHECKPOINT_COLOR_COUNT; color++) {
				if (colors & (1U << color)) {
					progress->remaining[car_index][color]--;
				}
			}
		}
	}
}

static legacy_s16 checkpoint_complete(legacy_s16 car_index)
{
	const struct EXTENDED_CHECKPOINT_PROGRESS *progress = &extended_replay_clocks.checkpoints;
	return (checkpoint_totals[CHECKPOINT_BLUE_INDEX] != 0 &&
			progress->remaining[car_index][CHECKPOINT_BLUE_INDEX] == 0) ||
		   (checkpoint_totals[CHECKPOINT_RED_INDEX] != 0 &&
			progress->remaining[car_index][CHECKPOINT_RED_INDEX] == 0);
}

void extended_checkpoint_begin_step(void)
{
	if (!checkpoint_prepare()) {
		return;
	}
	for (legacy_u16 car = 0; car < EXTENDED_CHECKPOINT_CAR_COUNT; car++) {
		extended_replay_clocks.checkpoints.previous[car] =
			checkpoint_car((legacy_s16)car)->car_position;
	}
}

legacy_s16 extended_checkpoint_finish_allowed(legacy_s16 car_index)
{
	if (!checkpoint_prepare()) {
		return 1;
	}
	if (car_index < 0 || (legacy_u16)car_index >= EXTENDED_CHECKPOINT_CAR_COUNT) {
		return 0;
	}
	const struct CARSTATE *car = checkpoint_car(car_index);
	const struct CSTRK_OBJECT *finish =
		extended_track_object_at(start_finish_column, start_finish_row);
	if (!finish || car->car_lap_count == 0) {
		return 0;
	}
	struct CHECKPOINT_INTERSECTION hit;
	if (!checkpoint_intersection(
			extended_track_model(), finish, &extended_replay_clocks.checkpoints.previous[car_index],
			&car->car_position, (legacy_u16)track_angle / ANGLE_QUARTER_TURN, &hit) ||
		hit.previous_depth >= 0 || hit.current_depth < 0) {
		return 0;
	}
	checkpoint_observe(car_index, &hit);
	return checkpoint_complete(car_index);
}

void extended_checkpoint_end_step(void)
{
	if (!checkpoint_prepare()) {
		return;
	}
	legacy_u16 cars = gameconfig.game_opponenttype ? EXTENDED_CHECKPOINT_CAR_COUNT : 1U;
	for (legacy_u16 car = 0; car < cars; car++) {
		if (checkpoint_car((legacy_s16)car)->car_crashBmpFlag == CRASH_EVENT_NONE) {
			if (extended_checkpoint_finish_allowed((legacy_s16)car)) {
				update_crash_state(CRASH_EVENT_FINISH, (legacy_s16)car);
			}
			checkpoint_observe((legacy_s16)car, NULL);
		}
	}
}
#endif
#endif
