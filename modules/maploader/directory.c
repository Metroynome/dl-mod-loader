/* Verified NTSC-U multiplayer SDK entry points for the directory RPC helper. */
#include "maploader.h"
#include <sifrpc.h>
#include <sifcmd.h>
#include <kernel.h>

#define CALL(a, t) ((t)(a))
#define bindRpc    CALL(0x12a538, int (*)(SifRpcClientData_t*, int, int))
#define callRpc    CALL(0x12a718, int (*)(SifRpcClientData_t*, int, int, void*, int, void*, int, void*, void*))
#define rpcBusy    CALL(0x12a918, int (*)(SifRpcClientData_t*))
static SifRpcClientData_t client __attribute__((aligned(64)));
static int request[16] __attribute__((aligned(64)));
static mapScanReply_t reply __attribute__((aligned(64)));
static int pending, loading, attempted, bindRetries;

void* mapAlloc(u32 size)
{
	return CALL(0x11ef60, void* (*)(u32))(size);
}

void mapFree(void* p)
{
	if (p && mapApi->mode == DL_MODE_MULTIPLAYER)
		CALL(0x11efb0, void (*)(void*))(p);
}

static int install(void)
{
	int fd, size, id = -1, result = -1, q, n;
	void *raw = 0, *aligned, *remote = 0;
	SifDmaTransfer_t dma;
	fd = mapApi->open("host0:dl/mods/utils/mapscan.irx", 1, 0);
	if (fd < 0)
		return -1;
	size = CALL(0x12b740, int (*)(int, int, int))(fd, 0, 2);
	if (size <= 0 || size > 0x10000)
		goto done;
	CALL(0x12b740, int (*)(int, int, int))(fd, 0, 0);
	raw = mapAlloc(size + 79);
	if (!raw)
		goto done;
	aligned = (void*)(((u32)raw + 63) & ~(u32)63);
	memset(aligned, 0, (size + 15) & ~15);
	if (!mapReadExact(fd, aligned, size))
		goto done;
	CALL(0x12c1b8, int (*)(void))();
	remote = CALL(0x12c240, void* (*)(int))((size + 15) & ~15);
	if (!remote)
		goto done;
	dma.src = aligned;
	dma.dest = remote;
	dma.size = (size + 15) & ~15;
	dma.attr = 0;
	CALL(0x129ce0, void (*)(void*, int))(aligned, dma.size);
	q = CALL(0x126f60, int (*)(SifDmaTransfer_t*, int))(&dma, 1);
	if (!q)
		goto done;
	/* The DMA must finish before either source or destination can be freed. */
	for (n = 0; CALL(0x126f40, int (*)(int))(q) >= 0; ++n) {
		if (n == 1000000) {
			raw = remote = 0;
			goto done;
		} /* Quarantine in-flight DMA buffers on failure. */
	}
	/* The 0x12c2c0 wrapper discards the result pointer. This variant
	 * forwards it to the shared implementation at 0x12bd80. */
	id = CALL(0x12cbe0, int (*)(void*, int, const char*, int*))(remote, 0, 0, &result);
	if (result < 0)
		id = -1;
done:
	if (remote)
		CALL(0x12c3a8, int (*)(void*))(remote);
	mapFree(raw);
	mapApi->close(fd);
	mapApi->print("maploader: directory helper id=%d result=%d\n", id, result);
	return id;
}

int mapDirectoryIdle(void)
{
	return !rpcBusy(&client);
}

void mapDirectoryReset(void)
{
	pending = loading = attempted = bindRetries = 0;
	memset(&client, 0, sizeof(client));
}

/* 0 pending, 1 ready, -1 failure. No unbounded server-binding loop. */
int mapDirectoryReady(void)
{
	if (pending) {
		if (rpcBusy(&client))
			return 0;
		pending = 0;
		if (client.server)
			return 1;
		/* The new IOP thread may register its RPC server a few frames later. */
		if (attempted)
			return ++bindRetries >= 120 ? -1 : 0;
		loading = 1;
		return 0;
	}
	if (loading) {
		loading = 0;
		attempted = 1;
		if (install() < 0)
			return -1;
	}
	memset(&client, 0, sizeof(client));
	if (bindRpc(&client, MAP_DIRECTORY_SERVICE, SIF_RPC_M_NOWAIT) < 0)
		return -1;
	pending = 1;
	return 0;
}

int mapDirectoryRequest(int command)
{
	if (callRpc(&client, command, SIF_RPC_M_NOWAIT, request, sizeof(request), &reply, sizeof(reply), 0, 0) < 0)
		return 0;
	return 1;
}

int mapDirectoryPoll(char* name)
{
	u32 i;
	if (rpcBusy(&client))
		return MAP_DIRECTORY_PENDING;
	if (name) {
		for (i = 0; i < sizeof(reply.name); ++i)
			name[i] = reply.name[i];
		name[255] = 0;
	}
	return reply.status;
}
