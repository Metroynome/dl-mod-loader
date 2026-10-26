/* Offline map transitions based on Horizon Deadlocked's mapHopTo.
 * Queue from menu/PINE, then perform the transition on the next loader update. */
#include "maploader.h"
#include <libdl/game.h>
#include <libdl/gamesettings.h>
#include <libdl/player.h>
#include <libdl/graphics.h>
#include <libdl/map.h>

#define WORD(address) (*(volatile u32*)(address))
#define HOP_REQUIRED_PATCHES (4)
#define HOP_READ_SECTORS (512)

typedef struct hopPatch {
	/* 0x00 */ u32 address;
	/* 0x04 */ u32 value;
	/* 0x08 */ u32 original;
} hopPatch_t;

static char* hopMessage(int message);
static void drawPreparingMap(int mode);
static void presentPreparingMap(void);
static void prepareLoadingScreen(const char* name, const GameSettings* settings);

/* The first four patches are required for every hop. The remaining entries
 * bypass VSync only when Quick Load is enabled for a custom destination. */
static hopPatch_t patches[] = {
	{0x4e4180, 0, 0},
	{0x4e4184, 0, 0},
	{0x5ab1e8, 0, 0},
	{0x5a99a8, 0x24060000, 0},
	{0x5a9ca8, 0, 0},
	{0x5a9f98, 0, 0},
	{0x5a9d08, 0, 0},
	{0x5a9d78, 0, 0},
	{0x4c496c, 0, 0},
	{0x4c4a94, 0, 0},
	{0x4c4acc, 0, 0}
};
static int queued, transitioning, ticks, sawLoading;
static u32 targetBase, appliedPatches;
static char loadingLabel[48];
static int preparationDone, preparationTotal;
static const int messageIds[] = {
	0x1a5a, 0x116d, 0x0f41, 0x2ea3, 0x1005, 0x0f51, 0x2ec5,
	0x3486, 0x341f, 0x3420, 0x0f3f, 0x0f3e, 0x2ea1, 0x23f3,
	0x0f31, 0x2ec7, 0x2ec8, 0x1328, 0x311a, 0x3487, 0x0f2c,
	0x3488, 0x3421, 0x2ee2, 0x2ea4, 0x2ea5, 0x311d, 0x3120,
	0x311b, 0x311e, 0x311f, 0x2ec1, 0x2ec2, 0x2ec3, 0x3121
};
static char messages[sizeof(messageIds) / sizeof(messageIds[0])][128];
int mapHopResult;

static void drawPreparingMap(int mode)
{
	void (*rectangle)(int top, int bottom, int left, int right, u32 color) = (void*)0x4c5790;
	int left = SCREEN_WIDTH * .21f;
	int right = SCREEN_WIDTH * .79f;
	/* The loading screen uses direct GS rectangles, not the level's effects
	 * renderer used by gfxScreenSpaceBox. Keep those effects out of this frame. */
	((void (*)(int))0x59b700)(mode);
	rectangle(0, SCREEN_HEIGHT, 0, SCREEN_WIDTH, 0x80000000);
	rectangle(SCREEN_HEIGHT * .40f, SCREEN_HEIGHT * .58f,
	          SCREEN_WIDTH * .18f, SCREEN_WIDTH * .82f, 0x80404040);
	gfxScreenSpaceText(SCREEN_WIDTH / 2, SCREEN_HEIGHT * .45f, 1, 1, 0x80ffffff,
	                   loadingLabel, -1, TEXT_ALIGN_MIDDLECENTER);
	rectangle(SCREEN_HEIGHT * .51f, SCREEN_HEIGHT * .545f, left, right, 0x80202020);
	if (preparationTotal > 0 && preparationDone > 0)
		rectangle(SCREEN_HEIGHT * .51f, SCREEN_HEIGHT * .545f,
		          left, left + (right - left) * preparationDone / preparationTotal, 0x80000040);
}

static void presentPreparingMap(void)
{
	u32 original = WORD(0x4c4a8c);
	u32 fade = WORD(0x4c4a44), delay = WORD(0x4c4a48);
	if (original != 0x0c166dc0 || fade != 0x3c030022 || delay != 0x8c633210 ||
	    WORD(0x4c4a90) != 0xaf82d891)
		return;
	/* Keep frame setup/submission, but skip the old-frame fade packet.
	 * Each progress frame clears the display instead of blending stale pixels. */
	WORD(0x4c4a44) = 0x10000011;
	WORD(0x4c4a48) = 0;
	WORD(0x4c4a90) = 0; /* The skipped packet does not advance the cursor. */
	WORD(0x4c4a8c) = 0x0c000000 | (((u32)drawPreparingMap >> 2) & 0x03ffffff);
	mapApi->flushCache(0);
	mapApi->flushCache(2);
	((void (*)(int, int))0x4c4930)(1, 0);
	WORD(0x4c4a8c) = original;
	WORD(0x4c4a44) = fade;
	WORD(0x4c4a48) = delay;
	WORD(0x4c4a90) = 0xaf82d891;
	mapApi->flushCache(0);
	mapApi->flushCache(2);
}

/* The online WAD overwrites the old level's message table during a hop.
 * Retain the loading-screen labels until the destination overlay is ready. */
static char* hopMessage(int message)
{
	u32 i;
	for (i = 0; i < sizeof(messageIds) / sizeof(messageIds[0]); ++i)
		if (messageIds[i] == message)
			return messages[i];
	return "";
}

const forgeApi_t* mapForgeApi(void)
{
	const forgeApi_t* forge = mapApi->findService(FORGE_SERVICE_ID, FORGE_API_VERSION);
	return forge && forge->version == FORGE_API_VERSION && forge->size >= sizeof(*forge) && forge->active() ? forge : 0;
}

int mapForgeActive(void)
{
	return mapForgeApi() != 0;
}

int mapQuickLoadEnabled(void)
{
	const forgeApi_t* forge = mapForgeApi();
	return mapSelection >= 0 && mapSelection < mapCount && forge &&
	       forge->quickLoadMaps && *forge->quickLoadMaps;
}

int mapHopInGame(void)
{
	return mapApi->context == DL_CONTEXT_LOCAL && isInGame() && mapIdToMask(WORD(0x21de10));
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
		if (!mapIdToMask(base))
			return 0;
	}
	mapSelection = index >= 0 ? index : -1;
	mapClearLoad();
	targetBase = base;
	settings->GameLevel = base;
	queued = transitioning = 1;
	mapHopResult = 1;
	sawLoading = ticks = 0;
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
	if (WORD(0x747f28) != 0x27bdff80 || !mapIdToMask(settings->GameLevel))
		return 0;
	targetBase = settings->GameLevel;
	queued = 2;
	transitioning = mapHopResult = 1;
	sawLoading = ticks = 0;
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
	if (!appliedPatches)
		return;
	for (i = 0; i < sizeof(patches) / sizeof(patches[0]); ++i)
		if ((appliedPatches & (1 << i)) && WORD(patches[i].address) == patches[i].value)
			WORD(patches[i].address) = patches[i].original;
	appliedPatches = 0;
	flush();
}

static void prepareLoadingScreen(const char* name, const GameSettings* settings)
{
	const char* prefix = "Loading ";
	u32 i, n = 0;
	while (*prefix)
		loadingLabel[n++] = *prefix++;
	for (i = 0; name && name[i] && n + 4 < sizeof(loadingLabel); ++i)
		loadingLabel[n++] = name[i];
	loadingLabel[n++] = '.';
	loadingLabel[n++] = '.';
	loadingLabel[n++] = '.';
	loadingLabel[n] = 0;
	preparationDone = 0;
	preparationTotal = (WORD(0x1ce414) + HOP_READ_SECTORS - 1) / HOP_READ_SECTORS;
	for (i = 0; i < GAME_MAX_PLAYERS; ++i)
		if (settings->PlayerSkins[i] >= 0)
			preparationTotal += 2;
	/* Copy labels before the online WAD overwrites the old message table. */
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
	int quickLoad = mapQuickLoadEnabled();
	const char* name = mapSelection >= 0 ? mapRecords[mapSelection].name : mapRetailName(targetBase);
	prepareLoadingScreen(name, settings);
	patches[0].value = 0x08000000 | (((u32)hopMessage >> 2) & 0x03ffffff);
	appliedPatches = 0;
	for (i = 0; i < sizeof(patches) / sizeof(patches[0]); ++i) {
		patches[i].original = WORD(patches[i].address);
		if (i >= HOP_REQUIRED_PATCHES && (!quickLoad || patches[i].original != 0x0c04e35c))
			continue;
		WORD(patches[i].address) = patches[i].value;
		appliedPatches |= 1 << i;
	}
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
	/* Present feedback before the blocking online-WAD read, as Horizon does.
	 * The game loop cannot draw another frame while fs::load is waiting. */
	presentPreparingMap();
	WORD(0x2209c0) = (u32)wad;
	((void (*)(int))0x1634a8)(1);
	/* Read at most 1 MiB per step so the bar reflects completed I/O rather
	 * than remaining frozen throughout the entire skin archive read. */
	{
		u32 remaining = WORD(0x1ce414);
		u32 sector = WORD(0x1ce410) + WORD(0x1ce40c);
		u8* dest = wad;
		while (remaining) {
			u32 count = remaining > HOP_READ_SECTORS ? HOP_READ_SECTORS : remaining;
			((void (*)(int, void*, int, int, int, void*, void*))0x163808)
			    (0, dest, sector, count, 0, 0, 0);
			sector += count;
			dest += count * 0x800;
			remaining -= count;
			++preparationDone;
			presentPreparingMap();
		}
	}
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
		preparationDone += 2;
		presentPreparingMap();
	}
	presentPreparingMap();
	((void (*)(int, int, int))0x4e2410)(targetBase, 1, -1);
}

void mapHopOnLoad(void)
{
	if (transitioning)
		sawLoading = 1;
}

void mapHopTick(void)
{
	if (!transitioning)
		return;
	if (queued) {
		int startGame = queued == 2;
		const forgeApi_t* forge = mapForgeApi();
		queued = 0;
		if (!forge) {
			mapHopCancel();
			return;
		}
		/* Free Cam must release the old player's input before hop() rebuilds it. */
		if (forge->prepareTransition)
			forge->prepareTransition();
		if (startGame) {
			if (!mapStaging() || mapApi->context != DL_CONTEXT_LOCAL || WORD(0x747f28) != 0x27bdff80) {
				mapHopCancel();
				return;
			}
			mapQuickLoadBegin();
			((void (*)(void*))0x747f28)((void*)WORD(0x11c7108));
		} else {
			hop();
		}
		return;
	}
	if (sawLoading && isInGame() &&
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
