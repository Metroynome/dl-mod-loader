/* Offline map transitions based on Horizon Deadlocked's mapHopTo.
 * Queue from menu/PINE, then perform the transition on the next loader update. */
#include "maploader.h"
#include <libdl/game.h>
#include <libdl/gamesettings.h>
#include <libdl/player.h>

#define WORD(address) (*(volatile u32*)(address))

typedef struct hopPatch {
	/* 0x00 */ u32 address;
	/* 0x04 */ u32 value;
	/* 0x08 */ u32 original;
} hopPatch_t;

static char* hopMessage(int message);

static hopPatch_t patches[] = {
	{0x4e4180, 0, 0},
	{0x4e4184, 0, 0},
	{0x5ab1e8, 0, 0},
	{0x5a99a8, 0x24060000, 0},
	{0x62e4f8, 0, 0},
	{0x62e508, 0, 0},
	{0x62e4a0, 0, 0},
	{0x62e4d8, 0, 0},
	{0x62e4e0, 0, 0}
};
static int queued, transitioning, patched, ticks;
static u32 targetBase;
static int quickLoad;
static const int messageIds[] = {
	0x1a5a, 0x116d, 0x0f41, 0x2ea3, 0x1005, 0x0f51, 0x2ec5,
	0x3486, 0x341f, 0x3420, 0x0f3f, 0x0f3e, 0x2ea1, 0x23f3,
	0x0f31, 0x2ec7, 0x2ec8, 0x1328, 0x311a, 0x3487, 0x0f2c,
	0x3488, 0x3421, 0x2ee2, 0x2ea4, 0x2ea5, 0x311d, 0x3120,
	0x311b, 0x311e, 0x311f, 0x2ec1, 0x2ec2, 0x2ec3, 0x3121
};
static char messages[sizeof(messageIds) / sizeof(messageIds[0])][128];
int mapHopSawLoading, mapHopResult;

/* The online WAD reuses the old level's message storage during a hop.
 * Follow Horizon's temporary empty-string lookup until the new overlay loads. */
static char* hopMessage(int message)
{
	u32 i;
	if (!quickLoad)
		for (i = 0; i < sizeof(messageIds) / sizeof(messageIds[0]); ++i)
			if (messageIds[i] == message)
				return messages[i];
	return "";
}

int mapForgeActive(void)
{
	const forgeApi_t* forge = mapApi->findService(FORGE_SERVICE_ID, FORGE_API_VERSION);
	return forge && forge->version == FORGE_API_VERSION && forge->size >= sizeof(*forge) && forge->active();
}

int mapHopInGame(void)
{
	return mapApi->context == DL_CONTEXT_LOCAL && isInGame() && mapRetailName(WORD(0x21de10));
}

int mapHopAvailable(void)
{
	return mapForgeActive() && mapHopInGame() && !transitioning && !mapLoading();
}

int mapHopRequest(int index)
{
	u32 base;
	GameSettings* settings = gameGetSettings();
	if (!mapHopAvailable() || !mapHooksReady() || mapScanActive() || !settings)
		return 0;
	if (index >= 0) {
		if (index >= mapCount || !mapValidate(&mapRecords[index]))
			return 0;
		base = mapRecords[index].base;
	} else {
		base = -index;
		if (!mapRetailName(base))
			return 0;
	}
	mapSelection = index >= 0 ? index : -1;
	mapClearLoad();
	targetBase = base;
	settings->GameLevel = base;
	queued = transitioning = 1;
	mapHopResult = 1;
	mapHopSawLoading = ticks = 0;
	return 1;
}

int mapStartGame(void)
{
	GameSettings* settings = gameGetSettings();
	if (mapApi->context != DL_CONTEXT_LOCAL || !mapStaging() || !settings ||
	    transitioning || mapScanActive() || mapLoading() || !mapHooksReady())
		return 0;
	/* Local Play's confirmation handler calls this with the staging object.
	 * Queue it so a menu or mailbox callback never starts loading mid-update. */
	if (WORD(0x747f28) != 0x27bdff80 || !mapRetailName(settings->GameLevel))
		return 0;
	targetBase = settings->GameLevel;
	queued = 2;
	transitioning = mapHopResult = 1;
	mapHopSawLoading = ticks = 0;
	return 1;
}

static void flush(void)
{
	mapApi->flushCache(0);
	mapApi->flushCache(2);
}

static void restorePatches(void)
{
	u32 i;
	if (!patched)
		return;
	for (i = 0; i < sizeof(patches) / sizeof(patches[0]); ++i)
		if (WORD(patches[i].address) == patches[i].value)
			WORD(patches[i].address) = patches[i].original;
	patched = 0;
	flush();
}

static void hop(void)
{
	GameSettings* settings = gameGetSettings();
	Player** players = playerGetAll();
	u32 i;
	void** bins = (void**)0x1dfbf0;
	void** textures = (void**)0x1dfc18;
	u8* buffer = (u8*)WORD(0x240d78);
	u8* wad = (u8*)(WORD(0x21dd90) - 0x7d0000);
	int (*decompress)(void*, void*) = (void*)0x4f5140;
	const forgeApi_t* forge = mapApi->findService(FORGE_SERVICE_ID, FORGE_API_VERSION);
	quickLoad = forge && forge->size >= sizeof(*forge) && forge->quickLoadMaps && *forge->quickLoadMaps;
	/* Copy labels before the online WAD overwrites the old message table. */
	if (!quickLoad) {
		for (i = 0; i < sizeof(messageIds) / sizeof(messageIds[0]); ++i) {
			const char* text = ((char* (*)(int))0x4e4180)(messageIds[i]);
			u32 n = 0;
			while (text && text[n] && n + 1 < sizeof(messages[i])) {
				messages[i][n] = text[n];
				++n;
			}
			messages[i][n] = 0;
		}
	}
	patches[0].value = 0x08000000 | (((u32)hopMessage >> 2) & 0x03ffffff);
	for (i = 0; i < sizeof(patches) / sizeof(patches[0]); ++i) {
		patches[i].original = WORD(patches[i].address);
		if (i >= 4 && !quickLoad)
			continue;
		WORD(patches[i].address) = patches[i].value;
	}
	patched = 1;
	flush();
	for (i = 0; i < GAME_MAX_PLAYERS; ++i) {
		Player* player = players[i];
		if (settings->PlayerStates[i] > 0 && settings->PlayerClients[i] >= 0)
			settings->PlayerStates[i] = 7;
		else if (!i)
			settings->PlayerStates[i] = 8;
		if (playerIsValid(player) && player->pNetPlayer)
			player->pNetPlayer->bCallbackCalled = 0;
	}
	settings->PlayerCountAtStart = settings->PlayerCount;
	settings->GameLoadStartTime = gameGetTime();
	WORD(0x2209c0) = (u32)wad;
	((void (*)(int))0x1634a8)(1);
	((void (*)(int, void*, int, int, int, void*, void*))0x163808)
	    (0, wad, WORD(0x1ce410) + WORD(0x1ce40c), WORD(0x1ce414), 0, 0, 0);
	((void (*)(int))0x1634a8)(1);
	for (i = 0; i < GAME_MAX_PLAYERS; ++i) {
		int skin = settings->PlayerSkins[i];
		int local = settings->PlayerClients[i] == gameGetMyClientId();
		u8* definition;
		int size;
		if (skin < 0)
			continue;
		definition = wad + 0x250 + (local ? 0 : 0x14) + 0x28 * skin;
		size = decompress(wad + *(u32*)(definition + 4), buffer);
		bins[i] = buffer;
		buffer += (size + 63) & ~63;
		size = decompress(wad + *(u32*)(definition + 12), buffer);
		textures[i] = buffer;
		buffer += (size + 63) & ~63;
	}
	((void (*)(int, int, int))0x4e2410)(targetBase, 1, -1);
}

void mapHopOnLoad(void)
{
	if (transitioning)
		mapHopSawLoading = 1;
}

void mapHopTick(void)
{
	if (!transitioning)
		return;
	if (queued) {
		int startGame = queued == 2;
		queued = 0;
		if (startGame) {
			if (!mapStaging() || mapApi->context != DL_CONTEXT_LOCAL || WORD(0x747f28) != 0x27bdff80) {
				mapHopCancel();
				return;
			}
			((void (*)(void*))0x747f28)((void*)WORD(0x11c7108));
		} else {
			hop();
		}
		return;
	}
	if (mapHopSawLoading && isInGame() &&
	    (WORD(0x21de10) == targetBase || WORD(0x21de10) == targetBase + 20)) {
		restorePatches();
		transitioning = 0;
		mapHopResult = 2;
		mapMessage("Map loaded");
	} else if (++ticks > 3600) {
		mapHopCancel();
		mapMessage("Map transition timed out");
	}
}

void mapHopCancel(void)
{
	restorePatches();
	queued = transitioning = 0;
	if (mapHopResult == 1)
		mapHopResult = -1;
}
