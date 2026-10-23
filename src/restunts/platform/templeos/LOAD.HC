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

// Keep the loaded port directory even when the launcher selects other game data.
legacy_u8 *temple_program_directory=StrNew(__DIR__);
RsFileCarSetBase(temple_program_directory);

#include "LAUNCH.HC"
