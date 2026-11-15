#include "race_internal.h"

#ifdef RESTUNTS_SDL3
#include "../opponent/unit_opponent.h"
#include "../hires/render_workers.h"
#include "../../platform/sdl3/sdl3.h"
#include "../platform.h"
#include "../externs.h"
#include "../ui/ui_dialog.h"
#include "../video/video_color.h"

#define UNIT_PREPARATION_DURATION_MS 10000U
#define UNIT_PREPARATION_POLL_MS 1U

legacy_s16 race_prepare_unit(void)
{
	if (!unit_opponent_is_selected() || idle_expired != 0 || is_in_replay != 0) {
		return 1;
	}
	legacy_s16 suspended = (legacy_s16)sdl3_timer_callbacks_suspended();
	dos_timer_set_callbacks_suspended(1);
	legacy_u64 started = SDL_GetTicks();
	sdl3_video_planning_progress(0, UNIT_PREPARATION_DURATION_MS);
#if defined(__EMSCRIPTEN__) && defined(__EMSCRIPTEN_PTHREADS__)
	/* The browser has a preloaded thread pool. Return idle renderer slots for
	 * startup planning; rendering lazily recreates them after preparation. */
	hires_render_workers_shutdown();
#endif
	legacy_int prepared = unit_opponent_prepare_begin();
	if (prepared) {
		for (;;) {
			legacy_u64 elapsed = SDL_GetTicks() - started;
			if (elapsed >= UNIT_PREPARATION_DURATION_MS) {
				break;
			}
			sdl3_video_planning_progress((legacy_u32)elapsed, UNIT_PREPARATION_DURATION_MS);
			sdl3_platform_pump();
			unit_opponent_prepare_work();
			sdl3_platform_delay(UNIT_PREPARATION_POLL_MS);
		}
		/* Retire startup workers before gameplay/render workers resume. Each
		 * planner job is bounded, including the single-threaded Web fallback. */
		prepared = unit_opponent_prepare_end();
		sdl3_video_planning_progress(UNIT_PREPARATION_DURATION_MS, UNIT_PREPARATION_DURATION_MS);
	}
	if (!prepared) {
		unit_opponent_stop();
	}
	sdl3_video_planning_progress(0, 0);
	sdl3_timer_rebase();
	dos_timer_set_callbacks_suspended(suspended);
	if (!prepared) {
		show_dialog(DIALOG_TYPE_MESSAGE, DIALOG_NO_BACKGROUND_SAVE,
					"Unable to prepare UNIT's route.", -1, -1, VIDEO_UI_COLOR(dialog_border_color),
					0, 0);
	}
	return prepared != 0;
}
#endif
