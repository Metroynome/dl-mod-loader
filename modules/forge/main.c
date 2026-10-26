/* Forge boot and Local Play controls. Map Loader owns map transitions. */
#include "modules/forge/include/forge.h"
#include "modules/modmenu/include/modmenu.h"
#include <libdl/game.h>
#include <libdl/gamesettings.h>
#include <libdl/pad.h>
#include <libdl/ui.h>

typedef enum forgeBootStage {
	FORGE_BOOT_INITIAL,
	FORGE_BOOT_MULTIPLAYER,
	FORGE_BOOT_CREATE,
	FORGE_BOOT_STAGING,
	FORGE_BOOT_DONE
} forgeBootStage_e;

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
static int bootToLocalPlay;
static int bootRequested;
static forgeState_e bootResult;
static forgeBootStage_e bootStage;
static const moduleConfigField_t settings[] = {
	{"quickLoadMaps", &quickLoadMaps, 0, 1},
	{"gameMode", &selectedGameMode, FORGE_MODE_CQ, FORGE_MODE_JUGGY},
	{"chargeboots", &chargeboots, 0, 1},
	{"infiniteAmmo", &infiniteAmmo, 0, 1},
	{"bootToLocalPlay", &bootToLocalPlay, 0, 1}
};
static const forgeApi_t service = {FORGE_API_VERSION, sizeof(forgeApi_t), active, &quickLoadMaps, &selectedGameMode};

static int active(void)
{
	return enabled && loader->context == DL_CONTEXT_LOCAL;
}

/* Automatic boot runs once. An explicit mailbox request can also use this
 * flow without changing the saved Boot to Local Play preference. */
static void bootLocalPlay(void)
{
	UiMenu_t* current;
	if (bootStage == FORGE_BOOT_DONE)
		return;
	if (!bootToLocalPlay && !bootRequested) {
		bootStage = FORGE_BOOT_DONE;
		return;
	}
	if (loader->mode == DL_MODE_CAMPAIGN) {
		if (bootStage == FORGE_BOOT_INITIAL && *(volatile int*)0x21de10 == -1) {
			*(volatile u8*)0x21de69 = 1; /* Boot into the multiplayer UI. */
			*(volatile u32*)0x21de80 = 0; /* eNWLobbyInitToMain, never Online. */
			*(volatile u32*)0x21edbc = 1; /* Skip the intro. */
			bootStage = FORGE_BOOT_MULTIPLAYER;
		}
		return;
	}
	if (!active() || *(volatile int*)0x21de10 != 0 || *(volatile u32*)0x22026c != 1)
		return;
	current = uiGetActivePointer();
	if (!current)
		return;
	if (bootStage == FORGE_BOOT_MULTIPLAYER && uiGetActive() == UI_ID_MULTIPLAYER_MENU) {
		*(volatile u8*)0x21de69 = 0; /* Do not redirect later returns to the boot menu. */
		/* Native Local Play action selects this Create Game screen. */
		uiChangeMenu(UI_MENU_ID_CREATE_GAME1);
		bootStage = FORGE_BOOT_CREATE;
	} else if (bootStage == FORGE_BOOT_CREATE && current == uiGetPointer(UI_MENU_ID_CREATE_GAME1)) {
		/* Native Create Game submission initializes the local roster, rules,
		 * map and network state before entering staging. No fake pad input. */
		if (*(volatile u32*)0x743da0 != 0x27bdffb0) {
			bootStage = FORGE_BOOT_DONE;
			bootResult = FORGE_FAILED;
			return;
		}
		((void (*)(UiMenu_t*))0x743da0)(current);
		uiChangeMenu(UI_MENU_ID_LOCAL_PLAY_STAGING);
		bootStage = FORGE_BOOT_STAGING;
	} else if (bootStage == FORGE_BOOT_STAGING && current == uiGetPointer(UI_MENU_ID_LOCAL_PLAY_STAGING)) {
		bootStage = FORGE_BOOT_DONE;
		bootResult = FORGE_COMPLETE;
	}
}

int forgeBootAvailable(void)
{
	int level = *(volatile int*)0x21de10;
	return enabled && loader->mode == DL_MODE_CAMPAIGN &&
	       (level == -1 || (level == 1 && *(volatile int*)0x1711a8 == -1)) &&
	       *(volatile u8*)0x306dc0 && /* Boot menu is active, not an intro/preview movie. */
	       *(volatile int*)0x306dc4 == 32 && /* GuiMenu_PlayerData[0].iScreen: BootMain. */
	       *(volatile u32*)0x4a20c8 == 0x27bdfff0 &&
	       *(volatile u32*)0x4a20d8 == 0x0c126c68 &&
	       (bootStage == FORGE_BOOT_INITIAL || bootStage == FORGE_BOOT_DONE);
}

u32 forgeBootState(void)
{
	return bootResult;
}

int forgeBootRequest(void)
{
	const modMenuApi_t* currentMenu;
	if (!forgeBootAvailable())
		return FORGE_ERROR_CONTEXT;
	currentMenu = modmenuFind(loader);
	if (currentMenu)
		currentMenu->close();
	bootRequested = 1;
	bootResult = FORGE_LOADING;
	bootStage = FORGE_BOOT_MULTIPLAYER;
	/* BootMain_CMDMultiCB queues the native Multiplayer screen. Startup
	 * flags alone no longer take effect once BootMain is already open. */
	((void (*)(void*))0x4a20c8)(0);
	return 0;
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
	if (bootResult == FORGE_LOADING && *(volatile u32*)0x1af91c) {
		bootResult = FORGE_FAILED;
		bootStage = FORGE_BOOT_DONE;
	}
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
	api->restrictContexts(DL_CONTEXT_CAMPAIGN | DL_CONTEXT_LOCAL);
	api->setLifecycle(activate, deactivate);
	enabled = 1;
	reloadHeld = 1;
	bootLocalPlay();
	return api->publishService(FORGE_SERVICE_ID, FORGE_API_VERSION, &service);
}

void moduleUpdate(void)
{
	bootLocalPlay();
	if (loader->mode != DL_MODE_MULTIPLAYER) {
		forgeBridgeTick(0);
		return; /* No libdl menu/player calls in campaign or the boot overlay. */
	}
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
	static const modMenuItem_t boot = {"Boot to Local Play", MODMENU_TOGGLE, &bootToLocalPlay, 0, 1, 1, 0, 0};
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
		menu->addItem(tab, &boot);
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
}
