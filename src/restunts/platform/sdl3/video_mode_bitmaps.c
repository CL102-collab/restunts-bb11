#include "video_mode_bitmaps.h"
#include "video_mode.h"
#include "../../c/shape2d/shape2d.h"
#include "../../c/resource.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define BITMAP_TABLE_SIZE 256U
#define BITMAP_PALETTE_INDEX_COUNT 10U
#define BITMAP_RESOURCE_LIMIT 4096U
#define BITMAP_CHECKER_MASK 1U

struct SDL3_BITMAP_RESOURCE {
	struct SDL3_BITMAP_RESOURCE *next;
	const legacy_u8 *begin;
	legacy_u32 size;
	legacy_u8 parsed;
	legacy_u8 is_default;
	legacy_u8 source_phase;
	legacy_u8 have_ega;
	legacy_u8 have_cga;
	legacy_u8 ega[BITMAP_TABLE_SIZE];
	legacy_u8 cga[BITMAP_TABLE_SIZE];
};

static struct SDL3_BITMAP_RESOURCE *bitmap_resources;
static const void *bitmap_default_resource;
static legacy_u8 bitmap_default_ega[BITMAP_TABLE_SIZE];
static legacy_u8 bitmap_default_cga[BITMAP_TABLE_SIZE];
static legacy_u8 bitmap_have_default_ega;
static legacy_u8 bitmap_have_default_cga;

static struct SDL3_BITMAP_RESOURCE *bitmap_resource_at(const void *pointer)
{
	uintptr_t address = (uintptr_t)pointer;
	for (struct SDL3_BITMAP_RESOURCE *resource = bitmap_resources; resource;
		 resource = resource->next) {
		uintptr_t start = (uintptr_t)resource->begin;
		if (address >= start && address - start < resource->size) {
			return resource;
		}
	}
	return NULL;
}

static legacy_u32 bitmap_resource_offset(const struct SDL3_BITMAP_RESOURCE *resource,
										 legacy_u16 count, legacy_u16 index)
{
	return LEGACY_READ_U32_LE(resource->begin + RESOURCE_FILE_DIRECTORY_OFFSET +
							  (legacy_u32)count * RESOURCE_FILE_IDENTIFIER_SIZE +
							  (legacy_u32)index * RESOURCE_FILE_OFFSET_SIZE);
}

static legacy_u8 bitmap_decode_table(const legacy_u8 *source, legacy_u32 available,
									 legacy_u8 parsed, legacy_u8 output[BITMAP_TABLE_SIZE])
{
	if (!parsed) {
		if (available < BITMAP_TABLE_SIZE) {
			return 0;
		}
		memcpy(output, source, BITMAP_TABLE_SIZE);
		return 1;
	}
	legacy_u32 input = 0;
	legacy_u16 used = 0;
	while (used < BITMAP_TABLE_SIZE) {
		if (input >= available) {
			return 0;
		}
		legacy_s16 control = LEGACY_S8_FROM_BITS(source[input++]);
		if (control == 0) {
			return 0;
		}
		legacy_u16 count = (legacy_u16)(control < 0 ? -control : control);
		legacy_u16 bytes = control < 0 ? count : 1U;
		if (count > BITMAP_TABLE_SIZE - used || bytes > available - input) {
			return 0;
		}
		if (control < 0) {
			memcpy(output + used, source + input, count);
		} else {
			memset(output + used, source[input], count);
		}
		input += bytes;
		used += count;
	}
	return 1;
}

static legacy_u8 bitmap_read_table(const struct SDL3_BITMAP_RESOURCE *resource, legacy_u8 family,
								   legacy_u8 palette, legacy_u8 output[BITMAP_TABLE_SIZE])
{
	legacy_u16 count = LEGACY_READ_U16_LE(resource->begin + RESOURCE_FILE_COUNT_OFFSET);
	legacy_u32 base =
		RESOURCE_FILE_DIRECTORY_OFFSET +
		(legacy_u32)count * (RESOURCE_FILE_IDENTIFIER_SIZE + RESOURCE_FILE_OFFSET_SIZE);
	/* Search the requested numbered table first, then the resource's own !eg0
	 * or !cg0. An invalid requested table must not hide a valid default table. */
	for (legacy_u8 attempt = 0; attempt < (palette ? 2U : 1U); attempt++) {
		legacy_u8 selected = attempt ? 0 : palette;
		for (legacy_u16 index = 0; index < count; index++) {
			const legacy_u8 *name = resource->begin + RESOURCE_FILE_DIRECTORY_OFFSET +
									(legacy_u32)index * RESOURCE_FILE_IDENTIFIER_SIZE;
			if (name[0] != '!' || name[1] != family || name[2] != 'g' ||
				name[3] != (legacy_u8)('0' + selected)) {
				continue;
			}
			legacy_u32 offset = bitmap_resource_offset(resource, count, index);
			if (offset > resource->size - base) {
				continue;
			}
			legacy_u32 start = base + offset;
			legacy_u32 end = resource->size;
			/* Directories need not be sorted. Keep malformed runs out of the
			 * following subresource as well as outside the containing chunk. */
			for (legacy_u16 other = 0; other < count; other++) {
				legacy_u32 next = bitmap_resource_offset(resource, count, other);
				if (next > offset && next < end - base) {
					end = base + next;
				}
			}
			if (end - start < SHAPE2D_HEADER_SIZE) {
				continue;
			}
			const legacy_u8 *shape = resource->begin + start;
			legacy_u32 pixels = (legacy_u32)LEGACY_READ_U16_LE(shape) *
								LEGACY_READ_U16_LE(shape + sizeof(legacy_u16));
			if (pixels != BITMAP_TABLE_SIZE) {
				continue;
			}
			if (bitmap_decode_table(shape + SHAPE2D_HEADER_SIZE, end - start - SHAPE2D_HEADER_SIZE,
									resource->parsed, output)) {
				return 1;
			}
		}
	}
	return 0;
}

static void bitmap_select_palette(struct SDL3_BITMAP_RESOURCE *resource, legacy_u8 palette)
{
	resource->have_ega = bitmap_read_table(resource, 'e', palette, resource->ega);
	resource->have_cga = bitmap_read_table(resource, 'c', palette, resource->cga);
	if (resource->is_default) {
		bitmap_default_resource = resource->begin;
		bitmap_have_default_ega = resource->have_ega;
		bitmap_have_default_cga = resource->have_cga;
		memcpy(bitmap_default_ega, resource->ega, sizeof(bitmap_default_ega));
		memcpy(bitmap_default_cga, resource->cga, sizeof(bitmap_default_cga));
	}
}

void sdl3_video_bitmap_forget(const void *begin, legacy_u32 size)
{
	if (!begin || !size) {
		return;
	}
	uintptr_t start = (uintptr_t)begin;
	struct SDL3_BITMAP_RESOURCE **previous = &bitmap_resources;
	while (*previous) {
		struct SDL3_BITMAP_RESOURCE *resource = *previous;
		uintptr_t address = (uintptr_t)resource->begin;
		if ((address >= start && address - start < size) ||
			(start >= address && start - address < resource->size)) {
			*previous = resource->next;
			if (bitmap_default_resource == resource->begin) {
				bitmap_default_resource = NULL;
				bitmap_have_default_ega = 0;
				bitmap_have_default_cga = 0;
			}
			free(resource);
		} else {
			previous = &resource->next;
		}
	}
}

void sdl3_video_bitmap_reset(void)
{
	bitmap_default_resource = NULL;
	bitmap_have_default_ega = 0;
	bitmap_have_default_cga = 0;
	while (bitmap_resources) {
		struct SDL3_BITMAP_RESOURCE *next = bitmap_resources->next;
		free(bitmap_resources);
		bitmap_resources = next;
	}
}

void sdl3_video_bitmap_register(const void *pointer, legacy_u32 size, legacy_u8 parsed)
{
	if (!pointer) {
		return;
	}
	sdl3_video_bitmap_forget(pointer, size ? size : 1U);
	if (size < RESOURCE_FILE_DIRECTORY_OFFSET) {
		return;
	}
	const legacy_u8 *bytes = pointer;
	legacy_u16 count = LEGACY_READ_U16_LE(bytes + RESOURCE_FILE_COUNT_OFFSET);
	legacy_u32 base =
		RESOURCE_FILE_DIRECTORY_OFFSET +
		(legacy_u32)count * (RESOURCE_FILE_IDENTIFIER_SIZE + RESOURCE_FILE_OFFSET_SIZE);
	if (!count || count > BITMAP_RESOURCE_LIMIT || base > size) {
		return;
	}
	struct SDL3_BITMAP_RESOURCE *resource = calloc(1, sizeof(*resource));
	if (!resource) {
		return;
	}
	resource->begin = bytes;
	resource->size = size;
	resource->parsed = parsed;
	resource->source_phase = 1;
	for (legacy_u16 index = 0; index < count; index++) {
		const legacy_u8 *name = bytes + RESOURCE_FILE_DIRECTORY_OFFSET +
								(legacy_u32)index * RESOURCE_FILE_IDENTIFIER_SIZE;
		if (memcmp(name, "!pal", RESOURCE_FILE_IDENTIFIER_SIZE) == 0) {
			resource->is_default = 1;
			break;
		}
	}
	resource->next = bitmap_resources;
	bitmap_resources = resource;
	bitmap_select_palette(resource, 0);
}

const legacy_u8 *sdl3_video_bitmap_table(const void *source, legacy_u8 mode)
{
	struct SDL3_BITMAP_RESOURCE *resource = bitmap_resource_at(source);
	if (!resource) {
		return NULL;
	}
	if (mode == SDL3_VIDEO_MODE_EGA && resource->have_ega) {
		return resource->ega;
	}
	if (mode == SDL3_VIDEO_MODE_CGA && resource->have_cga) {
		return resource->cga;
	}
	return NULL;
}

const legacy_u8 *sdl3_video_bitmap_default_table(legacy_u8 mode)
{
	if (mode == SDL3_VIDEO_MODE_EGA && bitmap_have_default_ega) {
		return bitmap_default_ega;
	}
	if (mode == SDL3_VIDEO_MODE_CGA && bitmap_have_default_cga) {
		return bitmap_default_cga;
	}
	return NULL;
}

legacy_u8 sdl3_video_bitmap_row_phase(const struct SHAPE2D *shape, legacy_u16 draw_y)
{
	if (!shape) {
		return 0;
	}
	legacy_u16 stored_y =
		LEGACY_READ_U16_LE((const legacy_u8 *)shape + offsetof(struct SHAPE2D, position_y));
	return (legacy_u8)((stored_y ^ draw_y) & BITMAP_CHECKER_MASK);
}

void sdl3_video_bitmap_set_raw(void *pointer)
{
	struct SDL3_BITMAP_RESOURCE *resource = bitmap_resource_at(pointer);
	if (resource) {
		resource->source_phase = 0;
	}
}

legacy_u8 sdl3_video_bitmap_source_phase(const void *source)
{
	struct SDL3_BITMAP_RESOURCE *resource = bitmap_resource_at(source);
	return resource ? resource->source_phase : 0;
}

void sdl3_video_bitmap_select(void *pointer, legacy_u8 palette)
{
	struct SDL3_BITMAP_RESOURCE *resource = bitmap_resource_at(pointer);
	if (resource && palette < BITMAP_PALETTE_INDEX_COUNT) {
		bitmap_select_palette(resource, palette);
	}
}

#if defined(RESTUNTS_SDL3) && defined(RESTUNTS_FULL) && !defined(RESTUNTS_HEADLESS)
void shape2d_select_ega_palette(void *pointer, legacy_u8 palette)
{
	sdl3_video_bitmap_select(pointer, palette);
}
#endif
