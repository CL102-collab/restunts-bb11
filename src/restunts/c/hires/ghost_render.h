#ifndef RESTUNTS_HIRES_GHOST_RENDER_H
#define RESTUNTS_HIRES_GHOST_RENDER_H
#include "../legacy.h"
struct CARSTATE;
struct GHOST_CAMERA_STATE;
#ifdef RESTUNTS_SDL3
/* Sample cached recorded poses at (live_frame - lag / 65536) / live_frame_rate.
 * This presentation-only read never advances physics or changes the live ghost.
 * Failure leaves both outputs untouched. Start/end times clamp to the replay. */
legacy_s16 ghost_sample_render_pose(legacy_u32 live_frame, legacy_u16 live_frame_rate,
									legacy_u32 lag, struct CARSTATE *car,
									struct GHOST_CAMERA_STATE *camera);
#endif
#ifdef RESTUNTS_SDL3
void ghost_render_reset(void);
#endif
#endif
