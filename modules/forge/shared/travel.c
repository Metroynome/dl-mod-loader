#include "travel.h"
#include "modules/forge/include/forge.h"
#include "modules/modmenu/include/modmenu.h"

/* Only this translation unit uses the isolated campaign library namespace. */
#define GetAddressImmediate forgeTravel_GetAddressImmediate
#define vaSwitchToLevelFunc forgeTravel_vaSwitchToLevelFunc
#define vaTravelMissionIndex forgeTravel_vaTravelMissionIndex
#define vaCoopGameMode forgeTravel_vaCoopGameMode
#define Mission forgeTravel_Mission
#include <librac4/game.h>
#include <librac4/map.h>

#define WORD(address) (*(volatile u32*)(address))
#define TRAVEL_TIMEOUT (3600)

static const moduleApi_t* loader;
static const modMenuApi_t* menu;
static int tab, jumpItem, reloadItem, selectedPlanet, selectedMission, lastLevel = -1;
static int targetLevel, targetMission, originLevel, travelContext, stage, frames, sawTransition;
static u32 state, error;


/*
 * NAME : forgeTravelContext
 * DESCRIPTION : Identify a loaded campaign or co-op planet without crossing modes.
 * NOTES : Front ends, multiplayer and custom multiplayer maps are excluded.
 * ARGS :
 * RETURN : Forge context, or zero.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
u32 forgeTravelContext(void)
{
	if (!loader || loader->mode != DL_MODE_CAMPAIGN || loader->context != DL_CONTEXT_CAMPAIGN)
		return 0;
	if (forgeSingleplayerTarget(Level, 1))
		return FORGE_CONTEXT_CAMPAIGN;
	if (forgeCooperativeTarget(Level, 1))
		return FORGE_CONTEXT_COOPERATIVE;
	return 0;
}

/*
 * NAME : forgeTravelAvailable
 * DESCRIPTION : Require a stable loaded mission and verified native transition entry.
 * NOTES : Accepts gameplay or native pause, never movies, vendors or loading.
 * ARGS :
 * RETURN : Nonzero when a new travel request can be accepted.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int forgeTravelAvailable(void)
{
	u32 mode, transition;
	if (!forgeTravelContext() || stage || Mission->Loading)
		return 0;
	mode = GetAddressImmediate(&vaCoopGameMode);
	transition = GetAddressImmediate(&vaSwitchToLevelFunc);
	return mode && transition && WORD(transition) == 0x27bdffd0 &&
	    (WORD(mode) == GAME_MODE_NORMAL || WORD(mode) == GAME_MODE_PAUSE);
}

/*
 * NAME : forgeTravelState
 * DESCRIPTION : Read the current travel operation state.
 * NOTES :
 * ARGS :
 * RETURN : Forge state.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
u32 forgeTravelState(void)
{
	return state;
}

/*
 * NAME : forgeTravelError
 * DESCRIPTION : Read the last travel failure.
 * NOTES :
 * ARGS :
 * RETURN : Forge error code.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
u32 forgeTravelError(void)
{
	return error;
}

/*
 * NAME : forgeTravelRequest
 * DESCRIPTION : Queue a native planet/mission transition inside the current mode.
 * NOTES : Does not request an automatic save or unlock progression.
 * ARGS : Unprefixed planet ID and relative mission index.
 * RETURN : Zero when queued, otherwise a Forge error.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
int forgeTravelRequest(int planet, int mission)
{
	u32 context = forgeTravelContext();
	int target;
	if (stage)
		return FORGE_ERROR_BUSY;
	if (!context || !forgeTravelAvailable())
		return FORGE_ERROR_CONTEXT;
	if (forgePlanetIndex(planet) < 0)
		return FORGE_ERROR_PLANET;
	if (mission < 0 || mission >= forgeMissionCount(planet))
		return FORGE_ERROR_MISSION;
	target = context == FORGE_CONTEXT_CAMPAIGN ? forgeSingleplayerTarget(Level, planet) :
	    forgeCooperativeTarget(Level, planet);
	if (!target)
		return FORGE_ERROR_CONTEXT;
	travelContext = context;
	originLevel = Level;
	targetLevel = target;
	targetMission = mission;
	frames = sawTransition = error = 0;
	state = FORGE_LOADING;
	stage = 1;
	if (menu)
		menu->returnToGame();
	return 0;
}

/*
 * NAME : planetLabel
 * DESCRIPTION : Name a planet selector entry.
 * NOTES :
 * ARGS : Menu context and selection.
 * RETURN : Planet label.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static const char* planetLabel(void* unused, int index)
{
	(void)unused;
	return forgePlanetName(index);
}

/*
 * NAME : missionLabel
 * DESCRIPTION : Name a mission on the selected planet.
 * NOTES :
 * ARGS : Menu context and relative index.
 * RETURN : Mission label.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static const char* missionLabel(void* unused, int index)
{
	(void)unused;
	return forgeMissionName(forgePlanetId(selectedPlanet), index);
}

/*
 * NAME : selectedMissionLabel
 * DESCRIPTION : Show the current choice on the non-scrolling mission field.
 * NOTES : X opens the mission catalog; this field has no numeric range.
 * ARGS : Unused menu context and value.
 * RETURN : Selected mission label.
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static const char* selectedMissionLabel(void* unused, int value)
{
	(void)value;
	return missionLabel(unused, selectedMission);
}

/*
 * NAME : planetChanged
 * DESCRIPTION : Reset the mission choice when selecting a different planet.
 * NOTES :
 * ARGS : Unused menu context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void planetChanged(void* unused)
{
	(void)unused;
	selectedMission = 0;
}

/*
 * NAME : missionSelected
 * DESCRIPTION : Return from the native mission catalog to Forge controls.
 * NOTES : Selection alone never starts loading.
 * ARGS : Menu context and relative mission index.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void missionSelected(void* unused, int index)
{
	(void)unused;
	selectedMission = index;
	menu->setList(tab, 0, 0, 0, 0, 0);
}

/*
 * NAME : chooseMission
 * DESCRIPTION : Browse valid missions for the selected planet.
 * NOTES :
 * ARGS : Unused menu context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void chooseMission(void* unused)
{
	(void)unused;
	menu->setList(tab, forgeMissionCount(forgePlanetId(selectedPlanet)), missionLabel,
	    missionSelected, 0, selectedMission);
}

/*
 * NAME : jump
 * DESCRIPTION : Submit the selected planet and mission.
 * NOTES : The transition is deferred until native pause has closed.
 * ARGS : Unused menu context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void jump(void* unused)
{
	(void)unused;
	forgeTravelRequest(forgePlanetId(selectedPlanet), selectedMission);
}

/*
 * NAME : reload
 * DESCRIPTION : Request the current planet and native relative mission again.
 * NOTES :
 * ARGS : Unused menu context.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
static void reload(void* unused)
{
	u32 index = GetAddressImmediate(&vaTravelMissionIndex);
	int planet = Level > 20 ? Level - 20 : Level;
	(void)unused;
	if (index)
		forgeTravelRequest(planet, WORD(index));
}

/*
 * NAME : forgeTravelInit
 * DESCRIPTION : Store the loader for shared campaign/co-op travel.
 * NOTES :
 * ARGS : Loader API.
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void forgeTravelInit(const moduleApi_t* api)
{
	loader = api;
}

/*
 * NAME : forgeTravelDisable
 * DESCRIPTION : Release the travel menu and fail any unfinished operation.
 * NOTES : No native hooks are installed by travel.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void forgeTravelDisable(void)
{
	if (menu && tab > 0)
		menu->removeTab(tab);
	tab = 0;
	menu = 0;
	lastLevel = -1;
	if (stage) {
		state = FORGE_FAILED;
		error = FORGE_ERROR_CONTEXT;
		stage = 0;
	}
}

/*
 * NAME : forgeTravelUpdate
 * DESCRIPTION : Complete queued travel and expose mode-specific planet controls.
 * NOTES : Completion requires observing the transition and the requested mission.
 * ARGS :
 * RETURN :
 * AUTHOR : Troy "Metroynome" Pruitt
 */
void forgeTravelUpdate(void)
{
	u32 context = forgeTravelContext(), mode, transition, mission;
	static const modMenuItem_t planet = {"Planet", MODMENU_RANGE, &selectedPlanet, 0, 11, 1, planetChanged, 0};
	static const modMenuItem_t choice = {"Mission", MODMENU_FIELD, 0, 0, 0, 0, chooseMission, 0};
	static const modMenuItem_t action = {"Jump to Planet / Mission", MODMENU_ACTION, 0, 0, 0, 0, jump, 0};
	static const modMenuItem_t reset = {"Reload Current Mission", MODMENU_ACTION, 0, 0, 0, 0, reload, 0};
	if (!context) {
		forgeTravelDisable();
		return;
	}
	mode = GetAddressImmediate(&vaCoopGameMode);
	mission = GetAddressImmediate(&vaTravelMissionIndex);
	if (stage) {
		if (context != (u32)travelContext || ++frames > TRAVEL_TIMEOUT) {
			stage = 0;
			state = FORGE_FAILED;
			error = FORGE_ERROR_LOAD;
		} else if (stage == 1 && mode && WORD(mode) == GAME_MODE_NORMAL) {
			transition = GetAddressImmediate(&vaSwitchToLevelFunc);
			if (!transition || WORD(transition) != 0x27bdffd0) {
				stage = 0;
				state = FORGE_FAILED;
				error = FORGE_ERROR_CONTEXT;
			} else {
				stage = 2;
				((void (*)(int, int, int))transition)(targetLevel, 0, targetMission);
			}
		} else if (stage == 2) {
			if (Mission->Loading || Level != originLevel)
				sawTransition = 1;
			if (sawTransition && Level == targetLevel && !Mission->Loading && mission &&
			    (int)WORD(mission) == targetMission && mode && WORD(mode) == GAME_MODE_NORMAL) {
				state = FORGE_COMPLETE;
				stage = 0;
			}
		}
	}
	if (!menu)
		menu = modmenuFind(loader);
	if (!menu)
		return;
	if (lastLevel != Level && !Mission->Loading) {
		selectedPlanet = forgePlanetIndex(Level > 20 ? Level - 20 : Level);
		selectedMission = mission ? (int)WORD(mission) : 0;
		if (selectedMission < 0 || selectedMission >= forgeMissionCount(forgePlanetId(selectedPlanet)))
			selectedMission = 0;
		lastLevel = Level;
	}
	if (!tab) {
		tab = menu->addTab("forge-travel", "Forge", DL_MODE_CAMPAIGN);
		if (tab <= 0) {
			tab = 0;
			return;
		}
		menu->setTabVisibility(tab, MODMENU_SHOW_CAMPAIGN);
		menu->setTabContexts(tab, DL_CONTEXT_CAMPAIGN);
		menu->setItemValueLabel(menu->addItem(tab, &planet), planetLabel);
		menu->setItemValueLabel(menu->addItem(tab, &choice), selectedMissionLabel);
		jumpItem = menu->addItem(tab, &action);
		reloadItem = menu->addItem(tab, &reset);
	}
	menu->setItemEnabled(jumpItem, forgeTravelAvailable());
	menu->setItemEnabled(reloadItem, forgeTravelAvailable());
}
