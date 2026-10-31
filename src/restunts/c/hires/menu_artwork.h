#ifndef RESTUNTS_HIRES_MENU_ARTWORK_H
#define RESTUNTS_HIRES_MENU_ARTWORK_H

#include "../menu/menu_background.h"

void hires_menu_background_draw(const struct SPRITE *target, const struct SHAPE2D *original,
								enum MENU_BACKGROUND background);
void hires_menu_background_draw_overlay(const struct SPRITE *target, const struct SHAPE2D *overlay,
										enum MENU_BACKGROUND background);
void hires_menu_background_unload(void);

#endif
