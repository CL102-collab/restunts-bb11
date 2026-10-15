// Reconstruct DEFAULT.RPL with the translated game and render its native racing line.
// Requires the unchanged original game resources in C:/DATA.
#include "LOAD.HC"

#define TEMPLE_GHOST_LINE_FRAME 100
#define TEMPLE_GHOST_LINE_CHECKPOINT_BYTES (1120*20)
#define TEMPLE_GHOST_LINE_SPEED_MAX 65535
#define TEMPLE_GHOST_LINE_SEED_BYTES 6
#define TEMPLE_GHOST_LINE_DEBUG_PORT 0xE9

legacy_void TempleGhostLineProgress(legacy_u8 *message)
{
  while(*message) {
    OutU8(TEMPLE_GHOST_LINE_DEBUG_PORT,*message++);
  }
  OutU8(TEMPLE_GHOST_LINE_DEBUG_PORT,'\n');
}

legacy_void TempleGhostLineAssert(legacy_s64 condition,legacy_u8 *message)
{
  if(!condition) {
    PrintErr("Ghost racing line: %s\n",message);
    throw('GhostLin');
  }
}

legacy_u8 *TempleGhostLineCapture(legacy_u16 speed)
{
  CGameState saved,presentation;
  CGameInfo saved_config;
  legacy_u8 seed_before[TEMPLE_GHOST_LINE_SEED_BYTES];
  legacy_u8 seed_after[TEMPLE_GHOST_LINE_SEED_BYTES];
  legacy_u8 *pixels=MAlloc(TEMPLE_RENDER_WIDTH*TEMPLE_RENDER_HEIGHT);
  CTempleRasterSurface *surface;
  legacy_s64 x,y,offset,slot;
  MemCpy(&saved,&state,sizeof(saved));
  MemCpy(&presentation,&state,sizeof(presentation));
  MemCpy(&saved_config,&gameconfig,sizeof(saved_config));
  RsGetKevinrandomSeed(seed_before);
  presentation.playerstate.car_actual_speed=speed;
  full_redraw_frames_remaining=1;
  RsSpriteSelectRenderWindow;
  RsUpdateFrameSnapshot(0,&rect_windshield,&presentation,RsGhostCarState,RsGhostCameraState);
  surface=temple_raster_target;
  for(y=0;y<TEMPLE_RENDER_HEIGHT;y++) {
    for(x=0;x<TEMPLE_RENDER_WIDTH;x++) {
      offset=surface->rows[y/TEMPLE_RENDER_SCALE]+x/TEMPLE_RENDER_SCALE;
      slot=y%TEMPLE_RENDER_SCALE*TEMPLE_RENDER_SCALE+x%TEMPLE_RENDER_SCALE;
      pixels[y*TEMPLE_RENDER_WIDTH+x]=
          surface->samples[offset*TEMPLE_RASTER_CELL_PIXELS+slot];
    }
  }
  RsGetKevinrandomSeed(seed_after);
  TempleGhostLineAssert(!MemCmp(&state,&saved,sizeof(saved)),"render preserves physics state");
  TempleGhostLineAssert(!MemCmp(&gameconfig,&saved_config,sizeof(saved_config)),
      "render preserves game configuration");
  TempleGhostLineAssert(
      !MemCmp(seed_before,seed_after,sizeof(seed_before)),"render preserves random seed");
  return pixels;
}

legacy_void TempleGhostLineColors(legacy_u16 reference_speed,legacy_u8 mode)
{
  legacy_u8 *green=TempleGhostLineCapture(0);
  legacy_u8 *red=TempleGhostLineCapture(TEMPLE_GHOST_LINE_SPEED_MAX);
  legacy_u8 *yellow=TempleGhostLineCapture(reference_speed);
  legacy_s64 index,colored=0,neutral=0;
  for(index=0;index<TEMPLE_RENDER_WIDTH*TEMPLE_RENDER_HEIGHT;index++) {
    if(green[index]==10&&red[index]==12) {
      colored++;
      if(yellow[index]==14) {
        neutral++;
      }
    }
  }
  Print("Ghost racing line mode %d: %d green/red pixels, %d yellow pixels.\n",mode,colored,
      neutral);
  TempleGhostLineAssert(colored>0,"native renderer emits red and green ribbon pixels");
  TempleGhostLineAssert(neutral>0,"native renderer emits yellow ribbon pixels");
  Free(green);
  Free(red);
  Free(yellow);
}

legacy_void TempleGhostLineTest()
{
  legacy_s8 *arguments[2];
  legacy_u16 noreg reference_speed;
  legacy_u8 *slow,*fast;
  CGhostRacingLineSegment *segment;
  legacy_s64 east,south,count=0;
  arguments[0]="TEMPLE_GHOST_LINE";
  arguments[1]="DEFAULT";
  TempleGhostLineAssert(Cd("C:/DATA"),"game data directory");
  TempleGhostLineProgress("GHOST LINE: initialize game");
  RsFullDataInitialize;
  TempleMaterialInit;
  audiodriverstring[0]='p';
  audiodriverstring[1]='c';
  RsInitMain(2,arguments);
  RsInitDiv0;
  RsInitRowTables;
  mainresptr=RsFileLoadResfile("main");
  fontdefptr=RsFileLoadResource(0,"fontdef.fnt");
  fontnptr=RsFileLoadResource(0,"fontn.fnt");
  RsFontSetFontdef;
  RsInitPolyinfo;
  RsInitTrackdata;
  RsResetRaceLoopState;
  RsInitKevinrandom("kevin");
  cvxptr=RsMmgrAllocResbytes("cvx",TEMPLE_GHOST_LINE_CHECKPOINT_BYTES);
  TempleGhostLineAssert(!RsFileLoadReplay("","DEFAULT"),"load replay");
  TempleGhostLineAssert(!RsGhostSelectReplay("","DEFAULT"),"select ghost replay");
  gameconfig.game_opponenttype=0;
  framespersec=gameconfig.game_framespersec;
  TempleGhostLineProgress("GHOST LINE: reconstruct full recording");
  TempleGhostLineAssert(!RsGhostPrepareRace,"prepare ghost");
  TempleGhostLineAssert(RsGhostIsActive,"ghost active");
  for(south=0;south<30;south++) {
    for(east=0;east<30;east++) {
      segment=RsGhostRacingLineFirst(east,south);
      while(segment) {
        count++;
        segment=RsGhostRacingLineNext(segment);
      }
    }
  }
  TempleGhostLineAssert(count>0,"recording reconstructs cached ribbon segments");
  Print("Ghost racing line reconstructed %d segments.\n",count);
  TempleGhostLineAssert(!RsFileLoadReplay("","DEFAULT"),"reload live controls");
  gameconfig.game_opponenttype=0;
  MemCpy(&gameconfigcopy,&gameconfig,sizeof(gameconfig));
  TempleGhostLineAssert(!RsTrackSetup,"track setup");
  replay_filename[0]=0;
  RsInitGameState(-1);
  viewport_bottom_cache=-1;
  run_game_random=TempleCastS16(RsGetKevinrandom<<3);
  replaybar_toggle=FALSE;
  is_in_replay=TRUE;
  idle_expired=FALSE;
  cameramode=CAMERA_MODE_COCKPIT;
  game_replay_mode=REPLAY_MODE_PLAYBACK;
  detail_level=0;
  slow_video_mgmt=TRUE;
  slow_video_mgmt_copy=TRUE;
  TempleGhostLineAssert(!RsSetupPlayerCarsWithoutDashboard,"car resources");
  kbormouse=FALSE;
  replay_playback_speed=REPLAY_PLAYBACK_NORMAL;
  race_exit_request=TRUE;
  game_replay_mode_copy=TEMPLE_BYTE_MASK;
  frame_buffer_index=0;
  dashboard_buffer_index=0;
  recording_limit_warning_requested=FALSE;
  dashb_toggle=FALSE;
  follow_opponent_flag=FALSE;
  start_flag_animation=500;
  rect_windshield.left=0;
  rect_windshield.right=TEMPLE_LOGICAL_WIDTH;
  rect_windshield.top=0;
  rect_windshield.bottom=TEMPLE_LOGICAL_HEIGHT;
  RsSetProjection(35,TEMPLE_LOGICAL_HEIGHT/6,TEMPLE_LOGICAL_WIDTH,TEMPLE_LOGICAL_HEIGHT);
  RsRestoreGamestate(0);
  TempleGhostLineProgress("GHOST LINE: live player frame 100");
  while(state.game_frame<TEMPLE_GHOST_LINE_FRAME) {
    RsUpdateGamestate;
  }
  RsInitRectArrays;
  RsShape3dSetLegacyRenderStack(NULL,0,0,NULL);
  reference_speed=state.playerstate.car_actual_speed;
  RsGhostUpdate(gameconfig.game_recordedframes,framespersec);
  TempleGhostLineProgress("GHOST LINE: native frame captures");
  TempleGhostLineColors(reference_speed,FALSE);
  supersight_enabled=TRUE;
  TempleGhostLineColors(reference_speed,TRUE);
  RsGhostEndRace;
  slow=TempleGhostLineCapture(0);
  fast=TempleGhostLineCapture(TEMPLE_GHOST_LINE_SPEED_MAX);
  TempleGhostLineAssert(!MemCmp(slow,fast,TEMPLE_RENDER_WIDTH*TEMPLE_RENDER_HEIGHT),
      "ending ghost removes speed-colored ribbon");
  Free(slow);
  Free(fast);
  RsGhostClear;
  RsDosTimerShutdown;
  RsDosAudioShutdown;
  TempleFileShutdown;
  TempleVideoShutdown;
  RsKbExitHandler;
  Print("Ghost racing line: reconstruction, native colors, distant ghost and physics isolation "
        "passed.\n");
}

TempleGhostLineTest;
