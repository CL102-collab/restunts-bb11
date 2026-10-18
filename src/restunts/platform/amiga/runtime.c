#include "amiga.h"
#include "../../c/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static legacy_u8 pumping;
static void platform_pump(legacy_u8 refresh)
{
	if (pumping) {
		return;
	}
	pumping = 1;
	amiga_input_pump();
	amiga_timer_pump();
	if (refresh) {
		amiga_video_refresh();
	}
	pumping = 0;
}
void amiga_platform_poll(void)
{
	platform_pump(0);
}
void amiga_platform_pump(void)
{
	platform_pump(1);
}
void amiga_platform_shutdown(void)
{
	dos_audio_shutdown();
	dos_timer_shutdown();
	amiga_video_shutdown();
	amiga_file_shutdown();
	amiga_memory_shutdown();
}
void *_memcpy(void *destination, const void *source, legacy_u16 length)
{
	return memcpy(destination, source, length);
}
void *__fmemcpy(void *destination, const void *source, legacy_u16 length)
{
	return memcpy(destination, source, length);
}
legacy_s16 dos_write_stdout(const legacy_s8 *text, legacy_u16 length)
{
	return fwrite(text, 1, length, stdout) == length ? (legacy_s16)length : -1;
}
legacy_s16 dos_write_stderr(const legacy_s8 *text, legacy_u16 length)
{
	legacy_s16 result;
#ifdef RESTUNTS_AMIGA_TRACE
	(void)fwrite(text, 1, length, stdout);
	fflush(stdout);
#endif
	result = fwrite(text, 1, length, stderr) == length ? (legacy_s16)length : -1;
	fflush(stderr);
	return result;
}
void dos_show_startup_error(const legacy_s8 *message)
{
	fputs((const char *)message, stderr);
}
void dos_process_exit(legacy_s16 status)
{
	exit(status);
}
void headless_exit(legacy_s16 status)
{
	dos_process_exit(status);
}
legacy_s16 dos_data_stack_segments_match(void)
{
	return 1;
}
void dos_install_divide_error_handler(void)
{ /* Shared arithmetic checks DOS divide overflow. */
}
void dos_interrupts_disable(void)
{ /* Timer callbacks only run synchronously while polling. */
}
void dos_interrupts_enable(void)
{
}
void dos_set_critical_error_handler(legacy_s16 (*callback)(void))
{
	(void)callback;
}
