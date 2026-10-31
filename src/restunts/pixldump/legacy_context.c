#ifdef RESTUNTS_SDL3
#include <stdlib.h>
#endif

#include "legacy_context.h"
#include "../c/platform.h"
#include "../c/externs.h"
#include "../c/fileio.h"
#include "../c/memmgr.h"
#include "../c/shape2d/shape2d.h"
#include "../c/shape2d/shape2d_internal.h"

#define PIXLDUMP_LEGACY_INITIAL_STACK_POINTER 52318U
#define PIXLDUMP_PSP_ENVIRONMENT_OFFSET 44U
#define PIXLDUMP_PSP_INTERRUPT_OPCODE 205U
#define PIXLDUMP_PSP_EXIT_INTERRUPT 32U
#define PIXLDUMP_ARGUMENT_STORAGE_OVERHEAD 3U
#define PIXLDUMP_DOS_PSP_PARAGRAPHS 16U
#define PIXLDUMP_LEGACY_HASH_POLYGON_FRAME_DEPTH 558U
#define PIXLDUMP_LEGACY_BMP_POLYGON_FRAME_DEPTH 556U
#define PIXLDUMP_LEGACY_POLYGON_CODE_PARAGRAPH 5334U
#define PIXLDUMP_LEGACY_IMAGE_PARAGRAPHS 14822U
#define PIXLDUMP_DOS_MCB_PARAGRAPHS 1U
#define PIXLDUMP_ENVIRONMENT_STRING_COUNT_OFFSET 2U
#define PIXLDUMP_ENVIRONMENT_PATH_OFFSET                                                           \
	(PIXLDUMP_ENVIRONMENT_STRING_COUNT_OFFSET + LEGACY_WORD_BYTES)

#ifdef RESTUNTS_SDL3
/* The archived oracle runs at the root of the mounted DOS drive. Native
 * addresses cannot reproduce its stack and resource-segment residue. Keep
 * that logical DOS context independent of host ASLR, argv[0], and bitness.
 * The overrides permit comparison with an oracle launched under another
 * DOS environment without changing the renderer or replay. */
#define PIXLDUMP_NATIVE_ORACLE_PSP_SEGMENT 654U
#define PIXLDUMP_NATIVE_ORACLE_PROGRAM_PATH "C:\\PIXLDUMP.EXE"
#endif

static legacy_u16 pixldump_dos_psp_segment(void)
{
#ifdef RESTUNTS_SDL3
	const legacy_char *setting = getenv("RESTUNTS_ORACLE_PSP_SEGMENT");
	if (setting != 0 && setting[0] != 0) {
		legacy_char *end;
		/* Validate the complete host result before narrowing to a DOS segment. */
		legacy_u64 value = strtoul(setting, &end, 0);
		if (*end == 0 && value > 0 && value <= LEGACY_U16_MAX) {
			return (legacy_u16)value;
		}
	}
	return PIXLDUMP_NATIVE_ORACLE_PSP_SEGMENT;
#else
	/* This project's DOS adapter returns DS:PSP-segment, following the original
	 * mmgr interface. The offset word, rather than the pointer, identifies PSP. */
	void far *psp_reference = dos_memory_get_psp();
	return psp_reference == 0 ? 0 : dos_memory_pointer_offset(psp_reference);
#endif
}

legacy_u16 pixldump_legacy_load_segment(void)
{
	legacy_u16 psp_segment = pixldump_dos_psp_segment();
	return psp_segment == 0 ? 0 : LEGACY_U16_WRAP_ADD(psp_segment, PIXLDUMP_DOS_PSP_PARAGRAPHS);
}

legacy_u16 pixldump_legacy_polygon_frame_pointer(legacy_s16 argv_si, legacy_s16 bmp_mode)
{
	/* The BMP loop has two argument words but does not save SI/DI, making
	 * its render call two bytes shallower than the hash loop. */
	return LEGACY_U16_WRAP_SUB((legacy_u16)argv_si, bmp_mode
														? PIXLDUMP_LEGACY_BMP_POLYGON_FRAME_DEPTH
														: PIXLDUMP_LEGACY_HASH_POLYGON_FRAME_DEPTH);
}

legacy_u16 pixldump_legacy_polygon_code_segment(void)
{
	legacy_u16 load_segment = pixldump_legacy_load_segment();
	return load_segment == 0
			   ? 0
			   : LEGACY_U16_WRAP_ADD(load_segment, PIXLDUMP_LEGACY_POLYGON_CODE_PARAGRAPH);
}

legacy_u16 pixldump_legacy_polyinfo_segment(void)
{
	legacy_u16 segment = pixldump_legacy_load_segment();
	if (segment == 0) {
		return 0;
	}
	/* The incremental Borland oracle CRT retains 39E6 paragraphs after its load
	 * segment. DOS places an MCB between that block and the memory manager's arena.
	 * test-pixldump-legacy-layout.py checks this against the linked oracle. */
	segment = LEGACY_U16_WRAP_ADD(segment, PIXLDUMP_LEGACY_IMAGE_PARAGRAPHS);
	segment = LEGACY_U16_WRAP_ADD(segment, PIXLDUMP_DOS_MCB_PARAGRAPHS);
	/* Rebuild the original live allocation order from shared resource sizes.
	 * PC15 is the archived wrapper's driver; the port's extra audio record
	 * ring and its configured driver do not belong to this logical arena. */
	segment = LEGACY_U16_WRAP_ADD(segment, file_paras_fatal((const legacy_s8 *)"pc15.drv"));
	segment = LEGACY_U16_WRAP_ADD(
		segment, mmgr_get_chunk_size((legacy_s8 far *)mouse_small_sprite->sprite_bitmapptr));
	segment = LEGACY_U16_WRAP_ADD(
		segment, mmgr_get_chunk_size((legacy_s8 far *)mouse_medium_sprite->sprite_bitmapptr));
	segment = LEGACY_U16_WRAP_ADD(
		segment, mmgr_get_chunk_size((legacy_s8 far *)mouse_background_sprite->sprite_bitmapptr));
	segment = LEGACY_U16_WRAP_ADD(segment, mmgr_get_chunk_size((legacy_s8 far *)mainresptr));
	segment = LEGACY_U16_WRAP_ADD(segment, mmgr_get_chunk_size((legacy_s8 far *)fontdefptr));
	return LEGACY_U16_WRAP_ADD(segment, mmgr_get_chunk_size((legacy_s8 far *)fontnptr));
}

static legacy_u16 pixldump_argument_length(const legacy_s8 *argument)
{
	legacy_u16 length = 0;
	while (length < LEGACY_U16_MAX && argument[length] != 0) {
		length++;
	}
	return length;
}

static legacy_u16 pixldump_dos_program_path_length(void)
{
#ifdef RESTUNTS_SDL3
	const legacy_char *setting = getenv("RESTUNTS_ORACLE_PROGRAM_PATH");
	if (setting == 0 || setting[0] == 0) {
		setting = PIXLDUMP_NATIVE_ORACLE_PROGRAM_PATH;
	}
	return pixldump_argument_length((const legacy_s8 *)setting);
#else
	legacy_u16 psp_segment = pixldump_dos_psp_segment();
	if (psp_segment == 0) {
		return 0;
	}
	const legacy_u8 far *psp = (const legacy_u8 far *)dos_memory_make_pointer(psp_segment, 0);
	if (psp == 0 || psp[0] != PIXLDUMP_PSP_INTERRUPT_OPCODE ||
		psp[1] != PIXLDUMP_PSP_EXIT_INTERRUPT) {
		return 0;
	}
	legacy_u16 environment_segment =
		(legacy_u16)(psp[PIXLDUMP_PSP_ENVIRONMENT_OFFSET] |
					 ((legacy_u16)psp[PIXLDUMP_PSP_ENVIRONMENT_OFFSET + 1U] << LEGACY_BYTE_BITS));
	if (environment_segment == 0) {
		return 0;
	}
	const legacy_u8 far *environment =
		(const legacy_u8 far *)dos_memory_make_pointer(environment_segment, 0);
	if (environment == 0) {
		return 0;
	}

	/* DOS 3+ follows the double NUL with a word count and the executable path.
	 * Keep every read inside this segment, including malformed environments. */
	legacy_u32 offset;
	for (offset = 0; offset < LEGACY_U16_MAX; offset++) {
		if (environment[(legacy_u16)offset] == 0 && environment[(legacy_u16)(offset + 1U)] == 0) {
			break;
		}
	}
	if (offset > LEGACY_U16_MAX - PIXLDUMP_ENVIRONMENT_PATH_OFFSET) {
		return 0;
	}
	legacy_u16 string_count =
		(legacy_u16)(environment[(legacy_u16)(offset + PIXLDUMP_ENVIRONMENT_STRING_COUNT_OFFSET)] |
					 ((legacy_u16)environment[(
						  legacy_u16)(offset + PIXLDUMP_ENVIRONMENT_STRING_COUNT_OFFSET + 1U)]
					  << LEGACY_BYTE_BITS));
	if (string_count == 0) {
		return 0;
	}
	legacy_u32 start = offset + PIXLDUMP_ENVIRONMENT_PATH_OFFSET;
	for (offset = start; offset <= LEGACY_U16_MAX; offset++) {
		if (environment[(legacy_u16)offset] == 0) {
			return (legacy_u16)(offset - start);
		}
	}
	return 0;
#endif
}

legacy_s16 pixldump_legacy_argv_si(legacy_s16 argc, legacy_s8 *argv[])
{
	legacy_u16 program_path_length = pixldump_dos_program_path_length();
	/* Without a valid DOS path, use the supplied program name. This remains
	 * deterministic without inventing a drive or directory for the fallback. */
	if (program_path_length == 0) {
		program_path_length = pixldump_argument_length(argv[0]);
	}
	legacy_u16 allocation = program_path_length;
	for (legacy_s16 index = 1; index < argc; index++) {
		allocation = LEGACY_U16_WRAP_ADD(allocation, pixldump_argument_length(argv[index]));
	}
	/* The original CRT starts SP at CC5E. It reserves each argument's NUL,
	 * two-byte argv entries, their terminator, and rounds storage up to words. */
	allocation = LEGACY_U16_WRAP_ADD(
		allocation, LEGACY_U16_WRAP_MUL((legacy_u16)argc, PIXLDUMP_ARGUMENT_STORAGE_OVERHEAD));
	allocation = LEGACY_U16_WRAP_ADD(allocation, PIXLDUMP_ARGUMENT_STORAGE_OVERHEAD);
	allocation &= (legacy_u16)(LEGACY_U16_MAX - 1U);
	return LEGACY_S16_FROM_BITS(
		LEGACY_U16_WRAP_SUB(PIXLDUMP_LEGACY_INITIAL_STACK_POINTER, allocation));
}
