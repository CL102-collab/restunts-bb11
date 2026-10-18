#include "dos_interrupts.h"

#define DOS_RUNTIME_INTERRUPT 33
#define DOS_RUNTIME_GET_DRIVE_FUNCTION 25
#define DOS_RUNTIME_SELECT_DRIVE_FUNCTION 14
#define DOS_RUNTIME_GET_DIRECTORY_FUNCTION 71
#define DOS_RUNTIME_CHANGE_DIRECTORY_FUNCTION 59
#define DOS_RUNTIME_DIRECTORY_BUFFER_SIZE 66U

static legacy_u16 startup_original_drive;
static legacy_u16 startup_data_drive;
static legacy_s8 startup_original_directory[DOS_RUNTIME_DIRECTORY_BUFFER_SIZE];
static legacy_u8 startup_directory_selected;

static legacy_u16 dos_current_drive(void)
{
	legacy_u16 drive;
	__asm {
		mov ah, DOS_RUNTIME_GET_DRIVE_FUNCTION
		int DOS_RUNTIME_INTERRUPT
		xor ah, ah
		mov drive, ax
	}
	return drive;
}

static legacy_s16 dos_select_drive(legacy_u16 drive)
{
	__asm {
		mov dx, drive
		mov ah, DOS_RUNTIME_SELECT_DRIVE_FUNCTION
		int DOS_RUNTIME_INTERRUPT
	}
	return dos_current_drive() == drive;
}

static legacy_s16 dos_change_directory(const legacy_s8 *directory)
{
	legacy_u16 directory_offset = FP_OFF(directory);
	legacy_u16 failed = 0;
	__asm {
		mov dx, directory_offset
		mov ah, DOS_RUNTIME_CHANGE_DIRECTORY_FUNCTION
		int DOS_RUNTIME_INTERRUPT
		jnc short directory_changed
		mov failed, 1
	directory_changed:
	}
	return !failed;
}

void dos_startup_restore_data_directory(void)
{
	if (startup_directory_selected != 0) {
		if (dos_select_drive(startup_data_drive)) {
			(void)dos_change_directory(startup_original_directory);
		}
		(void)dos_select_drive(startup_original_drive);
		startup_directory_selected = 0;
	}
}

legacy_s16 dos_startup_select_data_directory(const legacy_s8 *directory)
{
	startup_original_drive = dos_current_drive();
	startup_data_drive = startup_original_drive;
	if (directory[0] != 0 && directory[1] == ':') {
		legacy_s8 letter = directory[0];
		if (letter >= 'a' && letter <= 'z') {
			letter -= 'a' - 'A';
		}
		if (letter < 'A' || letter > 'Z') {
			return 0;
		}
		startup_data_drive = letter - 'A';
	}
	/* DOS drive/CWD state belongs to the shell, not the child's PSP. Preserve
	 * the target drive's CWD as well as the caller's default drive, including
	 * when the game ends through its fatal-error path. AH=47h omits the slash. */
	startup_original_directory[0] = '\\';
	legacy_u16 directory_offset = FP_OFF(startup_original_directory + 1);
	legacy_u16 drive_number = startup_data_drive + 1U;
	legacy_u16 failed = 0;
	__asm {
		push si
		mov dx, drive_number
		mov si, directory_offset
		mov ah, DOS_RUNTIME_GET_DIRECTORY_FUNCTION
		int DOS_RUNTIME_INTERRUPT
		pop si
		jnc short original_directory_saved
		mov failed, 1
	original_directory_saved:
	}
	if (failed)
	{
		return 0;
	}
	startup_directory_selected = 1;
	if (!dos_select_drive(startup_data_drive) || !dos_change_directory(directory)) {
		dos_startup_restore_data_directory();
		return 0;
	}
	return 1;
}
