#ifndef DL_MAPSCAN_H
#define DL_MAPSCAN_H

/* Shared EE/IOP directory RPC contract. Both targets use 32-bit int fields. */
#define MAP_DIRECTORY_SERVICE (0x554d4151)

typedef enum mapDirectoryCommand {
	MAP_DIRECTORY_OPEN = 1,
	MAP_DIRECTORY_NEXT = 2,
	MAP_DIRECTORY_CLOSE = 3
} mapDirectoryCommand_e;

typedef struct mapScanReply {
	/* 0x00 */ int status;
	/* 0x04 */ char name[256];
	/* 0x104 */ char padding[60];
} mapScanReply_t;

#endif
