#include "include/online.h"
#include "transport.h"
#include "modules/modmenu/include/modmenu.h"
#include <string.h>

#define HELLO (1)
#define WELCOME (2)
#define KEEPALIVE (3)
#define MESSAGE (4)
#define PROTOCOL_VERSION (2)
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

volatile onlineControl_t moduleExports DL_MODULE_EXPORTS;
static const onlineApi_t onlineApi = {
	ONLINE_API_VERSION, sizeof(onlineApi_t), &moduleExports, sessionRole, sendMessage, receiveMessage
};
static const moduleApi_t* loader;
static const modMenuApi_t* menu;
static onlineRpc_t operation;
static u32 pollTime, sendTime, receivedTime, startTime, clientToken, hostToken, peerAddress;
static u16 peerPort;
static int role, phase, waiting, active, stopping, tab;
static int hostPort = 28600, localPort = 28600, ip[4] = {192, 168, 1, 100};
static onlineMessage_t outgoing, incoming;
static u32 sendSequence, receiveSequence;
static int outgoingReady, incomingReady, peerConfirmed;

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
 * NAME : stateLabel
 * DESCRIPTION : Label the read-only connection status row.
 * NOTES :
 * ARGS : Unused menu arguments.
 * RETURN : Connection status.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static const char* stateLabel(void* unused, int value)
{
	static const char* names[] = {"Idle", "Loading drivers", "Obtaining IP", "Hosting", "Connecting", "Connected", "Failed"};
	(void)unused;
	(void)value;
	return moduleExports.status <= ONLINE_FAILED ? names[moduleExports.status] : "Unavailable";
}

/*
 * NAME : begin
 * DESCRIPTION : Begin an asynchronous host or join request.
 * NOTES : One peer only; native Horizon/Multiplayer sessions are not used.
 * ARGS : Role, destination IP/port and local UDP port.
 * RETURN : Zero accepted, negative invalid/busy/context.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int begin(int nextRole, u32 address, u16 port, u16 bindPort)
{
	if (!active || loader->context != DL_CONTEXT_CAMPAIGN || !onlineTransportSupported())
		return -1;
	if (waiting || stopping || moduleExports.status != ONLINE_IDLE || !bindPort ||
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
	outgoingReady = incomingReady = 0;
	peerConfirmed = 0;
	sendSequence = receiveSequence = 0;
	startTime = ticks();
	clientToken = (startTime ^ ((u32)&moduleExports >> 4) ^ bindPort) | 1;
	hostToken = nextRole == ONLINE_HOST ? clientToken : 0;
	return 0;
}

/*
 * NAME : command
 * DESCRIPTION : Submit menu actions through the public test mailbox.
 * NOTES : Payload is written before the request ID.
 * ARGS : Host, join or stop command.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void command(void* context)
{
	moduleExports.command = (u32)context;
	moduleExports.localPort = localPort;
	moduleExports.hostPort = hostPort;
	moduleExports.hostAddress = ip[0] | (ip[1] << 8) | (ip[2] << 16) | ((u32)ip[3] << 24);
	moduleExports.requestId = moduleExports.acceptedId + 1;
}

/*
 * NAME : registerMenu
 * DESCRIPTION : Register optional direct-IP controls using the shared menu.
 * NOTES : External callers can use the mailbox without Mod Menu.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void registerMenu(void)
{
	static const modMenuButton_t buttons[] = {
		{"Host", command, (void*)ONLINE_HOST},
		{"Join", command, (void*)ONLINE_JOIN},
		{"Disconnect", command, (void*)ONLINE_STOP}
	};
	static const char* labels[] = {"Host IP: first byte", "Host IP: second byte", "Host IP: third byte", "Host IP: fourth byte"};
	modMenuItem_t row = {"", MODMENU_BUTTON_ROW, 0, 0, 0, 0, 0, 0};
	modMenuItem_t status = {"Connection", MODMENU_RANGE, (int*)&moduleExports.status, ONLINE_IDLE, ONLINE_FAILED, 1, 0, 0};
	modMenuItem_t port = {"Host Port", MODMENU_RANGE, &hostPort, 1024, 65535, 1, 0, 0};
	modMenuItem_t bind = {"Local Port", MODMENU_RANGE, &localPort, 1024, 65535, 1, 0, 0};
	modMenuItem_t info = {"Connection test; no co-op sync yet", MODMENU_LABEL, 0, 0, 0, 0, 0, 0};
	int i, item;
	if (tab)
		return;
	menu = modmenuFind(loader);
	if (!menu)
		return;
	tab = menu->addTab("online", "Online", DL_MODE_CAMPAIGN);
	if (tab <= 0) {
		tab = 0;
		return;
	}
	menu->setTabContexts(tab, DL_CONTEXT_CAMPAIGN);
	menu->setItemButtons(menu->addItem(tab, &row), buttons, 3);
	item = menu->addItem(tab, &status);
	menu->setItemValueLabel(item, stateLabel);
	menu->setItemEnabled(item, 0);
	for (i = 0; i < 4; ++i) {
		modMenuItem_t byte = {labels[i], MODMENU_RANGE, &ip[i], 0, 255, 1, 0, 0};
		menu->addItem(tab, &byte);
	}
	menu->addItem(tab, &port);
	menu->addItem(tab, &bind);
	menu->addItem(tab, &info);
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
	if (role == ONLINE_HOST && moduleExports.status == ONLINE_HOSTING && packet.kind == HELLO && packet.clientToken) {
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
 * DESCRIPTION : Stop connection activity and remove the optional tab.
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
	if (menu && tab)
		menu->removeTab(tab);
	menu = 0;
	tab = 0;
	moduleExports.status = ONLINE_IDLE;
	outgoingReady = incomingReady = 0;
}

/*
 * NAME : activate
 * DESCRIPTION : Enable campaign connection controls.
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
	moduleExports.localPort = moduleExports.hostPort = 28600;
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
	registerMenu();
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
		moduleExports.error = -6;
		moduleExports.status = ONLINE_FAILED;
		return;
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
