#ifndef DL_ONLINE_TRANSPORT_H
#define DL_ONLINE_TRANSPORT_H

#include "module.h"

#define ONLINE_RPC_SERVICE (0x444c4e31)
#define ONLINE_PACKET_BYTES (256)

typedef enum onlineRpcCommand {
	ONLINE_RPC_CONFIG = 1,
	ONLINE_RPC_OPEN,
	ONLINE_RPC_POLL,
	ONLINE_RPC_CLOSE
} onlineRpcCommand_e;

/* Shared EE/IOP layout; POLL addresses are IPv4 bytes in network order.
 * OPEN uses address as a host/listener flag and port as the local bind port. */
typedef struct onlineRpc {
	/* 0x00 */ int result;
	/* 0x04 */ u32 address;
	/* 0x08 */ u16 port;
	/* 0x0a */ u16 length;
	/* 0x0c */ u32 localAddress;
	/* 0x10 */ u8 data[ONLINE_PACKET_BYTES];
} onlineRpc_t;

void onlineTransportInit(const moduleApi_t* api);
int onlineTransportSupported(void);
int onlineTransportReady(void);
int onlineTransportRequest(int command, const onlineRpc_t* request);
int onlineTransportReply(onlineRpc_t* reply);

#endif
