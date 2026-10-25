#ifndef DL_FORGE_H
#define DL_FORGE_H

#include "module.h"

#define FORGE_EXPORTS_MAGIC (0x31584746)
#define FORGE_SERVICE_ID    (0x464f5247)
#define MAP_HOP_SERVICE_ID  (0x484f5053)
#define FORGE_API_VERSION   (3)

/* Module-owned little-endian EE mailbox. Only request fields belong to the host.
 * Write command/name/optional gameMode first, requestId last. One request at a time. */
#define FORGE_MAILBOX_MAGIC   (0x34475246)
#define FORGE_FILENAME_SIZE   (129)

#ifndef FORGE_MAILBOX
#define FORGE_MAILBOX (&forgeMailbox)
#endif

/* Values are shared with PINE clients; keep their numeric assignments stable. */
typedef enum forgeCommand {
	FORGE_RELOAD = 1,
	FORGE_REFRESH = 2,
	FORGE_SELECT = 3,
	FORGE_REFRESH_SELECT = 4,
	FORGE_START_GAME = 5,
	FORGE_SELECT_RETAIL = 6
} forgeCommand_e;

typedef enum forgeContext {
	FORGE_CONTEXT_UNAVAILABLE = 0,
	FORGE_CONTEXT_STAGING = 1,
	FORGE_CONTEXT_GAME = 2
} forgeContext_e;

/* Native libdl GameRuleIds. Zero also means the optional host byte was omitted. */
typedef enum forgeGameMode {
	FORGE_MODE_CQ = 0,
	FORGE_MODE_CTF = 1,
	FORGE_MODE_DM = 2,
	FORGE_MODE_KOTH = 3,
	FORGE_MODE_JUGGY = 4
} forgeGameMode_e;

typedef enum forgeState {
	FORGE_IDLE = 0,
	FORGE_SCANNING = 1,
	FORGE_LOADING = 2,
	FORGE_COMPLETE = 3,
	FORGE_FAILED = 4
} forgeState_e;

typedef enum forgeError {
	FORGE_ERROR_CONTEXT = 1,
	FORGE_ERROR_BUSY = 2,
	FORGE_ERROR_COMMAND = 3,
	FORGE_ERROR_NAME = 4,
	FORGE_ERROR_NOT_FOUND = 5,
	FORGE_ERROR_SCAN = 6,
	FORGE_ERROR_LOAD = 7,
	FORGE_ERROR_INACTIVE = 8,
	FORGE_ERROR_MODE = 9
} forgeError_e;

/* Forge owns the capability; inactive modules must return false. */
/* Offsets below use the EE's 32-bit pointers. */
typedef struct forgeApi {
	/* 0x00 */ u32 version;
	/* 0x04 */ u32 size;
	/* 0x08 */ int (*active)(void);
	/* 0x0c */ const int* quickLoadMaps;
	/* 0x10 */ const int* gameMode; /* Current/pending UI mode, initialized from the game. */
} forgeApi_t;

/* Requests execute asynchronously. reload() retains the mode and returns positive
 * when queued. request() takes a native GameRuleIds byte (CQ=0), returning zero
 * when accepted or a FORGE_ERROR_* rejection. Refresh ignores the mode. */
typedef struct mapHopApi {
	/* 0x00 */ u32 version;
	/* 0x04 */ u32 size;
	/* 0x08 */ int (*available)(void);
	/* 0x0c */ int (*reload)(void);
	/* 0x10 */ const char* (*status)(void);
	/* 0x14 */ u32 (*context)(void); /* 0 unavailable, 1 staging, 2 game */
	/* 0x18 */ int (*request)(u32 command, const char* filename, u8 gameMode);
	/* 0x1c */ u32 (*state)(void);
	/* 0x20 */ u32 (*error)(void);
	/* 0x24 */ int (*setStagingGameMode)(u8 gameMode); /* No load; zero on rejection. */
} mapHopApi_t;

typedef struct forgeMailbox {
	/* Game-owned status. Host clients only read these fields. */
	/* 0x00 */ u32 magic;
	/* 0x04 */ u32 heartbeat;
	/* 0x08 */ u32 acceptedId;
	/* 0x0c */ u32 completedId;
	/* 0x10 */ u32 rejectedId;
	/* 0x14 */ u8 available;
	/* 0x15 */ u8 context;
	/* 0x16 */ u8 capabilities;
	/* 0x17 */ u8 state;
	/* 0x18 */ u8 error;
	/* 0x19 */ u8 rejectError;
	/* 0x1a..0x1b: alignment padding before the 32-bit request ID. */

	/* Host request. Write the payload first and requestId last. */
	/* 0x1c */ u32 requestId;
	/* 0x20 */ u8 command;
	/* 0x21 */ u8 gameMode; /* GameRuleIds; reset to CQ after consumption. */
	/* 0x22 */ char filename[FORGE_FILENAME_SIZE];
	/* 0xa3 */ u8 padding[1];
} forgeMailbox_t;

/* Offsets are for 32-bit EE pointers. Export discovery remains version 1. */
typedef struct forgeExports {
	/* 0x00 */ u32 magic;
	/* 0x04 */ u32 version;
	/* 0x08 */ u32 size;
	/* 0x0c */ volatile forgeMailbox_t* mailbox;
} forgeExports_t;

typedef char forgeMailboxSize[(sizeof(forgeMailbox_t) == 164) ? 1 : -1];

extern volatile forgeMailbox_t forgeMailbox;
#endif
