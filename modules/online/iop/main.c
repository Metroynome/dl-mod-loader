#include <loadcore.h>
#include <sifcmd.h>
#include <thbase.h>
#include <sysclib.h>
#include <ps2ip.h>
#include "transport.h"

#define BEACON_INTERVAL (2 * 36864000)

IRX_ID("dl_online", 1, 0);

static SifRpcDataQueue_t queue;
static SifRpcServerData_t server;
static onlineRpc_t request __attribute__((aligned(64)));
static onlineRpc_t reply __attribute__((aligned(64)));
static int udp = -1, configured, hosting;
static u16 localPort;
static u32 beaconTime;

/*
 * NAME : advertise
 * DESCRIPTION : Keep the host's incoming UDP mapping open on socket-based emulators.
 * NOTES : A tiny LAN probe is ignored by the session parser; no discovery service is required.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void advertise(void)
{
	iop_sys_clock_t now;
	struct sockaddr_in address;
	u8 probe = 0;
	GetSystemTime(&now);
	if (!hosting || now.lo - beaconTime < BEACON_INTERVAL)
		return;
	beaconTime = now.lo;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = 0xffffffff;
	address.sin_port = PP_HTONS(localPort);
	lwip_sendto(udp, &probe, sizeof(probe), MSG_DONTWAIT, (struct sockaddr*)&address, sizeof(address));
}

/*
 * NAME : dispatch
 * DESCRIPTION : Run bounded UDP operations on the IOP RPC thread.
 * NOTES : Never wait for a remote packet; all receive/send calls are nonblocking.
 * ARGS : RPC command, request buffer and byte count.
 * RETURN : Reply buffer.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void* dispatch(int command, void* data, int size)
{
	onlineRpc_t* value = data;
	struct sockaddr_in address;
	t_ip_info config;
	socklen_t length = sizeof(address);
	int nonblocking = 1, received;
	memset(&reply, 0, sizeof(reply));
	reply.result = -1;
	if (size != sizeof(*value) || value->length > ONLINE_PACKET_BYTES)
		return &reply;
	if (command == ONLINE_RPC_CONFIG) {
		if (ps2ip_getconfig("sm0", &config) <= 0)
			return &reply;
		if (!configured) {
			config.ipaddr.s_addr = config.netmask.s_addr = config.gw.s_addr = 0;
			config.dhcp_enabled = 1;
			if (ps2ip_setconfig(&config) <= 0)
				return &reply;
			configured = 1;
			config.ipaddr.s_addr = 0;
		}
		reply.localAddress = config.ipaddr.s_addr;
		reply.result = reply.localAddress ? 1 : 0;
	} else if (command == ONLINE_RPC_OPEN) {
		if (udp >= 0)
			lwip_close(udp);
		udp = lwip_socket(AF_INET, SOCK_DGRAM, 0);
		if (udp < 0)
			return &reply;
		memset(&address, 0, sizeof(address));
		address.sin_family = AF_INET;
		address.sin_port = PP_HTONS(value->port);
		if (lwip_ioctl(udp, FIONBIO, &nonblocking) < 0 ||
		    lwip_setsockopt(udp, SOL_SOCKET, SO_BROADCAST, &nonblocking, sizeof(nonblocking)) < 0 ||
		    lwip_bind(udp, (struct sockaddr*)&address, sizeof(address)) < 0) {
			lwip_close(udp);
			udp = -1;
			return &reply;
		}
		hosting = value->address != 0;
		localPort = value->port;
		beaconTime = 0;
		advertise();
		reply.result = 1;
	} else if (command == ONLINE_RPC_POLL && udp >= 0) {
		advertise();
		reply.result = 1;
		if (value->length) {
			memset(&address, 0, sizeof(address));
			address.sin_family = AF_INET;
			address.sin_addr.s_addr = value->address;
			address.sin_port = PP_HTONS(value->port);
			if (lwip_sendto(udp, value->data, value->length, MSG_DONTWAIT,
			                (struct sockaddr*)&address, sizeof(address)) != value->length)
				reply.result = -1;
		}
		received = lwip_recvfrom(udp, reply.data, sizeof(reply.data), MSG_DONTWAIT,
		                         (struct sockaddr*)&address, &length);
		if (received > 0) {
			reply.length = received;
			reply.address = address.sin_addr.s_addr;
			reply.port = PP_NTOHS(address.sin_port);
		}
	} else if (command == ONLINE_RPC_CLOSE) {
		if (udp >= 0)
			lwip_close(udp);
		udp = -1;
		hosting = 0;
		reply.result = 1;
	}
	return &reply;
}

/*
 * NAME : serve
 * DESCRIPTION : Register and run the private UDP RPC service.
 * NOTES :
 * ARGS : Unused thread argument.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void serve(void* unused)
{
	(void)unused;
	SifSetRpcQueue(&queue, GetThreadId());
	SifRegisterRpc(&server, ONLINE_RPC_SERVICE, dispatch, &request, 0, 0, &queue);
	SifRpcLoop(&queue);
}

/*
 * NAME : _start
 * DESCRIPTION : Start the small IOP networking helper.
 * NOTES : Ethernet and TCP/IP drivers must already be loaded.
 * ARGS : Unused module arguments.
 * RETURN : Resident status.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int _start(int argc, char** argv)
{
	iop_thread_t thread;
	int id;
	(void)argc;
	(void)argv;
	memset(&thread, 0, sizeof(thread));
	thread.attr = TH_C;
	thread.thread = serve;
	thread.priority = 40;
	thread.stacksize = 0x2000;
	SifInitRpc(0);
	id = CreateThread(&thread);
	if (id < 0)
		return MODULE_NO_RESIDENT_END;
	if (StartThread(id, 0) < 0) {
		DeleteThread(id);
		return MODULE_NO_RESIDENT_END;
	}
	return MODULE_RESIDENT_END;
}
