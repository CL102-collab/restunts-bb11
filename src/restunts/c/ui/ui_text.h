#ifndef RESTUNTS_UI_TEXT_H
#define RESTUNTS_UI_TEXT_H

#include "../legacy.h"
#include "../math/math.h"

extern legacy_u8 far *active_font_definition;

legacy_s16 font_prefix_width(const legacy_s8 *text, legacy_s16 glyph_count);
legacy_s16 font_text_width(const legacy_s8 *text);
legacy_s16 font_centered_text_x(const legacy_s8 *text);
void font_set_colors(legacy_s16 color, legacy_s16 background_color);
struct RECTANGLE *intro_draw_text(legacy_s8 *text, legacy_s16 x, legacy_s16 y, legacy_s16 color,
								  legacy_s16 shadow_color);

void format_integer(legacy_s8 *destination, legacy_s16 value, legacy_s16 zero_pad,
					legacy_s16 width);
void format_frame_as_string(legacy_s8 *destination, legacy_s16 frame_count,
							legacy_s16 include_hundredths);
void parse_filepath_separators(legacy_s8 *destination, const legacy_s8 *path);
legacy_u16 legacy_near_string_length(const legacy_s8 *text);

extern legacy_s16 dialog_fnt_colour;
extern legacy_s16 font_glyph_height;

void font_set_fontdef2(void far *data);

void set_fontdefseg(void far *data);

void font_draw_text(const legacy_s8 *text, legacy_s16 x, legacy_s16 y);

extern struct RECTANGLE intro_text_bounds;

#endif
