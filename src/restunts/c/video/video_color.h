#ifndef RESTUNTS_VIDEO_COLOR_H
#define RESTUNTS_VIDEO_COLOR_H

/* Carry an authored drawing role alongside the VGA byte until it reaches a
 * primitive. Bitmap pixels retain their own resource conversion tables. */
#if defined(RESTUNTS_SDL3) && defined(RESTUNTS_FULL) && !defined(RESTUNTS_HEADLESS) &&             \
	!defined(RESTUNTS_TEMPLEOS)
#include "../../platform/sdl3/video_mode.h"
#include "../../platform/sdl3/video_mode_colors.h"
#define VIDEO_UI_VALUE(role, value) sdl3_video_mode_ui_color(SDL3_VIDEO_UI_##role, value)
#define VIDEO_UI_COLOR(symbol) VIDEO_UI_VALUE(symbol, symbol)
#define VIDEO_COLOR_VALUE(color) sdl3_video_mode_color_value(color)
#define VIDEO_COLOR_BEGIN(color) sdl3_video_mode_color_begin(color)
#define VIDEO_MATERIAL_BEGIN(material) sdl3_video_mode_material_begin(material)
#define VIDEO_DIRECT_END() sdl3_video_mode_direct_end()
#define VIDEO_LINE_BEGIN() sdl3_video_mode_line_begin()
#define VIDEO_LINE_END() sdl3_video_mode_line_end()
#define VIDEO_POINT_BEGIN() sdl3_video_mode_point_begin()
#define VIDEO_POINT_END() sdl3_video_mode_point_end()
#define VIDEO_CLEAR_TARGET(color) sprite_clear_target_color(color)
void sprite_clear_target_color(legacy_u16 color);
#else
#define VIDEO_UI_VALUE(role, value) (value)
#define VIDEO_UI_COLOR(symbol) (symbol)
#define VIDEO_COLOR_VALUE(color) (color)
#define VIDEO_COLOR_BEGIN(color)
#define VIDEO_MATERIAL_BEGIN(material)
#define VIDEO_DIRECT_END()
#define VIDEO_LINE_BEGIN()
#define VIDEO_LINE_END()
#define VIDEO_POINT_BEGIN()
#define VIDEO_POINT_END()
#define VIDEO_CLEAR_TARGET(color) sprite_clear_target(color)
#endif

#endif
