#include "transport.h"
#include "strings.h"
#include <string.h>
#include <kernel.h>
#include <sifrpc.h>
#include <sifcmd.h>

#define CALL(address, type) ((type)(address))
#define IRX_BYTES (4096)
#define IRX_LIMIT (256 * 1024)
#define bindRpc CALL(0x12a538, int (*)(SifRpcClientData_t*, int, int))
#define callRpc CALL(0x12a718, int (*)(SifRpcClientData_t*, int, int, void*, int, void*, int, void*, void*))
#define rpcBusy CALL(0x12a918, int (*)(SifRpcClientData_t*))

static const moduleApi_t* loader;
static SifRpcClientData_t client __attribute__((aligned(64)));
static onlineRpc_t request __attribute__((aligned(64)));
static onlineRpc_t response __attribute__((aligned(64)));
static u8 image[IRX_BYTES] __attribute__((aligned(64)));
static int stage, pending, retries, failed;
static const char* drivers[] = {
	"host0:dl/mods/utils/online/ps2dev9.irx",
	"host0:dl/mods/utils/online/netman.irx",
	"host0:dl/mods/utils/online/smap.irx",
	"host0:dl/mods/utils/online/ps2ip-nm.irx",
	"host0:dl/mods/utils/online/online.irx"
};

/*
 * NAME : install
 * DESCRIPTION : Load one networking driver without resetting the campaign IOP.
 * NOTES : Resident SDK entry points match the verified mapscan loader.
 * ARGS : Driver filename.
 * RETURN : Module ID, or negative on failure.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static int install(const char* path)
{
	SifDmaTransfer_t dma;
	void* remote;
	int fd, size, received = 0, result = -1, id = -1, transfer, n, chunk, used;
	fd = loader->open(path, 1, 0);
	if (fd < 0)
		return fd;
	size = CALL(0x12b740, int (*)(int, int, int))(fd, 0, 2);
	CALL(0x12b740, int (*)(int, int, int))(fd, 0, 0);
	if (size <= 0 || size > IRX_LIMIT) {
		loader->close(fd);
		return -1;
	}
	CALL(0x12c1b8, int (*)(void))();
	remote = CALL(0x12c240, void* (*)(int))((size + 15) & ~15);
	if (!remote) {
		loader->close(fd);
		return -1;
	}
	while (received < size) {
		chunk = size - received;
		if (chunk > IRX_BYTES)
			chunk = IRX_BYTES;
		memset(image, 0, (chunk + 15) & ~15);
		for (used = 0; used < chunk; used += n) {
			n = loader->read(fd, image + used, chunk - used);
			if (n <= 0)
				goto done;
		}
		dma.src = image;
		dma.dest = (u8*)remote + received;
		dma.size = (chunk + 15) & ~15;
		dma.attr = 0;
		CALL(0x129ce0, void (*)(void*, int))(image, dma.size);
		transfer = CALL(0x126f60, int (*)(SifDmaTransfer_t*, int))(&dma, 1);
		if (!transfer)
			goto done;
		for (n = 0; CALL(0x126f40, int (*)(int))(transfer) >= 0; ++n) {
			if (n == 1000000) {
				loader->close(fd);
				return -1; /* Quarantine in-flight DMA and both buffers. */
			}
		}
		received += chunk;
	}
	id = CALL(0x12cbe0, int (*)(void*, int, const char*, int*))(remote, 0, 0, &result);
done:
	CALL(0x12c3a8, int (*)(void*))(remote);
	loader->close(fd);
	loader->print("online: %s id=%d result=%d\n", path, id, result);
	return result < 0 ? result : id;
}

/*
 * NAME : onlineTransportInit
 * DESCRIPTION : Initialize local RPC bookkeeping once.
 * NOTES : No drivers are loaded until hosting or joining is requested.
 * ARGS : Loader API.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void onlineTransportInit(const moduleApi_t* api)
{
	loader = api;
}

/*
 * NAME : onlineTransportSupported
 * DESCRIPTION : Keep driver startup out of the boot and multiplayer overlays.
 * NOTES : NTSC-U Deadlocked backend; other games need their own backend verification.
 * ARGS :
 * RETURN : Nonzero for a loaded campaign or Cooperative map.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int onlineTransportSupported(void)
{
	int level = *(volatile int*)0x21de10;
	return level >= 1 && level <= 35;
}

/*
 * NAME : onlineTransportReady
 * DESCRIPTION : Load drivers and bind the nonblocking network RPC service.
 * NOTES : Failure is terminal until restart; never reset or unload the game IOP.
 * ARGS :
 * RETURN : One ready, zero pending, negative failed.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int onlineTransportReady(void)
{
	if (failed)
		return -1;
	if (stage < 5) {
		if (install(drivers[stage]) < 0) {
			failed = 1;
			return -1;
		}
		++stage;
		return 0;
	}
	if (pending) {
		if (rpcBusy(&client)) {
			if (++retries > 1800)
				failed = 1;
			return failed ? -1 : 0;
		}
		pending = 0;
		if (client.server)
			return 1;
	}
	if (++retries > 1800 || bindRpc(&client, ONLINE_RPC_SERVICE, SIF_RPC_M_NOWAIT) < 0) {
		failed = 1;
		return -1;
	}
	pending = 1;
	return 0;
}

/*
 * NAME : onlineTransportRequest
 * DESCRIPTION : Submit one asynchronous UDP operation.
 * NOTES : Buffers remain owned until the reply completes.
 * ARGS : Command and request.
 * RETURN : One submitted, zero busy, negative failure.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int onlineTransportRequest(int command, const onlineRpc_t* value)
{
	if (failed || !client.server)
		return -1;
	if (pending || rpcBusy(&client))
		return 0;
	memcpy(&request, value, sizeof(request));
	if (callRpc(&client, command, SIF_RPC_M_NOWAIT, &request, sizeof(request), &response, sizeof(response), 0, 0) < 0) {
		failed = 1;
		return -1;
	}
	pending = 1;
	retries = 0;
	return 1;
}

/*
 * NAME : onlineTransportReply
 * DESCRIPTION : Collect a completed RPC without waiting on the game thread.
 * NOTES : Timed-out buffers stay quarantined until restart.
 * ARGS : Reply destination.
 * RETURN : One completed, zero pending, negative failure.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int onlineTransportReply(onlineRpc_t* value)
{
	if (failed)
		return -1;
	if (!pending)
		return 0;
	if (rpcBusy(&client)) {
		if (++retries > 1800)
			failed = 1;
		return failed ? -1 : 0;
	}
	memcpy(value, &response, sizeof(response));
	pending = 0;
	return 1;
}
