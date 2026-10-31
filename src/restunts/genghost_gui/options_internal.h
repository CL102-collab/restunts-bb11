#ifndef RESTUNTS_GENGHOST_GUI_OPTIONS_INTERNAL_H
#define RESTUNTS_GENGHOST_GUI_OPTIONS_INTERNAL_H

#include "options.h"

/* Common car-id validation shared by legacy and extended replay readers. */
wxString genghost_gui_header_car(const legacy_u8 *header);

#endif
