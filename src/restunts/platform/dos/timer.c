#include "../../c/platform.h"

#include "dos_interrupts.h"
#include "../../c/fatal.h"

typedef void interrupt(far *interrupt_handler_type)(void);

#define DOS_TIMER_CALLBACK_CAPACITY 6U
#define DOS_TIMER_LAST_CALLBACK_INDEX 4U
#define DOS_TIMER_INTERRUPT_VECTOR 8
#define DOS_TIMER_VECTOR_OFFSET_ADDRESS 32
#define DOS_TIMER_VECTOR_SEGMENT_ADDRESS 34
#define DOS_TIMER_PIC_COMMAND_PORT 32U
#define DOS_TIMER_PIC_END_OF_INTERRUPT 32U
#define DOS_TIMER_PIC_MASK_PORT 33U
#define DOS_TIMER_IRQ_DISABLE_MASK 3U
#define DOS_TIMER_IRQ_ENABLE_MASK 252U
#define DOS_TIMER_PIT_COUNTER_PORT 64U
#define DOS_TIMER_PIT_CONTROL_PORT 67U
#define DOS_TIMER_PIT_CONTROL_WORD 182U
#define DOS_TIMER_PIT_DIVISOR_LOW_BYTE 156U
#define DOS_TIMER_PIT_DIVISOR_HIGH_BYTE 46U
#define DOS_TIMER_PIT_BIOS_DIVISOR_BYTE 0U
#define DOS_TIMER_SPEAKER_CONTROL_PORT 97U
#define DOS_TIMER_SPEAKER_CONTROL_CLEAR_MASK 252U
#define DOS_TIMER_DWORD_HIGH_WORD_OFFSET 2
enum DOS_TIMER_CALLBACK_EXECUTION_STATE {
	DOS_TIMER_CALLBACKS_IDLE = 0,
	DOS_TIMER_CALLBACKS_RUNNING = 1
};

static legacy_u32 dos_timer_counter;
static legacy_u32 dos_timer_realtime_counter;
static legacy_s16 dos_timer_callbacks_suspended;
void(far *dos_timer_callbacks[DOS_TIMER_CALLBACK_CAPACITY])(void);

static legacy_u32 dos_timer_last_counter;
static legacy_u16 dos_timer_slow_low;
static legacy_u16 dos_timer_slow_high;
static legacy_u16 dos_timer_divider_period;
static legacy_u16 dos_timer_divider;
static legacy_u16 dos_timer_max_reentry;
static legacy_u16 dos_timer_reentry;
static legacy_u8 dos_timer_in_callbacks;
static interrupt_handler_type previous_timer_interrupt;

static void interrupt dos_timer_interrupt(void);

static void dos_timer_chain_previous_handler(void)
{
	__asm {
		pushf
		call dword ptr previous_timer_interrupt
	}
}

static void dos_timer_increment_counter(legacy_u32 *counter)
{
	*counter = LEGACY_U32_WRAP_ADD(*counter, 1UL);
}

legacy_s16 dos_timer_register_callback(void(far *callback)(void))
{
	legacy_u16 callback_index;

	for (callback_index = 0; callback_index < DOS_TIMER_USABLE_CALLBACK_COUNT; callback_index++) {
		if (FP_SEG(dos_timer_callbacks[callback_index]) == 0U) {
			break;
		}
	}
	if (callback_index == DOS_TIMER_USABLE_CALLBACK_COUNT) {
		return DOS_TIMER_CALLBACK_REGISTRATION_FAILED;
	}

	_disable();
	dos_timer_callbacks[callback_index] = 0;
	dos_timer_callbacks[callback_index] = callback;
	dos_timer_callbacks[callback_index + 1U] = 0;
	_enable();
	return DOS_TIMER_CALLBACK_REGISTRATION_SUCCEEDED;
}

void dos_timer_unregister_callback(void(far *callback)(void))
{
	legacy_u16 callback_index;

	for (callback_index = 0; callback_index < DOS_TIMER_USABLE_CALLBACK_COUNT; callback_index++) {
		if (dos_timer_callbacks[callback_index] == callback) {
			break;
		}
	}
	if (callback_index == DOS_TIMER_USABLE_CALLBACK_COUNT) {
		return;
	}

	_disable();
	while (callback_index < DOS_TIMER_LAST_CALLBACK_INDEX) {
		dos_timer_callbacks[callback_index] = dos_timer_callbacks[callback_index + 1U];
		callback_index++;
	}
	dos_timer_callbacks[DOS_TIMER_LAST_CALLBACK_INDEX] = 0;
	_enable();
}

void dos_timer_reset_counter(void)
{
	dos_timer_counter = 0;
}

void dos_timer_set_callbacks_suspended(legacy_s16 suspended)
{
	dos_timer_callbacks_suspended = suspended;
}

static void interrupt dos_timer_interrupt(void)
{
	/* Match the original IRQ0 handler: allow nested interrupts after the
	 * compiler's interrupt prologue has saved the interrupted registers. */
	_enable();
	dos_timer_increment_counter(&dos_timer_realtime_counter);
	dos_timer_divider = (legacy_u16)(dos_timer_divider - 1U);
	if (LEGACY_S16_FROM_BITS(dos_timer_divider) <= 0) {
		dos_timer_slow_low = (legacy_u16)(dos_timer_slow_low + 1U);
		if (dos_timer_slow_low == 0) {
			dos_timer_slow_high = (legacy_u16)(dos_timer_slow_high + 1U);
		}
		dos_timer_divider = dos_timer_divider_period;

		dos_timer_chain_previous_handler();
	} else {
		outp(DOS_TIMER_PIC_COMMAND_PORT, DOS_TIMER_PIC_END_OF_INTERRUPT);
	}

	if (((legacy_u16)dos_timer_callbacks_suspended & DOS_TIMER_CALLBACK_SUSPENDED_MASK) != 0) {
		goto callbacks_finished;
	}

	dos_timer_increment_counter(&dos_timer_counter);
	_disable();
	if (dos_timer_in_callbacks == DOS_TIMER_CALLBACKS_IDLE) {
		dos_timer_in_callbacks = DOS_TIMER_CALLBACKS_RUNNING;
		_enable();
		for (legacy_u16 callback_index = 0; callback_index < DOS_TIMER_CALLBACK_CAPACITY;
			 callback_index++) {
			if (FP_SEG(dos_timer_callbacks[callback_index]) == 0U) {
				break;
			}
			dos_timer_callbacks[callback_index]();
		}
		goto callbacks_finished;
	}

	dos_timer_reentry = (legacy_u16)(dos_timer_reentry + 1U);
	legacy_s16 reentry_count = LEGACY_S16_FROM_BITS(dos_timer_reentry);
	if (LEGACY_S16_FROM_BITS(dos_timer_max_reentry) < reentry_count) {
		dos_timer_max_reentry = dos_timer_reentry;
	}
	return;

callbacks_finished:
	dos_timer_in_callbacks = DOS_TIMER_CALLBACKS_IDLE;
	dos_timer_reentry = 0;
}

static void dos_timer_write_vector(interrupt_handler_type handler)
{
	legacy_u16 handler_offset = FP_OFF(handler);
	legacy_u16 handler_segment = FP_SEG(handler);
	__asm {
		cli
		push    es
		xor     ax, ax
		mov     es, ax
		mov     ax, handler_offset
		mov     es:[DOS_TIMER_VECTOR_OFFSET_ADDRESS], ax
		mov     ax, handler_segment
		mov     es:[DOS_TIMER_VECTOR_SEGMENT_ADDRESS], ax
		pop     es
		sti
	}
}

void dos_timer_shutdown(void)
{
	interrupt_handler_type installed_handler = _dos_getvect(DOS_TIMER_INTERRUPT_VECTOR);
	if (installed_handler != dos_timer_interrupt) {
		return;
	}

	legacy_u8 interrupt_mask = (legacy_u8)inp(DOS_TIMER_PIC_MASK_PORT);
	outp(DOS_TIMER_PIC_MASK_PORT, interrupt_mask | DOS_TIMER_IRQ_DISABLE_MASK);
	dos_timer_write_vector(previous_timer_interrupt);
	interrupt_mask = (legacy_u8)inp(DOS_TIMER_PIC_MASK_PORT);
	outp(DOS_TIMER_PIC_MASK_PORT, interrupt_mask & DOS_TIMER_IRQ_ENABLE_MASK);
	outp(DOS_TIMER_PIT_COUNTER_PORT, DOS_TIMER_PIT_BIOS_DIVISOR_BYTE);
	outp(DOS_TIMER_PIT_COUNTER_PORT, DOS_TIMER_PIT_BIOS_DIVISOR_BYTE);
	outp(DOS_TIMER_SPEAKER_CONTROL_PORT,
		 inp(DOS_TIMER_SPEAKER_CONTROL_PORT) & DOS_TIMER_SPEAKER_CONTROL_CLEAR_MASK);
}

void dos_timer_setup_interrupt(void)
{
	dos_timer_divider_period = DOS_TIMER_DEFAULT_DIVIDER_PERIOD;
	dos_timer_divider = DOS_TIMER_DEFAULT_DIVIDER_PERIOD;

	_disable();
	dos_timer_in_callbacks = DOS_TIMER_CALLBACKS_IDLE;
	dos_timer_callbacks[0] = 0;
	_enable();

	outp(DOS_TIMER_SPEAKER_CONTROL_PORT,
		 inp(DOS_TIMER_SPEAKER_CONTROL_PORT) & DOS_TIMER_SPEAKER_CONTROL_CLEAR_MASK);
	outp(DOS_TIMER_PIT_CONTROL_PORT, DOS_TIMER_PIT_CONTROL_WORD);
	legacy_u8 interrupt_mask = (legacy_u8)inp(DOS_TIMER_PIC_MASK_PORT);
	outp(DOS_TIMER_PIC_MASK_PORT, interrupt_mask | DOS_TIMER_IRQ_DISABLE_MASK);

	interrupt_handler_type installed_handler = _dos_getvect(DOS_TIMER_INTERRUPT_VECTOR);
	if (installed_handler != dos_timer_interrupt) {
		previous_timer_interrupt = installed_handler;
		dos_timer_write_vector(dos_timer_interrupt);
	}

	interrupt_mask = (legacy_u8)inp(DOS_TIMER_PIC_MASK_PORT);
	outp(DOS_TIMER_PIC_MASK_PORT, interrupt_mask & DOS_TIMER_IRQ_ENABLE_MASK);
	outp(DOS_TIMER_PIT_COUNTER_PORT, DOS_TIMER_PIT_DIVISOR_LOW_BYTE);
	outp(DOS_TIMER_PIT_COUNTER_PORT, DOS_TIMER_PIT_DIVISOR_HIGH_BYTE);
	add_exit_handler(dos_timer_shutdown);
}

legacy_u32 dos_timer_get_realtime_counter(void)
{
	legacy_u32 result;
	__asm {
		pushf
		cli
		mov ax, word ptr dos_timer_realtime_counter
		mov dx, word ptr dos_timer_realtime_counter+DOS_TIMER_DWORD_HIGH_WORD_OFFSET
		popf
		mov word ptr result, ax
		mov word ptr result+DOS_TIMER_DWORD_HIGH_WORD_OFFSET, dx
	}
	return result;
}

legacy_u32 timer_get_counter(void)
{
	/* The DOS timer interrupt can update either half between ordinary C
	 * loads.  Keep interrupts disabled for the paired 16-bit read. */
	legacy_u32 result;
	__asm {
		cli
		mov     ax, word ptr dos_timer_counter
		mov     dx, word ptr dos_timer_counter+DOS_TIMER_DWORD_HIGH_WORD_OFFSET
		sti
		mov     word ptr result, ax
		mov     word ptr result+DOS_TIMER_DWORD_HIGH_WORD_OFFSET, dx
	}

	return result;
}

legacy_u32 timer_get_delta(void)
{
	/* Read and update the 32-bit counters as the original 8086 routine did;
	 * ordinary C reads can be split into independently interruptible words. */
	legacy_u32 result;
	__asm {
		mov     bx, word ptr dos_timer_last_counter
		mov     cx, word ptr dos_timer_last_counter+DOS_TIMER_DWORD_HIGH_WORD_OFFSET
		cli
		mov     ax, word ptr dos_timer_counter
		mov     dx, word ptr dos_timer_counter+DOS_TIMER_DWORD_HIGH_WORD_OFFSET
		sti
		mov     word ptr dos_timer_last_counter, ax
		mov     word ptr dos_timer_last_counter+DOS_TIMER_DWORD_HIGH_WORD_OFFSET, dx
		sub     ax, bx
		sbb     dx, cx
		mov     word ptr result, ax
		mov     word ptr result+DOS_TIMER_DWORD_HIGH_WORD_OFFSET, dx
	}

	return result;
}

legacy_u32 timer_get_slow_counter(void)
{
	/* This counter advances when the interrupt divider expires, rather than
	 * on every hardware timer interrupt. */
	legacy_u32 result;
	__asm {
		cli
		mov     ax, dos_timer_slow_low
		mov     dx, dos_timer_slow_high
		sti
		mov     word ptr result, ax
		mov     word ptr result+DOS_TIMER_DWORD_HIGH_WORD_OFFSET, dx
	}

	return result;
}
