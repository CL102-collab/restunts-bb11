#include "video_mode.h"
#include "video_mode_bitmaps.h"
#include "../../c/shape2d/shape2d.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MODE_ADDRESS_COUNT (LEGACY_U16_MAX + 1UL)
#define MODE_SCREEN_WIDTH 320U
#define MODE_SCREEN_HEIGHT 200U
#define MODE_SCREEN_PIXELS (MODE_SCREEN_WIDTH * MODE_SCREEN_HEIGHT)
#define MODE_PALETTE_COUNT (LEGACY_U8_MAX + 1U)
#define MODE_EGA_COLOR_COUNT 16U
#define MODE_SOURCE_STACK_CAPACITY 8U
#define MODE_NATIVE_COLOR_COUNT 3U
#define MODE_COLOR_MASK LEGACY_U8_MAX
#define MODE_EXPECTED_SHIFT LEGACY_BYTE_BITS
#define MODE_XOR_SHIFT LEGACY_WORD_BITS
#define MODE_VALID (1UL << LEGACY_THREE_BYTE_BITS)
#define MODE_PHASE_X_MASK 3U
#define MODE_PHASE_Y_SHIFT 2U
#define MODE_RED_WEIGHT 30U
#define MODE_GREEN_WEIGHT 59U
#define MODE_BLUE_WEIGHT 11U
#define MODE_CHANNEL_MASK LEGACY_U8_MAX

struct VIDEO_MODE_SURFACE {
	struct VIDEO_MODE_SURFACE *next;
	const legacy_u8 *base;
	legacy_u32 extent;
	const legacy_u8 *line_offsets;
	legacy_u16 pitch;
	legacy_u16 rows;
	legacy_u32 cells[MODE_ADDRESS_COUNT];
	legacy_u8 phase[MODE_ADDRESS_COUNT];
};

struct VIDEO_MODE_SOURCE {
	const legacy_u8 *table;
	legacy_u8 row_phase;
	legacy_u8 x_phase;
	legacy_u8 stored_row;
	legacy_u8 source_phase;
};

legacy_u8 sdl3_video_mode_selected = SDL3_VIDEO_MODE_VGA;
static struct VIDEO_MODE_SURFACE *mode_surfaces;
static struct VIDEO_MODE_SOURCE mode_sources[MODE_SOURCE_STACK_CAPACITY];
static legacy_u32 mode_source_depth;
static legacy_u8 mode_sample_pending;
static legacy_u8 mode_sample_phase;
static legacy_u32 mode_palette[MODE_PALETTE_COUNT];
static legacy_u8 mode_nearest[MODE_PALETTE_COUNT];
static legacy_u8 mode_framebuffer[MODE_SCREEN_PIXELS];
static legacy_u32 mode_generation;
static legacy_u8 mode_primitive_freeze;
static legacy_u8 mode_primitive_capture;
static legacy_u16 mode_line_depth;
static legacy_u16 mode_point_depth;
static legacy_u16 mode_native_colors[MODE_NATIVE_COLOR_COUNT];
static const legacy_u8 mode_cga_colors[] = {0, 3, 5, 7};
static const legacy_u8 *mode_xor_base;
static legacy_u16 mode_xor_offset;
static legacy_u8 mode_xor_mask;
static legacy_u8 mode_xor_pending;

static struct VIDEO_MODE_SURFACE *mode_surface(const legacy_u8 *base, legacy_u8 create)
{
	if (!base) {
		return NULL;
	}
	for (struct VIDEO_MODE_SURFACE *surface = mode_surfaces; surface; surface = surface->next) {
		if (surface->base == base) {
			return surface;
		}
	}
	if (!create) {
		return NULL;
	}
	struct VIDEO_MODE_SURFACE *surface = calloc(1, sizeof(*surface));
	if (!surface) {
		return NULL;
	}
	surface->base = base;
	for (legacy_u32 offset = 0; offset < MODE_ADDRESS_COUNT; offset++) {
		surface->phase[offset] =
			(legacy_u8)((offset % MODE_SCREEN_WIDTH & MODE_PHASE_X_MASK) |
						((offset / MODE_SCREEN_WIDTH & 1U) << MODE_PHASE_Y_SHIFT));
	}
	surface->next = mode_surfaces;
	mode_surfaces = surface;
	return surface;
}

static legacy_u8 mode_expected(legacy_u32 cell)
{
	return (legacy_u8)(cell >> MODE_EXPECTED_SHIFT);
}

static legacy_u8 mode_cell_matches(legacy_u32 cell, legacy_u8 value)
{
	return (cell & MODE_VALID) != 0 && mode_expected(cell) == value;
}

static legacy_u8 mode_hardware(legacy_u32 cell, legacy_u8 value)
{
	if (mode_cell_matches(cell, value) && !(cell & (MODE_COLOR_MASK << MODE_XOR_SHIFT))) {
		return (legacy_u8)cell;
	}
	return mode_nearest[value];
}

static void mode_store(struct VIDEO_MODE_SURFACE *surface, legacy_u16 offset, legacy_u8 value,
					   legacy_u8 hardware, legacy_u8 xor_mask)
{
	legacy_u32 cell = MODE_VALID | hardware | ((legacy_u32)value << MODE_EXPECTED_SHIFT) |
					  ((legacy_u32)xor_mask << MODE_XOR_SHIFT);
	if (surface->cells[offset] != cell) {
		surface->cells[offset] = cell;
		mode_generation++;
	}
	if (surface->extent <= offset) {
		surface->extent = (legacy_u32)offset + 1U;
	}
}

static void mode_update_nearest(void)
{
	legacy_u8 nearest[MODE_PALETTE_COUNT];
	for (legacy_u16 color = 0; color < MODE_PALETTE_COUNT; color++) {
		legacy_u32 best = LEGACY_U32_MAX;
		legacy_u8 selected = 0;
		legacy_u8 count = sdl3_video_mode_selected == SDL3_VIDEO_MODE_CGA ? sizeof(mode_cga_colors)
																		  : MODE_EGA_COLOR_COUNT;
		for (legacy_u8 candidate = 0; candidate < count; candidate++) {
			legacy_u8 index = sdl3_video_mode_selected == SDL3_VIDEO_MODE_CGA
								  ? mode_cga_colors[candidate]
								  : candidate;
			legacy_s32 red =
				(legacy_s32)((mode_palette[color] >> LEGACY_WORD_BITS) & MODE_CHANNEL_MASK) -
				(legacy_s32)((mode_palette[index] >> LEGACY_WORD_BITS) & MODE_CHANNEL_MASK);
			legacy_s32 green =
				(legacy_s32)((mode_palette[color] >> LEGACY_BYTE_BITS) & MODE_CHANNEL_MASK) -
				(legacy_s32)((mode_palette[index] >> LEGACY_BYTE_BITS) & MODE_CHANNEL_MASK);
			legacy_s32 blue = (legacy_s32)(mode_palette[color] & MODE_CHANNEL_MASK) -
							  (legacy_s32)(mode_palette[index] & MODE_CHANNEL_MASK);
			legacy_u32 distance = (legacy_u32)(red * red) * MODE_RED_WEIGHT +
								  (legacy_u32)(green * green) * MODE_GREEN_WEIGHT +
								  (legacy_u32)(blue * blue) * MODE_BLUE_WEIGHT;
			if (distance < best) {
				best = distance;
				selected = index;
			}
		}
		nearest[color] = selected;
	}
	if (memcmp(nearest, mode_nearest, sizeof(nearest)) != 0) {
		memcpy(mode_nearest, nearest, sizeof(nearest));
		mode_generation++;
	}
}

static legacy_u8 mode_bitmap_color(legacy_u8 value, legacy_u8 phase, legacy_u8 explicit_phase)
{
	const legacy_u8 *table;
	legacy_u8 row_phase = 0;
	if (mode_source_depth) {
		if (mode_source_depth > MODE_SOURCE_STACK_CAPACITY) {
			return mode_nearest[value];
		}
		const struct VIDEO_MODE_SOURCE *source = &mode_sources[mode_source_depth - 1U];
		table = source->table;
		if (!explicit_phase) {
			phase ^= source->x_phase;
			row_phase = source->row_phase;
		}
	} else {
		table = sdl3_video_bitmap_default_table(sdl3_video_mode_selected);
	}
	if (!table) {
		return mode_nearest[value];
	}
	legacy_u8 x = phase & MODE_PHASE_X_MASK;
	legacy_u8 y = ((phase >> MODE_PHASE_Y_SHIFT) ^ row_phase) & 1U;
	if (sdl3_video_mode_selected == SDL3_VIDEO_MODE_EGA) {
		return (legacy_u8)((table[value] >> (((x ^ y) & 1U) ? 0U : 4U)) & 15U);
	}
	legacy_u8 shift = (legacy_u8)((y ? 2U : 6U) - (x & 1U) * 2U);
	return mode_cga_colors[(table[value] >> shift) & 3U];
}

static legacy_u16 mode_native_word(legacy_u8 color)
{
	return color >= SDL3_VIDEO_NATIVE_PRIMARY && color <= SDL3_VIDEO_NATIVE_TERTIARY
			   ? mode_native_colors[color - SDL3_VIDEO_NATIVE_PRIMARY]
			   : color;
}

static legacy_u8 mode_native_color(legacy_u8 color, legacy_u8 phase)
{
	legacy_u16 word = mode_native_word(color);
	if (sdl3_video_mode_selected == SDL3_VIDEO_MODE_EGA) {
		return (legacy_u8)(word & (MODE_EGA_COLOR_COUNT - 1U));
	}
	legacy_u8 row = (phase >> MODE_PHASE_Y_SHIFT) & 1U;
	legacy_u8 packed = (legacy_u8)((mode_line_depth || row) ? word : word >> LEGACY_BYTE_BITS);
	legacy_u8 shift = (legacy_u8)(6U - (phase & MODE_PHASE_X_MASK) * 2U);
	return mode_cga_colors[(packed >> shift) & 3U];
}

/* CGA's original single-pixel writer ORs the low material byte into its
 * packed four-pixel byte. Higher material bits can therefore affect pixels
 * to the left of the requested point, while VGA memory stays untouched. */
static void mode_native_point(struct VIDEO_MODE_SURFACE *surface, legacy_u16 offset,
							  legacy_u8 color)
{
	legacy_u8 x = surface->phase[offset] & MODE_PHASE_X_MASK;
	legacy_u16 first = (legacy_u16)(offset - x);
	legacy_u8 packed = 0;
	for (legacy_u8 pixel = 0; pixel <= MODE_PHASE_X_MASK; pixel++) {
		legacy_u16 at = (legacy_u16)(first + pixel);
		legacy_u8 hardware = mode_hardware(surface->cells[at], surface->base[at]);
		legacy_u8 logical = 0;
		for (legacy_u8 candidate = 0; candidate < sizeof(mode_cga_colors); candidate++) {
			if (hardware == mode_cga_colors[candidate]) {
				logical = candidate;
				break;
			}
		}
		packed |= (legacy_u8)(logical << (6U - pixel * 2U));
	}
	legacy_u8 shift = (legacy_u8)(6U - x * 2U);
	packed = (legacy_u8)((packed & ~(3U << shift)) |
						 ((mode_native_word(color) << shift) & MODE_COLOR_MASK));
	for (legacy_u8 pixel = 0; pixel <= x; pixel++) {
		legacy_u16 at = (legacy_u16)(first + pixel);
		legacy_u8 hardware = mode_cga_colors[(packed >> (6U - pixel * 2U)) & 3U];
		mode_store(surface, at, surface->base[at], hardware, 0);
	}
}

void sdl3_video_mode_target(const struct SPRITE *sprite)
{
	if (sdl3_video_mode_selected == SDL3_VIDEO_MODE_VGA || !sprite || !sprite->sprite_bitmapptr ||
		!sprite->sprite_lineofs || !sprite->sprite_pitch) {
		return;
	}
	struct VIDEO_MODE_SURFACE *surface =
		mode_surface((const legacy_u8 *)sprite->sprite_bitmapptr, 1);
	if (!surface ||
		(surface->line_offsets == sprite->sprite_lineofs &&
		 surface->pitch == sprite->sprite_pitch && surface->rows >= sprite->sprite_bottom)) {
		return;
	}
	surface->line_offsets = sprite->sprite_lineofs;
	surface->pitch = sprite->sprite_pitch;
	surface->rows = sprite->sprite_bottom;
	for (legacy_u32 y = 0; y < sprite->sprite_bottom; y++) {
		legacy_u16 row = LEGACY_READ_U16_LE(sprite->sprite_lineofs + y * LEGACY_WORD_BYTES);
		for (legacy_u32 x = 0; x < sprite->sprite_pitch; x++) {
			legacy_u16 offset = (legacy_u16)(row + x);
			surface->phase[offset] =
				(legacy_u8)((x & MODE_PHASE_X_MASK) | ((y & 1U) << MODE_PHASE_Y_SHIFT));
			if (surface->extent <= offset) {
				surface->extent = (legacy_u32)offset + 1U;
			}
		}
	}
}

void sdl3_video_mode_source_begin_at(const struct SHAPE2D *shape, legacy_u16 draw_x,
									 legacy_u16 draw_y)
{
	mode_sample_pending = 0;
	if (sdl3_video_mode_selected == SDL3_VIDEO_MODE_VGA) {
		return;
	}
	if (mode_source_depth < MODE_SOURCE_STACK_CAPACITY) {
		struct VIDEO_MODE_SOURCE *source = &mode_sources[mode_source_depth];
		source->table = sdl3_video_bitmap_table(shape, sdl3_video_mode_selected);
		source->row_phase = sdl3_video_bitmap_row_phase(shape, draw_y);
		source->source_phase = sdl3_video_bitmap_source_phase(shape);
		source->x_phase = source->source_phase ? draw_x & 1U : 0;
		source->stored_row =
			shape ? (legacy_u8)(LEGACY_READ_U16_LE((const legacy_u8 *)shape +
												   offsetof(struct SHAPE2D, position_y)) &
								1U)
				  : 0;
	}
	mode_source_depth++;
}

void sdl3_video_mode_source_begin(const struct SHAPE2D *shape, legacy_u16 draw_y)
{
	sdl3_video_mode_source_begin_at(shape, 0, draw_y);
}

void sdl3_video_mode_source_sample(legacy_u16 source_x, legacy_u16 source_y)
{
	mode_sample_pending = 0;
	if (!mode_source_depth || mode_source_depth > MODE_SOURCE_STACK_CAPACITY) {
		return;
	}
	const struct VIDEO_MODE_SOURCE *source = &mode_sources[mode_source_depth - 1U];
	if (source->source_phase) {
		mode_sample_phase =
			(legacy_u8)((source_x & MODE_PHASE_X_MASK) |
						(((source->stored_row + source_y) & 1U) << MODE_PHASE_Y_SHIFT));
		mode_sample_pending = 1;
	}
}

void sdl3_video_mode_source_end(void)
{
	mode_sample_pending = 0;
	if (mode_source_depth) {
		mode_source_depth--;
	}
}

legacy_u8 sdl3_video_mode_write(const legacy_u8 *base, legacy_u16 offset, legacy_u8 color)
{
	legacy_u8 explicit_phase = mode_sample_pending;
	legacy_u8 source_phase = mode_sample_phase;
	mode_sample_pending = 0;
	if (sdl3_video_mode_selected == SDL3_VIDEO_MODE_VGA || !base) {
		return 0;
	}
	struct VIDEO_MODE_SURFACE *surface = mode_surface(base, 1);
	if (!surface) {
		return mode_primitive_capture;
	}
	if (mode_primitive_capture) {
		if (mode_point_depth && sdl3_video_mode_selected == SDL3_VIDEO_MODE_CGA) {
			mode_native_point(surface, offset, color);
		} else {
			mode_store(surface, offset, base[offset],
					   mode_native_color(color, surface->phase[offset]), 0);
		}
		return 1;
	}
	legacy_u32 old = surface->cells[offset];
	if (mode_xor_pending && mode_xor_base == base && mode_xor_offset == offset) {
		legacy_u8 xor_mask =
			mode_cell_matches(old, base[offset]) ? (legacy_u8)(old >> MODE_XOR_SHIFT) : 0;
		legacy_u8 hardware =
			mode_cell_matches(old, base[offset]) ? (legacy_u8)old : mode_nearest[base[offset]];
		mode_store(surface, offset, color, hardware, xor_mask ^ mode_xor_mask);
		mode_xor_pending = 0;
	} else if (mode_primitive_freeze) {
		mode_store(surface, offset, color, mode_hardware(old, base[offset]), 0);
	} else {
		mode_store(surface, offset, color,
				   mode_bitmap_color(color, explicit_phase ? source_phase : surface->phase[offset],
									 explicit_phase),
				   0);
	}
	return 0;
}

legacy_u8 sdl3_video_mode_raster(const legacy_u8 *destination, legacy_u16 destination_offset,
								 const legacy_u8 *source, legacy_u16 source_offset,
								 legacy_u16 count, legacy_s16 operation, const legacy_u8 *palette)
{
	legacy_u8 explicit_phase = mode_sample_pending;
	legacy_u8 source_phase = mode_sample_phase;
	mode_sample_pending = 0;
	if (sdl3_video_mode_selected == SDL3_VIDEO_MODE_VGA || !destination || !source) {
		return 0;
	}
	struct VIDEO_MODE_SURFACE *target = mode_surface(destination, 1);
	if (!target) {
		return mode_primitive_capture;
	}
	struct VIDEO_MODE_SURFACE *input = mode_surface(source, 0);
	for (legacy_u32 index = 0; index < count; index++) {
		legacy_u16 to = (legacy_u16)(destination_offset + index);
		legacy_u16 from = (legacy_u16)(source_offset + index);
		legacy_u32 old = target->cells[to];
		legacy_u8 previous = destination[to];
		legacy_u8 value = source[from];
		if (source == destination && (legacy_u16)(from - destination_offset) < index && input &&
			(input->cells[from] & MODE_VALID)) {
			value = mode_expected(input->cells[from]);
		}
		legacy_u8 source_value = value;
		if (operation == SHAPE2D_RASTER_AND) {
			value &= previous;
		} else if (operation == SHAPE2D_RASTER_OR) {
			value |= previous;
		} else if (operation == SHAPE2D_RASTER_MAP && palette) {
			value = palette[value];
			if (value == SHAPE2D_TRANSPARENT_COLOR) {
				continue;
			}
		}
		if (mode_primitive_capture) {
			if (mode_point_depth && sdl3_video_mode_selected == SDL3_VIDEO_MODE_CGA) {
				mode_native_point(target, to, value);
			} else {
				mode_store(target, to, previous, mode_native_color(value, target->phase[to]), 0);
			}
		} else if (mode_primitive_freeze) {
			mode_store(target, to, value, mode_hardware(old, previous), 0);
		} else if ((operation == SHAPE2D_RASTER_AND || operation == SHAPE2D_RASTER_OR) &&
				   value == previous) {
			continue;
		} else {
			legacy_u8 phase = explicit_phase
								  ? (legacy_u8)((source_phase & ~MODE_PHASE_X_MASK) |
												((source_phase + index) & MODE_PHASE_X_MASK))
								  : target->phase[to];
			legacy_u8 hardware = mode_bitmap_color(value, phase, explicit_phase);
			if (value == source_value && input &&
				mode_cell_matches(input->cells[from], source_value)) {
				hardware = mode_hardware(input->cells[from], source_value);
			}
			mode_store(target, to, value, hardware, 0);
		}
	}
	return mode_primitive_capture;
}

void sdl3_video_mode_copy_sample(const legacy_u8 *destination, legacy_u16 destination_offset,
								 const legacy_u8 *source, legacy_u16 source_offset)
{
	if (sdl3_video_mode_selected == SDL3_VIDEO_MODE_VGA || !destination || !source) {
		return;
	}
	struct VIDEO_MODE_SURFACE *input = mode_surface(source, 0);
	legacy_u8 value = destination[destination_offset];
	if (!input || value != source[source_offset] ||
		!mode_cell_matches(input->cells[source_offset], value)) {
		/* Resource sampling already captured the authored source phase in the
		 * preceding write. Only composed surfaces can replace that sample. */
		return;
	}
	struct VIDEO_MODE_SURFACE *target = mode_surface(destination, 1);
	if (target) {
		mode_store(target, destination_offset, value,
				   mode_hardware(input->cells[source_offset], value), 0);
	}
}

void sdl3_video_mode_xor_begin(const legacy_u8 *base, legacy_u16 offset, legacy_u8 mask)
{
	mode_xor_base = base;
	mode_xor_offset = offset;
	mode_xor_mask = mask;
	mode_xor_pending = sdl3_video_mode_selected != SDL3_VIDEO_MODE_VGA;
}

void sdl3_video_mode_primitive_begin(void)
{
	mode_line_depth = 0;
	mode_point_depth = 0;
	mode_primitive_freeze = sdl3_video_mode_selected != SDL3_VIDEO_MODE_VGA;
	mode_primitive_capture = 0;
}

void sdl3_video_mode_primitive_capture(const legacy_u16 colors[MODE_NATIVE_COLOR_COUNT])
{
	if (colors) {
		memcpy(mode_native_colors, colors, sizeof(mode_native_colors));
	}
	mode_primitive_capture = sdl3_video_mode_selected != SDL3_VIDEO_MODE_VGA;
	mode_primitive_freeze = 0;
}

void sdl3_video_mode_primitive_end(void)
{
	mode_line_depth = 0;
	mode_point_depth = 0;
	mode_primitive_capture = 0;
	mode_primitive_freeze = 0;
}

void sdl3_video_mode_line_begin(void)
{
	if (mode_primitive_capture && sdl3_video_mode_selected == SDL3_VIDEO_MODE_CGA) {
		mode_line_depth++;
	}
}

void sdl3_video_mode_line_end(void)
{
	if (mode_line_depth) {
		mode_line_depth--;
	}
}

void sdl3_video_mode_point_begin(void)
{
	if (mode_primitive_capture) {
		mode_point_depth++;
	}
}

void sdl3_video_mode_point_end(void)
{
	if (mode_point_depth) {
		mode_point_depth--;
	}
}

const legacy_u8 *sdl3_video_mode_framebuffer(const legacy_u8 *pixels)
{
	if (sdl3_video_mode_selected == SDL3_VIDEO_MODE_VGA || !pixels) {
		return pixels;
	}
	const struct VIDEO_MODE_SURFACE *surface = mode_surface(pixels, 0);
	for (legacy_u32 index = 0; index < MODE_SCREEN_PIXELS; index++) {
		mode_framebuffer[index] = mode_hardware(surface ? surface->cells[index] : 0, pixels[index]);
	}
	return mode_framebuffer;
}

void sdl3_video_mode_palette(const legacy_u32 *palette)
{
	if (palette) {
		memcpy(mode_palette, palette, sizeof(mode_palette));
		if (sdl3_video_mode_selected != SDL3_VIDEO_MODE_VGA) {
			mode_update_nearest();
		}
	}
}

legacy_u32 sdl3_video_mode_generation(void)
{
	return mode_generation;
}

static void mode_clear_surfaces(void)
{
	while (mode_surfaces) {
		struct VIDEO_MODE_SURFACE *next = mode_surfaces->next;
		free(mode_surfaces);
		mode_surfaces = next;
	}
	mode_source_depth = 0;
	mode_sample_pending = 0;
	mode_line_depth = 0;
	mode_point_depth = 0;
	mode_primitive_freeze = 0;
	mode_primitive_capture = 0;
	mode_xor_pending = 0;
	mode_generation++;
}

void sdl3_video_mode_forget(const void *base, legacy_u32 size)
{
	if (!base) {
		return;
	}
	uintptr_t begin = (uintptr_t)base;
	struct VIDEO_MODE_SURFACE **previous = &mode_surfaces;
	while (*previous) {
		struct VIDEO_MODE_SURFACE *surface = *previous;
		uintptr_t start = (uintptr_t)surface->base;
		if ((!size && surface->base == base) ||
			(size && ((start >= begin && start - begin < size) ||
					  (begin >= start && begin - start < surface->extent)))) {
			*previous = surface->next;
			free(surface);
			mode_generation++;
		} else {
			previous = &surface->next;
		}
	}
	sdl3_video_bitmap_forget(base, size ? size : 1U);
	if (mode_xor_base == base) {
		mode_xor_pending = 0;
	}
}

void sdl3_video_mode_shutdown(void)
{
	mode_clear_surfaces();
	sdl3_video_bitmap_reset();
}

void sdl3_video_set_startup_mode(legacy_u8 mode)
{
	if (mode > SDL3_VIDEO_MODE_CGA) {
		mode = SDL3_VIDEO_MODE_VGA;
	}
	if (mode != sdl3_video_mode_selected) {
		mode_clear_surfaces();
		sdl3_video_mode_selected = mode;
		mode_update_nearest();
	}
}
