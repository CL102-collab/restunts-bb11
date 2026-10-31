#ifndef RESTUNTS_HIRES_SKYBOX_SCENE_H
#define RESTUNTS_HIRES_SKYBOX_SCENE_H

#include "../legacy.h"

#ifdef RESTUNTS_SDL3
struct RECTANGLE;
struct MATRIX;
legacy_s16 skybox_hires_render_scene(struct RECTANGLE *clip, const struct MATRIX *rotation,
									 legacy_s16 direction, legacy_s16 angle, legacy_s16 camera_y);
#endif

#endif
