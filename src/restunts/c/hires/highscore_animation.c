#include "highscore_animation.h"

#ifdef RESTUNTS_SDL3
#include "../highscore_internal.h"
#include "hires.h"
#include "../opponent/opponent_animation.h"
#include "../shape2d/shape2d_internal.h"

void hires_highscore_reset_animation(struct END_SCREEN_STATE *screen)
{
	screen->hires_animation.drawn = 0;
}

void hires_highscore_load_animation(const struct END_SCREEN_STATE *screen)
{
	if (screen->outcome != END_SCREEN_OUTCOME_NONE) {
		opponent_animation_load(screen->opponent_active, screen->outcome == END_SCREEN_OUTCOME_WIN);
	}
}

void hires_highscore_show_animation(struct END_SCREEN_STATE *screen, legacy_s16 animation_width,
									legacy_s16 animation_height)
{
	screen->hires_animation.width = animation_width;
	screen->hires_animation.height = animation_height;
	if (screen->outcome != END_SCREEN_OUTCOME_NONE) {
		screen->hires_animation.drawn =
			opponent_animation_draw(&drawing_sprite, screen->animation_x, screen->animation_y,
									animation_width, animation_height) != 0;
	}
}

void hires_highscore_update_animation(struct END_SCREEN_STATE *screen,
									  HIRES_HIGHSCORE_DRAW_FRAME draw_frame)
{
	if (screen->outcome == END_SCREEN_OUTCOME_NONE || !hires_enabled()) {
		screen->hires_animation.drawn = 0;
		return;
	}
	sprite_select_screen_compat();
	mouse_draw_opaque_check();
	legacy_u8 drawn =
		opponent_animation_draw(&drawing_sprite, screen->animation_x, screen->animation_y,
								screen->hires_animation.width, screen->hires_animation.height) != 0;
	mouse_draw_transparent_check();
	if (screen->hires_animation.drawn != 0 && drawn == 0) {
		/* A later decode error must retire the old full-color frame immediately. */
		draw_frame(screen->animation_resource, screen->animation_sequence, screen->animation_frame,
				   screen->animation_x, screen->animation_y, screen->animation_sprite, 0);
	}
	screen->hires_animation.drawn = drawn;
}

legacy_u8 hires_highscore_display_input(legacy_u16 input)
{
	if (input != (legacy_u16)KEY_F12 && input != (legacy_u16)KEY_SHIFT_F12) {
		return 0;
	}
	(void)handle_ingame_kb_shortcuts(LEGACY_S16_FROM_BITS(input));
	return 1;
}
#endif
