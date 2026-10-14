#include "command_line.h"
#include "platform.h"

#ifndef RESTUNTS_GIT_HASH
#define RESTUNTS_GIT_HASH "source-archive"
#endif
#ifndef RESTUNTS_BUILD_DATE
#define RESTUNTS_BUILD_DATE __DATE__
#endif

#define COMMAND_LINE_GIT_HASH_LENGTH 7U
#define COMMAND_LINE_FIRST_OPTION_INDEX 1
#define COMMAND_LINE_DATA_DIRECTORY_ARGUMENT_COUNT 2

#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
#define COMMAND_LINE_NEWLINE "\n"
#else
#define COMMAND_LINE_NEWLINE "\r\n"
#endif

static const legacy_s8 command_line_version_prefix[] =
	"Chocolate Stunts (Restunts)" COMMAND_LINE_NEWLINE "Version ";
static const legacy_s8 command_line_revision[] = RESTUNTS_GIT_HASH;
static const legacy_s8 command_line_version_suffix[] =
	" (" RESTUNTS_BUILD_DATE ")" COMMAND_LINE_NEWLINE;

static const legacy_s8 command_line_help[] = COMMAND_LINE_NEWLINE
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
	"Usage: restunts [--data-dir DIR] [options]" COMMAND_LINE_NEWLINE
#else
	"Usage: restunts.exe [options]" COMMAND_LINE_NEWLINE
#endif
	COMMAND_LINE_NEWLINE "  --help, -h       Show this help and exit." COMMAND_LINE_NEWLINE
	"  --version, -v    Show the build version and exit." COMMAND_LINE_NEWLINE
#if defined(RESTUNTS_SDL3) && !defined(__DJGPP__)
	"  --licenses       Show dependency notices and exit; use alone." COMMAND_LINE_NEWLINE
#endif
	COMMAND_LINE_NEWLINE
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
	"  --data-dir DIR   Read game data and saves in DIR; must be first." COMMAND_LINE_NEWLINE
#endif
	"  --nointro        Skip the startup intro." COMMAND_LINE_NEWLINE
#ifdef RESTUNTS_SDL3
	"  --ogg:on|off     Start with Ogg replacements enabled/disabled; supply "
	"once." COMMAND_LINE_NEWLINE
	"  --fps:on|off     Start with FPS display enabled/disabled; supply once." COMMAND_LINE_NEWLINE
#endif
	COMMAND_LINE_NEWLINE
	"  --pg:on|off      Enable/disable the original power gear bug." COMMAND_LINE_NEWLINE
	"  --lc:on|off      Enable/disable original collision behavior." COMMAND_LINE_NEWLINE
	"  --lcb:on|off     Enable/disable the original left corner bias." COMMAND_LINE_NEWLINE
	"  --owoot          Enable One Wheel On/Over Track driving "
	"rules." COMMAND_LINE_NEWLINE COMMAND_LINE_NEWLINE
#ifdef RESTUNTS_SDL3
	"  --hv:auto        Enable HyperVision with automatic quality." COMMAND_LINE_NEWLINE
	"  --hv:full        Lock HyperVision: full track at 1280x800." COMMAND_LINE_NEWLINE
	"  --hv:high        Lock HyperVision: large visibility mask at 1280x800." COMMAND_LINE_NEWLINE
	"  --hv:medium      Lock HyperVision: large visibility mask at 640x400." COMMAND_LINE_NEWLINE
	"  --hv:low         Lock HyperVision: large visibility mask at 320x200." COMMAND_LINE_NEWLINE
	"                   Supply exactly one preset." COMMAND_LINE_NEWLINE COMMAND_LINE_NEWLINE
#endif
	"  /ns              Start with music and sound effects disabled." COMMAND_LINE_NEWLINE
#ifdef RESTUNTS_SDL3
	"  /sXX             Legacy audio selection; SDL3 always uses AdLib." COMMAND_LINE_NEWLINE
	"  /h               Legacy video switch; no alternate mode in SDL3." COMMAND_LINE_NEWLINE
#elif defined(RESTUNTS_AMIGA)
	"  /sXX             Legacy selection; Amiga uses Paula with AdLib instruments." COMMAND_LINE_NEWLINE
	"  /h               Legacy switch; Amiga always uses 320x200, 32 colors." COMMAND_LINE_NEWLINE
#else
	"  /sXX             Load DOS audio driver XX15.DRV." COMMAND_LINE_NEWLINE
	"                   IDs: AD (AdLib), PC (PC speaker), MT (MT-32), TD." COMMAND_LINE_NEWLINE
	"                   SB selects AdLib FM for Sound Blaster cards." COMMAND_LINE_NEWLINE
	"  /h               Request legacy Hercules/mode-4 initialization." COMMAND_LINE_NEWLINE
#endif
	"  /nd              Recognized legacy switch with no "
	"effect." COMMAND_LINE_NEWLINE COMMAND_LINE_NEWLINE
	"Defaults: intro and sound on; --pg:on, --lc:on, --lcb:on; --owoot off." COMMAND_LINE_NEWLINE
#ifdef RESTUNTS_SDL3
	"HyperVision starts off. F12 and Shift+F12 change it during play." COMMAND_LINE_NEWLINE
	"Ogg music starts off. Shift+F10 switches music sources outside races." COMMAND_LINE_NEWLINE
	"FPS display starts off. F11 toggles it during play." COMMAND_LINE_NEWLINE
#elif defined(RESTUNTS_AMIGA)
	"Amiga 500+: 68000, ECS, 320x200, 32 colors, four Paula voices." COMMAND_LINE_NEWLINE
#else
	"AdLib is the default driver. F12 toggles SuperSight; no presets." COMMAND_LINE_NEWLINE
#endif
	"Physics switches are case-insensitive; the last on/off value wins." COMMAND_LINE_NEWLINE
#ifdef RESTUNTS_SDL3
	"HyperVision presets and Ogg music options are also case-insensitive." COMMAND_LINE_NEWLINE
	"FPS display options are case-insensitive." COMMAND_LINE_NEWLINE
#endif
	"Other switch names are case-sensitive; keep legacy names lowercase." COMMAND_LINE_NEWLINE
	"For example, /sAD uses lowercase s and a two-letter audio driver ID." COMMAND_LINE_NEWLINE
	"The first --help/-h or --version/-v request wins." COMMAND_LINE_NEWLINE;

static legacy_s16 command_line_matches(const legacy_s8 *argument, const legacy_char *option)
{
	while (*argument != 0 && *argument == *option) {
		argument++;
		option++;
	}
	return *argument == 0 && *option == 0;
}

static legacy_u16 command_line_revision_length(void)
{
	legacy_u16 length = 0;
	legacy_u8 is_hash = 1;
	while (command_line_revision[length] != 0) {
		legacy_s8 digit = command_line_revision[length++];
		if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f') ||
			  (digit >= 'A' && digit <= 'F'))) {
			is_hash = 0;
		}
	}
	return is_hash != 0 && length > COMMAND_LINE_GIT_HASH_LENGTH ? COMMAND_LINE_GIT_HASH_LENGTH
																 : length;
}

static legacy_s16 command_line_write(const legacy_s8 *text, legacy_u16 length)
{
	return dos_write_stdout(text, length) == (legacy_s16)length ? COMMAND_LINE_SUCCESS
																: COMMAND_LINE_FAILURE;
}

static legacy_s16 command_line_print_version(void)
{
	if (command_line_write(command_line_version_prefix, sizeof(command_line_version_prefix) - 1U) !=
			COMMAND_LINE_SUCCESS ||
		command_line_write(command_line_revision, command_line_revision_length()) !=
			COMMAND_LINE_SUCCESS ||
		command_line_write(command_line_version_suffix, sizeof(command_line_version_suffix) - 1U) !=
			COMMAND_LINE_SUCCESS) {
		return COMMAND_LINE_FAILURE;
	}
	return COMMAND_LINE_SUCCESS;
}

legacy_s16 command_line_print_info(legacy_s32 argc, legacy_s8 *argv[])
{
	legacy_s32 first_option = COMMAND_LINE_FIRST_OPTION_INDEX;
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
	if (argc > first_option && command_line_matches(argv[first_option], "--data-dir")) {
		first_option += COMMAND_LINE_DATA_DIRECTORY_ARGUMENT_COUNT;
	}
#endif
	for (legacy_s32 index = first_option; index < argc; index++) {
		legacy_s16 show_help =
			command_line_matches(argv[index], "--help") || command_line_matches(argv[index], "-h");
		if (show_help != 0 || command_line_matches(argv[index], "--version") ||
			command_line_matches(argv[index], "-v")) {
			if (command_line_print_version() != COMMAND_LINE_SUCCESS) {
				return COMMAND_LINE_FAILURE;
			}
			if (show_help != 0) {
				return command_line_write(command_line_help, sizeof(command_line_help) - 1U);
			}
			return COMMAND_LINE_SUCCESS;
		}
	}
	return COMMAND_LINE_CONTINUE;
}
