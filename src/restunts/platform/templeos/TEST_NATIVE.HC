// Run only in the disposable VM test image: creates and removes C:/NATIVE.TS*.
#include "LEGACY.HH"
#include "LIBC.HC"
#include "NATIVE.HH"

#define TEMPLE_TEST_PATH "C:/NATIVE.TST"
#define TEMPLE_TEST_SECOND_PATH "C:/NATIVE.TS2"
#define TEMPLE_TEST_NAME "NATIVE.TST"
#define TEMPLE_TEST_SECOND_NAME "NATIVE.TS2"
#define TEMPLE_TEST_ABSOLUTE_PATTERN "c:\\native.ts*"
#define TEMPLE_TEST_RELATIVE_PATTERN "..\\native.ts?"
#define TEMPLE_TEST_MISSING_PATTERN "c:/native.missing"
#define TEMPLE_TEST_FIRST_MATCH 1
#define TEMPLE_TEST_SECOND_MATCH 2
#define TEMPLE_TEST_ALL_MATCHES (TEMPLE_TEST_FIRST_MATCH|TEMPLE_TEST_SECOND_MATCH)
#define TEMPLE_TEST_BUFFER_SIZE 8
#define TEMPLE_TEST_KEY_F11 0x8500
#define TEMPLE_TEST_KEY_F9 0x4300
#define TEMPLE_TEST_KEY_CTRL_F9 0x6600
#define TEMPLE_TEST_TICK_WAIT_MS 35
#define TEMPLE_TEST_TIMER_SECONDS 1
#define TEMPLE_TEST_TIMER_TOLERANCE_TICKS 2
#define TEMPLE_TEST_PUMP_ROUNDS 4
#define TEMPLE_TEST_WORKER_KEY 'K'

legacy_s16 RsKbParseKey(legacy_s16 value)
{
  return value;
}

legacy_u32 temple_test_audio_ticks=0;
legacy_u32 temple_test_callback_ticks=0;
legacy_u32 temple_test_worker_request=0,temple_test_worker_acknowledged=0;

legacy_void TempleVideoPresent()
{
}

legacy_void TempleVideoShutdown()
{
}

#include "FILE.HC"
#include "INPUT.HC"
#include "TIMER.HC"
#include "RUNTIME.HC"

legacy_void TempleAudioTick()
{
  temple_test_audio_ticks++;
}

legacy_void TempleTestAssert(legacy_u8 condition,legacy_u8 *message)
{
  if(!condition) {
    PrintErr("Native test: %s\n",message);
    throw('Native');
  }
}

legacy_void TempleTestCallback()
{
  temple_test_callback_ticks++;
}

legacy_void TempleTestFindPattern(legacy_u8 *pattern)
{
  legacy_u8 *directory=StrNew(Fs->cur_dir);
  legacy_u8 noreg drive=Fs->cur_dv->drv_let;
  legacy_u8 *name=RsDosFileFindFirst(pattern);
  legacy_u8 noreg found=0,noreg match;
  while(name) {
    match=0;
    if(!StrCmp(name,TEMPLE_TEST_NAME)) {
      match=TEMPLE_TEST_FIRST_MATCH;
    } else if(!StrCmp(name,TEMPLE_TEST_SECOND_NAME)) {
      match=TEMPLE_TEST_SECOND_MATCH;
    }
    TempleTestAssert(match!=0,"wildcard returns a matching basename");
    TempleTestAssert(!(found&match),"find next never repeats a file");
    found|=match;
    name=RsDosFileFindNext();
  }
  TempleTestAssert(found==TEMPLE_TEST_ALL_MATCHES,"wildcard enumerates both test files");
  TempleTestAssert(RsDosFileFindNext()==NULL,"exhausted search remains exhausted");
  TempleTestAssert(drive==Fs->cur_dv->drv_let&&!StrCmp(directory,Fs->cur_dir),
      "file search preserves the current drive and directory");
  Free(directory);
}

legacy_void TempleTestFileSearch()
{
  legacy_u16 noreg handle=RsDosFileOpen(TEMPLE_TEST_SECOND_PATH,TEMPLE_FILE_CREATE);
  legacy_u8 *name;
  TempleTestAssert(handle!=TEMPLE_FILE_INVALID_HANDLE,"create second search fixture");
  TempleTestAssert(RsDosFileWrite(handle,"S",1)==1,"write second search fixture");
  TempleTestAssert(!RsDosFileClose(handle),"persist second search fixture");
  TempleTestFindPattern(TEMPLE_TEST_ABSOLUTE_PATTERN);
  TempleTestFindPattern(TEMPLE_TEST_RELATIVE_PATTERN);
  TempleTestAssert(RsDosFileFindFirst(TEMPLE_TEST_ABSOLUTE_PATTERN)!=NULL,
      "start a search before resetting it");
  TempleTestAssert(RsDosFileFindFirst(TEMPLE_TEST_MISSING_PATTERN)==NULL,
      "no-match search replaces previous results");
  TempleTestAssert(RsDosFileFindNext()==NULL,"no stale results after no match");
  name=RsDosFileFindFirst(TEMPLE_TEST_PATH);
  TempleTestAssert(name!=NULL,"exact search can restart after no match");
  TempleTestAssert(!StrCmp(name,TEMPLE_TEST_NAME),"exact search returns a basename");
  TempleTestAssert(RsDosFileFindNext()==NULL,"exact search has one result");
  TempleTestFindPattern(TEMPLE_TEST_ABSOLUTE_PATTERN);
  TempleFileShutdown();
  TempleTestAssert(RsDosFileFindNext()==NULL,"shutdown releases search results");
  TempleTestAssert(!RsDosFileRemove(TEMPLE_TEST_SECOND_PATH),"delete second search fixture");
}

legacy_void TempleTestPumpWorker(CTask *recipient)
{
  while(TRUE) {
    if(temple_test_worker_request!=temple_test_worker_acknowledged) {
      TaskMsg(recipient,NULL,MSG_KEY_DOWN,TEMPLE_TEST_WORKER_KEY,0,0);
      temple_test_worker_acknowledged=temple_test_worker_request;
    }
    Yield();
  }
}

legacy_void TempleTestPumpScheduling()
{
  legacy_u16 noreg round;
  legacy_u32 noreg before;
  legacy_u8 noreg progressed=TRUE,noreg advanced=TRUE,noreg delivered=TRUE;
  CTask *worker=Spawn(&TempleTestPumpWorker,Fs,"Pump regression",Fs->gs->num);
  TempleTestAssert(worker!=NULL,"spawn cooperative worker");
  RsKbInitInterrupt();
  for(round=0;round<TEMPLE_TEST_PUMP_ROUNDS;round++) {
    temple_test_worker_request++;
    /* Force an elapsed tick so timer_get_counter cannot yield through its
     * idle Sleep branch. Busy drawing causes the same condition in game.
     */
    temple_timer_last_tick=ToI64(tS*TEMPLE_TIMER_TICKS_PER_SECOND)-1;
    before=temple_timer_counter;
    RsTimerGetCounter();
    if(temple_test_worker_acknowledged!=temple_test_worker_request) {
      progressed=FALSE;
    }
    if(temple_timer_counter<=before) {
      advanced=FALSE;
    }
    if(temple_key_count!=1||temple_key_queue[temple_key_read]!=TEMPLE_TEST_WORKER_KEY) {
      delivered=FALSE;
    }
    RsKbInitInterrupt();
  }
  Kill(worker);
  TempleTestAssert(progressed,"busy pump yields to another task");
  TempleTestAssert(advanced,"busy pump advances the timer");
  TempleTestAssert(delivered,"busy pump receives worker input messages");
}

legacy_void TempleTestTimerCadence()
{
  legacy_s64 start,elapsed;
  legacy_u32 noreg expected;
  RsDosTimerSetupInterrupt();
  start=cnts.jiffies;
  while((cnts.jiffies-start)<TEMPLE_TEST_TIMER_SECONDS*JIFFY_FREQ) {
    // Exercise busy polling; sleeping between ticks can hide clock rollbacks.
    TempleTimerPump();
    Yield;
  }
  TempleTimerPump();
  elapsed=cnts.jiffies-start;
  expected=elapsed*TEMPLE_TIMER_TICKS_PER_SECOND/JIFFY_FREQ;
  Print("Timer cadence: %d ticks in %d jiffies (%d expected).\n",temple_timer_counter,elapsed,
      expected);
  TempleTestAssert(temple_timer_counter+TEMPLE_TEST_TIMER_TOLERANCE_TICKS>=expected&&
          temple_timer_counter<=expected+TEMPLE_TEST_TIMER_TOLERANCE_TICKS,
      "busy timer follows elapsed real time");
  RsDosTimerShutdown();
}

legacy_void TempleTestMouse()
{
  legacy_s64 pixel,logical,first,last;
  RsDosMouseSetMinmax(0,0,TEMPLE_LOGICAL_WIDTH-1,TEMPLE_LOGICAL_HEIGHT-1);
  TempleTestAssert(TempleMouseX(-1)==0&&TempleMouseY(-1)==0&&
          TempleMouseX(TEMPLE_SCREEN_WIDTH)==TEMPLE_LOGICAL_WIDTH-1&&
          TempleMouseY(TEMPLE_SCREEN_HEIGHT)==TEMPLE_LOGICAL_HEIGHT-1,
      "off-screen mouse positions clamp to the game edges");
  for(logical=0;logical<TEMPLE_LOGICAL_WIDTH;logical++) {
    first=(logical*TEMPLE_SCREEN_WIDTH+TEMPLE_LOGICAL_WIDTH-1)/TEMPLE_LOGICAL_WIDTH;
    last=((logical+1)*TEMPLE_SCREEN_WIDTH+TEMPLE_LOGICAL_WIDTH-1)/TEMPLE_LOGICAL_WIDTH;
    for(pixel=first;pixel<last;pixel++) {
      TempleTestAssert(TempleMouseX(pixel)==logical,"mouse hits every displayed column");
    }
    RsDosMouseSetPosition(logical,0);
    TempleTestAssert(ms.pos.x==first&&ms.pos.y==0&&TempleMouseX(ms.pos.x)==logical,
        "mouse column warp selects the first displayed pixel and round trips");
  }
  for(logical=0;logical<TEMPLE_LOGICAL_HEIGHT;logical++) {
    first=(logical*TEMPLE_SCREEN_HEIGHT+TEMPLE_LOGICAL_HEIGHT-1)/TEMPLE_LOGICAL_HEIGHT;
    last=
        ((logical+1)*TEMPLE_SCREEN_HEIGHT+TEMPLE_LOGICAL_HEIGHT-1)/TEMPLE_LOGICAL_HEIGHT;
    for(pixel=first;pixel<last;pixel++) {
      TempleTestAssert(TempleMouseY(pixel)==logical,"mouse hits every displayed row");
    }
    RsDosMouseSetPosition(0,logical);
    TempleTestAssert(ms.pos.x==0&&ms.pos.y==first&&TempleMouseY(ms.pos.y)==logical,
        "mouse row warp selects the first displayed pixel and round trips");
  }
  RsDosMouseSetPosition(-1,-1);
  TempleTestAssert(ms.pos.x==0&&ms.pos.y==0,"mouse warp clamps to the upper left corner");
  RsDosMouseSetPosition(TEMPLE_LOGICAL_WIDTH,TEMPLE_LOGICAL_HEIGHT);
  TempleTestAssert(TempleMouseX(ms.pos.x)==TEMPLE_LOGICAL_WIDTH-1&&
          TempleMouseY(ms.pos.y)==TEMPLE_LOGICAL_HEIGHT-1,
      "mouse warp clamps to the lower right corner");
}

legacy_void TempleTestNative()
{
  legacy_u16 noreg handle;
  legacy_u8 buffer[TEMPLE_TEST_BUFFER_SIZE];
  legacy_u8 *path;
  legacy_u32 noreg before;
  TempleTestAssert(sizeof(legacy_u16)==2&&sizeof(legacy_u32)==4&&sizeof(legacy_u8 *)==8,
      "fixed fields/native pointer sizes");
  path=TempleFilePath("c:\\mixed.res");
  TempleTestAssert(!StrCmp(path,"C:/MIXED.RES"),"DOS path normalization");
  Free(path);
  handle=RsDosFileOpen(TEMPLE_TEST_PATH,TEMPLE_FILE_CREATE);
  TempleTestAssert(handle!=TEMPLE_FILE_INVALID_HANDLE,"create");
  TempleTestAssert(RsDosFileWrite(handle,"ABCD",4)==4,"write");
  TempleTestAssert(!RsDosFileSeek(handle,1,TEMPLE_FILE_SEEK_BEGIN),"seek begin");
  TempleTestAssert(RsDosFileWrite(handle,"Z",1)==1,"overwrite");
  TempleTestAssert(!RsDosFileSeek(handle,2,TEMPLE_FILE_SEEK_END),"seek past end");
  TempleTestAssert(RsDosFileWrite(handle,"Q",1)==1,"sparse write");
  TempleTestAssert(!RsDosFileClose(handle),"persist");
  handle=RsDosFileOpen(TEMPLE_TEST_PATH,TEMPLE_FILE_OPEN_EXISTING);
  TempleTestAssert(handle!=TEMPLE_FILE_INVALID_HANDLE,"reopen");
  MemSet(buffer,TEMPLE_BYTE_MASK,sizeof(buffer));
  TempleTestAssert(RsDosFileRead(handle,buffer,sizeof(buffer))==7,"short read");
  TempleTestAssert(!MemCmp(buffer,"AZCD\0\0Q",7),"persisted bytes and zero-filled gap");
  TempleTestAssert(RsDosFileRead(handle,buffer,1)==0,"EOF");
  TempleTestAssert(
      RsDosFileSeek(handle,-1,TEMPLE_FILE_SEEK_BEGIN)==-1,"negative seek rejected");
  TempleTestAssert(RsDosFileError()==TEMPLE_FILE_INVALID_SEEK,"seek error code");
  TempleTestAssert(!RsDosFileError(),"error read clears status");
  RsDosFileClose(handle);
  TempleTestFileSearch();
  TempleTestAssert(!RsDosFileRemove(TEMPLE_TEST_PATH),"delete test file");
  TempleTestAssert(!FileFind(TEMPLE_TEST_PATH),"file removed");
  TempleTestAssert(RsDosFileOpen(TEMPLE_TEST_PATH,TEMPLE_FILE_OPEN_EXISTING)==0,
      "DOS missing-file handle contract");
  TempleTestAssert(RsDosFileError()!=0&&!RsDosFileError(),"missing-file status");
  TempleTestAssert(RsDosFileRead(TEMPLE_FILE_INVALID_HANDLE,buffer,sizeof(buffer))==0&&
          RsDosFileError()!=0,
      "DOS invalid-handle read contract");
  TempleTestMouse();
  TempleTestAssert(TempleInputKey(0,SC_F11)==TEMPLE_TEST_KEY_F11,"BIOS function-key mapping");
  TempleTestAssert(TempleInputKey(0,SC_F9)==TEMPLE_TEST_KEY_F9,"unmodified F9 mapping");
  TempleTestAssert(TempleInputKey(0,SC_F9|SCF_CTRL)==TEMPLE_TEST_KEY_CTRL_F9,
      "Ctrl+F9 MPH shortcut mapping");
  TempleTestAssert(!TempleInputKey(0,SC_F9|SCF_CTRL|SCF_SHIFT)&&
          !TempleInputKey(0,SC_F9|SCF_CTRL|SCF_ALT),
      "MPH shortcut requires Ctrl without Shift or Alt");
  RsDosTimerSetupInterrupt();
  TempleTestAssert(RsDosTimerRegisterCallback(&TempleTestCallback),"register callback");
  Sleep(TEMPLE_TEST_TICK_WAIT_MS);
  TempleTimerPump();
  TempleTestAssert(
      temple_test_callback_ticks>0&&temple_test_audio_ticks==temple_test_callback_ticks,
      "timer/audio cadence");
  TempleTestPumpScheduling();
  RsDosTimerSetCallbacksSuspended(TRUE);
  before=temple_test_callback_ticks;
  Sleep(TEMPLE_TEST_TICK_WAIT_MS);
  TempleTimerPump();
  TempleTestAssert(temple_test_callback_ticks==before,"suspended callbacks");
  RsDosTimerUnregisterCallback(&TempleTestCallback);
  RsDosTimerShutdown();
  TempleTestTimerCadence();
  Print("Native file/input/timer/scheduling tests passed.\n");
}

TempleTestNative();
