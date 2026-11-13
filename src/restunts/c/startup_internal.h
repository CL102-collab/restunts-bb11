#ifndef RESTUNTS_STARTUP_INTERNAL_H
#define RESTUNTS_STARTUP_INTERNAL_H

#include "legacy.h"

#define GAME_SCREEN_WIDTH 320
#define GAME_SCREEN_HEIGHT 200

#define RANDOM_WAIT_SPIN_LIMIT 12000
#define RANDOM_WAIT_RESOURCE_INDEX 1024
#define NO_OPPONENT_CAR_ID 255U

#define CALLBACK_GRAPHICS_MENU_KEY 7
#define CALLBACK_JOYSTICK_HELP_KEY 10
#define CALLBACK_KEYBOARD_HELP_KEY 11
#define CALLBACK_MOUSE_HELP_KEY 12800
#define CALLBACK_PAUSE_HELP_KEY 16
#define CALLBACK_DOS_HELP_KEY 17
#define CALLBACK_SOUND_HELP_KEY 19
#define CALLBACK_DOS_HELP_ALT_KEY 24

#define STARTUP_NO_SOUND_OPTION "--nosound"
#define STARTUP_OGG_MUSIC_PREFIX "--ogg:"
#define STARTUP_SOUND_MODE_PREFIX "--soundmode:"
#define STARTUP_SOUND_ADLIB 0U
#define STARTUP_SOUND_MT32 1U
#define STARTUP_SOUND_PC_SPEAKER 2U
#define STARTUP_MT32_ROM_DIRECTORY_OPTION "--mt-32-rom-dir"
#define STARTUP_MT32_MODEL_OPTION "--mt-32-model"
#if defined(RESTUNTS_SDL3) && !defined(__DJGPP__)
#define STARTUP_MULTI_CORE_PREFIX "--multi-core:"
#define STARTUP_MULTI_CORE_OFF 0U
#define STARTUP_MULTI_CORE_ON 1U
#endif
#if defined(RESTUNTS_SDL3) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) &&                 \
	!defined(__DJGPP__)
#define STARTUP_FULLSCREEN_PREFIX "--fullscreen:"
#endif
#if defined(RESTUNTS_SDL3) && !defined(__ANDROID__)
#define STARTUP_CONTROLLER_VIBRATION_PREFIX "--controller-vibration:"
#endif
#define STARTUP_FPS_DISPLAY_PREFIX "--fps:"
#define STARTUP_MPH_DISPLAY_PREFIX "--showmph:"
#define STARTUP_SKIP_INTRO_OPTION "--nointro"
#define STARTUP_WAITING_MESSAGE_Y 180
#if defined(RESTUNTS_SDL3) && (defined(__ANDROID__) || defined(__EMSCRIPTEN__))
#define STARTUP_CONTROL_LAYOUT_PREFIX "--control-layout:"
#define STARTUP_CONTROL_LAYOUT_OFF 0U
#define STARTUP_CONTROL_LAYOUT_ON 1U
#endif

#define TRACK_PATH_STORAGE_SIZE 81
#define TRACK_PRIMARY_PATH_OFFSET REPLAY_TRACK_SIZE
#define TRACK_SECONDARY_PATH_OFFSET (REPLAY_TRACK_SIZE + TRACK_PATH_STORAGE_SIZE)

struct STARTUP_OPTIONS {
	legacy_u8 mode4_requested;
	legacy_u8 sound_disabled;
	legacy_u8 skip_intro;
	legacy_u8 fps_display_enabled;
	legacy_u8 fps_display_specified;
	legacy_u8 mph_display_enabled;
	legacy_u8 mph_display_specified;
#ifdef RESTUNTS_SDL3
	legacy_u8 sound_mode;
	const legacy_s8 *mt32_rom_directory;
	const legacy_s8 *mt32_model;
	legacy_u16 hires_preset;
	legacy_u8 hires_specified;
	legacy_u8 ogg_music_enabled;
	legacy_u8 ogg_music_specified;
#ifndef __DJGPP__
	legacy_u8 multi_core_enabled;
	legacy_u8 multi_core_specified;
#endif
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) && !defined(__DJGPP__)
	legacy_u8 fullscreen_enabled;
	legacy_u8 fullscreen_specified;
#endif
#ifndef __ANDROID__
	legacy_u8 controller_vibration_enabled;
	legacy_u8 controller_vibration_specified;
#endif
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
	legacy_u8 control_layout_enabled;
	legacy_u8 control_layout_specified;
#endif
#endif
};

extern struct STARTUP_OPTIONS startup_options;
legacy_s16 random_wait(void);
void copy_material_list_pointers(void *clrlist, void *clrlist2, void *patlist, void *patlist2);

#ifdef RESTUNTS_AMIGA_TRACE
#include <stdio.h>
#define STARTUP_TRACE(message)                                                                     \
	do {                                                                                           \
		puts("Amiga startup: " message);                                                           \
		fflush(stdout);                                                                            \
	} while (0)
#else
#define STARTUP_TRACE(message) ((void)0)
#endif

#endif
