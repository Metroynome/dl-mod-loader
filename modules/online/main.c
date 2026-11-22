#include "include/online.h"
#include "transport.h"
#include <string.h>

#define HELLO (1)
#define WELCOME (2)
#define KEEPALIVE (3)
#define MESSAGE (4)
#define PROTOCOL_VERSION (3)
#define POLL_INTERVAL (2457600) /* EE Count: up to 60 nonblocking polls per second. */
#define CHANNEL_SLOTS (4)
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
static int sendEvent(u32 channel, const void* data, int size);
static int receiveMessage(u32 channel, void* data, int capacity);
static int request(u32 command, u32 address, u16 hostPort, u16 localPort);

volatile onlineControl_t moduleExports DL_MODULE_EXPORTS;
static const onlineApi_t onlineApi = {
	ONLINE_API_VERSION, sizeof(onlineApi_t), &moduleExports, sessionRole, sendMessage, receiveMessage, request, sendEvent
};
static const moduleApi_t* loader;
static onlineRpc_t operation;
static u32 pollTime, sendTime, receivedTime, startTime, clientToken, hostToken, peerAddress;
static u16 peerPort;
static int role, phase, waiting, active, stopping;
static onlineMessage_t outgoing[CHANNEL_SLOTS], incoming[CHANNEL_SLOTS];
static u32 sendSequence;
static int sendCursor;
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
	sendSequence = 0;
	sendCursor = 0;
	memset(outgoing, 0, sizeof(outgoing));
	memset(incoming, 0, sizeof(incoming));
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
 * NAME : channelSlot
 * DESCRIPTION : Find a channel's persistent slot or reserve an unused slot.
 * NOTES : Slots are cleared on session reset; no other channel is evicted.
 * ARGS : Message slots and a nonzero channel ID.
 * RETURN : Slot index, or minus one when all slots are occupied.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int channelSlot(onlineMessage_t* slots, u32 channel)
{
	int i, available = -1;
	for (i = 0; i < CHANNEL_SLOTS; ++i) {
		if (slots[i].channel == channel)
			return i;
		if (!slots[i].channel && available < 0)
			available = i;
	}
	return available;
}

/*
 * NAME : sendMessage
 * DESCRIPTION : Queue the latest adapter message without blocking.
 * NOTES : Each channel keeps its newest snapshot; channels cannot replace one another.
 * ARGS : Channel, payload and byte count.
 * RETURN : Zero accepted, negative unavailable/invalid.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int sendMessage(u32 channel, const void* data, int size)
{
	int slot;
	if (!sessionRole() || !channel || !data || size <= 0 || size > ONLINE_MESSAGE_BYTES)
		return -1;
	slot = channelSlot(outgoing, channel);
	if (slot < 0)
		return -1;
	outgoing[slot].channel = channel;
	outgoing[slot].length = size;
	memcpy(outgoing[slot].data, data, size);
	outgoingReady |= 1 << slot;
	return 0;
}

/*
 * NAME : sendEvent
 * DESCRIPTION : Queue an event without replacing an unsent message on its channel.
 * NOTES : This supplies backpressure, not UDP reliability; adapters retain and acknowledge events.
 * ARGS : Channel, event payload and byte count.
 * RETURN : Zero accepted, negative unavailable or busy.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int sendEvent(u32 channel, const void* data, int size)
{
	int slot;
	if (!sessionRole() || !channel)
		return -1;
	slot = channelSlot(outgoing, channel);
	if (slot < 0 || (outgoingReady & (1 << slot)))
		return -1;
	return sendMessage(channel, data, size);
}

/*
 * NAME : receiveMessage
 * DESCRIPTION : Consume the newest message for the active adapter channel.
 * NOTES : Sequence checks are per channel; cross-channel reordering loses no unread data.
 * ARGS : Channel, destination and capacity.
 * RETURN : Bytes copied, zero empty, negative invalid capacity.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int receiveMessage(u32 channel, void* data, int capacity)
{
	int slot;
	if (!sessionRole() || !channel)
		return 0;
	slot = channelSlot(incoming, channel);
	if (slot < 0 || !(incomingReady & (1 << slot)))
		return 0;
	if (!data || capacity < incoming[slot].length)
		return -1;
	memcpy(data, incoming[slot].data, incoming[slot].length);
	incomingReady &= ~(1 << slot);
	return incoming[slot].length;
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
	int slot;
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
		    reply->length != 0x1c + message.length || !message.sequence)
			return;
		slot = channelSlot(incoming, message.channel);
		if (slot < 0 || (incoming[slot].sequence && (int)(message.sequence - incoming[slot].sequence) <= 0))
			return;
		peerConfirmed = 1;
		memcpy(&incoming[slot], &message, sizeof(message));
		incomingReady |= 1 << slot;
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
	int result, selected = -1, i;
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
			for (i = 0; i < CHANNEL_SLOTS; ++i) {
				int slot = (sendCursor + i) % CHANNEL_SLOTS;
				if (outgoingReady & (1 << slot)) {
					selected = slot;
					break;
				}
			}
			if (selected >= 0) {
				outgoing[selected].header = packet;
				outgoing[selected].header.kind = MESSAGE;
				outgoing[selected].sequence = sendSequence + 1;
				if (!outgoing[selected].sequence)
					outgoing[selected].sequence = 1;
				outgoing[selected].reserved = 0;
				operation.length = 0x1c + outgoing[selected].length;
				memcpy(operation.data, &outgoing[selected], operation.length);
			}
		}
		sendTime = now;
		++moduleExports.sent;
	}
	result = onlineTransportRequest(phase, &operation);
	if (result > 0) {
		waiting = 1;
		if (selected >= 0) {
			sendSequence = outgoing[selected].sequence;
			outgoingReady &= ~(1 << selected);
			sendCursor = (selected + 1) % CHANNEL_SLOTS;
		}
	} else if (result < 0) {
		moduleExports.error = -4;
		moduleExports.status = ONLINE_FAILED;
	}
}
