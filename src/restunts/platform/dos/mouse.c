#include "dos_interrupts.h"
#include "../../c/platform.h"

#define DOS_MOUSE_INTERRUPT 51
#define DOS_MOUSE_BIOS_INTERRUPT 21
#define DOS_MOUSE_BIOS_DISABLE_POINTING_DEVICE 49665U
#define DOS_MOUSE_DOUBLE_WIDTH 320
#define DOS_MOUSE_DOUBLE_WIDTH_SCALE 1U
#define DOS_MOUSE_DEFAULT_PIXEL_RATIO 16U

enum DOS_MOUSE_FUNCTION {
	DOS_MOUSE_RESET_FUNCTION = 0,
	DOS_MOUSE_GET_STATE_FUNCTION = 3,
	DOS_MOUSE_SET_POSITION_FUNCTION = 4,
	DOS_MOUSE_SET_HORIZONTAL_RANGE_FUNCTION = 7,
	DOS_MOUSE_SET_VERTICAL_RANGE_FUNCTION = 8,
	DOS_MOUSE_SET_PIXEL_RATIO_FUNCTION = 15
};

static legacy_u16 dos_mouse_button_count;

static legacy_u8 dos_mouse_horizontal_scale;

static void dos_mouse_set_pixel_ratio(legacy_u16 horizontal, legacy_u16 vertical)
{
	union REGS registers;

	registers.x.ax = DOS_MOUSE_SET_PIXEL_RATIO_FUNCTION;
	registers.x.cx = horizontal;
	registers.x.dx = vertical;
	int86(DOS_MOUSE_INTERRUPT, &registers, &registers);
}

void dos_mouse_set_minmax(legacy_s16 minimum_x, legacy_s16 minimum_y, legacy_s16 maximum_x,
						  legacy_s16 maximum_y)
{
	legacy_u16 scale = dos_mouse_horizontal_scale;
	union REGS registers;
	registers.x.ax = DOS_MOUSE_SET_HORIZONTAL_RANGE_FUNCTION;
	registers.x.cx = (legacy_u16)minimum_x << scale;
	registers.x.dx = (legacy_u16)maximum_x << scale;
	int86(DOS_MOUSE_INTERRUPT, &registers, &registers);

	registers.x.ax = DOS_MOUSE_SET_VERTICAL_RANGE_FUNCTION;
	registers.x.cx = (legacy_u16)minimum_y;
	registers.x.dx = (legacy_u16)maximum_y;
	int86(DOS_MOUSE_INTERRUPT, &registers, &registers);
}

legacy_s16 dos_mouse_init(legacy_s16 width, legacy_s16 height)
{
	union REGS registers;
	registers.x.ax = 0;
	registers.x.bx = 0;
	registers.x.cx = 0;
	registers.x.dx = 0;
	registers.x.si = 0;
	registers.x.di = 0;
	registers.x.cflag = 0;
	registers.x.ax = DOS_MOUSE_BIOS_DISABLE_POINTING_DEVICE;
	int86(DOS_MOUSE_BIOS_INTERRUPT, &registers, &registers);

	registers.x.ax = DOS_MOUSE_RESET_FUNCTION;
	int86(DOS_MOUSE_INTERRUPT, &registers, &registers);
	legacy_s16 installed = (legacy_s16)registers.x.ax;
	dos_mouse_button_count = registers.x.bx;
	if (installed != 0) {
		dos_mouse_horizontal_scale =
			width == DOS_MOUSE_DOUBLE_WIDTH ? DOS_MOUSE_DOUBLE_WIDTH_SCALE : 0U;
		dos_mouse_set_minmax(0, 0, LEGACY_S16_WRAP_SUB(width, 1), LEGACY_S16_WRAP_SUB(height, 1));
		dos_mouse_set_pixel_ratio(DOS_MOUSE_DEFAULT_PIXEL_RATIO, DOS_MOUSE_DEFAULT_PIXEL_RATIO);
	}
	return installed;
}

void dos_mouse_set_position(legacy_s16 x, legacy_s16 y)
{
	union REGS registers;

	registers.x.ax = DOS_MOUSE_SET_POSITION_FUNCTION;
	registers.x.cx = (legacy_u16)x << dos_mouse_horizontal_scale;
	registers.x.dx = (legacy_u16)y;
	int86(DOS_MOUSE_INTERRUPT, &registers, &registers);
}

void dos_mouse_get_state(legacy_s16 *buttons, legacy_s16 *x, legacy_s16 *y)
{
	union REGS registers;

	registers.x.ax = DOS_MOUSE_GET_STATE_FUNCTION;
	int86(DOS_MOUSE_INTERRUPT, &registers, &registers);
	*buttons = (legacy_s16)registers.x.bx;
	*x = (legacy_s16)(registers.x.cx >> dos_mouse_horizontal_scale);
	*y = (legacy_s16)registers.x.dx;
}

legacy_u16 dos_mouse_get_button_count(void)
{
	return dos_mouse_button_count;
}
