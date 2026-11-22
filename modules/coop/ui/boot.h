#ifndef DL_COOP_BOOT_UI_H
#define DL_COOP_BOOT_UI_H

#include "../include/coop.h"

void coopBootInit(const moduleApi_t* api, volatile coopControl_t* control);
void coopBootUpdate(void);
void coopBootDeactivate(void);

#endif
