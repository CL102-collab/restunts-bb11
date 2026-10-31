#ifndef RESTUNTS_INTRO_RENDER_INTERNAL_H
#define RESTUNTS_INTRO_RENDER_INTERNAL_H

#include "intro_view.h"
#include "hires/intro_presentation.h"

#define INTRO_STAR_COUNT 100U
#define INTRO_POINT_BUFFER_COUNT 2

struct INTRO_SESSION {
	legacy_s8 far *title_resource;
	struct VECTOR stars[INTRO_STAR_COUNT];
	struct POINT2D point_buffers[INTRO_POINT_BUFFER_COUNT][INTRO_STAR_COUNT];
	legacy_s16 point_counts[INTRO_POINT_BUFFER_COUNT];
	struct RECTANGLE shape_rect;
	struct RECTANGLE combined_rect;
	legacy_s16 camera_x;
	legacy_s16 camera_y;
	legacy_s16 camera_z;
	legacy_s16 target_x;
	legacy_s16 target_y;
	legacy_s16 target_z;
	legacy_s16 frame_count;
	legacy_s16 logo_changed;
	legacy_s16 needs_render;
	legacy_u16 rect_index;
#ifdef RESTUNTS_SDL3
	struct HIRES_INTRO_PRESENTATION presentation;
#endif
};

void intro_capture_view(struct INTRO_SESSION *intro, struct INTRO_VIEW *view);
void intro_render_session(struct INTRO_SESSION *intro, const struct INTRO_VIEW *view);

#endif
