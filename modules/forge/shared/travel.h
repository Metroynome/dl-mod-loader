#ifndef DL_FORGE_TRAVEL_H
#define DL_FORGE_TRAVEL_H

#include "module.h"

#ifdef FORGE_TRAVEL_ENABLED
void forgeTravelInit(const moduleApi_t* api);
void forgeTravelDisable(void);
void forgeTravelUpdate(void);
u32 forgeTravelContext(void);
u32 forgeTravelState(void);
u32 forgeTravelError(void);
int forgeTravelAvailable(void);
int forgeTravelRequest(int planet, int mission);
#else
#define forgeTravelInit(api) ((void)(api))
#define forgeTravelDisable() ((void)0)
#define forgeTravelUpdate() ((void)0)
#define forgeTravelContext() (0)
#define forgeTravelState() (0)
#define forgeTravelError() (0)
#define forgeTravelAvailable() (0)
#define forgeTravelRequest(planet, mission) (FORGE_ERROR_CONTEXT)
#endif
#ifdef FORGE_SINGLEPLAYER
int forgeSingleplayerTarget(int current, int planet);
#else
#define forgeSingleplayerTarget(current, planet) (0)
#endif
#ifdef FORGE_COOPERATIVE
int forgeCooperativeTarget(int current, int planet);
#else
#define forgeCooperativeTarget(current, planet) (0)
#endif
int forgePlanetCount(void);
int forgePlanetId(int index);
int forgePlanetIndex(int planet);
int forgeMissionCount(int planet);
const char* forgePlanetName(int index);
const char* forgeMissionName(int planet, int mission);

#endif
