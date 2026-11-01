// Exercises generated sprite code against the native renderer. No game assets needed.
#include "LOAD.HC"

#define TEMPLE_GRAPHICS_HEADER_BYTES 16
#define TEMPLE_GRAPHICS_POINT_BYTES 4
#define TEMPLE_GRAPHICS_VECTOR_BYTES 6
#define TEMPLE_GRAPHICS_SHAPE_HEADER_BYTES 4
#define TEMPLE_GRAPHICS_SHAPE_VERTEX_COUNT 3
#define TEMPLE_GRAPHICS_SHAPE_MASK_BYTES 4
#define TEMPLE_GRAPHICS_SHAPE_BYTES 40
#define TEMPLE_GRAPHICS_WINDOW_SIZE 16
#define TEMPLE_GRAPHICS_CAPTURE_X 4
#define TEMPLE_GRAPHICS_CAPTURE_Y 3
#define TEMPLE_GRAPHICS_CAPTURE_SIDE 8
#define TEMPLE_GRAPHICS_TEST_COLOR 12
#define TEMPLE_GRAPHICS_CLEAR_COLOR 2
#define TEMPLE_GRAPHICS_POLYGON_HEADER_BYTES 6
#define TEMPLE_GRAPHICS_CLOUD_VERTICES 8
#define TEMPLE_GRAPHICS_TRIANGLE_VERTICES 3
#define TEMPLE_GRAPHICS_CLIPPED_VERTICES 4
#define TEMPLE_GRAPHICS_PRIMITIVE_BYTES 8
#define TEMPLE_GRAPHICS_PRIMITIVE_HEADER_BYTES 3
#define TEMPLE_GRAPHICS_NEAR_DEPTH 12
#define TEMPLE_GRAPHICS_COORDINATE_LIMIT (I16_MAX/TEMPLE_RENDER_SCALE)
#define TEMPLE_GRAPHICS_PRETRANSFORMED_FLAG 2
#define TEMPLE_GRAPHICS_BOUNDS_FLAG 8
#define TEMPLE_GRAPHICS_ALWAYS_VISIBLE_FLAG 1
#define TEMPLE_GRAPHICS_FOCAL_LENGTH 256
#define TEMPLE_GRAPHICS_DEPTH 100
#define TEMPLE_GRAPHICS_WORLD_SIDE 20
#define TEMPLE_GRAPHICS_PROJECTED_LEFT 325
#define TEMPLE_GRAPHICS_PROJECTED_RIGHT 427
#define TEMPLE_GRAPHICS_PROJECTED_TOP 103
#define TEMPLE_GRAPHICS_PROJECTED_BOTTOM 205
#define TEMPLE_GRAPHICS_PROJECTED_CLIP_TOP 168
#define TEMPLE_GRAPHICS_SAMPLE_Y 150
#define TEMPLE_GRAPHICS_LARGE_WHEEL_RADIUS 46338
#define TEMPLE_GRAPHICS_WHEEL_PADDING 1
#define TEMPLE_GRAPHICS_LARGE_BOUND ((TEMPLE_GRAPHICS_COORDINATE_LIMIT+1)/TEMPLE_RENDER_SCALE)
#define TEMPLE_GRAPHICS_SPHERE_X 20000
#define TEMPLE_GRAPHICS_SPHERE_Z_OFFSET 30000
#define TEMPLE_GRAPHICS_SPHERE_VERTICES 2
#define TEMPLE_GRAPHICS_SPHERE_RADIUS_WORD 5

legacy_void TempleGraphicsAssert(legacy_s64 condition,legacy_u8 *message)
{
  if(!condition) {
    PrintErr("Graphics integration: %s\n",message);
    throw('Graphics');
  }
}

legacy_void TestMaterialAssert(legacy_s64 condition)
{
  TempleGraphicsAssert(condition,"original EGA material tables and raster colors");
}

#include "TEST_MATERIAL.HC"

legacy_void TempleGraphicsQueueTest()
{
  legacy_u16 noreg expected;
  RsInitPolyinfo();
  RsPolyinfoReset();
  shape_polygon_predecessor=polygon_list_tail;
  polygon_insertion_cursor=polygon_list_tail;
  shape_polygon_count=0;
  transshapenumvertscopy=TEMPLE_GRAPHICS_CLOUD_VERTICES;
  TempleGraphicsAssert(!RsPolygonInsertNewest(0,0),"first primitive fits queue");
  expected=TEMPLE_GRAPHICS_POLYGON_HEADER_BYTES+
      TEMPLE_GRAPHICS_CLOUD_VERTICES*TEMPLE_GRAPHICS_POINT_BYTES;
  TempleGraphicsAssert(polyinfoptrnext==expected&&polyinfonumpolys==1,
      "byte vertex count produces exact packed record size");
  transshapenumvertscopy=TEMPLE_GRAPHICS_TRIANGLE_VERTICES;
  TempleGraphicsAssert(!RsPolygonInsertNewest(0,0),"next primitive fits queue");
  expected+=TEMPLE_GRAPHICS_POLYGON_HEADER_BYTES+
      TEMPLE_GRAPHICS_TRIANGLE_VERTICES*TEMPLE_GRAPHICS_POINT_BYTES;
  TempleGraphicsAssert(polyinfoptrnext==expected&&polyinfonumpolys==2,
      "queue appends subsequent primitives without false overflow");
  RsPolyinfoReset();
}

legacy_void TempleGraphicsPoint(legacy_u16 index,legacy_s16 x,legacy_s16 y)
{
  CPoint2D actual;
  RsPolyinfoReadPoint(polyinfoptr,index,&actual);
  TempleGraphicsAssert(
      actual.px==x&&actual.py==y,"queued geometry retains native projection precision");
}

legacy_u8 TempleGraphicsSample(legacy_s64 x,legacy_s64 y)
{
  legacy_s64 offset=temple_raster_target->rows[y/TEMPLE_RENDER_SCALE]+x/TEMPLE_RENDER_SCALE;
  legacy_s64 slot=y%TEMPLE_RENDER_SCALE*TEMPLE_RENDER_SCALE+x%TEMPLE_RENDER_SCALE;
  return temple_raster_target->samples[offset*TEMPLE_RASTER_CELL_PIXELS+slot]&
      TEMPLE_BYTE_MASK;
}

legacy_void TempleGraphicsProjectionTest()
{
  CShape3D shape;
  CTransformedShape3D instance;
  CRectangle bounds;
  CRectangle *saved_bounds=transshaperectptr;
  CVector vertex;
  CPoint2D logical,native;
  legacy_u8 vertices[TEMPLE_GRAPHICS_TRIANGLE_VERTICES*TEMPLE_GRAPHICS_VECTOR_BYTES];
  legacy_u8 primitives[TEMPLE_GRAPHICS_PRIMITIVE_BYTES];
  legacy_u8 mask[TEMPLE_GRAPHICS_SHAPE_MASK_BYTES];
  legacy_s64 material,x,y;
  legacy_s16 colors[1],patterns[1];
  legacy_s16 *saved_colors=material_clrlist_ptr_cpy;
  legacy_s16 *saved_patterns=material_patlist_ptr_cpy;
  legacy_s16 *saved_secondary=material_clrlist2_ptr_cpy;
  legacy_s16 *saved_bits=material_patlist2_ptr_cpy;
  legacy_u16 noreg saved_reserved=reserved_material_video_word;
  MemSet(&shape,0,sizeof(shape));
  MemSet(&instance,0,sizeof(instance));
  MemSet(primitives,0,sizeof(primitives));
  MemSet(mask,TEMPLE_BYTE_MASK,sizeof(mask));
  shape.shape3d_numverts=TEMPLE_GRAPHICS_TRIANGLE_VERTICES;
  shape.shape3d_numprimitives=1;
  shape.shape3d_numpaints=1;
  shape.shape3d_vertex_bytes=vertices;
  shape.shape3d_primitives=primitives;
  shape.shape3d_visibility_masks=mask;
  shape.shape3d_front_facing_masks=mask;
  primitives[0]=SHAPE3D_PRIMITIVE_POLYGON_FIRST;
  primitives[1]=TEMPLE_GRAPHICS_ALWAYS_VISIBLE_FLAG;
  primitives[TEMPLE_GRAPHICS_PRIMITIVE_HEADER_BYTES+1]=1;
  primitives[TEMPLE_GRAPHICS_PRIMITIVE_HEADER_BYTES+2]=2;
  instance.shapeptr=&shape;
  instance.rectptr=&bounds;
  instance.ts_flags=TEMPLE_GRAPHICS_PRETRANSFORMED_FLAG|TEMPLE_GRAPHICS_BOUNDS_FLAG;
  instance.culling_distance=TEMPLE_GRAPHICS_DEPTH;
  projection_focal_length_x=TEMPLE_GRAPHICS_FOCAL_LENGTH;
  projection_focal_length_y=TEMPLE_GRAPHICS_FOCAL_LENGTH;
  projection_center_x=TEMPLE_LOGICAL_WIDTH/TEMPLE_RENDER_SCALE;
  projection_center_y=TEMPLE_LOGICAL_HEIGHT/TEMPLE_RENDER_SCALE;
  select_rect_rc.left=0;
  select_rect_rc.right=TEMPLE_LOGICAL_WIDTH-1;
  select_rect_rc.top=0;
  select_rect_rc.bottom=TEMPLE_LOGICAL_HEIGHT-1;
  MemCpy(&mat_temp,RsMatRotZxy(0,0,0,MATRIX_ROTATION_ORDER_ZXY),sizeof(CMatrix));
  bounds.left=I16_MAX;
  bounds.top=I16_MAX;
  bounds.right=I16_MIN;
  bounds.bottom=I16_MIN;
  vertex.x=1;
  vertex.y=-1;
  vertex.z=TEMPLE_GRAPHICS_DEPTH;
  RsShape3dVertexWrite(&shape,0,&vertex);
  RsVectorToPoint(&vertex,&logical);
  vertex.x+=TEMPLE_GRAPHICS_WORLD_SIDE;
  RsShape3dVertexWrite(&shape,1,&vertex);
  vertex.x=1;
  vertex.y+=TEMPLE_GRAPHICS_WORLD_SIDE;
  RsShape3dVertexWrite(&shape,2,&vertex);
  RsPolyinfoReset();
  TempleGraphicsAssert(!RsShape3dTransformAndQueue(&instance)&&polyinfonumpolys==1,
      "fractional triangle survives logical viewport clipping");
  TempleGraphicsPoint(0,TEMPLE_GRAPHICS_PROJECTED_LEFT,TEMPLE_GRAPHICS_PROJECTED_BOTTOM);
  TempleGraphicsPoint(1,TEMPLE_GRAPHICS_PROJECTED_RIGHT,TEMPLE_GRAPHICS_PROJECTED_BOTTOM);
  TempleGraphicsPoint(2,TEMPLE_GRAPHICS_PROJECTED_LEFT,TEMPLE_GRAPHICS_PROJECTED_TOP);
  TempleGraphicsAssert(TEMPLE_GRAPHICS_PROJECTED_LEFT!=logical.px*TEMPLE_RENDER_SCALE&&
          TEMPLE_GRAPHICS_PROJECTED_BOTTOM!=logical.py*TEMPLE_RENDER_SCALE,
      "native projection adds detail beyond doubled DOS coordinates");
  TempleGraphicsAssert(bounds.left==TEMPLE_GRAPHICS_PROJECTED_LEFT/TEMPLE_RENDER_SCALE&&
          bounds.right==TEMPLE_GRAPHICS_PROJECTED_RIGHT/TEMPLE_RENDER_SCALE+1&&
          bounds.top==TEMPLE_GRAPHICS_PROJECTED_TOP/TEMPLE_RENDER_SCALE&&
          bounds.bottom==TEMPLE_GRAPHICS_PROJECTED_BOTTOM/TEMPLE_RENDER_SCALE+1,
      "native triangle keeps logical background capture bounds");
  colors[0]=TEMPLE_GRAPHICS_TEST_COLOR;
  patterns[0]=SHAPE3D_PATTERN_SOLID;
  material_clrlist_ptr_cpy=colors;
  material_patlist_ptr_cpy=patterns;
  RsSpriteClearTarget(TEMPLE_GRAPHICS_CLEAR_COLOR);
  RsShape3dRenderQueuedPrimitives();
  TempleGraphicsAssert(TempleGraphicsSample(TEMPLE_GRAPHICS_PROJECTED_LEFT,
                          TEMPLE_GRAPHICS_SAMPLE_Y)==TEMPLE_GRAPHICS_TEST_COLOR&&
          TempleGraphicsSample(TEMPLE_GRAPHICS_PROJECTED_LEFT-1,TEMPLE_GRAPHICS_SAMPLE_Y)==
              TEMPLE_GRAPHICS_CLEAR_COLOR,
      "queued geometry paints an odd native edge without expanding it into a DOS cell");
  TempleGraphicsAssert(!temple_raster_native,"queue drawing restores logical UI coordinates");
  material_clrlist_ptr_cpy=saved_colors;
  material_patlist_ptr_cpy=saved_patterns;

  /* Send the real EGA materials through the generated transform and queue. */
  RsCopyMaterialListPointers(material_clrlist_ptr,material_clrlist2_ptr,material_patlist_ptr,
      material_patlist2_ptr,saved_reserved);
  for(material=TEST_MATERIAL_BLUE_FIRST;material<=TEST_MATERIAL_BLUE_LAST;material++) {
    primitives[TEMPLE_GRAPHICS_PRIMITIVE_HEADER_BYTES-1]=material;
    TempleGraphicsAssert(!RsShape3dTransformAndQueue(&instance)&&polyinfonumpolys==1,
        "EGA blue material survives real geometry queue");
    RsSpriteClearTarget(TEMPLE_GRAPHICS_CLEAR_COLOR);
    RsShape3dRenderQueuedPrimitives();
    for(y=TEMPLE_GRAPHICS_SAMPLE_Y;y<TEMPLE_GRAPHICS_SAMPLE_Y+TEMPLE_RENDER_SCALE;y++) {
      for(x=TEMPLE_GRAPHICS_PROJECTED_LEFT;
          x<TEMPLE_GRAPHICS_PROJECTED_LEFT+TEMPLE_RENDER_SCALE;x++) {
        TempleGraphicsAssert(
            TempleGraphicsSample(x,y)==TestMaterialExpectedColor(material,x,y),
            "queued EGA blue paint retains DOS colors and checker phase");
      }
    }
    TempleGraphicsAssert(!temple_raster_native,"EGA queue restores logical UI coordinates");
  }
  primitives[TEMPLE_GRAPHICS_PRIMITIVE_HEADER_BYTES-1]=0;
  RsCopyMaterialListPointers(
      saved_colors,saved_secondary,saved_patterns,saved_bits,saved_reserved);

  /* Clipping against z=12 must retain the fractional 3D intersections. */
  vertex.x=0;
  vertex.y=1;
  vertex.z=0;
  RsShape3dVertexWrite(&shape,2,&vertex);
  bounds.left=I16_MAX;
  bounds.top=I16_MAX;
  bounds.right=I16_MIN;
  bounds.bottom=I16_MIN;
  TempleGraphicsAssert(!RsShape3dTransformAndQueue(&instance)&&polyinfonumpolys==1,
      "triangle crossing the near plane remains visible");
  TempleGraphicsAssert(polyinfoptrnext==
          TEMPLE_GRAPHICS_POLYGON_HEADER_BYTES+
              TEMPLE_GRAPHICS_CLIPPED_VERTICES*TEMPLE_GRAPHICS_POINT_BYTES,
      "near-plane clipping emits the four surviving native corners");
  TempleGraphicsPoint(0,TEMPLE_GRAPHICS_PROJECTED_LEFT,TEMPLE_GRAPHICS_PROJECTED_CLIP_TOP);
  TempleGraphicsPoint(1,TEMPLE_GRAPHICS_PROJECTED_LEFT,TEMPLE_GRAPHICS_PROJECTED_BOTTOM);
  TempleGraphicsPoint(2,TEMPLE_GRAPHICS_PROJECTED_RIGHT,TEMPLE_GRAPHICS_PROJECTED_BOTTOM);
  TempleGraphicsPoint(3,TEMPLE_GRAPHICS_PROJECTED_RIGHT,TEMPLE_GRAPHICS_PROJECTED_CLIP_TOP);
  TempleGraphicsAssert(bounds.top==TEMPLE_GRAPHICS_PROJECTED_CLIP_TOP/TEMPLE_RENDER_SCALE,
      "near-clipped bounds use the surviving native geometry");
  RsPolyinfoReset();

  vertex.x=I16_MAX;
  vertex.y=I16_MIN;
  vertex.z=TEMPLE_GRAPHICS_NEAR_DEPTH;
  RsShape3dProjectPoint(&vertex,&native);
  TempleGraphicsAssert(native.px==TEMPLE_GRAPHICS_COORDINATE_LIMIT&&
          native.py==TEMPLE_GRAPHICS_COORDINATE_LIMIT,
      "positive offscreen projections saturate without signed overflow");
  vertex.x=I16_MIN;
  vertex.y=I16_MAX;
  RsShape3dProjectPoint(&vertex,&native);
  TempleGraphicsAssert(native.px==-TEMPLE_GRAPHICS_COORDINATE_LIMIT&&
          native.py==-TEMPLE_GRAPHICS_COORDINATE_LIMIT,
      "negative offscreen projections retain their signs when saturated");
  native.px=TEMPLE_RENDER_WIDTH-1;
  native.py=TEMPLE_RENDER_HEIGHT-1;
  TempleGraphicsAssert(!RsShape3dGeometryClipFlags(&native),
      "final odd native column and row remain inside the logical viewport");
  native.px=-1;
  TempleGraphicsAssert(RsShape3dGeometryClipFlags(&native),
      "negative native coordinates floor outside the logical viewport");
  RsShape3dAdjustGeometryBounds(&native,&bounds);
  TempleGraphicsAssert(
      bounds.left==-1,"negative native coordinates expand logical capture bounds downward");

  bounds.left=I16_MAX;
  bounds.top=I16_MAX;
  bounds.right=I16_MIN;
  bounds.bottom=I16_MIN;
  native.px=TEMPLE_GRAPHICS_COORDINATE_LIMIT;
  native.py=TEMPLE_GRAPHICS_COORDINATE_LIMIT;
  transshaperectptr=&bounds;
  RsShape3dPrimitivesShape3dAdjustRoundBounds(
      &native,TEMPLE_GRAPHICS_LARGE_WHEEL_RADIUS,TEMPLE_GRAPHICS_WHEEL_PADDING);
  TempleGraphicsAssert(bounds.left==-TEMPLE_GRAPHICS_LARGE_BOUND&&
          bounds.top==-TEMPLE_GRAPHICS_LARGE_BOUND&&
          bounds.right==TEMPLE_GRAPHICS_LARGE_BOUND&&
          bounds.bottom==TEMPLE_GRAPHICS_LARGE_BOUND,
      "large offscreen wheel bounds remain conservative without signed wrap");

  shape.shape3d_numverts=TEMPLE_GRAPHICS_SPHERE_VERTICES;
  primitives[0]=SHAPE3D_PRIMITIVE_SPHERE;
  primitives[TEMPLE_GRAPHICS_PRIMITIVE_HEADER_BYTES+TEMPLE_GRAPHICS_SPHERE_VERTICES]=0;
  vertex.x=0;
  vertex.y=0;
  vertex.z=TEMPLE_GRAPHICS_DEPTH;
  RsShape3dVertexWrite(&shape,0,&vertex);
  vertex.x=TEMPLE_GRAPHICS_SPHERE_X;
  vertex.z=TEMPLE_GRAPHICS_SPHERE_Z_OFFSET;
  TempleGraphicsAssert(
      RsPolarRadius3D(&vertex)<0,"large sphere fixture crosses the signed radius boundary");
  vertex.z+=TEMPLE_GRAPHICS_DEPTH;
  RsShape3dVertexWrite(&shape,1,&vertex);
  TempleGraphicsAssert(!RsShape3dTransformAndQueue(&instance)&&polyinfonumpolys==1,
      "sphere with unsigned radius above the signed word limit remains visible");
  TempleGraphicsAssert(RsPolyinfoReadWord(polyinfoptr,TEMPLE_GRAPHICS_SPHERE_RADIUS_WORD)==
          TEMPLE_GRAPHICS_COORDINATE_LIMIT,
      "large sphere radius saturates positively before rasterization");
  RsPolyinfoReset();
  transshaperectptr=saved_bounds;
}

legacy_void TempleGraphicsTest()
{
  CSprite *window;
  CSprite *background;
  CSprite context[2];
  CPoint2D triangle[3];
  CShape3D model_info;
  CVector vertex;
  legacy_u8 model[TEMPLE_GRAPHICS_SHAPE_BYTES];
  legacy_u8 *window_bitmap;
  CTempleRasterSurface *surface;
  legacy_u32 saved[TEMPLE_GRAPHICS_CAPTURE_SIDE*TEMPLE_GRAPHICS_CAPTURE_SIDE*
      TEMPLE_RASTER_CELL_PIXELS];
  legacy_u64 x,y,slot,index,offset;
  legacy_u8 noreg mouse_was_shown=ms.show;

  TempleGraphicsAssert(sizeof(CShape2D)==TEMPLE_GRAPHICS_HEADER_BYTES,"bitmap header layout");
  TempleGraphicsAssert(sizeof(CPoint2D)==TEMPLE_GRAPHICS_POINT_BYTES,"polygon point layout");
  TempleGraphicsAssert(sizeof(CVector)==TEMPLE_GRAPHICS_VECTOR_BYTES,"binary vertex layout");
  MemSet(model,0,sizeof(model));
  model[0]=TEMPLE_GRAPHICS_SHAPE_VERTEX_COUNT;
  model[1]=1;
  model[2]=1;
  RsShape3dInitShape(model,&model_info);
  TempleGraphicsAssert(
      model_info.shape3d_vertex_bytes==model+TEMPLE_GRAPHICS_SHAPE_HEADER_BYTES,
      "binary vertex pointer");
  offset=TEMPLE_GRAPHICS_SHAPE_HEADER_BYTES+
      TEMPLE_GRAPHICS_VECTOR_BYTES*TEMPLE_GRAPHICS_SHAPE_VERTEX_COUNT;
  TempleGraphicsAssert(
      model_info.shape3d_visibility_masks==model+offset,"binary visibility mask pointer");
  TempleGraphicsAssert(
      model_info.shape3d_front_facing_masks==model+offset+TEMPLE_GRAPHICS_SHAPE_MASK_BYTES,
      "binary facing mask pointer");
  TempleGraphicsAssert(
      model_info.shape3d_primitives==model+offset+TEMPLE_GRAPHICS_SHAPE_MASK_BYTES*2,
      "binary primitive pointer");
  vertex.x=-1;
  vertex.y=2;
  vertex.z=I16_MIN;
  RsShape3dVertexWrite(&model_info,0,&vertex);
  MemSet(&vertex,0,sizeof(vertex));
  RsShape3dVertexRead(&model_info,0,&vertex);
  TempleGraphicsAssert(
      vertex.x==-1&&vertex.y==2&&vertex.z==I16_MIN,"signed binary vertex round trip");
  RsFullDataInitialize;
  TestMaterialInitialization();
  RsInitVideoGeometryFlags;
  RsMmgrInitConventionalArena;
  TempleGraphicsQueueTest();
  RsDosVideoSetMode13h;
  TempleGraphicsAssert((temple_video_front->flags&DCF_NO_TRANSPARENTS)!=0&&
          (temple_video_back->flags&DCF_NO_TRANSPARENTS)!=0,
      "opaque frames select native row-copy blitting");
  RsVideoPagesInitialize;
  RsSpriteSelectScreen;
  TestMaterialRaster(temple_video_front);
  TempleGraphicsProjectionTest();
  window=RsSpriteMakeWnd(TEMPLE_GRAPHICS_WINDOW_SIZE,TEMPLE_GRAPHICS_WINDOW_SIZE,0);
  background=RsSpriteMakeWnd(TEMPLE_GRAPHICS_CAPTURE_SIDE,TEMPLE_GRAPHICS_CAPTURE_SIDE,0);
  RsSpriteSelectTarget(window);
  RsSpriteSaveContext(context);
  RsSpriteSelectTarget(background);
  RsSpriteRestoreContext(context);
  TempleGraphicsAssert(drawing_sprite.sprite_bitmapptr==window->sprite_bitmapptr,
      "restored native bitmap pointer");
  TempleGraphicsAssert(
      drawing_sprite.sprite_lineofs==window->sprite_lineofs,"restored native row pointer");
  TempleGraphicsAssert(
      temple_raster_target==TempleRasterFind(window->sprite_bitmapptr),"restored raster target");
  RsSpriteClearTarget(0);
  triangle[0].px=TEMPLE_GRAPHICS_CAPTURE_X;
  triangle[0].py=TEMPLE_GRAPHICS_CAPTURE_Y;
  triangle[1].px=TEMPLE_GRAPHICS_CAPTURE_X+TEMPLE_GRAPHICS_CAPTURE_SIDE;
  triangle[1].py=TEMPLE_GRAPHICS_CAPTURE_Y;
  triangle[2].px=TEMPLE_GRAPHICS_CAPTURE_X;
  triangle[2].py=TEMPLE_GRAPHICS_CAPTURE_Y+TEMPLE_GRAPHICS_CAPTURE_SIDE;
  RsPreRenderDefault(TEMPLE_GRAPHICS_TEST_COLOR,3,triangle);
  surface=temple_raster_target;
  index=0;
  for(y=0;y<TEMPLE_GRAPHICS_CAPTURE_SIDE;y++) {
    for(x=0;x<TEMPLE_GRAPHICS_CAPTURE_SIDE;x++) {
      offset=surface->rows[y+TEMPLE_GRAPHICS_CAPTURE_Y]+x+TEMPLE_GRAPHICS_CAPTURE_X;
      for(slot=0;slot<TEMPLE_RASTER_CELL_PIXELS;slot++) {
        saved[index++]=surface->samples[offset*TEMPLE_RASTER_CELL_PIXELS+slot];
      }
    }
  }
  RsSpriteClearShapeAlt(
      background->sprite_bitmapptr,TEMPLE_GRAPHICS_CAPTURE_X,TEMPLE_GRAPHICS_CAPTURE_Y);
  RsSpriteClearTarget(TEMPLE_GRAPHICS_CLEAR_COLOR);
  RsSpritePutimage(background->sprite_bitmapptr);
  index=0;
  for(y=0;y<TEMPLE_GRAPHICS_CAPTURE_SIDE;y++) {
    for(x=0;x<TEMPLE_GRAPHICS_CAPTURE_SIDE;x++) {
      offset=surface->rows[y+TEMPLE_GRAPHICS_CAPTURE_Y]+x+TEMPLE_GRAPHICS_CAPTURE_X;
      for(slot=0;slot<TEMPLE_RASTER_CELL_PIXELS;slot++) {
        TempleGraphicsAssert(
            saved[index++]==surface->samples[offset*TEMPLE_RASTER_CELL_PIXELS+slot],
            "generated capture/blit retains every native sample");
      }
    }
  }
  RsSpriteSelectScreen;
  RsSpriteClearTarget(0);
  RsSpritePutimage(window->sprite_bitmapptr);
  TempleVideoPresent;
  window_bitmap=window->sprite_bitmapptr;
  RsSpriteFreeWnd(background);
  RsSpriteFreeWnd(window);
  TempleGraphicsAssert(!TempleRasterFind(window_bitmap),"window raster release");
  TempleVideoShutdown;
  TempleGraphicsAssert(ms.show==mouse_was_shown,"mouse visibility restoration");
  "Generated EGA materials, native projection, geometry queue, sprite capture/blit and "
  "lifetime checks "
  "passed.\n";
}
TempleGraphicsTest;
