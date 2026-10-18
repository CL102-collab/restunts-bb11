#!/usr/bin/env python3
"""Exercise native HolyC argument construction with host sanitizers.

Only the two parser helpers and Restunts wrapper are compiled; RestuntsArgs is
replaced by a capture fixture. TEST_LAUNCH.HC validates the full shared parsers,
help/version path, and HolyC semantics in the real TempleOS guest.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "LAUNCH.HC").read_text()
helpers, rest = source.split("// argv[0] is the program name", 1)
wrapper = rest[rest.index("legacy_s16 Restunts("):]
wrapper = wrapper.replace('legacy_u8 *data_directory="C:/DATA"', 'legacy_u8 *data_directory')
wrapper = wrapper.replace('legacy_u8 skip_intro=FALSE', 'legacy_u8 skip_intro')
wrapper = wrapper.replace('legacy_u8 amiga_grass=FALSE', 'legacy_u8 amiga_grass')
wrapper = wrapper.replace('legacy_s8 *options=""', 'legacy_s8 *options')
source = re.sub(r"\bnoreg\b", "", helpers + wrapper)
prefix = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef char legacy_s8;
typedef char legacy_u8;
typedef int16_t legacy_s16;
typedef int32_t legacy_s32;
#define FALSE 0
#define TRUE 1
#define PrintErr(...) fprintf(stderr,__VA_ARGS__)
#define StrNew strdup
#define StrLen strlen
#define MAlloc malloc
#define Free free
static int captured_count;
static char captured[6000][128];
static legacy_s16 RestuntsArgs(legacy_u8 *directory,legacy_s32 count,legacy_s8 **arguments)
{
  assert(!strcmp(directory,"C:/DATA"));
  assert(count>0 && count<6000);
  assert(!arguments[count]);
  captured_count=count;
  for(int i=0;i<count;i++) {
    assert(strlen(arguments[i])<sizeof(captured[i]));
    strcpy(captured[i],arguments[i]);
  }
  return 42;
}
'''
tests = r'''
int main(void)
{
  assert(Restunts("C:/DATA",FALSE,FALSE,"")==42);
  assert(captured_count==1 && !strcmp(captured[0],"RESTUNTS"));
  assert(Restunts("C:/DATA",TRUE,TRUE,"")==42);
  assert(captured_count==3);
  assert(!strcmp(captured[1],"--nointro") && !strcmp(captured[2],"/amigagrass"));
  assert(Restunts("C:/DATA",FALSE,FALSE,
      "--pg:off --lc:off --lcb:off /amigagrass --owoot /ns --fps:on --nointro")==42);
  assert(captured_count==9 && !strcmp(captured[6],"/ns"));
  assert(!strcmp(captured[7],"--fps:on") && !strcmp(captured[8],"--nointro"));
  assert(Restunts("C:/DATA",TRUE,TRUE,"--pg:off --pg:on --pg:off")==42);
  assert(captured_count==6 && !strcmp(captured[5],"--pg:off"));
  assert(Restunts("C:/DATA",FALSE,FALSE," \t\r\n \"--owoot\" '--pg:off' \"two words\" '' C:\\DATA ")==42);
  assert(captured_count==6);
  assert(!strcmp(captured[1],"--owoot") && !strcmp(captured[2],"--pg:off"));
  assert(!strcmp(captured[3],"two words") && !strcmp(captured[4],""));
  assert(!strcmp(captured[5],"C:\\DATA"));
  char *many=calloc(5000,12);
  assert(many);
  for(int i=0;i<5000;i++) strcat(many,"--pg:off ");
  assert(Restunts("C:/DATA",FALSE,FALSE,many)==42);
  assert(captured_count==5001 && !strcmp(captured[5000],"--pg:off"));
  free(many);
  captured_count=0;
  assert(Restunts("C:/DATA",FALSE,FALSE,"--pg:off \"--owoot")==1);
  assert(captured_count==0);
  char too_many[]="--owoot";
  char *arguments[1];
  assert(TempleLaunchSplit(too_many,arguments,TEMPLE_LAUNCH_MAX_ARGUMENTS)==-1);
  puts("TempleOS launcher argument construction checks passed.");
  return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="restunts-templeos-launch-") as temporary:
    path = Path(temporary)
    fixture = path / "launch.c"
    fixture.write_text(prefix + source + tests)
    executable = path / "launch"
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
                    str(fixture), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
