// Shared argument parsing is used for every option. Existing calls keep their defaults.
#define TEMPLE_LAUNCH_MAX_ARGUMENTS 32767
#define TEMPLE_LAUNCH_FIXED_ARGUMENTS 4

legacy_u8 TempleLaunchSpace(legacy_s8 value)
{
  return value==' ' ||value=='\t' ||value=='\r' ||value=='\n';
}

// Split a writable copy in place; quotes group text and backslashes stay literal.
// The caller reserves at least StrLen(text)+count+1 pointers, including the trailing NULL.
legacy_s32 TempleLaunchSplit(legacy_s8 *text,legacy_s8 **arguments,legacy_s32 count)
{
  legacy_s8 *source=text,*destination=text;
  legacy_s8 noreg quote=0,noreg value;
  while(*source) {
    while(TempleLaunchSpace(*source)) {
      source++;
    }
    if(!*source) {
      break;
    }
    if(count>=TEMPLE_LAUNCH_MAX_ARGUMENTS) {
      PrintErr("Too many Restunts arguments.\n");
      return-1;
    }
    arguments[count++]=destination;
    while(*source) {
      value=*source++;
      if(!quote&&TempleLaunchSpace(value)) {
        break;
      }
      if(value=='\"' ||value=='\'') {
        if(!quote) {
          quote=value;
        } else if(quote==value) {
          quote=0;
        } else {
          *destination++=value;
        }
      } else {
        *destination++=value;
      }
    }
    if(quote) {
      PrintErr("Unclosed quote in Restunts options.\n");
      return-1;
    }
    *destination++=0;
  }
  arguments[count]=NULL;
  return count;
}

// argv[0] is the program name, just as on DOS, Amiga, and SDL3.
legacy_s16 RestuntsArgs(legacy_u8 *data_directory,legacy_s32 argument_count,legacy_s8 **arguments)
{
  legacy_u8 *previous_directory,*restore_directory;
  legacy_u8 noreg previous_drive;
  legacy_s32 noreg index;
  legacy_s16 noreg information;
  if(argument_count<1||argument_count>TEMPLE_LAUNCH_MAX_ARGUMENTS||!arguments) {
    PrintErr("Invalid Restunts argument list.\n");
    return 1;
  }
  for(index=0;index<argument_count;index++) {
    if(!arguments[index]) {
      PrintErr("Missing Restunts argument %d.\n",index);
      return 1;
    }
  }
  // Help/version must work without game resources, video, or audio initialization.
  information=RsCommandLinePrintInfo(argument_count,arguments);
  if(information!=COMMAND_LINE_CONTINUE) {
    return information;
  }
  previous_directory=StrNew(Fs->cur_dir);
  previous_drive=Fs->cur_dv->drv_let;
  if(!Cd(data_directory)) {
    PrintErr("Cannot open Restunts data directory: %s\n",data_directory);
    Free(previous_directory);
    return 1;
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

legacy_s16 Restunts(legacy_u8 *data_directory="C:/DATA",legacy_u8 skip_intro=FALSE,
    legacy_u8 amiga_grass=FALSE,legacy_s8 *options="")
{
  legacy_s8 *option_copy=StrNew(options),**arguments;
  legacy_s32 noreg argument_count=1;
  legacy_s16 noreg status=1;
  arguments=MAlloc((StrLen(option_copy)+TEMPLE_LAUNCH_FIXED_ARGUMENTS)*sizeof(legacy_s8 *));
  arguments[0]="RESTUNTS";
  if(skip_intro) {
    arguments[argument_count++]="--nointro";
  }
  if(amiga_grass) {
    arguments[argument_count++]="/amigagrass";
  }
  argument_count=TempleLaunchSplit(option_copy,arguments,argument_count);
  if(argument_count>=0) {
    status=RestuntsArgs(data_directory,argument_count,arguments);
  }
  Free(arguments);
  Free(option_copy);
  return status;
}
