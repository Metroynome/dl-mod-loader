#ifndef DL_COOP_CONNECTION_UI_H
#define DL_COOP_CONNECTION_UI_H

#include "module.h"
#include "modules/online/include/online.h"

#define COOP_CONNECTION_DEFAULT_NAME "localhost"

typedef enum coopConnectionField {
	COOP_CONNECTION_ADDRESS,
	COOP_CONNECTION_HOST_PORT,
	COOP_CONNECTION_LOCAL_PORT
} coopConnectionField_e;

/* Standalone callers own input capture, release-on-close and a safe UI render
 * pass. No Mod Menu or connection is needed to edit/load/save defaults.
 * Open edits a private copy; Input returns 0 editing, 1 accepted, -1 cancelled.
 * Input takes both new presses and currently held buttons for D-pad repeat.
 * There is deliberately no standalone opening shortcut yet. */
void coopConnectionInit(const moduleApi_t* api);
void coopConnectionUpdate(const onlineApi_t* online);
void coopConnectionDeactivate(void);
int coopConnectionOpen(coopConnectionField_e field);
int coopConnectionInput(u32 pressed, u32 held);
void coopConnectionDraw(void);
int coopConnectionLoad(void);
int coopConnectionSave(void);
int coopConnectionRequest(u32 command);
void coopConnectionDisconnect(void);
int coopConnectionCanDisconnect(void);
int coopConnectionStopped(void);
const char* coopConnectionValue(coopConnectionField_e field);

const char* coopConnectionProfileLabel(int slot);
int coopConnectionProfileSelect(int slot);
void coopConnectionProfileBegin(int slot);
int coopConnectionProfileChanged(int slot);
void coopConnectionProfileEnd(int slot, int save);
void coopConnectionProfileDelete(int slot);

#endif
