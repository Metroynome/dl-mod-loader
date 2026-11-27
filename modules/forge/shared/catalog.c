#include "travel.h"

/* Native LevelBox catalog, checked against NTSC-U retail mission records and text. */
typedef struct forgePlanet {
	/* 0x00 */ int id;
	/* 0x04 */ int first;
	/* 0x08 */ int count;
	/* 0x0c */ const char* name;
} forgePlanet_t;

static const forgePlanet_t planets[] = {
	{1, 1, 45, "DreadZone Station"},
	{2, 46, 7, "Catacrom Four"},
	{4, 53, 7, "Sarathos"},
	{5, 60, 11, "Kronos"},
	{6, 71, 7, "Shaar"},
	{7, 78, 7, "The Valix Belt"},
	{8, 85, 7, "Orxon"},
	{10, 92, 5, "Planet Torval"},
	{11, 97, 7, "Stygia"},
	{13, 104, 7, "Maraxus"},
	{14, 111, 4, "Ghost Station"},
	{15, 116, 4, "DreadZone Station Interior"},
};

static const char* const missions[] = {
	"Default", /* 0 */
	"DreadZone Qualification", /* 1 */
	"DreadZone Qualification", /* 2 */
	"DreadZone Qualification", /* 3 */
	"DreadZone Qualification", /* 4 */
	"DreadZone Qualification", /* 5 */
	"DreadZone Qualification", /* 6 */
	"DreadZone Qualification", /* 7 */
	"Default", /* 8 */
	"Boot Camp ", /* 9 */
	"The Tower of Power ", /* 10 */
	"Grist for the Mill ", /* 11 */
	"The Big Sleep", /* 12 */
	"Perfect Chrome Finish", /* 13 */
	"Manic Speed Demon", /* 14 */
	"Close and Personal", /* 15 */
	"Climb the Tower of Power", /* 16 */
	"Zombie Attack", /* 17 */
	"Static Deathtrap", /* 18 */
	"Marathon", /* 19 */
	"Heavy Metal", /* 20 */
	"Endzone", /* 21 */
	"Murphy's Law", /* 22 */
	"Higher Ground", /* 23 */
	"Reactor Battle", /* 24 */
	"The Corkscrew", /* 25 */
	"Air Drop", /* 26 */
	"Dynamite Baseball", /* 27 */
	"Default", /* 28 */
	"Swarmer Surprise", /* 29 */
	"Default", /* 30 */
	"Default", /* 31 */
	"Default", /* 32 */
	"Default", /* 33 */
	"Default", /* 34 */
	"Accelerator", /* 35 */
	"Default", /* 36 */
	"Less is More", /* 37 */
	"Advanced Qualifier", /* 38 */
	"Eviscerator Battle", /* 39 */
	"Ace Hardlight Battle", /* 40 */
	"Default", /* 41 */
	"Default", /* 42 */
	"Default", /* 43 */
	"Default", /* 44 */
	"Default", /* 45 */
	"Valley of Heroes ", /* 46 */
	"On the Prowl ", /* 47 */
	"All Aboard the Landstalker", /* 48 */
	"Rise Up ", /* 49 */
	"Hoverbike Madness", /* 50 */
	"Destruction Derby ", /* 51 */
	"Monuments of Death ", /* 52 */
	"Alien Soil", /* 53 */
	"Sarathos Sprint", /* 54 */
	"Where the Leviathans Roam", /* 55 */
	"King of the Leviathans", /* 56 */
	"Search and Destroy", /* 57 */
	"Check Me Out", /* 58 */
	"Swamp Fever", /* 59 */
	"Infiltrate the Cathedral ", /* 60 */
	"Introducing Shellshock ", /* 61 */
	"Enemies are Such a Grind ", /* 62 */
	"Infiltrate the Cathedral, Part 2 ", /* 63 */
	"Shellshock Returns!", /* 64 */
	"Fight to the Cathedral ", /* 65 */
	"Showdown with Shellshock ", /* 66 */
	"Default", /* 67 */
	"Night Flight ", /* 68 */
	"Inverted Action ", /* 69 */
	"Dark City Arenas ", /* 70 */
	"Return to the Deathbowl", /* 71 */
	"Landstalker Stalkin'", /* 72 */
	"As the Wrench Turns", /* 73 */
	"Assault on the Frozen Tundra", /* 74 */
	"Shoot to Kill", /* 75 */
	"Whack-A-Swarmer", /* 76 */
	"Robots in a Barrel", /* 77 */
	"Lost and Found", /* 78 */
	"Space Ace", /* 79 */
	"Power Up!", /* 80 */
	"Let the Light Shine Through", /* 81 */
	"Time Extension!", /* 82 */
	"Your Land is my Land", /* 83 */
	"Fire in the Sky", /* 84 */
	"Labyrinth of Death", /* 85 */
	"Node Overload", /* 86 */
	"Refinery Ambush", /* 87 */
	"Chains of Villainy", /* 88 */
	"Swarming the Gauntlet", /* 89 */
	"Scoring with the Blarg", /* 90 */
	"Raritanium Race", /* 91 */
	"Hoverbike or Bust", /* 92 */
	"Seek and Destroy", /* 93 */
	"The Turn On", /* 94 */
	"Blast of Fresh Air", /* 95 */
	"Turret Trouble", /* 96 */
	"Junking the Jammers", /* 97 */
	"Energy Collector", /* 98 */
	"Shields Up!", /* 99 */
	"Shield Survival", /* 100 */
	"Land Stalking", /* 101 */
	"Hovership Acrobatics", /* 102 */
	"Hovership Carnage", /* 103 */
	"Jail Break", /* 104 */
	"Total Control", /* 105 */
	"Leave No Man Behind", /* 106 */
	"Showdown in Delta Block", /* 107 */
	"Project Desert Swarm", /* 108 */
	"Speed Trap", /* 109 */
	"Spider on a Wire", /* 110 */
	"The Grinding Chase", /* 111 */
	"Running the Gauntlet", /* 112 */
	"Prison Break", /* 113 */
	"Escape", /* 114 */
	"Payback", /* 115 */
	"Destroy the East Generator", /* 116 */
	"Destroy the West Generator", /* 117 */
	"Destroy the Central Core", /* 118 */
	"Defeat Gleeman Vox", /* 119 */
};

/*
 * NAME : forgePlanetCount
 * DESCRIPTION : Count the campaign/co-op planet choices.
 * NOTES :
 * ARGS : 
 * RETURN : Planet count.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int forgePlanetCount(void)
{
	return sizeof(planets) / sizeof(planets[0]);
}

/*
 * NAME : forgePlanetId
 * DESCRIPTION : Resolve a menu index to its unprefixed planet ID.
 * NOTES :
 * ARGS : Menu index.
 * RETURN : Planet ID, or zero.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int forgePlanetId(int index)
{
	return index >= 0 && index < forgePlanetCount() ? planets[index].id : 0;
}

/*
 * NAME : forgePlanetIndex
 * DESCRIPTION : Validate an unprefixed planet ID.
 * NOTES :
 * ARGS : Planet ID.
 * RETURN : Menu index, or minus one.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int forgePlanetIndex(int planet)
{
	int i;
	for (i = 0; i < forgePlanetCount(); ++i)
		if (planets[i].id == planet)
			return i;
	return -1;
}

/*
 * NAME : forgeMissionCount
 * DESCRIPTION : Count native relative missions for a planet.
 * NOTES :
 * ARGS : Unprefixed planet ID.
 * RETURN : Mission count, or zero.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int forgeMissionCount(int planet)
{
	int i = forgePlanetIndex(planet);
	return i >= 0 ? planets[i].count : 0;
}

/*
 * NAME : forgePlanetName
 * DESCRIPTION : Name a planet selector entry.
 * NOTES :
 * ARGS : Menu index.
 * RETURN : Native English planet label.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
const char* forgePlanetName(int index)
{
	return index >= 0 && index < forgePlanetCount() ? planets[index].name : "";
}

/*
 * NAME : forgeMissionName
 * DESCRIPTION : Name a native relative mission.
 * NOTES :
 * ARGS : Planet ID and relative mission index.
 * RETURN : Native English label, or an empty string.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
const char* forgeMissionName(int planet, int mission)
{
	int i = forgePlanetIndex(planet);
	if (i < 0 || mission < 0 || mission >= planets[i].count)
		return "";
	return missions[planets[i].first + mission];
}

