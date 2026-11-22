#ifndef DL_COOP_WORLD_H
#define DL_COOP_WORLD_H

#include "module.h"
#include "modules/online/include/online.h"

typedef struct coopHostSession {
	/* 0x00 */ int level;
	/* 0x04 */ int mission;
	/* 0x08 */ int missionLink;
} coopHostSession_t;

int coopWorldGetHostSession(coopHostSession_t* session);
int coopWorldReady(void);
void coopWorldApplyPlayers(void);
int coopWorldUpdate(const moduleApi_t* loader, const onlineApi_t* online, int enabled);

#endif
