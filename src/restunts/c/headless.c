#include <stdarg.h>
#include "externs.h"
#include "memmgr.h"
#include "platform.h"
#include "fatal.h"
#include "car_speed.h"
#include "physics_internal.h"
#include "owoot.h"

extern void headless_exit(legacy_s16 result);

void add_exit_handler(void(far *exit_handler)(void))
{
	(void)exit_handler;
}

static void headless_write_text(const legacy_s8 *text)
{
	legacy_u16 length = 0;
	while (text[length] != 0) {
		length++;
	}
	(void)dos_write_stdout(text, length);
}

void fatal_error(const legacy_s8 *format, ...)
{
	headless_write_text(format);
	va_list arguments;
	for (legacy_u16 index = 0; format[index] != 0; index++) {
		if (format[index] == '%' && format[index + 1] == 's') {
			va_start(arguments, format);
			const legacy_s8 *detail = va_arg(arguments, const legacy_s8 *);
			va_end(arguments);
			headless_write_text(" ");
			headless_write_text(detail);
			break;
		}
	}
	headless_write_text("\r\n");
	headless_exit(1);
}

void init_div0(void)
{
	dos_install_divide_error_handler();
}

void init_main(legacy_s16 argc, legacy_s8 *argv[])
{
	configure_powergear_bug(argc, argv);
	configure_legacy_collision(argc, argv);
	configure_left_corner_bias(argc, argv);
	configure_amiga_grass(argc, argv);
	configure_owoot(argc, argv);
	init_video_geometry_flags();
	video_uses_page_flipping = 0;
	video_page_count = 1;
	textresprefix = 'e';
	framespersec = GAME_FRAME_RATE_NORMAL;
	mmgr_init_conventional_arena();
}

legacy_s16 kb_read_char(void)
{
	return 0;
}
