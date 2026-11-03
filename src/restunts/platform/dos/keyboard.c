#include "dos_interrupts.h"
#include "keyboard.h"
#include "../../c/platform.h"
#include "../../c/fatal.h"
#include "../../c/game_input.h"

typedef void interrupt(far *voidinterruptfunctype)();

static voidinterruptfunctype old_kb_int9_handler;
static voidinterruptfunctype old_kb_int16_handler;

#define DOS_KB_SCANCODE_COUNT 90
#define DOS_KB_BUFFER_CAPACITY 64
#define DOS_KB_BUFFER_ENTRY_BYTES 2
#define DOS_KB_DATA_PORT 96U
#define DOS_KB_CONTROL_PORT 97U
#define DOS_KB_INTERRUPT_ACK_MASK 128U
#define DOS_KB_BREAK_CODE_MASK 128U
#define DOS_KB_LEFT_SHIFT_SCANCODE 42U
#define DOS_KB_RIGHT_SHIFT_SCANCODE 54U
#define DOS_KB_CAPS_LOCK_SCANCODE 58U
#define DOS_KB_ASCII_BYTE_SHIFT LEGACY_BYTE_BITS
#define DOS_PIC_COMMAND_PORT 32U
#define DOS_PIC_END_OF_INTERRUPT 32U
#define DOS_PIC_MASK_PORT 33U
#define DOS_KB_IRQ_MASK 3U
#define DOS_KB_HARDWARE_INTERRUPT_VECTOR 9
#define DOS_KB_BIOS_INTERRUPT_VECTOR 22
#define DOS_KB_BIOS_DATA_SEGMENT 64U
#define DOS_KB_BIOS_FLAGS_OFFSET 23U
#define DOS_KB_BIOS_FLAGS_LINEAR_OFFSET 1047U
#define DOS_KB_NUM_LOCK_FLAG 32U
#define DOS_KB_NUM_LOCK_CLEAR_MASK 223U
#define DOS_KB_BIOS_SHIFT_FLAGS_CLEAR_MASK 240U
#define DOS_KB_X86_ZERO_FLAG 64U

enum DOS_KEYBOARD_BIOS_FUNCTION {
	DOS_KB_BIOS_READ_FUNCTION = 0,
	DOS_KB_BIOS_STATUS_FUNCTION = 1,
	DOS_KB_BIOS_SHIFT_STATUS_FUNCTION = 2
};

static legacy_u8 dos_kb_input[DOS_KB_SCANCODE_COUNT];

static legacy_u16 dos_kb_buffer_write;
static legacy_u16 dos_kb_buffer_read;
static legacy_u16 dos_kb_buffer_size = DOS_KB_BUFFER_ENTRY_BYTES;
static legacy_u16 dos_kb_buffer_count;
static legacy_u16 dos_kb_buffer[DOS_KB_BUFFER_CAPACITY];

static const legacy_u8 dos_kb_keymap1[DOS_KB_PRIMARY_KEYMAP_SIZE] = {
	0,	 27,  49,  50,	51,	 52,  53,  54,	55,	 56,  57,  48,	45,	 61,  8,   9,	113, 119, 101,
	114, 116, 121, 117, 105, 111, 112, 91,	93,	 13,  0,   97,	115, 100, 102, 103, 104, 106, 107,
	108, 59,  39,  96,	0,	 92,  122, 120, 99,	 118, 98,  110, 109, 44,  46,  47,	0,	 42,  0,
	32,	 0,	  187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 0,	 0,	  199, 200, 201, 45,  203,
	204, 205, 43,  207, 208, 209, 210, 211, 0,	 0,	  0,   0,	0,	 0,	  0};

static const legacy_u8 dos_kb_keymap2[DOS_KB_PRIMARY_KEYMAP_SIZE] = {
	0,	 27,  33,  64,	35,	 36,  37,  94,	38,	 42,  40,  41,	95, 43, 8,	 143, 81,  87, 69,
	82,	 84,  89,  85,	73,	 79,  80,  123, 125, 13,  0,   65,	83, 68, 70,	 71,  72,  74, 75,
	76,	 58,  34,  126, 0,	 124, 90,  88,	67,	 86,  66,  78,	77, 60, 62,	 63,  0,   0,  0,
	32,	 0,	  212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 0,	0,	199, 200, 201, 45, 203,
	204, 205, 43,  207, 208, 209, 210, 211, 0,	 0,	  0,   0,	0,	0,	0};

static const legacy_u8 dos_kb_keymap3[DOS_KB_PRIMARY_KEYMAP_SIZE] = {
	0,	 27,  49,  50,	51,	 52,  53,  54,	55,	 56,  57,  48,	45, 61, 8,	 143, 81,  87, 69,
	82,	 84,  89,  85,	73,	 79,  80,  91,	93,	 13,  0,   65,	83, 68, 70,	 71,  72,  74, 75,
	76,	 59,  39,  96,	0,	 92,  90,  88,	67,	 86,  66,  78,	77, 44, 46,	 47,  0,   0,  0,
	32,	 0,	  212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 0,	0,	199, 200, 201, 45, 203,
	204, 205, 43,  207, 208, 209, 210, 211, 0,	 0,	  0,   0,	0,	0,	0};

static const legacy_u8 dos_kb_keymap4[DOS_KB_PRIMARY_KEYMAP_SIZE] = {
	0,	 27,  33,  0,	35,	 36,  37,  30,	38,	 42,  40,  41,	31,	 43, 127, 9,   17,	23, 5,
	18,	 20,  25,  21,	9,	 15,  16,  27,	29,	 13,  0,   1,	19,	 4,	 6,	  7,   8,	10, 11,
	12,	 59,  44,  96,	0,	 28,  26,  24,	3,	 22,  2,   14,	178, 60, 62,  63,  0,	0,	0,
	32,	 0,	  222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 0,	 0,	 199, 200, 201, 45, 203,
	204, 205, 43,  207, 208, 209, 210, 211, 0,	 0,	  0,   0,	0,	 0,	 0};

static const legacy_u8 dos_kb_keymap5[DOS_KB_ALT_KEYMAP_SIZE] = {
	0,	 27,  33,  64,	35,	 36,  37,  94,	38,	 42,  40,  41,	95,	 43,  8,   143, 144, 145, 146,
	147, 148, 149, 150, 151, 152, 153, 123, 125, 13,  0,   158, 159, 160, 161, 162, 163, 164, 165,
	166, 58,  34,  126, 0,	 124, 172, 173, 174, 175, 176, 177, 178, 60,  62,  63,	0,	 0,	  0,
	32,	 0,	  248, 249, 250, 251, 252, 253, 254, 255, 128, 129, 0,	 0,	  199, 200, 201, 45,  203,
	204, 205, 43,  207, 208, 209, 210, 211, 0,	 0,	  0,   0,	0,	 0,	  0,   0};

// The original opens with `sti` before it touches anything, so the rest of
// the handler runs with interrupts on and only the buffer update is fenced by
// cli/sti. This C handler leaves interrupts disabled through its prologue.
// Only latency differs; no code here depends on being re-entered.
void interrupt kb_int9_handler(void)
{
	legacy_u8 kbc = inp(DOS_KB_DATA_PORT);
	legacy_u8 kbp = inp(DOS_KB_CONTROL_PORT);
	outp(DOS_KB_CONTROL_PORT, kbp | DOS_KB_INTERRUPT_ACK_MASK);
	outp(DOS_KB_CONTROL_PORT, kbp);

	if ((kbc & DOS_KB_BREAK_CODE_MASK) == 0) {
		if (kbc >= DOS_KB_SCANCODE_COUNT) {
			kbc = 0;
		}
		legacy_u8 was_pressed = dos_kb_input[kbc];
		dos_kb_input[kbc] = 1;

		legacy_u16 kbval;
		if (kbc == DOS_KB_F11_SCANCODE || kbc == DOS_KB_F12_SCANCODE) {
			/* Toggle once per physical press; F11 also accepts either Shift key. */
			if (was_pressed != 0 || dos_kb_input[DOS_KB_ALT_SCANCODE] != 0 ||
				dos_kb_input[DOS_KB_CONTROL_SCANCODE] != 0 ||
				(kbc == DOS_KB_F12_SCANCODE && (dos_kb_input[DOS_KB_LEFT_SHIFT_SCANCODE] != 0 ||
												dos_kb_input[DOS_KB_RIGHT_SHIFT_SCANCODE] != 0))) {
				goto acknowledge_interrupt;
			}
			kbval = (legacy_u16)(kbc == DOS_KB_F11_SCANCODE ? KEY_F11 : KEY_F12);
		} else if (dos_kb_input[DOS_KB_ALT_SCANCODE] == 1) {
			kbval = dos_kb_keymap5[kbc];
		} else if (dos_kb_input[DOS_KB_CONTROL_SCANCODE] == 1) {
			kbval = dos_kb_keymap4[kbc];
		} else if (dos_kb_input[DOS_KB_LEFT_SHIFT_SCANCODE] == 1 ||
				   dos_kb_input[DOS_KB_RIGHT_SHIFT_SCANCODE] == 1) {
			/* Either shift key. */
			kbval = dos_kb_keymap2[kbc];
		} else if (dos_kb_input[DOS_KB_CAPS_LOCK_SCANCODE] == 1) {
			kbval = dos_kb_keymap3[kbc];
		} else {
			kbval = dos_kb_keymap1[kbc];
		}

		if ((kbval & DOS_KB_EXTENDED_KEY_FLAG) != 0) {
			if (kbval >= DOS_KB_EXTENDED_KEY_NORMALIZE_MIN) {
				kbval &= DOS_KB_SCANCODE_MASK;
			}
			kbval <<= DOS_KB_ASCII_BYTE_SHIFT;
		}

		legacy_u16 kbdata = dos_kb_buffer_write;
		_disable();
		dos_kb_buffer[kbdata / DOS_KB_BUFFER_ENTRY_BYTES] = kbval;
		kbdata += DOS_KB_BUFFER_ENTRY_BYTES;
		if (kbdata >= dos_kb_buffer_size) { // data3 = kb_buffer_pos
			kbdata = 0;
		}
		dos_kb_buffer_write = kbdata;

		kbdata = dos_kb_buffer_count;
		kbdata += DOS_KB_BUFFER_ENTRY_BYTES;
		if (kbdata > dos_kb_buffer_size) {
			kbdata = dos_kb_buffer_size;
			dos_kb_buffer_read = dos_kb_buffer_write;
		}
		dos_kb_buffer_count = kbdata;
		_enable();

	} else {
		kbc &= DOS_KB_SCANCODE_MASK;
		if (kbc >= DOS_KB_SCANCODE_COUNT) {
			kbc = 0;
		}
		dos_kb_input[kbc] = 0;
	}

acknowledge_interrupt:
	outp(DOS_PIC_COMMAND_PORT, DOS_PIC_END_OF_INTERRUPT);
}

// The original returns with the status flags left by its final XOR, SUB, CMP
// or OR, plus IF set by STI. Capture those operations directly instead of
// merging only ZF into the caller's saved flags. The interrupt epilogue uses
// the flags in the compiler's saved-register frame when executing IRET.
static legacy_u16 kb_flags_after_zero(void)
{
	legacy_u16 result;
	__asm {
		xor ax, ax
		pushf
		pop ax
		mov result, ax
	}
	return result;
}

static legacy_u16 kb_flags_after_subtract_two(legacy_u16 value)
{
	legacy_u16 result;
	__asm {
		mov ax, value
		sub ax, DOS_KB_BUFFER_ENTRY_BYTES
		pushf
		pop ax
		mov result, ax
	}
	return result;
}

static legacy_u16 kb_flags_after_compare_zero(legacy_u16 value)
{
	legacy_u16 result;
	__asm {
		mov ax, value
		cmp ax, 0
		pushf
		pop ax
		mov result, ax
	}
	return result;
}

static legacy_u16 kb_flags_after_or(legacy_u8 left, legacy_u8 right)
{
	legacy_u16 result;
	__asm {
		mov al, left
		or al, right
		pushf
		pop ax
		mov result, ax
	}
	return result;
}

void interrupt kb_int16_handler(union INTPACK registers)
{
	legacy_u8 bioscall = registers.w.ax >> LEGACY_BYTE_BITS;
	_disable();
	legacy_u16 kbdata;
	legacy_u16 result;
	if (bioscall == DOS_KB_BIOS_READ_FUNCTION) {
		kbdata = dos_kb_buffer_count;
		if (kbdata == 0) {
			_enable();
			registers.w.ax = 0;
			registers.w.flags = kb_flags_after_zero();
			return;
		}
		kbdata = dos_kb_buffer_read;
		result = dos_kb_buffer[kbdata / DOS_KB_BUFFER_ENTRY_BYTES];
		kbdata += DOS_KB_BUFFER_ENTRY_BYTES;
		if (kbdata >= dos_kb_buffer_size) {
			kbdata = 0;
		}
		dos_kb_buffer_read = kbdata;
		kbdata = dos_kb_buffer_count;
		dos_kb_buffer_count = kbdata - DOS_KB_BUFFER_ENTRY_BYTES;
		_enable();
		registers.w.ax = result;
		registers.w.flags = kb_flags_after_subtract_two(kbdata);
		return;
	}

	if (bioscall == DOS_KB_BIOS_STATUS_FUNCTION) {
		kbdata = dos_kb_buffer_count;
		if (kbdata == 0) {
			_enable();
			registers.w.ax = 0;
			registers.w.flags = kb_flags_after_zero();
			return;
		}
		result = dos_kb_buffer[dos_kb_buffer_read / DOS_KB_BUFFER_ENTRY_BYTES];
		_enable();
		registers.w.ax = result;
		registers.w.flags = kb_flags_after_compare_zero(kbdata);
		return;
	}

	if (bioscall == DOS_KB_BIOS_SHIFT_STATUS_FUNCTION) {
		legacy_u8 shiftleft = dos_kb_input[DOS_KB_LEFT_SHIFT_SCANCODE];
		legacy_u8 shiftright = dos_kb_input[DOS_KB_RIGHT_SHIFT_SCANCODE];
		result = shiftleft | shiftright;
		_enable();
		registers.w.ax = result & LEGACY_U8_MAX;
		registers.w.flags = kb_flags_after_or(shiftleft, shiftright);
		return;
	}
	_enable();
	registers.w.ax = 0;
	registers.w.flags = kb_flags_after_zero();
	//return 0;
}

void kb_init_interrupt(void)
{
	legacy_u8 irqmask = inp(DOS_PIC_MASK_PORT);
	outp(DOS_PIC_MASK_PORT, irqmask | DOS_KB_IRQ_MASK);

	// The original compares only the offset word read from vector 9.
	voidinterruptfunctype current_kb_int9_handler = _dos_getvect(DOS_KB_HARDWARE_INTERRUPT_VECTOR);
	if (FP_OFF(current_kb_int9_handler) != FP_OFF(kb_int9_handler)) {
		old_kb_int9_handler = current_kb_int9_handler;
		_dos_setvect(DOS_KB_HARDWARE_INTERRUPT_VECTOR, kb_int9_handler);

		old_kb_int16_handler = _dos_getvect(DOS_KB_BIOS_INTERRUPT_VECTOR);
		_dos_setvect(DOS_KB_BIOS_INTERRUPT_VECTOR, kb_int16_handler);
	}

	outp(DOS_PIC_MASK_PORT, irqmask);

	/* Clear every tracked hardware scancode state. */
	for (legacy_s16 i = 0; i < DOS_KB_SCANCODE_COUNT; i++) {
		dos_kb_input[i] = 0;
	}

	add_exit_handler(kb_exit_handler);
}

void kb_exit_handler(void)
{
	legacy_u8 irqmask = inp(DOS_PIC_MASK_PORT);
	outp(DOS_PIC_MASK_PORT, irqmask | DOS_KB_IRQ_MASK);

	// The original guards this block with the saved offset word alone.
	if (FP_OFF(old_kb_int9_handler) != 0) {
		_dos_setvect(DOS_KB_HARDWARE_INTERRUPT_VECTOR, old_kb_int9_handler);
		_dos_setvect(DOS_KB_BIOS_INTERRUPT_VECTOR, old_kb_int16_handler);
		pokeb(0, DOS_KB_BIOS_FLAGS_LINEAR_OFFSET,
			  peekb(0, DOS_KB_BIOS_FLAGS_LINEAR_OFFSET) & DOS_KB_BIOS_SHIFT_FLAGS_CLEAR_MASK);
	}

	outp(DOS_PIC_MASK_PORT, irqmask);
}

legacy_s16 kb_get_key_state(legacy_s16 key)
{
	return dos_kb_input[key];
}

static legacy_u32 dos_kb_bios_call(legacy_u8 function)
{
	/* Watcom's int86 REGS does not expose ZF. Preserve the actual BIOS flags
	 * before the compiler or interrupt wrapper can change them. Returning both
	 * words also avoids passing a near pointer into a possibly foreign stack. */
	legacy_u16 result;
	legacy_u16 result_flags;
	__asm {
		mov ah, function
		int DOS_KB_BIOS_INTERRUPT_VECTOR
		mov result, ax
		pushf
		pop ax
		mov result_flags, ax
	}
	return LEGACY_U32_FROM_WORDS(result, result_flags);
}

legacy_s16 dos_kb_get_char(void)
{
	legacy_u32 status = dos_kb_bios_call(DOS_KB_BIOS_STATUS_FUNCTION);
	legacy_u16 key = (legacy_u16)status;
	if (((legacy_u16)(status >> LEGACY_WORD_BITS) & DOS_KB_X86_ZERO_FLAG) != 0) {
		return 0;
	}

	/* A timer callback may ask while a foreign stack is active. The original
	 * reports the pending key but postpones consuming and dispatching it. */
	if (dos_data_stack_segments_match() == 0) {
		return key;
	}

	key = (legacy_u16)dos_kb_bios_call(DOS_KB_BIOS_READ_FUNCTION);
	return kb_parse_key(key);
}

void dos_kb_set_numlock(void)
{
	pokeb(DOS_KB_BIOS_DATA_SEGMENT, DOS_KB_BIOS_FLAGS_OFFSET,
		  peekb(DOS_KB_BIOS_DATA_SEGMENT, DOS_KB_BIOS_FLAGS_OFFSET) | DOS_KB_NUM_LOCK_FLAG);
	(void)kb_checking();
}

void dos_kb_clear_numlock(void)
{
	pokeb(DOS_KB_BIOS_DATA_SEGMENT, DOS_KB_BIOS_FLAGS_OFFSET,
		  peekb(DOS_KB_BIOS_DATA_SEGMENT, DOS_KB_BIOS_FLAGS_OFFSET) & DOS_KB_NUM_LOCK_CLEAR_MASK);
	(void)kb_checking();
}

legacy_s16 kb_call_readchar_callback(void)
{
	// the orginal code uses a (hard-coded, non-changing) callback for
	// reading chars.. we just call kb_read_char() directly:
	return kb_read_char();
}

legacy_s16 kb_read_char(void)
{
	// we could've called kb_int16_handler_c() directly
	union REGS inregs;

	inregs.h.ah = DOS_KB_BIOS_STATUS_FUNCTION;
	union REGS outregs;
	int86(DOS_KB_BIOS_INTERRUPT_VECTOR, &inregs, &outregs);
	if (!outregs.x.ax) {
		return 0;
	}

	inregs.h.ah = DOS_KB_BIOS_READ_FUNCTION;
	int86(DOS_KB_BIOS_INTERRUPT_VECTOR, &inregs, &outregs);
	// AL == 0 marks an extended key, whose scancode kb_int9_handler put in AH.
	// The original keeps the whole word in that case (or al,al / jz, skipping
	// the xor ah,ah) and returns AL alone otherwise.
	if (outregs.h.al == 0) {
		return outregs.x.ax;
	}
	return outregs.h.al;
}

legacy_s16 kb_checking(void)
{
	union REGS inregs;
	inregs.h.ah = DOS_KB_BIOS_STATUS_FUNCTION;
	union REGS outregs;
	int86(DOS_KB_BIOS_INTERRUPT_VECTOR, &inregs, &outregs);
	if (outregs.h.al == 0) {
		return outregs.x.ax;
	}
	return outregs.h.al;
}

void flush_stdin(void)
{
	legacy_s16 result;
	do {
		result = kb_call_readchar_callback();
	} while (result == 0);
}

legacy_s16 kb_check(void)
{
	union REGS inregs;
	union REGS outregs;
	while (1) {
		inregs.h.ah = DOS_KB_BIOS_STATUS_FUNCTION;
		int86(DOS_KB_BIOS_INTERRUPT_VECTOR, &inregs, &outregs);
		if (!outregs.x.ax) {
			return 0;
		}

		inregs.h.ah = DOS_KB_BIOS_READ_FUNCTION;
		int86(DOS_KB_BIOS_INTERRUPT_VECTOR, &inregs, &outregs);
	}
}
