/* NTSC-U Deadlocked loading hooks, derived from Horizon's maploader.c.
 * File reads use the loader's resident hostfs API; no Horizon state is needed. */
#include "maploader.h"
#include <libdl/game.h>
#include <libdl/gamesettings.h>

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
static void quickLoadBegin(void);
static void quickLoadEnd(void);

int mapAllowOnline;
u32 mapSceneRequest;
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
	switch (base) {
	case 41: return "Battledome Tower";
	case 42: return "Catacrom Graveyard";
	case 44: return "Sarathos Swamp";
	case 45: return "Dark Cathedral";
	case 46: return "Temple of Shaar";
	case 47: return "Valix Lighthouse";
	case 48: return "Mining Facility";
	case 50: return "Torval Ruins";
	case 51: return "Tempus Station";
	case 53: return "Maraxus Prison";
	case 54: return "Ghost Station";
	default: return 0;
	}
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
	if (!mapStaging() || !settings || !mapRetailName(base))
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

void mapUpdateName(void)
{
	u32 staging, label;
	const char* name;
	int i;
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
	/* NTSC-U Local Play: child 0 is the map-name text element.
	 * Its inline text starts at 0x60, independently of online staging. */
	label = WORD(staging + 0xb0);
	if (!label)
		return;
	name = mapSelection >= 0 && mapSelection < mapCount ? mapRecords[mapSelection].name : mapRetailName(mapCurrentBase());
	if (!name)
		return;
	for (i = 0; i < 31 && name[i]; ++i)
		((char*)label)[0x60 + i] = name[i];
	((char*)label)[0x60 + i] = 0;
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
		quickLoadBegin();
	((void (*)(u32, u32, void*))0x159a00)(start, count, dest);
	mapSceneRequest = level;
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

static void quickLoadBegin(void)
{
	const forgeApi_t* forge = mapApi->findService(FORGE_SERVICE_ID, FORGE_API_VERSION);
	u32 i;
	if (!forge || forge->size < sizeof(*forge) || !forge->active() ||
	    !forge->quickLoadMaps || !*forge->quickLoadMaps)
		return;
	/* Horizon's menu-overlay loading loops. Never patch these addresses in
	 * another overlay: they contain unrelated gameplay instructions/data. */
	for (i = 0; i < sizeof(quickLoadSites) / sizeof(quickLoadSites[0]); ++i) {
		if (WORD(quickLoadSites[i]) == JAL(0x138d70)) {
			WORD(quickLoadSites[i]) = 0;
			quickLoadPatched |= 1 << i;
		}
	}
	mapApi->flushCache(0);
	mapApi->flushCache(2);
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
	if (isInGame() && mapHopResult != 1)
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
