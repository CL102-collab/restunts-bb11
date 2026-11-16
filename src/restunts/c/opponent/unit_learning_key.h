#ifndef RESTUNTS_UNIT_LEARNING_KEY_H
#define RESTUNTS_UNIT_LEARNING_KEY_H

#include "unit_learning_cache.h"

struct UNIT_ROUTE;
/* Immutable simulation identity, captured before learned recipes are applied. */
legacy_int unit_learning_key_current(const struct UNIT_ROUTE *route, struct UNIT_LEARNING_KEY *key);

#endif
