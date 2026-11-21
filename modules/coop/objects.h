#ifndef DL_COOP_OBJECTS_H
#define DL_COOP_OBJECTS_H

#include "module.h"
#include "modules/online/include/online.h"

#define COOP_OBJECT_BYTES (128)

int coopObjectsUpdate(const moduleApi_t* loader, const onlineApi_t* online, int enabled, int ready, int canSend);
int coopObjectsReady(void);
void coopObjectsReceive(const void* data, int bytes);
int coopObjectsWrite(void* data);
int coopObjectsBounds(u32* start, u32* end);
int coopObjectsEnemyClass(u16 oClass);
void coopObjectsReset(void);

#endif
