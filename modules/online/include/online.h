#ifndef DL_ONLINE_H
#define DL_ONLINE_H

#include "module.h"

#define ONLINE_MAGIC (0x314e4c44)
#define ONLINE_SERVICE_ID (0x4f4e4c4e)
#define ONLINE_API_VERSION (2)
#define ONLINE_MESSAGE_BYTES (64)

typedef enum onlineCommand {
	ONLINE_HOST = 1,
	ONLINE_JOIN,
	ONLINE_STOP
} onlineCommand_e;

typedef enum onlineState {
	ONLINE_IDLE,
	ONLINE_STARTING,
	ONLINE_ADDRESS,
	ONLINE_HOSTING,
	ONLINE_CONNECTING,
	ONLINE_CONNECTED,
	ONLINE_FAILED
} onlineState_e;

/* Experimental connection test, not a campaign synchronization API.
 * IPv4 is stored as four network-order bytes. Write command/ports/address first,
 * requestId last. acceptedId acknowledges the request, not connection success. */
typedef struct onlineControl {
	/* 0x00 */ u32 magic;
	/* 0x04 */ u32 size;
	/* 0x08 */ u32 heartbeat;
	/* 0x0c */ u32 status;
	/* 0x10 */ int error;
	/* 0x14 */ u32 acceptedId;
	/* 0x18 */ u32 received;
	/* 0x1c */ u32 sent;
	/* 0x20 */ u32 requestId;
	/* 0x24 */ u32 command;
	/* 0x28 */ u16 localPort;
	/* 0x2a */ u16 hostPort;
	/* 0x2c */ u32 hostAddress;
	/* 0x30 */ u32 localAddress;
} onlineControl_t;

/* EE pointer layout. Latest-message datagrams, not a reliable event stream.
 * One active adapter/channel is intended for this prototype. */
typedef struct onlineApi {
	/* 0x00 */ u32 version;
	/* 0x04 */ u32 size;
	/* 0x08 */ const volatile onlineControl_t* control;
	/* 0x0c */ int (*role)(void);
	/* 0x10 */ int (*send)(u32 channel, const void* data, int size);
	/* 0x14 */ int (*receive)(u32 channel, void* data, int capacity);
} onlineApi_t;

static inline const onlineApi_t* onlineFind(const moduleApi_t* loader)
{
	const onlineApi_t* api = loader->findService(ONLINE_SERVICE_ID, ONLINE_API_VERSION);
	return api && api->version == ONLINE_API_VERSION && api->size >= sizeof(*api) ? api : 0;
}

#endif
