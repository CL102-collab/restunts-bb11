#ifndef RESTUNTS_EXTENDED_GHOST_H
#define RESTUNTS_EXTENDED_GHOST_H

#include "csrpl.h"
#include "../replay/replay.h"

struct EXTENDED_GHOST_REPLAY {
	uint8_t *bytes;
	size_t size;
	struct CSRPL_VIEW view;
};

/* Selected ghost inputs remain independent of the player's recording. */
int extended_ghost_read(const legacy_s8 *path, struct EXTENDED_GHOST_REPLAY *out,
						struct GAMEINFO *config);
/* Decode the shared native replay metadata without taking ownership of bytes. */
legacy_int extended_ghost_decode(const legacy_u8 *bytes, size_t size, struct CSRPL_VIEW *view,
								 struct GAMEINFO *config);
void extended_ghost_free(struct EXTENDED_GHOST_REPLAY *replay);
int extended_ghost_matches_track(const struct EXTENDED_GHOST_REPLAY *replay);

#endif
