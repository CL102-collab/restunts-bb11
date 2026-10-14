#include <stdarg.h>
#include "externs.h"
#include "fatal.h"
#include "platform.h"
#include "shape2d.h"
#include "keyboard.h"

#ifdef RESTUNTS_SDL3
extern legacy_s32 sdl3_batch_mode;
#endif

#define FATAL_OUTPUT_BUFFER_SIZE 96U
#define FATAL_NUMBER_SCRATCH_SIZE 12U

enum FATAL_FORMAT_RADIX { FATAL_FORMAT_DECIMAL_RADIX = 10, FATAL_FORMAT_HEXADECIMAL_RADIX = 16 };

struct FATAL_OUTPUT_STATE {
	legacy_s8 buffer[FATAL_OUTPUT_BUFFER_SIZE];
	legacy_u16 length;
};

void add_exit_handler(void(far *exit_handler)(void))
{
	for (legacy_s16 index = 0; index < EXIT_HANDLER_MAX_COUNT; index++) {
		if (exitlistfuncs[index] == exit_handler) {
			return;
		}
		if (exitlistfuncs[index] == 0) {
			exitlistfuncs[index] = exit_handler;
			exitlistfuncs[index + 1] = 0;
			return;
		}
	}
	fatal_error(exit_handler_overflow_message);
}

void call_exitlist(void)
{
	for (legacy_s16 index = EXIT_HANDLER_MAX_COUNT; index >= 0; index--) {
		if (exitlistfuncs[index] != 0) {
			exitlistfuncs[index]();
		}
	}
}

void call_exitlist2(void)
{
	call_exitlist();
	dos_process_exit(0);
}

static void fatal_flush_output(struct FATAL_OUTPUT_STATE *output)
{
	if (output->length != 0) {
		dos_write_stderr(output->buffer, output->length);
		output->length = 0;
	}
}

static void fatal_emit_character(struct FATAL_OUTPUT_STATE *output, legacy_s8 character)
{
	if (output->length == FATAL_OUTPUT_BUFFER_SIZE) {
		fatal_flush_output(output);
	}
	output->buffer[output->length++] = character;
}

static void fatal_emit_padding(struct FATAL_OUTPUT_STATE *output, legacy_s8 character,
							   legacy_s16 count)
{
	while (count-- > 0) {
		fatal_emit_character(output, character);
	}
}

static void fatal_emit_text(struct FATAL_OUTPUT_STATE *output, const legacy_s8 *text,
							legacy_s16 width, legacy_s16 precision, legacy_s16 left_aligned)
{
	if (text == 0) {
		text = "(null)";
	}
	legacy_s16 length;
	for (length = 0; text[length] != 0 && (precision < 0 || length < precision); length++) {
	}
	legacy_s16 emitted_length = length;
	if (!left_aligned) {
		fatal_emit_padding(output, ' ', (legacy_s16)(width - length));
	}
	while (length-- > 0) {
		fatal_emit_character(output, *text++);
	}
	if (left_aligned) {
		fatal_emit_padding(output, ' ', (legacy_s16)(width - emitted_length));
	}
}

static void fatal_emit_number(struct FATAL_OUTPUT_STATE *output, legacy_u32 value,
							  legacy_s16 negative, legacy_u16 radix, legacy_s16 uppercase,
							  legacy_s16 width, legacy_s16 precision, legacy_s16 left_aligned,
							  legacy_s16 zero_padded)
{
	const legacy_s8 *alphabet = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
	legacy_s8 digits[FATAL_NUMBER_SCRATCH_SIZE];
	legacy_s16 digit_count = 0;
	do {
		if (radix == 0U) {
			digits[digit_count++] = alphabet[0];
			value = 0UL;
		} else {
			digits[digit_count++] = alphabet[(legacy_u16)(value % radix)];
			value = LEGACY_U32_DIV_OR_ZERO(value, radix);
		}
	} while (value != 0);
	legacy_s16 zero_count = precision > digit_count ? precision - digit_count : 0;
	if (zero_padded && precision < 0 && !left_aligned) {
		zero_count = width - digit_count - (negative != 0);
		if (zero_count < 0) {
			zero_count = 0;
		}
	}
	legacy_s16 space_count = width - digit_count - zero_count - (negative != 0);
	if (!left_aligned) {
		fatal_emit_padding(output, ' ', space_count);
	}
	if (negative) {
		fatal_emit_character(output, '-');
	}
	fatal_emit_padding(output, '0', zero_count);
	for (legacy_s16 index = digit_count - 1; index >= 0; index--) {
		fatal_emit_character(output, digits[index]);
	}
	if (left_aligned) {
		fatal_emit_padding(output, ' ', space_count);
	}
}

struct FATAL_CONVERSION {
	legacy_s16 left_aligned, zero_padded, width, precision, long_value;
	legacy_s8 conversion;
};

static const legacy_s8 *fatal_parse_conversion(const legacy_s8 *format,
											   struct FATAL_CONVERSION *field)
{
	field->left_aligned = 0;
	field->zero_padded = 0;
	while (*format == '-' || *format == '0') {
		if (*format == '-') {
			field->left_aligned = 1;
		} else {
			field->zero_padded = 1;
		}
		format++;
	}
	field->width = 0;
	while (*format >= '0' && *format <= '9') {
		field->width = (legacy_s16)(field->width * FATAL_FORMAT_DECIMAL_RADIX + *format++ - '0');
	}
	field->precision = -1;
	if (*format == '.') {
		format++;
		field->precision = 0;
		while (*format >= '0' && *format <= '9') {
			field->precision =
				(legacy_s16)(field->precision * FATAL_FORMAT_DECIMAL_RADIX + *format++ - '0');
		}
	}
	field->long_value = 0;
	if (*format == 'l') {
		field->long_value = 1;
		format++;
	}
	field->conversion = *format;
	if (field->conversion != 0) {
		format++;
	}
	return format;
}

static void fatal_emit_field_character(struct FATAL_OUTPUT_STATE *output,
									   const struct FATAL_CONVERSION *field, legacy_s8 value)
{
	if (!field->left_aligned) {
		fatal_emit_padding(output, ' ', (legacy_s16)(field->width - 1));
	}
	fatal_emit_character(output, value);
	if (field->left_aligned) {
		fatal_emit_padding(output, ' ', (legacy_s16)(field->width - 1));
	}
}

static void fatal_emit_signed_field(struct FATAL_OUTPUT_STATE *output,
									const struct FATAL_CONVERSION *field, legacy_s32 value)
{
	legacy_u32 magnitude = value < 0 ? (legacy_u32)(0UL - (legacy_u32)value) : (legacy_u32)value;
	fatal_emit_number(output, magnitude, value < 0, FATAL_FORMAT_DECIMAL_RADIX, 0, field->width,
					  field->precision, field->left_aligned, field->zero_padded);
}

static void fatal_emit_unsigned_field(struct FATAL_OUTPUT_STATE *output,
									  const struct FATAL_CONVERSION *field, legacy_u32 value)
{
	fatal_emit_number(output, value, 0,
					  field->conversion == 'u' ? FATAL_FORMAT_DECIMAL_RADIX
											   : FATAL_FORMAT_HEXADECIMAL_RADIX,
					  field->conversion == 'X', field->width, field->precision, field->left_aligned,
					  field->zero_padded);
}

static void fatal_emit_unknown_field(struct FATAL_OUTPUT_STATE *output, legacy_s8 conversion)
{
	fatal_emit_character(output, '%');
	if (conversion != 0) {
		fatal_emit_character(output, conversion);
	}
}

/* Word arguments undergo the C default promotions at this variadic boundary. */
static void fatal_vprintf(const legacy_s8 *format, va_list arguments)
{
	struct FATAL_OUTPUT_STATE output;
	output.length = 0;
	legacy_u32 unsigned_value;
	struct FATAL_CONVERSION field;
	legacy_s16 value;
	legacy_s32 long_signed_value;
	while (*format != 0) {
		if (*format != '%') {
			fatal_emit_character(&output, *format++);
			continue;
		}
		format = fatal_parse_conversion(format + 1, &field);
		switch (field.conversion) {
			case '%':
				fatal_emit_character(&output, '%');
				break;
			case 'c':
				value = (legacy_s16)va_arg(arguments, legacy_int);
				fatal_emit_field_character(&output, &field, (legacy_s8)value);
				break;
			case 's':
				fatal_emit_text(&output, va_arg(arguments, legacy_s8 *), field.width,
								field.precision, field.left_aligned);
				break;
			case 'd':
			case 'i':
				long_signed_value = field.long_value ? va_arg(arguments, legacy_s32)
													 : (legacy_s16)va_arg(arguments, legacy_int);
				fatal_emit_signed_field(&output, &field, long_signed_value);
				break;
			case 'u':
			case 'x':
			case 'X':
				unsigned_value = field.long_value ? va_arg(arguments, legacy_u32)
												  : (legacy_u16)va_arg(arguments, legacy_uint);
				fatal_emit_unsigned_field(&output, &field, unsigned_value);
				break;
			default:
				fatal_emit_unknown_field(&output, field.conversion);
				break;
		}
	}
	fatal_flush_output(&output);
}

void fatal_error(const legacy_s8 *format, ...)
{
	sprite_select_screen();
	va_list arguments;
	va_start(arguments, format);
	fatal_vprintf(format, arguments);
	va_end(arguments);
#ifdef RESTUNTS_SDL3
	if (!sdl3_batch_mode) {
		flush_stdin();
	}
#elif !defined(RESTUNTS_AMIGA)
	flush_stdin();
#endif
	/* Native Amiga teardown restores the shell so fatal errors remain visible. */
	call_exitlist();
	va_start(arguments, format);
	fatal_vprintf(format, arguments);
	va_end(arguments);
	dos_process_exit(1);
}
