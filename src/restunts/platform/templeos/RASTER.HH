#ifndef RESTUNTS_TEMPLE_RASTER_HH
#define RESTUNTS_TEMPLE_RASTER_HH

#include "LEGACY.HH"

extern legacy_void TempleRasterForget(legacy_u8 *bitmap);
extern legacy_void TempleRasterForgetRange(legacy_u8 *begin,legacy_u32 size);
extern legacy_void TempleRasterForgetParagraphs(legacy_u16 segment,legacy_u16 paragraphs);
extern legacy_void TempleRasterResizeParagraphs(
    legacy_u16 segment,legacy_u16 previous,legacy_u16 paragraphs);

#endif
