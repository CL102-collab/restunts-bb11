#include "../frame_internal.h"
#include "../game_input.h"
#include "../ui/ui_text.h"
#include "../shape2d/shape2d.h"
#include "../shape2d/shape2d_internal.h"
#include "../race/race_stats.h"
#include "../camera.h"
#include "../video_frame.h"
#include "../skybox.h"
#include "../race/race_graphics.h"
#include "../externs.h"
#include "../memmgr.h"
#include "../resource.h"
#include "../crash_state.h"
#include "../platform.h"
#include "../dashboard.h"
#include "frame_render.h"

#define REPLAY_TEXT_LEFT_X 8
#define REPLAY_TEXT_RIGHT_X 312
#define REPLAY_TEXT_CHARACTER_WIDTH 8U
#define REPLAY_TEXT_Y 15
#define REPLAY_FILENAME_Y 3
#define REPLAY_TEXT_LINE_HEIGHT (REPLAY_TEXT_Y - REPLAY_FILENAME_Y)
#define REPLAY_TEXT_MAX_CHARACTERS                                                                 \
	((REPLAY_TEXT_RIGHT_X - REPLAY_TEXT_LEFT_X) / REPLAY_TEXT_CHARACTER_WIDTH)

#define FPS_TEXT_RED 4
#define FPS_TEXT_GREEN 2
#define FPS_TEXT_TARGET 20U
#define FPS_TEXT_SUPERSIGHT_TARGET 60U
#define FPS_TEXT_NUMBER_BASE 10U
#define FPS_TEXT_MAX_DIGITS 5U
#define FPS_TEXT_SUFFIX_LENGTH 4U
#define FPS_TEXT_BUFFER_SIZE (FPS_TEXT_MAX_DIGITS + FPS_TEXT_SUFFIX_LENGTH + 1U)
#define FPS_TEXT_RIGHT_X                                                                           \
	(REPLAY_TEXT_LEFT_X + (FPS_TEXT_BUFFER_SIZE - 1U) * REPLAY_TEXT_CHARACTER_WIDTH + 1U)
#ifdef RESTUNTS_SDL3
#define SUPERSIGHT_STATUS_PREFIX "HyperVision: "
#define SUPERSIGHT_STATUS_Y (REPLAY_TEXT_Y + REPLAY_TEXT_LINE_HEIGHT)
#define RENDER_TIME_SAMPLE_COUNT 100U
#define RENDER_TIME_NS_PER_TENTH_MS 100000ULL
#define RENDER_TIME_TENTHS_PER_MS 10U
#define RENDER_TIME_MAX_TENTHS ((legacy_u32)LEGACY_U16_MAX * RENDER_TIME_TENTHS_PER_MS)
#define RENDER_TIME_BUFFER_SIZE (FPS_TEXT_MAX_DIGITS + sizeof(".0ms"))
#else
#define SUPERSIGHT_STATUS_PREFIX "SuperSight: "
#define SUPERSIGHT_STATUS_Y REPLAY_TEXT_Y
#endif
#define SUPERSIGHT_STATUS_NAME_LENGTH (sizeof("Medium") - 1U)
#define SUPERSIGHT_STATUS_BUFFER_SIZE                                                              \
	(sizeof(SUPERSIGHT_STATUS_PREFIX) + SUPERSIGHT_STATUS_NAME_LENGTH)
#define SUPERSIGHT_STATUS_DURATION (2U * DOS_TIMER_REALTIME_TICKS_PER_SECOND)
#define SUPERSIGHT_STATUS_RESTORE_PAGES 2U
#define SUPERSIGHT_STATUS_RIGHT_X                                                                  \
	(REPLAY_TEXT_LEFT_X + (SUPERSIGHT_STATUS_BUFFER_SIZE - 1U) * REPLAY_TEXT_CHARACTER_WIDTH + 1U)

static legacy_u32 fps_sample_start;
static legacy_u32 fps_last_presented;
static legacy_u16 fps_sample_frames;
static legacy_u16 fps_sample_value;
static legacy_u8 fps_sample_started;
#ifdef RESTUNTS_SDL3
static legacy_u64 render_time_samples[RENDER_TIME_SAMPLE_COUNT];
static legacy_u64 render_time_sum;
static legacy_u16 render_time_count;
static legacy_u16 render_time_next;

void frame_render_timing_reset(void)
{
	render_time_sum = 0;
	render_time_count = 0;
	render_time_next = 0;
}

void frame_render_timing_record(legacy_u64 elapsed_ns)
{
	/* Bound pathological stalls before summing; the display has five integer digits. */
	legacy_u64 maximum = (legacy_u64)RENDER_TIME_MAX_TENTHS * RENDER_TIME_NS_PER_TENTH_MS;
	if (elapsed_ns > maximum) {
		elapsed_ns = maximum;
	}
	if (render_time_count == RENDER_TIME_SAMPLE_COUNT) {
		render_time_sum -= render_time_samples[render_time_next];
	} else {
		render_time_count++;
	}
	render_time_samples[render_time_next] = elapsed_ns;
	render_time_sum += elapsed_ns;
	render_time_next = (render_time_next + 1U) % RENDER_TIME_SAMPLE_COUNT;
}
#endif
static legacy_s8 supersight_status_text[SUPERSIGHT_STATUS_BUFFER_SIZE];
static legacy_u32 supersight_status_start;
static legacy_u8 supersight_status_active;
static legacy_u8 supersight_status_expired_pending;
static legacy_u8 supersight_status_clear_frames;
static struct RECTANGLE fps_present_bounds;
static legacy_u8 fps_present_pending;

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

legacy_s16 frame_status_overlay_active(void)
{
	if (supersight_status_active != 0) {
		frame_supersight_expire_status(dos_timer_get_realtime_counter());
	}
	return supersight_status_active != 0 || supersight_status_clear_frames != 0 ||
		   supersight_status_expired_pending != 0;
}

legacy_s16 frame_display_overlay_active(void)
{
	legacy_s16 status_active = frame_status_overlay_active();
	return fps_display_enabled != 0 || status_active != 0;
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

void frame_fps_reset(void)
{
	fps_sample_frames = 0;
	fps_sample_value = 0;
	fps_sample_started = 0;
}

static legacy_s16 frame_status_expire_at(legacy_u32 now)
{
	frame_supersight_expire_status(now);
	return supersight_status_expired_pending != 0 ||
		   (supersight_status_active == 0 && supersight_status_clear_frames != 0);
}

legacy_s16 frame_status_expire_idle(void)
{
	if (supersight_status_active == 0 && supersight_status_expired_pending == 0 &&
		supersight_status_clear_frames == 0) {
		return 0;
	}
	return frame_status_expire_at(dos_timer_get_realtime_counter());
}

legacy_s16 frame_fps_expire_idle(void)
{
	legacy_s16 fps_can_expire =
		fps_display_enabled != 0 && fps_sample_value != 0 && fps_sample_started != 0;
	if (supersight_status_active == 0 && supersight_status_expired_pending == 0 &&
		supersight_status_clear_frames == 0 && fps_can_expire == 0) {
		return 0;
	}
	legacy_u32 now = dos_timer_get_realtime_counter();
	legacy_s16 expired = frame_status_expire_at(now);
	if (fps_can_expire != 0 &&
		LEGACY_U32_WRAP_SUB(now, fps_last_presented) >= DOS_TIMER_REALTIME_TICKS_PER_SECOND) {
		/* Ask the waiting replay loop to repaint the expired value only once. */
		frame_fps_reset();
		expired = 1;
	}
	return expired;
}

void frame_fps_record_presented(void)
{
	if (fps_display_enabled == 0) {
		frame_fps_reset();
		return;
	}

	legacy_u32 now = dos_timer_get_realtime_counter();
	fps_last_presented = now;
	if (fps_sample_started == 0) {
		fps_sample_start = now;
		fps_sample_started = 1;
		return;
	}
	if (fps_sample_frames != LEGACY_U16_MAX) {
		fps_sample_frames++;
	}
	legacy_u32 elapsed = LEGACY_U32_WRAP_SUB(now, fps_sample_start);
	if (elapsed >= DOS_TIMER_REALTIME_TICKS_PER_SECOND) {
		/* Count presented intervals, rather than simulation updates or render calls. */
		legacy_u32 value =
			(legacy_u32)fps_sample_frames * DOS_TIMER_REALTIME_TICKS_PER_SECOND / elapsed;
		fps_sample_value = value > LEGACY_U16_MAX ? LEGACY_U16_MAX : (legacy_u16)value;
		fps_sample_start = now;
		fps_sample_frames = 0;
	}
}

static legacy_s16 frame_fps_adaptive_enabled(void)
{
#if defined(RESTUNTS_SDL3)
	return supersight_enabled != 0;
#else
	return 0;
#endif
}

static void frame_fps_restore_roof(const struct RECTANGLE *overlay_bounds)
{
	fps_present_pending = 0;
	if (overlay_bounds->left >= overlay_bounds->right || dashboard_visible == 0 ||
		roofbmpheight_copy <= overlay_bounds->top) {
		return;
	}
	fps_present_bounds = *overlay_bounds;
	if (fps_present_bounds.bottom > roofbmpheight_copy) {
		fps_present_bounds.bottom = roofbmpheight_copy;
	}
	fps_present_pending = 1;
	struct SHAPE2D far *roof =
		(struct SHAPE2D far *)locate_shape_nofatal(stdaresptr, dashboard_roof_shape_id);
	if (roof != 0) {
		struct SPRITE saved_context[SPRITE_STATE_COUNT];
		sprite_save_context(saved_context);
		sprite_set_target_clip_bounds(fps_present_bounds.left, fps_present_bounds.right,
									  fps_present_bounds.top, fps_present_bounds.bottom);
		shape2d_rle_copy_position_clipped(roof);
		sprite_restore_context(saved_context);
	}
}

void frame_fps_present_roof(void)
{
	if (fps_present_pending == 0) {
		return;
	}
	fps_present_pending = 0;
	if (video_uses_page_flipping != 0 || dashboard_visible == 0) {
		return;
	}
	/* The normal camera copy excludes the static cockpit roof. This also
	 * presents the restored status area on the frame after its text expires. */
	struct SPRITE saved_context[SPRITE_STATE_COUNT];
	sprite_save_context(saved_context);
	sprite_set_target_clip_bounds(fps_present_bounds.left, fps_present_bounds.right,
								  fps_present_bounds.top, fps_present_bounds.bottom);
	mouse_draw_opaque_check();
	sprite_putimage(render_window_sprite->sprite_bitmapptr);
	mouse_draw_transparent_check();
	sprite_restore_context(saved_context);
}

static legacy_u16 frame_fps_format_number(legacy_s8 *text, legacy_u16 value)
{
	legacy_s8 digits[FPS_TEXT_MAX_DIGITS];
	legacy_u16 count = 0;
	do {
		digits[count++] = (legacy_s8)('0' + value % FPS_TEXT_NUMBER_BASE);
		value /= FPS_TEXT_NUMBER_BASE;
	} while (value != 0);
	for (legacy_u16 i = 0; i < count; i++) {
		text[i] = digits[count - i - 1U];
	}
	text[count] = 0;
	return count;
}

struct RECTANGLE *frame_status_draw_text(void)
{
	if (frame_status_overlay_active() == 0) {
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
							dialog_fnt_colour, 0);
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

struct RECTANGLE *frame_fps_draw_text(void)
{
	if (frame_display_overlay_active() == 0) {
		return &empty_rect;
	}

	static struct RECTANGLE bounds;
	bounds = empty_rect;
	if (fps_display_enabled != 0) {
		legacy_s8 text[FPS_TEXT_BUFFER_SIZE];
		legacy_u16 count = frame_fps_format_number(text, fps_sample_value);
		copy_string(text + count, " FPS");
		legacy_u16 target =
			frame_fps_adaptive_enabled() ? FPS_TEXT_SUPERSIGHT_TARGET : FPS_TEXT_TARGET;
		legacy_s16 color = fps_sample_value < target ? FPS_TEXT_RED : FPS_TEXT_GREEN;
		bounds = *intro_draw_text(text, REPLAY_TEXT_LEFT_X, REPLAY_FILENAME_Y, color, 0);
#ifdef RESTUNTS_SDL3
		legacy_u64 average_ns = render_time_count != 0 ? render_time_sum / render_time_count : 0;
		legacy_u32 tenths = (legacy_u32)((average_ns + RENDER_TIME_NS_PER_TENTH_MS / 2U) /
										 RENDER_TIME_NS_PER_TENTH_MS);
		legacy_s8 timing[RENDER_TIME_BUFFER_SIZE];
		count = frame_fps_format_number(timing, (legacy_u16)(tenths / RENDER_TIME_TENTHS_PER_MS));
		timing[count++] = '.';
		timing[count++] = (legacy_s8)('0' + tenths % RENDER_TIME_TENTHS_PER_MS);
		copy_string(timing + count, "ms");
		rect_union(&bounds,
				   intro_draw_text(timing, REPLAY_TEXT_LEFT_X, REPLAY_TEXT_Y, dialog_fnt_colour, 0),
				   &bounds);
#endif
	}
	rect_union(&bounds, frame_status_draw_text(), &bounds);
	return &bounds;
}

legacy_u16 frame_hires_prepare_text(void)
{
	if (frame_display_overlay_active() == 0) {
		fps_present_pending = 0;
		return 0;
	}
	struct RECTANGLE overlay_bounds = empty_rect;
	if (fps_display_enabled != 0) {
		overlay_bounds.left = REPLAY_TEXT_LEFT_X;
		overlay_bounds.right = FPS_TEXT_RIGHT_X;
		overlay_bounds.top = REPLAY_FILENAME_Y;
		overlay_bounds.bottom = REPLAY_FILENAME_Y + font_glyph_height + 1;
#ifdef RESTUNTS_SDL3
		overlay_bounds.bottom = REPLAY_TEXT_Y + font_glyph_height + 1;
#endif
	}
	if (supersight_status_active != 0 || supersight_status_clear_frames != 0) {
		struct RECTANGLE status_bounds = frame_supersight_status_bounds();
		rect_union(&overlay_bounds, &status_bounds, &overlay_bounds);
	}
	/* Only the roof lies outside the scene that was redrawn this frame. */
	frame_fps_restore_roof(&overlay_bounds);
	/* Reserve the dirty region now; the race controller paints the text after
	 * composing the dashboard so custom upper artwork cannot cover it. */
	rect_union(&rect_ingame_text, &overlay_bounds, &rect_ingame_text);
	/* Reserve the maximum width so changing counter digits never rewraps the filename. */
	return fps_display_enabled != 0 ? FPS_TEXT_BUFFER_SIZE : 0;
}

legacy_u16 frame_hires_filename_reserved(legacy_s16 y, legacy_u16 reserved)
{
#ifdef RESTUNTS_SDL3
	if (y == REPLAY_TEXT_Y && fps_display_enabled != FRAME_FPS_DISPLAY_OFF) {
		reserved = RENDER_TIME_BUFFER_SIZE;
	}
#endif
	if (y == SUPERSIGHT_STATUS_Y && supersight_status_active != 0) {
		reserved = SUPERSIGHT_STATUS_BUFFER_SIZE;
	}
	return reserved;
}
