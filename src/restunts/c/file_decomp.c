#include "extended/extended_fileio.h"
#include <stddef.h>
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA_TRACE)
#include <stdio.h>
#endif
#include "externs.h"
#include "fileio.h"
#include "memmgr.h"
#include "platform.h"
#include "shape2d/shape2d.h"
#include "fatal.h"
#include "resource.h"
#include "ui/ui_dialog.h"
#include "audio/audio_control.h"
#ifdef RESTUNTS_SDL3
#include "driving_rules.h"
#ifndef RESTUNTS_HEADLESS
#endif
#endif

#include "fileio_internal.h"

// Sequential byte runs pass of run-length encoding.
//
// The original opens with a bail-out this has no equivalent for. It is not
// reproduced. With a single escape code the caller's
// subhdr->esc[RS_RLE_ESCSEQ_POS] would be reading past the table anyway, and
// the original's own bail-out returns with ax:dx undefined, which the caller
// then feeds into copy_paras_reverse - so neither side is coherent there. No
// Stunts resource carries esclen == 1.
legacy_u32 file_decomp_rle_seq(legacy_u8 huge *src, legacy_u8 huge *dst, legacy_u32 srclen,
							   legacy_u8 esc)
{
	legacy_u8 huge *srcend = src + srclen;
	legacy_u8 huge *dststart = dst;

	while (src < srcend) {
		legacy_u8 cur = *src++;

		// Byte sequence start.
		if (cur == esc) {
			legacy_u8 huge *seqstart = src;

			// Copy sequence.
			while ((cur = *src++) != esc) {
				*dst++ = cur;
			}

			// Number of repetitions, already written once.
			legacy_u8 rep = (*src++) - 1;
			legacy_u8 huge *seqend = src;

			// Copy remaining repetitions. The original is a do-while
			// (`dec dl / jnz short loc_30D5A`), so a stored count of 1
			// makes dl wrap and it copies 256 times where this copies none.
			// Counts of 2 and up agree exactly, and a count of 1 would mean
			// "repeat this run once", which the encoder has no reason to
			// emit.
			while (rep--) {
				src = seqstart;
				while (src < seqend - RLE_SEQUENCE_TRAILER_SIZE) {
					*dst++ = *src++;
				}
			}

			src = seqend;
		}
		// No sequence.
		else {
			*dst++ = cur;
		}
	}

	return dst - dststart;
}

// Single byte runs pass of run-length encoding.
legacy_u32 file_decomp_rle_single(legacy_u8 huge *src, legacy_u8 huge *dst, legacy_u32 len,
								  legacy_u8 *esclookup)
{
	legacy_u8 huge *dststart = dst;
	legacy_u8 huge *dstend = dst + len;

	legacy_u16 repw;
	legacy_u8 rep;
	while (dst < dstend) {
		legacy_u8 cur = *src++;

		if (esclookup[cur]) {
			switch (esclookup[cur]) {
				case RLE_BYTE_COUNT_CODE:
					rep = *src++;
					cur = *src++;

					while (rep--) {
						*dst++ = cur;
					}
					break;

				case RLE_WORD_COUNT_CODE:
					repw = *src++;
					repw |= *src++ << BYTE_SHIFT;
					cur = *src++;

					while (repw--) {
						*dst++ = cur;
					}
					break;

				default:
					rep = esclookup[cur] - 1;
					cur = *src++;

					while (rep--) {
						*dst++ = cur;
					}
					break;
			}
		} else {
			*dst++ = cur;
		}
	}

	return dst - dststart;
}

static void far *file_relocate_decomp_output(void far *destination, legacy_u16 decompparas,
											 legacy_u32 length)
{
	legacy_u16 paras = file_bytes_to_paras((legacy_s32)length);
	void far *source =
		dos_memory_make_pointer(decompparas - paras + dos_memory_pointer_segment(destination),
								dos_memory_pointer_offset(destination));
	copy_paras_reverse(dos_memory_pointer_segment(destination), dos_memory_pointer_segment(source),
					   paras);
	return source;
}

// Decompress run-length encoded sub-file.
legacy_u32 file_decomp_rle(legacy_u8 huge *src, legacy_u8 huge *dst, legacy_u16 decompparas)
{
	(void)decompparas;

	// Get decompressed size from header.
	legacy_u32 len = LEGACY_READ_U16_LE(src + COMPR_SIZE_LOW_OFFSET) |
					 ((legacy_u32)src[COMPR_SIZE_HIGH_OFFSET] << LEGACY_WORD_BITS);
	legacy_u8 huge *origsrc = src += COMPR_HEADER_SIZE;

	// Get source size and escape codes.
	legacy_u32 srclen =
		LEGACY_READ_U16_LE(src) | ((legacy_u32)src[COMPR_RLE_SIZE_HIGH_OFFSET] << LEGACY_WORD_BITS);
	legacy_u8 esclen = src[COMPR_RLE_ESCLEN_OFFSET];
	legacy_u8 huge *escapes = src + COMPR_RLE_HEADER_SIZE;

	// MSB denotes skipping the initial pass for byte sequence runs. Match the
	// original's strict compare against 128: exactly 128 still runs the pass,
	// using the byte in the sequence-escape slot even though the declared
	// escape-code count is zero.
	legacy_u16 skipseq = esclen > BYTE_HIGH_BIT;
	esclen &= (legacy_u8)~BYTE_HIGH_BIT;

	// Set pos to after escape codes.
	src = origsrc + COMPR_RLE_HEADER_SIZE + esclen;

	// Escape code lookup.
	legacy_u8 esclookup[RS_RLE_ESCLOOKUP_LEN];
	for (legacy_u16 i = 0; i < RS_RLE_ESCLOOKUP_LEN; ++i) {
		esclookup[i] = 0;
	}

	/* Keep this induction variable full width. GCC 6 for the 68000 can
	 * lower the 16-bit form to an address-register countdown whose upper
	 * bits retain part of escapes, preventing the loop from terminating. */
	for (legacy_u32 i = 0; i < esclen; ++i) {
		esclookup[escapes[i]] = i + 1;
	}

#ifdef RESTUNTS_AMIGA_TRACE
	fprintf(stdout,
			"Amiga decompress: RLE length=%" LEGACY_PRIu32 " source=%" LEGACY_PRIu32
			" escapes=%u skip=%u\n",
			len, srclen, (legacy_uint)esclen, (legacy_uint)skipseq);
	fflush(stdout);
#endif
	if (!skipseq) {
		AMIGA_DECOMP_TRACE("RLE sequence begin");
		legacy_u32 passlen = file_decomp_rle_seq(src, dst, srclen, escapes[RS_RLE_ESCSEQ_POS]);

		AMIGA_DECOMP_TRACE("RLE sequence complete");
		// In main decomp func:
		src = file_relocate_decomp_output(dst, decompparas, passlen);
	}

	// The original discards the single pass's count and returns the size out
	// of the compression header instead. The two only differ when the last
	// run overshoots - file_decomp_rle_single
	// stops on `dst < dstend` and a run that straddles the end writes past it,
	// so its count can exceed len. The declared length must still be returned:
	// file_decomp uses it to position the source for a following pass.
	AMIGA_DECOMP_TRACE("RLE single begin");
	file_decomp_rle_single(src, dst, len, esclookup);
	AMIGA_DECOMP_TRACE("RLE single complete");
	return len;
}

struct VLE_DECODER {
	legacy_u16 escape_alphabet_offsets[RS_VLE_ESC_LEN];
	legacy_u16 escape_code_limits[RS_VLE_ESC_LEN];
	legacy_u8 alphabet[RS_VLE_ALPH_LEN];
	legacy_u8 symbols[RS_VLE_ALPH_LEN];
	legacy_u8 widths[RS_VLE_ALPH_LEN];
	legacy_u8 huge *source;
	legacy_u16 additive;
	legacy_u16 current_word;
	legacy_u8 current_width;
	legacy_u8 current_symbol;
};

static legacy_u8 huge *file_vle_read_alphabet(struct VLE_DECODER *decoder, legacy_u8 huge *source,
											  legacy_u8 escape_length)
{
	// Generate the canonical code limits and alphabet offsets for each width.
	legacy_u16 index;
	legacy_u16 alphabet_length;
	legacy_u16 code_limit;
	for (index = 0, code_limit = 0, alphabet_length = 0; index < escape_length;
		 ++index, code_limit *= VLE_ALPHABET_BRANCH_FACTOR) {
		decoder->escape_alphabet_offsets[index] = alphabet_length - code_limit;
		legacy_u8 width_symbol_count = *source++;
		code_limit += width_symbol_count;
		alphabet_length += width_symbol_count;
		decoder->escape_code_limits[index] = code_limit;
	}
	for (index = 0; index < alphabet_length; ++index) {
		decoder->alphabet[index] = *source++;
	}
	return source;
}

static void file_vle_prepare_lookup(struct VLE_DECODER *decoder, legacy_u8 huge *source,
									legacy_u8 escape_length)
{
	// Expand short codes into byte lookup tables; pad longer prefixes as escapes.
	legacy_u16 width = 1;
	legacy_u16 width_distribution =
		(escape_length >= BYTE_BIT_COUNT ? BYTE_BIT_COUNT : escape_length);
	legacy_u8 symbol_count = RS_VLE_NUM_SYMB;
	legacy_u16 alphabet_index;
	legacy_u16 index;
	for (index = 0, alphabet_index = 0; width <= width_distribution; ++width, symbol_count >>= 1) {
		for (legacy_u8 symbol_width = *source++; symbol_width > 0;
			 --symbol_width, ++alphabet_index) {
			for (legacy_u8 symbols_left = symbol_count; symbols_left; --symbols_left, ++index) {
				decoder->symbols[index] = decoder->alphabet[alphabet_index];
				decoder->widths[index] = width;
			}
		}
	}
	for (; index < RS_VLE_ALPH_LEN; ++index) {
		decoder->widths[index] = RS_VLE_ESC_WIDTH;
	}
}

static void file_vle_write_symbol(struct VLE_DECODER *decoder, legacy_u8 huge *destination,
								  legacy_u8 symbol)
{
	if (decoder->additive) {
		decoder->current_symbol += symbol;
	} else {
		decoder->current_symbol = symbol;
	}
	*destination = decoder->current_symbol;
}

static legacy_u8 file_vle_read_byte(struct VLE_DECODER *decoder)
{
	return *decoder->source++;
}

static legacy_u8 file_vle_expand_escape(struct VLE_DECODER *decoder, legacy_u8 huge *destination)
{
	legacy_u8 code = decoder->current_word;
	decoder->current_word >>= BYTE_SHIFT;
	legacy_u16 index = VLE_EXTENDED_CODE_START_WIDTH;
	while (1) {
		if (!decoder->current_width) {
			code = file_vle_read_byte(decoder);
			decoder->current_width = BYTE_BIT_COUNT;
		}
		decoder->current_word =
			(decoder->current_word << 1) + ((code & BYTE_HIGH_BIT) == BYTE_HIGH_BIT);
		code <<= 1;
		--decoder->current_width;
		++index;
		if (decoder->current_word < decoder->escape_code_limits[index]) {
			decoder->current_word += decoder->escape_alphabet_offsets[index];
			file_vle_write_symbol(decoder, destination, decoder->alphabet[decoder->current_word]);
			break;
		}
	}
	decoder->current_word = (code << decoder->current_width) | file_vle_read_byte(decoder);
	legacy_u8 next_width = BYTE_BIT_COUNT - decoder->current_width;
	decoder->current_width = BYTE_BIT_COUNT;
	return next_width;
}

static legacy_u8 file_vle_decode_direct(struct VLE_DECODER *decoder, legacy_u8 huge *destination,
										legacy_u8 code, legacy_u8 next_width)
{
	file_vle_write_symbol(decoder, destination, decoder->symbols[code]);
	if (decoder->current_width < next_width) {
		decoder->current_word <<= decoder->current_width;
		next_width -= decoder->current_width;
		decoder->current_width = BYTE_BIT_COUNT;
		decoder->current_word |= file_vle_read_byte(decoder);
	}
	return next_width;
}

// Decompress variable-length encoded sub-file.
legacy_u32 file_decomp_vle(legacy_u8 huge *src, legacy_u8 huge *dst, legacy_u16 decompparas)
{
	(void)decompparas;
	legacy_u32 lenleft;
	legacy_u32 len = lenleft = LEGACY_READ_U16_LE(src + COMPR_SIZE_LOW_OFFSET) |
							   ((legacy_u32)src[COMPR_SIZE_HIGH_OFFSET] << LEGACY_WORD_BITS);
	src += COMPR_HEADER_SIZE;
	legacy_u8 escape_length = *src++;
	struct VLE_DECODER decoder;
	decoder.additive = (escape_length & BYTE_HIGH_BIT) == BYTE_HIGH_BIT;
	escape_length &= (legacy_u8)~BYTE_HIGH_BIT;
	// Save the code stream while revisiting the width data for direct lookups.
	decoder.source = file_vle_read_alphabet(&decoder, src, escape_length);
	file_vle_prepare_lookup(&decoder, src, escape_length);
	decoder.current_word =
		*decoder.source << BYTE_SHIFT | *(decoder.source + VLE_CODE_WORD_SECOND_BYTE_OFFSET);
	decoder.source += LEGACY_WORD_BYTES;
	decoder.current_width = BYTE_BIT_COUNT;
	decoder.current_symbol = 0;
	// The original writes len + 1 bytes but returns the declared header length.
	++lenleft;
	while (lenleft) {
		legacy_u8 code = decoder.current_word >> BYTE_SHIFT;
		legacy_u8 next_width = decoder.widths[code];
		if (next_width > BYTE_BIT_COUNT) {
			next_width = file_vle_expand_escape(&decoder, dst);
		} else {
			next_width = file_vle_decode_direct(&decoder, dst, code, next_width);
		}
		dst++;
		--lenleft;
		decoder.current_word <<= next_width;
		decoder.current_width -= next_width;
	}
	return len;
}

static legacy_s16 file_decomp_pass(legacy_u8 far *source, legacy_u8 far *destination,
								   legacy_u16 paragraphs, legacy_u32 *length)
{
	switch (*source) {
		case COMPRESSION_RLE_TYPE:
			*length = file_decomp_rle(source, destination, paragraphs);
			return 1;
		case COMPRESSION_VLE_TYPE:
			*length = file_decomp_vle(source, destination, paragraphs);
			return 1;
	}
	return 0;
}

/* Tail snapshots own their storage. Initialize even the workspace before placing
 * compressed input at its historical offset, so unwritten bytes are stable. */
void far *file_alloc_tail_pages(const legacy_s8 *filename, legacy_u16 paragraphs,
								legacy_u16 tail_bytes)
{
	legacy_u8 huge *result = mmgr_alloc_pages(filename, paragraphs);
	if (tail_bytes) {
		legacy_u32 length = (legacy_u32)paragraphs << DOS_PARAGRAPH_SHIFT;
		for (legacy_u32 index = 0; index < length; index++) {
			result[index] = 0;
		}
	}
	return result;
}

// Decompress file. Returns pointer to result, NULL or raises fatal error.
void far *file_decomp_internal(const legacy_s8 *filename, legacy_s16 fatal, legacy_u16 tail_bytes)
{
	// Ordinary cached resources have already discarded the decompression tail.
	legacy_u8 far *dst = tail_bytes ? 0 : mmgr_get_chunk_by_name(filename);
	if (dst) {
		return dst;
	}

	AMIGA_DECOMP_TRACE("header begin");
	legacy_u16 decompparas = file_decomp_paras(filename, fatal);
	AMIGA_DECOMP_TRACE("header complete");

	legacy_s16 err = 0;
	legacy_u32 passlen;
	if (decompparas) {
		// Allocate extra paragraphs for alphabet and escape tables
		// overhead used during decompression.
		legacy_u16 retained_paragraphs = decompparas + file_bytes_to_paras(tail_bytes);
		decompparas += COMPRESSION_WORKSPACE_PARAGRAPHS;
		legacy_u16 allocated_paragraphs =
			retained_paragraphs > decompparas ? retained_paragraphs : decompparas;
		AMIGA_DECOMP_TRACE("allocation begin");
		dst = file_alloc_tail_pages(filename, allocated_paragraphs, tail_bytes);
		AMIGA_DECOMP_TRACE("allocation complete");
		/* Extra retained storage must not move the compressed source or any
		 * intermediate pass: those offsets determine the legacy tail bytes. */

		AMIGA_DECOMP_TRACE("file size begin");
		legacy_u16 paras = file_paras(filename, fatal);
		AMIGA_DECOMP_TRACE("file size complete");
		if (paras) {
			legacy_u8 far *src =
				dos_memory_make_pointer(decompparas - paras + dos_memory_pointer_segment(dst),
										dos_memory_pointer_offset(dst));
			AMIGA_DECOMP_TRACE("read begin");
			src = file_read(filename, src, fatal);
			AMIGA_DECOMP_TRACE("read complete");
			if (src) {
				legacy_u8 passes = *src;

				// If the multi-pass flag is set, the first byte contains the
				// number of compression passes and the next three bytes holds
				// the final decompressed size.
				if (passes & BYTE_HIGH_BIT) {
					passes &= (legacy_u8)~BYTE_HIGH_BIT;
					src += COMPR_HEADER_SIZE;
				}
				// Flag not set, first byte is compression type of the first
				// and only pass.
				else {
					passes = 1;
				}

				// Decode all compression passes.
				while (!err && passes) {
#ifdef RESTUNTS_AMIGA_TRACE
					fprintf(stdout, "Amiga decompress: pass begin, remaining=%u codec=%u\n",
							(legacy_uint)passes, (legacy_uint)*src);
					fflush(stdout);
#endif
					if (file_decomp_pass(src, dst, decompparas, &passlen) == 0) {
						err = 1;
					}

					AMIGA_DECOMP_TRACE("pass complete");
					// Set source for next pass.
					if (!err && (--passes != 0)) {
						AMIGA_DECOMP_TRACE("relocate begin");
						src = file_relocate_decomp_output(dst, decompparas, passlen);
						AMIGA_DECOMP_TRACE("relocate complete");
					}
				}

				// Free unneeded overhead.
				if (!err) {
					AMIGA_DECOMP_TRACE("resize begin");
					mmgr_resize_memory(dos_memory_pointer_offset(dst),
									   dos_memory_pointer_segment(dst), retained_paragraphs);
					AMIGA_DECOMP_TRACE("resize complete");

					return dst;
				}
			}
		}
	}

	if (tail_bytes && dst) {
		mmgr_release(dst);
	}
	if (fatal) {
		fatal_error(invalid_pack_type_error_format, filename);
	}

	return 0;
}

void far *file_decomp(const legacy_s8 *filename, legacy_s16 fatal)
{
	return file_decomp_internal(filename, fatal, 0);
}

void far *file_decomp_fatal(const legacy_s8 *filename)
{
	return file_decomp(filename, 1);
}

void far *file_decomp_nofatal(const legacy_s8 *filename)
{
	return file_decomp(filename, 0);
}
