/* NTSC-U Deadlocked loading hooks, derived from Horizon's maploader.c.
 * File reads use the loader's resident hostfs API; no Horizon state is needed. */
#include "maploader.h"
#include <libdl/game.h>
#include <libdl/gamesettings.h>
#include <libdl/map.h>

#define WORD(address) (*(volatile u32*)(address))
#define JAL(address) (0x0c000000 | ((u32)(address) >> 2))

typedef void (*soundCallback_t)(int, int);

typedef struct loadHook {
	/* 0x00 */ u32 address;
	/* 0x04 */ u32 original;
	/* 0x08 */ void* replacement;
} loadHook_t;

static void hookedTable(u32 start, u32 count, u8* dest, u32 level);
static void gameLoad(u64 a0, void* dest, u32 start, u32 count, u64 t0, u64 t1, u64 t2);
static void menuLoad(u64 a0, void* dest, u32 start, u32 count, u64 t0, u64 t1, u64 t2);
static u32 hookedCheck(void);
static void hookedSound(int location, int offset, soundCallback_t callback, u64 data);
static char* hookedName(char* dest, const char* source);
static void hookedRadar(u64 a0, void* dest, u32 start, u32 count, u64 t0, u64 t1, u64 t2);
static void hookedBackground(u64 a0, void* dest, u32 start, u32 count, u64 t0, u64 t1, u64 t2);
static void gameSoundInit(void);
static void menuSoundInit(void);
static void gameMusicInit(void);
static void menuMusicInit(void);
static u64 hookedDiscLoad(u64 a0, u64 a1, u64 a2, u64 a3, u64 t0, u64 t1, u64 t2);
static void quickLoadEnd(void);

/* Alphabetical display order; background indices follow the resident disc table. */
const mapRetail_t mapRetailMaps[MAP_RETAIL_COUNT] = {
	{MAP_ID_BATTLEDOME, 0},
	{MAP_ID_CATACROM, 1},
	{MAP_ID_DC, 3},
	{MAP_ID_GS, 10},
	{MAP_ID_MARAXUS, 9},
	{MAP_ID_MF, 6},
	{MAP_ID_SARATHOS, 2},
	{MAP_ID_SHAAR, 4},
	{MAP_ID_TEMPUS, 8},
	{MAP_ID_TORVAL, 7},
	{MAP_ID_VALIX, 5}
};

int mapAllowOnline;
static char retailNames[MAP_RETAIL_COUNT][32];
static int busy, overrideActive, hostLoaded, soundLoaded, originalBase = -1;
static void* soundBuffer;
static u32 previewStaging, previewBase;
static const u32 quickLoadSites[] = {0x6868a0, 0x6865b0, 0x5b0354, 0x5b047c, 0x5b04b4};
static u32 quickLoadPatched;
static loadHook_t hooks[] = {
	{0x159b20, JAL(0x159a00), hookedTable},
	{0x5cfb48, JAL(0x163840), menuLoad},
	{0x5cf9b0, JAL(0x163928), hookedCheck},
	{0x5fec74, JAL(0x1582a8), hookedSound},
	{0x7055b4, JAL(0x11a9ec), hookedName},
	{0x4e9ed0, JAL(0x163840), gameLoad},
	{0x4e9d38, JAL(0x163928), hookedCheck},
	{0x51f80c, JAL(0x1582a8), hookedSound},
	{0x62ce64, JAL(0x11a9ec), hookedName},
	{0x557580, JAL(0x163808), hookedRadar},
	{0x4ea028, JAL(0x58d5d0), gameSoundInit},
	{0x4ea05c, JAL(0x51f7b8), gameMusicInit},
	{0x5cfca0, JAL(0x66b170), menuSoundInit},
	{0x5cfcd4, JAL(0x5fec20), menuMusicInit},
	{0x705554, JAL(0x163808), hookedBackground},
	{0x62ce04, JAL(0x163808), hookedBackground},
	{0x163814, JAL(0x163840), hookedDiscLoad}
};

const char* mapRetailName(u32 base)
{
	u32 i;
	for (i = 0; i < MAP_RETAIL_COUNT; ++i) {
		if (mapRetailMaps[i].base == base) {
			/* libdl's help lookup returns NULL during scene loading. Preserve
			 * its text in module memory before the source string table is reused. */
			const char* name = mapGetName(base);
			if (name && name[0]) {
				u32 n = 0;
				while (name[n] && n + 1 < sizeof(retailNames[i])) {
					retailNames[i][n] = name[n];
					++n;
				}
				retailNames[i][n] = 0;
			}
			return retailNames[i];
		}
	}
	return 0;
}

int mapMultiplayerReady(void)
{
	return mapApi && mapApi->mode == DL_MODE_MULTIPLAYER && WORD(0x12b980) == 0x27bdff20;
}

int mapOnlineDisabled(void)
{
	return !mapAllowOnline && mapApi->context == DL_CONTEXT_ONLINE;
}

int mapStaging(void)
{
	u32 active;
	if (!mapMultiplayerReady() || WORD(0x21de10) != 0 || WORD(0x22026c) != 1)
		return 0;
	active = WORD(0x11c7108);
	if (active && active == WORD(0x11c70c8))
		return 1;
	return 0;
}

u32 mapCurrentBase(void)
{
	GameSettings* settings = gameGetSettings();
	return settings ? settings->GameLevel : 0;
}

int mapChooseBase(u32 base)
{
	GameSettings* settings = gameGetSettings();
	if (!mapStaging() || !settings || !mapIdToMask(base))
		return 0;
	if (originalBase < 0)
		originalBase = settings->GameLevel;
	settings->GameLevel = base;
	return 1;
}

int mapChooseRetail(u32 base)
{
	if (!mapChooseBase(base))
		return 0;
	originalBase = -1;
	mapSelection = -1;
	mapClearLoad();
	return 1;
}

void mapRestoreBase(void)
{
	GameSettings* settings = gameGetSettings();
	if (originalBase >= 0 && mapStaging() && settings)
		settings->GameLevel = originalBase;
	originalBase = -1;
}

/* Local Play text widgets own a 32-byte inline label at 0x60. */
static void stagingText(u32 label, const char* text)
{
	int i;
	if (!label || !text)
		return;
	for (i = 0; i < 31 && text[i]; ++i)
		((char*)label)[0x60 + i] = text[i];
	((char*)label)[0x60 + i] = 0;
}

void mapUpdateName(void)
{
	u32 staging;
	const char* name;
	GameSettings* settings;
	if (!mapStaging())
		return;
	staging = WORD(0x11c70c8);
	if (previewStaging != staging || previewBase != mapCurrentBase()) {
		/* Same map-dependent image setup used by Local Play's entry handler. */
		if (WORD(0x7483a8) == 0x27bdfff0)
			((void (*)(void*, int))0x7483a8)((void*)staging, mapCurrentBase());
		previewStaging = staging;
		previewBase = mapCurrentBase();
	}
	/* NTSC-U Local Play children 0/1 are the map/mode text elements,
	 * independently of the online staging layout. */
	name = mapSelection >= 0 && mapSelection < mapCount ? mapRecords[mapSelection].name : mapRetailName(mapCurrentBase());
	stagingText(WORD(staging + 0xb0), name);
	settings = gameGetSettings();
	if (settings)
		stagingText(WORD(staging + 0xb4), gameGetGameModeName(settings->GameRules));
}

int mapLoading(void)
{
	return busy;
}

void mapClearLoad(void)
{
	overrideActive = hostLoaded = soundLoaded = 0;
	soundBuffer = 0;
}

static mapRecord_t* selectedMap(void)
{
	return mapSelection >= 0 && mapSelection < mapCount ? &mapRecords[mapSelection] : 0;
}

static void hookedTable(u32 start, u32 count, u8* dest, u32 level)
{
	mapRecord_t* map = selectedMap();
	if (level && map)
		mapQuickLoadBegin();
	((void (*)(u32, u32, void*))0x159a00)(start, count, dest);
	mapClearLoad();
	if (map && (level == map->base || level == map->base + 20) && !mapOnlineDisabled()) {
		overrideActive = 1;
		((u32*)dest)[7] = map->wad / 0x800 + 1;
	}
	mapHopOnLoad();
}

static void hookedSound(int location, int offset, soundCallback_t callback, u64 data)
{
	if (overrideActive && soundBuffer) {
		if (callback && !soundLoaded) {
			int bank = ((int (*)(void*))0x158400)(soundBuffer);
			callback(bank, data);
			soundLoaded = 1;
		}
		return;
	}
	((void (*)(int, int, soundCallback_t, u64))0x1582a8)(location, offset, callback, data);
}

static void hookedLoad(u64 a0, void* dest, u32 start, u32 count, u64 t0, u64 t1, u64 t2, int gameOverlay)
{
	mapRecord_t* map = selectedMap();
	if (overrideActive && map) {
		busy = 1;
		if (map->sound) {
			if (!mapReadPart(map, ".sound", dest, map->sound))
				goto failed;
			soundBuffer = dest;
			if (gameOverlay) {
				((void (*)(void))0x58d5d0)();
				((void (*)(void))0x51f7b8)();
			} else {
				((void (*)(void))0x66b170)();
				((void (*)(void))0x5fec20)();
			}
		}
		if (!mapReadPart(map, ".wad", dest, map->wad))
			goto failed;
		hostLoaded = 1;
		busy = 0;
		return;
	}
	((void (*)(u64, void*, u32, u32, u64, u64, u64))0x163840)(a0, dest, start, count, t0, t1, t2);
	return;
failed:
	busy = 0;
	mapClearLoad();
	mapMessage("Map read failed; loading retail map");
	((void (*)(u64, void*, u32, u32, u64, u64, u64))0x163840)(a0, dest, start, count, t0, t1, t2);
}

static void gameLoad(u64 a0, void* dest, u32 start, u32 count, u64 t0, u64 t1, u64 t2)
{
	hookedLoad(a0, dest, start, count, t0, t1, t2, 1);
}

static void menuLoad(u64 a0, void* dest, u32 start, u32 count, u64 t0, u64 t1, u64 t2)
{
	hookedLoad(a0, dest, start, count, t0, t1, t2, 0);
}

static u32 hookedCheck(void)
{
	return hostLoaded ? 0 : ((u32 (*)(void))0x163928)();
}

static void gameSoundInit(void)
{
	if (!(hostLoaded && soundLoaded))
		((void (*)(void))0x58d5d0)();
}

static void gameMusicInit(void)
{
	if (!(hostLoaded && soundLoaded))
		((void (*)(void))0x51f7b8)();
}

/* The source overlay remains loaded after isInGame() becomes false on exit.
 * Each call site must retain its own original function address. */
static void menuSoundInit(void)
{
	if (!(hostLoaded && soundLoaded))
		((void (*)(void))0x66b170)();
}

static void menuMusicInit(void)
{
	if (!(hostLoaded && soundLoaded))
		((void (*)(void))0x5fec20)();
}

static char* hookedName(char* dest, const char* source)
{
	int i;
	mapRecord_t* map = selectedMap();
	if (map)
		source = map->name;
	else {
		const char* retail = mapRetailName(mapCurrentBase());
		if (retail)
			source = retail;
	}
	for (i = 0; i < 31 && source[i]; ++i)
		dest[i] = source[i];
	dest[i] = 0;
	return dest;
}

/* Both staging starts and in-game hops load their background through this
 * CDVD call. Host .bg files need not include the disc's sector padding. */
static void hookedBackground(u64 a0, void* dest, u32 start, u32 count, u64 t0, u64 t1, u64 t2)
{
	mapRecord_t* map = selectedMap();
	if (map && !busy && !mapOnlineDisabled() && count && count <= 0x80000) {
		int fd, size, ok = 0;
		u32 capacity = count * 0x800;
		int (*seek)(int, int, int) = (void*)0x12b740;
		busy = 1;
		fd = mapOpenPart(map, ".bg");
		if (fd >= 0) {
			size = seek(fd, 0, 2);
			if (size > 0 && (u32)size <= capacity && seek(fd, 0, 0) == 0) {
				ok = mapReadExact(fd, dest, size);
				if (ok)
					memset((u8*)dest + size, 0, capacity - size);
			}
			mapApi->close(fd);
		}
		busy = 0;
		if (ok)
			return;
	}
	/* The source overlay passes its old background during a hop. Resolve the
	 * destination through the same resident disc table as the native loader.
	 * Keep the caller's buffer capacity as an upper bound. */
	if (mapHopResult == 1 && !mapOnlineDisabled()) {
		u32 i, base = mapCurrentBase();
		for (i = 0; i < MAP_RETAIL_COUNT; ++i) {
			if (mapRetailMaps[i].base == base) {
				u32 offset = mapRetailMaps[i].background * 8;
				u32 sectors = WORD(0x1ce41c + offset);
				if (sectors && sectors <= count) {
					start = WORD(0x1ce40c) + WORD(0x1ce418 + offset);
					count = sectors;
				}
				break;
			}
		}
	}
	((void (*)(u64, void*, u32, u32, u64, u64, u64))0x163808)(a0, dest, start, count, t0, t1, t2);
}

static void hookedRadar(u64 a0, void* dest, u32 start, u32 count, u64 t0, u64 t1, u64 t2)
{
	mapRecord_t* map = selectedMap();
	if (overrideActive && map && map->radar && mapReadPart(map, ".map", dest, map->radar))
		return;
	((void (*)(u64, void*, u32, u32, u64, u64, u64))0x163808)(a0, dest, start, count, t0, t1, t2);
}

/* The new overlay can request its radar before the next module update.
 * Horizon refreshes hooks here for that reason; retain the original result. */
static u64 hookedDiscLoad(u64 a0, u64 a1, u64 a2, u64 a3, u64 t0, u64 t1, u64 t2)
{
	u64 result = ((u64 (*)(u64, u64, u64, u64, u64, u64, u64))0x163840)(a0, a1, a2, a3, t0, t1, t2);
	mapInstallHooks();
	return result;
}

void mapQuickLoadBegin(void)
{
	u32 i, previous = quickLoadPatched;
	if (!mapQuickLoadEnabled())
		return;
	/* These are menu-overlay loading loops. The early fade occurs before
	 * the level table is read, so enable them as soon as loading begins. */
	if (WORD(0x747f28) != 0x27bdff80 || WORD(0x5cfb48) != JAL(menuLoad))
		return;
	for (i = 0; i < sizeof(quickLoadSites) / sizeof(quickLoadSites[0]); ++i) {
		if (WORD(quickLoadSites[i]) == JAL(0x138d70)) {
			WORD(quickLoadSites[i]) = 0;
			quickLoadPatched |= 1 << i;
		}
	}
	if (quickLoadPatched != previous) {
		mapApi->flushCache(0);
		mapApi->flushCache(2);
	}
}

static void quickLoadEnd(void)
{
	u32 i;
	if (!quickLoadPatched)
		return;
	/* Once the menu overlay is replaced, a zero at an old patch address
	 * belongs to the new overlay and must not be restored as an instruction. */
	if (WORD(0x747f28) == 0x27bdff80 && WORD(0x5cfb48) == JAL(menuLoad))
		for (i = 0; i < sizeof(quickLoadSites) / sizeof(quickLoadSites[0]); ++i)
			if ((quickLoadPatched & (1 << i)) && WORD(quickLoadSites[i]) == 0)
				WORD(quickLoadSites[i]) = JAL(0x138d70);
	quickLoadPatched = 0;
	mapApi->flushCache(0);
	mapApi->flushCache(2);
}

void mapInstallHooks(void)
{
	u32 i;
	int changed = 0;
	if (!mapMultiplayerReady() || mapOnlineDisabled())
		return;
	if (isSceneLoading() && selectedMap())
		mapQuickLoadBegin();
	if (!selectedMap() || (isInGame() && mapHopResult != 1))
		quickLoadEnd();
	for (i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i) {
		loadHook_t* hook = &hooks[i];
		if (WORD(hook->address) == hook->original) {
			WORD(hook->address) = JAL(hook->replacement);
			changed = 1;
		}
	}
	if (changed) {
		mapApi->flushCache(0);
		mapApi->flushCache(2);
	}
}

int mapHooksReady(void)
{
	u32 index = isInGame() ? 5 : 1;
	return WORD(hooks[0].address) == JAL(hookedTable) && WORD(hooks[index].address) == JAL(hooks[index].replacement);
}

void mapDisableHooks(void)
{
	u32 i;
	quickLoadEnd();
	for (i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i) {
		loadHook_t* hook = &hooks[i];
		if (WORD(hook->address) == JAL(hook->replacement))
			WORD(hook->address) = hook->original;
	}
	mapApi->flushCache(0);
	mapApi->flushCache(2);
	mapClearLoad();
}
