#ifndef RESTUNTS_EXTENDED_TRACK_H
#define RESTUNTS_EXTENDED_TRACK_H

#include "legacy.h"

#define EXTENDED_TRACK_MAX_DIMENSION 90U
#define EXTENDED_TRACK_MAX_CELLS 8100U
#define EXTENDED_TRACK_ROUTE_CAPACITY 16201U
#define EXTENDED_TRACK_CELL_OBJECT_MAX 16U

#ifdef RESTUNTS_SDL3
#include "cstrk.h"

int extended_track_active(void);
int extended_track_width(void);
int extended_track_height(void);
unsigned extended_track_route_capacity(void);
const struct CSTRK_TRACK *extended_track_model(void);
int extended_track_load(const uint8_t *data, size_t size, char *error, size_t error_size);
int extended_track_activate(const struct CSTRK_TRACK *track, char *error, size_t error_size);
void extended_track_unload(void);
/* All lookup rows use the editor's north-to-south map convention. */
const struct CSTRK_OBJECT *extended_track_object_at(int column, int row);
const struct CSTRK_OBJECT *extended_track_object_origin_at(int column, int row);
/* Includes unclaimed cells inside each piece's full footprint rectangle. */
legacy_u16 extended_track_objects_in_cell(legacy_s16 column, legacy_s16 row,
										  const struct CSTRK_OBJECT **objects, legacy_u16 capacity);
const struct CSTRK_OBJECT *extended_track_object_for_id(int column, int row, unsigned id);
unsigned extended_track_object_id(const struct CSTRK_OBJECT *object);
/* Graphical height at a map cell's fractional point, measured in 1/1024 tile. */
legacy_s16 extended_track_elevation(int column, int row, int local_x, int local_y);
/* Called after init_trackdata has installed the ordinary pointers. */
void extended_track_rebind(void);
#else
#define extended_track_active() 0
#define extended_track_width() 30
#define extended_track_height() 30
#define extended_track_route_capacity() 901U
#endif

#endif
