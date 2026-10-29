#include "extended_validation.h"

#include <stdio.h>
#include "track_objects.h"

int extended_track_validation_message(int status, unsigned column, unsigned row, char *buffer,
									  size_t size)
{
	if (!buffer || !size) {
		return 0;
	}
	buffer[0] = '\0';
	if (status == TRACK_SETUP_OK) {
		return 0;
	}

	const char *cause;
	const char *hint;
	int show_location = 1;
	char unknown[64];
	switch (status) {
		case TRACK_SETUP_NO_START_FINISH:
			cause = "No start / finish element.";
			hint = "Add exactly one start / finish.";
			show_location = 0;
			break;
		case TRACK_SETUP_INTERNAL_ERROR:
			cause = "The route could not be compiled.";
			hint = "Check the track and try again.";
			show_location = 0;
			break;
		case TRACK_SETUP_MANY_START_FINISH:
			cause = "More than one start / finish.";
			hint = "Keep exactly one start / finish.";
			break;
		case TRACK_SETUP_ELEMENT_MISMATCH:
			cause = "Track pieces do not connect.";
			hint = "Check their directions and types.";
			break;
		case TRACK_SETUP_WRONG_WAY:
			cause = "A track piece faces the wrong way.";
			hint = "Rotate or replace that piece.";
			break;
		case TRACK_SETUP_MANY_ELEMENTS:
			cause = "This track has too many elements.";
			hint = "Reduce the number of road pieces.";
			break;
		case TRACK_SETUP_NO_PATH:
			cause = "The track has no complete route.";
			hint = "Connect back to start / finish.";
			break;
		case TRACK_SETUP_MANY_PATHS:
			cause = "This track has too many branches.";
			hint = "Simplify the branching roads.";
			break;
		case TRACK_SETUP_NO_RUNWAY:
			cause = "A jump needs a longer runway.";
			hint = "Add straight road before the ramp.";
			break;
		case TRACK_SETUP_LONG_JUMP:
			cause = "The jump crosses too many tiles.";
			hint = "Shorten the gap between ramps.";
			break;
		case TRACK_SETUP_TERRAIN_MISMATCH:
			cause = "Terrain edges do not match.";
			hint = "Check the terrain at this position.";
			break;
		default:
			snprintf(unknown, sizeof(unknown), "Track validation error %d.", status);
			cause = unknown;
			hint = "Check the track and try again.";
			show_location = 0;
			break;
	}

	char location[64] = "";
	if (show_location) {
		snprintf(location, sizeof(location), "Column %llu, row %llu.]",
				 (unsigned long long)column + 1ULL, (unsigned long long)row + 1ULL);
	}
	int length = snprintf(buffer, size, "Cannot start this track.]%s]%s%s]", cause, location, hint);
	if (length < 0 || (size_t)length >= size) {
		buffer[0] = '\0';
		return 0;
	}
	return 1;
}
