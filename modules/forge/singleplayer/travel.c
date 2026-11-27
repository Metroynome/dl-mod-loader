#include "../shared/travel.h"

/*
 * NAME : forgeSingleplayerTarget
 * DESCRIPTION : Keep campaign travel inside the campaign level range.
 * NOTES : Rejects co-op, multiplayer and front-end source levels.
 * ARGS : Current native level and unprefixed planet ID.
 * RETURN : Native target level, or zero on rejection.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int forgeSingleplayerTarget(int current, int planet)
{
	return current > 0 && current < 20 && forgePlanetIndex(current) >= 0 &&
	    forgePlanetIndex(planet) >= 0 ? planet : 0;
}
