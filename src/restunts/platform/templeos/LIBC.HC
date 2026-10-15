// The DOS game supplies its own string, allocation, and number routines.
// These are the remaining host services needed by generated code and TIMER.HC.
#include "LEGACY.HH"

#define TEMPLE_STDOUT 1
#define TEMPLE_STDERR 2

legacy_s64 stdout=TEMPLE_STDOUT;
legacy_s64 stderr=TEMPLE_STDERR;

legacy_u8 *RsMemmove(legacy_u8 *destination,legacy_u8 *source,legacy_u64 length)
{
  legacy_u64 index;
  if(destination>source&&destination<source+length) {
    index=length;
    while(index) {
      index--;
      destination[index]=source[index];
    }
  } else {
    MemCpy(destination,source,length);
  }
  return destination;
}

legacy_s32 RsFputs(legacy_u8 *text,legacy_s64 stream)
{
  no_warn stream;
  Print("%s",text);
  return StrLen(text);
}

legacy_void RsFree(legacy_u8 *pointer)
{
  Free(pointer);
}

legacy_u8 *RsRealloc(legacy_u8 *pointer,legacy_u64 size)
{
  legacy_u8 *resized;
  legacy_u64 copy_size;
  if(!size) {
    Free(pointer);
    return 0;
  }
  resized=MAlloc(size);
  if(!resized) {
    return 0;
  }
  if(pointer) {
    copy_size=MSize(pointer);
    if(copy_size>size) {
      copy_size=size;
    }
    MemCpy(resized,pointer,copy_size);
    Free(pointer);
  }
  return resized;
}
