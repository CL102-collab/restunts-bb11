// No game resources are needed. Exercise the generated penalty tracker and
// penalty-time commit with the same dual-way graph as test-penalty-routing.c.
#include "LOAD.HC"

#define TEMPLE_DWS_ROUTE_STORAGE 904
#define TEMPLE_DWS_ROUTE_COUNT 19
#define TEMPLE_DWS_REPLAY_BYTES 12000
#define TEMPLE_DWS_BACKUP_BYTES 1964
#define TEMPLE_DWS_FRAMES_PER_SECOND 20
#define TEMPLE_DWS_POSITION_SCALE 65536

legacy_s16 temple_dws_primary[TEMPLE_DWS_ROUTE_STORAGE];
legacy_s16 temple_dws_alternate[TEMPLE_DWS_ROUTE_STORAGE];
legacy_s8 temple_dws_columns[TEMPLE_DWS_ROUTE_STORAGE];
legacy_s8 temple_dws_rows[TEMPLE_DWS_ROUTE_STORAGE];
legacy_s8 temple_dws_elements[TEMPLE_DWS_ROUTE_STORAGE];
legacy_s8 temple_dws_replay[TEMPLE_DWS_REPLAY_BYTES];
legacy_s8 temple_dws_backup[TEMPLE_DWS_BACKUP_BYTES];

legacy_void TempleDwsAssert(legacy_u8 condition,legacy_u8 *message)
{
  if(!condition) {
    PrintErr("Dual-Way Switching test: %s\n",message);
    throw('Dws');
  }
}

legacy_void TempleDwsConfigure(legacy_s8 *option=NULL)
{
  legacy_s8 *arguments[2];
  legacy_s64 count=1;
  if(option) {
    count=2;
  }
  arguments[0]="TEMPLE_DWS_TEST";
  arguments[1]=option;
  RsConfigureDualWaySwitching(count,arguments);
}

legacy_void TempleDwsPlace(legacy_s64 piece)
{
  state.playerstate.car_position.lx=temple_dws_columns[piece]*TEMPLE_DWS_POSITION_SCALE;
  state.playerstate.car_position.lz=(29-temple_dws_rows[piece])*TEMPLE_DWS_POSITION_SCALE;
}

legacy_void TempleDwsReset()
{
  legacy_s64 index;
  MemSet(&state,0,sizeof(state));
  MemSet(trk_object_list,0,sizeof(trk_object_list));
  MemSet(temple_dws_columns,0,sizeof(temple_dws_columns));
  MemSet(temple_dws_rows,0,sizeof(temple_dws_rows));
  MemSet(temple_dws_elements,0,sizeof(temple_dws_elements));
  MemSet(temple_dws_replay,0,sizeof(temple_dws_replay));
  MemSet(temple_dws_backup,0,sizeof(temple_dws_backup));
  track_primary_route_links=temple_dws_primary;
  track_alternate_route_links=temple_dws_alternate;
  track_route_columns=temple_dws_columns;
  track_route_rows=temple_dws_rows;
  track_route_element_ids=temple_dws_elements;
  replay_input_buffer=temple_dws_replay;
  track_and_directory_backup=temple_dws_backup;
  track_pieces_counter=TEMPLE_DWS_ROUTE_COUNT;
  framespersec=TEMPLE_DWS_FRAMES_PER_SECOND;
  legacy_execution_residue.penalty_route_word=-1;
  state.game_startcol=state.game_startcol2=-1;
  state.game_startrow=state.game_startrow2=-1;
  for(index=0;index<TEMPLE_DWS_ROUTE_STORAGE;index++) {
    temple_dws_primary[index]=-1;
    temple_dws_alternate[index]=-1;
  }
  for(index=0;index<TEMPLE_DWS_ROUTE_COUNT;index++) {
    temple_dws_primary[index]=index+1;
    temple_dws_columns[index]=index;
    temple_dws_rows[index]=5;
  }
  // Split after 1: the first path is 2..7, the second is 8..15.
  // The two split footprints are shared; both paths rejoin at 16.
  temple_dws_primary[7]=16;
  temple_dws_alternate[1]=8;
  temple_dws_columns[8]=temple_dws_columns[2];
  temple_dws_primary[18]=0;
  state.game_player_confirmed_route=state.game_player_previous_route=3;
  state.game_player_route_status=ROUTE_TRACKING_NORMAL;
  TempleDwsPlace(3);
}

legacy_void TempleDwsSwitching()
{
  legacy_s64 mode,piece,expected;
  for(mode=0;mode<3;mode++) {
    if(mode==0) {
      TempleDwsConfigure;
    } else {
      if(mode==1) {
        TempleDwsConfigure("--dws:on");
      } else {
        TempleDwsConfigure("--dws:off");
      }
    }
    TempleDwsReset;
    for(piece=13;piece<=18;piece++) {
      TempleDwsPlace(piece);
      RsStateUpdatePlayerRoutePenalty;
      if(mode==2&&piece<16) {
        TempleDwsAssert(state.game_player_confirmed_route==3,
            "switch must retain the chosen branch until rejoining");
        TempleDwsAssert(state.game_player_route_status==ROUTE_TRACKING_OUTSIDE_TRACK,
            "sibling-branch tiles must not forgive the cut");
      }
    }
    expected=0;
    if(mode==2) {
      expected=18;
    }
    TempleDwsAssert(state.game_penalty==expected*TEMPLE_DWS_FRAMES_PER_SECOND,
        "forward branch-switch penalty");
    TempleDwsPlace(0);
    RsStateUpdatePlayerRoutePenalty;
    TempleDwsAssert(state.playerstate.car_lap_count==1,"forward switch reaches finish");
    TempleDwsAssert(state.game_penalty==expected*TEMPLE_DWS_FRAMES_PER_SECOND,
        "forward penalty survives finish");
    Print("DWS: mode %d forward switch penalty %d seconds.\n",mode,expected);

    TempleDwsReset;
    state.game_player_confirmed_route=state.game_player_previous_route=9;
    piece=4;
    while(piece!=0) {
      TempleDwsPlace(piece);
      RsStateUpdatePlayerRoutePenalty;
      piece=temple_dws_primary[piece];
    }
    TempleDwsPlace(0);
    RsStateUpdatePlayerRoutePenalty;
    expected=0;
    if(mode==2) {
      expected=24;
    }
    TempleDwsAssert(state.playerstate.car_lap_count==1,"reverse switch reaches finish");
    TempleDwsAssert(state.game_penalty==expected*TEMPLE_DWS_FRAMES_PER_SECOND,
        "reverse branch-switch penalty");
    Print("DWS: mode %d reverse switch penalty %d seconds.\n",mode,expected);
  }
}

legacy_void TempleDwsLegalRoutes()
{
  legacy_s64 branch,piece;
  legacy_s16 noreg current,penalty;
  TempleDwsConfigure("--dws:off");
  for(branch=0;branch<2;branch++) {
    TempleDwsReset;
    state.game_player_confirmed_route=state.game_player_previous_route=0;
    piece=1;
    while(piece!=0) {
      TempleDwsPlace(piece);
      RsStateUpdatePlayerRoutePenalty;
      if(branch&&piece==1) {
        piece=8;
      } else {
        piece=temple_dws_primary[piece];
      }
    }
    TempleDwsPlace(0);
    RsStateUpdatePlayerRoutePenalty;
    TempleDwsAssert(state.playerstate.car_lap_count==1,"legal branch reaches finish");
    TempleDwsAssert(state.game_penalty==0,"legal branch remains penalty-free");
    Print("DWS: legal branch %d remains penalty-free.\n",branch);
  }
  TempleDwsReset;
  temple_dws_primary[3]=0;
  temple_dws_alternate[3]=8;
  temple_dws_alternate[9]=11;
  temple_dws_primary[11]=12;
  TempleDwsPlace(12);
  current=3;
  TempleDwsAssert(RsDetectPenalty(&current,&penalty)==1,"nested alternative is found");
  TempleDwsAssert(current==12&&penalty>=0,"nested alternative survives finish boundary");

  TempleDwsReset;
  state.game_player_confirmed_route=state.game_player_previous_route=17;
  TempleDwsPlace(0);
  RsStateUpdatePlayerRoutePenalty;
  TempleDwsAssert(state.game_penalty==3*TEMPLE_DWS_FRAMES_PER_SECOND,
      "skipping the last piece still costs three seconds");
  TempleDwsAssert(state.playerstate.car_lap_count==1,"last-piece cut still finishes");

  TempleDwsReset;
  state.game_player_confirmed_route=state.game_player_previous_route=5;
  TempleDwsPlace(4);
  RsStateUpdatePlayerRoutePenalty;
  TempleDwsAssert(state.game_player_route_status==ROUTE_TRACKING_WRONG_WAY,
      "driving backwards remains a wrong-way event");
  TempleDwsAssert(state.game_penalty==0,"wrong-way event does not add shortcut penalty");
}

TempleDwsSwitching;
TempleDwsLegalRoutes;
Print("TempleOS Dual-Way Switching penalty regressions passed.\n");
