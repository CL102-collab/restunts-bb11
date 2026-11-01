#ifndef RESTUNTS_HIRES_RACE_PRESENTATION_INTERNAL_H
#define RESTUNTS_HIRES_RACE_PRESENTATION_INTERNAL_H
#include "race_presentation.h"
#include "presentation.h"
#include "../gamestate.h"
#include "../ghost/ghost.h"
#ifdef RESTUNTS_SDL3
#define RACE_PRESENTATION_VIEW_COUNT 13U
#define RACE_PRESENTATION_SLOW_INTERVAL_SCALE 2U
#define RACE_PRESENTATION_FAST_FRAME_SPAN 2

struct RACE_PRESENTATION {
	struct PRESENTATION_CLOCK clock;
	struct GAMESTATE previous;
	struct GAMESTATE current;
	struct GAMESTATE interpolated;
	struct CARSTATE ghost_interpolated;
	struct GHOST_CAMERA_STATE ghost_camera_interpolated;
	legacy_u64 sample_time;
	legacy_u64 control_time;
	legacy_u16 sample_span;
	legacy_s16 sample_frame;
	legacy_s16 view[RACE_PRESENTATION_VIEW_COUNT];
	legacy_u8 history_valid;
	legacy_u8 ghost_valid;
	legacy_u8 started;
	legacy_u8 interpolating;
};

#endif
#endif
