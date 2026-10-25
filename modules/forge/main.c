/* Development-only Local Play controls. Map Loader owns all transition work. */
#include "modules/forge/include/forge.h"
#include "modules/modmenu/include/modmenu.h"
#include <libdl/game.h>
#include <libdl/gamesettings.h>
#include <libdl/pad.h>

void forgeBridgeInit(void);
void forgeBridgeDisable(void);
void forgeBridgeTick(const mapHopApi_t*);
static int active(void);
static void reloadShortcut(const mapHopApi_t* hopApi);
static void gameModeChanged(void* unused);

static const moduleApi_t* loader;
static const modMenuApi_t* menu;
static int enabled, tab, item, refreshItem, lastReady = -1, lastRefresh = -1;
static int quickLoadMaps, reloadHeld = 1;
static int selectedGameMode, lastGameContext = -1;
static int chargeboots = 1, infiniteAmmo = 1;
static const moduleConfigField_t settings[] = {
	{"quickLoadMaps", &quickLoadMaps, 0, 1},
	{"gameMode", &selectedGameMode, FORGE_MODE_CQ, FORGE_MODE_JUGGY},
	{"chargeboots", &chargeboots, 0, 1},
	{"infiniteAmmo", &infiniteAmmo, 0, 1}
};
static const forgeApi_t service = {FORGE_API_VERSION, sizeof(forgeApi_t), active, &quickLoadMaps, &selectedGameMode};

static int active(void)
{
	return enabled && loader->context == DL_CONTEXT_LOCAL;
}

/* Forge owns these two rules while active, including in staging. Keep the
 * resident setup synchronized because Start Game copies it into active options. */
static void applyGameOptions(void* unused)
{
	GameOptions* options = gameGetOptions();
	GameOptions* setup = (GameOptions*)0x1737e8;
	(void)unused;
	if (!active() || !gameGetSettings() || !options)
		return;
	options->GameFlags.MultiplayerGameFlags.SpawnWithChargeboots = chargeboots;
	options->GameFlags.MultiplayerGameFlags.UnlimitedAmmo = infiniteAmmo;
	setup->GameFlags.MultiplayerGameFlags.SpawnWithChargeboots = chargeboots;
	setup->GameFlags.MultiplayerGameFlags.UnlimitedAmmo = infiniteAmmo;
}

static const mapHopApi_t* hop(void)
{
	const mapHopApi_t* hopApi = loader->findService(MAP_HOP_SERVICE_ID, FORGE_API_VERSION);
	return hopApi && hopApi->version == FORGE_API_VERSION && hopApi->size >= sizeof(*hopApi) ? hopApi : 0;
}

static void gameModeChanged(void* unused)
{
	const mapHopApi_t* hopApi = hop();
	(void)unused;
	if (hopApi && hopApi->context() == FORGE_CONTEXT_STAGING)
		hopApi->setStagingGameMode(selectedGameMode);
}

static void reload(void* unused)
{
	const mapHopApi_t* hopApi = hop();
	(void)unused;
	if (hopApi && !hopApi->request(FORGE_RELOAD, 0, selectedGameMode) && menu)
		menu->close();
}

static const char* gameModeLabel(void* context, int index)
{
	const char* name;
	(void)context;
	name = gameGetGameModeName(index);
	return name ? name : "Unavailable";
}

/* Match Horizon's normal reload combo. Require a fresh press after menus or
 * loading so holding the buttons cannot repeatedly reload the level. */
static void reloadShortcut(const mapHopApi_t* hopApi)
{
	int held;
	if (!active() || !hopApi || hopApi->context() != FORGE_CONTEXT_GAME ||
	    (menu && menu->isOpen()) || gameIsAnyStartMenuOpen()) {
		reloadHeld = 1;
		return;
	}
	held = padGetButton(0, PAD_L1 | PAD_UP | PAD_CIRCLE) > 0;
	if (held && !reloadHeld && hopApi->available())
		reload(0);
	reloadHeld = held;
}

static void refresh(void* unused)
{
	const mapHopApi_t* hopApi = hop();
	(void)unused;
	if (hopApi && !hopApi->request(FORGE_REFRESH, 0, FORGE_MODE_CQ) && menu)
		menu->close();
}

static void configAction(void* saving)
{
	int count = sizeof(settings) / sizeof(settings[0]);
	int result = saving ? loader->saveConfig("forge", settings, count) : loader->loadConfig("forge", settings, count);
	applyGameOptions(0);
	if (!saving && result == DL_CONFIG_OK)
		gameModeChanged(0);
	loader->print("forge: %s settings result=%d\n", saving ? "save" : "load", result);
}

static void activate(void)
{
	forgeBridgeDisable();
	enabled = 1;
	reloadHeld = 1;
	lastGameContext = -1;
}

static void deactivate(void)
{
	enabled = 0;
	reloadHeld = 1;
	forgeBridgeDisable();
	if (menu && tab > 0)
		menu->removeTab(tab);
	menu = 0;
	tab = 0;
	lastReady = -1;
}

int moduleInit(const moduleApi_t* api)
{
	loader = api;
	forgeBridgeInit();
	if (api->version < DL_API_VERSION || api->size < sizeof(*api))
		return -1;
	api->loadConfig("forge", settings, sizeof(settings) / sizeof(settings[0]));
	api->restrictContexts(DL_CONTEXT_LOCAL);
	api->setLifecycle(activate, deactivate);
	enabled = 1;
	reloadHeld = 1;
	return api->publishService(FORGE_SERVICE_ID, FORGE_API_VERSION, &service);
}

void moduleUpdate(void)
{
	const mapHopApi_t* hopApi = hop();
	int ready = hopApi && hopApi->available();
	static const modMenuItem_t action = {"Reload Current Map", MODMENU_ACTION, 0, 0, 0, 0, reload, 0};
	static const modMenuItem_t refreshAction = {"Refresh Custom Maps", MODMENU_ACTION, 0, 0, 0, 0, refresh, 0};
	static const modMenuItem_t quickLoad = {"Quick Load Maps", MODMENU_TOGGLE, &quickLoadMaps, 0, 1, 1, 0, 0};
	static const modMenuItem_t mode = {
		"Game Mode", MODMENU_RANGE, &selectedGameMode, FORGE_MODE_CQ, FORGE_MODE_JUGGY, 1, gameModeChanged, 0
	};
	static const modMenuItem_t boots = {"Chargeboots", MODMENU_TOGGLE, &chargeboots, 0, 1, 1, applyGameOptions, 0};
	static const modMenuItem_t ammo = {"Infinite Ammo", MODMENU_TOGGLE, &infiniteAmmo, 0, 1, 1, applyGameOptions, 0};
	static const modMenuItem_t save = {"Save Settings", MODMENU_ACTION, 0, 0, 0, 0, configAction, (void*)1};
	static const modMenuItem_t load = {"Load Settings", MODMENU_ACTION, 0, 0, 0, 0, configAction, 0};
	int canRefresh = hopApi && hopApi->context() && hopApi->state() != FORGE_SCANNING && hopApi->state() != FORGE_LOADING;
	int context = hopApi ? hopApi->context() : FORGE_CONTEXT_UNAVAILABLE;
	GameSettings* current = gameGetSettings();
	/* Each new staging/game context starts with its actual mode. A selection
	 * made during gameplay remains pending until its next transition. */
	if (context && (context == FORGE_CONTEXT_STAGING || context != lastGameContext) && current &&
	    current->GameRules >= FORGE_MODE_CQ && current->GameRules <= FORGE_MODE_JUGGY)
		selectedGameMode = current->GameRules;
	lastGameContext = context;
	applyGameOptions(0);
	forgeBridgeTick(hopApi);
	if (menu && hopApi && (hopApi->state() == FORGE_SCANNING || hopApi->state() == FORGE_LOADING))
		menu->close();
	if (!menu)
		menu = modmenuFind(loader);
	reloadShortcut(hopApi);
	if (!menu)
		return;
	if (tab <= 0) {
		tab = menu->addTab("forge", "Forge", DL_MODE_MULTIPLAYER);
		if (tab <= 0)
			return;
		menu->setTabContexts(tab, DL_CONTEXT_LOCAL);
		item = menu->addItem(tab, &action);
		refreshItem = menu->addItem(tab, &refreshAction);
		menu->addItem(tab, &quickLoad);
		menu->setItemValueLabel(menu->addItem(tab, &mode), gameModeLabel);
		menu->addItem(tab, &boots);
		menu->addItem(tab, &ammo);
		menu->addItem(tab, &save);
		menu->addItem(tab, &load);
		lastReady = lastRefresh = -1;
	}
	if (lastRefresh != canRefresh) {
		menu->setItemEnabled(refreshItem, canRefresh);
		lastRefresh = canRefresh;
	}
	if (lastReady != ready) {
		menu->setItemEnabled(item, ready);
		lastReady = ready;
	}
}
