#ifndef RESTUNTS_DRIVING_RULES_H
#define RESTUNTS_DRIVING_RULES_H

#include "legacy.h"

/* Stable recording bits, ordered as the launcher's Driving rules choices. */
#define DRIVING_RULE_POWER_GEAR 0x0001U
#define DRIVING_RULE_LEGACY_COLLISION 0x0002U
#define DRIVING_RULE_LEFT_CORNER_BIAS 0x0004U
#define DRIVING_RULE_DUAL_WAY_SWITCHING 0x0008U
#define DRIVING_RULE_CHICANE_FAST_GRASS 0x0010U
#define DRIVING_RULE_AMIGA_GRASS 0x0020U
#define DRIVING_RULE_OWOOT 0x0040U
#define DRIVING_RULE_KNOWN_MASK 0x007fU

/* Read the configured simulation switches without changing game behavior. */
legacy_u16 driving_rules_current(void);

#endif
