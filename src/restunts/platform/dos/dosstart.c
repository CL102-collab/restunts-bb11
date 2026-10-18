#include "legacy.h"
#include "platform.h"

#include "dos_interrupts.h"
#include "restunts.h"
#ifdef RESTUNTS_FULL
#include "command_line.h"
#endif

#define HEADLESS_COMMAND_LINE_SIZE 128
/* The shortest argument and its separator occupy two command-tail bytes. */
#define HEADLESS_MIN_ARGUMENT_SIZE 2U
#define HEADLESS_MAX_ARGS (HEADLESS_COMMAND_LINE_SIZE / HEADLESS_MIN_ARGUMENT_SIZE + 1U)
#define HEADLESS_STACK_PARAGRAPHS 512
#define HEADLESS_DOS_INTERRUPT 33
#define HEADLESS_DOS_RESIZE_MEMORY_FUNCTION 74
#define HEADLESS_PSP_COMMAND_LINE_OFFSET 128U

#if defined(RESTUNTS_FULL) || defined(RESTUNTS_PIXLDUMP)
extern void full_data_initialize(void);
#endif

#ifdef RESTUNTS_FULL

#define dos_program_main run_main_menu_loop
#else
extern legacy_s16 stuntsmain(legacy_s16 argc, legacy_s8 *argv[]);
#define dos_program_main stuntsmain
#endif
extern legacy_u8 headless_bss_start;
extern legacy_u8 headless_bss_end;
extern legacy_u8 headless_stack_top;

static legacy_u16 headless_psp_segment;
static legacy_u16 headless_program_paragraphs;
#ifdef RESTUNTS_PIXLDUMP
static legacy_s8 headless_program_name[] = "PIXLDUMP";
#else
static legacy_s8 headless_program_name[] = "REPLDUMP";
#endif
static legacy_s8 headless_command_line[HEADLESS_COMMAND_LINE_SIZE];
static legacy_s8 *headless_argv[HEADLESS_MAX_ARGS];

void headless_exit(legacy_s16 result)
{
	dos_process_exit(result);
}

static void headless_release_extra_memory(void)
{
	legacy_u16 psp_segment = headless_psp_segment;
	legacy_u16 program_paragraphs = headless_program_paragraphs;

	__asm {
		mov bx, program_paragraphs
		mov es, psp_segment
		mov ah, HEADLESS_DOS_RESIZE_MEMORY_FUNCTION
		int HEADLESS_DOS_INTERRUPT
		push ds
		pop es
	}
}

static legacy_s16 headless_parse_command_line(void)
{
	legacy_u8 far *source =
		(legacy_u8 far *)MK_FP(headless_psp_segment, HEADLESS_PSP_COMMAND_LINE_OFFSET);
	legacy_u16 source_length = source[0];
	headless_argv[0] = headless_program_name;

	legacy_u16 source_index = 1;
	legacy_u16 destination_index = 0;
	legacy_s16 argc = 1;
	while (source_index <= source_length && argc < HEADLESS_MAX_ARGS &&
		   destination_index + 1U < HEADLESS_COMMAND_LINE_SIZE) {
		while (source_index <= source_length &&
			   (source[source_index] == ' ' || source[source_index] == '\t')) {
			source_index++;
		}
		if (source_index > source_length) {
			break;
		}

		headless_argv[argc++] = &headless_command_line[destination_index];
		legacy_u8 quoted = 0;
		while (source_index <= source_length) {
			legacy_u8 character = source[source_index++];
			if (character == '"') {
				quoted ^= 1U;
				continue;
			}
			if (!quoted && (character == ' ' || character == '\t')) {
				break;
			}
			if (destination_index + 1U < HEADLESS_COMMAND_LINE_SIZE) {
				headless_command_line[destination_index++] = character;
			}
		}
		headless_command_line[destination_index++] = 0;
	}

	return argc;
}

#ifdef RESTUNTS_FULL
static legacy_s16 headless_is_data_directory_option(const legacy_s8 *argument)
{
	static const legacy_s8 option[] = "--data-dir";
	legacy_u16 index = 0;
	while (argument[index] != 0 && argument[index] == option[index]) {
		index++;
	}
	return argument[index] == 0 && option[index] == 0;
}

static legacy_s16 headless_select_data_directory(legacy_s16 *argc)
{
	if (*argc > 1 && headless_is_data_directory_option(headless_argv[1])) {
		if (*argc <= 2 || !dos_startup_select_data_directory(headless_argv[2])) {
			static const legacy_s8 message[] = "Cannot open --data-dir directory\r\n";
			dos_write_stdout(message, sizeof(message) - 1U);
			return 0;
		}
		*argc -= 2;
		for (legacy_s16 index = 1; index < *argc; index++) {
			headless_argv[index] = headless_argv[index + 2];
		}
	}
	return 1;
}
#endif

static void headless_run(void)
{
	headless_release_extra_memory();
	legacy_s16 argc = headless_parse_command_line();
#ifdef RESTUNTS_FULL
	legacy_s16 info_result = command_line_print_info(argc, headless_argv);
	if (info_result != COMMAND_LINE_CONTINUE) {
		headless_exit(info_result);
		return;
	}
	if (!headless_select_data_directory(&argc)) {
		headless_exit(1);
		return;
	}
#endif
#if defined(RESTUNTS_FULL) || defined(RESTUNTS_PIXLDUMP)
	full_data_initialize();
#endif
	legacy_s16 result = dos_program_main(argc, headless_argv);
	headless_exit(result);
}

void headless_start(void)
{
	/* DOS enters an EXE with ES pointing at its PSP. The medium memory model
	 * requires SS == DS whenever a pointer to an automatic object is passed as
	 * an ordinary near pointer, so normalize both registers to DGROUP before
	 * calling any C routine.  The linker-visible STACK remains last and gives
	 * us both the initial entry stack and the final near offset. */
	legacy_u16 bss_offset = FP_OFF(&headless_bss_start);
	legacy_u16 bss_length = (legacy_u16)(FP_OFF(&headless_bss_end) - bss_offset);
	legacy_u16 stack_pointer = FP_OFF(&headless_stack_top);
	__asm {
		cld
		mov bx, es
		mov cx, ss
		sub cx, bx
		add cx, HEADLESS_STACK_PARAGRAPHS
		mov dx, stack_pointer
		mov ax, seg headless_psp_segment
		mov ds, ax
		push es
		push ds
		pop es
		mov di, bss_offset
		push cx
		mov cx, bss_length
		xor ax, ax
		rep stosb
		pop cx
		pop es
		mov ax, seg headless_psp_segment
		mov headless_psp_segment, bx
		mov headless_program_paragraphs, cx
		cli
		mov ss, ax
		mov sp, dx
		sti
	}

	headless_run();
}
