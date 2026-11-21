#ifndef DL_COOP_WORLD_H
#define DL_COOP_WORLD_H

#include "module.h"
#include "modules/online/include/online.h"

int coopWorldReady(void);
void coopWorldApplyPlayers(void);
int coopWorldUpdate(const moduleApi_t* loader, const onlineApi_t* online, int enabled);

#endif
