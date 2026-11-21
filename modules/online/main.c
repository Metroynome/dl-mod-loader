#include "include/online.h"
#include "transport.h"
#include <string.h>

#define HELLO (1)
#define WELCOME (2)
#define KEEPALIVE (3)
#define MESSAGE (4)
#define PROTOCOL_VERSION (3)
#define POLL_INTERVAL (4000000)
#define SEND_INTERVAL (90000000)
#define TIMEOUT (1500000000)

/* Little-endian wire format for the initial, private protocol revision. */
typedef struct onlinePacket {
	/* 0x00 */ u32 magic;
	/* 0x04 */ u16 version;
	/* 0x06 */ u16 kind;
	/* 0x08 */ u32 clientToken;
	/* 0x0c */ u32 hostToken;
} onlinePacket_t;

typedef struct onlineMessage {
	/* 0x00 */ onlinePacket_t header;
	/* 0x10 */ u32 sequence;
	/* 0x14 */ u32 channel;
	/* 0x18 */ u16 length;
	/* 0x1a */ u16 reserved;
	/* 0x1c */ u8 data[ONLINE_MESSAGE_BYTES];
} onlineMessage_t;

static int sessionRole(void);
static int sendMessage(u32 channel, const void* data, int size);
static int receiveMessage(u32 channel, void* data, int capacity);
static int request(u32 command, u32 address, u16 hostPort, u16 localPort);

volatile onlineControl_t moduleExports DL_MODULE_EXPORTS;
static const onlineApi_t onlineApi = {
	ONLINE_API_VERSION, sizeof(onlineApi_t), &moduleExports, sessionRole, sendMessage, receiveMessage, request
};
static const moduleApi_t* loader;
static onlineRpc_t operation;
static u32 pollTime, sendTime, receivedTime, startTime, clientToken, hostToken, peerAddress;
static u16 peerPort;
static int role, phase, waiting, active, stopping;
static onlineMessage_t outgoing, incoming;
static u32 sendSequence, receiveSequence;
static int outgoingReady, incomingReady, peerConfirmed;

/*
 * NAME : resetMessages
 * DESCRIPTION : Discard snapshots and sequence state from the previous peer session.
 * NOTES : A reconnect must complete its handshake before sending adapter data.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void resetMessages(void)
{
	outgoingReady = incomingReady = peerConfirmed = 0;
	sendSequence = receiveSequence = 0;
}

/*
 * NAME : sessionRole
 * DESCRIPTION : Identify the active host or guest.
 * NOTES : Zero while inactive or disconnected.
 * ARGS :
 * RETURN : ONLINE_HOST, ONLINE_JOIN or zero.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int sessionRole(void)
{
	return active && moduleExports.status == ONLINE_CONNECTED ? role : 0;
}

/*
 * NAME : sendMessage
 * DESCRIPTION : Queue the latest adapter message without blocking.
 * NOTES : Replaces an unsent snapshot instead of accumulating latency.
 * ARGS : Channel, payload and byte count.
 * RETURN : Zero accepted, negative unavailable/invalid.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int sendMessage(u32 channel, const void* data, int size)
{
	if (!sessionRole() || !channel || !data || size <= 0 || size > ONLINE_MESSAGE_BYTES)
		return -1;
	/* Preserve another channel's queued packet until the IOP consumes it.
	 * Input on the same channel still keeps only its newest snapshot. */
	if (outgoingReady && outgoing.channel != channel)
		return -1;
	outgoing.channel = channel;
	outgoing.length = size;
	memcpy(outgoing.data, data, size);
	outgoingReady = 1;
	return 0;
}

/*
 * NAME : receiveMessage
 * DESCRIPTION : Consume the newest message for the active adapter channel.
 * NOTES : Older/reordered UDP messages are discarded before queuing.
 * ARGS : Channel, destination and capacity.
 * RETURN : Bytes copied, zero empty, negative invalid capacity.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int receiveMessage(u32 channel, void* data, int capacity)
{
	if (!sessionRole() || !incomingReady || incoming.channel != channel)
		return 0;
	if (!data || capacity < incoming.length)
		return -1;
	memcpy(data, incoming.data, incoming.length);
	incomingReady = 0;
	return incoming.length;
}

/*
 * NAME : ticks
 * DESCRIPTION : Read the wrapping EE clock without a level-dependent call.
 * NOTES : Unsigned intervals remain below a complete counter wrap.
 * ARGS :
 * RETURN : EE count register.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static u32 ticks(void)
{
	u32 value;
	__asm__ volatile("mfc0 %0, $9" : "=r"(value));
	return value;
}

/*
 * NAME : begin
 * DESCRIPTION : Begin an asynchronous host or join request.
 * NOTES : Failed sessions may retry; opening the new socket closes the old one.
 * ARGS : Role, destination IP/port and local UDP port.
 * RETURN : Zero accepted, negative invalid/busy/context.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int begin(int nextRole, u32 address, u16 port, u16 bindPort)
{
	if (!active || loader->context != DL_CONTEXT_CAMPAIGN || !onlineTransportSupported())
		return -1;
	if (waiting || stopping || (moduleExports.status != ONLINE_IDLE && moduleExports.status != ONLINE_FAILED) || !bindPort ||
	    (nextRole == ONLINE_JOIN && (!address || !port)))
		return -2;
	role = nextRole;
	peerAddress = nextRole == ONLINE_HOST ? 0 : address;
	peerPort = port;
	moduleExports.localPort = bindPort;
	moduleExports.error = 0;
	moduleExports.sent = moduleExports.received = 0;
	moduleExports.status = ONLINE_STARTING;
	phase = waiting = stopping = 0;
	resetMessages();
	startTime = ticks();
	clientToken = (startTime ^ ((u32)&moduleExports >> 4) ^ bindPort) | 1;
	hostToken = nextRole == ONLINE_HOST ? clientToken : 0;
	return 0;
}

/*
 * NAME : request
 * DESCRIPTION : Queue a transport command independently of any UI.
 * NOTES : Acknowledgement means processed, not connection success.
 * ARGS : Command, IPv4 bytes and host/local UDP ports.
 * RETURN : Zero queued, negative invalid or an outstanding request.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int request(u32 command, u32 address, u16 hostPort, u16 localPort)
{
	if (command < ONLINE_HOST || command > ONLINE_STOP ||
	    (command != ONLINE_STOP && (!hostPort || !localPort)) ||
	    (command == ONLINE_JOIN && !address))
		return -1;
	if (moduleExports.requestId != moduleExports.acceptedId)
		return -2;
	moduleExports.command = command;
	moduleExports.hostAddress = address;
	moduleExports.hostPort = hostPort;
	moduleExports.localPort = command == ONLINE_HOST ? hostPort : localPort;
	moduleExports.requestId = moduleExports.acceptedId + 1;
	return 0;
}

/*
 * NAME : receivePacket
 * DESCRIPTION : Validate a handshake or heartbeat from the selected peer.
 * NOTES : Established traffic must match both address/port and session tokens.
 * ARGS : UDP reply and local time.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void receivePacket(const onlineRpc_t* reply, u32 now)
{
	onlinePacket_t packet;
	onlineMessage_t message;
	if (reply->length < sizeof(packet))
		return;
	memcpy(&packet, reply->data, sizeof(packet));
	if (packet.magic != ONLINE_MAGIC || packet.version != PROTOCOL_VERSION)
		return;
	if (packet.kind == MESSAGE) {
		if (moduleExports.status != ONLINE_CONNECTED || reply->address != peerAddress || reply->port != peerPort ||
		    packet.clientToken != clientToken || packet.hostToken != hostToken ||
		    reply->length < 0x1c || reply->length > sizeof(message))
			return;
		memset(&message, 0, sizeof(message));
		memcpy(&message, reply->data, reply->length);
		if (!message.channel || !message.length || message.length > ONLINE_MESSAGE_BYTES ||
		    reply->length != 0x1c + message.length || (int)(message.sequence - receiveSequence) <= 0)
			return;
		receiveSequence = message.sequence;
		peerConfirmed = 1;
		memcpy(&incoming, &message, sizeof(incoming));
		incomingReady = 1;
		++moduleExports.received;
		receivedTime = now;
		return;
	}
	if (reply->length != sizeof(packet))
		return;
	if (role == ONLINE_HOST && packet.kind == HELLO && packet.clientToken && !packet.hostToken) {
		if (moduleExports.status != ONLINE_HOSTING &&
		    (moduleExports.status != ONLINE_CONNECTED || reply->address != peerAddress || reply->port != peerPort))
			return;
		if (moduleExports.status == ONLINE_HOSTING || clientToken != packet.clientToken)
			resetMessages();
		peerAddress = reply->address;
		peerPort = reply->port;
		clientToken = packet.clientToken;
		moduleExports.status = ONLINE_CONNECTED;
	} else if (role == ONLINE_JOIN && moduleExports.status == ONLINE_CONNECTING &&
	           reply->address == peerAddress && reply->port == peerPort && packet.kind == WELCOME &&
	           packet.clientToken == clientToken && packet.hostToken) {
		hostToken = packet.hostToken;
		peerConfirmed = 1;
		moduleExports.status = ONLINE_CONNECTED;
	} else if (moduleExports.status != ONLINE_CONNECTED || reply->address != peerAddress || reply->port != peerPort ||
	           packet.clientToken != clientToken || packet.hostToken != hostToken ||
	           (packet.kind != KEEPALIVE && packet.kind != WELCOME && !(role == ONLINE_HOST && packet.kind == HELLO)))
		return;
	++moduleExports.received;
	if (packet.kind == KEEPALIVE)
		peerConfirmed = 1;
	receivedTime = now;
	if (role == ONLINE_HOST && packet.kind == HELLO)
		sendTime = now - SEND_INTERVAL;
}

/*
 * NAME : deactivate
 * DESCRIPTION : Stop connection activity and close the transport socket.
 * NOTES : Never unload drivers/reset IOP during campaign. Close finishes on re-entry if RPC is busy.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void deactivate(void)
{
	active = 0;
	stopping = 1;
	if (!waiting && phase) {
		memset(&operation, 0, sizeof(operation));
		if (onlineTransportRequest(ONLINE_RPC_CLOSE, &operation) > 0) {
			phase = ONLINE_RPC_CLOSE;
			waiting = 1;
		}
	}
	moduleExports.status = ONLINE_IDLE;
	outgoingReady = incomingReady = 0;
}

/*
 * NAME : activate
 * DESCRIPTION : Enable campaign transport requests.
 * NOTES :
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void activate(void)
{
	active = 1;
}

/*
 * NAME : moduleInit
 * DESCRIPTION : Publish the optional connection-test mailbox.
 * NOTES : Does not load drivers or modify players at startup.
 * ARGS : Loader API.
 * RETURN : Zero success, negative registration failure.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int moduleInit(const moduleApi_t* api)
{
	loader = api;
	onlineTransportInit(api);
	moduleExports.magic = ONLINE_MAGIC;
	moduleExports.size = sizeof(moduleExports);
	moduleExports.localPort = 25566;
	moduleExports.hostPort = 25565;
	moduleExports.hostAddress = 0;
	api->setLifecycle(activate, deactivate);
	return api->publishService(ONLINE_SERVICE_ID, ONLINE_API_VERSION, &onlineApi);
}

/*
 * NAME : moduleUpdate
 * DESCRIPTION : Advance driver startup, direct UDP handshake and peer heartbeats.
 * NOTES : No player/world synchronization is performed yet.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void moduleUpdate(void)
{
	onlineRpc_t reply;
	onlinePacket_t packet;
	u32 now = ticks();
	int result;
	++moduleExports.heartbeat;
	if (moduleExports.requestId != moduleExports.acceptedId) {
		moduleExports.acceptedId = moduleExports.requestId;
		if (moduleExports.command == ONLINE_STOP)
			stopping = 1;
		else if (moduleExports.command == ONLINE_HOST || moduleExports.command == ONLINE_JOIN)
			moduleExports.error = begin(moduleExports.command, moduleExports.hostAddress,
			                            moduleExports.hostPort, moduleExports.localPort);
		else
			moduleExports.error = -3;
	}
	if (waiting) {
		result = onlineTransportReply(&reply);
		if (!result)
			return;
		waiting = 0;
		if (result < 0 || reply.result < 0) {
			moduleExports.error = -4;
			moduleExports.status = ONLINE_FAILED;
			return;
		}
		if (phase == ONLINE_RPC_CONFIG) {
			moduleExports.localAddress = reply.localAddress;
			if (reply.result == 1)
				phase = ONLINE_RPC_OPEN;
		} else if (phase == ONLINE_RPC_OPEN) {
			moduleExports.status = role == ONLINE_HOST ? ONLINE_HOSTING : ONLINE_CONNECTING;
			phase = ONLINE_RPC_POLL;
			receivedTime = now;
			sendTime = now - SEND_INTERVAL;
		} else if (phase == ONLINE_RPC_POLL)
			receivePacket(&reply, now);
		else if (phase == ONLINE_RPC_CLOSE) {
			stopping = 0;
			moduleExports.status = ONLINE_IDLE;
		}
	}
	if (moduleExports.status == ONLINE_STARTING) {
		result = onlineTransportReady();
		if (!result)
			return;
		if (result < 0) {
			moduleExports.error = -5;
			moduleExports.status = ONLINE_FAILED;
			return;
		}
		moduleExports.status = ONLINE_ADDRESS;
		phase = ONLINE_RPC_CONFIG;
		startTime = now;
	}
	if (stopping) {
		memset(&operation, 0, sizeof(operation));
		result = onlineTransportRequest(ONLINE_RPC_CLOSE, &operation);
		if (result > 0) {
			phase = ONLINE_RPC_CLOSE;
			waiting = 1;
		} else if (result < 0) {
			stopping = 0;
			moduleExports.status = ONLINE_IDLE;
		}
		return;
	}
	if (moduleExports.status == ONLINE_IDLE || moduleExports.status == ONLINE_FAILED)
		return;
	if ((moduleExports.status == ONLINE_ADDRESS && now - startTime > TIMEOUT) ||
	    ((moduleExports.status == ONLINE_CONNECTING || moduleExports.status == ONLINE_CONNECTED) && now - receivedTime > TIMEOUT)) {
		if (role == ONLINE_HOST && moduleExports.status == ONLINE_CONNECTED) {
			peerAddress = peerPort = clientToken = 0;
			resetMessages();
			moduleExports.status = ONLINE_HOSTING;
			moduleExports.error = 0;
		} else {
			moduleExports.error = -6;
			moduleExports.status = ONLINE_FAILED;
			return;
		}
	}
	if (now - pollTime < POLL_INTERVAL)
		return;
	pollTime = now;
	memset(&operation, 0, sizeof(operation));
	if (phase == ONLINE_RPC_OPEN) {
		operation.port = moduleExports.localPort;
		operation.address = role == ONLINE_HOST; /* Host-only incoming-port probe. */
	}
	if (phase == ONLINE_RPC_POLL && peerAddress && (outgoingReady || now - sendTime >= SEND_INTERVAL)) {
		packet.magic = ONLINE_MAGIC;
		packet.version = PROTOCOL_VERSION;
		packet.kind = role == ONLINE_HOST ? WELCOME : moduleExports.status == ONLINE_CONNECTED ? KEEPALIVE : HELLO;
		packet.clientToken = clientToken;
		packet.hostToken = hostToken;
		operation.address = peerAddress;
		operation.port = peerPort;
		operation.length = sizeof(packet);
		memcpy(operation.data, &packet, sizeof(packet));
		if (outgoingReady && peerConfirmed && moduleExports.status == ONLINE_CONNECTED) {
			outgoing.header = packet;
			outgoing.header.kind = MESSAGE;
			outgoing.sequence = ++sendSequence;
			outgoing.reserved = 0;
			operation.length = 0x1c + outgoing.length;
			memcpy(operation.data, &outgoing, operation.length);
			outgoingReady = 0;
		}
		sendTime = now;
		++moduleExports.sent;
	}
	result = onlineTransportRequest(phase, &operation);
	if (result > 0)
		waiting = 1;
	else if (result < 0) {
		moduleExports.error = -4;
		moduleExports.status = ONLINE_FAILED;
	}
}
