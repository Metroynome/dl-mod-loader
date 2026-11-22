#ifndef DL_COOP_PAUSE_UI_H
#define DL_COOP_PAUSE_UI_H

#include "module.h"
#include "modules/online/include/online.h"

void coopPauseUpdate(const moduleApi_t* loader, const onlineApi_t* online);
void coopPauseReset(const moduleApi_t* loader);

#endif
