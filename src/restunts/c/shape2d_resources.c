#include <stddef.h>
#ifdef RESTUNTS_AMIGA
#include <string.h>
#endif
#include "externs.h"
#include "fileio.h"
#include "legacy.h"
#include "memmgr.h"
#include "platform.h"
#include "resource.h"
#include "shape2d.h"
#include "shape2d_internal.h"
#ifdef RESTUNTS_AMIGA_TRACE
#include <stdio.h>
#define AMIGA_SHAPE2D_TRACE(stage)                                                                 \
	do {                                                                                           \
		puts("Amiga shape2d: " stage);                                                             \
		fflush(stdout);                                                                            \
	} while (0)
#else
#define AMIGA_SHAPE2D_TRACE(stage) ((void)0)
#endif

#define DOS_PARAGRAPH_SHIFT 4U
#define DOS_PARAGRAPH_OFFSET_MASK 15U
#define DWORD_SECOND_BYTE_SHIFT 8U
#define DWORD_THIRD_BYTE_SHIFT 16U
#define DWORD_FOURTH_BYTE_SHIFT 24U
#define RESOURCE_TABLE_ENTRY_SIZE 4U
#define RESOURCE_FILE_HEADER_SIZE 6U
#define PARSED_RESOURCE_TABLE_ENTRY_SIZE 8U
#define SHAPE2D_HEIGHT_OFFSET 2U
#define RLE_LITERAL_RUN_LIMIT 3
#define RLE_BLOCK_LIMIT 127
#define RLE_FULL_LITERAL_CONTROL 129U
#define SHAPE2D_FLIP_FLAG_MASK 240
#define SHAPE2D_FLIP_FLAG_SHIFT 4U
#define SHAPE2D_FLIP_TYPE_COUNT 3
#define SHAPE2D_PATTERN_MASK 15
#define SHAPE2D_PLANE_ENABLED_FLAG 1
#define SHAPE2D_PLANAR_PLANE_COUNT 4
#define SHAPE2D_BITS_PER_PLANE_BYTE 8
#define SHAPE2D_SOURCE_HIGH_BIT 128
#define SHAPE2D_COPIED_HEADER_WORDS 6U
#define SHAPE2D_EXPANDED_PIXEL_LIMIT 8000
#define SHAPE2D_UNFLIP_PARAGRAPH_PADDING 32
#define VGA_PALETTE_COLOR_COUNT 16
#define SHAPE2D_PATH_BUFFER_SIZE 100
#define PES_UNFLIP_WORKSPACE_PARAGRAPHS 1000
#define DOS_PARAGRAPH_BYTES 16U
#define MMGR_CHUNK_OVERHEAD_PARAGRAPHS 2U
#define OVERLAP_MARGIN_HALF_SHIFT 1U
#define OVERLAP_MARGIN_QUARTER_SHIFT 2U
#define OVERLAP_RESERVE_EIGHTH_SHIFT 3U

legacy_u32 shape2d_pointer_to_linear(void far *data)
{
	return ((legacy_u32)dos_memory_pointer_segment(data) << DOS_PARAGRAPH_SHIFT) +
		   dos_memory_pointer_offset(data);
}

void far *shape2d_pointer_from_linear(legacy_u32 linear_address)
{
	return dos_memory_make_pointer((legacy_u16)(linear_address >> DOS_PARAGRAPH_SHIFT),
								   (legacy_u16)linear_address & DOS_PARAGRAPH_OFFSET_MASK);
}

legacy_s16 shape2d_count_repeated_bytes(void far *data)
{
	legacy_u16 source_segment = dos_memory_pointer_segment(data);
	legacy_u16 source = dos_memory_pointer_offset(data);
#ifdef RESTUNTS_AMIGA
	/* No allocation occurs while scanning. Resolve this segment once, keeping
	 * the original 16-bit offset wrap even on native 32-bit pointers. */
	const legacy_u8 *source_base = dos_memory_make_pointer(source_segment, 0);
	legacy_u8 value = source_base[source];
#else
	legacy_u8 far *source_ptr = (legacy_u8 far *)dos_memory_make_pointer(source_segment, source);
	legacy_u8 value = *source_ptr;
#endif
	legacy_u16 count = 0;
	for (;;) {
#ifdef RESTUNTS_AMIGA
		legacy_u8 next = source_base[source];
#else
		source_ptr = (legacy_u8 far *)dos_memory_make_pointer(source_segment, source);
		legacy_u8 next = *source_ptr;
#endif
		source++;
		if (next != value) {
			return count;
		}
		count++;
	}
}

static legacy_u8 shape2d_far_read_byte(legacy_u16 segment, legacy_u16 offset)
{
	return *(legacy_u8 far *)dos_memory_make_pointer(segment, offset);
}

static void shape2d_far_write_byte(legacy_u16 segment, legacy_u16 offset, legacy_u8 value)
{
	*(legacy_u8 far *)dos_memory_make_pointer(segment, offset) = value;
}

static void shape2d_far_write_dword(legacy_u16 segment, legacy_u16 offset, legacy_u32 value)
{
	shape2d_far_write_byte(segment, offset, (legacy_u8)value);
	offset++;
	shape2d_far_write_byte(segment, offset, (legacy_u8)(value >> DWORD_SECOND_BYTE_SHIFT));
	offset++;
	shape2d_far_write_byte(segment, offset, (legacy_u8)(value >> DWORD_THIRD_BYTE_SHIFT));
	offset++;
	shape2d_far_write_byte(segment, offset, (legacy_u8)(value >> DWORD_FOURTH_BYTE_SHIFT));
}

static void shape2d_copy_wrapped(legacy_u16 source_segment, legacy_u16 *source,
								 legacy_u16 destination_segment, legacy_u16 *destination,
								 legacy_u16 count)
{
	legacy_u16 copied = 0;
#ifdef RESTUNTS_AMIGA
	/* Both mappings are local to this copy, so a later arena move cannot
	 * leave cached resource addresses behind. Increment the offsets, not
	 * the native pointers, to retain segment wrapping and overlap order. */
	const legacy_u8 *source_base = dos_memory_make_pointer(source_segment, 0);
	legacy_u8 *destination_base = dos_memory_make_pointer(destination_segment, 0);
#endif
	while (LEGACY_S16_FROM_BITS(copied) < LEGACY_S16_FROM_BITS(count)) {
#ifdef RESTUNTS_AMIGA
		destination_base[*destination] = source_base[*source];
#else
		shape2d_far_write_byte(destination_segment, *destination,
							   shape2d_far_read_byte(source_segment, *source));
#endif
		(*source)++;
		(*destination)++;
		copied++;
	}
}

static void shape2d_write_run(legacy_u16 source_segment, legacy_u16 *source,
							  legacy_u16 destination_segment, legacy_u16 *destination,
							  legacy_u16 count)
{
	shape2d_far_write_byte(destination_segment, *destination, (legacy_u8)count);
	(*destination)++;
	shape2d_far_write_byte(destination_segment, *destination,
						   shape2d_far_read_byte(source_segment, *source));
	(*destination)++;
	*source = LEGACY_U16_WRAP_ADD(*source, count);
}

struct SHAPE2D_PARSE_OUTPUT {
	legacy_u16 output_segment;
	legacy_u16 output_offset;
};

struct SHAPE2D_PARSE_RUN {
	legacy_u16 source_segment;
	legacy_u16 scan_offset;
	legacy_u16 literal_offset;
	legacy_u16 remaining;
	legacy_u16 literal_count;
};

static void shape2d_parse_literals(struct SHAPE2D_PARSE_RUN *run,
								   struct SHAPE2D_PARSE_OUTPUT *output)
{
	while (LEGACY_S16_FROM_BITS(run->literal_count) > RLE_BLOCK_LIMIT) {
		run->literal_count = LEGACY_U16_WRAP_SUB(run->literal_count, RLE_BLOCK_LIMIT);
		run->remaining = LEGACY_U16_WRAP_SUB(run->remaining, RLE_BLOCK_LIMIT);
		shape2d_far_write_byte(output->output_segment, output->output_offset,
							   RLE_FULL_LITERAL_CONTROL);
		output->output_offset++;
		shape2d_copy_wrapped(run->source_segment, &run->literal_offset, output->output_segment,
							 &output->output_offset, RLE_BLOCK_LIMIT);
	}
	if (run->literal_count != 0) {
		shape2d_far_write_byte(output->output_segment, output->output_offset,
							   (legacy_u8)(0U - run->literal_count));
		output->output_offset++;
		run->remaining = LEGACY_U16_WRAP_SUB(run->remaining, run->literal_count);
		shape2d_copy_wrapped(run->source_segment, &run->literal_offset, output->output_segment,
							 &output->output_offset, run->literal_count);
	}
}

static void shape2d_parse_repeats(struct SHAPE2D_PARSE_RUN *run,
								  struct SHAPE2D_PARSE_OUTPUT *output, legacy_u16 run_count)
{
	if (run_count > run->remaining) {
		run_count = run->remaining;
	}
	while (LEGACY_S16_FROM_BITS(run_count) > RLE_BLOCK_LIMIT) {
		run_count = LEGACY_U16_WRAP_SUB(run_count, RLE_BLOCK_LIMIT);
		run->remaining = LEGACY_U16_WRAP_SUB(run->remaining, RLE_BLOCK_LIMIT);
		shape2d_write_run(run->source_segment, &run->scan_offset, output->output_segment,
						  &output->output_offset, RLE_BLOCK_LIMIT);
	}
	if (LEGACY_S16_FROM_BITS(run_count) > RLE_LITERAL_RUN_LIMIT) {
		run->remaining = LEGACY_U16_WRAP_SUB(run->remaining, run_count);
		shape2d_write_run(run->source_segment, &run->scan_offset, output->output_segment,
						  &output->output_offset, run_count);
	}
}

static void shape2d_parse_bitmap(struct SHAPE2D far *shape, struct SHAPE2D_PARSE_OUTPUT *output)
{
	struct SHAPE2D_PARSE_RUN run;
	run.source_segment = dos_memory_pointer_segment(shape);
	legacy_u16 source_offset = dos_memory_pointer_offset(shape);
	shape2d_copy_wrapped(run.source_segment, &source_offset, output->output_segment,
						 &output->output_offset, SHAPE2D_HEADER_SIZE);
	run.scan_offset = source_offset;
	run.literal_offset = run.scan_offset;
	run.literal_count = 0;
	run.remaining =
		LEGACY_U16_WRAP_MUL(shape2d_get_word((legacy_u8 far *)shape),
							shape2d_get_word((legacy_u8 far *)shape + SHAPE2D_HEIGHT_OFFSET));
	run.scan_offset++;
	run.literal_count++;

	if (run.remaining != 0) {
		for (;;) {
			legacy_u16 run_count = (legacy_u16)shape2d_count_repeated_bytes(
				dos_memory_make_pointer(run.source_segment, run.scan_offset));
			if (LEGACY_S16_FROM_BITS(run_count) <= RLE_LITERAL_RUN_LIMIT &&
				run.literal_count < run.remaining) {
				run.scan_offset++;
				run.literal_count++;
				continue;
			}

			shape2d_parse_literals(&run, output);
			shape2d_parse_repeats(&run, output, run_count);
			run.literal_offset = run.scan_offset;
			run.literal_count = 0;
			run.scan_offset++;
			run.literal_count++;
			if (run.remaining == 0) {
				break;
			}
		}
	}
	shape2d_far_write_byte(output->output_segment, output->output_offset, 0);
	output->output_offset++;
}

void parse_shape2d(void far *memchunk, void far *mempages)
{
	legacy_u16 chunk_segment = dos_memory_pointer_segment(memchunk);
	legacy_u16 chunk_offset = dos_memory_pointer_offset(memchunk);
	legacy_u16 pages_segment = dos_memory_pointer_segment(mempages);
	legacy_u16 pages_offset = dos_memory_pointer_offset(mempages);
	legacy_u16 shape_count = file_get_res_shape_count(memchunk);
	legacy_u16 offsets_offset = LEGACY_U16_WRAP_ADD(
		pages_offset,
		LEGACY_U16_WRAP_ADD(LEGACY_U16_WRAP_MUL(shape_count, RESOURCE_TABLE_ENTRY_SIZE),
							RESOURCE_FILE_HEADER_SIZE));
	legacy_u16 header_size = LEGACY_U16_WRAP_ADD(
		LEGACY_U16_WRAP_MUL(shape_count, RESOURCE_TABLE_ENTRY_SIZE), RESOURCE_FILE_HEADER_SIZE);
	legacy_u16 copied = 0;
	while (LEGACY_S16_FROM_BITS(header_size) > LEGACY_S16_FROM_BITS(copied)) {
		shape2d_far_write_byte(pages_segment, pages_offset,
							   shape2d_far_read_byte(chunk_segment, chunk_offset));
		chunk_offset++;
		pages_offset++;
		copied++;
	}
	struct SHAPE2D_PARSE_OUTPUT output;
	output.output_segment = dos_memory_pointer_segment(mempages);
	output.output_offset = LEGACY_U16_WRAP_ADD(
		dos_memory_pointer_offset(mempages),
		LEGACY_U16_WRAP_ADD(LEGACY_U16_WRAP_MUL(shape_count, PARSED_RESOURCE_TABLE_ENTRY_SIZE),
							RESOURCE_FILE_HEADER_SIZE));
	legacy_u32 initial_output_linear = shape2d_pointer_to_linear(
		dos_memory_make_pointer(output.output_segment, output.output_offset));

	struct SHAPE2D far *shape;
	legacy_u16 shape_index = 0;
	while (LEGACY_S16_FROM_BITS(shape_index) < LEGACY_S16_FROM_BITS(shape_count)) {
		shape = file_get_shape2d((legacy_u8 far *)memchunk, shape_index);
		legacy_u32 output_linear = shape2d_pointer_to_linear(
			dos_memory_make_pointer(output.output_segment, output.output_offset));
		void far *output_pointer = shape2d_pointer_from_linear(output_linear);
		output.output_segment = dos_memory_pointer_segment(output_pointer);
		output.output_offset = dos_memory_pointer_offset(output_pointer);
		shape2d_far_write_dword(pages_segment, offsets_offset,
								output_linear - initial_output_linear);
		offsets_offset = LEGACY_U16_WRAP_ADD(offsets_offset, RESOURCE_TABLE_ENTRY_SIZE);

		shape2d_parse_bitmap(shape, &output);
		shape_index++;
	}

	legacy_u32 output_size = shape2d_pointer_to_linear(dos_memory_make_pointer(
								 output.output_segment, output.output_offset)) -
							 shape2d_pointer_to_linear(mempages);
	if ((legacy_u8)output_size & DOS_PARAGRAPH_OFFSET_MASK) {
		output_size = (output_size >> DOS_PARAGRAPH_SHIFT) + 1UL;
	} else {
		output_size >>= DOS_PARAGRAPH_SHIFT;
	}
	mmgr_resize_memory(dos_memory_pointer_offset(mempages), dos_memory_pointer_segment(mempages),
					   (legacy_u16)output_size);
}

static legacy_u8 far *file_get_shape2d_bytes(legacy_u8 far *memchunk, legacy_s16 index)
{
	return resource_file_data(memchunk, (legacy_u16)index);
}

struct SHAPE2D far *file_get_shape2d(legacy_u8 far *memchunk, legacy_s16 index)
{
	return (struct SHAPE2D far *)file_get_shape2d_bytes(memchunk, index);
}

void file_get_shape2d_identifier(legacy_u8 far *memchunk, legacy_u16 index, legacy_u32 *result)
{
	*result = LEGACY_READ_U32_LE(resource_file_identifier(memchunk, index));
}

legacy_u16 file_get_res_shape_count(void far *memchunk)
{
	return resource_file_count((const legacy_u8 far *)memchunk);
}

#ifdef RESTUNTS_AMIGA
static legacy_u32 shape2d_unflip_source(legacy_u32 destination, legacy_u16 width, legacy_u16 height,
										legacy_u8 layout)
{
	legacy_u16 row;
	legacy_u16 column;
	if (destination <= LEGACY_U16_MAX) {
		/* Keep the division 16-bit so GCC uses the 68000 DIVU instruction
		 * instead of its software 32-bit division routine for game images. */
		legacy_u16 narrow = (legacy_u16)destination;
		row = (legacy_u16)(narrow / width);
		column = (legacy_u16)(narrow % width);
	} else {
		row = (legacy_u16)(destination / width);
		column = (legacy_u16)(destination % width);
	}
	if (layout == 1) {
		return (legacy_u32)column * height + row;
	}
	legacy_u16 even_rows = ((legacy_u32)height + 1U) >> 1;
	if (layout == 2) {
		return (legacy_u32)column * height + (row >> 1) + ((row & 1U) != 0 ? even_rows : 0U);
	}
	if ((row & 1U) != 0) {
		return (legacy_u32)width * even_rows + (legacy_u32)column * (legacy_u16)(height >> 1) +
			   (row >> 1);
	}
	return (legacy_u32)column * even_rows + (row >> 1);
}

/* A layout is a permutation of the bitmap bytes. Rotate each cycle in place
 * and remember the completed pixels using one bit each. A 320x200 bitmap
 * needs 8000 bytes of workspace instead of a second 64000-byte bitmap. */
static void shape2d_unflip_in_place(legacy_u8 *pixels, legacy_u8 *visited, legacy_s16 width,
									legacy_s16 height, legacy_u8 layout)
{
	if (width <= 0 || height <= 0) {
		return;
	}
	legacy_u32 count = (legacy_u32)width * height;
	memset(visited, 0, (count + 7U) >> 3);
	for (legacy_u32 start = 0; start < count; start++) {
		if ((visited[start >> 3] & (1U << (start & 7U))) != 0) {
			continue;
		}
		legacy_u8 saved = pixels[start];
		legacy_u32 destination = start;
		for (;;) {
			visited[destination >> 3] |= (legacy_u8)(1U << (destination & 7U));
			legacy_u32 source = shape2d_unflip_source(destination, width, height, layout);
			if (source == start) {
				pixels[destination] = saved;
				break;
			}
			pixels[destination] = pixels[source];
			destination = source;
		}
	}
}

static legacy_u16 shape2d_unflip_workspace_pages(legacy_u8 *resource)
{
	legacy_u16 count = resource_file_count(resource);
	legacy_u32 largest = 0;
	for (legacy_u16 index = 0; index < count; index++) {
		const struct SHAPE2D *shape = file_get_shape2d(resource, index);
		legacy_u8 layout = shape->plane_flags[2] >> SHAPE2D_FLIP_FLAG_SHIFT;
		if ((shape->plane_flags[3] & SHAPE2D_FLIP_FLAG_MASK) != 0 || layout == 0 ||
			layout > SHAPE2D_FLIP_TYPE_COUNT) {
			continue;
		}
		legacy_s16 width = shape2d_get_width(shape);
		legacy_s16 height = shape2d_get_height(shape);
		if (width > 0 && height > 0) {
			legacy_u32 pixels = (legacy_u32)width * height;
			if (pixels > largest) {
				largest = pixels;
			}
		}
	}
	return (legacy_u16)((largest + 127U) >> 7);
}
#else
static void shape2d_unflip_transpose(legacy_s8 far *membitmapptr, legacy_s8 far *mempages,
									 legacy_s16 width, legacy_s16 height)
{
	// regular flip
	for (legacy_s16 j = 0; j < height; j++) {	 // height
		for (legacy_s16 i = 0; i < width; i++) { // width
			mempages[i + j * width] = membitmapptr[j + i * height];
		}
	}
}

static void shape2d_unflip_interlace(legacy_s8 far *membitmapptr, legacy_s8 far *mempages,
									 legacy_s16 width, legacy_s16 height)
{
	// interlaced: the even rows first, then the odd
	// ones. loc_32BBA walks the second pass with
	// dx = 1, 3, .. while dx < height, so an odd
	// height gets one fewer odd row than even rows.
	for (legacy_s16 j = 0; j < height; j += 2) { // even rows
		for (legacy_s16 i = 0; i < width; i++) { // width
			mempages[i + j * width] = membitmapptr[LEGACY_S16_DIV_OR_ZERO(j, 2) + i * height];
		}
	}
	for (legacy_s16 j = 1; j < height; j += 2) { // odd rows
		for (legacy_s16 i = 0; i < width; i++) { // width
			mempages[i + j * width] =
				membitmapptr[LEGACY_S16_DIV_OR_ZERO(LEGACY_S16_WRAP_ADD(height, j), 2) +
							 i * height];
		}
	}
}

static void shape2d_unflip_column_blocks(legacy_s8 far *membitmapptr, legacy_s8 far *mempages,
										 legacy_s16 width, legacy_s16 height)
{
	// loc_32BDE. Even and odd rows are stored as two
	// separate column-major blocks: the even one holds
	// ceil(height/2) samples per column from offset 0,
	// the odd one holds height/2 per column starting at
	// width * ceil(height/2). The original never reloads
	// bx between the two halves of a pass, which is what
	// puts the odd rows at that offset.
	legacy_s16 evenrows = LEGACY_S16_DIV_OR_ZERO(LEGACY_S16_WRAP_ADD(height, 1), 2);
	legacy_s16 oddrows = LEGACY_S16_DIV_OR_ZERO(height, 2);
	for (legacy_s16 j = 0; j < height; j += 2) { // even rows
		for (legacy_s16 i = 0; i < width; i++) { // width
			mempages[i + j * width] = membitmapptr[LEGACY_S16_DIV_OR_ZERO(j, 2) + i * evenrows];
		}
	}
	for (legacy_s16 j = 1; j < height; j += 2) { // odd rows
		for (legacy_s16 i = 0; i < width; i++) { // width
			mempages[i + j * width] =
				membitmapptr[width * evenrows + LEGACY_S16_DIV_OR_ZERO(j, 2) + i * oddrows];
		}
	}
}
#endif

void file_unflip_shape2d(legacy_u8 far *memchunk, legacy_s8 far *mempages)
{

	legacy_s16 shapecount = resource_file_count(memchunk);
	struct SHAPE2D far *shape_header;
	legacy_s16 counter = 0;
	do {
		legacy_u8 far *memshape = file_get_shape2d_bytes(memchunk, counter);
		shape_header = (struct SHAPE2D far *)memshape;
		legacy_s8 far *membitmapptr = (legacy_s8 far *)memshape + SHAPE2D_HEADER_SIZE;
		legacy_u8 flag = shape_header->plane_flags[3];
		if ((flag & SHAPE2D_FLIP_FLAG_MASK) == 0) {
			flag = shape_header->plane_flags[2] >> SHAPE2D_FLIP_FLAG_SHIFT;
			if (flag != 0) {
				// The original does not merely skip an unknown flip type, it
				// gives up on the whole resource, so the shapes after this
				// one are left flipped and the
				// caller is told. This port skips the shape and carries on,
				// and is declared void. Harmless: the only call site
				// (asmorig/seg034.asm:237) does `add sp, 8` and goes straight
				// on to mmgr_release without ever reading ax, and the three
				// arms below cover every flip type the resources use.
				if (flag <= SHAPE2D_FLIP_TYPE_COUNT) {
					legacy_s16 width = shape2d_get_width(shape_header);
					legacy_s16 height = shape2d_get_height(shape_header);
#ifdef RESTUNTS_AMIGA
					shape2d_unflip_in_place((legacy_u8 *)membitmapptr, (legacy_u8 *)mempages, width,
											height, flag);
#else
					switch (flag - 1) {
						case 0:
							shape2d_unflip_transpose(membitmapptr, mempages, width, height);
							break;
						case 1:
							shape2d_unflip_interlace(membitmapptr, mempages, width, height);
							break;
						case 2:
							shape2d_unflip_column_blocks(membitmapptr, mempages, width, height);
							break;
					}

					// copy flipped bits from mempages -> subres
					for (legacy_s16 j = 0; j < height; j++) {	 // height
						for (legacy_s16 i = 0; i < width; i++) { // width
							membitmapptr[i + j * width] = mempages[i + j * width];
						}
					}
#endif
				}
			}
		}
		counter++;
		shapecount--;
	} while (shapecount > 0);
}

void file_unflip_shape2d_pes(legacy_u8 far *memchunk, legacy_s8 far *mempages)
{
	legacy_s16 shapecount = file_get_res_shape_count(memchunk);

	struct SHAPE2D far *shape_header;
	for (legacy_s16 i = 0; i < shapecount; ++i) {
		legacy_u8 far *memshape = file_get_shape2d_bytes(memchunk, i);
		shape_header = (struct SHAPE2D far *)memshape;

		if (!(shape_header->plane_flags[3] & SHAPE2D_FLIP_FLAG_MASK)) {
			legacy_u8 val =
				(shape_header->plane_flags[2] >> SHAPE2D_FLIP_FLAG_SHIFT) & SHAPE2D_PATTERN_MASK;

			if (val) {
				legacy_s16 width = shape2d_get_width(shape_header);
				legacy_s16 height = shape2d_get_height(shape_header);
				legacy_u8 far *membitmapptr = memshape + SHAPE2D_HEADER_SIZE;

				for (legacy_s16 j = 0; j < SHAPE2D_PLANAR_PLANE_COUNT; ++j) {
					if (val & SHAPE2D_PLANE_ENABLED_FLAG) {
						for (legacy_s16 y = 0; y < height; ++y) {
							for (legacy_s16 x = 0; x < width; ++x) {
								mempages[y * width + x] = membitmapptr[x * height + y];
							}
						}

						// Copy flipped data from mempages -> subres
						for (legacy_s16 y = 0; y < height; ++y) {
							for (legacy_s16 x = 0; x < width; ++x) {
								membitmapptr[y * width + x] = mempages[y * width + x];
							}
						}
					}
					membitmapptr += width * height;
					val >>= 1;
				}
			}
		}
	}
}

void file_load_shape2d_expand(legacy_u8 far *memchunk, legacy_s8 far *mempages)
{
	legacy_s16 shapecount = file_get_res_shape_count(memchunk);

	// Skip size.
	legacy_u8 far *memchunkptr = memchunk + RESOURCE_FILE_COUNT_OFFSET;
	legacy_u8 far *mempagesptr = mempages + RESOURCE_FILE_COUNT_OFFSET;

	// Copy count and ids.
	legacy_u16 directory_prefix_size = LEGACY_U16_WRAP_ADD(
		RESOURCE_FILE_COUNT_SIZE, LEGACY_U16_WRAP_MUL(shapecount, RESOURCE_FILE_IDENTIFIER_SIZE));
	fmemcpy(mempagesptr, memchunkptr, directory_prefix_size);

	struct SHAPE2D far *destination_header;
	legacy_u32 nextoffset = 0;
	struct SHAPE2D far *source_header;
	for (legacy_s16 i = 0; i < shapecount; ++i) {
		legacy_u8 far *srcshape = file_get_shape2d_bytes(memchunk, i);
		source_header = (struct SHAPE2D far *)srcshape;
		legacy_u32 product =
			(legacy_u32)shape2d_get_width(source_header) * shape2d_get_height(source_header);
		legacy_s16 length = (legacy_s16)(legacy_u16)product;

		// dx:ax at this point is HIWORD(w*h) : (LOWORD(w*h)*8 + 16), each
		// half 16 bits wide and wrapping on its own - the three shl's and
		// the `add ax, size SHAPE2D` never carry into dx. Only the
		// `add ax, bx / adc dx, cx` that folds in the running offset does.
		legacy_u16 lowterm = (legacy_u16)length * SHAPE2D_BITS_PER_PLANE_BYTE + SHAPE2D_HEADER_SIZE;

		resource_file_set_offset((legacy_u8 far *)mempages, (legacy_u16)shapecount, (legacy_u16)i,
								 nextoffset);
		nextoffset +=
			(legacy_u32)lowterm +
			((legacy_u32)(legacy_u16)(product >> DWORD_THIRD_BYTE_SHIFT) << DWORD_THIRD_BYTE_SHIFT);

		legacy_u8 far *dstshape = file_get_shape2d_bytes((legacy_u8 far *)mempages, i);
		destination_header = (struct SHAPE2D far *)dstshape;
		// `mov cx, 6 / rep movsw` - the first six words only, up to and
		// including s2d_pos_y. s2d_unk3..s2d_unk6 hold the pattern and flip
		// nibbles and are deliberately left alone in the destination.
		fmemcpy(dstshape, srcshape, SHAPE2D_COPIED_HEADER_WORDS * sizeof(legacy_u16));

		shape2d_put_word(dstshape + offsetof(struct SHAPE2D, width),
						 LEGACY_U16_WRAP_MUL(shape2d_get_width(destination_header),
											 SHAPE2D_BITS_PER_PLANE_BYTE));

		if (length && length <= SHAPE2D_EXPANDED_PIXEL_LIMIT) {
			mempagesptr = dstshape + SHAPE2D_HEADER_SIZE;

			legacy_u32 val = source_header->plane_flags[1] >> SHAPE2D_FLIP_FLAG_SHIFT;
			val |= val << DWORD_SECOND_BYTE_SHIFT;

			for (legacy_s16 j = 0; j < length * SHAPE2D_PLANAR_PLANE_COUNT; ++j) {
				shape2d_put_word(mempagesptr, val);
				mempagesptr += sizeof(legacy_u16);
			}
			memchunkptr = srcshape + SHAPE2D_HEADER_SIZE;

			for (legacy_s16 j = 0; j < SHAPE2D_PLANAR_PLANE_COUNT; ++j) {
				legacy_u8 far pat = source_header->plane_flags[j] & SHAPE2D_PATTERN_MASK;

				if (pat) {
					mempagesptr = dstshape + SHAPE2D_HEADER_SIZE;
					for (legacy_s16 k = 0; k < length; ++k) {
						legacy_u8 far px = *memchunkptr++;
						for (legacy_s16 l = 0; l < SHAPE2D_BITS_PER_PLANE_BYTE; ++l) {
							if (px & SHAPE2D_SOURCE_HIGH_BIT) {
								*mempagesptr |= pat;
							}
							px <<= 1;
							mempagesptr++;
						}
					}
				} else {
					break;
				}
			}
		}
	}

	// Final size. The original folds this in as a 16-bit term too
	// (bx = shapecount*8 + 6, then `add ax, bx / adc dx, 0`).
	resource_file_set_size((legacy_u8 far *)mempages,
						   (legacy_u32)resource_file_data_start((legacy_u16)shapecount) +
							   nextoffset);
}

legacy_u16 file_get_unflip_size(legacy_s8 far *memchunk)
{
	legacy_u16 shapecount = file_get_res_shape_count(memchunk);

	struct SHAPE2D far *shape_header;
	legacy_u16 maxsize = 0;
	for (legacy_u16 i = 0; i < shapecount; i++) {
		legacy_u8 far *memshape = file_get_shape2d_bytes((legacy_u8 far *)memchunk, i);
		shape_header = (struct SHAPE2D far *)memshape;
		legacy_u16 size = (shape2d_get_width(shape_header) * shape2d_get_height(shape_header) +
						   SHAPE2D_UNFLIP_PARAGRAPH_PADDING) >>
						  DOS_PARAGRAPH_SHIFT;
		if (size > maxsize) {
			maxsize = size;
		}
	}
	return maxsize;
}

legacy_u16 file_load_shape2d_expandedsize(void far *memchunk)
{
	legacy_u16 shapecount = file_get_res_shape_count(memchunk);

	// The original forms this seed in AX, then uses CWD: both the shift and
	// header addition wrap to 16 bits before the result is sign-extended.
	legacy_s32 size = (legacy_s16)(legacy_u16)((shapecount * PARSED_RESOURCE_TABLE_ENTRY_SIZE) +
											   SHAPE2D_HEADER_SIZE);

	struct SHAPE2D far *shape_header;
	for (legacy_u16 i = 0; i < shapecount; ++i) {
		legacy_u8 far *memshape = file_get_shape2d_bytes((legacy_u8 far *)memchunk, i);
		shape_header = (struct SHAPE2D far *)memshape;
		// `shl ax, 3` then `sub dx, dx / adc`: the per-shape term is a
		// 16-bit value ZERO-extended into the accumulator, and the header
		// size is folded in afterwards with its own carry.
		size += (legacy_u32)(legacy_u16)(shape2d_get_width(shape_header) *
										 shape2d_get_height(shape_header) *
										 SHAPE2D_BITS_PER_PLANE_BYTE) +
				SHAPE2D_HEADER_SIZE;
	}

	return (size + SHAPE2D_HEADER_SIZE) >> DOS_PARAGRAPH_SHIFT;
}

void file_load_shape2d_palmap_init(legacy_u8 far *pal)
{
	for (legacy_s16 i = 0; i < VGA_PALETTE_COLOR_COUNT; ++i) {
		palmap[i] = pal[i];
	}
}

void file_load_shape2d_palmap_apply(legacy_u8 far *memchunk, legacy_u8 palmap[])
{
	legacy_u16 shapecount = file_get_res_shape_count(memchunk);

	struct SHAPE2D far *shape_header;
	for (legacy_u16 i = 0; i < shapecount; ++i) {
		legacy_u8 far *memshape = file_get_shape2d_bytes(memchunk, i);
		shape_header = (struct SHAPE2D far *)memshape;
		legacy_u16 length = shape2d_get_width(shape_header) * shape2d_get_height(shape_header);

		legacy_u8 far *memchunkptr = memshape + SHAPE2D_HEADER_SIZE;

		for (legacy_u16 j = 0; j < length; ++j) {
			// `mov bl, es:[di] / mov al, [bx+si] / stosb` - the lookup reads
			// the byte di is on, and only stosb advances di afterwards.
			*memchunkptr = palmap[*memchunkptr];
			memchunkptr++;
		}
	}
}

void far *file_load_shape2d_esh(void far *memchunk, const legacy_s8 *str)
{
	legacy_u16 expandedsize = file_load_shape2d_expandedsize(memchunk);

	void far *palmapres = locate_shape_nofatal(memchunk, "!MGA");

	if (palmapres) {
		file_load_shape2d_palmap_init((legacy_u8 far *)palmapres + SHAPE2D_HEADER_SIZE);
	}

	void far *mempages = mmgr_alloc_pages(str, expandedsize);

	resource_file_set_size((legacy_u8 far *)mempages,
						   (legacy_u32)expandedsize * DOS_PARAGRAPH_BYTES);

	file_load_shape2d_expand(memchunk, mempages);
	mmgr_release(memchunk);
	memchunk = mmgr_compact_live_chunk(mempages);
	file_load_shape2d_palmap_apply(memchunk, palmap);

	return memchunk;
}

static void far *shape2d_find_cached(const legacy_s8 *shapename, legacy_s8 *str,
									 legacy_s8 **extension)
{
	strcpy(str, shapename);
	legacy_s8 *strptr = str;

	while (*strptr != '.' && *strptr) {
		strptr++;
	}

	void far *memchunk;
	if (*strptr != 0) {
		memchunk = mmgr_get_chunk_by_name(str);
		if (memchunk) {
			return memchunk; // return existing chunk with same name
		}
	} else {
		for (legacy_s16 counter = 0; *shapeexts[counter] != 0; counter++) {
			strcpy(strptr, shapeexts[counter]);
			memchunk = mmgr_get_chunk_by_name(str);
			if (memchunk) {
				return memchunk; // return existing chunk with same name
			}

			if (file_find(str)) {
				break;
			}
		}
		// list exhausted: fall through to the dispatch with the last extension
		// (".ESH") still in str, like the original loc_3AA53 `jz _try_load_pvs`
	}

	*extension = strptr;
	return 0;
}

void far *file_load_shape2d(const legacy_s8 *shapename, legacy_s16 fatal)
{
	legacy_s8 *strptr;
	legacy_s8 str[SHAPE2D_PATH_BUFFER_SIZE];
	void far *memchunk = shape2d_find_cached(shapename, str, &strptr);
	if (memchunk) {
		return memchunk;
	}

	void far *mempages;
	if (stricmp(strptr, ".PVS") == 0) {
		AMIGA_SHAPE2D_TRACE("decompress begin");
		memchunk = file_decomp(str, fatal);
		AMIGA_SHAPE2D_TRACE("decompress complete");
		if (!memchunk) {
			return dos_memory_make_pointer(0, 0);
		}

#ifdef RESTUNTS_AMIGA
		AMIGA_SHAPE2D_TRACE("workspace sizing begin");
		legacy_u16 unflipsize = shape2d_unflip_workspace_pages(memchunk);
		AMIGA_SHAPE2D_TRACE("workspace sizing complete");
		if (unflipsize == 0) {
			return memchunk;
		}
#else
		legacy_s16 unflipsize = file_get_unflip_size(memchunk);
#endif
		mempages = mmgr_alloc_pages("UNFLIP", unflipsize);
		AMIGA_SHAPE2D_TRACE("unflip begin");
		file_unflip_shape2d(memchunk, mempages);
		AMIGA_SHAPE2D_TRACE("unflip complete");
		mmgr_release(mempages);
		AMIGA_SHAPE2D_TRACE("workspace released");

		return memchunk;
	} else if (stricmp(strptr, ".XVS") == 0) {
		return file_decomp(str, fatal);
	} else if (stricmp(strptr, ".PES") == 0) {
		memchunk = file_decomp(str, fatal);
		if (!memchunk) {
			return dos_memory_make_pointer(0, 0);
		}

		mempages = mmgr_alloc_pages("UNFLIP", PES_UNFLIP_WORKSPACE_PARAGRAPHS);
		file_unflip_shape2d_pes(memchunk, mempages);
		mmgr_release(mempages);

		return file_load_shape2d_esh(memchunk, str);
	} else if (stricmp(strptr, ".ESH") == 0) {
		memchunk = file_load_binary(str, fatal);
		if (!memchunk) {
			return dos_memory_make_pointer(0, 0);
		}

		return file_load_shape2d_esh(memchunk, str);
	} else { // .VSH or an explicit unknown extension
		return file_load_binary(str, fatal);
	}
}

void far *file_load_shape2d_fatal(const legacy_s8 *shapename)
{
	return file_load_shape2d(shapename, 1);
}

void far *file_load_shape2d_nofatal(const legacy_s8 *shapename)
{
	return file_load_shape2d(shapename, 0);
}

void far *file_load_shape2d_nofatal2(const legacy_s8 *shapename)
{
	return file_load_shape2d_nofatal(shapename);
}

void far *file_load_shape2d_res(const legacy_s8 *resname, legacy_s16 fatal)
{
	const legacy_s8 *shapename = mmgr_path_to_name(resname);
	void far *memchunk = mmgr_get_chunk_by_name(shapename);

	if (memchunk) {
		return memchunk;
	}

	memchunk = file_load_shape2d(shapename, fatal);
	if (!memchunk) {
		return 0;
	}

	legacy_s16 chunksize = mmgr_get_chunk_size(memchunk);

	// Parsing normally needs a second buffer as large as the loaded one, and
	// the largest custom dashboards leave no room for that in the arena.
	// If the second buffer does not fit, grow the chunk so the raw data can
	// slide up inside it and parse_shape2d can write downwards into it.
	//
	// That overlap is safe: both cursors run forwards with the writer
	// starting a margin below the reader, and across the stock and custom
	// cars the writer leads by at most 38% of the resource against a margin
	// of 60% or more. Compared shape by shape against the two-buffer output
	// the bytes are identical everywhere parse_shape2d writes; only the tail
	// past the last shape differs, and the parser leaves that region alone
	// in either case. What is left afterwards has the same size, position
	// and name as the two-buffer path would have produced.
	legacy_u16 freeparas = mmgr_get_ofs_diff();
	if (freeparas < (legacy_u16)chunksize + MMGR_CHUNK_OVERHEAD_PARAGRAPHS) {
		legacy_u16 margin = ((legacy_u16)chunksize >> OVERLAP_MARGIN_HALF_SHIFT) +
							((legacy_u16)chunksize >> OVERLAP_MARGIN_QUARTER_SHIFT);
		if (margin > freeparas - (freeparas >> OVERLAP_RESERVE_EIGHTH_SHIFT)) {
			margin = freeparas - (freeparas >> OVERLAP_RESERVE_EIGHTH_SHIFT);
		}

		if (margin >= ((legacy_u16)chunksize >> OVERLAP_MARGIN_HALF_SHIFT)) {
			legacy_u16 rawseg = dos_memory_pointer_segment(memchunk);
			mmgr_resize_memory(0, rawseg, chunksize + margin);
			copy_paras_reverse(rawseg, rawseg + margin, chunksize);
			parse_shape2d(dos_memory_make_pointer(rawseg + margin, 0),
						  dos_memory_make_pointer(rawseg, 0));
			/* parse_shape2d has already trimmed the chunk to the parsed size. */
			mmgr_rename_chunk(dos_memory_make_pointer(rawseg, 0), resname);
			return dos_memory_make_pointer(rawseg, 0);
		}
	}

	void far *mempages = mmgr_alloc_pages(resname, chunksize);

	parse_shape2d(memchunk, mempages);

	mmgr_release(memchunk);
	return mmgr_compact_live_chunk(mempages);
}

void far *file_load_shape2d_res_fatal(const legacy_s8 *resname)
{
	return file_load_shape2d_res(resname, 1);
}

void far *file_load_shape2d_res_nofatal(const legacy_s8 *resname)
{
	return file_load_shape2d_res(resname, 0);
}
