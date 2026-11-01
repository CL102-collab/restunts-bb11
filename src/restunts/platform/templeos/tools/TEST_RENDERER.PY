#!/usr/bin/env python3
"""Run host checks against the HolyC palette and native raster algorithms.

This performs only the syntax adaptations needed by the host C compiler; it is
not a HolyC compiler. Run TEST_RENDERER.HC in TempleOS for the guest semantics.
The host run adds address/undefined-behavior sanitizers for bounds and masks.
"""

from pathlib import Path
import json
import os
import re
import runpy
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
style = runpy.run_path(str(root / "tools" / "STYLE.PY"))
names = json.loads((root / "generated" / "NAMES.JSON").read_text())
# These identifiers belong to the host C runtime, not the translated game.
host_runtime = {
    "assert", "calloc", "ceil", "fabs", "free", "llabs", "main", "memcmp",
    "memcpy", "memset", "puts", "sqrt",
}
fixture_names = {name: renamed for name, renamed in names.items() if name not in host_runtime}
s='\n'.join((root/f).read_text() for f in ['PALETTE.HH','PALETTE.HC','RASTER.HH','RASTER.HC','MATERIAL.HC'])
s=re.sub(r'^#include.*$','',s,flags=re.M)
s=re.sub(r'\bclass\s+(\w+)\s*\{',r'typedef struct \1 \1; struct \1 {',s)
s=re.sub(r'\boutput\s*\[\s*index\s*\]\s*=\s*0\s*;',
         'memset(&output[index],0,sizeof(output[index]));',s)
s=re.sub(r'\(\s*\*\s*source\s*\+\+\s*\)\s*\(\s*legacy_s8\s*\)',
         '((legacy_s8)*source++)',s)
for name in ['start_x','start_y','end_x','end_y']:
 s=re.sub(r'\b'+name+r'\s*\(\s*legacy_s16\s*\)','((legacy_s16)'+name+')',s)
prefix=r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "RESTUNTS_LEGACY_INCLUDE"
#define legacy_void void
RESTUNTS_TEMPLE_CONSTANTS
RESTUNTS_MATERIAL_COUNT
legacy_s16 material_color_list[SHAPE3D_MATERIAL_COUNT];
legacy_s16 material_pattern_list[SHAPE3D_MATERIAL_COUNT];
legacy_s16 material_pattern2_list[SHAPE3D_MATERIAL_COUNT];
legacy_s16 *material_clrlist_ptr, *material_clrlist2_ptr;
legacy_s16 *material_patlist_ptr, *material_patlist2_ptr;
#define FALSE 0
#define TRUE 1
#define BLACK 0
#define SHAPE2D_RASTER_AND 0
#define SHAPE2D_RASTER_OR 1
#define SHAPE2D_RASTER_COPY 2
#define SHAPE2D_RASTER_MAP 3
#define SHAPE2D_TRANSPARENT_COLOR 255
#define MemCpy memcpy
#define MemSet memset
#define Free free
#define CAlloc(n) calloc(1,n)
#define MaxI64(a,b) (((a)>(b))?(a):(b))
#define MinI64(a,b) (((a)<(b))?(a):(b))
#define AbsI64 llabs
#define Abs fabs
#define Ceil ceil
#define Sqrt sqrt
#define ToF64(x) ((legacy_f64)(x))
#define ToI64(x) ((legacy_s64)(x))
typedef struct { legacy_u16 b,g,r; } CBGR48;
CBGR48 hardware[16];
legacy_void GrPaletteGet(CBGR48 *out) { memcpy(out,hardware,sizeof(hardware)); }
legacy_void GrPaletteSet(CBGR48 *in) { memcpy(hardware,in,sizeof(hardware)); }
typedef struct { legacy_s16 px,py; } POINT2D;
typedef struct { legacy_u8 *sprite_bitmapptr,*sprite_lineofs; legacy_u16 sprite_buffer_width,sprite_top,sprite_bottom,sprite_raster_left,sprite_raster_right;} SPRITE;
SPRITE drawing_sprite,screen_sprite;
typedef struct { legacy_u8 *body; legacy_s64 width_internal; } CDC;
legacy_u8 arena[0x100000];
legacy_void *dos_memory_make_pointer(legacy_u16 segment, legacy_u16 offset) {return arena+segment*16+offset;}
legacy_u16 dos_memory_pointer_segment(legacy_void *p) {return ((legacy_u8*)p-arena)/16;}
legacy_s16 mmgr_get_chunk_size(legacy_void *p) {(void)p;return 4096;}
'''
presentation_tests = "#define TestAssert assert\n" + (root / "TEST_PRESENTATION.HC").read_text()
bitmap_tests = (root / "TEST_BITMAP.HC").read_text()
polygon_tests = (root / "TEST_POLYGON.HC").read_text()
line_tests = (root / "TEST_LINE.HC").read_text()
material_tests = "#define TestMaterialAssert assert\n" + (root / "TEST_MATERIAL.HC").read_text()
tests=r'''
legacy_int main(legacy_void) {
 TemplePaletteInit();
 TestMaterialInitialization();
 for(legacy_int i=0;i<16;i++)assert(TemplePaletteNearest(temple_palette_standard[i*3],temple_palette_standard[i*3+1],temple_palette_standard[i*3+2])==i);
 legacy_u8 rgb[3]={63,0,0}; TemplePaletteSet(255,65535,rgb); assert(TemplePaletteColor(255)==4);
 rgb[0]=rgb[1]=rgb[2]=0;TemplePaletteSet(255,1,rgb);assert(TemplePaletteColor(255)==0);
 legacy_u8 rows[400]; for(legacy_int y=0;y<200;y++){rows[2*y]=(y*320)&255;rows[2*y+1]=(y*320)>>8;}
 drawing_sprite=(SPRITE){arena+0x10000,rows,320,0,200,0,320};screen_sprite=drawing_sprite;
 TempleRasterSelect(&drawing_sprite);
 POINT2D triangle[3]={{0,0},{7,0},{0,7}};preRender_default(12,3,triangle);
 legacy_u32 *cell=temple_raster_target->samples+5*TEMPLE_RASTER_CELL_PIXELS;
 legacy_u32 saved[TEMPLE_RASTER_CELL_PIXELS];memcpy(saved,cell,sizeof(saved));
 legacy_int saw_mixed=0;for(legacy_int i=0;i<100;i++){legacy_u32 *p=temple_raster_target->samples+i*TEMPLE_RASTER_CELL_PIXELS;for(legacy_int j=1;j<TEMPLE_RASTER_CELL_PIXELS;j++)if(p[0]!=p[j])saw_mixed=1;}assert(saw_mixed);
 TempleRasterXorPixel(drawing_sprite.sprite_bitmapptr,5,15);TempleRasterXorPixel(drawing_sprite.sprite_bitmapptr,5,15);assert(!memcmp(saved,cell,sizeof(saved)));
 legacy_u8 neutral=255;video_pages_raster_span(drawing_sprite.sprite_bitmapptr,5,&neutral,0,1,SHAPE2D_RASTER_AND,NULL);assert(!memcmp(saved,cell,sizeof(saved)));
 neutral=0;video_pages_raster_span(drawing_sprite.sprite_bitmapptr,5,&neutral,0,1,SHAPE2D_RASTER_OR,NULL);assert(!memcmp(saved,cell,sizeof(saved)));
 legacy_u8 map[256];memset(map,255,sizeof(map));neutral=3;video_pages_raster_span(drawing_sprite.sprite_bitmapptr,5,&neutral,0,1,SHAPE2D_RASTER_MAP,map);assert(!memcmp(saved,cell,sizeof(saved)));
 SPRITE background={arena+0x20000,rows,320,0,200,0,320};TempleRasterRegisterSprite(&background);
 video_pages_copy_span(background.sprite_bitmapptr,9,drawing_sprite.sprite_bitmapptr,5,1);assert(!memcmp(saved,TempleRasterFind(background.sprite_bitmapptr)->samples+9*TEMPLE_RASTER_CELL_PIXELS,sizeof(saved)));
 video_pages_write_pixel(drawing_sprite.sprite_bitmapptr,5,2);video_pages_copy_span(drawing_sprite.sprite_bitmapptr,5,background.sprite_bitmapptr,9,1);assert(!memcmp(saved,cell,sizeof(saved)));
 CDC dc={calloc(TEMPLE_SCREEN_PIXELS,1),TEMPLE_SCREEN_WIDTH};TempleRasterPresent(&dc);for(legacy_int i=0;i<TEMPLE_SCREEN_PIXELS;i++)assert(dc.body[i]<16);TestPolygonSharedEdges(&dc);TestNativeGeometry(&dc);TestLineCoverage(&dc);TestMaterialRaster(&dc);TestPresentation(&dc);TestBitmapPalettes(&dc,&background);free(dc.body);
 for(legacy_int y=0;y<TEMPLE_RENDER_HEIGHT;y++){legacy_int ly=y/TEMPLE_RASTER_Y_SCALE;assert(TempleRasterY(ly)<=y&&TempleRasterY(ly+1)>y);}
 legacy_u8 *res=arena+0x30000;res[4]=1;memcpy(res+6,"!eg0",4);res[14]=0;res[15]=1;res[16]=1;for(legacy_int i=0;i<256;i++)res[30+i]=i;
 TempleRasterResource(res);assert(TempleRasterPairs(res+30)[127]==127);
 TempleRasterForget(res);assert(!TempleRasterPairs(res+30));
 res[30]=128;for(legacy_int i=0;i<128;i++)res[31+i]=i;res[159]=128;for(legacy_int i=0;i<128;i++)res[160+i]=128+i;
 TempleRasterResourceParsed(res);assert(TempleRasterPairs(res+30)[255]==255);
 MemCpy(res+6,"test",4);TempleRasterResource(res);assert(!TempleRasterPairs(res+30));
 temple_raster_have_default=TRUE;memset(temple_raster_default_pairs,0xcc,sizeof(temple_raster_default_pairs));
 res[30]=255;video_pages_raster_span(drawing_sprite.sprite_bitmapptr,10,res,30,1,SHAPE2D_RASTER_COPY,NULL);
 assert(!(temple_raster_target->samples[10*TEMPLE_RASTER_CELL_PIXELS]&TEMPLE_RASTER_PAIR_FLAG));
 TempleRasterSource(res+30);video_pages_write_pixel(drawing_sprite.sprite_bitmapptr,11,255);TempleRasterSource(NULL);
 assert(!(temple_raster_target->samples[11*TEMPLE_RASTER_CELL_PIXELS]&TEMPLE_RASTER_PAIR_FLAG));
 TempleRasterForgetRange(arena+0x1f000,16);assert(!TempleRasterFind(drawing_sprite.sprite_bitmapptr));assert(TempleRasterFind(background.sprite_bitmapptr));
 TempleRasterForgetRange(res+31,1);assert(!TempleRasterResourceAt(res+30));
 TempleRasterShutdown();TemplePaletteShutdown();puts("palette/raster host regression checks passed");return 0;
}
'''
prefix = prefix.replace("RESTUNTS_TEMPLE_CONSTANTS", "\n".join(
    line for line in (root / "LEGACY.HH").read_text().splitlines()
    if line.startswith("#define TEMPLE_")
))
prefix = prefix.replace("RESTUNTS_MATERIAL_COUNT", next(
    line for line in (root.parents[1] / "c" / "shape3d" / "shape3d_internal.h").read_text().splitlines()
    if line.startswith("#define SHAPE3D_MATERIAL_COUNT ")
))
prefix = prefix.replace("RESTUNTS_LEGACY_INCLUDE", str(root.parents[1] / "c" / "legacy.h"))
# The source fixtures keep their C-side names so the generated manifest remains
# the single mapping between shared C and native HolyC symbols.
prefix = style["rename_identifiers"](prefix, fixture_names)
tests = style["rename_identifiers"](tests, fixture_names)


def host_storage_keywords(text):
    ignoring = False

    def replace(token):
        nonlocal ignoring
        # C scalar locals already keep their declared width without noreg.
        if token.lastgroup == "word" and token[0] == "noreg":
            return ""
        if token.lastgroup == "word" and token[0] == "no_warn":
            ignoring = True
            return "(legacy_void)"
        if ignoring and token[0] == ",":
            return ",(legacy_void)"
        if token[0] == ";":
            ignoring = False
        return token[0]

    return style["TOKEN"].sub(replace, text)


host_source = host_storage_keywords(prefix + s + presentation_tests + bitmap_tests + polygon_tests + line_tests + material_tests + tests)
with tempfile.TemporaryDirectory(prefix="restunts-raster-") as directory:
    source = Path(directory) / "TEST.C"
    program = Path(directory) / "TEST"
    source.write_text(host_source, encoding="ascii")
    subprocess.run([
        os.environ.get("CC", "cc"), "-x", "c", "-std=c99",
        "-fsanitize=address,undefined", "-g", "-O1", str(source), "-lm", "-o", str(program),
    ], check=True)
    subprocess.run([str(program)], check=True)
