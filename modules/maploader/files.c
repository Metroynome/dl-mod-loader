#include "maploader.h"
#include <libdl/map.h>

#ifndef MAP_SEEK
#define MAP_SEEK ((int (*)(int, int, int))0x12b740)
#endif
#define MAP_SCAN_STEPS (32)
#define MAP_SCAN_TICKS (589824) /* 2 ms at the EE's 294.912 MHz. */
#ifndef MAP_SCAN_CLOCK
#define MAP_SCAN_CLOCK() (scanClock())
#define MAP_SCAN_NATIVE_CLOCK (1)
#endif

typedef enum mapScanPhase {
	MAP_SCAN_IDLE = 0,
	MAP_SCAN_BIND = 1,
	MAP_SCAN_OPEN = 2,
	MAP_SCAN_READ = 3,
	MAP_SCAN_INSPECT = 4,
	MAP_SCAN_CLOSE = 5
} mapScanPhase_e;

#ifdef MAP_SCAN_NATIVE_CLOCK
static u32 scanClock(void);
#endif
/* Keep repeated catalog moves out of the scan state machine. */
void mapCopyRecord(mapRecord_t* to, const mapRecord_t* from) __attribute__((noinline));

const moduleApi_t* mapApi;
mapRecord_t* mapRecords;
int mapCount, mapSelection = -1;
int mapCatalogReady;
u32 mapCatalogRevision;
/* Scan work is batched within a small budget each Local Play update.
 * One retained allocation, refilled in place on explicit refresh. */
static char selected[FORGE_FILENAME_SIZE];
static mapScanPhase_e phase;
static int candidates, current, kept, waitTicks;

static int validName(const char* text, u32 size)
{
	u32 i;
	if (!text[0] || text[0] == '.')
		return 0;
	for (i = 0; i < size; ++i) {
		u8 c = text[i];
		if (!c)
			return 1;
		if (c < 32 || c > 126 || c == '/' || c == '\\' || c == ':' || c == '"' || c == '*' || c == '?')
			return 0;
	}
	return 0;
}

static void append(char* dst, u32* pos, const char* src)
{
	while (*src)
		dst[(*pos)++] = *src++;
	dst[*pos] = 0;
}

int mapReadExact(int fd, void* dest, u32 size)
{
	u8* p = dest;
	int quick = mapQuickLoadEnabled();
	while (size) {
		/* Horizon requests the remaining WAD in one operation. Avoid hundreds
		 * of EE/IOP round trips in quick mode; still handle partial reads. */
		u32 chunk = !quick && size > 0x10000 ? 0x10000 : size;
		int n = mapApi->read(fd, p, chunk);
		if (n <= 0 || (u32)n > chunk)
			return 0;
		p += n;
		size -= n;
	}
	return 1;
}

int mapOpenPart(const mapRecord_t* map, const char* extension)
{
	char path[176];
	u32 pos = 0;
	if (!validName(map->filename, sizeof(map->filename)))
		return -1;
	append(path, &pos, "host0:dl/");
	append(path, &pos, map->filename);
#if DL_MODULE_REGION == 2
	append(path, &pos, ".pal");
#endif
	append(path, &pos, extension);
	return mapApi->open(path, 1, 0);
}

int mapReadPart(const mapRecord_t* map, const char* extension, void* dest, u32 size)
{
	int fd = mapOpenPart(map, extension), ok;
	u8 tail;
	if (fd < 0)
		return 0;
	ok = mapReadExact(fd, dest, size) && mapApi->read(fd, &tail, 1) == 0;
	mapApi->close(fd);
	return ok;
}

/* Use the verified NTSC-U multiplayer seek entry point. Campaign has no
 * map-loading backend; the loader still owns ordinary hostfs file I/O. */
static int sizeMatches(const mapRecord_t* map, const char* extension, u32 size)
{
	int fd, result;
	if (!size)
		return 1;
	fd = mapOpenPart(map, extension);
	if (fd < 0)
		return 0;
	result = MAP_SEEK(fd, 0, 2);
	mapApi->close(fd);
	return result == (int)size;
}

int mapValidate(const mapRecord_t* map)
{
	if (!mapMultiplayerReady())
		return 0;
	return sizeMatches(map, ".wad", map->wad) && sizeMatches(map, ".world", map->world) &&
	       sizeMatches(map, ".sound", map->sound) && sizeMatches(map, ".map", map->radar);
}

/* .version metadata contains a 48-byte header, author[32],
 * description[256]. Older packs may contain just the header. */
int mapReadInfo(const mapRecord_t* map, char* author, char* description)
{
	char path[176], header[48];
	u32 pos = 0, i;
	int fd, ok;
	author[0] = description[0] = 0;
	if (!validName(map->filename, sizeof(map->filename)))
		return 0;
	append(path, &pos, "host0:dl/");
	append(path, &pos, map->filename);
	append(path, &pos, ".version");
	fd = mapApi->open(path, 1, 0);
	if (fd < 0)
		return 0;
	ok = mapReadExact(fd, header, 48) && mapReadExact(fd, author, 32) && mapReadExact(fd, description, 256);
	mapApi->close(fd);
	if (!ok) {
		author[0] = description[0] = 0;
		return 0;
	}
	author[31] = 0;
	description[255] = 0;
	for (i = 0; author[i]; ++i)
		if ((u8)author[i] < 32)
			author[i] = ' ';
	for (i = 0; description[i]; ++i)
		if ((u8)description[i] < 32 && description[i] != '\n')
			description[i] = ' ';
	return 1;
}

int mapCompareText(const char* a, const char* b)
{
	while (*a && *a == *b) {
		++a;
		++b;
	}
	return (u8)*a - (u8)*b;
}

static void copyName(char* to, const char* from)
{
	while ((*to++ = *from++))
		;
}

static int validRecord(const mapRecord_t* m)
{
	return validName(m->filename, sizeof(m->filename)) && validName(m->name, 32) && mapIdToMask(m->base) != 0 &&
	       (m->wad || m->world) && m->wad <= 0x1000000 && m->world <= 0x400000 && m->sound <= 0x400000 &&
	       m->radar <= 0x27400;
}

static int partSize(const mapRecord_t* m, const char* ext)
{
	int fd = mapOpenPart(m, ext), size;
	if (fd < 0)
		return 0;
	size = MAP_SEEK(fd, 0, 2);
	if (mapApi->close(fd) < 0)
		return -1;
	return size;
}

static int inspect(mapRecord_t* m)
{
	u8 data[384] __attribute__((aligned(64)));
	char path[176];
	u32 pos = 0;
	int fd, n, total = 0, code;
	append(path, &pos, "host0:dl/");
	append(path, &pos, m->filename);
	append(path, &pos, ".version");
	fd = mapApi->open(path, 1, 0);
	if (fd < 0)
		return 0;
	memset(data, 0, sizeof(data));
	while (total < (int)sizeof(data)) {
		n = mapApi->read(fd, data + total, sizeof(data) - total);
		if (n < 0 || n > (int)sizeof(data) - total) {
			mapApi->close(fd);
			return -1;
		}
		if (!n)
			break;
		total += n;
	}
	if (mapApi->close(fd) < 0)
		return -1;
	if (total < 48 || *(int*)data < 0 || *(short*)(data + 8))
		return 0;
	m->version = *(u32*)data;
	m->base = *(u32*)(data + 4);
	for (n = 0; n < 32 && data[16 + n]; ++n)
		m->name[n] = data[16 + n];
	code = mapOpenPart(m, ".code");
	if (code >= 0) {
		mapApi->close(code);
		return 0;
	}
	n = partSize(m, ".wad");
	if (n < 0)
		return -1;
	m->wad = n;
	n = partSize(m, ".world");
	if (n < 0)
		return -1;
	m->world = n;
	n = partSize(m, ".sound");
	if (n < 0)
		return -1;
	m->sound = n;
	n = partSize(m, ".map");
	if (n < 0)
		return -1;
	m->radar = n;
	if (!validRecord(m))
		return 0;
	return 1;
}

static void finish(int error)
{
	phase = MAP_SCAN_IDLE;
	if (error) {
		mapCount = 0;
		mapCatalogReady = 0;
		if (selected[0]) {
			mapRestoreBase();
			mapClearLoad();
		}
		selected[0] = 0;
		mapMessage("Map scan failed; use Refresh Custom Maps to retry");
	} else
		mapCatalogReady = 1;
}

void mapScanStart(void)
{
	if (phase || !mapMultiplayerReady())
		return;
	if (!mapDirectoryIdle()) {
		mapMessage("Directory service is still busy");
		return;
	}
	if (!mapRecords)
		mapRecords = mapAlloc(MAP_LIMIT * sizeof(mapRecord_t));
	if (!mapRecords) {
		mapMessage("Not enough memory to scan custom maps");
		return;
	}
	selected[0] = 0;
	if (mapSelection >= 0 && mapSelection < mapCount)
		copyName(selected, mapRecords[mapSelection].filename);
	mapCount = 0;
	mapSelection = -1;
	mapCatalogReady = 0;
	memset(mapRecords, 0, MAP_LIMIT * sizeof(mapRecord_t));
	candidates = current = kept = waitTicks = 0;
	++mapCatalogRevision; /* Re-read descriptions even when the catalog is unchanged. */
	mapDirectoryReset();
	phase = MAP_SCAN_BIND;
}

int mapScanActive(void)
{
	return phase != 0;
}

int mapScanCurrent(void)
{
	return current;
}

int mapScanTotal(void)
{
	return phase == MAP_SCAN_INSPECT ? candidates : 0;
}

void mapCopyRecord(mapRecord_t* to, const mapRecord_t* from)
{
	u32 i;
	u8* d = (u8*)to;
	const u8* s = (const u8*)from;
	for (i = 0; i < sizeof(*to); ++i)
		d[i] = s[i];
}

/* 2 made progress, 0 waiting, 1 completed, -1 failed. */
static int scanStep(void)
{
	int r, n, i;
	char name[256];
	mapRecord_t temp;
	if (!phase)
		return 0;
	if (phase == MAP_SCAN_BIND) {
		r = mapDirectoryReady();
		if (r < 0) {
			finish(1);
			return -1;
		}
		if (r > 0) {
			if (!mapDirectoryRequest(MAP_DIRECTORY_OPEN)) {
				finish(1);
				return -1;
			}
			phase = MAP_SCAN_OPEN;
			waitTicks = 0;
		}
		return r > 0 ? 2 : 0;
	}
	if (phase == MAP_SCAN_OPEN || phase == MAP_SCAN_READ || phase == MAP_SCAN_CLOSE) {
		r = mapDirectoryPoll(name);
		if (r == MAP_DIRECTORY_PENDING)
			return 0;
		waitTicks = 0;
		if (phase == MAP_SCAN_CLOSE) {
			if (r < 0) {
				finish(1);
				return -1;
			}
			phase = MAP_SCAN_INSPECT;
			return 2;
		}
		if (r < 0) {
			mapDirectoryRequest(MAP_DIRECTORY_CLOSE);
			finish(1);
			return -1;
		}
		if (phase == MAP_SCAN_READ && !r) {
			if (!mapDirectoryRequest(MAP_DIRECTORY_CLOSE)) {
				finish(1);
				return -1;
			}
			phase = MAP_SCAN_CLOSE;
			return 2;
		}
		if (phase == MAP_SCAN_READ) {
			for (n = 0; name[n]; ++n)
				;
			if (n > 8 && n < (int)FORGE_FILENAME_SIZE + 8 && !mapCompareText(name + n - 8, ".version")) {
				name[n - 8] = 0;
				if (validName(name, FORGE_FILENAME_SIZE)) {
					if (candidates == MAP_LIMIT) {
						mapDirectoryRequest(MAP_DIRECTORY_CLOSE);
						finish(1);
						return -1;
					}
					copyName(mapRecords[candidates++].filename, name);
				}
			}
		}
		if (!mapDirectoryRequest(MAP_DIRECTORY_NEXT)) {
			finish(1);
			return -1;
		}
		phase = MAP_SCAN_READ;
		return 2;
	}
	if (phase == MAP_SCAN_INSPECT && current < candidates) {
		r = inspect(&mapRecords[current]);
		if (r < 0) {
			finish(1);
			return -1;
		}
		if (r)
			mapCopyRecord(&mapRecords[kept++], &mapRecords[current]);
		++current;
		waitTicks = 0;
		return 2;
	}
	/* Sort the completed catalog by display name. */
	for (n = 1; n < kept; ++n) {
		mapCopyRecord(&temp, &mapRecords[n]);
		i = n;
		while (i && mapCompareText(mapRecords[i - 1].name, temp.name) > 0) {
			mapCopyRecord(&mapRecords[i], &mapRecords[i - 1]);
			--i;
		}
		mapCopyRecord(&mapRecords[i], &temp);
	}
	mapCount = kept;
	for (i = 0; i < mapCount; ++i)
		if (!mapCompareText(selected, mapRecords[i].filename))
			mapSelection = i;
	if (selected[0] && mapSelection < 0) {
		mapRestoreBase();
		mapClearLoad();
	}
	++mapCatalogRevision;
	selected[0] = 0;
	mapMessage("Custom maps refreshed");
	finish(0);
	return 1;
}

#ifdef MAP_SCAN_NATIVE_CLOCK
/* EE Count advances at 294.912 MHz. Unsigned subtraction handles wraparound. */
static u32 scanClock(void)
{
	u32 ticks;
	__asm__ volatile("mfc0 %0, $9" : "=r"(ticks));
	return ticks;
}
#endif

int mapScanTick(void)
{
	u32 started;
	int result, steps = 0;

	if (!phase)
		return 0;
	/* Count timeout in frames, not polls within a frame. */
	if (++waitTicks > 6000) {
		finish(1);
		return -1;
	}

	started = MAP_SCAN_CLOCK();
	do {
		result = scanStep();
		if (result == -1 || result == 1)
			return result;
		/* Helper startup/rebind retries must still advance once per frame. */
		if (phase == MAP_SCAN_BIND)
			break;
		if (result == 2)
			++steps;
		/* Allow pending RPCs to finish within this budget instead of always
		 * waiting another frame. Reuse the same request and catalog buffers.
		 * Metadata reads are synchronous: an individual read may exceed it. */
	} while (steps < MAP_SCAN_STEPS && (u32)(MAP_SCAN_CLOCK() - started) < MAP_SCAN_TICKS);

	return 0;
}

void mapScanRelease(void)
{
	/* Never free an old multiplayer pointer through the campaign allocator. */
	if (phase && mapApi->mode == DL_MODE_MULTIPLAYER && mapDirectoryIdle())
		mapDirectoryRequest(MAP_DIRECTORY_CLOSE);
	phase = MAP_SCAN_IDLE;
	selected[0] = 0;
	mapFree(mapRecords);
	mapRecords = 0;
	mapCount = 0;
	mapSelection = -1;
	mapCatalogReady = 0;
}
