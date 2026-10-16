// Load the native game. Call Restunts("C:/DATA") from a fresh task.
#include "LEGACY.HH"
#include "LIBC.HC"
#include "generated/TYPES.HH"
#include "generated/DECLS.HH"
#include "NATIVE.HH"
#include "RASTER.HH"
#include "generated/DATA.HC"
#include "MATERIAL.HC"
#include "PALETTE.HC"
#include "RASTER.HC"
#include "VIDEO.HC"
#include "FILE.HC"
#include "INPUT.HC"
#include "TIMER.HC"
#include "AUDIO.HC"
#include "RUNTIME.HC"
#include "generated/ALL.HC"

#define TEMPLE_LAUNCH_ARGUMENT_CAPACITY 3

legacy_s16 Restunts(
    legacy_u8 *data_directory="C:/DATA",legacy_u8 skip_intro=FALSE,legacy_u8 amiga_grass=FALSE)
{
  legacy_u8 *previous_directory=StrNew(Fs->cur_dir);
  legacy_u8 noreg previous_drive=Fs->cur_dv->drv_let;
  legacy_s8 *arguments[TEMPLE_LAUNCH_ARGUMENT_CAPACITY];
  legacy_s16 noreg argument_count=1;
  legacy_u8 *restore_directory;
  if(!Cd(data_directory)) {
    PrintErr("Cannot open Restunts data directory: %s\n",data_directory);
    Free(previous_directory);
    return 1;
  }
  arguments[0]="RESTUNTS";
  if(skip_intro) {
    arguments[argument_count++]="--nointro";
  }
  if(amiga_grass) {
    arguments[argument_count++]="/amigagrass";
  }
  temple_exit_status=0;
  try {
    RsFullDataInitialize();
    TempleMaterialInit();
    audiodriverstring[0]='p';
    audiodriverstring[1]='c';
    temple_exit_status=RsRunMainMenuLoop(argument_count,arguments);
  } catch {
    if(Fs->except_ch==TEMPLE_EXIT_EXCEPTION) {
      Fs->catch_except=TRUE;
    } else {
      Fs->catch_except=TRUE;
      temple_exit_status=1;
      PutExcept();
    }
  }
  RsDosTimerShutdown();
  RsDosAudioShutdown();
  TempleFileShutdown();
  TempleVideoShutdown();
  RsKbExitHandler();
  restore_directory=MStrPrint("%c:%s",previous_drive,previous_directory);
  Cd(restore_directory);
  Free(restore_directory);
  Free(previous_directory);
  return temple_exit_status;
}
