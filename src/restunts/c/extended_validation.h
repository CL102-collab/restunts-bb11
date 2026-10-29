#ifndef RESTUNTS_EXTENDED_VALIDATION_H
#define RESTUNTS_EXTENDED_VALIDATION_H

#include <stddef.h>

/* Format an SDL3 route failure for show_dialog. Coordinates use the editor's
 * zero-based map axes and are displayed one-based. Every displayed line ends
 * with ']'. Returns 1 for a complete message, or 0 for success/no usable output;
 * an available buffer is empty if the complete message does not fit. */
int extended_track_validation_message(int status, unsigned column, unsigned row, char *buffer,
									  size_t size);

#endif
