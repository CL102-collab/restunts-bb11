#ifndef RESTUNTS_HIRES_MEMORY_H
#define RESTUNTS_HIRES_MEMORY_H

#include "hires.h"
#include "../platform.h"

#define HIRES_MEMORY_PARAGRAPH_SHIFT 4U

/* Companion rasters must forget arena storage when its legacy contents change. */
static inline void hires_forget_paragraphs(legacy_u16 segment, legacy_u16 paragraphs)
{
	hires_forget_range(dos_memory_make_pointer(segment, 0),
					   (legacy_u32)paragraphs << HIRES_MEMORY_PARAGRAPH_SHIFT);
}

static inline void hires_resize_paragraphs(legacy_u16 segment, legacy_u16 previous,
										   legacy_u16 paragraphs)
{
	if (paragraphs != previous) {
		legacy_u16 retained = paragraphs < previous ? paragraphs : previous;
		legacy_u16 changed = paragraphs < previous ? previous - paragraphs : paragraphs - previous;
		hires_forget_paragraphs(segment + retained, changed);
	}
}

#endif
