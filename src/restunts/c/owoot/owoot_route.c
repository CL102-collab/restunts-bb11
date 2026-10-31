#include "owoot_route.h"
#include "../extended/extended_owoot_route.h"

legacy_s16 owoot_route_is_valid(struct CARSTATE *carstate, legacy_s16 allowed_jump)
{
	return extended_owoot_route_is_valid(carstate, allowed_jump);
}

legacy_s16 owoot_jump_is_valid(struct CARSTATE *carstate)
{
	return extended_owoot_jump_is_valid(carstate);
}
