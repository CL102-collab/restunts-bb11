#ifndef RESTUNTS_MENU_BACKGROUND_H
#define RESTUNTS_MENU_BACKGROUND_H

#include "../legacy.h"

struct SPRITE;
struct SHAPE2D;

enum MENU_BACKGROUND {
	MENU_BACKGROUND_MAIN,
	MENU_BACKGROUND_SHOWROOM,
	MENU_BACKGROUND_OPPONENT,
	MENU_BACKGROUND_COUNT
};

/* Overlay optional doubled artwork after the indexed background, before the UI
 * and rotating car. The current sprite clipping also applies to companion pixels. */
void menu_background_draw(const struct SPRITE *target, const struct SHAPE2D *original,
						  enum MENU_BACKGROUND background);
/* Restore background artwork over a decoded shape's palette-mapped opaque
 * pixels. Transparent holes retain foreground art such as an opponent portrait. */
void menu_background_draw_overlay(const struct SPRITE *target, const struct SHAPE2D *overlay,
								  enum MENU_BACKGROUND background);
void menu_background_unload(void);

#endif
