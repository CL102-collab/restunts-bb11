#ifndef RESTUNTS_DOS_INTERRUPTS_H
#define RESTUNTS_DOS_INTERRUPTS_H

#include <dos.h>
#include <conio.h>
#include "../../c/legacy.h"

/* Handlers that access saved registers receive Watcom's INTPACK frame,
 * including reserved GS/FS slots even when generating 8086 instructions. */
typedef void interrupt(far *dos_interrupt_handler_type)();

#define peek(segment, offset) (*(legacy_u16 far *)MK_FP(segment, offset))
#define poke(segment, offset, value) (peek(segment, offset) = (legacy_u16)(value))
#define peekb(segment, offset) (*(legacy_u8 far *)MK_FP(segment, offset))
#define pokeb(segment, offset, value) (peekb(segment, offset) = (legacy_u8)(value))

/* Select startup resources and restore the caller's DOS drive/CWD on exit. */
legacy_s16 dos_startup_select_data_directory(const legacy_s8 *directory);
void dos_startup_restore_data_directory(void);

#endif
