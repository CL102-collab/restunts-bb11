/* Build with make -C src/restunts/tests from the repository root. Run
 * build/watcom/release/DOSPLAT.EXE in a disposable DOSBox-X directory with
 * core=dynamic and cycles=max. The test creates and removes DOSABI.TMP. */
#include "../platform/dos/dos_interrupts.h"
#include "../platform/dos/dump_timer.h"
#include "../c/platform.h"
#include "../c/fileio.h"

static legacy_u16 car_base_initialized;

void file_car_set_base(const legacy_s8 *directory)
{
	const legacy_s8 *end = directory;
	while (*end != 0) {
		end++;
	}
	car_base_initialized =
		end != directory && (end[-1] == '\\' || end[-1] == '/' || end[-1] == ':');
}
#include "../c/resource.h"
#include "../c/fatal.h"

#define TEST_FILENAME "DOSABI.TMP"
#define TEST_BUFFER_SIZE 32U
#define TEST_DATA_SIZE 24U
#define TEST_REPLACEMENT_OFFSET 5U
#define TEST_REPLACEMENT_SIZE 3U
#define TEST_TRUNCATED_SIZE 8U
#define TEST_LONG_OFFSET 65540UL
#define TEST_INTERRUPT_VECTOR 96
#define TEST_INTERRUPT_RESULT 9320U
#define TEST_ZERO_FLAG 64U
#define TEST_RESOURCE_SIZE 71680UL
#define TEST_RESOURCE_PAYLOAD_OFFSET 65520UL
#define TEST_RESOURCE_PAYLOAD_SIZE 512U
#define TEST_PARAGRAPH_SHIFT 4U
#define TEST_PARAGRAPH_MASK 15U
#define TEST_PIC_MASK_PORT 33U
#define TEST_TIMER_IRQ_MASK 1U
#define TEST_OTHER_IRQ_MASK 164U
#define TEST_INTERRUPT_FLAG 512U

const legacy_s8 missing_shape_error_format[] = "missing test shape";
const legacy_s8 missing_sound_error_format[] = "missing test sound";

static void test_message(const legacy_s8 *message)
{
	legacy_u16 length = 0;
	while (message[length] != 0) {
		length++;
	}
	dos_write_stdout(message, length);
}

static legacy_s16 test_failure(const legacy_s8 *message)
{
	test_message("FAIL: ");
	test_message(message);
	test_message("\r\n");
	return 1;
}

void fatal_error(const legacy_s8 *format, ...)
{
	test_failure(format);
	dos_process_exit(1);
}

#define CHECK(condition, message)                                                                  \
	do {                                                                                           \
		if (!(condition)) {                                                                        \
			return test_failure(message);                                                          \
		}                                                                                          \
	} while (0)

static legacy_s16 test_dump_timer_irq(void)
{
	legacy_u16 original_flags;
	legacy_u16 disabled_flags;
	legacy_u16 repeated_flags;
	legacy_u16 enabled_flags;
	legacy_u8 original_mask;
	legacy_u8 disabled_mask;
	legacy_u8 repeated_mask;
	legacy_u8 enabled_mask;

	__asm {
		pushf
		pop ax
		mov original_flags, ax
		cli
	}
	original_mask = (legacy_u8)inp(TEST_PIC_MASK_PORT);
	/* Exercise mixed mask bits with IF clear, then the live DOS mask with IF set.
	 * Restore both before checking results so failures cannot leave IRQs masked. */
	outp(TEST_PIC_MASK_PORT, TEST_OTHER_IRQ_MASK);
	dump_disable_timer_irq();
	disabled_mask = (legacy_u8)inp(TEST_PIC_MASK_PORT);
	__asm {
		pushf
		pop ax
		mov disabled_flags, ax
	}
	dump_disable_timer_irq();
	repeated_mask = (legacy_u8)inp(TEST_PIC_MASK_PORT);
	__asm {
		pushf
		pop ax
		mov repeated_flags, ax
	}
	outp(TEST_PIC_MASK_PORT, original_mask & ~TEST_TIMER_IRQ_MASK);
	__asm {
		sti
	}
	dump_disable_timer_irq();
	enabled_mask = (legacy_u8)inp(TEST_PIC_MASK_PORT);
	__asm {
		pushf
		pop ax
		mov enabled_flags, ax
		cli
	}
	outp(TEST_PIC_MASK_PORT, original_mask);
	__asm {
		push original_flags
		popf
	}
	CHECK(disabled_mask == (TEST_OTHER_IRQ_MASK | TEST_TIMER_IRQ_MASK),
		  "offline capture masks only IRQ0");
	CHECK((disabled_flags & TEST_INTERRUPT_FLAG) == 0, "offline capture preserves cleared IF");
	CHECK(repeated_mask == disabled_mask && (repeated_flags & TEST_INTERRUPT_FLAG) == 0,
		  "offline capture timer mask is idempotent");
	CHECK(enabled_mask == (original_mask | TEST_TIMER_IRQ_MASK),
		  "offline capture preserves live PIC mask");
	CHECK((enabled_flags & TEST_INTERRUPT_FLAG) != 0, "offline capture preserves set IF");
	return 0;
}

static void interrupt test_interrupt_handler(union INTPACK registers)
{
	registers.w.ax = TEST_INTERRUPT_RESULT;
	registers.w.flags |= TEST_ZERO_FLAG;
}

static legacy_s16 test_psp_pointer(void)
{
	legacy_u16 psp_segment;
	legacy_u16 data_segment;
	void far *psp;

	__asm {
		mov ah, 62h
		int 21h
		mov psp_segment, bx
		mov data_segment, ds
	}
	psp = dos_memory_get_psp();
	/* This legacy API encodes the PSP segment in the offset of a DS pointer. */
	CHECK(dos_memory_pointer_offset(psp) == psp_segment, "PSP pointer offset");
	CHECK(dos_memory_pointer_segment(psp) == data_segment, "PSP pointer segment");
	return 0;
}

static legacy_s16 test_interrupt_frame(void)
{
	dos_interrupt_handler_type previous_handler = _dos_getvect(TEST_INTERRUPT_VECTOR);
	_dos_setvect(TEST_INTERRUPT_VECTOR, test_interrupt_handler);
	legacy_u16 flags;
	legacy_u16 result;
	__asm {
		mov ax, 1
		or ax, ax
		int TEST_INTERRUPT_VECTOR
		mov result, ax
		pushf
		pop ax
		mov flags, ax
	}
	_dos_setvect(TEST_INTERRUPT_VECTOR, previous_handler);
	CHECK(result == TEST_INTERRUPT_RESULT, "interrupt return AX");
	CHECK((flags & TEST_ZERO_FLAG) != 0, "interrupt return ZF");
	return 0;
}

static legacy_s16 test_resource_pointer(void)
{
	legacy_u16 memory_segment =
		dos_memory_allocate((legacy_u16)(TEST_RESOURCE_SIZE >> TEST_PARAGRAPH_SHIFT));
	legacy_u8 far *resource = dos_memory_make_pointer(memory_segment, 0);
	resource_file_set_size(resource, TEST_RESOURCE_SIZE);
	LEGACY_WRITE_U16_LE(resource + RESOURCE_FILE_COUNT_OFFSET, 1U);
	resource_file_set_offset(resource, 1U, 0U,
							 TEST_RESOURCE_PAYLOAD_OFFSET - resource_file_data_start(1U));

	/* Initialize by physical address so the test does not depend on a compiler's
	 * huge-pointer arithmetic to prepare its own expected bytes. */
	legacy_u8 far *byte;
	for (legacy_u16 index = 0; index < TEST_RESOURCE_PAYLOAD_SIZE; index++) {
		legacy_u32 position = TEST_RESOURCE_PAYLOAD_OFFSET + index;
		byte =
			dos_memory_make_pointer(memory_segment + (legacy_u16)(position >> TEST_PARAGRAPH_SHIFT),
									(legacy_u16)position & TEST_PARAGRAPH_MASK);
		*byte = (legacy_u8)(index ^ (index >> LEGACY_BYTE_BITS));
	}

	legacy_u8 far *payload = resource_file_data(resource, 0);
	legacy_u16 payload_segment = dos_memory_pointer_segment(payload);
	legacy_u16 payload_offset = dos_memory_pointer_offset(payload);
	CHECK(payload_offset <= TEST_PARAGRAPH_MASK, "resource payload pointer is normalized");
	CHECK(payload_segment ==
			  memory_segment + (legacy_u16)(TEST_RESOURCE_PAYLOAD_OFFSET >> TEST_PARAGRAPH_SHIFT),
		  "normalized resource segment");

	/* The DOS rasterizers advance a 16-bit offset within one fixed segment.
	 * A payload beginning near FFF0 would wrap into unrelated resource bytes. */
	for (legacy_u16 index = 0; index < TEST_RESOURCE_PAYLOAD_SIZE; index++) {
		byte = dos_memory_make_pointer(payload_segment, (legacy_u16)(payload_offset + index));
		CHECK(*byte == (legacy_u8)(index ^ (index >> LEGACY_BYTE_BITS)),
			  "resource payload crosses original 64 KiB boundary");
	}
	return 0;
}

static legacy_s16 test_file_io(legacy_u8 far *buffer)
{
	for (legacy_u16 index = 0; index < TEST_DATA_SIZE; index++) {
		buffer[index] = (legacy_u8)(index + 1U);
	}
	legacy_u16 handle = dos_file_open(TEST_FILENAME, DOS_FILE_CREATE);
	CHECK(handle != 0, "create");
	CHECK(dos_file_write(handle, buffer, TEST_DATA_SIZE) == TEST_DATA_SIZE, "far write");
	CHECK(dos_file_tell(handle) == TEST_DATA_SIZE, "tell after write");
	dos_file_seek(handle, TEST_REPLACEMENT_OFFSET, DOS_FILE_SEEK_BEGIN);
	CHECK(dos_file_error() == 0, "seek from beginning");
	CHECK(dos_file_write(handle, "XYZ", TEST_REPLACEMENT_SIZE) == TEST_REPLACEMENT_SIZE,
		  "near write");
	CHECK(dos_file_tell(handle) == TEST_TRUNCATED_SIZE, "tell after overwrite");
	CHECK(dos_file_write(handle, buffer, 0) == 0, "zero-length truncate");
	dos_file_seek(handle, 0, DOS_FILE_SEEK_END);
	CHECK(dos_file_tell(handle) == TEST_TRUNCATED_SIZE, "truncated file size");
	CHECK(dos_file_close(handle) == 0, "close after writing");

	handle = dos_file_open(TEST_FILENAME, DOS_FILE_OPEN_EXISTING);
	CHECK(handle != 0, "open existing");
	CHECK(dos_file_write(handle, buffer, 1) == 0, "read-only write result");
	CHECK(dos_file_error() != 0, "read-only write error");
	CHECK(dos_file_error() == 0, "error is cleared after reading");
	CHECK(dos_file_read(handle, buffer, TEST_BUFFER_SIZE) == TEST_TRUNCATED_SIZE,
		  "short read at end of file");
	for (legacy_u16 index = 0; index < TEST_TRUNCATED_SIZE; index++) {
		legacy_u8 expected = index < TEST_REPLACEMENT_OFFSET
								 ? (legacy_u8)(index + 1U)
								 : (legacy_u8)('X' + index - TEST_REPLACEMENT_OFFSET);
		CHECK(buffer[index] == expected, "read contents");
	}
	CHECK(dos_file_read(handle, buffer, 1) == 0, "end-of-file read");
	CHECK(dos_file_error() == 0, "end of file is not an error");
	CHECK(dos_file_close(handle) == 0, "close after reading");
	CHECK(dos_file_read(handle, buffer, 1) == 0, "closed-handle read result");
	CHECK(dos_file_error() != 0, "closed-handle read error");
	CHECK(dos_file_close(handle) == -1, "closed-handle close result");
	CHECK(dos_file_error() != 0, "closed-handle close error");

	handle = dos_file_open(TEST_FILENAME, DOS_FILE_CREATE);
	CHECK(handle != 0, "recreate");
	dos_file_seek(handle, TEST_LONG_OFFSET, DOS_FILE_SEEK_BEGIN);
	CHECK(dos_file_error() == 0, "seek beyond 64 KiB");
	CHECK(dos_file_tell(handle) == TEST_LONG_OFFSET, "32-bit file position");
	buffer[0] = 165U;
	CHECK(dos_file_write(handle, buffer, 1) == 1, "write beyond 64 KiB");
	dos_file_seek(handle, -1, DOS_FILE_SEEK_END);
	CHECK(dos_file_error() == 0, "negative seek from end");
	CHECK(dos_file_tell(handle) == TEST_LONG_OFFSET, "position after negative seek");
	buffer[0] = 0;
	CHECK(dos_file_read(handle, buffer, 1) == 1 && buffer[0] == 165U, "read beyond 64 KiB");
	CHECK(dos_file_close(handle) == 0, "close large file");
	CHECK(dos_file_remove(TEST_FILENAME) == 0, "remove");
	CHECK(dos_file_open(TEST_FILENAME, DOS_FILE_OPEN_EXISTING) == 0, "missing-file result");
	CHECK(dos_file_error() != 0, "missing-file error");
	return 0;
}

legacy_s16 stuntsmain(legacy_s16 argc, legacy_s8 *argv[])
{
	CHECK(car_base_initialized != 0, "executable resource base before startup");
	(void)argc;
	(void)argv;
	CHECK(dos_data_stack_segments_match() != 0, "startup DS equals SS");
	legacy_u16 memory_segment = dos_memory_allocate(TEST_BUFFER_SIZE / 16U);
	legacy_u8 far *buffer = dos_memory_make_pointer(memory_segment, 0);
	CHECK(dos_memory_pointer_segment(buffer) == memory_segment, "far pointer segment");
	CHECK(dos_memory_pointer_offset(buffer) == 0, "far pointer offset");
	CHECK(test_dump_timer_irq() == 0, "offline dump timer IRQ");
	CHECK(test_psp_pointer() == 0, "PSP pointer ABI");
	CHECK(test_interrupt_frame() == 0, "interrupt frame");
	CHECK(test_resource_pointer() == 0, "resource pointer ABI");
	CHECK(test_file_io(buffer) == 0, "DOS file I/O");
	test_message("DOS platform ABI checks passed\r\n");
	return 0;
}
