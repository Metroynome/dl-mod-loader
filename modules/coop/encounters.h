#ifndef DL_COOP_ENCOUNTERS_H
#define DL_COOP_ENCOUNTERS_H

#include "module.h"
#include "modules/online/include/online.h"

int coopEncountersUpdate(const moduleApi_t* loader, const onlineApi_t* online, int enabled);
void coopEncountersReset(const moduleApi_t* loader);

#endif
