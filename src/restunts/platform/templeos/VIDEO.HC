#include "NATIVE.HH"
#include "PALETTE.HH"

#define TEMPLE_VIDEO_SEGMENT 0xA000
#define TEMPLE_VIDEO_RETRACE_STATUS 8
#define TEMPLE_VIDEO_PAGE_COUNT 1
#define TEMPLE_VIDEO_REFRESH_HZ 60
#define TEMPLE_VIDEO_REFRESH_SECONDS (1.0/TEMPLE_VIDEO_REFRESH_HZ)
#define TEMPLE_VIDEO_RETRACE_PHASES 2

CDC *temple_video_front=NULL;
CDC *temple_video_back=NULL;
legacy_u8 temple_video_active=FALSE;
legacy_u8 temple_video_mouse_was_shown=FALSE;
legacy_f64 temple_video_next_present=0;
legacy_u64 temple_video_palette_generation=0;
legacy_u8 temple_video_presented=FALSE;

legacy_void TempleVideoDraw(CTask *task,CDC *dc)
{
  no_warn task;
  if(temple_video_front) {
    GrBlot(dc,0,0,temple_video_front);
  }
}

legacy_void RsDosVideoSetMode13h()
{
  if(temple_video_active) {
    return;
  }
  SettingsPush();
  temple_video_mouse_was_shown=ms.show;
  ms.show=FALSE;
  temple_video_active=TRUE;
  temple_video_presented=FALSE;
  temple_video_next_present=0;
  AutoComplete(OFF);
  WinBorder(OFF);
  WinHorz(0,TEXT_COLS-1);
  WinVert(0,TEXT_ROWS-1);
  DocCursor(OFF);
  Fs->scroll_x=0;
  Fs->scroll_y=0;
  Fs->win_inhibit=WIG_TASK_DFT&~(WIF_SELF_FOCUS|WIF_SELF_BORDER);
  temple_video_front=DCNew(TEMPLE_SCREEN_WIDTH,TEMPLE_SCREEN_HEIGHT);
  temple_video_back=DCNew(TEMPLE_SCREEN_WIDTH,TEMPLE_SCREEN_HEIGHT);
  /* Both frames contain only palette indices, so GrBlot can copy full rows. */
  temple_video_front->flags|=DCF_NO_TRANSPARENTS;
  temple_video_back->flags|=DCF_NO_TRANSPARENTS;
  DCFill(temple_video_front,BLACK);
  DCFill(temple_video_back,BLACK);
  TemplePaletteInit();
  MemSet(RsDosMemoryMakePointer(TEMPLE_VIDEO_SEGMENT,0),0,
      TEMPLE_LOGICAL_WIDTH*TEMPLE_LOGICAL_HEIGHT);
  Fs->draw_it=&TempleVideoDraw;
}

legacy_void TempleVideoPresent()
{
  CDC *previous;
  legacy_u64 palette_generation;
  if(!temple_video_active||tS<temple_video_next_present) {
    return;
  }
  palette_generation=TemplePaletteGeneration();
  /* Input and timer polling can run between game frames. Reuse the last
   * converted screen until its pixels or palette mapping actually change.
   */
  if(temple_video_presented&&!TempleRasterScreenDirty()&&
      temple_video_palette_generation==palette_generation) {
    return;
  }
  TempleRasterPresent(temple_video_back);
  previous=temple_video_front;
  temple_video_front=temple_video_back;
  temple_video_back=previous;
  TempleRasterScreenPresented();
  temple_video_palette_generation=palette_generation;
  temple_video_presented=TRUE;
  /* Conversion may exceed one refresh interval under software emulation. */
  temple_video_next_present=tS+TEMPLE_VIDEO_REFRESH_SECONDS;
}

legacy_void TempleVideoShutdown()
{
  if(!temple_video_active) {
    return;
  }
  Fs->draw_it=NULL;
  /* Restore the caller's callback before releasing frames used by the window
   * manager. SettingsPop synchronizes the manager with Refresh internally.
   */
  SettingsPop();
  ms.show=temple_video_mouse_was_shown;
  DCDel(temple_video_front);
  DCDel(temple_video_back);
  temple_video_front=NULL;
  temple_video_back=NULL;
  temple_video_active=FALSE;
  TempleRasterShutdown();
  TemplePaletteShutdown();
}

legacy_s16 RsDosVideoGetStatus()
{
  TemplePump();
  if(ToI64(tS*TEMPLE_VIDEO_REFRESH_HZ*TEMPLE_VIDEO_RETRACE_PHASES)&1) {
    return TEMPLE_VIDEO_RETRACE_STATUS;
  }
  return 0;
}

legacy_void RsDosVideoSetPalette(legacy_u16 start,legacy_u16 count,legacy_u8 *palette)
{
  TemplePaletteSet(start,count,palette);
}

legacy_u8 RsDosVideoEnablePlanarPages()
{
  return FALSE;
}

legacy_void RsDosVideoSetWritePlanes(legacy_u8 mask)
{
  no_warn mask;
}

legacy_void RsDosVideoSetReadPlane(legacy_u8 plane)
{
  no_warn plane;
}

legacy_void RsDosVideoShowPage(legacy_u16 address)
{
  no_warn address;
  TempleVideoPresent();
}

legacy_void RsDosVideoSetMode4()
{
  RsDosVideoSetMode13h();
}

legacy_void RsDosVideoSetMode7()
{
  TempleVideoShutdown();
}

legacy_void RsVideoPagesInitialize()
{
  TempleRasterRegisterSprite(&screen_sprite);
}

legacy_u8 RsVideoPagesIsActive()
{
  return FALSE;
}

legacy_void RsVideoPagesBeginRace()
{
  video_page_count=TEMPLE_VIDEO_PAGE_COUNT;
  video_uses_page_flipping=FALSE;
}

legacy_void RsVideoPagesEndRace()
{
}

legacy_void RsVideoPagesSelectBackbuffer()
{
}

legacy_void RsVideoPagesPresent()
{
  TempleVideoPresent();
}
