#include "../shared/travel.h"

/*
 * NAME : forgeCooperativeTarget
 * DESCRIPTION : Keep co-op travel inside its native level range.
 * NOTES : Uses the same planet IDs and mission catalog as campaign.
 * ARGS : Current native level and unprefixed planet ID.
 * RETURN : Native target level with the co-op offset, or zero on rejection.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int forgeCooperativeTarget(int current, int planet)
{
	return current > 20 && current < 40 && forgePlanetIndex(current - 20) >= 0 &&
	    forgePlanetIndex(planet) >= 0 ? planet + 20 : 0;
}
