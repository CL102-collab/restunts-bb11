#if !defined(RESTUNTS_SDL3)
#include "frame_overlay.h"
#include "../video/video_color.h"
#include "../frame/frame_fps.h"
#include "settings.h"
#include "../ui/ui_text.h"
#include "../externs.h"
#include "../platform.h"
#include "../video/video_frame.h"

#define SUPERSIGHT_STATUS_PREFIX "SuperSight: "
#define SUPERSIGHT_STATUS_Y REPLAY_TEXT_Y
#define SUPERSIGHT_STATUS_NAME_LENGTH (sizeof("Medium") - 1U)
#define SUPERSIGHT_STATUS_BUFFER_SIZE                                                              \
	(sizeof(SUPERSIGHT_STATUS_PREFIX) + SUPERSIGHT_STATUS_NAME_LENGTH)
#define SUPERSIGHT_STATUS_DURATION (2U * DOS_TIMER_REALTIME_TICKS_PER_SECOND)
#define SUPERSIGHT_STATUS_RESTORE_PAGES 2U
#define SUPERSIGHT_STATUS_RIGHT_X                                                                  \
	(REPLAY_TEXT_LEFT_X + (SUPERSIGHT_STATUS_BUFFER_SIZE - 1U) * REPLAY_TEXT_CHARACTER_WIDTH + 1U)

static legacy_s8 supersight_status_text[SUPERSIGHT_STATUS_BUFFER_SIZE];
static legacy_u32 supersight_status_start;
static legacy_u8 supersight_status_active;
static legacy_u8 supersight_status_expired_pending;
static legacy_u8 supersight_status_clear_frames;

void frame_supersight_show_status(const legacy_s8 *name)
{
	legacy_u16 prefix_length = sizeof(SUPERSIGHT_STATUS_PREFIX) - 1U;
	legacy_u16 length = 0;
	copy_string(supersight_status_text, SUPERSIGHT_STATUS_PREFIX);
	while (length < SUPERSIGHT_STATUS_NAME_LENGTH && name[length] != 0) {
		supersight_status_text[prefix_length + length] = name[length];
		length++;
	}
	supersight_status_text[prefix_length + length] = 0;
	supersight_status_start = dos_timer_get_realtime_counter();
	supersight_status_active = 1;
	supersight_status_expired_pending = 0;
}

static legacy_s16 frame_supersight_expire_status(legacy_u32 now)
{
	if (supersight_status_active == 0 ||
		LEGACY_U32_WRAP_SUB(now, supersight_status_start) < SUPERSIGHT_STATUS_DURATION) {
		return 0;
	}
	supersight_status_active = 0;
	supersight_status_expired_pending = 1;
	return 1;
}

legacy_s16 frame_supersight_status_overlay_active(void)
{
	if (supersight_status_active != 0) {
		frame_supersight_expire_status(dos_timer_get_realtime_counter());
	}
	return supersight_status_active != 0 || supersight_status_clear_frames != 0 ||
		   supersight_status_expired_pending != 0;
}

static struct RECTANGLE frame_supersight_status_bounds(void)
{
	struct RECTANGLE bounds;
	bounds.left = REPLAY_TEXT_LEFT_X;
	bounds.right = SUPERSIGHT_STATUS_RIGHT_X;
	bounds.top = SUPERSIGHT_STATUS_Y;
	bounds.bottom = SUPERSIGHT_STATUS_Y + font_glyph_height + 1;
	return bounds;
}

legacy_s16 frame_supersight_status_expire_at(legacy_u32 now)
{
	frame_supersight_expire_status(now);
	return supersight_status_expired_pending != 0 ||
		   (supersight_status_active == 0 && supersight_status_clear_frames != 0);
}

legacy_s16 frame_supersight_status_expire_idle(void)
{
	if (supersight_status_active == 0 && supersight_status_expired_pending == 0 &&
		supersight_status_clear_frames == 0) {
		return 0;
	}
	return frame_supersight_status_expire_at(dos_timer_get_realtime_counter());
}

struct RECTANGLE *frame_supersight_status_draw_text(void)
{
	if (frame_supersight_status_overlay_active() == 0) {
		return &empty_rect;
	}
	static struct RECTANGLE bounds;
	bounds = empty_rect;
	if (supersight_status_active != 0 || supersight_status_clear_frames != 0) {
		/* Keep the longest status dirty while drawing and erasing. Replacing a
		 * long preset name with a short one must not leave trailing letters. */
		bounds = frame_supersight_status_bounds();
		if (supersight_status_active != 0) {
			intro_draw_text(supersight_status_text, REPLAY_TEXT_LEFT_X, SUPERSIGHT_STATUS_Y,
							VIDEO_UI_COLOR(dialog_fnt_colour), 0);
			supersight_status_clear_frames =
				video_uses_page_flipping != 0 ? SUPERSIGHT_STATUS_RESTORE_PAGES : 1U;
		} else {
			supersight_status_clear_frames--;
		}
	}
	/* A visibility query must not consume the waiting loop's cleanup request. */
	supersight_status_expired_pending = 0;
	return &bounds;
}

legacy_s16 frame_supersight_status_redraw_pending(void)
{
	return supersight_status_active != 0 || supersight_status_expired_pending != 0 ||
		   supersight_status_clear_frames != 0;
}

struct RECTANGLE *frame_supersight_status_redraw_bounds(void)
{
	static struct RECTANGLE bounds;
	if (supersight_status_active == 0 && supersight_status_clear_frames == 0) {
		return &empty_rect;
	}
	bounds = frame_supersight_status_bounds();
	return &bounds;
}

legacy_u16 frame_supersight_fps_target(legacy_u16 target)
{
	return target;
}

legacy_u16 frame_supersight_filename_reserved(legacy_s16 y, legacy_u16 reserved)
{
	if (y == SUPERSIGHT_STATUS_Y && supersight_status_active != 0) {
		reserved = SUPERSIGHT_STATUS_BUFFER_SIZE;
	}
	return reserved;
}
#endif
