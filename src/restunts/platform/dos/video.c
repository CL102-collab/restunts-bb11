#include "dos_interrupts.h"
#include "../../c/legacy.h"
#include "../../c/platform.h"
#include "../../c/video/video_pages.h"
#include "../../c/fatal.h"

#define DOS_VIDEO_BIOS_INTERRUPT 16
#define DOS_VIDEO_BIOS_SET_DAC_BLOCK_FUNCTION 4114
#define DOS_VIDEO_BIOS_DATA_SEGMENT 64U
#define DOS_VIDEO_BIOS_EQUIPMENT_OFFSET 16U
#define DOS_VIDEO_EQUIPMENT_DISPLAY_CLEAR_MASK 65487U
#define DOS_VIDEO_EQUIPMENT_COLOR_BITS 16U
#define DOS_VIDEO_EQUIPMENT_MODE4_BITS 32U
#define DOS_VIDEO_EQUIPMENT_MONOCHROME_BITS 48U
#define DOS_VIDEO_CRTC_REGISTER_COUNT 12U
#define DOS_VIDEO_CRTC_INDEX_PORT 948U
#define DOS_VIDEO_CRTC_DATA_PORT 949U
#define DOS_VIDEO_STATUS_PORT 986U
#define DOS_VIDEO_GRAPHICS_SEGMENT 40960U
#define DOS_VIDEO_GRAPHICS_CLEAR_WORDS 64000U
#define DOS_VIDEO_MONOCHROME_SEGMENT 47104U
#define DOS_VIDEO_MONOCHROME_CLEAR_WORDS 16384U
#define DOS_VIDEO_HERCULES_CONFIG_PORT 959U
#define DOS_VIDEO_HERCULES_CONTROL_PORT 952U
#define DOS_VIDEO_HERCULES_CONFIG_ENABLE 3U
#define DOS_VIDEO_HERCULES_MODE4_CONTROL 2U
#define DOS_VIDEO_HERCULES_MODE4_ACTIVE_CONTROL 138U
#define DOS_VIDEO_HERCULES_MODE7_CONTROL 32U
#define DOS_VIDEO_HERCULES_MODE7_ACTIVE_CONTROL 40U
enum DOS_VIDEO_BIOS_FUNCTION {
	DOS_VIDEO_BIOS_SET_MODE_FUNCTION = 0,
	DOS_VIDEO_BIOS_SET_BACKGROUND_FUNCTION = 11,
	DOS_VIDEO_BIOS_GET_MODE_FUNCTION = 15
};

enum DOS_VIDEO_BIOS_MODE {
	DOS_VIDEO_BIOS_MODE3 = 3,
	DOS_VIDEO_BIOS_MODE4 = 4,
	DOS_VIDEO_BIOS_MODE7 = 7,
	DOS_VIDEO_BIOS_MODE13 = 19
};

static legacy_u8 saved_video_mode;
static legacy_u8 saved_equipment_byte;
static legacy_u8 mode4_active;

static const legacy_u8 mode4_crtc_registers[DOS_VIDEO_CRTC_REGISTER_COUNT] = {
	53U, 40U, 44U, 7U, 121U, 2U, 100U, 110U, 2U, 2U, 0U, 0U};

static const legacy_u8 mode7_crtc_registers[DOS_VIDEO_CRTC_REGISTER_COUNT] = {
	97U, 80U, 82U, 15U, 25U, 6U, 25U, 25U, 2U, 13U, 11U, 12U};

static void dos_video_set_equipment_bits(legacy_u16 display_bits)
{
	legacy_u16 equipment =
		(legacy_u16)peek(DOS_VIDEO_BIOS_DATA_SEGMENT, DOS_VIDEO_BIOS_EQUIPMENT_OFFSET);
	equipment = (legacy_u16)((equipment & DOS_VIDEO_EQUIPMENT_DISPLAY_CLEAR_MASK) | display_bits);
	poke(DOS_VIDEO_BIOS_DATA_SEGMENT, DOS_VIDEO_BIOS_EQUIPMENT_OFFSET, equipment);
}

static void dos_video_set_bios_mode(legacy_u8 mode)
{
	union REGS registers;

	registers.h.ah = DOS_VIDEO_BIOS_SET_MODE_FUNCTION;
	registers.h.al = mode;
	int86(DOS_VIDEO_BIOS_INTERRUPT, &registers, &registers);
}

static void dos_video_reset_palette(void)
{
	union REGS registers;

	registers.h.ah = DOS_VIDEO_BIOS_SET_BACKGROUND_FUNCTION;
	registers.x.bx = 0;
	int86(DOS_VIDEO_BIOS_INTERRUPT, &registers, &registers);
}

static void dos_video_fill(legacy_u16 segment, legacy_u16 value, legacy_u16 word_count)
{
	legacy_u16 far *destination = (legacy_u16 far *)MK_FP(segment, 0);
	for (legacy_u16 index = 0; index < word_count; ++index) {
		destination[index] = value;
	}
}

static void dos_video_program_crtc(const legacy_u8 *values)
{
	for (legacy_u16 index = 0; index < DOS_VIDEO_CRTC_REGISTER_COUNT; ++index) {
		outp(DOS_VIDEO_CRTC_INDEX_PORT, index);
		outp(DOS_VIDEO_CRTC_DATA_PORT, values[index]);
	}
}

static void far dos_video_on_exit(void)
{
	pokeb(DOS_VIDEO_BIOS_DATA_SEGMENT, DOS_VIDEO_BIOS_EQUIPMENT_OFFSET, saved_equipment_byte);
	dos_video_set_bios_mode(saved_video_mode);
	pokeb(DOS_VIDEO_BIOS_DATA_SEGMENT, DOS_VIDEO_BIOS_EQUIPMENT_OFFSET, saved_equipment_byte);
	legacy_u8 equipment = saved_equipment_byte;
	if ((equipment & DOS_VIDEO_EQUIPMENT_MONOCHROME_BITS) == DOS_VIDEO_EQUIPMENT_MONOCHROME_BITS) {
		dos_video_fill(DOS_VIDEO_GRAPHICS_SEGMENT, 0, DOS_VIDEO_GRAPHICS_CLEAR_WORDS);
	}
	dos_video_reset_palette();
}

static void dos_video_add_exit_handler(void)
{
	if (saved_video_mode != 0) {
		return;
	}
	union REGS registers;
	registers.h.ah = DOS_VIDEO_BIOS_GET_MODE_FUNCTION;
	int86(DOS_VIDEO_BIOS_INTERRUPT, &registers, &registers);
	saved_video_mode = registers.h.al;
	saved_equipment_byte =
		(legacy_u8)peekb(DOS_VIDEO_BIOS_DATA_SEGMENT, DOS_VIDEO_BIOS_EQUIPMENT_OFFSET);
	add_exit_handler(dos_video_on_exit);
}

static void dos_video_set_mode3(void)
{
	dos_video_fill(DOS_VIDEO_GRAPHICS_SEGMENT, 0, DOS_VIDEO_GRAPHICS_CLEAR_WORDS);
	dos_video_set_equipment_bits(DOS_VIDEO_EQUIPMENT_COLOR_BITS);
	dos_video_set_bios_mode(DOS_VIDEO_BIOS_MODE3);
	dos_video_reset_palette();
}

legacy_s16 dos_video_get_status(void)
{
	return (legacy_s16)(inpw(DOS_VIDEO_STATUS_PORT) & DOS_VIDEO_RETRACE_STATUS_BIT);
}

void dos_video_set_palette(legacy_u16 start, legacy_u16 count, legacy_u8 *palette)
{
	/* The source palette is a near pointer in the game's data segment. */
	__asm {
		push    es
		mov     ax, ds
		mov     es, ax
		mov     bx, start
		mov     cx, count
		mov     dx, palette
		mov     ax, DOS_VIDEO_BIOS_SET_DAC_BLOCK_FUNCTION
		int     DOS_VIDEO_BIOS_INTERRUPT
		pop     es
	}
}

void dos_video_set_mode_13h(void)
{
	dos_video_add_exit_handler();
	dos_video_set_equipment_bits(DOS_VIDEO_EQUIPMENT_COLOR_BITS);
	dos_video_reset_palette();
	dos_video_set_bios_mode(DOS_VIDEO_BIOS_MODE13);
	dos_video_fill(DOS_VIDEO_GRAPHICS_SEGMENT, 0, DOS_VIDEO_GRAPHICS_CLEAR_WORDS);
}

#define DOS_VIDEO_VGA_CRTC_PORT 0x3D4U
#define DOS_VIDEO_SEQUENCER_PORT 0x3C4U
#define DOS_VIDEO_GRAPHICS_PORT 0x3CEU
#define DOS_VIDEO_ALL_PLANES ((1U << VGA_PLANE_COUNT) - 1U)
#define DOS_VIDEO_PROBE_REGISTER_COUNT 11U
#define DOS_VIDEO_PACKED_SCREEN_WORDS 32000U
#define DOS_VIDEO_PLANE_WORDS 32768U
#define DOS_VIDEO_UNSET_PLANE LEGACY_U8_MAX
#define DOS_VIDEO_PROBE_FIRST_BASE 17U
#define DOS_VIDEO_PROBE_SECOND_BASE 33U
#define DOS_VIDEO_BIOS_DISPLAY_COMBINATION_FUNCTION 0x1AU
#define DOS_VIDEO_BIOS_VGA_MONOCHROME 7U
#define DOS_VIDEO_BIOS_VGA_COLOR 8U
#define DOS_VIDEO_UNCHAINED_MEMORY_MODE 0x06U
#define DOS_VIDEO_PLANAR_GRAPHICS_MODE 0x40U
#define DOS_VIDEO_GRAPHICS_MAP 0x05U
#define DOS_VIDEO_BYTE_ADDRESSING_MODE 0xE3U
#define DOS_VIDEO_SCANLINE_OFFSET 0x28U

enum DOS_VIDEO_SEQUENCER_REGISTER {
	DOS_VIDEO_MAP_MASK_REGISTER = 2,
	DOS_VIDEO_MEMORY_MODE_REGISTER = 4
};

enum DOS_VIDEO_GRAPHICS_REGISTER {
	DOS_VIDEO_ENABLE_SET_RESET_REGISTER = 1,
	DOS_VIDEO_DATA_ROTATE_REGISTER = 3,
	DOS_VIDEO_READ_MAP_REGISTER = 4,
	DOS_VIDEO_GRAPHICS_MODE_REGISTER = 5,
	DOS_VIDEO_GRAPHICS_MISC_REGISTER = 6,
	DOS_VIDEO_BIT_MASK_REGISTER = 8
};

enum DOS_VIDEO_CRTC_REGISTER {
	DOS_VIDEO_START_ADDRESS_HIGH_REGISTER = 0x0c,
	DOS_VIDEO_START_ADDRESS_LOW_REGISTER = 0x0d,
	DOS_VIDEO_OFFSET_REGISTER = 0x13,
	DOS_VIDEO_UNDERLINE_REGISTER = 0x14,
	DOS_VIDEO_MODE_CONTROL_REGISTER = 0x17
};

#define DOS_VIDEO_REGISTER_VALUE(index, value) (((legacy_u16)(value) << LEGACY_BYTE_BITS) | (index))

struct DOS_VIDEO_PROBE_REGISTER {
	legacy_u16 port;
	legacy_u8 index;
};

static const struct DOS_VIDEO_PROBE_REGISTER probe_registers[DOS_VIDEO_PROBE_REGISTER_COUNT] = {
	{DOS_VIDEO_SEQUENCER_PORT, DOS_VIDEO_MAP_MASK_REGISTER},
	{DOS_VIDEO_SEQUENCER_PORT, DOS_VIDEO_MEMORY_MODE_REGISTER},
	{DOS_VIDEO_GRAPHICS_PORT, DOS_VIDEO_ENABLE_SET_RESET_REGISTER},
	{DOS_VIDEO_GRAPHICS_PORT, DOS_VIDEO_DATA_ROTATE_REGISTER},
	{DOS_VIDEO_GRAPHICS_PORT, DOS_VIDEO_READ_MAP_REGISTER},
	{DOS_VIDEO_GRAPHICS_PORT, DOS_VIDEO_GRAPHICS_MODE_REGISTER},
	{DOS_VIDEO_GRAPHICS_PORT, DOS_VIDEO_GRAPHICS_MISC_REGISTER},
	{DOS_VIDEO_GRAPHICS_PORT, DOS_VIDEO_BIT_MASK_REGISTER},
	{DOS_VIDEO_VGA_CRTC_PORT, DOS_VIDEO_OFFSET_REGISTER},
	{DOS_VIDEO_VGA_CRTC_PORT, DOS_VIDEO_UNDERLINE_REGISTER},
	{DOS_VIDEO_VGA_CRTC_PORT, DOS_VIDEO_MODE_CONTROL_REGISTER}};

static legacy_u8 video_write_mask = DOS_VIDEO_UNSET_PLANE;
static legacy_u8 video_read_plane = DOS_VIDEO_UNSET_PLANE;

void dos_video_set_write_planes(legacy_u8 mask)
{
	if (video_write_mask != mask) {
		outpw(DOS_VIDEO_SEQUENCER_PORT,
			  DOS_VIDEO_REGISTER_VALUE(DOS_VIDEO_MAP_MASK_REGISTER, mask));
		video_write_mask = mask;
	}
}

void dos_video_set_read_plane(legacy_u8 plane)
{
	if (video_read_plane != plane) {
		outpw(DOS_VIDEO_GRAPHICS_PORT,
			  DOS_VIDEO_REGISTER_VALUE(DOS_VIDEO_READ_MAP_REGISTER, plane));
		video_read_plane = plane;
	}
}

legacy_u8 dos_video_enable_planar_pages(void)
{
	union REGS registers;
	registers.x.ax = DOS_VIDEO_BIOS_DISPLAY_COMBINATION_FUNCTION << LEGACY_BYTE_BITS;
	registers.x.bx = 0;
	int86(DOS_VIDEO_BIOS_INTERRUPT, &registers, &registers);
	/* MCGA supports mode 13h but not the VGA plane/address registers. */
	if (registers.h.al != DOS_VIDEO_BIOS_DISPLAY_COMBINATION_FUNCTION ||
		(registers.h.bl != DOS_VIDEO_BIOS_VGA_MONOCHROME &&
		 registers.h.bl != DOS_VIDEO_BIOS_VGA_COLOR)) {
		return 0;
	}

	legacy_u8 saved_registers[DOS_VIDEO_PROBE_REGISTER_COUNT];
	for (legacy_u16 index = 0; index < DOS_VIDEO_PROBE_REGISTER_COUNT; index++) {
		outp(probe_registers[index].port, probe_registers[index].index);
		saved_registers[index] = (legacy_u8)inp(probe_registers[index].port + 1U);
	}

	/* Keep the BIOS 320x200 timing and palette. Unchain CPU accesses and use
	 * byte-addressed CRTC scanout: 80 bytes per row in each of four planes.
	 * Register definitions: https://www.scs.stanford.edu/10wi-cs140/pintos/
	 * specs/freevga/vga/{seqreg,graphreg,crtcreg}.htm */
	outpw(DOS_VIDEO_SEQUENCER_PORT, DOS_VIDEO_REGISTER_VALUE(DOS_VIDEO_MEMORY_MODE_REGISTER,
															 DOS_VIDEO_UNCHAINED_MEMORY_MODE));
	outpw(DOS_VIDEO_GRAPHICS_PORT,
		  DOS_VIDEO_REGISTER_VALUE(DOS_VIDEO_ENABLE_SET_RESET_REGISTER, 0));
	outpw(DOS_VIDEO_GRAPHICS_PORT, DOS_VIDEO_REGISTER_VALUE(DOS_VIDEO_DATA_ROTATE_REGISTER, 0));
	outpw(DOS_VIDEO_GRAPHICS_PORT, DOS_VIDEO_REGISTER_VALUE(DOS_VIDEO_GRAPHICS_MODE_REGISTER,
															DOS_VIDEO_PLANAR_GRAPHICS_MODE));
	outpw(DOS_VIDEO_GRAPHICS_PORT,
		  DOS_VIDEO_REGISTER_VALUE(DOS_VIDEO_GRAPHICS_MISC_REGISTER, DOS_VIDEO_GRAPHICS_MAP));
	outpw(DOS_VIDEO_GRAPHICS_PORT,
		  DOS_VIDEO_REGISTER_VALUE(DOS_VIDEO_BIT_MASK_REGISTER, LEGACY_U8_MAX));
	outpw(DOS_VIDEO_VGA_CRTC_PORT, DOS_VIDEO_REGISTER_VALUE(DOS_VIDEO_UNDERLINE_REGISTER, 0));
	outpw(DOS_VIDEO_VGA_CRTC_PORT, DOS_VIDEO_REGISTER_VALUE(DOS_VIDEO_MODE_CONTROL_REGISTER,
															DOS_VIDEO_BYTE_ADDRESSING_MODE));
	outpw(DOS_VIDEO_VGA_CRTC_PORT,
		  DOS_VIDEO_REGISTER_VALUE(DOS_VIDEO_OFFSET_REGISTER, DOS_VIDEO_SCANLINE_OFFSET));
	video_write_mask = DOS_VIDEO_UNSET_PLANE;
	video_read_plane = DOS_VIDEO_UNSET_PLANE;

	volatile legacy_u8 far *memory = (volatile legacy_u8 far *)MK_FP(DOS_VIDEO_GRAPHICS_SEGMENT, 0);
	for (legacy_u8 plane = 0; plane < VGA_PLANE_COUNT; plane++) {
		dos_video_set_write_planes((legacy_u8)(1U << plane));
		memory[0] = (legacy_u8)(DOS_VIDEO_PROBE_FIRST_BASE + plane);
		memory[VGA_PAGE_PLANE_BYTES] = (legacy_u8)(DOS_VIDEO_PROBE_SECOND_BASE + plane);
	}
	legacy_u8 supported = 1;
	for (legacy_u8 plane = 0; plane < VGA_PLANE_COUNT; plane++) {
		dos_video_set_read_plane(plane);
		if (memory[0] != (legacy_u8)(DOS_VIDEO_PROBE_FIRST_BASE + plane) ||
			memory[VGA_PAGE_PLANE_BYTES] != (legacy_u8)(DOS_VIDEO_PROBE_SECOND_BASE + plane)) {
			supported = 0;
		}
	}
	if (supported == 0) {
		/* A BIOS mode reset would replace the game palette loaded at startup.
		 * Restore only the registers touched by the probe, then erase its pixels. */
		for (legacy_u16 index = 0; index < DOS_VIDEO_PROBE_REGISTER_COUNT; index++) {
			outpw(probe_registers[index].port,
				  DOS_VIDEO_REGISTER_VALUE(probe_registers[index].index, saved_registers[index]));
		}
		dos_video_fill(DOS_VIDEO_GRAPHICS_SEGMENT, 0, DOS_VIDEO_PACKED_SCREEN_WORDS);
		video_write_mask = DOS_VIDEO_UNSET_PLANE;
		video_read_plane = DOS_VIDEO_UNSET_PLANE;
		return 0;
	}
	dos_video_set_write_planes(DOS_VIDEO_ALL_PLANES);
	dos_video_fill(DOS_VIDEO_GRAPHICS_SEGMENT, 0, DOS_VIDEO_PLANE_WORDS);
	return 1;
}

void dos_video_show_page(legacy_u16 address)
{
	/* Start-address writes are latched at retrace. Wait for a fresh blanking
	 * interval so the old visible page is safe to reuse when this returns. */
	while ((inp(DOS_VIDEO_STATUS_PORT) & DOS_VIDEO_RETRACE_STATUS_BIT) != 0) {
	}
	outpw(DOS_VIDEO_VGA_CRTC_PORT,
		  (address & LEGACY_U16_HIGH_BYTE_MASK) | DOS_VIDEO_START_ADDRESS_HIGH_REGISTER);
	outpw(DOS_VIDEO_VGA_CRTC_PORT,
		  DOS_VIDEO_REGISTER_VALUE(DOS_VIDEO_START_ADDRESS_LOW_REGISTER, address));
	while ((inp(DOS_VIDEO_STATUS_PORT) & DOS_VIDEO_RETRACE_STATUS_BIT) == 0) {
	}
}

void dos_video_set_mode4(void)
{
	mode4_active = 1U;
	dos_video_set_equipment_bits(DOS_VIDEO_EQUIPMENT_MODE4_BITS);
	dos_video_set_bios_mode(DOS_VIDEO_BIOS_MODE4);
	outp(DOS_VIDEO_HERCULES_CONFIG_PORT, DOS_VIDEO_HERCULES_CONFIG_ENABLE);
	outp(DOS_VIDEO_HERCULES_CONTROL_PORT, DOS_VIDEO_HERCULES_MODE4_CONTROL);
	dos_video_program_crtc(mode4_crtc_registers);
	dos_video_fill(DOS_VIDEO_MONOCHROME_SEGMENT, 0, DOS_VIDEO_MONOCHROME_CLEAR_WORDS);
	outp(DOS_VIDEO_HERCULES_CONTROL_PORT, DOS_VIDEO_HERCULES_MODE4_ACTIVE_CONTROL);
}

void dos_video_set_mode7(void)
{
	if (mode4_active == 0) {
		dos_video_set_mode3();
		return;
	}

	dos_video_set_equipment_bits(DOS_VIDEO_EQUIPMENT_MONOCHROME_BITS);
	outp(DOS_VIDEO_HERCULES_CONFIG_PORT, DOS_VIDEO_HERCULES_CONFIG_ENABLE);
	outp(DOS_VIDEO_HERCULES_CONTROL_PORT, DOS_VIDEO_HERCULES_MODE7_CONTROL);
	dos_video_program_crtc(mode7_crtc_registers);
	dos_video_fill(DOS_VIDEO_MONOCHROME_SEGMENT, 0, DOS_VIDEO_MONOCHROME_CLEAR_WORDS);
	outp(DOS_VIDEO_HERCULES_CONTROL_PORT, DOS_VIDEO_HERCULES_MODE7_ACTIVE_CONTROL);
	dos_video_set_bios_mode(DOS_VIDEO_BIOS_MODE7);
}
