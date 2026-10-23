// Run from a prepared PORT with bundled CARS, only on the disposable guest disk.
#include "LOAD.HC"

#define TEMPLE_CAR_TEST_DIRECTORY "C:/CARTEST"
#define TEMPLE_CAR_TEST_CAPACITY 32
#define TEMPLE_CAR_TEST_ID_BYTES 5
#define TEMPLE_CAR_TEST_CHUNK_BYTES 4096
#define TEMPLE_CAR_TEST_LOOSE "C:/CARTEST/CARTST1.RES"
#define TEMPLE_CAR_TEST_DATA_FOLDER "C:/CARTEST/CARS/CARTST1.RES"

legacy_void TempleCarsAssert(legacy_u8 condition,legacy_u8 *message)
{
  if(!condition) {
    PrintErr("Car resources: %s\n",message);
    throw('CarFiles');
  }
}

legacy_u16 TempleCarsCount(legacy_s8 *requested)
{
  legacy_s8 ids[TEMPLE_CAR_TEST_CAPACITY*TEMPLE_CAR_TEST_ID_BYTES];
  legacy_u16 noreg count=RsFileFindCarIds(ids,TEMPLE_CAR_TEST_CAPACITY);
  legacy_u16 noreg index,noreg matches=0;
  for(index=0;index<count;index++) {
    if(!StrICmp(ids+index*TEMPLE_CAR_TEST_ID_BYTES,requested)) {
      matches++;
    }
  }
  return matches;
}

legacy_void TempleCarsCompare(legacy_s8 *filename)
{
  legacy_u8 *path=MStrPrint("%s/CARS/%s",temple_program_directory,filename);
  legacy_s64 length=0,offset=0;
  legacy_u8 *expected=FileRead(path,&length);
  legacy_u8 buffer[TEMPLE_CAR_TEST_CHUNK_BYTES];
  legacy_u16 noreg handle=RsFileOpenResource(filename);
  legacy_u16 noreg amount;
  TempleCarsAssert(expected!=NULL&&length>0,"bundled reference file");
  TempleCarsAssert(handle!=TEMPLE_FILE_INVALID_HANDLE,"automatic bundled resource lookup");
  while(offset<length) {
    amount=MinU64(TEMPLE_CAR_TEST_CHUNK_BYTES,length-offset);
    TempleCarsAssert(RsDosFileRead(handle,buffer,amount)==amount,"complete resource read");
    TempleCarsAssert(
        !MemCmp(expected+offset,buffer,amount),"resource bytes match bundled file");
    offset+=amount;
  }
  TempleCarsAssert(!RsDosFileRead(handle,buffer,1),"resource ends at expected size");
  RsDosFileClose(handle);
  Free(expected);
  Free(path);
}

legacy_void TempleCarsPriority(legacy_s8 expected)
{
  legacy_u8 value=0;
  legacy_u16 noreg handle=RsFileOpenResource("CARTST1.RES");
  TempleCarsAssert(handle!=TEMPLE_FILE_INVALID_HANDLE,"override resource lookup");
  TempleCarsAssert(RsDosFileRead(handle,&value,1)==1,"override resource read");
  TempleCarsAssert(value==expected,"loose and folder resource priority");
  RsDosFileClose(handle);
  TempleCarsAssert(TempleCarsCount("TST1")==1,"duplicate car IDs appear once");
}

legacy_void TempleCarsTest()
{
  legacy_u8 *previous=DirCur;
  legacy_u8 *base_loose=MStrPrint("%s/CARTST1.RES",temple_program_directory);
  legacy_u8 *base_folder=MStrPrint("%s/CARS/CARTST1.RES",temple_program_directory);
  TempleCarsAssert(!StrICmp(temple_program_directory,"C:/PORT"),"actual loaded code directory");
  TempleCarsAssert(DirMk(TEMPLE_CAR_TEST_DIRECTORY),"separate game directory");
  TempleCarsAssert(DirMk(TEMPLE_CAR_TEST_DIRECTORY "/CARS"),"private car folder");
  TempleCarsAssert(Cd(TEMPLE_CAR_TEST_DIRECTORY),"change away from the program directory");
  TempleCarsAssert(TempleCarsCount("MCMU")==1,"McMurtry discovered from the program folder");
  TempleCarsAssert(TempleCarsCount("TERR")==1,"Tesla discovered from the program folder");
  TempleCarsCompare("CARMCMU.RES");
  TempleCarsCompare("CARTERR.RES");
  TempleCarsCompare("STDAMCMU.VSH");
  TempleCarsCompare("STDATERR.VSH");
  TempleCarsCompare("STDBMCMU.VSH");
  TempleCarsCompare("STDBTERR.VSH");
  TempleCarsCompare("STMCMU.3SH");
  TempleCarsCompare("STTERR.3SH");
  FileWrite(base_folder,"B",1);
  FileWrite(TEMPLE_CAR_TEST_DATA_FOLDER,"D",1);
  FileWrite(base_loose,"P",1);
  FileWrite(TEMPLE_CAR_TEST_LOOSE,"L",1);
  TempleCarsPriority('L');
  Del(TEMPLE_CAR_TEST_LOOSE);
  TempleCarsPriority('P');
  Del(base_loose);
  TempleCarsPriority('D');
  Del(TEMPLE_CAR_TEST_DATA_FOLDER);
  TempleCarsPriority('B');
  Del(base_folder);
  TempleFileShutdown();
  Cd(previous);
  Free(previous);
  Free(base_folder);
  Free(base_loose);
  Print("TempleOS bundled-car discovery, resource reads, and override priority passed.\n");
}

TempleCarsTest();
