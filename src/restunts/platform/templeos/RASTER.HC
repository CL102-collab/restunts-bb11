#include "RASTER.HH"
#include "PALETTE.HH"

#define TEMPLE_RASTER_X_SCALE TEMPLE_RENDER_SCALE
#define TEMPLE_RASTER_Y_SCALE TEMPLE_RENDER_SCALE
#define TEMPLE_RASTER_CELL_ROWS TEMPLE_RASTER_Y_SCALE
#define TEMPLE_RASTER_CELL_PIXELS (TEMPLE_RASTER_X_SCALE*TEMPLE_RASTER_CELL_ROWS)
#define TEMPLE_RASTER_CELLS (TEMPLE_WORD_MASK+1)
#define TEMPLE_RASTER_PAIR_SHIFT TEMPLE_BYTE_BITS
#define TEMPLE_RASTER_PAIR_FLAG (1<<(TEMPLE_BYTE_BITS*2))
#define TEMPLE_RASTER_XOR_SHIFT (TEMPLE_BYTE_BITS*2+1)
#define TEMPLE_RASTER_XOR_MASK (TEMPLE_BYTE_MASK<<TEMPLE_RASTER_XOR_SHIFT)
#define TEMPLE_RASTER_PAIR_LOW_FLAG (1<<(TEMPLE_RASTER_XOR_SHIFT+TEMPLE_BYTE_BITS))
#define TEMPLE_RASTER_EGA_DEFAULT_INDEX 0
#define TEMPLE_RASTER_EGA_INDEX_COUNT 10
#define TEMPLE_RASTER_RESOURCE_HEADER 6
#define TEMPLE_RASTER_RESOURCE_ID_BYTES 4
#define TEMPLE_RASTER_RESOURCE_ENTRY_BYTES 8
#define TEMPLE_RASTER_SHAPE_HEADER 16
#define TEMPLE_RASTER_SHAPE_POSITION_Y 10
#define TEMPLE_RASTER_RESOURCE_LIMIT 4096
#define TEMPLE_RASTER_PARAGRAPH_BYTES 16
#define TEMPLE_RASTER_POINT_LIMIT 64
#define TEMPLE_RASTER_PATTERN_ROWS 2
#define TEMPLE_RASTER_PATTERN_MASK (TEMPLE_BYTE_BITS-1)
#define TEMPLE_RASTER_PAINT_SOLID 0
#define TEMPLE_RASTER_PAINT_MASKED 1
#define TEMPLE_RASTER_PAINT_TWO_COLOR 2
#define TEMPLE_RASTER_TRANSPARENT_COLOR TEMPLE_BYTE_MASK
#define TEMPLE_RASTER_GHOST_COLOR 256
#define TEMPLE_RASTER_GHOST_PATTERN 0xCC33
#define TEMPLE_RASTER_HALF 0.5
#define TEMPLE_RASTER_SPHERE_HEIGHT_NUMERATOR 13
#define TEMPLE_RASTER_SPHERE_HEIGHT_DENOMINATOR 16
#define TEMPLE_RASTER_BYTE_SIGN 128

/* Each DOS logical cell owns four independent native pixels. Saved backgrounds
 * and window copies preserve all four, including detail from native geometry.
 * Samples retain their VGA index for AND/OR masks and an optional authored EGA
 * pair and its logical-pixel checker phase. Copies retain the phase along with
 * native coverage. Quantization happens after composition, never on mask operands.
 */
class CTempleRasterSurface
{
  CTempleRasterSurface *next;
  legacy_u8 *bitmap;
  legacy_s64 size;
  legacy_u32 *samples;
  legacy_u16 rows[TEMPLE_LOGICAL_HEIGHT];
  legacy_u8 phases[TEMPLE_RASTER_CELLS];
  legacy_u8 dirty;
};

class CTempleRasterResourceMap
{
  CTempleRasterResourceMap *next;
  legacy_u8 *begin;
  legacy_s64 size;
  legacy_u8 has_pairs;
  legacy_u8 parsed;
  legacy_u8 pairs[TEMPLE_PALETTE_SOURCE_COUNT];
};

CTempleRasterSurface *temple_raster_surfaces=NULL;
CTempleRasterSurface *temple_raster_target=NULL;
CTempleRasterResourceMap *temple_raster_resources=NULL;
legacy_u8 temple_raster_default_pairs[TEMPLE_PALETTE_SOURCE_COUNT];
legacy_u8 temple_raster_have_default=FALSE;
legacy_u8 *temple_raster_source_pairs=NULL;
legacy_u8 temple_raster_source_set=FALSE;
legacy_u8 temple_raster_source_phase_delta=0;
legacy_u8 temple_raster_native=FALSE;

legacy_void TempleRasterNative(legacy_u8 enabled)
{
  temple_raster_native=enabled;
}

legacy_s64 TempleRasterCoordinateScale()
{
  if(temple_raster_native) {
    return 1;
  }
  return TEMPLE_RENDER_SCALE;
}

legacy_u16 TempleRasterRead16(legacy_u8 *bytes)
{
  return bytes[0]|(bytes[1]<<TEMPLE_BYTE_BITS);
}

legacy_s64 TempleRasterSigned16(legacy_u16 value)
{
  value&=TEMPLE_WORD_MASK;
  if(value&TEMPLE_WORD_SIGN_BIT) {
    return value-TEMPLE_RASTER_CELLS;
  }
  return value;
}

legacy_u32 TempleRasterRead32(legacy_u8 *bytes)
{
  return TempleRasterRead16(bytes)|
      (TempleRasterRead16(bytes+sizeof(legacy_u16))<<(TEMPLE_BYTE_BITS*2));
}

legacy_s64 TempleRasterY(legacy_s64 y)
{
  return y*TEMPLE_RASTER_Y_SCALE;
}

CTempleRasterResourceMap *TempleRasterResourceAt(legacy_u8 *source)
{
  CTempleRasterResourceMap *resource=temple_raster_resources;
  while(resource) {
    if(source>=resource->begin&&source<resource->begin+resource->size) {
      return resource;
    }
    resource=resource->next;
  }
  return NULL;
}

legacy_u8 *TempleRasterPairs(legacy_u8 *source)
{
  CTempleRasterResourceMap *resource=TempleRasterResourceAt(source);
  if(resource&&resource->has_pairs) {
    return resource->pairs;
  }
  return NULL;
}

legacy_u32 TempleRasterSample(legacy_u8 value,legacy_u8 *pairs)
{
  /* HolyC arguments use full registers even with a byte declaration. */
  value&=TEMPLE_BYTE_MASK;
  if(pairs) {
    return value|(pairs[value]<<TEMPLE_RASTER_PAIR_SHIFT)|TEMPLE_RASTER_PAIR_FLAG;
  }
  return value;
}

legacy_u32 TempleRasterSampleAt(legacy_u8 value,legacy_u8 *pairs,legacy_s64 phase)
{
  legacy_u32 noreg sample=TempleRasterSample(value,pairs);
  if(pairs&&(phase&TEMPLE_PALETTE_CHECKER_MASK)) {
    sample|=TEMPLE_RASTER_PAIR_LOW_FLAG;
  }
  return sample;
}

legacy_u8 *TempleRasterDefaultPairs()
{
  if(temple_raster_have_default) {
    return temple_raster_default_pairs;
  }
  return NULL;
}

CTempleRasterSurface *TempleRasterFind(legacy_u8 *bitmap)
{
  CTempleRasterSurface *surface=temple_raster_surfaces;
  while(surface) {
    if(surface->bitmap==bitmap) {
      return surface;
    }
    surface=surface->next;
  }
  return NULL;
}

legacy_void TempleRasterRegisterSprite(CSprite *sprite)
{
  legacy_u8 *bitmap;
  legacy_s64 x;
  legacy_s64 y;
  legacy_s64 slot;
  legacy_s64 width;
  legacy_s64 height;
  legacy_u16 noreg offset;
  legacy_u32 noreg sample;
  CTempleRasterSurface *surface;
  if(!sprite||!sprite->sprite_bitmapptr||!sprite->sprite_lineofs) {
    return;
  }
  bitmap=RsDosMemoryMakePointer(RsDosMemoryPointerSegment(sprite->sprite_bitmapptr),0);
  if(TempleRasterFind(bitmap)) {
    return;
  }
  width=sprite->sprite_buffer_width;
  height=sprite->sprite_bottom;
  if(width<=0||width>TEMPLE_LOGICAL_WIDTH||height<=0||height>TEMPLE_LOGICAL_HEIGHT) {
    return;
  }
  surface=CAlloc(sizeof(CTempleRasterSurface));
  surface->bitmap=bitmap;
  surface->dirty=TRUE;
  surface->samples=CAlloc(TEMPLE_RASTER_CELLS*TEMPLE_RASTER_CELL_PIXELS*sizeof(legacy_u32));
  for(y=0;y<height;y++) {
    surface->rows[y]=TempleRasterRead16(sprite->sprite_lineofs+y*sizeof(legacy_u16));
    surface->size=MaxI64(surface->size,MinI64(TEMPLE_RASTER_CELLS,surface->rows[y]+width));
    for(x=0;x<width;x++) {
      offset=(surface->rows[y]+x)&TEMPLE_WORD_MASK;
      surface->phases[offset]=(x^y)&TEMPLE_PALETTE_CHECKER_MASK;
      sample=
          TempleRasterSampleAt(bitmap[offset],TempleRasterDefaultPairs(),surface->phases[offset]);
      for(slot=0;slot<TEMPLE_RASTER_CELL_PIXELS;slot++) {
        surface->samples[offset*TEMPLE_RASTER_CELL_PIXELS+slot]=sample;
      }
    }
  }
  surface->next=temple_raster_surfaces;
  temple_raster_surfaces=surface;
}

legacy_void TempleRasterForgetRange(legacy_u8 *begin,legacy_u32 size)
{
  CTempleRasterSurface *surface=temple_raster_surfaces;
  CTempleRasterSurface *next_surface;
  CTempleRasterResourceMap *resource;
  CTempleRasterResourceMap *next_resource;
  legacy_u8 *end=begin+size;
  if(!begin||!size) {
    return;
  }
  while(surface) {
    next_surface=surface->next;
    if(surface->bitmap<end&&surface->bitmap+surface->size>begin) {
      TempleRasterForget(surface->bitmap);
    }
    surface=next_surface;
  }
  resource=temple_raster_resources;
  while(resource) {
    next_resource=resource->next;
    if(resource->begin<end&&resource->begin+resource->size>begin) {
      TempleRasterForget(resource->begin);
    }
    resource=next_resource;
  }
}

legacy_void TempleRasterForgetParagraphs(legacy_u16 segment,legacy_u16 paragraphs)
{
  TempleRasterForgetRange(
      RsDosMemoryMakePointer(segment,0),paragraphs*TEMPLE_RASTER_PARAGRAPH_BYTES);
}

legacy_void TempleRasterResizeParagraphs(
    legacy_u16 segment,legacy_u16 previous,legacy_u16 paragraphs)
{
  if(paragraphs!=previous) {
    legacy_u16 retained=paragraphs<previous ? paragraphs : previous;
    legacy_u16 changed=paragraphs<previous ? previous-paragraphs : paragraphs-previous;
    TempleRasterForgetParagraphs((segment+retained)&TEMPLE_WORD_MASK,changed);
  }
}

legacy_void TempleRasterSelect(CSprite *sprite)
{
  if(!sprite||!sprite->sprite_bitmapptr) {
    return;
  }
  TempleRasterRegisterSprite(sprite);
  temple_raster_target=TempleRasterFind(
      RsDosMemoryMakePointer(RsDosMemoryPointerSegment(sprite->sprite_bitmapptr),0));
  temple_raster_source_pairs=NULL;
  temple_raster_source_set=FALSE;
  temple_raster_source_phase_delta=0;
}

legacy_void TempleRasterForget(legacy_u8 *bitmap)
{
  CTempleRasterSurface **previous=&temple_raster_surfaces;
  CTempleRasterSurface *surface;
  CTempleRasterResourceMap **previous_resource=&temple_raster_resources;
  CTempleRasterResourceMap *resource;
  legacy_u8 *base;
  if(!bitmap) {
    return;
  }
  base=RsDosMemoryMakePointer(RsDosMemoryPointerSegment(bitmap),0);
  while(surface=*previous) {
    if(surface->bitmap==base) {
      *previous=surface->next;
      if(temple_raster_target==surface) {
        temple_raster_target=NULL;
      }
      Free(surface->samples);
      Free(surface);
      break;
    }
    previous=&surface->next;
  }
  while(resource=*previous_resource) {
    if(resource->begin==bitmap) {
      *previous_resource=resource->next;
      if(temple_raster_source_pairs==resource->pairs) {
        temple_raster_source_pairs=NULL;
        temple_raster_source_set=FALSE;
        temple_raster_source_phase_delta=0;
      }
      Free(resource);
      break;
    }
    previous_resource=&resource->next;
  }
}

legacy_void TempleRasterSource(legacy_u8 *shape)
{
  temple_raster_source_pairs=TempleRasterPairs(shape);
  temple_raster_source_set=shape!=NULL;
  temple_raster_source_phase_delta=0;
}

legacy_void TempleRasterSourceAt(legacy_u8 *shape,legacy_u16 y)
{
  TempleRasterSource(shape);
  if(shape) {
    /* DOS keeps the bitmap's nominal row phase when drawing it at another Y. */
    temple_raster_source_phase_delta=
        (TempleRasterRead16(shape+TEMPLE_RASTER_SHAPE_POSITION_Y)^y)&
        TEMPLE_PALETTE_CHECKER_MASK;
  }
}

legacy_u8 *TempleRasterRegisterResource(
    legacy_u8 *pointer,legacy_s64 parsed,legacy_u8 palette_index)
{
  legacy_u8 *bytes=pointer;
  legacy_s64 count;
  legacy_s64 size;
  legacy_s64 base;
  legacy_s64 index;
  legacy_s64 offset;
  legacy_s64 output;
  legacy_s64 run;
  legacy_s64 control;
  legacy_s64 source_bytes;
  legacy_u8 noreg value;
  legacy_u8 *name;
  legacy_u8 *source;
  legacy_u8 *pair_source=NULL;
  legacy_u8 *fallback_source=NULL;
  legacy_u8 pairs[TEMPLE_PALETTE_SOURCE_COUNT];
  legacy_u8 noreg is_default=FALSE;
  CTempleRasterResourceMap *resource;
  if(!bytes) {
    return pointer;
  }
  count=TempleRasterRead16(bytes+sizeof(legacy_u32));
  size=RsMmgrGetChunkSize(pointer)*TEMPLE_RASTER_PARAGRAPH_BYTES;
  base=TEMPLE_RASTER_RESOURCE_HEADER+count*TEMPLE_RASTER_RESOURCE_ENTRY_BYTES;
  if(count<=0||count>TEMPLE_RASTER_RESOURCE_LIMIT||base>size) {
    return pointer;
  }
  /* A known resource without !eg0 uses color matching. Registering its range
   * also clears any previous table when the memory manager reuses an address.
   */
  resource=temple_raster_resources;
  while(resource&&resource->begin!=bytes) {
    resource=resource->next;
  }
  if(!resource) {
    resource=CAlloc(sizeof(CTempleRasterResourceMap));
    resource->next=temple_raster_resources;
    temple_raster_resources=resource;
  }
  resource->begin=bytes;
  resource->size=size;
  resource->has_pairs=FALSE;
  resource->parsed=parsed;
  for(index=0;index<count;index++) {
    name=bytes+TEMPLE_RASTER_RESOURCE_HEADER+index*TEMPLE_RASTER_RESOURCE_ID_BYTES;
    if(name[0]=='!' &&name[1]=='p' &&name[2]=='a' &&name[3]=='l') {
      is_default=TRUE;
    }
    if(name[0]=='!' &&name[1]=='e' &&name[2]=='g' &&
        (name[3]=='0' +palette_index||name[3]=='0')) {
      offset=base+
          TempleRasterRead32(bytes+TEMPLE_RASTER_RESOURCE_HEADER+
              count*TEMPLE_RASTER_RESOURCE_ID_BYTES+index*sizeof(legacy_u32));
      if(offset>=base&&offset+TEMPLE_RASTER_SHAPE_HEADER<=size&&
          TempleRasterRead16(bytes+offset)*
                  TempleRasterRead16(bytes+offset+sizeof(legacy_u16))==
              TEMPLE_PALETTE_SOURCE_COUNT) {
        if(name[3]=='0') {
          fallback_source=bytes+offset+TEMPLE_RASTER_SHAPE_HEADER;
        }
        if(name[3]=='0' +palette_index) {
          pair_source=bytes+offset+TEMPLE_RASTER_SHAPE_HEADER;
        }
      }
    }
  }
  if(is_default) {
    temple_raster_have_default=FALSE;
  }
  if(!pair_source) {
    pair_source=fallback_source;
  }
  if(!pair_source) {
    return pointer;
  }
  if(!parsed) {
    if(pair_source+TEMPLE_PALETTE_SOURCE_COUNT>bytes+size) {
      return pointer;
    }
    MemCpy(pairs,pair_source,TEMPLE_PALETTE_SOURCE_COUNT);
  } else {
    /* Parsed bitmap resources use signed run counts and a zero terminator. */
    source=pair_source;
    output=0;
    while(output<TEMPLE_PALETTE_SOURCE_COUNT) {
      if(source>=bytes+size) {
        return pointer;
      }
      control=*source++;
      if(control&TEMPLE_RASTER_BYTE_SIGN) {
        control-=TEMPLE_PALETTE_SOURCE_COUNT;
      }
      if(!control) {
        return pointer;
      }
      run=AbsI64(control);
      source_bytes=1;
      if(control<0) {
        source_bytes=run;
      }
      if(run>TEMPLE_PALETTE_SOURCE_COUNT-output||source+source_bytes>bytes+size) {
        return pointer;
      }
      value=*source;
      for(index=0;index<run;index++) {
        if(control<0) {
          value=*source++;
        }
        pairs[output++]=value;
      }
      if(control>0) {
        source++;
      }
    }
  }
  MemCpy(resource->pairs,pairs,sizeof(pairs));
  resource->has_pairs=TRUE;
  if(is_default) {
    MemCpy(temple_raster_default_pairs,pairs,sizeof(pairs));
    temple_raster_have_default=TRUE;
  }
  return pointer;
}

legacy_u8 *TempleRasterResource(legacy_u8 *resource)
{
  return TempleRasterRegisterResource(resource,FALSE,TEMPLE_RASTER_EGA_DEFAULT_INDEX);
}

legacy_u8 *TempleRasterResourceParsed(legacy_u8 *resource)
{
  return TempleRasterRegisterResource(resource,TRUE,TEMPLE_RASTER_EGA_DEFAULT_INDEX);
}

/* The DOS EGA menus explicitly select !egN before drawing. Store the selected
 * table on its resource: other resources keep their own palette, and samples
 * already composed into a window keep the pair captured when they were drawn.
 */
legacy_void RsShape2dSelectEgaPalette(legacy_u8 *pointer,legacy_u8 index)
{
  CTempleRasterResourceMap *resource=TempleRasterResourceAt(pointer);
  if(!resource||index>=TEMPLE_RASTER_EGA_INDEX_COUNT) {
    return;
  }
  TempleRasterRegisterResource(resource->begin,resource->parsed,index);
}

legacy_u8 RsVideoPagesIsTarget(legacy_u8 *bitmap)
{
  return TempleRasterFind(bitmap)!=NULL;
}

legacy_u8 RsVideoPagesUsesRasterHooks(legacy_u8 *bitmap)
{
  no_warn bitmap;
  return TRUE;
}

legacy_u8 RsVideoPagesReadPixel(legacy_u8 *bitmap,legacy_u16 offset)
{
  return bitmap[offset];
}

legacy_void RsVideoPagesWritePixel(legacy_u8 *bitmap,legacy_u16 offset,legacy_u8 color)
{
  CTempleRasterSurface *surface=TempleRasterFind(bitmap);
  legacy_s64 slot;
  legacy_u32 noreg sample;
  legacy_u8 *pairs=temple_raster_source_pairs;
  bitmap[offset]=color;
  if(!surface) {
    return;
  }
  surface->dirty=TRUE;
  if(!pairs&&!temple_raster_source_set) {
    pairs=TempleRasterDefaultPairs();
  }
  sample=TempleRasterSampleAt(
      color,pairs,surface->phases[offset]^temple_raster_source_phase_delta);
  for(slot=0;slot<TEMPLE_RASTER_CELL_PIXELS;slot++) {
    surface->samples[offset*TEMPLE_RASTER_CELL_PIXELS+slot]=sample;
  }
}

legacy_void RsVideoPagesFillSpan(
    legacy_u8 *bitmap,legacy_u16 offset,legacy_u16 count,legacy_u8 color)
{
  while(count--) {
    RsVideoPagesWritePixel(bitmap,offset,color);
    offset=(offset+1)&TEMPLE_WORD_MASK;
  }
}

legacy_void TempleRasterXorPixel(legacy_u8 *bitmap,legacy_u16 offset,legacy_u8 mask)
{
  CTempleRasterSurface *surface=TempleRasterFind(bitmap);
  legacy_s64 slot;
  legacy_u32 noreg toggle=
      (mask&TEMPLE_BYTE_MASK)|((mask&TEMPLE_BYTE_MASK)<<TEMPLE_RASTER_XOR_SHIFT);
  bitmap[offset]^=mask;
  if(!surface) {
    return;
  }
  surface->dirty=TRUE;
  /* Keep the authored pair while a reversible XOR is active. Presentation
   * matches the altered VGA index; a second XOR restores the exact original
   * pair and all native coverage samples, including mixed logical cells.
   */
  for(slot=0;slot<TEMPLE_RASTER_CELL_PIXELS;slot++) {
    surface->samples[offset*TEMPLE_RASTER_CELL_PIXELS+slot]^=toggle;
  }
}

legacy_u32 TempleRasterApply(legacy_u32 previous,legacy_u32 sample,legacy_s16 operation,
    legacy_u8 *palette,legacy_u8 *pairs)
{
  legacy_u8 noreg before=previous&TEMPLE_BYTE_MASK;
  legacy_u8 noreg value=sample&TEMPLE_BYTE_MASK;
  if(operation==SHAPE2D_RASTER_AND) {
    value&=before;
  } else if(operation==SHAPE2D_RASTER_OR) {
    value|=before;
  } else if(operation==SHAPE2D_RASTER_MAP) {
    value=palette[value];
    if(value==TEMPLE_RASTER_TRANSPARENT_COLOR) {
      return previous;
    }
  }
  if((operation==SHAPE2D_RASTER_AND||operation==SHAPE2D_RASTER_OR)&&value==before) {
    return previous;
  }
  if(value==(sample&TEMPLE_BYTE_MASK)) {
    return sample;
  }
  return TempleRasterSample(value,pairs)|(sample&TEMPLE_RASTER_PAIR_LOW_FLAG);
}

legacy_void RsVideoPagesRasterSpan(legacy_u8 *destination,legacy_u16 destination_offset,
    legacy_u8 *source,legacy_u16 source_offset,legacy_u16 count,legacy_s16 operation,
    legacy_u8 *palette)
{
  CTempleRasterSurface *target=TempleRasterFind(destination);
  CTempleRasterSurface *origin=TempleRasterFind(source);
  legacy_u8 *pairs=TempleRasterPairs(source+source_offset);
  legacy_s64 slot;
  legacy_u32 noreg sample;
  legacy_u32 noreg source_sample;
  legacy_u32 noreg result;
  legacy_u32 *target_cell;
  legacy_u32 *source_cell;
  if(target&&count) {
    target->dirty=TRUE;
  }
  if(!pairs&&!TempleRasterResourceAt(source+source_offset)) {
    pairs=temple_raster_source_pairs;
    if(!pairs&&!temple_raster_source_set) {
      pairs=TempleRasterDefaultPairs();
    }
  }
  while(count--) {
    sample=TempleRasterSample(source[source_offset],pairs);
    if(target) {
      sample=TempleRasterSampleAt(source[source_offset],pairs,
          target->phases[destination_offset]^temple_raster_source_phase_delta);
    }
    result=TempleRasterApply(destination[destination_offset],sample,operation,palette,pairs);
    if(target) {
      target_cell=target->samples+destination_offset*TEMPLE_RASTER_CELL_PIXELS;
      source_cell=NULL;
      if(origin) {
        source_cell=origin->samples+source_offset*TEMPLE_RASTER_CELL_PIXELS;
      }
      for(slot=0;slot<TEMPLE_RASTER_CELL_PIXELS;slot++) {
        source_sample=sample;
        if(source_cell) {
          source_sample=source_cell[slot];
        }
        target_cell[slot]=
            TempleRasterApply(target_cell[slot],source_sample,operation,palette,pairs);
      }
    }
    destination[destination_offset]=result&TEMPLE_BYTE_MASK;
    destination_offset=(destination_offset+1)&TEMPLE_WORD_MASK;
    source_offset=(source_offset+1)&TEMPLE_WORD_MASK;
  }
}

legacy_void RsVideoPagesCopySpan(legacy_u8 *destination,legacy_u16 destination_offset,
    legacy_u8 *source,legacy_u16 source_offset,legacy_u16 count)
{
  RsVideoPagesRasterSpan(
      destination,destination_offset,source,source_offset,count,SHAPE2D_RASTER_COPY,NULL);
}

legacy_void RsVideoPagesPatternSpan(legacy_u8 *bitmap,legacy_u16 offset,legacy_u16 count,
    legacy_u8 pattern,legacy_u8 color,legacy_u8 alternate_color,legacy_s16 two_colors)
{
  while(count--) {
    pattern=((pattern<<1)|(pattern>>(TEMPLE_BYTE_BITS-1)))&TEMPLE_BYTE_MASK;
    if(pattern&1) {
      if(two_colors) {
        RsVideoPagesWritePixel(bitmap,offset,alternate_color);
      } else {
        RsVideoPagesWritePixel(bitmap,offset,color);
      }
    } else if(two_colors) {
      RsVideoPagesWritePixel(bitmap,offset,color);
    }
    offset=(offset+1)&TEMPLE_WORD_MASK;
  }
}

legacy_void TempleRasterPlot(legacy_s64 x,legacy_s64 y,legacy_u16 color)
{
  legacy_s64 logical_y;
  legacy_s64 slot;
  legacy_u16 noreg offset;
  if(!temple_raster_target||x<drawing_sprite.sprite_raster_left*TEMPLE_RASTER_X_SCALE||
      x>=drawing_sprite.sprite_raster_right*TEMPLE_RASTER_X_SCALE||
      y<TempleRasterY(drawing_sprite.sprite_top)||
      y>=TempleRasterY(drawing_sprite.sprite_bottom)||x<0||x>=TEMPLE_RENDER_WIDTH||
      y<0||y>=TEMPLE_RENDER_HEIGHT) {
    return;
  }
  logical_y=y/TEMPLE_RASTER_Y_SCALE;
  offset=(TempleRasterRead16(drawing_sprite.sprite_lineofs+logical_y*sizeof(legacy_u16))+
              x/TEMPLE_RASTER_X_SCALE)&
      TEMPLE_WORD_MASK;
  slot=y%TEMPLE_RASTER_Y_SCALE*TEMPLE_RASTER_X_SCALE+x%TEMPLE_RASTER_X_SCALE;
  temple_raster_target->samples[offset*TEMPLE_RASTER_CELL_PIXELS+slot]=
      TempleRasterSampleAt(color,TempleRasterDefaultPairs(),temple_raster_target->phases[offset]);
  temple_raster_target->dirty=TRUE;
  /* Logical bytes remain available for masks and original UI code. */
  temple_raster_target->bitmap[offset]=color;
}

legacy_void TempleRasterPaint(legacy_s64 x,legacy_s64 y,legacy_u16 color,legacy_u16 alternate,
    legacy_u16 pattern,legacy_s64 mode)
{
  legacy_s64 row;
  legacy_s64 bit;
  if(mode!=TEMPLE_RASTER_PAINT_SOLID) {
    /* DOS pattern words store the even scanline in their high byte. */
    row=TEMPLE_RASTER_PATTERN_ROWS-1-y%TEMPLE_RASTER_PATTERN_ROWS;
    bit=(TEMPLE_BYTE_BITS-1-(x&TEMPLE_RASTER_PATTERN_MASK))+row*TEMPLE_BYTE_BITS;
    if(pattern&(1<<bit)) {
      if(mode==TEMPLE_RASTER_PAINT_TWO_COLOR) {
        color=alternate;
      }
    } else if(mode==TEMPLE_RASTER_PAINT_MASKED) {
      return;
    }
  }
  TempleRasterPlot(x,y,color);
}

legacy_void TempleRasterPolygon(legacy_u16 color,legacy_u16 alternate,legacy_u16 pattern,
    legacy_s64 mode,legacy_u16 count,CPoint2D *vertices)
{
  legacy_f64 intersections[TEMPLE_RASTER_POINT_LIMIT];
  legacy_f64 scan_y;
  legacy_f64 ax;
  legacy_f64 ay;
  legacy_f64 bx;
  legacy_f64 by;
  legacy_f64 value;
  legacy_s64 y;
  legacy_s64 x;
  legacy_s64 edge;
  legacy_s64 previous;
  legacy_s64 hits;
  legacy_s64 index;
  legacy_s64 left;
  legacy_s64 right;
  legacy_s64 top=TEMPLE_RENDER_HEIGHT;
  legacy_s64 bottom=0;
  legacy_s64 scale=TempleRasterCoordinateScale();
  if(count<3||count>TEMPLE_RASTER_POINT_LIMIT||!vertices) {
    return;
  }
  TempleRasterSelect(&drawing_sprite);
  if(color==TEMPLE_RASTER_GHOST_COLOR) {
    color=BLACK;
    mode=TEMPLE_RASTER_PAINT_MASKED;
    pattern=TEMPLE_RASTER_GHOST_PATTERN;
  }
  /* Match the fractional edges and pixel-center samples below. Flooring the
   * scaled bounds can leave an uncovered row between adjoining polygons. */
  for(index=0;index<count;index++) {
    y=ToI64(Ceil(ToF64(vertices[index].py)*scale-TEMPLE_RASTER_HALF));
    top=MinI64(top,y);
    bottom=MaxI64(bottom,y);
  }
  top=MaxI64(top,MaxI64(0,TempleRasterY(drawing_sprite.sprite_top)));
  bottom=MinI64(bottom,MinI64(TEMPLE_RENDER_HEIGHT,TempleRasterY(drawing_sprite.sprite_bottom)));
  for(y=top;y<bottom;y++) {
    scan_y=y+TEMPLE_RASTER_HALF;
    hits=0;
    previous=count-1;
    for(edge=0;edge<count;edge++) {
      ax=vertices[previous].px*scale;
      ay=ToF64(vertices[previous].py)*scale;
      bx=vertices[edge].px*scale;
      by=ToF64(vertices[edge].py)*scale;
      if((ay<=scan_y&&by>scan_y)||(by<=scan_y&&ay>scan_y)) {
        intersections[hits++]=ax+(scan_y-ay)*(bx-ax)/(by-ay);
      }
      previous=edge;
    }
    for(edge=1;edge<hits;edge++) {
      value=intersections[edge];
      index=edge;
      while(index>0&&intersections[index-1]>value) {
        intersections[index]=intersections[index-1];
        index--;
      }
      intersections[index]=value;
    }
    for(edge=0;edge+1<hits;edge+=2) {
      left=MaxI64(0,ToI64(Ceil(intersections[edge]-TEMPLE_RASTER_HALF)));
      right=MinI64(TEMPLE_RENDER_WIDTH,ToI64(Ceil(intersections[edge+1]-TEMPLE_RASTER_HALF)));
      for(x=left;x<right;x++) {
        TempleRasterPaint(x,y,color,alternate,pattern,mode);
      }
    }
  }
}

legacy_void RsPreRenderDefault(legacy_u16 color,legacy_u16 count,CPoint2D *vertices)
{
  TempleRasterPolygon(color,color,0,TEMPLE_RASTER_PAINT_SOLID,count,vertices);
}

legacy_void RsPreRenderDefaultAlt(legacy_u16 color,legacy_u16 count,CPoint2D *vertices)
{
  RsPreRenderDefault(color,count,vertices);
}

legacy_void RsPreRenderPatterned(
    legacy_u16 pattern,legacy_u16 color,legacy_u16 count,CPoint2D *vertices)
{
  TempleRasterPolygon(color,color,pattern,TEMPLE_RASTER_PAINT_MASKED,count,vertices);
}

legacy_void RsPreRenderTwoColor(legacy_u16 pattern,legacy_u16 color,legacy_u16 alternate,
    legacy_u16 count,CPoint2D *vertices)
{
  TempleRasterPolygon(color,alternate,pattern,TEMPLE_RASTER_PAINT_TWO_COLOR,count,vertices);
}

legacy_void RsPreRenderLine(
    legacy_u16 start_x,legacy_u16 start_y,legacy_u16 end_x,legacy_u16 end_y,legacy_u16 color)
{
  legacy_s64 scale=TempleRasterCoordinateScale();
  legacy_s64 x=TempleRasterSigned16(start_x);
  legacy_s64 y=TempleRasterSigned16(start_y);
  legacy_s64 right=TempleRasterSigned16(end_x);
  legacy_s64 bottom=TempleRasterSigned16(end_y);
  legacy_s64 dx=AbsI64(right-x);
  legacy_s64 dy=-AbsI64(bottom-y);
  legacy_s64 sx=-1;
  legacy_s64 sy=-1;
  legacy_s64 error=dx+dy;
  legacy_s64 doubled;
  legacy_s64 column;
  legacy_s64 row;
  if(x<right) {
    sx=1;
  }
  if(y<bottom) {
    sy=1;
  }
  TempleRasterSelect(&drawing_sprite);
  while(TRUE) {
    /* UI lines retain each complete DOS pixel, including their endpoints.
     * Native geometry uses scale one and keeps its finer line detail. */
    for(row=0;row<scale;row++) {
      for(column=0;column<scale;column++) {
        if(color==TEMPLE_RASTER_GHOST_COLOR) {
          TempleRasterPaint(x*scale+column,y*scale+row,BLACK,BLACK,
              TEMPLE_RASTER_GHOST_PATTERN,TEMPLE_RASTER_PAINT_MASKED);
        } else {
          TempleRasterPlot(x*scale+column,y*scale+row,color);
        }
      }
    }
    if(x==right&&y==bottom) {
      break;
    }
    doubled=error*2;
    if(doubled>=dy) {
      error+=dy;
      x+=sx;
    }
    if(doubled<=dx) {
      error+=dx;
      y+=sy;
    }
  }
}

legacy_void RsPreRenderSphere(legacy_s16 x,legacy_s16 y,legacy_u16 size,legacy_u16 color)
{
  legacy_s64 scale=TempleRasterCoordinateScale();
  legacy_f64 radius_x=ToF64(size)*scale/2;
  legacy_f64 radius_y=ToF64(size)*TEMPLE_RASTER_SPHERE_HEIGHT_NUMERATOR/
      TEMPLE_RASTER_SPHERE_HEIGHT_DENOMINATOR*scale/2;
  legacy_f64 center_x=x*scale;
  legacy_f64 center_y=ToF64(y)*scale;
  legacy_f64 relative;
  legacy_f64 span;
  legacy_s64 row;
  legacy_s64 column;
  legacy_s64 left;
  legacy_s64 right;
  legacy_s64 top;
  legacy_s64 bottom;
  TempleRasterSelect(&drawing_sprite);
  if(size==0) {
    return;
  }
  top=MaxI64(0,ToI64(Ceil(center_y-radius_y-TEMPLE_RASTER_HALF)));
  bottom=MinI64(TEMPLE_RENDER_HEIGHT,ToI64(Ceil(center_y+radius_y-TEMPLE_RASTER_HALF)));
  for(row=top;row<bottom;row++) {
    relative=(row+TEMPLE_RASTER_HALF-center_y)/radius_y;
    if(Abs(relative)<=1) {
      span=radius_x*Sqrt(1-relative*relative);
      left=MaxI64(0,ToI64(Ceil(center_x-span-TEMPLE_RASTER_HALF)));
      right=MinI64(TEMPLE_RENDER_WIDTH,ToI64(Ceil(center_x+span-TEMPLE_RASTER_HALF)));
      for(column=left;column<right;column++) {
        if(color==TEMPLE_RASTER_GHOST_COLOR) {
          TempleRasterPaint(
              column,row,BLACK,BLACK,TEMPLE_RASTER_GHOST_PATTERN,TEMPLE_RASTER_PAINT_MASKED);
        } else {
          TempleRasterPlot(column,row,color);
        }
      }
    }
  }
}

legacy_u8 TempleRasterScreenDirty()
{
  CTempleRasterSurface *surface=TempleRasterFind(
      RsDosMemoryMakePointer(RsDosMemoryPointerSegment(screen_sprite.sprite_bitmapptr),0));
  if(surface) {
    return surface->dirty;
  }
  return FALSE;
}

legacy_void TempleRasterScreenPresented()
{
  CTempleRasterSurface *surface=TempleRasterFind(
      RsDosMemoryMakePointer(RsDosMemoryPointerSegment(screen_sprite.sprite_bitmapptr),0));
  if(surface) {
    surface->dirty=FALSE;
  }
}

legacy_void TempleRasterPresent(CDC *dc)
{
  CTempleRasterSurface *surface=TempleRasterFind(
      RsDosMemoryMakePointer(RsDosMemoryPointerSegment(screen_sprite.sprite_bitmapptr),0));
  legacy_s64 x;
  legacy_s64 y;
  legacy_s64 render_x;
  legacy_s64 render_y;
  legacy_s64 logical_y;
  legacy_s64 slot;
  legacy_u16 noreg offset;
  legacy_u32 noreg sample;
  legacy_u8 noreg color;
  if(!surface||!dc) {
    return;
  }
  /* DOS's 320x200 pixels were displayed at 4:3. Correct the entire composed
   * native raster to the physical screen, including artwork and 3D geometry.
   */
  for(y=0;y<TEMPLE_SCREEN_HEIGHT;y++) {
    render_y=y*TEMPLE_RENDER_HEIGHT/TEMPLE_SCREEN_HEIGHT;
    logical_y=render_y/TEMPLE_RASTER_Y_SCALE;
    slot=render_y%TEMPLE_RASTER_Y_SCALE*TEMPLE_RASTER_X_SCALE;
    for(x=0;x<TEMPLE_SCREEN_WIDTH;x++) {
      render_x=x*TEMPLE_RENDER_WIDTH/TEMPLE_SCREEN_WIDTH;
      offset=(surface->rows[logical_y]+render_x/TEMPLE_RASTER_X_SCALE)&TEMPLE_WORD_MASK;
      sample=surface->samples[offset*TEMPLE_RASTER_CELL_PIXELS+slot+
          render_x%TEMPLE_RASTER_X_SCALE];
      if((sample&TEMPLE_RASTER_PAIR_FLAG)&&!(sample&TEMPLE_RASTER_XOR_MASK)) {
        color=sample>>TEMPLE_RASTER_PAIR_SHIFT;
        if(!(sample&TEMPLE_RASTER_PAIR_LOW_FLAG)) {
          color>>=TEMPLE_PALETTE_NIBBLE_BITS;
        }
        color&=TEMPLE_PALETTE_NIBBLE_MASK;
      } else {
        color=TemplePaletteColor(sample&TEMPLE_BYTE_MASK);
      }
      dc->body[y*dc->width_internal+x]=color;
    }
  }
}

legacy_void TempleRasterShutdown()
{
  CTempleRasterSurface *surface;
  CTempleRasterResourceMap *resource;
  while(surface=temple_raster_surfaces) {
    temple_raster_surfaces=surface->next;
    Free(surface->samples);
    Free(surface);
  }
  while(resource=temple_raster_resources) {
    temple_raster_resources=resource->next;
    Free(resource);
  }
  temple_raster_target=NULL;
  temple_raster_source_pairs=NULL;
  temple_raster_source_set=FALSE;
  temple_raster_source_phase_delta=0;
  temple_raster_have_default=FALSE;
  temple_raster_native=FALSE;
}
