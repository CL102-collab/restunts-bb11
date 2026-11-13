#ifndef RESTUNTS_SDL3_MT32_ROMS_H
#define RESTUNTS_SDL3_MT32_ROMS_H

#include "../../c/legacy.h"
#include <stddef.h>
#include <mt32emu/c_interface/c_interface.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The returned JSON has {"sets":[{"id":"...","label":"..."}],"error":"..."}.
 * Only complete, compatible ROM sets appear. Filenames do not identify ROMs.
 * Release the returned SDL-allocated string with mt32_rom_inventory_free(). */
legacy_char *mt32_rom_inventory_json(const legacy_char *directory);
void mt32_rom_inventory_free(legacy_char *inventory);

/* Add the chosen images to a fresh Munt context; the caller opens the synth.
 * An omitted model is accepted only when exactly one complete set is present.
 * Call mt32_rom_release AFTER freeing the Munt context, including failed opens:
 * Munt borrows the selected ROM buffers until the context is destroyed. */
legacy_int mt32_rom_load(mt32emu_context context, const legacy_char *directory,
						 const legacy_char *model, legacy_char *error, size_t error_size);
void mt32_rom_release(mt32emu_context context);

#ifdef __cplusplus
}
#endif

#endif
