#include "menu_background.h"
#include "../hires/menu_artwork.h"

void menu_background_draw(const struct SPRITE *target, const struct SHAPE2D *original,
						  enum MENU_BACKGROUND background)
{
	hires_menu_background_draw(target, original, background);
}

void menu_background_draw_overlay(const struct SPRITE *target, const struct SHAPE2D *overlay,
								  enum MENU_BACKGROUND background)
{
	hires_menu_background_draw_overlay(target, overlay, background);
}

void menu_background_unload(void)
{
	hires_menu_background_unload();
}
