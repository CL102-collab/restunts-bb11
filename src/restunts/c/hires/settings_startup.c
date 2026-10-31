#include "settings.h"

#ifdef RESTUNTS_SDL3
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include "frame_adaptive.h"
#include "hires.h"
#include "../externs.h"
#include "../game_input.h"
#include "../platform.h"
#include "../startup_internal.h"

#define STARTUP_HYPERVISION_PREFIX "--hv:"

void hires_settings_reset_startup(struct STARTUP_OPTIONS *options)
{
	options->hypervision_preset = FRAME_ADAPTIVE_PRESET_AUTO;
	options->hypervision_specified = 0;
}

void hires_settings_parse_startup(const legacy_s8 *argument, struct STARTUP_OPTIONS *options)
{
	const legacy_s8 *prefix = STARTUP_HYPERVISION_PREFIX;
	const legacy_s8 *value = argument;
	while (*prefix != 0 && tolower((legacy_u8)*value) == *prefix) {
		prefix++;
		value++;
	}
	if (*prefix != 0) {
		return;
	}
	if (options->hypervision_specified != 0) {
		fprintf(stderr, "Only one HyperVision preset may be specified (--hv:).\n");
		dos_process_exit(EXIT_FAILURE);
		return;
	}
	static const struct {
		const legacy_s8 *name;
		enum FRAME_ADAPTIVE_PRESET preset;
	} presets[] = {{"auto", FRAME_ADAPTIVE_PRESET_AUTO},
				   {"full", FRAME_ADAPTIVE_PRESET_FULL},
				   {"high", FRAME_ADAPTIVE_PRESET_HIGH},
				   {"medium", FRAME_ADAPTIVE_PRESET_MEDIUM},
				   {"low", FRAME_ADAPTIVE_PRESET_LOW}};
	for (legacy_u16 index = 0; index < sizeof(presets) / sizeof(presets[0]); index++) {
		if (stricmp(value, presets[index].name) == 0) {
			options->hypervision_preset = presets[index].preset;
			options->hypervision_specified = 1;
			return;
		}
	}
	fprintf(stderr,
			"Invalid HyperVision preset '%s'; use --hv:auto, --hv:full, --hv:high, "
			"--hv:medium or --hv:low.\n",
			argument);
	dos_process_exit(EXIT_FAILURE);
}

void hires_settings_apply_startup(const struct STARTUP_OPTIONS *options)
{
	if (options->hypervision_specified != 0) {
		frame_adaptive_set_preset(&frame_adaptive,
								  (enum FRAME_ADAPTIVE_PRESET)options->hypervision_preset);
		supersight_enabled = 1;
		hires_set_enabled(supersight_enabled);
		hires_set_render_scale(frame_adaptive_render_scale(&frame_adaptive));
	}
}
#endif
