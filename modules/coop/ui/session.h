#ifndef DL_COOP_SESSION_UI_H
#define DL_COOP_SESSION_UI_H

#include "module.h"

typedef struct coopSessionMenu {
	/* 0x00 */ int screen;
	/* 0x04 */ int focus;
	/* 0x08 */ int planet;
} coopSessionMenu_t;

int coopSessionScreen(void);
int coopSessionRead(coopSessionMenu_t* frame);
void coopSessionApply(const coopSessionMenu_t* frame);
int coopSessionWatching(void);
void coopSessionReset(void);

#endif
