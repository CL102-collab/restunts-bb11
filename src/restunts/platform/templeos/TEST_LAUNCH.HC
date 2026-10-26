// No game data is needed: exercise the real generated option parsers and launcher.
#include "LOAD.HC"

#define TEMPLE_LAUNCH_TEST_CHICANE_GRASS_Z 500
#define TEMPLE_LAUNCH_TEST_CHICANE_ROAD_X 350
#define TEMPLE_LAUNCH_TEST_REPETITIONS 80
#define TEMPLE_LAUNCH_TEST_ARGUMENTS (TEMPLE_LAUNCH_TEST_REPETITIONS+1)
#define TEMPLE_LAUNCH_TEST_OPTION_BYTES 9
#define TEMPLE_LAUNCH_TEST_TEXT_BYTES                                                              \
  (TEMPLE_LAUNCH_TEST_REPETITIONS*TEMPLE_LAUNCH_TEST_OPTION_BYTES+1)

legacy_void TestLaunchAssert(legacy_u8 condition,legacy_u8 *message)
{
  if(!condition) {
    PrintErr("Launcher test: %s\n",message);
    throw('Launch');
  }
}

legacy_void TestLaunchConfigure(legacy_s32 count,legacy_s8 **arguments)
{
  RsConfigurePowergearBug(count,arguments);
  RsConfigureLegacyCollision(count,arguments);
  RsConfigureLeftCornerBias(count,arguments);
  RsConfigureDualWaySwitching(count,arguments);
  RsConfigureChicaneFastGrass(count,arguments);
  RsConfigureAmigaGrass(count,arguments);
  RsConfigureOwoot(count,arguments);
  RsResetCarCapabilities();
  RsRestuntsStartupParseOptions(count,arguments,&rs_restunts_startup_options);
}

legacy_void TestLaunchChicane(legacy_u8 fast_grass)
{
  CTrackObjectSample sample;
  legacy_s16 noreg model,noreg expected_surface=CAR_SURFACE_GRASS;
  if(fast_grass) {
    expected_surface=CAR_SURFACE_PAVED;
  }
  sample.surface_type=CAR_SURFACE_PAVED;
  sample.position.y=0;
  for(model=PHYSICAL_MODEL_CHICANE_RIGHT_LEFT;model<=PHYSICAL_MODEL_CHICANE_LEFT_RIGHT;
      model++) {
    sample.physical_model=model;
    sample.position.x=0;
    sample.position.z=TEMPLE_LAUNCH_TEST_CHICANE_GRASS_Z;
    RsTrackobjTrackObjectChicane(&sample);
    TestLaunchAssert(current_surf_type==expected_surface,
        "Chicane Fast-Grass controls the translated green surface");
    sample.position.x=TEMPLE_LAUNCH_TEST_CHICANE_ROAD_X;
    if(model==PHYSICAL_MODEL_CHICANE_RIGHT_LEFT) {
      sample.position.x=-TEMPLE_LAUNCH_TEST_CHICANE_ROAD_X;
    }
    sample.position.z=TEMPLE_LAUNCH_TEST_CHICANE_GRASS_Z;
    RsTrackobjTrackObjectChicane(&sample);
    TestLaunchAssert(current_surf_type==CAR_SURFACE_PAVED,
        "the translated chicane keeps its visible road paved");
  }
}

legacy_void TestLaunch()
{
  legacy_s8 *text=StrNew("  --pg:off\t--lc:off --lcb:off --dws:off --cfg:off --amigagrass --owoot "
                          "/ns --nointro --fps:on ");
  legacy_s8 *legacy_options=StrNew("/ns /nd /h /sPC");
  legacy_s8 *quoted=StrNew("\"--pg:off\" '--lc:off' --lcb:off");
  legacy_s8 many[TEMPLE_LAUNCH_TEST_TEXT_BYTES];
  legacy_s8 *arguments[TEMPLE_LAUNCH_TEST_ARGUMENTS+1];
  legacy_s32 noreg count,noreg index;
  legacy_u8 *previous=StrNew(Fs->cur_dir);
  legacy_u8 noreg previous_drive=Fs->cur_dv->drv_let;
  arguments[0]="RESTUNTS";
  count=TempleLaunchSplit(text,arguments,1);
  TestLaunchAssert(count==11&&arguments[count]==NULL,"all ten startup options arrive");
  TestLaunchConfigure(count,arguments);
  TestLaunchAssert(!rs_statecar_powergear_bug_enabled,"power gear can be disabled");
  TestLaunchAssert(!rs_physics_collision_legacy_collision_enabled,"collision can be disabled");
  TestLaunchAssert(!rs_physics_grip_left_corner_bias_enabled,"corner bias can be disabled");
  TestLaunchAssert(!rs_race_route_dual_way_switching_enabled,"Dual-Way Switching can be disabled");
  TestLaunchAssert(!rs_trackobj_chicane_fast_grass_enabled,"Chicane fast-grass can be disabled");
  TestLaunchChicane(FALSE);
  TestLaunchAssert(rs_physics_grip_amiga_grass_enabled,"Amiga grass can be enabled");
  TestLaunchAssert(owoot_enabled,"OWOOT can be enabled");
  TestLaunchAssert(rs_restunts_startup_options.sound_disabled,"sound can be disabled");
  TestLaunchAssert(rs_restunts_startup_options.skip_intro,"intro can be skipped");
  TestLaunchAssert(rs_restunts_startup_options.fps_display_enabled,"FPS can be enabled");
  count=TempleLaunchSplit(legacy_options,arguments,1);
  TestLaunchAssert(count==5,"all legacy argument strings arrive");
  TestLaunchConfigure(count,arguments);
  TestLaunchAssert(rs_restunts_startup_options.sound_disabled,"legacy sound flag");
  TestLaunchAssert(rs_restunts_startup_options.unused_nd_option,"recognized legacy /nd flag");
  TestLaunchAssert(rs_restunts_startup_options.mode4_requested==4,"legacy video flag parsing");
  TestLaunchAssert(audiodriverstring[0]=='P' &&audiodriverstring[1]=='C',
      "legacy PC audio argument reaches the parser");
  TestLaunchConfigure(1,arguments);
  TestLaunchAssert(rs_statecar_powergear_bug_enabled&&
          rs_physics_collision_legacy_collision_enabled&&
          rs_physics_grip_left_corner_bias_enabled&&rs_race_route_dual_way_switching_enabled&&
          rs_trackobj_chicane_fast_grass_enabled,
      "original physics defaults are restored");
  TestLaunchChicane(TRUE);
  TestLaunchAssert(
      !rs_physics_grip_amiga_grass_enabled&&!owoot_enabled,"opt-in physics resets when omitted");
  TestLaunchAssert(
      !rs_restunts_startup_options.sound_disabled&&!rs_restunts_startup_options.skip_intro,
      "intro and sound default to on");
  TestLaunchAssert(!rs_restunts_startup_options.fps_display_enabled&&
          !rs_restunts_startup_options.fps_display_specified,
      "FPS defaults to off");
  arguments[1]="--dws:off";
  arguments[2]="--DWS:ON";
  RsConfigureDualWaySwitching(3,arguments);
  TestLaunchAssert(rs_race_route_dual_way_switching_enabled,
      "Dual-Way Switching uses the last case-insensitive value");
  arguments[1]="--DWS:ON";
  arguments[2]="--dws:off";
  RsConfigureDualWaySwitching(3,arguments);
  TestLaunchAssert(!rs_race_route_dual_way_switching_enabled,
      "Dual-Way Switching can be disabled after an earlier option");
  RsConfigureDualWaySwitching(1,arguments);
  arguments[1]="--cfg:off";
  arguments[2]="--CFG:ON";
  RsConfigureChicaneFastGrass(3,arguments);
  TestLaunchAssert(rs_trackobj_chicane_fast_grass_enabled,
      "Chicane fast-grass uses the last case-insensitive value");
  arguments[1]="--CFG:ON";
  arguments[2]="--cfg:off";
  RsConfigureChicaneFastGrass(3,arguments);
  TestLaunchAssert(!rs_trackobj_chicane_fast_grass_enabled,
      "Chicane fast-grass can be disabled after an earlier option");
  RsConfigureChicaneFastGrass(1,arguments);
  count=TempleLaunchSplit(quoted,arguments,1);
  TestLaunchAssert(
      count==4&&!StrCmp(arguments[1],"--pg:off")&&!StrCmp(arguments[2],"--lc:off"),
      "quoted option strings");
  many[0]=0;
  for(index=0;index<TEMPLE_LAUNCH_TEST_REPETITIONS;index++) {
    StrCpy(many+index*TEMPLE_LAUNCH_TEST_OPTION_BYTES,"--pg:off ");
  }
  count=TempleLaunchSplit(many,arguments,1);
  TestLaunchAssert(count==TEMPLE_LAUNCH_TEST_ARGUMENTS&&arguments[count]==NULL,
      "argument list is not limited to the old three slots");
  arguments[1]="--version";
  TestLaunchAssert(!RestuntsArgs("Z:/MISSING",2,arguments),
      "argv version exits before opening the data directory");
  TestLaunchAssert(
      !Restunts("Z:/MISSING",FALSE,FALSE,"--help"),"options-string help works without data");
  TestLaunchAssert(previous_drive==Fs->cur_dv->drv_let&&!StrCmp(previous,Fs->cur_dir),
      "information requests preserve the current directory");
  Free(text);
  Free(legacy_options);
  Free(quoted);
  Free(previous);
  Print("TempleOS launcher and shared startup option tests passed.\n");
}

TestLaunch();
