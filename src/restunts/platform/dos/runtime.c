#include "../../c/legacy.h"
#include "../../c/platform.h"

#include <string.h>

#define DOS_RUNTIME_INTERRUPT 33
#define DOS_RUNTIME_WRITE_FUNCTION 64
#define DOS_RUNTIME_EXIT_FUNCTION 76
#define DOS_RUNTIME_STDOUT_HANDLE 1U
#define DOS_RUNTIME_STDERR_HANDLE 2U
#define DOS_RUNTIME_WRITE_ERROR (-1)

/* Watcom's -fpc helpers inspect these byte-sized runtime flags before choosing
 * hardware arithmetic. Keep them in software mode: our DOS entry point owns
 * startup and deliberately does not run the CRT's 8087 detection/initializers.
 * Defining the exact helper ABI symbols also avoids linking that CRT startup. */
legacy_u8 dos_software_float = 0;
legacy_u8 dos_software_real87 = 0;
#pragma aux dos_software_float "__8087"
#pragma aux dos_software_real87 "__real87"

/* Keep the game's legacy entry points while using the Watcom memory routines. */
void *_memcpy(void *destination, const void *source, legacy_u16 length)
{
	return memcpy(destination, source, length);
}

void far *__fmemcpy(void far *destination, const void far *source, legacy_u16 length)
{
	return _fmemcpy(destination, source, length);
}

static legacy_s16 dos_write_handle(legacy_u16 handle, const legacy_s8 *text, legacy_u16 length)
{
	legacy_s16 result;

	__asm {
		push    ds
		mov     ah, DOS_RUNTIME_WRITE_FUNCTION
		mov     bx, handle
		mov     cx, length
		mov     dx, text
		int     DOS_RUNTIME_INTERRUPT
		pop     ds
		jnc     write_finished
		mov     ax, DOS_RUNTIME_WRITE_ERROR
	write_finished:
		mov     result, ax
	}

	return result;
}

legacy_s16 dos_write_stdout(const legacy_s8 *text, legacy_u16 length)
{
	return dos_write_handle(DOS_RUNTIME_STDOUT_HANDLE, text, length);
}

legacy_s16 dos_write_stderr(const legacy_s8 *text, legacy_u16 length)
{
	return dos_write_handle(DOS_RUNTIME_STDERR_HANDLE, text, length);
}

void dos_show_startup_error(const legacy_s8 *message)
{
	dos_write_stderr(message, (legacy_u16)strlen((const legacy_char *)message));
}

void dos_process_exit(legacy_s16 status)
{
	__asm {
		mov     ax, status
		mov     ah, DOS_RUNTIME_EXIT_FUNCTION
		int     DOS_RUNTIME_INTERRUPT
	}

	for (;;)
	{
	}
}

legacy_s16 dos_data_stack_segments_match(void)
{
	/* Interrupt callbacks can arrive while foreign code owns SS.  Game code
	 * may only touch near data when the medium-model DS and SS agree. */
	legacy_s16 result;
	__asm {
		xor     ax, ax
		mov     bx, ss
		mov     dx, ds
		cmp     bx, dx
		jne     segments_differ
		inc     ax
	segments_differ:
		mov     result, ax
	}

	return result;
}
