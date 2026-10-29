#ifndef RESTUNTS_EXTENDED_REPLAY_TEST_STUBS_H
#define RESTUNTS_EXTENDED_REPLAY_TEST_STUBS_H

/* Legacy behavior fixtures deliberately keep the extended runtime inactive.
 * Actual extended storage/clock/rewind behavior has separate integration tests. */
#include "../c/externs.h"
#include "../c/fileio.h"
#include "../c/ui_dialog.h"
#ifdef RESTUNTS_SDL3
#include "../c/extended_replay_runtime.h"
#include "../c/extended_track.h"
#include "../c/extended_file.h"
#include "../c/extended_geometry.h"
#include "../c/extended_highscore.h"
int extended_highscore_load_or_create(const legacy_s8 *directory, const legacy_s8 *name,
									  const struct CSTRK_TRACK *track, int create_default)
{
	(void)directory;
	(void)name;
	(void)track;
	(void)create_default;
	assert(0);
	return 1;
}
const struct CSHIG_ENTRY *extended_highscore_entry(unsigned index)
{
	(void)index;
	assert(0);
	return NULL;
}
int extended_highscore_validate_track(const legacy_s8 *directory, const legacy_s8 *name,
									  const struct CSTRK_TRACK *track)
{
	(void)directory;
	(void)name;
	(void)track;
	assert(0);
	return 1;
}
uint64_t extended_highscore_ticks(uint32_t frames, uint16_t fps)
{
	(void)frames;
	(void)fps;
	assert(0);
	return CSHIG_EMPTY_TICKS;
}
int extended_highscore_qualifies(uint32_t frames, uint16_t fps)
{
	(void)frames;
	(void)fps;
	assert(0);
	return 0;
}
int extended_highscore_record(const legacy_s8 *directory, const legacy_s8 *name,
							  const struct CSTRK_TRACK *track, const struct CSHIG_ENTRY *entry,
							  unsigned *rank)
{
	(void)directory;
	(void)name;
	(void)track;
	(void)entry;
	(void)rank;
	assert(0);
	return 1;
}
int extended_highscore_format_time(legacy_s8 *out, size_t size, uint64_t ticks)
{
	(void)out;
	(void)size;
	(void)ticks;
	assert(0);
	return 1;
}
const legacy_s8 *extended_highscore_error(void)
{
	assert(0);
	return NULL;
}
#ifdef EXTENDED_TEST_GHOST_SELECTION
#include "../c/ghost.h"
legacy_s16 ghost_select_replay_format(const legacy_s8 *directory, const legacy_s8 *name,
									  const legacy_s8 *extension)
{
	assert(strcmp(extension, ".rpl") == 0);
	return ghost_select_replay(directory, name);
}
#endif
#ifdef EXTENDED_TEST_GHOST_STUBS
void file_warn_replay_version(legacy_u16 version)
{
	(void)version;
	assert(!"Unexpected extended replay warning in a legacy ghost fixture");
}
void file_warn_replay_driving_rules(legacy_u16 recorded)
{
	(void)recorded;
	assert(!"Unexpected extended replay rules warning in a legacy ghost fixture");
}
#include "../c/extended_ghost.h"
void extended_ghost_free(struct EXTENDED_GHOST_REPLAY *replay)
{
	assert(replay->bytes == NULL);
}
int extended_ghost_read(const legacy_s8 *path, struct EXTENDED_GHOST_REPLAY *out,
						struct GAMEINFO *config)
{
	(void)path;
	(void)out;
	(void)config;
	assert(0);
	return 0;
}
int extended_ghost_matches_track(const struct EXTENDED_GHOST_REPLAY *replay)
{
	(void)replay;
	assert(0);
	return 0;
}
#endif

struct EXTENDED_REPLAY_CLOCKS extended_replay_clocks;
struct EXTENDED_REPLAY_CLOCKS extended_race_stats;
void extended_race_stats_capture(void)
{
	assert(0);
}
int extended_replay_is_active(void)
{
	return 0;
}
legacy_u16 extended_replay_version(void)
{
	return 0;
}
legacy_u16 extended_replay_driving_rules(void)
{
	return 0;
}
void extended_replay_runtime_suspend(struct EXTENDED_REPLAY_SUSPENDED *saved)
{
	memset(saved, 0, sizeof(*saved));
}
void extended_replay_runtime_resume(struct EXTENDED_REPLAY_SUSPENDED *saved)
{
	(void)saved;
}
int extended_replay_runtime_load(const struct CSRPL_VIEW *view)
{
	(void)view;
	assert(0);
	return 0;
}
uint32_t extended_replay_frame(void)
{
	return (legacy_u16)state.game_frame;
}
uint32_t extended_replay_input_frame(void)
{
	return elapsed_time2;
}
uint32_t extended_replay_recorded_frames(void)
{
	return gameconfig.game_recordedframes;
}
void extended_replay_set_input_frame(uint32_t frame)
{
	elapsed_time2 = (legacy_u16)frame;
}
void extended_replay_runtime_begin(void)
{
	assert(!"Unexpected extended recording");
}
void extended_replay_runtime_clear(void)
{
}
void extended_replay_reset_simulation(int reset)
{
	(void)reset;
}
int extended_replay_record_input(uint8_t input)
{
	(void)input;
	assert(0);
	return 0;
}
int extended_replay_advance_input(void)
{
	assert(0);
	return 0;
}
uint8_t extended_replay_input_at(uint32_t frame)
{
	(void)frame;
	assert(0);
	return 0;
}
int extended_replay_replace_input(uint32_t frame, uint8_t input)
{
	(void)frame;
	(void)input;
	assert(0);
	return 0;
}
void extended_replay_before_step(int store)
{
	(void)store;
	assert(0);
}
void extended_replay_after_step(void)
{
	assert(0);
}
int extended_replay_seek(uint32_t frame)
{
	(void)frame;
	assert(0);
	return 0;
}
int extended_replay_branch(uint32_t frame)
{
	(void)frame;
	assert(0);
	return 0;
}
void extended_replay_format_runtime_time(legacy_s8 *out, uint32_t frame, int fraction)
{
	(void)out;
	(void)frame;
	(void)fraction;
	assert(0);
}
uint32_t extended_replay_timeline_position(uint32_t frame, uint32_t count, uint32_t width)
{
	return count ? (uint32_t)((uint64_t)frame * width / count) : 0U;
}

int extended_track_active(void)
{
	return 0;
}
int extended_track_width(void)
{
	return 30;
}
int extended_track_height(void)
{
	return 30;
}
unsigned extended_track_route_capacity(void)
{
	return 901U;
}
const struct CSTRK_TRACK *extended_track_model(void)
{
	assert(0);
	return NULL;
}
void extended_track_unload(void)
{
}
void extended_track_rebind(void)
{
}
const struct CSTRK_OBJECT *extended_track_object_at(int x, int y)
{
	(void)x;
	(void)y;
	assert(0);
	return NULL;
}
const struct CSTRK_OBJECT *extended_track_object_origin_at(int x, int y)
{
	(void)x;
	(void)y;
	assert(0);
	return NULL;
}
unsigned extended_track_object_id(const struct CSTRK_OBJECT *object)
{
	(void)object;
	assert(0);
	return 0;
}
legacy_s16 extended_track_elevation(int x, int y, int lx, int ly)
{
	(void)x;
	(void)y;
	(void)lx;
	(void)ly;
	assert(0);
	return 0;
}
int extended_track_load(const uint8_t *data, size_t size, char *error, size_t error_size)
{
	(void)data;
	(void)size;
	(void)error;
	(void)error_size;
	assert(0);
	return 0;
}
int cstrk_encode(const struct CSTRK_TRACK *track, uint8_t **data, size_t *size, char *error,
				 size_t error_size)
{
	(void)track;
	(void)data;
	(void)size;
	(void)error;
	(void)error_size;
	assert(0);
	return 0;
}
int extended_file_load_track(const legacy_s8 *path)
{
	(void)path;
	assert(0);
	return 1;
}
const legacy_s8 *extended_file_error(void)
{
	assert(0);
	return (const legacy_s8 *)"inactive";
}
int extended_editor_open(void)
{
	assert(0);
	return 0;
}

void extended_geometry_set_context(legacy_s32 x, legacy_s32 z)
{
	(void)x;
	(void)z;
}
void extended_geometry_get_context(legacy_s32 *x, legacy_s32 *z)
{
	*x = 0;
	*z = 0;
}
legacy_s32 extended_geometry_unwrap(legacy_s16 coordinate, legacy_s32 reference)
{
	(void)reference;
	return coordinate;
}
void extended_geometry_world_position(const struct VECTOR *position, legacy_s32 *x, legacy_s32 *z)
{
	(void)position;
	(void)x;
	(void)z;
	assert(0);
}
int extended_geometry_terrain(const struct CSTRK_TRACK *track, legacy_s32 x, legacy_s32 z,
							  struct EXTENDED_TERRAIN_SAMPLE *sample)
{
	(void)track;
	(void)x;
	(void)z;
	(void)sample;
	assert(0);
	return 0;
}
void extended_geometry_object_origin(const struct CSTRK_TRACK *track,
									 const struct CSTRK_OBJECT *object, struct VECTOR *origin)
{
	(void)track;
	(void)object;
	(void)origin;
	assert(0);
}
int extended_geometry_transform_shape(const struct SHAPE3D *source, struct SHAPE3D *destination,
									  legacy_s16 orientation, legacy_u16 surface,
									  legacy_s16 physical)
{
	(void)source;
	(void)destination;
	(void)orientation;
	(void)surface;
	(void)physical;
	assert(0);
	return 0;
}
void extended_geometry_free_shape(struct SHAPE3D *shape)
{
	(void)shape;
	assert(0);
}
#endif

static const legacy_s8 *extended_test_selected_extension = (const legacy_s8 *)".rpl";
legacy_s8 do_format_fileselect_dialog(legacy_s8 *directory, legacy_s8 *filename,
									  legacy_s8 *extension, legacy_s8 far *prompt)
{
	extended_test_selected_extension = extension;
	return do_fileselect_dialog(directory, filename, extension, prompt);
}
const legacy_s8 *file_dialog_selected_extension(void)
{
	return extended_test_selected_extension;
}
legacy_s16 file_load_replay_format(const legacy_s8 *directory, const legacy_s8 *name,
								   const legacy_s8 *extension)
{
	(void)extension;
	return file_load_replay(directory, name);
}

#endif
