#ifndef RESTUNTS_LEGACY_H
#define RESTUNTS_LEGACY_H

#include <float.h>
#include <limits.h>

/* Preserve segmented pointers for the Open Watcom 16-bit DOS build. */
#if defined(__WATCOMC__) && defined(__I86__)
#define RESTUNTS_DOS16 1
#endif

/* Segmented-memory qualifiers have no meaning on flat-memory hosts. */
#if !defined(RESTUNTS_DOS16) && !defined(far)
#define far
#endif
#if !defined(RESTUNTS_DOS16) && !defined(huge)
#define huge
#endif

/* Preserve exact C ABI types for strings, callbacks, entry points, and
 * default argument promotions. Game state and arithmetic use the checked
 * widths below, independent of the host data model. */
typedef char legacy_char;
typedef int legacy_int;
typedef unsigned int legacy_uint;

#if CHAR_BIT != 8 || SCHAR_MAX != 127 || SCHAR_MIN != (-127 - 1)
#error Restunts requires exact 8-bit bytes
#endif
typedef signed char legacy_s8;
typedef unsigned char legacy_u8;
#if SHRT_MAX == 32767 && USHRT_MAX == 65535U
typedef signed short legacy_s16;
typedef unsigned short legacy_u16;
#elif INT_MAX == 32767 && UINT_MAX == 65535U
typedef signed int legacy_s16;
typedef unsigned int legacy_u16;
#else
#error Restunts requires an exact 16-bit integer type
#endif

#define LEGACY_BYTE_BITS 8U
#define LEGACY_WORD_BITS 16U
#define LEGACY_DWORD_BITS 32U
#define LEGACY_THREE_BYTE_BITS 24U
#define LEGACY_WORD_BYTES 2U
#define LEGACY_DWORD_BYTES 4U
#define LEGACY_S8_MAX 127U
#define LEGACY_U8_SIGN_BIT 128U
#define LEGACY_U8_MAX 255U
#define LEGACY_S16_MAX 32767U
#define LEGACY_U16_SIGN_BIT 32768U
#define LEGACY_U16_HIGH_BYTE_MASK 65280U
#define LEGACY_U16_MAX 65535U
#define LEGACY_S32_MAX 2147483647UL
#define LEGACY_U32_SIGN_BIT 2147483648UL
#define LEGACY_U32_HIGH_WORD_MASK 4294901760UL
#define LEGACY_U32_MAX 4294967295UL
#define LEGACY_X86_ROTATE16_COUNT_MASK 15U
#define LEGACY_X86_SHIFT_COUNT_MASK 31U

/* printf fragments follow the selected types, including default promotions. */
#if INT_MAX == 2147483647L && UINT_MAX == 4294967295UL
typedef signed int legacy_s32;
typedef unsigned int legacy_u32;
#define LEGACY_PRId32 "d"
#define LEGACY_PRIu32 "u"
#define LEGACY_PRIx32 "x"
#define LEGACY_PRIX32 "X"
#elif LONG_MAX == 2147483647L && ULONG_MAX == 4294967295UL
typedef signed long legacy_s32;
typedef unsigned long legacy_u32;
#define LEGACY_PRId32 "ld"
#define LEGACY_PRIu32 "lu"
#define LEGACY_PRIx32 "lx"
#define LEGACY_PRIX32 "lX"
#elif SHRT_MAX == 2147483647L && USHRT_MAX == 4294967295UL
typedef signed short legacy_s32;
typedef unsigned short legacy_u32;
#define LEGACY_PRId32 "hd"
#define LEGACY_PRIu32 "hu"
#define LEGACY_PRIx32 "hx"
#define LEGACY_PRIX32 "hX"
#else
#error Restunts requires an exact 32-bit integer type
#endif

/* Wider intermediates are needed by road clipping and platform timers. */
#if LLONG_MAX == 9223372036854775807LL && ULLONG_MAX == 18446744073709551615ULL
typedef signed long long legacy_s64;
typedef unsigned long long legacy_u64;
#define LEGACY_PRId64 "lld"
#define LEGACY_PRIu64 "llu"
#define LEGACY_PRIx64 "llx"
#define LEGACY_PRIX64 "llX"
#elif LONG_MAX == 9223372036854775807LL && ULONG_MAX == 18446744073709551615ULL
typedef signed long legacy_s64;
typedef unsigned long legacy_u64;
#define LEGACY_PRId64 "ld"
#define LEGACY_PRIu64 "lu"
#define LEGACY_PRIx64 "lx"
#define LEGACY_PRIX64 "lX"
#else
#error Restunts requires an exact 64-bit integer type
#endif

/* The high-resolution renderer requires binary32 storage and binary64 math. */
#if FLT_RADIX != 2 || FLT_MANT_DIG != 24 || FLT_MIN_EXP != -125 || FLT_MAX_EXP != 128
#error Restunts requires a 32-bit binary floating-point type
#endif
#if DBL_MANT_DIG != 53 || DBL_MIN_EXP != -1021 || DBL_MAX_EXP != 1024
#error Restunts requires a 64-bit binary floating-point type
#endif
typedef float legacy_f32;
typedef double legacy_f64;

/* Preserve the host's extended precision for independent reference calculations. */
typedef long double legacy_fext;

typedef legacy_s8 legacy_byte_must_be_8_bits[(CHAR_BIT == 8) ? 1 : -1];
typedef legacy_s8 legacy_s16_must_be_2_bytes[(sizeof(legacy_s16) == 2) ? 1 : -1];
typedef legacy_s8 legacy_u16_must_be_2_bytes[(sizeof(legacy_u16) == 2) ? 1 : -1];
typedef legacy_s8 legacy_s32_must_be_4_bytes[(sizeof(legacy_s32) == 4) ? 1 : -1];
typedef legacy_s8 legacy_u32_must_be_4_bytes[(sizeof(legacy_u32) == 4) ? 1 : -1];
typedef legacy_s8 legacy_s64_must_be_8_bytes[(sizeof(legacy_s64) == 8) ? 1 : -1];
typedef legacy_s8 legacy_u64_must_be_8_bytes[(sizeof(legacy_u64) == 8) ? 1 : -1];
typedef legacy_s8 legacy_f32_must_be_4_bytes[(sizeof(legacy_f32) == 4) ? 1 : -1];
typedef legacy_s8 legacy_f64_must_be_8_bytes[(sizeof(legacy_f64) == 8) ? 1 : -1];

legacy_u16 legacy_u16_div_or_zero(legacy_u16 numerator, legacy_u16 denominator);
legacy_s16 legacy_s16_div_or_zero(legacy_s16 numerator, legacy_s16 denominator);
legacy_u32 legacy_u32_div_or_zero(legacy_u32 numerator, legacy_u32 denominator);
legacy_s32 legacy_s32_div_or_zero(legacy_s32 numerator, legacy_s32 denominator);

/* Pass side-effect-free values to these legacy word operations. */
#if defined(RESTUNTS_DOS16)
#define LEGACY_S8_FROM_BITS(value) ((legacy_s8)(legacy_u8)(value))
#define LEGACY_S16_FROM_BITS(value) ((legacy_s16)(legacy_u16)(value))
#else
/* Calls can carry stateful expressions such as get_kevinrandom(). Decode
 * each value once; repeated macro evaluation changes replay simulation on
 * flat-memory hosts even when the resulting integer is otherwise identical. */
static inline legacy_s8 legacy_s8_from_bits(legacy_u8 value)
{
	return value <= LEGACY_S8_MAX ? (legacy_s8)value
								  : (legacy_s8)(-1 - (legacy_s8)(LEGACY_U8_MAX - value));
}

static inline legacy_s16 legacy_s16_from_bits(legacy_u16 value)
{
	return value <= LEGACY_S16_MAX ? (legacy_s16)value
								   : (legacy_s16)(-1 - (legacy_s16)(LEGACY_U16_MAX - value));
}

#define LEGACY_S8_FROM_BITS(value) legacy_s8_from_bits((legacy_u8)(value))
#define LEGACY_S16_FROM_BITS(value) legacy_s16_from_bits((legacy_u16)(value))
#endif

#define LEGACY_U8_WRAP_ADD(left, right) ((legacy_u8)((legacy_u8)(left) + (legacy_u8)(right)))
#define LEGACY_U8_WRAP_SUB(left, right) ((legacy_u8)((legacy_u8)(left) - (legacy_u8)(right)))
#define LEGACY_U8_WRAP_MUL(left, right)                                                            \
	((legacy_u8)((legacy_u16)(legacy_u8)(left) * (legacy_u16)(legacy_u8)(right)))
#define LEGACY_S8_WRAP_ADD(left, right) LEGACY_S8_FROM_BITS(LEGACY_U8_WRAP_ADD(left, right))
#define LEGACY_S8_WRAP_SUB(left, right) LEGACY_S8_FROM_BITS(LEGACY_U8_WRAP_SUB(left, right))
#define LEGACY_S8_WRAP_NEGATE(value) LEGACY_S8_FROM_BITS((legacy_u8)(0U - (legacy_u8)(value)))
#define LEGACY_S8_WRAP_MUL(left, right) LEGACY_S8_FROM_BITS(LEGACY_U8_WRAP_MUL(left, right))

#define LEGACY_U16_WRAP_ADD(left, right) ((legacy_u16)((legacy_u16)(left) + (legacy_u16)(right)))
#define LEGACY_U16_WRAP_SUB(left, right) ((legacy_u16)((legacy_u16)(left) - (legacy_u16)(right)))
#define LEGACY_U16_WRAP_MUL(left, right)                                                           \
	((legacy_u16)((legacy_u32)(legacy_u16)(left) * (legacy_u32)(legacy_u16)(right)))
#define LEGACY_S16_WRAP_ADD(left, right) LEGACY_S16_FROM_BITS(LEGACY_U16_WRAP_ADD(left, right))
#define LEGACY_S16_WRAP_SUB(left, right) LEGACY_S16_FROM_BITS(LEGACY_U16_WRAP_SUB(left, right))
#define LEGACY_S16_WRAP_NEGATE(value) LEGACY_S16_FROM_BITS((legacy_u16)(0U - (legacy_u16)(value)))
#define LEGACY_S16_WRAP_MUL(left, right) LEGACY_S16_FROM_BITS(LEGACY_U16_WRAP_MUL(left, right))

#define LEGACY_U16_LOW_BYTE(value) ((legacy_u8)(legacy_u16)(value))
#define LEGACY_U16_REPLACE_LOW_BYTE(word, value)                                                   \
	((legacy_u16)(((legacy_u16)(word) & LEGACY_U16_HIGH_BYTE_MASK) |                               \
				  (legacy_u16)(legacy_u8)(value)))

/* x86 masks variable shift counts to five bits. */
#define LEGACY_SHIFT_COUNT(value) ((legacy_u16)(value) & LEGACY_X86_SHIFT_COUNT_MASK)
#define LEGACY_ROTATE16_COUNT(value) ((legacy_u16)(value) & LEGACY_X86_ROTATE16_COUNT_MASK)

#define LEGACY_U16_SAR(value, count)                                                               \
	((legacy_u16)(LEGACY_SHIFT_COUNT(count) == 0U                                                  \
					  ? (legacy_u16)(value)                                                        \
					  : (((legacy_u16)(value) & LEGACY_U16_SIGN_BIT) != 0U                         \
							 ? (LEGACY_SHIFT_COUNT(count) >= LEGACY_WORD_BITS                      \
									? (legacy_u32)LEGACY_U16_MAX                                   \
									: (legacy_u32)((legacy_u16)(value) >>                          \
												   LEGACY_SHIFT_COUNT(count)) |                    \
										  (legacy_u16)((legacy_u32)LEGACY_U16_MAX                  \
													   << (LEGACY_WORD_BITS -                      \
														   LEGACY_SHIFT_COUNT(count))))            \
							 : (LEGACY_SHIFT_COUNT(count) >= LEGACY_WORD_BITS                      \
									? 0UL                                                          \
									: (legacy_u32)((legacy_u16)(value) >>                          \
												   LEGACY_SHIFT_COUNT(count))))))
#define LEGACY_S16_SAR(value, count) LEGACY_S16_FROM_BITS(LEGACY_U16_SAR(value, count))
#define LEGACY_U16_SAR2(value) LEGACY_U16_SAR(value, 2U)
#define LEGACY_S16_SAR2(value) LEGACY_S16_FROM_BITS(LEGACY_U16_SAR2(value))

#define LEGACY_U16_SHL(value, count)                                                               \
	((legacy_u16)(LEGACY_SHIFT_COUNT(count) >= LEGACY_WORD_BITS                                    \
					  ? 0UL                                                                        \
					  : (legacy_u32)(legacy_u16)(value) << LEGACY_SHIFT_COUNT(count)))
#define LEGACY_S16_SHL(value, count) LEGACY_S16_FROM_BITS(LEGACY_U16_SHL(value, count))

#define LEGACY_U16_ROL(value, count)                                                               \
	((legacy_u16)(LEGACY_ROTATE16_COUNT(count) == 0U                                               \
					  ? (legacy_u16)(value)                                                        \
					  : ((legacy_u32)(legacy_u16)(value) << LEGACY_ROTATE16_COUNT(count)) |        \
							((legacy_u16)(value) >>                                                \
							 (LEGACY_WORD_BITS - LEGACY_ROTATE16_COUNT(count)))))
#define LEGACY_U16_ROR(value, count)                                                               \
	((legacy_u16)(LEGACY_ROTATE16_COUNT(count) == 0U                                               \
					  ? (legacy_u16)(value)                                                        \
					  : ((legacy_u16)(value) >> LEGACY_ROTATE16_COUNT(count)) |                    \
							((legacy_u32)(legacy_u16)(value)                                       \
							 << (LEGACY_WORD_BITS - LEGACY_ROTATE16_COUNT(count)))))

#define LEGACY_U16_MUL_HIGH(left, right)                                                           \
	((legacy_u16)(((legacy_u32)(legacy_u16)(left) * (legacy_u32)(legacy_u16)(right)) >>            \
				  LEGACY_WORD_BITS))
#define LEGACY_S16_MUL_HIGH(left, right)                                                           \
	LEGACY_S16_FROM_BITS(                                                                          \
		(legacy_u16)LEGACY_U32_SAR((legacy_u32)((legacy_s32)LEGACY_S16_FROM_BITS(left) *           \
												(legacy_s32)LEGACY_S16_FROM_BITS(right)),          \
								   LEGACY_WORD_BITS))

/* The original #DE recovery skips a faulting division with a zero result. */
#define LEGACY_U16_DIV_OR_ZERO(numerator, denominator)                                             \
	legacy_u16_div_or_zero((legacy_u16)(numerator), (legacy_u16)(denominator))
#define LEGACY_S16_DIV_OR_ZERO(numerator, denominator)                                             \
	legacy_s16_div_or_zero(LEGACY_S16_FROM_BITS(numerator), LEGACY_S16_FROM_BITS(denominator))

#define LEGACY_READ_U16_LE(bytes)                                                                  \
	((legacy_u16)((legacy_u16)(legacy_u8)(bytes)[0] |                                              \
				  ((legacy_u16)(legacy_u8)(bytes)[1] << LEGACY_BYTE_BITS)))

#define LEGACY_WRITE_U16_LE(bytes, value)                                                          \
	do {                                                                                           \
		legacy_u16 legacy_write_u16_value_ = (legacy_u16)(value);                                  \
		(bytes)[0] = (legacy_u8)legacy_write_u16_value_;                                           \
		(bytes)[1] = (legacy_u8)(legacy_write_u16_value_ >> LEGACY_BYTE_BITS);                     \
	} while (0)

#if defined(RESTUNTS_DOS16)
#define LEGACY_S32_FROM_BITS(value) ((legacy_s32)(legacy_u32)(value))
#else
static inline legacy_s32 legacy_s32_from_bits(legacy_u32 value)
{
	return value <= (legacy_u32)LEGACY_S32_MAX
			   ? (legacy_s32)value
			   : (legacy_s32)(-1 - (legacy_s32)((legacy_u32)LEGACY_U32_MAX - value));
}

#define LEGACY_S32_FROM_BITS(value) legacy_s32_from_bits((legacy_u32)(value))
#endif

#define LEGACY_U32_SIGN_EXTEND_S16(value)                                                          \
	((legacy_u32)((((legacy_u16)(value) & LEGACY_U16_SIGN_BIT) != 0                                \
					   ? (legacy_u32)LEGACY_U32_HIGH_WORD_MASK                                     \
					   : (legacy_u32)0) |                                                          \
				  (legacy_u16)(value)))
#define LEGACY_U32_FROM_WORDS(low_word, high_word)                                                 \
	((legacy_u32)((legacy_u16)(low_word) |                                                         \
				  ((legacy_u32)(legacy_u16)(high_word) << LEGACY_WORD_BITS)))
#define LEGACY_S32_WRAP_ADD(left, right)                                                           \
	LEGACY_S32_FROM_BITS((legacy_u32)(left) + (legacy_u32)(right))
#define LEGACY_S32_WRAP_ADD_S16(left, right)                                                       \
	LEGACY_S32_FROM_BITS((legacy_u32)(left) + LEGACY_U32_SIGN_EXTEND_S16(right))
#define LEGACY_S32_WRAP_SUB_S16(left, right)                                                       \
	LEGACY_S32_FROM_BITS((legacy_u32)(left) - LEGACY_U32_SIGN_EXTEND_S16(right))

#define LEGACY_U32_WRAP_ADD(left, right) ((legacy_u32)((legacy_u32)(left) + (legacy_u32)(right)))
#define LEGACY_U32_WRAP_SUB(left, right) ((legacy_u32)((legacy_u32)(left) - (legacy_u32)(right)))
#define LEGACY_U32_WRAP_MUL(left, right) ((legacy_u32)((legacy_u32)(left) * (legacy_u32)(right)))
#define LEGACY_S32_WRAP_SUB(left, right) LEGACY_S32_FROM_BITS(LEGACY_U32_WRAP_SUB(left, right))
#define LEGACY_S32_WRAP_NEGATE(value)                                                              \
	LEGACY_S32_FROM_BITS((legacy_u32)((legacy_u32)0 - (legacy_u32)(value)))
#define LEGACY_S32_WRAP_MUL(left, right) LEGACY_S32_FROM_BITS(LEGACY_U32_WRAP_MUL(left, right))

#define LEGACY_U32_SAR(value, count)                                                               \
	((legacy_u32)(LEGACY_SHIFT_COUNT(count) == 0U                                                  \
					  ? (legacy_u32)(value)                                                        \
					  : (((legacy_u32)(value) & (legacy_u32)LEGACY_U32_SIGN_BIT) != 0UL            \
							 ? ((legacy_u32)(value) >> LEGACY_SHIFT_COUNT(count)) |                \
								   ((legacy_u32)LEGACY_U32_MAX                                     \
									<< (LEGACY_DWORD_BITS - LEGACY_SHIFT_COUNT(count)))            \
							 : (legacy_u32)(value) >> LEGACY_SHIFT_COUNT(count))))
#define LEGACY_S32_SAR(value, count) LEGACY_S32_FROM_BITS(LEGACY_U32_SAR(value, count))

#define LEGACY_U32_SHL(value, count) ((legacy_u32)(value) << LEGACY_SHIFT_COUNT(count))
#define LEGACY_S32_SHL(value, count) LEGACY_S32_FROM_BITS(LEGACY_U32_SHL(value, count))

#define LEGACY_U32_ROL(value, count)                                                               \
	((legacy_u32)(LEGACY_SHIFT_COUNT(count) == 0U                                                  \
					  ? (legacy_u32)(value)                                                        \
					  : ((legacy_u32)(value) << LEGACY_SHIFT_COUNT(count)) |                       \
							((legacy_u32)(value) >>                                                \
							 (LEGACY_DWORD_BITS - LEGACY_SHIFT_COUNT(count)))))
#define LEGACY_U32_ROR(value, count)                                                               \
	((legacy_u32)(LEGACY_SHIFT_COUNT(count) == 0U                                                  \
					  ? (legacy_u32)(value)                                                        \
					  : ((legacy_u32)(value) >> LEGACY_SHIFT_COUNT(count)) |                       \
							((legacy_u32)(value)                                                   \
							 << (LEGACY_DWORD_BITS - LEGACY_SHIFT_COUNT(count)))))

#define LEGACY_U32_DIV_OR_ZERO(numerator, denominator)                                             \
	legacy_u32_div_or_zero((legacy_u32)(numerator), (legacy_u32)(denominator))
#define LEGACY_S32_DIV_OR_ZERO(numerator, denominator)                                             \
	legacy_s32_div_or_zero(LEGACY_S32_FROM_BITS(numerator), LEGACY_S32_FROM_BITS(denominator))

#define LEGACY_READ_U32_LE(bytes)                                                                  \
	((legacy_u32)((legacy_u32)(legacy_u8)(bytes)[0] |                                              \
				  ((legacy_u32)(legacy_u8)(bytes)[1] << LEGACY_BYTE_BITS) |                        \
				  ((legacy_u32)(legacy_u8)(bytes)[2] << LEGACY_WORD_BITS) |                        \
				  ((legacy_u32)(legacy_u8)(bytes)[3] << LEGACY_THREE_BYTE_BITS)))
#define LEGACY_READ_S16_LE(bytes) LEGACY_S16_FROM_BITS(LEGACY_READ_U16_LE(bytes))
#define LEGACY_READ_S32_LE(bytes) LEGACY_S32_FROM_BITS(LEGACY_READ_U32_LE(bytes))

#define LEGACY_WRITE_U32_LE(bytes, value)                                                          \
	do {                                                                                           \
		legacy_u32 legacy_write_u32_value_ = (legacy_u32)(value);                                  \
		(bytes)[0] = (legacy_u8)legacy_write_u32_value_;                                           \
		(bytes)[1] = (legacy_u8)(legacy_write_u32_value_ >> LEGACY_BYTE_BITS);                     \
		(bytes)[2] = (legacy_u8)(legacy_write_u32_value_ >> LEGACY_WORD_BITS);                     \
		(bytes)[3] = (legacy_u8)(legacy_write_u32_value_ >> LEGACY_THREE_BYTE_BITS);               \
	} while (0)

#endif
