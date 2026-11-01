#if defined(RESTUNTS_SDL3)
#include "frame_overlay.h"
#include "../frame/frame_fps.h"
#include "settings.h"
#include "../ui/ui_text.h"
#include "../externs.h"
#include "../platform.h"
#include "../video/video_frame.h"

#define FPS_TEXT_HIRES_TARGET 60U
#define HIRES_STATUS_PREFIX "HyperVision: "
#define HIRES_STATUS_Y (REPLAY_TEXT_Y + REPLAY_TEXT_LINE_HEIGHT)
#define HIRES_STATUS_NAME_LENGTH (sizeof("Medium") - 1U)
#define HIRES_STATUS_BUFFER_SIZE (sizeof(HIRES_STATUS_PREFIX) + HIRES_STATUS_NAME_LENGTH)
#define HIRES_STATUS_DURATION (2U * DOS_TIMER_REALTIME_TICKS_PER_SECOND)
#define HIRES_STATUS_RESTORE_PAGES 2U
#define HIRES_STATUS_RIGHT_X                                                                       \
	(REPLAY_TEXT_LEFT_X + (HIRES_STATUS_BUFFER_SIZE - 1U) * REPLAY_TEXT_CHARACTER_WIDTH + 1U)

static legacy_s8 hires_status_text[HIRES_STATUS_BUFFER_SIZE];
static legacy_u32 hires_status_start;
static legacy_u8 hires_status_active;
static legacy_u8 hires_status_expired_pending;
static legacy_u8 hires_status_clear_frames;

void frame_hires_show_status(const legacy_s8 *name)
{
	legacy_u16 prefix_length = sizeof(HIRES_STATUS_PREFIX) - 1U;
	legacy_u16 length = 0;
	copy_string(hires_status_text, HIRES_STATUS_PREFIX);
	while (length < HIRES_STATUS_NAME_LENGTH && name[length] != 0) {
		hires_status_text[prefix_length + length] = name[length];
		length++;
	}
	hires_status_text[prefix_length + length] = 0;
	hires_status_start = dos_timer_get_realtime_counter();
	hires_status_active = 1;
	hires_status_expired_pending = 0;
}

static legacy_s16 frame_hires_expire_status(legacy_u32 now)
{
	if (hires_status_active == 0 ||
		LEGACY_U32_WRAP_SUB(now, hires_status_start) < HIRES_STATUS_DURATION) {
		return 0;
	}
	hires_status_active = 0;
	hires_status_expired_pending = 1;
	return 1;
}

legacy_s16 frame_hires_status_overlay_active(void)
{
	if (hires_status_active != 0) {
		frame_hires_expire_status(dos_timer_get_realtime_counter());
	}
	return hires_status_active != 0 || hires_status_clear_frames != 0 ||
		   hires_status_expired_pending != 0;
}

static struct RECTANGLE frame_hires_status_bounds(void)
{
	struct RECTANGLE bounds;
	bounds.left = REPLAY_TEXT_LEFT_X;
	bounds.right = HIRES_STATUS_RIGHT_X;
	bounds.top = HIRES_STATUS_Y;
	bounds.bottom = HIRES_STATUS_Y + font_glyph_height + 1;
	return bounds;
}

legacy_s16 frame_hires_status_expire_at(legacy_u32 now)
{
	frame_hires_expire_status(now);
	return hires_status_expired_pending != 0 ||
		   (hires_status_active == 0 && hires_status_clear_frames != 0);
}

legacy_s16 frame_hires_status_expire_idle(void)
{
	if (hires_status_active == 0 && hires_status_expired_pending == 0 &&
		hires_status_clear_frames == 0) {
		return 0;
	}
	return frame_hires_status_expire_at(dos_timer_get_realtime_counter());
}

struct RECTANGLE *frame_hires_status_draw_text(void)
{
	if (frame_hires_status_overlay_active() == 0) {
		return &empty_rect;
	}
	static struct RECTANGLE bounds;
	bounds = empty_rect;
	if (hires_status_active != 0 || hires_status_clear_frames != 0) {
		/* Keep the longest status dirty while drawing and erasing. Replacing a
		 * long preset name with a short one must not leave trailing letters. */
		bounds = frame_hires_status_bounds();
		if (hires_status_active != 0) {
			intro_draw_text(hires_status_text, REPLAY_TEXT_LEFT_X, HIRES_STATUS_Y,
							dialog_fnt_colour, 0);
			hires_status_clear_frames =
				video_uses_page_flipping != 0 ? HIRES_STATUS_RESTORE_PAGES : 1U;
		} else {
			hires_status_clear_frames--;
		}
	}
	/* A visibility query must not consume the waiting loop's cleanup request. */
	hires_status_expired_pending = 0;
	return &bounds;
}

legacy_s16 frame_hires_status_redraw_pending(void)
{
	return hires_status_active != 0 || hires_status_expired_pending != 0 ||
		   hires_status_clear_frames != 0;
}

struct RECTANGLE *frame_hires_status_redraw_bounds(void)
{
	static struct RECTANGLE bounds;
	if (hires_status_active == 0 && hires_status_clear_frames == 0) {
		return &empty_rect;
	}
	bounds = frame_hires_status_bounds();
	return &bounds;
}

legacy_u16 frame_hires_fps_target(legacy_u16 target)
{
	if (hires_active != 0) {
		return FPS_TEXT_HIRES_TARGET;
	}
	return target;
}

legacy_u16 frame_hires_filename_reserved(legacy_s16 y, legacy_u16 reserved)
{
	if (y == HIRES_STATUS_Y && hires_status_active != 0) {
		reserved = HIRES_STATUS_BUFFER_SIZE;
	}
	return reserved;
}
#endif
