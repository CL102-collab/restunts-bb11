#ifndef RESTUNTS_EXTENDED_FRAME_H
#define RESTUNTS_EXTENDED_FRAME_H

#include "legacy.h"

struct FRAME_TILE;
struct FRAME_TILE_SELECTION;
struct FRAME_CAMERA;
struct CARSTATE;

legacy_s16 extended_frame_draw_fences(const struct FRAME_TILE *tile,
									  const struct FRAME_TILE_SELECTION *tiles,
									  const struct FRAME_CAMERA *camera, legacy_s8 flags);
legacy_s16 extended_frame_draw_terrain(const struct FRAME_TILE *tile,
									   const struct FRAME_CAMERA *camera, legacy_s8 flags);
legacy_s16 extended_frame_draw_objects(struct FRAME_TILE *tile, const struct FRAME_CAMERA *camera,
									   legacy_s8 flags);
legacy_s16 extended_frame_add_start_flag(const struct FRAME_CAMERA *camera, legacy_s8 flags);
legacy_s16 extended_frame_car_visible(const struct CARSTATE *carstate);
legacy_s16 extended_frame_background_height(legacy_s16 height);
void extended_frame_position_camera_tiles(struct FRAME_TILE_SELECTION *tiles,
										  const struct FRAME_CAMERA *camera);
void extended_frame_set_context(const struct CARSTATE *viewed);
legacy_s16 extended_frame_first_tile_pass(void);
legacy_u8 extended_frame_should_draw_terrain(const struct FRAME_TILE *tile, legacy_s16 first_pass,
											 legacy_s16 pass);
#ifndef RESTUNTS_HEADLESS
legacy_s16 extended_frame_format_elapsed_time(legacy_s8 *out);
#endif

#endif
